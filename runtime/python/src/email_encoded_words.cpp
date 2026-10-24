/*
  email_encoded_words.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/_encoded_words.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email._encoded_words (CPython 3.13's Lib/email/_encoded_words.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_encoded_words_docs.hpp"

namespace neos::python_runtime::detail::email__encoded_words_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email._encoded_words.name"))
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
                py_raise("SystemError", std::string{ "email._encoded_words: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email._encoded_words.name">()))
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
        [[maybe_unused]] void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("email._encoded_words"))
                return py{ fn_import("email._encoded_words") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email._encoded_words" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email._encoded_words", email_encoded_words_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email._encoded_words (Lib/email/_encoded_words.py)

        void make_email__encoded_words()
        {
            static module_writer const& w = writer("email._encoded_words");
            // An ecoded word looks like this:
            // 
            // =?charset[*lang]?cte?encoded_string?=
            // 
            // for more information about charset see the charset module.  Here it is one
            // of the preferred MIME charset names (hopefully; you never know when parsing).
            // cte (Content Transfer Encoding) is either 'q' or 'b' (ignoring case).  In
            // theory other letters could be used for other encodings, but in practice this
            // (almost?) never happens.  There could be a public API for adding entries
            // to the CTE tables, but YAGNI for now.  'q' is Quoted Printable, 'b' is
            // Base64.  The meaning of encoded_string should be obvious.  'lang' is optional
            // as indicated by the brackets (they are not part of the syntax) but is almost
            // never encountered in practice.
            // 
            // The general interface for a CTE decoder is that it takes the encoded_string
            // as its argument, and returns a tuple (cte_decoded_string, defects).  The
            // cte_decoded_string is the original binary that was encoded using the
            // specified cte.  'defects' is a list of MessageDefect instances indicating any
            // problems encountered during conversion.  'charset' and 'lang' are the
            // corresponding strings extracted from the EW, case preserved.
            // 
            // The general interface for a CTE encoder is that it takes a binary sequence
            // as input and returns the cte_encoded_string, which is an ascii-only string.
            // 
            // Each decoder must also supply a length function that takes the binary
            // sequence as its argument and returns the length of the resulting encoded
            // string.
            // 
            // The main API functions for the module are decode, which calls the decoder
            // referenced by the cte specifier, and encode, which adds the appropriate
            // RFC 2047 "chrome" to the encoded string, and can optionally automatically
            // select the shortest possible encoding.  See their docstrings below for
            // details.
            define("email._encoded_words", "__all__", py_list({ S<"decode_q">(), S<"encode_q">(), S<"decode_b">(), S<"encode_b">(), S<"len_q">(), S<"len_b">(), S<"decode">(), S<"encode">() }));
            // 
            // Quoted Printable
            // 
            // regex based decoder.
            define("email._encoded_words", "_q_byte_subber", (Mod<"functools">()).method("partial", { (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x3d\x28\x5b\x61\x2d\x66\x41\x2d\x46\x30\x2d\x39\x5d\x7b\x32\x7d\x29", 17 }) } }).attr("sub"), w.function("<lambda>", "<lambda>", { py_positional("m") }, [](arguments a) -> py
                {
                    py m = a[0];
                    return (B<"bytes">()).method("fromhex", { m.method("group", { I<1>() }).method("decode") });
                }) }));
            define("email._encoded_words", "decode_q", w.function("decode_q", "decode_q", { py_positional("encoded") }, [](arguments a) -> py
                {
                    py encoded = a[0];
                    encoded = encoded.method("replace", { py{ make_bytes(std::string{ "\x5f", 1 }) }, py{ make_bytes(std::string{ "\x20", 1 }) } });
                    return py_tuple({ (G<"email._encoded_words._q_byte_subber">())(encoded), py_list() });
                }));

            // dict mapping bytes to their encoded form
            define("email._encoded_words", "_QByteMap", w.make_class("_QByteMap", { B<"dict">() }, {
                { "safe", (py{ make_bytes(std::string{ "\x2d\x21\x2a\x2b\x2f", 5 }) } + (Mod<"string">().attr("ascii_letters")).method("encode", { S<"ascii">() })) + (Mod<"string">().attr("digits")).method("encode", { S<"ascii">() }) },
                { "__missing__", w.method("_QByteMap", "__missing__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        if (self.attr("safe").contains(key))
                        {
                            self.set_item(key, (B<"chr">())(key));
                        }
                        else
                        {
                            self.set_item(key, (S<"={:02X}">()).method("format", { key }));
                        }
                        return self[key];
                    }) }, }));

            define("email._encoded_words", "_q_byte_map", (G<"email._encoded_words._QByteMap">())());
            // In headers spaces are mapped to '_'.
            (G<"email._encoded_words._q_byte_map">()).set_item((B<"ord">())(S<" ">()), S<"_">());
            define("email._encoded_words", "encode_q", w.function("encode_q", "encode_q", { py_positional("bstring") }, [](arguments a) -> py
                {
                    py bstring = a[0];
                    return (S<"">()).method("join", { [&]() -> py { py const comprehension1 = py_list(); py x; for (py const& item : iterate{ bstring }) { x = item; comprehension1.method("append", { (G<"email._encoded_words._q_byte_map">())[x] }); } return comprehension1; }() });
                }));

            define("email._encoded_words", "len_q", w.function("len_q", "len_q", { py_positional("bstring") }, [](arguments a) -> py
                {
                    py bstring = a[0];
                    return (B<"sum">())([&]() -> py { py const comprehension2 = py_list(); py x; for (py const& item : iterate{ bstring }) { x = item; comprehension2.method("append", { py{ (G<"email._encoded_words._q_byte_map">())[x].size() } }); } return comprehension2; }());
                }));

            // 
            // Base64
            // 
            define("email._encoded_words", "decode_b", w.function("decode_b", "decode_b", { py_positional("encoded") }, [](arguments a) -> py
                {
                    py encoded = a[0];
                    py missing_padding;
                    py pad_err;
                    pad_err = py{ encoded.size() } % (I<4>());
                    missing_padding = (pad_err.truthy() ? py{ make_bytes(std::string{ "\x3d\x3d\x3d", 3 }) }[B<"slice">()(none(), (I<4>()) - pad_err, none())] : py{ make_bytes(std::string{ "", 0 }) });
                    try
                    {
                        return py_tuple({ (Mod<"base64">().attr("b64decode")).call({ encoded + missing_padding }, { { "validate", boolean(true) } }), (pad_err.truthy() ? py_list({ (Mod<"email.errors">().attr("InvalidBase64PaddingDefect"))() }) : py_list()) });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler3;
                        if (matches(handler3.exception, Mod<"binascii">().attr("Error")))
                        {
                            // Since we had correct padding, this is likely an invalid char error.
                            // 
                            // The non-alphabet characters are ignored as far as padding
                            // goes, but we don't know how many there are.  So try without adding
                            // padding to see if it works.
                            try
                            {
                                return py_tuple({ (Mod<"base64">().attr("b64decode")).call({ encoded }, { { "validate", boolean(false) } }), py_list({ (Mod<"email.errors">().attr("InvalidBase64CharactersDefect"))() }) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler4;
                                if (matches(handler4.exception, Mod<"binascii">().attr("Error")))
                                {
                                    // Add as much padding as could possibly be necessary (extra padding
                                    // is ignored).
                                    try
                                    {
                                        return py_tuple({ (Mod<"base64">().attr("b64decode")).call({ encoded + py{ make_bytes(std::string{ "\x3d\x3d", 2 }) } }, { { "validate", boolean(false) } }), py_list({ (Mod<"email.errors">().attr("InvalidBase64CharactersDefect"))(), (Mod<"email.errors">().attr("InvalidBase64PaddingDefect"))() }) });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler5;
                                        if (matches(handler5.exception, Mod<"binascii">().attr("Error")))
                                        {
                                            // This only happens when the encoded string's length is 1 more
                                            // than a multiple of 4, which is invalid.
                                            // 
                                            // bpo-27397: Just return the encoded string since there's no
                                            // way to decode.
                                            return py_tuple({ encoded, py_list({ (Mod<"email.errors">().attr("InvalidBase64LengthDefect"))() }) });
                                        }
                                        else
                                            py_reraise(handler5.exception);
                                    }
                                }
                                else
                                    py_reraise(handler4.exception);
                            }
                        }
                        else
                            py_reraise(handler3.exception);
                    }
                    return none();
                }));

            define("email._encoded_words", "encode_b", w.function("encode_b", "encode_b", { py_positional("bstring") }, [](arguments a) -> py
                {
                    py bstring = a[0];
                    return (Mod<"base64">().attr("b64encode"))(bstring).method("decode", { S<"ascii">() });
                }));

            define("email._encoded_words", "len_b", w.function("len_b", "len_b", { py_positional("bstring") }, [](arguments a) -> py
                {
                    py bstring = a[0];
                    py groups_of_3;
                    py leftover;
                    auto const items6 = unpack((B<"divmod">())(py{ bstring.size() }, I<3>()), 2);
                    groups_of_3 = items6[0];
                    leftover = items6[1];
                    // 4 bytes out for each 3 bytes (or nonzero fraction thereof) in.
                    return (groups_of_3 * (I<4>())) + (leftover.truthy() ? I<4>() : I<0>());
                }));

            define("email._encoded_words", "_cte_decoders", dict_of({ { S<"q">(), G<"email._encoded_words.decode_q">() }, { S<"b">(), G<"email._encoded_words.decode_b">() } }));
            define("email._encoded_words", "decode", w.function("decode", "decode", { py_positional("ew") }, [](arguments a) -> py
                {
                    py ew = a[0];
                    py _;
                    py bstring;
                    py charset;
                    py cte;
                    py cte_string;
                    py defects;
                    py lang;
                    py string;
                    auto const items7 = unpack(ew.method("split", { S<"?">() }), 5);
                    _ = items7[0];
                    charset = items7[1];
                    cte = items7[2];
                    cte_string = items7[3];
                    _ = items7[4];
                    auto const items8 = unpack(charset.method("partition", { S<"*">() }), 3);
                    charset = items8[0];
                    _ = items8[1];
                    lang = items8[2];
                    cte = cte.method("lower");
                    // Recover the original bytes and do CTE decoding.
                    bstring = cte_string.method("encode", { S<"ascii">(), S<"surrogateescape">() });
                    auto const items9 = unpack((G<"email._encoded_words._cte_decoders">())[cte](bstring), 2);
                    bstring = items9[0];
                    defects = items9[1];
                    // Turn the CTE decoded bytes into a string.
                    try
                    {
                        string = bstring.method("decode", { charset });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler10;
                        if (matches(handler10.exception, B<"UnicodeDecodeError">()))
                        {
                            defects.method("append", { (Mod<"email.errors">().attr("UndecodableBytesDefect"))(S(std::string{ "Encoded word contains bytes not decodable using " } + charset.repr() + " charset")) });
                            string = bstring.method("decode", { charset, S<"surrogateescape">() });
                        }
                        else if (matches(handler10.exception, py_tuple({ B<"LookupError">(), B<"UnicodeEncodeError">() })))
                        {
                            string = bstring.method("decode", { S<"ascii">(), S<"surrogateescape">() });
                            if (charset.method("lower") != (S<"unknown-8bit">()))
                            {
                                defects.method("append", { (Mod<"email.errors">().attr("CharsetError"))(S(std::string{ "Unknown charset " } + charset.repr() + " in encoded word; decoded as unknown bytes")) });
                            }
                        }
                        else
                            py_reraise(handler10.exception);
                    }
                    return py_tuple({ string, charset, lang, defects });
                }));

            define("email._encoded_words", "_cte_encoders", dict_of({ { S<"q">(), G<"email._encoded_words.encode_q">() }, { S<"b">(), G<"email._encoded_words.encode_b">() } }));
            define("email._encoded_words", "_cte_encode_length", dict_of({ { S<"q">(), G<"email._encoded_words.len_q">() }, { S<"b">(), G<"email._encoded_words.len_b">() } }));
            define("email._encoded_words", "encode", w.function("encode", "encode", { py_positional("string"), py_positional("charset", S<"utf-8">()), py_positional("encoding", none()), py_positional("lang", S<"">()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py charset = a[1];
                    py encoding = a[2];
                    py lang = a[3];
                    py blen;
                    py bstring;
                    py encoded;
                    py qlen;
                    if (charset == (S<"unknown-8bit">()))
                    {
                        bstring = string.method("encode", { S<"utf-8">(), S<"surrogateescape">() });
                    }
                    else
                    {
                        bstring = string.method("encode", { charset });
                    }
                    if (encoding.is_none())
                    {
                        qlen = (G<"email._encoded_words._cte_encode_length">())[S<"q">()](bstring);
                        blen = (G<"email._encoded_words._cte_encode_length">())[S<"b">()](bstring);
                        // Bias toward q.  5 is arbitrary.
                        encoding = ((qlen - blen) < (I<5>()) ? S<"q">() : S<"b">());
                    }
                    encoded = (G<"email._encoded_words._cte_encoders">())[encoding](bstring);
                    if (lang.truthy())
                    {
                        lang = (S<"*">()) + lang;
                    }
                    return (S<"=?{}{}?{}?{}?=">()).method("format", { charset, lang, encoding, encoded });
                }));
        }

        bool email__encoded_words_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email__encoded_words();
                }
                if (auto const doc = email_encoded_words_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("base64", py{ fn_import("base64") }.release());
                aAttributes.emplace_back("binascii", py{ fn_import("binascii") }.release());
                aAttributes.emplace_back("functools", py{ fn_import("functools") }.release());
                aAttributes.emplace_back("ascii_letters", import_attribute("string", "ascii_letters").release());
                aAttributes.emplace_back("digits", import_attribute("string", "digits").release());
                aAttributes.emplace_back("errors", import_attribute("email", "errors").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email._encoded_words", email__encoded_words_module);
    }
}
