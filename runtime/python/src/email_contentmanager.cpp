/*
  email_contentmanager.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/contentmanager.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.contentmanager (CPython 3.13's Lib/email/contentmanager.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_contentmanager_docs.hpp"

namespace neos::python_runtime::detail::email_contentmanager_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.contentmanager.name"))
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
                py_raise("SystemError", std::string{ "email.contentmanager: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.contentmanager.name">()))
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
            if (modules_imported("email.contentmanager"))
                return py{ fn_import("email.contentmanager") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.contentmanager" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.contentmanager", email_contentmanager_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.contentmanager (Lib/email/contentmanager.py)

        void make_email_contentmanager()
        {
            static module_writer const& w = writer("email.contentmanager");
            define("email.contentmanager", "ContentManager", w.make_class("ContentManager", {  }, {
                { "__init__", w.method("ContentManager", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("get_handlers", py_dict());
                        self.set_attr("set_handlers", py_dict());
                        return none();
                    }) },
                { "add_get_handler", w.method("ContentManager", "add_get_handler", { py_positional("key"), py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py handler = a[2];
                        self.attr("get_handlers").set_item(key, handler);
                        return none();
                    }) },
                { "get_content", w.method("ContentManager", "get_content", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py args = a[2];
                        py kw = a[3];
                        py content_type;
                        py maintype;
                        content_type = msg.method("get_content_type");
                        if (self.attr("get_handlers").contains(content_type))
                        {
                            return call_ex(self.attr("get_handlers")[content_type], { msg }, args, kw);
                        }
                        maintype = msg.method("get_content_maintype");
                        if (self.attr("get_handlers").contains(maintype))
                        {
                            return call_ex(self.attr("get_handlers")[maintype], { msg }, args, kw);
                        }
                        if (self.attr("get_handlers").contains((S<"">())))
                        {
                            return call_ex(self.attr("get_handlers")[S<"">()], { msg }, args, kw);
                        }
                        py_raise((B<"KeyError">())(content_type));
                    }, true, true) },
                { "add_set_handler", w.method("ContentManager", "add_set_handler", { py_positional("typekey"), py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py typekey = a[1];
                        py handler = a[2];
                        self.attr("set_handlers").set_item(typekey, handler);
                        return none();
                    }) },
                { "set_content", w.method("ContentManager", "set_content", { py_positional("msg"), py_positional("obj") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py obj = a[2];
                        py args = a[3];
                        py kw = a[4];
                        py handler;
                        if (msg.method("get_content_maintype") == (S<"multipart">()))
                        {
                            // XXX: is this error a good idea or not?  We can remove it later,
                            // but we can't add it later, so do it for now.
                            py_raise((B<"TypeError">())(S<"set_content not valid on multipart">()));
                        }
                        handler = self.method("_find_set_handler", { msg, obj });
                        msg.method("clear_content");
                        call_ex(handler, { msg, obj }, args, kw);
                        return none();
                    }, true, true) },
                { "_find_set_handler", w.method("ContentManager", "_find_set_handler", { py_positional("msg"), py_positional("obj") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py obj = a[2];
                        py full_path;
                        py full_path_for_error;
                        py modname;
                        py name;
                        py qname;
                        py typ;
                        full_path_for_error = none();
                        for (py const& item : iterate{ (B<"type">())(obj).attr("__mro__") })
                        {
                            typ = item;
                            if (self.attr("set_handlers").contains(typ))
                            {
                                return self.attr("set_handlers")[typ];
                            }
                            qname = typ.attr("__qualname__");
                            modname = (B<"getattr">())(typ, S<"__module__">(), S<"">());
                            full_path = (modname.truthy() ? (S<".">()).method("join", { py_tuple({ modname, qname }) }) : qname);
                            if (full_path_for_error.is_none())
                            {
                                full_path_for_error = full_path;
                            }
                            if (self.attr("set_handlers").contains(full_path))
                            {
                                return self.attr("set_handlers")[full_path];
                            }
                            if (self.attr("set_handlers").contains(qname))
                            {
                                return self.attr("set_handlers")[qname];
                            }
                            name = typ.attr("__name__");
                            if (self.attr("set_handlers").contains(name))
                            {
                                return self.attr("set_handlers")[name];
                            }
                        }
                        if (self.attr("set_handlers").contains(none()))
                        {
                            return self.attr("set_handlers")[none()];
                        }
                        py_raise((B<"KeyError">())(full_path_for_error));
                    }) }, }));

            define("email.contentmanager", "raw_data_manager", (G<"email.contentmanager.ContentManager">())());
            define("email.contentmanager", "get_text_content", w.function("get_text_content", "get_text_content", { py_positional("msg"), py_positional("errors", S<"replace">()) }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py errors = a[1];
                    py charset;
                    py content;
                    content = msg.method("get_payload", {  }, { { "decode", boolean(true) } });
                    charset = msg.method("get_param", { S<"charset">(), S<"ASCII">() });
                    return content.method("decode", { charset }, { { "errors", errors } });
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"email.contentmanager.raw_data_manager">()).method("add_get_handler", { S<"text">(), G<"email.contentmanager.get_text_content">() });
                    return none();
                }))();
            define("email.contentmanager", "get_non_text_content", w.function("get_non_text_content", "get_non_text_content", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    return msg.method("get_payload", {  }, { { "decode", boolean(true) } });
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ (S<"audio image video application">()).method("split") })
                    {
                        set_global("email.contentmanager", "maintype", item);
                        (G<"email.contentmanager.raw_data_manager">()).method("add_get_handler", { G<"email.contentmanager.maintype">(), G<"email.contentmanager.get_non_text_content">() });
                    }
                    return none();
                }))();
            undefine("email.contentmanager", "maintype");
            define("email.contentmanager", "get_message_content", w.function("get_message_content", "get_message_content", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    return msg.method("get_payload", { I<0>() });
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ (S<"rfc822 external-body">()).method("split") })
                    {
                        set_global("email.contentmanager", "subtype", item);
                        (G<"email.contentmanager.raw_data_manager">()).method("add_get_handler", { (S<"message/">()) + (G<"email.contentmanager.subtype">()), G<"email.contentmanager.get_message_content">() });
                    }
                    return none();
                }))();
            undefine("email.contentmanager", "subtype");
            define("email.contentmanager", "get_and_fixup_unknown_message_content", w.function("get_and_fixup_unknown_message_content", "get_and_fixup_unknown_message_content", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    return (B<"bytes">())(msg.method("get_payload", { I<0>() }));
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"email.contentmanager.raw_data_manager">()).method("add_get_handler", { S<"message">(), G<"email.contentmanager.get_and_fixup_unknown_message_content">() });
                    return none();
                }))();
            define("email.contentmanager", "_prepare_set", w.function("_prepare_set", "_prepare_set", { py_positional("msg"), py_positional("maintype"), py_positional("subtype"), py_positional("headers") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py maintype = a[1];
                    py subtype = a[2];
                    py headers = a[3];
                    py exc;
                    py header;
                    py mp;
                    msg.set_item(S<"Content-Type">(), (S<"/">()).method("join", { py_tuple({ maintype, subtype }) }));
                    if (headers.truthy())
                    {
                        if (!has_attr(headers[I<0>()], "name"))
                        {
                            mp = msg.attr("policy");
                            headers = [&]() -> py { py const comprehension4 = py_list(); py header; for (py const& item : iterate{ headers }) { header = item; comprehension4.method("append", { call_ex(mp.attr("header_factory"), {  }, mp.method("header_source_parse", { py_list({ header }) }), py{}) }); } return comprehension4; }();
                        }
                        try
                        {
                            for (py const& item : iterate{ headers })
                            {
                                header = item;
                                if (header.attr("defects").truthy())
                                {
                                    py_raise(header.attr("defects")[I<0>()]);
                                }
                                msg.set_item(header.attr("name"), header);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, Mod<"email.errors">().attr("HeaderDefect")))
                            {
                                exc = handler5.exception;
                                fn_raise_from((B<"ValueError">())((S<"Invalid header: {}">()).method("format", { header.method("fold", {  }, { { "policy", msg.attr("policy") } }) })).get(), exc.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                    }
                    return none();
                }));

            define("email.contentmanager", "_finalize_set", w.function("_finalize_set", "_finalize_set", { py_positional("msg"), py_positional("disposition"), py_positional("filename"), py_positional("cid"), py_positional("params") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py disposition = a[1];
                    py filename = a[2];
                    py cid = a[3];
                    py params = a[4];
                    py key;
                    py value;
                    if ((disposition.is_none() && (!filename.is_none())))
                    {
                        disposition = S<"attachment">();
                    }
                    if (!disposition.is_none())
                    {
                        msg.set_item(S<"Content-Disposition">(), disposition);
                    }
                    if (!filename.is_none())
                    {
                        msg.method("set_param", { S<"filename">(), filename }, { { "header", S<"Content-Disposition">() }, { "replace", boolean(true) } });
                    }
                    if (!cid.is_none())
                    {
                        msg.set_item(S<"Content-ID">(), cid);
                    }
                    if (!params.is_none())
                    {
                        for (py const& item : iterate{ params.method("items") })
                        {
                            auto const items8 = unpack(item, 2);
                            key = items8[0];
                            value = items8[1];
                            msg.method("set_param", { key, value });
                        }
                    }
                    return none();
                }));

            // XXX: This is a cleaned-up version of base64mime.body_encode (including a bug
            // fix in the calculation of unencoded_bytes_per_line).  It would be nice to
            // drop both this and quoprimime.body_encode in favor of enhanced binascii
            // routines that accepted a max_line_length parameter.
            define("email.contentmanager", "_encode_base64", w.function("_encode_base64", "_encode_base64", { py_positional("data"), py_positional("max_line_length") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py max_line_length = a[1];
                    py encoded_lines;
                    py i;
                    py thisline;
                    py unencoded_bytes_per_line;
                    encoded_lines = py_list();
                    unencoded_bytes_per_line = floordiv(max_line_length, (I<4>())) * (I<3>());
                    for (py const& item : iterate{ (B<"range">())(I<0>(), py{ data.size() }, unencoded_bytes_per_line) })
                    {
                        i = item;
                        thisline = data[B<"slice">()(i, i + unencoded_bytes_per_line, none())];
                        encoded_lines.method("append", { (Mod<"binascii">().attr("b2a_base64"))(thisline).method("decode", { S<"ascii">() }) });
                    }
                    return (S<"">()).method("join", { encoded_lines });
                }));

            define("email.contentmanager", "_encode_text", w.function("_encode_text", "_encode_text", { py_positional("string"), py_positional("charset"), py_positional("cte"), py_positional("policy") }, [](arguments a) -> py
                {
                    py string = a[0];
                    py charset = a[1];
                    py cte = a[2];
                    py policy = a[3];
                    py data;
                    py embedded_body;
                    py lines;
                    auto const linesep = std::make_shared<py>();
                    py maxlen;
                    py normal_body;
                    py sniff;
                    py sniff_base64;
                    py sniff_qp;
                    maxlen = [&]() -> py { py const t = policy.attr("max_line_length"); return t.truthy() ? t : Mod<"sys">().attr("maxsize"); }();
                    lines = string.method("encode", { charset }).method("splitlines");
                    *linesep = policy.attr("linesep").method("encode", { S<"ascii">() });
                    embedded_body = w.function("embedded_body", "_encode_text.<locals>.embedded_body", { py_positional("lines") }, [linesep](arguments a) -> py
    {
        py lines = a[0];
        return (*linesep).method("join", { lines }) + (*linesep);
    });
                    normal_body = w.function("normal_body", "_encode_text.<locals>.normal_body", { py_positional("lines") }, [](arguments a) -> py
    {
        py lines = a[0];
        return py{ make_bytes(std::string{ "\x0a", 1 }) }.method("join", { lines }) + py{ make_bytes(std::string{ "\x0a", 1 }) };
    });
                    if (cte.is_none())
                    {
                        // Use heuristics to decide on the "best" encoding.
                        if ((B<"max">()).call({ (B<"map">())(B<"len">(), lines) }, { { "default", I<0>() } }) <= maxlen)
                        {
                            try
                            {
                                return py_tuple({ S<"7bit">(), normal_body(lines).method("decode", { S<"ascii">() }) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                if (matches(handler10.exception, B<"UnicodeDecodeError">()))
                                {
                                }
                                else
                                    py_reraise(handler10.exception);
                            }
                            if (policy.attr("cte_type") == (S<"8bit">()))
                            {
                                return py_tuple({ S<"8bit">(), normal_body(lines).method("decode", { S<"ascii">(), S<"surrogateescape">() }) });
                            }
                        }
                        sniff = embedded_body(lines[B<"slice">()(none(), I<10>(), none())]);
                        sniff_qp = (Mod<"email.quoprimime">().attr("body_encode"))(sniff.method("decode", { S<"latin-1">() }), maxlen);
                        sniff_base64 = (Mod<"binascii">().attr("b2a_base64"))(sniff);
                        // This is a little unfair to qp; it includes lineseps, base64 doesn't.
                        if (py{ sniff_qp.size() } > py{ sniff_base64.size() })
                        {
                            cte = S<"base64">();
                        }
                        else
                        {
                            cte = S<"quoted-printable">();
                            if (py{ lines.size() } <= (I<10>()))
                            {
                                return py_tuple({ cte, sniff_qp });
                            }
                        }
                    }
                    if (cte == (S<"7bit">()))
                    {
                        data = normal_body(lines).method("decode", { S<"ascii">() });
                    }
                    else if (cte == (S<"8bit">()))
                    {
                        data = normal_body(lines).method("decode", { S<"ascii">(), S<"surrogateescape">() });
                    }
                    else if (cte == (S<"quoted-printable">()))
                    {
                        data = (Mod<"email.quoprimime">().attr("body_encode"))(normal_body(lines).method("decode", { S<"latin-1">() }), maxlen);
                    }
                    else if (cte == (S<"base64">()))
                    {
                        data = (G<"email.contentmanager._encode_base64">())(embedded_body(lines), maxlen);
                    }
                    else
                    {
                        py_raise((B<"ValueError">())((S<"Unknown content transfer encoding {}">()).method("format", { cte })));
                    }
                    return py_tuple({ cte, data });
                }));

            define("email.contentmanager", "set_text_content", w.function("set_text_content", "set_text_content", { py_positional("msg"), py_positional("string"), py_positional("subtype", S<"plain">()), py_positional("charset", S<"utf-8">()), py_positional("cte", none()), py_positional("disposition", none()), py_positional("filename", none()), py_positional("cid", none()), py_positional("params", none()), py_positional("headers", none()) }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py string = a[1];
                    py subtype = a[2];
                    py charset = a[3];
                    py cte = a[4];
                    py disposition = a[5];
                    py filename = a[6];
                    py cid = a[7];
                    py params = a[8];
                    py headers = a[9];
                    py cs;
                    py payload;
                    (G<"email.contentmanager._prepare_set">())(msg, S<"text">(), subtype, headers);
                    cs = (Mod<"email.charset">().attr("Charset"))(charset);
                    charset = cs.attr("output_charset");
                    auto const items11 = unpack((G<"email.contentmanager._encode_text">())(string, charset, cte, msg.attr("policy")), 2);
                    cte = items11[0];
                    payload = items11[1];
                    msg.method("set_payload", { payload });
                    msg.method("set_param", { S<"charset">(), charset }, { { "replace", boolean(true) } });
                    msg.set_item(S<"Content-Transfer-Encoding">(), cte);
                    (G<"email.contentmanager._finalize_set">())(msg, disposition, filename, cid, params);
                    return none();
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"email.contentmanager.raw_data_manager">()).method("add_set_handler", { B<"str">(), G<"email.contentmanager.set_text_content">() });
                    return none();
                }))();
            define("email.contentmanager", "set_message_content", w.function("set_message_content", "set_message_content", { py_positional("msg"), py_positional("message"), py_positional("subtype", S<"rfc822">()), py_positional("cte", none()), py_positional("disposition", none()), py_positional("filename", none()), py_positional("cid", none()), py_positional("params", none()), py_positional("headers", none()) }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py message = a[1];
                    py subtype = a[2];
                    py cte = a[3];
                    py disposition = a[4];
                    py filename = a[5];
                    py cid = a[6];
                    py params = a[7];
                    py headers = a[8];
                    if (subtype == (S<"partial">()))
                    {
                        py_raise((B<"ValueError">())(S<"message/partial is not supported for Message objects">()));
                    }
                    if (subtype == (S<"rfc822">()))
                    {
                        if (!py_tuple({ none(), S<"7bit">(), S<"8bit">(), S<"binary">() }).contains(cte))
                        {
                            // http://tools.ietf.org/html/rfc2046#section-5.2.1 mandate.
                            py_raise((B<"ValueError">())((S<"message/rfc822 parts do not support cte={}">()).method("format", { cte })));
                        }
                        // 8bit will get coerced on serialization if policy.cte_type='7bit'.  We
                        // may end up claiming 8bit when it isn't needed, but the only negative
                        // result of that should be a gateway that needs to coerce to 7bit
                        // having to look through the whole embedded message to discover whether
                        // or not it actually has to do anything.
                        cte = (cte.is_none() ? S<"8bit">() : cte);
                    }
                    else if (subtype == (S<"external-body">()))
                    {
                        if (!py_tuple({ none(), S<"7bit">() }).contains(cte))
                        {
                            // http://tools.ietf.org/html/rfc2046#section-5.2.3 mandate.
                            py_raise((B<"ValueError">())((S<"message/external-body parts do not support cte={}">()).method("format", { cte })));
                        }
                        cte = S<"7bit">();
                    }
                    else if (cte.is_none())
                    {
                        // http://tools.ietf.org/html/rfc2046#section-5.2.4 says all future
                        // subtypes should be restricted to 7bit, so assume that.
                        cte = S<"7bit">();
                    }
                    (G<"email.contentmanager._prepare_set">())(msg, S<"message">(), subtype, headers);
                    msg.method("set_payload", { py_list({ message }) });
                    msg.set_item(S<"Content-Transfer-Encoding">(), cte);
                    (G<"email.contentmanager._finalize_set">())(msg, disposition, filename, cid, params);
                    return none();
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"email.contentmanager.raw_data_manager">()).method("add_set_handler", { Mod<"email.message">().attr("Message"), G<"email.contentmanager.set_message_content">() });
                    return none();
                }))();
            define("email.contentmanager", "set_bytes_content", w.function("set_bytes_content", "set_bytes_content", { py_positional("msg"), py_positional("data"), py_positional("maintype"), py_positional("subtype"), py_positional("cte", S<"base64">()), py_positional("disposition", none()), py_positional("filename", none()), py_positional("cid", none()), py_positional("params", none()), py_positional("headers", none()) }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py data = a[1];
                    py maintype = a[2];
                    py subtype = a[3];
                    py cte = a[4];
                    py disposition = a[5];
                    py filename = a[6];
                    py cid = a[7];
                    py params = a[8];
                    py headers = a[9];
                    (G<"email.contentmanager._prepare_set">())(msg, maintype, subtype, headers);
                    if (cte == (S<"base64">()))
                    {
                        data = (G<"email.contentmanager._encode_base64">()).call({ data }, { { "max_line_length", msg.attr("policy").attr("max_line_length") } });
                    }
                    else if (cte == (S<"quoted-printable">()))
                    {
                        // XXX: quoprimime.body_encode won't encode newline characters in data,
                        // so we can't use it.  This means max_line_length is ignored.  Another
                        // bug to fix later.  (Note: encoders.quopri is broken on line ends.)
                        data = (Mod<"binascii">().attr("b2a_qp")).call({ data }, { { "istext", boolean(false) }, { "header", boolean(false) }, { "quotetabs", boolean(true) } });
                        data = data.method("decode", { S<"ascii">() });
                    }
                    else if (cte == (S<"7bit">()))
                    {
                        data = data.method("decode", { S<"ascii">() });
                    }
                    else if (py_tuple({ S<"8bit">(), S<"binary">() }).contains(cte))
                    {
                        data = data.method("decode", { S<"ascii">(), S<"surrogateescape">() });
                    }
                    msg.method("set_payload", { data });
                    msg.set_item(S<"Content-Transfer-Encoding">(), cte);
                    (G<"email.contentmanager._finalize_set">())(msg, disposition, filename, cid, params);
                    return none();
                }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    for (py const& item : iterate{ py_tuple({ B<"bytes">(), B<"bytearray">(), B<"memoryview">() }) })
                    {
                        set_global("email.contentmanager", "typ", item);
                        (G<"email.contentmanager.raw_data_manager">()).method("add_set_handler", { G<"email.contentmanager.typ">(), G<"email.contentmanager.set_bytes_content">() });
                    }
                    return none();
                }))();
            undefine("email.contentmanager", "typ");
        }

        bool email_contentmanager_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_contentmanager();
                }
                if (auto const doc = email_contentmanager_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("binascii", py{ fn_import("binascii") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("quoprimime", import_attribute("email", "quoprimime").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.contentmanager", email_contentmanager_module);
    }
}
