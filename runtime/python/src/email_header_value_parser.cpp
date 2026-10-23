/*
  email_header_value_parser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/_header_value_parser.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email._header_value_parser (CPython 3.13's Lib/email/_header_value_parser.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_header_value_parser_docs.hpp"

namespace neos::python_runtime::detail::email__header_value_parser_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email._header_value_parser.name"))
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
                py_raise("SystemError", std::string{ "email._header_value_parser: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email._header_value_parser.name">()))
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
            if (modules_imported("email._header_value_parser"))
                return py{ fn_import("email._header_value_parser") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email._header_value_parser" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email._header_value_parser", email_header_value_parser_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email._header_value_parser (Lib/email/_header_value_parser.py)

        void make_email__header_value_parser()
        {
            static module_writer const& w = writer("email._header_value_parser");
            // 
            // Useful constants and functions
            // 
            define("email._header_value_parser", "_WSP", S<" \t">());
            define("email._header_value_parser", "WSP", (B<"set">())(G<"email._header_value_parser._WSP">()));
            define("email._header_value_parser", "CFWS_LEADER", (G<"email._header_value_parser.WSP">()) | (B<"set">())(S<"(">()));
            define("email._header_value_parser", "SPECIALS", (B<"set">())(S<"()<>@,:;.\\\"[]">()));
            define("email._header_value_parser", "ATOM_ENDS", (G<"email._header_value_parser.SPECIALS">()) | (G<"email._header_value_parser.WSP">()));
            define("email._header_value_parser", "DOT_ATOM_ENDS", (G<"email._header_value_parser.ATOM_ENDS">()) - (B<"set">())(S<".">()));
            // '.', '"', and '(' do not end phrases in order to support obs-phrase
            define("email._header_value_parser", "PHRASE_ENDS", (G<"email._header_value_parser.SPECIALS">()) - (B<"set">())(S<".\"(">()));
            define("email._header_value_parser", "TSPECIALS", ((G<"email._header_value_parser.SPECIALS">()) | (B<"set">())(S<"/?=">())) - (B<"set">())(S<".">()));
            define("email._header_value_parser", "TOKEN_ENDS", (G<"email._header_value_parser.TSPECIALS">()) | (G<"email._header_value_parser.WSP">()));
            define("email._header_value_parser", "ASPECIALS", (G<"email._header_value_parser.TSPECIALS">()) | (B<"set">())(S<"*'%">()));
            define("email._header_value_parser", "ATTRIBUTE_ENDS", (G<"email._header_value_parser.ASPECIALS">()) | (G<"email._header_value_parser.WSP">()));
            define("email._header_value_parser", "EXTENDED_ATTRIBUTE_ENDS", (G<"email._header_value_parser.ATTRIBUTE_ENDS">()) - (B<"set">())(S<"%">()));
            define("email._header_value_parser", "NLSET", set_of({ S<"\n">(), S<"\r">() }));
            define("email._header_value_parser", "SPECIALSNL", (G<"email._header_value_parser.SPECIALS">()) | (G<"email._header_value_parser.NLSET">()));
            define("email._header_value_parser", "make_quoted_pairs", w.function("make_quoted_pairs", "make_quoted_pairs", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    return (B<"str">())(value).method("replace", { S<"\\">(), S<"\\\\">() }).method("replace", { S<"\"">(), S<"\\\"">() });
                }));

            define("email._header_value_parser", "make_parenthesis_pairs", w.function("make_parenthesis_pairs", "make_parenthesis_pairs", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    return (B<"str">())(value).method("replace", { S<"\\">(), S<"\\\\">() }).method("replace", { S<"(">(), S<"\\(">() }).method("replace", { S<")">(), S<"\\)">() });
                }));

            define("email._header_value_parser", "quote_string", w.function("quote_string", "quote_string", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py escaped;
                    escaped = (G<"email._header_value_parser.make_quoted_pairs">())(value);
                    return S(std::string{ "\"" } + escaped.str() + "\"");
                }));

            // Match a RFC 2047 word, looks like =?utf-8?q?someword?=
            define("email._header_value_parser", "rfc2047_matcher", (Mod<"re">()).method("compile", { S<"\n   =\\?            # literal =?\n   [^?]*          # charset\n   \\?             # literal ?\n   [qQbB]         # literal 'q' or 'b', case insensitive\n   \\?             # literal ?\n  .*?             # encoded word\n  \\?=             # literal ?=\n">(), (Mod<"re">().attr("VERBOSE")) | (Mod<"re">().attr("MULTILINE")) }));
            // 
            // TokenList and its subclasses
            // 
            define("email._header_value_parser", "TokenList", w.make_class("TokenList", { B<"list">() }, {
                { "token_type", none() },
                { "syntactic_break", boolean(true) },
                { "ew_combine_allowed", boolean(true) },
                { "__init__", w.method("TokenList", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex((B<"super">()(G<"email._header_value_parser.TokenList">(), self)).attr("__init__"), {  }, args, kw);
                        self.set_attr("defects", py_list());
                        return none();
                    }, true, true) },
                { "__str__", w.method("TokenList", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"">()).method("join", { [&]() -> py { py const comprehension1 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension1.method("append", { (B<"str">())(x) }); } return comprehension1; }() });
                    }) },
                { "__repr__", w.method("TokenList", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"{}({})">()).method("format", { self.attr("__class__").attr("__name__"), (B<"super">()(G<"email._header_value_parser.TokenList">(), self)).method("__repr__") });
                    }) },
                { "value", B<"property">()(w.method("TokenList", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"">()).method("join", { [&]() -> py { py const comprehension2 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!x.attr("value").truthy()) continue; comprehension2.method("append", { x.attr("value") }); } return comprehension2; }() });
                    })) },
                { "all_defects", B<"property">()(w.method("TokenList", "all_defects", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"sum">())([&]() -> py { py const comprehension3 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension3.method("append", { x.attr("all_defects") }); } return comprehension3; }(), self.attr("defects"));
                    })) },
                { "startswith_fws", w.method("TokenList", "startswith_fws", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<0>()].method("startswith_fws");
                    }) },
                { "as_ew_allowed", B<"property">()(w.method("TokenList", "as_ew_allowed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"all">())([&]() -> py { py const comprehension4 = py_list(); py part; for (py const& item : iterate{ self }) { part = item; comprehension4.method("append", { part.attr("as_ew_allowed") }); } return comprehension4; }());
                    })) },
                { "comments", B<"property">()(w.method("TokenList", "comments", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py comments;
                        py token;
                        comments = py_list();
                        for (py const& item : iterate{ self })
                        {
                            token = item;
                            comments.method("extend", { token.attr("comments") });
                        }
                        return comments;
                    })) },
                { "fold", w.method("TokenList", "fold", { py_keyword("policy") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py policy = a[1];
                        return (G<"email._header_value_parser._refold_parse_tree">()).call({ self }, { { "policy", policy } });
                    }) },
                { "pprint", w.method("TokenList", "pprint", { py_positional("indent", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py indent = a[1];
                        (B<"print">())(self.method("ppstr", {  }, { { "indent", indent } }));
                        return none();
                    }) },
                { "ppstr", w.method("TokenList", "ppstr", { py_positional("indent", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py indent = a[1];
                        return (S<"\n">()).method("join", { self.method("_pp", {  }, { { "indent", indent } }) });
                    }) },
                { "_pp", w.method("TokenList", "_pp", { py_positional("indent", S<"">()) }, [](arguments a) -> py
                    {
                        return make_native_generator("_pp", "TokenList._pp", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py indent = a[1];
                            py extra;
                            py token;
                            py_yield((S<"{}{}/{}(">()).method("format", { indent, self.attr("__class__").attr("__name__"), self.attr("token_type") }));
                            for (py const& item : iterate{ self })
                            {
                                token = item;
                                if (!has_attr(token, "_pp"))
                                {
                                    py_yield(indent + (S<"    !! invalid element in token list: {!r}">()).method("format", { token }));
                                }
                                else
                                {
                                    yield_from(token.method("_pp", { indent + (S<"    ">()) }));
                                }
                            }
                            if (self.attr("defects").truthy())
                            {
                                extra = (S<" Defects: {}">()).method("format", { self.attr("defects") });
                            }
                            else
                            {
                                extra = S<"">();
                            }
                            py_yield((S<"{}){}">()).method("format", { indent, extra }));
                            return none();
                        });
                    }) }, }));

            define("email._header_value_parser", "WhiteSpaceTokenList", w.make_class("WhiteSpaceTokenList", { G<"email._header_value_parser.TokenList">() }, {
                { "value", B<"property">()(w.method("WhiteSpaceTokenList", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<" ">();
                    })) },
                { "comments", B<"property">()(w.method("WhiteSpaceTokenList", "comments", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension7 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!(x.attr("token_type") == (S<"comment">()))) continue; comprehension7.method("append", { x.attr("content") }); } return comprehension7; }();
                    })) }, }));

            define("email._header_value_parser", "UnstructuredTokenList", w.make_class("UnstructuredTokenList", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"unstructured">() }, }));

            define("email._header_value_parser", "Phrase", w.make_class("Phrase", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"phrase">() }, }));

            define("email._header_value_parser", "Word", w.make_class("Word", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"word">() }, }));

            define("email._header_value_parser", "CFWSList", w.make_class("CFWSList", { G<"email._header_value_parser.WhiteSpaceTokenList">() }, {
                { "token_type", S<"cfws">() }, }));

            define("email._header_value_parser", "Atom", w.make_class("Atom", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"atom">() }, }));

            define("email._header_value_parser", "Token", w.make_class("Token", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"token">() },
                { "encode_as_ew", boolean(false) }, }));

            define("email._header_value_parser", "EncodedWord", w.make_class("EncodedWord", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"encoded-word">() },
                { "cte", none() },
                { "charset", none() },
                { "lang", none() }, }));

            define("email._header_value_parser", "QuotedString", w.make_class("QuotedString", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"quoted-string">() },
                { "content", B<"property">()(w.method("QuotedString", "content", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py x;
                        for (py const& item : iterate{ self })
                        {
                            x = item;
                            if (x.attr("token_type") == (S<"bare-quoted-string">()))
                            {
                                return x.attr("value");
                            }
                        }
                        return none();
                    })) },
                { "quoted_value", B<"property">()(w.method("QuotedString", "quoted_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py res;
                        py x;
                        res = py_list();
                        for (py const& item : iterate{ self })
                        {
                            x = item;
                            if (x.attr("token_type") == (S<"bare-quoted-string">()))
                            {
                                res.method("append", { (B<"str">())(x) });
                            }
                            else
                            {
                                res.method("append", { x.attr("value") });
                            }
                        }
                        return (S<"">()).method("join", { res });
                    })) },
                { "stripped_value", B<"property">()(w.method("QuotedString", "stripped_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token;
                        for (py const& item : iterate{ self })
                        {
                            token = item;
                            if (token.attr("token_type") == (S<"bare-quoted-string">()))
                            {
                                return token.attr("value");
                            }
                        }
                        return none();
                    })) }, }));

            define("email._header_value_parser", "BareQuotedString", w.make_class("BareQuotedString", { G<"email._header_value_parser.QuotedString">() }, {
                { "token_type", S<"bare-quoted-string">() },
                { "__str__", w.method("BareQuotedString", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"email._header_value_parser.quote_string">())((S<"">()).method("join", { [&]() -> py { py const comprehension11 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension11.method("append", { (B<"str">())(x) }); } return comprehension11; }() }));
                    }) },
                { "value", B<"property">()(w.method("BareQuotedString", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"">()).method("join", { [&]() -> py { py const comprehension12 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension12.method("append", { (B<"str">())(x) }); } return comprehension12; }() });
                    })) }, }));

            define("email._header_value_parser", "Comment", w.make_class("Comment", { G<"email._header_value_parser.WhiteSpaceTokenList">() }, {
                { "token_type", S<"comment">() },
                { "__str__", w.method("Comment", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"">()).method("join", { (B<"sum">())(py_list({ py_list({ S<"(">() }), [&]() -> py { py const comprehension13 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension13.method("append", { self.method("quote", { x }) }); } return comprehension13; }(), py_list({ S<")">() }) }), py_list()) });
                    }) },
                { "quote", w.method("Comment", "quote", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (value.attr("token_type") == (S<"comment">()))
                        {
                            return (B<"str">())(value);
                        }
                        return (B<"str">())(value).method("replace", { S<"\\">(), S<"\\\\">() }).method("replace", { S<"(">(), S<"\\(">() }).method("replace", { S<")">(), S<"\\)">() });
                    }) },
                { "content", B<"property">()(w.method("Comment", "content", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"">()).method("join", { [&]() -> py { py const comprehension14 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; comprehension14.method("append", { (B<"str">())(x) }); } return comprehension14; }() });
                    })) },
                { "comments", B<"property">()(w.method("Comment", "comments", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_list({ self.attr("content") });
                    })) }, }));

            define("email._header_value_parser", "AddressList", w.make_class("AddressList", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"address-list">() },
                { "addresses", B<"property">()(w.method("AddressList", "addresses", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension15 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!(x.attr("token_type") == (S<"address">()))) continue; comprehension15.method("append", { x }); } return comprehension15; }();
                    })) },
                { "mailboxes", B<"property">()(w.method("AddressList", "mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"sum">())([&]() -> py { py const comprehension16 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!(x.attr("token_type") == (S<"address">()))) continue; comprehension16.method("append", { x.attr("mailboxes") }); } return comprehension16; }(), py_list());
                    })) },
                { "all_mailboxes", B<"property">()(w.method("AddressList", "all_mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"sum">())([&]() -> py { py const comprehension17 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!(x.attr("token_type") == (S<"address">()))) continue; comprehension17.method("append", { x.attr("all_mailboxes") }); } return comprehension17; }(), py_list());
                    })) }, }));

            define("email._header_value_parser", "Address", w.make_class("Address", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"address">() },
                { "display_name", B<"property">()(w.method("Address", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<0>()].attr("token_type") == (S<"group">()))
                        {
                            return self[I<0>()].attr("display_name");
                        }
                        return none();
                    })) },
                { "mailboxes", B<"property">()(w.method("Address", "mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<0>()].attr("token_type") == (S<"mailbox">()))
                        {
                            return py_list({ self[I<0>()] });
                        }
                        else if (self[I<0>()].attr("token_type") == (S<"invalid-mailbox">()))
                        {
                            return py_list();
                        }
                        return self[I<0>()].attr("mailboxes");
                    })) },
                { "all_mailboxes", B<"property">()(w.method("Address", "all_mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<0>()].attr("token_type") == (S<"mailbox">()))
                        {
                            return py_list({ self[I<0>()] });
                        }
                        else if (self[I<0>()].attr("token_type") == (S<"invalid-mailbox">()))
                        {
                            return py_list({ self[I<0>()] });
                        }
                        return self[I<0>()].attr("all_mailboxes");
                    })) }, }));

            define("email._header_value_parser", "MailboxList", w.make_class("MailboxList", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"mailbox-list">() },
                { "mailboxes", B<"property">()(w.method("MailboxList", "mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension18 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!(x.attr("token_type") == (S<"mailbox">()))) continue; comprehension18.method("append", { x }); } return comprehension18; }();
                    })) },
                { "all_mailboxes", B<"property">()(w.method("MailboxList", "all_mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension19 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!py_tuple({ S<"mailbox">(), S<"invalid-mailbox">() }).contains(x.attr("token_type"))) continue; comprehension19.method("append", { x }); } return comprehension19; }();
                    })) }, }));

            define("email._header_value_parser", "GroupList", w.make_class("GroupList", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"group-list">() },
                { "mailboxes", B<"property">()(w.method("GroupList", "mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((!self.truthy()) || (self[I<0>()].attr("token_type") != (S<"mailbox-list">()))))
                        {
                            return py_list();
                        }
                        return self[I<0>()].attr("mailboxes");
                    })) },
                { "all_mailboxes", B<"property">()(w.method("GroupList", "all_mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((!self.truthy()) || (self[I<0>()].attr("token_type") != (S<"mailbox-list">()))))
                        {
                            return py_list();
                        }
                        return self[I<0>()].attr("all_mailboxes");
                    })) }, }));

            define("email._header_value_parser", "Group", w.make_class("Group", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"group">() },
                { "mailboxes", B<"property">()(w.method("Group", "mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<2>()].attr("token_type") != (S<"group-list">()))
                        {
                            return py_list();
                        }
                        return self[I<2>()].attr("mailboxes");
                    })) },
                { "all_mailboxes", B<"property">()(w.method("Group", "all_mailboxes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<2>()].attr("token_type") != (S<"group-list">()))
                        {
                            return py_list();
                        }
                        return self[I<2>()].attr("all_mailboxes");
                    })) },
                { "display_name", B<"property">()(w.method("Group", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<0>()].attr("display_name");
                    })) }, }));

            define("email._header_value_parser", "NameAddr", w.make_class("NameAddr", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"name-addr">() },
                { "display_name", B<"property">()(w.method("NameAddr", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (py{ self.size() } == (I<1>()))
                        {
                            return none();
                        }
                        return self[I<0>()].attr("display_name");
                    })) },
                { "local_part", B<"property">()(w.method("NameAddr", "local_part", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<-1>()].attr("local_part");
                    })) },
                { "domain", B<"property">()(w.method("NameAddr", "domain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<-1>()].attr("domain");
                    })) },
                { "route", B<"property">()(w.method("NameAddr", "route", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<-1>()].attr("route");
                    })) },
                { "addr_spec", B<"property">()(w.method("NameAddr", "addr_spec", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<-1>()].attr("addr_spec");
                    })) }, }));

            define("email._header_value_parser", "AngleAddr", w.make_class("AngleAddr", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"angle-addr">() },
                { "local_part", B<"property">()(w.method("AngleAddr", "local_part", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py x;
                        for (py const& item : iterate{ self })
                        {
                            x = item;
                            if (x.attr("token_type") == (S<"addr-spec">()))
                            {
                                return x.attr("local_part");
                            }
                        }
                        return none();
                    })) },
                { "domain", B<"property">()(w.method("AngleAddr", "domain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py x;
                        for (py const& item : iterate{ self })
                        {
                            x = item;
                            if (x.attr("token_type") == (S<"addr-spec">()))
                            {
                                return x.attr("domain");
                            }
                        }
                        return none();
                    })) },
                { "route", B<"property">()(w.method("AngleAddr", "route", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py x;
                        for (py const& item : iterate{ self })
                        {
                            x = item;
                            if (x.attr("token_type") == (S<"obs-route">()))
                            {
                                return x.attr("domains");
                            }
                        }
                        return none();
                    })) },
                { "addr_spec", B<"property">()(w.method("AngleAddr", "addr_spec", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py x;
                        bool broke23 = false;
                        for (py const& item : iterate{ self })
                        {
                            x = item;
                            if (x.attr("token_type") == (S<"addr-spec">()))
                            {
                                if (x.attr("local_part").truthy())
                                {
                                    return x.attr("addr_spec");
                                }
                                else
                                {
                                    return (G<"email._header_value_parser.quote_string">())(x.attr("local_part")) + x.attr("addr_spec");
                                }
                            }
                        }
                        if (!broke23)
                        {
                            return S<"<>">();
                        }
                        return none();
                    })) }, }));

            define("email._header_value_parser", "ObsRoute", w.make_class("ObsRoute", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"obs-route">() },
                { "domains", B<"property">()(w.method("ObsRoute", "domains", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension24 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!(x.attr("token_type") == (S<"domain">()))) continue; comprehension24.method("append", { x.attr("domain") }); } return comprehension24; }();
                    })) }, }));

            define("email._header_value_parser", "Mailbox", w.make_class("Mailbox", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"mailbox">() },
                { "display_name", B<"property">()(w.method("Mailbox", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<0>()].attr("token_type") == (S<"name-addr">()))
                        {
                            return self[I<0>()].attr("display_name");
                        }
                        return none();
                    })) },
                { "local_part", B<"property">()(w.method("Mailbox", "local_part", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<0>()].attr("local_part");
                    })) },
                { "domain", B<"property">()(w.method("Mailbox", "domain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<0>()].attr("domain");
                    })) },
                { "route", B<"property">()(w.method("Mailbox", "route", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<0>()].attr("token_type") == (S<"name-addr">()))
                        {
                            return self[I<0>()].attr("route");
                        }
                        return none();
                    })) },
                { "addr_spec", B<"property">()(w.method("Mailbox", "addr_spec", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<0>()].attr("addr_spec");
                    })) }, }));

            define("email._header_value_parser", "InvalidMailbox", w.make_class("InvalidMailbox", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"invalid-mailbox">() },
                { "display_name", B<"property">()(w.method("InvalidMailbox", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    })) },
                { "local_part", (B<"property">()(w.method("InvalidMailbox", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }))) },
                { "domain", (B<"property">()(w.method("InvalidMailbox", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }))) },
                { "route", (B<"property">()(w.method("InvalidMailbox", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }))) },
                { "addr_spec", (B<"property">()(w.method("InvalidMailbox", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }))) }, }));

            define("email._header_value_parser", "Domain", w.make_class("Domain", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"domain">() },
                { "as_ew_allowed", boolean(false) },
                { "domain", B<"property">()(w.method("Domain", "domain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"">()).method("join", { (B<"super">()(G<"email._header_value_parser.Domain">(), self)).attr("value").method("split") });
                    })) }, }));

            define("email._header_value_parser", "DotAtom", w.make_class("DotAtom", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"dot-atom">() }, }));

            define("email._header_value_parser", "DotAtomText", w.make_class("DotAtomText", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"dot-atom-text">() },
                { "as_ew_allowed", boolean(true) }, }));

            define("email._header_value_parser", "NoFoldLiteral", w.make_class("NoFoldLiteral", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"no-fold-literal">() },
                { "as_ew_allowed", boolean(false) }, }));

            define("email._header_value_parser", "AddrSpec", w.make_class("AddrSpec", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"addr-spec">() },
                { "as_ew_allowed", boolean(false) },
                { "local_part", B<"property">()(w.method("AddrSpec", "local_part", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self[I<0>()].attr("local_part");
                    })) },
                { "domain", B<"property">()(w.method("AddrSpec", "domain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (py{ self.size() } < (I<3>()))
                        {
                            return none();
                        }
                        return self[I<-1>()].attr("domain");
                    })) },
                { "value", B<"property">()(w.method("AddrSpec", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (py{ self.size() } < (I<3>()))
                        {
                            return self[I<0>()].attr("value");
                        }
                        return (self[I<0>()].attr("value").method("rstrip") + self[I<1>()].attr("value")) + self[I<2>()].attr("value").method("lstrip");
                    })) },
                { "addr_spec", B<"property">()(w.method("AddrSpec", "addr_spec", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lp;
                        py nameset;
                        nameset = (B<"set">())(self.attr("local_part"));
                        if (py{ nameset.size() } > py{ (nameset - (G<"email._header_value_parser.DOT_ATOM_ENDS">())).size() })
                        {
                            lp = (G<"email._header_value_parser.quote_string">())(self.attr("local_part"));
                        }
                        else
                        {
                            lp = self.attr("local_part");
                        }
                        if (!self.attr("domain").is_none())
                        {
                            return (lp + (S<"@">())) + self.attr("domain");
                        }
                        return lp;
                    })) }, }));

            define("email._header_value_parser", "ObsLocalPart", w.make_class("ObsLocalPart", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"obs-local-part">() },
                { "as_ew_allowed", boolean(false) }, }));

            define("email._header_value_parser", "DisplayName", w.make_class("DisplayName", { G<"email._header_value_parser.Phrase">() }, {
                { "token_type", S<"display-name">() },
                { "ew_combine_allowed", boolean(false) },
                { "display_name", B<"property">()(w.method("DisplayName", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py res;
                        res = (G<"email._header_value_parser.TokenList">())(self);
                        if (py{ res.size() } == (I<0>()))
                        {
                            return res.attr("value");
                        }
                        if (res[I<0>()].attr("token_type") == (S<"cfws">()))
                        {
                            res.method("pop", { I<0>() });
                        }
                        else if ((isinstance(res[I<0>()], G<"email._header_value_parser.TokenList">()) && (res[I<0>()][I<0>()].attr("token_type") == (S<"cfws">()))))
                        {
                            res.set_item(I<0>(), (G<"email._header_value_parser.TokenList">())(res[I<0>()][B<"slice">()(I<1>(), none(), none())]));
                        }
                        if (res[I<-1>()].attr("token_type") == (S<"cfws">()))
                        {
                            res.method("pop");
                        }
                        else if ((isinstance(res[I<-1>()], G<"email._header_value_parser.TokenList">()) && (res[I<-1>()][I<-1>()].attr("token_type") == (S<"cfws">()))))
                        {
                            res.set_item(I<-1>(), (G<"email._header_value_parser.TokenList">())(res[I<-1>()][B<"slice">()(none(), I<-1>(), none())]));
                        }
                        return res.attr("value");
                    })) },
                { "value", B<"property">()(w.method("DisplayName", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py post;
                        py pre;
                        py quote;
                        py x;
                        quote = boolean(false);
                        if (self.attr("defects").truthy())
                        {
                            quote = boolean(true);
                        }
                        else
                        {
                            for (py const& item : iterate{ self })
                            {
                                x = item;
                                if (x.attr("token_type") == (S<"quoted-string">()))
                                {
                                    quote = boolean(true);
                                }
                            }
                        }
                        if (((py{ self.size() } != (I<0>())) && quote.truthy()))
                        {
                            py const value26 = S<"">();
                            pre = value26;
                            post = value26;
                            if (((self[I<0>()].attr("token_type") == (S<"cfws">())) || ([&]() -> py { py const t = boolean(isinstance(self[I<0>()], G<"email._header_value_parser.TokenList">())); return !t.truthy() ? t : boolean(self[I<0>()][I<0>()].attr("token_type") == (S<"cfws">())); }()).truthy()))
                            {
                                pre = S<" ">();
                            }
                            if (((self[I<-1>()].attr("token_type") == (S<"cfws">())) || ([&]() -> py { py const t = boolean(isinstance(self[I<-1>()], G<"email._header_value_parser.TokenList">())); return !t.truthy() ? t : boolean(self[I<-1>()][I<-1>()].attr("token_type") == (S<"cfws">())); }()).truthy()))
                            {
                                post = S<" ">();
                            }
                            return (pre + (G<"email._header_value_parser.quote_string">())(self.attr("display_name"))) + post;
                        }
                        else
                        {
                            return (B<"super">()(G<"email._header_value_parser.DisplayName">(), self)).attr("value");
                        }
                        return none();
                    })) }, }));

            define("email._header_value_parser", "LocalPart", w.make_class("LocalPart", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"local-part">() },
                { "as_ew_allowed", boolean(false) },
                { "value", B<"property">()(w.method("LocalPart", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self[I<0>()].attr("token_type") == (S<"quoted-string">()))
                        {
                            return self[I<0>()].attr("quoted_value");
                        }
                        else
                        {
                            return self[I<0>()].attr("value");
                        }
                        return none();
                    })) },
                { "local_part", B<"property">()(w.method("LocalPart", "local_part", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py is_tl;
                        py last;
                        py last_is_tl;
                        py res;
                        py tok;
                        res = py_list({ G<"email._header_value_parser.DOT">() });
                        last = G<"email._header_value_parser.DOT">();
                        last_is_tl = boolean(false);
                        for (py const& item : iterate{ self[I<0>()] + py_list({ G<"email._header_value_parser.DOT">() }) })
                        {
                            tok = item;
                            if (tok.attr("token_type") == (S<"cfws">()))
                            {
                                continue;
                            }
                            if ((last_is_tl.truthy() && (tok.attr("token_type") == (S<"dot">())) && last.truthy() && (last[I<-1>()].attr("token_type") == (S<"cfws">()))))
                            {
                                res.set_item(I<-1>(), (G<"email._header_value_parser.TokenList">())(last[B<"slice">()(none(), I<-1>(), none())]));
                            }
                            is_tl = boolean(isinstance(tok, G<"email._header_value_parser.TokenList">()));
                            if ((is_tl.truthy() && (last.attr("token_type") == (S<"dot">())) && tok.truthy() && (tok[I<0>()].attr("token_type") == (S<"cfws">()))))
                            {
                                res.method("append", { (G<"email._header_value_parser.TokenList">())(tok[B<"slice">()(I<1>(), none(), none())]) });
                            }
                            else
                            {
                                res.method("append", { tok });
                            }
                            last = res[I<-1>()];
                            last_is_tl = is_tl;
                        }
                        res = (G<"email._header_value_parser.TokenList">())(res[B<"slice">()(I<1>(), I<-1>(), none())]);
                        return res.attr("value");
                    })) }, }));

            define("email._header_value_parser", "DomainLiteral", w.make_class("DomainLiteral", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"domain-literal">() },
                { "as_ew_allowed", boolean(false) },
                { "domain", B<"property">()(w.method("DomainLiteral", "domain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"">()).method("join", { (B<"super">()(G<"email._header_value_parser.DomainLiteral">(), self)).attr("value").method("split") });
                    })) },
                { "ip", B<"property">()(w.method("DomainLiteral", "ip", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py x;
                        for (py const& item : iterate{ self })
                        {
                            x = item;
                            if (x.attr("token_type") == (S<"ptext">()))
                            {
                                return x.attr("value");
                            }
                        }
                        return none();
                    })) }, }));

            define("email._header_value_parser", "MIMEVersion", w.make_class("MIMEVersion", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"mime-version">() },
                { "major", none() },
                { "minor", none() }, }));

            define("email._header_value_parser", "Parameter", w.make_class("Parameter", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"parameter">() },
                { "sectioned", boolean(false) },
                { "extended", boolean(false) },
                { "charset", S<"us-ascii">() },
                { "section_number", B<"property">()(w.method("Parameter", "section_number", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (self.attr("sectioned").truthy() ? self[I<1>()].attr("number") : I<0>());
                    })) },
                { "param_value", B<"property">()(w.method("Parameter", "param_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token;
                        for (py const& item : iterate{ self })
                        {
                            token = item;
                            if (token.attr("token_type") == (S<"value">()))
                            {
                                return token.attr("stripped_value");
                            }
                            if (token.attr("token_type") == (S<"quoted-string">()))
                            {
                                for (py const& item : iterate{ token })
                                {
                                    token = item;
                                    if (token.attr("token_type") == (S<"bare-quoted-string">()))
                                    {
                                        for (py const& item : iterate{ token })
                                        {
                                            token = item;
                                            if (token.attr("token_type") == (S<"value">()))
                                            {
                                                return token.attr("stripped_value");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        return S<"">();
                    })) }, }));

            define("email._header_value_parser", "InvalidParameter", w.make_class("InvalidParameter", { G<"email._header_value_parser.Parameter">() }, {
                { "token_type", S<"invalid-parameter">() }, }));

            define("email._header_value_parser", "Attribute", w.make_class("Attribute", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"attribute">() },
                { "stripped_value", B<"property">()(w.method("Attribute", "stripped_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token;
                        for (py const& item : iterate{ self })
                        {
                            token = item;
                            if (token.attr("token_type").method("endswith", { S<"attrtext">() }).truthy())
                            {
                                return token.attr("value");
                            }
                        }
                        return none();
                    })) }, }));

            define("email._header_value_parser", "Section", w.make_class("Section", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"section">() },
                { "number", none() }, }));

            define("email._header_value_parser", "Value", w.make_class("Value", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"value">() },
                { "stripped_value", B<"property">()(w.method("Value", "stripped_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token;
                        token = self[I<0>()];
                        if (token.attr("token_type") == (S<"cfws">()))
                        {
                            token = self[I<1>()];
                        }
                        if (token.attr("token_type").method("endswith", { py_tuple({ S<"quoted-string">(), S<"attribute">(), S<"extended-attribute">() }) }).truthy())
                        {
                            return token.attr("stripped_value");
                        }
                        return self.attr("value");
                    })) }, }));

            define("email._header_value_parser", "MimeParameters", w.make_class("MimeParameters", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"mime-parameters">() },
                { "syntactic_break", boolean(false) },
                { "params", B<"property">()(w.method("MimeParameters", "params", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("params", "MimeParameters.params", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py charset;
                            py first_param;
                            py i;
                            py name;
                            py param;
                            py params;
                            py parts;
                            py section_number;
                            py token;
                            py value;
                            py value_parts;
                            params = py_dict();
                            for (py const& item : iterate{ self })
                            {
                                token = item;
                                if (!token.attr("token_type").method("endswith", { S<"parameter">() }).truthy())
                                {
                                    continue;
                                }
                                if (token[I<0>()].attr("token_type") != (S<"attribute">()))
                                {
                                    continue;
                                }
                                name = token[I<0>()].attr("value").method("strip");
                                if (!params.contains(name))
                                {
                                    params.set_item(name, py_list());
                                }
                                params[name].method("append", { py_tuple({ token.attr("section_number"), token }) });
                            }
                            for (py const& item : iterate{ params.method("items") })
                            {
                                auto const items35 = unpack(item, 2);
                                name = items35[0];
                                parts = items35[1];
                                parts = (B<"sorted">()).call({ parts }, { { "key", (Mod<"operator">().attr("itemgetter"))(I<0>()) } });
                                first_param = parts[I<0>()][I<1>()];
                                charset = first_param.attr("charset");
                                // Our arbitrary error recovery is to ignore duplicate parameters,
                                // to use appearance order if there are duplicate rfc 2231 parts,
                                // and to ignore gaps.  This mimics the error recovery of get_param.
                                if (((!first_param.attr("extended").truthy()) && (py{ parts.size() } > (I<1>()))))
                                {
                                    if (parts[I<1>()][I<0>()] == (I<0>()))
                                    {
                                        parts[I<1>()][I<1>()].attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"duplicate parameter name; duplicate(s) ignored">()) });
                                        parts = parts[B<"slice">()(none(), I<1>(), none())];
                                    }
                                }
                                // Else assume the *0* was missing...note that this is different
                                // from get_param, but we registered a defect for this earlier.
                                value_parts = py_list();
                                i = I<0>();
                                for (py const& item : iterate{ parts })
                                {
                                    auto const items37 = unpack(item, 2);
                                    section_number = items37[0];
                                    param = items37[1];
                                    if (section_number != i)
                                    {
                                        // We could get fancier here and look for a complete
                                        // duplicate extended parameter and ignore the second one
                                        // seen.  But we're not doing that.  The old code didn't.
                                        if (!param.attr("extended").truthy())
                                        {
                                            param.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"duplicate parameter name; duplicate ignored">()) });
                                            continue;
                                        }
                                        else
                                        {
                                            param.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"inconsistent RFC2231 parameter numbering">()) });
                                        }
                                    }
                                    i = inplace(NEOS_PY_ADD, i, I<1>());
                                    value = param.attr("param_value");
                                    if (param.attr("extended").truthy())
                                    {
                                        {
                                            bool otherwise38 = false;
                                            try
                                            {
                                                value = (Mod<"urllib">().attr("parse")).method("unquote_to_bytes", { value });
                                                otherwise38 = true;
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler38;
                                                if (matches(handler38.exception, B<"UnicodeEncodeError">()))
                                                {
                                                    // source had surrogate escaped bytes.  What we do now
                                                    // is a bit of an open question.  I'm not sure this is
                                                    // the best choice, but it is what the old algorithm did
                                                    value = (Mod<"urllib">().attr("parse")).method("unquote", { value }, { { "encoding", S<"latin-1">() } });
                                                }
                                                else
                                                    py_reraise(handler38.exception);
                                            }
                                            if (otherwise38)
                                            {
                                                try
                                                {
                                                    value = value.method("decode", { charset, S<"surrogateescape">() });
                                                }
                                                catch (raised const&)
                                                {
                                                    pyclass::handler_scope handler39;
                                                    if (matches(handler39.exception, py_tuple({ B<"LookupError">(), B<"UnicodeEncodeError">() })))
                                                    {
                                                        // XXX: there should really be a custom defect for
                                                        // unknown character set to make it easy to find,
                                                        // because otherwise unknown charset is a silent
                                                        // failure.
                                                        value = value.method("decode", { S<"us-ascii">(), S<"surrogateescape">() });
                                                    }
                                                    else
                                                        py_reraise(handler39.exception);
                                                }
                                                if ((Mod<"email.utils">().attr("_has_surrogates"))(value).truthy())
                                                {
                                                    param.attr("defects").method("append", { (Mod<"email.errors">().attr("UndecodableBytesDefect"))() });
                                                }
                                            }
                                        }
                                    }
                                    value_parts.method("append", { value });
                                }
                                value = (S<"">()).method("join", { value_parts });
                                py_yield(py_tuple({ name, value }));
                            }
                            return none();
                        });
                    })) },
                { "__str__", w.method("MimeParameters", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name;
                        py params;
                        py value;
                        params = py_list();
                        for (py const& item : iterate{ self.attr("params") })
                        {
                            auto const items41 = unpack(item, 2);
                            name = items41[0];
                            value = items41[1];
                            if (value.truthy())
                            {
                                params.method("append", { (S<"{}={}">()).method("format", { name, (G<"email._header_value_parser.quote_string">())(value) }) });
                            }
                            else
                            {
                                params.method("append", { name });
                            }
                        }
                        params = (S<"; ">()).method("join", { params });
                        return (params.truthy() ? (S<" ">()) + params : S<"">());
                    }) }, }));

                // Set this false so that the value doesn't wind up on a new line even
                // if it and the parameters would fit there but not on the first line.
            define("email._header_value_parser", "ParameterizedHeaderValue", w.make_class("ParameterizedHeaderValue", { G<"email._header_value_parser.TokenList">() }, {
                { "syntactic_break", boolean(false) },
                { "params", B<"property">()(w.method("ParameterizedHeaderValue", "params", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token;
                        for (py const& item : iterate{ (B<"reversed">())(self) })
                        {
                            token = item;
                            if (token.attr("token_type") == (S<"mime-parameters">()))
                            {
                                return token.attr("params");
                            }
                        }
                        return py_dict();
                    })) }, }));

            define("email._header_value_parser", "ContentType", w.make_class("ContentType", { G<"email._header_value_parser.ParameterizedHeaderValue">() }, {
                { "token_type", S<"content-type">() },
                { "as_ew_allowed", boolean(false) },
                { "maintype", S<"text">() },
                { "subtype", S<"plain">() }, }));

            define("email._header_value_parser", "ContentDisposition", w.make_class("ContentDisposition", { G<"email._header_value_parser.ParameterizedHeaderValue">() }, {
                { "token_type", S<"content-disposition">() },
                { "as_ew_allowed", boolean(false) },
                { "content_disposition", none() }, }));

            define("email._header_value_parser", "ContentTransferEncoding", w.make_class("ContentTransferEncoding", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"content-transfer-encoding">() },
                { "as_ew_allowed", boolean(false) },
                { "cte", S<"7bit">() }, }));

            define("email._header_value_parser", "HeaderLabel", w.make_class("HeaderLabel", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"header-label">() },
                { "as_ew_allowed", boolean(false) }, }));

            define("email._header_value_parser", "MsgID", w.make_class("MsgID", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"msg-id">() },
                { "as_ew_allowed", boolean(false) },
                { "fold", w.method("MsgID", "fold", { py_positional("policy") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py policy = a[1];
                        return (B<"str">())(self) + policy.attr("linesep");
                    }) }, }));

            define("email._header_value_parser", "MessageID", w.make_class("MessageID", { G<"email._header_value_parser.MsgID">() }, {
                { "token_type", S<"message-id">() }, }));

            define("email._header_value_parser", "InvalidMessageID", w.make_class("InvalidMessageID", { G<"email._header_value_parser.MessageID">() }, {
                { "token_type", S<"invalid-message-id">() }, }));

            define("email._header_value_parser", "MessageIDList", w.make_class("MessageIDList", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"message-id-list">() },
                { "message_ids", B<"property">()(w.method("MessageIDList", "message_ids", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension43 = py_list(); py x; for (py const& item : iterate{ self }) { x = item; if (!(x.attr("token_type") == (S<"msg-id">()))) continue; comprehension43.method("append", { x }); } return comprehension43; }();
                    })) }, }));

            define("email._header_value_parser", "Header", w.make_class("Header", { G<"email._header_value_parser.TokenList">() }, {
                { "token_type", S<"header">() }, }));

            // 
            // Terminal classes and instances
            // 
            define("email._header_value_parser", "Terminal", w.make_class("Terminal", { B<"str">() }, {
                { "as_ew_allowed", boolean(true) },
                { "ew_combine_allowed", boolean(true) },
                { "syntactic_break", boolean(true) },
                { "__new__", w.method("Terminal", "__new__", { py_positional("value"), py_positional("token_type") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py token_type = a[2];
                        py self;
                        self = (B<"super">()(G<"email._header_value_parser.Terminal">(), cls)).method("__new__", { cls, value });
                        self.set_attr("token_type", token_type);
                        self.set_attr("defects", py_list());
                        return self;
                    }) },
                { "__repr__", w.method("Terminal", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"{}({})">()).method("format", { self.attr("__class__").attr("__name__"), (B<"super">()(G<"email._header_value_parser.Terminal">(), self)).method("__repr__") });
                    }) },
                { "pprint", w.method("Terminal", "pprint", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"print">())((self.attr("__class__").attr("__name__") + (S<"/">())) + self.attr("token_type"));
                        return none();
                    }) },
                { "all_defects", B<"property">()(w.method("Terminal", "all_defects", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.attr("defects"));
                    })) },
                { "_pp", w.method("Terminal", "_pp", { py_positional("indent", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py indent = a[1];
                        return py_list({ (S<"{}{}/{}({}){}">()).method("format", { indent, self.attr("__class__").attr("__name__"), self.attr("token_type"), (B<"super">()(G<"email._header_value_parser.Terminal">(), self)).method("__repr__"), (!self.attr("defects").truthy() ? S<"">() : (S<" {}">()).method("format", { self.attr("defects") })) }) });
                    }) },
                { "pop_trailing_ws", w.method("Terminal", "pop_trailing_ws", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "comments", B<"property">()(w.method("Terminal", "comments", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_list();
                    })) },
                { "__getnewargs__", w.method("Terminal", "__getnewargs__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ (B<"str">())(self), self.attr("token_type") });
                    }) }, }));

            define("email._header_value_parser", "WhiteSpaceTerminal", w.make_class("WhiteSpaceTerminal", { G<"email._header_value_parser.Terminal">() }, {
                { "value", B<"property">()(w.method("WhiteSpaceTerminal", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<" ">();
                    })) },
                { "startswith_fws", w.method("WhiteSpaceTerminal", "startswith_fws", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self; return !t.truthy() ? t : boolean((G<"email._header_value_parser.WSP">()).contains(self[I<0>()])); }();
                    }) }, }));

            define("email._header_value_parser", "ValueTerminal", w.make_class("ValueTerminal", { G<"email._header_value_parser.Terminal">() }, {
                { "value", B<"property">()(w.method("ValueTerminal", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    })) },
                { "startswith_fws", w.method("ValueTerminal", "startswith_fws", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) }, }));

            define("email._header_value_parser", "EWWhiteSpaceTerminal", w.make_class("EWWhiteSpaceTerminal", { G<"email._header_value_parser.WhiteSpaceTerminal">() }, {
                { "value", B<"property">()(w.method("EWWhiteSpaceTerminal", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"">();
                    })) },
                { "__str__", w.method("EWWhiteSpaceTerminal", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"">();
                    }) }, }));

            define("email._header_value_parser", "_InvalidEwError", w.make_class("_InvalidEwError", { Mod<"email.errors">().attr("HeaderParseError") }, {
 }));

            // XXX these need to become classes and used as instances so
            // that a program can't change them in a parse tree and screw
            // up other parse trees.  Maybe should have  tests for that, too.
            define("email._header_value_parser", "DOT", (G<"email._header_value_parser.ValueTerminal">())(S<".">(), S<"dot">()));
            define("email._header_value_parser", "ListSeparator", (G<"email._header_value_parser.ValueTerminal">())(S<",">(), S<"list-separator">()));
            (G<"email._header_value_parser.ListSeparator">()).set_attr("as_ew_allowed", boolean(false));
            (G<"email._header_value_parser.ListSeparator">()).set_attr("syntactic_break", boolean(false));
            define("email._header_value_parser", "RouteComponentMarker", (G<"email._header_value_parser.ValueTerminal">())(S<"@">(), S<"route-component-marker">()));
            // 
            // Parser
            // 
            // Parse strings according to RFC822/2047/2822/5322 rules.
            // 
            // This is a stateless parser.  Each get_XXX function accepts a string and
            // returns either a Terminal or a TokenList representing the RFC object named
            // by the method and a string containing the remaining unparsed characters
            // from the input.  Thus a parser method consumes the next syntactic construct
            // of a given type and returns a token representing the construct plus the
            // unparsed remainder of the input string.
            // 
            // For example, if the first element of a structured header is a 'phrase',
            // then:
            // 
            // phrase, value = get_phrase(value)
            // 
            // returns the complete phrase from the start of the string value, plus any
            // characters left in the string after the phrase is removed.
            define("email._header_value_parser", "_wsp_splitter", (Mod<"re">()).method("compile", { (S<"([{}]+)">()).method("format", { (S<"">()).method("join", { G<"email._header_value_parser.WSP">() }) }) }).attr("split"));
            define("email._header_value_parser", "_non_atom_end_matcher", (Mod<"re">()).method("compile", { (S<"[^{}]+">()).method("format", { (Mod<"re">()).method("escape", { (S<"">()).method("join", { G<"email._header_value_parser.ATOM_ENDS">() }) }) }) }).attr("match"));
            define("email._header_value_parser", "_non_printable_finder", (Mod<"re">()).method("compile", { S<"[\\x00-\\x20\\x7F]">() }).attr("findall"));
            define("email._header_value_parser", "_non_token_end_matcher", (Mod<"re">()).method("compile", { (S<"[^{}]+">()).method("format", { (Mod<"re">()).method("escape", { (S<"">()).method("join", { G<"email._header_value_parser.TOKEN_ENDS">() }) }) }) }).attr("match"));
            define("email._header_value_parser", "_non_attribute_end_matcher", (Mod<"re">()).method("compile", { (S<"[^{}]+">()).method("format", { (Mod<"re">()).method("escape", { (S<"">()).method("join", { G<"email._header_value_parser.ATTRIBUTE_ENDS">() }) }) }) }).attr("match"));
            define("email._header_value_parser", "_non_extended_attribute_end_matcher", (Mod<"re">()).method("compile", { (S<"[^{}]+">()).method("format", { (Mod<"re">()).method("escape", { (S<"">()).method("join", { G<"email._header_value_parser.EXTENDED_ATTRIBUTE_ENDS">() }) }) }) }).attr("match"));
            define("email._header_value_parser", "_validate_xtext", w.function("_validate_xtext", "_validate_xtext", { py_positional("xtext") }, [](arguments a) -> py
                {
                    py xtext = a[0];
                    py non_printables;
                    non_printables = (G<"email._header_value_parser._non_printable_finder">())(xtext);
                    if (non_printables.truthy())
                    {
                        xtext.attr("defects").method("append", { (Mod<"email.errors">().attr("NonPrintableDefect"))(non_printables) });
                    }
                    if ((Mod<"email.utils">().attr("_has_surrogates"))(xtext).truthy())
                    {
                        xtext.attr("defects").method("append", { (Mod<"email.errors">().attr("UndecodableBytesDefect"))(S<"Non-ASCII characters found in header token">()) });
                    }
                    return none();
                }));

            define("email._header_value_parser", "_get_ptext_to_endchars", w.function("_get_ptext_to_endchars", "_get_ptext_to_endchars", { py_positional("value"), py_positional("endchars") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py endchars = a[1];
                    py escape;
                    py fragment;
                    py had_qp;
                    py pos;
                    py remainder;
                    py vchars;
                    if (!value.truthy())
                    {
                        return py_tuple({ S<"">(), S<"">(), boolean(false) });
                    }
                    auto const items44 = B<"list">()((G<"email._header_value_parser._wsp_splitter">())(value, I<1>())).items();
                    if (items44.size() < 1u)
                        py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items44.size()) + ")");
                    fragment = items44[0];
                    remainder = py_list(std::vector<py>(items44.begin() + 1, items44.end() - 0));
                    vchars = py_list();
                    escape = boolean(false);
                    had_qp = boolean(false);
                    bool broke45 = false;
                    for (py const& item : iterate{ (B<"range">())(py{ fragment.size() }) })
                    {
                        pos = item;
                        if (fragment[pos] == (S<"\\">()))
                        {
                            if (escape.truthy())
                            {
                                escape = boolean(false);
                                had_qp = boolean(true);
                            }
                            else
                            {
                                escape = boolean(true);
                                continue;
                            }
                        }
                        if (escape.truthy())
                        {
                            escape = boolean(false);
                        }
                        else if (endchars.contains(fragment[pos]))
                        {
                            broke45 = true;
                            break;
                        }
                        vchars.method("append", { fragment[pos] });
                    }
                    if (!broke45)
                    {
                        pos = pos + (I<1>());
                    }
                    return py_tuple({ (S<"">()).method("join", { vchars }), (S<"">()).method("join", { py_list({ fragment[B<"slice">()(pos, none(), none())] }) + remainder }), had_qp });
                }));

            define("email._header_value_parser", "get_fws", w.function("get_fws", "get_fws", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py fws;
                    py newvalue;
                    newvalue = value.method("lstrip");
                    fws = (G<"email._header_value_parser.WhiteSpaceTerminal">())(value[B<"slice">()(none(), py{ value.size() } - py{ newvalue.size() }, none())], S<"fws">());
                    return py_tuple({ fws, newvalue });
                }));

            define("email._header_value_parser", "get_encoded_word", w.function("get_encoded_word", "get_encoded_word", { py_positional("value"), py_positional("terminal_type", S<"vtext">()) }, [](arguments a) -> py
                {
                    py value = a[0];
                    py terminal_type = a[1];
                    py chars;
                    py charset;
                    py defects;
                    py ew;
                    py lang;
                    py remainder;
                    py remstr;
                    py rest;
                    py text;
                    py tok;
                    py token;
                    py vtext;
                    ew = (G<"email._header_value_parser.EncodedWord">())();
                    if (!value.method("startswith", { S<"=?">() }).truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected encoded word but found {}">()).method("format", { value })));
                    }
                    auto const items46 = B<"list">()(value[B<"slice">()(I<2>(), none(), none())].method("split", { S<"?=">(), I<1>() })).items();
                    if (items46.size() < 1u)
                        py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items46.size()) + ")");
                    tok = items46[0];
                    remainder = py_list(std::vector<py>(items46.begin() + 1, items46.end() - 0));
                    if (tok == value[B<"slice">()(I<2>(), none(), none())])
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected encoded word but found {}">()).method("format", { value })));
                    }
                    remstr = (S<"">()).method("join", { remainder });
                    if (((py{ remstr.size() } > (I<1>())) && (Mod<"string">().attr("hexdigits")).contains(remstr[I<0>()]) && (Mod<"string">().attr("hexdigits")).contains(remstr[I<1>()]) && (tok.method("count", { S<"?">() }) < (I<2>()))))
                    {
                        // The ? after the CTE was followed by an encoded word escape (=XX).
                        auto const items47 = B<"list">()(remstr.method("split", { S<"?=">(), I<1>() })).items();
                        if (items47.size() < 1u)
                            py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items47.size()) + ")");
                        rest = items47[0];
                        remainder = py_list(std::vector<py>(items47.begin() + 1, items47.end() - 0));
                        tok = (tok + (S<"?=">())) + rest;
                    }
                    if (py{ tok.method("split").size() } > (I<1>()))
                    {
                        ew.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"whitespace inside encoded word">()) });
                    }
                    ew.set_attr("cte", value);
                    value = (S<"">()).method("join", { remainder });
                    try
                    {
                        auto const items49 = unpack((Mod<"email._encoded_words">().attr("decode"))(((S<"=?">()) + tok) + (S<"?=">())), 4);
                        text = items49[0];
                        charset = items49[1];
                        lang = items49[2];
                        defects = items49[3];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler48;
                        if (matches(handler48.exception, py_tuple({ B<"ValueError">(), B<"KeyError">() })))
                        {
                            py_raise((G<"email._header_value_parser._InvalidEwError">())((S<"encoded word format invalid: '{}'">()).method("format", { ew.attr("cte") })));
                        }
                        else
                            py_reraise(handler48.exception);
                    }
                    ew.set_attr("charset", charset);
                    ew.set_attr("lang", lang);
                    ew.attr("defects").method("extend", { defects });
                    while (text.truthy())
                    {
                        if ((G<"email._header_value_parser.WSP">()).contains(text[I<0>()]))
                        {
                            auto const items51 = unpack((G<"email._header_value_parser.get_fws">())(text), 2);
                            token = items51[0];
                            text = items51[1];
                            ew.method("append", { token });
                            continue;
                        }
                        auto const items52 = B<"list">()((G<"email._header_value_parser._wsp_splitter">())(text, I<1>())).items();
                        if (items52.size() < 1u)
                            py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items52.size()) + ")");
                        chars = items52[0];
                        remainder = py_list(std::vector<py>(items52.begin() + 1, items52.end() - 0));
                        vtext = (G<"email._header_value_parser.ValueTerminal">())(chars, terminal_type);
                        (G<"email._header_value_parser._validate_xtext">())(vtext);
                        ew.method("append", { vtext });
                        text = (S<"">()).method("join", { remainder });
                    }
                    // Encoded words should be followed by a WS
                    if ((value.truthy() && (!(G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))))
                    {
                        ew.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"missing trailing whitespace after encoded-word">()) });
                    }
                    return py_tuple({ ew, value });
                }));

            define("email._header_value_parser", "get_unstructured", w.function("get_unstructured", "get_unstructured", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py have_ws;
                    py remainder;
                    py tok;
                    py token;
                    py unstructured;
                    py valid_ew;
                    py vtext;
                    // XXX: but what about bare CR and LF?  They might signal the start or
                    // end of an encoded word.  YAGNI for now, since our current parsers
                    // will never send us strings with bare CR or LF.
                    unstructured = (G<"email._header_value_parser.UnstructuredTokenList">())();
                    while (value.truthy())
                    {
                        if ((G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))
                        {
                            auto const items54 = unpack((G<"email._header_value_parser.get_fws">())(value), 2);
                            token = items54[0];
                            value = items54[1];
                            unstructured.method("append", { token });
                            continue;
                        }
                        valid_ew = boolean(true);
                        if (value.method("startswith", { S<"=?">() }).truthy())
                        {
                            {
                                bool otherwise55 = false;
                                try
                                {
                                    auto const items56 = unpack((G<"email._header_value_parser.get_encoded_word">())(value, S<"utext">()), 2);
                                    token = items56[0];
                                    value = items56[1];
                                    otherwise55 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler55;
                                    if (matches(handler55.exception, G<"email._header_value_parser._InvalidEwError">()))
                                    {
                                        valid_ew = boolean(false);
                                    }
                                    else if (matches(handler55.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                    {
                                        // XXX: Need to figure out how to register defects when
                                        // appropriate here.
                                    }
                                    else
                                        py_reraise(handler55.exception);
                                }
                                if (otherwise55)
                                {
                                    have_ws = boolean(true);
                                    if (py{ unstructured.size() } > (I<0>()))
                                    {
                                        if (unstructured[I<-1>()].attr("token_type") != (S<"fws">()))
                                        {
                                            unstructured.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"missing whitespace before encoded word">()) });
                                            have_ws = boolean(false);
                                        }
                                    }
                                    if ((have_ws.truthy() && (py{ unstructured.size() } > (I<1>()))))
                                    {
                                        if (unstructured[I<-2>()].attr("token_type") == (S<"encoded-word">()))
                                        {
                                            unstructured.set_item(I<-1>(), (G<"email._header_value_parser.EWWhiteSpaceTerminal">())(unstructured[I<-1>()], S<"fws">()));
                                        }
                                    }
                                    unstructured.method("append", { token });
                                    continue;
                                }
                            }
                        }
                        auto const items57 = B<"list">()((G<"email._header_value_parser._wsp_splitter">())(value, I<1>())).items();
                        if (items57.size() < 1u)
                            py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items57.size()) + ")");
                        tok = items57[0];
                        remainder = py_list(std::vector<py>(items57.begin() + 1, items57.end() - 0));
                        // Split in the middle of an atom if there is a rfc2047 encoded word
                        // which does not have WSP on both sides. The defect will be registered
                        // the next time through the loop.
                        // This needs to only be performed when the encoded word is valid;
                        // otherwise, performing it on an invalid encoded word can cause
                        // the parser to go in an infinite loop.
                        if ((valid_ew.truthy() && (G<"email._header_value_parser.rfc2047_matcher">()).method("search", { tok }).truthy()))
                        {
                            auto const items58 = B<"list">()(value.method("partition", { S<"=?">() })).items();
                            if (items58.size() < 1u)
                                py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items58.size()) + ")");
                            tok = items58[0];
                            remainder = py_list(std::vector<py>(items58.begin() + 1, items58.end() - 0));
                        }
                        vtext = (G<"email._header_value_parser.ValueTerminal">())(tok, S<"utext">());
                        (G<"email._header_value_parser._validate_xtext">())(vtext);
                        unstructured.method("append", { vtext });
                        value = (S<"">()).method("join", { remainder });
                    }
                    return unstructured;
                }));

            define("email._header_value_parser", "get_qp_ctext", w.function("get_qp_ctext", "get_qp_ctext", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py _;
                    py ptext;
                    auto const items59 = unpack((G<"email._header_value_parser._get_ptext_to_endchars">())(value, S<"()">()), 3);
                    ptext = items59[0];
                    value = items59[1];
                    _ = items59[2];
                    ptext = (G<"email._header_value_parser.WhiteSpaceTerminal">())(ptext, S<"ptext">());
                    (G<"email._header_value_parser._validate_xtext">())(ptext);
                    return py_tuple({ ptext, value });
                }));

            define("email._header_value_parser", "get_qcontent", w.function("get_qcontent", "get_qcontent", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py _;
                    py ptext;
                    auto const items60 = unpack((G<"email._header_value_parser._get_ptext_to_endchars">())(value, S<"\"">()), 3);
                    ptext = items60[0];
                    value = items60[1];
                    _ = items60[2];
                    ptext = (G<"email._header_value_parser.ValueTerminal">())(ptext, S<"ptext">());
                    (G<"email._header_value_parser._validate_xtext">())(ptext);
                    return py_tuple({ ptext, value });
                }));

            define("email._header_value_parser", "get_atext", w.function("get_atext", "get_atext", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py atext;
                    py m;
                    m = (G<"email._header_value_parser._non_atom_end_matcher">())(value);
                    if (!m.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected atext but found '{}'">()).method("format", { value })));
                    }
                    atext = m.method("group");
                    value = value[B<"slice">()(py{ atext.size() }, none(), none())];
                    atext = (G<"email._header_value_parser.ValueTerminal">())(atext, S<"atext">());
                    (G<"email._header_value_parser._validate_xtext">())(atext);
                    return py_tuple({ atext, value });
                }));

            define("email._header_value_parser", "get_bare_quoted_string", w.function("get_bare_quoted_string", "get_bare_quoted_string", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py bare_quoted_string;
                    py token;
                    py valid_ew;
                    if (((!value.truthy()) || (value[I<0>()] != (S<"\"">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected '\"' but found '{}'">()).method("format", { value })));
                    }
                    bare_quoted_string = (G<"email._header_value_parser.BareQuotedString">())();
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    if ((value.truthy() && (value[I<0>()] == (S<"\"">()))))
                    {
                        return py_tuple({ bare_quoted_string, value[B<"slice">()(I<1>(), none(), none())] });
                    }
                    while ((value.truthy() && (value[I<0>()] != (S<"\"">()))))
                    {
                        if ((G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))
                        {
                            auto const items62 = unpack((G<"email._header_value_parser.get_fws">())(value), 2);
                            token = items62[0];
                            value = items62[1];
                        }
                        else if (value[B<"slice">()(none(), I<2>(), none())] == (S<"=?">()))
                        {
                            valid_ew = boolean(false);
                            try
                            {
                                auto const items64 = unpack((G<"email._header_value_parser.get_encoded_word">())(value), 2);
                                token = items64[0];
                                value = items64[1];
                                bare_quoted_string.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"encoded word inside quoted string">()) });
                                valid_ew = boolean(true);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler63;
                                if (matches(handler63.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    auto const items65 = unpack((G<"email._header_value_parser.get_qcontent">())(value), 2);
                                    token = items65[0];
                                    value = items65[1];
                                }
                                else
                                    py_reraise(handler63.exception);
                            }
                            // Collapse the whitespace between two encoded words that occur in a
                            // bare-quoted-string.
                            if ((valid_ew.truthy() && (py{ bare_quoted_string.size() } > (I<1>()))))
                            {
                                if (((bare_quoted_string[I<-1>()].attr("token_type") == (S<"fws">())) && (bare_quoted_string[I<-2>()].attr("token_type") == (S<"encoded-word">()))))
                                {
                                    bare_quoted_string.set_item(I<-1>(), (G<"email._header_value_parser.EWWhiteSpaceTerminal">())(bare_quoted_string[I<-1>()], S<"fws">()));
                                }
                            }
                        }
                        else
                        {
                            auto const items66 = unpack((G<"email._header_value_parser.get_qcontent">())(value), 2);
                            token = items66[0];
                            value = items66[1];
                        }
                        bare_quoted_string.method("append", { token });
                    }
                    if (!value.truthy())
                    {
                        bare_quoted_string.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"end of header inside quoted string">()) });
                        return py_tuple({ bare_quoted_string, value });
                    }
                    return py_tuple({ bare_quoted_string, value[B<"slice">()(I<1>(), none(), none())] });
                }));

            define("email._header_value_parser", "get_comment", w.function("get_comment", "get_comment", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py comment;
                    py token;
                    if ((value.truthy() && (value[I<0>()] != (S<"(">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected '(' but found '{}'">()).method("format", { value })));
                    }
                    comment = (G<"email._header_value_parser.Comment">())();
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    while ((value.truthy() && (value[I<0>()] != (S<")">()))))
                    {
                        if ((G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))
                        {
                            auto const items68 = unpack((G<"email._header_value_parser.get_fws">())(value), 2);
                            token = items68[0];
                            value = items68[1];
                        }
                        else if (value[I<0>()] == (S<"(">()))
                        {
                            auto const items69 = unpack((G<"email._header_value_parser.get_comment">())(value), 2);
                            token = items69[0];
                            value = items69[1];
                        }
                        else
                        {
                            auto const items70 = unpack((G<"email._header_value_parser.get_qp_ctext">())(value), 2);
                            token = items70[0];
                            value = items70[1];
                        }
                        comment.method("append", { token });
                    }
                    if (!value.truthy())
                    {
                        comment.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"end of header inside comment">()) });
                        return py_tuple({ comment, value });
                    }
                    return py_tuple({ comment, value[B<"slice">()(I<1>(), none(), none())] });
                }));

            define("email._header_value_parser", "get_cfws", w.function("get_cfws", "get_cfws", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py cfws;
                    py token;
                    cfws = (G<"email._header_value_parser.CFWSList">())();
                    while ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        if ((G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))
                        {
                            auto const items72 = unpack((G<"email._header_value_parser.get_fws">())(value), 2);
                            token = items72[0];
                            value = items72[1];
                        }
                        else
                        {
                            auto const items73 = unpack((G<"email._header_value_parser.get_comment">())(value), 2);
                            token = items73[0];
                            value = items73[1];
                        }
                        cfws.method("append", { token });
                    }
                    return py_tuple({ cfws, value });
                }));

            define("email._header_value_parser", "get_quoted_string", w.function("get_quoted_string", "get_quoted_string", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py quoted_string;
                    py token;
                    quoted_string = (G<"email._header_value_parser.QuotedString">())();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items74 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items74[0];
                        value = items74[1];
                        quoted_string.method("append", { token });
                    }
                    auto const items75 = unpack((G<"email._header_value_parser.get_bare_quoted_string">())(value), 2);
                    token = items75[0];
                    value = items75[1];
                    quoted_string.method("append", { token });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items76 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items76[0];
                        value = items76[1];
                        quoted_string.method("append", { token });
                    }
                    return py_tuple({ quoted_string, value });
                }));

            define("email._header_value_parser", "get_atom", w.function("get_atom", "get_atom", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py atom;
                    py token;
                    atom = (G<"email._header_value_parser.Atom">())();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items77 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items77[0];
                        value = items77[1];
                        atom.method("append", { token });
                    }
                    if ((value.truthy() && (G<"email._header_value_parser.ATOM_ENDS">()).contains(value[I<0>()])))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected atom but found '{}'">()).method("format", { value })));
                    }
                    if (value.method("startswith", { S<"=?">() }).truthy())
                    {
                        try
                        {
                            auto const items79 = unpack((G<"email._header_value_parser.get_encoded_word">())(value), 2);
                            token = items79[0];
                            value = items79[1];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler78;
                            if (matches(handler78.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                // XXX: need to figure out how to register defects when
                                // appropriate here.
                                auto const items80 = unpack((G<"email._header_value_parser.get_atext">())(value), 2);
                                token = items80[0];
                                value = items80[1];
                            }
                            else
                                py_reraise(handler78.exception);
                        }
                    }
                    else
                    {
                        auto const items81 = unpack((G<"email._header_value_parser.get_atext">())(value), 2);
                        token = items81[0];
                        value = items81[1];
                    }
                    atom.method("append", { token });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items82 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items82[0];
                        value = items82[1];
                        atom.method("append", { token });
                    }
                    return py_tuple({ atom, value });
                }));

            define("email._header_value_parser", "get_dot_atom_text", w.function("get_dot_atom_text", "get_dot_atom_text", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py dot_atom_text;
                    py token;
                    dot_atom_text = (G<"email._header_value_parser.DotAtomText">())();
                    if (((!value.truthy()) || (G<"email._header_value_parser.ATOM_ENDS">()).contains(value[I<0>()])))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected atom at a start of dot-atom-text but found '{}'">()).method("format", { value })));
                    }
                    while ((value.truthy() && (!(G<"email._header_value_parser.ATOM_ENDS">()).contains(value[I<0>()]))))
                    {
                        auto const items84 = unpack((G<"email._header_value_parser.get_atext">())(value), 2);
                        token = items84[0];
                        value = items84[1];
                        dot_atom_text.method("append", { token });
                        if ((value.truthy() && (value[I<0>()] == (S<".">()))))
                        {
                            dot_atom_text.method("append", { G<"email._header_value_parser.DOT">() });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                    }
                    if (dot_atom_text[I<-1>()].is((G<"email._header_value_parser.DOT">())))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected atom at end of dot-atom-text but found '{}'">()).method("format", { (S<".">()) + value })));
                    }
                    return py_tuple({ dot_atom_text, value });
                }));

            define("email._header_value_parser", "get_dot_atom", w.function("get_dot_atom", "get_dot_atom", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py dot_atom;
                    py token;
                    dot_atom = (G<"email._header_value_parser.DotAtom">())();
                    if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                    {
                        auto const items85 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items85[0];
                        value = items85[1];
                        dot_atom.method("append", { token });
                    }
                    if (value.method("startswith", { S<"=?">() }).truthy())
                    {
                        try
                        {
                            auto const items87 = unpack((G<"email._header_value_parser.get_encoded_word">())(value), 2);
                            token = items87[0];
                            value = items87[1];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler86;
                            if (matches(handler86.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                // XXX: need to figure out how to register defects when
                                // appropriate here.
                                auto const items88 = unpack((G<"email._header_value_parser.get_dot_atom_text">())(value), 2);
                                token = items88[0];
                                value = items88[1];
                            }
                            else
                                py_reraise(handler86.exception);
                        }
                    }
                    else
                    {
                        auto const items89 = unpack((G<"email._header_value_parser.get_dot_atom_text">())(value), 2);
                        token = items89[0];
                        value = items89[1];
                    }
                    dot_atom.method("append", { token });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items90 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items90[0];
                        value = items90[1];
                        dot_atom.method("append", { token });
                    }
                    return py_tuple({ dot_atom, value });
                }));

            define("email._header_value_parser", "get_word", w.function("get_word", "get_word", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py leader;
                    py token;
                    if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                    {
                        auto const items91 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        leader = items91[0];
                        value = items91[1];
                    }
                    else
                    {
                        leader = none();
                    }
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"Expected 'atom' or 'quoted-string' but found nothing.">()));
                    }
                    if (value[I<0>()] == (S<"\"">()))
                    {
                        auto const items92 = unpack((G<"email._header_value_parser.get_quoted_string">())(value), 2);
                        token = items92[0];
                        value = items92[1];
                    }
                    else if ((G<"email._header_value_parser.SPECIALS">()).contains(value[I<0>()]))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"Expected 'atom' or 'quoted-string' but found '{}'">()).method("format", { value })));
                    }
                    else
                    {
                        auto const items93 = unpack((G<"email._header_value_parser.get_atom">())(value), 2);
                        token = items93[0];
                        value = items93[1];
                    }
                    if (!leader.is_none())
                    {
                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                    }
                    return py_tuple({ token, value });
                }));

            define("email._header_value_parser", "get_phrase", w.function("get_phrase", "get_phrase", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py phrase;
                    py token;
                    phrase = (G<"email._header_value_parser.Phrase">())();
                    try
                    {
                        auto const items95 = unpack((G<"email._header_value_parser.get_word">())(value), 2);
                        token = items95[0];
                        value = items95[1];
                        phrase.method("append", { token });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler94;
                        if (matches(handler94.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            phrase.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"phrase does not start with word">()) });
                        }
                        else
                            py_reraise(handler94.exception);
                    }
                    while ((value.truthy() && (!(G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()]))))
                    {
                        if (value[I<0>()] == (S<".">()))
                        {
                            phrase.method("append", { G<"email._header_value_parser.DOT">() });
                            phrase.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"period in 'phrase'">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                        else
                        {
                            try
                            {
                                auto const items98 = unpack((G<"email._header_value_parser.get_word">())(value), 2);
                                token = items98[0];
                                value = items98[1];
                                if (((token[I<0>()].attr("token_type") == (S<"encoded-word">())) && phrase.truthy() && (phrase[I<-1>()].attr("token_type") == (S<"atom">())) && (py{ phrase[I<-1>()].size() } > (I<1>())) && (phrase[I<-1>()][I<-2>()].attr("token_type") == (S<"encoded-word">())) && (phrase[I<-1>()][I<-1>()].attr("token_type") == (S<"cfws">())) && (!phrase[I<-1>()][I<-1>()].attr("comments").truthy())))
                                {
                                    // linear ws between ews needs special handing...
                                    phrase[I<-1>()].set_item(I<-1>(), (G<"email._header_value_parser.EWWhiteSpaceTerminal">())(phrase[I<-1>()], S<"fws">()));
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler97;
                                if (matches(handler97.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                                    {
                                        auto const items99 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                                        token = items99[0];
                                        value = items99[1];
                                        phrase.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"comment found without atom">()) });
                                    }
                                    else
                                    {
                                        py_reraise(handler97.exception);
                                    }
                                }
                                else
                                    py_reraise(handler97.exception);
                            }
                            phrase.method("append", { token });
                        }
                    }
                    return py_tuple({ phrase, value });
                }));

            define("email._header_value_parser", "get_local_part", w.function("get_local_part", "get_local_part", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py leader;
                    py local_part;
                    py obs_local_part;
                    py token;
                    local_part = (G<"email._header_value_parser.LocalPart">())();
                    leader = none();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items100 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        leader = items100[0];
                        value = items100[1];
                    }
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected local-part but found '{}'">()).method("format", { value })));
                    }
                    try
                    {
                        auto const items102 = unpack((G<"email._header_value_parser.get_dot_atom">())(value), 2);
                        token = items102[0];
                        value = items102[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler101;
                        if (matches(handler101.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            try
                            {
                                auto const items104 = unpack((G<"email._header_value_parser.get_word">())(value), 2);
                                token = items104[0];
                                value = items104[1];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler103;
                                if (matches(handler103.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    if (((value[I<0>()] != (S<"\\">())) && (G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()])))
                                    {
                                        py_reraise(handler103.exception);
                                    }
                                    token = (G<"email._header_value_parser.TokenList">())();
                                }
                                else
                                    py_reraise(handler103.exception);
                            }
                        }
                        else
                            py_reraise(handler101.exception);
                    }
                    if (!leader.is_none())
                    {
                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                    }
                    local_part.method("append", { token });
                    if ((value.truthy() && ([&]() -> py { py const t = boolean(value[I<0>()] == (S<"\\">())); return t.truthy() ? t : boolean(!(G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()])); }()).truthy()))
                    {
                        auto const items105 = unpack((G<"email._header_value_parser.get_obs_local_part">())((B<"str">())(local_part) + value), 2);
                        obs_local_part = items105[0];
                        value = items105[1];
                        if (obs_local_part.attr("token_type") == (S<"invalid-obs-local-part">()))
                        {
                            local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"local-part is not dot-atom, quoted-string, or obs-local-part">()) });
                        }
                        else
                        {
                            local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"local-part is not a dot-atom (contains CFWS)">()) });
                        }
                        local_part.set_item(I<0>(), obs_local_part);
                    }
                    try
                    {
                        local_part.attr("value").method("encode", { S<"ascii">() });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler106;
                        if (matches(handler106.exception, B<"UnicodeEncodeError">()))
                        {
                            local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("NonASCIILocalPartDefect"))(S<"local-part contains non-ASCII characters)">()) });
                        }
                        else
                            py_reraise(handler106.exception);
                    }
                    return py_tuple({ local_part, value });
                }));

            define("email._header_value_parser", "get_obs_local_part", w.function("get_obs_local_part", "get_obs_local_part", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py last_non_ws_was_dot;
                    py obs_local_part;
                    py token;
                    obs_local_part = (G<"email._header_value_parser.ObsLocalPart">())();
                    last_non_ws_was_dot = boolean(false);
                    while ((value.truthy() && ([&]() -> py { py const t = boolean(value[I<0>()] == (S<"\\">())); return t.truthy() ? t : boolean(!(G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()])); }()).truthy()))
                    {
                        if (value[I<0>()] == (S<".">()))
                        {
                            if (last_non_ws_was_dot.truthy())
                            {
                                obs_local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"invalid repeated '.'">()) });
                            }
                            obs_local_part.method("append", { G<"email._header_value_parser.DOT">() });
                            last_non_ws_was_dot = boolean(true);
                            value = value[B<"slice">()(I<1>(), none(), none())];
                            continue;
                        }
                        else if (value[I<0>()] == (S<"\\">()))
                        {
                            obs_local_part.method("append", { (G<"email._header_value_parser.ValueTerminal">())(value[I<0>()], S<"misplaced-special">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                            obs_local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"'\\' character outside of quoted-string/ccontent">()) });
                            last_non_ws_was_dot = boolean(false);
                            continue;
                        }
                        if ((obs_local_part.truthy() && (obs_local_part[I<-1>()].attr("token_type") != (S<"dot">()))))
                        {
                            obs_local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"missing '.' between words">()) });
                        }
                        try
                        {
                            auto const items109 = unpack((G<"email._header_value_parser.get_word">())(value), 2);
                            token = items109[0];
                            value = items109[1];
                            last_non_ws_was_dot = boolean(false);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler108;
                            if (matches(handler108.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                if (!(G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                                {
                                    py_reraise(handler108.exception);
                                }
                                auto const items110 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                                token = items110[0];
                                value = items110[1];
                            }
                            else
                                py_reraise(handler108.exception);
                        }
                        obs_local_part.method("append", { token });
                    }
                    if (!obs_local_part.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected obs-local-part but found '{}'">()).method("format", { value })));
                    }
                    if (((obs_local_part[I<0>()].attr("token_type") == (S<"dot">())) || ([&]() -> py { py const t = boolean(obs_local_part[I<0>()].attr("token_type") == (S<"cfws">())); return !t.truthy() ? t : [&]() -> py { py const t = boolean(py{ obs_local_part.size() } > (I<1>())); return !t.truthy() ? t : boolean(obs_local_part[I<1>()].attr("token_type") == (S<"dot">())); }(); }()).truthy()))
                    {
                        obs_local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Invalid leading '.' in local part">()) });
                    }
                    if (((obs_local_part[I<-1>()].attr("token_type") == (S<"dot">())) || ([&]() -> py { py const t = boolean(obs_local_part[I<-1>()].attr("token_type") == (S<"cfws">())); return !t.truthy() ? t : [&]() -> py { py const t = boolean(py{ obs_local_part.size() } > (I<1>())); return !t.truthy() ? t : boolean(obs_local_part[I<-2>()].attr("token_type") == (S<"dot">())); }(); }()).truthy()))
                    {
                        obs_local_part.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Invalid trailing '.' in local part">()) });
                    }
                    if (obs_local_part.attr("defects").truthy())
                    {
                        obs_local_part.set_attr("token_type", S<"invalid-obs-local-part">());
                    }
                    return py_tuple({ obs_local_part, value });
                }));

            define("email._header_value_parser", "get_dtext", w.function("get_dtext", "get_dtext", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py had_qp;
                    py ptext;
                    auto const items111 = unpack((G<"email._header_value_parser._get_ptext_to_endchars">())(value, S<"[]">()), 3);
                    ptext = items111[0];
                    value = items111[1];
                    had_qp = items111[2];
                    ptext = (G<"email._header_value_parser.ValueTerminal">())(ptext, S<"ptext">());
                    if (had_qp.truthy())
                    {
                        ptext.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"quoted printable found in domain-literal">()) });
                    }
                    (G<"email._header_value_parser._validate_xtext">())(ptext);
                    return py_tuple({ ptext, value });
                }));

            define("email._header_value_parser", "_check_for_early_dl_end", w.function("_check_for_early_dl_end", "_check_for_early_dl_end", { py_positional("value"), py_positional("domain_literal") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py domain_literal = a[1];
                    if (value.truthy())
                    {
                        return boolean(false);
                    }
                    domain_literal.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"end of input inside domain-literal">()) });
                    domain_literal.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"]">(), S<"domain-literal-end">()) });
                    return boolean(true);
                }));

            define("email._header_value_parser", "get_domain_literal", w.function("get_domain_literal", "get_domain_literal", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py domain_literal;
                    py token;
                    domain_literal = (G<"email._header_value_parser.DomainLiteral">())();
                    if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                    {
                        auto const items112 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items112[0];
                        value = items112[1];
                        domain_literal.method("append", { token });
                    }
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"expected domain-literal">()));
                    }
                    if (value[I<0>()] != (S<"[">()))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected '[' at start of domain-literal but found '{}'">()).method("format", { value })));
                    }
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    domain_literal.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"[">(), S<"domain-literal-start">()) });
                    if ((G<"email._header_value_parser._check_for_early_dl_end">())(value, domain_literal).truthy())
                    {
                        return py_tuple({ domain_literal, value });
                    }
                    if ((G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))
                    {
                        auto const items113 = unpack((G<"email._header_value_parser.get_fws">())(value), 2);
                        token = items113[0];
                        value = items113[1];
                        domain_literal.method("append", { token });
                    }
                    auto const items114 = unpack((G<"email._header_value_parser.get_dtext">())(value), 2);
                    token = items114[0];
                    value = items114[1];
                    domain_literal.method("append", { token });
                    if ((G<"email._header_value_parser._check_for_early_dl_end">())(value, domain_literal).truthy())
                    {
                        return py_tuple({ domain_literal, value });
                    }
                    if ((G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))
                    {
                        auto const items115 = unpack((G<"email._header_value_parser.get_fws">())(value), 2);
                        token = items115[0];
                        value = items115[1];
                        domain_literal.method("append", { token });
                    }
                    if ((G<"email._header_value_parser._check_for_early_dl_end">())(value, domain_literal).truthy())
                    {
                        return py_tuple({ domain_literal, value });
                    }
                    if (value[I<0>()] != (S<"]">()))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected ']' at end of domain-literal but found '{}'">()).method("format", { value })));
                    }
                    domain_literal.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"]">(), S<"domain-literal-end">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items116 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items116[0];
                        value = items116[1];
                        domain_literal.method("append", { token });
                    }
                    return py_tuple({ domain_literal, value });
                }));

            define("email._header_value_parser", "get_domain", w.function("get_domain", "get_domain", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py domain;
                    py leader;
                    py token;
                    domain = (G<"email._header_value_parser.Domain">())();
                    leader = none();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items117 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        leader = items117[0];
                        value = items117[1];
                    }
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected domain but found '{}'">()).method("format", { value })));
                    }
                    if (value[I<0>()] == (S<"[">()))
                    {
                        auto const items118 = unpack((G<"email._header_value_parser.get_domain_literal">())(value), 2);
                        token = items118[0];
                        value = items118[1];
                        if (!leader.is_none())
                        {
                            token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                        }
                        domain.method("append", { token });
                        return py_tuple({ domain, value });
                    }
                    try
                    {
                        auto const items120 = unpack((G<"email._header_value_parser.get_dot_atom">())(value), 2);
                        token = items120[0];
                        value = items120[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler119;
                        if (matches(handler119.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            auto const items121 = unpack((G<"email._header_value_parser.get_atom">())(value), 2);
                            token = items121[0];
                            value = items121[1];
                        }
                        else
                            py_reraise(handler119.exception);
                    }
                    if ((value.truthy() && (value[I<0>()] == (S<"@">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"Invalid Domain">()));
                    }
                    if (!leader.is_none())
                    {
                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                    }
                    domain.method("append", { token });
                    if ((value.truthy() && (value[I<0>()] == (S<".">()))))
                    {
                        domain.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"domain is not a dot-atom (contains CFWS)">()) });
                        if (domain[I<0>()].attr("token_type") == (S<"dot-atom">()))
                        {
                            domain.set_item(B<"slice">()(none(), none(), none()), domain[I<0>()]);
                        }
                        while ((value.truthy() && (value[I<0>()] == (S<".">()))))
                        {
                            domain.method("append", { G<"email._header_value_parser.DOT">() });
                            auto const items123 = unpack((G<"email._header_value_parser.get_atom">())(value[B<"slice">()(I<1>(), none(), none())]), 2);
                            token = items123[0];
                            value = items123[1];
                            domain.method("append", { token });
                        }
                    }
                    return py_tuple({ domain, value });
                }));

            define("email._header_value_parser", "get_addr_spec", w.function("get_addr_spec", "get_addr_spec", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py addr_spec;
                    py token;
                    addr_spec = (G<"email._header_value_parser.AddrSpec">())();
                    auto const items124 = unpack((G<"email._header_value_parser.get_local_part">())(value), 2);
                    token = items124[0];
                    value = items124[1];
                    addr_spec.method("append", { token });
                    if (((!value.truthy()) || (value[I<0>()] != (S<"@">()))))
                    {
                        addr_spec.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"addr-spec local part with no domain">()) });
                        return py_tuple({ addr_spec, value });
                    }
                    addr_spec.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"@">(), S<"address-at-symbol">()) });
                    auto const items125 = unpack((G<"email._header_value_parser.get_domain">())(value[B<"slice">()(I<1>(), none(), none())]), 2);
                    token = items125[0];
                    value = items125[1];
                    addr_spec.method("append", { token });
                    return py_tuple({ addr_spec, value });
                }));

            define("email._header_value_parser", "get_obs_route", w.function("get_obs_route", "get_obs_route", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py obs_route;
                    py token;
                    obs_route = (G<"email._header_value_parser.ObsRoute">())();
                    while ((value.truthy() && ([&]() -> py { py const t = boolean(value[I<0>()] == (S<",">())); return t.truthy() ? t : boolean((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])); }()).truthy()))
                    {
                        if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                        {
                            auto const items127 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                            token = items127[0];
                            value = items127[1];
                            obs_route.method("append", { token });
                        }
                        else if (value[I<0>()] == (S<",">()))
                        {
                            obs_route.method("append", { G<"email._header_value_parser.ListSeparator">() });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                    }
                    if (((!value.truthy()) || (value[I<0>()] != (S<"@">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected obs-route domain but found '{}'">()).method("format", { value })));
                    }
                    obs_route.method("append", { G<"email._header_value_parser.RouteComponentMarker">() });
                    auto const items128 = unpack((G<"email._header_value_parser.get_domain">())(value[B<"slice">()(I<1>(), none(), none())]), 2);
                    token = items128[0];
                    value = items128[1];
                    obs_route.method("append", { token });
                    while ((value.truthy() && (value[I<0>()] == (S<",">()))))
                    {
                        obs_route.method("append", { G<"email._header_value_parser.ListSeparator">() });
                        value = value[B<"slice">()(I<1>(), none(), none())];
                        if (!value.truthy())
                        {
                            break;
                        }
                        if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                        {
                            auto const items130 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                            token = items130[0];
                            value = items130[1];
                            obs_route.method("append", { token });
                        }
                        if (!value.truthy())
                        {
                            break;
                        }
                        if (value[I<0>()] == (S<"@">()))
                        {
                            obs_route.method("append", { G<"email._header_value_parser.RouteComponentMarker">() });
                            auto const items131 = unpack((G<"email._header_value_parser.get_domain">())(value[B<"slice">()(I<1>(), none(), none())]), 2);
                            token = items131[0];
                            value = items131[1];
                            obs_route.method("append", { token });
                        }
                    }
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"end of header while parsing obs-route">()));
                    }
                    if (value[I<0>()] != (S<":">()))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected ':' marking end of obs-route but found '{}'">()).method("format", { value })));
                    }
                    obs_route.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<":">(), S<"end-of-obs-route-marker">()) });
                    return py_tuple({ obs_route, value[B<"slice">()(I<1>(), none(), none())] });
                }));

            define("email._header_value_parser", "get_angle_addr", w.function("get_angle_addr", "get_angle_addr", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py angle_addr;
                    py token;
                    angle_addr = (G<"email._header_value_parser.AngleAddr">())();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items132 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items132[0];
                        value = items132[1];
                        angle_addr.method("append", { token });
                    }
                    if (((!value.truthy()) || (value[I<0>()] != (S<"<">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected angle-addr but found '{}'">()).method("format", { value })));
                    }
                    angle_addr.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"<">(), S<"angle-addr-start">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    // Although it is not legal per RFC5322, SMTP uses '<>' in certain
                    // circumstances.
                    if ((value.truthy() && (value[I<0>()] == (S<">">()))))
                    {
                        angle_addr.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<">">(), S<"angle-addr-end">()) });
                        angle_addr.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"null addr-spec in angle-addr">()) });
                        value = value[B<"slice">()(I<1>(), none(), none())];
                        return py_tuple({ angle_addr, value });
                    }
                    try
                    {
                        auto const items134 = unpack((G<"email._header_value_parser.get_addr_spec">())(value), 2);
                        token = items134[0];
                        value = items134[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler133;
                        if (matches(handler133.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            try
                            {
                                auto const items136 = unpack((G<"email._header_value_parser.get_obs_route">())(value), 2);
                                token = items136[0];
                                value = items136[1];
                                angle_addr.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"obsolete route specification in angle-addr">()) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler135;
                                if (matches(handler135.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected addr-spec or obs-route but found '{}'">()).method("format", { value })));
                                }
                                else
                                    py_reraise(handler135.exception);
                            }
                            angle_addr.method("append", { token });
                            auto const items137 = unpack((G<"email._header_value_parser.get_addr_spec">())(value), 2);
                            token = items137[0];
                            value = items137[1];
                        }
                        else
                            py_reraise(handler133.exception);
                    }
                    angle_addr.method("append", { token });
                    if ((value.truthy() && (value[I<0>()] == (S<">">()))))
                    {
                        value = value[B<"slice">()(I<1>(), none(), none())];
                    }
                    else
                    {
                        angle_addr.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"missing trailing '>' on angle-addr">()) });
                    }
                    angle_addr.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<">">(), S<"angle-addr-end">()) });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items138 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items138[0];
                        value = items138[1];
                        angle_addr.method("append", { token });
                    }
                    return py_tuple({ angle_addr, value });
                }));

            define("email._header_value_parser", "get_display_name", w.function("get_display_name", "get_display_name", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py display_name;
                    py token;
                    display_name = (G<"email._header_value_parser.DisplayName">())();
                    auto const items139 = unpack((G<"email._header_value_parser.get_phrase">())(value), 2);
                    token = items139[0];
                    value = items139[1];
                    display_name.method("extend", { token[B<"slice">()(none(), none(), none())] });
                    display_name.set_attr("defects", token.attr("defects")[B<"slice">()(none(), none(), none())]);
                    return py_tuple({ display_name, value });
                }));

            define("email._header_value_parser", "get_name_addr", w.function("get_name_addr", "get_name_addr", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py leader;
                    py name_addr;
                    py token;
                    name_addr = (G<"email._header_value_parser.NameAddr">())();
                    // Both the optional display name and the angle-addr can start with cfws.
                    leader = none();
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected name-addr but found '{}'">()).method("format", { value })));
                    }
                    if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                    {
                        auto const items140 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        leader = items140[0];
                        value = items140[1];
                        if (!value.truthy())
                        {
                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected name-addr but found '{}'">()).method("format", { leader })));
                        }
                    }
                    if (value[I<0>()] != (S<"<">()))
                    {
                        if ((G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()]))
                        {
                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected name-addr but found '{}'">()).method("format", { value })));
                        }
                        auto const items141 = unpack((G<"email._header_value_parser.get_display_name">())(value), 2);
                        token = items141[0];
                        value = items141[1];
                        if (!value.truthy())
                        {
                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected name-addr but found '{}'">()).method("format", { token })));
                        }
                        if (!leader.is_none())
                        {
                            if (isinstance(token[I<0>()], G<"email._header_value_parser.TokenList">()))
                            {
                                token[I<0>()].set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                            }
                            else
                            {
                                token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                            }
                            leader = none();
                        }
                        name_addr.method("append", { token });
                    }
                    auto const items142 = unpack((G<"email._header_value_parser.get_angle_addr">())(value), 2);
                    token = items142[0];
                    value = items142[1];
                    if (!leader.is_none())
                    {
                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                    }
                    name_addr.method("append", { token });
                    return py_tuple({ name_addr, value });
                }));

            define("email._header_value_parser", "get_mailbox", w.function("get_mailbox", "get_mailbox", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py mailbox;
                    py token;
                    // The only way to figure out if we are dealing with a name-addr or an
                    // addr-spec is to try parsing each one.
                    mailbox = (G<"email._header_value_parser.Mailbox">())();
                    try
                    {
                        auto const items144 = unpack((G<"email._header_value_parser.get_name_addr">())(value), 2);
                        token = items144[0];
                        value = items144[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler143;
                        if (matches(handler143.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            try
                            {
                                auto const items146 = unpack((G<"email._header_value_parser.get_addr_spec">())(value), 2);
                                token = items146[0];
                                value = items146[1];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler145;
                                if (matches(handler145.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected mailbox but found '{}'">()).method("format", { value })));
                                }
                                else
                                    py_reraise(handler145.exception);
                            }
                        }
                        else
                            py_reraise(handler143.exception);
                    }
                    if ((B<"any">())([&]() -> py { py const comprehension147 = py_list(); py x; for (py const& item : iterate{ token.attr("all_defects") }) { x = item; comprehension147.method("append", { boolean(isinstance(x, Mod<"email.errors">().attr("InvalidHeaderDefect"))) }); } return comprehension147; }()).truthy())
                    {
                        mailbox.set_attr("token_type", S<"invalid-mailbox">());
                    }
                    mailbox.method("append", { token });
                    return py_tuple({ mailbox, value });
                }));

            define("email._header_value_parser", "get_invalid_mailbox", w.function("get_invalid_mailbox", "get_invalid_mailbox", { py_positional("value"), py_positional("endchars") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py endchars = a[1];
                    py invalid_mailbox;
                    py token;
                    invalid_mailbox = (G<"email._header_value_parser.InvalidMailbox">())();
                    while ((value.truthy() && (!endchars.contains(value[I<0>()]))))
                    {
                        if ((G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()]))
                        {
                            invalid_mailbox.method("append", { (G<"email._header_value_parser.ValueTerminal">())(value[I<0>()], S<"misplaced-special">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                        else
                        {
                            auto const items149 = unpack((G<"email._header_value_parser.get_phrase">())(value), 2);
                            token = items149[0];
                            value = items149[1];
                            invalid_mailbox.method("append", { token });
                        }
                    }
                    return py_tuple({ invalid_mailbox, value });
                }));

            define("email._header_value_parser", "get_mailbox_list", w.function("get_mailbox_list", "get_mailbox_list", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py leader;
                    py mailbox;
                    py mailbox_list;
                    py token;
                    mailbox_list = (G<"email._header_value_parser.MailboxList">())();
                    while ((value.truthy() && (value[I<0>()] != (S<";">()))))
                    {
                        try
                        {
                            auto const items152 = unpack((G<"email._header_value_parser.get_mailbox">())(value), 2);
                            token = items152[0];
                            value = items152[1];
                            mailbox_list.method("append", { token });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler151;
                            if (matches(handler151.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                leader = none();
                                if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                                {
                                    auto const items153 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                                    leader = items153[0];
                                    value = items153[1];
                                    if (((!value.truthy()) || (S<",;">()).contains(value[I<0>()])))
                                    {
                                        mailbox_list.method("append", { leader });
                                        mailbox_list.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"empty element in mailbox-list">()) });
                                    }
                                    else
                                    {
                                        auto const items154 = unpack((G<"email._header_value_parser.get_invalid_mailbox">())(value, S<",;">()), 2);
                                        token = items154[0];
                                        value = items154[1];
                                        if (!leader.is_none())
                                        {
                                            token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                                        }
                                        mailbox_list.method("append", { token });
                                        mailbox_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"invalid mailbox in mailbox-list">()) });
                                    }
                                }
                                else if (value[I<0>()] == (S<",">()))
                                {
                                    mailbox_list.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"empty element in mailbox-list">()) });
                                }
                                else
                                {
                                    auto const items155 = unpack((G<"email._header_value_parser.get_invalid_mailbox">())(value, S<",;">()), 2);
                                    token = items155[0];
                                    value = items155[1];
                                    if (!leader.is_none())
                                    {
                                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                                    }
                                    mailbox_list.method("append", { token });
                                    mailbox_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"invalid mailbox in mailbox-list">()) });
                                }
                            }
                            else
                                py_reraise(handler151.exception);
                        }
                        if ((value.truthy() && (!(S<",;">()).contains(value[I<0>()]))))
                        {
                            // Crap after mailbox; treat it as an invalid mailbox.
                            // The mailbox info will still be available.
                            mailbox = mailbox_list[I<-1>()];
                            mailbox.set_attr("token_type", S<"invalid-mailbox">());
                            auto const items156 = unpack((G<"email._header_value_parser.get_invalid_mailbox">())(value, S<",;">()), 2);
                            token = items156[0];
                            value = items156[1];
                            mailbox.method("extend", { token });
                            mailbox_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"invalid mailbox in mailbox-list">()) });
                        }
                        if ((value.truthy() && (value[I<0>()] == (S<",">()))))
                        {
                            mailbox_list.method("append", { G<"email._header_value_parser.ListSeparator">() });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                    }
                    return py_tuple({ mailbox_list, value });
                }));

            define("email._header_value_parser", "get_group_list", w.function("get_group_list", "get_group_list", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py group_list;
                    py leader;
                    py token;
                    group_list = (G<"email._header_value_parser.GroupList">())();
                    if (!value.truthy())
                    {
                        group_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"end of header before group-list">()) });
                        return py_tuple({ group_list, value });
                    }
                    leader = none();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items157 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        leader = items157[0];
                        value = items157[1];
                        if (!value.truthy())
                        {
                            // This should never happen in email parsing, since CFWS-only is a
                            // legal alternative to group-list in a group, which is the only
                            // place group-list appears.
                            group_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"end of header in group-list">()) });
                            group_list.method("append", { leader });
                            return py_tuple({ group_list, value });
                        }
                        if (value[I<0>()] == (S<";">()))
                        {
                            group_list.method("append", { leader });
                            return py_tuple({ group_list, value });
                        }
                    }
                    auto const items158 = unpack((G<"email._header_value_parser.get_mailbox_list">())(value), 2);
                    token = items158[0];
                    value = items158[1];
                    if (py{ token.attr("all_mailboxes").size() } == (I<0>()))
                    {
                        if (!leader.is_none())
                        {
                            group_list.method("append", { leader });
                        }
                        group_list.method("extend", { token });
                        group_list.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"group-list with empty entries">()) });
                        return py_tuple({ group_list, value });
                    }
                    if (!leader.is_none())
                    {
                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                    }
                    group_list.method("append", { token });
                    return py_tuple({ group_list, value });
                }));

            define("email._header_value_parser", "get_group", w.function("get_group", "get_group", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py group;
                    py token;
                    group = (G<"email._header_value_parser.Group">())();
                    auto const items159 = unpack((G<"email._header_value_parser.get_display_name">())(value), 2);
                    token = items159[0];
                    value = items159[1];
                    if (((!value.truthy()) || (value[I<0>()] != (S<":">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected ':' at end of group display name but found '{}'">()).method("format", { value })));
                    }
                    group.method("append", { token });
                    group.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<":">(), S<"group-display-name-terminator">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    if ((value.truthy() && (value[I<0>()] == (S<";">()))))
                    {
                        group.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<";">(), S<"group-terminator">()) });
                        return py_tuple({ group, value[B<"slice">()(I<1>(), none(), none())] });
                    }
                    auto const items160 = unpack((G<"email._header_value_parser.get_group_list">())(value), 2);
                    token = items160[0];
                    value = items160[1];
                    group.method("append", { token });
                    if (!value.truthy())
                    {
                        group.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"end of header in group">()) });
                    }
                    else if (value[I<0>()] != (S<";">()))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected ';' at end of group but found {}">()).method("format", { value })));
                    }
                    group.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<";">(), S<"group-terminator">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items161 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items161[0];
                        value = items161[1];
                        group.method("append", { token });
                    }
                    return py_tuple({ group, value });
                }));

            define("email._header_value_parser", "get_address", w.function("get_address", "get_address", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py address;
                    py token;
                    // The formal grammar isn't very helpful when parsing an address.  mailbox
                    // and group, especially when allowing for obsolete forms, start off very
                    // similarly.  It is only when you reach one of @, <, or : that you know
                    // what you've got.  So, we try each one in turn, starting with the more
                    // likely of the two.  We could perhaps make this more efficient by looking
                    // for a phrase and then branching based on the next character, but that
                    // would be a premature optimization.
                    address = (G<"email._header_value_parser.Address">())();
                    try
                    {
                        auto const items163 = unpack((G<"email._header_value_parser.get_group">())(value), 2);
                        token = items163[0];
                        value = items163[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler162;
                        if (matches(handler162.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            try
                            {
                                auto const items165 = unpack((G<"email._header_value_parser.get_mailbox">())(value), 2);
                                token = items165[0];
                                value = items165[1];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler164;
                                if (matches(handler164.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected address but found '{}'">()).method("format", { value })));
                                }
                                else
                                    py_reraise(handler164.exception);
                            }
                        }
                        else
                            py_reraise(handler162.exception);
                    }
                    address.method("append", { token });
                    return py_tuple({ address, value });
                }));

            define("email._header_value_parser", "get_address_list", w.function("get_address_list", "get_address_list", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py address_list;
                    py leader;
                    py token;
                    address_list = (G<"email._header_value_parser.AddressList">())();
                    while (value.truthy())
                    {
                        try
                        {
                            auto const items168 = unpack((G<"email._header_value_parser.get_address">())(value), 2);
                            token = items168[0];
                            value = items168[1];
                            address_list.method("append", { token });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler167;
                            if (matches(handler167.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                leader = none();
                                if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                                {
                                    auto const items169 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                                    leader = items169[0];
                                    value = items169[1];
                                    if (((!value.truthy()) || (value[I<0>()] == (S<",">()))))
                                    {
                                        address_list.method("append", { leader });
                                        address_list.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"address-list entry with no content">()) });
                                    }
                                    else
                                    {
                                        auto const items170 = unpack((G<"email._header_value_parser.get_invalid_mailbox">())(value, S<",">()), 2);
                                        token = items170[0];
                                        value = items170[1];
                                        if (!leader.is_none())
                                        {
                                            token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                                        }
                                        address_list.method("append", { (G<"email._header_value_parser.Address">())(py_list({ token })) });
                                        address_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"invalid address in address-list">()) });
                                    }
                                }
                                else if (value[I<0>()] == (S<",">()))
                                {
                                    address_list.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"empty element in address-list">()) });
                                }
                                else
                                {
                                    auto const items171 = unpack((G<"email._header_value_parser.get_invalid_mailbox">())(value, S<",">()), 2);
                                    token = items171[0];
                                    value = items171[1];
                                    if (!leader.is_none())
                                    {
                                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                                    }
                                    address_list.method("append", { (G<"email._header_value_parser.Address">())(py_list({ token })) });
                                    address_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"invalid address in address-list">()) });
                                }
                            }
                            else
                                py_reraise(handler167.exception);
                        }
                        if ((value.truthy() && (value[I<0>()] != (S<",">()))))
                        {
                            // Crap after address: add it to the address list
                            // as an invalid mailbox
                            auto const items172 = unpack((G<"email._header_value_parser.get_invalid_mailbox">())(value, S<",">()), 2);
                            token = items172[0];
                            value = items172[1];
                            address_list.method("append", { (G<"email._header_value_parser.Address">())(py_list({ token })) });
                            address_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"invalid address in address-list">()) });
                        }
                        if (value.truthy())
                        {
                            address_list.method("append", { G<"email._header_value_parser.ListSeparator">() });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                    }
                    return py_tuple({ address_list, value });
                }));

            define("email._header_value_parser", "get_no_fold_literal", w.function("get_no_fold_literal", "get_no_fold_literal", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py no_fold_literal;
                    py token;
                    no_fold_literal = (G<"email._header_value_parser.NoFoldLiteral">())();
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected no-fold-literal but found '{}'">()).method("format", { value })));
                    }
                    if (value[I<0>()] != (S<"[">()))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected '[' at the start of no-fold-literal but found '{}'">()).method("format", { value })));
                    }
                    no_fold_literal.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"[">(), S<"no-fold-literal-start">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    auto const items173 = unpack((G<"email._header_value_parser.get_dtext">())(value), 2);
                    token = items173[0];
                    value = items173[1];
                    no_fold_literal.method("append", { token });
                    if (((!value.truthy()) || (value[I<0>()] != (S<"]">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected ']' at the end of no-fold-literal but found '{}'">()).method("format", { value })));
                    }
                    no_fold_literal.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"]">(), S<"no-fold-literal-end">()) });
                    return py_tuple({ no_fold_literal, value[B<"slice">()(I<1>(), none(), none())] });
                }));

            define("email._header_value_parser", "get_msg_id", w.function("get_msg_id", "get_msg_id", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py msg_id;
                    py token;
                    msg_id = (G<"email._header_value_parser.MsgID">())();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items174 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items174[0];
                        value = items174[1];
                        msg_id.method("append", { token });
                    }
                    if (((!value.truthy()) || (value[I<0>()] != (S<"<">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected msg-id but found '{}'">()).method("format", { value })));
                    }
                    msg_id.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"<">(), S<"msg-id-start">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    // Parse id-left.
                    try
                    {
                        auto const items176 = unpack((G<"email._header_value_parser.get_dot_atom_text">())(value), 2);
                        token = items176[0];
                        value = items176[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler175;
                        if (matches(handler175.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            try
                            {
                                // obs-id-left is same as local-part of add-spec.
                                auto const items178 = unpack((G<"email._header_value_parser.get_obs_local_part">())(value), 2);
                                token = items178[0];
                                value = items178[1];
                                msg_id.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"obsolete id-left in msg-id">()) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler177;
                                if (matches(handler177.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected dot-atom-text or obs-id-left but found '{}'">()).method("format", { value })));
                                }
                                else
                                    py_reraise(handler177.exception);
                            }
                        }
                        else
                            py_reraise(handler175.exception);
                    }
                    msg_id.method("append", { token });
                    if (((!value.truthy()) || (value[I<0>()] != (S<"@">()))))
                    {
                        msg_id.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"msg-id with no id-right">()) });
                        // Even though there is no id-right, if the local part
                        // ends with `>` let's just parse it too and return
                        // along with the defect.
                        if ((value.truthy() && (value[I<0>()] == (S<">">()))))
                        {
                            msg_id.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<">">(), S<"msg-id-end">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                        return py_tuple({ msg_id, value });
                    }
                    msg_id.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"@">(), S<"address-at-symbol">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    // Parse id-right.
                    try
                    {
                        auto const items180 = unpack((G<"email._header_value_parser.get_dot_atom_text">())(value), 2);
                        token = items180[0];
                        value = items180[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler179;
                        if (matches(handler179.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            try
                            {
                                auto const items182 = unpack((G<"email._header_value_parser.get_no_fold_literal">())(value), 2);
                                token = items182[0];
                                value = items182[1];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler181;
                                if (matches(handler181.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                {
                                    try
                                    {
                                        auto const items184 = unpack((G<"email._header_value_parser.get_domain">())(value), 2);
                                        token = items184[0];
                                        value = items184[1];
                                        msg_id.attr("defects").method("append", { (Mod<"email.errors">().attr("ObsoleteHeaderDefect"))(S<"obsolete id-right in msg-id">()) });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler183;
                                        if (matches(handler183.exception, Mod<"email.errors">().attr("HeaderParseError")))
                                        {
                                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected dot-atom-text, no-fold-literal or obs-id-right but found '{}'">()).method("format", { value })));
                                        }
                                        else
                                            py_reraise(handler183.exception);
                                    }
                                }
                                else
                                    py_reraise(handler181.exception);
                            }
                        }
                        else
                            py_reraise(handler179.exception);
                    }
                    msg_id.method("append", { token });
                    if ((value.truthy() && (value[I<0>()] == (S<">">()))))
                    {
                        value = value[B<"slice">()(I<1>(), none(), none())];
                    }
                    else
                    {
                        msg_id.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"missing trailing '>' on msg-id">()) });
                    }
                    msg_id.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<">">(), S<"msg-id-end">()) });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items185 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items185[0];
                        value = items185[1];
                        msg_id.method("append", { token });
                    }
                    return py_tuple({ msg_id, value });
                }));

            define("email._header_value_parser", "parse_message_id", w.function("parse_message_id", "parse_message_id", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py ex;
                    py message_id;
                    py token;
                    message_id = (G<"email._header_value_parser.MessageID">())();
                    {
                        bool otherwise186 = false;
                        try
                        {
                            auto const items187 = unpack((G<"email._header_value_parser.get_msg_id">())(value), 2);
                            token = items187[0];
                            value = items187[1];
                            message_id.method("append", { token });
                            otherwise186 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler186;
                            if (matches(handler186.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                ex = handler186.exception;
                                token = (G<"email._header_value_parser.get_unstructured">())(value);
                                message_id = (G<"email._header_value_parser.InvalidMessageID">())(token);
                                message_id.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Invalid msg-id: {!r}">()).method("format", { ex })) });
                            }
                            else
                                py_reraise(handler186.exception);
                        }
                        if (otherwise186)
                        {
                            // Value after parsing a valid msg_id should be None.
                            if (value.truthy())
                            {
                                message_id.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Unexpected {!r}">()).method("format", { value })) });
                            }
                        }
                    }
                    return message_id;
                }));

            define("email._header_value_parser", "parse_message_ids", w.function("parse_message_ids", "parse_message_ids", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py ex;
                    py message_id_list;
                    py token;
                    message_id_list = (G<"email._header_value_parser.MessageIDList">())();
                    while (value.truthy())
                    {
                        if (value[I<0>()] == (S<",">()))
                        {
                            // message id list separated with commas - this is invalid,
                            // but happens rather frequently in the wild
                            message_id_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"comma in msg-id list">()) });
                            message_id_list.method("append", { (G<"email._header_value_parser.WhiteSpaceTerminal">())(S<" ">(), S<"invalid-comma-replacement">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                            continue;
                        }
                        try
                        {
                            auto const items190 = unpack((G<"email._header_value_parser.get_msg_id">())(value), 2);
                            token = items190[0];
                            value = items190[1];
                            message_id_list.method("append", { token });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler189;
                            if (matches(handler189.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                ex = handler189.exception;
                                token = (G<"email._header_value_parser.get_unstructured">())(value);
                                message_id_list.method("append", { (G<"email._header_value_parser.InvalidMessageID">())(token) });
                                message_id_list.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Invalid msg-id: {!r}">()).method("format", { ex })) });
                                break;
                            }
                            else
                                py_reraise(handler189.exception);
                        }
                    }
                    return message_id_list;
                }));

            // 
            // XXX: As I begin to add additional header parsers, I'm realizing we probably
            // have two level of parser routines: the get_XXX methods that get a token in
            // the grammar, and parse_XXX methods that parse an entire field value.  So
            // get_address_list above should really be a parse_ method, as probably should
            // be get_unstructured.
            // 
            define("email._header_value_parser", "parse_mime_version", w.function("parse_mime_version", "parse_mime_version", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py digits;
                    py mime_version;
                    py token;
                    // The [CFWS] is implicit in the RFC 2045 BNF.
                    // XXX: This routine is a bit verbose, should factor out a get_int method.
                    mime_version = (G<"email._header_value_parser.MIMEVersion">())();
                    if (!value.truthy())
                    {
                        mime_version.attr("defects").method("append", { (Mod<"email.errors">().attr("HeaderMissingRequiredValue"))(S<"Missing MIME version number (eg: 1.0)">()) });
                        return mime_version;
                    }
                    if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                    {
                        auto const items191 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items191[0];
                        value = items191[1];
                        mime_version.method("append", { token });
                        if (!value.truthy())
                        {
                            mime_version.attr("defects").method("append", { (Mod<"email.errors">().attr("HeaderMissingRequiredValue"))(S<"Expected MIME version number but found only CFWS">()) });
                        }
                    }
                    digits = S<"">();
                    while ((value.truthy() && (value[I<0>()] != (S<".">())) && (!(G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))))
                    {
                        digits = inplace(NEOS_PY_ADD, digits, value[I<0>()]);
                        value = value[B<"slice">()(I<1>(), none(), none())];
                    }
                    if (!digits.method("isdigit").truthy())
                    {
                        mime_version.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Expected MIME major version number but found {!r}">()).method("format", { digits })) });
                        mime_version.method("append", { (G<"email._header_value_parser.ValueTerminal">())(digits, S<"xtext">()) });
                    }
                    else
                    {
                        mime_version.set_attr("major", (B<"int">())(digits));
                        mime_version.method("append", { (G<"email._header_value_parser.ValueTerminal">())(digits, S<"digits">()) });
                    }
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items193 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items193[0];
                        value = items193[1];
                        mime_version.method("append", { token });
                    }
                    if (((!value.truthy()) || (value[I<0>()] != (S<".">()))))
                    {
                        if (!mime_version.attr("major").is_none())
                        {
                            mime_version.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Incomplete MIME version; found only major number">()) });
                        }
                        if (value.truthy())
                        {
                            mime_version.method("append", { (G<"email._header_value_parser.ValueTerminal">())(value, S<"xtext">()) });
                        }
                        return mime_version;
                    }
                    mime_version.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<".">(), S<"version-separator">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items194 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items194[0];
                        value = items194[1];
                        mime_version.method("append", { token });
                    }
                    if (!value.truthy())
                    {
                        if (!mime_version.attr("major").is_none())
                        {
                            mime_version.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Incomplete MIME version; found only major number">()) });
                        }
                        return mime_version;
                    }
                    digits = S<"">();
                    while ((value.truthy() && (!(G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))))
                    {
                        digits = inplace(NEOS_PY_ADD, digits, value[I<0>()]);
                        value = value[B<"slice">()(I<1>(), none(), none())];
                    }
                    if (!digits.method("isdigit").truthy())
                    {
                        mime_version.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Expected MIME minor version number but found {!r}">()).method("format", { digits })) });
                        mime_version.method("append", { (G<"email._header_value_parser.ValueTerminal">())(digits, S<"xtext">()) });
                    }
                    else
                    {
                        mime_version.set_attr("minor", (B<"int">())(digits));
                        mime_version.method("append", { (G<"email._header_value_parser.ValueTerminal">())(digits, S<"digits">()) });
                    }
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items196 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items196[0];
                        value = items196[1];
                        mime_version.method("append", { token });
                    }
                    if (value.truthy())
                    {
                        mime_version.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Excess non-CFWS text after MIME version">()) });
                        mime_version.method("append", { (G<"email._header_value_parser.ValueTerminal">())(value, S<"xtext">()) });
                    }
                    return mime_version;
                }));

            define("email._header_value_parser", "get_invalid_parameter", w.function("get_invalid_parameter", "get_invalid_parameter", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py invalid_parameter;
                    py token;
                    invalid_parameter = (G<"email._header_value_parser.InvalidParameter">())();
                    while ((value.truthy() && (value[I<0>()] != (S<";">()))))
                    {
                        if ((G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()]))
                        {
                            invalid_parameter.method("append", { (G<"email._header_value_parser.ValueTerminal">())(value[I<0>()], S<"misplaced-special">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                        else
                        {
                            auto const items198 = unpack((G<"email._header_value_parser.get_phrase">())(value), 2);
                            token = items198[0];
                            value = items198[1];
                            invalid_parameter.method("append", { token });
                        }
                    }
                    return py_tuple({ invalid_parameter, value });
                }));

            define("email._header_value_parser", "get_ttext", w.function("get_ttext", "get_ttext", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py m;
                    py ttext;
                    m = (G<"email._header_value_parser._non_token_end_matcher">())(value);
                    if (!m.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected ttext but found '{}'">()).method("format", { value })));
                    }
                    ttext = m.method("group");
                    value = value[B<"slice">()(py{ ttext.size() }, none(), none())];
                    ttext = (G<"email._header_value_parser.ValueTerminal">())(ttext, S<"ttext">());
                    (G<"email._header_value_parser._validate_xtext">())(ttext);
                    return py_tuple({ ttext, value });
                }));

            define("email._header_value_parser", "get_token", w.function("get_token", "get_token", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py mtoken;
                    py token;
                    mtoken = (G<"email._header_value_parser.Token">())();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items199 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items199[0];
                        value = items199[1];
                        mtoken.method("append", { token });
                    }
                    if ((value.truthy() && (G<"email._header_value_parser.TOKEN_ENDS">()).contains(value[I<0>()])))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected token but found '{}'">()).method("format", { value })));
                    }
                    auto const items200 = unpack((G<"email._header_value_parser.get_ttext">())(value), 2);
                    token = items200[0];
                    value = items200[1];
                    mtoken.method("append", { token });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items201 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items201[0];
                        value = items201[1];
                        mtoken.method("append", { token });
                    }
                    return py_tuple({ mtoken, value });
                }));

            define("email._header_value_parser", "get_attrtext", w.function("get_attrtext", "get_attrtext", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py attrtext;
                    py m;
                    m = (G<"email._header_value_parser._non_attribute_end_matcher">())(value);
                    if (!m.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected attrtext but found {!r}">()).method("format", { value })));
                    }
                    attrtext = m.method("group");
                    value = value[B<"slice">()(py{ attrtext.size() }, none(), none())];
                    attrtext = (G<"email._header_value_parser.ValueTerminal">())(attrtext, S<"attrtext">());
                    (G<"email._header_value_parser._validate_xtext">())(attrtext);
                    return py_tuple({ attrtext, value });
                }));

            define("email._header_value_parser", "get_attribute", w.function("get_attribute", "get_attribute", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py attribute;
                    py token;
                    attribute = (G<"email._header_value_parser.Attribute">())();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items202 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items202[0];
                        value = items202[1];
                        attribute.method("append", { token });
                    }
                    if ((value.truthy() && (G<"email._header_value_parser.ATTRIBUTE_ENDS">()).contains(value[I<0>()])))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected token but found '{}'">()).method("format", { value })));
                    }
                    auto const items203 = unpack((G<"email._header_value_parser.get_attrtext">())(value), 2);
                    token = items203[0];
                    value = items203[1];
                    attribute.method("append", { token });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items204 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items204[0];
                        value = items204[1];
                        attribute.method("append", { token });
                    }
                    return py_tuple({ attribute, value });
                }));

            define("email._header_value_parser", "get_extended_attrtext", w.function("get_extended_attrtext", "get_extended_attrtext", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py attrtext;
                    py m;
                    m = (G<"email._header_value_parser._non_extended_attribute_end_matcher">())(value);
                    if (!m.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected extended attrtext but found {!r}">()).method("format", { value })));
                    }
                    attrtext = m.method("group");
                    value = value[B<"slice">()(py{ attrtext.size() }, none(), none())];
                    attrtext = (G<"email._header_value_parser.ValueTerminal">())(attrtext, S<"extended-attrtext">());
                    (G<"email._header_value_parser._validate_xtext">())(attrtext);
                    return py_tuple({ attrtext, value });
                }));

            define("email._header_value_parser", "get_extended_attribute", w.function("get_extended_attribute", "get_extended_attribute", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py attribute;
                    py token;
                    // XXX: should we have an ExtendedAttribute TokenList?
                    attribute = (G<"email._header_value_parser.Attribute">())();
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items205 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items205[0];
                        value = items205[1];
                        attribute.method("append", { token });
                    }
                    if ((value.truthy() && (G<"email._header_value_parser.EXTENDED_ATTRIBUTE_ENDS">()).contains(value[I<0>()])))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"expected token but found '{}'">()).method("format", { value })));
                    }
                    auto const items206 = unpack((G<"email._header_value_parser.get_extended_attrtext">())(value), 2);
                    token = items206[0];
                    value = items206[1];
                    attribute.method("append", { token });
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items207 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items207[0];
                        value = items207[1];
                        attribute.method("append", { token });
                    }
                    return py_tuple({ attribute, value });
                }));

            define("email._header_value_parser", "get_section", w.function("get_section", "get_section", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py digits;
                    py section;
                    section = (G<"email._header_value_parser.Section">())();
                    if (((!value.truthy()) || (value[I<0>()] != (S<"*">()))))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"Expected section but found {}">()).method("format", { value })));
                    }
                    section.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"*">(), S<"section-marker">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    if (((!value.truthy()) || (!value[I<0>()].method("isdigit").truthy())))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"Expected section number but found {}">()).method("format", { value })));
                    }
                    digits = S<"">();
                    while ((value.truthy() && value[I<0>()].method("isdigit").truthy()))
                    {
                        digits = inplace(NEOS_PY_ADD, digits, value[I<0>()]);
                        value = value[B<"slice">()(I<1>(), none(), none())];
                    }
                    if (((digits[I<0>()] == (S<"0">())) && (digits != (S<"0">()))))
                    {
                        section.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"section number has an invalid leading 0">()) });
                    }
                    section.set_attr("number", (B<"int">())(digits));
                    section.method("append", { (G<"email._header_value_parser.ValueTerminal">())(digits, S<"digits">()) });
                    return py_tuple({ section, value });
                }));

            define("email._header_value_parser", "get_value", w.function("get_value", "get_value", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py leader;
                    py token;
                    py v;
                    v = (G<"email._header_value_parser.Value">())();
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"Expected value but found end of string">()));
                    }
                    leader = none();
                    if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                    {
                        auto const items209 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        leader = items209[0];
                        value = items209[1];
                    }
                    if (!value.truthy())
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"Expected value but found only {}">()).method("format", { leader })));
                    }
                    if (value[I<0>()] == (S<"\"">()))
                    {
                        auto const items210 = unpack((G<"email._header_value_parser.get_quoted_string">())(value), 2);
                        token = items210[0];
                        value = items210[1];
                    }
                    else
                    {
                        auto const items211 = unpack((G<"email._header_value_parser.get_extended_attribute">())(value), 2);
                        token = items211[0];
                        value = items211[1];
                    }
                    if (!leader.is_none())
                    {
                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                    }
                    v.method("append", { token });
                    return py_tuple({ v, value });
                }));

            define("email._header_value_parser", "get_parameter", w.function("get_parameter", "get_parameter", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py appendto;
                    py inner_value;
                    py param;
                    py qstring;
                    py remainder;
                    py rest;
                    py semi_valid;
                    py t;
                    py token;
                    py v;
                    // It is possible CFWS would also be implicitly allowed between the section
                    // and the 'extended-attribute' marker (the '*') , but we've never seen that
                    // in the wild and we will therefore ignore the possibility.
                    param = (G<"email._header_value_parser.Parameter">())();
                    auto const items212 = unpack((G<"email._header_value_parser.get_attribute">())(value), 2);
                    token = items212[0];
                    value = items212[1];
                    param.method("append", { token });
                    if (((!value.truthy()) || (value[I<0>()] == (S<";">()))))
                    {
                        param.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Parameter contains name ({}) but no value">()).method("format", { token })) });
                        return py_tuple({ param, value });
                    }
                    if (value[I<0>()] == (S<"*">()))
                    {
                        try
                        {
                            auto const items214 = unpack((G<"email._header_value_parser.get_section">())(value), 2);
                            token = items214[0];
                            value = items214[1];
                            param.set_attr("sectioned", boolean(true));
                            param.method("append", { token });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler213;
                            if (matches(handler213.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                            }
                            else
                                py_reraise(handler213.exception);
                        }
                        if (!value.truthy())
                        {
                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"Incomplete parameter">()));
                        }
                        if (value[I<0>()] == (S<"*">()))
                        {
                            param.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"*">(), S<"extended-parameter-marker">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                            param.set_attr("extended", boolean(true));
                        }
                    }
                    if (value[I<0>()] != (S<"=">()))
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"Parameter not followed by '='">()));
                    }
                    param.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"=">(), S<"parameter-separator">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    if ((value.truthy() && (G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()])))
                    {
                        auto const items215 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                        token = items215[0];
                        value = items215[1];
                        param.method("append", { token });
                    }
                    remainder = none();
                    appendto = param;
                    if ((param.attr("extended").truthy() && value.truthy() && (value[I<0>()] == (S<"\"">()))))
                    {
                        // Now for some serious hackery to handle the common invalid case of
                        // double quotes around an extended value.  We also accept (with defect)
                        // a value marked as encoded that isn't really.
                        auto const items216 = unpack((G<"email._header_value_parser.get_quoted_string">())(value), 2);
                        qstring = items216[0];
                        remainder = items216[1];
                        inner_value = qstring.attr("stripped_value");
                        semi_valid = boolean(false);
                        if (param.attr("section_number") == (I<0>()))
                        {
                            if ((inner_value.truthy() && (inner_value[I<0>()] == (S<"'">()))))
                            {
                                semi_valid = boolean(true);
                            }
                            else
                            {
                                auto const items217 = unpack((G<"email._header_value_parser.get_attrtext">())(inner_value), 2);
                                token = items217[0];
                                rest = items217[1];
                                if ((rest.truthy() && (rest[I<0>()] == (S<"'">()))))
                                {
                                    semi_valid = boolean(true);
                                }
                            }
                        }
                        else
                        {
                            {
                                bool otherwise218 = false;
                                try
                                {
                                    auto const items219 = unpack((G<"email._header_value_parser.get_extended_attrtext">())(inner_value), 2);
                                    token = items219[0];
                                    rest = items219[1];
                                    otherwise218 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler218;
                                    {
                                    }
                                }
                                if (otherwise218)
                                {
                                    if (!rest.truthy())
                                    {
                                        semi_valid = boolean(true);
                                    }
                                }
                            }
                        }
                        if (semi_valid.truthy())
                        {
                            param.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Quoted string value for extended parameter is invalid">()) });
                            param.method("append", { qstring });
                            for (py const& item : iterate{ qstring })
                            {
                                t = item;
                                if (t.attr("token_type") == (S<"bare-quoted-string">()))
                                {
                                    t.set_item(B<"slice">()(none(), none(), none()), py_list());
                                    appendto = t;
                                    break;
                                }
                            }
                            value = inner_value;
                        }
                        else
                        {
                            remainder = none();
                            param.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Parameter marked as extended but appears to have a quoted string value that is non-encoded">()) });
                        }
                    }
                    if ((value.truthy() && (value[I<0>()] == (S<"'">()))))
                    {
                        token = none();
                    }
                    else
                    {
                        auto const items221 = unpack((G<"email._header_value_parser.get_value">())(value), 2);
                        token = items221[0];
                        value = items221[1];
                    }
                    if (((!param.attr("extended").truthy()) || (param.attr("section_number") > (I<0>()))))
                    {
                        if (((!value.truthy()) || (value[I<0>()] != (S<"'">()))))
                        {
                            appendto.method("append", { token });
                            if (!remainder.is_none())
                            {
                                if (value.truthy())
                                    py_raise(B<"AssertionError">()(value));
                                value = remainder;
                            }
                            return py_tuple({ param, value });
                        }
                        param.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Apparent initial-extended-value but attribute was not marked as extended or was not initial section">()) });
                    }
                    if (!value.truthy())
                    {
                        // Assume the charset/lang is missing and the token is the value.
                        param.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Missing required charset/lang delimiters">()) });
                        appendto.method("append", { token });
                        if (remainder.is_none())
                        {
                            return py_tuple({ param, value });
                        }
                    }
                    else
                    {
                        if (!token.is_none())
                        {
                            for (py const& item : iterate{ token })
                            {
                                t = item;
                                if (t.attr("token_type") == (S<"extended-attrtext">()))
                                {
                                    break;
                                }
                            }
                            t.attr("token_type") == (S<"attrtext">());
                            appendto.method("append", { t });
                            param.set_attr("charset", t.attr("value"));
                        }
                        if (value[I<0>()] != (S<"'">()))
                        {
                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"Expected RFC2231 char/lang encoding delimiter, but found {!r}">()).method("format", { value })));
                        }
                        appendto.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"'">(), S<"RFC2231-delimiter">()) });
                        value = value[B<"slice">()(I<1>(), none(), none())];
                        if ((value.truthy() && (value[I<0>()] != (S<"'">()))))
                        {
                            auto const items223 = unpack((G<"email._header_value_parser.get_attrtext">())(value), 2);
                            token = items223[0];
                            value = items223[1];
                            appendto.method("append", { token });
                            param.set_attr("lang", token.attr("value"));
                            if (((!value.truthy()) || (value[I<0>()] != (S<"'">()))))
                            {
                                py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"Expected RFC2231 char/lang encoding delimiter, but found {}">()).method("format", { value })));
                            }
                        }
                        appendto.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"'">(), S<"RFC2231-delimiter">()) });
                        value = value[B<"slice">()(I<1>(), none(), none())];
                    }
                    if (!remainder.is_none())
                    {
                        // Treat the rest of value as bare quoted string content.
                        v = (G<"email._header_value_parser.Value">())();
                        while (value.truthy())
                        {
                            if ((G<"email._header_value_parser.WSP">()).contains(value[I<0>()]))
                            {
                                auto const items225 = unpack((G<"email._header_value_parser.get_fws">())(value), 2);
                                token = items225[0];
                                value = items225[1];
                            }
                            else if (value[I<0>()] == (S<"\"">()))
                            {
                                token = (G<"email._header_value_parser.ValueTerminal">())(S<"\"">(), S<"DQUOTE">());
                                value = value[B<"slice">()(I<1>(), none(), none())];
                            }
                            else
                            {
                                auto const items226 = unpack((G<"email._header_value_parser.get_qcontent">())(value), 2);
                                token = items226[0];
                                value = items226[1];
                            }
                            v.method("append", { token });
                        }
                        token = v;
                    }
                    else
                    {
                        auto const items227 = unpack((G<"email._header_value_parser.get_value">())(value), 2);
                        token = items227[0];
                        value = items227[1];
                    }
                    appendto.method("append", { token });
                    if (!remainder.is_none())
                    {
                        if (value.truthy())
                            py_raise(B<"AssertionError">()(value));
                        value = remainder;
                    }
                    return py_tuple({ param, value });
                }));

            define("email._header_value_parser", "parse_mime_parameters", w.function("parse_mime_parameters", "parse_mime_parameters", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py leader;
                    py mime_parameters;
                    py param;
                    py token;
                    mime_parameters = (G<"email._header_value_parser.MimeParameters">())();
                    while (value.truthy())
                    {
                        try
                        {
                            auto const items230 = unpack((G<"email._header_value_parser.get_parameter">())(value), 2);
                            token = items230[0];
                            value = items230[1];
                            mime_parameters.method("append", { token });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler229;
                            if (matches(handler229.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                leader = none();
                                if ((G<"email._header_value_parser.CFWS_LEADER">()).contains(value[I<0>()]))
                                {
                                    auto const items231 = unpack((G<"email._header_value_parser.get_cfws">())(value), 2);
                                    leader = items231[0];
                                    value = items231[1];
                                }
                                if (!value.truthy())
                                {
                                    mime_parameters.method("append", { leader });
                                    return mime_parameters;
                                }
                                if (value[I<0>()] == (S<";">()))
                                {
                                    if (!leader.is_none())
                                    {
                                        mime_parameters.method("append", { leader });
                                    }
                                    mime_parameters.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"parameter entry with no content">()) });
                                }
                                else
                                {
                                    auto const items232 = unpack((G<"email._header_value_parser.get_invalid_parameter">())(value), 2);
                                    token = items232[0];
                                    value = items232[1];
                                    if (leader.truthy())
                                    {
                                        token.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ leader }));
                                    }
                                    mime_parameters.method("append", { token });
                                    mime_parameters.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"invalid parameter {!r}">()).method("format", { token })) });
                                }
                            }
                            else
                                py_reraise(handler229.exception);
                        }
                        if ((value.truthy() && (value[I<0>()] != (S<";">()))))
                        {
                            // Junk after the otherwise valid parameter.  Mark it as
                            // invalid, but it will have a value.
                            param = mime_parameters[I<-1>()];
                            param.set_attr("token_type", S<"invalid-parameter">());
                            auto const items233 = unpack((G<"email._header_value_parser.get_invalid_parameter">())(value), 2);
                            token = items233[0];
                            value = items233[1];
                            param.method("extend", { token });
                            mime_parameters.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"parameter with invalid trailing text {!r}">()).method("format", { token })) });
                        }
                        if (value.truthy())
                        {
                            // Must be a ';' at this point.
                            mime_parameters.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<";">(), S<"parameter-separator">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                    }
                    return mime_parameters;
                }));

            define("email._header_value_parser", "_find_mime_parameters", w.function("_find_mime_parameters", "_find_mime_parameters", { py_positional("tokenlist"), py_positional("value") }, [](arguments a) -> py
                {
                    py tokenlist = a[0];
                    py value = a[1];
                    py token;
                    while ((value.truthy() && (value[I<0>()] != (S<";">()))))
                    {
                        if ((G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()]))
                        {
                            tokenlist.method("append", { (G<"email._header_value_parser.ValueTerminal">())(value[I<0>()], S<"misplaced-special">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                        else
                        {
                            auto const items235 = unpack((G<"email._header_value_parser.get_phrase">())(value), 2);
                            token = items235[0];
                            value = items235[1];
                            tokenlist.method("append", { token });
                        }
                    }
                    if (!value.truthy())
                    {
                        return none();
                    }
                    tokenlist.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<";">(), S<"parameter-separator">()) });
                    tokenlist.method("append", { (G<"email._header_value_parser.parse_mime_parameters">())(value[B<"slice">()(I<1>(), none(), none())]) });
                    return none();
                }));

            define("email._header_value_parser", "parse_content_type_header", w.function("parse_content_type_header", "parse_content_type_header", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py ctype;
                    py token;
                    ctype = (G<"email._header_value_parser.ContentType">())();
                    if (!value.truthy())
                    {
                        ctype.attr("defects").method("append", { (Mod<"email.errors">().attr("HeaderMissingRequiredValue"))(S<"Missing content type specification">()) });
                        return ctype;
                    }
                    try
                    {
                        auto const items237 = unpack((G<"email._header_value_parser.get_token">())(value), 2);
                        token = items237[0];
                        value = items237[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler236;
                        if (matches(handler236.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            ctype.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Expected content maintype but found {!r}">()).method("format", { value })) });
                            (G<"email._header_value_parser._find_mime_parameters">())(ctype, value);
                            return ctype;
                        }
                        else
                            py_reraise(handler236.exception);
                    }
                    ctype.method("append", { token });
                    // XXX: If we really want to follow the formal grammar we should make
                    // mantype and subtype specialized TokenLists here.  Probably not worth it.
                    if (((!value.truthy()) || (value[I<0>()] != (S<"/">()))))
                    {
                        ctype.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Invalid content type">()) });
                        if (value.truthy())
                        {
                            (G<"email._header_value_parser._find_mime_parameters">())(ctype, value);
                        }
                        return ctype;
                    }
                    ctype.set_attr("maintype", token.attr("value").method("strip").method("lower"));
                    ctype.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<"/">(), S<"content-type-separator">()) });
                    value = value[B<"slice">()(I<1>(), none(), none())];
                    try
                    {
                        auto const items239 = unpack((G<"email._header_value_parser.get_token">())(value), 2);
                        token = items239[0];
                        value = items239[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler238;
                        if (matches(handler238.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            ctype.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Expected content subtype but found {!r}">()).method("format", { value })) });
                            (G<"email._header_value_parser._find_mime_parameters">())(ctype, value);
                            return ctype;
                        }
                        else
                            py_reraise(handler238.exception);
                    }
                    ctype.method("append", { token });
                    ctype.set_attr("subtype", token.attr("value").method("strip").method("lower"));
                    if (!value.truthy())
                    {
                        return ctype;
                    }
                    if (value[I<0>()] != (S<";">()))
                    {
                        ctype.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Only parameters are valid after content type, but found {!r}">()).method("format", { value })) });
                        // The RFC requires that a syntactically invalid content-type be treated
                        // as text/plain.  Perhaps we should postel this, but we should probably
                        // only do that if we were checking the subtype value against IANA.
                        del_attr(ctype, "maintype");
                        del_attr(ctype, "subtype");
                        (G<"email._header_value_parser._find_mime_parameters">())(ctype, value);
                        return ctype;
                    }
                    ctype.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<";">(), S<"parameter-separator">()) });
                    ctype.method("append", { (G<"email._header_value_parser.parse_mime_parameters">())(value[B<"slice">()(I<1>(), none(), none())]) });
                    return ctype;
                }));

            define("email._header_value_parser", "parse_content_disposition_header", w.function("parse_content_disposition_header", "parse_content_disposition_header", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py disp_header;
                    py token;
                    disp_header = (G<"email._header_value_parser.ContentDisposition">())();
                    if (!value.truthy())
                    {
                        disp_header.attr("defects").method("append", { (Mod<"email.errors">().attr("HeaderMissingRequiredValue"))(S<"Missing content disposition">()) });
                        return disp_header;
                    }
                    try
                    {
                        auto const items241 = unpack((G<"email._header_value_parser.get_token">())(value), 2);
                        token = items241[0];
                        value = items241[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler240;
                        if (matches(handler240.exception, Mod<"email.errors">().attr("HeaderParseError")))
                        {
                            disp_header.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Expected content disposition but found {!r}">()).method("format", { value })) });
                            (G<"email._header_value_parser._find_mime_parameters">())(disp_header, value);
                            return disp_header;
                        }
                        else
                            py_reraise(handler240.exception);
                    }
                    disp_header.method("append", { token });
                    disp_header.set_attr("content_disposition", token.attr("value").method("strip").method("lower"));
                    if (!value.truthy())
                    {
                        return disp_header;
                    }
                    if (value[I<0>()] != (S<";">()))
                    {
                        disp_header.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Only parameters are valid after content disposition, but found {!r}">()).method("format", { value })) });
                        (G<"email._header_value_parser._find_mime_parameters">())(disp_header, value);
                        return disp_header;
                    }
                    disp_header.method("append", { (G<"email._header_value_parser.ValueTerminal">())(S<";">(), S<"parameter-separator">()) });
                    disp_header.method("append", { (G<"email._header_value_parser.parse_mime_parameters">())(value[B<"slice">()(I<1>(), none(), none())]) });
                    return disp_header;
                }));

            define("email._header_value_parser", "parse_content_transfer_encoding_header", w.function("parse_content_transfer_encoding_header", "parse_content_transfer_encoding_header", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py cte_header;
                    py token;
                    // We should probably validate the values, since the list is fixed.
                    cte_header = (G<"email._header_value_parser.ContentTransferEncoding">())();
                    if (!value.truthy())
                    {
                        cte_header.attr("defects").method("append", { (Mod<"email.errors">().attr("HeaderMissingRequiredValue"))(S<"Missing content transfer encoding">()) });
                        return cte_header;
                    }
                    {
                        bool otherwise242 = false;
                        try
                        {
                            auto const items243 = unpack((G<"email._header_value_parser.get_token">())(value), 2);
                            token = items243[0];
                            value = items243[1];
                            otherwise242 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler242;
                            if (matches(handler242.exception, Mod<"email.errors">().attr("HeaderParseError")))
                            {
                                cte_header.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))((S<"Expected content transfer encoding but found {!r}">()).method("format", { value })) });
                            }
                            else
                                py_reraise(handler242.exception);
                        }
                        if (otherwise242)
                        {
                            cte_header.method("append", { token });
                            cte_header.set_attr("cte", token.attr("value").method("strip").method("lower"));
                        }
                    }
                    if (!value.truthy())
                    {
                        return cte_header;
                    }
                    while (value.truthy())
                    {
                        cte_header.attr("defects").method("append", { (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Extra text after content transfer encoding">()) });
                        if ((G<"email._header_value_parser.PHRASE_ENDS">()).contains(value[I<0>()]))
                        {
                            cte_header.method("append", { (G<"email._header_value_parser.ValueTerminal">())(value[I<0>()], S<"misplaced-special">()) });
                            value = value[B<"slice">()(I<1>(), none(), none())];
                        }
                        else
                        {
                            auto const items245 = unpack((G<"email._header_value_parser.get_phrase">())(value), 2);
                            token = items245[0];
                            value = items245[1];
                            cte_header.method("append", { token });
                        }
                    }
                    return cte_header;
                }));

            // 
            // Header folding
            // 
            // Header folding is complex, with lots of rules and corner cases.  The
            // following code does its best to obey the rules and handle the corner
            // cases, but you can be sure there are few bugs:)
            // 
            // This folder generally canonicalizes as it goes, preferring the stringified
            // version of each token.  The tokens contain information that supports the
            // folder, including which tokens can be encoded in which ways.
            // 
            // Folded text is accumulated in a simple list of strings ('lines'), each
            // one of which should be less than policy.max_line_length ('maxlen').
            // 
            define("email._header_value_parser", "_steal_trailing_WSP_if_exists", w.function("_steal_trailing_WSP_if_exists", "_steal_trailing_WSP_if_exists", { py_positional("lines") }, [](arguments a) -> py
                {
                    py lines = a[0];
                    py wsp;
                    wsp = S<"">();
                    if ((lines.truthy() && lines[I<-1>()].truthy() && (G<"email._header_value_parser.WSP">()).contains(lines[I<-1>()][I<-1>()])))
                    {
                        wsp = lines[I<-1>()][I<-1>()];
                        lines.set_item(I<-1>(), lines[I<-1>()][B<"slice">()(none(), I<-1>(), none())]);
                        // gh-142006: if the line is now empty, remove it entirely.
                        if (!lines[I<-1>()].truthy())
                        {
                            lines.method("pop");
                        }
                    }
                    return wsp;
                }));

            define("email._header_value_parser", "_refold_parse_tree", w.function("_refold_parse_tree", "_refold_parse_tree", { py_positional("parse_tree"), py_keyword("policy") }, [](arguments a) -> py
                {
                    py parse_tree = a[0];
                    py policy = a[1];
                    py charset;
                    py encoded_part;
                    py encoding;
                    py end_ew_not_allowed;
                    py last_charset;
                    py last_ew;
                    py last_word_is_ew;
                    py lines;
                    py maxlen;
                    py newline;
                    py newparts;
                    py part;
                    py parts;
                    py tstr;
                    py want_encoding;
                    py wrap_as_ew_blocked;
                    // max_line_length 0/None means no limit, ie: infinitely long.
                    maxlen = [&]() -> py { py const t = policy.attr("max_line_length"); return t.truthy() ? t : Mod<"sys">().attr("maxsize"); }();
                    encoding = (policy.attr("utf8").truthy() ? S<"utf-8">() : S<"us-ascii">());
                    lines = py_list({ S<"">() });
                    last_word_is_ew = boolean(false);
                    last_ew = none();
                    // points to the encoded word's first character
                    last_charset = none();
                    wrap_as_ew_blocked = I<0>();
                    want_encoding = boolean(false);
                    end_ew_not_allowed = (G<"email._header_value_parser.Terminal">())(S<"">(), S<"wrap_as_ew_blocked">());
                    parts = (B<"list">())(parse_tree);
                    while (parts.truthy())
                    {
                        part = parts.method("pop", { I<0>() });
                        if (part.is(end_ew_not_allowed))
                        {
                            wrap_as_ew_blocked = inplace(NEOS_PY_SUBTRACT, wrap_as_ew_blocked, I<1>());
                            continue;
                        }
                        tstr = (B<"str">())(part);
                        if (!want_encoding.truthy())
                        {
                            if (py_tuple({ S<"ptext">(), S<"vtext">() }).contains(part.attr("token_type")))
                            {
                                // Encode if tstr contains special characters.
                                want_encoding = boolean(!(G<"email._header_value_parser.SPECIALSNL">()).method("isdisjoint", { tstr }).truthy());
                            }
                            else
                            {
                                // Encode if tstr contains newlines.
                                want_encoding = boolean(!(G<"email._header_value_parser.NLSET">()).method("isdisjoint", { tstr }).truthy());
                            }
                        }
                        try
                        {
                            tstr.method("encode", { encoding });
                            charset = encoding;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler247;
                            if (matches(handler247.exception, B<"UnicodeEncodeError">()))
                            {
                                if ((B<"any">())([&]() -> py { py const comprehension248 = py_list(); py x; for (py const& item : iterate{ part.attr("all_defects") }) { x = item; comprehension248.method("append", { boolean(isinstance(x, Mod<"email.errors">().attr("UndecodableBytesDefect"))) }); } return comprehension248; }()).truthy())
                                {
                                    charset = S<"unknown-8bit">();
                                }
                                else
                                {
                                    // If policy.utf8 is false this should really be taken from a
                                    // 'charset' property on the policy.
                                    charset = S<"utf-8">();
                                }
                                want_encoding = boolean(true);
                            }
                            else
                                py_reraise(handler247.exception);
                        }
                        if (part.attr("token_type") == (S<"mime-parameters">()))
                        {
                            // Mime parameter folding (using RFC2231) is extra special.
                            (G<"email._header_value_parser._fold_mime_parameters">())(part, lines, maxlen, encoding);
                            last_word_is_ew = boolean(false);
                            continue;
                        }
                        if ((want_encoding.truthy() && (!wrap_as_ew_blocked.truthy())))
                        {
                            if (!part.attr("as_ew_allowed").truthy())
                            {
                                want_encoding = boolean(false);
                                last_ew = none();
                                if (part.attr("syntactic_break").truthy())
                                {
                                    encoded_part = part.method("fold", {  }, { { "policy", policy } })[B<"slice">()(none(), -py{ policy.attr("linesep").size() }, none())];
                                    if (!encoded_part.contains(policy.attr("linesep")))
                                    {
                                        // It fits on a single line
                                        if (py{ encoded_part.size() } > (maxlen - py{ lines[I<-1>()].size() }))
                                        {
                                            // But not on this one, so start a new one.
                                            newline = (G<"email._header_value_parser._steal_trailing_WSP_if_exists">())(lines);
                                            // XXX what if encoded_part has no leading FWS?
                                            lines.method("append", { newline });
                                        }
                                        {
                                            py const target249 = lines;
                                            py const key249 = I<-1>();
                                            target249.set_item(key249, inplace(NEOS_PY_ADD, target249[key249], encoded_part));
                                        }
                                        last_word_is_ew = boolean(false);
                                        continue;
                                    }
                                }
                            }
                            // Either this is not a major syntactic break, so we don't
                            // want it on a line by itself even if it fits, or it
                            // doesn't fit on a line by itself.  Either way, fall through
                            // to unpacking the subparts and wrapping them.
                            if (!has_attr(part, "encode"))
                            {
                                // It's not a Terminal, do each piece individually.
                                parts = (B<"list">())(part) + parts;
                                want_encoding = boolean(false);
                                continue;
                            }
                            else if (part.attr("as_ew_allowed").truthy())
                            {
                                // It's a terminal, wrap it as an encoded word, possibly
                                // combining it with previously encoded words if allowed.
                                if (((!last_ew.is_none()) && (charset != last_charset) && ([&]() -> py { py const t = boolean(last_charset == (S<"unknown-8bit">())); return t.truthy() ? t : [&]() -> py { py const t = boolean(last_charset == (S<"utf-8">())); return !t.truthy() ? t : boolean(charset != (S<"us-ascii">())); }(); }()).truthy()))
                                {
                                    last_ew = none();
                                }
                                last_ew = (G<"email._header_value_parser._fold_as_ew">())(tstr, lines, maxlen, last_ew, part.attr("ew_combine_allowed"), charset, last_word_is_ew);
                                last_word_is_ew = boolean(true);
                                last_charset = charset;
                                want_encoding = boolean(false);
                                continue;
                            }
                            else
                            {
                                // It's a terminal which should be kept non-encoded
                                // (e.g. a ListSeparator).
                                last_ew = none();
                                want_encoding = boolean(false);
                            }
                        }
                        // fall through
                        if (py{ tstr.size() } <= (maxlen - py{ lines[I<-1>()].size() }))
                        {
                            {
                                py const target250 = lines;
                                py const key250 = I<-1>();
                                target250.set_item(key250, inplace(NEOS_PY_ADD, target250[key250], tstr));
                            }
                            last_word_is_ew = [&]() -> py { py const t = last_word_is_ew; return !t.truthy() ? t : boolean(!(B<"bool">())(tstr.method("strip", { G<"email._header_value_parser._WSP">() })).truthy()); }();
                            continue;
                        }
                        // This part is too long to fit.  The RFC wants us to break at
                        // "major syntactic breaks", so unless we don't consider this
                        // to be one, check if it will fit on the next line by itself.
                        if ((part.attr("syntactic_break").truthy() && ((py{ tstr.size() } + (I<1>())) <= maxlen)))
                        {
                            newline = (G<"email._header_value_parser._steal_trailing_WSP_if_exists">())(lines);
                            if ((newline.truthy() || part.method("startswith_fws").truthy()))
                            {
                                lines.method("append", { newline + tstr });
                                last_word_is_ew = [&]() -> py { py const t = last_word_is_ew; return !t.truthy() ? t : boolean(!(B<"bool">())(lines[I<-1>()].method("strip", { G<"email._header_value_parser._WSP">() })).truthy()); }();
                                last_ew = none();
                                continue;
                            }
                        }
                        if (!has_attr(part, "encode"))
                        {
                            // It's not a terminal, try folding the subparts.
                            newparts = (B<"list">())(part);
                            if (part.attr("token_type") == (S<"bare-quoted-string">()))
                            {
                                // To fold a quoted string we need to create a list of terminal
                                // tokens that will render the leading and trailing quotes
                                // and use quoted pairs in the value as appropriate.
                                newparts = (py_list({ (G<"email._header_value_parser.ValueTerminal">())(S<"\"">(), S<"ptext">()) }) + ([&]() -> py { py const comprehension251 = py_list(); py p; for (py const& item : iterate{ newparts }) { p = item; comprehension251.method("append", { (G<"email._header_value_parser.ValueTerminal">())((G<"email._header_value_parser.make_quoted_pairs">())(p), S<"ptext">()) }); } return comprehension251; }())) + py_list({ (G<"email._header_value_parser.ValueTerminal">())(S<"\"">(), S<"ptext">()) });
                            }
                            if (part.attr("token_type") == (S<"comment">()))
                            {
                                newparts = (py_list({ (G<"email._header_value_parser.ValueTerminal">())(S<"(">(), S<"ptext">()) }) + ([&]() -> py { py const comprehension252 = py_list(); py p; for (py const& item : iterate{ newparts }) { p = item; comprehension252.method("append", { (p.attr("token_type") == (S<"ptext">()) ? (G<"email._header_value_parser.ValueTerminal">())((G<"email._header_value_parser.make_parenthesis_pairs">())(p), S<"ptext">()) : p) }); } return comprehension252; }())) + py_list({ (G<"email._header_value_parser.ValueTerminal">())(S<")">(), S<"ptext">()) });
                            }
                            if (!part.attr("as_ew_allowed").truthy())
                            {
                                wrap_as_ew_blocked = inplace(NEOS_PY_ADD, wrap_as_ew_blocked, I<1>());
                                newparts.method("append", { end_ew_not_allowed });
                            }
                            parts = newparts + parts;
                            continue;
                        }
                        if ((part.attr("as_ew_allowed").truthy() && (!wrap_as_ew_blocked.truthy())))
                        {
                            // It doesn't need CTE encoding, but encode it anyway so we can
                            // wrap it.
                            parts.method("insert", { I<0>(), part });
                            want_encoding = boolean(true);
                            continue;
                        }
                        // We can't figure out how to wrap, it, so give up.
                        newline = (G<"email._header_value_parser._steal_trailing_WSP_if_exists">())(lines);
                        if ((newline.truthy() || part.method("startswith_fws").truthy()))
                        {
                            lines.method("append", { newline + tstr });
                        }
                        else
                        {
                            // We can't fold it onto the next line either...
                            {
                                py const target253 = lines;
                                py const key253 = I<-1>();
                                target253.set_item(key253, inplace(NEOS_PY_ADD, target253[key253], tstr));
                            }
                        }
                        last_word_is_ew = [&]() -> py { py const t = last_word_is_ew; return !t.truthy() ? t : boolean(!(B<"bool">())(tstr.method("strip", { G<"email._header_value_parser._WSP">() })).truthy()); }();
                    }
                    return policy.attr("linesep").method("join", { lines }) + policy.attr("linesep");
                }));

            define("email._header_value_parser", "_fold_as_ew", w.function("_fold_as_ew", "_fold_as_ew", { py_positional("to_encode"), py_positional("lines"), py_positional("maxlen"), py_positional("last_ew"), py_positional("ew_combine_allowed"), py_positional("charset"), py_positional("last_word_is_ew") }, [](arguments a) -> py
                {
                    py to_encode = a[0];
                    py lines = a[1];
                    py maxlen = a[2];
                    py last_ew = a[3];
                    py ew_combine_allowed = a[4];
                    py charset = a[5];
                    py last_word_is_ew = a[6];
                    py chrome_len;
                    py encode_as;
                    py encoded_word;
                    py excess;
                    py leading_whitespace;
                    py leading_wsp;
                    py len_without_wsp;
                    py new_last_ew;
                    py newline;
                    py remaining_space;
                    py text_space;
                    py to_encode_word;
                    py trailing_wsp;
                    if (((!last_ew.is_none()) && ew_combine_allowed.truthy()))
                    {
                        to_encode = (B<"str">())((G<"email._header_value_parser.get_unstructured">())(lines[I<-1>()][B<"slice">()(last_ew, none(), none())] + to_encode));
                        lines.set_item(I<-1>(), lines[I<-1>()][B<"slice">()(none(), last_ew, none())]);
                    }
                    else if (last_word_is_ew.truthy())
                    {
                        // If we are following up an encoded word with another encoded word,
                        // any white space between the two will be ignored when decoded.
                        // Therefore, we encode all to-be-displayed whitespace in the second
                        // encoded word.
                        len_without_wsp = py{ lines[I<-1>()].method("rstrip", { G<"email._header_value_parser._WSP">() }).size() };
                        leading_whitespace = lines[I<-1>()][B<"slice">()(len_without_wsp, none(), none())];
                        lines.set_item(I<-1>(), lines[I<-1>()][B<"slice">()(none(), len_without_wsp, none())] + (leading_whitespace.truthy() ? S<" ">() : S<"">()));
                        to_encode = leading_whitespace + to_encode;
                    }
                    else if ((G<"email._header_value_parser.WSP">()).contains(to_encode[I<0>()]))
                    {
                        // We're joining this to non-encoded text, so don't encode
                        // the leading blank.
                        leading_wsp = to_encode[I<0>()];
                        to_encode = to_encode[B<"slice">()(I<1>(), none(), none())];
                        if (py{ lines[I<-1>()].size() } == maxlen)
                        {
                            lines.method("append", { (G<"email._header_value_parser._steal_trailing_WSP_if_exists">())(lines) });
                        }
                        {
                            py const target254 = lines;
                            py const key254 = I<-1>();
                            target254.set_item(key254, inplace(NEOS_PY_ADD, target254[key254], leading_wsp));
                        }
                    }
                    trailing_wsp = S<"">();
                    if ((G<"email._header_value_parser.WSP">()).contains(to_encode[I<-1>()]))
                    {
                        // Likewise for the trailing space.
                        trailing_wsp = to_encode[I<-1>()];
                        to_encode = to_encode[B<"slice">()(none(), I<-1>(), none())];
                    }
                    new_last_ew = (last_ew.is_none() ? py{ lines[I<-1>()].size() } : last_ew);
                    encode_as = (charset == (S<"us-ascii">()) ? S<"utf-8">() : charset);
                    // The RFC2047 chrome takes up 7 characters plus the length
                    // of the charset name.
                    chrome_len = py{ encode_as.size() } + (I<7>());
                    if ((chrome_len + (I<1>())) >= maxlen)
                    {
                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"max_line_length is too small to fit an encoded word">()));
                    }
                    while (to_encode.truthy())
                    {
                        remaining_space = maxlen - py{ lines[I<-1>()].size() };
                        text_space = remaining_space - chrome_len;
                        if (text_space <= (I<0>()))
                        {
                            newline = (G<"email._header_value_parser._steal_trailing_WSP_if_exists">())(lines);
                            lines.method("append", { [&]() -> py { py const t = newline; return t.truthy() ? t : S<" ">(); }() });
                            new_last_ew = py{ lines[I<-1>()].size() };
                            continue;
                        }
                        to_encode_word = to_encode[B<"slice">()(none(), text_space, none())];
                        encoded_word = (Mod<"email._encoded_words">().attr("encode")).call({ to_encode_word }, { { "charset", encode_as } });
                        excess = py{ encoded_word.size() } - remaining_space;
                        while (excess > (I<0>()))
                        {
                            // Since the chunk to encode is guaranteed to fit into less than 100 characters,
                            // shrinking it by one at a time shouldn't take long.
                            to_encode_word = to_encode_word[B<"slice">()(none(), I<-1>(), none())];
                            encoded_word = (Mod<"email._encoded_words">().attr("encode")).call({ to_encode_word }, { { "charset", encode_as } });
                            excess = py{ encoded_word.size() } - remaining_space;
                        }
                        {
                            py const target257 = lines;
                            py const key257 = I<-1>();
                            target257.set_item(key257, inplace(NEOS_PY_ADD, target257[key257], encoded_word));
                        }
                        to_encode = to_encode[B<"slice">()(py{ to_encode_word.size() }, none(), none())];
                        if (to_encode.truthy())
                        {
                            lines.method("append", { S<" ">() });
                            new_last_ew = py{ lines[I<-1>()].size() };
                        }
                    }
                    {
                        py const target258 = lines;
                        py const key258 = I<-1>();
                        target258.set_item(key258, inplace(NEOS_PY_ADD, target258[key258], trailing_wsp));
                    }
                    return (ew_combine_allowed.truthy() ? new_last_ew : none());
                }));

            define("email._header_value_parser", "_fold_mime_parameters", w.function("_fold_mime_parameters", "_fold_mime_parameters", { py_positional("part"), py_positional("lines"), py_positional("maxlen"), py_positional("encoding") }, [](arguments a) -> py
                {
                    py part = a[0];
                    py lines = a[1];
                    py maxlen = a[2];
                    py encoding = a[3];
                    py charset;
                    py chrome_len;
                    py encoded_value;
                    py encoding_required;
                    py error_handler;
                    py extra_chrome;
                    py maxchars;
                    py name;
                    py partial;
                    py section;
                    py splitpoint;
                    py tstr;
                    py value;
                    // Special case for RFC2231 encoding: start from decoded values and use
                    // RFC2231 encoding iff needed.
                    // 
                    // Note that the 1 and 2s being added to the length calculations are
                    // accounting for the possibly-needed spaces and semicolons we'll be adding.
                    // 
                    for (py const& item : iterate{ part.attr("params") })
                    {
                        auto const items260 = unpack(item, 2);
                        name = items260[0];
                        value = items260[1];
                        // XXX What if this ';' puts us over maxlen the first time through the
                        // loop?  We should split the header value onto a newline in that case,
                        // but to do that we need to recognize the need earlier or reparse the
                        // header, so I'm going to ignore that bug for now.  It'll only put us
                        // one character over.
                        if (!lines[I<-1>()].method("rstrip").method("endswith", { S<";">() }).truthy())
                        {
                            {
                                py const target261 = lines;
                                py const key261 = I<-1>();
                                target261.set_item(key261, inplace(NEOS_PY_ADD, target261[key261], S<";">()));
                            }
                        }
                        charset = encoding;
                        error_handler = S<"strict">();
                        try
                        {
                            value.method("encode", { encoding });
                            encoding_required = boolean(false);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler262;
                            if (matches(handler262.exception, B<"UnicodeEncodeError">()))
                            {
                                encoding_required = boolean(true);
                                if ((Mod<"email.utils">().attr("_has_surrogates"))(value).truthy())
                                {
                                    charset = S<"unknown-8bit">();
                                    error_handler = S<"surrogateescape">();
                                }
                                else
                                {
                                    charset = S<"utf-8">();
                                }
                            }
                            else
                                py_reraise(handler262.exception);
                        }
                        if (encoding_required.truthy())
                        {
                            encoded_value = (Mod<"urllib">().attr("parse")).method("quote", { value }, { { "safe", S<"">() }, { "errors", error_handler } });
                            tstr = (S<"{}*={}''{}">()).method("format", { name, charset, encoded_value });
                        }
                        else
                        {
                            tstr = (S<"{}={}">()).method("format", { name, (G<"email._header_value_parser.quote_string">())(value) });
                        }
                        if (((py{ lines[I<-1>()].size() } + py{ tstr.size() }) + (I<1>())) < maxlen)
                        {
                            lines.set_item(I<-1>(), (lines[I<-1>()] + (S<" ">())) + tstr);
                            continue;
                        }
                        else if ((py{ tstr.size() } + (I<2>())) <= maxlen)
                        {
                            lines.method("append", { (S<" ">()) + tstr });
                            continue;
                        }
                        // We need multiple sections.  We are allowed to mix encoded and
                        // non-encoded sections, but we aren't going to.  We'll encode them all.
                        section = I<0>();
                        extra_chrome = charset + (S<"''">());
                        while (value.truthy())
                        {
                            chrome_len = ((py{ name.size() } + py{ (B<"str">())(section).size() }) + (I<3>())) + py{ extra_chrome.size() };
                            if (maxlen <= (chrome_len + (I<3>())))
                            {
                                // We need room for the leading blank, the trailing semicolon,
                                // and at least one character of the value.  If we don't
                                // have that, we'd be stuck, so in that case fall back to
                                // the RFC standard width.
                                maxlen = I<78>();
                            }
                            py const value264 = (maxlen - chrome_len) - (I<2>());
                            splitpoint = value264;
                            maxchars = value264;
                            while (boolean(true).truthy())
                            {
                                partial = value[B<"slice">()(none(), splitpoint, none())];
                                encoded_value = (Mod<"urllib">().attr("parse")).method("quote", { partial }, { { "safe", S<"">() }, { "errors", error_handler } });
                                if (py{ encoded_value.size() } <= maxchars)
                                {
                                    break;
                                }
                                splitpoint = inplace(NEOS_PY_SUBTRACT, splitpoint, I<1>());
                            }
                            lines.method("append", { (S<" {}*{}*={}{}">()).method("format", { name, section, extra_chrome, encoded_value }) });
                            extra_chrome = S<"">();
                            section = inplace(NEOS_PY_ADD, section, I<1>());
                            value = value[B<"slice">()(splitpoint, none(), none())];
                            if (value.truthy())
                            {
                                {
                                    py const target266 = lines;
                                    py const key266 = I<-1>();
                                    target266.set_item(key266, inplace(NEOS_PY_ADD, target266[key266], S<";">()));
                                }
                            }
                        }
                    }
                    return none();
                }));
        }

        bool email__header_value_parser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email__header_value_parser();
                }
                if (auto const doc = email_header_value_parser_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("hexdigits", import_attribute("string", "hexdigits").release());
                aAttributes.emplace_back("itemgetter", import_attribute("operator", "itemgetter").release());
                aAttributes.emplace_back("_ew", import_attribute("email", "_encoded_words").release());
                aAttributes.emplace_back("errors", import_attribute("email", "errors").release());
                aAttributes.emplace_back("utils", import_attribute("email", "utils").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email._header_value_parser", email__header_value_parser_module);
    }
}
