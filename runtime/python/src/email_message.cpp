/*
  email_message.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/message.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.message (CPython 3.13's Lib/email/message.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_message_docs.hpp"

namespace neos::python_runtime::detail::email_message_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.message.name"))
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
                py_raise("SystemError", std::string{ "email.message: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.message.name">()))
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
            if (modules_imported("email.message"))
                return py{ fn_import("email.message") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.message" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.message", email_message_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.message (Lib/email/message.py)

        void make_email_message()
        {
            static module_writer const& w = writer("email.message");
            // Copyright (C) 2001-2007 Python Software Foundation
            // Author: Barry Warsaw
            // Contact: email-sig@python.org
            define("email.message", "__all__", py_list({ S<"Message">(), S<"EmailMessage">() }));
            // Intrapackage imports
            define("email.message", "Charset", Mod<"email.charset">().attr("Charset"));
            define("email.message", "SEMISPACE", S<"; ">());
            // Regular expression that matches `special' characters in parameters, the
            // existence of which force quoting of the parameter value.
            define("email.message", "tspecials", (Mod<"re">()).method("compile", { S<"[ \\(\\)<>@,;:\\\\\"/\\[\\]\\?=]">() }));
            define("email.message", "_splitparam", w.function("_splitparam", "_splitparam", { py_positional("param") }, [](arguments a) -> py
                {
                    py param = a[0];
                    py a_;
                    py b;
                    py sep;
                    auto const items1 = unpack((B<"str">())(param).method("partition", { S<";">() }), 3);
                    a_ = items1[0];
                    sep = items1[1];
                    b = items1[2];
                    if (!sep.truthy())
                    {
                        return py_tuple({ a_.method("strip"), none() });
                    }
                    return py_tuple({ a_.method("strip"), b.method("strip") });
                }));

            define("email.message", "_formatparam", w.function("_formatparam", "_formatparam", { py_positional("param"), py_positional("value", none()), py_positional("quote", boolean(true)) }, [](arguments a) -> py
                {
                    py param = a[0];
                    py value = a[1];
                    py quote = a[2];
                    if (((!value.is_none()) && (py{ value.size() } > (I<0>()))))
                    {
                        // A tuple is used for RFC 2231 encoded parameter values where items
                        // are (charset, language, value).  charset is a string, not a Charset
                        // instance.  RFC 2231 encoded values are never quoted, per RFC.
                        if (isinstance(value, B<"tuple">()))
                        {
                            // Encode as per RFC 2231
                            param = inplace(NEOS_PY_ADD, param, S<"*">());
                            value = (Mod<"email.utils">().attr("encode_rfc2231"))(value[I<2>()], value[I<0>()], value[I<1>()]);
                            return (S<"%s=%s">()) % py_tuple({ param, value });
                        }
                        else
                        {
                            try
                            {
                                value.method("encode", { S<"ascii">() });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, B<"UnicodeEncodeError">()))
                                {
                                    param = inplace(NEOS_PY_ADD, param, S<"*">());
                                    value = (Mod<"email.utils">().attr("encode_rfc2231"))(value, S<"utf-8">(), S<"">());
                                    return (S<"%s=%s">()) % py_tuple({ param, value });
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                        }
                        // BAW: Please check this.  I think that if quote is set it should
                        // force quoting even if not necessary.
                        if ((quote.truthy() || (G<"email.message.tspecials">()).method("search", { value }).truthy()))
                        {
                            return (S<"%s=\"%s\"">()) % py_tuple({ param, (Mod<"email.utils">().attr("quote"))(value) });
                        }
                        else
                        {
                            return (S<"%s=%s">()) % py_tuple({ param, value });
                        }
                    }
                    else
                    {
                        return param;
                    }
                    return none();
                }));

            define("email.message", "_parseparam", w.function("_parseparam", "_parseparam", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py diff;
                    py end;
                    py f;
                    py i;
                    py ind;
                    py plist;
                    py start;
                    s = (S<";">()) + (B<"str">())(s);
                    plist = py_list();
                    start = I<0>();
                    while (s.method("find", { S<";">(), start }) == start)
                    {
                        start = inplace(NEOS_PY_ADD, start, I<1>());
                        end = s.method("find", { S<";">(), start });
                        auto const items4 = unpack(py_tuple({ start, I<0>() }), 2);
                        ind = items4[0];
                        diff = items4[1];
                        while (end > (I<0>()))
                        {
                            diff = inplace(NEOS_PY_ADD, diff, s.method("count", { S<"\"">(), ind, end }) - s.method("count", { S<"\\\"">(), ind, end }));
                            if ((diff % (I<2>())) == (I<0>()))
                            {
                                break;
                            }
                            auto const items6 = unpack(py_tuple({ ind, s.method("find", { S<";">(), end + (I<1>()) }) }), 2);
                            end = items6[0];
                            ind = items6[1];
                        }
                        if (end < (I<0>()))
                        {
                            end = py{ s.size() };
                        }
                        i = s.method("find", { S<"=">(), start, end });
                        if (i == (I<-1>()))
                        {
                            f = s[B<"slice">()(start, end, none())];
                        }
                        else
                        {
                            f = (s[B<"slice">()(start, i, none())].method("rstrip").method("lower") + (S<"=">())) + s[B<"slice">()(i + (I<1>()), end, none())].method("lstrip");
                        }
                        plist.method("append", { f.method("strip") });
                        start = end;
                    }
                    return plist;
                }));

            define("email.message", "_unquotevalue", w.function("_unquotevalue", "_unquotevalue", { py_positional("value") }, [](arguments a) -> py
                {
                    py value = a[0];
                    if (isinstance(value, B<"tuple">()))
                    {
                        return py_tuple({ value[I<0>()], value[I<1>()], (Mod<"email.utils">().attr("unquote"))(value[I<2>()]) });
                    }
                    else
                    {
                        return (Mod<"email.utils">().attr("unquote"))(value);
                    }
                    return none();
                }));

            define("email.message", "_decode_uu", w.function("_decode_uu", "_decode_uu", { py_positional("encoded") }, [](arguments a) -> py
                {
                    py encoded = a[0];
                    py _;
                    py decoded_line;
                    py decoded_lines;
                    py encoded_lines_iter;
                    py line;
                    py mode;
                    py nbytes;
                    py path;
                    decoded_lines = py_list();
                    encoded_lines_iter = (B<"iter">())(encoded.method("splitlines"));
                    bool broke7 = false;
                    for (py const& item : iterate{ encoded_lines_iter })
                    {
                        line = item;
                        if (line.method("startswith", { py{ make_bytes(std::string{ "\x62\x65\x67\x69\x6e\x20", 6 }) } }).truthy())
                        {
                            auto const items8 = unpack(line.method("removeprefix", { py{ make_bytes(std::string{ "\x62\x65\x67\x69\x6e\x20", 6 }) } }).method("partition", { py{ make_bytes(std::string{ "\x20", 1 }) } }), 3);
                            mode = items8[0];
                            _ = items8[1];
                            path = items8[2];
                            {
                                bool otherwise9 = false;
                                try
                                {
                                    (B<"int">()).call({ mode }, { { "base", I<8>() } });
                                    otherwise9 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler9;
                                    if (matches(handler9.exception, B<"ValueError">()))
                                    {
                                        continue;
                                    }
                                    else
                                        py_reraise(handler9.exception);
                                }
                                if (otherwise9)
                                {
                                    broke7 = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!broke7)
                    {
                        py_raise((B<"ValueError">())(S<"`begin` line not found">()));
                    }
                    for (py const& item : iterate{ encoded_lines_iter })
                    {
                        line = item;
                        if (!line.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Truncated input">()));
                        }
                        else if (line.method("strip", { py{ make_bytes(std::string{ "\x20\x09\x0d\x0a\x0c", 5 }) } }) == py{ make_bytes(std::string{ "\x65\x6e\x64", 3 }) })
                        {
                            break;
                        }
                        try
                        {
                            decoded_line = (Mod<"binascii">().attr("a2b_uu"))(line);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            if (matches(handler11.exception, Mod<"binascii">().attr("Error")))
                            {
                                // Workaround for broken uuencoders by /Fredrik Lundh
                                nbytes = floordiv(((((line[I<0>()] - (I<32>())) & (I<63>())) * (I<4>())) + (I<5>())), (I<3>()));
                                decoded_line = (Mod<"binascii">().attr("a2b_uu"))(line[B<"slice">()(none(), nbytes, none())]);
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        decoded_lines.method("append", { decoded_line });
                    }
                    return py{ make_bytes(std::string{ "", 0 }) }.method("join", { decoded_lines });
                }));

                // 
                // Unix From_ line
                // 
                // 
                // Payload manipulation.
                // 
                // 
                // MAPPING INTERFACE (partial)
                // 
                // 
                // "Internal" methods (public API, but only intended for use by a parser
                // or generator, not normal application code.
                // 
                // 
                // Additional useful stuff
                // 
                // 
                // Use these three methods instead of the three above.
                // 
                // I.e. def walk(self): ...
            define("email.message", "Message", w.make_class("Message", {  }, {
                { "__init__", w.method("Message", "__init__", { py_positional("policy", Mod<"email._policybase">().attr("compat32")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py policy = a[1];
                        self.set_attr("policy", policy);
                        self.set_attr("_headers", py_list());
                        self.set_attr("_unixfrom", none());
                        self.set_attr("_payload", none());
                        self.set_attr("_charset", none());
                        // Defaults for multipart messages
                        py const value12 = none();
                        self.set_attr("preamble", value12);
                        self.set_attr("epilogue", value12);
                        self.set_attr("defects", py_list());
                        // Default content type
                        self.set_attr("_default_type", S<"text/plain">());
                        return none();
                    }) },
                { "__str__", w.method("Message", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("as_string");
                    }) },
                { "as_string", w.method("Message", "as_string", { py_positional("unixfrom", boolean(false)), py_positional("maxheaderlen", I<0>()), py_positional("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py unixfrom = a[1];
                        py maxheaderlen = a[2];
                        py policy = a[3];
                        py Generator;
                        py fp;
                        py g;
                        Generator = M("email.generator", "Generator");
                        policy = (policy.is_none() ? self.attr("policy") : policy);
                        fp = (Mod<"io">().attr("StringIO"))();
                        g = Generator.call({ fp }, { { "mangle_from_", boolean(false) }, { "maxheaderlen", maxheaderlen }, { "policy", policy } });
                        g.method("flatten", { self }, { { "unixfrom", unixfrom } });
                        return fp.method("getvalue");
                    }) },
                { "__bytes__", w.method("Message", "__bytes__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("as_bytes");
                    }) },
                { "as_bytes", w.method("Message", "as_bytes", { py_positional("unixfrom", boolean(false)), py_positional("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py unixfrom = a[1];
                        py policy = a[2];
                        py BytesGenerator;
                        py fp;
                        py g;
                        BytesGenerator = M("email.generator", "BytesGenerator");
                        policy = (policy.is_none() ? self.attr("policy") : policy);
                        fp = (Mod<"io">().attr("BytesIO"))();
                        g = BytesGenerator.call({ fp }, { { "mangle_from_", boolean(false) }, { "policy", policy } });
                        g.method("flatten", { self }, { { "unixfrom", unixfrom } });
                        return fp.method("getvalue");
                    }) },
                { "is_multipart", w.method("Message", "is_multipart", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(isinstance(self.attr("_payload"), B<"list">()));
                    }) },
                { "set_unixfrom", w.method("Message", "set_unixfrom", { py_positional("unixfrom") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py unixfrom = a[1];
                        self.set_attr("_unixfrom", unixfrom);
                        return none();
                    }) },
                { "get_unixfrom", w.method("Message", "get_unixfrom", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_unixfrom");
                    }) },
                { "attach", w.method("Message", "attach", { py_positional("payload") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py payload = a[1];
                        if (self.attr("_payload").is_none())
                        {
                            self.set_attr("_payload", py_list({ payload }));
                        }
                        else
                        {
                            try
                            {
                                self.attr("_payload").method("append", { payload });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler13;
                                if (matches(handler13.exception, B<"AttributeError">()))
                                {
                                    py_raise((B<"TypeError">())(S<"Attach is not valid on a message with a non-multipart payload">()));
                                }
                                else
                                    py_reraise(handler13.exception);
                            }
                        }
                        return none();
                    }) },
                { "get_payload", w.method("Message", "get_payload", { py_positional("i", none()), py_positional("decode", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py decode = a[2];
                        py bpayload;
                        py cte;
                        py defect;
                        py defects;
                        py payload;
                        py value;
                        // Here is the logic table for this code, based on the email5.0.0 code:
                        // i     decode  is_multipart  result
                        // ------  ------  ------------  ------------------------------
                        // None   True    True          None
                        // i     True    True          None
                        // None   False   True          _payload (a list)
                        // i     False   True          _payload element i (a Message)
                        // i     False   False         error (not a list)
                        // i     True    False         error (not a list)
                        // None   False   False         _payload
                        // None   True    False         _payload decoded (bytes)
                        // Note that Barry planned to factor out the 'decode' case, but that
                        // isn't so easy now that we handle the 8 bit data, which needs to be
                        // converted in both the decode and non-decode path.
                        if (self.method("is_multipart").truthy())
                        {
                            if (decode.truthy())
                            {
                                return none();
                            }
                            if (i.is_none())
                            {
                                return self.attr("_payload");
                            }
                            else
                            {
                                return self.attr("_payload")[i];
                            }
                        }
                        // For backward compatibility, Use isinstance and this error message
                        // instead of the more logical is_multipart test.
                        if (((!i.is_none()) && (!isinstance(self.attr("_payload"), B<"list">()))))
                        {
                            py_raise((B<"TypeError">())((S<"Expected list, got %s">()) % (B<"type">())(self.attr("_payload"))));
                        }
                        payload = self.attr("_payload");
                        cte = self.method("get", { S<"content-transfer-encoding">(), S<"">() });
                        if (has_attr(cte, "cte"))
                        {
                            cte = cte.attr("cte");
                        }
                        else
                        {
                            // cte might be a Header, so for now stringify it.
                            cte = (B<"str">())(cte).method("strip").method("lower");
                        }
                        // payload may be bytes here.
                        if (!decode.truthy())
                        {
                            if ((isinstance(payload, B<"str">()) && (Mod<"email.utils">().attr("_has_surrogates"))(payload).truthy()))
                            {
                                try
                                {
                                    bpayload = payload.method("encode", { S<"ascii">(), S<"surrogateescape">() });
                                    try
                                    {
                                        payload = bpayload.method("decode", { self.method("get_content_charset", { S<"ascii">() }), S<"replace">() });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler15;
                                        if (matches(handler15.exception, B<"LookupError">()))
                                        {
                                            payload = bpayload.method("decode", { S<"ascii">(), S<"replace">() });
                                        }
                                        else
                                            py_reraise(handler15.exception);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler14;
                                    if (matches(handler14.exception, B<"UnicodeEncodeError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler14.exception);
                                }
                            }
                            return payload;
                        }
                        if (isinstance(payload, B<"str">()))
                        {
                            try
                            {
                                bpayload = payload.method("encode", { S<"ascii">(), S<"surrogateescape">() });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler16;
                                if (matches(handler16.exception, B<"UnicodeEncodeError">()))
                                {
                                    // This won't happen for RFC compliant messages (messages
                                    // containing only ASCII code points in the string input).
                                    // If it does happen, turn the string into bytes in a way
                                    // guaranteed not to fail.
                                    bpayload = payload.method("encode", { S<"raw-unicode-escape">() });
                                }
                                else
                                    py_reraise(handler16.exception);
                            }
                        }
                        else
                        {
                            bpayload = payload;
                        }
                        if (cte == (S<"quoted-printable">()))
                        {
                            return (Mod<"quopri">().attr("decodestring"))(bpayload);
                        }
                        else if (cte == (S<"base64">()))
                        {
                            // XXX: this is a bit of a hack; decode_b should probably be factored
                            // out somewhere, but I haven't figured out where yet.
                            auto const items17 = unpack((Mod<"email._encoded_words">().attr("decode_b"))(py{ make_bytes(std::string{ "", 0 }) }.method("join", { bpayload.method("splitlines") })), 2);
                            value = items17[0];
                            defects = items17[1];
                            for (py const& item : iterate{ defects })
                            {
                                defect = item;
                                self.attr("policy").method("handle_defect", { self, defect });
                            }
                            return value;
                        }
                        else if (py_tuple({ S<"x-uuencode">(), S<"uuencode">(), S<"uue">(), S<"x-uue">() }).contains(cte))
                        {
                            try
                            {
                                return (G<"email.message._decode_uu">())(bpayload);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler19;
                                if (matches(handler19.exception, B<"ValueError">()))
                                {
                                    // Some decoding problem.
                                    return bpayload;
                                }
                                else
                                    py_reraise(handler19.exception);
                            }
                        }
                        if (isinstance(payload, B<"str">()))
                        {
                            return bpayload;
                        }
                        return payload;
                    }) },
                { "set_payload", w.method("Message", "set_payload", { py_positional("payload"), py_positional("charset", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py payload = a[1];
                        py charset = a[2];
                        if (has_attr(payload, "encode"))
                        {
                            if (charset.is_none())
                            {
                                self.set_attr("_payload", payload);
                                return none();
                            }
                            if (!isinstance(charset, G<"email.message.Charset">()))
                            {
                                charset = (G<"email.message.Charset">())(charset);
                            }
                            payload = payload.method("encode", { charset.attr("output_charset"), S<"surrogateescape">() });
                        }
                        if (has_attr(payload, "decode"))
                        {
                            self.set_attr("_payload", payload.method("decode", { S<"ascii">(), S<"surrogateescape">() }));
                        }
                        else
                        {
                            self.set_attr("_payload", payload);
                        }
                        if (!charset.is_none())
                        {
                            self.method("set_charset", { charset });
                        }
                        return none();
                    }) },
                { "set_charset", w.method("Message", "set_charset", { py_positional("charset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py charset = a[1];
                        py cte;
                        py payload;
                        if (charset.is_none())
                        {
                            self.method("del_param", { S<"charset">() });
                            self.set_attr("_charset", none());
                            return none();
                        }
                        if (!isinstance(charset, G<"email.message.Charset">()))
                        {
                            charset = (G<"email.message.Charset">())(charset);
                        }
                        self.set_attr("_charset", charset);
                        if (!self.contains((S<"MIME-Version">())))
                        {
                            self.method("add_header", { S<"MIME-Version">(), S<"1.0">() });
                        }
                        if (!self.contains((S<"Content-Type">())))
                        {
                            self.method("add_header", { S<"Content-Type">(), S<"text/plain">() }, { { "charset", charset.method("get_output_charset") } });
                        }
                        else
                        {
                            self.method("set_param", { S<"charset">(), charset.method("get_output_charset") });
                        }
                        if (charset != charset.method("get_output_charset"))
                        {
                            self.set_attr("_payload", charset.method("body_encode", { self.attr("_payload") }));
                        }
                        if (!self.contains((S<"Content-Transfer-Encoding">())))
                        {
                            cte = charset.method("get_body_encoding");
                            try
                            {
                                cte(self);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler20;
                                if (matches(handler20.exception, B<"TypeError">()))
                                {
                                    // This 'if' is for backward compatibility, it allows str
                                    // through even though that won't work correctly if the
                                    // message is serialized.
                                    payload = self.attr("_payload");
                                    if (payload.truthy())
                                    {
                                        try
                                        {
                                            payload = payload.method("encode", { S<"ascii">(), S<"surrogateescape">() });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler21;
                                            if (matches(handler21.exception, B<"UnicodeError">()))
                                            {
                                                payload = payload.method("encode", { charset.attr("output_charset") });
                                            }
                                            else
                                                py_reraise(handler21.exception);
                                        }
                                    }
                                    self.set_attr("_payload", charset.method("body_encode", { payload }));
                                    self.method("add_header", { S<"Content-Transfer-Encoding">(), cte });
                                }
                                else
                                    py_reraise(handler20.exception);
                            }
                        }
                        return none();
                    }) },
                { "get_charset", w.method("Message", "get_charset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_charset");
                    }) },
                { "__len__", w.method("Message", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_headers").size() };
                    }) },
                { "__getitem__", w.method("Message", "__getitem__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.method("get", { name });
                    }) },
                { "__setitem__", w.method("Message", "__setitem__", { py_positional("name"), py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py val = a[2];
                        py found;
                        py k;
                        py lname;
                        py max_count;
                        py v;
                        max_count = self.attr("policy").method("header_max_count", { name });
                        if (max_count.truthy())
                        {
                            lname = name.method("lower");
                            found = I<0>();
                            for (py const& item : iterate{ self.attr("_headers") })
                            {
                                auto const items23 = unpack(item, 2);
                                k = items23[0];
                                v = items23[1];
                                if (k.method("lower") == lname)
                                {
                                    found = inplace(NEOS_PY_ADD, found, I<1>());
                                    if (found >= max_count)
                                    {
                                        py_raise((B<"ValueError">())((S<"There may be at most {} {} headers in a message">()).method("format", { max_count, name })));
                                    }
                                }
                            }
                        }
                        self.attr("_headers").method("append", { self.attr("policy").method("header_store_parse", { name, val }) });
                        return none();
                    }) },
                { "__delitem__", w.method("Message", "__delitem__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py k;
                        py newheaders;
                        py v;
                        name = name.method("lower");
                        newheaders = py_list();
                        for (py const& item : iterate{ self.attr("_headers") })
                        {
                            auto const items25 = unpack(item, 2);
                            k = items25[0];
                            v = items25[1];
                            if (k.method("lower") != name)
                            {
                                newheaders.method("append", { py_tuple({ k, v }) });
                            }
                        }
                        self.set_attr("_headers", newheaders);
                        return none();
                    }) },
                { "__contains__", w.method("Message", "__contains__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py k;
                        py name_lower;
                        py v;
                        name_lower = name.method("lower");
                        for (py const& item : iterate{ self.attr("_headers") })
                        {
                            auto const items27 = unpack(item, 2);
                            k = items27[0];
                            v = items27[1];
                            if (name_lower == k.method("lower"))
                            {
                                return boolean(true);
                            }
                        }
                        return boolean(false);
                    }) },
                { "__iter__", w.method("Message", "__iter__", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("__iter__", "Message.__iter__", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py field;
                            py value;
                            for (py const& item : iterate{ self.attr("_headers") })
                            {
                                auto const items29 = unpack(item, 2);
                                field = items29[0];
                                value = items29[1];
                                py_yield(field);
                            }
                            return none();
                        });
                    }) },
                { "keys", w.method("Message", "keys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension30 = py_list(); py k; py v; for (py const& item : iterate{ self.attr("_headers") }) { auto const items31 = unpack(item, 2); k = items31[0]; v = items31[1]; comprehension30.method("append", { k }); } return comprehension30; }();
                    }) },
                { "values", w.method("Message", "values", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension32 = py_list(); py k; py v; for (py const& item : iterate{ self.attr("_headers") }) { auto const items33 = unpack(item, 2); k = items33[0]; v = items33[1]; comprehension32.method("append", { self.attr("policy").method("header_fetch_parse", { k, v }) }); } return comprehension32; }();
                    }) },
                { "items", w.method("Message", "items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension34 = py_list(); py k; py v; for (py const& item : iterate{ self.attr("_headers") }) { auto const items35 = unpack(item, 2); k = items35[0]; v = items35[1]; comprehension34.method("append", { py_tuple({ k, self.attr("policy").method("header_fetch_parse", { k, v }) }) }); } return comprehension34; }();
                    }) },
                { "get", w.method("Message", "get", { py_positional("name"), py_positional("failobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py failobj = a[2];
                        py k;
                        py v;
                        name = name.method("lower");
                        for (py const& item : iterate{ self.attr("_headers") })
                        {
                            auto const items37 = unpack(item, 2);
                            k = items37[0];
                            v = items37[1];
                            if (k.method("lower") == name)
                            {
                                return self.attr("policy").method("header_fetch_parse", { k, v });
                            }
                        }
                        return failobj;
                    }) },
                { "set_raw", w.method("Message", "set_raw", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        self.attr("_headers").method("append", { py_tuple({ name, value }) });
                        return none();
                    }) },
                { "raw_items", w.method("Message", "raw_items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"iter">())(self.attr("_headers").method("copy"));
                    }) },
                { "get_all", w.method("Message", "get_all", { py_positional("name"), py_positional("failobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py failobj = a[2];
                        py k;
                        py v;
                        py values;
                        values = py_list();
                        name = name.method("lower");
                        for (py const& item : iterate{ self.attr("_headers") })
                        {
                            auto const items39 = unpack(item, 2);
                            k = items39[0];
                            v = items39[1];
                            if (k.method("lower") == name)
                            {
                                values.method("append", { self.attr("policy").method("header_fetch_parse", { k, v }) });
                            }
                        }
                        if (!values.truthy())
                        {
                            return failobj;
                        }
                        return values;
                    }) },
                { "add_header", w.method("Message", "add_header", { py_positional("_name"), py_positional("_value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _name = a[1];
                        py _value = a[2];
                        py _params = a[3];
                        py k;
                        py parts;
                        py v;
                        parts = py_list();
                        for (py const& item : iterate{ _params.method("items") })
                        {
                            auto const items41 = unpack(item, 2);
                            k = items41[0];
                            v = items41[1];
                            if (v.is_none())
                            {
                                parts.method("append", { k.method("replace", { S<"_">(), S<"-">() }) });
                            }
                            else
                            {
                                parts.method("append", { (G<"email.message._formatparam">())(k.method("replace", { S<"_">(), S<"-">() }), v) });
                            }
                        }
                        if (!_value.is_none())
                        {
                            parts.method("insert", { I<0>(), _value });
                        }
                        self.set_item(_name, (G<"email.message.SEMISPACE">()).method("join", { parts }));
                        return none();
                    }, false, true) },
                { "replace_header", w.method("Message", "replace_header", { py_positional("_name"), py_positional("_value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _name = a[1];
                        py _value = a[2];
                        py i;
                        py k;
                        py v;
                        _name = _name.method("lower");
                        bool broke42 = false;
                        for (py const& item : iterate{ (B<"zip">())((B<"range">())(py{ self.attr("_headers").size() }), self.attr("_headers")) })
                        {
                            auto const items43 = unpack(item, 2);
                            i = items43[0];
                            auto const items44 = unpack(items43[1], 2);
                            k = items44[0];
                            v = items44[1];
                            if (k.method("lower") == _name)
                            {
                                self.attr("_headers").set_item(i, self.attr("policy").method("header_store_parse", { k, _value }));
                                broke42 = true;
                                break;
                            }
                        }
                        if (!broke42)
                        {
                            py_raise((B<"KeyError">())(_name));
                        }
                        return none();
                    }) },
                { "get_content_type", w.method("Message", "get_content_type", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ctype;
                        py missing;
                        py value;
                        missing = (B<"object">())();
                        value = self.method("get", { S<"content-type">(), missing });
                        if (value.is(missing))
                        {
                            // This should have no parameters
                            return self.method("get_default_type");
                        }
                        ctype = (G<"email.message._splitparam">())(value)[I<0>()].method("lower");
                        // RFC 2045, section 5.2 says if its invalid, use text/plain
                        if (ctype.method("count", { S<"/">() }) != (I<1>()))
                        {
                            return S<"text/plain">();
                        }
                        return ctype;
                    }) },
                { "get_content_maintype", w.method("Message", "get_content_maintype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ctype;
                        ctype = self.method("get_content_type");
                        return ctype.method("split", { S<"/">() })[I<0>()];
                    }) },
                { "get_content_subtype", w.method("Message", "get_content_subtype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ctype;
                        ctype = self.method("get_content_type");
                        return ctype.method("split", { S<"/">() })[I<1>()];
                    }) },
                { "get_default_type", w.method("Message", "get_default_type", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_default_type");
                    }) },
                { "set_default_type", w.method("Message", "set_default_type", { py_positional("ctype") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ctype = a[1];
                        self.set_attr("_default_type", ctype);
                        return none();
                    }) },
                { "_get_params_preserve", w.method("Message", "_get_params_preserve", { py_positional("failobj"), py_positional("header") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py failobj = a[1];
                        py header = a[2];
                        py missing;
                        py name;
                        py p;
                        py params;
                        py val;
                        py value;
                        missing = (B<"object">())();
                        value = self.method("get", { header, missing });
                        if (value.is(missing))
                        {
                            return failobj;
                        }
                        params = py_list();
                        for (py const& item : iterate{ (G<"email.message._parseparam">())(value) })
                        {
                            p = item;
                            try
                            {
                                auto const items47 = unpack(p.method("split", { S<"=">(), I<1>() }), 2);
                                name = items47[0];
                                val = items47[1];
                                name = name.method("strip");
                                val = val.method("strip");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler46;
                                if (matches(handler46.exception, B<"ValueError">()))
                                {
                                    // Must have been a bare attribute
                                    name = p.method("strip");
                                    val = S<"">();
                                }
                                else
                                    py_reraise(handler46.exception);
                            }
                            params.method("append", { py_tuple({ name, val }) });
                        }
                        params = (Mod<"email.utils">().attr("decode_params"))(params);
                        return params;
                    }) },
                { "get_params", w.method("Message", "get_params", { py_positional("failobj", none()), py_positional("header", S<"content-type">()), py_positional("unquote", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py failobj = a[1];
                        py header = a[2];
                        py unquote = a[3];
                        py missing;
                        py params;
                        missing = (B<"object">())();
                        params = self.method("_get_params_preserve", { missing, header });
                        if (params.is(missing))
                        {
                            return failobj;
                        }
                        if (unquote.truthy())
                        {
                            return [&]() -> py { py const comprehension48 = py_list(); py k; py v; for (py const& item : iterate{ params }) { auto const items49 = unpack(item, 2); k = items49[0]; v = items49[1]; comprehension48.method("append", { py_tuple({ k, (G<"email.message._unquotevalue">())(v) }) }); } return comprehension48; }();
                        }
                        else
                        {
                            return params;
                        }
                        return none();
                    }) },
                { "get_param", w.method("Message", "get_param", { py_positional("param"), py_positional("failobj", none()), py_positional("header", S<"content-type">()), py_positional("unquote", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py param = a[1];
                        py failobj = a[2];
                        py header = a[3];
                        py unquote = a[4];
                        py k;
                        py v;
                        if (!self.contains(header))
                        {
                            return failobj;
                        }
                        for (py const& item : iterate{ self.method("_get_params_preserve", { failobj, header }) })
                        {
                            auto const items51 = unpack(item, 2);
                            k = items51[0];
                            v = items51[1];
                            if (k.method("lower") == param.method("lower"))
                            {
                                if (unquote.truthy())
                                {
                                    return (G<"email.message._unquotevalue">())(v);
                                }
                                else
                                {
                                    return v;
                                }
                            }
                        }
                        return failobj;
                    }) },
                { "set_param", w.method("Message", "set_param", { py_positional("param"), py_positional("value"), py_positional("header", S<"Content-Type">()), py_positional("requote", boolean(true)), py_positional("charset", none()), py_positional("language", S<"">()), py_positional("replace", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py param = a[1];
                        py value = a[2];
                        py header = a[3];
                        py requote = a[4];
                        py charset = a[5];
                        py language = a[6];
                        py replace = a[7];
                        py append_param;
                        py ctype;
                        py old_param;
                        py old_value;
                        if (((!isinstance(value, B<"tuple">())) && charset.truthy()))
                        {
                            value = py_tuple({ charset, language, value });
                        }
                        if (((!self.contains(header)) && (header.method("lower") == (S<"content-type">()))))
                        {
                            ctype = S<"text/plain">();
                        }
                        else
                        {
                            ctype = self.method("get", { header });
                        }
                        if (!self.method("get_param", { param }, { { "header", header } }).truthy())
                        {
                            if (!ctype.truthy())
                            {
                                ctype = (G<"email.message._formatparam">())(param, value, requote);
                            }
                            else
                            {
                                ctype = (G<"email.message.SEMISPACE">()).method("join", { py_list({ ctype, (G<"email.message._formatparam">())(param, value, requote) }) });
                            }
                        }
                        else
                        {
                            ctype = S<"">();
                            for (py const& item : iterate{ self.method("get_params", {  }, { { "header", header }, { "unquote", requote } }) })
                            {
                                auto const items53 = unpack(item, 2);
                                old_param = items53[0];
                                old_value = items53[1];
                                append_param = S<"">();
                                if (old_param.method("lower") == param.method("lower"))
                                {
                                    append_param = (G<"email.message._formatparam">())(param, value, requote);
                                }
                                else
                                {
                                    append_param = (G<"email.message._formatparam">())(old_param, old_value, requote);
                                }
                                if (!ctype.truthy())
                                {
                                    ctype = append_param;
                                }
                                else
                                {
                                    ctype = (G<"email.message.SEMISPACE">()).method("join", { py_list({ ctype, append_param }) });
                                }
                            }
                        }
                        if (ctype != self.method("get", { header }))
                        {
                            if (replace.truthy())
                            {
                                self.method("replace_header", { header, ctype });
                            }
                            else
                            {
                                del_item(self, header);
                                self.set_item(header, ctype);
                            }
                        }
                        return none();
                    }) },
                { "del_param", w.method("Message", "del_param", { py_positional("param"), py_positional("header", S<"content-type">()), py_positional("requote", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py param = a[1];
                        py header = a[2];
                        py requote = a[3];
                        py new_ctype;
                        py p;
                        py v;
                        if (!self.contains(header))
                        {
                            return none();
                        }
                        new_ctype = S<"">();
                        for (py const& item : iterate{ self.method("get_params", {  }, { { "header", header }, { "unquote", requote } }) })
                        {
                            auto const items55 = unpack(item, 2);
                            p = items55[0];
                            v = items55[1];
                            if (p.method("lower") != param.method("lower"))
                            {
                                if (!new_ctype.truthy())
                                {
                                    new_ctype = (G<"email.message._formatparam">())(p, v, requote);
                                }
                                else
                                {
                                    new_ctype = (G<"email.message.SEMISPACE">()).method("join", { py_list({ new_ctype, (G<"email.message._formatparam">())(p, v, requote) }) });
                                }
                            }
                        }
                        if (new_ctype != self.method("get", { header }))
                        {
                            del_item(self, header);
                            self.set_item(header, new_ctype);
                        }
                        return none();
                    }) },
                { "set_type", w.method("Message", "set_type", { py_positional("type"), py_positional("header", S<"Content-Type">()), py_positional("requote", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py header = a[2];
                        py requote = a[3];
                        py p;
                        py params;
                        py v;
                        // BAW: should we be strict?
                        if (!(type.method("count", { S<"/">() }) == (I<1>())))
                        {
                            py_raise(B<"ValueError">());
                        }
                        // Set the Content-Type, you get a MIME-Version
                        if (header.method("lower") == (S<"content-type">()))
                        {
                            del_item(self, S<"mime-version">());
                            self.set_item(S<"MIME-Version">(), S<"1.0">());
                        }
                        if (!self.contains(header))
                        {
                            self.set_item(header, type);
                            return none();
                        }
                        params = self.method("get_params", {  }, { { "header", header }, { "unquote", requote } });
                        del_item(self, header);
                        self.set_item(header, type);
                        // Skip the first param; it's the old type.
                        for (py const& item : iterate{ params[B<"slice">()(I<1>(), none(), none())] })
                        {
                            auto const items57 = unpack(item, 2);
                            p = items57[0];
                            v = items57[1];
                            self.method("set_param", { p, v, header, requote });
                        }
                        return none();
                    }) },
                { "get_filename", w.method("Message", "get_filename", { py_positional("failobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py failobj = a[1];
                        py filename;
                        py missing;
                        missing = (B<"object">())();
                        filename = self.method("get_param", { S<"filename">(), missing, S<"content-disposition">() });
                        if (filename.is(missing))
                        {
                            filename = self.method("get_param", { S<"name">(), missing, S<"content-type">() });
                        }
                        if (filename.is(missing))
                        {
                            return failobj;
                        }
                        return (Mod<"email.utils">().attr("collapse_rfc2231_value"))(filename).method("strip");
                    }) },
                { "get_boundary", w.method("Message", "get_boundary", { py_positional("failobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py failobj = a[1];
                        py boundary;
                        py missing;
                        missing = (B<"object">())();
                        boundary = self.method("get_param", { S<"boundary">(), missing });
                        if (boundary.is(missing))
                        {
                            return failobj;
                        }
                        // RFC 2046 says that boundaries may begin but not end in w/s
                        return (Mod<"email.utils">().attr("collapse_rfc2231_value"))(boundary).method("rstrip");
                    }) },
                { "set_boundary", w.method("Message", "set_boundary", { py_positional("boundary") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py boundary = a[1];
                        py foundp;
                        py h;
                        py k;
                        py missing;
                        py newheaders;
                        py newparams;
                        py params;
                        py parts;
                        py pk;
                        py pv;
                        py v;
                        py val;
                        missing = (B<"object">())();
                        params = self.method("_get_params_preserve", { missing, S<"content-type">() });
                        if (params.is(missing))
                        {
                            // There was no Content-Type header, and we don't know what type
                            // to set it to, so raise an exception.
                            py_raise((Mod<"email.errors">().attr("HeaderParseError"))(S<"No Content-Type header found">()));
                        }
                        newparams = py_list();
                        foundp = boolean(false);
                        for (py const& item : iterate{ params })
                        {
                            auto const items59 = unpack(item, 2);
                            pk = items59[0];
                            pv = items59[1];
                            if (pk.method("lower") == (S<"boundary">()))
                            {
                                newparams.method("append", { py_tuple({ S<"boundary">(), (S<"\"%s\"">()) % boundary }) });
                                foundp = boolean(true);
                            }
                            else
                            {
                                newparams.method("append", { py_tuple({ pk, pv }) });
                            }
                        }
                        if (!foundp.truthy())
                        {
                            // The original Content-Type header had no boundary attribute.
                            // Tack one on the end.  BAW: should we raise an exception
                            // instead???
                            newparams.method("append", { py_tuple({ S<"boundary">(), (S<"\"%s\"">()) % boundary }) });
                        }
                        // Replace the existing Content-Type header with the new value
                        newheaders = py_list();
                        for (py const& item : iterate{ self.attr("_headers") })
                        {
                            auto const items61 = unpack(item, 2);
                            h = items61[0];
                            v = items61[1];
                            if (h.method("lower") == (S<"content-type">()))
                            {
                                parts = py_list();
                                for (py const& item : iterate{ newparams })
                                {
                                    auto const items63 = unpack(item, 2);
                                    k = items63[0];
                                    v = items63[1];
                                    if (v == (S<"">()))
                                    {
                                        parts.method("append", { k });
                                    }
                                    else
                                    {
                                        parts.method("append", { (S<"%s=%s">()) % py_tuple({ k, v }) });
                                    }
                                }
                                val = (G<"email.message.SEMISPACE">()).method("join", { parts });
                                newheaders.method("append", { self.attr("policy").method("header_store_parse", { h, val }) });
                            }
                            else
                            {
                                newheaders.method("append", { py_tuple({ h, v }) });
                            }
                        }
                        self.set_attr("_headers", newheaders);
                        return none();
                    }) },
                { "get_content_charset", w.method("Message", "get_content_charset", { py_positional("failobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py failobj = a[1];
                        py as_bytes;
                        py charset;
                        py missing;
                        py pcharset;
                        missing = (B<"object">())();
                        charset = self.method("get_param", { S<"charset">(), missing });
                        if (charset.is(missing))
                        {
                            return failobj;
                        }
                        if (isinstance(charset, B<"tuple">()))
                        {
                            // RFC 2231 encoded, so decode it, and it better end up as ascii.
                            pcharset = [&]() -> py { py const t = charset[I<0>()]; return t.truthy() ? t : S<"us-ascii">(); }();
                            try
                            {
                                // LookupError will be raised if the charset isn't known to
                                // Python.  UnicodeError will be raised if the encoded text
                                // contains a character not in the charset.
                                as_bytes = charset[I<2>()].method("encode", { S<"raw-unicode-escape">() });
                                charset = (B<"str">())(as_bytes, pcharset);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler64;
                                if (matches(handler64.exception, py_tuple({ B<"LookupError">(), B<"UnicodeError">() })))
                                {
                                    charset = charset[I<2>()];
                                }
                                else
                                    py_reraise(handler64.exception);
                            }
                        }
                        // charset characters must be in us-ascii range
                        try
                        {
                            charset.method("encode", { S<"us-ascii">() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler65;
                            if (matches(handler65.exception, B<"UnicodeError">()))
                            {
                                return failobj;
                            }
                            else
                                py_reraise(handler65.exception);
                        }
                        // RFC 2046, $4.1.2 says charsets are not case sensitive
                        return charset.method("lower");
                    }) },
                { "get_charsets", w.method("Message", "get_charsets", { py_positional("failobj", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py failobj = a[1];
                        return [&]() -> py { py const comprehension66 = py_list(); py part; for (py const& item : iterate{ self.method("walk") }) { part = item; comprehension66.method("append", { part.method("get_content_charset", { failobj }) }); } return comprehension66; }();
                    }) },
                { "get_content_disposition", w.method("Message", "get_content_disposition", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py c_d;
                        py value;
                        value = self.method("get", { S<"content-disposition">() });
                        if (value.is_none())
                        {
                            return none();
                        }
                        c_d = (G<"email.message._splitparam">())(value)[I<0>()].method("lower");
                        return c_d;
                    }) },
                { "walk", M("email.iterators", "walk") }, }));

            define("email.message", "MIMEPart", w.make_class("MIMEPart", { G<"email.message.Message">() }, {
                { "__init__", w.method("MIMEPart", "__init__", { py_positional("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py policy = a[1];
                        py default_;
                        if (policy.is_none())
                        {
                            default_ = M("email.policy", "default");
                            policy = default_;
                        }
                        (B<"super">()(G<"email.message.MIMEPart">(), self)).method("__init__", { policy });
                        return none();
                    }) },
                { "as_string", w.method("MIMEPart", "as_string", { py_positional("unixfrom", boolean(false)), py_positional("maxheaderlen", none()), py_positional("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py unixfrom = a[1];
                        py maxheaderlen = a[2];
                        py policy = a[3];
                        policy = (policy.is_none() ? self.attr("policy") : policy);
                        if (maxheaderlen.is_none())
                        {
                            maxheaderlen = policy.attr("max_line_length");
                        }
                        return (B<"super">()(G<"email.message.MIMEPart">(), self)).method("as_string", { unixfrom, maxheaderlen, policy });
                    }) },
                { "__str__", w.method("MIMEPart", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("as_string", {  }, { { "policy", self.attr("policy").method("clone", {  }, { { "utf8", boolean(true) } }) } });
                    }) },
                { "is_attachment", w.method("MIMEPart", "is_attachment", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py c_d;
                        c_d = self.method("get", { S<"content-disposition">() });
                        return (c_d.is_none() ? boolean(false) : boolean(c_d.attr("content_disposition") == (S<"attachment">())));
                    }) },
                { "_find_body", w.method("MIMEPart", "_find_body", { py_positional("part"), py_positional("preferencelist") }, [](arguments a) -> py
                    {
                        return make_native_generator("_find_body", "MIMEPart._find_body", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py part = a[1];
                            py preferencelist = a[2];
                            py candidate;
                            py maintype;
                            py start;
                            py subpart;
                            py subparts;
                            py subtype;
                            if (part.method("is_attachment").truthy())
                            {
                                return none();
                            }
                            auto const items67 = unpack(part.method("get_content_type").method("split", { S<"/">() }), 2);
                            maintype = items67[0];
                            subtype = items67[1];
                            if (maintype == (S<"text">()))
                            {
                                if (preferencelist.contains(subtype))
                                {
                                    py_yield(py_tuple({ preferencelist.method("index", { subtype }), part }));
                                }
                                return none();
                            }
                            if (((maintype != (S<"multipart">())) || (!self.method("is_multipart").truthy())))
                            {
                                return none();
                            }
                            if (subtype != (S<"related">()))
                            {
                                for (py const& item : iterate{ part.method("iter_parts") })
                                {
                                    subpart = item;
                                    yield_from(self.method("_find_body", { subpart, preferencelist }));
                                }
                                return none();
                            }
                            if (preferencelist.contains((S<"related">())))
                            {
                                py_yield(py_tuple({ preferencelist.method("index", { S<"related">() }), part }));
                            }
                            candidate = none();
                            start = part.method("get_param", { S<"start">() });
                            if (start.truthy())
                            {
                                for (py const& item : iterate{ part.method("iter_parts") })
                                {
                                    subpart = item;
                                    if (subpart[S<"content-id">()] == start)
                                    {
                                        candidate = subpart;
                                        break;
                                    }
                                }
                            }
                            if (candidate.is_none())
                            {
                                subparts = part.method("get_payload");
                                candidate = (subparts.truthy() ? subparts[I<0>()] : none());
                            }
                            if (!candidate.is_none())
                            {
                                yield_from(self.method("_find_body", { candidate, preferencelist }));
                            }
                            return none();
                        });
                    }) },
                { "get_body", w.method("MIMEPart", "get_body", { py_positional("preferencelist", py_tuple({ S<"related">(), S<"html">(), S<"plain">() })) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py preferencelist = a[1];
                        py best_prio;
                        py body;
                        py part;
                        py prio;
                        best_prio = py{ preferencelist.size() };
                        body = none();
                        for (py const& item : iterate{ self.method("_find_body", { self, preferencelist }) })
                        {
                            auto const items71 = unpack(item, 2);
                            prio = items71[0];
                            part = items71[1];
                            if (prio < best_prio)
                            {
                                best_prio = prio;
                                body = part;
                                if (prio == (I<0>()))
                                {
                                    break;
                                }
                            }
                        }
                        return body;
                    }) },
                { "_body_types", set_of({ py_tuple({ S<"text">(), S<"plain">() }), py_tuple({ S<"text">(), S<"html">() }), py_tuple({ S<"multipart">(), S<"related">() }), py_tuple({ S<"multipart">(), S<"alternative">() }) }) },
                { "iter_attachments", w.method("MIMEPart", "iter_attachments", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("iter_attachments", "MIMEPart.iter_attachments", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py attachments;
                            py found;
                            py maintype;
                            py part;
                            py parts;
                            py payload;
                            py seen;
                            py start;
                            py subtype;
                            auto const items72 = unpack(self.method("get_content_type").method("split", { S<"/">() }), 2);
                            maintype = items72[0];
                            subtype = items72[1];
                            if (((maintype != (S<"multipart">())) || (subtype == (S<"alternative">()))))
                            {
                                return none();
                            }
                            payload = self.method("get_payload");
                            // Certain malformed messages can have content type set to `multipart/*`
                            // but still have single part body, in which case payload.copy() can
                            // fail with AttributeError.
                            try
                            {
                                parts = payload.method("copy");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler73;
                                if (matches(handler73.exception, B<"AttributeError">()))
                                {
                                    // payload is not a list, it is most probably a string.
                                    return none();
                                }
                                else
                                    py_reraise(handler73.exception);
                            }
                            if (((maintype == (S<"multipart">())) && (subtype == (S<"related">()))))
                            {
                                // For related, we treat everything but the root as an attachment.
                                // The root may be indicated by 'start'; if there's no start or we
                                // can't find the named start, treat the first subpart as the root.
                                start = self.method("get_param", { S<"start">() });
                                if (start.truthy())
                                {
                                    found = boolean(false);
                                    attachments = py_list();
                                    for (py const& item : iterate{ parts })
                                    {
                                        part = item;
                                        if (part.method("get", { S<"content-id">() }) == start)
                                        {
                                            found = boolean(true);
                                        }
                                        else
                                        {
                                            attachments.method("append", { part });
                                        }
                                    }
                                    if (found.truthy())
                                    {
                                        yield_from(attachments);
                                        return none();
                                    }
                                }
                                parts.method("pop", { I<0>() });
                                yield_from(parts);
                                return none();
                            }
                            // Otherwise we more or less invert the remaining logic in get_body.
                            // This only really works in edge cases (ex: non-text related or
                            // alternatives) if the sending agent sets content-disposition.
                            seen = py_list();
                            for (py const& item : iterate{ parts })
                            {
                                part = item;
                                auto const items76 = unpack(part.method("get_content_type").method("split", { S<"/">() }), 2);
                                maintype = items76[0];
                                subtype = items76[1];
                                if ((self.attr("_body_types").contains(py_tuple({ maintype, subtype })) && (!part.method("is_attachment").truthy()) && (!seen.contains(subtype))))
                                {
                                    seen.method("append", { subtype });
                                    continue;
                                }
                                py_yield(part);
                            }
                            return none();
                        });
                    }) },
                { "iter_parts", w.method("MIMEPart", "iter_parts", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("iter_parts", "MIMEPart.iter_parts", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            if (self.method("is_multipart").truthy())
                            {
                                yield_from(self.method("get_payload"));
                            }
                            return none();
                        });
                    }) },
                { "get_content", w.method("MIMEPart", "get_content", { py_keyword("content_manager", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content_manager = a[1];
                        py args = a[2];
                        py kw = a[3];
                        if (content_manager.is_none())
                        {
                            content_manager = self.attr("policy").attr("content_manager");
                        }
                        return call_ex(content_manager.attr("get_content"), { self }, args, kw);
                    }, true, true) },
                { "set_content", w.method("MIMEPart", "set_content", { py_keyword("content_manager", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content_manager = a[1];
                        py args = a[2];
                        py kw = a[3];
                        if (content_manager.is_none())
                        {
                            content_manager = self.attr("policy").attr("content_manager");
                        }
                        call_ex(content_manager.attr("set_content"), { self }, args, kw);
                        return none();
                    }, true, true) },
                { "_make_multipart", w.method("MIMEPart", "_make_multipart", { py_positional("subtype"), py_positional("disallowed_subtypes"), py_positional("boundary") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py subtype = a[1];
                        py disallowed_subtypes = a[2];
                        py boundary = a[3];
                        py existing_subtype;
                        py keep_headers;
                        py name;
                        py part;
                        py part_headers;
                        py value;
                        if (self.method("get_content_maintype") == (S<"multipart">()))
                        {
                            existing_subtype = self.method("get_content_subtype");
                            disallowed_subtypes = disallowed_subtypes + py_tuple({ subtype });
                            if (disallowed_subtypes.contains(existing_subtype))
                            {
                                py_raise((B<"ValueError">())((S<"Cannot convert {} to {}">()).method("format", { existing_subtype, subtype })));
                            }
                        }
                        keep_headers = py_list();
                        part_headers = py_list();
                        for (py const& item : iterate{ self.attr("_headers") })
                        {
                            auto const items78 = unpack(item, 2);
                            name = items78[0];
                            value = items78[1];
                            if (name.method("lower").method("startswith", { S<"content-">() }).truthy())
                            {
                                part_headers.method("append", { py_tuple({ name, value }) });
                            }
                            else
                            {
                                keep_headers.method("append", { py_tuple({ name, value }) });
                            }
                        }
                        if (part_headers.truthy())
                        {
                            // There is existing content, move it to the first subpart.
                            part = (B<"type">())(self).call({  }, { { "policy", self.attr("policy") } });
                            part.set_attr("_headers", part_headers);
                            part.set_attr("_payload", self.attr("_payload"));
                            self.set_attr("_payload", py_list({ part }));
                        }
                        else
                        {
                            self.set_attr("_payload", py_list());
                        }
                        self.set_attr("_headers", keep_headers);
                        self.set_item(S<"Content-Type">(), (S<"multipart/">()) + subtype);
                        if (!boundary.is_none())
                        {
                            self.method("set_param", { S<"boundary">(), boundary });
                        }
                        return none();
                    }) },
                { "make_related", w.method("MIMEPart", "make_related", { py_positional("boundary", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py boundary = a[1];
                        self.method("_make_multipart", { S<"related">(), py_tuple({ S<"alternative">(), S<"mixed">() }), boundary });
                        return none();
                    }) },
                { "make_alternative", w.method("MIMEPart", "make_alternative", { py_positional("boundary", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py boundary = a[1];
                        self.method("_make_multipart", { S<"alternative">(), py_tuple({ S<"mixed">() }), boundary });
                        return none();
                    }) },
                { "make_mixed", w.method("MIMEPart", "make_mixed", { py_positional("boundary", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py boundary = a[1];
                        self.method("_make_multipart", { S<"mixed">(), py_tuple(), boundary });
                        return none();
                    }) },
                { "_add_multipart", w.method("MIMEPart", "_add_multipart", { py_positional("_subtype"), py_keyword("_disp", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _subtype = a[1];
                        py _disp = a[2];
                        py args = a[3];
                        py kw = a[4];
                        py part;
                        if (((self.method("get_content_maintype") != (S<"multipart">())) || (self.method("get_content_subtype") != _subtype)))
                        {
                            (B<"getattr">())(self, (S<"make_">()) + _subtype)();
                        }
                        part = (B<"type">())(self).call({  }, { { "policy", self.attr("policy") } });
                        call_ex(part.attr("set_content"), {  }, args, kw);
                        if ((_disp.truthy() && (!part.contains((S<"content-disposition">())))))
                        {
                            part.set_item(S<"Content-Disposition">(), _disp);
                        }
                        self.method("attach", { part });
                        return none();
                    }, true, true) },
                { "add_related", w.method("MIMEPart", "add_related", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex(self.attr("_add_multipart"), { S<"related">() }, args, kw, { { "_disp", S<"inline">() } });
                        return none();
                    }, true, true) },
                { "add_alternative", w.method("MIMEPart", "add_alternative", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex(self.attr("_add_multipart"), { S<"alternative">() }, args, kw);
                        return none();
                    }, true, true) },
                { "add_attachment", w.method("MIMEPart", "add_attachment", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex(self.attr("_add_multipart"), { S<"mixed">() }, args, kw, { { "_disp", S<"attachment">() } });
                        return none();
                    }, true, true) },
                { "clear", w.method("MIMEPart", "clear", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_headers", py_list());
                        self.set_attr("_payload", none());
                        return none();
                    }) },
                { "clear_content", w.method("MIMEPart", "clear_content", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_headers", [&]() -> py { py const comprehension79 = py_list(); py n; py v; for (py const& item : iterate{ self.attr("_headers") }) { auto const items80 = unpack(item, 2); n = items80[0]; v = items80[1]; if (n.method("lower").method("startswith", { S<"content-">() }).truthy()) continue; comprehension79.method("append", { py_tuple({ n, v }) }); } return comprehension79; }());
                        self.set_attr("_payload", none());
                        return none();
                    }) }, }));

            define("email.message", "EmailMessage", w.make_class("EmailMessage", { G<"email.message.MIMEPart">() }, {
                { "set_content", w.method("EmailMessage", "set_content", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex((B<"super">()(G<"email.message.EmailMessage">(), self)).attr("set_content"), {  }, args, kw);
                        if (!self.contains((S<"MIME-Version">())))
                        {
                            self.set_item(S<"MIME-Version">(), S<"1.0">());
                        }
                        return none();
                    }, true, true) }, }));
        }

        bool email_message_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_message();
                }
                if (auto const doc = email_message_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("binascii", py{ fn_import("binascii") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("quopri", py{ fn_import("quopri") }.release());
                aAttributes.emplace_back("BytesIO", import_attribute("io", "BytesIO").release());
                aAttributes.emplace_back("StringIO", import_attribute("io", "StringIO").release());
                aAttributes.emplace_back("utils", import_attribute("email", "utils").release());
                aAttributes.emplace_back("errors", import_attribute("email", "errors").release());
                aAttributes.emplace_back("compat32", import_attribute("email._policybase", "compat32").release());
                aAttributes.emplace_back("_charset", import_attribute("email", "charset").release());
                aAttributes.emplace_back("decode_b", import_attribute("email._encoded_words", "decode_b").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.message", email_message_module);
    }
}
