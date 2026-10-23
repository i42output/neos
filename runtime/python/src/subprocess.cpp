/*
  subprocess.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/subprocess.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/subprocess.py's copyright: Copyright (c) 2003-2005 by Peter Astrand
  <astrand@lysator.liu.se>. Licensed to PSF under a Contributor Agreement.
*/

// The module subprocess (CPython 3.13's Lib/subprocess.py, translated from Python by py2neos, a Python-to-C++ translator): run, call,
// check_call, check_output, getoutput, getstatusoutput, Popen, CompletedProcess and their exceptions; its classes are made (by type) of
// closures (pyclass.hpp), so a program's subclasses override their methods. Its child processes are made by _posixsubprocess.fork_exec
// (POSIX: posixsubprocess.cpp) or _winapi.CreateProcess (Windows: winapi.cpp).

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "subprocess_docs.hpp"

namespace neos::python_runtime::detail::subprocess_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "subprocess.Popen"))
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
                py_raise("SystemError", std::string{ "subprocess: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"subprocess.Popen">()))
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
        // (a module's global assigned (global name; name = value): its object, and the module's attribute (if it has been imported))
        void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "subprocess", subprocess_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- subprocess (Lib/subprocess.py)

        void make_subprocess()
        {
            static module_writer const& sp = writer("subprocess");
            // subprocess - Subprocesses with accessible I/O streams
            // 
            // For more information about this module, see PEP 324.
            // 
            // Copyright (c) 2003-2005 by Peter Astrand <astrand@lysator.liu.se>
            // 
            // Licensed to PSF under a Contributor Agreement.
            define("subprocess", "__all__", py_list({ S<"Popen">(), S<"PIPE">(), S<"STDOUT">(), S<"call">(), S<"check_call">(), S<"getstatusoutput">(), S<"getoutput">(), S<"check_output">(), S<"run">(), S<"CalledProcessError">(), S<"DEVNULL">(), S<"SubprocessError">(), S<"TimeoutExpired">(), S<"CompletedProcess">() }));
            // NOTE: We intentionally exclude list2cmdline as it is
            // considered an internal implementation detail.  issue10838.
            // use presence of msvcrt to detect Windows-like platforms (see bpo-8110)
            define("subprocess", "_mswindows", boolean((Mod<"sys">().attr("platform")) == (S<"win32">())));
            // some platforms do not support subprocesses
            define("subprocess", "_can_fork_exec", boolean(!set_of({ S<"emscripten">(), S<"wasi">(), S<"ios">(), S<"tvos">(), S<"watchos">() }).contains((Mod<"sys">().attr("platform")))));
#if defined(_WIN32)
            (sp.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"subprocess.__all__">()).method("extend", { py_list({ S<"CREATE_NEW_CONSOLE">(), S<"CREATE_NEW_PROCESS_GROUP">(), S<"STD_INPUT_HANDLE">(), S<"STD_OUTPUT_HANDLE">(), S<"STD_ERROR_HANDLE">(), S<"SW_HIDE">(), S<"STARTF_USESTDHANDLES">(), S<"STARTF_USESHOWWINDOW">(), S<"STARTF_FORCEONFEEDBACK">(), S<"STARTF_FORCEOFFFEEDBACK">(), S<"STARTUPINFO">(), S<"ABOVE_NORMAL_PRIORITY_CLASS">(), S<"BELOW_NORMAL_PRIORITY_CLASS">(), S<"HIGH_PRIORITY_CLASS">(), S<"IDLE_PRIORITY_CLASS">(), S<"NORMAL_PRIORITY_CLASS">(), S<"REALTIME_PRIORITY_CLASS">(), S<"CREATE_NO_WINDOW">(), S<"DETACHED_PROCESS">(), S<"CREATE_DEFAULT_ERROR_MODE">(), S<"CREATE_BREAKAWAY_FROM_JOB">() }) });
                    return none();
                }))();
#else
#if !defined(_WIN32)
                // used in methods that are called by __del__
            define("subprocess", "_del_safe", sp.make_class("_del_safe", {  }, {
                { "waitpid", Mod<"os">().attr("waitpid") },
                { "waitstatus_to_exitcode", Mod<"os">().attr("waitstatus_to_exitcode") },
                { "WIFSTOPPED", Mod<"os">().attr("WIFSTOPPED") },
                { "WSTOPSIG", Mod<"os">().attr("WSTOPSIG") },
                { "WNOHANG", Mod<"os">().attr("WNOHANG") },
                { "ECHILD", Mod<"errno">().attr("ECHILD") }, }));

#else
            define("subprocess", "_del_safe", sp.make_class("_del_safe", {  }, {
                { "waitpid", none() },
                { "waitstatus_to_exitcode", none() },
                { "WIFSTOPPED", none() },
                { "WSTOPSIG", none() },
                { "WNOHANG", none() },
                { "ECHILD", Mod<"errno">().attr("ECHILD") }, }));

#endif
#endif
            // Exception classes used by this module.
            define("subprocess", "SubprocessError", sp.make_class("SubprocessError", { B<"Exception">() }, {
 }));

            define("subprocess", "CalledProcessError", sp.make_class("CalledProcessError", { G<"subprocess.SubprocessError">() }, {
                { "__init__", sp.method("CalledProcessError", "__init__", { py_positional("returncode"), py_positional("cmd"), py_positional("output", none()), py_positional("stderr", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py returncode = a[1];
                        py cmd = a[2];
                        py output = a[3];
                        py stderr_ = a[4];
                        self.set_attr("returncode", returncode);
                        self.set_attr("cmd", cmd);
                        self.set_attr("output", output);
                        self.set_attr("stderr", stderr_);
                        return none();
                    }) },
                { "__str__", sp.method("CalledProcessError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((isinstance(self.attr("returncode"), B<"int">()) && (self.attr("returncode") < (I<0>()))))
                        {
                            try
                            {
                                return (S<"Command '%s' died with %r.">()) % py_tuple({ self.attr("cmd"), (Mod<"signal">().attr("Signals"))(-self.attr("returncode")) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                if (matches(handler1.exception, B<"ValueError">()))
                                {
                                    return (S<"Command '%s' died with unknown signal %d.">()) % py_tuple({ self.attr("cmd"), -self.attr("returncode") });
                                }
                                else
                                    py_reraise(handler1.exception);
                            }
                        }
                        else
                        {
                            return S(std::string{ "Command '" } + self.attr("cmd").str() + "' returned non-zero exit status " + self.attr("returncode").str() + ".");
                        }
                        return none();
                    }) },
                { "stdout", B<"property">()(sp.method("CalledProcessError", "stdout", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("output");
                    }), sp.method("CalledProcessError", "stdout", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("output", value);
                        return none();
                    })) }, }));

            define("subprocess", "TimeoutExpired", sp.make_class("TimeoutExpired", { G<"subprocess.SubprocessError">() }, {
                { "__init__", sp.method("TimeoutExpired", "__init__", { py_positional("cmd"), py_positional("timeout"), py_positional("output", none()), py_positional("stderr", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py timeout = a[2];
                        py output = a[3];
                        py stderr_ = a[4];
                        self.set_attr("cmd", cmd);
                        self.set_attr("timeout", timeout);
                        self.set_attr("output", output);
                        self.set_attr("stderr", stderr_);
                        return none();
                    }) },
                { "__str__", sp.method("TimeoutExpired", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"Command '%s' timed out after %s seconds">()) % py_tuple({ self.attr("cmd"), self.attr("timeout") });
                    }) },
                { "stdout", B<"property">()(sp.method("TimeoutExpired", "stdout", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("output");
                    }), sp.method("TimeoutExpired", "stdout", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("output", value);
                        return none();
                    })) }, }));

#if defined(_WIN32)
            define("subprocess", "STARTUPINFO", sp.make_class("STARTUPINFO", {  }, {
                { "__init__", sp.method("STARTUPINFO", "__init__", { py_keyword("dwFlags", I<0>()), py_keyword("hStdInput", none()), py_keyword("hStdOutput", none()), py_keyword("hStdError", none()), py_keyword("wShowWindow", I<0>()), py_keyword("lpAttributeList", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dwFlags = a[1];
                        py hStdInput = a[2];
                        py hStdOutput = a[3];
                        py hStdError = a[4];
                        py wShowWindow = a[5];
                        py lpAttributeList = a[6];
                        self.set_attr("dwFlags", dwFlags);
                        self.set_attr("hStdInput", hStdInput);
                        self.set_attr("hStdOutput", hStdOutput);
                        self.set_attr("hStdError", hStdError);
                        self.set_attr("wShowWindow", wShowWindow);
                        self.set_attr("lpAttributeList", [&]() -> py { py const t = lpAttributeList; return t.truthy() ? t : dict_of({ { S<"handle_list">(), py_list() } }); }());
                        return none();
                    }) },
                { "copy", sp.method("STARTUPINFO", "copy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr_list;
                        attr_list = self.attr("lpAttributeList").method("copy");
                        if (attr_list.contains((S<"handle_list">())))
                        {
                            attr_list.set_item(S<"handle_list">(), (B<"list">())(attr_list[S<"handle_list">()]));
                        }
                        return (G<"subprocess.STARTUPINFO">()).call({  }, { { "dwFlags", self.attr("dwFlags") }, { "hStdInput", self.attr("hStdInput") }, { "hStdOutput", self.attr("hStdOutput") }, { "hStdError", self.attr("hStdError") }, { "wShowWindow", self.attr("wShowWindow") }, { "lpAttributeList", attr_list } });
                    }) }, }));

            define("subprocess", "Handle", sp.make_class("Handle", { B<"int">() }, {
                { "closed", boolean(false) },
                { "Close", sp.method("Handle", "Close", { py_positional("CloseHandle", Mod<"_winapi">().attr("CloseHandle")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py CloseHandle = a[1];
                        if (!self.attr("closed").truthy())
                        {
                            self.set_attr("closed", boolean(true));
                            CloseHandle(self);
                        }
                        return none();
                    }) },
                { "Detach", sp.method("Handle", "Detach", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("closed").truthy())
                        {
                            self.set_attr("closed", boolean(true));
                            return (B<"int">())(self);
                        }
                        py_raise((B<"ValueError">())(S<"already closed">()));
                    }) },
                { "__repr__", sp.method("Handle", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s(%d)">()) % py_tuple({ self.attr("__class__").attr("__name__"), (B<"int">())(self) });
                    }) },
                { "__del__", sp.method("Handle", "__del__", {  }, [](arguments a) -> py { a[0].method("Close"); return none(); }) }, }));

#else
            define("subprocess", "_PIPE_BUF", (B<"getattr">())(Mod<"select">(), S<"PIPE_BUF">(), I<512>()));
            if ((B<"hasattr">())(Mod<"selectors">(), S<"PollSelector">()).truthy())
            {
                define("subprocess", "_PopenSelector", Mod<"selectors">().attr("PollSelector"));
            }
            else
            {
                define("subprocess", "_PopenSelector", Mod<"selectors">().attr("SelectSelector"));
            }
#endif
            // When select or poll has indicated that the file is writable,
            // we can write up to _PIPE_BUF bytes without risk of blocking.
            // POSIX defines PIPE_BUF as >= 512.
            // poll/select have the advantage of not requiring any extra file
            // descriptor, contrarily to epoll/kqueue (also, they require a single
            // syscall).
#if defined(_WIN32)
            define("subprocess", "_active", none());
            define("subprocess", "_cleanup", sp.function("_cleanup", "_cleanup", {  }, [](arguments a) -> py
                {
                    return none();
                }));

#else
            define("subprocess", "_active", py_list());
            define("subprocess", "_cleanup", sp.function("_cleanup", "_cleanup", {  }, [](arguments a) -> py
                {
                    py inst;
                    py res;
                    if ((G<"subprocess._active">()).is_none())
                    {
                        return none();
                    }
                    for (py const& item : iterate{ (G<"subprocess._active">())[B<"slice">()(none(), none(), none())] })
                    {
                        inst = item;
                        res = inst.method("_internal_poll", {  }, { { "_deadstate", Mod<"sys">().attr("maxsize") } });
                        if (!res.is_none())
                        {
                            try
                            {
                                (G<"subprocess._active">()).method("remove", { inst });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, B<"ValueError">()))
                                {
                                    // This can happen if two threads create a new Popen instance.
                                    // It's harmless that it was already removed, so ignore.
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                        }
                    }
                    return none();
                }));

