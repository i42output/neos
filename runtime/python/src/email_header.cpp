/*
  email_header.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/header.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.header (CPython 3.13's Lib/email/header.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_header_docs.hpp"

namespace neos::python_runtime::detail::email_header_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.header.name"))
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
                py_raise("SystemError", std::string{ "email.header: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.header.name">()))
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
            if (modules_imported("email.header"))
                return py{ fn_import("email.header") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.header" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.header", email_header_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.header (Lib/email/header.py)

        void make_email_header()
        {
            static module_writer const& w = writer("email.header");
            // Copyright (C) 2002-2007 Python Software Foundation
            // Author: Ben Gertzfield, Barry Warsaw
            // Contact: email-sig@python.org
            define("email.header", "__all__", py_list({ S<"Header">(), S<"decode_header">(), S<"make_header">() }));
            define("email.header", "Charset", Mod<"email.charset">().attr("Charset"));
            define("email.header", "NL", S<"\n">());
            define("email.header", "SPACE", S<" ">());
            define("email.header", "BSPACE", py{ make_bytes(std::string{ "\x20", 1 }) });
            define("email.header", "SPACE8", (S<" ">()) * (I<8>()));
            define("email.header", "EMPTYSTRING", S<"">());
            define("email.header", "MAXLINELEN", I<78>());
            define("email.header", "FWS", S<" \t">());
            define("email.header", "USASCII", (G<"email.header.Charset">())(S<"us-ascii">()));
            define("email.header", "UTF8", (G<"email.header.Charset">())(S<"utf-8">()));
            // Match encoded-word strings in the form =?charset?q?Hello_World?=
            define("email.header", "ecre", (Mod<"re">()).method("compile", { S<"\n  =\\?                   # literal =?\n  (?P<charset>[^?]*?)   # non-greedy up to the next ? is the charset\n  \\?                    # literal ?\n  (?P<encoding>[qQbB])  # either a \"q\" or a \"b\", case insensitive\n  \\?                    # literal ?\n  (?P<encoded>.*?)      # non-greedy up to the next ?= is the encoded string\n  \\?=                   # literal ?=\n  ">(), (Mod<"re">().attr("VERBOSE")) | (Mod<"re">().attr("MULTILINE")) }));
            // Field name regexp, including trailing colon, but not separating whitespace,
            // according to RFC 2822.  Character range is from tilde to exclamation mark.
            // For use with .match()
            define("email.header", "fcre", (Mod<"re">()).method("compile", { S<"[\\041-\\176]+:$">() }));
            // Find a header embedded in a putative header value.  Used to check for
            // header injection attack.
            define("email.header", "_embedded_header", (Mod<"re">()).method("compile", { S<"\\n[^ \\t]+:">() }));
            // Helpers
            define("email.header", "_max_append", Mod<"email.quoprimime">().attr("_max_append"));
            define("email.header", "decode_header", w.function("decode_header", "decode_header", { py_positional("header") }, [](arguments a) -> py
                {
                    py header = a[0];
                    py charset;
                    py collapsed;
                    py d;
                    py decoded_words;
                    py droplist;
                    py encoded;
                    py encoded_string;
                    py encoding;
                    py first;
                    py last_charset;
                    py last_word;
                    py line;
                    py n;
                    py paderr;
                    py parts;
                    py unencoded;
                    py w;
                    py word;
                    py words;
                    // If it is a Header object, we can just return the encoded chunks.
                    if (has_attr(header, "_chunks"))
                    {
                        return [&]() -> py { py const comprehension1 = py_list(); py charset; py string; for (py const& item : iterate{ header.attr("_chunks") }) { auto const items2 = unpack(item, 2); string = items2[0]; charset = items2[1]; comprehension1.method("append", { py_tuple({ (Mod<"email.charset">().attr("_encode"))(string, (B<"str">())(charset)), (B<"str">())(charset) }) }); } return comprehension1; }();
                    }
                    // If no encoding, just return the header with no charset.
                    if (!(G<"email.header.ecre">()).method("search", { header }).truthy())
                    {
                        return py_list({ py_tuple({ header, none() }) });
                    }
                    // First step is to parse all the encoded parts into triplets of the form
                    // (encoded_string, encoding, charset).  For unencoded strings, the last
                    // two parts will be None.
                    words = py_list();
                    for (py const& item : iterate{ header.method("splitlines") })
                    {
                        line = item;
                        parts = (G<"email.header.ecre">()).method("split", { line });
                        first = boolean(true);
                        while (parts.truthy())
                        {
                            unencoded = parts.method("pop", { I<0>() });
                            if (first.truthy())
                            {
                                unencoded = unencoded.method("lstrip");
                                first = boolean(false);
                            }
                            if (unencoded.truthy())
                            {
                                words.method("append", { py_tuple({ unencoded, none(), none() }) });
                            }
                            if (parts.truthy())
                            {
                                charset = parts.method("pop", { I<0>() }).method("lower");
                                encoding = parts.method("pop", { I<0>() }).method("lower");
                                encoded = parts.method("pop", { I<0>() });
                                words.method("append", { py_tuple({ encoded, encoding, charset }) });
                            }
                        }
                    }
                    // Now loop over words and remove words that consist of whitespace
                    // between two encoded strings.
                    droplist = py_list();
                    for (py const& item : iterate{ (B<"enumerate">())(words) })
                    {
                        auto const items6 = unpack(item, 2);
                        n = items6[0];
                        w = items6[1];
                        if (((n > (I<1>())) && w[I<1>()].truthy() && words[n - (I<2>())][I<1>()].truthy() && words[n - (I<1>())][I<0>()].method("isspace").truthy()))
                        {
                            droplist.method("append", { n - (I<1>()) });
                        }
                    }
                    for (py const& item : iterate{ (B<"reversed">())(droplist) })
                    {
                        d = item;
                        del_item(words, d);
                    }
                    // The next step is to decode each encoded word by applying the reverse
                    // base64 or quopri transformation.  decoded_words is now a list of the
                    // form (decoded_word, charset).
                    decoded_words = py_list();
                    for (py const& item : iterate{ words })
                    {
                        auto const items9 = unpack(item, 3);
                        encoded_string = items9[0];
                        encoding = items9[1];
                        charset = items9[2];
                        if (encoding.is_none())
                        {
                            // This is an unencoded word.
                            decoded_words.method("append", { py_tuple({ encoded_string, charset }) });
                        }
                        else if (encoding == (S<"q">()))
                        {
                            word = (Mod<"email.quoprimime">().attr("header_decode"))(encoded_string);
                            decoded_words.method("append", { py_tuple({ word, charset }) });
                        }
                        else if (encoding == (S<"b">()))
                        {
                            paderr = py{ encoded_string.size() } % (I<4>());
                            if (paderr.truthy())
                            {
                                encoded_string = inplace(NEOS_PY_ADD, encoded_string, (S<"===">())[B<"slice">()(none(), (I<4>()) - paderr, none())]);
                            }
                            {
                                bool otherwise10 = false;
                                try
                                {
                                    word = (Mod<"email.base64mime">().attr("decode"))(encoded_string);
                                    otherwise10 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler10;
                                    if (matches(handler10.exception, Mod<"binascii">().attr("Error")))
                                    {
                                        py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"Base64 decoding error">()));
                                    }
                                    else
                                        py_reraise(handler10.exception);
                                }
                                if (otherwise10)
                                {
                                    decoded_words.method("append", { py_tuple({ word, charset }) });
                                }
                            }
                        }
                        else
                        {
                            py_raise((B<"AssertionError">())((S<"Unexpected encoding: ">()) + encoding));
                        }
                    }
                    // Now convert all words to bytes and collapse consecutive runs of
                    // similarly encoded words.
                    collapsed = py_list();
                    py const value11 = none();
                    last_word = value11;
                    last_charset = value11;
                    for (py const& item : iterate{ decoded_words })
                    {
                        auto const items13 = unpack(item, 2);
                        word = items13[0];
                        charset = items13[1];
                        if (isinstance(word, B<"str">()))
                        {
                            word = (B<"bytes">())(word, S<"raw-unicode-escape">());
                        }
                        if (last_word.is_none())
                        {
                            last_word = word;
                            last_charset = charset;
                        }
                        else if (charset != last_charset)
                        {
                            collapsed.method("append", { py_tuple({ last_word, last_charset }) });
                            last_word = word;
                            last_charset = charset;
                        }
                        else if (last_charset.is_none())
                        {
                            last_word = inplace(NEOS_PY_ADD, last_word, (G<"email.header.BSPACE">()) + word);
                        }
                        else
                        {
                            last_word = inplace(NEOS_PY_ADD, last_word, word);
                        }
                    }
                    collapsed.method("append", { py_tuple({ last_word, last_charset }) });
                    return collapsed;
                }));

            define("email.header", "make_header", w.function("make_header", "make_header", { py_positional("decoded_seq"), py_positional("maxlinelen", none()), py_positional("header_name", none()), py_positional("continuation_ws", S<" ">()) }, [](arguments a) -> py
                {
                    py decoded_seq = a[0];
                    py maxlinelen = a[1];
                    py header_name = a[2];
                    py continuation_ws = a[3];
                    py charset;
                    py h;
                    py s;
                    h = (G<"email.header.Header">()).call({  }, { { "maxlinelen", maxlinelen }, { "header_name", header_name }, { "continuation_ws", continuation_ws } });
                    for (py const& item : iterate{ decoded_seq })
                    {
                        auto const items15 = unpack(item, 2);
                        s = items15[0];
                        charset = items15[1];
                        // None means us-ascii but we can simply pass it on to h.append()
                        if (((!charset.is_none()) && (!isinstance(charset, G<"email.header.Charset">()))))
                        {
                            charset = (G<"email.header.Charset">())(charset);
                        }
                        h.method("append", { s, charset });
                    }
                    return h;
                }));

                // Rich comparison operators for equality only.  BAW: does it make sense to
                // have or explicitly disable <, <=, >, >= operators?
            define("email.header", "Header", w.make_class("Header", {  }, {
                { "__init__", w.method("Header", "__init__", { py_positional("s", none()), py_positional("charset", none()), py_positional("maxlinelen", none()), py_positional("header_name", none()), py_positional("continuation_ws", S<" ">()), py_positional("errors", S<"strict">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        py charset = a[2];
                        py maxlinelen = a[3];
                        py header_name = a[4];
                        py continuation_ws = a[5];
                        py errors = a[6];
                        if (charset.is_none())
                        {
                            charset = G<"email.header.USASCII">();
                        }
                        else if (!isinstance(charset, G<"email.header.Charset">()))
                        {
                            charset = (G<"email.header.Charset">())(charset);
                        }
                        self.set_attr("_charset", charset);
                        self.set_attr("_continuation_ws", continuation_ws);
                        self.set_attr("_chunks", py_list());
                        if (!s.is_none())
                        {
                            self.method("append", { s, charset, errors });
                        }
                        if (maxlinelen.is_none())
                        {
                            maxlinelen = G<"email.header.MAXLINELEN">();
                        }
                        self.set_attr("_maxlinelen", maxlinelen);
                        if (header_name.is_none())
                        {
                            self.set_attr("_headerlen", I<0>());
                        }
                        else
                        {
                            // Take the separating colon and space into account.
                            self.set_attr("_headerlen", py{ header_name.size() } + (I<2>()));
                        }
                        return none();
                    }) },
                { "__str__", w.method("Header", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py charset;
                        py hasspace;
                        py lastcs;
                        py lastspace;
                        py nextcs;
                        py original_bytes;
                        py string;
                        py uchunks;
                        self.method("_normalize");
                        uchunks = py_list();
                        lastcs = none();
                        lastspace = none();
                        for (py const& item : iterate{ self.attr("_chunks") })
                        {
                            auto const items17 = unpack(item, 2);
                            string = items17[0];
                            charset = items17[1];
                            // We must preserve spaces between encoded and non-encoded word
                            // boundaries, which means for us we need to add a space when we go
                            // from a charset to None/us-ascii, or from None/us-ascii to a
                            // charset.  Only do this for the second and subsequent chunks.
                            // Don't add a space if the None/us-ascii string already has
                            // a space (trailing or leading depending on transition)
                            nextcs = charset;
                            if (nextcs == (Mod<"email.charset">().attr("UNKNOWN8BIT")))
                            {
                                original_bytes = string.method("encode", { S<"ascii">(), S<"surrogateescape">() });
                                string = original_bytes.method("decode", { S<"ascii">(), S<"replace">() });
                            }
                            if (uchunks.truthy())
                            {
                                hasspace = [&]() -> py { py const t = string; return !t.truthy() ? t : self.method("_nonctext", { string[I<0>()] }); }();
                                if (!py_tuple({ none(), S<"us-ascii">() }).contains(lastcs))
                                {
                                    if ((py_tuple({ none(), S<"us-ascii">() }).contains(nextcs) && (!hasspace.truthy())))
                                    {
                                        uchunks.method("append", { G<"email.header.SPACE">() });
                                        nextcs = none();
                                    }
                                }
                                else if (((!py_tuple({ none(), S<"us-ascii">() }).contains(nextcs)) && (!lastspace.truthy())))
                                {
                                    uchunks.method("append", { G<"email.header.SPACE">() });
                                }
                            }
                            lastspace = [&]() -> py { py const t = string; return !t.truthy() ? t : self.method("_nonctext", { string[I<-1>()] }); }();
                            lastcs = nextcs;
                            uchunks.method("append", { string });
                        }
                        return (G<"email.header.EMPTYSTRING">()).method("join", { uchunks });
                    }) },
                { "__eq__", w.method("Header", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(other == (B<"str">())(self));
                    }) },
                { "append", w.method("Header", "append", { py_positional("s"), py_positional("charset", none()), py_positional("errors", S<"strict">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        py charset = a[2];
                        py errors = a[3];
                        py input_charset;
                        py output_charset;
                        if (charset.is_none())
                        {
                            charset = self.attr("_charset");
                        }
                        else if (!isinstance(charset, G<"email.header.Charset">()))
                        {
                            charset = (G<"email.header.Charset">())(charset);
                        }
                        if (!isinstance(s, B<"str">()))
                        {
                            input_charset = [&]() -> py { py const t = charset.attr("input_codec"); return t.truthy() ? t : S<"us-ascii">(); }();
                            if (input_charset == (Mod<"email.charset">().attr("UNKNOWN8BIT")))
                            {
                                s = s.method("decode", { S<"us-ascii">(), S<"surrogateescape">() });
                            }
                            else
                            {
                                s = s.method("decode", { input_charset, errors });
                            }
                        }
                        // Ensure that the bytes we're storing can be decoded to the output
                        // character set, otherwise an early error is raised.
                        output_charset = [&]() -> py { py const t = charset.attr("output_codec"); return t.truthy() ? t : S<"us-ascii">(); }();
                        if (output_charset != (Mod<"email.charset">().attr("UNKNOWN8BIT")))
                        {
                            try
                            {
                                s.method("encode", { output_charset, errors });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler18;
                                if (matches(handler18.exception, B<"UnicodeEncodeError">()))
                                {
                                    if (output_charset != (S<"us-ascii">()))
                                    {
                                        py_reraise(handler18.exception);
                                    }
                                    charset = G<"email.header.UTF8">();
                                }
                                else
                                    py_reraise(handler18.exception);
                            }
                        }
                        self.attr("_chunks").method("append", { py_tuple({ s, charset }) });
                        return none();
                    }) },
                { "_nonctext", w.method("Header", "_nonctext", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        return [&]() -> py { py const t = s.method("isspace"); return t.truthy() ? t : boolean(py_tuple({ S<"(">(), S<")">(), S<"\\">() }).contains(s)); }();
                    }) },
                { "encode", w.method("Header", "encode", { py_positional("splitchars", S<";, \t">()), py_positional("maxlinelen", none()), py_positional("linesep", S<"\n">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py splitchars = a[1];
                        py maxlinelen = a[2];
                        py linesep = a[3];
                        py charset;
                        py formatter;
                        py fws;
                        py hasspace;
                        py lastcs;
                        py lastspace;
                        py line;
                        py lines;
                        py sline;
                        py string;
                        py value;
                        self.method("_normalize");
                        if (maxlinelen.is_none())
                        {
                            maxlinelen = self.attr("_maxlinelen");
                        }
                        // A maxlinelen of 0 means don't wrap.  For all practical purposes,
                        // choosing a huge number here accomplishes that and makes the
                        // _ValueFormatter algorithm much simpler.
                        if (maxlinelen == (I<0>()))
                        {
                            maxlinelen = I<1000000>();
                        }
                        formatter = (G<"email.header._ValueFormatter">())(self.attr("_headerlen"), maxlinelen, self.attr("_continuation_ws"), splitchars);
                        lastcs = none();
                        py const value19 = none();
                        hasspace = value19;
                        lastspace = value19;
                        for (py const& item : iterate{ self.attr("_chunks") })
                        {
                            auto const items21 = unpack(item, 2);
                            string = items21[0];
                            charset = items21[1];
                            if (!hasspace.is_none())
                            {
                                hasspace = [&]() -> py { py const t = string; return !t.truthy() ? t : self.method("_nonctext", { string[I<0>()] }); }();
                                if (!py_tuple({ none(), S<"us-ascii">() }).contains(lastcs))
                                {
                                    if (((!hasspace.truthy()) || (!py_tuple({ none(), S<"us-ascii">() }).contains(charset))))
                                    {
                                        formatter.method("add_transition");
                                    }
                                }
                                else if (((!py_tuple({ none(), S<"us-ascii">() }).contains(charset)) && (!lastspace.truthy())))
                                {
                                    formatter.method("add_transition");
                                }
                            }
                            lastspace = [&]() -> py { py const t = string; return !t.truthy() ? t : self.method("_nonctext", { string[I<-1>()] }); }();
                            lastcs = charset;
                            hasspace = boolean(false);
                            lines = string.method("splitlines");
                            if (lines.truthy())
                            {
                                formatter.method("feed", { S<"">(), lines[I<0>()], charset });
                            }
                            else
                            {
                                formatter.method("feed", { S<"">(), S<"">(), charset });
                            }
                            for (py const& item : iterate{ lines[B<"slice">()(I<1>(), none(), none())] })
                            {
                                line = item;
                                formatter.method("newline");
                                if (!charset.attr("header_encoding").is_none())
                                {
                                    formatter.method("feed", { self.attr("_continuation_ws"), (S<" ">()) + line.method("lstrip"), charset });
                                }
                                else
                                {
                                    sline = line.method("lstrip");
                                    fws = line[B<"slice">()(none(), py{ line.size() } - py{ sline.size() }, none())];
                                    formatter.method("feed", { fws, sline, charset });
                                }
                            }
                            if (py{ lines.size() } > (I<1>()))
                            {
                                formatter.method("newline");
                            }
                        }
                        if (self.attr("_chunks").truthy())
                        {
                            formatter.method("add_transition");
                        }
                        value = formatter.method("_str", { linesep });
                        if ((G<"email.header._embedded_header">()).method("search", { value }).truthy())
                        {
                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))((S<"header value appears to contain an embedded header: {!r}">()).method("format", { value })));
                        }
                        return value;
                    }) },
                { "_normalize", w.method("Header", "_normalize", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py charset;
                        py chunks;
                        py last_charset;
                        py last_chunk;
                        py string;
                        chunks = py_list();
                        last_charset = none();
                        last_chunk = py_list();
                        for (py const& item : iterate{ self.attr("_chunks") })
                        {
                            auto const items24 = unpack(item, 2);
                            string = items24[0];
                            charset = items24[1];
                            if (charset == last_charset)
                            {
                                last_chunk.method("append", { string });
                            }
                            else
                            {
                                if (!last_charset.is_none())
                                {
                                    chunks.method("append", { py_tuple({ (G<"email.header.SPACE">()).method("join", { last_chunk }), last_charset }) });
                                }
                                last_chunk = py_list({ string });
                                last_charset = charset;
                            }
                        }
                        if (last_chunk.truthy())
                        {
                            chunks.method("append", { py_tuple({ (G<"email.header.SPACE">()).method("join", { last_chunk }), last_charset }) });
                        }
                        self.set_attr("_chunks", chunks);
                        return none();
                    }) }, }));

            define("email.header", "_ValueFormatter", w.make_class("_ValueFormatter", {  }, {
                { "__init__", w.method("_ValueFormatter", "__init__", { py_positional("headerlen"), py_positional("maxlen"), py_positional("continuation_ws"), py_positional("splitchars") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py headerlen = a[1];
                        py maxlen = a[2];
                        py continuation_ws = a[3];
                        py splitchars = a[4];
                        self.set_attr("_maxlen", maxlen);
                        self.set_attr("_continuation_ws", continuation_ws);
                        self.set_attr("_continuation_ws_len", py{ continuation_ws.size() });
                        self.set_attr("_splitchars", splitchars);
                        self.set_attr("_lines", py_list());
                        self.set_attr("_current_line", (G<"email.header._Accumulator">())(headerlen));
                        return none();
                    }) },
                { "_str", w.method("_ValueFormatter", "_str", { py_positional("linesep") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py linesep = a[1];
                        self.method("newline");
                        return linesep.method("join", { self.attr("_lines") });
                    }) },
                { "__str__", w.method("_ValueFormatter", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_str", { G<"email.header.NL">() });
                    }) },
                { "newline", w.method("_ValueFormatter", "newline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py end_of_line;
                        end_of_line = self.attr("_current_line").method("pop");
                        if (end_of_line != py_tuple({ S<" ">(), S<"">() }))
                        {
                            call_ex(self.attr("_current_line").attr("push"), {  }, end_of_line, py{});
                        }
                        if (py{ self.attr("_current_line").size() } > (I<0>()))
                        {
                            if ((self.attr("_current_line").method("is_onlyws").truthy() && self.attr("_lines").truthy()))
                            {
                                {
                                    py const target25 = self.attr("_lines");
                                    py const key25 = I<-1>();
                                    target25.set_item(key25, inplace(NEOS_PY_ADD, target25[key25], (B<"str">())(self.attr("_current_line"))));
                                }
                            }
                            else
                            {
                                self.attr("_lines").method("append", { (B<"str">())(self.attr("_current_line")) });
                            }
                        }
                        self.attr("_current_line").method("reset");
                        return none();
                    }) },
                { "add_transition", w.method("_ValueFormatter", "add_transition", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_current_line").method("push", { S<" ">(), S<"">() });
                        return none();
                    }) },
                { "feed", w.method("_ValueFormatter", "feed", { py_positional("fws"), py_positional("string"), py_positional("charset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fws = a[1];
                        py string = a[2];
                        py charset = a[3];
                        py encoded_lines;
                        py first_line;
                        py last_line;
                        py line;
                        if (charset.attr("header_encoding").is_none())
                        {
                            self.method("_ascii_split", { fws, string, self.attr("_splitchars") });
                            return none();
                        }
                        // Otherwise, we're doing either a Base64 or a quoted-printable
                        // encoding which means we don't need to split the line on syntactic
                        // breaks.  We can basically just find enough characters to fit on the
                        // current line, minus the RFC 2047 chrome.  What makes this trickier
                        // though is that we have to split at octet boundaries, not character
                        // boundaries but it's only safe to split at character boundaries so at
                        // best we can only get close.
                        encoded_lines = charset.method("header_encode_lines", { string, self.method("_maxlengths") });
                        // The first element extends the current line, but if it's None then
                        // nothing more fit on the current line so start a new line.
                        try
                        {
                            first_line = encoded_lines.method("pop", { I<0>() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler26;
                            if (matches(handler26.exception, B<"IndexError">()))
                            {
                                // There are no encoded lines, so we're done.
                                return none();
                            }
                            else
                                py_reraise(handler26.exception);
                        }
                        if (!first_line.is_none())
                        {
                            self.method("_append_chunk", { fws, first_line });
                        }
                        try
                        {
                            last_line = encoded_lines.method("pop");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler27;
                            if (matches(handler27.exception, B<"IndexError">()))
                            {
                                // There was only one line.
                                return none();
                            }
                            else
                                py_reraise(handler27.exception);
                        }
                        self.method("newline");
                        self.attr("_current_line").method("push", { self.attr("_continuation_ws"), last_line });
                        // Everything else are full lines in themselves.
                        for (py const& item : iterate{ encoded_lines })
                        {
                            line = item;
                            self.attr("_lines").method("append", { self.attr("_continuation_ws") + line });
                        }
                        return none();
                    }) },
                { "_maxlengths", w.method("_ValueFormatter", "_maxlengths", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("_maxlengths", "_ValueFormatter._maxlengths", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py_yield(self.attr("_maxlen") - py{ self.attr("_current_line").size() });
                            while (boolean(true).truthy())
                            {
                                py_yield(self.attr("_maxlen") - self.attr("_continuation_ws_len"));
                            }
                            return none();
                        });
                    }) },
                { "_ascii_split", w.method("_ValueFormatter", "_ascii_split", { py_positional("fws"), py_positional("string"), py_positional("splitchars") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fws = a[1];
                        py string = a[2];
                        py splitchars = a[3];
                        py part;
                        py parts;
                        parts = (Mod<"re">().attr("split"))(((S<"([">()) + (G<"email.header.FWS">())) + (S<"]+)">()), fws + string);
                        if (parts[I<0>()].truthy())
                        {
                            parts.set_item(B<"slice">()(none(), I<0>(), none()), py_list({ S<"">() }));
                        }
                        else
                        {
                            parts.method("pop", { I<0>() });
                        }
                        for (py const& item : iterate{ call_ex((B<"zip">()), {  }, py_list({ (B<"iter">())(parts) }) * (I<2>()), py{}) })
                        {
                            auto const items31 = unpack(item, 2);
                            fws = items31[0];
                            part = items31[1];
                            self.method("_append_chunk", { fws, part });
                        }
                        return none();
                    }) },
                { "_append_chunk", w.method("_ValueFormatter", "_append_chunk", { py_positional("fws"), py_positional("string") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fws = a[1];
                        py string = a[2];
                        py ch;
                        py i;
                        py part;
                        py prevpart;
                        py remainder;
                        self.attr("_current_line").method("push", { fws, string });
                        if (py{ self.attr("_current_line").size() } > self.attr("_maxlen"))
                        {
                            // Find the best split point, working backward from the end.
                            // There might be none, on a long first line.
                            bool broke32 = false;
                            for (py const& item : iterate{ self.attr("_splitchars") })
                            {
                                ch = item;
                                bool broke33 = false;
                                for (py const& item : iterate{ (B<"range">())(self.attr("_current_line").method("part_count") - (I<1>()), I<0>(), I<-1>()) })
                                {
                                    i = item;
                                    if (ch.method("isspace").truthy())
                                    {
                                        fws = self.attr("_current_line")[i][I<0>()];
                                        if ((fws.truthy() && (fws[I<0>()] == ch)))
                                        {
                                            broke33 = true;
                                            break;
                                        }
                                    }
                                    prevpart = self.attr("_current_line")[i - (I<1>())][I<1>()];
                                    if ((prevpart.truthy() && (prevpart[I<-1>()] == ch)))
                                    {
                                        broke33 = true;
                                        break;
                                    }
                                }
                                if (!broke33)
                                {
                                    continue;
                                }
                                broke32 = true;
                                break;
                            }
                            if (!broke32)
                            {
                                auto const items34 = unpack(self.attr("_current_line").method("pop"), 2);
                                fws = items34[0];
                                part = items34[1];
                                if (self.attr("_current_line").attr("_initial_size") > (I<0>()))
                                {
                                    // There will be a header, so leave it on a line by itself.
                                    self.method("newline");
                                    if (!fws.truthy())
                                    {
                                        // We don't use continuation_ws here because the whitespace
                                        // after a header should always be a space.
                                        fws = S<" ">();
                                    }
                                }
                                self.attr("_current_line").method("push", { fws, part });
                                return none();
                            }
                            remainder = self.attr("_current_line").method("pop_from", { i });
                            self.attr("_lines").method("append", { (B<"str">())(self.attr("_current_line")) });
                            self.attr("_current_line").method("reset", { remainder });
                        }
                        return none();
                    }) }, }));

            define("email.header", "_Accumulator", w.make_class("_Accumulator", { B<"list">() }, {
                { "__init__", w.method("_Accumulator", "__init__", { py_positional("initial_size", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py initial_size = a[1];
                        self.set_attr("_initial_size", initial_size);
                        (B<"super">()(G<"email.header._Accumulator">(), self)).method("__init__");
                        return none();
                    }) },
                { "push", w.method("_Accumulator", "push", { py_positional("fws"), py_positional("string") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fws = a[1];
                        py string = a[2];
                        self.method("append", { py_tuple({ fws, string }) });
                        return none();
                    }) },
                { "pop_from", w.method("_Accumulator", "pop_from", { py_positional("i", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py popped;
                        popped = self[B<"slice">()(i, none(), none())];
                        self.set_item(B<"slice">()(i, none(), none()), py_list());
                        return popped;
                    }) },
                { "pop", w.method("_Accumulator", "pop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.method("part_count") == (I<0>()))
                        {
                            return py_tuple({ S<"">(), S<"">() });
                        }
                        return (B<"super">()(G<"email.header._Accumulator">(), self)).method("pop");
                    }) },
                { "__len__", w.method("_Accumulator", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"sum">())([&]() -> py { py const comprehension35 = py_list(); py fws; py part; for (py const& item : iterate{ self }) { auto const items36 = unpack(item, 2); fws = items36[0]; part = items36[1]; comprehension35.method("append", { py{ fws.size() } + py{ part.size() } }); } return comprehension35; }(), self.attr("_initial_size"));
                    }) },
                { "__str__", w.method("_Accumulator", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"email.header.EMPTYSTRING">()).method("join", { [&]() -> py { py const comprehension37 = py_list(); py fws; py part; for (py const& item : iterate{ self }) { auto const items38 = unpack(item, 2); fws = items38[0]; part = items38[1]; comprehension37.method("append", { (G<"email.header.EMPTYSTRING">()).method("join", { py_tuple({ fws, part }) }) }); } return comprehension37; }() });
                    }) },
                { "reset", w.method("_Accumulator", "reset", { py_positional("startval", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py startval = a[1];
                        if (startval.is_none())
                        {
                            startval = py_list();
                        }
                        self.set_item(B<"slice">()(none(), none(), none()), startval);
                        self.set_attr("_initial_size", I<0>());
                        return none();
                    }) },
                { "is_onlyws", w.method("_Accumulator", "is_onlyws", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = boolean(self.attr("_initial_size") == (I<0>())); return !t.truthy() ? t : [&]() -> py { py const t = boolean(!self.truthy()); return t.truthy() ? t : (B<"str">())(self).method("isspace"); }(); }();
                    }) },
                { "part_count", w.method("_Accumulator", "part_count", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"super">()(G<"email.header._Accumulator">(), self)).method("__len__");
                    }) }, }));
        }

        bool email_header_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_header();
                }
                if (auto const doc = email_header_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("binascii", py{ fn_import("binascii") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("HeaderParseError", import_attribute("email.errors", "HeaderParseError").release());
                aAttributes.emplace_back("_charset", import_attribute("email", "charset").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.header", email_header_module);
    }
}
