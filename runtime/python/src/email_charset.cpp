/*
  email_charset.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/charset.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.charset (CPython 3.13's Lib/email/charset.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_charset_docs.hpp"

namespace neos::python_runtime::detail::email_charset_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.charset.name"))
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
                py_raise("SystemError", std::string{ "email.charset: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.charset.name">()))
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
            if (modules_imported("email.charset"))
                return py{ fn_import("email.charset") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.charset" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.charset", email_charset_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.charset (Lib/email/charset.py)

        void make_email_charset()
        {
            static module_writer const& w = writer("email.charset");
            // Copyright (C) 2001-2007 Python Software Foundation
            // Author: Ben Gertzfield, Barry Warsaw
            // Contact: email-sig@python.org
            define("email.charset", "__all__", py_list({ S<"Charset">(), S<"add_alias">(), S<"add_charset">(), S<"add_codec">() }));
            // Flags for types of header encodings
            define("email.charset", "QP", I<1>());
            define("email.charset", "BASE64", I<2>());
            define("email.charset", "SHORTEST", I<3>());
            // In "=?charset?q?hello_world?=", the =?, ?q?, and ?= add up to 7
            define("email.charset", "RFC2047_CHROME_LEN", I<7>());
            define("email.charset", "DEFAULT_CHARSET", S<"us-ascii">());
            define("email.charset", "UNKNOWN8BIT", S<"unknown-8bit">());
            define("email.charset", "EMPTYSTRING", S<"">());
            // Defaults
            define("email.charset", "CHARSETS", dict_of({ { S<"iso-8859-1">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-2">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-3">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-4">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-9">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-10">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-13">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-14">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-15">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"iso-8859-16">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"windows-1252">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"viscii">(), py_tuple({ G<"email.charset.QP">(), G<"email.charset.QP">(), none() }) }, { S<"us-ascii">(), py_tuple({ none(), none(), none() }) }, { S<"big5">(), py_tuple({ G<"email.charset.BASE64">(), G<"email.charset.BASE64">(), none() }) }, { S<"gb2312">(), py_tuple({ G<"email.charset.BASE64">(), G<"email.charset.BASE64">(), none() }) }, { S<"euc-jp">(), py_tuple({ G<"email.charset.BASE64">(), none(), S<"iso-2022-jp">() }) }, { S<"shift_jis">(), py_tuple({ G<"email.charset.BASE64">(), none(), S<"iso-2022-jp">() }) }, { S<"iso-2022-jp">(), py_tuple({ G<"email.charset.BASE64">(), none(), none() }) }, { S<"koi8-r">(), py_tuple({ G<"email.charset.BASE64">(), G<"email.charset.BASE64">(), none() }) }, { S<"utf-8">(), py_tuple({ G<"email.charset.SHORTEST">(), G<"email.charset.BASE64">(), S<"utf-8">() }) } }));
            // input        header enc  body enc output conv
            // iso-8859-5 is Cyrillic, and not especially used
            // iso-8859-6 is Arabic, also not particularly used
            // iso-8859-7 is Greek, QP will not make it readable
            // iso-8859-8 is Hebrew, QP will not make it readable
            // iso-8859-11 is Thai, QP will not make it readable
            // Aliases for other commonly-used names for character sets.  Map
            // them to the real ones used in email.
            define("email.charset", "ALIASES", dict_of({ { S<"latin_1">(), S<"iso-8859-1">() }, { S<"latin-1">(), S<"iso-8859-1">() }, { S<"latin_2">(), S<"iso-8859-2">() }, { S<"latin-2">(), S<"iso-8859-2">() }, { S<"latin_3">(), S<"iso-8859-3">() }, { S<"latin-3">(), S<"iso-8859-3">() }, { S<"latin_4">(), S<"iso-8859-4">() }, { S<"latin-4">(), S<"iso-8859-4">() }, { S<"latin_5">(), S<"iso-8859-9">() }, { S<"latin-5">(), S<"iso-8859-9">() }, { S<"latin_6">(), S<"iso-8859-10">() }, { S<"latin-6">(), S<"iso-8859-10">() }, { S<"latin_7">(), S<"iso-8859-13">() }, { S<"latin-7">(), S<"iso-8859-13">() }, { S<"latin_8">(), S<"iso-8859-14">() }, { S<"latin-8">(), S<"iso-8859-14">() }, { S<"latin_9">(), S<"iso-8859-15">() }, { S<"latin-9">(), S<"iso-8859-15">() }, { S<"latin_10">(), S<"iso-8859-16">() }, { S<"latin-10">(), S<"iso-8859-16">() }, { S<"cp949">(), S<"ks_c_5601-1987">() }, { S<"euc_jp">(), S<"euc-jp">() }, { S<"euc_kr">(), S<"euc-kr">() }, { S<"ascii">(), S<"us-ascii">() } }));
            // Map charsets to their Unicode codec strings.
            define("email.charset", "CODEC_MAP", dict_of({ { S<"us-ascii">(), none() } }));
            // Hack: We don't want *any* conversion for stuff marked us-ascii, as all
            // sorts of garbage might be sent to us in the guise of 7-bit us-ascii.
            // Let that stuff pass through without conversion to/from Unicode.
            // Convenience functions for extending the above mappings
            define("email.charset", "add_charset", w.function("add_charset", "add_charset", { py_positional("charset"), py_positional("header_enc", none()), py_positional("body_enc", none()), py_positional("output_charset", none()) }, [](arguments a) -> py
                {
                    py charset = a[0];
                    py header_enc = a[1];
                    py body_enc = a[2];
                    py output_charset = a[3];
                    if (body_enc == (G<"email.charset.SHORTEST">()))
                    {
                        py_raise((B<"ValueError">())(S<"SHORTEST not allowed for body_enc">()));
                    }
                    (G<"email.charset.CHARSETS">()).set_item(charset, py_tuple({ header_enc, body_enc, output_charset }));
                    return none();
                }));

            define("email.charset", "add_alias", w.function("add_alias", "add_alias", { py_positional("alias"), py_positional("canonical") }, [](arguments a) -> py
                {
                    py alias = a[0];
                    py canonical = a[1];
                    (G<"email.charset.ALIASES">()).set_item(alias, canonical);
                    return none();
                }));

            define("email.charset", "add_codec", w.function("add_codec", "add_codec", { py_positional("charset"), py_positional("codecname") }, [](arguments a) -> py
                {
                    py charset = a[0];
                    py codecname = a[1];
                    (G<"email.charset.CODEC_MAP">()).set_item(charset, codecname);
                    return none();
                }));

            // Convenience function for encoding strings, taking into account
            // that they might be unknown-8bit (ie: have surrogate-escaped bytes)
            define("email.charset", "_encode", w.function("_encode", "_encode", { py_positional("string"), py_positional("codec") }, [](arguments a) -> py
                {
                    py string = a[0];
                    py codec = a[1];
                    if (codec == (G<"email.charset.UNKNOWN8BIT">()))
                    {
                        return string.method("encode", { S<"ascii">(), S<"surrogateescape">() });
                    }
                    else
                    {
                        return string.method("encode", { codec });
                    }
                    return none();
                }));

            define("email.charset", "Charset", w.make_class("Charset", {  }, {
                { "__init__", w.method("Charset", "__init__", { py_positional("input_charset", G<"email.charset.DEFAULT_CHARSET">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input_charset = a[1];
                        py benc;
                        py conv;
                        py henc;
                        try
                        {
                            if (isinstance(input_charset, B<"str">()))
                            {
                                input_charset.method("encode", { S<"ascii">() });
                            }
                            else
                            {
                                input_charset = (B<"str">())(input_charset, S<"ascii">());
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"UnicodeError">()))
                            {
                                py_raise((Mod<"email.errors">().attr("CharsetError"))(input_charset));
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        input_charset = input_charset.method("lower");
                        // Set the input charset after filtering through the aliases
                        self.set_attr("input_charset", (G<"email.charset.ALIASES">()).method("get", { input_charset, input_charset }));
                        // We can try to guess which encoding and conversion to use by the
                        // charset_map dictionary.  Try that first, but let the user override
                        // it.
                        auto const items2 = unpack((G<"email.charset.CHARSETS">()).method("get", { self.attr("input_charset"), py_tuple({ G<"email.charset.SHORTEST">(), G<"email.charset.BASE64">(), none() }) }), 3);
                        henc = items2[0];
                        benc = items2[1];
                        conv = items2[2];
                        if (!conv.truthy())
                        {
                            conv = self.attr("input_charset");
                        }
                        // Set the attributes, allowing the arguments to override the default.
                        self.set_attr("header_encoding", henc);
                        self.set_attr("body_encoding", benc);
                        self.set_attr("output_charset", (G<"email.charset.ALIASES">()).method("get", { conv, conv }));
                        // Now set the codecs.  If one isn't defined for input_charset,
                        // guess and try a Unicode codec with the same name as input_codec.
                        self.set_attr("input_codec", (G<"email.charset.CODEC_MAP">()).method("get", { self.attr("input_charset"), self.attr("input_charset") }));
                        self.set_attr("output_codec", (G<"email.charset.CODEC_MAP">()).method("get", { self.attr("output_charset"), self.attr("output_charset") }));
                        return none();
                    }) },
                { "__repr__", w.method("Charset", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("input_charset").method("lower");
                    }) },
                { "__eq__", w.method("Charset", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean((B<"str">())(self) == (B<"str">())(other).method("lower"));
                    }) },
                { "get_body_encoding", w.method("Charset", "get_body_encoding", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!(self.attr("body_encoding") != (G<"email.charset.SHORTEST">())))
                            py_raise(B<"AssertionError">()());
                        if (self.attr("body_encoding") == (G<"email.charset.QP">()))
                        {
                            return S<"quoted-printable">();
                        }
                        else if (self.attr("body_encoding") == (G<"email.charset.BASE64">()))
                        {
                            return S<"base64">();
                        }
                        else
                        {
                            return Mod<"email.encoders">().attr("encode_7or8bit");
                        }
                        return none();
                    }) },
                { "get_output_charset", w.method("Charset", "get_output_charset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("output_charset"); return t.truthy() ? t : self.attr("input_charset"); }();
                    }) },
                { "header_encode", w.method("Charset", "header_encode", { py_positional("string") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py string = a[1];
                        py codec;
                        py encoder_module;
                        py header_bytes;
                        codec = [&]() -> py { py const t = self.attr("output_codec"); return t.truthy() ? t : S<"us-ascii">(); }();
                        header_bytes = (G<"email.charset._encode">())(string, codec);
                        // 7bit/8bit encodings return the string unchanged (modulo conversions)
                        encoder_module = self.method("_get_encoder", { header_bytes });
                        if (encoder_module.is_none())
                        {
                            return string;
                        }
                        return encoder_module.method("header_encode", { header_bytes, codec });
                    }) },
                { "header_encode_lines", w.method("Charset", "header_encode_lines", { py_positional("string"), py_positional("maxlengths") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py string = a[1];
                        py maxlengths = a[2];
                        py character;
                        py charset;
                        py codec;
                        py current_line;
                        py encoder;
                        py encoder_module;
                        py extra;
                        py header_bytes;
                        py joined_line;
                        py length;
                        py lines;
                        py maxlen;
                        py this_line;
                        // See which encoding we should use.
                        codec = [&]() -> py { py const t = self.attr("output_codec"); return t.truthy() ? t : S<"us-ascii">(); }();
                        header_bytes = (G<"email.charset._encode">())(string, codec);
                        encoder_module = self.method("_get_encoder", { header_bytes });
                        encoder = (Mod<"functools">().attr("partial")).call({ encoder_module.attr("header_encode") }, { { "charset", codec } });
                        // Calculate the number of characters that the RFC 2047 chrome will
                        // contribute to each line.
                        charset = self.method("get_output_charset");
                        extra = py{ charset.size() } + (G<"email.charset.RFC2047_CHROME_LEN">());
                        // Now comes the hard part.  We must encode bytes but we can't split on
                        // bytes because some character sets are variable length and each
                        // encoded word must stand on its own.  So the problem is you have to
                        // encode to bytes to figure out this word's length, but you must split
                        // on characters.  This causes two problems: first, we don't know how
                        // many octets a specific substring of characters will get
                        // encoded to, and second, we don't know how many ASCII characters
                        // those octets will get encoded to.  Unless we try it.  Which seems
                        // inefficient.  In the interest of being correct rather than fast (and
                        // in the hope that there will be few encoded headers in any such
                        // message), brute force it. :(
                        lines = py_list();
                        current_line = py_list();
                        maxlen = (B<"next">())(maxlengths) - extra;
                        for (py const& item : iterate{ string })
                        {
                            character = item;
                            current_line.method("append", { character });
                            this_line = (G<"email.charset.EMPTYSTRING">()).method("join", { current_line });
                            length = encoder_module.method("header_length", { (G<"email.charset._encode">())(this_line, charset) });
                            if (length > maxlen)
                            {
                                // This last character doesn't fit so pop it off.
                                current_line.method("pop");
                                // Does nothing fit on the first line?
                                if (((!lines.truthy()) && (!current_line.truthy())))
                                {
                                    lines.method("append", { none() });
                                }
                                else
                                {
                                    joined_line = (G<"email.charset.EMPTYSTRING">()).method("join", { current_line });
                                    header_bytes = (G<"email.charset._encode">())(joined_line, codec);
                                    lines.method("append", { encoder(header_bytes) });
                                }
                                current_line = py_list({ character });
                                maxlen = (B<"next">())(maxlengths) - extra;
                            }
                        }
                        joined_line = (G<"email.charset.EMPTYSTRING">()).method("join", { current_line });
                        header_bytes = (G<"email.charset._encode">())(joined_line, codec);
                        lines.method("append", { encoder(header_bytes) });
                        return lines;
                    }) },
                { "_get_encoder", w.method("Charset", "_get_encoder", { py_positional("header_bytes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header_bytes = a[1];
                        py len64;
                        py lenqp;
                        if (self.attr("header_encoding") == (G<"email.charset.BASE64">()))
                        {
                            return Mod<"email.base64mime">();
                        }
                        else if (self.attr("header_encoding") == (G<"email.charset.QP">()))
                        {
                            return Mod<"email.quoprimime">();
                        }
                        else if (self.attr("header_encoding") == (G<"email.charset.SHORTEST">()))
                        {
                            len64 = (Mod<"email.base64mime">().attr("header_length"))(header_bytes);
                            lenqp = (Mod<"email.quoprimime">().attr("header_length"))(header_bytes);
                            if (len64 < lenqp)
                            {
                                return Mod<"email.base64mime">();
                            }
                            else
                            {
                                return Mod<"email.quoprimime">();
                            }
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "body_encode", w.method("Charset", "body_encode", { py_positional("string") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py string = a[1];
                        if (!string.truthy())
                        {
                            return string;
                        }
                        if (self.attr("body_encoding").is((G<"email.charset.BASE64">())))
                        {
                            if (isinstance(string, B<"str">()))
                            {
                                string = string.method("encode", { self.attr("output_charset") });
                            }
                            return (Mod<"email.base64mime">().attr("body_encode"))(string);
                        }
                        else if (self.attr("body_encoding").is((G<"email.charset.QP">())))
                        {
                            // quopromime.body_encode takes a string, but operates on it as if
                            // it were a list of byte codes.  For a (minimal) history on why
                            // this is so, see changeset 0cf700464177.  To correctly encode a
                            // character set, then, we must turn it into pseudo bytes via the
                            // latin1 charset, which will encode any byte as a single code point
                            // between 0 and 255, which is what body_encode is expecting.
                            if (isinstance(string, B<"str">()))
                            {
                                string = string.method("encode", { self.attr("output_charset") });
                            }
                            string = string.method("decode", { S<"latin1">() });
                            return (Mod<"email.quoprimime">().attr("body_encode"))(string);
                        }
                        else
                        {
                            if (isinstance(string, B<"str">()))
                            {
                                string = string.method("encode", { self.attr("output_charset") }).method("decode", { S<"ascii">() });
                            }
                            return string;
                        }
                        return none();
                    }) }, }));
        }

        bool email_charset_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_charset();
                }
                if (auto const doc = email_charset_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("partial", import_attribute("functools", "partial").release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("errors", import_attribute("email", "errors").release());
                aAttributes.emplace_back("encode_7or8bit", import_attribute("email.encoders", "encode_7or8bit").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.charset", email_charset_module);
    }
}
