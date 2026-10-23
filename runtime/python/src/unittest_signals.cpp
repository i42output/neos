/*
  unittest_signals.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/signals.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/signals.py's copyright: Copyright (c) 1999-2003 Steve
  Purcell; Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.signals (CPython 3.13's Lib/unittest/signals.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_signals_docs.hpp"

namespace neos::python_runtime::detail::unittest_signals_module
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.signals.name"))
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
                py_raise("SystemError", std::string{ "unittest.signals: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.signals.name">()))
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
            static module_writer const sWriter{ "unittest.signals", unittest_signals_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.signals (Lib/unittest/signals.py)

        void make_unittest_signals()
        {
            static module_writer const& w = writer("unittest.signals");
            define("unittest.signals", "__unittest", boolean(true));
            define("unittest.signals", "_InterruptHandler", w.make_class("_InterruptHandler", { B<"object">() }, {
                { "__init__", w.method("_InterruptHandler", "__init__", { py_positional("default_handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py default_handler = a[1];
                        self.set_attr("called", boolean(false));
                        self.set_attr("original_handler", default_handler);
                        if (isinstance(default_handler, B<"int">()))
                        {
                            if (default_handler == (Mod<"signal">().attr("SIG_DFL")))
                            {
                                // Pretend it's signal.default_int_handler instead.
                                default_handler = Mod<"signal">().attr("default_int_handler");
                            }
                            else if (default_handler == (Mod<"signal">().attr("SIG_IGN")))
                            {
                                // Not quite the same thing as SIG_IGN, but the closest we
                                // can make it: do nothing.
                                default_handler = w.function("default_handler", "_InterruptHandler.__init__.<locals>.default_handler", { py_positional("unused_signum"), py_positional("unused_frame") }, [](arguments a) -> py
            {
                py unused_signum = a[0];
                py unused_frame = a[1];
                return none();
            });
                            }
                            else
                            {
                                py_raise((B<"TypeError">())(S<"expected SIGINT signal handler to be signal.SIG_IGN, signal.SIG_DFL, or a callable object">()));
                            }
                        }
                        self.set_attr("default_handler", default_handler);
                        return none();
                    }) },
                { "__call__", w.method("_InterruptHandler", "__call__", { py_positional("signum"), py_positional("frame") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py signum = a[1];
                        py frame = a[2];
                        py installed_handler;
                        py result;
                        installed_handler = (Mod<"signal">().attr("getsignal"))(Mod<"signal">().attr("SIGINT"));
                        if (!installed_handler.is(self))
                        {
                            // if we aren't the installed handler, then delegate immediately
                            // to the default handler
                            self.method("default_handler", { signum, frame });
                        }
                        if (self.attr("called").truthy())
                        {
                            self.method("default_handler", { signum, frame });
                        }
                        self.set_attr("called", boolean(true));
                        for (py const& item : iterate{ (G<"unittest.signals._results">()).method("keys") })
                        {
                            result = item;
                            result.method("stop");
                        }
                        return none();
                    }) }, }));

            define("unittest.signals", "_results", (Mod<"weakref">()).method("WeakKeyDictionary"));
            define("unittest.signals", "registerResult", w.function("registerResult", "registerResult", { py_positional("result") }, [](arguments a) -> py
                {
                    py result = a[0];
                    (G<"unittest.signals._results">()).set_item(result, I<1>());
                    return none();
                }));

            define("unittest.signals", "removeResult", w.function("removeResult", "removeResult", { py_positional("result") }, [](arguments a) -> py
                {
                    py result = a[0];
                    return (B<"bool">())((G<"unittest.signals._results">()).method("pop", { result, none() }));
                }));

            define("unittest.signals", "_interrupt_handler", none());
            define("unittest.signals", "installHandler", w.function("installHandler", "installHandler", {  }, [](arguments a) -> py
                {
                    py default_handler;
                    if ((G<"unittest.signals._interrupt_handler">()).is_none())
                    {
                        default_handler = (Mod<"signal">().attr("getsignal"))(Mod<"signal">().attr("SIGINT"));
                        set_global("unittest.signals", "_interrupt_handler", (G<"unittest.signals._InterruptHandler">())(default_handler));
                        (Mod<"signal">().attr("signal"))(Mod<"signal">().attr("SIGINT"), G<"unittest.signals._interrupt_handler">());
                    }
                    return none();
                }));

            define("unittest.signals", "removeHandler", w.function("removeHandler", "removeHandler", { py_positional("method", none()) }, [](arguments a) -> py
                {
                    auto const method = std::make_shared<py>(a[0]);
                    py inner;
                    if (!(*method).is_none())
                    {
                        inner = (Mod<"functools">().attr("wraps"))((*method))(w.function("inner", "removeHandler.<locals>.inner", {  }, [method](arguments a) -> py
        {
            py args = a[0];
            py kwargs = a[1];
            py initial;
            initial = (Mod<"signal">().attr("getsignal"))(Mod<"signal">().attr("SIGINT"));
            (G<"unittest.signals.removeHandler">())();
            {
                std::optional<py> pending2;
                [[maybe_unused]] int flow2 = 0;
                [[maybe_unused]] py result2;
                try
                {
                    result2 = call_ex((*method), {  }, args, kwargs);
                    flow2 = 1;
                    goto finally2;
                }
                catch (raised const&)
                {
                    pending2 = py_take_exception();
                }
                finally2:
                (Mod<"signal">().attr("signal"))(Mod<"signal">().attr("SIGINT"), initial);
                if (pending2)
                    py_reraise(*pending2);
                if (flow2 == 1)
                    return result2;
            }
            return none();
        }, true, true));
                        return inner;
                    }
                    if (!(G<"unittest.signals._interrupt_handler">()).is_none())
                    {
                        (Mod<"signal">().attr("signal"))(Mod<"signal">().attr("SIGINT"), (G<"unittest.signals._interrupt_handler">()).attr("original_handler"));
                    }
                    return none();
                }));
        }

        bool unittest_signals_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_signals();
                }
                if (auto const doc = unittest_signals_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("signal", py{ fn_import("signal") }.release());
                aAttributes.emplace_back("weakref", py{ fn_import("weakref") }.release());
                aAttributes.emplace_back("wraps", import_attribute("functools", "wraps").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.signals", unittest_signals_module);
    }
}
