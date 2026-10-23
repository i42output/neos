/*
  pty.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/pty.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module pty (CPython 3.13's Lib/pty.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "pty_docs.hpp"

namespace neos::python_runtime::detail::pty_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "pty.name"))
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
                py_raise("SystemError", std::string{ "pty: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"pty.name">()))
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
            if (modules_imported("pty"))
                return py{ fn_import("pty") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "pty" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "pty", pty_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- pty (Lib/pty.py)

        void make_pty()
        {
            static module_writer const& w = writer("pty");
            // Bugs: No signal handling.  Doesn't set slave termios and window size.
            // Only tested on Linux, FreeBSD, and macOS.
            // See:  W. Richard Stevens. 1992.  Advanced Programming in the
            // UNIX Environment.  Chapter 19.
            // Author: Steen Lumholt -- with additions by Guido.
            // names imported directly for test mocking purposes
            define("pty", "__all__", py_list({ S<"openpty">(), S<"fork">(), S<"spawn">() }));
            define("pty", "STDIN_FILENO", I<0>());
            define("pty", "STDOUT_FILENO", I<1>());
            define("pty", "STDERR_FILENO", I<2>());
            define("pty", "CHILD", I<0>());
            define("pty", "openpty", w.function("openpty", "openpty", {  }, [](arguments a) -> py
                {
                    py master_fd;
                    py slave_fd;
                    py slave_name;
                    try
                    {
                        return (Mod<"os">().attr("openpty"))();
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler1;
                        if (matches(handler1.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                        {
                        }
                        else
                            py_reraise(handler1.exception);
                    }
                    auto const items2 = unpack((G<"pty._open_terminal">())(), 2);
                    master_fd = items2[0];
                    slave_name = items2[1];
                    slave_fd = (G<"pty.slave_open">())(slave_name);
                    return py_tuple({ master_fd, slave_fd });
                }));

            define("pty", "master_open", w.function("master_open", "master_open", {  }, [](arguments a) -> py
                {
                    py master_fd;
                    py slave_fd;
                    py slave_name;
                    py warnings;
                    warnings = module_of("warnings");
                    warnings.method("warn", { S<"Use pty.openpty() instead.">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    {
                        bool otherwise3 = false;
                        try
                        {
                            auto const items4 = unpack((Mod<"os">().attr("openpty"))(), 2);
                            master_fd = items4[0];
                            slave_fd = items4[1];
                            otherwise3 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            if (matches(handler3.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                            {
                            }
                            else
                                py_reraise(handler3.exception);
                        }
                        if (otherwise3)
                        {
                            slave_name = (Mod<"os">().attr("ttyname"))(slave_fd);
                            (Mod<"os">().attr("close"))(slave_fd);
                            return py_tuple({ master_fd, slave_name });
                        }
                    }
                    return (G<"pty._open_terminal">())();
                }));

            define("pty", "_open_terminal", w.function("_open_terminal", "_open_terminal", {  }, [](arguments a) -> py
                {
                    py fd;
                    py pty_name;
                    py x;
                    py y;
                    for (py const& item : iterate{ S<"pqrstuvwxyzPQRST">() })
                    {
                        x = item;
                        for (py const& item : iterate{ S<"0123456789abcdef">() })
                        {
                            y = item;
                            pty_name = ((S<"/dev/pty">()) + x) + y;
                            try
                            {
                                fd = (Mod<"os">().attr("open"))(pty_name, Mod<"os">().attr("O_RDWR"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler7;
                                if (matches(handler7.exception, B<"OSError">()))
                                {
                                    continue;
                                }
                                else
                                    py_reraise(handler7.exception);
                            }
                            return py_tuple({ fd, ((S<"/dev/tty">()) + x) + y });
                        }
                    }
                    py_raise((B<"OSError">())(S<"out of pty devices">()));
                }));

            define("pty", "slave_open", w.function("slave_open", "slave_open", { py_positional("tty_name") }, [](arguments a) -> py
                {
                    py tty_name = a[0];
                    py I_PUSH;
                    py ioctl;
                    py result;
                    py warnings;
                    warnings = module_of("warnings");
                    warnings.method("warn", { S<"Use pty.openpty() instead.">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                    result = (Mod<"os">().attr("open"))(tty_name, Mod<"os">().attr("O_RDWR"));
                    try
                    {
                        ioctl = M("fcntl", "ioctl");
                        I_PUSH = M("fcntl", "I_PUSH");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler8;
                        if (matches(handler8.exception, B<"ImportError">()))
                        {
                            return result;
                        }
                        else
                            py_reraise(handler8.exception);
                    }
                    try
                    {
                        ioctl(result, I_PUSH, S<"ptem">());
                        ioctl(result, I_PUSH, S<"ldterm">());
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler9;
                        if (matches(handler9.exception, B<"OSError">()))
                        {
                        }
                        else
                            py_reraise(handler9.exception);
                    }
                    return result;
                }));

            define("pty", "fork", w.function("fork", "fork", {  }, [](arguments a) -> py
                {
                    py fd;
                    py master_fd;
                    py pid;
                    py slave_fd;
                    {
                        bool otherwise10 = false;
                        try
                        {
                            auto const items11 = unpack((Mod<"os">().attr("forkpty"))(), 2);
                            pid = items11[0];
                            fd = items11[1];
                            otherwise10 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler10;
                            if (matches(handler10.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                            {
                            }
                            else
                                py_reraise(handler10.exception);
                        }
                        if (otherwise10)
                        {
                            if (pid == (G<"pty.CHILD">()))
                            {
                                try
                                {
                                    (Mod<"os">().attr("setsid"))();
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler12;
                                    if (matches(handler12.exception, B<"OSError">()))
                                    {
                                        // os.forkpty() already set us session leader
                                    }
                                    else
                                        py_reraise(handler12.exception);
                                }
                            }
                            return py_tuple({ pid, fd });
                        }
                    }
                    auto const items13 = unpack((G<"pty.openpty">())(), 2);
                    master_fd = items13[0];
                    slave_fd = items13[1];
                    pid = (Mod<"os">().attr("fork"))();
                    if (pid == (G<"pty.CHILD">()))
                    {
                        (Mod<"os">().attr("close"))(master_fd);
                        (Mod<"os">().attr("login_tty"))(slave_fd);
                    }
                    else
                    {
                        (Mod<"os">().attr("close"))(slave_fd);
                    }
                    // Parent and child process.
                    return py_tuple({ pid, master_fd });
                }));

            define("pty", "_read", w.function("_read", "_read", { py_positional("fd") }, [](arguments a) -> py
                {
                    py fd = a[0];
                    return (Mod<"os">().attr("read"))(fd, I<1024>());
                }));

            define("pty", "_copy", w.function("_copy", "_copy", { py_positional("master_fd"), py_positional("master_read", G<"pty._read">()), py_positional("stdin_read", G<"pty._read">()) }, [](arguments a) -> py
                {
                    py master_fd = a[0];
                    py master_read = a[1];
                    py stdin_read = a[2];
                    py _xfds;
                    py data;
                    py high_waterlevel;
                    py i_buf;
                    py n;
                    py o_buf;
                    py rfds;
                    py stdin_avail;
                    py stdout_avail;
                    py wfds;
                    if ((Mod<"os">().attr("get_blocking"))(master_fd).truthy())
                    {
                        // If we write more than tty/ndisc is willing to buffer, we may block
                        // indefinitely. So we set master_fd to non-blocking temporarily during
                        // the copy operation.
                        (Mod<"os">().attr("set_blocking"))(master_fd, boolean(false));
                        {
                            std::optional<py> pending14;
                            [[maybe_unused]] int flow14 = 0;
                            [[maybe_unused]] py result14;
                            try
                            {
                                (G<"pty._copy">()).call({ master_fd }, { { "master_read", master_read }, { "stdin_read", stdin_read } });
                            }
                            catch (raised const&)
                            {
                                pending14 = py_take_exception();
                            }
                            finally14:
                            // restore blocking mode for backwards compatibility
                            (Mod<"os">().attr("set_blocking"))(master_fd, boolean(true));
                            if (pending14)
                                py_reraise(*pending14);
                            if (flow14 == 1)
                                return result14;
                        }
                        return none();
                    }
                    high_waterlevel = I<4096>();
                    stdin_avail = boolean(master_fd != (G<"pty.STDIN_FILENO">()));
                    stdout_avail = boolean(master_fd != (G<"pty.STDOUT_FILENO">()));
                    i_buf = py{ make_bytes(std::string{ "", 0 }) };
                    o_buf = py{ make_bytes(std::string{ "", 0 }) };
                    while ((I<1>()).truthy())
                    {
                        rfds = py_list();
                        wfds = py_list();
                        if ((stdin_avail.truthy() && (py{ i_buf.size() } < high_waterlevel)))
                        {
                            rfds.method("append", { G<"pty.STDIN_FILENO">() });
                        }
                        if ((stdout_avail.truthy() && (py{ o_buf.size() } < high_waterlevel)))
                        {
                            rfds.method("append", { master_fd });
                        }
                        if ((stdout_avail.truthy() && (py{ o_buf.size() } > (I<0>()))))
                        {
                            wfds.method("append", { G<"pty.STDOUT_FILENO">() });
                        }
                        if (py{ i_buf.size() } > (I<0>()))
                        {
                            wfds.method("append", { master_fd });
                        }
                        auto const items16 = unpack((Mod<"select">().attr("select"))(rfds, wfds, py_list()), 3);
                        rfds = items16[0];
                        wfds = items16[1];
                        _xfds = items16[2];
                        if (wfds.contains((G<"pty.STDOUT_FILENO">())))
                        {
                            try
                            {
                                n = (Mod<"os">().attr("write"))(G<"pty.STDOUT_FILENO">(), o_buf);
                                o_buf = o_buf[B<"slice">()(n, none(), none())];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler17;
                                if (matches(handler17.exception, B<"OSError">()))
                                {
                                    stdout_avail = boolean(false);
                                }
                                else
                                    py_reraise(handler17.exception);
                            }
                        }
                        if (rfds.contains(master_fd))
                        {
                            // Some OSes signal EOF by returning an empty byte string,
                            // some throw OSErrors.
                            try
                            {
                                data = master_read(master_fd);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler18;
                                if (matches(handler18.exception, B<"OSError">()))
                                {
                                    data = py{ make_bytes(std::string{ "", 0 }) };
                                }
                                else
                                    py_reraise(handler18.exception);
                            }
                            if (!data.truthy())
                            {
                                return none();
                            }
                            // unreachable, so we clean up.
                            o_buf = inplace(NEOS_PY_ADD, o_buf, data);
                        }
                        if (wfds.contains(master_fd))
                        {
                            n = (Mod<"os">().attr("write"))(master_fd, i_buf);
                            i_buf = i_buf[B<"slice">()(n, none(), none())];
                        }
                        if ((stdin_avail.truthy() && rfds.contains((G<"pty.STDIN_FILENO">()))))
                        {
                            data = stdin_read(G<"pty.STDIN_FILENO">());
                            if (!data.truthy())
                            {
                                stdin_avail = boolean(false);
                            }
                            else
                            {
                                i_buf = inplace(NEOS_PY_ADD, i_buf, data);
                            }
                        }
                    }
                    return none();
                }));

            define("pty", "spawn", w.function("spawn", "spawn", { py_positional("argv"), py_positional("master_read", G<"pty._read">()), py_positional("stdin_read", G<"pty._read">()) }, [](arguments a) -> py
                {
                    py argv = a[0];
                    py master_read = a[1];
                    py stdin_read = a[2];
                    py master_fd;
                    py mode;
                    py pid;
                    py restore;
                    if (isinstance(argv, B<"str">()))
                    {
                        argv = py_tuple({ argv });
                    }
                    (Mod<"sys">().attr("audit"))(S<"pty.spawn">(), argv);
                    auto const items19 = unpack((G<"pty.fork">())(), 2);
                    pid = items19[0];
                    master_fd = items19[1];
                    if (pid == (G<"pty.CHILD">()))
                    {
                        call_ex((Mod<"os">().attr("execlp")), { argv[I<0>()] }, argv, py{});
                    }
                    try
                    {
                        mode = (Mod<"tty">().attr("tcgetattr"))(G<"pty.STDIN_FILENO">());
                        (Mod<"tty">().attr("setraw"))(G<"pty.STDIN_FILENO">());
                        restore = boolean(true);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler20;
                        if (matches(handler20.exception, Mod<"tty">().attr("error")))
                        {
                            restore = boolean(false);
                        }
                        else
                            py_reraise(handler20.exception);
                    }
                    {
                        std::optional<py> pending21;
                        [[maybe_unused]] int flow21 = 0;
                        [[maybe_unused]] py result21;
                        try
                        {
                            (G<"pty._copy">())(master_fd, master_read, stdin_read);
                        }
                        catch (raised const&)
                        {
                            pending21 = py_take_exception();
                        }
                        finally21:
                        if (restore.truthy())
                        {
                            (Mod<"tty">().attr("tcsetattr"))(G<"pty.STDIN_FILENO">(), Mod<"tty">().attr("TCSAFLUSH"), mode);
                        }
                        if (pending21)
                            py_reraise(*pending21);
                        if (flow21 == 1)
                            return result21;
                    }
                    (Mod<"os">().attr("close"))(master_fd);
                    return (Mod<"os">().attr("waitpid"))(pid, I<0>())[I<1>()];
                }));
        }

        bool pty_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_pty();
                }
                if (auto const doc = pty_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("select", import_attribute("select", "select").release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("tty", py{ fn_import("tty") }.release());
                aAttributes.emplace_back("close", import_attribute("os", "close").release());
                aAttributes.emplace_back("waitpid", import_attribute("os", "waitpid").release());
                aAttributes.emplace_back("setraw", import_attribute("tty", "setraw").release());
                aAttributes.emplace_back("tcgetattr", import_attribute("tty", "tcgetattr").release());
                aAttributes.emplace_back("tcsetattr", import_attribute("tty", "tcsetattr").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("pty", pty_module);
    }
}
