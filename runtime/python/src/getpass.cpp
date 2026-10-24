/*
  getpass.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/getpass.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module getpass (CPython 3.13's Lib/getpass.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "getpass_docs.hpp"

namespace neos::python_runtime::detail::getpass_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "getpass.name"))
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
                py_raise("SystemError", std::string{ "getpass: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"getpass.name">()))
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
            if (modules_imported("getpass"))
                return py{ fn_import("getpass") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "getpass" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "getpass", getpass_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- getpass (Lib/getpass.py)

        void make_getpass()
        {
            static module_writer const& w = writer("getpass");
            // Authors: Piers Lauder (original)
            // Guido van Rossum (Windows support and cleanup)
            // Gregory P. Smith (tty support & GetPassWarning)
            define("getpass", "__all__", py_list({ S<"getpass">(), S<"getuser">(), S<"GetPassWarning">() }));
            define("getpass", "GetPassWarning", w.make_class("GetPassWarning", { B<"UserWarning">() }, {
 }));

            define("getpass", "unix_getpass", w.function("unix_getpass", "unix_getpass", { py_positional("prompt", S<"Password: ">()), py_positional("stream", none()) }, [](arguments a) -> py
                {
                    py prompt = a[0];
                    py stream = a[1];
                    py fd;
                    py input;
                    py new_;
                    py old;
                    py passwd;
                    py stack;
                    py tcsetattr_flags;
                    py tty;
                    passwd = none();
                    {
                        py const manager1 = (Mod<"contextlib">().attr("ExitStack"))();
                        py const exit1 = pyclass::special_method(manager1, "__exit__");
                        py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                        stack = value1;
                        [[maybe_unused]] int flow1 = 0;
                        [[maybe_unused]] py result1;
                        bool exited1 = false;
                        try
                        {
                            try
                            {
                                // Always try reading and writing directly on the tty first.
                                fd = (Mod<"os">().attr("open"))(S<"/dev/tty">(), (Mod<"os">().attr("O_RDWR")) | (Mod<"os">().attr("O_NOCTTY")));
                                tty = (Mod<"io">().attr("FileIO"))(fd, S<"w+">());
                                stack.method("enter_context", { tty });
                                input = (Mod<"io">().attr("TextIOWrapper"))(tty);
                                stack.method("enter_context", { input });
                                if (!stream.truthy())
                                {
                                    stream = input;
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, B<"OSError">()))
                                {
                                    // If that fails, see if stdin can be controlled.
                                    stack.method("close");
                                    try
                                    {
                                        fd = (Mod<"sys">().attr("stdin")).method("fileno");
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler3;
                                        if (matches(handler3.exception, py_tuple({ B<"AttributeError">(), B<"ValueError">() })))
                                        {
                                            fd = none();
                                            passwd = (G<"getpass.fallback_getpass">())(prompt, stream);
                                        }
                                        else
                                            py_reraise(handler3.exception);
                                    }
                                    input = Mod<"sys">().attr("stdin");
                                    if (!stream.truthy())
                                    {
                                        stream = Mod<"sys">().attr("stderr");
                                    }
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                            if (!fd.is_none())
                            {
                                try
                                {
                                    old = (Mod<"termios">().attr("tcgetattr"))(fd);
                                    new_ = old[B<"slice">()(none(), none(), none())];
                                    {
                                        py const target5 = new_;
                                        py const key5 = I<3>();
                                        target5.set_item(key5, inplace(NEOS_PY_AND, target5[key5], py{ unary(NEOS_PY_INVERT, (Mod<"termios">().attr("ECHO")).get()) }));
                                    }
                                    tcsetattr_flags = Mod<"termios">().attr("TCSAFLUSH");
                                    if (has_attr(Mod<"termios">(), "TCSASOFT"))
                                    {
                                        tcsetattr_flags = inplace(NEOS_PY_OR, tcsetattr_flags, Mod<"termios">().attr("TCSASOFT"));
                                    }
                                    {
                                        std::optional<py> pending6;
                                        [[maybe_unused]] int flow6 = 0;
                                        [[maybe_unused]] py result6;
                                        try
                                        {
                                            (Mod<"termios">().attr("tcsetattr"))(fd, tcsetattr_flags, new_);
                                            passwd = (G<"getpass._raw_input">()).call({ prompt, stream }, { { "input", input } });
                                        }
                                        catch (raised const&)
                                        {
                                            pending6 = py_take_exception();
                                        }
                                        (Mod<"termios">().attr("tcsetattr"))(fd, tcsetattr_flags, old);
                                        stream.method("flush");
                                        if (pending6)
                                            py_reraise(*pending6);
                                        if (flow6 == 1)
                                            { result1 = result6; flow1 = 1; goto finally1; }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler4;
                                    if (matches(handler4.exception, Mod<"termios">().attr("error")))
                                    {
                                        if (!passwd.is_none())
                                        {
                                            // _raw_input succeeded.  The final tcsetattr failed.  Reraise
                                            // instead of leaving the terminal in an unknown state.
                                            py_reraise(handler4.exception);
                                        }
                                        // We can't control the tty or stdin.  Give up and use normal IO.
                                        // fallback_getpass() raises an appropriate warning.
                                        if (!stream.is(input))
                                        {
                                            // clean up unused file objects before blocking
                                            stack.method("close");
                                        }
                                        passwd = (G<"getpass.fallback_getpass">())(prompt, stream);
                                    }
                                    else
                                        py_reraise(handler4.exception);
                                }
                            }
                            stream.method("write", { S<"\n">() });
                            result1 = passwd;
                            flow1 = 1;
                            goto finally1;
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

            define("getpass", "win_getpass", w.function("win_getpass", "win_getpass", { py_positional("prompt", S<"Password: ">()), py_positional("stream", none()) }, [](arguments a) -> py
                {
                    py prompt = a[0];
                    py stream = a[1];
                    py c;
                    py pw;
                    if (!(Mod<"sys">().attr("stdin")).is((Mod<"sys">().attr("__stdin__"))))
                    {
                        return (G<"getpass.fallback_getpass">())(prompt, stream);
                    }
                    for (py const& item : iterate{ prompt })
                    {
                        c = item;
                        (Mod<"msvcrt">().attr("putwch"))(c);
                    }
                    pw = S<"">();
                    while ((I<1>()).truthy())
                    {
                        c = (Mod<"msvcrt">().attr("getwch"))();
                        if (((c == (S<"\r">())) || (c == (S<"\n">()))))
                        {
                            break;
                        }
                        if (c == (S<"\u0003">()))
                        {
                            py_raise(B<"KeyboardInterrupt">());
                        }
                        if (c == (S<"\b">()))
                        {
                            pw = pw[B<"slice">()(none(), I<-1>(), none())];
                        }
                        else
                        {
                            pw = pw + c;
                        }
                    }
                    (Mod<"msvcrt">().attr("putwch"))(S<"\r">());
                    (Mod<"msvcrt">().attr("putwch"))(S<"\n">());
                    return pw;
                }));

            define("getpass", "fallback_getpass", w.function("fallback_getpass", "fallback_getpass", { py_positional("prompt", S<"Password: ">()), py_positional("stream", none()) }, [](arguments a) -> py
                {
                    py prompt = a[0];
                    py stream = a[1];
                    py warnings;
                    warnings = module_of("warnings");
                    warnings.method("warn", { S<"Can not control echo on the terminal.">(), G<"getpass.GetPassWarning">() }, { { "stacklevel", I<1>() } });
                    if (!stream.truthy())
                    {
                        stream = Mod<"sys">().attr("stderr");
                    }
                    (B<"print">()).call({ S<"Warning: Password input may be echoed.">() }, { { "file", stream } });
                    return (G<"getpass._raw_input">())(prompt, stream);
                }));

            define("getpass", "_raw_input", w.function("_raw_input", "_raw_input", { py_positional("prompt", S<"">()), py_positional("stream", none()), py_positional("input", none()) }, [](arguments a) -> py
                {
                    py prompt = a[0];
                    py stream = a[1];
                    py input = a[2];
                    py line;
                    if (!stream.truthy())
                    {
                        stream = Mod<"sys">().attr("stderr");
                    }
                    if (!input.truthy())
                    {
                        input = Mod<"sys">().attr("stdin");
                    }
                    prompt = (B<"str">())(prompt);
                    if (prompt.truthy())
                    {
                        try
                        {
                            stream.method("write", { prompt });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, B<"UnicodeEncodeError">()))
                            {
                                // Use replace error handler to get as much as possible printed.
                                prompt = prompt.method("encode", { stream.attr("encoding"), S<"replace">() });
                                prompt = prompt.method("decode", { stream.attr("encoding") });
                                stream.method("write", { prompt });
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        stream.method("flush");
                    }
                    // NOTE: The Python C API calls flockfile() (and unlock) during readline.
                    line = input.method("readline");
                    if (!line.truthy())
                    {
                        py_raise(B<"EOFError">());
                    }
                    if (line[I<-1>()] == (S<"\n">()))
                    {
                        line = line[B<"slice">()(none(), I<-1>(), none())];
                    }
                    return line;
                }));

            define("getpass", "getuser", w.function("getuser", "getuser", {  }, [](arguments a) -> py
                {
                    py e;
                    py name;
                    py pwd;
                    py user;
                    for (py const& item : iterate{ py_tuple({ S<"LOGNAME">(), S<"USER">(), S<"LNAME">(), S<"USERNAME">() }) })
                    {
                        name = item;
                        user = (Mod<"os">().attr("environ")).method("get", { name });
                        if (user.truthy())
                        {
                            return user;
                        }
                    }
                    try
                    {
                        pwd = module_of("pwd");
                        return pwd.method("getpwuid", { (Mod<"os">().attr("getuid"))() })[I<0>()];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler11;
                        if (matches(handler11.exception, py_tuple({ B<"ImportError">(), B<"KeyError">() })))
                        {
                            e = handler11.exception;
                            fn_raise_from((B<"OSError">())(S<"No username set in the environment">()).get(), e.get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler11.exception);
                    }
                    return none();
                }));

            // Bind the name getpass to the appropriate function
#if defined(_WIN32)
            define("getpass", "getpass", G<"getpass.win_getpass">());
#else
            define("getpass", "getpass", G<"getpass.unix_getpass">());
#endif
        }

        bool getpass_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_getpass();
                }
                if (auto const doc = getpass_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("contextlib", py{ fn_import("contextlib") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("getpass", getpass_module);
    }
}
