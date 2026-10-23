/*
  tokenize.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/tokenize.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module tokenize (CPython 3.13's Lib/tokenize.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "tokenize_docs.hpp"

namespace neos::python_runtime::detail::tokenize_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "tokenize.name"))
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
                py_raise("SystemError", std::string{ "tokenize: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"tokenize.name">()))
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
            if (modules_imported("tokenize"))
                return py{ fn_import("tokenize") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "tokenize" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "tokenize", tokenize_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- tokenize (Lib/tokenize.py)

        void make_tokenize()
        {
            static module_writer const& w = writer("tokenize");
            define("tokenize", "__author__", S<"Ka-Ping Yee <ping@lfw.org>">());
            define("tokenize", "__credits__", S<"GvR, ESR, Tim Peters, Thomas Wouters, Fred Drake, Skip Montanaro, Raymond Hettinger, Trent Nelson, Michael Foord">());
            define("tokenize", "cookie_re", (Mod<"re">()).method("compile", { S<"^[ \\t\\f]*#.*?coding[:=][ \\t]*([-\\w.]+)">(), Mod<"re">().attr("ASCII") }));
            define("tokenize", "blank_re", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5e\x5b\x20\x5c\x74\x5c\x66\x5d\x2a\x28\x3f\x3a\x5b\x23\x5c\x72\x5c\x6e\x5d\x7c\x24\x29", 22 }) }, Mod<"re">().attr("ASCII") }));
            define("tokenize", "__all__", (Mod<"token">().attr("__all__")) + py_list({ S<"tokenize">(), S<"generate_tokens">(), S<"detect_encoding">(), S<"untokenize">(), S<"TokenInfo">(), S<"open">(), S<"TokenError">() }));
            undefine("tokenize", "token");
            define("tokenize", "TokenInfo", w.make_class("TokenInfo", { (Mod<"collections">()).method("namedtuple", { S<"TokenInfo">(), S<"type string start end line">() }) }, {
                { "__repr__", w.method("TokenInfo", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py annotated_type;
                        annotated_type = (S<"%d (%s)">()) % py_tuple({ self.attr("type"), (Mod<"token">().attr("tok_name"))[self.attr("type")] });
                        return (S<"TokenInfo(type=%s, string=%r, start=%r, end=%r, line=%r)">()) % self.method("_replace", {  }, { { "type", annotated_type } });
                    }) },
                { "exact_type", B<"property">()(w.method("TokenInfo", "exact_type", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((self.attr("type") == (Mod<"token">().attr("OP"))) && (Mod<"token">().attr("EXACT_TOKEN_TYPES")).contains(self.attr("string"))))
                        {
                            return (Mod<"token">().attr("EXACT_TOKEN_TYPES"))[self.attr("string")];
                        }
                        else
                        {
                            return self.attr("type");
                        }
                        return none();
                    })) }, }));

            define("tokenize", "group", w.function("group", "group", {  }, [](arguments a) -> py
                {
                    py choices = a[0];
                    return ((S<"(">()) + (S<"|">()).method("join", { choices })) + (S<")">());
                }, true));

            define("tokenize", "any", w.function("any", "any", {  }, [](arguments a) -> py
                {
                    py choices = a[0];
                    return call_ex((G<"tokenize.group">()), {  }, choices, py{}) + (S<"*">());
                }, true));

            define("tokenize", "maybe", w.function("maybe", "maybe", {  }, [](arguments a) -> py
                {
                    py choices = a[0];
                    return call_ex((G<"tokenize.group">()), {  }, choices, py{}) + (S<"?">());
                }, true));

            // Note: we use unicode matching for names ("\w") but ascii matching for
            // number literals.
            define("tokenize", "Whitespace", S<"[ \\f\\t]*">());
            define("tokenize", "Comment", S<"#[^\\r\\n]*">());
            define("tokenize", "Ignore", ((G<"tokenize.Whitespace">()) + (G<"tokenize.any">())((S<"\\\\\\r?\\n">()) + (G<"tokenize.Whitespace">()))) + (G<"tokenize.maybe">())(G<"tokenize.Comment">()));
            define("tokenize", "Name", S<"\\w+">());
            define("tokenize", "Hexnumber", S<"0[xX](?:_?[0-9a-fA-F])+">());
            define("tokenize", "Binnumber", S<"0[bB](?:_?[01])+">());
            define("tokenize", "Octnumber", S<"0[oO](?:_?[0-7])+">());
            define("tokenize", "Decnumber", S<"(?:0(?:_?0)*|[1-9](?:_?[0-9])*)">());
            define("tokenize", "Intnumber", (G<"tokenize.group">())(G<"tokenize.Hexnumber">(), G<"tokenize.Binnumber">(), G<"tokenize.Octnumber">(), G<"tokenize.Decnumber">()));
            define("tokenize", "Exponent", S<"[eE][-+]?[0-9](?:_?[0-9])*">());
            define("tokenize", "Pointfloat", (G<"tokenize.group">())(S<"[0-9](?:_?[0-9])*\\.(?:[0-9](?:_?[0-9])*)?">(), S<"\\.[0-9](?:_?[0-9])*">()) + (G<"tokenize.maybe">())(G<"tokenize.Exponent">()));
            define("tokenize", "Expfloat", (S<"[0-9](?:_?[0-9])*">()) + (G<"tokenize.Exponent">()));
            define("tokenize", "Floatnumber", (G<"tokenize.group">())(G<"tokenize.Pointfloat">(), G<"tokenize.Expfloat">()));
            define("tokenize", "Imagnumber", (G<"tokenize.group">())(S<"[0-9](?:_?[0-9])*[jJ]">(), (G<"tokenize.Floatnumber">()) + (S<"[jJ]">())));
            define("tokenize", "Number", (G<"tokenize.group">())(G<"tokenize.Imagnumber">(), G<"tokenize.Floatnumber">(), G<"tokenize.Intnumber">()));
            // Return the empty string, plus all of the valid string prefixes.
            define("tokenize", "_all_string_prefixes", w.function("_all_string_prefixes", "_all_string_prefixes", {  }, [](arguments a) -> py
                {
                    py _valid_string_prefixes;
                    py prefix;
                    py result;
                    py t;
                    py u;
                    _valid_string_prefixes = py_list({ S<"b">(), S<"r">(), S<"u">(), S<"f">(), S<"br">(), S<"fr">() });
                    // if we add binary f-strings, add: ['fb', 'fbr']
                    result = set_of({ S<"">() });
                    for (py const& item : iterate{ _valid_string_prefixes })
                    {
                        prefix = item;
                        for (py const& item : iterate{ (Mod<"itertools">().attr("permutations"))(prefix) })
                        {
                            t = item;
                            // create a list with upper and lower versions of each
                            // character
                            for (py const& item : iterate{ call_ex((Mod<"itertools">().attr("product")), {  }, [&]() -> py { py const comprehension3 = py_list(); py c; for (py const& item : iterate{ t }) { c = item; comprehension3.method("append", { py_tuple({ c, c.method("upper") }) }); } return comprehension3; }(), py{}) })
                            {
                                u = item;
                                result.method("add", { (S<"">()).method("join", { u }) });
                            }
                        }
                    }
                    return result;
                }));

            define("tokenize", "_compile", (Mod<"functools">().attr("lru_cache"))(w.function("_compile", "_compile", { py_positional("expr") }, [](arguments a) -> py
                {
                    py expr = a[0];
                    return (Mod<"re">().attr("compile"))(expr, Mod<"re">().attr("UNICODE"));
                })));

            // Note that since _all_string_prefixes includes the empty string,
            // StringPrefix can be the empty string (making it optional).
            define("tokenize", "StringPrefix", call_ex((G<"tokenize.group">()), {  }, (G<"tokenize._all_string_prefixes">())(), py{}));
            // Tail end of ' string.
            define("tokenize", "Single", S<"[^'\\\\]*(?:\\\\.[^'\\\\]*)*'">());
            // Tail end of " string.
            define("tokenize", "Double", S<"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"">());
            // Tail end of ''' string.
            define("tokenize", "Single3", S<"[^'\\\\]*(?:(?:\\\\.|'(?!''))[^'\\\\]*)*'''">());
            // Tail end of """ string.
            define("tokenize", "Double3", S<"[^\"\\\\]*(?:(?:\\\\.|\"(?!\"\"))[^\"\\\\]*)*\"\"\"">());
            define("tokenize", "Triple", (G<"tokenize.group">())((G<"tokenize.StringPrefix">()) + (S<"'''">()), (G<"tokenize.StringPrefix">()) + (S<"\"\"\"">())));
            // Single-line ' or " string.
            define("tokenize", "String", (G<"tokenize.group">())((G<"tokenize.StringPrefix">()) + (S<"'[^\\n'\\\\]*(?:\\\\.[^\\n'\\\\]*)*'">()), (G<"tokenize.StringPrefix">()) + (S<"\"[^\\n\"\\\\]*(?:\\\\.[^\\n\"\\\\]*)*\"">())));
            // Sorting in reverse order puts the long operators before their prefixes.
            // Otherwise if = came before ==, == would get recognized as two instances
            // of =.
            define("tokenize", "Special", call_ex((G<"tokenize.group">()), {  }, (B<"map">())(Mod<"re">().attr("escape"), (B<"sorted">()).call({ Mod<"token">().attr("EXACT_TOKEN_TYPES") }, { { "reverse", boolean(true) } })), py{}));
            define("tokenize", "Funny", (G<"tokenize.group">())(S<"\\r?\\n">(), G<"tokenize.Special">()));
            define("tokenize", "PlainToken", (G<"tokenize.group">())(G<"tokenize.Number">(), G<"tokenize.Funny">(), G<"tokenize.String">(), G<"tokenize.Name">()));
            define("tokenize", "Token", (G<"tokenize.Ignore">()) + (G<"tokenize.PlainToken">()));
            // First (or only) line of ' or " string.
            define("tokenize", "ContStr", (G<"tokenize.group">())(((G<"tokenize.StringPrefix">()) + (S<"'[^\\n'\\\\]*(?:\\\\.[^\\n'\\\\]*)*">())) + (G<"tokenize.group">())(S<"'">(), S<"\\\\\\r?\\n">()), ((G<"tokenize.StringPrefix">()) + (S<"\"[^\\n\"\\\\]*(?:\\\\.[^\\n\"\\\\]*)*">())) + (G<"tokenize.group">())(S<"\"">(), S<"\\\\\\r?\\n">())));
            define("tokenize", "PseudoExtras", (G<"tokenize.group">())(S<"\\\\\\r?\\n|\\Z">(), G<"tokenize.Comment">(), G<"tokenize.Triple">()));
            define("tokenize", "PseudoToken", (G<"tokenize.Whitespace">()) + (G<"tokenize.group">())(G<"tokenize.PseudoExtras">(), G<"tokenize.Number">(), G<"tokenize.Funny">(), G<"tokenize.ContStr">(), G<"tokenize.Name">()));
            // For a given string prefix plus quotes, endpats maps it to a regex
            // to match the remainder of that string. _prefix can be empty, for
            // a normal single or triple quoted string (with no prefix).
            define("tokenize", "endpats", py_dict());
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ (G<"tokenize._all_string_prefixes">())() })
                    {
                        set_global("tokenize", "_prefix", item);
                        (G<"tokenize.endpats">()).set_item((G<"tokenize._prefix">()) + (S<"'">()), G<"tokenize.Single">());
                        (G<"tokenize.endpats">()).set_item((G<"tokenize._prefix">()) + (S<"\"">()), G<"tokenize.Double">());
                        (G<"tokenize.endpats">()).set_item((G<"tokenize._prefix">()) + (S<"'''">()), G<"tokenize.Single3">());
                        (G<"tokenize.endpats">()).set_item((G<"tokenize._prefix">()) + (S<"\"\"\"">()), G<"tokenize.Double3">());
                    }
                    return none();
                }))();
            undefine("tokenize", "_prefix");
            // A set of all of the single and triple quoted string prefixes,
            // including the opening quotes.
            define("tokenize", "single_quoted", (B<"set">())());
            define("tokenize", "triple_quoted", (B<"set">())());
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ (G<"tokenize._all_string_prefixes">())() })
                    {
                        set_global("tokenize", "t", item);
                        for (py const& item : iterate{ py_tuple({ (G<"tokenize.t">()) + (S<"\"">()), (G<"tokenize.t">()) + (S<"'">()) }) })
                        {
                            set_global("tokenize", "u", item);
                            (G<"tokenize.single_quoted">()).method("add", { G<"tokenize.u">() });
                        }
                        for (py const& item : iterate{ py_tuple({ (G<"tokenize.t">()) + (S<"\"\"\"">()), (G<"tokenize.t">()) + (S<"'''">()) }) })
                        {
                            set_global("tokenize", "u", item);
                            (G<"tokenize.triple_quoted">()).method("add", { G<"tokenize.u">() });
                        }
                    }
                    return none();
                }))();
            undefine("tokenize", "t");
            undefine("tokenize", "u");
            define("tokenize", "tabsize", I<8>());
            define("tokenize", "TokenError", w.make_class("TokenError", { B<"Exception">() }, {
 }));

            define("tokenize", "Untokenizer", w.make_class("Untokenizer", {  }, {
                { "__init__", w.method("Untokenizer", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("tokens", py_list());
                        self.set_attr("prev_row", I<1>());
                        self.set_attr("prev_col", I<0>());
                        self.set_attr("prev_type", none());
                        self.set_attr("prev_line", S<"">());
                        self.set_attr("encoding", none());
                        return none();
                    }) },
                { "add_whitespace", w.method("Untokenizer", "add_whitespace", { py_positional("start") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py start = a[1];
                        py col;
                        py col_offset;
                        py row;
                        auto const items9 = unpack(start, 2);
                        row = items9[0];
                        col = items9[1];
                        if (((row < self.attr("prev_row")) || ([&]() -> py { py const t = boolean(row == self.attr("prev_row")); return !t.truthy() ? t : boolean(col < self.attr("prev_col")); }()).truthy()))
                        {
                            py_raise((B<"ValueError">())((S<"start ({},{}) precedes previous end ({},{})">()).method("format", { row, col, self.attr("prev_row"), self.attr("prev_col") })));
                        }
                        self.method("add_backslash_continuation", { start });
                        col_offset = col - self.attr("prev_col");
                        if (col_offset.truthy())
                        {
                            self.attr("tokens").method("append", { (S<" ">()) * col_offset });
                        }
                        return none();
                    }) },
                { "add_backslash_continuation", w.method("Untokenizer", "add_backslash_continuation", { py_positional("start") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py start = a[1];
                        py line;
                        py newline;
                        py row;
                        py row_offset;
                        py ws;
                        row = start[I<0>()];
                        row_offset = row - self.attr("prev_row");
                        if (row_offset == (I<0>()))
                        {
                            return none();
                        }
                        newline = (self.attr("prev_line").method("endswith", { S<"\r\n">() }).truthy() ? S<"\r\n">() : S<"\n">());
                        line = self.attr("prev_line").method("rstrip", { S<"\\\r\n">() });
                        ws = (S<"">()).method("join", { (Mod<"itertools">().attr("takewhile"))((B<"str">()).attr("isspace"), (B<"reversed">())(line)) });
                        self.attr("tokens").method("append", { ws + (S(std::string{ "\\" } + newline.str()) * row_offset) });
                        self.set_attr("prev_col", I<0>());
                        return none();
                    }) },
                { "escape_brackets", w.method("Untokenizer", "escape_brackets", { py_positional("token") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token = a[1];
                        py character;
                        py characters;
                        py consume_until_next_bracket;
                        py n_backslashes;
                        characters = py_list();
                        consume_until_next_bracket = boolean(false);
                        for (py const& item : iterate{ token })
                        {
                            character = item;
                            if (character == (S<"}">()))
                            {
                                if (consume_until_next_bracket.truthy())
                                {
                                    consume_until_next_bracket = boolean(false);
                                }
                                else
                                {
                                    characters.method("append", { character });
                                }
                            }
                            if (character == (S<"{">()))
                            {
                                n_backslashes = (B<"sum">())([&]() -> py { py const comprehension11 = py_list(); py char_; for (py const& item : iterate{ (Mod<"itertools">().attr("takewhile"))((S<"\\">()).attr("__eq__"), characters[B<"slice">()(I<-2>(), none(), I<-1>())]) }) { char_ = item; comprehension11.method("append", { I<1>() }); } return comprehension11; }());
                                if ((((n_backslashes % (I<2>())) == (I<0>())) || (characters[I<-1>()] != (S<"N">()))))
                                {
                                    characters.method("append", { character });
                                }
                                else
                                {
                                    consume_until_next_bracket = boolean(true);
                                }
                            }
                            characters.method("append", { character });
                        }
                        return (S<"">()).method("join", { characters });
                    }) },
                { "untokenize", w.method("Untokenizer", "untokenize", { py_positional("iterable") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py iterable = a[1];
                        py end;
                        py end_col;
                        py end_line;
                        py extra_chars;
                        py indent;
                        py indents;
                        py it;
                        py last_line;
                        py line;
                        py start;
                        py startline;
                        py t;
                        py tok_type;
                        py token;
                        it = (B<"iter">())(iterable);
                        indents = py_list();
                        startline = boolean(false);
                        for (py const& item : iterate{ it })
                        {
                            t = item;
                            if (py{ t.size() } == (I<2>()))
                            {
                                self.method("compat", { t, it });
                                break;
                            }
                            auto const items13 = unpack(t, 5);
                            tok_type = items13[0];
                            token = items13[1];
                            start = items13[2];
                            end = items13[3];
                            line = items13[4];
                            if (tok_type == (Mod<"token">().attr("ENCODING")))
                            {
                                self.set_attr("encoding", token);
                                continue;
                            }
                            if (tok_type == (Mod<"token">().attr("ENDMARKER")))
                            {
                                break;
                            }
                            if (tok_type == (Mod<"token">().attr("INDENT")))
                            {
                                indents.method("append", { token });
                                continue;
                            }
                            else if (tok_type == (Mod<"token">().attr("DEDENT")))
                            {
                                indents.method("pop");
                                auto const items14 = unpack(end, 2);
                                self.set_attr("prev_row", items14[0]);
                                self.set_attr("prev_col", items14[1]);
                                continue;
                            }
                            else if (py_tuple({ Mod<"token">().attr("NEWLINE"), Mod<"token">().attr("NL") }).contains(tok_type))
                            {
                                startline = boolean(true);
                            }
                            else if ((startline.truthy() && indents.truthy()))
                            {
                                indent = indents[I<-1>()];
                                if (start[I<1>()] >= py{ indent.size() })
                                {
                                    self.attr("tokens").method("append", { indent });
                                    self.set_attr("prev_col", py{ indent.size() });
                                }
                                startline = boolean(false);
                            }
                            else if (tok_type == (Mod<"token">().attr("FSTRING_MIDDLE")))
                            {
                                if ((token.contains((S<"{">())) || token.contains((S<"}">()))))
                                {
                                    token = self.method("escape_brackets", { token });
                                    last_line = token.method("splitlines")[I<-1>()];
                                    auto const items15 = unpack(end, 2);
                                    end_line = items15[0];
                                    end_col = items15[1];
                                    extra_chars = last_line.method("count", { S<"{{">() }) + last_line.method("count", { S<"}}">() });
                                    end = py_tuple({ end_line, end_col + extra_chars });
                                }
                            }
                            self.method("add_whitespace", { start });
                            self.attr("tokens").method("append", { token });
                            auto const items16 = unpack(end, 2);
                            self.set_attr("prev_row", items16[0]);
                            self.set_attr("prev_col", items16[1]);
                            if (py_tuple({ Mod<"token">().attr("NEWLINE"), Mod<"token">().attr("NL") }).contains(tok_type))
                            {
                                {
                                    py const target17 = self;
                                    target17.set_attr("prev_row", inplace(NEOS_PY_ADD, target17.attr("prev_row"), I<1>()));
                                }
                                self.set_attr("prev_col", I<0>());
                            }
                            self.set_attr("prev_type", tok_type);
                            self.set_attr("prev_line", line);
                        }
                        return (S<"">()).method("join", { self.attr("tokens") });
                    }) },
                { "compat", w.method("Untokenizer", "compat", { py_positional("token"), py_positional("iterable") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token = a[1];
                        py iterable = a[2];
                        py in_fstring;
                        py indents;
                        py prevstring;
                        py startline;
                        py tok;
                        py toknum;
                        py toks_append;
                        py tokval;
                        indents = py_list();
                        toks_append = self.attr("tokens").attr("append");
                        startline = boolean(py_tuple({ Mod<"token">().attr("NEWLINE"), Mod<"token">().attr("NL") }).contains(token[I<0>()]));
                        prevstring = boolean(false);
                        in_fstring = I<0>();
                        for (py const& item : iterate{ (Mod<"itertools">().attr("chain"))(py_list({ token }), iterable) })
                        {
                            tok = item;
                            auto const items19 = unpack(tok[B<"slice">()(none(), I<2>(), none())], 2);
                            toknum = items19[0];
                            tokval = items19[1];
                            if (toknum == (Mod<"token">().attr("ENCODING")))
                            {
                                self.set_attr("encoding", tokval);
                                continue;
                            }
                            if (py_tuple({ Mod<"token">().attr("NAME"), Mod<"token">().attr("NUMBER") }).contains(toknum))
                            {
                                tokval = inplace(NEOS_PY_ADD, tokval, S<" ">());
                            }
                            // Insert a space between two consecutive strings
                            if (toknum == (Mod<"token">().attr("STRING")))
                            {
                                if (prevstring.truthy())
                                {
                                    tokval = (S<" ">()) + tokval;
                                }
                                prevstring = boolean(true);
                            }
                            else
                            {
                                prevstring = boolean(false);
                            }
                            if (toknum == (Mod<"token">().attr("FSTRING_START")))
                            {
                                in_fstring = inplace(NEOS_PY_ADD, in_fstring, I<1>());
                            }
                            else if (toknum == (Mod<"token">().attr("FSTRING_END")))
                            {
                                in_fstring = inplace(NEOS_PY_SUBTRACT, in_fstring, I<1>());
                            }
                            if (toknum == (Mod<"token">().attr("INDENT")))
                            {
                                indents.method("append", { tokval });
                                continue;
                            }
                            else if (toknum == (Mod<"token">().attr("DEDENT")))
                            {
                                indents.method("pop");
                                continue;
                            }
                            else if (py_tuple({ Mod<"token">().attr("NEWLINE"), Mod<"token">().attr("NL") }).contains(toknum))
                            {
                                startline = boolean(true);
                            }
                            else if ((startline.truthy() && indents.truthy()))
                            {
                                toks_append(indents[I<-1>()]);
                                startline = boolean(false);
                            }
                            else if (toknum == (Mod<"token">().attr("FSTRING_MIDDLE")))
                            {
                                tokval = self.method("escape_brackets", { tokval });
                            }
                            // Insert a space between two consecutive brackets if we are in an f-string
                            if ((set_of({ S<"{">(), S<"}">() }).contains(tokval) && self.attr("tokens").truthy() && (self.attr("tokens")[I<-1>()] == tokval) && in_fstring.truthy()))
                            {
                                tokval = (S<" ">()) + tokval;
                            }
                            // Insert a space between two consecutive f-strings
                            if ((py_tuple({ Mod<"token">().attr("STRING"), Mod<"token">().attr("FSTRING_START") }).contains(toknum) && py_tuple({ Mod<"token">().attr("STRING"), Mod<"token">().attr("FSTRING_END") }).contains(self.attr("prev_type"))))
                            {
                                self.attr("tokens").method("append", { S<" ">() });
                            }
                            toks_append(tokval);
                            self.set_attr("prev_type", toknum);
                        }
                        return none();
                    }) }, }));

            define("tokenize", "untokenize", w.function("untokenize", "untokenize", { py_positional("iterable") }, [](arguments a) -> py
                {
                    py iterable = a[0];
                    py out;
                    py ut;
                    ut = (G<"tokenize.Untokenizer">())();
                    out = ut.method("untokenize", { iterable });
                    if (!ut.attr("encoding").is_none())
                    {
                        out = out.method("encode", { ut.attr("encoding") });
                    }
                    return out;
                }));

            define("tokenize", "_get_normal_name", w.function("_get_normal_name", "_get_normal_name", { py_positional("orig_enc") }, [](arguments a) -> py
                {
                    py orig_enc = a[0];
                    py enc;
                    // Only care about the first 12 characters.
                    enc = orig_enc[B<"slice">()(none(), I<12>(), none())].method("lower").method("replace", { S<"_">(), S<"-">() });
                    if (((enc == (S<"utf-8">())) || enc.method("startswith", { S<"utf-8-">() }).truthy()))
                    {
                        return S<"utf-8">();
                    }
                    if ((py_tuple({ S<"latin-1">(), S<"iso-8859-1">(), S<"iso-latin-1">() }).contains(enc) || enc.method("startswith", { py_tuple({ S<"latin-1-">(), S<"iso-8859-1-">(), S<"iso-latin-1-">() }) }).truthy()))
                    {
                        return S<"iso-8859-1">();
                    }
                    return orig_enc;
                }));

            define("tokenize", "detect_encoding", w.function("detect_encoding", "detect_encoding", { py_positional("readline") }, [](arguments a) -> py
                {
                    auto const readline = std::make_shared<py>(a[0]);
                    auto const bom_found = std::make_shared<py>();
                    py default_;
                    auto const encoding = std::make_shared<py>();
                    auto const filename = std::make_shared<py>();
                    py find_cookie;
                    py first;
                    py read_or_stop;
                    py second;
                    try
                    {
                        *filename = (*readline).attr("__self__").attr("name");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler20;
                        if (matches(handler20.exception, B<"AttributeError">()))
                        {
                            *filename = none();
                        }
                        else
                            py_reraise(handler20.exception);
                    }
                    *bom_found = boolean(false);
                    *encoding = none();
                    default_ = S<"utf-8">();
                    read_or_stop = w.function("read_or_stop", "detect_encoding.<locals>.read_or_stop", {  }, [readline](arguments a) -> py
    {
        try
        {
            return (*readline)();
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler21;
            if (matches(handler21.exception, B<"StopIteration">()))
            {
                return py{ make_bytes(std::string{ "", 0 }) };
            }
            else
                py_reraise(handler21.exception);
        }
        return none();
    });
                    find_cookie = w.function("find_cookie", "detect_encoding.<locals>.find_cookie", { py_positional("line") }, [bom_found, filename](arguments a) -> py
    {
        py line = a[0];
        py codec;
        py encoding;
        py line_string;
        py match;
        py msg;
        try
        {
            // Decode as UTF-8. Either the line is an encoding declaration,
            // in which case it should be pure ASCII, or it must be UTF-8
            // per default encoding.
            line_string = line.method("decode", { S<"utf-8">() });
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler22;
            if (matches(handler22.exception, B<"UnicodeDecodeError">()))
            {
                msg = S<"invalid or missing encoding declaration">();
                if (!(*filename).is_none())
                {
                    msg = (S<"{} for {!r}">()).method("format", { msg, (*filename) });
                }
                py_raise((B<"SyntaxError">())(msg));
            }
            else
                py_reraise(handler22.exception);
        }
        match = (G<"tokenize.cookie_re">()).method("match", { line_string });
        if (!match.truthy())
        {
            return none();
        }
        encoding = (G<"tokenize._get_normal_name">())(match.method("group", { I<1>() }));
        try
        {
            codec = (Mod<"codecs">().attr("lookup"))(encoding);
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler23;
            if (matches(handler23.exception, B<"LookupError">()))
            {
                // This behaviour mimics the Python interpreter
                if ((*filename).is_none())
                {
                    msg = (S<"unknown encoding: ">()) + encoding;
                }
                else
                {
                    msg = (S<"unknown encoding for {!r}: {}">()).method("format", { (*filename), encoding });
                }
                py_raise((B<"SyntaxError">())(msg));
            }
            else
                py_reraise(handler23.exception);
        }
        if ((*bom_found).truthy())
        {
            if (encoding != (S<"utf-8">()))
            {
                // This behaviour mimics the Python interpreter
                if ((*filename).is_none())
                {
                    msg = S<"encoding problem: utf-8">();
                }
                else
                {
                    msg = (S<"encoding problem for {!r}: utf-8">()).method("format", { (*filename) });
                }
                py_raise((B<"SyntaxError">())(msg));
            }
            encoding = inplace(NEOS_PY_ADD, encoding, S<"-sig">());
        }
        return encoding;
    });
                    first = read_or_stop();
                    if (first.method("startswith", { Mod<"codecs">().attr("BOM_UTF8") }).truthy())
                    {
                        *bom_found = boolean(true);
                        first = first[B<"slice">()(I<3>(), none(), none())];
                        default_ = S<"utf-8-sig">();
                    }
                    if (!first.truthy())
                    {
                        return py_tuple({ default_, py_list() });
                    }
                    *encoding = find_cookie(first);
                    if ((*encoding).truthy())
                    {
                        return py_tuple({ (*encoding), py_list({ first }) });
                    }
                    if (!(G<"tokenize.blank_re">()).method("match", { first }).truthy())
                    {
                        return py_tuple({ default_, py_list({ first }) });
                    }
                    second = read_or_stop();
                    if (!second.truthy())
                    {
                        return py_tuple({ default_, py_list({ first }) });
                    }
                    *encoding = find_cookie(second);
                    if ((*encoding).truthy())
                    {
                        return py_tuple({ (*encoding), py_list({ first, second }) });
                    }
                    return py_tuple({ default_, py_list({ first, second }) });
                }));

            define("tokenize", "open", w.function("open", "open", { py_positional("filename") }, [](arguments a) -> py
                {
                    py filename = a[0];
                    py buffer;
                    py encoding;
                    py lines;
                    py text;
                    buffer = (Mod<"builtins">().attr("open"))(filename, S<"rb">());
                    try
                    {
                        auto const items25 = unpack((G<"tokenize.detect_encoding">())(buffer.attr("readline")), 2);
                        encoding = items25[0];
                        lines = items25[1];
                        buffer.method("seek", { I<0>() });
                        text = (Mod<"io">().attr("TextIOWrapper")).call({ buffer, encoding }, { { "line_buffering", boolean(true) } });
                        text.set_attr("mode", S<"r">());
                        return text;
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler24;
                        {
                            buffer.method("close");
                            py_reraise(handler24.exception);
                        }
                    }
                    return none();
                }));

            define("tokenize", "tokenize", w.function("tokenize", "tokenize", { py_positional("readline") }, [](arguments a) -> py
                {
                    return make_native_generator("tokenize", "tokenize", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py readline = a[0];
                        py consumed;
                        py encoding;
                        py rl_gen;
                        auto const items26 = unpack((G<"tokenize.detect_encoding">())(readline), 2);
                        encoding = items26[0];
                        consumed = items26[1];
                        rl_gen = (Mod<"itertools">().attr("chain"))(consumed, (B<"iter">())(readline, py{ make_bytes(std::string{ "", 0 }) }));
                        if (!encoding.is_none())
                        {
                            if (encoding == (S<"utf-8-sig">()))
                            {
                                // BOM will already have been stripped.
                                encoding = S<"utf-8">();
                            }
                            py_yield((G<"tokenize.TokenInfo">())(Mod<"token">().attr("ENCODING"), encoding, py_tuple({ I<0>(), I<0>() }), py_tuple({ I<0>(), I<0>() }), S<"">()));
                        }
                        yield_from((G<"tokenize._generate_tokens_from_c_tokenizer">()).call({ rl_gen.attr("__next__"), encoding }, { { "extra_tokens", boolean(true) } }));
                        return none();
                    });
                }));

            define("tokenize", "generate_tokens", w.function("generate_tokens", "generate_tokens", { py_positional("readline") }, [](arguments a) -> py
                {
                    py readline = a[0];
                    return (G<"tokenize._generate_tokens_from_c_tokenizer">()).call({ readline }, { { "extra_tokens", boolean(true) } });
                }));

            define("tokenize", "main", w.function("main", "main", {  }, [](arguments a) -> py
                {
                    py argparse;
                    auto const args = std::make_shared<py>();
                    py column;
                    py err;
                    py error_;
                    py f;
                    py filename;
                    py line;
                    py parser;
                    auto const perror = std::make_shared<py>();
                    py token;
                    py token_range;
                    py token_type;
                    py tokens;
                    argparse = module_of("argparse");
                    // Helper error handling routines
                    *perror = w.function("perror", "main.<locals>.perror", { py_positional("message") }, [](arguments a) -> py
    {
        py message = a[0];
        (Mod<"sys">().attr("stderr")).method("write", { message });
        (Mod<"sys">().attr("stderr")).method("write", { S<"\n">() });
        return none();
    });
                    error_ = w.function("error", "main.<locals>.error", { py_positional("message"), py_positional("filename", none()), py_positional("location", none()) }, [perror](arguments a) -> py
    {
        py message = a[0];
        py filename = a[1];
        py location = a[2];
        py args;
        if (location.truthy())
        {
            args = (py_tuple({ filename }) + location) + py_tuple({ message });
            (*perror)((S<"%s:%d:%d: error: %s">()) % args);
        }
        else if (filename.truthy())
        {
            (*perror)((S<"%s: error: %s">()) % py_tuple({ filename, message }));
        }
        else
        {
            (*perror)((S<"error: %s">()) % message);
        }
        (Mod<"sys">().attr("exit"))(I<1>());
        return none();
    });
                    // Parse the arguments and options
                    parser = argparse.method("ArgumentParser", {  }, { { "prog", S<"python -m tokenize">() } });
                    parser.method("add_argument", {  }, { { "dest", S<"filename">() }, { "nargs", S<"?">() }, { "metavar", S<"filename.py">() }, { "help", S<"the file to tokenize; defaults to stdin">() } });
                    parser.method("add_argument", { S<"-e">(), S<"--exact">() }, { { "dest", S<"exact">() }, { "action", S<"store_true">() }, { "help", S<"display token names using the exact type">() } });
                    *args = parser.method("parse_args");
                    try
                    {
                        // Tokenize the input
                        if ((*args).attr("filename").truthy())
                        {
                            filename = (*args).attr("filename");
                            {
                                py const manager28 = (Mod<"builtins">().attr("open"))(filename, S<"rb">());
                                py const exit28 = pyclass::special_method(manager28, "__exit__");
                                py const value28 = pyclass::special_method(manager28, "__enter__")(manager28);
                                f = value28;
                                [[maybe_unused]] int flow28 = 0;
                                [[maybe_unused]] py result28;
                                bool exited28 = false;
                                try
                                {
                                    tokens = (B<"list">())((G<"tokenize.tokenize">())(f.attr("readline")));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler28;
                                    exited28 = true;
                                    if (!exit28(manager28, type_of(handler28.exception), handler28.exception, handler28.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler28.exception);
                                }
                                finally28:
                                if (!exited28)
                                    exit28(manager28, none(), none(), none());
                                if (flow28 == 1)
                                    return result28;
                            }
                        }
                        else
                        {
                            filename = S<"<stdin>">();
                            tokens = (G<"tokenize._generate_tokens_from_c_tokenizer">()).call({ (Mod<"sys">().attr("stdin")).attr("readline") }, { { "extra_tokens", boolean(true) } });
                        }
                        // Output the tokenization
                        for (py const& item : iterate{ tokens })
                        {
                            token = item;
                            token_type = token.attr("type");
                            if ((*args).attr("exact").truthy())
                            {
                                token_type = token.attr("exact_type");
                            }
                            token_range = (S<"%d,%d-%d,%d:">()) % (token.attr("start") + token.attr("end"));
                            (B<"print">())((S<"%-20s%-15s%-15r">()) % py_tuple({ token_range, (Mod<"token">().attr("tok_name"))[token_type], token.attr("string") }));
                        }
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler27;
                        if (matches(handler27.exception, B<"IndentationError">()))
                        {
                            err = handler27.exception;
                            auto const items30 = unpack(err.attr("args")[I<1>()][B<"slice">()(I<1>(), I<3>(), none())], 2);
                            line = items30[0];
                            column = items30[1];
                            error_(err.attr("args")[I<0>()], filename, py_tuple({ line, column }));
                        }
                        else if (matches(handler27.exception, G<"tokenize.TokenError">()))
                        {
                            err = handler27.exception;
                            auto const items31 = unpack(err.attr("args")[I<1>()], 2);
                            line = items31[0];
                            column = items31[1];
                            error_(err.attr("args")[I<0>()], filename, py_tuple({ line, column }));
                        }
                        else if (matches(handler27.exception, B<"SyntaxError">()))
                        {
                            err = handler27.exception;
                            error_(err, filename);
                        }
                        else if (matches(handler27.exception, B<"OSError">()))
                        {
                            err = handler27.exception;
                            error_(err);
                        }
                        else if (matches(handler27.exception, B<"KeyboardInterrupt">()))
                        {
                            (B<"print">())(S<"interrupted\n">());
                        }
                        else if (matches(handler27.exception, B<"Exception">()))
                        {
                            err = handler27.exception;
                            (*perror)((S<"unexpected error: %s">()) % err);
                            py_reraise(handler27.exception);
                        }
                        else
                            py_reraise(handler27.exception);
                    }
                    return none();
                }));

            define("tokenize", "_transform_msg", w.function("_transform_msg", "_transform_msg", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    if (msg.contains((S<"unterminated triple-quoted string literal">())))
                    {
                        return S<"EOF in multi-line string">();
                    }
                    return msg;
                }));

            define("tokenize", "_generate_tokens_from_c_tokenizer", w.function("_generate_tokens_from_c_tokenizer", "_generate_tokens_from_c_tokenizer", { py_positional("source"), py_positional("encoding", none()), py_positional("extra_tokens", boolean(false)) }, [](arguments a) -> py
                {
                    return make_native_generator("_generate_tokens_from_c_tokenizer", "_generate_tokens_from_c_tokenizer", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py source = a[0];
                        py encoding = a[1];
                        py extra_tokens = a[2];
                        py e;
                        py info;
                        py it;
                        py msg;
                        if (encoding.is_none())
                        {
                            it = (Mod<"_tokenize">().attr("TokenizerIter")).call({ source }, { { "extra_tokens", extra_tokens } });
                        }
                        else
                        {
                            it = (Mod<"_tokenize">().attr("TokenizerIter")).call({ source }, { { "encoding", encoding }, { "extra_tokens", extra_tokens } });
                        }
                        try
                        {
                            for (py const& item : iterate{ it })
                            {
                                info = item;
                                py_yield((G<"tokenize.TokenInfo">()).method("_make", { info }));
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler32;
                            if (matches(handler32.exception, B<"SyntaxError">()))
                            {
                                e = handler32.exception;
                                if ((B<"type">())(e) != (B<"SyntaxError">()))
                                {
                                    fn_raise_from(e.get(), none().get());
                                    throw raised{};
                                }
                                msg = (G<"tokenize._transform_msg">())(e.attr("msg"));
                                fn_raise_from((G<"tokenize.TokenError">())(msg, py_tuple({ e.attr("lineno"), e.attr("offset") })).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler32.exception);
                        }
                        return none();
                    });
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool tokenize_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_tokenize();
                }
                if (auto const doc = tokenize_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("_builtin_open", import_attribute("builtins", "open").release());
                aAttributes.emplace_back("lookup", import_attribute("codecs", "lookup").release());
                aAttributes.emplace_back("BOM_UTF8", import_attribute("codecs", "BOM_UTF8").release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                aAttributes.emplace_back("functools", py{ fn_import("functools") }.release());
                aAttributes.emplace_back("TextIOWrapper", import_attribute("io", "TextIOWrapper").release());
                aAttributes.emplace_back("_itertools", py{ fn_import("itertools") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                {
                    py const m{ fn_import("token") };
                    bool const all = has_attr(m, "__all__");
                    for (py const& n : iterate{ all ? m.attr("__all__") : builtin("dir")(m) })
                        if (auto const text = n.str(); all || !text.starts_with("_"))
                            aAttributes.emplace_back(text, m.attr(text.c_str()).release());
                }
                aAttributes.emplace_back("EXACT_TOKEN_TYPES", import_attribute("token", "EXACT_TOKEN_TYPES").release());
                aAttributes.emplace_back("_tokenize", py{ fn_import("_tokenize") }.release());
                aAttributes.emplace_back("token", py{ fn_import("token") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("tokenize", tokenize_module);
    }
}
