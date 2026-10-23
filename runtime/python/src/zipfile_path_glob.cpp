/*
  zipfile_path_glob.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/zipfile/_path/glob.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module zipfile._path.glob (CPython 3.13's Lib/zipfile/_path/glob.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "zipfile_path_glob_docs.hpp"

namespace neos::python_runtime::detail::zipfile__path_glob_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "zipfile._path.glob.name"))
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
                py_raise("SystemError", std::string{ "zipfile._path.glob: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"zipfile._path.glob.name">()))
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
            static module_writer const sWriter{ "zipfile._path.glob", zipfile_path_glob_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- zipfile._path.glob (Lib/zipfile/_path/glob.py)

        void make_zipfile__path_glob()
        {
            static module_writer const& w = writer("zipfile._path.glob");
            define("zipfile._path.glob", "_default_seps", (Mod<"os">().attr("sep")) + ((B<"str">())(Mod<"os">().attr("altsep")) * (B<"bool">())(Mod<"os">().attr("altsep"))));
            define("zipfile._path.glob", "Translator", w.make_class("Translator", {  }, {
                { "__init__", w.method("Translator", "__init__", { py_positional("seps", G<"zipfile._path.glob._default_seps">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py seps = a[1];
                        if (!([&]() -> py { py const t = seps; return !t.truthy() ? t : boolean((B<"set">())(seps) <= (B<"set">())(G<"zipfile._path.glob._default_seps">())); }()).truthy())
                            py_raise(B<"AssertionError">()(S<"Invalid separators">()));
                        self.set_attr("seps", seps);
                        return none();
                    }) },
                { "translate", w.method("Translator", "translate", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        return self.method("extend", { self.method("match_dirs", { self.method("translate_core", { pattern }) }) });
                    }) },
                { "extend", w.method("Translator", "extend", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        return S(std::string{ "(?s:" } + pattern.str() + ")\\Z");
                    }) },
                { "match_dirs", w.method("Translator", "match_dirs", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        return S(pattern.str() + "[/]?");
                    }) },
                { "translate_core", w.method("Translator", "translate_core", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        self.method("restrict_rglob", { pattern });
                        return (S<"">()).method("join", { (B<"map">())(self.attr("replace"), (G<"zipfile._path.glob.separate">())(self.method("star_not_empty", { pattern }))) });
                    }) },
                { "replace", w.method("Translator", "replace", { py_positional("match") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py match = a[1];
                        return [&]() -> py { py const t = match.method("group", { S<"set">() }); return t.truthy() ? t : (Mod<"re">().attr("escape"))(match.method("group", { I<0>() })).method("replace", { S<"\\*\\*">(), S<".*">() }).method("replace", { S<"\\*">(), S(std::string{ "[^" } + (Mod<"re">().attr("escape"))(self.attr("seps")).str() + "]*") }).method("replace", { S<"\\?">(), S<"[^/]">() }); }();
                    }) },
                { "restrict_rglob", w.method("Translator", "restrict_rglob", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        py segments;
                        py seps_pattern;
                        seps_pattern = S(std::string{ "[" } + (Mod<"re">().attr("escape"))(self.attr("seps")).str() + "]+");
                        segments = (Mod<"re">().attr("split"))(seps_pattern, pattern);
                        if ((B<"any">())([&]() -> py { py const comprehension1 = py_list(); py segment; for (py const& item : iterate{ segments }) { segment = item; comprehension1.method("append", { [&]() -> py { py const t = boolean(segment.contains((S<"**">()))); return !t.truthy() ? t : boolean(segment != (S<"**">())); }() }); } return comprehension1; }()).truthy())
                        {
                            py_raise((B<"ValueError">())(S<"** must appear alone in a path segment">()));
                        }
                        return none();
                    }) },
                { "star_not_empty", w.method("Translator", "star_not_empty", { py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pattern = a[1];
                        py handle_segment;
                        py not_seps_pattern;
                        handle_segment = w.function("handle_segment", "Translator.star_not_empty.<locals>.handle_segment", { py_positional("match") }, [](arguments a) -> py
    {
        py match = a[0];
        py segment;
        segment = match.method("group", { I<0>() });
        return (segment == (S<"*">()) ? S<"?*">() : segment);
    });
                        not_seps_pattern = S(std::string{ "[^" } + (Mod<"re">().attr("escape"))(self.attr("seps")).str() + "]+");
                        return (Mod<"re">().attr("sub"))(not_seps_pattern, handle_segment, pattern);
                    }) },
                { "__annotations__", dict_of({ { S<"seps">(), B<"str">() } }) }, }));

            define("zipfile._path.glob", "separate", w.function("separate", "separate", { py_positional("pattern") }, [](arguments a) -> py
                {
                    py pattern = a[0];
                    return (Mod<"re">().attr("finditer"))(S<"([^\\[]+)|(?P<set>[\\[].*?[\\]])|([\\[][^\\]]*$)">(), pattern);
                }));
        }

        bool zipfile__path_glob_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_zipfile__path_glob();
                }
                if (auto const doc = zipfile_path_glob_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("zipfile._path.glob", zipfile__path_glob_module);
    }
}
