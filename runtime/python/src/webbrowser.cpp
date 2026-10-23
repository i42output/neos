/*
  webbrowser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/webbrowser.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module webbrowser (CPython 3.13's Lib/webbrowser.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "webbrowser_docs.hpp"

namespace neos::python_runtime::detail::webbrowser_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "webbrowser.name"))
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
                py_raise("SystemError", std::string{ "webbrowser: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"webbrowser.name">()))
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
            if (modules_imported("webbrowser"))
                return py{ fn_import("webbrowser") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "webbrowser" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "webbrowser", webbrowser_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- webbrowser (Lib/webbrowser.py)

        void make_webbrowser()
        {
            static module_writer const& w = writer("webbrowser");
            // ! /usr/bin/env python3
            // Maintained by Georg Brandl.
            define("webbrowser", "__all__", py_list({ S<"Error">(), S<"open">(), S<"open_new">(), S<"open_new_tab">(), S<"get">(), S<"register">() }));
            define("webbrowser", "Error", w.make_class("Error", { B<"Exception">() }, {
 }));

            define("webbrowser", "_lock", (Mod<"threading">()).method("RLock"));
            define("webbrowser", "_browsers", py_dict());
            define("webbrowser", "_tryorder", none());
            define("webbrowser", "_os_preferred_browser", none());
            define("webbrowser", "register", w.function("register", "register", { py_positional("name"), py_positional("klass"), py_positional("instance", none()), py_keyword("preferred", boolean(false)) }, [](arguments a) -> py
                {
                    py name = a[0];
                    py klass = a[1];
                    py instance = a[2];
                    py preferred = a[3];
                    {
                        py const manager1 = G<"webbrowser._lock">();
                        py const exit1 = pyclass::special_method(manager1, "__exit__");
                        py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                        [[maybe_unused]] int flow1 = 0;
                        [[maybe_unused]] py result1;
                        bool exited1 = false;
                        try
                        {
                            if ((G<"webbrowser._tryorder">()).is_none())
                            {
                                (G<"webbrowser.register_standard_browsers">())();
                            }
                            (G<"webbrowser._browsers">()).set_item(name.method("lower"), py_list({ klass, instance }));
                            // Preferred browsers go to the front of the list.
                            // Need to match to the default browser returned by xdg-settings, which
                            // may be of the form e.g. "firefox.desktop".
                            if ((preferred.truthy() || ([&]() -> py { py const t = G<"webbrowser._os_preferred_browser">(); return !t.truthy() ? t : boolean(S(name.str() + ".desktop") == (G<"webbrowser._os_preferred_browser">())); }()).truthy()))
                            {
                                (G<"webbrowser._tryorder">()).method("insert", { I<0>(), name });
                            }
                            else
                            {
                                (G<"webbrowser._tryorder">()).method("append", { name });
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            exited1 = true;
                            if (!exit1(manager1, type_of(handler1.exception), handler1.exception, handler1.exception.attr("__traceback__")).truthy())
                                py_reraise(handler1.exception);
                        }
                        finally1:
                        if (!exited1)
                            exit1(manager1, none(), none(), none());
                        if (flow1 == 1)
                            return result1;
                    }
                    return none();
                }));

            define("webbrowser", "get", w.function("get", "get", { py_positional("using", none()) }, [](arguments a) -> py
                {
                    py using_ = a[0];
                    py alternatives;
                    py browser;
                    py command;
                    if ((G<"webbrowser._tryorder">()).is_none())
                    {
                        {
                            py const manager2 = G<"webbrowser._lock">();
                            py const exit2 = pyclass::special_method(manager2, "__exit__");
                            py const value2 = pyclass::special_method(manager2, "__enter__")(manager2);
                            [[maybe_unused]] int flow2 = 0;
                            [[maybe_unused]] py result2;
                            bool exited2 = false;
                            try
                            {
                                if ((G<"webbrowser._tryorder">()).is_none())
                                {
                                    (G<"webbrowser.register_standard_browsers">())();
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                exited2 = true;
                                if (!exit2(manager2, type_of(handler2.exception), handler2.exception, handler2.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler2.exception);
                            }
                            finally2:
                            if (!exited2)
                                exit2(manager2, none(), none(), none());
                            if (flow2 == 1)
                                return result2;
                        }
                    }
                    if (!using_.is_none())
                    {
                        alternatives = py_list({ using_ });
                    }
                    else
                    {
                        alternatives = G<"webbrowser._tryorder">();
                    }
                    for (py const& item : iterate{ alternatives })
                    {
                        browser = item;
                        if (browser.contains((S<"%s">())))
                        {
                            // User gave us a command line, split it into name and args
                            browser = (Mod<"shlex">().attr("split"))(browser);
                            if (browser[I<-1>()] == (S<"&">()))
                            {
                                return (G<"webbrowser.BackgroundBrowser">())(browser[B<"slice">()(none(), I<-1>(), none())]);
                            }
                            else
                            {
                                return (G<"webbrowser.GenericBrowser">())(browser);
                            }
                        }
                        else
                        {
                            // User gave us a browser name or path.
                            try
                            {
                                command = (G<"webbrowser._browsers">())[browser.method("lower")];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler4;
                                if (matches(handler4.exception, B<"KeyError">()))
                                {
                                    command = (G<"webbrowser._synthesize">())(browser);
                                }
                                else
                                    py_reraise(handler4.exception);
                            }
                            if (!command[I<1>()].is_none())
                            {
                                return command[I<1>()];
                            }
                            else if (!command[I<0>()].is_none())
                            {
                                return command[I<0>()]();
                            }
                        }
                    }
                    py_raise((G<"webbrowser.Error">())(S<"could not locate runnable browser">()));
                }));

            // Please note: the following definition hides a builtin function.
            // It is recommended one does "import webbrowser" and uses webbrowser.open(url)
            // instead of "from webbrowser import *".
            define("webbrowser", "open", w.function("open", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                {
                    py url = a[0];
                    py new_ = a[1];
                    py autoraise = a[2];
                    py browser;
                    py name;
                    if ((G<"webbrowser._tryorder">()).is_none())
                    {
                        {
                            py const manager5 = G<"webbrowser._lock">();
                            py const exit5 = pyclass::special_method(manager5, "__exit__");
                            py const value5 = pyclass::special_method(manager5, "__enter__")(manager5);
                            [[maybe_unused]] int flow5 = 0;
                            [[maybe_unused]] py result5;
                            bool exited5 = false;
                            try
                            {
                                if ((G<"webbrowser._tryorder">()).is_none())
                                {
                                    (G<"webbrowser.register_standard_browsers">())();
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                exited5 = true;
                                if (!exit5(manager5, type_of(handler5.exception), handler5.exception, handler5.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler5.exception);
                            }
                            finally5:
                            if (!exited5)
                                exit5(manager5, none(), none(), none());
                            if (flow5 == 1)
                                return result5;
                        }
                    }
                    for (py const& item : iterate{ G<"webbrowser._tryorder">() })
                    {
                        name = item;
                        browser = (G<"webbrowser.get">())(name);
                        if (browser.method("open", { url, new_, autoraise }).truthy())
                        {
                            return boolean(true);
                        }
                    }
                    return boolean(false);
                }));

            define("webbrowser", "open_new", w.function("open_new", "open_new", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    return (G<"webbrowser.open">())(url, I<1>());
                }));

            define("webbrowser", "open_new_tab", w.function("open_new_tab", "open_new_tab", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    return (G<"webbrowser.open">())(url, I<2>());
                }));

            define("webbrowser", "_synthesize", w.function("_synthesize", "_synthesize", { py_positional("browser"), py_keyword("preferred", boolean(false)) }, [](arguments a) -> py
                {
                    py browser = a[0];
                    py preferred = a[1];
                    py cmd;
                    py command;
                    py controller;
                    py copy;
                    py name;
                    cmd = browser.method("split")[I<0>()];
                    if (!(Mod<"shutil">().attr("which"))(cmd).truthy())
                    {
                        return py_list({ none(), none() });
                    }
                    name = (Mod<"os">().attr("path")).method("basename", { cmd });
                    try
                    {
                        command = (G<"webbrowser._browsers">())[name.method("lower")];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler7;
                        if (matches(handler7.exception, B<"KeyError">()))
                        {
                            return py_list({ none(), none() });
                        }
                        else
                            py_reraise(handler7.exception);
                    }
                    // now attempt to clone to fit the new name:
                    controller = command[I<1>()];
                    if ((controller.truthy() && (name.method("lower") == controller.attr("basename"))))
                    {
                        copy = module_of("copy");
                        controller = copy.method("copy", { controller });
                        controller.set_attr("name", browser);
                        controller.set_attr("basename", (Mod<"os">().attr("path")).method("basename", { browser }));
                        (G<"webbrowser.register">()).call({ browser, none() }, { { "instance", controller }, { "preferred", preferred } });
                        return py_list({ none(), controller });
                    }
                    return py_list({ none(), none() });
                }));

            // General parent classes
            define("webbrowser", "BaseBrowser", w.make_class("BaseBrowser", {  }, {
                { "args", py_list({ S<"%s">() }) },
                { "__init__", w.method("BaseBrowser", "__init__", { py_positional("name", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.set_attr("name", name);
                        self.set_attr("basename", name);
                        return none();
                    }) },
                { "open", w.method("BaseBrowser", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py new_ = a[2];
                        py autoraise = a[3];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "open_new", w.method("BaseBrowser", "open_new", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        return self.method("open", { url, I<1>() });
                    }) },
                { "open_new_tab", w.method("BaseBrowser", "open_new_tab", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        return self.method("open", { url, I<2>() });
                    }) },
                { "_check_url", w.static_method("BaseBrowser", "_check_url", { py_positional("url") }, [](arguments a) -> py
                    {
                        py url = a[0];
                        if ((url.truthy() && url.method("lstrip").method("startswith", { S<"-">() }).truthy()))
                        {
                            py_raise((B<"ValueError">())(S(std::string{ "Invalid URL (leading dash disallowed): " } + url.repr())));
                        }
                        return none();
                    }) }, }));

            define("webbrowser", "GenericBrowser", w.make_class("GenericBrowser", { G<"webbrowser.BaseBrowser">() }, {
                { "__init__", w.method("GenericBrowser", "__init__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (isinstance(name, B<"str">()))
                        {
                            self.set_attr("name", name);
                            self.set_attr("args", py_list({ S<"%s">() }));
                        }
                        else
                        {
                            // name should be a list with arguments
                            self.set_attr("name", name[I<0>()]);
                            self.set_attr("args", name[B<"slice">()(I<1>(), none(), none())]);
                        }
                        self.set_attr("basename", (Mod<"os">().attr("path")).method("basename", { self.attr("name") }));
                        return none();
                    }) },
                { "open", w.method("GenericBrowser", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py new_ = a[2];
                        py autoraise = a[3];
                        py cmdline;
                        py p;
                        (Mod<"sys">().attr("audit"))(S<"webbrowser.open">(), url);
                        self.method("_check_url", { url });
                        cmdline = py_list({ self.attr("name") }) + ([&]() -> py { py const comprehension8 = py_list(); py arg; for (py const& item : iterate{ self.attr("args") }) { arg = item; comprehension8.method("append", { arg.method("replace", { S<"%s">(), url }) }); } return comprehension8; }());
                        try
                        {
                            if ((Mod<"sys">().attr("platform"))[B<"slice">()(none(), I<3>(), none())] == (S<"win">()))
                            {
                                p = (Mod<"subprocess">().attr("Popen"))(cmdline);
                            }
                            else
                            {
                                p = (Mod<"subprocess">().attr("Popen")).call({ cmdline }, { { "close_fds", boolean(true) } });
                            }
                            return boolean(!p.method("wait").truthy());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, B<"OSError">()))
                            {
                                return boolean(false);
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        return none();
                    }) }, }));

            define("webbrowser", "BackgroundBrowser", w.make_class("BackgroundBrowser", { G<"webbrowser.GenericBrowser">() }, {
                { "open", w.method("BackgroundBrowser", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py new_ = a[2];
                        py autoraise = a[3];
                        py cmdline;
                        py p;
                        cmdline = py_list({ self.attr("name") }) + ([&]() -> py { py const comprehension10 = py_list(); py arg; for (py const& item : iterate{ self.attr("args") }) { arg = item; comprehension10.method("append", { arg.method("replace", { S<"%s">(), url }) }); } return comprehension10; }());
                        (Mod<"sys">().attr("audit"))(S<"webbrowser.open">(), url);
                        self.method("_check_url", { url });
                        try
                        {
                            if ((Mod<"sys">().attr("platform"))[B<"slice">()(none(), I<3>(), none())] == (S<"win">()))
                            {
                                p = (Mod<"subprocess">().attr("Popen"))(cmdline);
                            }
                            else
                            {
                                p = (Mod<"subprocess">().attr("Popen")).call({ cmdline }, { { "close_fds", boolean(true) }, { "start_new_session", boolean(true) } });
                            }
                            return boolean(p.method("poll").is_none());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            if (matches(handler11.exception, B<"OSError">()))
                            {
                                return boolean(false);
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        return none();
                    }) }, }));

                // In remote_args, %s will be replaced with the requested URL.  %action will
                // be replaced depending on the value of 'new' passed to open.
                // remote_action is used for new=0 (open).  If newwin is not None, it is
                // used for new=1 (open_new).  If newtab is not None, it is used for
                // new=3 (open_new_tab).  After both substitutions are made, any empty
                // strings in the transformed remote_args list will be removed.
            define("webbrowser", "UnixBrowser", w.make_class("UnixBrowser", { G<"webbrowser.BaseBrowser">() }, {
                { "raise_opts", none() },
                { "background", boolean(false) },
                { "redirect_stdout", boolean(true) },
                { "remote_args", py_list({ S<"%action">(), S<"%s">() }) },
                { "remote_action", none() },
                { "remote_action_newwin", none() },
                { "remote_action_newtab", none() },
                { "_invoke", w.method("UnixBrowser", "_invoke", { py_positional("args"), py_positional("remote"), py_positional("autoraise"), py_positional("url", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py remote = a[2];
                        py autoraise = a[3];
                        py url = a[4];
                        py cmdline;
                        py inout;
                        py opt;
                        py p;
                        py raise_opt;
                        py rc;
                        raise_opt = py_list();
                        if ((remote.truthy() && self.attr("raise_opts").truthy()))
                        {
                            // use autoraise argument only for remote invocation
                            autoraise = (B<"int">())(autoraise);
                            opt = self.attr("raise_opts")[autoraise];
                            if (opt.truthy())
                            {
                                raise_opt = py_list({ opt });
                            }
                        }
                        cmdline = (py_list({ self.attr("name") }) + raise_opt) + args;
                        if ((remote.truthy() || self.attr("background").truthy()))
                        {
                            inout = py{ -3 } /* subprocess.DEVNULL */;
                        }
                        else
                        {
                            // for TTY browsers, we need stdin/out
                            inout = none();
                        }
                        p = (Mod<"subprocess">().attr("Popen")).call({ cmdline }, { { "close_fds", boolean(true) }, { "stdin", inout }, { "stdout", [&]() -> py { py const t = [&]() -> py { py const t = self.attr("redirect_stdout"); return !t.truthy() ? t : inout; }(); return t.truthy() ? t : none(); }() }, { "stderr", inout }, { "start_new_session", boolean(true) } });
                        if (remote.truthy())
                        {
                            // wait at most five seconds. If the subprocess is not finished, the
                            // remote invocation has (hopefully) started a new instance.
                            try
                            {
                                rc = p.method("wait", { I<5>() });
                                // if remote call failed, open() will try direct invocation
                                return boolean(!rc.truthy());
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler12;
                                if (matches(handler12.exception, Mod<"subprocess">().attr("TimeoutExpired")))
                                {
                                    return boolean(true);
                                }
                                else
                                    py_reraise(handler12.exception);
                            }
                        }
                        else if (self.attr("background").truthy())
                        {
                            if (p.method("poll").is_none())
                            {
                                return boolean(true);
                            }
                            else
                            {
                                return boolean(false);
                            }
                        }
                        else
                        {
                            return boolean(!p.method("wait").truthy());
                        }
                        return none();
                    }) },
                { "open", w.method("UnixBrowser", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py new_ = a[2];
                        py autoraise = a[3];
                        py action;
                        py args;
                        py success;
                        (Mod<"sys">().attr("audit"))(S<"webbrowser.open">(), url);
                        if (new_ == (I<0>()))
                        {
                            action = self.attr("remote_action");
                        }
                        else if (new_ == (I<1>()))
                        {
                            action = self.attr("remote_action_newwin");
                        }
                        else if (new_ == (I<2>()))
                        {
                            if (self.attr("remote_action_newtab").is_none())
                            {
                                action = self.attr("remote_action_newwin");
                            }
                            else
                            {
                                action = self.attr("remote_action_newtab");
                            }
                        }
                        else
                        {
                            py_raise((G<"webbrowser.Error">())(S(std::string{ "Bad 'new' parameter to open(); expected 0, 1, or 2, got " } + new_.str())));
                        }
                        self.method("_check_url", { url.method("replace", { S<"%action">(), action }) });
                        args = [&]() -> py { py const comprehension13 = py_list(); py arg; for (py const& item : iterate{ self.attr("remote_args") }) { arg = item; comprehension13.method("append", { arg.method("replace", { S<"%action">(), action }).method("replace", { S<"%s">(), url }) }); } return comprehension13; }();
                        args = [&]() -> py { py const comprehension14 = py_list(); py arg; for (py const& item : iterate{ args }) { arg = item; if (!arg.truthy()) continue; comprehension14.method("append", { arg }); } return comprehension14; }();
                        success = self.method("_invoke", { args, boolean(true), autoraise, url });
                        if (!success.truthy())
                        {
                            // remote invocation failed, try straight way
                            args = [&]() -> py { py const comprehension15 = py_list(); py arg; for (py const& item : iterate{ self.attr("args") }) { arg = item; comprehension15.method("append", { arg.method("replace", { S<"%s">(), url }) }); } return comprehension15; }();
                            return self.method("_invoke", { args, boolean(false), boolean(false) });
                        }
                        else
                        {
                            return boolean(true);
                        }
                        return none();
                    }) }, }));

            define("webbrowser", "Mozilla", w.make_class("Mozilla", { G<"webbrowser.UnixBrowser">() }, {
                { "remote_args", py_list({ S<"%action">(), S<"%s">() }) },
                { "remote_action", S<"">() },
                { "remote_action_newwin", S<"-new-window">() },
                { "remote_action_newtab", S<"-new-tab">() },
                { "background", boolean(true) }, }));

            define("webbrowser", "Epiphany", w.make_class("Epiphany", { G<"webbrowser.UnixBrowser">() }, {
                { "raise_opts", py_list({ S<"-noraise">(), S<"">() }) },
                { "remote_args", py_list({ S<"%action">(), S<"%s">() }) },
                { "remote_action", S<"-n">() },
                { "remote_action_newwin", S<"-w">() },
                { "background", boolean(true) }, }));

            define("webbrowser", "Chrome", w.make_class("Chrome", { G<"webbrowser.UnixBrowser">() }, {
                { "remote_args", py_list({ S<"%action">(), S<"%s">() }) },
                { "remote_action", S<"">() },
                { "remote_action_newwin", S<"--new-window">() },
                { "remote_action_newtab", S<"">() },
                { "background", boolean(true) }, }));

            define("webbrowser", "Chromium", G<"webbrowser.Chrome">());
            define("webbrowser", "Opera", w.make_class("Opera", { G<"webbrowser.UnixBrowser">() }, {
                { "remote_args", py_list({ S<"%action">(), S<"%s">() }) },
                { "remote_action", S<"">() },
                { "remote_action_newwin", S<"--new-window">() },
                { "remote_action_newtab", S<"">() },
                { "background", boolean(true) }, }));

                // elinks doesn't like its stdout to be redirected -
                // it uses redirected stdout as a signal to do -dump
            define("webbrowser", "Elinks", w.make_class("Elinks", { G<"webbrowser.UnixBrowser">() }, {
                { "remote_args", py_list({ S<"-remote">(), S<"openURL(%s%action)">() }) },
                { "remote_action", S<"">() },
                { "remote_action_newwin", S<",new-window">() },
                { "remote_action_newtab", S<",new-tab">() },
                { "background", boolean(false) },
                { "redirect_stdout", boolean(false) }, }));

            define("webbrowser", "Konqueror", w.make_class("Konqueror", { G<"webbrowser.BaseBrowser">() }, {
                { "open", w.method("Konqueror", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py new_ = a[2];
                        py autoraise = a[3];
                        py action;
                        py devnull;
                        py p;
                        (Mod<"sys">().attr("audit"))(S<"webbrowser.open">(), url);
                        self.method("_check_url", { url });
                        // XXX Currently I know no way to prevent KFM from opening a new win.
                        if (new_ == (I<2>()))
                        {
                            action = S<"newTab">();
                        }
                        else
                        {
                            action = S<"openURL">();
                        }
                        devnull = py{ -3 } /* subprocess.DEVNULL */;
                        {
                            bool otherwise16 = false;
                            try
                            {
                                p = (Mod<"subprocess">().attr("Popen")).call({ py_list({ S<"kfmclient">(), action, url }) }, { { "close_fds", boolean(true) }, { "stdin", devnull }, { "stdout", devnull }, { "stderr", devnull } });
                                otherwise16 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler16;
                                if (matches(handler16.exception, B<"OSError">()))
                                {
                                    // fall through to next variant
                                }
                                else
                                    py_reraise(handler16.exception);
                            }
                            if (otherwise16)
                            {
                                p.method("wait");
                                // kfmclient's return code unfortunately has no meaning as it seems
                                return boolean(true);
                            }
                        }
                        {
                            bool otherwise17 = false;
                            try
                            {
                                p = (Mod<"subprocess">().attr("Popen")).call({ py_list({ S<"konqueror">(), S<"--silent">(), url }) }, { { "close_fds", boolean(true) }, { "stdin", devnull }, { "stdout", devnull }, { "stderr", devnull }, { "start_new_session", boolean(true) } });
                                otherwise17 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler17;
                                if (matches(handler17.exception, B<"OSError">()))
                                {
                                    // fall through to next variant
                                }
                                else
                                    py_reraise(handler17.exception);
                            }
                            if (otherwise17)
                            {
                                if (p.method("poll").is_none())
                                {
                                    // Should be running now.
                                    return boolean(true);
                                }
                            }
                        }
                        {
                            bool otherwise18 = false;
                            try
                            {
                                p = (Mod<"subprocess">().attr("Popen")).call({ py_list({ S<"kfm">(), S<"-d">(), url }) }, { { "close_fds", boolean(true) }, { "stdin", devnull }, { "stdout", devnull }, { "stderr", devnull }, { "start_new_session", boolean(true) } });
                                otherwise18 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler18;
                                if (matches(handler18.exception, B<"OSError">()))
                                {
                                    return boolean(false);
                                }
                                else
                                    py_reraise(handler18.exception);
                            }
                            if (otherwise18)
                            {
                                return boolean(p.method("poll").is_none());
                            }
                        }
                        return none();
                    }) }, }));

            define("webbrowser", "Edge", w.make_class("Edge", { G<"webbrowser.UnixBrowser">() }, {
                { "remote_args", py_list({ S<"%action">(), S<"%s">() }) },
                { "remote_action", S<"">() },
                { "remote_action_newwin", S<"--new-window">() },
                { "remote_action_newtab", S<"">() },
                { "background", boolean(true) }, }));

            // 
            // Platform support for Unix
            // 
            // These are the right tests because all these Unix browsers require either
            // a console terminal or an X display to run.
            define("webbrowser", "register_X_browsers", w.function("register_X_browsers", "register_X_browsers", {  }, [](arguments a) -> py
                {
                    py browser;
                    py xdg_desktop;
                    if ((Mod<"shutil">().attr("which"))(S<"xdg-open">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"xdg-open">(), none(), (G<"webbrowser.BackgroundBrowser">())(S<"xdg-open">()));
                    }
                    // Opens an appropriate browser for the URL scheme according to
                    // freedesktop.org settings (GNOME, KDE, XFCE, etc.)
                    if ((Mod<"shutil">().attr("which"))(S<"gio">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"gio">(), none(), (G<"webbrowser.BackgroundBrowser">())(py_list({ S<"gio">(), S<"open">(), S<"--">(), S<"%s">() })));
                    }
                    xdg_desktop = (Mod<"os">().attr("getenv"))(S<"XDG_CURRENT_DESKTOP">(), S<"">()).method("split", { S<":">() });
                    // The default GNOME3 browser
                    if ((([&]() -> py { py const t = boolean(xdg_desktop.contains((S<"GNOME">()))); return t.truthy() ? t : boolean((Mod<"os">().attr("environ")).contains((S<"GNOME_DESKTOP_SESSION_ID">()))); }()).truthy() && (Mod<"shutil">().attr("which"))(S<"gvfs-open">()).truthy()))
                    {
                        (G<"webbrowser.register">())(S<"gvfs-open">(), none(), (G<"webbrowser.BackgroundBrowser">())(S<"gvfs-open">()));
                    }
                    // The default KDE browser
                    if ((([&]() -> py { py const t = boolean(xdg_desktop.contains((S<"KDE">()))); return t.truthy() ? t : boolean((Mod<"os">().attr("environ")).contains((S<"KDE_FULL_SESSION">()))); }()).truthy() && (Mod<"shutil">().attr("which"))(S<"kfmclient">()).truthy()))
                    {
                        (G<"webbrowser.register">())(S<"kfmclient">(), G<"webbrowser.Konqueror">(), (G<"webbrowser.Konqueror">())(S<"kfmclient">()));
                    }
                    // Common symbolic link for the default X11 browser
                    if ((Mod<"shutil">().attr("which"))(S<"x-www-browser">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"x-www-browser">(), none(), (G<"webbrowser.BackgroundBrowser">())(S<"x-www-browser">()));
                    }
                    // The Mozilla browsers
                    for (py const& item : iterate{ py_tuple({ S<"firefox">(), S<"iceweasel">(), S<"seamonkey">(), S<"mozilla-firefox">(), S<"mozilla">() }) })
                    {
                        browser = item;
                        if ((Mod<"shutil">().attr("which"))(browser).truthy())
                        {
                            (G<"webbrowser.register">())(browser, none(), (G<"webbrowser.Mozilla">())(browser));
                        }
                    }
                    // Konqueror/kfm, the KDE browser.
                    if ((Mod<"shutil">().attr("which"))(S<"kfm">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"kfm">(), G<"webbrowser.Konqueror">(), (G<"webbrowser.Konqueror">())(S<"kfm">()));
                    }
                    else if ((Mod<"shutil">().attr("which"))(S<"konqueror">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"konqueror">(), G<"webbrowser.Konqueror">(), (G<"webbrowser.Konqueror">())(S<"konqueror">()));
                    }
                    // Gnome's Epiphany
                    if ((Mod<"shutil">().attr("which"))(S<"epiphany">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"epiphany">(), none(), (G<"webbrowser.Epiphany">())(S<"epiphany">()));
                    }
                    // Google Chrome/Chromium browsers
                    for (py const& item : iterate{ py_tuple({ S<"google-chrome">(), S<"chrome">(), S<"chromium">(), S<"chromium-browser">() }) })
                    {
                        browser = item;
                        if ((Mod<"shutil">().attr("which"))(browser).truthy())
                        {
                            (G<"webbrowser.register">())(browser, none(), (G<"webbrowser.Chrome">())(browser));
                        }
                    }
                    // Opera, quite popular
                    if ((Mod<"shutil">().attr("which"))(S<"opera">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"opera">(), none(), (G<"webbrowser.Opera">())(S<"opera">()));
                    }
                    if ((Mod<"shutil">().attr("which"))(S<"microsoft-edge">()).truthy())
                    {
                        (G<"webbrowser.register">())(S<"microsoft-edge">(), none(), (G<"webbrowser.Edge">())(S<"microsoft-edge">()));
                    }
                    return none();
                }));

            define("webbrowser", "register_standard_browsers", w.function("register_standard_browsers", "register_standard_browsers", {  }, [](arguments a) -> py
                {
                    py browser;
                    py cmd;
                    py cmdline;
                    py edge32;
                    py edge64;
                    py raw_result;
                    py result;
                    py userchoices;
                    set_global("webbrowser", "_tryorder", py_list());
                    #if defined(__APPLE__)
                    {
                        (G<"webbrowser.register">())(S<"MacOSX">(), none(), (G<"webbrowser.MacOSXOSAScript">())(S<"default">()));
                        (G<"webbrowser.register">())(S<"chrome">(), none(), (G<"webbrowser.MacOSXOSAScript">())(S<"chrome">()));
                        (G<"webbrowser.register">())(S<"firefox">(), none(), (G<"webbrowser.MacOSXOSAScript">())(S<"firefox">()));
                        (G<"webbrowser.register">())(S<"safari">(), none(), (G<"webbrowser.MacOSXOSAScript">())(S<"safari">()));
                    }
                    #endif
                    // OS X can use below Unix support (but we prefer using the OS X
                    // specific stuff)
                    if ((Mod<"sys">().attr("platform")) == (S<"ios">()))
                    {
                        (G<"webbrowser.register">()).call({ S<"iosbrowser">(), none(), (G<"webbrowser.IOSBrowser">())() }, { { "preferred", boolean(true) } });
                    }
                    if ((Mod<"sys">().attr("platform")) == (S<"serenityos">()))
                    {
                        // SerenityOS webbrowser, simply called "Browser".
                        (G<"webbrowser.register">())(S<"Browser">(), none(), (G<"webbrowser.BackgroundBrowser">())(S<"Browser">()));
                    }
                    if ((Mod<"sys">().attr("platform"))[B<"slice">()(none(), I<3>(), none())] == (S<"win">()))
                    {
                        // First try to use the default Windows browser
                        (G<"webbrowser.register">())(S<"windows-default">(), G<"webbrowser.WindowsDefault">());
                        // Detect some common Windows browsers, fallback to Microsoft Edge
                        // location in 64-bit Windows
                        edge64 = (Mod<"os">().attr("path")).method("join", { (Mod<"os">().attr("environ")).method("get", { S<"PROGRAMFILES(x86)">(), S<"C:\\Program Files (x86)">() }), S<"Microsoft\\Edge\\Application\\msedge.exe">() });
                        // location in 32-bit Windows
                        edge32 = (Mod<"os">().attr("path")).method("join", { (Mod<"os">().attr("environ")).method("get", { S<"PROGRAMFILES">(), S<"C:\\Program Files">() }), S<"Microsoft\\Edge\\Application\\msedge.exe">() });
                        for (py const& item : iterate{ py_tuple({ S<"firefox">(), S<"seamonkey">(), S<"mozilla">(), S<"chrome">(), S<"opera">(), edge64, edge32 }) })
                        {
                            browser = item;
                            if ((Mod<"shutil">().attr("which"))(browser).truthy())
                            {
                                (G<"webbrowser.register">())(browser, none(), (G<"webbrowser.BackgroundBrowser">())(browser));
                            }
                        }
                        if ((Mod<"shutil">().attr("which"))(S<"MicrosoftEdge.exe">()).truthy())
                        {
                            (G<"webbrowser.register">())(S<"microsoft-edge">(), none(), (G<"webbrowser.Edge">())(S<"MicrosoftEdge.exe">()));
                        }
                    }
                    else
                    {
                        // Prefer X browsers if present
                        // 
                        // NOTE: Do not check for X11 browser on macOS,
                        // XQuartz installation sets a DISPLAY environment variable and will
                        // autostart when someone tries to access the display. Mac users in
                        // general don't need an X11 browser.
                        if ((((Mod<"sys">().attr("platform")) != (S<"darwin">())) && ([&]() -> py { py const t = (Mod<"os">().attr("environ")).method("get", { S<"DISPLAY">() }); return t.truthy() ? t : (Mod<"os">().attr("environ")).method("get", { S<"WAYLAND_DISPLAY">() }); }()).truthy()))
                        {
                            {
                                bool otherwise22 = false;
                                try
                                {
                                    cmd = (S<"xdg-settings get default-web-browser">()).method("split");
                                    raw_result = (Mod<"subprocess">().attr("check_output")).call({ cmd }, { { "stderr", py{ -3 } /* subprocess.DEVNULL */ } });
                                    result = raw_result.method("decode").method("strip");
                                    otherwise22 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler22;
                                    if (matches(handler22.exception, py_tuple({ B<"FileNotFoundError">(), Mod<"subprocess">().attr("CalledProcessError"), B<"PermissionError">(), B<"NotADirectoryError">() })))
                                    {
                                    }
                                    else
                                        py_reraise(handler22.exception);
                                }
                                if (otherwise22)
                                {
                                    set_global("webbrowser", "_os_preferred_browser", result);
                                }
                            }
                            (G<"webbrowser.register_X_browsers">())();
                        }
                        // Also try console browsers
                        if ((Mod<"os">().attr("environ")).method("get", { S<"TERM">() }).truthy())
                        {
                            // Common symbolic link for the default text-based browser
                            if ((Mod<"shutil">().attr("which"))(S<"www-browser">()).truthy())
                            {
                                (G<"webbrowser.register">())(S<"www-browser">(), none(), (G<"webbrowser.GenericBrowser">())(S<"www-browser">()));
                            }
                            // The Links/elinks browsers <http://links.twibright.com/>
                            if ((Mod<"shutil">().attr("which"))(S<"links">()).truthy())
                            {
                                (G<"webbrowser.register">())(S<"links">(), none(), (G<"webbrowser.GenericBrowser">())(S<"links">()));
                            }
                            if ((Mod<"shutil">().attr("which"))(S<"elinks">()).truthy())
                            {
                                (G<"webbrowser.register">())(S<"elinks">(), none(), (G<"webbrowser.Elinks">())(S<"elinks">()));
                            }
                            // The Lynx browser <https://lynx.invisible-island.net/>, <http://lynx.browser.org/>
                            if ((Mod<"shutil">().attr("which"))(S<"lynx">()).truthy())
                            {
                                (G<"webbrowser.register">())(S<"lynx">(), none(), (G<"webbrowser.GenericBrowser">())(S<"lynx">()));
                            }
                            // The w3m browser <http://w3m.sourceforge.net/>
                            if ((Mod<"shutil">().attr("which"))(S<"w3m">()).truthy())
                            {
                                (G<"webbrowser.register">())(S<"w3m">(), none(), (G<"webbrowser.GenericBrowser">())(S<"w3m">()));
                            }
                        }
                    }
                    // OK, now that we know what the default preference orders for each
                    // platform are, allow user to override them with the BROWSER variable.
                    if ((Mod<"os">().attr("environ")).contains((S<"BROWSER">())))
                    {
                        userchoices = (Mod<"os">().attr("environ"))[S<"BROWSER">()].method("split", { Mod<"os">().attr("pathsep") });
                        userchoices.method("reverse");
                        // Treat choices in same way as if passed into get() but do register
                        // and prepend to _tryorder
                        for (py const& item : iterate{ userchoices })
                        {
                            cmdline = item;
                            if (cmdline != (S<"">()))
                            {
                                cmd = (G<"webbrowser._synthesize">()).call({ cmdline }, { { "preferred", boolean(true) } });
                                if (cmd[I<1>()].is_none())
                                {
                                    (G<"webbrowser.register">()).call({ cmdline, none(), (G<"webbrowser.GenericBrowser">())(cmdline) }, { { "preferred", boolean(true) } });
                                }
                            }
                        }
                    }
                    return none();
                }));

            // what to do if _tryorder is now empty?
            // 
            // Platform support for Windows
            // 
            if ((Mod<"sys">().attr("platform"))[B<"slice">()(none(), I<3>(), none())] == (S<"win">()))
            {
                define("webbrowser", "WindowsDefault", w.make_class("WindowsDefault", { G<"webbrowser.BaseBrowser">() }, {
                    { "open", w.method("WindowsDefault", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py url = a[1];
                            py new_ = a[2];
                            py autoraise = a[3];
                            (Mod<"sys">().attr("audit"))(S<"webbrowser.open">(), url);
                            self.method("_check_url", { url });
                            {
                                bool otherwise24 = false;
                                try
                                {
                                    (Mod<"os">().attr("startfile"))(url);
                                    otherwise24 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler24;
                                    if (matches(handler24.exception, B<"OSError">()))
                                    {
                                        // [Error 22] No application is associated with the specified
                                        // file for this operation: '<URL>'
                                        return boolean(false);
                                    }
                                    else
                                        py_reraise(handler24.exception);
                                }
                                if (otherwise24)
                                {
                                    return boolean(true);
                                }
                            }
                            return none();
                        }) }, }));

            }
            // 
            // Platform support for macOS
            // 
