/*
  mimetypes.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/mimetypes.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module mimetypes (CPython 3.13's Lib/mimetypes.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "mimetypes_docs.hpp"

namespace neos::python_runtime::detail::mimetypes_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "mimetypes.name"))
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
                py_raise("SystemError", std::string{ "mimetypes: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"mimetypes.name">()))
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
            static module_writer const sWriter{ "mimetypes", mimetypes_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- mimetypes (Lib/mimetypes.py)

        void make_mimetypes()
        {
            static module_writer const& w = writer("mimetypes");
            define("mimetypes", "_mimetypes_read_windows_registry", none());
            define("mimetypes", "_winreg", none());
            define("mimetypes", "__all__", py_list({ S<"knownfiles">(), S<"inited">(), S<"MimeTypes">(), S<"guess_type">(), S<"guess_file_type">(), S<"guess_all_extensions">(), S<"guess_extension">(), S<"add_type">(), S<"init">(), S<"read_mime_types">(), S<"suffix_map">(), S<"encodings_map">(), S<"types_map">(), S<"common_types">() }));
            define("mimetypes", "knownfiles", py_list({ S<"/etc/mime.types">(), S<"/etc/httpd/mime.types">(), S<"/etc/httpd/conf/mime.types">(), S<"/etc/apache/mime.types">(), S<"/etc/apache2/mime.types">(), S<"/usr/local/etc/httpd/conf/mime.types">(), S<"/usr/local/lib/netscape/mime.types">(), S<"/usr/local/etc/httpd/conf/mime.types">(), S<"/usr/local/etc/mime.types">() }));
            define("mimetypes", "inited", boolean(false));
            define("mimetypes", "_db", none());
            define("mimetypes", "MimeTypes", w.make_class("MimeTypes", {  }, {
                { "__init__", w.method("MimeTypes", "__init__", { py_positional("filenames", py_tuple()), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filenames = a[1];
                        py strict = a[2];
                        py ext;
                        py name;
                        py type;
                        if (!(G<"mimetypes.inited">()).truthy())
                        {
                            (G<"mimetypes.init">())();
                        }
                        self.set_attr("encodings_map", (G<"mimetypes._encodings_map_default">()).method("copy"));
                        self.set_attr("suffix_map", (G<"mimetypes._suffix_map_default">()).method("copy"));
                        self.set_attr("types_map", py_tuple({ py_dict(), py_dict() }));
                        self.set_attr("types_map_inv", py_tuple({ py_dict(), py_dict() }));
                        for (py const& item : iterate{ (G<"mimetypes._types_map_default">()).method("items") })
                        {
                            auto const items2 = unpack(item, 2);
                            ext = items2[0];
                            type = items2[1];
                            self.method("add_type", { type, ext, boolean(true) });
                        }
                        for (py const& item : iterate{ (G<"mimetypes._common_types_default">()).method("items") })
                        {
                            auto const items4 = unpack(item, 2);
                            ext = items4[0];
                            type = items4[1];
                            self.method("add_type", { type, ext, boolean(false) });
                        }
                        for (py const& item : iterate{ filenames })
                        {
                            name = item;
                            self.method("read", { name, strict });
                        }
                        return none();
                    }) },
                { "add_type", w.method("MimeTypes", "add_type", { py_positional("type"), py_positional("ext"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py ext = a[2];
                        py strict = a[3];
                        py exts;
                        if (!type.truthy())
                        {
                            return none();
                        }
                        self.attr("types_map")[strict].set_item(ext, type);
                        exts = self.attr("types_map_inv")[strict].method("setdefault", { type, py_list() });
                        if (!exts.contains(ext))
                        {
                            exts.method("append", { ext });
                        }
                        return none();
                    }) },
                { "guess_type", w.method("MimeTypes", "guess_type", { py_positional("url"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py strict = a[2];
                        py comma;
                        py p;
                        py scheme;
                        py semi;
                        py type;
                        // TODO: Deprecate accepting file paths (in particular path-like objects).
                        url = (Mod<"os">().attr("fspath"))(url);
                        p = (Mod<"urllib.parse">().attr("urlparse"))(url);
                        if ((p.attr("scheme").truthy() && (py{ p.attr("scheme").size() } > (I<1>()))))
                        {
                            scheme = p.attr("scheme");
                            url = p.attr("path");
                        }
                        else
                        {
                            return self.method("guess_file_type", { url }, { { "strict", strict } });
                        }
                        if (scheme == (S<"data">()))
                        {
                            // syntax of data URLs:
                            // dataurl   := "data:" [ mediatype ] [ ";base64" ] "," data
                            // mediatype := [ type "/" subtype ] *( ";" parameter )
                            // data      := *urlchar
                            // parameter := attribute "=" value
                            // type/subtype defaults to "text/plain"
                            comma = url.method("find", { S<",">() });
                            if (comma < (I<0>()))
                            {
                                // bad data URL
                                return py_tuple({ none(), none() });
                            }
                            semi = url.method("find", { S<";">(), I<0>(), comma });
                            if (semi >= (I<0>()))
                            {
                                type = url[B<"slice">()(none(), semi, none())];
                            }
                            else
                            {
                                type = url[B<"slice">()(none(), comma, none())];
                            }
                            if ((type.contains((S<"=">())) || (!type.contains((S<"/">())))))
                            {
                                type = S<"text/plain">();
                            }
                            return py_tuple({ type, none() });
                        }
                        return self.method("_guess_file_type", { url, strict, Mod<"posixpath">().attr("splitext") });
                    }) },
                { "guess_file_type", w.method("MimeTypes", "guess_file_type", { py_positional("path"), py_keyword("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py strict = a[2];
                        path = (Mod<"os">().attr("fsdecode"))(path);
                        path = (Mod<"os">().attr("path")).method("splitdrive", { path })[I<1>()];
                        return self.method("_guess_file_type", { path, strict, (Mod<"os">().attr("path")).attr("splitext") });
                    }) },
                { "_guess_file_type", w.method("MimeTypes", "_guess_file_type", { py_positional("path"), py_positional("strict"), py_positional("splitext") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py strict = a[2];
                        py splitext = a[3];
                        py base;
                        py encoding;
                        py ext;
                        py ext_lower;
                        py types_map;
                        auto const items6 = unpack(splitext(path), 2);
                        base = items6[0];
                        ext = items6[1];
                        while (self.attr("suffix_map").contains(([&]() -> py { ext_lower = ext.method("lower"); return ext_lower; }())))
                        {
                            auto const items8 = unpack(splitext(base + self.attr("suffix_map")[ext_lower]), 2);
                            base = items8[0];
                            ext = items8[1];
                        }
                        // encodings_map is case sensitive
                        if (self.attr("encodings_map").contains(ext))
                        {
                            encoding = self.attr("encodings_map")[ext];
                            auto const items9 = unpack(splitext(base), 2);
                            base = items9[0];
                            ext = items9[1];
                        }
                        else
                        {
                            encoding = none();
                        }
                        ext = ext.method("lower");
                        types_map = self.attr("types_map")[boolean(true)];
                        if (types_map.contains(ext))
                        {
                            return py_tuple({ types_map[ext], encoding });
                        }
                        else if (strict.truthy())
                        {
                            return py_tuple({ none(), encoding });
                        }
                        types_map = self.attr("types_map")[boolean(false)];
                        if (types_map.contains(ext))
                        {
                            return py_tuple({ types_map[ext], encoding });
                        }
                        else
                        {
                            return py_tuple({ none(), encoding });
                        }
                        return none();
                    }) },
                { "guess_all_extensions", w.method("MimeTypes", "guess_all_extensions", { py_positional("type"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py strict = a[2];
                        py ext;
                        py extensions;
                        type = type.method("lower");
                        extensions = (B<"list">())(self.attr("types_map_inv")[boolean(true)].method("get", { type, py_list() }));
                        if (!strict.truthy())
                        {
                            for (py const& item : iterate{ self.attr("types_map_inv")[boolean(false)].method("get", { type, py_list() }) })
                            {
                                ext = item;
                                if (!extensions.contains(ext))
                                {
                                    extensions.method("append", { ext });
                                }
                            }
                        }
                        return extensions;
                    }) },
                { "guess_extension", w.method("MimeTypes", "guess_extension", { py_positional("type"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py strict = a[2];
                        py extensions;
                        extensions = self.method("guess_all_extensions", { type, strict });
                        if (!extensions.truthy())
                        {
                            return none();
                        }
                        return extensions[I<0>()];
                    }) },
                { "read", w.method("MimeTypes", "read", { py_positional("filename"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py strict = a[2];
                        py fp;
                        {
                            py const manager11 = (B<"open">()).call({ filename }, { { "encoding", S<"utf-8">() }, { "errors", S<"surrogateescape">() } });
                            py const exit11 = pyclass::special_method(manager11, "__exit__");
                            py const value11 = pyclass::special_method(manager11, "__enter__")(manager11);
                            fp = value11;
                            [[maybe_unused]] int flow11 = 0;
                            [[maybe_unused]] py result11;
                            bool exited11 = false;
                            try
                            {
                                self.method("readfp", { fp, strict });
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
                    }) },
                { "readfp", w.method("MimeTypes", "readfp", { py_positional("fp"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py strict = a[2];
                        py i;
                        py line;
                        py suff;
                        py suffixes;
                        py type;
                        py words;
                        while (([&]() -> py { line = fp.method("readline"); return line; }()).truthy())
                        {
                            words = line.method("split");
                            for (py const& item : iterate{ (B<"range">())(py{ words.size() }) })
                            {
                                i = item;
                                if (words[i][I<0>()] == (S<"#">()))
                                {
                                    del_item(words, B<"slice">()(i, none(), none()));
                                    break;
                                }
                            }
                            if (!words.truthy())
                            {
                                continue;
                            }
                            auto const items14 = unpack(py_tuple({ words[I<0>()], words[B<"slice">()(I<1>(), none(), none())] }), 2);
                            type = items14[0];
                            suffixes = items14[1];
                            for (py const& item : iterate{ suffixes })
                            {
                                suff = item;
                                self.method("add_type", { type, (S<".">()) + suff, strict });
                            }
                        }
                        return none();
                    }) },
                { "read_windows_registry", w.method("MimeTypes", "read_windows_registry", { py_positional("strict", boolean(true)) }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py strict = a[1];
                        py add_type;
                        if (((!(G<"mimetypes._mimetypes_read_windows_registry">()).truthy()) && (!(G<"mimetypes._winreg">()).truthy())))
                        {
                            return none();
                        }
                        add_type = (*self).attr("add_type");
                        if (strict.truthy())
                        {
                            add_type = w.function("<lambda>", "MimeTypes.read_windows_registry.<locals>.<lambda>", { py_positional("type"), py_positional("ext") }, [self](arguments a) -> py
        {
            py type = a[0];
            py ext = a[1];
            return (*self).method("add_type", { type, ext, boolean(true) });
        });
                        }
                        // Accelerated function if it is available
                        if ((G<"mimetypes._mimetypes_read_windows_registry">()).truthy())
                        {
                            (G<"mimetypes._mimetypes_read_windows_registry">())(add_type);
                        }
                        else if ((G<"mimetypes._winreg">()).truthy())
                        {
                            (*self).method("_read_windows_registry", { add_type });
                        }
                        return none();
                    }) },
                { "_read_windows_registry", w.class_method("MimeTypes", "_read_windows_registry", { py_positional("add_type") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py add_type = a[1];
                        py datatype;
                        py enum_types;
                        py hkcr;
                        py mimetype;
                        py subkey;
                        py subkeyname;
                        enum_types = w.function("enum_types", "MimeTypes._read_windows_registry.<locals>.enum_types", { py_positional("mimedb") }, [](arguments a) -> py
    {
        return make_native_generator("enum_types", "MimeTypes._read_windows_registry.<locals>.enum_types", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
        {
            py mimedb = a[0];
            py ctype;
            py i;
            i = I<0>();
            while (boolean(true).truthy())
            {
                {
                    bool otherwise17 = false;
                    try
                    {
                        ctype = (G<"mimetypes._winreg">()).method("EnumKey", { mimedb, i });
                        otherwise17 = true;
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler17;
                        if (matches(handler17.exception, B<"OSError">()))
                        {
                            break;
                        }
                        else
                            py_reraise(handler17.exception);
                    }
                    if (otherwise17)
                    {
                        if (!ctype.contains((S<"\u0000">())))
                        {
                            py_yield(ctype);
                        }
                    }
                }
                i = inplace(NEOS_PY_ADD, i, I<1>());
            }
            return none();
        });
    });
                        {
                            py const manager18 = (G<"mimetypes._winreg">()).method("OpenKey", { (G<"mimetypes._winreg">()).attr("HKEY_CLASSES_ROOT"), S<"">() });
                            py const exit18 = pyclass::special_method(manager18, "__exit__");
                            py const value18 = pyclass::special_method(manager18, "__enter__")(manager18);
                            hkcr = value18;
                            [[maybe_unused]] int flow18 = 0;
                            [[maybe_unused]] py result18;
                            bool exited18 = false;
                            try
                            {
                                for (py const& item : iterate{ enum_types(hkcr) })
                                {
                                    subkeyname = item;
                                    try
                                    {
                                        {
                                            py const manager21 = (G<"mimetypes._winreg">()).method("OpenKey", { hkcr, subkeyname });
                                            py const exit21 = pyclass::special_method(manager21, "__exit__");
                                            py const value21 = pyclass::special_method(manager21, "__enter__")(manager21);
                                            subkey = value21;
                                            [[maybe_unused]] int flow21 = 0;
                                            [[maybe_unused]] py result21;
                                            bool exited21 = false;
                                            try
                                            {
                                                // Only check file extensions
                                                if (!subkeyname.method("startswith", { S<".">() }).truthy())
                                                {
                                                    flow21 = 3;
                                                    goto finally21;
                                                }
                                                // raises OSError if no 'Content Type' value
                                                auto const items22 = unpack((G<"mimetypes._winreg">()).method("QueryValueEx", { subkey, S<"Content Type">() }), 2);
                                                mimetype = items22[0];
                                                datatype = items22[1];
                                                if (datatype != (G<"mimetypes._winreg">()).attr("REG_SZ"))
                                                {
                                                    flow21 = 3;
                                                    goto finally21;
                                                }
                                                add_type(mimetype, subkeyname);
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler21;
                                                exited21 = true;
                                                if (!exit21(manager21, type_of(handler21.exception), handler21.exception, handler21.exception.attr("__traceback__")).truthy())
                                                    py_reraise(handler21.exception);
                                            }
                                            finally21:
                                            if (!exited21)
                                                exit21(manager21, none(), none(), none());
                                            if (flow21 == 1)
                                                { result18 = result21; flow18 = 1; goto finally18; }
                                            if (flow21 == 2)
                                                break;
                                            if (flow21 == 3)
                                                continue;
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler20;
                                        if (matches(handler20.exception, B<"OSError">()))
                                        {
                                            continue;
                                        }
                                        else
                                            py_reraise(handler20.exception);
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler18;
                                exited18 = true;
                                if (!exit18(manager18, type_of(handler18.exception), handler18.exception, handler18.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler18.exception);
                            }
                            finally18:
                            if (!exited18)
                                exit18(manager18, none(), none(), none());
                            if (flow18 == 1)
                                return result18;
                        }
                        return none();
                    }) }, }));

            define("mimetypes", "guess_type", w.function("guess_type", "guess_type", { py_positional("url"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py url = a[0];
                    py strict = a[1];
                    if ((G<"mimetypes._db">()).is_none())
                    {
                        (G<"mimetypes.init">())();
                    }
                    return (G<"mimetypes._db">()).method("guess_type", { url, strict });
                }));

            define("mimetypes", "guess_file_type", w.function("guess_file_type", "guess_file_type", { py_positional("path"), py_keyword("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py path = a[0];
                    py strict = a[1];
                    if ((G<"mimetypes._db">()).is_none())
                    {
                        (G<"mimetypes.init">())();
                    }
                    return (G<"mimetypes._db">()).method("guess_file_type", { path }, { { "strict", strict } });
                }));

            define("mimetypes", "guess_all_extensions", w.function("guess_all_extensions", "guess_all_extensions", { py_positional("type"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py type = a[0];
                    py strict = a[1];
                    if ((G<"mimetypes._db">()).is_none())
                    {
                        (G<"mimetypes.init">())();
                    }
                    return (G<"mimetypes._db">()).method("guess_all_extensions", { type, strict });
                }));

            define("mimetypes", "guess_extension", w.function("guess_extension", "guess_extension", { py_positional("type"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py type = a[0];
                    py strict = a[1];
                    if ((G<"mimetypes._db">()).is_none())
                    {
                        (G<"mimetypes.init">())();
                    }
                    return (G<"mimetypes._db">()).method("guess_extension", { type, strict });
                }));

            define("mimetypes", "add_type", w.function("add_type", "add_type", { py_positional("type"), py_positional("ext"), py_positional("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py type = a[0];
                    py ext = a[1];
                    py strict = a[2];
                    if ((G<"mimetypes._db">()).is_none())
                    {
                        (G<"mimetypes.init">())();
                    }
                    return (G<"mimetypes._db">()).method("add_type", { type, ext, strict });
                }));

            define("mimetypes", "init", w.function("init", "init", { py_positional("files", none()) }, [](arguments a) -> py
                {
                    py files = a[0];
                    py db;
                    py file;
                    set_global("mimetypes", "inited", boolean(true));
                    if ((files.is_none() || (G<"mimetypes._db">()).is_none()))
                    {
                        db = (G<"mimetypes.MimeTypes">())();
                        // Quick return if not supported
                        db.method("read_windows_registry");
                        if (files.is_none())
                        {
                            files = G<"mimetypes.knownfiles">();
                        }
                        else
                        {
                            files = (G<"mimetypes.knownfiles">()) + (B<"list">())(files);
                        }
                    }
                    else
                    {
                        db = G<"mimetypes._db">();
                    }
                    for (py const& item : iterate{ files })
                    {
                        file = item;
                        if ((Mod<"os">().attr("path")).method("isfile", { file }).truthy())
                        {
                            db.method("read", { file });
                        }
                    }
                    set_global("mimetypes", "encodings_map", db.attr("encodings_map"));
                    set_global("mimetypes", "suffix_map", db.attr("suffix_map"));
                    set_global("mimetypes", "types_map", db.attr("types_map")[boolean(true)]);
                    set_global("mimetypes", "common_types", db.attr("types_map")[boolean(false)]);
                    // Make the DB a global variable now that it is fully initialized
                    set_global("mimetypes", "_db", db);
                    return none();
                }));

            define("mimetypes", "read_mime_types", w.function("read_mime_types", "read_mime_types", { py_positional("file") }, [](arguments a) -> py
                {
                    py file = a[0];
                    py db;
                    py f;
                    try
                    {
                        f = (B<"open">()).call({ file }, { { "encoding", S<"utf-8">() }, { "errors", S<"surrogateescape">() } });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler24;
                        if (matches(handler24.exception, B<"OSError">()))
                        {
                            return none();
                        }
                        else
                            py_reraise(handler24.exception);
                    }
                    {
                        py const manager25 = f;
                        py const exit25 = pyclass::special_method(manager25, "__exit__");
                        py const value25 = pyclass::special_method(manager25, "__enter__")(manager25);
                        [[maybe_unused]] int flow25 = 0;
                        [[maybe_unused]] py result25;
                        bool exited25 = false;
                        try
                        {
                            db = (G<"mimetypes.MimeTypes">())();
                            db.method("readfp", { f, boolean(true) });
                            result25 = db.attr("types_map")[boolean(true)];
                            flow25 = 1;
                            goto finally25;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler25;
                            exited25 = true;
                            if (!exit25(manager25, type_of(handler25.exception), handler25.exception, handler25.exception.attr("__traceback__")).truthy())
                                py_reraise(handler25.exception);
                        }
                        finally25:
                        if (!exited25)
                            exit25(manager25, none(), none(), none());
                        if (flow25 == 1)
                            return result25;
                    }
                    return none();
                }));

            define("mimetypes", "_default_mime_types", w.function("_default_mime_types", "_default_mime_types", {  }, [](arguments a) -> py
                {
                    py const value26 = dict_of({ { S<".svgz">(), S<".svg.gz">() }, { S<".tgz">(), S<".tar.gz">() }, { S<".taz">(), S<".tar.gz">() }, { S<".tz">(), S<".tar.gz">() }, { S<".tbz2">(), S<".tar.bz2">() }, { S<".txz">(), S<".tar.xz">() } });
                    set_global("mimetypes", "suffix_map", value26);
                    set_global("mimetypes", "_suffix_map_default", value26);
                    py const value27 = dict_of({ { S<".gz">(), S<"gzip">() }, { S<".Z">(), S<"compress">() }, { S<".bz2">(), S<"bzip2">() }, { S<".xz">(), S<"xz">() }, { S<".br">(), S<"br">() } });
                    set_global("mimetypes", "encodings_map", value27);
                    set_global("mimetypes", "_encodings_map_default", value27);
                    // Before adding new types, make sure they are either registered with IANA,
                    // at http://www.iana.org/assignments/media-types
                    // or extensions, i.e. using the x- prefix
                    // If you add to these, please keep them sorted by mime type.
                    // Make sure the entry with the preferred file extension for a particular mime type
                    // appears before any others of the same mimetype.
                    py const value28 = dict_of({ { S<".js">(), S<"text/javascript">() }, { S<".mjs">(), S<"text/javascript">() }, { S<".json">(), S<"application/json">() }, { S<".webmanifest">(), S<"application/manifest+json">() }, { S<".doc">(), S<"application/msword">() }, { S<".dot">(), S<"application/msword">() }, { S<".wiz">(), S<"application/msword">() }, { S<".nq">(), S<"application/n-quads">() }, { S<".nt">(), S<"application/n-triples">() }, { S<".bin">(), S<"application/octet-stream">() }, { S<".a">(), S<"application/octet-stream">() }, { S<".dll">(), S<"application/octet-stream">() }, { S<".exe">(), S<"application/octet-stream">() }, { S<".o">(), S<"application/octet-stream">() }, { S<".obj">(), S<"application/octet-stream">() }, { S<".so">(), S<"application/octet-stream">() }, { S<".oda">(), S<"application/oda">() }, { S<".pdf">(), S<"application/pdf">() }, { S<".p7c">(), S<"application/pkcs7-mime">() }, { S<".ps">(), S<"application/postscript">() }, { S<".ai">(), S<"application/postscript">() }, { S<".eps">(), S<"application/postscript">() }, { S<".trig">(), S<"application/trig">() }, { S<".m3u">(), S<"application/vnd.apple.mpegurl">() }, { S<".m3u8">(), S<"application/vnd.apple.mpegurl">() }, { S<".xls">(), S<"application/vnd.ms-excel">() }, { S<".xlb">(), S<"application/vnd.ms-excel">() }, { S<".ppt">(), S<"application/vnd.ms-powerpoint">() }, { S<".pot">(), S<"application/vnd.ms-powerpoint">() }, { S<".ppa">(), S<"application/vnd.ms-powerpoint">() }, { S<".pps">(), S<"application/vnd.ms-powerpoint">() }, { S<".pwz">(), S<"application/vnd.ms-powerpoint">() }, { S<".wasm">(), S<"application/wasm">() }, { S<".bcpio">(), S<"application/x-bcpio">() }, { S<".cpio">(), S<"application/x-cpio">() }, { S<".csh">(), S<"application/x-csh">() }, { S<".dvi">(), S<"application/x-dvi">() }, { S<".gtar">(), S<"application/x-gtar">() }, { S<".hdf">(), S<"application/x-hdf">() }, { S<".h5">(), S<"application/x-hdf5">() }, { S<".latex">(), S<"application/x-latex">() }, { S<".mif">(), S<"application/x-mif">() }, { S<".cdf">(), S<"application/x-netcdf">() }, { S<".nc">(), S<"application/x-netcdf">() }, { S<".p12">(), S<"application/x-pkcs12">() }, { S<".pfx">(), S<"application/x-pkcs12">() }, { S<".ram">(), S<"application/x-pn-realaudio">() }, { S<".pyc">(), S<"application/x-python-code">() }, { S<".pyo">(), S<"application/x-python-code">() }, { S<".sh">(), S<"application/x-sh">() }, { S<".shar">(), S<"application/x-shar">() }, { S<".swf">(), S<"application/x-shockwave-flash">() }, { S<".sv4cpio">(), S<"application/x-sv4cpio">() }, { S<".sv4crc">(), S<"application/x-sv4crc">() }, { S<".tar">(), S<"application/x-tar">() }, { S<".tcl">(), S<"application/x-tcl">() }, { S<".tex">(), S<"application/x-tex">() }, { S<".texi">(), S<"application/x-texinfo">() }, { S<".texinfo">(), S<"application/x-texinfo">() }, { S<".roff">(), S<"application/x-troff">() }, { S<".t">(), S<"application/x-troff">() }, { S<".tr">(), S<"application/x-troff">() }, { S<".man">(), S<"application/x-troff-man">() }, { S<".me">(), S<"application/x-troff-me">() }, { S<".ms">(), S<"application/x-troff-ms">() }, { S<".ustar">(), S<"application/x-ustar">() }, { S<".src">(), S<"application/x-wais-source">() }, { S<".xsl">(), S<"application/xml">() }, { S<".rdf">(), S<"application/xml">() }, { S<".wsdl">(), S<"application/xml">() }, { S<".xpdl">(), S<"application/xml">() }, { S<".zip">(), S<"application/zip">() }, { S<".3gp">(), S<"audio/3gpp">() }, { S<".3gpp">(), S<"audio/3gpp">() }, { S<".3g2">(), S<"audio/3gpp2">() }, { S<".3gpp2">(), S<"audio/3gpp2">() }, { S<".aac">(), S<"audio/aac">() }, { S<".adts">(), S<"audio/aac">() }, { S<".loas">(), S<"audio/aac">() }, { S<".ass">(), S<"audio/aac">() }, { S<".au">(), S<"audio/basic">() }, { S<".snd">(), S<"audio/basic">() }, { S<".mp3">(), S<"audio/mpeg">() }, { S<".mp2">(), S<"audio/mpeg">() }, { S<".opus">(), S<"audio/opus">() }, { S<".aif">(), S<"audio/x-aiff">() }, { S<".aifc">(), S<"audio/x-aiff">() }, { S<".aiff">(), S<"audio/x-aiff">() }, { S<".ra">(), S<"audio/x-pn-realaudio">() }, { S<".wav">(), S<"audio/x-wav">() }, { S<".avif">(), S<"image/avif">() }, { S<".bmp">(), S<"image/bmp">() }, { S<".gif">(), S<"image/gif">() }, { S<".ief">(), S<"image/ief">() }, { S<".jpg">(), S<"image/jpeg">() }, { S<".jpe">(), S<"image/jpeg">() }, { S<".jpeg">(), S<"image/jpeg">() }, { S<".heic">(), S<"image/heic">() }, { S<".heif">(), S<"image/heif">() }, { S<".png">(), S<"image/png">() }, { S<".svg">(), S<"image/svg+xml">() }, { S<".tiff">(), S<"image/tiff">() }, { S<".tif">(), S<"image/tiff">() }, { S<".ico">(), S<"image/vnd.microsoft.icon">() }, { S<".webp">(), S<"image/webp">() }, { S<".ras">(), S<"image/x-cmu-raster">() }, { S<".pnm">(), S<"image/x-portable-anymap">() }, { S<".pbm">(), S<"image/x-portable-bitmap">() }, { S<".pgm">(), S<"image/x-portable-graymap">() }, { S<".ppm">(), S<"image/x-portable-pixmap">() }, { S<".rgb">(), S<"image/x-rgb">() }, { S<".xbm">(), S<"image/x-xbitmap">() }, { S<".xpm">(), S<"image/x-xpixmap">() }, { S<".xwd">(), S<"image/x-xwindowdump">() }, { S<".eml">(), S<"message/rfc822">() }, { S<".mht">(), S<"message/rfc822">() }, { S<".mhtml">(), S<"message/rfc822">() }, { S<".nws">(), S<"message/rfc822">() }, { S<".css">(), S<"text/css">() }, { S<".csv">(), S<"text/csv">() }, { S<".html">(), S<"text/html">() }, { S<".htm">(), S<"text/html">() }, { S<".md">(), S<"text/markdown">() }, { S<".markdown">(), S<"text/markdown">() }, { S<".n3">(), S<"text/n3">() }, { S<".txt">(), S<"text/plain">() }, { S<".bat">(), S<"text/plain">() }, { S<".c">(), S<"text/plain">() }, { S<".h">(), S<"text/plain">() }, { S<".ksh">(), S<"text/plain">() }, { S<".pl">(), S<"text/plain">() }, { S<".srt">(), S<"text/plain">() }, { S<".rtx">(), S<"text/richtext">() }, { S<".rtf">(), S<"text/rtf">() }, { S<".tsv">(), S<"text/tab-separated-values">() }, { S<".vtt">(), S<"text/vtt">() }, { S<".py">(), S<"text/x-python">() }, { S<".rst">(), S<"text/x-rst">() }, { S<".etx">(), S<"text/x-setext">() }, { S<".sgm">(), S<"text/x-sgml">() }, { S<".sgml">(), S<"text/x-sgml">() }, { S<".vcf">(), S<"text/x-vcard">() }, { S<".xml">(), S<"text/xml">() }, { S<".mp4">(), S<"video/mp4">() }, { S<".mpeg">(), S<"video/mpeg">() }, { S<".m1v">(), S<"video/mpeg">() }, { S<".mpa">(), S<"video/mpeg">() }, { S<".mpe">(), S<"video/mpeg">() }, { S<".mpg">(), S<"video/mpeg">() }, { S<".mov">(), S<"video/quicktime">() }, { S<".qt">(), S<"video/quicktime">() }, { S<".webm">(), S<"video/webm">() }, { S<".avi">(), S<"video/x-msvideo">() }, { S<".movie">(), S<"video/x-sgi-movie">() } });
                    set_global("mimetypes", "types_map", value28);
                    set_global("mimetypes", "_types_map_default", value28);
                    // These are non-standard types, commonly found in the wild.  They will
                    // only match if strict=0 flag is given to the API methods.
                    // Please sort these too
                    py const value29 = dict_of({ { S<".rtf">(), S<"application/rtf">() }, { S<".midi">(), S<"audio/midi">() }, { S<".mid">(), S<"audio/midi">() }, { S<".jpg">(), S<"image/jpg">() }, { S<".pict">(), S<"image/pict">() }, { S<".pct">(), S<"image/pict">() }, { S<".pic">(), S<"image/pict">() }, { S<".xul">(), S<"text/xul">() } });
                    set_global("mimetypes", "common_types", value29);
                    set_global("mimetypes", "_common_types_default", value29);
                    return none();
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"mimetypes._default_mime_types">())();
                    return none();
                }))();
            define("mimetypes", "_main", w.function("_main", "_main", {  }, [](arguments a) -> py
                {
                    auto const USAGE = std::make_shared<py>();
                    py arg;
                    py args;
                    py encoding;
                    py extension;
                    py getopt;
                    py gtype;
                    py guess;
                    py msg;
                    py opt;
                    py opts;
                    py strict;
                    py usage;
                    getopt = module_of("getopt");
                    *USAGE = S<"Usage: mimetypes.py [options] type\n\nOptions:\n    --help / -h       -- print this message and exit\n    --lenient / -l    -- additionally search of some common, but non-standard\n                         types.\n    --extension / -e  -- guess extension instead of type\n\nMore than one type argument may be given.\n">();
                    usage = w.function("usage", "_main.<locals>.usage", { py_positional("code"), py_positional("msg", S<"">()) }, [USAGE](arguments a) -> py
    {
        py code = a[0];
        py msg = a[1];
        (B<"print">())((*USAGE));
        if (msg.truthy())
        {
            (B<"print">())(msg);
        }
        (Mod<"sys">().attr("exit"))(code);
        return none();
    });
                    try
                    {
                        auto const items31 = unpack(getopt.method("getopt", { (Mod<"sys">().attr("argv"))[B<"slice">()(I<1>(), none(), none())], S<"hle">(), py_list({ S<"help">(), S<"lenient">(), S<"extension">() }) }), 2);
                        opts = items31[0];
                        args = items31[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler30;
                        if (matches(handler30.exception, getopt.attr("error")))
                        {
                            msg = handler30.exception;
                            usage(I<1>(), msg);
                        }
                        else
                            py_reraise(handler30.exception);
                    }
                    strict = I<1>();
                    extension = I<0>();
                    for (py const& item : iterate{ opts })
                    {
                        auto const items33 = unpack(item, 2);
                        opt = items33[0];
                        arg = items33[1];
                        if (py_tuple({ S<"-h">(), S<"--help">() }).contains(opt))
                        {
                            usage(I<0>());
                        }
                        else if (py_tuple({ S<"-l">(), S<"--lenient">() }).contains(opt))
                        {
                            strict = I<0>();
                        }
                        else if (py_tuple({ S<"-e">(), S<"--extension">() }).contains(opt))
                        {
                            extension = I<1>();
                        }
                    }
                    for (py const& item : iterate{ args })
                    {
                        gtype = item;
                        if (extension.truthy())
                        {
                            guess = (G<"mimetypes.guess_extension">())(gtype, strict);
                            if (!guess.truthy())
                            {
                                (B<"print">())(S<"I don't know anything about type">(), gtype);
                            }
                            else
                            {
                                (B<"print">())(guess);
                            }
                        }
                        else
                        {
                            auto const items35 = unpack((G<"mimetypes.guess_type">())(gtype, strict), 2);
                            guess = items35[0];
                            encoding = items35[1];
                            if (!guess.truthy())
                            {
                                (B<"print">())(S<"I don't know anything about type">(), gtype);
                            }
                            else
                            {
                                (B<"print">())(S<"type:">(), guess, S<"encoding:">(), encoding);
                            }
                        }
                    }
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool mimetypes_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_mimetypes();
                }
                if (auto const doc = mimetypes_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("posixpath", py{ fn_import("posixpath") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("mimetypes", mimetypes_module);
    }
}
