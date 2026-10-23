/*
  tomllib_parser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/tomllib/_parser.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/tomllib/_parser.py's copyright: SPDX-License-Identifier: MIT.
  SPDX-FileCopyrightText: 2021 Taneli Hukkinen. Licensed to PSF under a Contributor Agreement.
*/

// The module tomllib._parser (CPython 3.13's Lib/tomllib/_parser.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "tomllib_parser_docs.hpp"

namespace neos::python_runtime::detail::tomllib__parser_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "tomllib._parser.name"))
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
                py_raise("SystemError", std::string{ "tomllib._parser: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"tomllib._parser.name">()))
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
            static module_writer const sWriter{ "tomllib._parser", tomllib_parser_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- tomllib._parser (Lib/tomllib/_parser.py)

        void make_tomllib__parser()
        {
            static module_writer const& w = writer("tomllib._parser");
            // SPDX-License-Identifier: MIT
            // SPDX-FileCopyrightText: 2021 Taneli Hukkinen
            // Licensed to PSF under a Contributor Agreement.
            // Pathologically excessive number of parts in a key runs into quadratic
            // behavior (e.g. in Flags.is_).
            // Even if keys aren't currently parsed using recursion, they name a
            // recursive structure, so it makes sense to limit it using getrecursionlimit()
            // and RecursionError.
            define("tomllib._parser", "MAX_KEY_PARTS", (Mod<"sys">()).method("getrecursionlimit"));
            define("tomllib._parser", "ASCII_CTRL", (B<"frozenset">())([&]() -> py { py const comprehension1 = py_list(); py i; for (py const& item : iterate{ (B<"range">())(I<32>()) }) { i = item; comprehension1.method("append", { (B<"chr">())(i) }); } return comprehension1; }()) | (B<"frozenset">())((B<"chr">())(I<127>())));
            // Neither of these sets include quotation mark or backslash. They are
            // currently handled as separate cases in the parser functions.
            define("tomllib._parser", "ILLEGAL_BASIC_STR_CHARS", (G<"tomllib._parser.ASCII_CTRL">()) - (B<"frozenset">())(S<"\t">()));
            define("tomllib._parser", "ILLEGAL_MULTILINE_BASIC_STR_CHARS", (G<"tomllib._parser.ASCII_CTRL">()) - (B<"frozenset">())(S<"\t\n">()));
            define("tomllib._parser", "ILLEGAL_LITERAL_STR_CHARS", G<"tomllib._parser.ILLEGAL_BASIC_STR_CHARS">());
            define("tomllib._parser", "ILLEGAL_MULTILINE_LITERAL_STR_CHARS", G<"tomllib._parser.ILLEGAL_MULTILINE_BASIC_STR_CHARS">());
            define("tomllib._parser", "ILLEGAL_COMMENT_CHARS", G<"tomllib._parser.ILLEGAL_BASIC_STR_CHARS">());
            define("tomllib._parser", "TOML_WS", (B<"frozenset">())(S<" \t">()));
            define("tomllib._parser", "TOML_WS_AND_NEWLINE", (G<"tomllib._parser.TOML_WS">()) | (B<"frozenset">())(S<"\n">()));
            define("tomllib._parser", "BARE_KEY_CHARS", (B<"frozenset">())(((Mod<"string">().attr("ascii_letters")) + (Mod<"string">().attr("digits"))) + (S<"-_">())));
            define("tomllib._parser", "KEY_INITIAL_CHARS", (G<"tomllib._parser.BARE_KEY_CHARS">()) | (B<"frozenset">())(S<"\"'">()));
            define("tomllib._parser", "HEXDIGIT_CHARS", (B<"frozenset">())(Mod<"string">().attr("hexdigits")));
            define("tomllib._parser", "BASIC_STR_ESCAPE_REPLACEMENTS", (Mod<"types">().attr("MappingProxyType"))(dict_of({ { S<"\\b">(), S<"\b">() }, { S<"\\t">(), S<"\t">() }, { S<"\\n">(), S<"\n">() }, { S<"\\f">(), S<"\f">() }, { S<"\\r">(), S<"\r">() }, { S<"\\\"">(), S<"\"">() }, { S<"\\\\">(), S<"\\">() } })));
            define("tomllib._parser", "TOMLDecodeError", w.make_class("TOMLDecodeError", { B<"ValueError">() }, {
 }));

            define("tomllib._parser", "load", w.function("load", "load", { py_positional_only("fp"), py_keyword("parse_float", B<"float">()) }, [](arguments a) -> py
                {
                    py fp = a[0];
                    py parse_float = a[1];
                    py b;
                    py s;
                    b = fp.method("read");
                    try
                    {
                        s = b.method("decode");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler2;
                        if (matches(handler2.exception, B<"AttributeError">()))
                        {
                            fn_raise_from((B<"TypeError">())(S<"File must be opened in binary mode, e.g. use `open('foo.toml', 'rb')`">()).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler2.exception);
                    }
                    return (G<"tomllib._parser.loads">()).call({ s }, { { "parse_float", parse_float } });
                }));

            define("tomllib._parser", "loads", w.function("loads", "loads", { py_positional_only("s"), py_keyword("parse_float", B<"float">()) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py parse_float = a[1];
                    py char_;
                    py header;
                    py out;
                    py pos;
                    py second_char;
                    py src;
                    // The spec allows converting "\r\n" to "\n", even in string
                    // literals. Let's do so to simplify parsing.
                    src = s.method("replace", { S<"\r\n">(), S<"\n">() });
                    pos = I<0>();
                    out = (G<"tomllib._parser.Output">())((G<"tomllib._parser.NestedDict">())(), (G<"tomllib._parser.Flags">())());
                    header = py_tuple();
                    parse_float = (G<"tomllib._parser.make_safe_parse_float">())(parse_float);
                    // Parse one statement at a time
                    // (typically means one line in TOML source)
                    while (boolean(true).truthy())
                    {
                        // 1. Skip line leading whitespace
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                        // 2. Parse rules. Expect one of the following:
                        // - end of file
                        // - end of line
                        // - comment
                        // - key/value pair
                        // - append dict to list (and move to its namespace)
                        // - create dict (and move to its namespace)
                        // Skip trailing whitespace when applicable.
                        try
                        {
                            char_ = src[pos];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, B<"IndexError">()))
                            {
                                break;
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                        if (char_ == (S<"\n">()))
                        {
                            pos = inplace(NEOS_PY_ADD, pos, I<1>());
                            continue;
                        }
                        if ((G<"tomllib._parser.KEY_INITIAL_CHARS">()).contains(char_))
                        {
                            pos = (G<"tomllib._parser.key_value_rule">())(src, pos, out, header, parse_float);
                            pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                        }
                        else if (char_ == (S<"[">()))
                        {
                            try
                            {
                                second_char = src[pos + (I<1>())];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, B<"IndexError">()))
                                {
                                    second_char = none();
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            out.attr("flags").method("finalize_pending");
                            if (second_char == (S<"[">()))
                            {
                                auto const items6 = unpack((G<"tomllib._parser.create_list_rule">())(src, pos, out), 2);
                                pos = items6[0];
                                header = items6[1];
                            }
                            else
                            {
                                auto const items7 = unpack((G<"tomllib._parser.create_dict_rule">())(src, pos, out), 2);
                                pos = items7[0];
                                header = items7[1];
                            }
                            pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                        }
                        else if (char_ != (S<"#">()))
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Invalid statement">()));
                        }
                        // 3. Skip comment
                        pos = (G<"tomllib._parser.skip_comment">())(src, pos);
                        // 4. Expect end of line or end of file
                        try
                        {
                            char_ = src[pos];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler8;
                            if (matches(handler8.exception, B<"IndexError">()))
                            {
                                break;
                            }
                            else
                                py_reraise(handler8.exception);
                        }
                        if (char_ != (S<"\n">()))
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Expected newline or end of document after a statement">()));
                        }
                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    }
                    return out.attr("data").attr("dict");
                }));

                // Marks an immutable namespace (inline array or inline table).
                // Marks a nest that has been explicitly created and can no longer
                // be opened using the "[table]" syntax.
            define("tomllib._parser", "Flags", w.make_class("Flags", {  }, {
                { "FROZEN", I<0>() },
                { "EXPLICIT_NEST", I<1>() },
                { "__init__", w.method("Flags", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_flags", py_dict());
                        self.set_attr("_pending_flags", (B<"set">())());
                        return none();
                    }) },
                { "add_pending", w.method("Flags", "add_pending", { py_positional("key"), py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py flag = a[2];
                        self.attr("_pending_flags").method("add", { py_tuple({ key, flag }) });
                        return none();
                    }) },
                { "finalize_pending", w.method("Flags", "finalize_pending", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag;
                        py key;
                        for (py const& item : iterate{ self.attr("_pending_flags") })
                        {
                            auto const items10 = unpack(item, 2);
                            key = items10[0];
                            flag = items10[1];
                            self.method("set", { key, flag }, { { "recursive", boolean(false) } });
                        }
                        self.attr("_pending_flags").method("clear");
                        return none();
                    }) },
                { "unset_all", w.method("Flags", "unset_all", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py cont;
                        py k;
                        cont = self.attr("_flags");
                        for (py const& item : iterate{ key[B<"slice">()(none(), I<-1>(), none())] })
                        {
                            k = item;
                            if (!cont.contains(k))
                            {
                                return none();
                            }
                            cont = cont[k][S<"nested">()];
                        }
                        cont.method("pop", { key[I<-1>()], none() });
                        return none();
                    }) },
                { "set", w.method("Flags", "set", { py_positional("key"), py_positional("flag"), py_keyword("recursive") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py flag = a[2];
                        py recursive = a[3];
                        py cont;
                        py k;
                        py key_parent;
                        py key_stem;
                        cont = self.attr("_flags");
                        auto const items12 = unpack(py_tuple({ key[B<"slice">()(none(), I<-1>(), none())], key[I<-1>()] }), 2);
                        key_parent = items12[0];
                        key_stem = items12[1];
                        for (py const& item : iterate{ key_parent })
                        {
                            k = item;
                            if (!cont.contains(k))
                            {
                                cont.set_item(k, dict_of({ { S<"flags">(), (B<"set">())() }, { S<"recursive_flags">(), (B<"set">())() }, { S<"nested">(), py_dict() } }));
                            }
                            cont = cont[k][S<"nested">()];
                        }
                        if (!cont.contains(key_stem))
                        {
                            cont.set_item(key_stem, dict_of({ { S<"flags">(), (B<"set">())() }, { S<"recursive_flags">(), (B<"set">())() }, { S<"nested">(), py_dict() } }));
                        }
                        cont[key_stem][(recursive.truthy() ? S<"recursive_flags">() : S<"flags">())].method("add", { flag });
                        return none();
                    }) },
                { "is_", w.method("Flags", "is_", { py_positional("key"), py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py flag = a[2];
                        py cont;
                        py inner_cont;
                        py k;
                        py key_stem;
                        if (!key.truthy())
                        {
                            return boolean(false);
                        }
                        cont = self.attr("_flags");
                        for (py const& item : iterate{ key[B<"slice">()(none(), I<-1>(), none())] })
                        {
                            k = item;
                            if (!cont.contains(k))
                            {
                                return boolean(false);
                            }
                            inner_cont = cont[k];
                            if (inner_cont[S<"recursive_flags">()].contains(flag))
                            {
                                return boolean(true);
                            }
                            cont = inner_cont[S<"nested">()];
                        }
                        key_stem = key[I<-1>()];
                        if (cont.contains(key_stem))
                        {
                            cont = cont[key_stem];
                            return [&]() -> py { py const t = boolean(cont[S<"flags">()].contains(flag)); return t.truthy() ? t : boolean(cont[S<"recursive_flags">()].contains(flag)); }();
                        }
                        return boolean(false);
                    }) }, }));

            define("tomllib._parser", "NestedDict", w.make_class("NestedDict", {  }, {
                { "__init__", w.method("NestedDict", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("dict", py_dict());
                        return none();
                    }) },
                { "get_or_create_nest", w.method("NestedDict", "get_or_create_nest", { py_positional("key"), py_keyword("access_lists", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py access_lists = a[2];
                        py cont;
                        py k;
                        cont = self.attr("dict");
                        for (py const& item : iterate{ key })
                        {
                            k = item;
                            if (!cont.contains(k))
                            {
                                cont.set_item(k, py_dict());
                            }
                            cont = cont[k];
                            if ((access_lists.truthy() && isinstance(cont, B<"list">())))
                            {
                                cont = cont[I<-1>()];
                            }
                            if (!isinstance(cont, B<"dict">()))
                            {
                                py_raise((B<"KeyError">())(S<"There is no nest behind this key">()));
                            }
                        }
                        return cont;
                    }) },
                { "append_nest_to_list", w.method("NestedDict", "append_nest_to_list", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py cont;
                        py last_key;
                        py list_;
                        cont = self.method("get_or_create_nest", { key[B<"slice">()(none(), I<-1>(), none())] });
                        last_key = key[I<-1>()];
                        if (cont.contains(last_key))
                        {
                            list_ = cont[last_key];
                            if (!isinstance(list_, B<"list">()))
                            {
                                py_raise((B<"KeyError">())(S<"An object other than list found behind this key">()));
                            }
                            list_.method("append", { py_dict() });
                        }
                        else
                        {
                            cont.set_item(last_key, py_list({ py_dict() }));
                        }
                        return none();
                    }) }, }));

            define("tomllib._parser", "Output", w.make_class("Output", { Mod<"typing">().attr("NamedTuple") }, {
                { "__annotations__", dict_of({ { S<"data">(), S<"NestedDict">() }, { S<"flags">(), S<"Flags">() } }) }, }));

            define("tomllib._parser", "skip_chars", w.function("skip_chars", "skip_chars", { py_positional("src"), py_positional("pos"), py_positional("chars") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py chars = a[2];
                    try
                    {
                        while (chars.contains(src[pos]))
                        {
                            pos = inplace(NEOS_PY_ADD, pos, I<1>());
                        }
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler16;
                        if (matches(handler16.exception, B<"IndexError">()))
                        {
                        }
                        else
                            py_reraise(handler16.exception);
                    }
                    return pos;
                }));

            define("tomllib._parser", "skip_until", w.function("skip_until", "skip_until", { py_positional("src"), py_positional("pos"), py_positional("expect"), py_keyword("error_on"), py_keyword("error_on_eof") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py expect = a[2];
                    py error_on = a[3];
                    py error_on_eof = a[4];
                    py new_pos;
                    try
                    {
                        new_pos = src.method("index", { expect, pos });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler18;
                        if (matches(handler18.exception, B<"ValueError">()))
                        {
                            new_pos = py{ src.size() };
                            if (error_on_eof.truthy())
                            {
                                fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, new_pos, S(std::string{ "Expected " } + expect.repr())).get(), none().get());
                                throw raised{};
                            }
                        }
                        else
                            py_reraise(handler18.exception);
                    }
                    if (!error_on.method("isdisjoint", { src[B<"slice">()(pos, new_pos, none())] }).truthy())
                    {
                        while (!error_on.contains(src[pos]))
                        {
                            pos = inplace(NEOS_PY_ADD, pos, I<1>());
                        }
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Found invalid character " } + src[pos].repr())));
                    }
                    return new_pos;
                }));

            define("tomllib._parser", "skip_comment", w.function("skip_comment", "skip_comment", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py char_;
                    try
                    {
                        char_ = src[pos];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler20;
                        if (matches(handler20.exception, B<"IndexError">()))
                        {
                            char_ = none();
                        }
                        else
                            py_reraise(handler20.exception);
                    }
                    if (char_ == (S<"#">()))
                    {
                        return (G<"tomllib._parser.skip_until">()).call({ src, pos + (I<1>()), S<"\n">() }, { { "error_on", G<"tomllib._parser.ILLEGAL_COMMENT_CHARS">() }, { "error_on_eof", boolean(false) } });
                    }
                    return pos;
                }));

            define("tomllib._parser", "skip_comments_and_array_ws", w.function("skip_comments_and_array_ws", "skip_comments_and_array_ws", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py pos_before_skip;
                    while (boolean(true).truthy())
                    {
                        pos_before_skip = pos;
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS_AND_NEWLINE">());
                        pos = (G<"tomllib._parser.skip_comment">())(src, pos);
                        if (pos == pos_before_skip)
                        {
                            return pos;
                        }
                    }
                    return none();
                }));

            define("tomllib._parser", "create_dict_rule", w.function("create_dict_rule", "create_dict_rule", { py_positional("src"), py_positional("pos"), py_positional("out") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py out = a[2];
                    py key;
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                    auto const items22 = unpack((G<"tomllib._parser.parse_key">())(src, pos), 2);
                    pos = items22[0];
                    key = items22[1];
                    if ((out.attr("flags").method("is_", { key, (G<"tomllib._parser.Flags">()).attr("EXPLICIT_NEST") }).truthy() || out.attr("flags").method("is_", { key, (G<"tomllib._parser.Flags">()).attr("FROZEN") }).truthy()))
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Cannot declare " } + key.str() + " twice")));
                    }
                    out.attr("flags").method("set", { key, (G<"tomllib._parser.Flags">()).attr("EXPLICIT_NEST") }, { { "recursive", boolean(false) } });
                    try
                    {
                        out.attr("data").method("get_or_create_nest", { key });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler23;
                        if (matches(handler23.exception, B<"KeyError">()))
                        {
                            fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Cannot overwrite a value">()).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler23.exception);
                    }
                    if (!src.method("startswith", { S<"]">(), pos }).truthy())
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Expected ']' at the end of a table declaration">()));
                    }
                    return py_tuple({ pos + (I<1>()), key });
                }));

            define("tomllib._parser", "create_list_rule", w.function("create_list_rule", "create_list_rule", { py_positional("src"), py_positional("pos"), py_positional("out") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py out = a[2];
                    py key;
                    pos = inplace(NEOS_PY_ADD, pos, I<2>());
                    pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                    auto const items24 = unpack((G<"tomllib._parser.parse_key">())(src, pos), 2);
                    pos = items24[0];
                    key = items24[1];
                    if (out.attr("flags").method("is_", { key, (G<"tomllib._parser.Flags">()).attr("FROZEN") }).truthy())
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Cannot mutate immutable namespace " } + key.str())));
                    }
                    // Free the namespace now that it points to another empty list item...
                    out.attr("flags").method("unset_all", { key });
                    // ...but this key precisely is still prohibited from table declaration
                    out.attr("flags").method("set", { key, (G<"tomllib._parser.Flags">()).attr("EXPLICIT_NEST") }, { { "recursive", boolean(false) } });
                    try
                    {
                        out.attr("data").method("append_nest_to_list", { key });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler25;
                        if (matches(handler25.exception, B<"KeyError">()))
                        {
                            fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Cannot overwrite a value">()).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler25.exception);
                    }
                    if (!src.method("startswith", { S<"]]">(), pos }).truthy())
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Expected ']]' at the end of an array declaration">()));
                    }
                    return py_tuple({ pos + (I<2>()), key });
                }));

            define("tomllib._parser", "key_value_rule", w.function("key_value_rule", "key_value_rule", { py_positional("src"), py_positional("pos"), py_positional("out"), py_positional("header"), py_positional("parse_float") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py out = a[2];
                    py header = a[3];
                    py parse_float = a[4];
                    py abs_key_parent;
                    py cont_key;
                    py key;
                    py key_parent;
                    py key_stem;
                    py nest;
                    py relative_path_cont_keys;
                    py value;
                    auto const items26 = unpack((G<"tomllib._parser.parse_key_value_pair">())(src, pos, parse_float), 3);
                    pos = items26[0];
                    key = items26[1];
                    value = items26[2];
                    auto const items27 = unpack(py_tuple({ key[B<"slice">()(none(), I<-1>(), none())], key[I<-1>()] }), 2);
                    key_parent = items27[0];
                    key_stem = items27[1];
                    abs_key_parent = header + key_parent;
                    relative_path_cont_keys = [&]() -> py { py const comprehension28 = py_list(); py i; for (py const& item : iterate{ (B<"range">())(I<1>(), py{ key.size() }) }) { i = item; comprehension28.method("append", { header + key[B<"slice">()(none(), i, none())] }); } return comprehension28; }();
                    for (py const& item : iterate{ relative_path_cont_keys })
                    {
                        cont_key = item;
                        // Check that dotted key syntax does not redefine an existing table
                        if (out.attr("flags").method("is_", { cont_key, (G<"tomllib._parser.Flags">()).attr("EXPLICIT_NEST") }).truthy())
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Cannot redefine namespace " } + cont_key.str())));
                        }
                        // Containers in the relative path can't be opened with the table syntax or
                        // dotted key/value syntax in following table sections.
                        out.attr("flags").method("add_pending", { cont_key, (G<"tomllib._parser.Flags">()).attr("EXPLICIT_NEST") });
                    }
                    if (out.attr("flags").method("is_", { abs_key_parent, (G<"tomllib._parser.Flags">()).attr("FROZEN") }).truthy())
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Cannot mutate immutable namespace " } + abs_key_parent.str())));
                    }
                    try
                    {
                        nest = out.attr("data").method("get_or_create_nest", { abs_key_parent });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler30;
                        if (matches(handler30.exception, B<"KeyError">()))
                        {
                            fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Cannot overwrite a value">()).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler30.exception);
                    }
                    if (nest.contains(key_stem))
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Cannot overwrite a value">()));
                    }
                    // Mark inline table and array namespaces recursively immutable
                    if (isinstance(value, py_tuple({ B<"dict">(), B<"list">() })))
                    {
                        out.attr("flags").method("set", { header + key, (G<"tomllib._parser.Flags">()).attr("FROZEN") }, { { "recursive", boolean(true) } });
                    }
                    nest.set_item(key_stem, value);
                    return pos;
                }));

            define("tomllib._parser", "parse_key_value_pair", w.function("parse_key_value_pair", "parse_key_value_pair", { py_positional("src"), py_positional("pos"), py_positional("parse_float") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py parse_float = a[2];
                    py char_;
                    py key;
                    py value;
                    auto const items31 = unpack((G<"tomllib._parser.parse_key">())(src, pos), 2);
                    pos = items31[0];
                    key = items31[1];
                    try
                    {
                        char_ = src[pos];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler32;
                        if (matches(handler32.exception, B<"IndexError">()))
                        {
                            char_ = none();
                        }
                        else
                            py_reraise(handler32.exception);
                    }
                    if (char_ != (S<"=">()))
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Expected '=' after a key in a key/value pair">()));
                    }
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                    auto const items33 = unpack((G<"tomllib._parser.parse_value">())(src, pos, parse_float), 2);
                    pos = items33[0];
                    value = items33[1];
                    return py_tuple({ pos, key, value });
                }));

            define("tomllib._parser", "parse_key", w.function("parse_key", "parse_key", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py char_;
                    py key;
                    py key_part;
                    auto const items34 = unpack((G<"tomllib._parser.parse_key_part">())(src, pos), 2);
                    pos = items34[0];
                    key_part = items34[1];
                    key = py_tuple({ key_part });
                    pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                    while (boolean(true).truthy())
                    {
                        try
                        {
                            char_ = src[pos];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler36;
                            if (matches(handler36.exception, B<"IndexError">()))
                            {
                                char_ = none();
                            }
                            else
                                py_reraise(handler36.exception);
                        }
                        if (char_ != (S<".">()))
                        {
                            return py_tuple({ pos, key });
                        }
                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                        auto const items37 = unpack((G<"tomllib._parser.parse_key_part">())(src, pos), 2);
                        pos = items37[0];
                        key_part = items37[1];
                        key = inplace(NEOS_PY_ADD, key, py_tuple({ key_part }));
                        if (py{ key.size() } > (G<"tomllib._parser.MAX_KEY_PARTS">()))
                        {
                            py_raise((B<"RecursionError">())(S(std::string{ "TOML key has more than the allowed " } + (G<"tomllib._parser.MAX_KEY_PARTS">()).str() + " parts")));
                        }
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                    }
                    return none();
                }));

            define("tomllib._parser", "parse_key_part", w.function("parse_key_part", "parse_key_part", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py char_;
                    py start_pos;
                    try
                    {
                        char_ = src[pos];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler38;
                        if (matches(handler38.exception, B<"IndexError">()))
                        {
                            char_ = none();
                        }
                        else
                            py_reraise(handler38.exception);
                    }
                    if ((G<"tomllib._parser.BARE_KEY_CHARS">()).contains(char_))
                    {
                        start_pos = pos;
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.BARE_KEY_CHARS">());
                        return py_tuple({ pos, src[B<"slice">()(start_pos, pos, none())] });
                    }
                    if (char_ == (S<"'">()))
                    {
                        return (G<"tomllib._parser.parse_literal_str">())(src, pos);
                    }
                    if (char_ == (S<"\"">()))
                    {
                        return (G<"tomllib._parser.parse_one_line_basic_str">())(src, pos);
                    }
                    py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Invalid initial character for a key part">()));
                }));

            define("tomllib._parser", "parse_one_line_basic_str", w.function("parse_one_line_basic_str", "parse_one_line_basic_str", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    return (G<"tomllib._parser.parse_basic_str">()).call({ src, pos }, { { "multiline", boolean(false) } });
                }));

            define("tomllib._parser", "parse_array", w.function("parse_array", "parse_array", { py_positional("src"), py_positional("pos"), py_positional("parse_float") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py parse_float = a[2];
                    py array;
                    py c;
                    py val;
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    array = py_list();
                    pos = (G<"tomllib._parser.skip_comments_and_array_ws">())(src, pos);
                    if (src.method("startswith", { S<"]">(), pos }).truthy())
                    {
                        return py_tuple({ pos + (I<1>()), array });
                    }
                    while (boolean(true).truthy())
                    {
                        auto const items40 = unpack((G<"tomllib._parser.parse_value">())(src, pos, parse_float), 2);
                        pos = items40[0];
                        val = items40[1];
                        array.method("append", { val });
                        pos = (G<"tomllib._parser.skip_comments_and_array_ws">())(src, pos);
                        c = src[B<"slice">()(pos, pos + (I<1>()), none())];
                        if (c == (S<"]">()))
                        {
                            return py_tuple({ pos + (I<1>()), array });
                        }
                        if (c != (S<",">()))
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Unclosed array">()));
                        }
                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                        pos = (G<"tomllib._parser.skip_comments_and_array_ws">())(src, pos);
                        if (src.method("startswith", { S<"]">(), pos }).truthy())
                        {
                            return py_tuple({ pos + (I<1>()), array });
                        }
                    }
                    return none();
                }));

            define("tomllib._parser", "parse_inline_table", w.function("parse_inline_table", "parse_inline_table", { py_positional("src"), py_positional("pos"), py_positional("parse_float") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py parse_float = a[2];
                    py c;
                    py flags;
                    py key;
                    py key_parent;
                    py key_stem;
                    py nest;
                    py nested_dict;
                    py value;
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    nested_dict = (G<"tomllib._parser.NestedDict">())();
                    flags = (G<"tomllib._parser.Flags">())();
                    pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                    if (src.method("startswith", { S<"}">(), pos }).truthy())
                    {
                        return py_tuple({ pos + (I<1>()), nested_dict.attr("dict") });
                    }
                    while (boolean(true).truthy())
                    {
                        auto const items42 = unpack((G<"tomllib._parser.parse_key_value_pair">())(src, pos, parse_float), 3);
                        pos = items42[0];
                        key = items42[1];
                        value = items42[2];
                        auto const items43 = unpack(py_tuple({ key[B<"slice">()(none(), I<-1>(), none())], key[I<-1>()] }), 2);
                        key_parent = items43[0];
                        key_stem = items43[1];
                        if (flags.method("is_", { key, (G<"tomllib._parser.Flags">()).attr("FROZEN") }).truthy())
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Cannot mutate immutable namespace " } + key.str())));
                        }
                        try
                        {
                            nest = nested_dict.method("get_or_create_nest", { key_parent }, { { "access_lists", boolean(false) } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler44;
                            if (matches(handler44.exception, B<"KeyError">()))
                            {
                                fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Cannot overwrite a value">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler44.exception);
                        }
                        if (nest.contains(key_stem))
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Duplicate inline table key " } + key_stem.repr())));
                        }
                        nest.set_item(key_stem, value);
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                        c = src[B<"slice">()(pos, pos + (I<1>()), none())];
                        if (c == (S<"}">()))
                        {
                            return py_tuple({ pos + (I<1>()), nested_dict.attr("dict") });
                        }
                        if (c != (S<",">()))
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Unclosed inline table">()));
                        }
                        if (isinstance(value, py_tuple({ B<"dict">(), B<"list">() })))
                        {
                            flags.method("set", { key, (G<"tomllib._parser.Flags">()).attr("FROZEN") }, { { "recursive", boolean(true) } });
                        }
                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                    }
                    return none();
                }));

            define("tomllib._parser", "parse_basic_str_escape", w.function("parse_basic_str_escape", "parse_basic_str_escape", { py_positional("src"), py_positional("pos"), py_keyword("multiline", boolean(false)) }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py multiline = a[2];
                    py char_;
                    py escape_id;
                    escape_id = src[B<"slice">()(pos, pos + (I<2>()), none())];
                    pos = inplace(NEOS_PY_ADD, pos, I<2>());
                    if ((multiline.truthy() && set_of({ S<"\\ ">(), S<"\\\t">(), S<"\\\n">() }).contains(escape_id)))
                    {
                        // Skip whitespace until next non-whitespace character or end of
                        // the doc. Error if non-whitespace is found before newline.
                        if (escape_id != (S<"\\\n">()))
                        {
                            pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS">());
                            try
                            {
                                char_ = src[pos];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler45;
                                if (matches(handler45.exception, B<"IndexError">()))
                                {
                                    return py_tuple({ pos, S<"">() });
                                }
                                else
                                    py_reraise(handler45.exception);
                            }
                            if (char_ != (S<"\n">()))
                            {
                                py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Unescaped '\\' in a string">()));
                            }
                            pos = inplace(NEOS_PY_ADD, pos, I<1>());
                        }
                        pos = (G<"tomllib._parser.skip_chars">())(src, pos, G<"tomllib._parser.TOML_WS_AND_NEWLINE">());
                        return py_tuple({ pos, S<"">() });
                    }
                    if (escape_id == (S<"\\u">()))
                    {
                        return (G<"tomllib._parser.parse_hex_char">())(src, pos, I<4>());
                    }
                    if (escape_id == (S<"\\U">()))
                    {
                        return (G<"tomllib._parser.parse_hex_char">())(src, pos, I<8>());
                    }
                    try
                    {
                        return py_tuple({ pos, (G<"tomllib._parser.BASIC_STR_ESCAPE_REPLACEMENTS">())[escape_id] });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler46;
                        if (matches(handler46.exception, B<"KeyError">()))
                        {
                            fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Unescaped '\\' in a string">()).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler46.exception);
                    }
                    return none();
                }));

            define("tomllib._parser", "parse_basic_str_escape_multiline", w.function("parse_basic_str_escape_multiline", "parse_basic_str_escape_multiline", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    return (G<"tomllib._parser.parse_basic_str_escape">()).call({ src, pos }, { { "multiline", boolean(true) } });
                }));

            define("tomllib._parser", "parse_hex_char", w.function("parse_hex_char", "parse_hex_char", { py_positional("src"), py_positional("pos"), py_positional("hex_len") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py hex_len = a[2];
                    py hex_int;
                    py hex_str;
                    hex_str = src[B<"slice">()(pos, pos + hex_len, none())];
                    if (((py{ hex_str.size() } != hex_len) || (!(G<"tomllib._parser.HEXDIGIT_CHARS">()).method("issuperset", { hex_str }).truthy())))
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Invalid hex value">()));
                    }
                    pos = inplace(NEOS_PY_ADD, pos, hex_len);
                    hex_int = (B<"int">())(hex_str, I<16>());
                    if (!(G<"tomllib._parser.is_unicode_scalar_value">())(hex_int).truthy())
                    {
                        py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Escaped character is not a Unicode scalar value">()));
                    }
                    return py_tuple({ pos, (B<"chr">())(hex_int) });
                }));

            define("tomllib._parser", "parse_literal_str", w.function("parse_literal_str", "parse_literal_str", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py start_pos;
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    start_pos = pos;
                    pos = (G<"tomllib._parser.skip_until">()).call({ src, pos, S<"'">() }, { { "error_on", G<"tomllib._parser.ILLEGAL_LITERAL_STR_CHARS">() }, { "error_on_eof", boolean(true) } });
                    return py_tuple({ pos + (I<1>()), src[B<"slice">()(start_pos, pos, none())] });
                }));

            define("tomllib._parser", "parse_multiline_str", w.function("parse_multiline_str", "parse_multiline_str", { py_positional("src"), py_positional("pos"), py_keyword("literal") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py literal = a[2];
                    py delim;
                    py end_pos;
                    py result;
                    pos = inplace(NEOS_PY_ADD, pos, I<3>());
                    if (src.method("startswith", { S<"\n">(), pos }).truthy())
                    {
                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    }
                    if (literal.truthy())
                    {
                        delim = S<"'">();
                        end_pos = (G<"tomllib._parser.skip_until">()).call({ src, pos, S<"'''">() }, { { "error_on", G<"tomllib._parser.ILLEGAL_MULTILINE_LITERAL_STR_CHARS">() }, { "error_on_eof", boolean(true) } });
                        result = src[B<"slice">()(pos, end_pos, none())];
                        pos = end_pos + (I<3>());
                    }
                    else
                    {
                        delim = S<"\"">();
                        auto const items47 = unpack((G<"tomllib._parser.parse_basic_str">()).call({ src, pos }, { { "multiline", boolean(true) } }), 2);
                        pos = items47[0];
                        result = items47[1];
                    }
                    // Add at maximum two extra apostrophes/quotes if the end sequence
                    // is 4 or 5 chars long instead of just 3.
                    if (!src.method("startswith", { delim, pos }).truthy())
                    {
                        return py_tuple({ pos, result });
                    }
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    if (!src.method("startswith", { delim, pos }).truthy())
                    {
                        return py_tuple({ pos, result + delim });
                    }
                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    return py_tuple({ pos, result + (delim * (I<2>())) });
                }));

            define("tomllib._parser", "parse_basic_str", w.function("parse_basic_str", "parse_basic_str", { py_positional("src"), py_positional("pos"), py_keyword("multiline") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py multiline = a[2];
                    py char_;
                    py error_on;
                    py parse_escapes;
                    py parsed_escape;
                    py result;
                    py start_pos;
                    if (multiline.truthy())
                    {
                        error_on = G<"tomllib._parser.ILLEGAL_MULTILINE_BASIC_STR_CHARS">();
                        parse_escapes = G<"tomllib._parser.parse_basic_str_escape_multiline">();
                    }
                    else
                    {
                        error_on = G<"tomllib._parser.ILLEGAL_BASIC_STR_CHARS">();
                        parse_escapes = G<"tomllib._parser.parse_basic_str_escape">();
                    }
                    result = S<"">();
                    start_pos = pos;
                    while (boolean(true).truthy())
                    {
                        try
                        {
                            char_ = src[pos];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler49;
                            if (matches(handler49.exception, B<"IndexError">()))
                            {
                                fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Unterminated string">()).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler49.exception);
                        }
                        if (char_ == (S<"\"">()))
                        {
                            if (!multiline.truthy())
                            {
                                return py_tuple({ pos + (I<1>()), result + src[B<"slice">()(start_pos, pos, none())] });
                            }
                            if (src.method("startswith", { S<"\"\"\"">(), pos }).truthy())
                            {
                                return py_tuple({ pos + (I<3>()), result + src[B<"slice">()(start_pos, pos, none())] });
                            }
                            pos = inplace(NEOS_PY_ADD, pos, I<1>());
                            continue;
                        }
                        if (char_ == (S<"\\">()))
                        {
                            result = inplace(NEOS_PY_ADD, result, src[B<"slice">()(start_pos, pos, none())]);
                            auto const items50 = unpack(parse_escapes(src, pos), 2);
                            pos = items50[0];
                            parsed_escape = items50[1];
                            result = inplace(NEOS_PY_ADD, result, parsed_escape);
                            start_pos = pos;
                            continue;
                        }
                        if (error_on.contains(char_))
                        {
                            py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S(std::string{ "Illegal character " } + char_.repr())));
                        }
                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                    }
                    return none();
                }));

            define("tomllib._parser", "parse_value", w.function("parse_value", "parse_value", { py_positional("src"), py_positional("pos"), py_positional("parse_float") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py parse_float = a[2];
                    py char_;
                    py datetime_match;
                    py datetime_obj;
                    py e;
                    py first_four;
                    py first_three;
                    py localtime_match;
                    py number_match;
                    try
                    {
                        char_ = src[pos];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler51;
                        if (matches(handler51.exception, B<"IndexError">()))
                        {
                            char_ = none();
                        }
                        else
                            py_reraise(handler51.exception);
                    }
                    // IMPORTANT: order conditions based on speed of checking and likelihood
                    // Basic strings
                    if (char_ == (S<"\"">()))
                    {
                        if (src.method("startswith", { S<"\"\"\"">(), pos }).truthy())
                        {
                            return (G<"tomllib._parser.parse_multiline_str">()).call({ src, pos }, { { "literal", boolean(false) } });
                        }
                        return (G<"tomllib._parser.parse_one_line_basic_str">())(src, pos);
                    }
                    // Literal strings
                    if (char_ == (S<"'">()))
                    {
                        if (src.method("startswith", { S<"'''">(), pos }).truthy())
                        {
                            return (G<"tomllib._parser.parse_multiline_str">()).call({ src, pos }, { { "literal", boolean(true) } });
                        }
                        return (G<"tomllib._parser.parse_literal_str">())(src, pos);
                    }
                    // Booleans
                    if (char_ == (S<"t">()))
                    {
                        if (src.method("startswith", { S<"true">(), pos }).truthy())
                        {
                            return py_tuple({ pos + (I<4>()), boolean(true) });
                        }
                    }
                    if (char_ == (S<"f">()))
                    {
                        if (src.method("startswith", { S<"false">(), pos }).truthy())
                        {
                            return py_tuple({ pos + (I<5>()), boolean(false) });
                        }
                    }
                    // Arrays
                    if (char_ == (S<"[">()))
                    {
                        return (G<"tomllib._parser.parse_array">())(src, pos, parse_float);
                    }
                    // Inline tables
                    if (char_ == (S<"{">()))
                    {
                        return (G<"tomllib._parser.parse_inline_table">())(src, pos, parse_float);
                    }
                    // Dates and times
                    datetime_match = (Mod<"tomllib._re">().attr("RE_DATETIME")).method("match", { src, pos });
                    if (datetime_match.truthy())
                    {
                        try
                        {
                            datetime_obj = (Mod<"tomllib._re">().attr("match_to_datetime"))(datetime_match);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler52;
                            if (matches(handler52.exception, B<"ValueError">()))
                            {
                                e = handler52.exception;
                                fn_raise_from((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Invalid date or datetime">()).get(), e.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler52.exception);
                        }
                        return py_tuple({ datetime_match.method("end"), datetime_obj });
                    }
                    localtime_match = (Mod<"tomllib._re">().attr("RE_LOCALTIME")).method("match", { src, pos });
                    if (localtime_match.truthy())
                    {
                        return py_tuple({ localtime_match.method("end"), (Mod<"tomllib._re">().attr("match_to_localtime"))(localtime_match) });
                    }
                    // Integers and "normal" floats.
                    // The regex will greedily match any type starting with a decimal
                    // char, so needs to be located after handling of dates and times.
                    number_match = (Mod<"tomllib._re">().attr("RE_NUMBER")).method("match", { src, pos });
                    if (number_match.truthy())
                    {
                        return py_tuple({ number_match.method("end"), (Mod<"tomllib._re">().attr("match_to_number"))(number_match, parse_float) });
                    }
                    // Special floats
                    first_three = src[B<"slice">()(pos, pos + (I<3>()), none())];
                    if (set_of({ S<"inf">(), S<"nan">() }).contains(first_three))
                    {
                        return py_tuple({ pos + (I<3>()), parse_float(first_three) });
                    }
                    first_four = src[B<"slice">()(pos, pos + (I<4>()), none())];
                    if (set_of({ S<"-inf">(), S<"+inf">(), S<"-nan">(), S<"+nan">() }).contains(first_four))
                    {
                        return py_tuple({ pos + (I<4>()), parse_float(first_four) });
                    }
                    py_raise((G<"tomllib._parser.suffixed_err">())(src, pos, S<"Invalid value">()));
                }));

            define("tomllib._parser", "suffixed_err", w.function("suffixed_err", "suffixed_err", { py_positional("src"), py_positional("pos"), py_positional("msg") }, [](arguments a) -> py
                {
                    py src = a[0];
                    py pos = a[1];
                    py msg = a[2];
                    py coord_repr;
                    coord_repr = w.function("coord_repr", "suffixed_err.<locals>.coord_repr", { py_positional("src"), py_positional("pos") }, [](arguments a) -> py
    {
        py src = a[0];
        py pos = a[1];
        py column;
        py line;
        if (pos >= py{ src.size() })
        {
            return S<"end of document">();
        }
        line = src.method("count", { S<"\n">(), I<0>(), pos }) + (I<1>());
        if (line == (I<1>()))
        {
            column = pos + (I<1>());
        }
        else
        {
            column = pos - src.method("rindex", { S<"\n">(), I<0>(), pos });
        }
        return S(std::string{ "line " } + line.str() + ", column " + column.str());
    });
                    return (G<"tomllib._parser.TOMLDecodeError">())(S(msg.str() + " (at " + coord_repr(src, pos).str() + ")"));
                }));

            define("tomllib._parser", "is_unicode_scalar_value", w.function("is_unicode_scalar_value", "is_unicode_scalar_value", { py_positional("codepoint") }, [](arguments a) -> py
                {
                    py codepoint = a[0];
                    return [&]() -> py { py const t = boolean((((I<0>()) <= codepoint) && (codepoint <= (I<55295>())))); return t.truthy() ? t : boolean((((I<57344>()) <= codepoint) && (codepoint <= (I<1114111>())))); }();
                }));

            define("tomllib._parser", "make_safe_parse_float", w.function("make_safe_parse_float", "make_safe_parse_float", { py_positional("parse_float") }, [](arguments a) -> py
                {
                    auto const parse_float = std::make_shared<py>(a[0]);
                    py safe_parse_float;
                    // The default `float` callable never returns illegal types. Optimize it.
                    if ((*parse_float).is((B<"float">())))
                    {
                        return B<"float">();
                    }
                    safe_parse_float = w.function("safe_parse_float", "make_safe_parse_float.<locals>.safe_parse_float", { py_positional("float_str") }, [parse_float](arguments a) -> py
    {
        py float_str = a[0];
        py float_value;
        float_value = (*parse_float)(float_str);
        if (isinstance(float_value, py_tuple({ B<"dict">(), B<"list">() })))
        {
            py_raise((B<"ValueError">())(S<"parse_float must not return dicts or lists">()));
        }
        return float_value;
    });
                    return safe_parse_float;
                }));
        }

        bool tomllib__parser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_tomllib__parser();
                }
                if (auto const doc = tomllib_parser_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("Iterable", import_attribute("collections.abc", "Iterable").release());
                aAttributes.emplace_back("string", py{ fn_import("string") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("MappingProxyType", import_attribute("types", "MappingProxyType").release());
                aAttributes.emplace_back("Any", import_attribute("typing", "Any").release());
                aAttributes.emplace_back("BinaryIO", import_attribute("typing", "BinaryIO").release());
                aAttributes.emplace_back("NamedTuple", import_attribute("typing", "NamedTuple").release());
                aAttributes.emplace_back("Final", import_attribute("typing", "Final").release());
                aAttributes.emplace_back("RE_DATETIME", import_attribute("tomllib._re", "RE_DATETIME").release());
                aAttributes.emplace_back("RE_LOCALTIME", import_attribute("tomllib._re", "RE_LOCALTIME").release());
                aAttributes.emplace_back("RE_NUMBER", import_attribute("tomllib._re", "RE_NUMBER").release());
                aAttributes.emplace_back("match_to_datetime", import_attribute("tomllib._re", "match_to_datetime").release());
                aAttributes.emplace_back("match_to_localtime", import_attribute("tomllib._re", "match_to_localtime").release());
                aAttributes.emplace_back("match_to_number", import_attribute("tomllib._re", "match_to_number").release());
                aAttributes.emplace_back("Key", import_attribute("tomllib._types", "Key").release());
                aAttributes.emplace_back("ParseFloat", import_attribute("tomllib._types", "ParseFloat").release());
                aAttributes.emplace_back("Pos", import_attribute("tomllib._types", "Pos").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("tomllib._parser", tomllib__parser_module);
    }
}