#if defined(__APPLE__)
            define("webbrowser", "MacOSXOSAScript", w.make_class("MacOSXOSAScript", { G<"webbrowser.BaseBrowser">() }, {
                { "__init__", w.method("MacOSXOSAScript", "__init__", { py_positional("name", S<"default">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        (B<"super">()(G<"webbrowser.MacOSXOSAScript">(), self)).method("__init__", { name });
                        return none();
                    }) },
                { "open", w.method("MacOSXOSAScript", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py new_ = a[2];
                        py autoraise = a[3];
                        py osapipe;
                        py rc;
                        py script;
                        (Mod<"sys">().attr("audit"))(S<"webbrowser.open">(), url);
                        self.method("_check_url", { url });
                        url = url.method("replace", { S<"\"">(), S<"%22">() });
                        if (self.attr("name") == (S<"default">()))
                        {
                            script = S(std::string{ "open location \"" } + url.str() + "\"");
                        }
                        else
                        {
                            script = S(std::string{ "\n                   tell application \"" } + self.attr("name").str() + "\"\n                       activate\n                       open location \"" + url.str() + "\"\n                   end\n                   ");
                        }
                        osapipe = (Mod<"os">().attr("popen"))(S<"/usr/bin/osascript">(), S<"w">());
                        if (osapipe.is_none())
                        {
                            return boolean(false);
                        }
                        osapipe.method("write", { script });
                        rc = osapipe.method("close");
                        return boolean(!rc.truthy());
                    }) }, }));

