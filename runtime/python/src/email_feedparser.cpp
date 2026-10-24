/*
  email_feedparser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/feedparser.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.feedparser (CPython 3.13's Lib/email/feedparser.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_feedparser_docs.hpp"

namespace neos::python_runtime::detail::email_feedparser_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.feedparser.name"))
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
                py_raise("SystemError", std::string{ "email.feedparser: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.feedparser.name">()))
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
            if (modules_imported("email.feedparser"))
                return py{ fn_import("email.feedparser") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.feedparser" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.feedparser", email_feedparser_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.feedparser (Lib/email/feedparser.py)

        void make_email_feedparser()
        {
            static module_writer const& w = writer("email.feedparser");
            // Copyright (C) 2004-2006 Python Software Foundation
            // Authors: Baxter, Wouters and Warsaw
            // Contact: email-sig@python.org
            define("email.feedparser", "__all__", py_list({ S<"FeedParser">(), S<"BytesFeedParser">() }));
            define("email.feedparser", "NLCRE", (Mod<"re">()).method("compile", { S<"\\r\\n|\\r|\\n">() }));
            define("email.feedparser", "NLCRE_bol", (Mod<"re">()).method("compile", { S<"(\\r\\n|\\r|\\n)">() }));
            define("email.feedparser", "NLCRE_eol", (Mod<"re">()).method("compile", { S<"(\\r\\n|\\r|\\n)\\Z">() }));
            define("email.feedparser", "NLCRE_crack", (Mod<"re">()).method("compile", { S<"(\\r\\n|\\r|\\n)">() }));
            // RFC 5322 section 3.6.8 Optional fields.  ftext is %d33-57 / %d59-126, Any character
            // except controls, SP, and ":".
            define("email.feedparser", "headerRE", (Mod<"re">()).method("compile", { S<"^(From |[\\041-\\071\\073-\\176]*:|[\\t ])">() }));
            define("email.feedparser", "EMPTYSTRING", S<"">());
            define("email.feedparser", "NL", S<"\n">());
            define("email.feedparser", "boundaryendRE", (Mod<"re">()).method("compile", { S<"(?P<end>--)?(?P<ws>[ \\t]*)(?P<linesep>\\r\\n|\\r|\\n)?$">() }));
            define("email.feedparser", "NeedMoreData", (B<"object">())());
            define("email.feedparser", "BufferedSubFile", w.make_class("BufferedSubFile", { B<"object">() }, {
                { "__init__", w.method("BufferedSubFile", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_partial", (Mod<"io">().attr("StringIO")).call({  }, { { "newline", S<"">() } }));
                        // A deque of full, pushed lines
                        self.set_attr("_lines", (Mod<"collections">().attr("deque"))());
                        // The stack of false-EOF checking predicates.
                        self.set_attr("_eofstack", py_list());
                        // A flag indicating whether the file has been closed or not.
                        self.set_attr("_closed", boolean(false));
                        return none();
                    }) },
                { "push_eof_matcher", w.method("BufferedSubFile", "push_eof_matcher", { py_positional("pred") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pred = a[1];
                        self.attr("_eofstack").method("append", { pred });
                        return none();
                    }) },
                { "pop_eof_matcher", w.method("BufferedSubFile", "pop_eof_matcher", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_eofstack").method("pop");
                    }) },
                { "close", w.method("BufferedSubFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_partial").method("seek", { I<0>() });
                        self.method("pushlines", { self.attr("_partial").method("readlines") });
                        self.attr("_partial").method("seek", { I<0>() });
                        self.attr("_partial").method("truncate");
                        self.set_attr("_closed", boolean(true));
                        return none();
                    }) },
                { "readline", w.method("BufferedSubFile", "readline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ateof;
                        py line;
                        if (!self.attr("_lines").truthy())
                        {
                            if (self.attr("_closed").truthy())
                            {
                                return S<"">();
                            }
                            return G<"email.feedparser.NeedMoreData">();
                        }
                        // Pop the line off the stack and see if it matches the current
                        // false-EOF predicate.
                        line = self.attr("_lines").method("popleft");
                        // RFC 2046, section 5.1.2 requires us to recognize outer level
                        // boundaries at any level of inner nesting.  Do this, but be sure it's
                        // in the order of most to least nested.
                        for (py const& item : iterate{ (B<"reversed">())(self.attr("_eofstack")) })
                        {
                            ateof = item;
                            if (ateof(line).truthy())
                            {
                                // We're at the false EOF.  But push the last line back first.
                                self.attr("_lines").method("appendleft", { line });
                                return S<"">();
                            }
                        }
                        return line;
                    }) },
                { "unreadline", w.method("BufferedSubFile", "unreadline", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if (line.is((G<"email.feedparser.NeedMoreData">())))
                            py_raise(B<"AssertionError">()());
                        self.attr("_lines").method("appendleft", { line });
                        return none();
                    }) },
                { "push", w.method("BufferedSubFile", "push", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py parts;
                        self.attr("_partial").method("write", { data });
                        if (((!data.contains((S<"\n">()))) && (!data.contains((S<"\r">())))))
                        {
                            // No new complete lines, wait for more.
                            return none();
                        }
                        // Crack into lines, preserving the linesep characters.
                        self.attr("_partial").method("seek", { I<0>() });
                        parts = self.attr("_partial").method("readlines");
                        self.attr("_partial").method("seek", { I<0>() });
                        self.attr("_partial").method("truncate");
                        // If the last element of the list does not end in a newline, then treat
                        // it as a partial line.  We only check for '\n' here because a line
                        // ending with '\r' might be a line that was split in the middle of a
                        // '\r\n' sequence (see bugs 1555570 and 1721862).
                        if (!parts[I<-1>()].method("endswith", { S<"\n">() }).truthy())
                        {
                            self.attr("_partial").method("write", { parts.method("pop") });
                        }
                        self.method("pushlines", { parts });
                        return none();
                    }) },
                { "pushlines", w.method("BufferedSubFile", "pushlines", { py_positional("lines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lines = a[1];
                        self.attr("_lines").method("extend", { lines });
                        return none();
                    }) },
                { "__iter__", w.method("BufferedSubFile", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__next__", w.method("BufferedSubFile", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        line = self.method("readline");
                        if (line == (S<"">()))
                        {
                            py_raise(B<"StopIteration">());
                        }
                        return line;
                    }) }, }));

                // Non-public interface for supporting Parser's headersonly flag
            define("email.feedparser", "FeedParser", w.make_class("FeedParser", {  }, {
                { "__init__", w.method("FeedParser", "__init__", { py_positional("_factory", none()), py_keyword("policy", Mod<"email._policybase">().attr("compat32")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _factory = a[1];
                        py policy = a[2];
                        py Message;
                        self.set_attr("policy", policy);
                        self.set_attr("_old_style_factory", boolean(false));
                        if (_factory.is_none())
                        {
                            if (policy.attr("message_factory").is_none())
                            {
                                Message = M("email.message", "Message");
                                self.set_attr("_factory", Message);
                            }
                            else
                            {
                                self.set_attr("_factory", policy.attr("message_factory"));
                            }
                        }
                        else
                        {
                            self.set_attr("_factory", _factory);
                            try
                            {
                                _factory.call({  }, { { "policy", self.attr("policy") } });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, B<"TypeError">()))
                                {
                                    // Assume this is an old-style factory
                                    self.set_attr("_old_style_factory", boolean(true));
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                        }
                        self.set_attr("_input", (G<"email.feedparser.BufferedSubFile">())());
                        self.set_attr("_msgstack", py_list());
                        self.set_attr("_parse", self.method("_parsegen").attr("__next__"));
                        self.set_attr("_cur", none());
                        self.set_attr("_last", none());
                        self.set_attr("_headersonly", boolean(false));
                        return none();
                    }) },
                { "_set_headersonly", w.method("FeedParser", "_set_headersonly", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_headersonly", boolean(true));
                        return none();
                    }) },
                { "feed", w.method("FeedParser", "feed", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("_input").method("push", { data });
                        self.method("_call_parse");
                        return none();
                    }) },
                { "_call_parse", w.method("FeedParser", "_call_parse", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            self.method("_parse");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            if (matches(handler3.exception, B<"StopIteration">()))
                            {
                            }
                            else
                                py_reraise(handler3.exception);
                        }
                        return none();
                    }) },
                { "close", w.method("FeedParser", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py defect;
                        py root;
                        self.attr("_input").method("close");
                        self.method("_call_parse");
                        root = self.method("_pop_message");
                        if (self.attr("_msgstack").truthy())
                            py_raise(B<"AssertionError">()());
                        // Look for final set of defects
                        if (((root.method("get_content_maintype") == (S<"multipart">())) && (!root.method("is_multipart").truthy()) && (!self.attr("_headersonly").truthy())))
                        {
                            defect = (Mod<"email.errors">().attr("MultipartInvariantViolationDefect"))();
                            self.attr("policy").method("handle_defect", { root, defect });
                        }
                        return root;
                    }) },
                { "_new_message", w.method("FeedParser", "_new_message", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        if (self.attr("_old_style_factory").truthy())
                        {
                            msg = self.method("_factory");
                        }
                        else
                        {
                            msg = self.method("_factory", {  }, { { "policy", self.attr("policy") } });
                        }
                        if ((self.attr("_cur").truthy() && (self.attr("_cur").method("get_content_type") == (S<"multipart/digest">()))))
                        {
                            msg.method("set_default_type", { S<"message/rfc822">() });
                        }
                        if (self.attr("_msgstack").truthy())
                        {
                            self.attr("_msgstack")[I<-1>()].method("attach", { msg });
                        }
                        self.attr("_msgstack").method("append", { msg });
                        self.set_attr("_cur", msg);
                        self.set_attr("_last", msg);
                        return none();
                    }) },
                { "_pop_message", w.method("FeedParser", "_pop_message", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py retval;
                        retval = self.attr("_msgstack").method("pop");
                        if (self.attr("_msgstack").truthy())
                        {
                            self.set_attr("_cur", self.attr("_msgstack")[I<-1>()]);
                        }
                        else
                        {
                            self.set_attr("_cur", none());
                        }
                        return retval;
                    }) },
                { "_parsegen", w.method("FeedParser", "_parsegen", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("_parsegen", "FeedParser._parsegen", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py bolmo;
                            py boundary;
                            py boundarymatch;
                            py capturing_preamble;
                            py close_boundary_seen;
                            py defect;
                            py end;
                            py eolmo;
                            py epilogue;
                            py firstline;
                            py headers;
                            py lastline;
                            py line;
                            py lines;
                            py linesep;
                            py mo;
                            py payload;
                            py preamble;
                            py retval;
                            auto const separator = std::make_shared<py>();
                            self.method("_new_message");
                            headers = py_list();
                            // Collect the headers, searching for a line that doesn't match the RFC
                            // 2822 header or continuation pattern (including an empty line).
                            for (py const& item : iterate{ self.attr("_input") })
                            {
                                line = item;
                                if (line.is((G<"email.feedparser.NeedMoreData">())))
                                {
                                    py_yield(G<"email.feedparser.NeedMoreData">());
                                    continue;
                                }
                                if (!(G<"email.feedparser.headerRE">()).method("match", { line }).truthy())
                                {
                                    // If we saw the RFC defined header/body separator
                                    // (i.e. newline), just throw it away. Otherwise the line is
                                    // part of the body so push it back.
                                    if (!(G<"email.feedparser.NLCRE">()).method("match", { line }).truthy())
                                    {
                                        defect = (Mod<"email.errors">().attr("MissingHeaderBodySeparatorDefect"))();
                                        self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                        self.attr("_input").method("unreadline", { line });
                                    }
                                    break;
                                }
                                headers.method("append", { line });
                            }
                            // Done with the headers, so parse them and figure out what we're
                            // supposed to see in the body of the message.
                            self.method("_parse_headers", { headers });
                            // Headers-only parsing is a backwards compatibility hack, which was
                            // necessary in the older parser, which could raise errors.  All
                            // remaining lines in the input are thrown into the message body.
                            if (self.attr("_headersonly").truthy())
                            {
                                lines = py_list();
                                while (boolean(true).truthy())
                                {
                                    line = self.attr("_input").method("readline");
                                    if (line.is((G<"email.feedparser.NeedMoreData">())))
                                    {
                                        py_yield(G<"email.feedparser.NeedMoreData">());
                                        continue;
                                    }
                                    if (line == (S<"">()))
                                    {
                                        break;
                                    }
                                    lines.method("append", { line });
                                }
                                self.attr("_cur").method("set_payload", { (G<"email.feedparser.EMPTYSTRING">()).method("join", { lines }) });
                                return none();
                            }
                            if (self.attr("_cur").method("get_content_type") == (S<"message/delivery-status">()))
                            {
                                // message/delivery-status contains blocks of headers separated by
                                // a blank line.  We'll represent each header block as a separate
                                // nested message object, but the processing is a bit different
                                // than standard message/* types because there is no body for the
                                // nested messages.  A blank line separates the subparts.
                                while (boolean(true).truthy())
                                {
                                    self.attr("_input").method("push_eof_matcher", { (G<"email.feedparser.NLCRE">()).attr("match") });
                                    for (py const& item : iterate{ self.method("_parsegen") })
                                    {
                                        retval = item;
                                        if (retval.is((G<"email.feedparser.NeedMoreData">())))
                                        {
                                            py_yield(G<"email.feedparser.NeedMoreData">());
                                            continue;
                                        }
                                        break;
                                    }
                                    self.method("_pop_message");
                                    // We need to pop the EOF matcher in order to tell if we're at
                                    // the end of the current file, not the end of the last block
                                    // of message headers.
                                    self.attr("_input").method("pop_eof_matcher");
                                    // The input stream must be sitting at the newline or at the
                                    // EOF.  We want to see if we're at the end of this subpart, so
                                    // first consume the blank line, then test the next line to see
                                    // if we're at this subpart's EOF.
                                    while (boolean(true).truthy())
                                    {
                                        line = self.attr("_input").method("readline");
                                        if (line.is((G<"email.feedparser.NeedMoreData">())))
                                        {
                                            py_yield(G<"email.feedparser.NeedMoreData">());
                                            continue;
                                        }
                                        break;
                                    }
                                    while (boolean(true).truthy())
                                    {
                                        line = self.attr("_input").method("readline");
                                        if (line.is((G<"email.feedparser.NeedMoreData">())))
                                        {
                                            py_yield(G<"email.feedparser.NeedMoreData">());
                                            continue;
                                        }
                                        break;
                                    }
                                    if (line == (S<"">()))
                                    {
                                        break;
                                    }
                                    // Not at EOF so this is a line we're going to need.
                                    self.attr("_input").method("unreadline", { line });
                                }
                                return none();
                            }
                            if (self.attr("_cur").method("get_content_maintype") == (S<"message">()))
                            {
                                // The message claims to be a message/* type, then what follows is
                                // another RFC 5322 message.
                                for (py const& item : iterate{ self.method("_parsegen") })
                                {
                                    retval = item;
                                    if (retval.is((G<"email.feedparser.NeedMoreData">())))
                                    {
                                        py_yield(G<"email.feedparser.NeedMoreData">());
                                        continue;
                                    }
                                    break;
                                }
                                self.method("_pop_message");
                                return none();
                            }
                            if (self.attr("_cur").method("get_content_maintype") == (S<"multipart">()))
                            {
                                boundary = self.attr("_cur").method("get_boundary");
                                if (boundary.is_none())
                                {
                                    // The message /claims/ to be a multipart but it has not
                                    // defined a boundary.  That's a problem which we'll handle by
                                    // reading everything until the EOF and marking the message as
                                    // defective.
                                    defect = (Mod<"email.errors">().attr("NoBoundaryInMultipartDefect"))();
                                    self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                    lines = py_list();
                                    for (py const& item : iterate{ self.attr("_input") })
                                    {
                                        line = item;
                                        if (line.is((G<"email.feedparser.NeedMoreData">())))
                                        {
                                            py_yield(G<"email.feedparser.NeedMoreData">());
                                            continue;
                                        }
                                        lines.method("append", { line });
                                    }
                                    self.attr("_cur").method("set_payload", { (G<"email.feedparser.EMPTYSTRING">()).method("join", { lines }) });
                                    return none();
                                }
                                // Make sure a valid content type was specified per RFC 2045:6.4.
                                if (!py_tuple({ S<"7bit">(), S<"8bit">(), S<"binary">() }).contains((B<"str">())(self.attr("_cur").method("get", { S<"content-transfer-encoding">(), S<"8bit">() })).method("lower")))
                                {
                                    defect = (Mod<"email.errors">().attr("InvalidMultipartContentTransferEncodingDefect"))();
                                    self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                }
                                // Create a line match predicate which matches the inter-part
                                // boundary as well as the end-of-multipart boundary.  Don't push
                                // this onto the input stream until we've scanned past the
                                // preamble.
                                *separator = (S<"--">()) + boundary;
                                boundarymatch = w.function("boundarymatch", "FeedParser._parsegen.<locals>.boundarymatch", { py_positional("line") }, [separator](arguments a) -> py
        {
            py line = a[0];
            if (!line.method("startswith", { (*separator) }).truthy())
            {
                return none();
            }
            return (G<"email.feedparser.boundaryendRE">()).method("match", { line, py{ (*separator).size() } });
        });
                                capturing_preamble = boolean(true);
                                preamble = py_list();
                                linesep = boolean(false);
                                close_boundary_seen = boolean(false);
                                while (boolean(true).truthy())
                                {
                                    line = self.attr("_input").method("readline");
                                    if (line.is((G<"email.feedparser.NeedMoreData">())))
                                    {
                                        py_yield(G<"email.feedparser.NeedMoreData">());
                                        continue;
                                    }
                                    if (line == (S<"">()))
                                    {
                                        break;
                                    }
                                    mo = boundarymatch(line);
                                    if (mo.truthy())
                                    {
                                        // If we're looking at the end boundary, we're done with
                                        // this multipart.  If there was a newline at the end of
                                        // the closing boundary, then we need to initialize the
                                        // epilogue with the empty string (see below).
                                        if (mo.method("group", { S<"end">() }).truthy())
                                        {
                                            close_boundary_seen = boolean(true);
                                            linesep = mo.method("group", { S<"linesep">() });
                                            break;
                                        }
                                        // We saw an inter-part boundary.  Were we in the preamble?
                                        if (capturing_preamble.truthy())
                                        {
                                            if (preamble.truthy())
                                            {
                                                // According to RFC 2046, the last newline belongs
                                                // to the boundary.
                                                lastline = preamble[I<-1>()];
                                                eolmo = (G<"email.feedparser.NLCRE_eol">()).method("search", { lastline });
                                                if (eolmo.truthy())
                                                {
                                                    preamble.set_item(I<-1>(), lastline[B<"slice">()(none(), -py{ eolmo.method("group", { I<0>() }).size() }, none())]);
                                                }
                                                self.attr("_cur").set_attr("preamble", (G<"email.feedparser.EMPTYSTRING">()).method("join", { preamble }));
                                            }
                                            capturing_preamble = boolean(false);
                                            self.attr("_input").method("unreadline", { line });
                                            continue;
                                        }
                                        // We saw a boundary separating two parts.  Consume any
                                        // multiple boundary lines that may be following.  Our
                                        // interpretation of RFC 2046 BNF grammar does not produce
                                        // body parts within such double boundaries.
                                        while (boolean(true).truthy())
                                        {
                                            line = self.attr("_input").method("readline");
                                            if (line.is((G<"email.feedparser.NeedMoreData">())))
                                            {
                                                py_yield(G<"email.feedparser.NeedMoreData">());
                                                continue;
                                            }
                                            mo = boundarymatch(line);
                                            if (!mo.truthy())
                                            {
                                                self.attr("_input").method("unreadline", { line });
                                                break;
                                            }
                                        }
                                        // Recurse to parse this subpart; the input stream points
                                        // at the subpart's first line.
                                        self.attr("_input").method("push_eof_matcher", { boundarymatch });
                                        for (py const& item : iterate{ self.method("_parsegen") })
                                        {
                                            retval = item;
                                            if (retval.is((G<"email.feedparser.NeedMoreData">())))
                                            {
                                                py_yield(G<"email.feedparser.NeedMoreData">());
                                                continue;
                                            }
                                            break;
                                        }
                                        // Because of RFC 2046, the newline preceding the boundary
                                        // separator actually belongs to the boundary, not the
                                        // previous subpart's payload (or epilogue if the previous
                                        // part is a multipart).
                                        if (self.attr("_last").method("get_content_maintype") == (S<"multipart">()))
                                        {
                                            epilogue = self.attr("_last").attr("epilogue");
                                            if (epilogue == (S<"">()))
                                            {
                                                self.attr("_last").set_attr("epilogue", none());
                                            }
                                            else if (!epilogue.is_none())
                                            {
                                                mo = (G<"email.feedparser.NLCRE_eol">()).method("search", { epilogue });
                                                if (mo.truthy())
                                                {
                                                    end = py{ mo.method("group", { I<0>() }).size() };
                                                    self.attr("_last").set_attr("epilogue", epilogue[B<"slice">()(none(), -end, none())]);
                                                }
                                            }
                                        }
                                        else
                                        {
                                            payload = self.attr("_last").attr("_payload");
                                            if (isinstance(payload, B<"str">()))
                                            {
                                                mo = (G<"email.feedparser.NLCRE_eol">()).method("search", { payload });
                                                if (mo.truthy())
                                                {
                                                    payload = payload[B<"slice">()(none(), -py{ mo.method("group", { I<0>() }).size() }, none())];
                                                    self.attr("_last").set_attr("_payload", payload);
                                                }
                                            }
                                        }
                                        self.attr("_input").method("pop_eof_matcher");
                                        self.method("_pop_message");
                                        // Set the multipart up for newline cleansing, which will
                                        // happen if we're in a nested multipart.
                                        self.set_attr("_last", self.attr("_cur"));
                                    }
                                    else
                                    {
                                        // I think we must be in the preamble
                                        if (!capturing_preamble.truthy())
                                            py_raise(B<"AssertionError">()());
                                        preamble.method("append", { line });
                                    }
                                }
                                // We've seen either the EOF or the end boundary.  If we're still
                                // capturing the preamble, we never saw the start boundary.  Note
                                // that as a defect and store the captured text as the payload.
                                if (capturing_preamble.truthy())
                                {
                                    defect = (Mod<"email.errors">().attr("StartBoundaryNotFoundDefect"))();
                                    self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                    self.attr("_cur").method("set_payload", { (G<"email.feedparser.EMPTYSTRING">()).method("join", { preamble }) });
                                    epilogue = py_list();
                                    for (py const& item : iterate{ self.attr("_input") })
                                    {
                                        line = item;
                                        if (line.is((G<"email.feedparser.NeedMoreData">())))
                                        {
                                            py_yield(G<"email.feedparser.NeedMoreData">());
                                            continue;
                                        }
                                    }
                                    self.attr("_cur").set_attr("epilogue", (G<"email.feedparser.EMPTYSTRING">()).method("join", { epilogue }));
                                    return none();
                                }
                                // If we're not processing the preamble, then we might have seen
                                // EOF without seeing that end boundary...that is also a defect.
                                if (!close_boundary_seen.truthy())
                                {
                                    defect = (Mod<"email.errors">().attr("CloseBoundaryNotFoundDefect"))();
                                    self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                    return none();
                                }
                                // Everything from here to the EOF is epilogue.  If the end boundary
                                // ended in a newline, we'll need to make sure the epilogue isn't
                                // None
                                if (linesep.truthy())
                                {
                                    epilogue = py_list({ S<"">() });
                                }
                                else
                                {
                                    epilogue = py_list();
                                }
                                for (py const& item : iterate{ self.attr("_input") })
                                {
                                    line = item;
                                    if (line.is((G<"email.feedparser.NeedMoreData">())))
                                    {
                                        py_yield(G<"email.feedparser.NeedMoreData">());
                                        continue;
                                    }
                                    epilogue.method("append", { line });
                                }
                                // Any CRLF at the front of the epilogue is not technically part of
                                // the epilogue.  Also, watch out for an empty string epilogue,
                                // which means a single newline.
                                if (epilogue.truthy())
                                {
                                    firstline = epilogue[I<0>()];
                                    bolmo = (G<"email.feedparser.NLCRE_bol">()).method("match", { firstline });
                                    if (bolmo.truthy())
                                    {
                                        epilogue.set_item(I<0>(), firstline[B<"slice">()(py{ bolmo.method("group", { I<0>() }).size() }, none(), none())]);
                                    }
                                }
                                self.attr("_cur").set_attr("epilogue", (G<"email.feedparser.EMPTYSTRING">()).method("join", { epilogue }));
                                return none();
                            }
                            // Otherwise, it's some non-multipart type, so the entire rest of the
                            // file contents becomes the payload.
                            lines = py_list();
                            for (py const& item : iterate{ self.attr("_input") })
                            {
                                line = item;
                                if (line.is((G<"email.feedparser.NeedMoreData">())))
                                {
                                    py_yield(G<"email.feedparser.NeedMoreData">());
                                    continue;
                                }
                                lines.method("append", { line });
                            }
                            self.attr("_cur").method("set_payload", { (G<"email.feedparser.EMPTYSTRING">()).method("join", { lines }) });
                            return none();
                        });
                    }) },
                { "_parse_headers", w.method("FeedParser", "_parse_headers", { py_positional("lines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lines = a[1];
                        py defect;
                        py i;
                        py lastheader;
                        py lastvalue;
                        py line;
                        py lineno;
                        py mo;
                        lastheader = S<"">();
                        lastvalue = py_list();
                        for (py const& item : iterate{ (B<"enumerate">())(lines) })
                        {
                            auto const items19 = unpack(item, 2);
                            lineno = items19[0];
                            line = items19[1];
                            // Check for continuation
                            if ((S<" \t">()).contains(line[I<0>()]))
                            {
                                if (!lastheader.truthy())
                                {
                                    // The first line of the headers was a continuation.  This
                                    // is illegal, so let's note the defect, store the illegal
                                    // line, and ignore it for purposes of headers.
                                    defect = (Mod<"email.errors">().attr("FirstHeaderLineIsContinuationDefect"))(line);
                                    self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                    continue;
                                }
                                lastvalue.method("append", { line });
                                continue;
                            }
                            if (lastheader.truthy())
                            {
                                call_ex(self.attr("_cur").attr("set_raw"), {  }, self.attr("policy").method("header_source_parse", { lastvalue }), py{});
                                auto const items20 = unpack(py_tuple({ S<"">(), py_list() }), 2);
                                lastheader = items20[0];
                                lastvalue = items20[1];
                            }
                            // Check for envelope header, i.e. unix-from
                            if (line.method("startswith", { S<"From ">() }).truthy())
                            {
                                if (lineno == (I<0>()))
                                {
                                    // Strip off the trailing newline
                                    mo = (G<"email.feedparser.NLCRE_eol">()).method("search", { line });
                                    if (mo.truthy())
                                    {
                                        line = line[B<"slice">()(none(), -py{ mo.method("group", { I<0>() }).size() }, none())];
                                    }
                                    self.attr("_cur").method("set_unixfrom", { line });
                                    continue;
                                }
                                else if (lineno == (py{ lines.size() } - (I<1>())))
                                {
                                    // Something looking like a unix-from at the end - it's
                                    // probably the first line of the body, so push back the
                                    // line and stop.
                                    self.attr("_input").method("unreadline", { line });
                                    return none();
                                }
                                else
                                {
                                    // Weirdly placed unix-from line.
                                    defect = (Mod<"email.errors">().attr("MisplacedEnvelopeHeaderDefect"))(line);
                                    self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                    continue;
                                }
                            }
                            // Split the line on the colon separating field name from value.
                            // There will always be a colon, because if there wasn't the part of
                            // the parser that calls us would have started parsing the body.
                            i = line.method("find", { S<":">() });
                            // If the colon is on the start of the line the header is clearly
                            // malformed, but we might be able to salvage the rest of the
                            // message. Track the error but keep going.
                            if (i == (I<0>()))
                            {
                                defect = (Mod<"email.errors">().attr("InvalidHeaderDefect"))(S<"Missing header name.">());
                                self.attr("policy").method("handle_defect", { self.attr("_cur"), defect });
                                continue;
                            }
                            if (!(i > (I<0>())))
                                py_raise(B<"AssertionError">()(S<"_parse_headers fed line with no : and no leading WS">()));
                            lastheader = line[B<"slice">()(none(), i, none())];
                            lastvalue = py_list({ line });
                        }
                        // Done with all the lines, so handle the last header.
                        if (lastheader.truthy())
                        {
                            call_ex(self.attr("_cur").attr("set_raw"), {  }, self.attr("policy").method("header_source_parse", { lastvalue }), py{});
                        }
                        return none();
                    }) }, }));

            define("email.feedparser", "BytesFeedParser", w.make_class("BytesFeedParser", { G<"email.feedparser.FeedParser">() }, {
                { "feed", w.method("BytesFeedParser", "feed", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        (B<"super">()(G<"email.feedparser.BytesFeedParser">(), self)).method("feed", { data.method("decode", { S<"ascii">(), S<"surrogateescape">() }) });
                        return none();
                    }) }, }));
        }

        bool email_feedparser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_feedparser();
                }
                if (auto const doc = email_feedparser_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("errors", import_attribute("email", "errors").release());
                aAttributes.emplace_back("compat32", import_attribute("email._policybase", "compat32").release());
                aAttributes.emplace_back("deque", import_attribute("collections", "deque").release());
                aAttributes.emplace_back("StringIO", import_attribute("io", "StringIO").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.feedparser", email_feedparser_module);
    }
}
