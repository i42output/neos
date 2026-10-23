/*
  filecmp.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/filecmp.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module filecmp (CPython 3.13's Lib/filecmp.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "filecmp_docs.hpp"

namespace neos::python_runtime::detail::filecmp_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "filecmp.name"))
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
                py_raise("SystemError", std::string{ "filecmp: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"filecmp.name">()))
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
            if (modules_imported("filecmp"))
                return py{ fn_import("filecmp") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "filecmp" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "filecmp", filecmp_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- filecmp (Lib/filecmp.py)

        void make_filecmp()
        {
            static module_writer const& w = writer("filecmp");
            define("filecmp", "__all__", py_list({ S<"clear_cache">(), S<"cmp">(), S<"dircmp">(), S<"cmpfiles">(), S<"DEFAULT_IGNORES">() }));
            define("filecmp", "_cache", py_dict());
            define("filecmp", "BUFSIZE", (I<8>()) * (I<1024>()));
            define("filecmp", "DEFAULT_IGNORES", py_list({ S<"RCS">(), S<"CVS">(), S<"tags">(), S<".git">(), S<".hg">(), S<".bzr">(), S<"_darcs">(), S<"__pycache__">() }));
            define("filecmp", "clear_cache", w.function("clear_cache", "clear_cache", {  }, [](arguments a) -> py
                {
                    (G<"filecmp._cache">()).method("clear");
                    return none();
                }));

            define("filecmp", "cmp", w.function("cmp", "cmp", { py_positional("f1"), py_positional("f2"), py_positional("shallow", boolean(true)) }, [](arguments a) -> py
                {
                    py f1 = a[0];
                    py f2 = a[1];
                    py shallow = a[2];
                    py outcome;
                    py s1;
                    py s2;
                    s1 = (G<"filecmp._sig">())((Mod<"os">().attr("stat"))(f1));
                    s2 = (G<"filecmp._sig">())((Mod<"os">().attr("stat"))(f2));
                    if (((s1[I<0>()] != (Mod<"stat">().attr("S_IFREG"))) || (s2[I<0>()] != (Mod<"stat">().attr("S_IFREG")))))
                    {
                        return boolean(false);
                    }
                    if ((shallow.truthy() && (s1 == s2)))
                    {
                        return boolean(true);
                    }
                    if (s1[I<1>()] != s2[I<1>()])
                    {
                        return boolean(false);
                    }
                    outcome = (G<"filecmp._cache">()).method("get", { py_tuple({ f1, f2, s1, s2 }) });
                    if (outcome.is_none())
                    {
                        outcome = (G<"filecmp._do_cmp">())(f1, f2);
                        if (py{ (G<"filecmp._cache">()).size() } > (I<100>()))
                        {
                            (G<"filecmp.clear_cache">())();
                        }
                        (G<"filecmp._cache">()).set_item(py_tuple({ f1, f2, s1, s2 }), outcome);
                    }
                    return outcome;
                }));

            define("filecmp", "_sig", w.function("_sig", "_sig", { py_positional("st") }, [](arguments a) -> py
                {
                    py st = a[0];
                    return py_tuple({ (Mod<"stat">().attr("S_IFMT"))(st.attr("st_mode")), st.attr("st_size"), st.attr("st_mtime") });
                }));

            define("filecmp", "_do_cmp", w.function("_do_cmp", "_do_cmp", { py_positional("f1"), py_positional("f2") }, [](arguments a) -> py
                {
                    py f1 = a[0];
                    py f2 = a[1];
                    py b1;
                    py b2;
                    py bufsize;
                    py fp1;
                    py fp2;
                    bufsize = G<"filecmp.BUFSIZE">();
                    {
                        py const manager1 = (B<"open">())(f1, S<"rb">());
                        py const exit1 = pyclass::special_method(manager1, "__exit__");
                        py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                        fp1 = value1;
                        [[maybe_unused]] int flow1 = 0;
                        [[maybe_unused]] py result1;
                        bool exited1 = false;
                        try
                        {
                            {
                                py const manager2 = (B<"open">())(f2, S<"rb">());
                                py const exit2 = pyclass::special_method(manager2, "__exit__");
                                py const value2 = pyclass::special_method(manager2, "__enter__")(manager2);
                                fp2 = value2;
                                [[maybe_unused]] int flow2 = 0;
                                [[maybe_unused]] py result2;
                                bool exited2 = false;
                                try
                                {
                                    while (boolean(true).truthy())
                                    {
                                        b1 = fp1.method("read", { bufsize });
                                        b2 = fp2.method("read", { bufsize });
                                        if (b1 != b2)
                                        {
                                            result2 = boolean(false);
                                            flow2 = 1;
                                            goto finally2;
                                        }
                                        if (!b1.truthy())
                                        {
                                            result2 = boolean(true);
                                            flow2 = 1;
                                            goto finally2;
                                        }
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
                                    { result1 = result2; flow1 = 1; goto finally1; }
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

            // Directory comparison class.
            // 
            define("filecmp", "dircmp", w.make_class("dircmp", {  }, {
                { "__init__", w.method("dircmp", "__init__", { py_positional("a"), py_positional("b"), py_positional("ignore", none()), py_positional("hide", none()), py_keyword("shallow", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py ignore = a[3];
                        py hide = a[4];
                        py shallow = a[5];
                        self.set_attr("left", a_);
                        self.set_attr("right", b);
                        if (hide.is_none())
                        {
                            self.set_attr("hide", py_list({ Mod<"os">().attr("curdir"), Mod<"os">().attr("pardir") }));
                        }
                        else
                        {
                            self.set_attr("hide", hide);
                        }
                        if (ignore.is_none())
                        {
                            self.set_attr("ignore", G<"filecmp.DEFAULT_IGNORES">());
                        }
                        else
                        {
                            self.set_attr("ignore", ignore);
                        }
                        self.set_attr("shallow", shallow);
                        return none();
                    }) },
                { "phase0", w.method("dircmp", "phase0", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("left_list", (G<"filecmp._filter">())((Mod<"os">().attr("listdir"))(self.attr("left")), self.attr("hide") + self.attr("ignore")));
                        self.set_attr("right_list", (G<"filecmp._filter">())((Mod<"os">().attr("listdir"))(self.attr("right")), self.attr("hide") + self.attr("ignore")));
                        self.attr("left_list").method("sort");
                        self.attr("right_list").method("sort");
                        return none();
                    }) },
                { "phase1", w.method("dircmp", "phase1", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_;
                        py b;
                        a_ = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("left_list")), self.attr("left_list")));
                        b = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("right_list")), self.attr("right_list")));
                        self.set_attr("common", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (B<"filter">())(b.attr("__contains__"), a_))));
                        self.set_attr("left_only", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(b.attr("__contains__"), a_))));
                        self.set_attr("right_only", (B<"list">())((B<"map">())(b.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(a_.attr("__contains__"), b))));
                        return none();
                    }) },
                { "phase2", w.method("dircmp", "phase2", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_path;
                        py a_stat;
                        py a_type;
                        py b_path;
                        py b_stat;
                        py b_type;
                        py ok;
                        py x;
                        self.set_attr("common_dirs", py_list());
                        self.set_attr("common_files", py_list());
                        self.set_attr("common_funny", py_list());
                        for (py const& item : iterate{ self.attr("common") })
                        {
                            x = item;
                            a_path = (Mod<"os">().attr("path")).method("join", { self.attr("left"), x });
                            b_path = (Mod<"os">().attr("path")).method("join", { self.attr("right"), x });
                            ok = boolean(true);
                            try
                            {
                                a_stat = (Mod<"os">().attr("stat"))(a_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // See https://github.com/python/cpython/issues/122400
                                    // for the rationale for protecting against ValueError.
                                    // print('Can\'t stat', a_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            try
                            {
                                b_stat = (Mod<"os">().attr("stat"))(b_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                if (matches(handler6.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // print('Can\'t stat', b_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler6.exception);
                            }
                            if (ok.truthy())
                            {
                                a_type = (Mod<"stat">().attr("S_IFMT"))(a_stat.attr("st_mode"));
                                b_type = (Mod<"stat">().attr("S_IFMT"))(b_stat.attr("st_mode"));
                                if (a_type != b_type)
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISDIR"))(a_type).truthy())
                                {
                                    self.attr("common_dirs").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISREG"))(a_type).truthy())
                                {
                                    self.attr("common_files").method("append", { x });
                                }
                                else
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                            }
                            else
                            {
                                self.attr("common_funny").method("append", { x });
                            }
                        }
                        return none();
                    }) },
                { "phase3", w.method("dircmp", "phase3", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py xx;
                        xx = (G<"filecmp.cmpfiles">())(self.attr("left"), self.attr("right"), self.attr("common_files"), self.attr("shallow"));
                        auto const items7 = unpack(xx, 3);
                        self.set_attr("same_files", items7[0]);
                        self.set_attr("diff_files", items7[1]);
                        self.set_attr("funny_files", items7[2]);
                        return none();
                    }) },
                { "phase4", w.method("dircmp", "phase4", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_x;
                        py b_x;
                        py x;
                        self.set_attr("subdirs", py_dict());
                        for (py const& item : iterate{ self.attr("common_dirs") })
                        {
                            x = item;
                            a_x = (Mod<"os">().attr("path")).method("join", { self.attr("left"), x });
                            b_x = (Mod<"os">().attr("path")).method("join", { self.attr("right"), x });
                            self.attr("subdirs").set_item(x, self.method("__class__", { a_x, b_x, self.attr("ignore"), self.attr("hide") }, { { "shallow", self.attr("shallow") } }));
                        }
                        return none();
                    }) },
                { "phase4_closure", w.method("dircmp", "phase4_closure", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sd;
                        self.method("phase4");
                        for (py const& item : iterate{ self.attr("subdirs").method("values") })
                        {
                            sd = item;
                            sd.method("phase4_closure");
                        }
                        return none();
                    }) },
                { "report", w.method("dircmp", "report", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"print">())(S<"diff">(), self.attr("left"), self.attr("right"));
                        if (self.attr("left_only").truthy())
                        {
                            self.attr("left_only").method("sort");
                            (B<"print">())(S<"Only in">(), self.attr("left"), S<":">(), self.attr("left_only"));
                        }
                        if (self.attr("right_only").truthy())
                        {
                            self.attr("right_only").method("sort");
                            (B<"print">())(S<"Only in">(), self.attr("right"), S<":">(), self.attr("right_only"));
                        }
                        if (self.attr("same_files").truthy())
                        {
                            self.attr("same_files").method("sort");
                            (B<"print">())(S<"Identical files :">(), self.attr("same_files"));
                        }
                        if (self.attr("diff_files").truthy())
                        {
                            self.attr("diff_files").method("sort");
                            (B<"print">())(S<"Differing files :">(), self.attr("diff_files"));
                        }
                        if (self.attr("funny_files").truthy())
                        {
                            self.attr("funny_files").method("sort");
                            (B<"print">())(S<"Trouble with common files :">(), self.attr("funny_files"));
                        }
                        if (self.attr("common_dirs").truthy())
                        {
                            self.attr("common_dirs").method("sort");
                            (B<"print">())(S<"Common subdirectories :">(), self.attr("common_dirs"));
                        }
                        if (self.attr("common_funny").truthy())
                        {
                            self.attr("common_funny").method("sort");
                            (B<"print">())(S<"Common funny cases :">(), self.attr("common_funny"));
                        }
                        return none();
                    }) },
                { "report_partial_closure", w.method("dircmp", "report_partial_closure", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sd;
                        self.method("report");
                        for (py const& item : iterate{ self.attr("subdirs").method("values") })
                        {
                            sd = item;
                            (B<"print">())();
                            sd.method("report");
                        }
                        return none();
                    }) },
                { "report_full_closure", w.method("dircmp", "report_full_closure", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sd;
                        self.method("report");
                        for (py const& item : iterate{ self.attr("subdirs").method("values") })
                        {
                            sd = item;
                            (B<"print">())();
                            sd.method("report_full_closure");
                        }
                        return none();
                    }) },
                { "methodmap", (B<"dict">()).call({  }, { { "subdirs", w.method("dircmp", "phase4", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_x;
                        py b_x;
                        py x;
                        self.set_attr("subdirs", py_dict());
                        for (py const& item : iterate{ self.attr("common_dirs") })
                        {
                            x = item;
                            a_x = (Mod<"os">().attr("path")).method("join", { self.attr("left"), x });
                            b_x = (Mod<"os">().attr("path")).method("join", { self.attr("right"), x });
                            self.attr("subdirs").set_item(x, self.method("__class__", { a_x, b_x, self.attr("ignore"), self.attr("hide") }, { { "shallow", self.attr("shallow") } }));
                        }
                        return none();
                    }) }, { "same_files", w.method("dircmp", "phase3", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py xx;
                        xx = (G<"filecmp.cmpfiles">())(self.attr("left"), self.attr("right"), self.attr("common_files"), self.attr("shallow"));
                        auto const items7 = unpack(xx, 3);
                        self.set_attr("same_files", items7[0]);
                        self.set_attr("diff_files", items7[1]);
                        self.set_attr("funny_files", items7[2]);
                        return none();
                    }) }, { "diff_files", w.method("dircmp", "phase3", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py xx;
                        xx = (G<"filecmp.cmpfiles">())(self.attr("left"), self.attr("right"), self.attr("common_files"), self.attr("shallow"));
                        auto const items7 = unpack(xx, 3);
                        self.set_attr("same_files", items7[0]);
                        self.set_attr("diff_files", items7[1]);
                        self.set_attr("funny_files", items7[2]);
                        return none();
                    }) }, { "funny_files", w.method("dircmp", "phase3", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py xx;
                        xx = (G<"filecmp.cmpfiles">())(self.attr("left"), self.attr("right"), self.attr("common_files"), self.attr("shallow"));
                        auto const items7 = unpack(xx, 3);
                        self.set_attr("same_files", items7[0]);
                        self.set_attr("diff_files", items7[1]);
                        self.set_attr("funny_files", items7[2]);
                        return none();
                    }) }, { "common_dirs", w.method("dircmp", "phase2", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_path;
                        py a_stat;
                        py a_type;
                        py b_path;
                        py b_stat;
                        py b_type;
                        py ok;
                        py x;
                        self.set_attr("common_dirs", py_list());
                        self.set_attr("common_files", py_list());
                        self.set_attr("common_funny", py_list());
                        for (py const& item : iterate{ self.attr("common") })
                        {
                            x = item;
                            a_path = (Mod<"os">().attr("path")).method("join", { self.attr("left"), x });
                            b_path = (Mod<"os">().attr("path")).method("join", { self.attr("right"), x });
                            ok = boolean(true);
                            try
                            {
                                a_stat = (Mod<"os">().attr("stat"))(a_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // See https://github.com/python/cpython/issues/122400
                                    // for the rationale for protecting against ValueError.
                                    // print('Can\'t stat', a_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            try
                            {
                                b_stat = (Mod<"os">().attr("stat"))(b_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                if (matches(handler6.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // print('Can\'t stat', b_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler6.exception);
                            }
                            if (ok.truthy())
                            {
                                a_type = (Mod<"stat">().attr("S_IFMT"))(a_stat.attr("st_mode"));
                                b_type = (Mod<"stat">().attr("S_IFMT"))(b_stat.attr("st_mode"));
                                if (a_type != b_type)
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISDIR"))(a_type).truthy())
                                {
                                    self.attr("common_dirs").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISREG"))(a_type).truthy())
                                {
                                    self.attr("common_files").method("append", { x });
                                }
                                else
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                            }
                            else
                            {
                                self.attr("common_funny").method("append", { x });
                            }
                        }
                        return none();
                    }) }, { "common_files", w.method("dircmp", "phase2", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_path;
                        py a_stat;
                        py a_type;
                        py b_path;
                        py b_stat;
                        py b_type;
                        py ok;
                        py x;
                        self.set_attr("common_dirs", py_list());
                        self.set_attr("common_files", py_list());
                        self.set_attr("common_funny", py_list());
                        for (py const& item : iterate{ self.attr("common") })
                        {
                            x = item;
                            a_path = (Mod<"os">().attr("path")).method("join", { self.attr("left"), x });
                            b_path = (Mod<"os">().attr("path")).method("join", { self.attr("right"), x });
                            ok = boolean(true);
                            try
                            {
                                a_stat = (Mod<"os">().attr("stat"))(a_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // See https://github.com/python/cpython/issues/122400
                                    // for the rationale for protecting against ValueError.
                                    // print('Can\'t stat', a_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            try
                            {
                                b_stat = (Mod<"os">().attr("stat"))(b_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                if (matches(handler6.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // print('Can\'t stat', b_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler6.exception);
                            }
                            if (ok.truthy())
                            {
                                a_type = (Mod<"stat">().attr("S_IFMT"))(a_stat.attr("st_mode"));
                                b_type = (Mod<"stat">().attr("S_IFMT"))(b_stat.attr("st_mode"));
                                if (a_type != b_type)
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISDIR"))(a_type).truthy())
                                {
                                    self.attr("common_dirs").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISREG"))(a_type).truthy())
                                {
                                    self.attr("common_files").method("append", { x });
                                }
                                else
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                            }
                            else
                            {
                                self.attr("common_funny").method("append", { x });
                            }
                        }
                        return none();
                    }) }, { "common_funny", w.method("dircmp", "phase2", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_path;
                        py a_stat;
                        py a_type;
                        py b_path;
                        py b_stat;
                        py b_type;
                        py ok;
                        py x;
                        self.set_attr("common_dirs", py_list());
                        self.set_attr("common_files", py_list());
                        self.set_attr("common_funny", py_list());
                        for (py const& item : iterate{ self.attr("common") })
                        {
                            x = item;
                            a_path = (Mod<"os">().attr("path")).method("join", { self.attr("left"), x });
                            b_path = (Mod<"os">().attr("path")).method("join", { self.attr("right"), x });
                            ok = boolean(true);
                            try
                            {
                                a_stat = (Mod<"os">().attr("stat"))(a_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // See https://github.com/python/cpython/issues/122400
                                    // for the rationale for protecting against ValueError.
                                    // print('Can\'t stat', a_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            try
                            {
                                b_stat = (Mod<"os">().attr("stat"))(b_path);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                if (matches(handler6.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                                {
                                    // print('Can\'t stat', b_path, ':', why.args[1])
                                    ok = boolean(false);
                                }
                                else
                                    py_reraise(handler6.exception);
                            }
                            if (ok.truthy())
                            {
                                a_type = (Mod<"stat">().attr("S_IFMT"))(a_stat.attr("st_mode"));
                                b_type = (Mod<"stat">().attr("S_IFMT"))(b_stat.attr("st_mode"));
                                if (a_type != b_type)
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISDIR"))(a_type).truthy())
                                {
                                    self.attr("common_dirs").method("append", { x });
                                }
                                else if ((Mod<"stat">().attr("S_ISREG"))(a_type).truthy())
                                {
                                    self.attr("common_files").method("append", { x });
                                }
                                else
                                {
                                    self.attr("common_funny").method("append", { x });
                                }
                            }
                            else
                            {
                                self.attr("common_funny").method("append", { x });
                            }
                        }
                        return none();
                    }) }, { "common", w.method("dircmp", "phase1", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_;
                        py b;
                        a_ = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("left_list")), self.attr("left_list")));
                        b = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("right_list")), self.attr("right_list")));
                        self.set_attr("common", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (B<"filter">())(b.attr("__contains__"), a_))));
                        self.set_attr("left_only", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(b.attr("__contains__"), a_))));
                        self.set_attr("right_only", (B<"list">())((B<"map">())(b.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(a_.attr("__contains__"), b))));
                        return none();
                    }) }, { "left_only", w.method("dircmp", "phase1", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_;
                        py b;
                        a_ = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("left_list")), self.attr("left_list")));
                        b = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("right_list")), self.attr("right_list")));
                        self.set_attr("common", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (B<"filter">())(b.attr("__contains__"), a_))));
                        self.set_attr("left_only", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(b.attr("__contains__"), a_))));
                        self.set_attr("right_only", (B<"list">())((B<"map">())(b.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(a_.attr("__contains__"), b))));
                        return none();
                    }) }, { "right_only", w.method("dircmp", "phase1", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_;
                        py b;
                        a_ = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("left_list")), self.attr("left_list")));
                        b = (B<"dict">())((B<"zip">())((B<"map">())((Mod<"os">().attr("path")).attr("normcase"), self.attr("right_list")), self.attr("right_list")));
                        self.set_attr("common", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (B<"filter">())(b.attr("__contains__"), a_))));
                        self.set_attr("left_only", (B<"list">())((B<"map">())(a_.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(b.attr("__contains__"), a_))));
                        self.set_attr("right_only", (B<"list">())((B<"map">())(b.attr("__getitem__"), (Mod<"itertools">().attr("filterfalse"))(a_.attr("__contains__"), b))));
                        return none();
                    }) }, { "left_list", w.method("dircmp", "phase0", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("left_list", (G<"filecmp._filter">())((Mod<"os">().attr("listdir"))(self.attr("left")), self.attr("hide") + self.attr("ignore")));
                        self.set_attr("right_list", (G<"filecmp._filter">())((Mod<"os">().attr("listdir"))(self.attr("right")), self.attr("hide") + self.attr("ignore")));
                        self.attr("left_list").method("sort");
                        self.attr("right_list").method("sort");
                        return none();
                    }) }, { "right_list", w.method("dircmp", "phase0", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("left_list", (G<"filecmp._filter">())((Mod<"os">().attr("listdir"))(self.attr("left")), self.attr("hide") + self.attr("ignore")));
                        self.set_attr("right_list", (G<"filecmp._filter">())((Mod<"os">().attr("listdir"))(self.attr("right")), self.attr("hide") + self.attr("ignore")));
                        self.attr("left_list").method("sort");
                        self.attr("right_list").method("sort");
                        return none();
                    }) } }) },
                { "__getattr__", w.method("dircmp", "__getattr__", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        if (!self.attr("methodmap").contains(attr))
                        {
                            py_raise((B<"AttributeError">())(attr));
                        }
                        self.attr("methodmap")[attr](self);
                        return (B<"getattr">())(self, attr);
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

            define("filecmp", "cmpfiles", w.function("cmpfiles", "cmpfiles", { py_positional("a"), py_positional("b"), py_positional("common"), py_positional("shallow", boolean(true)) }, [](arguments a) -> py
                {
                    py a_ = a[0];
                    py b = a[1];
                    py common = a[2];
                    py shallow = a[3];
                    py ax;
                    py bx;
                    py res;
                    py x;
                    res = py_tuple({ py_list(), py_list(), py_list() });
                    for (py const& item : iterate{ common })
                    {
                        x = item;
                        ax = (Mod<"os">().attr("path")).method("join", { a_, x });
                        bx = (Mod<"os">().attr("path")).method("join", { b, x });
                        res[(G<"filecmp._cmp">())(ax, bx, shallow)].method("append", { x });
                    }
                    return res;
                }));

            // Compare two files.
            // Return:
            // 0 for equal
            // 1 for different
            // 2 for funny cases (can't stat, NUL bytes, etc.)
            // 
            define("filecmp", "_cmp", w.function("_cmp", "_cmp", { py_positional("a"), py_positional("b"), py_positional("sh"), py_positional("abs", B<"abs">()), py_positional("cmp", G<"filecmp.cmp">()) }, [](arguments a) -> py
                {
                    py a_ = a[0];
                    py b = a[1];
                    py sh = a[2];
                    py abs = a[3];
                    py cmp = a[4];
                    try
                    {
                        return boolean(!abs(cmp(a_, b, sh)).truthy());
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler13;
                        if (matches(handler13.exception, py_tuple({ B<"OSError">(), B<"ValueError">() })))
                        {
                            return I<2>();
                        }
                        else
                            py_reraise(handler13.exception);
                    }
                    return none();
                }));

            // Return a copy with items that occur in skip removed.
            // 
            define("filecmp", "_filter", w.function("_filter", "_filter", { py_positional("flist"), py_positional("skip") }, [](arguments a) -> py
                {
                    py flist = a[0];
                    py skip = a[1];
                    return (B<"list">())((Mod<"itertools">().attr("filterfalse"))(skip.attr("__contains__"), flist));
                }));

            // Demonstration and testing.
            // 
            define("filecmp", "demo", w.function("demo", "demo", {  }, [](arguments a) -> py
                {
                    py args;
                    py dd;
                    py getopt;
                    py options;
                    py sys;
                    sys = module_of("sys");
                    getopt = module_of("getopt");
                    auto const items14 = unpack(getopt.method("getopt", { sys.attr("argv")[B<"slice">()(I<1>(), none(), none())], S<"r">() }), 2);
                    options = items14[0];
                    args = items14[1];
                    if (py{ args.size() } != (I<2>()))
                    {
                        py_raise(getopt.method("GetoptError", { S<"need exactly two args">(), none() }));
                    }
                    dd = (G<"filecmp.dircmp">())(args[I<0>()], args[I<1>()]);
                    if (options.contains(py_tuple({ S<"-r">(), S<"">() })))
                    {
                        dd.method("report_full_closure");
                    }
                    else
                    {
                        dd.method("report");
                    }
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool filecmp_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_filecmp();
                }
                if (auto const doc = filecmp_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("stat", py{ fn_import("stat") }.release());
                aAttributes.emplace_back("filterfalse", import_attribute("itertools", "filterfalse").release());
                aAttributes.emplace_back("GenericAlias", import_attribute("types", "GenericAlias").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("filecmp", filecmp_module);
    }
}
