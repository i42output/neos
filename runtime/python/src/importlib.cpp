/*
  socketserver.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/importlib/__init__.py, Lib/importlib/util.py and
  Lib/importlib/_bootstrap.py: import_module, _gcd_import, _sanity_check, _resolve_name, reload's checks, find_spec and
  ModuleSpec), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python
  Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the
  neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The package importlib (CPython 3.13's Lib/importlib): import_module (an absolute name's module, as __import__ imports it, or a 
// relative name's in its package), invalidate_caches and reload (the runtime has no finders' caches, and a module is not run again:
// reload returns it); importlib.util's find_spec (a module's spec: its own if it has been imported, else a ModuleSpec of the module 
// that the runtime implements (built-in) or of its source file on sys.path (or its package's __path__)) and resolve_name; and 
// importlib.machinery's ModuleSpec and suffixes.

#include <filesystem>
#include <string>
#include <vector>
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"

namespace neos::python_runtime::detail
{
    using module_initializer = bool (*)(std::vector<std::pair<std::string, object*>>& aAttributes);
    std::map<std::string, module_initializer, std::less<>>& modules();
    py import_find_and_load(std::string const& aName);

    namespace
    {
        using pyclass::S;
        using pyclass::none;
        using pyclass::boolean;

        py native(char const* aName, char const* aModule, char const* aDoc, native_body aBody)
        {
            return make_native_function(aName, aName, aDoc, aModule, std::move(aBody));
        }

        // (_bootstrap._resolve_name: a relative name's absolute name)
        std::string resolve_name(std::string const& aName, std::string const& aPackage, long long aLevel)
        {
            // (package.rsplit('.', level - 1))
            std::string base = aPackage;
            for (long long i = 1; i < aLevel; ++i)
            {
                auto const dot = base.rfind('.');
                if (dot == std::string::npos)
                    py_raise("ImportError", "attempted relative import beyond top-level package");
                base = base.substr(0u, dot);
            }
            return aName.empty() ? base : base + "." + aName;
        }

        // (_bootstrap._sanity_check)
        void sanity_check(py const& aName, py const& aPackage, long long aLevel)
        {
            if (!isinstance(aName, builtin("str")))
                py_raise("TypeError", "module name must be str, not " + type_of(aName).repr());
            if (aLevel < 0)
                py_raise("ValueError", "level must be >= 0");
            if (aLevel > 0)
            {
                if (!isinstance(aPackage, builtin("str")))
                    py_raise("TypeError", "__package__ not set to a string");
                if (!aPackage.truthy())
                    py_raise("ImportError", "attempted relative import with no known parent package");
            }
            if (!aName.truthy() && aLevel == 0)
                py_raise("ValueError", "Empty module name");
        }

        // (a name with leading dots: its level, and the name without them; the package is required)
        std::pair<std::string, long long> relative_name(py const& aName, py const& aPackage, char const* aWhat)
        {
            if (!isinstance(aName, builtin("str")))
                return { {}, 0 };
            auto const text = aName.str();
            long long level = 0;
            while (static_cast<std::size_t>(level) < text.size() && text[static_cast<std::size_t>(level)] == '.')
                ++level;
            if (level > 0 && !aPackage.truthy())
                py_raise("TypeError", std::string{ aWhat } + aName.repr());
            return { text.substr(static_cast<std::size_t>(level)), level };
        }

        struct importlib_state
        {
            py moduleSpec;      ///< (importlib.machinery.ModuleSpec)
        };
        importlib_state& state()
        {
            static importlib_state* sState = new importlib_state{};
            return *sState;
        }

        // (ModuleSpec (_bootstrap's): name, loader, origin, loader_state, submodule_search_locations, cached, parent and has_location)
        py make_module_spec()
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S("importlib.machinery"));
            ns.set_item(S("__qualname__"), S("ModuleSpec"));
            ns.set_item(S("__doc__"), S("The specification for a module, used for loading.\n\nA module's spec is the source for information about the module.  For\ndata associated with the module, including source, use the spec's\nloader.\n\n`name` is the absolute name of the module.  `loader` is the loader\nto use when loading the module.  `parent` is the name of the\npackage the module is in.  The parent is derived from the name.\n\n`is_package` determines if the module is considered a package or\nnot.  On modules this is reflected by the `__path__` attribute.\n\n`origin` is the specific location used by the loader from which to\nload the module, if that information is available.  When filename is\nset, origin will match.\n\n`has_location` indicates that a spec's \"origin\" reflects a location.\nWhen this is True, `__file__` attribute of the module is set.\n\n`cached` is the location of the cached bytecode file, if any.  It\ncorresponds to the `__cached__` attribute.\n\n`submodule_search_locations` is the sequence of path entries to\nsearch when importing submodules.  If set, is_package should be\nTrue--and False otherwise.\n\nPackages are simply modules that (may) have submodules.  If a spec\nhas an empty list for submodule_search_locations, the import\nsystem will consider modules loaded from the spec as packages.\n\nOnly finders (see importlib.abc.MetaPathFinder and\nimportlib.abc.PathEntryFinder) should modify ModuleSpec instances."));
            ns.set_item(S("__init__"), native("__init__", "importlib.machinery", "", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("ModuleSpec.__init__", { py_positional("self"), py_positional("name"), py_positional("loader"), py_keyword("origin", py::none()),
                    py_keyword("loader_state", py::none()), py_keyword("is_package", py::none()) }, aArgs, aKwargs);
                auto const& self = a[0];
                self.set_attr("name", a[1]);
                self.set_attr("loader", a[2]);
                self.set_attr("origin", a[3]);
                self.set_attr("loader_state", a[4]);
                self.set_attr("submodule_search_locations", a[5].truthy() ? py_list() : py::none());
                self.set_attr("_uninitialized_submodules", py_list());
                self.set_attr("_set_fileattr", boolean(false));
                self.set_attr("cached", py::none());
                return py::none();
            }));
            ns.set_item(S("__repr__"), native("__repr__", "importlib.machinery", "", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("ModuleSpec.__repr__", { py_positional("self") }, aArgs, aKwargs);
                auto const& self = a[0];
                std::string text = "ModuleSpec(name=" + self.attr("name").repr() + ", loader=" + self.attr("loader").repr();
                if (!self.attr("origin").is_none())
                    text += ", origin=" + self.attr("origin").repr();
                if (!self.attr("submodule_search_locations").is_none())
                    text += ", submodule_search_locations=" + self.attr("submodule_search_locations").repr();
                return py{ text + ")" };
            }));
            ns.set_item(S("__eq__"), native("__eq__", "importlib.machinery", "", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("ModuleSpec.__eq__", { py_positional("self"), py_positional("other") }, aArgs, aKwargs);
                auto const& self = a[0];
                auto const& other = a[1];
                for (auto name : { "name", "loader", "origin", "submodule_search_locations", "cached", "has_location" })
                    if (!builtin("hasattr")(other, py{ name }).truthy())
                        return builtin("NotImplemented");
                for (auto name : { "name", "loader", "origin", "submodule_search_locations", "cached", "has_location" })
                    if (self.attr(name) != other.attr(name))
                        return boolean(false);
                return boolean(true);
            }));
            ns.set_item(S("parent"), builtin("property")(native("parent", "importlib.machinery", "The name of the module's parent.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("ModuleSpec.parent", { py_positional("self") }, aArgs, aKwargs);
                auto const name = a[0].attr("name").str();
                if (a[0].attr("submodule_search_locations").is_none())
                {
                    auto const dot = name.rfind('.');
                    return py{ dot == std::string::npos ? std::string{} : name.substr(0u, dot) };
                }
                return py{ name };
            })));
            ns.set_item(S("has_location"), builtin("property")(native("has_location", "importlib.machinery", "", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("ModuleSpec.has_location", { py_positional("self") }, aArgs, aKwargs);
                return a[0].attr("_set_fileattr");
            })));
            return builtin("type")(S("ModuleSpec"), py_tuple({ builtin("object") }), ns);
        }

        py module_spec()
        {
            auto& s = state();
            if (s.moduleSpec.empty())
                s.moduleSpec = make_module_spec();
            return s.moduleSpec;
        }

        // (a module's spec: its __spec__ if it has one, else one of what it is (built-in, or its file))
        py spec_of_module(py const& aModule, std::string const& aName)
        {
            if (py const spec = builtin("getattr")(aModule, S("__spec__"), py::none()); !spec.is_none())
                return spec;
            py const file = builtin("getattr")(aModule, S("__file__"), py::none());
            bool const hasPath = builtin("hasattr")(aModule, S("__path__")).truthy();
            // (a package: it has __path__, or its source is a package's (__init__.py))
            std::filesystem::path const filePath{ isinstance(file, builtin("str")) ? file.str() : std::string{} };
            bool const initFile = filePath.filename() == "__init__.py";
            bool const package = hasPath || initFile;
            py const spec = module_spec().call({ py{ aName }, py::none() }, { { "origin", file.is_none() ? S("built-in") : file }, { "is_package", boolean(package) } });
            if (!file.is_none())
                spec.set_attr("_set_fileattr", boolean(true));
            if (hasPath)
                spec.set_attr("submodule_search_locations", builtin("list")(aModule.attr("__path__")));
            else if (initFile)
                spec.set_attr("submodule_search_locations", py_list({ py{ filePath.parent_path().string() } }));
            return spec;
        }

        // (a module that is not imported: its spec (the runtime's module, or its source file in the directories); None if there is none)
        py find_unimported(std::string const& aName, std::vector<std::string> const& aDirectories)
        {
            if (modules().contains(aName))
                return module_spec().call({ py{ aName }, py::none() }, { { "origin", S("built-in") } });
            auto const dot = aName.rfind('.');
            std::string const last = dot == std::string::npos ? aName : aName.substr(dot + 1u);
            for (auto const& d : aDirectories)
            {
                std::error_code e;
                std::filesystem::path const directory{ d.empty() ? std::string{ "." } : d };
                auto const package = directory / last / "__init__.py";
                if (std::filesystem::is_regular_file(package, e))
                {
                    py const spec = module_spec().call({ py{ aName }, py::none() }, { { "origin", py{ package.string() } }, { "is_package", boolean(true) } });
                    spec.set_attr("submodule_search_locations", py_list({ py{ (directory / last).string() } }));
                    spec.set_attr("_set_fileattr", boolean(true));
                    return spec;
                }
                auto const file = directory / (last + ".py");
                if (std::filesystem::is_regular_file(file, e))
                {
                    py const spec = module_spec().call({ py{ aName }, py::none() }, { { "origin", py{ file.string() } } });
                    spec.set_attr("_set_fileattr", boolean(true));
                    return spec;
                }
            }
            return py::none();
        }

        bool importlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S("A pure Python implementation of import."));
                add("__all__", py_list({ S("__import__"), S("import_module"), S("invalidate_caches"), S("reload") }));
                add("__import__", builtin("__import__"));
                add("import_module", native("import_module", "importlib", "Import a module.\n\nThe 'package' argument is required when performing a relative import. It\nspecifies the package to use as the anchor point from which to resolve the\nrelative import to an absolute import.\n\n",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("import_module", { py_positional("name"), py_positional("package", py::none()) }, aArgs, aKwargs);
                    auto const [name, level] = relative_name(a[0], a[1], "the 'package' argument is required to perform a relative import for ");
                    // (_gcd_import)
                    py const bare = level > 0 ? py{ name } : a[0];
                    sanity_check(bare, a[1], level);
                    return import_find_and_load(level > 0 ? resolve_name(name, a[1].str(), level) : name);
                }));
                add("invalidate_caches", native("invalidate_caches", "importlib", "Call the invalidate_caches() method on all meta path finders stored in\nsys.meta_path (where implemented).",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("invalidate_caches", {}, aArgs, aKwargs);
                    return py::none();
                }));
                add("reload", native("reload", "importlib", "Reload the module and return it.\n\nThe module must have been successfully imported before.\n\n",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("reload", { py_positional("module") }, aArgs, aKwargs);
                    py name;
                    try
                    {
                        py const spec = a[0].attr("__spec__");
                        name = spec.is_none() ? a[0].attr("__name__") : spec.attr("name");
                    }
                    catch (raised const&)
                    {
                        py const e = py_take_exception();
                        if (!isinstance(e, builtin("AttributeError")))
                            py_reraise(e);
                        try
                        {
                            name = a[0].attr("__name__");
                        }
                        catch (raised const&)
                        {
                            py const e2 = py_take_exception();
                            if (!isinstance(e2, builtin("AttributeError")))
                                py_reraise(e2);
                            py_raise("TypeError", "reload() argument must be a module");
                        }
                    }
                    py const modules = import_attribute("sys", "modules");
                    if (!modules.contains(name) || !modules[name].is(a[0]))
                        py_raise(builtin("ImportError").call({ py{ "module " + name.str() + " not in sys.modules" } }, { { "name", name } }));
                    return a[0];
                }));
                add("util", py{ fn_import("importlib.util") });
                add("machinery", py{ fn_import("importlib.machinery") });
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        bool importlib_util_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S("Utility code for constructing importers, etc."));
                add("resolve_name", native("resolve_name", "importlib.util", "Resolve a relative module name to an absolute one.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("resolve_name", { py_positional("name"), py_positional("package") }, aArgs, aKwargs);
                    auto const text = a[0].str();
                    if (text.empty() || text[0] != '.')
                        return a[0];
                    if (!a[1].truthy())
                        py_raise("ImportError", "no package specified for " + a[0].repr() + " (required for relative module names)");
                    std::size_t level = 0u;
                    while (level < text.size() && text[level] == '.')
                        ++level;
                    return py{ resolve_name(text.substr(level), a[1].str(), static_cast<long long>(level)) };
                }));
                add("find_spec", native("find_spec", "importlib.util", "Return the spec for the specified module.\n\nFirst, sys.modules is checked to see if the module was already imported. If\nso, then sys.modules[name].__spec__ is returned. If that happens to be\nset to None, then ValueError is raised. If the module is not in\nsys.modules, then sys.meta_path is searched for a suitable spec with the\nvalue of 'path' given to the finders. None is returned if no spec could\nbe found.\n\nIf the name is for submodule (contains a dot), the parent module is\nautomatically imported.\n\nThe name and package arguments work the same as importlib.import_module().\nIn other words, relative module names (with leading dots) work.\n\n",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("find_spec", { py_positional("name"), py_positional("package", py::none()) }, aArgs, aKwargs);
                    auto text = a[0].str();
                    if (!text.empty() && text[0] == '.')
                    {
                        if (!a[1].truthy())
                            py_raise("ImportError", "no package specified for " + a[0].repr() + " (required for relative module names)");
                        std::size_t level = 0u;
                        while (level < text.size() && text[level] == '.')
                            ++level;
                        text = resolve_name(text.substr(level), a[1].str(), static_cast<long long>(level));
                    }
                    py const modules = import_attribute("sys", "modules");
                    py const key{ text };
                    if (modules.contains(key))
                    {
                        py const module = modules[key];
                        if (module.is_none())
                            return py::none();
                        // (the runtime's modules have no __spec__ of their own (but __main__'s is None, as CPython's): one of what they are)
                        py const spec = builtin("getattr")(module, S("__spec__"), py::none());
                        if (spec.is_none() && text == "__main__")
                            py_raise("ValueError", text + ".__spec__ is None");
                        return spec_of_module(module, text);
                    }
                    // (a submodule: its package's (imported) directories)
                    std::vector<std::string> directories;
                    if (auto const dot = text.rfind('.'); dot != std::string::npos)
                    {
                        auto const parentName = text.substr(0u, dot);
                        py const parent = import_find_and_load(parentName);
                        py const path = spec_of_module(parent, parentName).attr("submodule_search_locations");
                        if (path.is_none())
                        {
                            if (modules().contains(text))
                                return find_unimported(text, {});
                            py_raise(builtin("ModuleNotFoundError").call({ py{ "__path__ attribute not found on " + py{ parentName }.repr() + " while trying to find " + key.repr() } },
                                { { "name", key } }));
                        }
                        for (py const& d : path.items())
                            directories.push_back(d.str());
                    }
                    else
                        for (py const& d : import_attribute("sys", "path").items())
                            if (isinstance(d, builtin("str")))
                                directories.push_back(d.str());
                    return find_unimported(text, directories);
                }));
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        bool importlib_machinery_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S("The machinery of importlib: finders, loaders, hooks, etc."));
                add("ModuleSpec", module_spec());
                add("SOURCE_SUFFIXES", py_list({ S(".py") }));
                add("BYTECODE_SUFFIXES", py_list({ S(".pyc") }));
                add("EXTENSION_SUFFIXES", py_list());
                add("all_suffixes", native("all_suffixes", "importlib.machinery", "Returns a list of all recognized module suffixes for this process", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("all_suffixes", {}, aArgs, aKwargs);
                    return py_list({ S(".py"), S(".pyc") });
                }));
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sImportlib = register_module("importlib", importlib_module);
        inline bool const sImportlibUtil = register_module("importlib.util", importlib_util_module);
        inline bool const sImportlibMachinery = register_module("importlib.machinery", importlib_machinery_module);
    }
}