#endif
            // 
            // Platform support for iOS
            // 
            if ((Mod<"sys">().attr("platform")) == (S<"ios">()))
            {
                if ((Mod<"_ios_support">().attr("objc")).truthy())
                {
                }
                    // If objc exists, we know ctypes is also importable.
                define("webbrowser", "IOSBrowser", w.make_class("IOSBrowser", { G<"webbrowser.BaseBrowser">() }, {
                    { "open", w.method("IOSBrowser", "open", { py_positional("url"), py_positional("new", I<0>()), py_positional("autoraise", boolean(true)) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py url = a[1];
                            py new_ = a[2];
                            py autoraise = a[3];
                            py NSString;
                            py NSURL;
                            py UIApplication;
                            py constructor;
                            py ns_url;
                            py openURL_;
                            py sharedApplication;
                            py shared_app;
                            py urlWithString_;
                            py url_string;
                            (Mod<"sys">().attr("audit"))(S<"webbrowser.open">(), url);
                            self.method("_check_url", { url });
                            // If ctypes isn't available, we can't open a browser
                            if ((Mod<"_ios_support">().attr("objc")).is_none())
                            {
                                return boolean(false);
                            }
                            // All the messages in this call return object references.
                            (Mod<"_ios_support">().attr("objc")).attr("objc_msgSend").set_attr("restype", Mod<"ctypes">().attr("c_void_p"));
                            // This is the equivalent of:
                            // NSString url_string =
                            // [NSString stringWithCString:url.encode("utf-8")
                            // encoding:NSUTF8StringEncoding];
                            NSString = (Mod<"_ios_support">().attr("objc")).method("objc_getClass", { py{ make_bytes(std::string{ "\x4e\x53\x53\x74\x72\x69\x6e\x67", 8 }) } });
                            constructor = (Mod<"_ios_support">().attr("objc")).method("sel_registerName", { py{ make_bytes(std::string{ "\x73\x74\x72\x69\x6e\x67\x57\x69\x74\x68\x43\x53\x74\x72\x69\x6e\x67\x3a\x65\x6e\x63\x6f\x64\x69\x6e\x67\x3a", 27 }) } });
                            (Mod<"_ios_support">().attr("objc")).attr("objc_msgSend").set_attr("argtypes", py_list({ Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_char_p"), Mod<"ctypes">().attr("c_ulong") }));
                            url_string = (Mod<"_ios_support">().attr("objc")).method("objc_msgSend", { NSString, constructor, url.method("encode", { S<"utf-8">() }), I<4>() });
                            // Create an NSURL object representing the URL
                            // This is the equivalent of:
                            // NSURL *nsurl = [NSURL URLWithString:url];
                            NSURL = (Mod<"_ios_support">().attr("objc")).method("objc_getClass", { py{ make_bytes(std::string{ "\x4e\x53\x55\x52\x4c", 5 }) } });
                            urlWithString_ = (Mod<"_ios_support">().attr("objc")).method("sel_registerName", { py{ make_bytes(std::string{ "\x55\x52\x4c\x57\x69\x74\x68\x53\x74\x72\x69\x6e\x67\x3a", 14 }) } });
                            (Mod<"_ios_support">().attr("objc")).attr("objc_msgSend").set_attr("argtypes", py_list({ Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p") }));
                            ns_url = (Mod<"_ios_support">().attr("objc")).method("objc_msgSend", { NSURL, urlWithString_, url_string });
                            // Get the shared UIApplication instance
                            // This code is the equivalent of:
                            // UIApplication shared_app = [UIApplication sharedApplication]
                            UIApplication = (Mod<"_ios_support">().attr("objc")).method("objc_getClass", { py{ make_bytes(std::string{ "\x55\x49\x41\x70\x70\x6c\x69\x63\x61\x74\x69\x6f\x6e", 13 }) } });
                            sharedApplication = (Mod<"_ios_support">().attr("objc")).method("sel_registerName", { py{ make_bytes(std::string{ "\x73\x68\x61\x72\x65\x64\x41\x70\x70\x6c\x69\x63\x61\x74\x69\x6f\x6e", 17 }) } });
                            (Mod<"_ios_support">().attr("objc")).attr("objc_msgSend").set_attr("argtypes", py_list({ Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p") }));
                            shared_app = (Mod<"_ios_support">().attr("objc")).method("objc_msgSend", { UIApplication, sharedApplication });
                            // Open the URL on the shared application
                            // This code is the equivalent of:
                            // [shared_app openURL:ns_url
                            // options:NIL
                            // completionHandler:NIL];
                            openURL_ = (Mod<"_ios_support">().attr("objc")).method("sel_registerName", { py{ make_bytes(std::string{ "\x6f\x70\x65\x6e\x55\x52\x4c\x3a\x6f\x70\x74\x69\x6f\x6e\x73\x3a\x63\x6f\x6d\x70\x6c\x65\x74\x69\x6f\x6e\x48\x61\x6e\x64\x6c\x65\x72\x3a", 34 }) } });
                            (Mod<"_ios_support">().attr("objc")).attr("objc_msgSend").set_attr("argtypes", py_list({ Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p"), Mod<"ctypes">().attr("c_void_p") }));
                            // Method returns void
                            (Mod<"_ios_support">().attr("objc")).attr("objc_msgSend").set_attr("restype", none());
                            (Mod<"_ios_support">().attr("objc")).method("objc_msgSend", { shared_app, openURL_, ns_url, none(), none() });
                            return boolean(true);
                        }) }, }));

            }
            define("webbrowser", "parse_args", w.function("parse_args", "parse_args", { py_positional("arg_list") }, [](arguments a) -> py
                {
                    py arg_list = a[0];
                    py argparse;
                    py args;
                    py group;
                    py parser;
                    argparse = module_of("argparse");
                    parser = argparse.method("ArgumentParser", {  }, { { "description", S<"Open URL in a web browser.">() } });
                    parser.method("add_argument", { S<"url">() }, { { "help", S<"URL to open">() } });
                    group = parser.method("add_mutually_exclusive_group");
                    group.method("add_argument", { S<"-n">(), S<"--new-window">() }, { { "action", S<"store_const">() }, { "const", I<1>() }, { "default", I<0>() }, { "dest", S<"new_win">() }, { "help", S<"open new window">() } });
                    group.method("add_argument", { S<"-t">(), S<"--new-tab">() }, { { "action", S<"store_const">() }, { "const", I<2>() }, { "default", I<0>() }, { "dest", S<"new_win">() }, { "help", S<"open new tab">() } });
                    args = parser.method("parse_args", { arg_list });
                    return args;
                }));

            define("webbrowser", "main", w.function("main", "main", { py_positional("arg_list", none()) }, [](arguments a) -> py
                {
                    py arg_list = a[0];
                    py args;
                    args = (G<"webbrowser.parse_args">())(arg_list);
                    (G<"webbrowser.open">())(args.attr("url"), args.attr("new_win"));
                    (B<"print">())(S<"\u0007">());
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool webbrowser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_webbrowser();
                }
                if (auto const doc = webbrowser_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("shlex", py{ fn_import("shlex") }.release());
                aAttributes.emplace_back("shutil", py{ fn_import("shutil") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("subprocess", py{ fn_import("subprocess") }.release());
                aAttributes.emplace_back("threading", py{ fn_import("threading") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("webbrowser", webbrowser_module);
    }
}
