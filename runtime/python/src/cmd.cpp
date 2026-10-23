/*
  cmd.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/cmd.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module cmd (CPython 3.13's Lib/cmd.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "cmd_docs.hpp"

namespace neos::python_runtime::detail::cmd_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "cmd.name"))
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
                py_raise("SystemError", std::string{ "cmd: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"cmd.name">()))
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
            static module_writer const sWriter{ "cmd", cmd_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- cmd (Lib/cmd.py)

        void make_cmd()
        {
            static module_writer const& w = writer("cmd");
            define("cmd", "__all__", py_list({ S<"Cmd">() }));
            define("cmd", "PROMPT", S<"(Cmd) ">());
            define("cmd", "IDENTCHARS", ((Mod<"string">().attr("ascii_letters")) + (Mod<"string">().attr("digits"))) + (S<"_">()));
            define("cmd", "Cmd", w.make_class("Cmd", {  }, {
                { "prompt", G<"cmd.PROMPT">() },
                { "identchars", G<"cmd.IDENTCHARS">() },
                { "ruler", S<"=">() },
                { "lastcmd", S<"">() },
                { "intro", none() },
                { "doc_leader", S<"">() },
                { "doc_header", S<"Documented commands (type help <topic>):">() },
                { "misc_header", S<"Miscellaneous help topics:">() },
                { "undoc_header", S<"Undocumented commands:">() },
                { "nohelp", S<"*** No help on %s">() },
                { "use_rawinput", I<1>() },
                { "__init__", w.method("Cmd", "__init__", { py_positional("completekey", S<"tab">()), py_positional("stdin", none()), py_positional("stdout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py completekey = a[1];
                        py stdin_ = a[2];
                        py stdout_ = a[3];
                        if (!stdin_.is_none())
                        {
                            self.set_attr("stdin", stdin_);
                        }
                        else
                        {
                            self.set_attr("stdin", Mod<"sys">().attr("stdin"));
                        }
                        if (!stdout_.is_none())
                        {
                            self.set_attr("stdout", stdout_);
                        }
                        else
                        {
                            self.set_attr("stdout", Mod<"sys">().attr("stdout"));
                        }
                        self.set_attr("cmdqueue", py_list());
                        self.set_attr("completekey", completekey);
                        return none();
                    }) },
                { "cmdloop", w.method("Cmd", "cmdloop", { py_positional("intro", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py intro = a[1];
                        py command_string;
                        py line;
                        py readline;
                        py stop;
                        self.method("preloop");
                        if ((self.attr("use_rawinput").truthy() && self.attr("completekey").truthy()))
                        {
                            try
                            {
                                readline = module_of("readline");
                                self.set_attr("old_completer", readline.method("get_completer"));
                                readline.method("set_completer", { self.attr("complete") });
                                if (readline.attr("backend") == (S<"editline">()))
                                {
                                    if (self.attr("completekey") == (S<"tab">()))
                                    {
                                        // libedit uses "^I" instead of "tab"
                                        command_string = S<"bind ^I rl_complete">();
                                    }
                                    else
                                    {
                                        command_string = S(std::string{ "bind " } + self.attr("completekey").str() + " rl_complete");
                                    }
                                }
                                else
                                {
                                    command_string = S(self.attr("completekey").str() + ": complete");
                                }
                                readline.method("parse_and_bind", { command_string });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                if (matches(handler1.exception, B<"ImportError">()))
                                {
                                }
                                else
                                    py_reraise(handler1.exception);
                            }
                        }
                        {
                            std::optional<py> pending2;
                            [[maybe_unused]] int flow2 = 0;
                            [[maybe_unused]] py result2;
                            try
                            {
                                if (!intro.is_none())
                                {
                                    self.set_attr("intro", intro);
                                }
                                if (self.attr("intro").truthy())
                                {
                                    self.attr("stdout").method("write", { (B<"str">())(self.attr("intro")) + (S<"\n">()) });
                                }
                                stop = none();
                                while (!stop.truthy())
                                {
                                    if (self.attr("cmdqueue").truthy())
                                    {
                                        line = self.attr("cmdqueue").method("pop", { I<0>() });
                                    }
                                    else if (self.attr("use_rawinput").truthy())
                                    {
                                        try
                                        {
                                            line = (B<"input">())(self.attr("prompt"));
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler4;
                                            if (matches(handler4.exception, B<"EOFError">()))
                                            {
                                                line = S<"EOF">();
                                            }
                                            else
                                                py_reraise(handler4.exception);
                                        }
                                    }
                                    else
                                    {
                                        self.attr("stdout").method("write", { self.attr("prompt") });
                                        self.attr("stdout").method("flush");
                                        line = self.attr("stdin").method("readline");
                                        if (!py{ line.size() }.truthy())
                                        {
                                            line = S<"EOF">();
                                        }
                                        else
                                        {
                                            line = line.method("rstrip", { S<"\r\n">() });
                                        }
                                    }
                                    line = self.method("precmd", { line });
                                    stop = self.method("onecmd", { line });
                                    stop = self.method("postcmd", { stop, line });
                                }
                                self.method("postloop");
                            }
                            catch (raised const&)
                            {
                                pending2 = py_take_exception();
                            }
                            finally2:
                            if ((self.attr("use_rawinput").truthy() && self.attr("completekey").truthy()))
                            {
                                try
                                {
                                    readline = module_of("readline");
                                    readline.method("set_completer", { self.attr("old_completer") });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler5;
                                    if (matches(handler5.exception, B<"ImportError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler5.exception);
                                }
                            }
                            if (pending2)
                                py_reraise(*pending2);
                            if (flow2 == 1)
                                return result2;
                        }
                        return none();
                    }) },
                { "precmd", w.method("Cmd", "precmd", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        return line;
                    }) },
                { "postcmd", w.method("Cmd", "postcmd", { py_positional("stop"), py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stop = a[1];
                        py line = a[2];
                        return stop;
                    }) },
                { "preloop", w.method("Cmd", "preloop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "postloop", w.method("Cmd", "postloop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "parseline", w.method("Cmd", "parseline", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        py arg;
                        py cmd;
                        py i;
                        py n;
                        line = line.method("strip");
                        if (!line.truthy())
                        {
                            return py_tuple({ none(), none(), line });
                        }
                        else if (line[I<0>()] == (S<"?">()))
                        {
                            line = (S<"help ">()) + line[B<"slice">()(I<1>(), none(), none())];
                        }
                        else if (line[I<0>()] == (S<"!">()))
                        {
                            if (has_attr(self, "do_shell"))
                            {
                                line = (S<"shell ">()) + line[B<"slice">()(I<1>(), none(), none())];
                            }
                            else
                            {
                                return py_tuple({ none(), none(), line });
                            }
                        }
                        auto const items6 = unpack(py_tuple({ I<0>(), py{ line.size() } }), 2);
                        i = items6[0];
                        n = items6[1];
                        while (((i < n) && self.attr("identchars").contains(line[i])))
                        {
                            i = i + (I<1>());
                        }
                        auto const items8 = unpack(py_tuple({ line[B<"slice">()(none(), i, none())], line[B<"slice">()(i, none(), none())].method("strip") }), 2);
                        cmd = items8[0];
                        arg = items8[1];
                        return py_tuple({ cmd, arg, line });
                    }) },
                { "onecmd", w.method("Cmd", "onecmd", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        py arg;
                        py cmd;
                        py func;
                        auto const items9 = unpack(self.method("parseline", { line }), 3);
                        cmd = items9[0];
                        arg = items9[1];
                        line = items9[2];
                        if (!line.truthy())
                        {
                            return self.method("emptyline");
                        }
                        if (cmd.is_none())
                        {
                            return self.method("default", { line });
                        }
                        self.set_attr("lastcmd", line);
                        if (line == (S<"EOF">()))
                        {
                            self.set_attr("lastcmd", S<"">());
                        }
                        if (cmd == (S<"">()))
                        {
                            return self.method("default", { line });
                        }
                        else
                        {
                            func = (B<"getattr">())(self, (S<"do_">()) + cmd, none());
                            if (func.is_none())
                            {
                                return self.method("default", { line });
                            }
                            return func(arg);
                        }
                        return none();
                    }) },
                { "emptyline", w.method("Cmd", "emptyline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("lastcmd").truthy())
                        {
                            return self.method("onecmd", { self.attr("lastcmd") });
                        }
                        return none();
                    }) },
                { "default", w.method("Cmd", "default", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        self.attr("stdout").method("write", { (S<"*** Unknown syntax: %s\n">()) % line });
                        return none();
                    }) },
                { "completedefault", w.method("Cmd", "completedefault", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ignored = a[1];
                        return py_list();
                    }, true) },
                { "completenames", w.method("Cmd", "completenames", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py ignored = a[2];
                        py dotext;
                        dotext = (S<"do_">()) + text;
                        return [&]() -> py { py const comprehension10 = py_list(); py a_; for (py const& item : iterate{ self.method("get_names") }) { a_ = item; if (!a_.method("startswith", { dotext }).truthy()) continue; comprehension10.method("append", { a_[B<"slice">()(I<3>(), none(), none())] }); } return comprehension10; }();
                    }, true) },
                { "complete", w.method("Cmd", "complete", { py_positional("text"), py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py state = a[2];
                        py args;
                        py begidx;
                        py cmd;
                        py compfunc;
                        py endidx;
                        py foo;
                        py line;
                        py origline;
                        py readline;
                        py stripped;
                        if (state == (I<0>()))
                        {
                            readline = module_of("readline");
                            origline = readline.method("get_line_buffer");
                            line = origline.method("lstrip");
                            stripped = py{ origline.size() } - py{ line.size() };
                            begidx = readline.method("get_begidx") - stripped;
                            endidx = readline.method("get_endidx") - stripped;
                            if (begidx > (I<0>()))
                            {
                                auto const items11 = unpack(self.method("parseline", { line }), 3);
                                cmd = items11[0];
                                args = items11[1];
                                foo = items11[2];
                                if (cmd == (S<"">()))
                                {
                                    compfunc = self.attr("completedefault");
                                }
                                else
                                {
                                    try
                                    {
                                        compfunc = (B<"getattr">())(self, (S<"complete_">()) + cmd);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler12;
                                        if (matches(handler12.exception, B<"AttributeError">()))
                                        {
                                            compfunc = self.attr("completedefault");
                                        }
                                        else
                                            py_reraise(handler12.exception);
                                    }
                                }
                            }
                            else
                            {
                                compfunc = self.attr("completenames");
                            }
                            self.set_attr("completion_matches", compfunc(text, line, begidx, endidx));
                        }
                        try
                        {
                            return self.attr("completion_matches")[state];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler13;
                            if (matches(handler13.exception, B<"IndexError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler13.exception);
                        }
                        return none();
                    }) },
                { "get_names", w.method("Cmd", "get_names", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"dir">())(self.attr("__class__"));
                    }) },
                { "complete_help", w.method("Cmd", "complete_help", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py commands;
                        py topics;
                        commands = (B<"set">())(call_ex(self.attr("completenames"), {  }, args, py{}));
                        topics = (B<"set">())([&]() -> py { py const comprehension14 = py_list(); py a_; for (py const& item : iterate{ self.method("get_names") }) { a_ = item; if (!a_.method("startswith", { (S<"help_">()) + args[I<0>()] }).truthy()) continue; comprehension14.method("append", { a_[B<"slice">()(I<5>(), none(), none())] }); } return comprehension14; }());
                        return (B<"list">())(commands | topics);
                    }, true) },
                { "do_help", w.method("Cmd", "do_help", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        py cmd;
                        py cmds_doc;
                        py cmds_undoc;
                        py doc;
                        py func;
                        py name;
                        py names;
                        py prevname;
                        py topics;
                        if (arg.truthy())
                        {
                            // XXX check arg syntax
                            try
                            {
                                func = (B<"getattr">())(self, (S<"help_">()) + arg);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler15;
                                if (matches(handler15.exception, B<"AttributeError">()))
                                {
                                    try
                                    {
                                        doc = (B<"getattr">())(self, (S<"do_">()) + arg).attr("__doc__");
                                        doc = (Mod<"inspect">().attr("cleandoc"))(doc);
                                        if (doc.truthy())
                                        {
                                            self.attr("stdout").method("write", { (S<"%s\n">()) % (B<"str">())(doc) });
                                            return none();
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler16;
                                        if (matches(handler16.exception, B<"AttributeError">()))
                                        {
                                        }
                                        else
                                            py_reraise(handler16.exception);
                                    }
                                    self.attr("stdout").method("write", { (S<"%s\n">()) % (B<"str">())(self.attr("nohelp") % py_tuple({ arg })) });
                                    return none();
                                }
                                else
                                    py_reraise(handler15.exception);
                            }
                            func();
                        }
                        else
                        {
                            names = self.method("get_names");
                            cmds_doc = py_list();
                            cmds_undoc = py_list();
                            topics = (B<"set">())();
                            for (py const& item : iterate{ names })
                            {
                                name = item;
                                if (name[B<"slice">()(none(), I<5>(), none())] == (S<"help_">()))
                                {
                                    topics.method("add", { name[B<"slice">()(I<5>(), none(), none())] });
                                }
                            }
                            names.method("sort");
                            // There can be duplicates if routines overridden
                            prevname = S<"">();
                            for (py const& item : iterate{ names })
                            {
                                name = item;
                                if (name[B<"slice">()(none(), I<3>(), none())] == (S<"do_">()))
                                {
                                    if (name == prevname)
                                    {
                                        continue;
                                    }
                                    prevname = name;
                                    cmd = name[B<"slice">()(I<3>(), none(), none())];
                                    if (topics.contains(cmd))
                                    {
                                        cmds_doc.method("append", { cmd });
                                        topics.method("remove", { cmd });
                                    }
                                    else if ((B<"getattr">())(self, name).attr("__doc__").truthy())
                                    {
                                        cmds_doc.method("append", { cmd });
                                    }
                                    else
                                    {
                                        cmds_undoc.method("append", { cmd });
                                    }
                                }
                            }
                            self.attr("stdout").method("write", { (S<"%s\n">()) % (B<"str">())(self.attr("doc_leader")) });
                            self.method("print_topics", { self.attr("doc_header"), cmds_doc, I<15>(), I<80>() });
                            self.method("print_topics", { self.attr("misc_header"), (B<"sorted">())(topics), I<15>(), I<80>() });
                            self.method("print_topics", { self.attr("undoc_header"), cmds_undoc, I<15>(), I<80>() });
                        }
                        return none();
                    }) },
                { "print_topics", w.method("Cmd", "print_topics", { py_positional("header"), py_positional("cmds"), py_positional("cmdlen"), py_positional("maxcol") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header = a[1];
                        py cmds = a[2];
                        py cmdlen = a[3];
                        py maxcol = a[4];
                        if (cmds.truthy())
                        {
                            self.attr("stdout").method("write", { (S<"%s\n">()) % (B<"str">())(header) });
                            if (self.attr("ruler").truthy())
                            {
                                self.attr("stdout").method("write", { (S<"%s\n">()) % (B<"str">())(self.attr("ruler") * py{ header.size() }) });
                            }
                            self.method("columnize", { cmds, maxcol - (I<1>()) });
                            self.attr("stdout").method("write", { S<"\n">() });
                        }
                        return none();
                    }) },
                { "columnize", w.method("Cmd", "columnize", { py_positional("list"), py_positional("displaywidth", I<80>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py list = a[1];
                        py displaywidth = a[2];
                        py col;
                        py colwidth;
                        py colwidths;
                        py i;
                        py ncols;
                        py nonstrings;
                        py nrows;
                        py row;
                        py size;
                        py texts;
                        py totwidth;
                        py x;
                        if (!list.truthy())
                        {
                            self.attr("stdout").method("write", { S<"<empty>\n">() });
                            return none();
                        }
                        nonstrings = [&]() -> py { py const comprehension19 = py_list(); py i; for (py const& item : iterate{ (B<"range">())(py{ list.size() }) }) { i = item; if (isinstance(list[i], B<"str">())) continue; comprehension19.method("append", { i }); } return comprehension19; }();
                        if (nonstrings.truthy())
                        {
                            py_raise((B<"TypeError">())((S<"list[i] not a string for i in %s">()) % (S<", ">()).method("join", { (B<"map">())(B<"str">(), nonstrings) })));
                        }
                        size = py{ list.size() };
                        if (size == (I<1>()))
                        {
                            self.attr("stdout").method("write", { (S<"%s\n">()) % (B<"str">())(list[I<0>()]) });
                            return none();
                        }
                        // Try every row count from 1 upwards
                        bool broke20 = false;
                        for (py const& item : iterate{ (B<"range">())(I<1>(), py{ list.size() }) })
                        {
                            nrows = item;
                            ncols = floordiv(((size + nrows) - (I<1>())), nrows);
                            colwidths = py_list();
                            totwidth = I<-2>();
                            for (py const& item : iterate{ (B<"range">())(ncols) })
                            {
                                col = item;
                                colwidth = I<0>();
                                for (py const& item : iterate{ (B<"range">())(nrows) })
                                {
                                    row = item;
                                    i = row + (nrows * col);
                                    if (i >= size)
                                    {
                                        break;
                                    }
                                    x = list[i];
                                    colwidth = (B<"max">())(colwidth, py{ x.size() });
                                }
                                colwidths.method("append", { colwidth });
                                totwidth = inplace(NEOS_PY_ADD, totwidth, colwidth + (I<2>()));
                                if (totwidth > displaywidth)
                                {
                                    break;
                                }
                            }
                            if (totwidth <= displaywidth)
                            {
                                broke20 = true;
                                break;
                            }
                        }
                        if (!broke20)
                        {
                            nrows = py{ list.size() };
                            ncols = I<1>();
                            colwidths = py_list({ I<0>() });
                        }
                        for (py const& item : iterate{ (B<"range">())(nrows) })
                        {
                            row = item;
                            texts = py_list();
                            for (py const& item : iterate{ (B<"range">())(ncols) })
                            {
                                col = item;
                                i = row + (nrows * col);
                                if (i >= size)
                                {
                                    x = S<"">();
                                }
                                else
                                {
                                    x = list[i];
                                }
                                texts.method("append", { x });
                            }
                            while ((texts.truthy() && (!texts[I<-1>()].truthy())))
                            {
                                del_item(texts, I<-1>());
                            }
                            for (py const& item : iterate{ (B<"range">())(py{ texts.size() }) })
                            {
                                col = item;
                                texts.set_item(col, texts[col].method("ljust", { colwidths[col] }));
                            }
                            self.attr("stdout").method("write", { (S<"%s\n">()) % (B<"str">())((S<"  ">()).method("join", { texts })) });
                        }
                        return none();
                    }) }, }));
        }

        bool cmd_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_cmd();
                }
                if (auto const doc = cmd_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("inspect", py{ fn_import("inspect") }.release());
                aAttributes.emplace_back("string", py{ fn_import("string") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("cmd", cmd_module);
    }
}
