/*
  html_parser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/html/parser.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module html.parser (CPython 3.13's Lib/html/parser.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "html_parser_docs.hpp"

namespace neos::python_runtime::detail::html_parser_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "html.parser.name"))
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
                py_raise("SystemError", std::string{ "html.parser: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"html.parser.name">()))
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
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "html.parser", html_parser_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- html.parser (Lib/html/parser.py)

        void make_html_parser()
        {
            static module_writer const& w = writer("html.parser");
            // This file is based on sgmllib.py, but the API is slightly different.
            // XXX There should be a way to distinguish between PCDATA (parsed
            // character data -- the normal case), RCDATA (replaceable character
            // data -- only char and entity references and end tags are special)
            // and CDATA (character data -- only end tags are special).
            define("html.parser", "__all__", py_list({ S<"HTMLParser">() }));
            // Regular expressions used for parsing
            define("html.parser", "interesting_normal", (Mod<"re">()).method("compile", { S<"[&<]">() }));
            define("html.parser", "incomplete", (Mod<"re">()).method("compile", { S<"&[a-zA-Z#]">() }));
            define("html.parser", "entityref", (Mod<"re">()).method("compile", { S<"&([a-zA-Z][-.a-zA-Z0-9]*)[^a-zA-Z0-9]">() }));
            define("html.parser", "charref", (Mod<"re">()).method("compile", { S<"&#(?:[0-9]+|[xX][0-9a-fA-F]+)[^0-9a-fA-F]">() }));
            define("html.parser", "incomplete_charref", (Mod<"re">()).method("compile", { S<"&#(?:[0-9]|[xX][0-9a-fA-F])">() }));
            define("html.parser", "attr_charref", (Mod<"re">()).method("compile", { S<"&(#[0-9]+|#[xX][0-9a-fA-F]+|[a-zA-Z][a-zA-Z0-9]*)[;=]?">() }));
            define("html.parser", "starttagopen", (Mod<"re">()).method("compile", { S<"<[a-zA-Z]">() }));
            define("html.parser", "endtagopen", (Mod<"re">()).method("compile", { S<"</[a-zA-Z]">() }));
            define("html.parser", "piclose", (Mod<"re">()).method("compile", { S<">">() }));
            define("html.parser", "commentclose", (Mod<"re">()).method("compile", { S<"--!?>">() }));
            define("html.parser", "commentabruptclose", (Mod<"re">()).method("compile", { S<"-?>">() }));
            // Note:
            // 1) if you change tagfind/attrfind remember to update locatetagend too;
            // 2) if you change tagfind/attrfind and/or locatetagend the parser will
            // explode, so don't do it.
            // see the HTML5 specs section "13.2.5.6 Tag open state",
            // "13.2.5.8 Tag name state" and "13.2.5.33 Attribute name state".
            // https://html.spec.whatwg.org/multipage/parsing.html#tag-open-state
            // https://html.spec.whatwg.org/multipage/parsing.html#tag-name-state
            // https://html.spec.whatwg.org/multipage/parsing.html#attribute-name-state
            define("html.parser", "tagfind_tolerant", (Mod<"re">()).method("compile", { S<"([a-zA-Z][^\\t\\n\\r\\f />]*)(?:[\\t\\n\\r\\f ]|/(?!>))*">() }));
            define("html.parser", "attrfind_tolerant", (Mod<"re">()).method("compile", { S<"\n  (\n    (?<=['\"\\t\\n\\r\\f /])[^\\t\\n\\r\\f />][^\\t\\n\\r\\f /=>]*  # attribute name\n   )\n  ([\\t\\n\\r\\f ]*=[\\t\\n\\r\\f ]*        # value indicator\n    ('[^']*'                        # LITA-enclosed value\n    |\"[^\"]*\"                        # LIT-enclosed value\n    |(?!['\"])[^>\\t\\n\\r\\f ]*         # bare value\n    )\n   )?\n  (?:[\\t\\n\\r\\f ]|/(?!>))*           # possibly followed by a space\n">(), Mod<"re">().attr("VERBOSE") }));
            define("html.parser", "locatetagend", (Mod<"re">()).method("compile", { S<"\n  [a-zA-Z][^\\t\\n\\r\\f />]*           # tag name\n  [\\t\\n\\r\\f /]*                     # optional whitespace before attribute name\n  (?:(?<=['\"\\t\\n\\r\\f /])[^\\t\\n\\r\\f />][^\\t\\n\\r\\f /=>]*  # attribute name\n    (?:[\\t\\n\\r\\f ]*=[\\t\\n\\r\\f ]*    # value indicator\n      (?:'[^']*'                    # LITA-enclosed value\n        |\"[^\"]*\"                    # LIT-enclosed value\n        |(?!['\"])[^>\\t\\n\\r\\f ]*     # bare value\n       )\n     )?\n    [\\t\\n\\r\\f /]*                   # possibly followed by a space\n   )*\n   >?\n">(), Mod<"re">().attr("VERBOSE") }));
            // The following variables are not used, but are temporarily left for
            // backward compatibility.
            define("html.parser", "locatestarttagend_tolerant", (Mod<"re">()).method("compile", { S<"\n  <[a-zA-Z][^\\t\\n\\r\\f />\\x00]*       # tag name\n  (?:[\\s/]*                          # optional whitespace before attribute name\n    (?:(?<=['\"\\s/])[^\\s/>][^\\s/=>]*  # attribute name\n      (?:\\s*=+\\s*                    # value indicator\n        (?:'[^']*'                   # LITA-enclosed value\n          |\"[^\"]*\"                   # LIT-enclosed value\n          |(?!['\"])[^>\\s]*           # bare value\n         )\n        \\s*                          # possibly followed by a space\n       )?(?:\\s|/(?!>))*\n     )*\n   )?\n  \\s*                                # trailing whitespace\n">(), Mod<"re">().attr("VERBOSE") }));
            define("html.parser", "endendtag", (Mod<"re">()).method("compile", { S<">">() }));
            define("html.parser", "endtagfind", (Mod<"re">()).method("compile", { S<"</\\s*([a-zA-Z][-.a-zA-Z0-9:_]*)\\s*>">() }));
            // Character reference processing logic specific to attribute values
            // See: https://html.spec.whatwg.org/multipage/parsing.html#named-character-reference-state
            define("html.parser", "_replace_attr_charref", w.function("_replace_attr_charref", "_replace_attr_charref", { py_positional("match") }, [](arguments a) -> py
                {
                    py match = a[0];
                    py ref_;
                    ref_ = match.method("group", { I<0>() });
                    // Numeric / hex char refs must always be unescaped
                    if (ref_.method("startswith", { S<"&#">() }).truthy())
                    {
                        return (Mod<"html">().attr("unescape"))(ref_);
                    }
                    // Named character / entity references must only be unescaped
                    // if they are an exact match, and they are not followed by an equals sign
                    if (((!ref_.method("endswith", { S<"=">() }).truthy()) && (Mod<"html.entities">().attr("html5")).contains(ref_[B<"slice">()(I<1>(), none(), none())])))
                    {
                        return (Mod<"html">().attr("unescape"))(ref_);
                    }
                    // Otherwise do not unescape
                    return ref_;
                }));

            define("html.parser", "_unescape_attrvalue", w.function("_unescape_attrvalue", "_unescape_attrvalue", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    return (G<"html.parser.attr_charref">()).method("sub", { G<"html.parser._replace_attr_charref">(), s });
                }));

                // See the HTML5 specs section "13.4 Parsing HTML fragments".
                // https://html.spec.whatwg.org/multipage/parsing.html#parsing-html-fragments
                // CDATA_CONTENT_ELEMENTS are parsed in RAWTEXT mode
                // Internal -- handle data as far as reasonable.  May leave state
                // and data to be processed by a subsequent call.  If 'end' is
                // true, force handling all data as if followed by EOF marker.
                // Internal -- parse html declarations, return length or -1 if not terminated
                // See w3.org/TR/html5/tokenization.html#markup-declaration-open-state
                // See also parse_declaration in _markupbase
                // Internal -- parse comment, return length or -1 if not terminated
                // see https://html.spec.whatwg.org/multipage/parsing.html#comment-start-state
                // Internal -- parse bogus comment, return length or -1 if not terminated
                // see https://html.spec.whatwg.org/multipage/parsing.html#bogus-comment-state
                // Internal -- parse processing instr, return end or -1 if not terminated
                // Internal -- handle starttag, return end or -1 if not terminated
                // Internal -- check to see if we have a complete starttag; return end
                // or -1 if incomplete.
                // Internal -- parse endtag, return end or -1 if incomplete
                // Overridable -- finish processing of start+end tag: <tag.../>
                // Overridable -- handle start tag
                // Overridable -- handle end tag
                // Overridable -- handle character reference
                // Overridable -- handle entity reference
                // Overridable -- handle data
                // Overridable -- handle comment
                // Overridable -- handle declaration
                // Overridable -- handle processing instruction
            define("html.parser", "HTMLParser", w.make_class("HTMLParser", { Mod<"_markupbase">().attr("ParserBase") }, {
                { "CDATA_CONTENT_ELEMENTS", py_tuple({ S<"script">(), S<"style">(), S<"xmp">(), S<"iframe">(), S<"noembed">(), S<"noframes">() }) },
                { "RCDATA_CONTENT_ELEMENTS", py_tuple({ S<"textarea">(), S<"title">() }) },
                { "__init__", w.method("HTMLParser", "__init__", { py_keyword("convert_charrefs", boolean(true)), py_keyword("scripting", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py convert_charrefs = a[1];
                        py scripting = a[2];
                        (B<"super">()(G<"html.parser.HTMLParser">(), self)).method("__init__");
                        self.set_attr("convert_charrefs", convert_charrefs);
                        self.set_attr("scripting", scripting);
                        self.method("reset");
                        return none();
                    }) },
                { "reset", w.method("HTMLParser", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("rawdata", S<"">());
                        self.set_attr("lasttag", S<"?\?\?">());
                        self.set_attr("interesting", G<"html.parser.interesting_normal">());
                        self.set_attr("cdata_elem", none());
                        self.set_attr("_support_cdata", boolean(true));
                        self.set_attr("_escapable", boolean(true));
                        self.set_attr("_pending", py_list());
                        self.set_attr("_pending_len", I<0>());
                        self.set_attr("_parse_threshold", I<1>());
                        (B<"super">()(G<"html.parser.HTMLParser">(), self)).method("reset");
                        return none();
                    }) },
                { "feed", w.method("HTMLParser", "feed", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py n;
                        // Accumulate new data in a list and only join and parse it once
                        // enough has piled up.  Rescanning an unparsed buffer (e.g. an
                        // unterminated tag) and concatenating onto it on every call would
                        // both be quadratic in the input size.
                        {
                            py const target1 = self;
                            target1.set_attr("_pending_len", inplace(NEOS_PY_ADD, target1.attr("_pending_len"), py{ data.size() }));
                        }
                        if (self.attr("_pending_len") < self.attr("_parse_threshold"))
                        {
                            self.attr("_pending").method("append", { data });
                        }
                        else
                        {
                            if (!self.attr("_pending").truthy())
                            {
                                {
                                    py const target2 = self;
                                    target2.set_attr("rawdata", inplace(NEOS_PY_ADD, target2.attr("rawdata"), data));
                                }
                            }
                            else
                            {
                                self.attr("_pending").method("append", { data });
                                {
                                    py const target3 = self;
                                    target3.set_attr("rawdata", inplace(NEOS_PY_ADD, target3.attr("rawdata"), (S<"">()).method("join", { self.attr("_pending") })));
                                }
                                self.attr("_pending").method("clear");
                            }
                            self.set_attr("_pending_len", I<0>());
                            n = py{ self.attr("rawdata").size() };
                            self.method("goahead", { I<0>() });
                            if (py{ self.attr("rawdata").size() } < n)
                            {
                                // Some data was parsed; resume on the next call.
                                self.set_attr("_parse_threshold", I<1>());
                            }
                            else
                            {
                                // Nothing was parsed; wait until the buffer doubles.
                                self.set_attr("_parse_threshold", py{ self.attr("rawdata").size() });
                            }
                        }
                        return none();
                    }) },
                { "close", w.method("HTMLParser", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_pending").truthy())
                        {
                            {
                                py const target4 = self;
                                target4.set_attr("rawdata", inplace(NEOS_PY_ADD, target4.attr("rawdata"), (S<"">()).method("join", { self.attr("_pending") })));
                            }
                            self.attr("_pending").method("clear");
                            self.set_attr("_pending_len", I<0>());
                        }
                        self.method("goahead", { I<1>() });
                        return none();
                    }) },
                { "_HTMLParser__starttag_text", none() },
                { "get_starttag_text", w.method("HTMLParser", "get_starttag_text", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_HTMLParser__starttag_text");
                    }) },
                { "set_cdata_mode", w.method("HTMLParser", "set_cdata_mode", { py_positional("elem"), py_keyword("escapable", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py elem = a[1];
                        py escapable = a[2];
                        self.set_attr("cdata_elem", elem.method("lower"));
                        self.set_attr("_escapable", escapable);
                        if (self.attr("cdata_elem") == (S<"plaintext">()))
                        {
                            self.set_attr("interesting", (Mod<"re">().attr("compile"))(S<"\\Z">()));
                        }
                        else if ((escapable.truthy() && (!self.attr("convert_charrefs").truthy())))
                        {
                            self.set_attr("interesting", (Mod<"re">().attr("compile"))((S<"&|</%s(?=[\\t\\n\\r\\f />])">()) % self.attr("cdata_elem"), (Mod<"re">().attr("IGNORECASE")) | (Mod<"re">().attr("ASCII"))));
                        }
                        else
                        {
                            self.set_attr("interesting", (Mod<"re">().attr("compile"))((S<"</%s(?=[\\t\\n\\r\\f />])">()) % self.attr("cdata_elem"), (Mod<"re">().attr("IGNORECASE")) | (Mod<"re">().attr("ASCII"))));
                        }
                        return none();
                    }) },
                { "clear_cdata_mode", w.method("HTMLParser", "clear_cdata_mode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("interesting", G<"html.parser.interesting_normal">());
                        self.set_attr("cdata_elem", none());
                        self.set_attr("_escapable", boolean(true));
                        return none();
                    }) },
                { "_set_support_cdata", w.method("HTMLParser", "_set_support_cdata", { py_positional("flag", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag = a[1];
                        self.set_attr("_support_cdata", flag);
                        return none();
                    }) },
                { "goahead", w.method("HTMLParser", "goahead", { py_positional("end") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py end = a[1];
                        py amppos;
                        py i;
                        py j;
                        py k;
                        py match;
                        py n;
                        py name;
                        py rawdata;
                        py startswith;
                        py suffix;
                        rawdata = self.attr("rawdata");
                        i = I<0>();
                        n = py{ rawdata.size() };
                        while (i < n)
                        {
                            if ((self.attr("convert_charrefs").truthy() && (!self.attr("cdata_elem").truthy())))
                            {
                                j = rawdata.method("find", { S<"<">(), i });
                                if (j < (I<0>()))
                                {
                                    // if we can't find the next <, either we are at the end
                                    // or there's more text incoming.  If the latter is True,
                                    // we can't pass the text to handle_data in case we have
                                    // a charref cut in half at end.  Try to determine if
                                    // this is the case before proceeding by looking for an
                                    // & near the end and see if it's followed by a space or ;.
                                    amppos = rawdata.method("rfind", { S<"&">(), (B<"max">())(i, n - (I<34>())) });
                                    if (((amppos >= (I<0>())) && (!(Mod<"re">().attr("compile"))(S<"[\\t\\n\\r\\f ;]">()).method("search", { rawdata, amppos }).truthy())))
                                    {
                                        break;
                                    }
                                    j = n;
                                }
                            }
                            else
                            {
                                match = self.attr("interesting").method("search", { rawdata, i });
                                if (match.truthy())
                                {
                                    j = match.method("start");
                                }
                                else
                                {
                                    if (self.attr("cdata_elem").truthy())
                                    {
                                        break;
                                    }
                                    j = n;
                                }
                            }
                            if (i < j)
                            {
                                if ((self.attr("convert_charrefs").truthy() && self.attr("_escapable").truthy()))
                                {
                                    self.method("handle_data", { (Mod<"html">().attr("unescape"))(rawdata[B<"slice">()(i, j, none())]) });
                                }
                                else
                                {
                                    self.method("handle_data", { rawdata[B<"slice">()(i, j, none())] });
                                }
                            }
                            i = self.method("updatepos", { i, j });
                            if (i == n)
                            {
                                break;
                            }
                            startswith = rawdata.attr("startswith");
                            if (startswith(S<"<">(), i).truthy())
                            {
                                if ((G<"html.parser.starttagopen">()).method("match", { rawdata, i }).truthy())
                                {
                                    k = self.method("parse_starttag", { i });
                                }
                                else if (startswith(S<"</">(), i).truthy())
                                {
                                    k = self.method("parse_endtag", { i });
                                }
                                else if (startswith(S<"<!--">(), i).truthy())
                                {
                                    k = self.method("parse_comment", { i });
                                }
                                else if (startswith(S<"<?">(), i).truthy())
                                {
                                    k = self.method("parse_pi", { i });
                                }
                                else if (startswith(S<"<!">(), i).truthy())
                                {
                                    k = self.method("parse_html_declaration", { i });
                                }
                                else if ((((i + (I<1>())) < n) || end.truthy()))
                                {
                                    self.method("handle_data", { S<"<">() });
                                    k = i + (I<1>());
                                }
                                else
                                {
                                    break;
                                }
                                if (k < (I<0>()))
                                {
                                    if (!end.truthy())
                                    {
                                        break;
                                    }
                                    if ((G<"html.parser.starttagopen">()).method("match", { rawdata, i }).truthy())
                                    {
                                    }
                                    else if (startswith(S<"</">(), i).truthy())
                                    {
                                        if ((i + (I<2>())) == n)
                                        {
                                            self.method("handle_data", { S<"</">() });
                                        }
                                        else if ((G<"html.parser.endtagopen">()).method("match", { rawdata, i }).truthy())
                                        {
                                        }
                                        else
                                        {
                                            // bogus comment
                                            self.method("handle_comment", { rawdata[B<"slice">()(i + (I<2>()), none(), none())] });
                                        }
                                    }
                                    else if (startswith(S<"<!--">(), i).truthy())
                                    {
                                        j = n;
                                        for (py const& item : iterate{ py_tuple({ S<"--!">(), S<"--">(), S<"-">() }) })
                                        {
                                            suffix = item;
                                            if (rawdata.method("endswith", { suffix, i + (I<4>()) }).truthy())
                                            {
                                                j = inplace(NEOS_PY_SUBTRACT, j, py{ suffix.size() });
                                                break;
                                            }
                                        }
                                        self.method("handle_comment", { rawdata[B<"slice">()(i + (I<4>()), j, none())] });
                                    }
                                    else if ((startswith(S<"<![CDATA[">(), i).truthy() && self.attr("_support_cdata").truthy()))
                                    {
                                        self.method("unknown_decl", { rawdata[B<"slice">()(i + (I<3>()), none(), none())] });
                                    }
                                    else if (rawdata[B<"slice">()(i, i + (I<9>()), none())].method("lower") == (S<"<!doctype">()))
                                    {
                                        self.method("handle_decl", { rawdata[B<"slice">()(i + (I<2>()), none(), none())] });
                                    }
                                    else if (startswith(S<"<!">(), i).truthy())
                                    {
                                        // bogus comment
                                        self.method("handle_comment", { rawdata[B<"slice">()(i + (I<2>()), none(), none())] });
                                    }
                                    else if (startswith(S<"<?">(), i).truthy())
                                    {
                                        self.method("handle_pi", { rawdata[B<"slice">()(i + (I<2>()), none(), none())] });
                                    }
                                    else
                                    {
                                        py_raise((B<"AssertionError">())(S<"we should not get here!">()));
                                    }
                                    k = n;
                                }
                                i = self.method("updatepos", { i, k });
                            }
                            else if (startswith(S<"&#">(), i).truthy())
                            {
                                match = (G<"html.parser.charref">()).method("match", { rawdata, i });
                                if (match.truthy())
                                {
                                    name = match.method("group")[B<"slice">()(I<2>(), I<-1>(), none())];
                                    self.method("handle_charref", { name });
                                    k = match.method("end");
                                    if (!startswith(S<";">(), k - (I<1>())).truthy())
                                    {
                                        k = k - (I<1>());
                                    }
                                    i = self.method("updatepos", { i, k });
                                    continue;
                                }
                                match = (G<"html.parser.incomplete_charref">()).method("match", { rawdata, i });
                                if (match.truthy())
                                {
                                    if (end.truthy())
                                    {
                                        self.method("handle_charref", { rawdata[B<"slice">()(i + (I<2>()), none(), none())] });
                                        i = self.method("updatepos", { i, n });
                                        break;
                                    }
                                    // incomplete
                                    break;
                                }
                                else if ((i + (I<3>())) < n)
                                {
                                    // not the end of the buffer, and can't be confused
                                    // with some other construct
                                    self.method("handle_data", { S<"&#">() });
                                    i = self.method("updatepos", { i, i + (I<2>()) });
                                }
                                else
                                {
                                    break;
                                }
                            }
                            else if (startswith(S<"&">(), i).truthy())
                            {
                                match = (G<"html.parser.entityref">()).method("match", { rawdata, i });
                                if (match.truthy())
                                {
                                    name = match.method("group", { I<1>() });
                                    self.method("handle_entityref", { name });
                                    k = match.method("end");
                                    if (!startswith(S<";">(), k - (I<1>())).truthy())
                                    {
                                        k = k - (I<1>());
                                    }
                                    i = self.method("updatepos", { i, k });
                                    continue;
                                }
                                match = (G<"html.parser.incomplete">()).method("match", { rawdata, i });
                                if (match.truthy())
                                {
                                    if (end.truthy())
                                    {
                                        self.method("handle_entityref", { rawdata[B<"slice">()(i + (I<1>()), none(), none())] });
                                        i = self.method("updatepos", { i, n });
                                        break;
                                    }
                                    // incomplete
                                    break;
                                }
                                else if ((i + (I<1>())) < n)
                                {
                                    // not the end of the buffer, and can't be confused
                                    // with some other construct
                                    self.method("handle_data", { S<"&">() });
                                    i = self.method("updatepos", { i, i + (I<1>()) });
                                }
                                else
                                {
                                    break;
                                }
                            }
                            else
                            {
                                if (!(I<0>()).truthy())
                                    py_raise(B<"AssertionError">()(S<"interesting.search() lied">()));
                            }
                        }
                        // end while
                        if ((end.truthy() && (i < n)))
                        {
                            if ((self.attr("convert_charrefs").truthy() && self.attr("_escapable").truthy()))
                            {
                                self.method("handle_data", { (Mod<"html">().attr("unescape"))(rawdata[B<"slice">()(i, n, none())]) });
                            }
                            else
                            {
                                self.method("handle_data", { rawdata[B<"slice">()(i, n, none())] });
                            }
                            i = self.method("updatepos", { i, n });
                        }
                        self.set_attr("rawdata", rawdata[B<"slice">()(i, none(), none())]);
                        return none();
                    }) },
                { "parse_html_declaration", w.method("HTMLParser", "parse_html_declaration", { py_positional("i") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py gtpos;
                        py j;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        if (!(rawdata[B<"slice">()(i, i + (I<2>()), none())] == (S<"<!">())))
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_html_declaration()">()));
                        if (rawdata[B<"slice">()(i, i + (I<4>()), none())] == (S<"<!--">()))
                        {
                            // this case is actually already handled in goahead()
                            return self.method("parse_comment", { i });
                        }
                        else if (((rawdata[B<"slice">()(i, i + (I<9>()), none())] == (S<"<![CDATA[">())) && self.attr("_support_cdata").truthy()))
                        {
                            j = rawdata.method("find", { S<"]]>">(), i + (I<9>()) });
                            if (j < (I<0>()))
                            {
                                return I<-1>();
                            }
                            self.method("unknown_decl", { rawdata[B<"slice">()(i + (I<3>()), j, none())] });
                            return j + (I<3>());
                        }
                        else if (rawdata[B<"slice">()(i, i + (I<9>()), none())].method("lower") == (S<"<!doctype">()))
                        {
                            // find the closing >
                            gtpos = rawdata.method("find", { S<">">(), i + (I<9>()) });
                            if (gtpos == (I<-1>()))
                            {
                                return I<-1>();
                            }
                            self.method("handle_decl", { rawdata[B<"slice">()(i + (I<2>()), gtpos, none())] });
                            return gtpos + (I<1>());
                        }
                        else
                        {
                            return self.method("parse_bogus_comment", { i });
                        }
                        return none();
                    }) },
                { "parse_comment", w.method("HTMLParser", "parse_comment", { py_positional("i"), py_positional("report", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py report = a[2];
                        py j;
                        py match;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        if (!rawdata.method("startswith", { S<"<!--">(), i }).truthy())
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_comment()">()));
                        // An empty comment is abruptly closed by the first ">" or "->",
                        // taking priority over a later "-->" or "--!>" close.
                        match = (G<"html.parser.commentabruptclose">()).method("match", { rawdata, i + (I<4>()) });
                        if (!match.truthy())
                        {
                            match = (G<"html.parser.commentclose">()).method("search", { rawdata, i + (I<4>()) });
                            if (!match.truthy())
                            {
                                return I<-1>();
                            }
                        }
                        if (report.truthy())
                        {
                            j = match.method("start");
                            self.method("handle_comment", { rawdata[B<"slice">()(i + (I<4>()), j, none())] });
                        }
                        return match.method("end");
                    }) },
                { "parse_bogus_comment", w.method("HTMLParser", "parse_bogus_comment", { py_positional("i"), py_positional("report", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py report = a[2];
                        py pos;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        if (!py_tuple({ S<"<!">(), S<"</">() }).contains(rawdata[B<"slice">()(i, i + (I<2>()), none())]))
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_bogus_comment()">()));
                        pos = rawdata.method("find", { S<">">(), i + (I<2>()) });
                        if (pos == (I<-1>()))
                        {
                            return I<-1>();
                        }
                        if (report.truthy())
                        {
                            self.method("handle_comment", { rawdata[B<"slice">()(i + (I<2>()), pos, none())] });
                        }
                        return pos + (I<1>());
                    }) },
                { "parse_pi", w.method("HTMLParser", "parse_pi", { py_positional("i") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py j;
                        py match;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        if (!(rawdata[B<"slice">()(i, i + (I<2>()), none())] == (S<"<?">())))
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_pi()">()));
                        match = (G<"html.parser.piclose">()).method("search", { rawdata, i + (I<2>()) });
                        if (!match.truthy())
                        {
                            return I<-1>();
                        }
                        j = match.method("start");
                        self.method("handle_pi", { rawdata[B<"slice">()(i + (I<2>()), j, none())] });
                        j = match.method("end");
                        return j;
                    }) },
                { "parse_starttag", w.method("HTMLParser", "parse_starttag", { py_positional("i") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py attrname;
                        py attrs;
                        py attrvalue;
                        py end;
                        py endpos;
                        py k;
                        py m;
                        py match;
                        py rawdata;
                        py rest;
                        py tag;
                        self.set_attr("_HTMLParser__starttag_text", none());
                        endpos = self.method("check_for_whole_start_tag", { i });
                        if (endpos < (I<0>()))
                        {
                            return endpos;
                        }
                        rawdata = self.attr("rawdata");
                        self.set_attr("_HTMLParser__starttag_text", rawdata[B<"slice">()(i, endpos, none())]);
                        // Now parse the data between i+1 and j into a tag and attrs
                        attrs = py_list();
                        match = (G<"html.parser.tagfind_tolerant">()).method("match", { rawdata, i + (I<1>()) });
                        if (!match.truthy())
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_starttag()">()));
                        k = match.method("end");
                        py const value7 = match.method("group", { I<1>() }).method("lower");
                        self.set_attr("lasttag", value7);
                        tag = value7;
                        while (k < endpos)
                        {
                            m = (G<"html.parser.attrfind_tolerant">()).method("match", { rawdata, k });
                            if (!m.truthy())
                            {
                                break;
                            }
                            auto const items9 = unpack(m.method("group", { I<1>(), I<2>(), I<3>() }), 3);
                            attrname = items9[0];
                            rest = items9[1];
                            attrvalue = items9[2];
                            if (!rest.truthy())
                            {
                                attrvalue = none();
                            }
                            else if ((((attrvalue[B<"slice">()(none(), I<1>(), none())] == (S<"'">())) && ((S<"'">()) == attrvalue[B<"slice">()(I<-1>(), none(), none())])) || ((attrvalue[B<"slice">()(none(), I<1>(), none())] == (S<"\"">())) && ((S<"\"">()) == attrvalue[B<"slice">()(I<-1>(), none(), none())]))))
                            {
                                attrvalue = attrvalue[B<"slice">()(I<1>(), I<-1>(), none())];
                            }
                            if (attrvalue.truthy())
                            {
                                attrvalue = (G<"html.parser._unescape_attrvalue">())(attrvalue);
                            }
                            attrs.method("append", { py_tuple({ attrname.method("lower"), attrvalue }) });
                            k = m.method("end");
                        }
                        end = rawdata[B<"slice">()(k, endpos, none())].method("strip");
                        if (!py_tuple({ S<">">(), S<"/>">() }).contains(end))
                        {
                            self.method("handle_data", { rawdata[B<"slice">()(i, endpos, none())] });
                            return endpos;
                        }
                        if (end.method("endswith", { S<"/>">() }).truthy())
                        {
                            // XHTML-style empty tag: <span attr="value" />
                            self.method("handle_startendtag", { tag, attrs });
                        }
                        else
                        {
                            self.method("handle_starttag", { tag, attrs });
                            if ((self.attr("CDATA_CONTENT_ELEMENTS").contains(tag) || ([&]() -> py { py const t = self.attr("scripting"); return !t.truthy() ? t : boolean(tag == (S<"noscript">())); }()).truthy() || (tag == (S<"plaintext">()))))
                            {
                                self.method("set_cdata_mode", { tag }, { { "escapable", boolean(false) } });
                            }
                            else if (self.attr("RCDATA_CONTENT_ELEMENTS").contains(tag))
                            {
                                self.method("set_cdata_mode", { tag }, { { "escapable", boolean(true) } });
                            }
                        }
                        return endpos;
                    }) },
                { "check_for_whole_start_tag", w.method("HTMLParser", "check_for_whole_start_tag", { py_positional("i") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py j;
                        py match;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        match = (G<"html.parser.locatetagend">()).method("match", { rawdata, i + (I<1>()) });
                        if (!match.truthy())
                            py_raise(B<"AssertionError">()());
                        j = match.method("end");
                        if (rawdata[j - (I<1>())] != (S<">">()))
                        {
                            return I<-1>();
                        }
                        return j;
                    }) },
                { "parse_endtag", w.method("HTMLParser", "parse_endtag", { py_positional("i") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py j;
                        py match;
                        py rawdata;
                        py tag;
                        rawdata = self.attr("rawdata");
                        if (!(rawdata[B<"slice">()(i, i + (I<2>()), none())] == (S<"</">())))
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_endtag">()));
                        if (rawdata.method("find", { S<">">(), i + (I<2>()) }) < (I<0>()))
                        {
                            return I<-1>();
                        }
                        if (!(G<"html.parser.endtagopen">()).method("match", { rawdata, i }).truthy())
                        {
                            if (rawdata[B<"slice">()(i + (I<2>()), i + (I<3>()), none())] == (S<">">()))
                            {
                                // "missing-end-tag-name" parser error
                                return i + (I<3>());
                            }
                            else
                            {
                                return self.method("parse_bogus_comment", { i });
                            }
                        }
                        match = (G<"html.parser.locatetagend">()).method("match", { rawdata, i + (I<2>()) });
                        if (!match.truthy())
                            py_raise(B<"AssertionError">()());
                        j = match.method("end");
                        if (rawdata[j - (I<1>())] != (S<">">()))
                        {
                            return I<-1>();
                        }
                        // find the name: "13.2.5.8 Tag name state"
                        // https://html.spec.whatwg.org/multipage/parsing.html#tag-name-state
                        match = (G<"html.parser.tagfind_tolerant">()).method("match", { rawdata, i + (I<2>()) });
                        if (!match.truthy())
                            py_raise(B<"AssertionError">()());
                        tag = match.method("group", { I<1>() }).method("lower");
                        self.method("handle_endtag", { tag });
                        self.method("clear_cdata_mode");
                        return j;
                    }) },
                { "handle_startendtag", w.method("HTMLParser", "handle_startendtag", { py_positional("tag"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrs = a[2];
                        self.method("handle_starttag", { tag, attrs });
                        self.method("handle_endtag", { tag });
                        return none();
                    }) },
                { "handle_starttag", w.method("HTMLParser", "handle_starttag", { py_positional("tag"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrs = a[2];
                        return none();
                    }) },
                { "handle_endtag", w.method("HTMLParser", "handle_endtag", { py_positional("tag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        return none();
                    }) },
                { "handle_charref", w.method("HTMLParser", "handle_charref", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return none();
                    }) },
                { "handle_entityref", w.method("HTMLParser", "handle_entityref", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return none();
                    }) },
                { "handle_data", w.method("HTMLParser", "handle_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        return none();
                    }) },
                { "handle_comment", w.method("HTMLParser", "handle_comment", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        return none();
                    }) },
                { "handle_decl", w.method("HTMLParser", "handle_decl", { py_positional("decl") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py decl = a[1];
                        return none();
                    }) },
                { "handle_pi", w.method("HTMLParser", "handle_pi", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        return none();
                    }) },
                { "unknown_decl", w.method("HTMLParser", "unknown_decl", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        return none();
                    }) }, }));
        }

        bool html_parser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_html_parser();
                }
                if (auto const doc = html_parser_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("_markupbase", py{ fn_import("_markupbase") }.release());
                aAttributes.emplace_back("unescape", import_attribute("html", "unescape").release());
                aAttributes.emplace_back("html5_entities", import_attribute("html.entities", "html5").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("html.parser", html_parser_module);
    }
}
