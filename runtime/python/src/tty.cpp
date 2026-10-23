/*
  tty.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/tty.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module tty (CPython 3.13's Lib/tty.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "tty_docs.hpp"

namespace neos::python_runtime::detail::tty_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "tty.name"))
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
                py_raise("SystemError", std::string{ "tty: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"tty.name">()))
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
            if (modules_imported("tty"))
                return py{ fn_import("tty") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "tty" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "tty", tty_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- tty (Lib/tty.py)

        void make_tty()
        {
            static module_writer const& w = writer("tty");
            // Author: Steen Lumholt.
            define("tty", "__all__", py_list({ S<"cfmakeraw">(), S<"cfmakecbreak">(), S<"setraw">(), S<"setcbreak">() }));
            // Indices for termios list.
            define("tty", "IFLAG", I<0>());
            define("tty", "OFLAG", I<1>());
            define("tty", "CFLAG", I<2>());
            define("tty", "LFLAG", I<3>());
            define("tty", "ISPEED", I<4>());
            define("tty", "OSPEED", I<5>());
            define("tty", "CC", I<6>());
            define("tty", "cfmakeraw", w.function("cfmakeraw", "cfmakeraw", { py_positional("mode") }, [](arguments a) -> py
                {
                    py mode = a[0];
                    // Clear all POSIX.1-2017 input mode flags.
                    // See chapter 11 "General Terminal Interface"
                    // of POSIX.1-2017 Base Definitions.
                    {
                        py const target1 = mode;
                        py const key1 = G<"tty.IFLAG">();
                        target1.set_item(key1, inplace(NEOS_PY_AND, target1[key1], py{ unary(NEOS_PY_INVERT, ((((((((((((Mod<"termios">().attr("IGNBRK")) | (Mod<"termios">().attr("BRKINT"))) | (Mod<"termios">().attr("IGNPAR"))) | (Mod<"termios">().attr("PARMRK"))) | (Mod<"termios">().attr("INPCK"))) | (Mod<"termios">().attr("ISTRIP"))) | (Mod<"termios">().attr("INLCR"))) | (Mod<"termios">().attr("IGNCR"))) | (Mod<"termios">().attr("ICRNL"))) | (Mod<"termios">().attr("IXON"))) | (Mod<"termios">().attr("IXANY"))) | (Mod<"termios">().attr("IXOFF"))).get()) }));
                    }
                    // Do not post-process output.
                    {
                        py const target2 = mode;
                        py const key2 = G<"tty.OFLAG">();
                        target2.set_item(key2, inplace(NEOS_PY_AND, target2[key2], py{ unary(NEOS_PY_INVERT, (Mod<"termios">().attr("OPOST")).get()) }));
                    }
                    // Disable parity generation and detection; clear character size mask;
                    // let character size be 8 bits.
                    {
                        py const target3 = mode;
                        py const key3 = G<"tty.CFLAG">();
                        target3.set_item(key3, inplace(NEOS_PY_AND, target3[key3], py{ unary(NEOS_PY_INVERT, ((Mod<"termios">().attr("PARENB")) | (Mod<"termios">().attr("CSIZE"))).get()) }));
                    }
                    {
                        py const target4 = mode;
                        py const key4 = G<"tty.CFLAG">();
                        target4.set_item(key4, inplace(NEOS_PY_OR, target4[key4], Mod<"termios">().attr("CS8")));
                    }
                    // Clear all POSIX.1-2017 local mode flags.
                    {
                        py const target5 = mode;
                        py const key5 = G<"tty.LFLAG">();
                        target5.set_item(key5, inplace(NEOS_PY_AND, target5[key5], py{ unary(NEOS_PY_INVERT, (((((((((Mod<"termios">().attr("ECHO")) | (Mod<"termios">().attr("ECHOE"))) | (Mod<"termios">().attr("ECHOK"))) | (Mod<"termios">().attr("ECHONL"))) | (Mod<"termios">().attr("ICANON"))) | (Mod<"termios">().attr("IEXTEN"))) | (Mod<"termios">().attr("ISIG"))) | (Mod<"termios">().attr("NOFLSH"))) | (Mod<"termios">().attr("TOSTOP"))).get()) }));
                    }
                    // POSIX.1-2017, 11.1.7 Non-Canonical Mode Input Processing,
                    // Case B: MIN>0, TIME=0
                    // A pending read shall block until MIN (here 1) bytes are received,
                    // or a signal is received.
                    mode.set_item(G<"tty.CC">(), (B<"list">())(mode[G<"tty.CC">()]));
                    mode[G<"tty.CC">()].set_item(Mod<"termios">().attr("VMIN"), I<1>());
                    mode[G<"tty.CC">()].set_item(Mod<"termios">().attr("VTIME"), I<0>());
                    return none();
                }));

            define("tty", "cfmakecbreak", w.function("cfmakecbreak", "cfmakecbreak", { py_positional("mode") }, [](arguments a) -> py
                {
                    py mode = a[0];
                    // Do not echo characters; disable canonical input.
                    {
                        py const target6 = mode;
                        py const key6 = G<"tty.LFLAG">();
                        target6.set_item(key6, inplace(NEOS_PY_AND, target6[key6], py{ unary(NEOS_PY_INVERT, ((Mod<"termios">().attr("ECHO")) | (Mod<"termios">().attr("ICANON"))).get()) }));
                    }
                    // POSIX.1-2017, 11.1.7 Non-Canonical Mode Input Processing,
                    // Case B: MIN>0, TIME=0
                    // A pending read shall block until MIN (here 1) bytes are received,
                    // or a signal is received.
                    mode.set_item(G<"tty.CC">(), (B<"list">())(mode[G<"tty.CC">()]));
                    mode[G<"tty.CC">()].set_item(Mod<"termios">().attr("VMIN"), I<1>());
                    mode[G<"tty.CC">()].set_item(Mod<"termios">().attr("VTIME"), I<0>());
                    return none();
                }));

            define("tty", "setraw", w.function("setraw", "setraw", { py_positional("fd"), py_positional("when", Mod<"termios">().attr("TCSAFLUSH")) }, [](arguments a) -> py
                {
                    py fd = a[0];
                    py when = a[1];
                    py mode;
                    py new_;
                    mode = (Mod<"termios">().attr("tcgetattr"))(fd);
                    new_ = (B<"list">())(mode);
                    (G<"tty.cfmakeraw">())(new_);
                    (Mod<"termios">().attr("tcsetattr"))(fd, when, new_);
                    return mode;
                }));

            define("tty", "setcbreak", w.function("setcbreak", "setcbreak", { py_positional("fd"), py_positional("when", Mod<"termios">().attr("TCSAFLUSH")) }, [](arguments a) -> py
                {
                    py fd = a[0];
                    py when = a[1];
                    py mode;
                    py new_;
                    mode = (Mod<"termios">().attr("tcgetattr"))(fd);
                    new_ = (B<"list">())(mode);
                    (G<"tty.cfmakecbreak">())(new_);
                    (Mod<"termios">().attr("tcsetattr"))(fd, when, new_);
                    return mode;
                }));
        }

        bool tty_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_tty();
                }
                if (auto const doc = tty_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                {
                    py const m{ fn_import("termios") };
                    bool const all = has_attr(m, "__all__");
                    for (py const& n : iterate{ all ? m.attr("__all__") : builtin("dir")(m) })
                        if (auto const text = n.str(); all || !text.starts_with("_"))
                            aAttributes.emplace_back(text, m.attr(text.c_str()).release());
                }
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("tty", tty_module);
    }
}
