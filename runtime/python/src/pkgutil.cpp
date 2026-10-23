/*
  pkgutil.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/pkgutil.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module pkgutil (CPython 3.13's Lib/pkgutil.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "pkgutil_docs.hpp"

namespace neos::python_runtime::detail::pkgutil_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "pkgutil.name"))
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
                py_raise("SystemError", std::string{ "pkgutil: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"pkgutil.name">()))
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
            static module_writer const sWriter{ "pkgutil", pkgutil_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- pkgutil (Lib/pkgutil.py)

        void make_pkgutil()
        {
            static module_writer const& w = writer("pkgutil");
            define("pkgutil", "__all__", py_list({ S<"get_importer">(), S<"iter_importers">(), S<"get_loader">(), S<"find_loader">(), S<"walk_packages">(), S<"iter_modules">(), S<"get_data">(), S<"read_code">(), S<"extend_path">(), S<"ModuleInfo">() }));
            define("pkgutil", "ModuleInfo", (Mod<"collections">().attr("namedtuple"))(S<"ModuleInfo">(), S<"module_finder name ispkg">()));
            (G<"pkgutil.ModuleInfo">()).set_attr("__doc__", S<"A namedtuple with minimal info about a module.">());
            define("pkgutil", "read_code", w.function("read_code", "read_code", { py_positional("stream") }, [](arguments a) -> py
                {
                    py stream = a[0];
                    py magic;
                    py marshal;
                    marshal = module_of("marshal");
                    magic = stream.method("read", { I<4>() });
                    if (magic != (Mod<"importlib.util">().attr("MAGIC_NUMBER")))
                    {
                        return none();
                    }
                    stream.method("read", { I<12>() });
                    return marshal.method("load", { stream });
                }));

            define("pkgutil", "walk_packages", w.function("walk_packages", "walk_packages", { py_positional("path", none()), py_positional("prefix", S<"">()), py_positional("onerror", none()) }, [](arguments a) -> py
                {
                    return make_native_generator("walk_packages", "walk_packages", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py path = a[0];
                        py prefix = a[1];
                        py onerror = a[2];
                        py info;
                        py seen;
                        seen = w.function("seen", "walk_packages.<locals>.seen", { py_positional("p"), py_positional("m", py_dict()) }, [](arguments a) -> py
    {
        py p = a[0];
        py m = a[1];
        if (m.contains(p))
        {
            return boolean(true);
        }
        m.set_item(p, boolean(true));
        return none();
    });
                        for (py const& item : iterate{ (G<"pkgutil.iter_modules">())(path, prefix) })
                        {
                            info = item;
                            py_yield(info);
                            if (info.attr("ispkg").truthy())
                            {
                                {
                                    bool otherwise2 = false;
                                    try
                                    {
                                        (B<"__import__">())(info.attr("name"));
                                        otherwise2 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler2;
                                        if (matches(handler2.exception, B<"ImportError">()))
                                        {
                                            if (!onerror.is_none())
                                            {
                                                onerror(info.attr("name"));
                                            }
                                        }
                                        else if (matches(handler2.exception, B<"Exception">()))
                                        {
                                            if (!onerror.is_none())
                                            {
                                                onerror(info.attr("name"));
                                            }
                                            else
                                            {
                                                py_reraise(handler2.exception);
                                            }
                                        }
                                        else
                                            py_reraise(handler2.exception);
                                    }
                                    if (otherwise2)
                                    {
                                        path = [&]() -> py { py const t = (B<"getattr">())((Mod<"sys">().attr("modules"))[info.attr("name")], S<"__path__">(), none()); return t.truthy() ? t : py_list(); }();
                                        // don't traverse path items we've seen before
                                        path = [&]() -> py { py const comprehension3 = py_list(); py p; for (py const& item : iterate{ path }) { p = item; if (seen(p).truthy()) continue; comprehension3.method("append", { p }); } return comprehension3; }();
                                        yield_from((G<"pkgutil.walk_packages">())(path, info.attr("name") + (S<".">()), onerror));
                                    }
                                }
                            }
                        }
                        return none();
                    });
                }));

            define("pkgutil", "iter_modules", w.function("iter_modules", "iter_modules", { py_positional("path", none()), py_positional("prefix", S<"">()) }, [](arguments a) -> py
                {
                    return make_native_generator("iter_modules", "iter_modules", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py path = a[0];
                        py prefix = a[1];
                        py i;
                        py importers;
                        py ispkg;
                        py name;
                        py yielded;
                        if (path.is_none())
                        {
                            importers = (G<"pkgutil.iter_importers">())();
                        }
                        else if (isinstance(path, B<"str">()))
                        {
                            py_raise((B<"ValueError">())(S<"path must be None or list of paths to look for modules in">()));
                        }
                        else
                        {
                            importers = (B<"map">())(G<"pkgutil.get_importer">(), path);
                        }
                        yielded = py_dict();
                        for (py const& item : iterate{ importers })
                        {
                            i = item;
                            for (py const& item : iterate{ (G<"pkgutil.iter_importer_modules">())(i, prefix) })
                            {
                                auto const items6 = unpack(item, 2);
                                name = items6[0];
                                ispkg = items6[1];
                                if (!yielded.contains(name))
                                {
                                    yielded.set_item(name, I<1>());
                                    py_yield((G<"pkgutil.ModuleInfo">())(i, name, ispkg));
                                }
                            }
                        }
                        return none();
                    });
                }));

            define("pkgutil", "iter_importer_modules", (Mod<"functools">().attr("singledispatch"))(w.function("iter_importer_modules", "iter_importer_modules", { py_positional("importer"), py_positional("prefix", S<"">()) }, [](arguments a) -> py
                {
                    py importer = a[0];
                    py prefix = a[1];
                    if (!has_attr(importer, "iter_modules"))
                    {
                        return py_list();
                    }
                    return importer.method("iter_modules", { prefix });
                })));

            // Implement a file walker for the normal importlib path hook
            define("pkgutil", "_iter_file_finder_modules", w.function("_iter_file_finder_modules", "_iter_file_finder_modules", { py_positional("importer"), py_positional("prefix", S<"">()) }, [](arguments a) -> py
                {
                    return make_native_generator("_iter_file_finder_modules", "_iter_file_finder_modules", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py importer = a[0];
                        py prefix = a[1];
                        py dircontents;
                        py filenames;
                        py fn;
                        py inspect;
                        py ispkg;
                        py modname;
                        py path;
                        py subname;
                        py yielded;
                        if ((importer.attr("path").is_none() || (!(Mod<"os.path">().attr("isdir"))(importer.attr("path")).truthy())))
                        {
                            return none();
                        }
                        yielded = py_dict();
                        inspect = module_of("inspect");
                        try
                        {
                            filenames = (Mod<"os">().attr("listdir"))(importer.attr("path"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            if (matches(handler7.exception, B<"OSError">()))
                            {
                                // ignore unreadable directories like import does
                                filenames = py_list();
                            }
                            else
                                py_reraise(handler7.exception);
                        }
                        filenames.method("sort");
                        for (py const& item : iterate{ filenames })
                        {
                            fn = item;
                            modname = inspect.method("getmodulename", { fn });
                            if (((modname == (S<"__init__">())) || yielded.contains(modname)))
                            {
                                continue;
                            }
                            path = (Mod<"os.path">().attr("join"))(importer.attr("path"), fn);
                            ispkg = boolean(false);
                            if (((!modname.truthy()) && (Mod<"os.path">().attr("isdir"))(path).truthy() && (!fn.contains((S<".">())))))
                            {
                                modname = fn;
                                try
                                {
                                    dircontents = (Mod<"os">().attr("listdir"))(path);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler9;
                                    if (matches(handler9.exception, B<"OSError">()))
                                    {
                                        // ignore unreadable directories like import does
                                        dircontents = py_list();
                                    }
                                    else
                                        py_reraise(handler9.exception);
                                }
                                bool broke10 = false;
                                for (py const& item : iterate{ dircontents })
                                {
                                    fn = item;
                                    subname = inspect.method("getmodulename", { fn });
                                    if (subname == (S<"__init__">()))
                                    {
                                        ispkg = boolean(true);
                                        broke10 = true;
                                        break;
                                    }
                                }
                                if (!broke10)
                                {
                                    continue;
                                }
                            }
                            if ((modname.truthy() && (!modname.contains((S<".">())))))
                            {
                                yielded.set_item(modname, I<1>());
                                py_yield(py_tuple({ prefix + modname, ispkg }));
                            }
                        }
                        return none();
                    });
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"pkgutil.iter_importer_modules">()).method("register", { Mod<"importlib.machinery">().attr("FileFinder"), G<"pkgutil._iter_file_finder_modules">() });
                    return none();
                }))();
            define("pkgutil", "get_importer", w.function("get_importer", "get_importer", { py_positional("path_item") }, [](arguments a) -> py
                {
                    py path_item = a[0];
                    py importer;
                    py path_hook;
                    path_item = (Mod<"os">().attr("fsdecode"))(path_item);
                    try
                    {
                        importer = (Mod<"sys">().attr("path_importer_cache"))[path_item];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler11;
                        if (matches(handler11.exception, B<"KeyError">()))
                        {
                            bool broke12 = false;
                            for (py const& item : iterate{ Mod<"sys">().attr("path_hooks") })
                            {
                                path_hook = item;
                                try
                                {
                                    importer = path_hook(path_item);
                                    (Mod<"sys">().attr("path_importer_cache")).method("setdefault", { path_item, importer });
                                    broke12 = true;
                                    break;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler13;
                                    if (matches(handler13.exception, B<"ImportError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler13.exception);
                                }
                            }
                            if (!broke12)
                            {
                                importer = none();
                            }
                        }
                        else
                            py_reraise(handler11.exception);
                    }
                    return importer;
                }));

            define("pkgutil", "iter_importers", w.function("iter_importers", "iter_importers", { py_positional("fullname", S<"">()) }, [](arguments a) -> py
                {
                    return make_native_generator("iter_importers", "iter_importers", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py fullname = a[0];
                        py item_;
                        py msg;
                        py path;
                        py pkg;
                        py pkg_name;
                        if (fullname.method("startswith", { S<".">() }).truthy())
                        {
                            msg = (S<"Relative module name {!r} not supported">()).method("format", { fullname });
                            py_raise((B<"ImportError">())(msg));
                        }
                        if (fullname.contains((S<".">())))
                        {
                            // Get the containing package's __path__
                            pkg_name = fullname.method("rpartition", { S<".">() })[I<0>()];
                            pkg = (Mod<"importlib">().attr("import_module"))(pkg_name);
                            path = (B<"getattr">())(pkg, S<"__path__">(), none());
                            if (path.is_none())
                            {
                                return none();
                            }
                        }
                        else
                        {
                            yield_from(Mod<"sys">().attr("meta_path"));
                            path = Mod<"sys">().attr("path");
                        }
                        for (py const& item : iterate{ path })
                        {
                            item_ = item;
                            py_yield((G<"pkgutil.get_importer">())(item_));
                        }
                        return none();
                    });
                }));

            define("pkgutil", "get_loader", w.function("get_loader", "get_loader", { py_positional("module_or_name") }, [](arguments a) -> py
                {
                    py module_or_name = a[0];
                    py fullname;
                    py loader;
                    py module;
                    (Mod<"warnings">().attr("_deprecated")).call({ S<"pkgutil.get_loader">(), S((Mod<"warnings">().attr("_DEPRECATED_MSG")).str() + "; use importlib.util.find_spec() instead") }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                    if ((Mod<"sys">().attr("modules")).contains(module_or_name))
                    {
                        module_or_name = (Mod<"sys">().attr("modules"))[module_or_name];
                        if (module_or_name.is_none())
                        {
                            return none();
                        }
                    }
                    if (isinstance(module_or_name, Mod<"types">().attr("ModuleType")))
                    {
                        module = module_or_name;
                        loader = (B<"getattr">())(module, S<"__loader__">(), none());
                        if (!loader.is_none())
                        {
                            return loader;
                        }
                        if ((B<"getattr">())(module, S<"__spec__">(), none()).is_none())
                        {
                            return none();
                        }
                        fullname = module.attr("__name__");
                    }
                    else
                    {
                        fullname = module_or_name;
                    }
                    return (G<"pkgutil.find_loader">())(fullname);
                }));

            define("pkgutil", "find_loader", w.function("find_loader", "find_loader", { py_positional("fullname") }, [](arguments a) -> py
                {
                    py fullname = a[0];
                    py ex;
                    py msg;
                    py spec;
                    (Mod<"warnings">().attr("_deprecated")).call({ S<"pkgutil.find_loader">(), S((Mod<"warnings">().attr("_DEPRECATED_MSG")).str() + "; use importlib.util.find_spec() instead") }, { { "remove", py_tuple({ I<3>(), I<14>() }) } });
                    if (fullname.method("startswith", { S<".">() }).truthy())
                    {
                        msg = (S<"Relative module name {!r} not supported">()).method("format", { fullname });
                        py_raise((B<"ImportError">())(msg));
                    }
                    try
                    {
                        spec = (Mod<"importlib.util">().attr("find_spec"))(fullname);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler15;
                        if (matches(handler15.exception, py_tuple({ B<"ImportError">(), B<"AttributeError">(), B<"TypeError">(), B<"ValueError">() })))
                        {
                            ex = handler15.exception;
                            // This hack fixes an impedance mismatch between pkgutil and
                            // importlib, where the latter raises other errors for cases where
                            // pkgutil previously raised ImportError
                            msg = S<"Error while finding loader for {!r} ({}: {})">();
                            fn_raise_from((B<"ImportError">())(msg.method("format", { fullname, (B<"type">())(ex), ex })).get(), ex.get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler15.exception);
                    }
                    return (!spec.is_none() ? spec.attr("loader") : none());
                }));

            define("pkgutil", "extend_path", w.function("extend_path", "extend_path", { py_positional("path"), py_positional("name") }, [](arguments a) -> py
                {
                    py path = a[0];
                    py name = a[1];
                    py _;
                    py dir;
                    py f;
                    py final_name;
                    py finder;
                    py line;
                    py msg;
                    py parent_package;
                    py pkgfile;
                    py portion;
                    py portions;
                    py search_path;
                    py sname_pkg;
                    py spec;
                    if (!isinstance(path, B<"list">()))
                    {
                        // This could happen e.g. when this is called from inside a
                        // frozen package.  Return the path unchanged in that case.
                        return path;
                    }
                    sname_pkg = name + (S<".pkg">());
                    path = path[B<"slice">()(none(), none(), none())];
                    auto const items16 = unpack(name.method("rpartition", { S<".">() }), 3);
                    parent_package = items16[0];
                    _ = items16[1];
                    final_name = items16[2];
                    if (parent_package.truthy())
                    {
                        try
                        {
                            search_path = (Mod<"sys">().attr("modules"))[parent_package].attr("__path__");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler17;
                            if (matches(handler17.exception, py_tuple({ B<"KeyError">(), B<"AttributeError">() })))
                            {
                                // We can't do anything: find_loader() returns None when
                                // passed a dotted name.
                                return path;
                            }
                            else
                                py_reraise(handler17.exception);
                        }
                    }
                    else
                    {
                        search_path = Mod<"sys">().attr("path");
                    }
                    for (py const& item : iterate{ search_path })
                    {
                        dir = item;
                        if (!isinstance(dir, B<"str">()))
                        {
                            continue;
                        }
                        finder = (G<"pkgutil.get_importer">())(dir);
                        if (!finder.is_none())
                        {
                            portions = py_list();
                            if (has_attr(finder, "find_spec"))
                            {
                                spec = finder.method("find_spec", { final_name });
                                if (!spec.is_none())
                                {
                                    portions = [&]() -> py { py const t = spec.attr("submodule_search_locations"); return t.truthy() ? t : py_list(); }();
                                }
                            }
                            // Is this finder PEP 420 compliant?
                            else if (has_attr(finder, "find_loader"))
                            {
                                auto const items19 = unpack(finder.method("find_loader", { final_name }), 2);
                                _ = items19[0];
                                portions = items19[1];
                            }
                            for (py const& item : iterate{ portions })
                            {
                                portion = item;
                                // XXX This may still add duplicate entries to path on
                                // case-insensitive filesystems
                                if (!path.contains(portion))
                                {
                                    path.method("append", { portion });
                                }
                            }
                        }
                        // XXX Is this the right thing for subpackages like zope.app?
                        // It looks for a file named "zope.app.pkg"
                        pkgfile = (Mod<"os.path">().attr("join"))(dir, sname_pkg);
                        if ((Mod<"os.path">().attr("isfile"))(pkgfile).truthy())
                        {
                            {
                                bool otherwise21 = false;
                                try
                                {
                                    f = (B<"open">())(pkgfile);
                                    otherwise21 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler21;
                                    if (matches(handler21.exception, B<"OSError">()))
                                    {
                                        msg = handler21.exception;
                                        (Mod<"sys">().attr("stderr")).method("write", { (S<"Can't open %s: %s\n">()) % py_tuple({ pkgfile, msg }) });
                                    }
                                    else
                                        py_reraise(handler21.exception);
                                }
                                if (otherwise21)
                                {
                                    {
                                        py const manager22 = f;
                                        py const exit22 = pyclass::special_method(manager22, "__exit__");
                                        py const value22 = pyclass::special_method(manager22, "__enter__")(manager22);
                                        [[maybe_unused]] int flow22 = 0;
                                        [[maybe_unused]] py result22;
                                        bool exited22 = false;
                                        try
                                        {
                                            for (py const& item : iterate{ f })
                                            {
                                                line = item;
                                                line = line.method("rstrip", { S<"\n">() });
                                                if (((!line.truthy()) || line.method("startswith", { S<"#">() }).truthy()))
                                                {
                                                    continue;
                                                }
                                                path.method("append", { line });
                                            }
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler22;
                                            exited22 = true;
                                            if (!exit22(manager22, type_of(handler22.exception), handler22.exception, handler22.exception.attr("__traceback__")).truthy())
                                                py_reraise(handler22.exception);
                                        }
                                        finally22:
                                        if (!exited22)
                                            exit22(manager22, none(), none(), none());
                                        if (flow22 == 1)
                                            return result22;
                                        if (flow22 == 2)
                                            break;
                                        if (flow22 == 3)
                                            continue;
                                    }
                                }
                            }
                        }
                    }
                    return path;
                }));

            define("pkgutil", "get_data", w.function("get_data", "get_data", { py_positional("package"), py_positional("resource") }, [](arguments a) -> py
                {
                    py package = a[0];
                    py resource = a[1];
                    py loader;
                    py mod;
                    py parts;
                    py resource_name;
                    py spec;
                    spec = (Mod<"importlib.util">().attr("find_spec"))(package);
                    if (spec.is_none())
                    {
                        return none();
                    }
                    loader = spec.attr("loader");
                    if ((loader.is_none() || (!has_attr(loader, "get_data"))))
                    {
                        return none();
                    }
                    // XXX needs test
                    mod = [&]() -> py { py const t = (Mod<"sys">().attr("modules")).method("get", { package }); return t.truthy() ? t : (Mod<"importlib">().attr("_bootstrap")).method("_load", { spec }); }();
                    if ((mod.is_none() || (!has_attr(mod, "__file__"))))
                    {
                        return none();
                    }
                    // Modify the resource name to be compatible with the loader.get_data
                    // signature - an os.path format "filename" starting with the dirname of
                    // the package's __file__
                    parts = resource.method("split", { S<"/">() });
                    parts.method("insert", { I<0>(), (Mod<"os.path">().attr("dirname"))(mod.attr("__file__")) });
                    resource_name = call_ex((Mod<"os.path">().attr("join")), {  }, parts, py{});
                    return loader.method("get_data", { resource_name });
                }));

            define("pkgutil", "_NAME_PATTERN", none());
            define("pkgutil", "resolve_name", w.function("resolve_name", "resolve_name", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py dotted_words;
                    py gd;
                    py m;
                    py mod;
                    py modname;
                    py p;
                    py parts;
                    py re;
                    py result;
                    py s;
                    if ((G<"pkgutil._NAME_PATTERN">()).is_none())
                    {
                        // Lazy import to speedup Python startup time
                        re = module_of("re");
                        dotted_words = S<"(?!\\d)(\\w+)(\\.(?!\\d)(\\w+))*">();
                        set_global("pkgutil", "_NAME_PATTERN", re.method("compile", { S(std::string{ "^(?P<pkg>" } + dotted_words.str() + ")(?P<cln>:(?P<obj>" + dotted_words.str() + ")?)?$"), re.attr("UNICODE") }));
                    }
                    m = (G<"pkgutil._NAME_PATTERN">()).method("match", { name });
                    if (!m.truthy())
                    {
                        py_raise((B<"ValueError">())(S(std::string{ "invalid format: " } + name.repr())));
                    }
                    gd = m.method("groupdict");
                    if (gd.method("get", { S<"cln">() }).truthy())
                    {
                        // there is a colon - a one-step import is all that's needed
                        mod = (Mod<"importlib">().attr("import_module"))(gd[S<"pkg">()]);
                        parts = gd.method("get", { S<"obj">() });
                        parts = (parts.truthy() ? parts.method("split", { S<".">() }) : py_list());
                    }
                    else
                    {
                        // no colon - have to iterate to find the package boundary
                        parts = name.method("split", { S<".">() });
                        modname = parts.method("pop", { I<0>() });
                        // first part *must* be a module/package.
                        mod = (Mod<"importlib">().attr("import_module"))(modname);
                        while (parts.truthy())
                        {
                            p = parts[I<0>()];
                            s = S(modname.str() + "." + p.str());
                            try
                            {
                                mod = (Mod<"importlib">().attr("import_module"))(s);
                                parts.method("pop", { I<0>() });
                                modname = s;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler25;
                                if (matches(handler25.exception, B<"ImportError">()))
                                {
                                    break;
                                }
                                else
                                    py_reraise(handler25.exception);
                            }
                        }
                    }
                    // if we reach this point, mod is the module, already imported, and
                    // parts is the list of parts in the object hierarchy to be traversed, or
                    // an empty list if just the module is wanted.
                    result = mod;
                    for (py const& item : iterate{ parts })
                    {
                        p = item;
                        result = (B<"getattr">())(result, p);
                    }
                    return result;
                }));
        }

        bool pkgutil_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_pkgutil();
                }
                if (auto const doc = pkgutil_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("namedtuple", import_attribute("collections", "namedtuple").release());
                aAttributes.emplace_back("simplegeneric", import_attribute("functools", "singledispatch").release());
                aAttributes.emplace_back("importlib", py{ fn_import("importlib") }.release());
                aAttributes.emplace_back("importlib", py{ fn_import("importlib") }.release());
                aAttributes.emplace_back("importlib", py{ fn_import("importlib") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("ModuleType", import_attribute("types", "ModuleType").release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("pkgutil", pkgutil_module);
    }
}
