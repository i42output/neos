/*
  getopt.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/getopt.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module getopt (CPython 3.13's Lib/getopt.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "getopt_docs.hpp"

namespace neos::python_runtime::detail::getopt_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "getopt.name"))
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
                py_raise("SystemError", std::string{ "getopt: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"getopt.name">()))
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
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "getopt", getopt_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- getopt (Lib/getopt.py)

        void make_getopt()
        {
            static module_writer const& w = writer("getopt");
            // Long option support added by Lars Wirzenius <liw@iki.fi>.
            // 
            // Gerrit Holl <gerrit@nl.linux.org> moved the string-based exceptions
            // to class-based exceptions.
            // 
            // Peter Åstrand <astrand@lysator.liu.se> added gnu_getopt().
            // 
            // TODO for gnu_getopt():
            // 
            // - GNU getopt_long_only mechanism
            // - allow the caller to specify ordering
            // - RETURN_IN_ORDER option
            // - GNU extension with '-' as first character of option string
            // - optional arguments, specified by double colons
            // - an option string with a W followed by semicolon should
            // treat "-W foo" as "--foo"
            define("getopt", "__all__", py_list({ S<"GetoptError">(), S<"error">(), S<"getopt">(), S<"gnu_getopt">() }));
            define("getopt", "_", w.function("_", "_", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    return s;
                }));

            define("getopt", "GetoptError", w.make_class("GetoptError", { B<"Exception">() }, {
                { "opt", S<"">() },
                { "msg", S<"">() },
                { "__init__", w.method("GetoptError", "__init__", { py_positional("msg"), py_positional("opt", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py opt = a[2];
                        self.set_attr("msg", msg);
                        self.set_attr("opt", opt);
                        (B<"Exception">()).method("__init__", { self, msg, opt });
                        return none();
                    }) },
                { "__str__", w.method("GetoptError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("msg");
                    }) }, }));

            define("getopt", "error", G<"getopt.GetoptError">());
            define("getopt", "getopt", w.function("getopt", "getopt", { py_positional("args"), py_positional("shortopts"), py_positional("longopts", py_list()) }, [](arguments a) -> py
                {
                    py args = a[0];
                    py shortopts = a[1];
                    py longopts = a[2];
                    py opts;
                    opts = py_list();
                    if (isinstance(longopts, B<"str">()))
                    {
                        longopts = py_list({ longopts });
                    }
                    else
                    {
                        longopts = (B<"list">())(longopts);
                    }
                    while ((args.truthy() && args[I<0>()].method("startswith", { S<"-">() }).truthy() && (args[I<0>()] != (S<"-">()))))
                    {
                        if (args[I<0>()] == (S<"--">()))
                        {
                            args = args[B<"slice">()(I<1>(), none(), none())];
                            break;
                        }
                        if (args[I<0>()].method("startswith", { S<"--">() }).truthy())
                        {
                            auto const items2 = unpack((G<"getopt.do_longs">())(opts, args[I<0>()][B<"slice">()(I<2>(), none(), none())], longopts, args[B<"slice">()(I<1>(), none(), none())]), 2);
                            opts = items2[0];
                            args = items2[1];
                        }
                        else
                        {
                            auto const items3 = unpack((G<"getopt.do_shorts">())(opts, args[I<0>()][B<"slice">()(I<1>(), none(), none())], shortopts, args[B<"slice">()(I<1>(), none(), none())]), 2);
                            opts = items3[0];
                            args = items3[1];
                        }
                    }
                    return py_tuple({ opts, args });
                }));

            define("getopt", "gnu_getopt", w.function("gnu_getopt", "gnu_getopt", { py_positional("args"), py_positional("shortopts"), py_positional("longopts", py_list()) }, [](arguments a) -> py
                {
                    py args = a[0];
                    py shortopts = a[1];
                    py longopts = a[2];
                    py all_options_first;
                    py opts;
                    py prog_args;
                    opts = py_list();
                    prog_args = py_list();
                    if (isinstance(longopts, B<"str">()))
                    {
                        longopts = py_list({ longopts });
                    }
                    else
                    {
                        longopts = (B<"list">())(longopts);
                    }
                    // Allow options after non-option arguments?
                    if (shortopts.method("startswith", { S<"+">() }).truthy())
                    {
                        shortopts = shortopts[B<"slice">()(I<1>(), none(), none())];
                        all_options_first = boolean(true);
                    }
                    else if ((Mod<"os">().attr("environ")).method("get", { S<"POSIXLY_CORRECT">() }).truthy())
                    {
                        all_options_first = boolean(true);
                    }
                    else
                    {
                        all_options_first = boolean(false);
                    }
                    while (args.truthy())
                    {
                        if (args[I<0>()] == (S<"--">()))
                        {
                            prog_args = inplace(NEOS_PY_ADD, prog_args, args[B<"slice">()(I<1>(), none(), none())]);
                            break;
                        }
                        if (args[I<0>()][B<"slice">()(none(), I<2>(), none())] == (S<"--">()))
                        {
                            auto const items5 = unpack((G<"getopt.do_longs">())(opts, args[I<0>()][B<"slice">()(I<2>(), none(), none())], longopts, args[B<"slice">()(I<1>(), none(), none())]), 2);
                            opts = items5[0];
                            args = items5[1];
                        }
                        else if (((args[I<0>()][B<"slice">()(none(), I<1>(), none())] == (S<"-">())) && (args[I<0>()] != (S<"-">()))))
                        {
                            auto const items6 = unpack((G<"getopt.do_shorts">())(opts, args[I<0>()][B<"slice">()(I<1>(), none(), none())], shortopts, args[B<"slice">()(I<1>(), none(), none())]), 2);
                            opts = items6[0];
                            args = items6[1];
                        }
                        else if (all_options_first.truthy())
                        {
                            prog_args = inplace(NEOS_PY_ADD, prog_args, args);
                            break;
                        }
                        else
                        {
                            prog_args.method("append", { args[I<0>()] });
                            args = args[B<"slice">()(I<1>(), none(), none())];
                        }
                    }
                    return py_tuple({ opts, prog_args });
                }));

            define("getopt", "do_longs", w.function("do_longs", "do_longs", { py_positional("opts"), py_positional("opt"), py_positional("longopts"), py_positional("args") }, [](arguments a) -> py
                {
                    py opts = a[0];
                    py opt = a[1];
                    py longopts = a[2];
                    py args = a[3];
                    py has_arg;
                    py i;
                    py optarg;
                    {
                        bool otherwise7 = false;
                        try
                        {
                            i = opt.method("index", { S<"=">() });
                            otherwise7 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            if (matches(handler7.exception, B<"ValueError">()))
                            {
                                optarg = none();
                            }
                            else
                                py_reraise(handler7.exception);
                        }
                        if (otherwise7)
                        {
                            auto const items8 = unpack(py_tuple({ opt[B<"slice">()(none(), i, none())], opt[B<"slice">()(i + (I<1>()), none(), none())] }), 2);
                            opt = items8[0];
                            optarg = items8[1];
                        }
                    }
                    auto const items9 = unpack((G<"getopt.long_has_args">())(opt, longopts), 2);
                    has_arg = items9[0];
                    opt = items9[1];
                    if (has_arg.truthy())
                    {
                        if (optarg.is_none())
                        {
                            if (!args.truthy())
                            {
                                py_raise((G<"getopt.GetoptError">())((G<"getopt._">())(S<"option --%s requires argument">()) % opt, opt));
                            }
                            auto const items10 = unpack(py_tuple({ args[I<0>()], args[B<"slice">()(I<1>(), none(), none())] }), 2);
                            optarg = items10[0];
                            args = items10[1];
                        }
                    }
                    else if (!optarg.is_none())
                    {
                        py_raise((G<"getopt.GetoptError">())((G<"getopt._">())(S<"option --%s must not have an argument">()) % opt, opt));
                    }
                    opts.method("append", { py_tuple({ (S<"--">()) + opt, [&]() -> py { py const t = optarg; return t.truthy() ? t : S<"">(); }() }) });
                    return py_tuple({ opts, args });
                }));

            // Return:
            // has_arg?
            // full option name
            define("getopt", "long_has_args", w.function("long_has_args", "long_has_args", { py_positional("opt"), py_positional("longopts") }, [](arguments a) -> py
                {
                    py opt = a[0];
                    py longopts = a[1];
                    py has_arg;
                    py possibilities;
                    py unique_match;
                    possibilities = [&]() -> py { py const comprehension11 = py_list(); py o; for (py const& item : iterate{ longopts }) { o = item; if (!o.method("startswith", { opt }).truthy()) continue; comprehension11.method("append", { o }); } return comprehension11; }();
                    if (!possibilities.truthy())
                    {
                        py_raise((G<"getopt.GetoptError">())((G<"getopt._">())(S<"option --%s not recognized">()) % opt, opt));
                    }
                    // Is there an exact match?
                    if (possibilities.contains(opt))
                    {
                        return py_tuple({ boolean(false), opt });
                    }
                    else if (possibilities.contains((opt + (S<"=">()))))
                    {
                        return py_tuple({ boolean(true), opt });
                    }
                    // No exact match, so better be unique.
                    if (py{ possibilities.size() } > (I<1>()))
                    {
                        // XXX since possibilities contains all valid continuations, might be
                        // nice to work them into the error msg
                        py_raise((G<"getopt.GetoptError">())((G<"getopt._">())(S<"option --%s not a unique prefix">()) % opt, opt));
                    }
                    if (!(py{ possibilities.size() } == (I<1>())))
                        py_raise(B<"AssertionError">()());
                    unique_match = possibilities[I<0>()];
                    has_arg = unique_match.method("endswith", { S<"=">() });
                    if (has_arg.truthy())
                    {
                        unique_match = unique_match[B<"slice">()(none(), I<-1>(), none())];
                    }
                    return py_tuple({ has_arg, unique_match });
                }));

            define("getopt", "do_shorts", w.function("do_shorts", "do_shorts", { py_positional("opts"), py_positional("optstring"), py_positional("shortopts"), py_positional("args") }, [](arguments a) -> py
                {
                    py opts = a[0];
                    py optstring = a[1];
                    py shortopts = a[2];
                    py args = a[3];
                    py opt;
                    py optarg;
                    while (optstring != (S<"">()))
                    {
                        auto const items13 = unpack(py_tuple({ optstring[I<0>()], optstring[B<"slice">()(I<1>(), none(), none())] }), 2);
                        opt = items13[0];
                        optstring = items13[1];
                        if ((G<"getopt.short_has_arg">())(opt, shortopts).truthy())
                        {
                            if (optstring == (S<"">()))
                            {
                                if (!args.truthy())
                                {
                                    py_raise((G<"getopt.GetoptError">())((G<"getopt._">())(S<"option -%s requires argument">()) % opt, opt));
                                }
                                auto const items14 = unpack(py_tuple({ args[I<0>()], args[B<"slice">()(I<1>(), none(), none())] }), 2);
                                optstring = items14[0];
                                args = items14[1];
                            }
                            auto const items15 = unpack(py_tuple({ optstring, S<"">() }), 2);
                            optarg = items15[0];
                            optstring = items15[1];
                        }
                        else
                        {
                            optarg = S<"">();
                        }
                        opts.method("append", { py_tuple({ (S<"-">()) + opt, optarg }) });
                    }
                    return py_tuple({ opts, args });
                }));

            define("getopt", "short_has_arg", w.function("short_has_arg", "short_has_arg", { py_positional("opt"), py_positional("shortopts") }, [](arguments a) -> py
                {
                    py opt = a[0];
                    py shortopts = a[1];
                    py i;
                    for (py const& item : iterate{ (B<"range">())(py{ shortopts.size() }) })
                    {
                        i = item;
                        if ((opt == shortopts[i]) && (shortopts[i] != (S<":">())))
                        {
                            return shortopts.method("startswith", { S<":">(), i + (I<1>()) });
                        }
                    }
                    py_raise((G<"getopt.GetoptError">())((G<"getopt._">())(S<"option -%s not recognized">()) % opt, opt));
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool getopt_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_getopt();
                }
                if (auto const doc = getopt_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("getopt", getopt_module);
    }
}
