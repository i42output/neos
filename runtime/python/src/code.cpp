/*
  code.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/code.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module code (CPython 3.13's Lib/code.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "code_docs.hpp"

namespace neos::python_runtime::detail::code_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "code.name"))
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
                py_raise("SystemError", std::string{ "code: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"code.name">()))
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
            if (modules_imported("code"))
                return py{ fn_import("code") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "code" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "code", code_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- code (Lib/code.py)

        void make_code()
        {
            static module_writer const& w = writer("code");
            // Inspired by similar code by Jeff Epler and Fredrik Lundh.
            define("code", "__all__", py_list({ S<"InteractiveInterpreter">(), S<"InteractiveConsole">(), S<"interact">(), S<"compile_command">() }));
            define("code", "InteractiveInterpreter", w.make_class("InteractiveInterpreter", {  }, {
                { "__init__", w.method("InteractiveInterpreter", "__init__", { py_positional("locals", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py locals = a[1];
                        if (locals.is_none())
                        {
                            locals = dict_of({ { S<"__name__">(), S<"__console__">() }, { S<"__doc__">(), none() } });
                        }
                        self.set_attr("locals", locals);
                        self.set_attr("compile", (Mod<"codeop">().attr("CommandCompiler"))());
                        return none();
                    }) },
                { "runsource", w.method("InteractiveInterpreter", "runsource", { py_positional("source"), py_positional("filename", S<"<input>">()), py_positional("symbol", S<"single">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py filename = a[2];
                        py symbol = a[3];
                        py code;
                        try
                        {
                            code = self.method("compile", { source, filename, symbol });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"Exception">()))
                            {
                                // Case 1
                                self.method("showsyntaxerror", { filename }, { { "source", source } });
                                return boolean(false);
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        if (code.is_none())
                        {
                            // Case 2
                            return boolean(true);
                        }
                        // Case 3
                        self.method("runcode", { code });
                        return boolean(false);
                    }) },
                { "runcode", w.method("InteractiveInterpreter", "runcode", { py_positional("code") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        try
                        {
                            (B<"exec">())(code, self.attr("locals"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, B<"SystemExit">()))
                            {
                                py_reraise(handler2.exception);
                            }
                            else
                            {
                                self.method("showtraceback");
                            }
                        }
                        return none();
                    }) },
                { "showsyntaxerror", w.method("InteractiveInterpreter", "showsyntaxerror", { py_positional("filename", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py kwargs = a[2];
                        py source;
                        py tb;
                        py typ;
                        py value;
                        {
                            std::optional<py> pending3;
                            [[maybe_unused]] int flow3 = 0;
                            [[maybe_unused]] py result3;
                            try
                            {
                                auto const items4 = unpack((Mod<"sys">().attr("exc_info"))(), 3);
                                typ = items4[0];
                                value = items4[1];
                                tb = items4[2];
                                if ((filename.truthy() && issubclass(typ, B<"SyntaxError">())))
                                {
                                    value.set_attr("filename", filename);
                                }
                                source = kwargs.method("pop", { S<"source">(), S<"">() });
                                self.method("_showtraceback", { typ, value, none(), source });
                            }
                            catch (raised const&)
                            {
                                pending3 = py_take_exception();
                            }
                            finally3:
                            py const value5 = none();
                            typ = value5;
                            value = value5;
                            tb = value5;
                            if (pending3)
                                py_reraise(*pending3);
                            if (flow3 == 1)
                                return result3;
                        }
                        return none();
                    }, false, true) },
                { "showtraceback", w.method("InteractiveInterpreter", "showtraceback", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tb;
                        py typ;
                        py value;
                        {
                            std::optional<py> pending6;
                            [[maybe_unused]] int flow6 = 0;
                            [[maybe_unused]] py result6;
                            try
                            {
                                auto const items7 = unpack((Mod<"sys">().attr("exc_info"))(), 3);
                                typ = items7[0];
                                value = items7[1];
                                tb = items7[2];
                                self.method("_showtraceback", { typ, value, tb.attr("tb_next"), S<"">() });
                            }
                            catch (raised const&)
                            {
                                pending6 = py_take_exception();
                            }
                            finally6:
                            py const value8 = none();
                            typ = value8;
                            value = value8;
                            tb = value8;
                            if (pending6)
                                py_reraise(*pending6);
                            if (flow6 == 1)
                                return result6;
                        }
                        return none();
                    }) },
                { "_showtraceback", w.method("InteractiveInterpreter", "_showtraceback", { py_positional("typ"), py_positional("value"), py_positional("tb"), py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py typ = a[1];
                        py value = a[2];
                        py tb = a[3];
                        py source = a[4];
                        py e;
                        py lines;
                        (Mod<"sys">()).set_attr("last_type", typ);
                        (Mod<"sys">()).set_attr("last_traceback", tb);
                        value = value.method("with_traceback", { tb });
                        // Set the line of text that the exception refers to
                        lines = source.method("splitlines");
                        if ((source.truthy() && typ.is((B<"SyntaxError">())) && (!value.attr("text").truthy()) && (!value.attr("lineno").is_none()) && (py{ lines.size() } >= value.attr("lineno"))))
                        {
                            value.set_attr("text", lines[value.attr("lineno") - (I<1>())]);
                        }
                        py const value9 = value.method("with_traceback", { tb });
                        (Mod<"sys">()).set_attr("last_exc", value9);
                        (Mod<"sys">()).set_attr("last_value", value9);
                        value = value9;
                        if ((Mod<"sys">().attr("excepthook")).is((Mod<"sys">().attr("__excepthook__"))))
                        {
                            self.method("_excepthook", { typ, value, tb });
                        }
                        else
                        {
                            // If someone has set sys.excepthook, we let that take precedence
                            // over self.write
                            try
                            {
                                (Mod<"sys">().attr("excepthook"))(typ, value, tb);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                if (matches(handler10.exception, B<"SystemExit">()))
                                {
                                    py_reraise(handler10.exception);
                                }
                                else if (matches(handler10.exception, B<"BaseException">()))
                                {
                                    e = handler10.exception;
                                    e.set_attr("__context__", none());
                                    e = e.method("with_traceback", { e.attr("__traceback__").attr("tb_next") });
                                    (B<"print">()).call({ S<"Error in sys.excepthook:">() }, { { "file", Mod<"sys">().attr("stderr") } });
                                    (Mod<"sys">().attr("__excepthook__"))((B<"type">())(e), e, e.attr("__traceback__"));
                                    (B<"print">()).call({  }, { { "file", Mod<"sys">().attr("stderr") } });
                                    (B<"print">()).call({ S<"Original exception was:">() }, { { "file", Mod<"sys">().attr("stderr") } });
                                    (Mod<"sys">().attr("__excepthook__"))(typ, value, tb);
                                }
                                else
                                    py_reraise(handler10.exception);
                            }
                        }
                        return none();
                    }) },
                { "_excepthook", w.method("InteractiveInterpreter", "_excepthook", { py_positional("typ"), py_positional("value"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py typ = a[1];
                        py value = a[2];
                        py tb = a[3];
                        py lines;
                        lines = (Mod<"traceback">().attr("format_exception"))(typ, value, tb);
                        self.method("write", { (S<"">()).method("join", { lines }) });
                        return none();
                    }) },
                { "write", w.method("InteractiveInterpreter", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        (Mod<"sys">().attr("stderr")).method("write", { data });
                        return none();
                    }) }, }));

            define("code", "InteractiveConsole", w.make_class("InteractiveConsole", { G<"code.InteractiveInterpreter">() }, {
                { "__init__", w.method("InteractiveConsole", "__init__", { py_positional("locals", none()), py_positional("filename", S<"<console>">()), py_keyword("local_exit", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py locals = a[1];
                        py filename = a[2];
                        py local_exit = a[3];
                        (G<"code.InteractiveInterpreter">()).method("__init__", { self, locals });
                        self.set_attr("filename", filename);
                        self.set_attr("local_exit", local_exit);
                        self.method("resetbuffer");
                        return none();
                    }) },
                { "resetbuffer", w.method("InteractiveConsole", "resetbuffer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("buffer", py_list());
                        return none();
                    }) },
                { "interact", w.method("InteractiveConsole", "interact", { py_positional("banner", none()), py_positional("exitmsg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py banner = a[1];
                        py exitmsg = a[2];
                        py _exit;
                        py _quit;
                        py cprt;
                        py e;
                        py line;
                        py more;
                        py prompt;
                        try
                        {
                            Mod<"sys">().attr("ps1");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            if (matches(handler11.exception, B<"AttributeError">()))
                            {
                                (Mod<"sys">()).set_attr("ps1", S<">>> ">());
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        try
                        {
                            Mod<"sys">().attr("ps2");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler12;
                            if (matches(handler12.exception, B<"AttributeError">()))
                            {
                                (Mod<"sys">()).set_attr("ps2", S<"... ">());
                            }
                            else
                                py_reraise(handler12.exception);
                        }
                        cprt = S<"Type \"help\", \"copyright\", \"credits\" or \"license\" for more information.">();
                        if (banner.is_none())
                        {
                            self.method("write", { (S<"Python %s on %s\n%s\n(%s)\n">()) % py_tuple({ Mod<"sys">().attr("version"), Mod<"sys">().attr("platform"), cprt, self.attr("__class__").attr("__name__") }) });
                        }
                        else if (banner.truthy())
                        {
                            self.method("write", { (S<"%s\n">()) % (B<"str">())(banner) });
                        }
                        more = I<0>();
                        // When the user uses exit() or quit() in their interactive shell
                        // they probably just want to exit the created shell, not the whole
                        // process. exit and quit in builtins closes sys.stdin which makes
                        // it super difficult to restore
                        // 
                        // When self.local_exit is True, we overwrite the builtins so
                        // exit() and quit() only raises SystemExit and we can catch that
                        // to only exit the interactive shell
                        _exit = none();
                        _quit = none();
                        if (self.attr("local_exit").truthy())
                        {
                            if (has_attr(Mod<"builtins">(), "exit"))
                            {
                                _exit = Mod<"builtins">().attr("exit");
                                (Mod<"builtins">()).set_attr("exit", (G<"code.Quitter">())(S<"exit">()));
                            }
                            if (has_attr(Mod<"builtins">(), "quit"))
                            {
                                _quit = Mod<"builtins">().attr("quit");
                                (Mod<"builtins">()).set_attr("quit", (G<"code.Quitter">())(S<"quit">()));
                            }
                        }
                        {
                            std::optional<py> pending13;
                            [[maybe_unused]] int flow13 = 0;
                            [[maybe_unused]] py result13;
                            try
                            {
                                while (boolean(true).truthy())
                                {
                                    try
                                    {
                                        if (more.truthy())
                                        {
                                            prompt = Mod<"sys">().attr("ps2");
                                        }
                                        else
                                        {
                                            prompt = Mod<"sys">().attr("ps1");
                                        }
                                        {
                                            bool otherwise16 = false;
                                            try
                                            {
                                                line = self.method("raw_input", { prompt });
                                                otherwise16 = true;
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler16;
                                                if (matches(handler16.exception, B<"EOFError">()))
                                                {
                                                    self.method("write", { S<"\n">() });
                                                    break;
                                                }
                                                else
                                                    py_reraise(handler16.exception);
                                            }
                                            if (otherwise16)
                                            {
                                                more = self.method("push", { line });
                                            }
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler15;
                                        if (matches(handler15.exception, B<"KeyboardInterrupt">()))
                                        {
                                            self.method("write", { S<"\nKeyboardInterrupt\n">() });
                                            self.method("resetbuffer");
                                            more = I<0>();
                                        }
                                        else if (matches(handler15.exception, B<"SystemExit">()))
                                        {
                                            e = handler15.exception;
                                            if (self.attr("local_exit").truthy())
                                            {
                                                self.method("write", { S<"\n">() });
                                                break;
                                            }
                                            else
                                            {
                                                py_raise(e);
                                            }
                                        }
                                        else
                                            py_reraise(handler15.exception);
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending13 = py_take_exception();
                            }
                            finally13:
                            // restore exit and quit in builtins if they were modified
                            if (!_exit.is_none())
                            {
                                (Mod<"builtins">()).set_attr("exit", _exit);
                            }
                            if (!_quit.is_none())
                            {
                                (Mod<"builtins">()).set_attr("quit", _quit);
                            }
                            if (exitmsg.is_none())
                            {
                                self.method("write", { (S<"now exiting %s...\n">()) % self.attr("__class__").attr("__name__") });
                            }
                            else if (exitmsg != (S<"">()))
                            {
                                self.method("write", { (S<"%s\n">()) % exitmsg });
                            }
                            if (pending13)
                                py_reraise(*pending13);
                            if (flow13 == 1)
                                return result13;
                        }
                        return none();
                    }) },
                { "push", w.method("InteractiveConsole", "push", { py_positional("line"), py_positional("filename", none()), py_positional("_symbol", S<"single">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        py filename = a[2];
                        py _symbol = a[3];
                        py more;
                        py source;
                        self.attr("buffer").method("append", { line });
                        source = (S<"\n">()).method("join", { self.attr("buffer") });
                        if (filename.is_none())
                        {
                            filename = self.attr("filename");
                        }
                        more = self.method("runsource", { source, filename }, { { "symbol", _symbol } });
                        if (!more.truthy())
                        {
                            self.method("resetbuffer");
                        }
                        return more;
                    }) },
                { "raw_input", w.method("InteractiveConsole", "raw_input", { py_positional("prompt", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prompt = a[1];
                        return (B<"input">())(prompt);
                    }) }, }));

            define("code", "Quitter", w.make_class("Quitter", {  }, {
                { "__init__", w.method("Quitter", "__init__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.set_attr("name", name);
                        #if defined(_WIN32)
                        {
                            self.set_attr("eof", S<"Ctrl-Z plus Return">());
                        }
                        #else
                        {
                            self.set_attr("eof", S<"Ctrl-D (i.e. EOF)">());
                        }
                        #endif
                        return none();
                    }) },
                { "__repr__", w.method("Quitter", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "Use " } + self.attr("name").str() + " or " + self.attr("eof").str() + " to exit");
                    }) },
                { "__call__", w.method("Quitter", "__call__", { py_positional("code", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py_raise((B<"SystemExit">())(code));
                    }) }, }));

            define("code", "interact", w.function("interact", "interact", { py_positional("banner", none()), py_positional("readfunc", none()), py_positional("local", none()), py_positional("exitmsg", none()), py_positional("local_exit", boolean(false)) }, [](arguments a) -> py
                {
                    py banner = a[0];
                    py readfunc = a[1];
                    py local = a[2];
                    py exitmsg = a[3];
                    py local_exit = a[4];
                    py console;
                    py readline;
                    console = (G<"code.InteractiveConsole">()).call({ local }, { { "local_exit", local_exit } });
                    if (!readfunc.is_none())
                    {
                        console.set_attr("raw_input", readfunc);
                    }
                    else
                    {
                        try
                        {
                            readline = module_of("readline");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler17;
                            if (matches(handler17.exception, B<"ImportError">()))
                            {
                            }
                            else
                                py_reraise(handler17.exception);
                        }
                    }
                    console.method("interact", { banner, exitmsg });
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool code_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_code();
                }
                if (auto const doc = code_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("builtins", py{ fn_import("builtins") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("traceback", py{ fn_import("traceback") }.release());
                aAttributes.emplace_back("CommandCompiler", import_attribute("codeop", "CommandCompiler").release());
                aAttributes.emplace_back("compile_command", import_attribute("codeop", "compile_command").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("code", code_module);
    }
}
