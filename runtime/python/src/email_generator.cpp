/*
  email_generator.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/generator.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.generator (CPython 3.13's Lib/email/generator.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_generator_docs.hpp"

namespace neos::python_runtime::detail::email_generator_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.generator.name"))
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
                py_raise("SystemError", std::string{ "email.generator: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.generator.name">()))
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
            if (modules_imported("email.generator"))
                return py{ fn_import("email.generator") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.generator" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.generator", email_generator_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.generator (Lib/email/generator.py)

        void make_email_generator()
        {
            static module_writer const& w = writer("email.generator");
            // Copyright (C) 2001-2010 Python Software Foundation
            // Author: Barry Warsaw
            // Contact: email-sig@python.org
            define("email.generator", "__all__", py_list({ S<"Generator">(), S<"DecodedGenerator">(), S<"BytesGenerator">() }));
            define("email.generator", "UNDERSCORE", S<"_">());
            define("email.generator", "NL", S<"\n">());
            define("email.generator", "NLCRE", (Mod<"re">()).method("compile", { S<"\\r\\n|\\r|\\n">() }));
            define("email.generator", "fcre", (Mod<"re">()).method("compile", { S<"^From ">(), Mod<"re">().attr("MULTILINE") }));
            define("email.generator", "NEWLINE_WITHOUT_FWSP", (Mod<"re">()).method("compile", { S<"\\r\\n[^ \\t]|\\r[^ \\n\\t]|\\n[^ \\t]">() }));
            define("email.generator", "NEWLINE_WITHOUT_FWSP_BYTES", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x72\x5c\x6e\x5b\x5e\x20\x5c\x74\x5d\x7c\x5c\x72\x5b\x5e\x20\x5c\x6e\x5c\x74\x5d\x7c\x5c\x6e\x5b\x5e\x20\x5c\x74\x5d", 30 }) } }));
                // 
                // Public interface
                // 
                // 
                // Protected interface - undocumented ;/
                // 
                // Note that we use 'self.write' when what we are writing is coming from
                // the source, and self._fp.write when what we are writing is coming from a
                // buffer (because the Bytes subclass has already had a chance to transform
                // the data in its write method in that case).  This is an entirely
                // pragmatic split determined by experiment; we could be more general by
                // always using write and having the Bytes subclass write method detect when
                // it has already transformed the input; but, since this whole thing is a
                // hack anyway this seems good enough.
                // XXX logic tells me this else should be needed, but the tests fail
                // with it and pass without it.  (NLCRE.split ends with a blank element
                // if and only if there was a trailing newline.)
                // else:
                // self.write(self._NL)
                // 
                // Default handlers
                // 
                // 
                // Handlers for writing types and subtypes
                // 
                // Default body handler
                // This used to be a module level function; we use a classmethod for this
                // and _compile_re so we can continue to provide the module level function
                // for backward compatibility by doing
                // _make_boundary = Generator._make_boundary
                // at the end of the module.  It *is* internal, so we could drop that...
            define("email.generator", "Generator", w.make_class("Generator", {  }, {
                { "__init__", w.method("Generator", "__init__", { py_positional("outfp"), py_positional("mangle_from_", none()), py_positional("maxheaderlen", none()), py_keyword("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py outfp = a[1];
                        py mangle_from_ = a[2];
                        py maxheaderlen = a[3];
                        py policy = a[4];
                        if (mangle_from_.is_none())
                        {
                            mangle_from_ = (policy.is_none() ? boolean(true) : policy.attr("mangle_from_"));
                        }
                        self.set_attr("_fp", outfp);
                        self.set_attr("_mangle_from_", mangle_from_);
                        self.set_attr("maxheaderlen", maxheaderlen);
                        self.set_attr("policy", policy);
                        return none();
                    }) },
                { "write", w.method("Generator", "write", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        self.attr("_fp").method("write", { s });
                        return none();
                    }) },
                { "flatten", w.method("Generator", "flatten", { py_positional("msg"), py_positional("unixfrom", boolean(false)), py_positional("linesep", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py unixfrom = a[2];
                        py linesep = a[3];
                        py old_gen_policy;
                        py old_msg_policy;
                        py policy;
                        py ufrom;
                        // We use the _XXX constants for operating on data that comes directly
                        // from the msg, and _encoded_XXX constants for operating on data that
                        // has already been converted (to bytes in the BytesGenerator) and
                        // inserted into a temporary buffer.
                        policy = (self.attr("policy").is_none() ? msg.attr("policy") : self.attr("policy"));
                        if (!linesep.is_none())
                        {
                            policy = policy.method("clone", {  }, { { "linesep", linesep } });
                        }
                        if (!self.attr("maxheaderlen").is_none())
                        {
                            policy = policy.method("clone", {  }, { { "max_line_length", self.attr("maxheaderlen") } });
                        }
                        self.set_attr("_NL", policy.attr("linesep"));
                        self.set_attr("_encoded_NL", self.method("_encode", { self.attr("_NL") }));
                        self.set_attr("_EMPTY", S<"">());
                        self.set_attr("_encoded_EMPTY", self.method("_encode", { self.attr("_EMPTY") }));
                        // Because we use clone (below) when we recursively process message
                        // subparts, and because clone uses the computed policy (not None),
                        // submessages will automatically get set to the computed policy when
                        // they are processed by this code.
                        old_gen_policy = self.attr("policy");
                        old_msg_policy = msg.attr("policy");
                        {
                            std::optional<py> pending1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            try
                            {
                                self.set_attr("policy", policy);
                                msg.set_attr("policy", policy);
                                if (unixfrom.truthy())
                                {
                                    ufrom = msg.method("get_unixfrom");
                                    if (!ufrom.truthy())
                                    {
                                        ufrom = (S<"From nobody ">()) + (Mod<"time">().attr("ctime"))((Mod<"time">().attr("time"))());
                                    }
                                    self.method("write", { ufrom + self.attr("_NL") });
                                }
                                self.method("_write", { msg });
                            }
                            catch (raised const&)
                            {
                                pending1 = py_take_exception();
                            }
                            self.set_attr("policy", old_gen_policy);
                            msg.set_attr("policy", old_msg_policy);
                            if (pending1)
                                py_reraise(*pending1);
                            if (flow1 == 1)
                                return result1;
                        }
                        return none();
                    }) },
                { "clone", w.method("Generator", "clone", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        return self.method("__class__", { fp, self.attr("_mangle_from_"), none() }, { { "policy", self.attr("policy") } });
                    }) },
                { "_new_buffer", w.method("Generator", "_new_buffer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"io">().attr("StringIO"))();
                    }) },
                { "_encode", w.method("Generator", "_encode", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        return s;
                    }) },
                { "_write_lines", w.method("Generator", "_write_lines", { py_positional("lines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lines = a[1];
                        py line;
                        if (!lines.truthy())
                        {
                            return none();
                        }
                        lines = (G<"email.generator.NLCRE">()).method("split", { lines });
                        for (py const& item : iterate{ lines[B<"slice">()(none(), I<-1>(), none())] })
                        {
                            line = item;
                            self.method("write", { line });
                            self.method("write", { self.attr("_NL") });
                        }
                        if (lines[I<-1>()].truthy())
                        {
                            self.method("write", { lines[I<-1>()] });
                        }
                        return none();
                    }) },
                { "_write", w.method("Generator", "_write", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py meth;
                        py munge_cte;
                        py oldfp;
                        py sfp;
                        oldfp = self.attr("_fp");
                        {
                            std::optional<py> pending3;
                            [[maybe_unused]] int flow3 = 0;
                            [[maybe_unused]] py result3;
                            try
                            {
                                self.set_attr("_munge_cte", none());
                                py const value4 = self.method("_new_buffer");
                                self.set_attr("_fp", value4);
                                sfp = value4;
                                self.method("_dispatch", { msg });
                            }
                            catch (raised const&)
                            {
                                pending3 = py_take_exception();
                            }
                            self.set_attr("_fp", oldfp);
                            munge_cte = self.attr("_munge_cte");
                            del_attr(self, "_munge_cte");
                            if (pending3)
                                py_reraise(*pending3);
                            if (flow3 == 1)
                                return result3;
                        }
                        // If we munged the cte, copy the message again and re-fix the CTE.
                        if (munge_cte.truthy())
                        {
                            msg = (Mod<"copy">().attr("deepcopy"))(msg);
                            // Preserve the header order if the CTE header already exists.
                            if (msg.method("get", { S<"content-transfer-encoding">() }).is_none())
                            {
                                msg.set_item(S<"Content-Transfer-Encoding">(), munge_cte[I<0>()]);
                            }
                            else
                            {
                                msg.method("replace_header", { S<"content-transfer-encoding">(), munge_cte[I<0>()] });
                            }
                            msg.method("replace_header", { S<"content-type">(), munge_cte[I<1>()] });
                        }
                        // Write the headers.  First we see if the message object wants to
                        // handle that itself.  If not, we'll do it generically.
                        meth = (B<"getattr">())(msg, S<"_write_headers">(), none());
                        if (meth.is_none())
                        {
                            self.method("_write_headers", { msg });
                        }
                        else
                        {
                            meth(self);
                        }
                        self.attr("_fp").method("write", { sfp.method("getvalue") });
                        return none();
                    }) },
                { "_dispatch", w.method("Generator", "_dispatch", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py generic;
                        py main;
                        py meth;
                        py specific;
                        py sub;
                        main = msg.method("get_content_maintype");
                        sub = msg.method("get_content_subtype");
                        specific = (G<"email.generator.UNDERSCORE">()).method("join", { py_tuple({ main, sub }) }).method("replace", { S<"-">(), S<"_">() });
                        meth = (B<"getattr">())(self, (S<"_handle_">()) + specific, none());
                        if (meth.is_none())
                        {
                            generic = main.method("replace", { S<"-">(), S<"_">() });
                            meth = (B<"getattr">())(self, (S<"_handle_">()) + generic, none());
                            if (meth.is_none())
                            {
                                meth = self.attr("_writeBody");
                            }
                        }
                        meth(msg);
                        return none();
                    }) },
                { "_write_headers", w.method("Generator", "_write_headers", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py folded;
                        py h;
                        py linesep;
                        py v;
                        for (py const& item : iterate{ msg.method("raw_items") })
                        {
                            auto const items6 = unpack(item, 2);
                            h = items6[0];
                            v = items6[1];
                            folded = self.attr("policy").method("fold", { h, v });
                            if (self.attr("policy").attr("verify_generated_headers").truthy())
                            {
                                linesep = self.attr("policy").attr("linesep");
                                if (!folded.method("endswith", { self.attr("policy").attr("linesep") }).truthy())
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderWriteError"))(S(std::string{ "folded header does not end with " } + linesep.repr() + ": " + folded.repr())));
                                }
                                if ((G<"email.generator.NEWLINE_WITHOUT_FWSP">()).method("search", { folded.method("removesuffix", { linesep }) }).truthy())
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderWriteError"))(S(std::string{ "folded header contains newline: " } + folded.repr())));
                                }
                            }
                            self.method("write", { folded });
                        }
                        // A blank line always separates headers from body
                        self.method("write", { self.attr("_NL") });
                        return none();
                    }) },
                { "_handle_text", w.method("Generator", "_handle_text", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py charset;
                        py payload;
                        payload = msg.method("get_payload");
                        if (payload.is_none())
                        {
                            return none();
                        }
                        if (!isinstance(payload, B<"str">()))
                        {
                            py_raise((B<"TypeError">())((S<"string payload expected: %s">()) % (B<"type">())(payload)));
                        }
                        if ((Mod<"email.utils">().attr("_has_surrogates"))(msg.attr("_payload")).truthy())
                        {
                            charset = msg.method("get_param", { S<"charset">() });
                            if (!charset.is_none())
                            {
                                // XXX: This copy stuff is an ugly hack to avoid modifying the
                                // existing message.
                                msg = (Mod<"copy">().attr("deepcopy"))(msg);
                                del_item(msg, S<"content-transfer-encoding">());
                                msg.method("set_payload", { msg.attr("_payload"), charset });
                                payload = msg.method("get_payload");
                                self.set_attr("_munge_cte", py_tuple({ msg[S<"content-transfer-encoding">()], msg[S<"content-type">()] }));
                            }
                        }
                        if (self.attr("_mangle_from_").truthy())
                        {
                            payload = (G<"email.generator.fcre">()).method("sub", { S<">From ">(), payload });
                        }
                        self.method("_write_lines", { payload });
                        return none();
                    }) },
                { "_writeBody", w.method("Generator", "_handle_text", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py charset;
                        py payload;
                        payload = msg.method("get_payload");
                        if (payload.is_none())
                        {
                            return none();
                        }
                        if (!isinstance(payload, B<"str">()))
                        {
                            py_raise((B<"TypeError">())((S<"string payload expected: %s">()) % (B<"type">())(payload)));
                        }
                        if ((Mod<"email.utils">().attr("_has_surrogates"))(msg.attr("_payload")).truthy())
                        {
                            charset = msg.method("get_param", { S<"charset">() });
                            if (!charset.is_none())
                            {
                                // XXX: This copy stuff is an ugly hack to avoid modifying the
                                // existing message.
                                msg = (Mod<"copy">().attr("deepcopy"))(msg);
                                del_item(msg, S<"content-transfer-encoding">());
                                msg.method("set_payload", { msg.attr("_payload"), charset });
                                payload = msg.method("get_payload");
                                self.set_attr("_munge_cte", py_tuple({ msg[S<"content-transfer-encoding">()], msg[S<"content-type">()] }));
                            }
                        }
                        if (self.attr("_mangle_from_").truthy())
                        {
                            payload = (G<"email.generator.fcre">()).method("sub", { S<">From ">(), payload });
                        }
                        self.method("_write_lines", { payload });
                        return none();
                    }) },
                { "_handle_multipart", w.method("Generator", "_handle_multipart", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py alltext;
                        py body_part;
                        py boundary;
                        py epilogue;
                        py g;
                        py msgtexts;
                        py part;
                        py preamble;
                        py s;
                        py subparts;
                        msgtexts = py_list();
                        subparts = msg.method("get_payload");
                        if (subparts.is_none())
                        {
                            subparts = py_list();
                        }
                        else if (isinstance(subparts, B<"str">()))
                        {
                            // e.g. a non-strict parse of a message with no starting boundary.
                            self.method("write", { subparts });
                            return none();
                        }
                        else if (!isinstance(subparts, B<"list">()))
                        {
                            // Scalar payload
                            subparts = py_list({ subparts });
                        }
                        for (py const& item : iterate{ subparts })
                        {
                            part = item;
                            s = self.method("_new_buffer");
                            g = self.method("clone", { s });
                            g.method("flatten", { part }, { { "unixfrom", boolean(false) }, { "linesep", self.attr("_NL") } });
                            msgtexts.method("append", { s.method("getvalue") });
                        }
                        // BAW: What about boundaries that are wrapped in double-quotes?
                        boundary = msg.method("get_boundary");
                        if (!boundary.truthy())
                        {
                            // Create a boundary that doesn't appear in any of the
                            // message texts.
                            alltext = self.attr("_encoded_NL").method("join", { msgtexts });
                            boundary = self.method("_make_boundary", { alltext });
                            msg.method("set_boundary", { boundary });
                        }
                        // If there's a preamble, write it out, with a trailing CRLF
                        if (!msg.attr("preamble").is_none())
                        {
                            if (self.attr("_mangle_from_").truthy())
                            {
                                preamble = (G<"email.generator.fcre">()).method("sub", { S<">From ">(), msg.attr("preamble") });
                            }
                            else
                            {
                                preamble = msg.attr("preamble");
                            }
                            self.method("_write_lines", { preamble });
                            self.method("write", { self.attr("_NL") });
                        }
                        // dash-boundary transport-padding CRLF
                        self.method("write", { ((S<"--">()) + boundary) + self.attr("_NL") });
                        // body-part
                        if (msgtexts.truthy())
                        {
                            self.attr("_fp").method("write", { msgtexts.method("pop", { I<0>() }) });
                        }
                        // *encapsulation
                        // --> delimiter transport-padding
                        // --> CRLF body-part
                        for (py const& item : iterate{ msgtexts })
                        {
                            body_part = item;
                            // delimiter transport-padding CRLF
                            self.method("write", { ((self.attr("_NL") + (S<"--">())) + boundary) + self.attr("_NL") });
                            // body-part
                            self.attr("_fp").method("write", { body_part });
                        }
                        // close-delimiter transport-padding
                        self.method("write", { (((self.attr("_NL") + (S<"--">())) + boundary) + (S<"--">())) + self.attr("_NL") });
                        if (!msg.attr("epilogue").is_none())
                        {
                            if (self.attr("_mangle_from_").truthy())
                            {
                                epilogue = (G<"email.generator.fcre">()).method("sub", { S<">From ">(), msg.attr("epilogue") });
                            }
                            else
                            {
                                epilogue = msg.attr("epilogue");
                            }
                            self.method("_write_lines", { epilogue });
                        }
                        return none();
                    }) },
                { "_handle_multipart_signed", w.method("Generator", "_handle_multipart_signed", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py p;
                        p = self.attr("policy");
                        self.set_attr("policy", p.method("clone", {  }, { { "max_line_length", I<0>() } }));
                        {
                            std::optional<py> pending9;
                            [[maybe_unused]] int flow9 = 0;
                            [[maybe_unused]] py result9;
                            try
                            {
                                self.method("_handle_multipart", { msg });
                            }
                            catch (raised const&)
                            {
                                pending9 = py_take_exception();
                            }
                            self.set_attr("policy", p);
                            if (pending9)
                                py_reraise(*pending9);
                            if (flow9 == 1)
                                return result9;
                        }
                        return none();
                    }) },
                { "_handle_message_delivery_status", w.method("Generator", "_handle_message_delivery_status", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py blocks;
                        py g;
                        py lines;
                        py part;
                        py s;
                        py text;
                        blocks = py_list();
                        for (py const& item : iterate{ msg.method("get_payload") })
                        {
                            part = item;
                            s = self.method("_new_buffer");
                            g = self.method("clone", { s });
                            g.method("flatten", { part }, { { "unixfrom", boolean(false) }, { "linesep", self.attr("_NL") } });
                            text = s.method("getvalue");
                            lines = text.method("split", { self.attr("_encoded_NL") });
                            // Strip off the unnecessary trailing empty line
                            if ((lines.truthy() && (lines[I<-1>()] == self.attr("_encoded_EMPTY"))))
                            {
                                blocks.method("append", { self.attr("_encoded_NL").method("join", { lines[B<"slice">()(none(), I<-1>(), none())] }) });
                            }
                            else
                            {
                                blocks.method("append", { text });
                            }
                        }
                        // Now join all the blocks with an empty line.  This has the lovely
                        // effect of separating each block with an empty line, but not adding
                        // an extra one after the last one.
                        self.attr("_fp").method("write", { self.attr("_encoded_NL").method("join", { blocks }) });
                        return none();
                    }) },
                { "_handle_message", w.method("Generator", "_handle_message", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py g;
                        py payload;
                        py s;
                        s = self.method("_new_buffer");
                        g = self.method("clone", { s });
                        // The payload of a message/rfc822 part should be a multipart sequence
                        // of length 1.  The zeroth element of the list should be the Message
                        // object for the subpart.  Extract that object, stringify it, and
                        // write it out.
                        // Except, it turns out, when it's a string instead, which happens when
                        // and only when HeaderParser is used on a message of mime type
                        // message/rfc822.  Such messages are generated by, for example,
                        // Groupwise when forwarding unadorned messages.  (Issue 7970.)  So
                        // in that case we just emit the string body.
                        payload = msg.attr("_payload");
                        if (isinstance(payload, B<"list">()))
                        {
                            g.method("flatten", { msg.method("get_payload", { I<0>() }) }, { { "unixfrom", boolean(false) }, { "linesep", self.attr("_NL") } });
                            payload = s.method("getvalue");
                        }
                        else
                        {
                            payload = self.method("_encode", { payload });
                        }
                        self.attr("_fp").method("write", { payload });
                        return none();
                    }) },
                { "_make_boundary", w.class_method("Generator", "_make_boundary", { py_positional("text", none()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py text = a[1];
                        py b;
                        py boundary;
                        py counter;
                        py cre;
                        py token;
                        token = (Mod<"random">().attr("randrange"))(Mod<"sys">().attr("maxsize"));
                        boundary = (((S<"=">()) * (I<15>())) + ((G<"email.generator._fmt">()) % token)) + (S<"==">());
                        if (text.is_none())
                        {
                            return boundary;
                        }
                        b = boundary;
                        counter = I<0>();
                        while (boolean(true).truthy())
                        {
                            cre = cls.method("_compile_re", { ((S<"^--">()) + (Mod<"re">().attr("escape"))(b)) + (S<"(--)?\r?$">()), Mod<"re">().attr("MULTILINE") });
                            if (!cre.method("search", { text }).truthy())
                            {
                                break;
                            }
                            b = (boundary + (S<".">())) + (B<"str">())(counter);
                            counter = inplace(NEOS_PY_ADD, counter, I<1>());
                        }
                        return b;
                    }) },
                { "_compile_re", w.class_method("Generator", "_compile_re", { py_positional("s"), py_positional("flags") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py s = a[1];
                        py flags = a[2];
                        return (Mod<"re">().attr("compile"))(s, flags);
                    }) }, }));

                // Default body handler
            define("email.generator", "BytesGenerator", w.make_class("BytesGenerator", { G<"email.generator.Generator">() }, {
                { "write", w.method("BytesGenerator", "write", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        self.attr("_fp").method("write", { s.method("encode", { S<"ascii">(), S<"surrogateescape">() }) });
                        return none();
                    }) },
                { "_new_buffer", w.method("BytesGenerator", "_new_buffer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"io">().attr("BytesIO"))();
                    }) },
                { "_encode", w.method("BytesGenerator", "_encode", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        return s.method("encode", { S<"ascii">() });
                    }) },
                { "_write_headers", w.method("BytesGenerator", "_write_headers", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py folded;
                        py h;
                        py linesep;
                        py v;
                        for (py const& item : iterate{ msg.method("raw_items") })
                        {
                            auto const items13 = unpack(item, 2);
                            h = items13[0];
                            v = items13[1];
                            folded = self.attr("policy").method("fold_binary", { h, v });
                            if (self.attr("policy").attr("verify_generated_headers").truthy())
                            {
                                linesep = self.attr("policy").attr("linesep").method("encode");
                                if (!folded.method("endswith", { linesep }).truthy())
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderWriteError"))(S(std::string{ "folded header does not end with " } + linesep.repr() + ": " + folded.repr())));
                                }
                                if ((G<"email.generator.NEWLINE_WITHOUT_FWSP_BYTES">()).method("search", { folded.method("removesuffix", { linesep }) }).truthy())
                                {
                                    py_raise((Mod<"email.errors">().attr("HeaderWriteError"))(S(std::string{ "folded header contains newline: " } + folded.repr())));
                                }
                            }
                            self.attr("_fp").method("write", { folded });
                        }
                        // A blank line always separates headers from body
                        self.method("write", { self.attr("_NL") });
                        return none();
                    }) },
                { "_handle_text", w.method("BytesGenerator", "_handle_text", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        if (msg.attr("_payload").is_none())
                        {
                            return none();
                        }
                        if (((Mod<"email.utils">().attr("_has_surrogates"))(msg.attr("_payload")).truthy() && (!(self.attr("policy").attr("cte_type") == (S<"7bit">())))))
                        {
                            if (self.attr("_mangle_from_").truthy())
                            {
                                msg.set_attr("_payload", (G<"email.generator.fcre">()).method("sub", { S<">From ">(), msg.attr("_payload") }));
                            }
                            self.method("_write_lines", { msg.attr("_payload") });
                        }
                        else
                        {
                            (B<"super">())(G<"email.generator.BytesGenerator">(), self).method("_handle_text", { msg });
                        }
                        return none();
                    }) },
                { "_writeBody", w.method("BytesGenerator", "_handle_text", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        if (msg.attr("_payload").is_none())
                        {
                            return none();
                        }
                        if (((Mod<"email.utils">().attr("_has_surrogates"))(msg.attr("_payload")).truthy() && (!(self.attr("policy").attr("cte_type") == (S<"7bit">())))))
                        {
                            if (self.attr("_mangle_from_").truthy())
                            {
                                msg.set_attr("_payload", (G<"email.generator.fcre">()).method("sub", { S<">From ">(), msg.attr("_payload") }));
                            }
                            self.method("_write_lines", { msg.attr("_payload") });
                        }
                        else
                        {
                            (B<"super">())(G<"email.generator.BytesGenerator">(), self).method("_handle_text", { msg });
                        }
                        return none();
                    }) },
                { "_compile_re", w.class_method("BytesGenerator", "_compile_re", { py_positional("s"), py_positional("flags") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py s = a[1];
                        py flags = a[2];
                        return (Mod<"re">().attr("compile"))(s.method("encode", { S<"ascii">() }), flags);
                    }) }, }));

            define("email.generator", "_FMT", S<"[Non-text (%(type)s) part of message omitted, filename %(filename)s]">());
            define("email.generator", "DecodedGenerator", w.make_class("DecodedGenerator", { G<"email.generator.Generator">() }, {
                { "__init__", w.method("DecodedGenerator", "__init__", { py_positional("outfp"), py_positional("mangle_from_", none()), py_positional("maxheaderlen", none()), py_positional("fmt", none()), py_keyword("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py outfp = a[1];
                        py mangle_from_ = a[2];
                        py maxheaderlen = a[3];
                        py fmt = a[4];
                        py policy = a[5];
                        (G<"email.generator.Generator">()).method("__init__", { self, outfp, mangle_from_, maxheaderlen }, { { "policy", policy } });
                        if (fmt.is_none())
                        {
                            self.set_attr("_fmt", G<"email.generator._FMT">());
                        }
                        else
                        {
                            self.set_attr("_fmt", fmt);
                        }
                        return none();
                    }) },
                { "_dispatch", w.method("DecodedGenerator", "_dispatch", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py maintype;
                        py part;
                        for (py const& item : iterate{ msg.method("walk") })
                        {
                            part = item;
                            maintype = part.method("get_content_maintype");
                            if (maintype == (S<"text">()))
                            {
                                (B<"print">()).call({ part.method("get_payload", {  }, { { "decode", boolean(false) } }) }, { { "file", self } });
                            }
                            else if (maintype == (S<"multipart">()))
                            {
                                // Just skip this
                            }
                            else
                            {
                                (B<"print">()).call({ self.attr("_fmt") % dict_of({ { S<"type">(), part.method("get_content_type") }, { S<"maintype">(), part.method("get_content_maintype") }, { S<"subtype">(), part.method("get_content_subtype") }, { S<"filename">(), part.method("get_filename", { S<"[no filename]">() }) }, { S<"description">(), part.method("get", { S<"Content-Description">(), S<"[no description]">() }) }, { S<"encoding">(), part.method("get", { S<"Content-Transfer-Encoding">(), S<"[no encoding]">() }) } }) }, { { "file", self } });
                            }
                        }
                        return none();
                    }) }, }));

            // Helper used by Generator._make_boundary
            define("email.generator", "_width", (B<"len">())((B<"repr">())((Mod<"sys">().attr("maxsize")) - (I<1>()))));
            define("email.generator", "_fmt", (S<"%%0%dd">()) % (G<"email.generator._width">()));
            // Backward compatibility
            define("email.generator", "_make_boundary", (G<"email.generator.Generator">()).attr("_make_boundary"));
        }

        bool email_generator_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_generator();
                }
                if (auto const doc = email_generator_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("random", py{ fn_import("random") }.release());
                aAttributes.emplace_back("deepcopy", import_attribute("copy", "deepcopy").release());
                aAttributes.emplace_back("StringIO", import_attribute("io", "StringIO").release());
                aAttributes.emplace_back("BytesIO", import_attribute("io", "BytesIO").release());
                aAttributes.emplace_back("_has_surrogates", import_attribute("email.utils", "_has_surrogates").release());
                aAttributes.emplace_back("HeaderWriteError", import_attribute("email.errors", "HeaderWriteError").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.generator", email_generator_module);
    }
}
