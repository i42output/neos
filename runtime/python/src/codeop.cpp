/*
  codeop.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/codeop.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module codeop (CPython 3.13's Lib/codeop.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "codeop_docs.hpp"

namespace neos::python_runtime::detail::codeop_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "codeop.name"))
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
                py_raise("SystemError", std::string{ "codeop: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"codeop.name">()))
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
            if (modules_imported("codeop"))
                return py{ fn_import("codeop") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "codeop" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "codeop", codeop_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- codeop (Lib/codeop.py)

        void make_codeop()
        {
            static module_writer const& w = writer("codeop");
            define("codeop", "_features", [&]() -> py { py const comprehension1 = py_list(); py fname; for (py const& item : iterate{ Mod<"__future__">().attr("all_feature_names") }) { fname = item; comprehension1.method("append", { (B<"getattr">())(Mod<"__future__">(), fname) }); } return comprehension1; }());
            define("codeop", "__all__", py_list({ S<"compile_command">(), S<"Compile">(), S<"CommandCompiler">() }));
            // The following flags match the values from Include/cpython/compile.h
            // Caveat emptor: These flags are undocumented on purpose and depending
            // on their effect outside the standard library is **unsupported**.
            define("codeop", "PyCF_DONT_IMPLY_DEDENT", I<512>());
            define("codeop", "PyCF_ONLY_AST", I<1024>());
            define("codeop", "PyCF_ALLOW_INCOMPLETE_INPUT", I<16384>());
            define("codeop", "_maybe_compile", w.function("_maybe_compile", "_maybe_compile", { py_positional("compiler"), py_positional("source"), py_positional("filename"), py_positional("symbol") }, [](arguments a) -> py
                {
                    py compiler = a[0];
                    py source = a[1];
                    py filename = a[2];
                    py symbol = a[3];
                    py e;
                    py line;
                    bool broke2 = false;
                    for (py const& item : iterate{ source.method("split", { S<"\n">() }) })
                    {
                        line = item;
                        line = line.method("strip");
                        if ((line.truthy() && (line[I<0>()] != (S<"#">()))))
                        {
                            broke2 = true;
                            break;
                        }
                    }
                    if (!broke2)
                    {
                        if (symbol != (S<"eval">()))
                        {
                            source = S<"pass">();
                        }
                    }
                    // Disable compiler warnings when checking for incomplete input.
                    {
                        py const manager3 = (Mod<"warnings">().attr("catch_warnings"))();
                        py const exit3 = pyclass::special_method(manager3, "__exit__");
                        py const value3 = pyclass::special_method(manager3, "__enter__")(manager3);
                        [[maybe_unused]] int flow3 = 0;
                        [[maybe_unused]] py result3;
                        bool exited3 = false;
                        try
                        {
                            (Mod<"warnings">().attr("simplefilter"))(S<"ignore">(), py_tuple({ B<"SyntaxWarning">(), B<"DeprecationWarning">() }));
                            try
                            {
                                compiler(source, filename, symbol);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler4;
                                if (matches(handler4.exception, B<"SyntaxError">()))
                                {
                                    try
                                    {
                                        compiler(source + (S<"\n">()), filename, symbol);
                                        result3 = none();
                                        flow3 = 1;
                                        goto finally3;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler5;
                                        if (matches(handler5.exception, B<"_IncompleteInputError">()))
                                        {
                                            e = handler5.exception;
                                            result3 = none();
                                            flow3 = 1;
                                            goto finally3;
                                        }
                                        else if (matches(handler5.exception, B<"SyntaxError">()))
                                        {
                                            e = handler5.exception;
                                        }
                                        else
                                            py_reraise(handler5.exception);
                                    }
                                }
                                else
                                    py_reraise(handler4.exception);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            exited3 = true;
                            if (!exit3(manager3, type_of(handler3.exception), handler3.exception, handler3.exception.attr("__traceback__")).truthy())
                                py_reraise(handler3.exception);
                        }
                        finally3:
                        if (!exited3)
                            exit3(manager3, none(), none(), none());
                        if (flow3 == 1)
                            return result3;
                    }
                    // fallthrough
                    return compiler.call({ source, filename, symbol }, { { "incomplete_input", boolean(false) } });
                }));

            define("codeop", "_compile", w.function("_compile", "_compile", { py_positional("source"), py_positional("filename"), py_positional("symbol"), py_positional("incomplete_input", boolean(true)) }, [](arguments a) -> py
                {
                    py source = a[0];
                    py filename = a[1];
                    py symbol = a[2];
                    py incomplete_input = a[3];
                    py flags;
                    flags = I<0>();
                    if (incomplete_input.truthy())
                    {
                        flags = inplace(NEOS_PY_OR, flags, G<"codeop.PyCF_ALLOW_INCOMPLETE_INPUT">());
                        flags = inplace(NEOS_PY_OR, flags, G<"codeop.PyCF_DONT_IMPLY_DEDENT">());
                    }
                    return (B<"compile">())(source, filename, symbol, flags);
                }));

            define("codeop", "compile_command", w.function("compile_command", "compile_command", { py_positional("source"), py_positional("filename", S<"<input>">()), py_positional("symbol", S<"single">()) }, [](arguments a) -> py
                {
                    py source = a[0];
                    py filename = a[1];
                    py symbol = a[2];
                    return (G<"codeop._maybe_compile">())(G<"codeop._compile">(), source, filename, symbol);
                }));

            define("codeop", "Compile", w.make_class("Compile", {  }, {
                { "__init__", w.method("Compile", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("flags", (G<"codeop.PyCF_DONT_IMPLY_DEDENT">()) | (G<"codeop.PyCF_ALLOW_INCOMPLETE_INPUT">()));
                        return none();
                    }) },
                { "__call__", w.method("Compile", "__call__", { py_positional("source"), py_positional("filename"), py_positional("symbol"), py_positional("flags", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py filename = a[2];
                        py symbol = a[3];
                        py flags = a[4];
                        py kwargs = a[5];
                        py codeob;
                        py feature;
                        flags = inplace(NEOS_PY_OR, flags, self.attr("flags"));
                        if (kwargs.method("get", { S<"incomplete_input">(), boolean(true) }).is(boolean(false)))
                        {
                            flags = inplace(NEOS_PY_AND, flags, py{ unary(NEOS_PY_INVERT, (G<"codeop.PyCF_DONT_IMPLY_DEDENT">()).get()) });
                            flags = inplace(NEOS_PY_AND, flags, py{ unary(NEOS_PY_INVERT, (G<"codeop.PyCF_ALLOW_INCOMPLETE_INPUT">()).get()) });
                        }
                        codeob = (B<"compile">())(source, filename, symbol, flags, boolean(true));
                        if ((flags & (G<"codeop.PyCF_ONLY_AST">())).truthy())
                        {
                            return codeob;
                        }
                        for (py const& item : iterate{ G<"codeop._features">() })
                        {
                            feature = item;
                            if ((codeob.attr("co_flags") & feature.attr("compiler_flag")).truthy())
                            {
                                {
                                    py const target7 = self;
                                    target7.set_attr("flags", inplace(NEOS_PY_OR, target7.attr("flags"), feature.attr("compiler_flag")));
                                }
                            }
                        }
                        return codeob;
                    }, false, true) }, }));

            define("codeop", "CommandCompiler", w.make_class("CommandCompiler", {  }, {
                { "__init__", w.method("CommandCompiler", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("compiler", (G<"codeop.Compile">())());
                        return none();
                    }) },
                { "__call__", w.method("CommandCompiler", "__call__", { py_positional("source"), py_positional("filename", S<"<input>">()), py_positional("symbol", S<"single">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py filename = a[2];
                        py symbol = a[3];
                        return (G<"codeop._maybe_compile">())(self.attr("compiler"), source, filename, symbol);
                    }) }, }));
        }

        bool codeop_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_codeop();
                }
                if (auto const doc = codeop_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("__future__", py{ fn_import("__future__") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("codeop", codeop_module);
    }
}
