/*
  email_quoprimime.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/quoprimime.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.quoprimime (CPython 3.13's Lib/email/quoprimime.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_quoprimime_docs.hpp"

namespace neos::python_runtime::detail::email_quoprimime_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.quoprimime.name"))
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
                py_raise("SystemError", std::string{ "email.quoprimime: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.quoprimime.name">()))
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
            if (modules_imported("email.quoprimime"))
                return py{ fn_import("email.quoprimime") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.quoprimime" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.quoprimime", email_quoprimime_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.quoprimime (Lib/email/quoprimime.py)

        void make_email_quoprimime()
        {
            static module_writer const& w = writer("email.quoprimime");
            // Copyright (C) 2001-2006 Python Software Foundation
            // Author: Ben Gertzfield
            // Contact: email-sig@python.org
            define("email.quoprimime", "__all__", py_list({ S<"body_decode">(), S<"body_encode">(), S<"body_length">(), S<"decode">(), S<"decodestring">(), S<"header_decode">(), S<"header_encode">(), S<"header_length">(), S<"quote">(), S<"unquote">() }));
            define("email.quoprimime", "CRLF", S<"\r\n">());
            define("email.quoprimime", "NL", S<"\n">());
            define("email.quoprimime", "EMPTYSTRING", S<"">());
            // Build a mapping of octets to the expansion of that octet.  Since we're only
            // going to have 256 of these things, this isn't terribly inefficient
            // space-wise.  Remember that headers and bodies have different sets of safe
            // characters.  Initialize both maps with the full expansion, and then override
            // the safe bytes with the more compact form.
            define("email.quoprimime", "_QUOPRI_MAP", [&]() -> py { py const comprehension1 = py_list(); py c; for (py const& item : iterate{ (B<"range">())(I<256>()) }) { c = item; comprehension1.method("append", { (S<"=%02X">()) % c }); } return comprehension1; }());
            define("email.quoprimime", "_QUOPRI_HEADER_MAP", (G<"email.quoprimime._QUOPRI_MAP">())[B<"slice">()(none(), none(), none())]);
            define("email.quoprimime", "_QUOPRI_BODY_MAP", (G<"email.quoprimime._QUOPRI_MAP">())[B<"slice">()(none(), none(), none())]);
            // Safe header bytes which need no encoding.
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ (py{ make_bytes(std::string{ "\x2d\x21\x2a\x2b\x2f", 5 }) } + (Mod<"string">().attr("ascii_letters")).method("encode", { S<"ascii">() })) + (Mod<"string">().attr("digits")).method("encode", { S<"ascii">() }) })
                    {
                        set_global("email.quoprimime", "c", item);
                        (G<"email.quoprimime._QUOPRI_HEADER_MAP">()).set_item(G<"email.quoprimime.c">(), (B<"chr">())(G<"email.quoprimime.c">()));
                    }
                    return none();
                }))();
            // Headers have one other special encoding; spaces become underscores.
            (G<"email.quoprimime._QUOPRI_HEADER_MAP">()).set_item((B<"ord">())(S<" ">()), S<"_">());
            // Safe body bytes which need no encoding.
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ py{ make_bytes(std::string{ "\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3e\x3f\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x09", 95 }) } })
                    {
                        set_global("email.quoprimime", "c", item);
                        (G<"email.quoprimime._QUOPRI_BODY_MAP">()).set_item(G<"email.quoprimime.c">(), (B<"chr">())(G<"email.quoprimime.c">()));
                    }
                    return none();
                }))();
            // Helpers
            define("email.quoprimime", "header_check", w.function("header_check", "header_check", { py_positional("octet") }, [](arguments a) -> py
                {
                    py octet = a[0];
                    return boolean((B<"chr">())(octet) != (G<"email.quoprimime._QUOPRI_HEADER_MAP">())[octet]);
                }));

            define("email.quoprimime", "body_check", w.function("body_check", "body_check", { py_positional("octet") }, [](arguments a) -> py
                {
                    py octet = a[0];
                    return boolean((B<"chr">())(octet) != (G<"email.quoprimime._QUOPRI_BODY_MAP">())[octet]);
                }));

            define("email.quoprimime", "header_length", w.function("header_length", "header_length", { py_positional("bytearray") }, [](arguments a) -> py
                {
                    py bytearray = a[0];
                    return (B<"sum">())([&]() -> py { py const comprehension4 = py_list(); py octet; for (py const& item : iterate{ bytearray }) { octet = item; comprehension4.method("append", { py{ (G<"email.quoprimime._QUOPRI_HEADER_MAP">())[octet].size() } }); } return comprehension4; }());
                }));

            define("email.quoprimime", "body_length", w.function("body_length", "body_length", { py_positional("bytearray") }, [](arguments a) -> py
                {
                    py bytearray = a[0];
                    return (B<"sum">())([&]() -> py { py const comprehension5 = py_list(); py octet; for (py const& item : iterate{ bytearray }) { octet = item; comprehension5.method("append", { py{ (G<"email.quoprimime._QUOPRI_BODY_MAP">())[octet].size() } }); } return comprehension5; }());
                }));

            define("email.quoprimime", "_max_append", w.function("_max_append", "_max_append", { py_positional("L"), py_positional("s"), py_positional("maxlen"), py_positional("extra", S<"">()) }, [](arguments a) -> py
                {
                    py L = a[0];
                    py s = a[1];
                    py maxlen = a[2];
                    py extra = a[3];
                    if (!isinstance(s, B<"str">()))
                    {
                        s = (B<"chr">())(s);
                    }
                    if (!L.truthy())
                    {
                        L.method("append", { s.method("lstrip") });
                    }
                    else if ((py{ L[I<-1>()].size() } + py{ s.size() }) <= maxlen)
                    {
                        {
                            py const target6 = L;
                            py const key6 = I<-1>();
                            target6.set_item(key6, inplace(NEOS_PY_ADD, target6[key6], extra + s));
                        }
                    }
                    else
                    {
                        L.method("append", { s.method("lstrip") });
                    }
                    return none();
                }));

            define("email.quoprimime", "unquote", w.function("unquote", "unquote", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    return (B<"chr">())((B<"int">())(s[B<"slice">()(I<1>(), I<3>(), none())], I<16>()));
                }));

            define("email.quoprimime", "quote", w.function("quote", "quote", { py_positional("c") }, [](arguments a) -> py
                {
                    py c = a[0];
                    return (G<"email.quoprimime._QUOPRI_MAP">())[(B<"ord">())(c)];
                }));

            define("email.quoprimime", "header_encode", w.function("header_encode", "header_encode", { py_positional("header_bytes"), py_positional("charset", S<"iso-8859-1">()) }, [](arguments a) -> py
                {
                    py header_bytes = a[0];
                    py charset = a[1];
                    py encoded;
                    // Return empty headers as an empty string.
                    if (!header_bytes.truthy())
                    {
                        return S<"">();
                    }
                    // Iterate over every byte, encoding if necessary.
                    encoded = header_bytes.method("decode", { S<"latin1">() }).method("translate", { G<"email.quoprimime._QUOPRI_HEADER_MAP">() });
                    // Now add the RFC chrome to each encoded chunk and glue the chunks
                    // together.
                    return (S<"=?%s?q?%s?=">()) % py_tuple({ charset, encoded });
                }));

            define("email.quoprimime", "_QUOPRI_BODY_ENCODE_MAP", (G<"email.quoprimime._QUOPRI_BODY_MAP">())[B<"slice">()(none(), none(), none())]);
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ py{ make_bytes(std::string{ "\x0d\x0a", 2 }) } })
                    {
                        set_global("email.quoprimime", "c", item);
                        (G<"email.quoprimime._QUOPRI_BODY_ENCODE_MAP">()).set_item(G<"email.quoprimime.c">(), (B<"chr">())(G<"email.quoprimime.c">()));
                    }
                    return none();
                }))();
            undefine("email.quoprimime", "c");
            define("email.quoprimime", "body_encode", w.function("body_encode", "body_encode", { py_positional("body"), py_positional("maxlinelen", I<76>()), py_positional("eol", G<"email.quoprimime.NL">()) }, [](arguments a) -> py
                {
                    py body = a[0];
                    py maxlinelen = a[1];
                    py eol = a[2];
                    py append;
                    py encoded_body;
                    py laststart;
                    py line;
                    py maxlinelen1;
                    py q;
                    py room;
                    py soft_break;
                    py start;
                    py stop;
                    if (maxlinelen < (I<4>()))
                    {
                        py_raise((B<"ValueError">())(S<"maxlinelen must be at least 4">()));
                    }
                    if (!body.truthy())
                    {
                        return body;
                    }
                    // quote special characters
                    body = body.method("translate", { G<"email.quoprimime._QUOPRI_BODY_ENCODE_MAP">() });
                    soft_break = (S<"=">()) + eol;
                    // leave space for the '=' at the end of a line
                    maxlinelen1 = maxlinelen - (I<1>());
                    encoded_body = py_list();
                    append = encoded_body.attr("append");
                    for (py const& item : iterate{ body.method("splitlines") })
                    {
                        line = item;
                        // break up the line into pieces no longer than maxlinelen - 1
                        start = I<0>();
                        laststart = (py{ line.size() } - (I<1>())) - maxlinelen;
                        while (start <= laststart)
                        {
                            stop = start + maxlinelen1;
                            // make sure we don't break up an escape sequence
                            if (line[stop - (I<2>())] == (S<"=">()))
                            {
                                append(line[B<"slice">()(start, stop - (I<1>()), none())]);
                                start = stop - (I<2>());
                            }
                            else if (line[stop - (I<1>())] == (S<"=">()))
                            {
                                append(line[B<"slice">()(start, stop, none())]);
                                start = stop - (I<1>());
                            }
                            else
                            {
                                append(line[B<"slice">()(start, stop, none())] + (S<"=">()));
                                start = stop;
                            }
                        }
                        // handle rest of line, special case if line ends in whitespace
                        if ((line.truthy() && (S<" \t">()).contains(line[I<-1>()])))
                        {
                            room = start - laststart;
                            if (room >= (I<3>()))
                            {
                                // It's a whitespace character at end-of-line, and we have room
                                // for the three-character quoted encoding.
                                q = (G<"email.quoprimime.quote">())(line[I<-1>()]);
                            }
                            else if (room == (I<2>()))
                            {
                                // There's room for the whitespace character and a soft break.
                                q = line[I<-1>()] + soft_break;
                            }
                            else
                            {
                                // There's room only for a soft break.  The quoted whitespace
                                // will be the only content on the subsequent line.
                                q = soft_break + (G<"email.quoprimime.quote">())(line[I<-1>()]);
                            }
                            append(line[B<"slice">()(start, I<-1>(), none())] + q);
                        }
                        else
                        {
                            append(line[B<"slice">()(start, none(), none())]);
                        }
                    }
                    // add back final newline if present
                    if ((G<"email.quoprimime.CRLF">()).contains(body[I<-1>()]))
                    {
                        append(S<"">());
                    }
                    return eol.method("join", { encoded_body });
                }));

            // BAW: I'm not sure if the intent was for the signature of this function to be
            // the same as base64MIME.decode() or not...
            define("email.quoprimime", "decode", w.function("decode", "decode", { py_positional("encoded"), py_positional("eol", G<"email.quoprimime.NL">()) }, [](arguments a) -> py
                {
                    py encoded = a[0];
                    py eol = a[1];
                    py c;
                    py decoded;
                    py i;
                    py line;
                    py n;
                    if (!encoded.truthy())
                    {
                        return encoded;
                    }
                    // BAW: see comment in encode() above.  Again, we're building up the
                    // decoded string with string concatenation, which could be done much more
                    // efficiently.
                    decoded = S<"">();
                    for (py const& item : iterate{ encoded.method("splitlines") })
                    {
                        line = item;
                        line = line.method("rstrip");
                        if (!line.truthy())
                        {
                            decoded = inplace(NEOS_PY_ADD, decoded, eol);
                            continue;
                        }
                        i = I<0>();
                        n = py{ line.size() };
                        while (i < n)
                        {
                            c = line[i];
                            if (c != (S<"=">()))
                            {
                                decoded = inplace(NEOS_PY_ADD, decoded, c);
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                            }
                            // Otherwise, c == "=".  Are we at the end of the line?  If so, add
                            // a soft line break.
                            else if ((i + (I<1>())) == n)
                            {
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                                continue;
                            }
                            // Decode if in form =AB
                            else if ((((i + (I<2>())) < n) && (Mod<"string">().attr("hexdigits")).contains(line[i + (I<1>())]) && (Mod<"string">().attr("hexdigits")).contains(line[i + (I<2>())])))
                            {
                                decoded = inplace(NEOS_PY_ADD, decoded, (G<"email.quoprimime.unquote">())(line[B<"slice">()(i, i + (I<3>()), none())]));
                                i = inplace(NEOS_PY_ADD, i, I<3>());
                            }
                            else
                            {
                                // Otherwise, not in form =AB, pass literally
                                decoded = inplace(NEOS_PY_ADD, decoded, c);
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                            }
                            if (i == n)
                            {
                                decoded = inplace(NEOS_PY_ADD, decoded, eol);
                            }
                        }
                    }
                    // Special case if original string did not end with eol
                    if (((!(S<"\r\n">()).contains(encoded[I<-1>()])) && decoded.method("endswith", { eol }).truthy()))
                    {
                        decoded = decoded[B<"slice">()(none(), -py{ eol.size() }, none())];
                    }
                    return decoded;
                }));

            // For convenience and backwards compatibility w/ standard base64 module
            define("email.quoprimime", "body_decode", G<"email.quoprimime.decode">());
            define("email.quoprimime", "decodestring", G<"email.quoprimime.decode">());
            define("email.quoprimime", "_unquote_match", w.function("_unquote_match", "_unquote_match", { py_positional("match") }, [](arguments a) -> py
                {
                    py match = a[0];
                    py s;
                    s = match.method("group", { I<0>() });
                    return (G<"email.quoprimime.unquote">())(s);
                }));

            // Header decoding is done a bit differently
            define("email.quoprimime", "header_decode", w.function("header_decode", "header_decode", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    s = s.method("replace", { S<"_">(), S<" ">() });
                    return (Mod<"re">().attr("sub")).call({ S<"=[a-fA-F0-9]{2}">(), G<"email.quoprimime._unquote_match">(), s }, { { "flags", Mod<"re">().attr("ASCII") } });
                }));
        }

        bool email_quoprimime_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_quoprimime();
                }
                if (auto const doc = email_quoprimime_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("ascii_letters", import_attribute("string", "ascii_letters").release());
                aAttributes.emplace_back("digits", import_attribute("string", "digits").release());
                aAttributes.emplace_back("hexdigits", import_attribute("string", "hexdigits").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.quoprimime", email_quoprimime_module);
    }
}
