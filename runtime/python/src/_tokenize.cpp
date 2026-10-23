/*
  _tokenize.cpp

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

  Portions of this file are derived from CPython 3.11 (Lib/tokenize.py: its regular expressions and its tokenizer,
  _tokenize), Copyright (c) 2001-2023 Python Software Foundation; All Rights Reserved. They are used under the Python
  Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: the tokenizer gives tokens as CPython
  3.13's C tokenizer (Parser/tokenizer.c) gives them to its tokenize module (f-strings' FSTRING_START, FSTRING_MIDDLE and
  FSTRING_END tokens, and its errors); translated from Python to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The module _tokenize (the tokenizer of CPython 3.13's tokenize module: TokenizerIter; written in Python from CPython 3.11's Lib/tokenize.py, translated
// by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "tokenize_c_docs.hpp"

namespace neos::python_runtime::detail::_tokenize_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "_tokenize.name"))
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
                py_raise("SystemError", std::string{ "_tokenize: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"_tokenize.name">()))
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
            if (modules_imported("_tokenize"))
                return py{ fn_import("_tokenize") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "_tokenize" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "_tokenize", tokenize_c_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- _tokenize (from CPython 3.11's Lib/tokenize.py)

        void make__tokenize()
        {
            static module_writer const& w = writer("_tokenize");
            // --- CPython 3.11's Lib/tokenize.py's regular expressions
            define("_tokenize", "_group", w.function("_group", "_group", {  }, [](arguments a) -> py
                {
                    py choices = a[0];
                    return ((S<"(">()) + (S<"|">()).method("join", { choices })) + (S<")">());
                }, true));

            define("_tokenize", "_maybe", w.function("_maybe", "_maybe", {  }, [](arguments a) -> py
                {
                    py choices = a[0];
                    return call_ex((G<"_tokenize._group">()), {  }, choices, py{}) + (S<"?">());
                }, true));

            define("_tokenize", "Whitespace", S<"[ \\f\\t]*">());
            define("_tokenize", "Comment", S<"#[^\\r\\n]*">());
            define("_tokenize", "Name", S<"\\w+">());
            define("_tokenize", "Hexnumber", S<"0[xX](?:_?[0-9a-fA-F])+">());
            define("_tokenize", "Binnumber", S<"0[bB](?:_?[01])+">());
            define("_tokenize", "Octnumber", S<"0[oO](?:_?[0-7])+">());
            define("_tokenize", "Decnumber", S<"(?:0(?:_?0)*|[1-9](?:_?[0-9])*)">());
            define("_tokenize", "Intnumber", (G<"_tokenize._group">())(G<"_tokenize.Hexnumber">(), G<"_tokenize.Binnumber">(), G<"_tokenize.Octnumber">(), G<"_tokenize.Decnumber">()));
            define("_tokenize", "Exponent", S<"[eE][-+]?[0-9](?:_?[0-9])*">());
            define("_tokenize", "Pointfloat", (G<"_tokenize._group">())(S<"[0-9](?:_?[0-9])*\\.(?:[0-9](?:_?[0-9])*)?">(), S<"\\.[0-9](?:_?[0-9])*">()) + (G<"_tokenize._maybe">())(G<"_tokenize.Exponent">()));
            define("_tokenize", "Expfloat", (S<"[0-9](?:_?[0-9])*">()) + (G<"_tokenize.Exponent">()));
            define("_tokenize", "Floatnumber", (G<"_tokenize._group">())(G<"_tokenize.Pointfloat">(), G<"_tokenize.Expfloat">()));
            define("_tokenize", "Imagnumber", (G<"_tokenize._group">())(S<"[0-9](?:_?[0-9])*[jJ]">(), (G<"_tokenize.Floatnumber">()) + (S<"[jJ]">())));
            define("_tokenize", "Number", (G<"_tokenize._group">())(G<"_tokenize.Imagnumber">(), G<"_tokenize.Floatnumber">(), G<"_tokenize.Intnumber">()));
            define("_tokenize", "_all_string_prefixes", w.function("_all_string_prefixes", "_all_string_prefixes", {  }, [](arguments a) -> py
                {
                    py _valid_string_prefixes;
                    py prefix;
                    py result;
                    py t;
                    py u;
                    _valid_string_prefixes = py_list({ S<"b">(), S<"r">(), S<"u">(), S<"f">(), S<"br">(), S<"fr">() });
                    result = set_of({ S<"">() });
                    for (py const& item : iterate{ _valid_string_prefixes })
                    {
                        prefix = item;
                        for (py const& item : iterate{ (G<"_tokenize._permutations">())(prefix) })
                        {
                            t = item;
                            for (py const& item : iterate{ (G<"_tokenize._cases">())(t) })
                            {
                                u = item;
                                result.method("add", { (S<"">()).method("join", { u }) });
                            }
                        }
                    }
                    return result;
                }));

            define("_tokenize", "_permutations", w.function("_permutations", "_permutations", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py i;
                    py rest;
                    py result;
                    if (py{ s.size() } <= (I<1>()))
                    {
                        return py_list({ s });
                    }
                    result = py_list();
                    for (py const& item : iterate{ (B<"range">())(py{ s.size() }) })
                    {
                        i = item;
                        for (py const& item : iterate{ (G<"_tokenize._permutations">())(s[B<"slice">()(none(), i, none())] + s[B<"slice">()(i + (I<1>()), none(), none())]) })
                        {
                            rest = item;
                            result.method("append", { s[i] + rest });
                        }
                    }
                    return result;
                }));

            define("_tokenize", "_cases", w.function("_cases", "_cases", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py c;
                    py result;
                    result = py_list({ S<"">() });
                    for (py const& item : iterate{ s })
                    {
                        c = item;
                        result = [&]() -> py { py const comprehension7 = py_list(); py r; py x; for (py const& item : iterate{ result }) { r = item; for (py const& item : iterate{ py_tuple({ c.method("lower"), c.method("upper") }) }) { x = item; comprehension7.method("append", { r + x }); } } return comprehension7; }();
                    }
                    return result;
                }));

            define("_tokenize", "StringPrefix", call_ex((G<"_tokenize._group">()), {  }, (B<"sorted">())((G<"_tokenize._all_string_prefixes">())()), py{}));
            // (the tail end of ' and " strings, and of ''' and """ strings)
            define("_tokenize", "Single", S<"[^'\\\\]*(?:\\\\.[^'\\\\]*)*'">());
            define("_tokenize", "Double", S<"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"">());
            define("_tokenize", "Single3", S<"[^'\\\\]*(?:(?:\\\\.|'(?!''))[^'\\\\]*)*'''">());
            define("_tokenize", "Double3", S<"[^\"\\\\]*(?:(?:\\\\.|\"(?!\"\"))[^\"\\\\]*)*\"\"\"">());
            define("_tokenize", "Triple", (G<"_tokenize._group">())((G<"_tokenize.StringPrefix">()) + (S<"'''">()), (G<"_tokenize.StringPrefix">()) + (S<"\"\"\"">())));
            define("_tokenize", "Special", call_ex((G<"_tokenize._group">()), {  }, (B<"map">())(Mod<"re">().attr("escape"), (B<"sorted">()).call({ Mod<"token">().attr("EXACT_TOKEN_TYPES") }, { { "reverse", boolean(true) } })), py{}));
            define("_tokenize", "Funny", (G<"_tokenize._group">())(S<"\\r?\\n">(), G<"_tokenize.Special">()));
            // (the first line of strings that may continue on the next line)
            define("_tokenize", "ContStr", (G<"_tokenize._group">())(((G<"_tokenize.StringPrefix">()) + (S<"'[^\\n'\\\\]*(?:\\\\.[^\\n'\\\\]*)*">())) + (G<"_tokenize._group">())(S<"'">(), S<"\\\\\\r?\\n">()), ((G<"_tokenize.StringPrefix">()) + (S<"\"[^\\n\"\\\\]*(?:\\\\.[^\\n\"\\\\]*)*">())) + (G<"_tokenize._group">())(S<"\"">(), S<"\\\\\\r?\\n">())));
            define("_tokenize", "PseudoExtras", (G<"_tokenize._group">())(S<"\\\\\\r?\\n|\\Z">(), G<"_tokenize.Comment">(), G<"_tokenize.Triple">()));
            define("_tokenize", "PseudoToken", (G<"_tokenize.Whitespace">()) + (G<"_tokenize._group">())(G<"_tokenize.PseudoExtras">(), G<"_tokenize.Number">(), G<"_tokenize.Funny">(), G<"_tokenize.ContStr">(), G<"_tokenize.Name">()));
            define("_tokenize", "_pseudo", (Mod<"re">()).method("compile", { G<"_tokenize.PseudoToken">(), Mod<"re">().attr("UNICODE") }));
            define("_tokenize", "_endprogs", dict_of({ { S<"'">(), (Mod<"re">()).method("compile", { G<"_tokenize.Single">() }) }, { S<"\"">(), (Mod<"re">()).method("compile", { G<"_tokenize.Double">() }) }, { S<"'''">(), (Mod<"re">()).method("compile", { G<"_tokenize.Single3">() }) }, { S<"\"\"\"">(), (Mod<"re">()).method("compile", { G<"_tokenize.Double3">() }) } }));
            define("_tokenize", "single_quoted", (B<"set">())());
            define("_tokenize", "triple_quoted", (B<"set">())());
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ (G<"_tokenize._all_string_prefixes">())() })
                    {
                        set_global("_tokenize", "_t", item);
                        for (py const& item : iterate{ py_tuple({ (G<"_tokenize._t">()) + (S<"\"">()), (G<"_tokenize._t">()) + (S<"'">()) }) })
                        {
                            set_global("_tokenize", "_u", item);
                            (G<"_tokenize.single_quoted">()).method("add", { G<"_tokenize._u">() });
                        }
                        for (py const& item : iterate{ py_tuple({ (G<"_tokenize._t">()) + (S<"\"\"\"">()), (G<"_tokenize._t">()) + (S<"'''">()) }) })
                        {
                            set_global("_tokenize", "_u", item);
                            (G<"_tokenize.triple_quoted">()).method("add", { G<"_tokenize._u">() });
                        }
                    }
                    return none();
                }))();
            undefine("_tokenize", "_t");
            undefine("_tokenize", "_u");
            define("_tokenize", "tabsize", I<8>());
            define("_tokenize", "_fstring_prefix", (Mod<"re">()).method("compile", { S<"(?i)(?:f|fr|rf)(?:\\'\\'\\'|\"\"\"|\\'|\")">() }));
            define("_tokenize", "_syntax_error", w.function("_syntax_error", "_syntax_error", { py_positional("message"), py_positional("lnum"), py_positional("offset"), py_positional("line") }, [](arguments a) -> py
                {
                    py message = a[0];
                    py lnum = a[1];
                    py offset = a[2];
                    py line = a[3];
                    return (B<"SyntaxError">())(message, py_tuple({ S<"<tokenize>">(), lnum, offset, line }));
                }));

                // (an f-string being tokenized: its quote, whether it is raw, and its replacement fields (their brackets' depths and
                // whether a format spec is being tokenized))
            define("_tokenize", "_FString", w.make_class("_FString", {  }, {
                { "__init__", w.method("_FString", "__init__", { py_positional("quote"), py_positional("raw"), py_positional("start"), py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py quote = a[1];
                        py raw = a[2];
                        py start = a[3];
                        py line = a[4];
                        self.set_attr("quote", quote);
                        self.set_attr("raw", raw);
                        self.set_attr("start", start);
                        self.set_attr("line", line);
                        self.set_attr("fields", py_list());
                        return none();
                    }) }, }));

            define("_tokenize", "TokenizerIter", w.make_class("TokenizerIter", {  }, {
                { "__init__", w.method("TokenizerIter", "__init__", { py_positional("readline"), py_keyword("extra_tokens"), py_keyword("encoding", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py readline = a[1];
                        py extra_tokens = a[2];
                        py encoding = a[3];
                        self.set_attr("_tokens", (G<"_tokenize._tokenize">())(readline, encoding, extra_tokens));
                        return none();
                    }) },
                { "__iter__", w.method("TokenizerIter", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__next__", w.method("TokenizerIter", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"next">())(self.attr("_tokens"));
                    }) }, }));

            define("_tokenize", "_tokenize", w.function("_tokenize", "_tokenize", { py_positional("readline"), py_positional("encoding"), py_positional("extra_tokens") }, [](arguments a) -> py
                {
                    return make_native_generator("_tokenize", "_tokenize", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py readline = a[0];
                        py encoding = a[1];
                        py extra_tokens = a[2];
                        py c;
                        py close;
                        py column;
                        py comment_token;
                        py continued;
                        py contline;
                        py contstr;
                        py end;
                        py endmatch;
                        py endprog;
                        py epos;
                        py fs;
                        py fstart;
                        py fstrings;
                        py indent;
                        py indents;
                        py initial;
                        py last_line;
                        py line;
                        py lnum;
                        py max_;
                        py middle;
                        py needcont;
                        py numchars;
                        py parenlev;
                        py pos;
                        py pseudomatch;
                        py quote;
                        py spec;
                        py spos;
                        py start;
                        py strstart;
                        py text;
                        py token;
                        py const value11 = I<0>();
                        lnum = value11;
                        parenlev = value11;
                        continued = value11;
                        numchars = S<"0123456789">();
                        auto const items12 = unpack(py_tuple({ S<"">(), I<0>() }), 2);
                        contstr = items12[0];
                        needcont = items12[1];
                        contline = none();
                        strstart = none();
                        endprog = none();
                        indents = py_list({ I<0>() });
                        fstrings = py_list();
                        middle = none();
                        last_line = S<"">();
                        line = S<"">();
                        while (boolean(true).truthy())
                        {
                            try
                            {
                                last_line = line;
                                line = readline();
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler14;
                                if (matches(handler14.exception, B<"StopIteration">()))
                                {
                                    line = S<"">();
                                }
                                else
                                    py_reraise(handler14.exception);
                            }
                            if (((!encoding.is_none()) && isinstance(line, B<"bytes">())))
                            {
                                line = line.method("decode", { encoding });
                            }
                            lnum = inplace(NEOS_PY_ADD, lnum, I<1>());
                            auto const items15 = unpack(py_tuple({ I<0>(), py{ line.size() } }), 2);
                            pos = items15[0];
                            max_ = items15[1];
                            if ((([&]() -> py { py const t = fstrings; return !t.truthy() ? t : boolean(!fstrings[I<-1>()].attr("fields").truthy()); }()).truthy() || ([&]() -> py { py const t = fstrings; return !t.truthy() ? t : fstrings[I<-1>()].attr("fields")[I<-1>()][I<1>()]; }()).truthy()))
                            {
                                // (an f-string's literal text (or format spec) continued from the previous line)
                                if (!line.truthy())
                                {
                                    py_raise((G<"_tokenize._syntax_error">())((S<"unterminated %sf-string literal (detected at line %d)">()) % py_tuple({ (py{ fstrings[I<-1>()].attr("quote").size() } == (I<3>()) ? S<"triple-quoted ">() : S<"">()), lnum - (I<1>()) }), fstrings[I<-1>()].attr("start")[I<0>()], fstrings[I<-1>()].attr("start")[I<1>()] + (I<1>()), fstrings[I<-1>()].attr("line")));
                                }
                                if (!middle.is_none())
                                {
                                    {
                                        py const target16 = middle;
                                        py const key16 = I<2>();
                                        target16.set_item(key16, inplace(NEOS_PY_ADD, target16[key16], line));
                                    }
                                }
                            }
                            else if (contstr.truthy())
                            {
                                // (a string continued from the previous line)
                                if (!line.truthy())
                                {
                                    py_raise((G<"_tokenize._syntax_error">())((S<"unterminated triple-quoted string literal (detected at line %d)">()) % (lnum - (I<1>())), strstart[I<0>()], strstart[I<1>()] + (I<1>()), contline));
                                }
                                endmatch = endprog.method("match", { line });
                                if (endmatch.truthy())
                                {
                                    py const value17 = endmatch.method("end", { I<0>() });
                                    pos = value17;
                                    end = value17;
                                    py_yield(py_tuple({ Mod<"token">().attr("STRING"), contstr + line[B<"slice">()(none(), end, none())], strstart, py_tuple({ lnum, end }), contline + line }));
                                    auto const items18 = unpack(py_tuple({ S<"">(), I<0>() }), 2);
                                    contstr = items18[0];
                                    needcont = items18[1];
                                    contline = none();
                                }
                                else if ((needcont.truthy() && (line[B<"slice">()(I<-2>(), none(), none())] != (S<"\\\n">())) && (line[B<"slice">()(I<-3>(), none(), none())] != (S<"\\\r\n">()))))
                                {
                                    py_raise((G<"_tokenize._syntax_error">())((S<"unterminated string literal (detected at line %d)">()) % strstart[I<0>()], strstart[I<0>()], strstart[I<1>()] + (I<1>()), contline));
                                }
                                else
                                {
                                    contstr = contstr + line;
                                    contline = contline + line;
                                    continue;
                                }
                            }
                            else if (((parenlev == (I<0>())) && (!continued.truthy()) && (!fstrings.truthy())))
                            {
                                // (a new statement)
                                if (!line.truthy())
                                {
                                    break;
                                }
                                column = I<0>();
                                while (pos < max_)
                                {
                                    if (line[pos] == (S<" ">()))
                                    {
                                        column = inplace(NEOS_PY_ADD, column, I<1>());
                                    }
                                    else if (line[pos] == (S<"\t">()))
                                    {
                                        column = (floordiv(column, (G<"_tokenize.tabsize">())) + (I<1>())) * (G<"_tokenize.tabsize">());
                                    }
                                    else if (line[pos] == (S<"\f">()))
                                    {
                                        column = I<0>();
                                    }
                                    else
                                    {
                                        break;
                                    }
                                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                                }
                                if (pos == max_)
                                {
                                    // (whitespace only, at the end of the source)
                                    if ((extra_tokens.truthy() && line.truthy()))
                                    {
                                        py_yield(py_tuple({ Mod<"token">().attr("NL"), S<"">(), py_tuple({ lnum, pos }), py_tuple({ lnum, pos + (I<1>()) }), line }));
                                    }
                                    break;
                                }
                                if ((S<"#\r\n">()).contains(line[pos]))
                                {
                                    // (a comment, or a blank line)
                                    if (line[pos] == (S<"#">()))
                                    {
                                        comment_token = line[B<"slice">()(pos, none(), none())].method("rstrip", { S<"\r\n">() });
                                        if (extra_tokens.truthy())
                                        {
                                            py_yield(py_tuple({ Mod<"token">().attr("COMMENT"), comment_token, py_tuple({ lnum, pos }), py_tuple({ lnum, pos + py{ comment_token.size() } }), line }));
                                        }
                                        pos = inplace(NEOS_PY_ADD, pos, py{ comment_token.size() });
                                    }
                                    if (extra_tokens.truthy())
                                    {
                                        py_yield(py_tuple({ Mod<"token">().attr("NL"), line[B<"slice">()(pos, none(), none())], py_tuple({ lnum, pos }), py_tuple({ lnum, py{ line.size() } + (line[B<"slice">()(pos, none(), none())].truthy() ? I<0>() : I<1>()) }), line }));
                                    }
                                    continue;
                                }
                                if (column > indents[I<-1>()])
                                {
                                    indents.method("append", { column });
                                    py_yield(py_tuple({ Mod<"token">().attr("INDENT"), line[B<"slice">()(none(), pos, none())], py_tuple({ lnum, I<0>() }), py_tuple({ lnum, pos }), line }));
                                }
                                while (column < indents[I<-1>()])
                                {
                                    if (!indents.contains(column))
                                    {
                                        text = line.method("rstrip", { S<"\r\n">() });
                                        py_raise((B<"IndentationError">())(S<"unindent does not match any outer indentation level">(), py_tuple({ S<"<string>">(), lnum, py{ text.size() } + (I<1>()), text, none(), none() })));
                                    }
                                    indents = indents[B<"slice">()(none(), I<-1>(), none())];
                                    py_yield(py_tuple({ Mod<"token">().attr("DEDENT"), S<"">(), py_tuple({ lnum, pos }), py_tuple({ lnum, pos }), line }));
                                }
                            }
                            else
                            {
                                // (a statement continued)
                                if (!line.truthy())
                                {
                                    // (at the end of the last line, unless brackets are open)
                                    if (((parenlev > (I<0>())) && (!fstrings.truthy()) && (!continued.truthy())))
                                    {
                                        py_raise((G<"_tokenize._syntax_error">())(S<"unexpected EOF in multi-line statement">(), lnum - (I<1>()), I<0>(), last_line));
                                    }
                                    py_raise((G<"_tokenize._syntax_error">())(S<"unexpected EOF in multi-line statement">(), lnum - (I<1>()), py{ last_line.size() }, last_line));
                                }
                                continued = I<0>();
                            }
                            while (pos < max_)
                            {
                                if ((fstrings.truthy() && ([&]() -> py { py const t = boolean(!fstrings[I<-1>()].attr("fields").truthy()); return t.truthy() ? t : fstrings[I<-1>()].attr("fields")[I<-1>()][I<1>()]; }()).truthy()))
                                {
                                    // (an f-string's literal text, or a format spec: to its end, or to a replacement field)
                                    fs = fstrings[I<-1>()];
                                    spec = (B<"bool">())(fs.attr("fields"));
                                    if (middle.is_none())
                                    {
                                        middle = py_list({ S<"">(), py_tuple({ lnum, pos }), line });
                                    }
                                    while (pos < max_)
                                    {
                                        c = line[pos];
                                        if (((!spec.truthy()) && line.method("startswith", { fs.attr("quote"), pos }).truthy()))
                                        {
                                            break;
                                        }
                                        if ((spec.truthy() && (S<"{}">()).contains(c)))
                                        {
                                            break;
                                        }
                                        if ((spec.truthy() && (S<"\r\n">()).contains(c) && (py{ fs.attr("quote").size() } == (I<1>()))))
                                        {
                                            break;
                                        }
                                        if (((!spec.truthy()) && (S<"{}">()).contains(c)))
                                        {
                                            if (line.method("startswith", { c * (I<2>()), pos }).truthy())
                                            {
                                                // ({{ or }}: the brace (the token ends at it; the next starts after the other))
                                                {
                                                    py const target23 = middle;
                                                    py const key23 = I<0>();
                                                    target23.set_item(key23, inplace(NEOS_PY_ADD, target23[key23], c));
                                                }
                                                py_yield(py_tuple({ Mod<"token">().attr("FSTRING_MIDDLE"), middle[I<0>()], middle[I<1>()], py_tuple({ lnum, pos + (I<1>()) }), middle[I<2>()] }));
                                                pos = inplace(NEOS_PY_ADD, pos, I<2>());
                                                middle = py_list({ S<"">(), py_tuple({ lnum, pos }), line });
                                                continue;
                                            }
                                            break;
                                        }
                                        if (((c == (S<"\\">())) && ((pos + (I<1>())) < max_)))
                                        {
                                            // (an escape: \N{name}'s braces are its own; a raw f-string's backslash escapes only quotes and
                                            // backslashes)
                                            if (((!fs.attr("raw").truthy()) && line.method("startswith", { S<"N{">(), pos + (I<1>()) }).truthy() && line[B<"slice">()(pos + (I<3>()), none(), none())].contains((S<"}">()))))
                                            {
                                                close = line.method("index", { S<"}">(), pos + (I<3>()) });
                                                {
                                                    py const target24 = middle;
                                                    py const key24 = I<0>();
                                                    target24.set_item(key24, inplace(NEOS_PY_ADD, target24[key24], line[B<"slice">()(pos, close + (I<1>()), none())]));
                                                }
                                                pos = close + (I<1>());
                                                // (the token ends at the escape's brace, as at a doubled brace's)
                                                py_yield(py_tuple({ Mod<"token">().attr("FSTRING_MIDDLE"), middle[I<0>()], middle[I<1>()], py_tuple({ lnum, pos }), middle[I<2>()] }));
                                                middle = py_list({ S<"">(), py_tuple({ lnum, pos }), line });
                                                continue;
                                            }
                                            if (((S<"{}">()).contains(line[pos + (I<1>())]) || ([&]() -> py { py const t = fs.attr("raw"); return !t.truthy() ? t : boolean(!(S<"'\"\\">()).contains(line[pos + (I<1>())])); }()).truthy()))
                                            {
                                                {
                                                    py const target25 = middle;
                                                    py const key25 = I<0>();
                                                    target25.set_item(key25, inplace(NEOS_PY_ADD, target25[key25], c));
                                                }
                                                pos = inplace(NEOS_PY_ADD, pos, I<1>());
                                                continue;
                                            }
                                            {
                                                py const target26 = middle;
                                                py const key26 = I<0>();
                                                target26.set_item(key26, inplace(NEOS_PY_ADD, target26[key26], line[B<"slice">()(pos, pos + (I<2>()), none())]));
                                            }
                                            pos = inplace(NEOS_PY_ADD, pos, I<2>());
                                            continue;
                                        }
                                        if (((S<"\r\n">()).contains(c) && (py{ fs.attr("quote").size() } == (I<1>())) && (!([&]() -> py { py const t = boolean(pos > (I<0>())); return !t.truthy() ? t : boolean(line[pos - (I<1>())] == (S<"\\">())); }()).truthy())))
                                        {
                                            py_raise((G<"_tokenize._syntax_error">())((S<"unterminated f-string literal (detected at line %d)">()) % lnum, fs.attr("start")[I<0>()], fs.attr("start")[I<1>()] + (I<1>()), fs.attr("line")));
                                        }
                                        {
                                            py const target27 = middle;
                                            py const key27 = I<0>();
                                            target27.set_item(key27, inplace(NEOS_PY_ADD, target27[key27], c));
                                        }
                                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                                    }
                                    if (pos >= max_)
                                    {
                                        break;
                                    }
                                    c = line[pos];
                                    if (((!spec.truthy()) && line.method("startswith", { fs.attr("quote"), pos }).truthy()))
                                    {
                                        if (middle[I<0>()].truthy())
                                        {
                                            py_yield(py_tuple({ Mod<"token">().attr("FSTRING_MIDDLE"), middle[I<0>()], middle[I<1>()], py_tuple({ lnum, pos }), middle[I<2>()] }));
                                        }
                                        middle = none();
                                        py_yield(py_tuple({ Mod<"token">().attr("FSTRING_END"), fs.attr("quote"), py_tuple({ lnum, pos }), py_tuple({ lnum, pos + py{ fs.attr("quote").size() } }), line }));
                                        pos = inplace(NEOS_PY_ADD, pos, py{ fs.attr("quote").size() });
                                        fstrings.method("pop");
                                        continue;
                                    }
                                    if ((spec.truthy() && (S<"\r\n">()).contains(c)))
                                    {
                                        py_raise((G<"_tokenize._syntax_error">())(S<"f-string: newlines are not allowed in format specifiers for single quoted f-strings">(), lnum, pos + (I<1>()), line));
                                    }
                                    if ((middle[I<0>()].truthy() || ([&]() -> py { py const t = spec; return !t.truthy() ? t : boolean(c == (S<"}">())); }()).truthy()))
                                    {
                                        // (a format spec's text: even if it is empty, at its end)
                                        py_yield(py_tuple({ Mod<"token">().attr("FSTRING_MIDDLE"), middle[I<0>()], middle[I<1>()], py_tuple({ lnum, pos }), middle[I<2>()] }));
                                    }
                                    middle = none();
                                    if (c == (S<"{">()))
                                    {
                                        // (a replacement field: its expression)
                                        py_yield(py_tuple({ Mod<"token">().attr("OP"), c, py_tuple({ lnum, pos }), py_tuple({ lnum, pos + (I<1>()) }), line }));
                                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                                        parenlev = inplace(NEOS_PY_ADD, parenlev, I<1>());
                                        fs.attr("fields").method("append", { py_list({ parenlev, boolean(false) }) });
                                        continue;
                                    }
                                    // (the end of a format spec's replacement field)
                                    if (!spec.truthy())
                                    {
                                        py_raise((G<"_tokenize._syntax_error">())(S<"f-string: single '}' is not allowed">(), lnum, pos + (I<1>()), line));
                                    }
                                    py_yield(py_tuple({ Mod<"token">().attr("OP"), c, py_tuple({ lnum, pos }), py_tuple({ lnum, pos + (I<1>()) }), line }));
                                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                                    parenlev = inplace(NEOS_PY_SUBTRACT, parenlev, I<1>());
                                    fs.attr("fields").method("pop");
                                    continue;
                                }
                                pseudomatch = (G<"_tokenize._pseudo">()).method("match", { line, pos });
                                if (pseudomatch.truthy())
                                {
                                    auto const items28 = unpack(pseudomatch.method("span", { I<1>() }), 2);
                                    start = items28[0];
                                    end = items28[1];
                                    // (an f-string's start: its prefix and quote (the expression matched the string))
                                    fstart = (G<"_tokenize._fstring_prefix">()).method("match", { line, start });
                                    if (fstart.truthy())
                                    {
                                        end = fstart.method("end");
                                    }
                                    auto const items29 = unpack(py_tuple({ py_tuple({ lnum, start }), py_tuple({ lnum, end }), end }), 3);
                                    spos = items29[0];
                                    epos = items29[1];
                                    pos = items29[2];
                                    if (start == end)
                                    {
                                        continue;
                                    }
                                    auto const items30 = unpack(py_tuple({ line[B<"slice">()(start, end, none())], line[start] }), 2);
                                    token = items30[0];
                                    initial = items30[1];
                                    fs = (fstrings.truthy() ? fstrings[I<-1>()] : none());
                                    if (((!fs.is_none()) && fs.attr("fields").truthy() && (parenlev == fs.attr("fields")[I<-1>()][I<0>()]) && (token == (S<":=">()))))
                                    {
                                        // (a replacement field's := : its format spec's start (an assignment expression is in parentheses))
                                        auto const items31 = unpack(py_tuple({ S<":">(), start + (I<1>()) }), 2);
                                        token = items31[0];
                                        end = items31[1];
                                        auto const items32 = unpack(py_tuple({ py_tuple({ lnum, end }), end }), 2);
                                        epos = items32[0];
                                        pos = items32[1];
                                    }
                                    if (((!fs.is_none()) && fs.attr("fields").truthy() && (parenlev == fs.attr("fields")[I<-1>()][I<0>()]) && py_tuple({ S<"}">(), S<":">(), S<"!">() }).contains(token)))
                                    {
                                        if (token == (S<"}">()))
                                        {
                                            // (the end of the replacement field)
                                            py_yield(py_tuple({ Mod<"token">().attr("OP"), token, spos, epos, line }));
                                            parenlev = inplace(NEOS_PY_SUBTRACT, parenlev, I<1>());
                                            fs.attr("fields").method("pop");
                                            continue;
                                        }
                                        if (token == (S<":">()))
                                        {
                                            // (its format spec)
                                            py_yield(py_tuple({ Mod<"token">().attr("OP"), token, spos, epos, line }));
                                            fs.attr("fields")[I<-1>()].set_item(I<1>(), boolean(true));
                                            continue;
                                        }
                                        py_yield(py_tuple({ Mod<"token">().attr("OP"), token, spos, epos, line }));
                                        continue;
                                    }
                                    if ((numchars.contains(initial) || ([&]() -> py { py const t = boolean(initial == (S<".">())); return !t.truthy() ? t : [&]() -> py { py const t = boolean(token != (S<".">())); return !t.truthy() ? t : boolean(token != (S<"...">())); }(); }()).truthy()))
                                    {
                                        py_yield(py_tuple({ Mod<"token">().attr("NUMBER"), token, spos, epos, line }));
                                    }
                                    else if ((S<"\r\n">()).contains(initial))
                                    {
                                        if (parenlev > (I<0>()))
                                        {
                                            if (extra_tokens.truthy())
                                            {
                                                py_yield(py_tuple({ Mod<"token">().attr("NL"), token, spos, epos, line }));
                                            }
                                        }
                                        else
                                        {
                                            py_yield(py_tuple({ Mod<"token">().attr("NEWLINE"), token, spos, epos, line }));
                                        }
                                    }
                                    else if (initial == (S<"#">()))
                                    {
                                        if (extra_tokens.truthy())
                                        {
                                            py_yield(py_tuple({ Mod<"token">().attr("COMMENT"), token, spos, epos, line }));
                                        }
                                    }
                                    else if (fstart.truthy())
                                    {
                                        // (an f-string: its start, then its literal text and replacement fields)
                                        quote = token.method("lstrip", { S<"fFrR">() });
                                        py_yield(py_tuple({ Mod<"token">().attr("FSTRING_START"), token, spos, epos, line }));
                                        fstrings.method("append", { (G<"_tokenize._FString">())(quote, boolean(token[B<"slice">()(none(), py{ token.size() } - py{ quote.size() }, none())].method("lower").contains((S<"r">()))), spos, line) });
                                        middle = none();
                                    }
                                    else if ((G<"_tokenize.triple_quoted">()).contains(token))
                                    {
                                        endprog = (G<"_tokenize._endprogs">())[token.method("lstrip", { S<"bBrRuU">() })];
                                        endmatch = endprog.method("match", { line, pos });
                                        if (endmatch.truthy())
                                        {
                                            pos = endmatch.method("end", { I<0>() });
                                            token = line[B<"slice">()(start, pos, none())];
                                            py_yield(py_tuple({ Mod<"token">().attr("STRING"), token, spos, py_tuple({ lnum, pos }), line }));
                                        }
                                        else
                                        {
                                            strstart = py_tuple({ lnum, start });
                                            contstr = line[B<"slice">()(start, none(), none())];
                                            contline = line;
                                            break;
                                        }
                                    }
                                    else if (((G<"_tokenize.single_quoted">()).contains(initial) || (G<"_tokenize.single_quoted">()).contains(token[B<"slice">()(none(), I<2>(), none())]) || (G<"_tokenize.single_quoted">()).contains(token[B<"slice">()(none(), I<3>(), none())])))
                                    {
                                        if (token[I<-1>()] == (S<"\n">()))
                                        {
                                            strstart = py_tuple({ lnum, start });
                                            endprog = [&]() -> py { py const t = (G<"_tokenize._endprogs">()).method("get", { initial }); return t.truthy() ? t : [&]() -> py { py const t = (G<"_tokenize._endprogs">()).method("get", { token[I<1>()] }); return t.truthy() ? t : (G<"_tokenize._endprogs">()).method("get", { token[I<2>()] }); }(); }();
                                            auto const items33 = unpack(py_tuple({ line[B<"slice">()(start, none(), none())], I<1>() }), 2);
                                            contstr = items33[0];
                                            needcont = items33[1];
                                            contline = line;
                                            break;
                                        }
                                        else
                                        {
                                            py_yield(py_tuple({ Mod<"token">().attr("STRING"), token, spos, epos, line }));
                                        }
                                    }
                                    else if (initial.method("isidentifier").truthy())
                                    {
                                        py_yield(py_tuple({ Mod<"token">().attr("NAME"), token, spos, epos, line }));
                                    }
                                    else if (initial == (S<"\\">()))
                                    {
                                        continued = I<1>();
                                    }
                                    else
                                    {
                                        if ((S<"([{">()).contains(initial))
                                        {
                                            parenlev = inplace(NEOS_PY_ADD, parenlev, I<1>());
                                        }
                                        else if ((S<")]}">()).contains(initial))
                                        {
                                            parenlev = inplace(NEOS_PY_SUBTRACT, parenlev, I<1>());
                                        }
                                        py_yield(py_tuple({ (extra_tokens.truthy() ? Mod<"token">().attr("OP") : (Mod<"token">().attr("EXACT_TOKEN_TYPES")).method("get", { token, Mod<"token">().attr("OP") })), token, spos, epos, line }));
                                    }
                                }
                                else
                                {
                                    while ((S<" \t\f">()).contains(line[pos]))
                                    {
                                        pos = inplace(NEOS_PY_ADD, pos, I<1>());
                                    }
                                    c = line[pos];
                                    if ((S<"'\"">()).contains(c))
                                    {
                                        py_raise((G<"_tokenize._syntax_error">())((S<"unterminated string literal (detected at line %d)">()) % lnum, lnum, pos + (I<1>()), line));
                                    }
                                    if (!c.method("isprintable").truthy())
                                    {
                                        py_raise((G<"_tokenize._syntax_error">())((S<"invalid non-printable character U+%04X">()) % (B<"ord">())(c), lnum, pos + (I<1>()), line));
                                    }
                                    // (another character: an operator (the parser's error))
                                    py_yield(py_tuple({ Mod<"token">().attr("OP"), c, py_tuple({ lnum, pos }), py_tuple({ lnum, pos + (I<1>()) }), line }));
                                    pos = inplace(NEOS_PY_ADD, pos, I<1>());
                                }
                            }
                        }
                        if ((last_line.truthy() && (!(S<"\r\n">()).contains(last_line[I<-1>()])) && (!last_line.method("strip").method("startswith", { S<"#">() }).truthy())))
                        {
                            py_yield(py_tuple({ Mod<"token">().attr("NEWLINE"), S<"">(), py_tuple({ lnum - (I<1>()), py{ last_line.size() } }), py_tuple({ lnum - (I<1>()), py{ last_line.size() } + (I<1>()) }), S<"">() }));
                        }
                        for (py const& item : iterate{ indents[B<"slice">()(I<1>(), none(), none())] })
                        {
                            indent = item;
                            py_yield(py_tuple({ Mod<"token">().attr("DEDENT"), S<"">(), py_tuple({ lnum, I<0>() }), py_tuple({ lnum, I<0>() }), S<"">() }));
                        }
                        py_yield(py_tuple({ Mod<"token">().attr("ENDMARKER"), S<"">(), py_tuple({ lnum, I<0>() }), py_tuple({ lnum, I<0>() }), S<"">() }));
                        return none();
                    });
                }));
        }

        bool _tokenize_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make__tokenize();
                }
                if (auto const doc = tokenize_c_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("NUMBER", import_attribute("token", "NUMBER").release());
                aAttributes.emplace_back("NL", import_attribute("token", "NL").release());
                aAttributes.emplace_back("NEWLINE", import_attribute("token", "NEWLINE").release());
                aAttributes.emplace_back("COMMENT", import_attribute("token", "COMMENT").release());
                aAttributes.emplace_back("STRING", import_attribute("token", "STRING").release());
                aAttributes.emplace_back("NAME", import_attribute("token", "NAME").release());
                aAttributes.emplace_back("OP", import_attribute("token", "OP").release());
                aAttributes.emplace_back("ERRORTOKEN", import_attribute("token", "ERRORTOKEN").release());
                aAttributes.emplace_back("INDENT", import_attribute("token", "INDENT").release());
                aAttributes.emplace_back("DEDENT", import_attribute("token", "DEDENT").release());
                aAttributes.emplace_back("ENDMARKER", import_attribute("token", "ENDMARKER").release());
                aAttributes.emplace_back("FSTRING_START", import_attribute("token", "FSTRING_START").release());
                aAttributes.emplace_back("FSTRING_MIDDLE", import_attribute("token", "FSTRING_MIDDLE").release());
                aAttributes.emplace_back("FSTRING_END", import_attribute("token", "FSTRING_END").release());
                aAttributes.emplace_back("EXACT_TOKEN_TYPES", import_attribute("token", "EXACT_TOKEN_TYPES").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("_tokenize", _tokenize_module);
    }
}