#endif
            define("subprocess", "PIPE", I<-1>());
            define("subprocess", "STDOUT", I<-2>());
            define("subprocess", "DEVNULL", I<-3>());
            // XXX This function is only used by multiprocessing and the test suite,
            // but it's here so that it can be imported when Python is compiled without
            // threads.
            define("subprocess", "_optim_args_from_interpreter_flags", sp.function("_optim_args_from_interpreter_flags", "_optim_args_from_interpreter_flags", {  }, [](arguments a) -> py
                {
                    py args;
                    py value;
                    args = py_list();
                    value = (Mod<"sys">().attr("flags")).attr("optimize");
                    if (value > (I<0>()))
                    {
                        args.method("append", { (S<"-">()) + ((S<"O">()) * value) });
                    }
                    return args;
                }));

            define("subprocess", "_args_from_interpreter_flags", sp.function("_args_from_interpreter_flags", "_args_from_interpreter_flags", {  }, [](arguments a) -> py
                {
                    py arg;
                    py args;
                    py bytes_warning;
                    py dev_mode;
                    py flag;
                    py flag_opt_map;
                    py opt;
                    py v;
                    py value;
                    py warnopts;
                    py xoptions;
                    flag_opt_map = dict_of({ { S<"debug">(), S<"d">() }, { S<"dont_write_bytecode">(), S<"B">() }, { S<"no_site">(), S<"S">() }, { S<"verbose">(), S<"v">() }, { S<"bytes_warning">(), S<"b">() }, { S<"quiet">(), S<"q">() } });
                    // 'inspect': 'i',
                    // 'interactive': 'i',
                    // -O is handled in _optim_args_from_interpreter_flags()
                    args = (G<"subprocess._optim_args_from_interpreter_flags">())();
                    for (py const& item : iterate{ flag_opt_map.method("items") })
                    {
                        auto const items5 = unpack(item, 2);
                        flag = items5[0];
                        opt = items5[1];
                        v = (B<"getattr">())(Mod<"sys">().attr("flags"), flag);
                        if (v > (I<0>()))
                        {
                            args.method("append", { (S<"-">()) + (opt * v) });
                        }
                    }
                    if ((Mod<"sys">().attr("flags")).attr("isolated").truthy())
                    {
                        args.method("append", { S<"-I">() });
                    }
                    else
                    {
                        if ((Mod<"sys">().attr("flags")).attr("ignore_environment").truthy())
                        {
                            args.method("append", { S<"-E">() });
                        }
                        if ((Mod<"sys">().attr("flags")).attr("no_user_site").truthy())
                        {
                            args.method("append", { S<"-s">() });
                        }
                        if ((Mod<"sys">().attr("flags")).attr("safe_path").truthy())
                        {
                            args.method("append", { S<"-P">() });
                        }
                    }
                    // -W options
                    warnopts = (Mod<"sys">().attr("warnoptions"))[B<"slice">()(none(), none(), none())];
                    xoptions = (B<"getattr">())(Mod<"sys">(), S<"_xoptions">(), py_dict());
                    bytes_warning = (Mod<"sys">().attr("flags")).attr("bytes_warning");
                    dev_mode = (Mod<"sys">().attr("flags")).attr("dev_mode");
                    if (bytes_warning > (I<1>()))
                    {
                        warnopts.method("remove", { S<"error::BytesWarning">() });
                    }
                    else if (bytes_warning.truthy())
                    {
                        warnopts.method("remove", { S<"default::BytesWarning">() });
                    }
                    if (dev_mode.truthy())
                    {
                        warnopts.method("remove", { S<"default">() });
                    }
                    for (py const& item : iterate{ warnopts })
                    {
                        opt = item;
                        args.method("append", { (S<"-W">()) + opt });
                    }
                    // -X options
                    if (dev_mode.truthy())
                    {
                        args.method("extend", { py_tuple({ S<"-X">(), S<"dev">() }) });
                    }
                    for (py const& item : iterate{ (B<"sorted">())(xoptions) })
                    {
                        opt = item;
                        if (opt == (S<"dev">()))
                        {
                            // handled above via sys.flags.dev_mode
                            continue;
                        }
                        value = xoptions[opt];
                        if (value.is(boolean(true)))
                        {
                            arg = opt;
                        }
                        else
                        {
                            arg = (S<"%s=%s">()) % py_tuple({ opt, value });
                        }
                        args.method("extend", { py_tuple({ S<"-X">(), arg }) });
                    }
                    return args;
                }));

            define("subprocess", "_text_encoding", sp.function("_text_encoding", "_text_encoding", {  }, [](arguments a) -> py
                {
                    py f;
                    py filename;
                    py stacklevel;
                    if ((Mod<"sys">().attr("flags")).attr("warn_default_encoding").truthy())
                    {
                        f = (Mod<"sys">().attr("_getframe"))();
                        filename = f.attr("f_code").attr("co_filename");
                        stacklevel = I<2>();
                        while (([&]() -> py { f = f.attr("f_back"); return f; }()).truthy())
                        {
                            if (f.attr("f_code").attr("co_filename") != filename)
                            {
                                break;
                            }
                            stacklevel = inplace(NEOS_PY_ADD, stacklevel, I<1>());
                        }
                        (Mod<"warnings">().attr("warn"))(S<"'encoding' argument not specified.">(), B<"EncodingWarning">(), stacklevel);
                    }
                    if ((Mod<"sys">().attr("flags")).attr("utf8_mode").truthy())
                    {
                        return S<"utf-8">();
                    }
                    else
                    {
                        return (Mod<"locale">().attr("getencoding"))();
                    }
                    return none();
                }));

            define("subprocess", "call", sp.function("call", "call", { py_keyword("timeout", none()) }, [](arguments a) -> py
                {
                    py timeout = a[0];
                    py popenargs = a[1];
                    py kwargs = a[2];
                    py p;
                    {
                        py const manager9 = call_ex((G<"subprocess.Popen">()), {  }, popenargs, kwargs);
                        py const exit9 = pyclass::special_method(manager9, "__exit__");
                        py const value9 = pyclass::special_method(manager9, "__enter__")(manager9);
                        p = value9;
                        [[maybe_unused]] int flow9 = 0;
                        [[maybe_unused]] py result9;
                        bool exited9 = false;
                        try
                        {
                            try
                            {
                                result9 = p.method("wait", {  }, { { "timeout", timeout } });
                                flow9 = 1;
                                goto finally9;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                {
                                    p.method("kill");
                                    // We don't call p.wait() again as p.__exit__ does that for us.
                                    py_reraise(handler10.exception);
                                }
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
                    return none();
                }, true, true));

            define("subprocess", "check_call", sp.function("check_call", "check_call", {  }, [](arguments a) -> py
                {
                    py popenargs = a[0];
                    py kwargs = a[1];
                    py cmd;
                    py retcode;
                    retcode = call_ex((G<"subprocess.call">()), {  }, popenargs, kwargs);
                    if (retcode.truthy())
                    {
                        cmd = kwargs.method("get", { S<"args">() });
                        if (cmd.is_none())
                        {
                            cmd = popenargs[I<0>()];
                        }
                        py_raise((G<"subprocess.CalledProcessError">())(retcode, cmd));
                    }
                    return I<0>();
                }, true, true));

            define("subprocess", "check_output", sp.function("check_output", "check_output", { py_keyword("timeout", none()) }, [](arguments a) -> py
                {
                    py timeout = a[0];
                    py popenargs = a[1];
                    py kwargs = a[2];
                    py empty;
                    py kw;
                    for (py const& item : iterate{ py_tuple({ S<"stdout">(), S<"check">() }) })
                    {
                        kw = item;
                        if (kwargs.contains(kw))
                        {
                            py_raise((B<"ValueError">())(S(kw.str() + " argument not allowed, it will be overridden.")));
                        }
                    }
                    if ((kwargs.contains((S<"input">())) && kwargs[S<"input">()].is_none()))
                    {
                        // Explicitly passing input=None was previously equivalent to passing an
                        // empty string. That is maintained here for backwards compatibility.
                        if ((kwargs.method("get", { S<"universal_newlines">() }).truthy() || kwargs.method("get", { S<"text">() }).truthy() || kwargs.method("get", { S<"encoding">() }).truthy() || kwargs.method("get", { S<"errors">() }).truthy()))
                        {
                            empty = S<"">();
                        }
                        else
                        {
                            empty = py{ make_bytes(std::string{ "", 0 }) };
                        }
                        kwargs.set_item(S<"input">(), empty);
                    }
                    return call_ex((G<"subprocess.run">()), {  }, popenargs, kwargs, { { "stdout", G<"subprocess.PIPE">() }, { "timeout", timeout }, { "check", boolean(true) } }).attr("stdout");
                }, true, true));

            define("subprocess", "CompletedProcess", sp.make_class("CompletedProcess", { B<"object">() }, {
                { "__init__", sp.method("CompletedProcess", "__init__", { py_positional("args"), py_positional("returncode"), py_positional("stdout", none()), py_positional("stderr", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py returncode = a[2];
                        py stdout_ = a[3];
                        py stderr_ = a[4];
                        self.set_attr("args", args);
                        self.set_attr("returncode", returncode);
                        self.set_attr("stdout", stdout_);
                        self.set_attr("stderr", stderr_);
                        return none();
                    }) },
                { "__repr__", sp.method("CompletedProcess", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        args = py_list({ (S<"args={!r}">()).method("format", { self.attr("args") }), (S<"returncode={!r}">()).method("format", { self.attr("returncode") }) });
                        if (!self.attr("stdout").is_none())
                        {
                            args.method("append", { (S<"stdout={!r}">()).method("format", { self.attr("stdout") }) });
                        }
                        if (!self.attr("stderr").is_none())
                        {
                            args.method("append", { (S<"stderr={!r}">()).method("format", { self.attr("stderr") }) });
                        }
                        return (S<"{}({})">()).method("format", { (B<"type">())(self).attr("__name__"), (S<", ">()).method("join", { args }) });
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) },
                { "check_returncode", sp.method("CompletedProcess", "check_returncode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("returncode").truthy())
                        {
                            py_raise((G<"subprocess.CalledProcessError">())(self.attr("returncode"), self.attr("args"), self.attr("stdout"), self.attr("stderr")));
                        }
                        return none();
                    }) }, }));

            define("subprocess", "run", sp.function("run", "run", { py_keyword("input", none()), py_keyword("capture_output", boolean(false)), py_keyword("timeout", none()), py_keyword("check", boolean(false)) }, [](arguments a) -> py
                {
                    py input = a[0];
                    py capture_output = a[1];
                    py timeout = a[2];
                    py check = a[3];
                    py popenargs = a[4];
                    py kwargs = a[5];
                    py exc;
                    py process;
                    py retcode;
                    py stderr_;
                    py stdout_;
                    if (!input.is_none())
                    {
                        if (!kwargs.method("get", { S<"stdin">() }).is_none())
                        {
                            py_raise((B<"ValueError">())(S<"stdin and input arguments may not both be used.">()));
                        }
                        kwargs.set_item(S<"stdin">(), G<"subprocess.PIPE">());
                    }
                    if (capture_output.truthy())
                    {
                        if (((!kwargs.method("get", { S<"stdout">() }).is_none()) || (!kwargs.method("get", { S<"stderr">() }).is_none())))
                        {
                            py_raise((B<"ValueError">())(S<"stdout and stderr arguments may not be used with capture_output.">()));
                        }
                        kwargs.set_item(S<"stdout">(), G<"subprocess.PIPE">());
                        kwargs.set_item(S<"stderr">(), G<"subprocess.PIPE">());
                    }
                    {
                        py const manager12 = call_ex((G<"subprocess.Popen">()), {  }, popenargs, kwargs);
                        py const exit12 = pyclass::special_method(manager12, "__exit__");
                        py const value12 = pyclass::special_method(manager12, "__enter__")(manager12);
                        process = value12;
                        [[maybe_unused]] int flow12 = 0;
                        [[maybe_unused]] py result12;
                        bool exited12 = false;
                        try
                        {
                            try
                            {
                                auto const items14 = unpack(process.method("communicate", { input }, { { "timeout", timeout } }), 2);
                                stdout_ = items14[0];
                                stderr_ = items14[1];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler13;
                                if (matches(handler13.exception, G<"subprocess.TimeoutExpired">()))
                                {
                                    exc = handler13.exception;
                                    process.method("kill");
                    #if defined(_WIN32)
                                    {
                                        // Windows accumulates the output in a single blocking
                                        // read() call run on child threads, with the timeout
                                        // being done in a join() on those threads.  communicate()
                                        // _after_ kill() is required to collect that and add it
                                        // to the exception.
                                        auto const items15 = unpack(process.method("communicate"), 2);
                                        exc.set_attr("stdout", items15[0]);
                                        exc.set_attr("stderr", items15[1]);
                                    }
                    #else
                                    {
                                        // POSIX _communicate already populated the output so
                                        // far into the TimeoutExpired exception.
                                        process.method("wait");
                                    }
                    #endif
                                    py_reraise(handler13.exception);
                                }
                                else
                                {
                                    process.method("kill");
                                    // We don't call process.wait() as .__exit__ does that for us.
                                    py_reraise(handler13.exception);
                                }
                            }
                            retcode = process.method("poll");
                            if ((check.truthy() && retcode.truthy()))
                            {
                                py_raise((G<"subprocess.CalledProcessError">()).call({ retcode, process.attr("args") }, { { "output", stdout_ }, { "stderr", stderr_ } }));
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
                            return result12;
                    }
                    return (G<"subprocess.CompletedProcess">())(process.attr("args"), retcode, stdout_, stderr_);
                }, true, true));

            define("subprocess", "list2cmdline", sp.function("list2cmdline", "list2cmdline", { py_positional("seq") }, [](arguments a) -> py
                {
                    py seq = a[0];
                    py arg;
                    py bs_buf;
                    py c;
                    py needquote;
                    py result;
                    // See
                    // http://msdn.microsoft.com/en-us/library/17w5ykft.aspx
                    // or search http://msdn.microsoft.com for
                    // "Parsing C++ Command-Line Arguments"
                    result = py_list();
                    needquote = boolean(false);
                    for (py const& item : iterate{ (B<"map">())(Mod<"os">().attr("fsdecode"), seq) })
                    {
                        arg = item;
                        bs_buf = py_list();
                        // Add a space to separate this argument from the others
                        if (result.truthy())
                        {
                            result.method("append", { S<" ">() });
                        }
                        needquote = [&]() -> py { py const t = boolean(arg.contains((S<" ">()))); return t.truthy() ? t : [&]() -> py { py const t = boolean(arg.contains((S<"\t">()))); return t.truthy() ? t : boolean(!arg.truthy()); }(); }();
                        if (needquote.truthy())
                        {
                            result.method("append", { S<"\"">() });
                        }
                        for (py const& item : iterate{ arg })
                        {
                            c = item;
                            if (c == (S<"\\">()))
                            {
                                // Don't know if we need to double yet.
                                bs_buf.method("append", { c });
                            }
                            else if (c == (S<"\"">()))
                            {
                                // Double backslashes.
                                result.method("append", { ((S<"\\">()) * py{ bs_buf.size() }) * (I<2>()) });
                                bs_buf = py_list();
                                result.method("append", { S<"\\\"">() });
                            }
                            else
                            {
                                // Normal char
                                if (bs_buf.truthy())
                                {
                                    result.method("extend", { bs_buf });
                                    bs_buf = py_list();
                                }
                                result.method("append", { c });
                            }
                        }
                        // Add remaining backslashes, if any.
                        if (bs_buf.truthy())
                        {
                            result.method("extend", { bs_buf });
                        }
                        if (needquote.truthy())
                        {
                            result.method("extend", { bs_buf });
                            result.method("append", { S<"\"">() });
                        }
                    }
                    return (S<"">()).method("join", { result });
                }));

            // Various tools for executing commands and looking at their output and status.
            // 
            define("subprocess", "getstatusoutput", sp.function("getstatusoutput", "getstatusoutput", { py_positional("cmd"), py_keyword("encoding", none()), py_keyword("errors", none()) }, [](arguments a) -> py
                {
                    py cmd = a[0];
                    py encoding = a[1];
                    py errors = a[2];
                    py data;
                    py ex;
                    py exitcode;
                    try
                    {
                        data = (G<"subprocess.check_output">()).call({ cmd }, { { "shell", boolean(true) }, { "text", boolean(true) }, { "stderr", G<"subprocess.STDOUT">() }, { "encoding", encoding }, { "errors", errors } });
                        exitcode = I<0>();
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler18;
                        if (matches(handler18.exception, G<"subprocess.CalledProcessError">()))
                        {
                            ex = handler18.exception;
                            data = ex.attr("output");
                            exitcode = ex.attr("returncode");
                        }
                        else
                            py_reraise(handler18.exception);
                    }
                    if (data[B<"slice">()(I<-1>(), none(), none())] == (S<"\n">()))
                    {
                        data = data[B<"slice">()(none(), I<-1>(), none())];
                    }
                    return py_tuple({ exitcode, data });
                }));

            define("subprocess", "getoutput", sp.function("getoutput", "getoutput", { py_positional("cmd"), py_keyword("encoding", none()), py_keyword("errors", none()) }, [](arguments a) -> py
                {
                    py cmd = a[0];
                    py encoding = a[1];
                    py errors = a[2];
                    return (G<"subprocess.getstatusoutput">()).call({ cmd }, { { "encoding", encoding }, { "errors", errors } })[I<1>()];
                }));

            define("subprocess", "_use_posix_spawn", sp.function("_use_posix_spawn", "_use_posix_spawn", {  }, [](arguments a) -> py
                {
                    py _env;
                    py libc;
                    py parts;
                    py ver;
                    py version;
                    if (((G<"subprocess._mswindows">()).truthy() || (!has_attr(Mod<"os">(), "posix_spawn"))))
                    {
                        // os.posix_spawn() is not available
                        return boolean(false);
                    }
                    if (py_tuple({ S<"0">(), S<"1">() }).contains(([&]() -> py { _env = (Mod<"os">().attr("environ")).method("get", { S<"_PYTHON_SUBPROCESS_USE_POSIX_SPAWN">() }); return _env; }())))
                    {
                        return (B<"bool">())((B<"int">())(_env));
                    }
                    if (py_tuple({ S<"darwin">(), S<"sunos5">() }).contains((Mod<"sys">().attr("platform"))))
                    {
                        // posix_spawn() is a syscall on both macOS and Solaris,
                        // and properly reports errors
                        return boolean(true);
                    }
                    // Check libc name and runtime libc version
                    try
                    {
                        ver = (Mod<"os">().attr("confstr"))(S<"CS_GNU_LIBC_VERSION">());
                        // parse 'glibc 2.28' as ('glibc', (2, 28))
                        parts = ver.method("split", {  }, { { "maxsplit", I<1>() } });
                        if (py{ parts.size() } != (I<2>()))
                        {
                            // reject unknown format
                            py_raise(B<"ValueError">());
                        }
                        libc = parts[I<0>()];
                        version = (B<"tuple">())((B<"map">())(B<"int">(), parts[I<1>()].method("split", { S<".">() })));
                        if ((((Mod<"sys">().attr("platform")) == (S<"linux">())) && (libc == (S<"glibc">())) && (version >= py_tuple({ I<2>(), I<24>() }))))
                        {
                            // glibc 2.24 has a new Linux posix_spawn implementation using vfork
                            // which properly reports errors to the parent process.
                            return boolean(true);
                        }
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler19;
                        // Note: Don't use the implementation in earlier glibc because it doesn't
                        // use vfork (even if glibc 2.26 added a pipe to properly report errors
                        // to the parent process).
                        if (matches(handler19.exception, py_tuple({ B<"AttributeError">(), B<"ValueError">(), B<"OSError">() })))
                        {
                            // os.confstr() or CS_GNU_LIBC_VERSION value not available
                        }
                        else
                            py_reraise(handler19.exception);
                    }
                    // By default, assume that posix_spawn() does not properly report errors.
                    return boolean(false);
                }));

            // These are primarily fail-safe knobs for negatives. A True value does not
            // guarantee the given libc/syscall API will be used.
            define("subprocess", "_USE_POSIX_SPAWN", (G<"subprocess._use_posix_spawn">())());
            define("subprocess", "_USE_VFORK", boolean(true));
            define("subprocess", "_HAVE_POSIX_SPAWN_CLOSEFROM", (B<"hasattr">())(Mod<"os">(), S<"POSIX_SPAWN_CLOSEFROM">()));
                // 
                // Windows methods
                // 
                // 
                // POSIX methods
                // 
            define("subprocess", "Popen", sp.make_class("Popen", {  }, {
                { "_child_created", boolean(false) },
                { "__init__", sp.method("Popen", "__init__", { py_positional("args"), py_positional("bufsize", I<-1>()), py_positional("executable", none()), py_positional("stdin", none()), py_positional("stdout", none()), py_positional("stderr", none()), py_positional("preexec_fn", none()), py_positional("close_fds", boolean(true)), py_positional("shell", boolean(false)), py_positional("cwd", none()), py_positional("env", none()), py_positional("universal_newlines", none()), py_positional("startupinfo", none()), py_positional("creationflags", I<0>()), py_positional("restore_signals", boolean(true)), py_positional("start_new_session", boolean(false)), py_positional("pass_fds", py_tuple()), py_keyword("user", none()), py_keyword("group", none()), py_keyword("extra_groups", none()), py_keyword("encoding", none()), py_keyword("errors", none()), py_keyword("text", none()), py_keyword("umask", I<-1>()), py_keyword("pipesize", I<-1>()), py_keyword("process_group", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py bufsize = a[2];
                        py executable = a[3];
                        py stdin_ = a[4];
                        py stdout_ = a[5];
                        py stderr_ = a[6];
                        py preexec_fn = a[7];
                        py close_fds = a[8];
                        py shell = a[9];
                        py cwd = a[10];
                        py env = a[11];
                        py universal_newlines = a[12];
                        py startupinfo = a[13];
                        py creationflags = a[14];
                        py restore_signals = a[15];
                        py start_new_session = a[16];
                        py pass_fds = a[17];
                        py user = a[18];
                        py group = a[19];
                        py extra_groups = a[20];
                        py encoding = a[21];
                        py errors = a[22];
                        py text = a[23];
                        py umask = a[24];
                        py pipesize = a[25];
                        py process_group = a[26];
                        py c2pread;
                        py c2pwrite;
                        py errread;
                        py errwrite;
                        py extra_group;
                        py f;
                        py fd;
                        py gid;
                        py gid_check;
                        py gids;
                        py grp;
                        py line_buffering;
                        py p2cread;
                        py p2cwrite;
                        py pwd;
                        py to_close;
                        py uid;
                        #if !(!defined(_WIN32))
                        {
                            py_raise((B<"OSError">())(Mod<"errno">().attr("ENOTSUP"), S((Mod<"sys">().attr("platform")).str() + " does not support processes.")));
                        }
                        #endif
                        (G<"subprocess._cleanup">())();
                        // Held while anything is calling waitpid before returncode has been
                        // updated to prevent clobbering returncode if wait() or poll() are
                        // called from multiple threads at once.  After acquiring the lock,
                        // code must re-check self.returncode to see if another thread just
                        // finished a waitpid() call.
                        self.set_attr("_waitpid_lock", (Mod<"threading">().attr("Lock"))());
                        self.set_attr("_input", none());
                        self.set_attr("_communication_started", boolean(false));
                        if (bufsize.is_none())
                        {
                            bufsize = I<-1>();
                        }
                        if (!isinstance(bufsize, B<"int">()))
                        {
                            py_raise((B<"TypeError">())(S<"bufsize must be an integer">()));
                        }
                        if (stdout_.is((G<"subprocess.STDOUT">())))
                        {
                            py_raise((B<"ValueError">())(S<"STDOUT can only be used for stderr">()));
                        }
                        if (pipesize.is_none())
                        {
                            pipesize = I<-1>();
                        }
                        if (!isinstance(pipesize, B<"int">()))
                        {
                            py_raise((B<"TypeError">())(S<"pipesize must be an integer">()));
                        }
                        #if defined(_WIN32)
                        {
                            if (!preexec_fn.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"preexec_fn is not supported on Windows platforms">()));
                            }
                        }
                        #else
                        {
                            // POSIX
                            if ((pass_fds.truthy() && (!close_fds.truthy())))
                            {
                                (Mod<"warnings">().attr("warn"))(S<"pass_fds overriding close_fds.">(), B<"RuntimeWarning">());
                                close_fds = boolean(true);
                            }
                            if (!startupinfo.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"startupinfo is only supported on Windows platforms">()));
                            }
                            if (creationflags != (I<0>()))
                            {
                                py_raise((B<"ValueError">())(S<"creationflags is only supported on Windows platforms">()));
                            }
                        }
                        #endif
                        self.set_attr("args", args);
                        self.set_attr("stdin", none());
                        self.set_attr("stdout", none());
                        self.set_attr("stderr", none());
                        self.set_attr("pid", none());
                        self.set_attr("returncode", none());
                        self.set_attr("encoding", encoding);
                        self.set_attr("errors", errors);
                        self.set_attr("pipesize", pipesize);
                        // Validate the combinations of text and universal_newlines
                        if (((!text.is_none()) && (!universal_newlines.is_none()) && ((B<"bool">())(universal_newlines) != (B<"bool">())(text))))
                        {
                            py_raise((G<"subprocess.SubprocessError">())(S<"Cannot disambiguate when both text and universal_newlines are supplied but different. Pass one or the other.">()));
                        }
                        self.set_attr("text_mode", [&]() -> py { py const t = encoding; return t.truthy() ? t : [&]() -> py { py const t = errors; return t.truthy() ? t : [&]() -> py { py const t = text; return t.truthy() ? t : universal_newlines; }(); }(); }());
                        if ((self.attr("text_mode").truthy() && encoding.is_none()))
                        {
                            py const value20 = (G<"subprocess._text_encoding">())();
                            self.set_attr("encoding", value20);
                            encoding = value20;
                        }
                        // How long to resume waiting on a child after the first ^C.
                        // There is no right value for this.  The purpose is to be polite
                        // yet remain good for interactive users trying to exit a tool.
                        self.set_attr("_sigint_wait_secs", F<0.25>());
                        self.set_attr("_closed_child_pipe_fds", boolean(false));
                        if (self.attr("text_mode").truthy())
                        {
                            if (bufsize == (I<1>()))
                            {
                                line_buffering = boolean(true);
                                // Use the default buffer size for the underlying binary streams
                                // since they don't support line buffering.
                                bufsize = I<-1>();
                            }
                            else
                            {
                                line_buffering = boolean(false);
                            }
                        }
                        if (process_group.is_none())
                        {
                            process_group = I<-1>();
                        }
                        gid = none();
                        if (!group.is_none())
                        {
                            if (!has_attr(Mod<"os">(), "setregid"))
                            {
                                py_raise((B<"ValueError">())(S<"The 'group' parameter is not supported on the current platform">()));
                            }
                            else if (isinstance(group, B<"str">()))
                            {
                                try
                                {
                                    grp = module_of("grp");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler21;
                                    if (matches(handler21.exception, B<"ImportError">()))
                                    {
                                        py_raise((B<"ValueError">())(S<"The group parameter cannot be a string on systems without the grp module">()));
                                    }
                                    else
                                        py_reraise(handler21.exception);
                                }
                                gid = grp.method("getgrnam", { group }).attr("gr_gid");
                            }
                            else if (isinstance(group, B<"int">()))
                            {
                                gid = group;
                            }
                            else
                            {
                                py_raise((B<"TypeError">())((S<"Group must be a string or an integer, not {}">()).method("format", { (B<"type">())(group) })));
                            }
                            if (gid < (I<0>()))
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "Group ID cannot be negative, got " } + gid.str())));
                            }
                        }
                        gids = none();
                        if (!extra_groups.is_none())
                        {
                            if (!has_attr(Mod<"os">(), "setgroups"))
                            {
                                py_raise((B<"ValueError">())(S<"The 'extra_groups' parameter is not supported on the current platform">()));
                            }
                            else if (isinstance(extra_groups, B<"str">()))
                            {
                                py_raise((B<"ValueError">())(S<"Groups must be a list, not a string">()));
                            }
                            gids = py_list();
                            for (py const& item : iterate{ extra_groups })
                            {
                                extra_group = item;
                                if (isinstance(extra_group, B<"str">()))
                                {
                                    try
                                    {
                                        grp = module_of("grp");
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler23;
                                        if (matches(handler23.exception, B<"ImportError">()))
                                        {
                                            py_raise((B<"ValueError">())(S<"Items in extra_groups cannot be strings on systems without the grp module">()));
                                        }
                                        else
                                            py_reraise(handler23.exception);
                                    }
                                    gids.method("append", { grp.method("getgrnam", { extra_group }).attr("gr_gid") });
                                }
                                else if (isinstance(extra_group, B<"int">()))
                                {
                                    gids.method("append", { extra_group });
                                }
                                else
                                {
                                    py_raise((B<"TypeError">())((S<"Items in extra_groups must be a string or integer, not {}">()).method("format", { (B<"type">())(extra_group) })));
                                }
                            }
                            // make sure that the gids are all positive here so we can do less
                            // checking in the C code
                            for (py const& item : iterate{ gids })
                            {
                                gid_check = item;
                                if (gid_check < (I<0>()))
                                {
                                    py_raise((B<"ValueError">())(S(std::string{ "Group ID cannot be negative, got " } + gid_check.str())));
                                }
                            }
                        }
                        uid = none();
                        if (!user.is_none())
                        {
                            if (!has_attr(Mod<"os">(), "setreuid"))
                            {
                                py_raise((B<"ValueError">())(S<"The 'user' parameter is not supported on the current platform">()));
                            }
                            else if (isinstance(user, B<"str">()))
                            {
                                try
                                {
                                    pwd = module_of("pwd");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler25;
                                    if (matches(handler25.exception, B<"ImportError">()))
                                    {
                                        py_raise((B<"ValueError">())(S<"The user parameter cannot be a string on systems without the pwd module">()));
                                    }
                                    else
                                        py_reraise(handler25.exception);
                                }
                                uid = pwd.method("getpwnam", { user }).attr("pw_uid");
                            }
                            else if (isinstance(user, B<"int">()))
                            {
                                uid = user;
                            }
                            else
                            {
                                py_raise((B<"TypeError">())(S<"User must be a string or an integer">()));
                            }
                            if (uid < (I<0>()))
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "User ID cannot be negative, got " } + uid.str())));
                            }
                        }
                        // Input and output objects. The general principle is like
                        // this:
                        // 
                        // Parent                   Child
                        // ------                   -----
                        // p2cwrite   ---stdin--->  p2cread
                        // c2pread    <--stdout---  c2pwrite
                        // errread    <--stderr---  errwrite
                        // 
                        // On POSIX, the child objects are file descriptors.  On
                        // Windows, these are Windows file handles.  The parent objects
                        // are file descriptors on both platforms.  The parent objects
                        // are -1 when not using PIPEs. The child objects are -1
                        // when not redirecting.
                        auto const items26 = unpack(self.method("_get_handles", { stdin_, stdout_, stderr_ }), 6);
                        p2cread = items26[0];
                        p2cwrite = items26[1];
                        c2pread = items26[2];
                        c2pwrite = items26[3];
                        errread = items26[4];
                        errwrite = items26[5];
                        // From here on, raising exceptions may cause file descriptor leakage
                        // We wrap OS handles *before* launching the child, otherwise a
                        // quickly terminating child could make our fds unwrappable
                        // (see #8458).
                        #if defined(_WIN32)
                        {
                            if (p2cwrite != (I<-1>()))
                            {
                                p2cwrite = (Mod<"msvcrt">().attr("open_osfhandle"))(p2cwrite.method("Detach"), I<0>());
                            }
                            if (c2pread != (I<-1>()))
                            {
                                c2pread = (Mod<"msvcrt">().attr("open_osfhandle"))(c2pread.method("Detach"), I<0>());
                            }
                            if (errread != (I<-1>()))
                            {
                                errread = (Mod<"msvcrt">().attr("open_osfhandle"))(errread.method("Detach"), I<0>());
                            }
                        }
                        #endif
                        try
                        {
                            if (p2cwrite != (I<-1>()))
                            {
                                self.set_attr("stdin", (Mod<"io">().attr("open"))(p2cwrite, S<"wb">(), bufsize));
                                if (self.attr("text_mode").truthy())
                                {
                                    self.set_attr("stdin", (Mod<"io">().attr("TextIOWrapper")).call({ self.attr("stdin") }, { { "write_through", boolean(true) }, { "line_buffering", line_buffering }, { "encoding", encoding }, { "errors", errors } }));
                                }
                            }
                            if (c2pread != (I<-1>()))
                            {
                                self.set_attr("stdout", (Mod<"io">().attr("open"))(c2pread, S<"rb">(), bufsize));
                                if (self.attr("text_mode").truthy())
                                {
                                    self.set_attr("stdout", (Mod<"io">().attr("TextIOWrapper")).call({ self.attr("stdout") }, { { "encoding", encoding }, { "errors", errors } }));
                                }
                            }
                            if (errread != (I<-1>()))
                            {
                                self.set_attr("stderr", (Mod<"io">().attr("open"))(errread, S<"rb">(), bufsize));
                                if (self.attr("text_mode").truthy())
                                {
                                    self.set_attr("stderr", (Mod<"io">().attr("TextIOWrapper")).call({ self.attr("stderr") }, { { "encoding", encoding }, { "errors", errors } }));
                                }
                            }
                            self.method("_execute_child", { args, executable, preexec_fn, close_fds, pass_fds, cwd, env, startupinfo, creationflags, shell, p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite, restore_signals, gid, gids, uid, umask, start_new_session, process_group });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler27;
                            {
                                // Cleanup if the child failed starting.
                                for (py const& item : iterate{ (B<"filter">())(none(), py_tuple({ self.attr("stdin"), self.attr("stdout"), self.attr("stderr") })) })
                                {
                                    f = item;
                                    try
                                    {
                                        f.method("close");
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler29;
                                        if (matches(handler29.exception, B<"OSError">()))
                                        {
                                        }
                                        else
                                            py_reraise(handler29.exception);
                                    }
                                }
                                if (!self.attr("_closed_child_pipe_fds").truthy())
                                {
                                    to_close = py_list();
                                    if (stdin_ == (G<"subprocess.PIPE">()))
                                    {
                                        to_close.method("append", { p2cread });
                                    }
                                    if (stdout_ == (G<"subprocess.PIPE">()))
                                    {
                                        to_close.method("append", { c2pwrite });
                                    }
                                    if (stderr_ == (G<"subprocess.PIPE">()))
                                    {
                                        to_close.method("append", { errwrite });
                                    }
                                    if (has_attr(self, "_devnull"))
                                    {
                                        to_close.method("append", { self.attr("_devnull") });
                                    }
                                    for (py const& item : iterate{ to_close })
                                    {
                                        fd = item;
                                        try
                                        {
                                            if (((G<"subprocess._mswindows">()).truthy() && isinstance(fd, G<"subprocess.Handle">())))
                                            {
                                                fd.method("Close");
                                            }
                                            else
                                            {
                                                (Mod<"os">().attr("close"))(fd);
                                            }
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler31;
                                            if (matches(handler31.exception, B<"OSError">()))
                                            {
                                            }
                                            else
                                                py_reraise(handler31.exception);
                                        }
                                    }
                                }
                                py_reraise(handler27.exception);
                            }
                        }
                        return none();
                    }) },
                { "__repr__", sp.method("Popen", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj_repr;
                        obj_repr = S(std::string{ "<" } + self.attr("__class__").attr("__name__").str() + ": returncode: " + self.attr("returncode").str() + " args: " + self.attr("args").repr() + ">");
                        if (py{ obj_repr.size() } > (I<80>()))
                        {
                            obj_repr = obj_repr[B<"slice">()(none(), I<76>(), none())] + (S<"...>">());
                        }
                        return obj_repr;
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) },
                { "universal_newlines", B<"property">()(sp.method("Popen", "universal_newlines", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("text_mode");
                    }), sp.method("Popen", "universal_newlines", { py_positional("universal_newlines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py universal_newlines = a[1];
                        self.set_attr("text_mode", (B<"bool">())(universal_newlines));
                        return none();
                    })) },
                { "_translate_newlines", sp.method("Popen", "_translate_newlines", { py_positional("data"), py_positional("encoding"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py encoding = a[2];
                        py errors = a[3];
                        data = data.method("decode", { encoding, errors });
                        return data.method("replace", { S<"\r\n">(), S<"\n">() }).method("replace", { S<"\r">(), S<"\n">() });
                    }) },
                { "__enter__", sp.method("Popen", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", sp.method("Popen", "__exit__", { py_positional("exc_type"), py_positional("value"), py_positional("traceback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py value = a[2];
                        py traceback = a[3];
                        if (self.attr("stdout").truthy())
                        {
                            self.attr("stdout").method("close");
                        }
                        if (self.attr("stderr").truthy())
                        {
                            self.attr("stderr").method("close");
                        }
                        {
                            std::optional<py> pending32;
                            [[maybe_unused]] int flow32 = 0;
                            [[maybe_unused]] py result32;
                            try
                            {
                                if (self.attr("stdin").truthy())
                                {
                                    self.attr("stdin").method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending32 = py_take_exception();
                            }
                            finally32:
                            if (exc_type == (B<"KeyboardInterrupt">()))
                            {
                                // https://bugs.python.org/issue25942
                                // In the case of a KeyboardInterrupt we assume the SIGINT
                                // was also already sent to our child processes.  We can't
                                // block indefinitely as that is not user friendly.
                                // If we have not already waited a brief amount of time in
                                // an interrupted .wait() or .communicate() call, do so here
                                // for consistency.
                                if (self.attr("_sigint_wait_secs") > (I<0>()))
                                {
                                    try
                                    {
                                        self.method("_wait", {  }, { { "timeout", self.attr("_sigint_wait_secs") } });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler33;
                                        if (matches(handler33.exception, G<"subprocess.TimeoutExpired">()))
                                        {
                                        }
                                        else
                                            py_reraise(handler33.exception);
                                    }
                                }
                                self.set_attr("_sigint_wait_secs", I<0>());
                                return none();
                            }
                            // Wait for the process to terminate, to avoid zombies.
                            self.method("wait");
                            if (pending32)
                                py_reraise(*pending32);
                            if (flow32 == 1)
                                return result32;
                        }
                        return none();
                    }) },
                { "__del__", sp.method("Popen", "__del__", { py_positional("_maxsize", Mod<"sys">().attr("maxsize")), py_positional("_warn", Mod<"warnings">().attr("warn")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _maxsize = a[1];
                        py _warn = a[2];
                        if (!self.attr("_child_created").truthy())
                        {
                            // We didn't get to successfully create a child process.
                            return none();
                        }
                        if (self.attr("returncode").is_none())
                        {
                            // Not reading subprocess exit status creates a zombie process which
                            // is only destroyed at the parent python process exit
                            _warn.call({ (S<"subprocess %s is still running">()) % self.attr("pid"), B<"ResourceWarning">() }, { { "source", self } });
                        }
                        // In case the child hasn't been waited on, check if it's done.
                        self.method("_internal_poll", {  }, { { "_deadstate", _maxsize } });
                        if ((self.attr("returncode").is_none() && (!(G<"subprocess._active">()).is_none())))
                        {
                            // Child is still running, keep us alive until we can wait on it.
                            (G<"subprocess._active">()).method("append", { self });
                        }
                        return none();
                    }) },
                { "_get_devnull", sp.method("Popen", "_get_devnull", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!has_attr(self, "_devnull"))
                        {
                            self.set_attr("_devnull", (Mod<"os">().attr("open"))(Mod<"os">().attr("devnull"), Mod<"os">().attr("O_RDWR")));
                        }
                        return self.attr("_devnull");
                    }) },
                { "_stdin_write", sp.method("Popen", "_stdin_write", { py_positional("input") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py exc;
                        if (input.truthy())
                        {
                            try
                            {
                                self.attr("stdin").method("write", { input });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler34;
                                if (matches(handler34.exception, B<"BrokenPipeError">()))
                                {
                                }
                                else if (matches(handler34.exception, B<"OSError">()))
                                {
                                    exc = handler34.exception;
                                    if (exc.attr("errno") == (Mod<"errno">().attr("EINVAL")))
                                    {
                                        // bpo-19612, bpo-30418: On Windows, stdin.write() fails
                                        // with EINVAL if the child process exited or if the child
                                        // process is still running but closed the pipe.
                                    }
                                    else
                                    {
                                        py_reraise(handler34.exception);
                                    }
                                }
                                else
                                    py_reraise(handler34.exception);
                            }
                        }
                        try
                        {
                            self.attr("stdin").method("close");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler35;
                            if (matches(handler35.exception, B<"BrokenPipeError">()))
                            {
                            }
                            else if (matches(handler35.exception, B<"OSError">()))
                            {
                                exc = handler35.exception;
                                if (exc.attr("errno") == (Mod<"errno">().attr("EINVAL")))
                                {
                                }
                                else
                                {
                                    py_reraise(handler35.exception);
                                }
                            }
                            else
                                py_reraise(handler35.exception);
                        }
                        return none();
                    }) },
                { "communicate", sp.method("Popen", "communicate", { py_positional("input", none()), py_positional("timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py timeout = a[2];
                        py endtime;
                        py exc;
                        py sigint_timeout;
                        py stderr_;
                        py stdout_;
                        py sts;
                        if ((self.attr("_communication_started").truthy() && input.truthy()))
                        {
                            py_raise((B<"ValueError">())(S<"Cannot send input after starting communication">()));
                        }
                        // Optimization: If we are not worried about timeouts, we haven't
                        // started communicating, and we have one or zero pipes, using select()
                        // or threads is unnecessary.
                        if ((timeout.is_none() && (!self.attr("_communication_started").truthy()) && (py_list({ self.attr("stdin"), self.attr("stdout"), self.attr("stderr") }).method("count", { none() }) >= (I<2>()))))
                        {
                            stdout_ = none();
                            stderr_ = none();
                            if (self.attr("stdin").truthy())
                            {
                                self.method("_stdin_write", { input });
                            }
                            else if (self.attr("stdout").truthy())
                            {
                                stdout_ = self.attr("stdout").method("read");
                                self.attr("stdout").method("close");
                            }
                            else if (self.attr("stderr").truthy())
                            {
                                stderr_ = self.attr("stderr").method("read");
                                self.attr("stderr").method("close");
                            }
                            self.method("wait");
                        }
                        else
                        {
                            if (!timeout.is_none())
                            {
                                endtime = (Mod<"time">().attr("monotonic"))() + timeout;
                            }
                            else
                            {
                                endtime = none();
                            }
                            {
                                std::optional<py> pending36;
                                [[maybe_unused]] int flow36 = 0;
                                [[maybe_unused]] py result36;
                                try
                                {
                                    try
                                    {
                                        auto const items37 = unpack(self.method("_communicate", { input, endtime, timeout }), 2);
                                        stdout_ = items37[0];
                                        stderr_ = items37[1];
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler36;
                                        if (matches(handler36.exception, B<"KeyboardInterrupt">()))
                                        {
                                            // https://bugs.python.org/issue25942
                                            // See the detailed comment in .wait().
                                            if (!timeout.is_none())
                                            {
                                                sigint_timeout = (B<"min">())(self.attr("_sigint_wait_secs"), self.method("_remaining_time", { endtime }));
                                            }
                                            else
                                            {
                                                sigint_timeout = self.attr("_sigint_wait_secs");
                                            }
                                            self.set_attr("_sigint_wait_secs", I<0>());
                                            try
                                            {
                                                self.method("_wait", {  }, { { "timeout", sigint_timeout } });
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler38;
                                                if (matches(handler38.exception, G<"subprocess.TimeoutExpired">()))
                                                {
                                                }
                                                else
                                                    py_reraise(handler38.exception);
                                            }
                                            py_reraise(handler36.exception);
                                        }
                                        else
                                            py_reraise(handler36.exception);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending36 = py_take_exception();
                                }
                                finally36:
                                self.set_attr("_communication_started", boolean(true));
                                if (pending36)
                                    py_reraise(*pending36);
                                if (flow36 == 1)
                                    return result36;
                            }
                            try
                            {
                                sts = self.method("wait", {  }, { { "timeout", self.method("_remaining_time", { endtime }) } });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler39;
                                if (matches(handler39.exception, G<"subprocess.TimeoutExpired">()))
                                {
                                    exc = handler39.exception;
                                    exc.set_attr("timeout", timeout);
                                    py_reraise(handler39.exception);
                                }
                                else
                                    py_reraise(handler39.exception);
                            }
                        }
                        return py_tuple({ stdout_, stderr_ });
                    }) },
                { "poll", sp.method("Popen", "poll", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_internal_poll");
                    }) },
                { "_remaining_time", sp.method("Popen", "_remaining_time", { py_positional("endtime") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py endtime = a[1];
                        if (endtime.is_none())
                        {
                            return none();
                        }
                        else
                        {
                            return endtime - (Mod<"time">().attr("monotonic"))();
                        }
                        return none();
                    }) },
                { "_check_timeout", sp.method("Popen", "_check_timeout", { py_positional("endtime"), py_positional("orig_timeout"), py_positional("stdout_seq"), py_positional("stderr_seq"), py_positional("skip_check_and_raise", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py endtime = a[1];
                        py orig_timeout = a[2];
                        py stdout_seq = a[3];
                        py stderr_seq = a[4];
                        py skip_check_and_raise = a[5];
                        if (endtime.is_none())
                        {
                            return none();
                        }
                        if ((skip_check_and_raise.truthy() || ((Mod<"time">().attr("monotonic"))() > endtime)))
                        {
                            py_raise((G<"subprocess.TimeoutExpired">()).call({ self.attr("args"), orig_timeout }, { { "output", (stdout_seq.truthy() ? py{ make_bytes(std::string{ "", 0 }) }.method("join", { stdout_seq }) : none()) }, { "stderr", (stderr_seq.truthy() ? py{ make_bytes(std::string{ "", 0 }) }.method("join", { stderr_seq }) : none()) } }));
                        }
                        return none();
                    }) },
                { "wait", sp.method("Popen", "wait", { py_positional("timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        py endtime;
                        py sigint_timeout;
                        if (!timeout.is_none())
                        {
                            endtime = (Mod<"time">().attr("monotonic"))() + timeout;
                        }
                        try
                        {
                            return self.method("_wait", {  }, { { "timeout", timeout } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler40;
                            if (matches(handler40.exception, B<"KeyboardInterrupt">()))
                            {
                                // https://bugs.python.org/issue25942
                                // The first keyboard interrupt waits briefly for the child to
                                // exit under the common assumption that it also received the ^C
                                // generated SIGINT and will exit rapidly.
                                if (!timeout.is_none())
                                {
                                    sigint_timeout = (B<"min">())(self.attr("_sigint_wait_secs"), self.method("_remaining_time", { endtime }));
                                }
                                else
                                {
                                    sigint_timeout = self.attr("_sigint_wait_secs");
                                }
                                self.set_attr("_sigint_wait_secs", I<0>());
                                try
                                {
                                    self.method("_wait", {  }, { { "timeout", sigint_timeout } });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler41;
                                    if (matches(handler41.exception, G<"subprocess.TimeoutExpired">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler41.exception);
                                }
                                py_reraise(handler40.exception);
                            }
                            else
                                py_reraise(handler40.exception);
                        }
                        return none();
                    }) },
                { "_close_pipe_fds", sp.method("Popen", "_close_pipe_fds", { py_positional("p2cread"), py_positional("p2cwrite"), py_positional("c2pread"), py_positional("c2pwrite"), py_positional("errread"), py_positional("errwrite") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py p2cread = a[1];
                        py p2cwrite = a[2];
                        py c2pread = a[3];
                        py c2pwrite = a[4];
                        py errread = a[5];
                        py errwrite = a[6];
                        py devnull_fd;
                        py stack;
                        devnull_fd = (B<"getattr">())(self, S<"_devnull">(), none());
                        {
                            py const manager42 = (Mod<"contextlib">().attr("ExitStack"))();
                            py const exit42 = pyclass::special_method(manager42, "__exit__");
                            py const value42 = pyclass::special_method(manager42, "__enter__")(manager42);
                            stack = value42;
                            [[maybe_unused]] int flow42 = 0;
                            [[maybe_unused]] py result42;
                            bool exited42 = false;
                            try
                            {
                        #if defined(_WIN32)
                                {
                                    if (p2cread != (I<-1>()))
                                    {
                                        stack.method("callback", { p2cread.attr("Close") });
                                    }
                                    if (c2pwrite != (I<-1>()))
                                    {
                                        stack.method("callback", { c2pwrite.attr("Close") });
                                    }
                                    if (errwrite != (I<-1>()))
                                    {
                                        stack.method("callback", { errwrite.attr("Close") });
                                    }
                                }
                        #else
                                {
                                    if (((p2cread != (I<-1>())) && (p2cwrite != (I<-1>())) && (p2cread != devnull_fd)))
                                    {
                                        stack.method("callback", { Mod<"os">().attr("close"), p2cread });
                                    }
                                    if (((c2pwrite != (I<-1>())) && (c2pread != (I<-1>())) && (c2pwrite != devnull_fd)))
                                    {
                                        stack.method("callback", { Mod<"os">().attr("close"), c2pwrite });
                                    }
                                    if (((errwrite != (I<-1>())) && (errread != (I<-1>())) && (errwrite != devnull_fd)))
                                    {
                                        stack.method("callback", { Mod<"os">().attr("close"), errwrite });
                                    }
                                }
                        #endif
                                if (!devnull_fd.is_none())
                                {
                                    stack.method("callback", { Mod<"os">().attr("close"), devnull_fd });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler42;
                                exited42 = true;
                                if (!exit42(manager42, type_of(handler42.exception), handler42.exception, handler42.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler42.exception);
                            }
                            finally42:
                            if (!exited42)
                                exit42(manager42, none(), none(), none());
                            if (flow42 == 1)
                                return result42;
                        }
                        // Prevent a double close of these handles/fds from __init__ on error.
                        self.set_attr("_closed_child_pipe_fds", boolean(true));
                        return none();
                    }) },
                { "_on_error_fd_closer", (Mod<"contextlib">().attr("contextmanager"))(sp.method("Popen", "_on_error_fd_closer", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("_on_error_fd_closer", "Popen._on_error_fd_closer", sp.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py fd;
                            py to_close;
                            to_close = py_list();
                            try
                            {
                                py_yield(to_close);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler43;
                                {
                                    if (has_attr(self, "_devnull"))
                                    {
                                        to_close.method("append", { self.attr("_devnull") });
                                        del_attr(self, "_devnull");
                                    }
                                    for (py const& item : iterate{ to_close })
                                    {
                                        fd = item;
                                        try
                                        {
                                            if (((G<"subprocess._mswindows">()).truthy() && isinstance(fd, G<"subprocess.Handle">())))
                                            {
                                                fd.method("Close");
                                            }
                                            else
                                            {
                                                (Mod<"os">().attr("close"))(fd);
                                            }
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler45;
                                            if (matches(handler45.exception, B<"OSError">()))
                                            {
                                            }
                                            else
                                                py_reraise(handler45.exception);
                                        }
                                    }
                                    py_reraise(handler43.exception);
                                }
                            }
                            return none();
                        });
                    })) },
#if defined(_WIN32)
                { "_get_handles", sp.method("Popen", "_get_handles", { py_positional("stdin"), py_positional("stdout"), py_positional("stderr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stdin_ = a[1];
                        py stdout_ = a[2];
                        py stderr_ = a[3];
                        py _;
                        py c2pread;
                        py c2pwrite;
                        py err_close_fds;
                        py errread;
                        py errwrite;
                        py p2cread;
                        py p2cwrite;
                        if ((stdin_.is_none() && stdout_.is_none() && stderr_.is_none()))
                        {
                            return py_tuple({ I<-1>(), I<-1>(), I<-1>(), I<-1>(), I<-1>(), I<-1>() });
                        }
                        auto const items46 = unpack(py_tuple({ I<-1>(), I<-1>() }), 2);
                        p2cread = items46[0];
                        p2cwrite = items46[1];
                        auto const items47 = unpack(py_tuple({ I<-1>(), I<-1>() }), 2);
                        c2pread = items47[0];
                        c2pwrite = items47[1];
                        auto const items48 = unpack(py_tuple({ I<-1>(), I<-1>() }), 2);
                        errread = items48[0];
                        errwrite = items48[1];
                        {
                            py const manager49 = self.method("_on_error_fd_closer");
                            py const exit49 = pyclass::special_method(manager49, "__exit__");
                            py const value49 = pyclass::special_method(manager49, "__enter__")(manager49);
                            err_close_fds = value49;
                            [[maybe_unused]] int flow49 = 0;
                            [[maybe_unused]] py result49;
                            bool exited49 = false;
                            try
                            {
                                if (stdin_.is_none())
                                {
                                    p2cread = (Mod<"_winapi">().attr("GetStdHandle"))(Mod<"_winapi">().attr("STD_INPUT_HANDLE"));
                                    if (p2cread.is_none())
                                    {
                                        auto const items50 = unpack((Mod<"_winapi">().attr("CreatePipe"))(none(), I<0>()), 2);
                                        p2cread = items50[0];
                                        _ = items50[1];
                                        p2cread = (G<"subprocess.Handle">())(p2cread);
                                        err_close_fds.method("append", { p2cread });
                                        (Mod<"_winapi">().attr("CloseHandle"))(_);
                                    }
                                }
                                else if (stdin_ == (G<"subprocess.PIPE">()))
                                {
                                    auto const items51 = unpack((Mod<"_winapi">().attr("CreatePipe"))(none(), I<0>()), 2);
                                    p2cread = items51[0];
                                    p2cwrite = items51[1];
                                    auto const items52 = unpack(py_tuple({ (G<"subprocess.Handle">())(p2cread), (G<"subprocess.Handle">())(p2cwrite) }), 2);
                                    p2cread = items52[0];
                                    p2cwrite = items52[1];
                                    err_close_fds.method("extend", { py_tuple({ p2cread, p2cwrite }) });
                                }
                                else if (stdin_ == (G<"subprocess.DEVNULL">()))
                                {
                                    p2cread = (Mod<"msvcrt">().attr("get_osfhandle"))(self.method("_get_devnull"));
                                }
                                else if (isinstance(stdin_, B<"int">()))
                                {
                                    p2cread = (Mod<"msvcrt">().attr("get_osfhandle"))(stdin_);
                                }
                                else
                                {
                                    // Assuming file-like object
                                    p2cread = (Mod<"msvcrt">().attr("get_osfhandle"))(stdin_.method("fileno"));
                                }
                                p2cread = self.method("_make_inheritable", { p2cread });
                                if (stdout_.is_none())
                                {
                                    c2pwrite = (Mod<"_winapi">().attr("GetStdHandle"))(Mod<"_winapi">().attr("STD_OUTPUT_HANDLE"));
                                    if (c2pwrite.is_none())
                                    {
                                        auto const items53 = unpack((Mod<"_winapi">().attr("CreatePipe"))(none(), I<0>()), 2);
                                        _ = items53[0];
                                        c2pwrite = items53[1];
                                        c2pwrite = (G<"subprocess.Handle">())(c2pwrite);
                                        err_close_fds.method("append", { c2pwrite });
                                        (Mod<"_winapi">().attr("CloseHandle"))(_);
                                    }
                                }
                                else if (stdout_ == (G<"subprocess.PIPE">()))
                                {
                                    auto const items54 = unpack((Mod<"_winapi">().attr("CreatePipe"))(none(), I<0>()), 2);
                                    c2pread = items54[0];
                                    c2pwrite = items54[1];
                                    auto const items55 = unpack(py_tuple({ (G<"subprocess.Handle">())(c2pread), (G<"subprocess.Handle">())(c2pwrite) }), 2);
                                    c2pread = items55[0];
                                    c2pwrite = items55[1];
                                    err_close_fds.method("extend", { py_tuple({ c2pread, c2pwrite }) });
                                }
                                else if (stdout_ == (G<"subprocess.DEVNULL">()))
                                {
                                    c2pwrite = (Mod<"msvcrt">().attr("get_osfhandle"))(self.method("_get_devnull"));
                                }
                                else if (isinstance(stdout_, B<"int">()))
                                {
                                    c2pwrite = (Mod<"msvcrt">().attr("get_osfhandle"))(stdout_);
                                }
                                else
                                {
                                    // Assuming file-like object
                                    c2pwrite = (Mod<"msvcrt">().attr("get_osfhandle"))(stdout_.method("fileno"));
                                }
                                c2pwrite = self.method("_make_inheritable", { c2pwrite });
                                if (stderr_.is_none())
                                {
                                    errwrite = (Mod<"_winapi">().attr("GetStdHandle"))(Mod<"_winapi">().attr("STD_ERROR_HANDLE"));
                                    if (errwrite.is_none())
                                    {
                                        auto const items56 = unpack((Mod<"_winapi">().attr("CreatePipe"))(none(), I<0>()), 2);
                                        _ = items56[0];
                                        errwrite = items56[1];
                                        errwrite = (G<"subprocess.Handle">())(errwrite);
                                        err_close_fds.method("append", { errwrite });
                                        (Mod<"_winapi">().attr("CloseHandle"))(_);
                                    }
                                }
                                else if (stderr_ == (G<"subprocess.PIPE">()))
                                {
                                    auto const items57 = unpack((Mod<"_winapi">().attr("CreatePipe"))(none(), I<0>()), 2);
                                    errread = items57[0];
                                    errwrite = items57[1];
                                    auto const items58 = unpack(py_tuple({ (G<"subprocess.Handle">())(errread), (G<"subprocess.Handle">())(errwrite) }), 2);
                                    errread = items58[0];
                                    errwrite = items58[1];
                                    err_close_fds.method("extend", { py_tuple({ errread, errwrite }) });
                                }
                                else if (stderr_ == (G<"subprocess.STDOUT">()))
                                {
                                    errwrite = c2pwrite;
                                }
                                else if (stderr_ == (G<"subprocess.DEVNULL">()))
                                {
                                    errwrite = (Mod<"msvcrt">().attr("get_osfhandle"))(self.method("_get_devnull"));
                                }
                                else if (isinstance(stderr_, B<"int">()))
                                {
                                    errwrite = (Mod<"msvcrt">().attr("get_osfhandle"))(stderr_);
                                }
                                else
                                {
                                    // Assuming file-like object
                                    errwrite = (Mod<"msvcrt">().attr("get_osfhandle"))(stderr_.method("fileno"));
                                }
                                errwrite = self.method("_make_inheritable", { errwrite });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler49;
                                exited49 = true;
                                if (!exit49(manager49, type_of(handler49.exception), handler49.exception, handler49.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler49.exception);
                            }
                            finally49:
                            if (!exited49)
                                exit49(manager49, none(), none(), none());
                            if (flow49 == 1)
                                return result49;
                        }
                        return py_tuple({ p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite });
                    }) },
                { "_make_inheritable", sp.method("Popen", "_make_inheritable", { py_positional("handle") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handle = a[1];
                        py h;
                        h = (Mod<"_winapi">().attr("DuplicateHandle"))((Mod<"_winapi">().attr("GetCurrentProcess"))(), handle, (Mod<"_winapi">().attr("GetCurrentProcess"))(), I<0>(), I<1>(), Mod<"_winapi">().attr("DUPLICATE_SAME_ACCESS"));
                        return (G<"subprocess.Handle">())(h);
                    }) },
                { "_filter_handle_list", sp.method("Popen", "_filter_handle_list", { py_positional("handle_list") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handle_list = a[1];
                        // An handle with it's lowest two bits set might be a special console
                        // handle that if passed in lpAttributeList["handle_list"], will
                        // cause it to fail.
                        return (B<"list">())([&]() -> py { py const comprehension59 = B<"set">()(); py handle; for (py const& item : iterate{ handle_list }) { handle = item; if (!([&]() -> py { py const t = boolean((handle & (I<3>())) != (I<3>())); return t.truthy() ? t : boolean((Mod<"_winapi">().attr("GetFileType"))(handle) != (Mod<"_winapi">().attr("FILE_TYPE_CHAR"))); }()).truthy()) continue; comprehension59.method("add", { handle }); } return comprehension59; }());
                    }) },
                { "_execute_child", sp.method("Popen", "_execute_child", { py_positional("args"), py_positional("executable"), py_positional("preexec_fn"), py_positional("close_fds"), py_positional("pass_fds"), py_positional("cwd"), py_positional("env"), py_positional("startupinfo"), py_positional("creationflags"), py_positional("shell"), py_positional("p2cread"), py_positional("p2cwrite"), py_positional("c2pread"), py_positional("c2pwrite"), py_positional("errread"), py_positional("errwrite"), py_positional("unused_restore_signals"), py_positional("unused_gid"), py_positional("unused_gids"), py_positional("unused_uid"), py_positional("unused_umask"), py_positional("unused_start_new_session"), py_positional("unused_process_group") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py executable = a[2];
                        py preexec_fn = a[3];
                        py close_fds = a[4];
                        py pass_fds = a[5];
                        py cwd = a[6];
                        py env = a[7];
                        py startupinfo = a[8];
                        py creationflags = a[9];
                        py shell = a[10];
                        py p2cread = a[11];
                        py p2cwrite = a[12];
                        py c2pread = a[13];
                        py c2pwrite = a[14];
                        py errread = a[15];
                        py errwrite = a[16];
                        py unused_restore_signals = a[17];
                        py unused_gid = a[18];
                        py unused_gids = a[19];
                        py unused_uid = a[20];
                        py unused_umask = a[21];
                        py unused_start_new_session = a[22];
                        py unused_process_group = a[23];
                        py attribute_list;
                        py comspec;
                        py handle_list;
                        py have_handle_list;
                        py hp;
                        py ht;
                        py pid;
                        py system_root;
                        py tid;
                        py use_std_handles;
                        if (pass_fds.truthy())
                            py_raise(B<"AssertionError">()(S<"pass_fds not supported on Windows.">()));
                        if (isinstance(args, B<"str">()))
                        {
                        }
                        else if (isinstance(args, B<"bytes">()))
                        {
                            if (shell.truthy())
                            {
                                py_raise((B<"TypeError">())(S<"bytes args is not allowed on Windows">()));
                            }
                            args = (G<"subprocess.list2cmdline">())(py_list({ args }));
                        }
                        else if (isinstance(args, Mod<"os">().attr("PathLike")))
                        {
                            if (shell.truthy())
                            {
                                py_raise((B<"TypeError">())(S<"path-like args is not allowed when shell is true">()));
                            }
                            args = (G<"subprocess.list2cmdline">())(py_list({ args }));
                        }
                        else
                        {
                            args = (G<"subprocess.list2cmdline">())(args);
                        }
                        if (!executable.is_none())
                        {
                            executable = (Mod<"os">().attr("fsdecode"))(executable);
                        }
                        // Process startup details
                        if (startupinfo.is_none())
                        {
                            startupinfo = (G<"subprocess.STARTUPINFO">())();
                        }
                        else
                        {
                            // bpo-34044: Copy STARTUPINFO since it is modified above,
                            // so the caller can reuse it multiple times.
                            startupinfo = startupinfo.method("copy");
                        }
                        use_std_handles = boolean(!py_tuple({ p2cread, c2pwrite, errwrite }).contains((I<-1>())));
                        if (use_std_handles.truthy())
                        {
                            {
                                py const target60 = startupinfo;
                                target60.set_attr("dwFlags", inplace(NEOS_PY_OR, target60.attr("dwFlags"), Mod<"_winapi">().attr("STARTF_USESTDHANDLES")));
                            }
                            startupinfo.set_attr("hStdInput", p2cread);
                            startupinfo.set_attr("hStdOutput", c2pwrite);
                            startupinfo.set_attr("hStdError", errwrite);
                        }
                        attribute_list = startupinfo.attr("lpAttributeList");
                        have_handle_list = (B<"bool">())([&]() -> py { py const t = attribute_list; return !t.truthy() ? t : [&]() -> py { py const t = boolean(attribute_list.contains((S<"handle_list">()))); return !t.truthy() ? t : attribute_list[S<"handle_list">()]; }(); }());
                        // If we were given an handle_list or need to create one
                        if ((have_handle_list.truthy() || ([&]() -> py { py const t = use_std_handles; return !t.truthy() ? t : close_fds; }()).truthy()))
                        {
                            if (attribute_list.is_none())
                            {
                                py const value61 = py_dict();
                                attribute_list = value61;
                                startupinfo.set_attr("lpAttributeList", value61);
                            }
                            py const value62 = (B<"list">())(attribute_list.method("get", { S<"handle_list">(), py_list() }));
                            handle_list = value62;
                            attribute_list.set_item(S<"handle_list">(), value62);
                            if (use_std_handles.truthy())
                            {
                                handle_list = inplace(NEOS_PY_ADD, handle_list, py_list({ (B<"int">())(p2cread), (B<"int">())(c2pwrite), (B<"int">())(errwrite) }));
                            }
                            handle_list.set_item(B<"slice">()(none(), none(), none()), self.method("_filter_handle_list", { handle_list }));
                            if (handle_list.truthy())
                            {
                                if (!close_fds.truthy())
                                {
                                    (Mod<"warnings">().attr("warn"))(S<"startupinfo.lpAttributeList['handle_list'] overriding close_fds">(), B<"RuntimeWarning">());
                                }
                                // When using the handle_list we always request to inherit
                                // handles but the only handles that will be inherited are
                                // the ones in the handle_list
                                close_fds = boolean(false);
                            }
                        }
                        if (shell.truthy())
                        {
                            {
                                py const target63 = startupinfo;
                                target63.set_attr("dwFlags", inplace(NEOS_PY_OR, target63.attr("dwFlags"), Mod<"_winapi">().attr("STARTF_USESHOWWINDOW")));
                            }
                            startupinfo.set_attr("wShowWindow", Mod<"_winapi">().attr("SW_HIDE"));
                            if (!executable.truthy())
                            {
                                // gh-101283: without a fully-qualified path, before Windows
                                // checks the system directories, it first looks in the
                                // application directory, and also the current directory if
                                // NeedCurrentDirectoryForExePathW(ExeName) is true, so try
                                // to avoid executing unqualified "cmd.exe".
                                comspec = (Mod<"os">().attr("environ")).method("get", { S<"ComSpec">() });
                                if (!comspec.truthy())
                                {
                                    system_root = (Mod<"os">().attr("environ")).method("get", { S<"SystemRoot">(), S<"">() });
                                    comspec = (Mod<"os">().attr("path")).method("join", { system_root, S<"System32">(), S<"cmd.exe">() });
                                    if (!(Mod<"os">().attr("path")).method("isabs", { comspec }).truthy())
                                    {
                                        py_raise((B<"FileNotFoundError">())(S<"shell not found: neither %ComSpec% nor %SystemRoot% is set">()));
                                    }
                                }
                                if ((Mod<"os">().attr("path")).method("isabs", { comspec }).truthy())
                                {
                                    executable = comspec;
                                }
                            }
                            else
                            {
                                comspec = executable;
                            }
                            args = (S<"{} /c \"{}\"">()).method("format", { comspec, args });
                        }
                        if (!cwd.is_none())
                        {
                            cwd = (Mod<"os">().attr("fsdecode"))(cwd);
                        }
                        (Mod<"sys">().attr("audit"))(S<"subprocess.Popen">(), executable, args, cwd, env);
                        // Start the process
                        {
                            std::optional<py> pending64;
                            [[maybe_unused]] int flow64 = 0;
                            [[maybe_unused]] py result64;
                            try
                            {
                                auto const items65 = unpack((Mod<"_winapi">().attr("CreateProcess"))(executable, args, none(), none(), (B<"int">())(boolean(!close_fds.truthy())), creationflags, env, cwd, startupinfo), 4);
                                hp = items65[0];
                                ht = items65[1];
                                pid = items65[2];
                                tid = items65[3];
                            }
                            catch (raised const&)
                            {
                                pending64 = py_take_exception();
                            }
                            finally64:
                            // no special security
                            // Child is launched. Close the parent's copy of those pipe
                            // handles that only the child should have open.  You need
                            // to make sure that no handles to the write end of the
                            // output pipe are maintained in this process or else the
                            // pipe will not close when the child process exits and the
                            // ReadFile will hang.
                            self.method("_close_pipe_fds", { p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite });
                            if (pending64)
                                py_reraise(*pending64);
                            if (flow64 == 1)
                                return result64;
                        }
                        // Retain the process handle, but close the thread handle
                        self.set_attr("_child_created", boolean(true));
                        self.set_attr("_handle", (G<"subprocess.Handle">())(hp));
                        self.set_attr("pid", pid);
                        (Mod<"_winapi">().attr("CloseHandle"))(ht);
                        return none();
                    }) },
                { "_internal_poll", sp.method("Popen", "_internal_poll", { py_positional("_deadstate", none()), py_positional("_WaitForSingleObject", Mod<"_winapi">().attr("WaitForSingleObject")), py_positional("_WAIT_OBJECT_0", Mod<"_winapi">().attr("WAIT_OBJECT_0")), py_positional("_GetExitCodeProcess", Mod<"_winapi">().attr("GetExitCodeProcess")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _deadstate = a[1];
                        py _WaitForSingleObject = a[2];
                        py _WAIT_OBJECT_0 = a[3];
                        py _GetExitCodeProcess = a[4];
                        if (self.attr("returncode").is_none())
                        {
                            if (_WaitForSingleObject(self.attr("_handle"), I<0>()) == _WAIT_OBJECT_0)
                            {
                                self.set_attr("returncode", _GetExitCodeProcess(self.attr("_handle")));
                            }
                        }
                        return self.attr("returncode");
                    }) },
                { "_wait", sp.method("Popen", "_wait", { py_positional("timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        py result;
                        py timeout_millis;
                        if (timeout.is_none())
                        {
                            timeout_millis = Mod<"_winapi">().attr("INFINITE");
                        }
                        else if (timeout <= (I<0>()))
                        {
                            timeout_millis = I<0>();
                        }
                        else
                        {
                            timeout_millis = (B<"int">())(timeout * (I<1000>()));
                        }
                        if (self.attr("returncode").is_none())
                        {
                            // API note: Returns immediately if timeout_millis == 0.
                            result = (Mod<"_winapi">().attr("WaitForSingleObject"))(self.attr("_handle"), timeout_millis);
                            if (result == (Mod<"_winapi">().attr("WAIT_TIMEOUT")))
                            {
                                py_raise((G<"subprocess.TimeoutExpired">())(self.attr("args"), timeout));
                            }
                            self.set_attr("returncode", (Mod<"_winapi">().attr("GetExitCodeProcess"))(self.attr("_handle")));
                        }
                        return self.attr("returncode");
                    }) },
                { "_readerthread", sp.method("Popen", "_readerthread", { py_positional("fh"), py_positional("buffer") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fh = a[1];
                        py buffer = a[2];
                        buffer.method("append", { fh.method("read") });
                        fh.method("close");
                        return none();
                    }) },
                { "_writerthread", sp.method("Popen", "_writerthread", { py_positional("input") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        self.method("_stdin_write", { input });
                        return none();
                    }) },
                { "_communicate", sp.method("Popen", "_communicate", { py_positional("input"), py_positional("endtime"), py_positional("orig_timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py endtime = a[2];
                        py orig_timeout = a[3];
                        py stderr_;
                        py stdout_;
                        if ((self.attr("stdout").truthy() && (!has_attr(self, "_stdout_buff"))))
                        {
                            self.set_attr("_stdout_buff", py_list());
                            self.set_attr("stdout_thread", (Mod<"threading">().attr("Thread")).call({  }, { { "target", self.attr("_readerthread") }, { "args", py_tuple({ self.attr("stdout"), self.attr("_stdout_buff") }) } }));
                            self.attr("stdout_thread").set_attr("daemon", boolean(true));
                            self.attr("stdout_thread").method("start");
                        }
                        if ((self.attr("stderr").truthy() && (!has_attr(self, "_stderr_buff"))))
                        {
                            self.set_attr("_stderr_buff", py_list());
                            self.set_attr("stderr_thread", (Mod<"threading">().attr("Thread")).call({  }, { { "target", self.attr("_readerthread") }, { "args", py_tuple({ self.attr("stderr"), self.attr("_stderr_buff") }) } }));
                            self.attr("stderr_thread").set_attr("daemon", boolean(true));
                            self.attr("stderr_thread").method("start");
                        }
                        // Start writer thread to send input to stdin, unless already
                        // started.  The thread writes input and closes stdin when done,
                        // or continues in the background on timeout.
                        if ((self.attr("stdin").truthy() && (!has_attr(self, "_stdin_thread"))))
                        {
                            self.set_attr("_stdin_thread", (Mod<"threading">().attr("Thread")).call({  }, { { "target", self.attr("_writerthread") }, { "args", py_tuple({ input }) } }));
                            self.attr("_stdin_thread").set_attr("daemon", boolean(true));
                            self.attr("_stdin_thread").method("start");
                        }
                        // Wait for the writer thread, or time out.  If we time out, the
                        // thread remains writing and the fd left open in case the user
                        // calls communicate again.
                        if (has_attr(self, "_stdin_thread"))
                        {
                            self.attr("_stdin_thread").method("join", { self.method("_remaining_time", { endtime }) });
                            if (self.attr("_stdin_thread").method("is_alive").truthy())
                            {
                                py_raise((G<"subprocess.TimeoutExpired">())(self.attr("args"), orig_timeout));
                            }
                        }
                        // Wait for the reader threads, or time out.  If we time out, the
                        // threads remain reading and the fds left open in case the user
                        // calls communicate again.
                        if (!self.attr("stdout").is_none())
                        {
                            self.attr("stdout_thread").method("join", { self.method("_remaining_time", { endtime }) });
                            if (self.attr("stdout_thread").method("is_alive").truthy())
                            {
                                py_raise((G<"subprocess.TimeoutExpired">())(self.attr("args"), orig_timeout));
                            }
                        }
                        if (!self.attr("stderr").is_none())
                        {
                            self.attr("stderr_thread").method("join", { self.method("_remaining_time", { endtime }) });
                            if (self.attr("stderr_thread").method("is_alive").truthy())
                            {
                                py_raise((G<"subprocess.TimeoutExpired">())(self.attr("args"), orig_timeout));
                            }
                        }
                        // Collect the output from and close both pipes, now that we know
                        // both have been read successfully.
                        stdout_ = none();
                        stderr_ = none();
                        if (self.attr("stdout").truthy())
                        {
                            stdout_ = self.attr("_stdout_buff");
                            self.attr("stdout").method("close");
                        }
                        if (self.attr("stderr").truthy())
                        {
                            stderr_ = self.attr("_stderr_buff");
                            self.attr("stderr").method("close");
                        }
                        // All data exchanged.  Translate lists into strings.
                        stdout_ = (stdout_.truthy() ? stdout_[I<0>()] : none());
                        stderr_ = (stderr_.truthy() ? stderr_[I<0>()] : none());
                        return py_tuple({ stdout_, stderr_ });
                    }) },
                { "send_signal", sp.method("Popen", "send_signal", { py_positional("sig") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sig = a[1];
                        // Don't signal a process that we know has already died.
                        if (!self.attr("returncode").is_none())
                        {
                            return none();
                        }
                        if (sig == (Mod<"signal">().attr("SIGTERM")))
                        {
                            self.method("terminate");
                        }
                        else if (sig == (Mod<"signal">().attr("CTRL_C_EVENT")))
                        {
                            (Mod<"os">().attr("kill"))(self.attr("pid"), Mod<"signal">().attr("CTRL_C_EVENT"));
                        }
                        else if (sig == (Mod<"signal">().attr("CTRL_BREAK_EVENT")))
                        {
                            (Mod<"os">().attr("kill"))(self.attr("pid"), Mod<"signal">().attr("CTRL_BREAK_EVENT"));
                        }
                        else
                        {
                            py_raise((B<"ValueError">())((S<"Unsupported signal: {}">()).method("format", { sig })));
                        }
                        return none();
                    }) },
                { "terminate", sp.method("Popen", "terminate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rc;
                        // Don't terminate a process that we know has already died.
                        if (!self.attr("returncode").is_none())
                        {
                            return none();
                        }
                        try
                        {
                            (Mod<"_winapi">().attr("TerminateProcess"))(self.attr("_handle"), I<1>());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler66;
                            if (matches(handler66.exception, B<"PermissionError">()))
                            {
                                // ERROR_ACCESS_DENIED (winerror 5) is received when the
                                // process already died.
                                rc = (Mod<"_winapi">().attr("GetExitCodeProcess"))(self.attr("_handle"));
                                if (rc == (Mod<"_winapi">().attr("STILL_ACTIVE")))
                                {
                                    py_reraise(handler66.exception);
                                }
                                self.set_attr("returncode", rc);
                            }
                            else
                                py_reraise(handler66.exception);
                        }
                        return none();
                    }) },
                { "kill", B<"terminate">() },
#else
                { "_get_handles", sp.method("Popen", "_get_handles", { py_positional("stdin"), py_positional("stdout"), py_positional("stderr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stdin_ = a[1];
                        py stdout_ = a[2];
                        py stderr_ = a[3];
                        py c2pread;
                        py c2pwrite;
                        py err_close_fds;
                        py errread;
                        py errwrite;
                        py p2cread;
                        py p2cwrite;
                        auto const items67 = unpack(py_tuple({ I<-1>(), I<-1>() }), 2);
                        p2cread = items67[0];
                        p2cwrite = items67[1];
                        auto const items68 = unpack(py_tuple({ I<-1>(), I<-1>() }), 2);
                        c2pread = items68[0];
                        c2pwrite = items68[1];
                        auto const items69 = unpack(py_tuple({ I<-1>(), I<-1>() }), 2);
                        errread = items69[0];
                        errwrite = items69[1];
                        {
                            py const manager70 = self.method("_on_error_fd_closer");
                            py const exit70 = pyclass::special_method(manager70, "__exit__");
                            py const value70 = pyclass::special_method(manager70, "__enter__")(manager70);
                            err_close_fds = value70;
                            [[maybe_unused]] int flow70 = 0;
                            [[maybe_unused]] py result70;
                            bool exited70 = false;
                            try
                            {
                                if (stdin_.is_none())
                                {
                                }
                                else if (stdin_ == (G<"subprocess.PIPE">()))
                                {
                                    auto const items71 = unpack((Mod<"os">().attr("pipe"))(), 2);
                                    p2cread = items71[0];
                                    p2cwrite = items71[1];
                                    err_close_fds.method("extend", { py_tuple({ p2cread, p2cwrite }) });
                                    if (((self.attr("pipesize") > (I<0>())) && has_attr(none(), "F_SETPIPE_SZ")))
                                    {
                                        none().method("fcntl", { p2cwrite, none().attr("F_SETPIPE_SZ"), self.attr("pipesize") });
                                    }
                                }
                                else if (stdin_ == (G<"subprocess.DEVNULL">()))
                                {
                                    p2cread = self.method("_get_devnull");
                                }
                                else if (isinstance(stdin_, B<"int">()))
                                {
                                    p2cread = stdin_;
                                }
                                else
                                {
                                    // Assuming file-like object
                                    p2cread = stdin_.method("fileno");
                                }
                                if (stdout_.is_none())
                                {
                                }
                                else if (stdout_ == (G<"subprocess.PIPE">()))
                                {
                                    auto const items72 = unpack((Mod<"os">().attr("pipe"))(), 2);
                                    c2pread = items72[0];
                                    c2pwrite = items72[1];
                                    err_close_fds.method("extend", { py_tuple({ c2pread, c2pwrite }) });
                                    if (((self.attr("pipesize") > (I<0>())) && has_attr(none(), "F_SETPIPE_SZ")))
                                    {
                                        none().method("fcntl", { c2pwrite, none().attr("F_SETPIPE_SZ"), self.attr("pipesize") });
                                    }
                                }
                                else if (stdout_ == (G<"subprocess.DEVNULL">()))
                                {
                                    c2pwrite = self.method("_get_devnull");
                                }
                                else if (isinstance(stdout_, B<"int">()))
                                {
                                    c2pwrite = stdout_;
                                }
                                else
                                {
                                    // Assuming file-like object
                                    c2pwrite = stdout_.method("fileno");
                                }
                                if (stderr_.is_none())
                                {
                                }
                                else if (stderr_ == (G<"subprocess.PIPE">()))
                                {
                                    auto const items73 = unpack((Mod<"os">().attr("pipe"))(), 2);
                                    errread = items73[0];
                                    errwrite = items73[1];
                                    err_close_fds.method("extend", { py_tuple({ errread, errwrite }) });
                                    if (((self.attr("pipesize") > (I<0>())) && has_attr(none(), "F_SETPIPE_SZ")))
                                    {
                                        none().method("fcntl", { errwrite, none().attr("F_SETPIPE_SZ"), self.attr("pipesize") });
                                    }
                                }
                                else if (stderr_ == (G<"subprocess.STDOUT">()))
                                {
                                    if (c2pwrite != (I<-1>()))
                                    {
                                        errwrite = c2pwrite;
                                    }
                                    else
                                    {
                                        errwrite = (Mod<"sys">().attr("__stdout__")).method("fileno");
                                    }
                                }
                                else if (stderr_ == (G<"subprocess.DEVNULL">()))
                                {
                                    errwrite = self.method("_get_devnull");
                                }
                                else if (isinstance(stderr_, B<"int">()))
                                {
                                    errwrite = stderr_;
                                }
                                else
                                {
                                    // Assuming file-like object
                                    errwrite = stderr_.method("fileno");
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler70;
                                exited70 = true;
                                if (!exit70(manager70, type_of(handler70.exception), handler70.exception, handler70.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler70.exception);
                            }
                            finally70:
                            if (!exited70)
                                exit70(manager70, none(), none(), none());
                            if (flow70 == 1)
                                return result70;
                        }
                        return py_tuple({ p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite });
                    }) },
                { "_posix_spawn", sp.method("Popen", "_posix_spawn", { py_positional("args"), py_positional("executable"), py_positional("env"), py_positional("restore_signals"), py_positional("close_fds"), py_positional("p2cread"), py_positional("p2cwrite"), py_positional("c2pread"), py_positional("c2pwrite"), py_positional("errread"), py_positional("errwrite") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py executable = a[2];
                        py env = a[3];
                        py restore_signals = a[4];
                        py close_fds = a[5];
                        py p2cread = a[6];
                        py p2cwrite = a[7];
                        py c2pread = a[8];
                        py c2pwrite = a[9];
                        py errread = a[10];
                        py errwrite = a[11];
                        py fd;
                        py fd2;
                        py file_actions;
                        py kwargs;
                        py signame;
                        py signum;
                        py sigset;
                        kwargs = py_dict();
                        if (restore_signals.truthy())
                        {
                            // See _Py_RestoreSignals() in Python/pylifecycle.c
                            sigset = py_list();
                            for (py const& item : iterate{ py_tuple({ S<"SIGPIPE">(), S<"SIGXFZ">(), S<"SIGXFSZ">() }) })
                            {
                                signame = item;
                                signum = (B<"getattr">())(Mod<"signal">(), signame, none());
                                if (!signum.is_none())
                                {
                                    sigset.method("append", { signum });
                                }
                            }
                            kwargs.set_item(S<"setsigdef">(), sigset);
                        }
                        file_actions = py_list();
                        for (py const& item : iterate{ py_tuple({ p2cwrite, c2pread, errread }) })
                        {
                            fd = item;
                            if (fd != (I<-1>()))
                            {
                                file_actions.method("append", { py_tuple({ Mod<"os">().attr("POSIX_SPAWN_CLOSE"), fd }) });
                            }
                        }
                        for (py const& item : iterate{ py_tuple({ py_tuple({ p2cread, I<0>() }), py_tuple({ c2pwrite, I<1>() }), py_tuple({ errwrite, I<2>() }) }) })
                        {
                            auto const items77 = unpack(item, 2);
                            fd = items77[0];
                            fd2 = items77[1];
                            if (fd != (I<-1>()))
                            {
                                file_actions.method("append", { py_tuple({ Mod<"os">().attr("POSIX_SPAWN_DUP2"), fd, fd2 }) });
                            }
                        }
                        if (close_fds.truthy())
                        {
                            file_actions.method("append", { py_tuple({ Mod<"os">().attr("POSIX_SPAWN_CLOSEFROM"), I<3>() }) });
                        }
                        if (file_actions.truthy())
                        {
                            kwargs.set_item(S<"file_actions">(), file_actions);
                        }
                        self.set_attr("pid", call_ex((Mod<"os">().attr("posix_spawn")), { executable, args, env }, py{}, kwargs));
                        self.set_attr("_child_created", boolean(true));
                        self.method("_close_pipe_fds", { p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite });
                        return none();
                    }) },
                { "_execute_child", sp.method("Popen", "_execute_child", { py_positional("args"), py_positional("executable"), py_positional("preexec_fn"), py_positional("close_fds"), py_positional("pass_fds"), py_positional("cwd"), py_positional("env"), py_positional("startupinfo"), py_positional("creationflags"), py_positional("shell"), py_positional("p2cread"), py_positional("p2cwrite"), py_positional("c2pread"), py_positional("c2pwrite"), py_positional("errread"), py_positional("errwrite"), py_positional("restore_signals"), py_positional("gid"), py_positional("gids"), py_positional("uid"), py_positional("umask"), py_positional("start_new_session"), py_positional("process_group") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py executable = a[2];
                        py preexec_fn = a[3];
                        py close_fds = a[4];
                        py pass_fds = a[5];
                        py cwd = a[6];
                        py env = a[7];
                        py startupinfo = a[8];
                        py creationflags = a[9];
                        py shell = a[10];
                        py p2cread = a[11];
                        py p2cwrite = a[12];
                        py c2pread = a[13];
                        py c2pwrite = a[14];
                        py errread = a[15];
                        py errwrite = a[16];
                        py restore_signals = a[17];
                        py gid = a[18];
                        py gids = a[19];
                        py uid = a[20];
                        py umask = a[21];
                        py start_new_session = a[22];
                        py process_group = a[23];
                        py child_exception_type;
                        py env_list;
                        py err_filename;
                        py err_msg;
                        py errno_num;
                        py errpipe_data;
                        py errpipe_read;
                        py errpipe_write;
                        py exception_name;
                        py executable_list;
                        py fds_to_keep;
                        py hex_errno;
                        py k;
                        py low_fd;
                        py low_fds_to_close;
                        py orig_executable;
                        py part;
                        py pid;
                        py sts;
                        py unix_shell;
                        py v;
                        if (isinstance(args, py_tuple({ B<"str">(), B<"bytes">() })))
                        {
                            args = py_list({ args });
                        }
                        else if (isinstance(args, Mod<"os">().attr("PathLike")))
                        {
                            if (shell.truthy())
                            {
                                py_raise((B<"TypeError">())(S<"path-like args is not allowed when shell is true">()));
                            }
                            args = py_list({ args });
                        }
                        else
                        {
                            args = (B<"list">())(args);
                        }
                        if (shell.truthy())
                        {
                            // On Android the default shell is at '/system/bin/sh'.
                            unix_shell = (has_attr(Mod<"sys">(), "getandroidapilevel") ? S<"/system/bin/sh">() : S<"/bin/sh">());
                            args = py_list({ unix_shell, S<"-c">() }) + args;
                            if (executable.truthy())
                            {
                                args.set_item(I<0>(), executable);
                            }
                        }
                        if (executable.is_none())
                        {
                            executable = args[I<0>()];
                        }
                        (Mod<"sys">().attr("audit"))(S<"subprocess.Popen">(), executable, args, cwd, env);
                        if (((G<"subprocess._USE_POSIX_SPAWN">()).truthy() && (Mod<"os">().attr("path")).method("dirname", { executable }).truthy() && preexec_fn.is_none() && ([&]() -> py { py const t = boolean(!close_fds.truthy()); return t.truthy() ? t : G<"subprocess._HAVE_POSIX_SPAWN_CLOSEFROM">(); }()).truthy() && (!pass_fds.truthy()) && cwd.is_none() && ([&]() -> py { py const t = boolean(p2cread == (I<-1>())); return t.truthy() ? t : boolean(p2cread > (I<2>())); }()).truthy() && ([&]() -> py { py const t = boolean(c2pwrite == (I<-1>())); return t.truthy() ? t : boolean(c2pwrite > (I<2>())); }()).truthy() && ([&]() -> py { py const t = boolean(errwrite == (I<-1>())); return t.truthy() ? t : boolean(errwrite > (I<2>())); }()).truthy() && (!start_new_session.truthy()) && (process_group == (I<-1>())) && gid.is_none() && gids.is_none() && uid.is_none() && (umask < (I<0>()))))
                        {
                            self.method("_posix_spawn", { args, executable, env, restore_signals, close_fds, p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite });
                            return none();
                        }
                        orig_executable = executable;
                        // For transferring possible exec failure from child to parent.
                        // Data format: "exception name:hex errno:description"
                        // Pickle is not used; it is complex and involves memory allocation.
                        auto const items78 = unpack((Mod<"os">().attr("pipe"))(), 2);
                        errpipe_read = items78[0];
                        errpipe_write = items78[1];
                        // errpipe_write must not be in the standard io 0, 1, or 2 fd range.
                        low_fds_to_close = py_list();
                        while (errpipe_write < (I<3>()))
                        {
                            low_fds_to_close.method("append", { errpipe_write });
                            errpipe_write = (Mod<"os">().attr("dup"))(errpipe_write);
                        }
                        for (py const& item : iterate{ low_fds_to_close })
                        {
                            low_fd = item;
                            (Mod<"os">().attr("close"))(low_fd);
                        }
                        {
                            std::optional<py> pending81;
                            [[maybe_unused]] int flow81 = 0;
                            [[maybe_unused]] py result81;
                            try
                            {
                                {
                                    std::optional<py> pending82;
                                    [[maybe_unused]] int flow82 = 0;
                                    [[maybe_unused]] py result82;
                                    try
                                    {
                                        // We must avoid complex work that could involve
                                        // malloc or free in the child process to avoid
                                        // potential deadlocks, thus we do all this here.
                                        // and pass it to fork_exec()
                                        if (!env.is_none())
                                        {
                                            env_list = py_list();
                                            for (py const& item : iterate{ env.method("items") })
                                            {
                                                auto const items84 = unpack(item, 2);
                                                k = items84[0];
                                                v = items84[1];
                                                k = (Mod<"os">().attr("fsencode"))(k);
                                                if (k.contains(py{ make_bytes(std::string{ "\x3d", 1 }) }))
                                                {
                                                    py_raise((B<"ValueError">())(S<"illegal environment variable name">()));
                                                }
                                                env_list.method("append", { (k + py{ make_bytes(std::string{ "\x3d", 1 }) }) + (Mod<"os">().attr("fsencode"))(v) });
                                            }
                                        }
                                        else
                                        {
                                            env_list = none();
                                        }
                                        executable = (Mod<"os">().attr("fsencode"))(executable);
                                        if ((Mod<"os">().attr("path")).method("dirname", { executable }).truthy())
                                        {
                                            executable_list = py_tuple({ executable });
                                        }
                                        else
                                        {
                                            // This matches the behavior of os._execvpe().
                                            executable_list = (B<"tuple">())([&]() -> py { py const comprehension85 = py_list(); py dir; for (py const& item : iterate{ (Mod<"os">().attr("get_exec_path"))(env) }) { dir = item; comprehension85.method("append", { (Mod<"os">().attr("path")).method("join", { (Mod<"os">().attr("fsencode"))(dir), executable }) }); } return comprehension85; }());
                                        }
                                        fds_to_keep = (B<"set">())(pass_fds);
                                        fds_to_keep.method("add", { errpipe_write });
                                        self.set_attr("pid", (Mod<"_posixsubprocess">().attr("fork_exec"))(args, executable_list, close_fds, (B<"tuple">())((B<"sorted">())((B<"map">())(B<"int">(), fds_to_keep))), cwd, env_list, p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite, errpipe_read, errpipe_write, restore_signals, start_new_session, process_group, gid, gids, uid, umask, preexec_fn, G<"subprocess._USE_VFORK">()));
                                        self.set_attr("_child_created", boolean(true));
                                    }
                                    catch (raised const&)
                                    {
                                        pending82 = py_take_exception();
                                    }
                                    finally82:
                                    // be sure the FD is closed no matter what
                                    (Mod<"os">().attr("close"))(errpipe_write);
                                    if (pending82)
                                        py_reraise(*pending82);
                                    if (flow82 == 1)
                                        { result81 = result82; flow81 = 1; goto finally81; }
                                }
                                self.method("_close_pipe_fds", { p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite });
                                // Wait for exec to fail or succeed; possibly raising an
                                // exception (limited in size)
                                errpipe_data = (B<"bytearray">())();
                                while (boolean(true).truthy())
                                {
                                    part = (Mod<"os">().attr("read"))(errpipe_read, I<50000>());
                                    errpipe_data = inplace(NEOS_PY_ADD, errpipe_data, part);
                                    if (((!part.truthy()) || (py{ errpipe_data.size() } > (I<50000>()))))
                                    {
                                        break;
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending81 = py_take_exception();
                            }
                            finally81:
                            // be sure the FD is closed no matter what
                            (Mod<"os">().attr("close"))(errpipe_read);
                            if (pending81)
                                py_reraise(*pending81);
                            if (flow81 == 1)
                                return result81;
                        }
                        if (errpipe_data.truthy())
                        {
                            try
                            {
                                auto const items88 = unpack((Mod<"os">().attr("waitpid"))(self.attr("pid"), I<0>()), 2);
                                pid = items88[0];
                                sts = items88[1];
                                if (pid == self.attr("pid"))
                                {
                                    self.method("_handle_exitstatus", { sts });
                                }
                                else
                                {
                                    self.set_attr("returncode", Mod<"sys">().attr("maxsize"));
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler87;
                                if (matches(handler87.exception, B<"ChildProcessError">()))
                                {
                                }
                                else
                                    py_reraise(handler87.exception);
                            }
                            try
                            {
                                auto const items90 = unpack(errpipe_data.method("split", { py{ make_bytes(std::string{ "\x3a", 1 }) }, I<2>() }), 3);
                                exception_name = items90[0];
                                hex_errno = items90[1];
                                err_msg = items90[2];
                                // The encoding here should match the encoding
                                // written in by the subprocess implementations
                                // like _posixsubprocess
                                err_msg = err_msg.method("decode");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler89;
                                if (matches(handler89.exception, B<"ValueError">()))
                                {
                                    exception_name = py{ make_bytes(std::string{ "\x53\x75\x62\x70\x72\x6f\x63\x65\x73\x73\x45\x72\x72\x6f\x72", 15 }) };
                                    hex_errno = py{ make_bytes(std::string{ "\x30", 1 }) };
                                    err_msg = (S<"Bad exception data from child: {!r}">()).method("format", { (B<"bytes">())(errpipe_data) });
                                }
                                else
                                    py_reraise(handler89.exception);
                            }
                            child_exception_type = (B<"getattr">())(Mod<"builtins">(), exception_name.method("decode", { S<"ascii">() }), G<"subprocess.SubprocessError">());
                            if ((issubclass(child_exception_type, B<"OSError">()) && hex_errno.truthy()))
                            {
                                errno_num = (B<"int">())(hex_errno, I<16>());
                                if (err_msg == (S<"noexec:chdir">()))
                                {
                                    err_msg = S<"">();
                                    // The error must be from chdir(cwd).
                                    err_filename = cwd;
                                }
                                else if (err_msg == (S<"noexec">()))
                                {
                                    err_msg = S<"">();
                                    err_filename = none();
                                }
                                else
                                {
                                    err_filename = orig_executable;
                                }
                                if (errno_num != (I<0>()))
                                {
                                    err_msg = (Mod<"os">().attr("strerror"))(errno_num);
                                }
                                if (!err_filename.is_none())
                                {
                                    py_raise(child_exception_type(errno_num, err_msg, err_filename));
                                }
                                else
                                {
                                    py_raise(child_exception_type(errno_num, err_msg));
                                }
                            }
                            py_raise(child_exception_type(err_msg));
                        }
                        return none();
                    }) },
                { "_handle_exitstatus", sp.method("Popen", "_handle_exitstatus", { py_positional("sts"), py_positional("_del_safe", G<"subprocess._del_safe">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sts = a[1];
                        py _del_safe = a[2];
                        // This method is called (indirectly) by __del__, so it cannot
                        // refer to anything outside of its local scope.
                        if (_del_safe.method("WIFSTOPPED", { sts }).truthy())
                        {
                            self.set_attr("returncode", -_del_safe.method("WSTOPSIG", { sts }));
                        }
                        else
                        {
                            self.set_attr("returncode", _del_safe.method("waitstatus_to_exitcode", { sts }));
                        }
                        return none();
                    }) },
                { "_internal_poll", sp.method("Popen", "_internal_poll", { py_positional("_deadstate", none()), py_positional("_del_safe", G<"subprocess._del_safe">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _deadstate = a[1];
                        py _del_safe = a[2];
                        py e;
                        py pid;
                        py sts;
                        if (self.attr("returncode").is_none())
                        {
                            if (!self.attr("_waitpid_lock").method("acquire", { boolean(false) }).truthy())
                            {
                                // Something else is busy calling waitpid.  Don't allow two
                                // at once.  We know nothing yet.
                                return none();
                            }
                            {
                                std::optional<py> pending91;
                                [[maybe_unused]] int flow91 = 0;
                                [[maybe_unused]] py result91;
                                try
                                {
                                    try
                                    {
                                        if (!self.attr("returncode").is_none())
                                        {
                                            result91 = self.attr("returncode");
                                            flow91 = 1;
                                            goto finally91;
                                        }
                                        auto const items92 = unpack(_del_safe.method("waitpid", { self.attr("pid"), _del_safe.attr("WNOHANG") }), 2);
                                        pid = items92[0];
                                        sts = items92[1];
                                        if (pid == self.attr("pid"))
                                        {
                                            self.method("_handle_exitstatus", { sts });
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler91;
                                        if (matches(handler91.exception, B<"OSError">()))
                                        {
                                            e = handler91.exception;
                                            if (!_deadstate.is_none())
                                            {
                                                self.set_attr("returncode", _deadstate);
                                            }
                                            else if (e.attr("errno") == _del_safe.attr("ECHILD"))
                                            {
                                                // This happens if SIGCLD is set to be ignored or
                                                // waiting for child processes has otherwise been
                                                // disabled for our process.  This child is dead, we
                                                // can't get the status.
                                                // http://bugs.python.org/issue15756
                                                self.set_attr("returncode", I<0>());
                                            }
                                        }
                                        else
                                            py_reraise(handler91.exception);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending91 = py_take_exception();
                                }
                                finally91:
                                self.attr("_waitpid_lock").method("release");
                                if (pending91)
                                    py_reraise(*pending91);
                                if (flow91 == 1)
                                    return result91;
                            }
                        }
                        return self.attr("returncode");
                    }) },
                { "_try_wait", sp.method("Popen", "_try_wait", { py_positional("wait_flags") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wait_flags = a[1];
                        py pid;
                        py sts;
                        try
                        {
                            auto const items94 = unpack((Mod<"os">().attr("waitpid"))(self.attr("pid"), wait_flags), 2);
                            pid = items94[0];
                            sts = items94[1];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler93;
                            if (matches(handler93.exception, B<"ChildProcessError">()))
                            {
                                // This happens if SIGCLD is set to be ignored or waiting
                                // for child processes has otherwise been disabled for our
                                // process.  This child is dead, we can't get the status.
                                pid = self.attr("pid");
                                sts = I<0>();
                            }
                            else
                                py_reraise(handler93.exception);
                        }
                        return py_tuple({ pid, sts });
                    }) },
                { "_wait", sp.method("Popen", "_wait", { py_positional("timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        py delay;
                        py endtime;
                        py pid;
                        py remaining;
                        py sts;
                        if (!self.attr("returncode").is_none())
                        {
                            return self.attr("returncode");
                        }
                        if (!timeout.is_none())
                        {
                            endtime = (Mod<"time">().attr("monotonic"))() + timeout;
                            // Enter a busy loop if we have a timeout.  This busy loop was
                            // cribbed from Lib/threading.py in Thread.wait() at r71065.
                            delay = F<0.0005>();
                            while (boolean(true).truthy())
                            {
                                if (self.attr("_waitpid_lock").method("acquire", { boolean(false) }).truthy())
                                {
                                    {
                                        std::optional<py> pending96;
                                        [[maybe_unused]] int flow96 = 0;
                                        [[maybe_unused]] py result96;
                                        try
                                        {
                                            if (!self.attr("returncode").is_none())
                                            {
                                                flow96 = 2;
                                                goto finally96;
                                            }
                                            auto const items97 = unpack(self.method("_try_wait", { Mod<"os">().attr("WNOHANG") }), 2);
                                            pid = items97[0];
                                            sts = items97[1];
                                            if (!([&]() -> py { py const t = boolean(pid == self.attr("pid")); return t.truthy() ? t : boolean(pid == (I<0>())); }()).truthy())
                                                py_raise(B<"AssertionError">()());
                                            if (pid == self.attr("pid"))
                                            {
                                                self.method("_handle_exitstatus", { sts });
                                                flow96 = 2;
                                                goto finally96;
                                            }
                                        }
                                        catch (raised const&)
                                        {
                                            pending96 = py_take_exception();
                                        }
                                        finally96:
                                        self.attr("_waitpid_lock").method("release");
                                        if (pending96)
                                            py_reraise(*pending96);
                                        if (flow96 == 1)
                                            return result96;
                                        if (flow96 == 2)
                                            break;
                                        if (flow96 == 3)
                                            continue;
                                    }
                                }
                                remaining = self.method("_remaining_time", { endtime });
                                if (remaining <= (I<0>()))
                                {
                                    py_raise((G<"subprocess.TimeoutExpired">())(self.attr("args"), timeout));
                                }
                                delay = (B<"min">())(delay * (I<2>()), remaining, F<0.05>());
                                (Mod<"time">().attr("sleep"))(delay);
                            }
                        }
                        else
                        {
                            while (self.attr("returncode").is_none())
                            {
                                {
                                    py const manager99 = self.attr("_waitpid_lock");
                                    py const exit99 = pyclass::special_method(manager99, "__exit__");
                                    py const value99 = pyclass::special_method(manager99, "__enter__")(manager99);
                                    [[maybe_unused]] int flow99 = 0;
                                    [[maybe_unused]] py result99;
                                    bool exited99 = false;
                                    try
                                    {
                                        if (!self.attr("returncode").is_none())
                                        {
                                            flow99 = 2;
                                            goto finally99;
                                        }
                                        auto const items100 = unpack(self.method("_try_wait", { I<0>() }), 2);
                                        pid = items100[0];
                                        sts = items100[1];
                                        // Check the pid and loop as waitpid has been known to
                                        // return 0 even without WNOHANG in odd situations.
                                        // http://bugs.python.org/issue14396.
                                        if (pid == self.attr("pid"))
                                        {
                                            self.method("_handle_exitstatus", { sts });
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler99;
                                        exited99 = true;
                                        if (!exit99(manager99, type_of(handler99.exception), handler99.exception, handler99.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler99.exception);
                                    }
                                    finally99:
                                    if (!exited99)
                                        exit99(manager99, none(), none(), none());
                                    if (flow99 == 1)
                                        return result99;
                                    if (flow99 == 2)
                                        break;
                                    if (flow99 == 3)
                                        continue;
                                }
                            }
                        }
                        return self.attr("returncode");
                    }) },
                { "_communicate", sp.method("Popen", "_communicate", { py_positional("input"), py_positional("endtime"), py_positional("orig_timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py endtime = a[2];
                        py orig_timeout = a[3];
                        py chunk;
                        py data;
                        py events;
                        py exc;
                        py input_view;
                        py key;
                        py ready;
                        py selector;
                        py stderr_;
                        py stdout_;
                        py timeout;
                        if ((self.attr("stdin").truthy() && (!self.attr("_communication_started").truthy())))
                        {
                            // Flush stdio buffer.  This might block, if the user has
                            // been writing to .stdin in an uncontrolled fashion.
                            try
                            {
                                self.attr("stdin").method("flush");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler101;
                                if (matches(handler101.exception, B<"BrokenPipeError">()))
                                {
                                }
                                else if (matches(handler101.exception, B<"ValueError">()))
                                {
                                    // ignore ValueError: I/O operation on closed file.
                                    if (!self.attr("stdin").attr("closed").truthy())
                                    {
                                        py_reraise(handler101.exception);
                                    }
                                }
                                else
                                    py_reraise(handler101.exception);
                            }
                            if (!input.truthy())
                            {
                                try
                                {
                                    self.attr("stdin").method("close");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler102;
                                    if (matches(handler102.exception, B<"BrokenPipeError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler102.exception);
                                }
                            }
                        }
                        stdout_ = none();
                        stderr_ = none();
                        // Only create this mapping if we haven't already.
                        if (!self.attr("_communication_started").truthy())
                        {
                            self.set_attr("_fileobj2output", py_dict());
                            if (self.attr("stdout").truthy())
                            {
                                self.attr("_fileobj2output").set_item(self.attr("stdout"), py_list());
                            }
                            if (self.attr("stderr").truthy())
                            {
                                self.attr("_fileobj2output").set_item(self.attr("stderr"), py_list());
                            }
                        }
                        if (self.attr("stdout").truthy())
                        {
                            stdout_ = self.attr("_fileobj2output")[self.attr("stdout")];
                        }
                        if (self.attr("stderr").truthy())
                        {
                            stderr_ = self.attr("_fileobj2output")[self.attr("stderr")];
                        }
                        self.method("_save_input", { input });
                        if (self.attr("_input").truthy())
                        {
                            if (!isinstance(self.attr("_input"), B<"memoryview">()))
                            {
                                input_view = (B<"memoryview">())(self.attr("_input"));
                            }
                            else
                            {
                                input_view = self.attr("_input").method("cast", { S<"b">() });
                            }
                        }
                        {
                            py const manager103 = (G<"subprocess._PopenSelector">())();
                            py const exit103 = pyclass::special_method(manager103, "__exit__");
                            py const value103 = pyclass::special_method(manager103, "__enter__")(manager103);
                            selector = value103;
                            [[maybe_unused]] int flow103 = 0;
                            [[maybe_unused]] py result103;
                            bool exited103 = false;
                            try
                            {
                                if ((self.attr("stdin").truthy() && (!self.attr("stdin").attr("closed").truthy()) && self.attr("_input").truthy()))
                                {
                                    selector.method("register", { self.attr("stdin"), Mod<"selectors">().attr("EVENT_WRITE") });
                                }
                                if ((self.attr("stdout").truthy() && (!self.attr("stdout").attr("closed").truthy())))
                                {
                                    selector.method("register", { self.attr("stdout"), Mod<"selectors">().attr("EVENT_READ") });
                                }
                                if ((self.attr("stderr").truthy() && (!self.attr("stderr").attr("closed").truthy())))
                                {
                                    selector.method("register", { self.attr("stderr"), Mod<"selectors">().attr("EVENT_READ") });
                                }
                                while (selector.method("get_map").truthy())
                                {
                                    timeout = self.method("_remaining_time", { endtime });
                                    if (((!timeout.is_none()) && (timeout <= (I<0>()))))
                                    {
                                        self.method("_check_timeout", { endtime, orig_timeout, stdout_, stderr_ }, { { "skip_check_and_raise", boolean(true) } });
                                        py_raise((B<"RuntimeError">())(S<"_check_timeout(..., skip_check_and_raise=True) failed to raise TimeoutExpired.">()));
                                    }
                                    ready = selector.method("select", { timeout });
                                    self.method("_check_timeout", { endtime, orig_timeout, stdout_, stderr_ });
                                    // XXX Rewrite these to use non-blocking I/O on the file
                                    // objects; they are no longer using C stdio!
                                    for (py const& item : iterate{ ready })
                                    {
                                        auto const items106 = unpack(item, 2);
                                        key = items106[0];
                                        events = items106[1];
                                        if (key.attr("fileobj").is(self.attr("stdin")))
                                        {
                                            chunk = input_view[B<"slice">()(self.attr("_input_offset"), self.attr("_input_offset") + (G<"subprocess._PIPE_BUF">()), none())];
                                            {
                                                bool otherwise107 = false;
                                                try
                                                {
                                                    {
                                                        py const target108 = self;
                                                        target108.set_attr("_input_offset", inplace(NEOS_PY_ADD, target108.attr("_input_offset"), (Mod<"os">().attr("write"))(key.attr("fd"), chunk)));
                                                    }
                                                    otherwise107 = true;
                                                }
                                                catch (raised const&)
                                                {
                                                    pyclass::handler_scope handler107;
                                                    if (matches(handler107.exception, B<"BrokenPipeError">()))
                                                    {
                                                        selector.method("unregister", { key.attr("fileobj") });
                                                        key.attr("fileobj").method("close");
                                                    }
                                                    else
                                                        py_reraise(handler107.exception);
                                                }
                                                if (otherwise107)
                                                {
                                                    if (self.attr("_input_offset") >= py{ input_view.size() })
                                                    {
                                                        selector.method("unregister", { key.attr("fileobj") });
                                                        key.attr("fileobj").method("close");
                                                    }
                                                }
                                            }
                                        }
                                        else if (py_tuple({ self.attr("stdout"), self.attr("stderr") }).contains(key.attr("fileobj")))
                                        {
                                            data = (Mod<"os">().attr("read"))(key.attr("fd"), I<32768>());
                                            if (!data.truthy())
                                            {
                                                selector.method("unregister", { key.attr("fileobj") });
                                                key.attr("fileobj").method("close");
                                            }
                                            self.attr("_fileobj2output")[key.attr("fileobj")].method("append", { data });
                                        }
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler103;
                                exited103 = true;
                                if (!exit103(manager103, type_of(handler103.exception), handler103.exception, handler103.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler103.exception);
                            }
                            finally103:
                            if (!exited103)
                                exit103(manager103, none(), none(), none());
                            if (flow103 == 1)
                                return result103;
                        }
                        try
                        {
                            self.method("wait", {  }, { { "timeout", self.method("_remaining_time", { endtime }) } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler109;
                            if (matches(handler109.exception, G<"subprocess.TimeoutExpired">()))
                            {
                                exc = handler109.exception;
                                exc.set_attr("timeout", orig_timeout);
                                py_reraise(handler109.exception);
                            }
                            else
                                py_reraise(handler109.exception);
                        }
                        // All data exchanged.  Translate lists into strings.
                        if (!stdout_.is_none())
                        {
                            stdout_ = py{ make_bytes(std::string{ "", 0 }) }.method("join", { stdout_ });
                        }
                        if (!stderr_.is_none())
                        {
                            stderr_ = py{ make_bytes(std::string{ "", 0 }) }.method("join", { stderr_ });
                        }
                        // Translate newlines, if requested.
                        // This also turns bytes into strings.
                        if (self.attr("text_mode").truthy())
                        {
                            if (!stdout_.is_none())
                            {
                                stdout_ = self.method("_translate_newlines", { stdout_, self.attr("stdout").attr("encoding"), self.attr("stdout").attr("errors") });
                            }
                            if (!stderr_.is_none())
                            {
                                stderr_ = self.method("_translate_newlines", { stderr_, self.attr("stderr").attr("encoding"), self.attr("stderr").attr("errors") });
                            }
                        }
                        return py_tuple({ stdout_, stderr_ });
                    }) },
                { "_save_input", sp.method("Popen", "_save_input", { py_positional("input") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        if ((self.attr("stdin").truthy() && self.attr("_input").is_none()))
                        {
                            self.set_attr("_input_offset", I<0>());
                            self.set_attr("_input", input);
                            if (((!input.is_none()) && self.attr("text_mode").truthy()))
                            {
                                self.set_attr("_input", self.attr("_input").method("encode", { self.attr("stdin").attr("encoding"), self.attr("stdin").attr("errors") }));
                            }
                        }
                        return none();
                    }) },
                { "send_signal", sp.method("Popen", "send_signal", { py_positional("sig") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sig = a[1];
                        // bpo-38630: Polling reduces the risk of sending a signal to the
                        // wrong process if the process completed, the Popen.returncode
                        // attribute is still None, and the pid has been reassigned
                        // (recycled) to a new different process. This race condition can
                        // happens in two cases.
                        // 
                        // Case 1. Thread A calls Popen.poll(), thread B calls
                        // Popen.send_signal(). In thread A, waitpid() succeed and returns
                        // the exit status. Thread B calls kill() because poll() in thread A
                        // did not set returncode yet. Calling poll() in thread B prevents
                        // the race condition thanks to Popen._waitpid_lock.
                        // 
                        // Case 2. waitpid(pid, 0) has been called directly, without
                        // using Popen methods: returncode is still None is this case.
                        // Calling Popen.poll() will set returncode to a default value,
                        // since waitpid() fails with ProcessLookupError.
                        self.method("poll");
                        if (!self.attr("returncode").is_none())
                        {
                            // Skip signalling a process that we know has already died.
                            return none();
                        }
                        // The race condition can still happen if the race condition
                        // described above happens between the returncode test
                        // and the kill() call.
                        try
                        {
                            (Mod<"os">().attr("kill"))(self.attr("pid"), sig);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler110;
                            if (matches(handler110.exception, B<"ProcessLookupError">()))
                            {
                                // Suppress the race condition error; bpo-40550.
                            }
                            else
                                py_reraise(handler110.exception);
                        }
                        return none();
                    }) },
                { "terminate", sp.method("Popen", "terminate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("send_signal", { Mod<"signal">().attr("SIGTERM") });
                        return none();
                    }) },
                { "kill", sp.method("Popen", "kill", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("send_signal", { Mod<"signal">().attr("SIGKILL") });
                        return none();
                    }) },
#endif
                 }));
        }

        bool subprocess_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_subprocess();
                }
                aAttributes.emplace_back("__doc__", py{ subprocess_docs::doc("") }.release());
                for (auto module : { "builtins", "errno", "io", "locale", "os", "time", "signal", "sys", "threading", "warnings", "contextlib", "types" })
                    aAttributes.emplace_back(module, fn_import(module));
                aAttributes.emplace_back("_time", import_attribute("time", "monotonic").release());
                aAttributes.emplace_back("fcntl", py::none().release());
#if defined(_WIN32)
                // (the constants of _winapi that subprocess exports)
                for (auto name : { "CREATE_NEW_CONSOLE", "CREATE_NEW_PROCESS_GROUP", "STD_INPUT_HANDLE", "STD_OUTPUT_HANDLE", "STD_ERROR_HANDLE", "SW_HIDE",
                    "STARTF_USESTDHANDLES", "STARTF_USESHOWWINDOW", "STARTF_FORCEONFEEDBACK", "STARTF_FORCEOFFFEEDBACK", "ABOVE_NORMAL_PRIORITY_CLASS", "BELOW_NORMAL_PRIORITY_CLASS", "HIGH_PRIORITY_CLASS",
                    "IDLE_PRIORITY_CLASS", "NORMAL_PRIORITY_CLASS", "REALTIME_PRIORITY_CLASS", "CREATE_NO_WINDOW", "DETACHED_PROCESS", "CREATE_DEFAULT_ERROR_MODE",
                    "CREATE_BREAKAWAY_FROM_JOB" })
                    aAttributes.emplace_back(name, import_attribute("_winapi", name).release());
                aAttributes.emplace_back("msvcrt", fn_import("msvcrt"));
                aAttributes.emplace_back("_winapi", fn_import("_winapi"));
#else
                aAttributes.emplace_back("_fork_exec", import_attribute("_posixsubprocess", "fork_exec").release());
                aAttributes.emplace_back("select", fn_import("select"));
                aAttributes.emplace_back("selectors", fn_import("selectors"));
#endif
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sSubprocess = register_module("subprocess", subprocess_module);
    }
}
