/*
  zipfile_path.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/zipfile/_path/__init__.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module zipfile._path (CPython 3.13's Lib/zipfile/_path/__init__.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "zipfile_path_docs.hpp"

namespace neos::python_runtime::detail::zipfile__path_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "zipfile._path.name"))
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
                py_raise("SystemError", std::string{ "zipfile._path: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"zipfile._path.name">()))
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
            static module_writer const sWriter{ "zipfile._path", zipfile_path_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- zipfile._path (Lib/zipfile/_path/__init__.py)

        void make_zipfile__path()
        {
            static module_writer const& w = writer("zipfile._path");
            define("zipfile._path", "__all__", py_list({ S<"Path">() }));
            define("zipfile._path", "_parents", w.function("_parents", "_parents", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    return (Mod<"itertools">().attr("islice"))((G<"zipfile._path._ancestry">())(path), I<1>(), none());
                }));

            define("zipfile._path", "_ancestry", w.function("_ancestry", "_ancestry", { py_positional("path") }, [](arguments a) -> py
                {
                    return make_native_generator("_ancestry", "_ancestry", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py path = a[0];
                        py tail;
                        path = path.method("rstrip", { Mod<"posixpath">().attr("sep") });
                        while (path.method("rstrip", { Mod<"posixpath">().attr("sep") }).truthy())
                        {
                            py_yield(path);
                            auto const items2 = unpack((Mod<"posixpath">().attr("split"))(path), 2);
                            path = items2[0];
                            tail = items2[1];
                        }
                        return none();
                    });
                }));

            define("zipfile._path", "_dedupe", (B<"dict">()).attr("fromkeys"));
            define("zipfile._path", "_difference", w.function("_difference", "_difference", { py_positional("minuend"), py_positional("subtrahend") }, [](arguments a) -> py
                {
                    py minuend = a[0];
                    py subtrahend = a[1];
                    return (Mod<"itertools">().attr("filterfalse"))((B<"set">())(subtrahend).attr("__contains__"), minuend);
                }));

            define("zipfile._path", "InitializedState", w.make_class("InitializedState", {  }, {
                { "__init__", w.method("InitializedState", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        self.set_attr("_InitializedState__args", args);
                        self.set_attr("_InitializedState__kwargs", kwargs);
                        call_ex((B<"super">()(G<"zipfile._path.InitializedState">(), self)).attr("__init__"), {  }, args, kwargs);
                        return none();
                    }, true, true) },
                { "__getstate__", w.method("InitializedState", "__getstate__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("_InitializedState__args"), self.attr("_InitializedState__kwargs") });
                    }) },
                { "__setstate__", w.method("InitializedState", "__setstate__", { py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py state = a[1];
                        py args;
                        py kwargs;
                        auto const items3 = unpack(state, 2);
                        args = items3[0];
                        kwargs = items3[1];
                        call_ex((B<"super">()(G<"zipfile._path.InitializedState">(), self)).attr("__init__"), {  }, args, kwargs);
                        return none();
                    }) }, }));

            define("zipfile._path", "CompleteDirs", w.make_class("CompleteDirs", { G<"zipfile._path.InitializedState">(), Mod<"zipfile">().attr("ZipFile") }, {
                { "_implied_dirs", w.static_method("CompleteDirs", "_implied_dirs", { py_positional("names") }, [](arguments a) -> py
                    {
                        py names = a[0];
                        py as_dirs;
                        py parents;
                        parents = (Mod<"itertools">().attr("chain")).method("from_iterable", { (B<"map">())(G<"zipfile._path._parents">(), names) });
                        as_dirs = [&]() -> py { py const comprehension4 = py_list(); py p; for (py const& item : iterate{ parents }) { p = item; comprehension4.method("append", { p + (Mod<"posixpath">().attr("sep")) }); } return comprehension4; }();
                        return (G<"zipfile._path._dedupe">())((G<"zipfile._path._difference">())(as_dirs, names));
                    }) },
                { "namelist", w.method("CompleteDirs", "namelist", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py names;
                        names = (B<"super">()(G<"zipfile._path.CompleteDirs">(), self)).method("namelist");
                        return names + (B<"list">())(self.method("_implied_dirs", { names }));
                    }) },
                { "_name_set", w.method("CompleteDirs", "_name_set", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"set">())(self.method("namelist"));
                    }) },
                { "resolve_dir", w.method("CompleteDirs", "resolve_dir", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py dir_match;
                        py dirname;
                        py names;
                        names = self.method("_name_set");
                        dirname = name + (S<"/">());
                        dir_match = [&]() -> py { py const t = boolean(!names.contains(name)); return !t.truthy() ? t : boolean(names.contains(dirname)); }();
                        return (dir_match.truthy() ? dirname : name);
                    }) },
                { "getinfo", w.method("CompleteDirs", "getinfo", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        try
                        {
                            return (B<"super">()(G<"zipfile._path.CompleteDirs">(), self)).method("getinfo", { name });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, B<"KeyError">()))
                            {
                                if (((!name.method("endswith", { S<"/">() }).truthy()) || (!self.method("_name_set").contains(name))))
                                {
                                    py_reraise(handler5.exception);
                                }
                                return (Mod<"zipfile">().attr("ZipInfo")).call({  }, { { "filename", name } });
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                        return none();
                    }) },
                { "make", w.class_method("CompleteDirs", "make", { py_positional("source") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py source = a[1];
                        if (isinstance(source, G<"zipfile._path.CompleteDirs">()))
                        {
                            return source;
                        }
                        if (!isinstance(source, Mod<"zipfile">().attr("ZipFile")))
                        {
                            return cls(source);
                        }
                        // Only allow for FastLookup when supplied zipfile is read-only
                        if (!source.attr("mode").contains((S<"r">())))
                        {
                            cls = G<"zipfile._path.CompleteDirs">();
                        }
                        source.set_attr("__class__", cls);
                        return source;
                    }) },
                { "inject", w.class_method("CompleteDirs", "inject", { py_positional("zf") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py zf = a[1];
                        py name;
                        for (py const& item : iterate{ cls.method("_implied_dirs", { zf.method("namelist") }) })
                        {
                            name = item;
                            zf.method("writestr", { name, py{ make_bytes(std::string{ "", 0 }) } });
                        }
                        return zf;
                    }) }, }));

            define("zipfile._path", "FastLookup", w.make_class("FastLookup", { G<"zipfile._path.CompleteDirs">() }, {
                { "namelist", w.method("FastLookup", "namelist", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const manager7 = (Mod<"contextlib">().attr("suppress"))(B<"AttributeError">());
                            py const exit7 = pyclass::special_method(manager7, "__exit__");
                            py const value7 = pyclass::special_method(manager7, "__enter__")(manager7);
                            [[maybe_unused]] int flow7 = 0;
                            [[maybe_unused]] py result7;
                            bool exited7 = false;
                            try
                            {
                                result7 = self.attr("_FastLookup__names");
                                flow7 = 1;
                                goto finally7;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler7;
                                exited7 = true;
                                if (!exit7(manager7, type_of(handler7.exception), handler7.exception, handler7.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler7.exception);
                            }
                            finally7:
                            if (!exited7)
                                exit7(manager7, none(), none(), none());
                            if (flow7 == 1)
                                return result7;
                        }
                        self.set_attr("_FastLookup__names", (B<"super">()(G<"zipfile._path.FastLookup">(), self)).method("namelist"));
                        return self.attr("_FastLookup__names");
                    }) },
                { "_name_set", w.method("FastLookup", "_name_set", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const manager8 = (Mod<"contextlib">().attr("suppress"))(B<"AttributeError">());
                            py const exit8 = pyclass::special_method(manager8, "__exit__");
                            py const value8 = pyclass::special_method(manager8, "__enter__")(manager8);
                            [[maybe_unused]] int flow8 = 0;
                            [[maybe_unused]] py result8;
                            bool exited8 = false;
                            try
                            {
                                result8 = self.attr("_FastLookup__lookup");
                                flow8 = 1;
                                goto finally8;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler8;
                                exited8 = true;
                                if (!exit8(manager8, type_of(handler8.exception), handler8.exception, handler8.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler8.exception);
                            }
                            finally8:
                            if (!exited8)
                                exit8(manager8, none(), none(), none());
                            if (flow8 == 1)
                                return result8;
                        }
                        self.set_attr("_FastLookup__lookup", (B<"super">()(G<"zipfile._path.FastLookup">(), self)).method("_name_set"));
                        return self.attr("_FastLookup__lookup");
                    }) }, }));

            define("zipfile._path", "_extract_text_encoding", w.function("_extract_text_encoding", "_extract_text_encoding", { py_positional("encoding", none()) }, [](arguments a) -> py
                {
                    py encoding = a[0];
                    py args = a[1];
                    py kwargs = a[2];
                    py is_old_pypi;
                    py is_pypy;
                    py stack_level;
                    is_pypy = boolean((Mod<"sys">().attr("implementation")).attr("name") == (S<"pypy">()));
                    // PyPy no longer special cased after 7.3.19 (or maybe 7.3.18)
                    // See jaraco/zipp#143
                    is_old_pypi = [&]() -> py { py const t = is_pypy; return !t.truthy() ? t : boolean((Mod<"sys">().attr("pypy_version_info")) < py_tuple({ I<7>(), I<3>(), I<19>() })); }();
                    stack_level = (I<3>()) + is_old_pypi;
                    return py_tuple({ (Mod<"io">().attr("text_encoding"))(encoding, stack_level), args, kwargs });
                }, true, true));

            define("zipfile._path", "Path", w.make_class("Path", {  }, {
                { "_Path__repr", S<"{self.__class__.__name__}({self.root.filename!r}, {self.at!r})">() },
                { "__init__", w.method("Path", "__init__", { py_positional("root"), py_positional("at", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py root = a[1];
                        py at = a[2];
                        self.set_attr("root", (G<"zipfile._path.FastLookup">()).method("make", { root }));
                        self.set_attr("at", at);
                        return none();
                    }) },
                { "__eq__", w.method("Path", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!self.attr("__class__").is(other.attr("__class__")))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(py_tuple({ self.attr("root"), self.attr("at") }) == py_tuple({ other.attr("root"), other.attr("at") }));
                    }) },
                { "__hash__", w.method("Path", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(py_tuple({ self.attr("root"), self.attr("at") }));
                    }) },
                { "open", w.method("Path", "open", { py_positional("mode", S<"r">()), py_keyword("pwd", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mode = a[1];
                        py pwd = a[2];
                        py args = a[3];
                        py kwargs = a[4];
                        py encoding;
                        py stream;
                        py zip_mode;
                        if (self.method("is_dir").truthy())
                        {
                            py_raise((B<"IsADirectoryError">())(self));
                        }
                        zip_mode = mode[I<0>()];
                        if (((zip_mode == (S<"r">())) && (!self.method("exists").truthy())))
                        {
                            py_raise((B<"FileNotFoundError">())(self));
                        }
                        stream = self.attr("root").method("open", { self.attr("at"), zip_mode }, { { "pwd", pwd } });
                        if (mode.contains((S<"b">())))
                        {
                            if ((args.truthy() || kwargs.truthy()))
                            {
                                py_raise((B<"ValueError">())(S<"encoding args invalid for binary operation">()));
                            }
                            return stream;
                        }
                        // Text mode:
                        auto const items9 = unpack(call_ex((G<"zipfile._path._extract_text_encoding">()), {  }, args, kwargs), 3);
                        encoding = items9[0];
                        args = items9[1];
                        kwargs = items9[2];
                        return call_ex((Mod<"io">().attr("TextIOWrapper")), { stream, encoding }, args, kwargs);
                    }, true, true) },
                { "_base", w.method("Path", "_base", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (self.attr("at").truthy() ? (Mod<"pathlib">().attr("PurePosixPath"))(self.attr("at")) : self.attr("filename"));
                    }) },
                { "name", B<"property">()(w.method("Path", "name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_base").attr("name");
                    })) },
                { "suffix", B<"property">()(w.method("Path", "suffix", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_base").attr("suffix");
                    })) },
                { "suffixes", B<"property">()(w.method("Path", "suffixes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_base").attr("suffixes");
                    })) },
                { "stem", B<"property">()(w.method("Path", "stem", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_base").attr("stem");
                    })) },
                { "filename", B<"property">()(w.method("Path", "filename", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"pathlib">().attr("Path"))(self.attr("root").attr("filename")).method("joinpath", { self.attr("at") });
                    })) },
                { "read_text", w.method("Path", "read_text", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py encoding;
                        py strm;
                        auto const items10 = unpack(call_ex((G<"zipfile._path._extract_text_encoding">()), {  }, args, kwargs), 3);
                        encoding = items10[0];
                        args = items10[1];
                        kwargs = items10[2];
                        {
                            py const manager11 = call_ex(self.attr("open"), { S<"r">(), encoding }, args, kwargs);
                            py const exit11 = pyclass::special_method(manager11, "__exit__");
                            py const value11 = pyclass::special_method(manager11, "__enter__")(manager11);
                            strm = value11;
                            [[maybe_unused]] int flow11 = 0;
                            [[maybe_unused]] py result11;
                            bool exited11 = false;
                            try
                            {
                                result11 = strm.method("read");
                                flow11 = 1;
                                goto finally11;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler11;
                                exited11 = true;
                                if (!exit11(manager11, type_of(handler11.exception), handler11.exception, handler11.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler11.exception);
                            }
                            finally11:
                            if (!exited11)
                                exit11(manager11, none(), none(), none());
                            if (flow11 == 1)
                                return result11;
                        }
                        return none();
                    }, true, true) },
                { "read_bytes", w.method("Path", "read_bytes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py strm;
                        {
                            py const manager12 = self.method("open", { S<"rb">() });
                            py const exit12 = pyclass::special_method(manager12, "__exit__");
                            py const value12 = pyclass::special_method(manager12, "__enter__")(manager12);
                            strm = value12;
                            [[maybe_unused]] int flow12 = 0;
                            [[maybe_unused]] py result12;
                            bool exited12 = false;
                            try
                            {
                                result12 = strm.method("read");
                                flow12 = 1;
                                goto finally12;
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
                        return none();
                    }) },
                { "_is_child", w.method("Path", "_is_child", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        return boolean((Mod<"posixpath">().attr("dirname"))(path.attr("at").method("rstrip", { S<"/">() })) == self.attr("at").method("rstrip", { S<"/">() }));
                    }) },
                { "_next", w.method("Path", "_next", { py_positional("at") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py at = a[1];
                        return self.method("__class__", { self.attr("root"), at });
                    }) },
                { "is_dir", w.method("Path", "is_dir", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(!self.attr("at").truthy()); return t.truthy() ? t : self.attr("at").method("endswith", { S<"/">() }); }();
                    }) },
                { "is_file", w.method("Path", "is_file", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.method("exists"); return !t.truthy() ? t : boolean(!self.method("is_dir").truthy()); }();
                    }) },
                { "exists", w.method("Path", "exists", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("root").method("_name_set").contains(self.attr("at")));
                    }) },
                { "iterdir", w.method("Path", "iterdir", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py subs;
                        if (!self.method("is_dir").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Can't listdir a file">()));
                        }
                        subs = (B<"map">())(self.attr("_next"), self.attr("root").method("namelist"));
                        return (B<"filter">())(self.attr("_is_child"), subs);
                    }) },
                { "match", w.method("Path", "match", { py_positional("path_pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path_pattern = a[1];
                        return (Mod<"pathlib">().attr("PurePosixPath"))(self.attr("at")).method("match", { path_pattern });
                    }) },
                { "is_symlink", w.method("Path", "is_symlink", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        py mode;
                        info = self.attr("root").method("getinfo", { self.attr("at") });
                        mode = info.attr("external_attr") >> (I<16>());
                        return (Mod<"stat">().attr("S_ISLNK"))(mode);
                    }) },
                { "glob", w.method("Path", "glob", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        py matches_;
                        py prefix;
                        py tr;
                        if (!pattern.truthy())
                        {
                            py_raise((B<"ValueError">())(S(std::string{ "Unacceptable pattern: " } + pattern.repr())));
                        }
                        prefix = (Mod<"re">().attr("escape"))(self.attr("at"));
                        tr = (Mod<"zipfile._path.glob">().attr("Translator")).call({  }, { { "seps", S<"/">() } });
                        matches_ = (Mod<"re">().attr("compile"))(prefix + tr.method("translate", { pattern })).attr("fullmatch");
                        return (B<"map">())(self.attr("_next"), (B<"filter">())(matches_, self.attr("root").method("namelist")));
                    }) },
                { "rglob", w.method("Path", "rglob", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        return self.method("glob", { S(std::string{ "**/" } + pattern.str()) });
                    }) },
                { "relative_to", w.method("Path", "relative_to", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py extra = a[2];
                        return (Mod<"posixpath">().attr("relpath"))((B<"str">())(self), (B<"str">())(call_ex(other.attr("joinpath"), {  }, extra, py{})));
                    }, true) },
                { "__str__", w.method("Path", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"posixpath">().attr("join"))(self.attr("root").attr("filename"), self.attr("at"));
                    }) },
                { "__repr__", w.method("Path", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_Path__repr").method("format", {  }, { { "self", self } });
                    }) },
                { "joinpath", w.method("Path", "joinpath", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py next;
                        next = call_ex((Mod<"posixpath">().attr("join")), { self.attr("at") }, other, py{});
                        return self.method("_next", { self.attr("root").method("resolve_dir", { next }) });
                    }, true) },
                { "__truediv__", w.method("Path", "joinpath", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py next;
                        next = call_ex((Mod<"posixpath">().attr("join")), { self.attr("at") }, other, py{});
                        return self.method("_next", { self.attr("root").method("resolve_dir", { next }) });
                    }, true) },
                { "parent", B<"property">()(w.method("Path", "parent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parent_at;
                        if (!self.attr("at").truthy())
                        {
                            return self.attr("filename").attr("parent");
                        }
                        parent_at = (Mod<"posixpath">().attr("dirname"))(self.attr("at").method("rstrip", { S<"/">() }));
                        if (parent_at.truthy())
                        {
                            parent_at = inplace(NEOS_PY_ADD, parent_at, S<"/">());
                        }
                        return self.method("_next", { parent_at });
                    })) }, }));
        }

        bool zipfile__path_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_zipfile__path();
                }
                if (auto const doc = zipfile_path_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("contextlib", py{ fn_import("contextlib") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("itertools", py{ fn_import("itertools") }.release());
                aAttributes.emplace_back("pathlib", py{ fn_import("pathlib") }.release());
                aAttributes.emplace_back("posixpath", py{ fn_import("posixpath") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("stat", py{ fn_import("stat") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("zipfile", py{ fn_import("zipfile") }.release());
                aAttributes.emplace_back("Translator", import_attribute("zipfile._path.glob", "Translator").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("zipfile._path", zipfile__path_module);
    }
}
