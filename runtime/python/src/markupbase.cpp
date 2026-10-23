/*
  _markupbase.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/_markupbase.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module _markupbase (CPython 3.13's Lib/_markupbase.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "markupbase_docs.hpp"

namespace neos::python_runtime::detail::_markupbase_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "_markupbase.name"))
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
                py_raise("SystemError", std::string{ "_markupbase: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"_markupbase.name">()))
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
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "_markupbase", markupbase_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- _markupbase (Lib/_markupbase.py)

        void make__markupbase()
        {
            static module_writer const& w = writer("_markupbase");
            define("_markupbase", "_declname_match", (Mod<"re">()).method("compile", { S<"[a-zA-Z][-_.a-zA-Z0-9]*\\s*">() }).attr("match"));
            define("_markupbase", "_declstringlit_match", (Mod<"re">()).method("compile", { S<"(\\'[^\\']*\\'|\"[^\"]*\")\\s*">() }).attr("match"));
            define("_markupbase", "_commentclose", (Mod<"re">()).method("compile", { S<"--\\s*>">() }));
            define("_markupbase", "_markedsectionclose", (Mod<"re">()).method("compile", { S<"]\\s*]\\s*>">() }));
            // An analysis of the MS-Word extensions is available at
            // http://www.planetpublish.com/xmlarena/xap/Thursday/WordtoXML.pdf
            define("_markupbase", "_msmarkedsectionclose", (Mod<"re">()).method("compile", { S<"]\\s*>">() }));
            undefine("_markupbase", "re");
                // Internal -- update line number and offset.  This should be
                // called for each piece of data exactly once, in order -- in other
                // words the concatenation of all the input strings to this
                // function should be exactly the entire input.
                // Internal -- parse declaration (for use by subclasses).
                // Internal -- parse a marked section
                // Override this to handle MS-word extension syntax <![if word]>content<![endif]>
                // Internal -- parse comment, return length or -1 if not terminated
                // Internal -- scan past the internal subset in a <!DOCTYPE declaration,
                // returning the index just past any whitespace following the trailing ']'.
                // Internal -- scan past <!ELEMENT declarations
                // Internal -- scan past <!ATTLIST declarations
                // Internal -- scan past <!NOTATION declarations
                // Internal -- scan past <!ENTITY declarations
                // Internal -- scan a name token and the new position and the token, or
                // return -1 if we've reached the end of the buffer.
                // To be overridden -- handlers for unknown objects
            define("_markupbase", "ParserBase", w.make_class("ParserBase", {  }, {
                { "__init__", w.method("ParserBase", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("__class__").is((G<"_markupbase.ParserBase">())))
                        {
                            py_raise((B<"RuntimeError">())(S<"_markupbase.ParserBase must be subclassed">()));
                        }
                        return none();
                    }) },
                { "reset", w.method("ParserBase", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("lineno", I<1>());
                        self.set_attr("offset", I<0>());
                        return none();
                    }) },
                { "getpos", w.method("ParserBase", "getpos", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("lineno"), self.attr("offset") });
                    }) },
                { "updatepos", w.method("ParserBase", "updatepos", { py_positional("i"), py_positional("j") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py j = a[2];
                        py nlines;
                        py pos;
                        py rawdata;
                        if (i >= j)
                        {
                            return j;
                        }
                        rawdata = self.attr("rawdata");
                        nlines = rawdata.method("count", { S<"\n">(), i, j });
                        if (nlines.truthy())
                        {
                            self.set_attr("lineno", self.attr("lineno") + nlines);
                            pos = rawdata.method("rindex", { S<"\n">(), i, j });
                            self.set_attr("offset", j - (pos + (I<1>())));
                        }
                        else
                        {
                            self.set_attr("offset", (self.attr("offset") + j) - i);
                        }
                        return j;
                    }) },
                { "_decl_otherchars", S<"">() },
                { "parse_declaration", w.method("ParserBase", "parse_declaration", { py_positional("i") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py c;
                        py data;
                        py decltype_;
                        py j;
                        py m;
                        py n;
                        py name;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        j = i + (I<2>());
                        if (!(rawdata[B<"slice">()(i, j, none())] == (S<"<!">())))
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_declaration">()));
                        if (rawdata[B<"slice">()(j, j + (I<1>()), none())] == (S<">">()))
                        {
                            // the empty comment <!>
                            return j + (I<1>());
                        }
                        if (py_tuple({ S<"-">(), S<"">() }).contains(rawdata[B<"slice">()(j, j + (I<1>()), none())]))
                        {
                            // Start of comment followed by buffer boundary,
                            // or just a buffer boundary.
                            return I<-1>();
                        }
                        // A simple, practical version could look like: ((name|stringlit) S*) + '>'
                        n = py{ rawdata.size() };
                        if (rawdata[B<"slice">()(j, j + (I<2>()), none())] == (S<"--">()))
                        {
                            // Locate --.*-- as the body of the comment
                            return self.method("parse_comment", { i });
                        }
                        else if (rawdata[j] == (S<"[">()))
                        {
                            // Locate [statusWord [...arbitrary SGML...]] as the body of the marked section
                            // Where statusWord is one of TEMP, CDATA, IGNORE, INCLUDE, RCDATA
                            // Note that this is extended by Microsoft Office "Save as Web" function
                            // to include [if...] and [endif].
                            return self.method("parse_marked_section", { i });
                        }
                        else
                        {
                            auto const items1 = unpack(self.method("_scan_name", { j, i }), 2);
                            decltype_ = items1[0];
                            j = items1[1];
                        }
                        if (j < (I<0>()))
                        {
                            return j;
                        }
                        if (decltype_ == (S<"doctype">()))
                        {
                            self.set_attr("_decl_otherchars", S<"">());
                        }
                        while (j < n)
                        {
                            c = rawdata[j];
                            if (c == (S<">">()))
                            {
                                // end of declaration syntax
                                data = rawdata[B<"slice">()(i + (I<2>()), j, none())];
                                if (decltype_ == (S<"doctype">()))
                                {
                                    self.method("handle_decl", { data });
                                }
                                else
                                {
                                    // According to the HTML5 specs sections "8.2.4.44 Bogus
                                    // comment state" and "8.2.4.45 Markup declaration open
                                    // state", a comment token should be emitted.
                                    // Calling unknown_decl provides more flexibility though.
                                    self.method("unknown_decl", { data });
                                }
                                return j + (I<1>());
                            }
                            if ((S<"\"'">()).contains(c))
                            {
                                m = (G<"_markupbase._declstringlit_match">())(rawdata, j);
                                if (!m.truthy())
                                {
                                    return I<-1>();
                                }
                                j = m.method("end");
                            }
                            else if ((S<"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ">()).contains(c))
                            {
                                auto const items3 = unpack(self.method("_scan_name", { j, i }), 2);
                                name = items3[0];
                                j = items3[1];
                            }
                            else if (self.attr("_decl_otherchars").contains(c))
                            {
                                j = j + (I<1>());
                            }
                            else if (c == (S<"[">()))
                            {
                                // this could be handled in a separate doctype parser
                                if (decltype_ == (S<"doctype">()))
                                {
                                    j = self.method("_parse_doctype_subset", { j + (I<1>()), i });
                                }
                                else if (set_of({ S<"attlist">(), S<"linktype">(), S<"link">(), S<"element">() }).contains(decltype_))
                                {
                                    // must tolerate []'d groups in a content model in an element declaration
                                    // also in data attribute specifications of attlist declaration
                                    // also link type declaration subsets in linktype declarations
                                    // also link attribute specification lists in link declarations
                                    py_raise((B<"AssertionError">())((S<"unsupported '[' char in %s declaration">()) % decltype_));
                                }
                                else
                                {
                                    py_raise((B<"AssertionError">())(S<"unexpected '[' char in declaration">()));
                                }
                            }
                            else
                            {
                                py_raise((B<"AssertionError">())((S<"unexpected %r char in declaration">()) % rawdata[j]));
                            }
                            if (j < (I<0>()))
                            {
                                return j;
                            }
                        }
                        return I<-1>();
                    }) },
                { "parse_marked_section", w.method("ParserBase", "parse_marked_section", { py_positional("i"), py_positional("report", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py report = a[2];
                        py j;
                        py match;
                        py rawdata;
                        py sectName;
                        rawdata = self.attr("rawdata");
                        if (!(rawdata[B<"slice">()(i, i + (I<3>()), none())] == (S<"<![">())))
                            py_raise(B<"AssertionError">()(S<"unexpected call to parse_marked_section()">()));
                        auto const items4 = unpack(self.method("_scan_name", { i + (I<3>()), i }), 2);
                        sectName = items4[0];
                        j = items4[1];
                        if (j < (I<0>()))
                        {
                            return j;
                        }
                        if (set_of({ S<"temp">(), S<"cdata">(), S<"ignore">(), S<"include">(), S<"rcdata">() }).contains(sectName))
                        {
                            // look for standard ]]> ending
                            match = (G<"_markupbase._markedsectionclose">()).method("search", { rawdata, i + (I<3>()) });
                        }
                        else if (set_of({ S<"if">(), S<"else">(), S<"endif">() }).contains(sectName))
                        {
                            // look for MS Office ]> ending
                            match = (G<"_markupbase._msmarkedsectionclose">()).method("search", { rawdata, i + (I<3>()) });
                        }
                        else
                        {
                            py_raise((B<"AssertionError">())((S<"unknown status keyword %r in marked section">()) % rawdata[B<"slice">()(i + (I<3>()), j, none())]));
                        }
                        if (!match.truthy())
                        {
                            return I<-1>();
                        }
                        if (report.truthy())
                        {
                            j = match.method("start", { I<0>() });
                            self.method("unknown_decl", { rawdata[B<"slice">()(i + (I<3>()), j, none())] });
                        }
                        return match.method("end", { I<0>() });
                    }) },
                { "parse_comment", w.method("ParserBase", "parse_comment", { py_positional("i"), py_positional("report", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py report = a[2];
                        py j;
                        py match;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        if (rawdata[B<"slice">()(i, i + (I<4>()), none())] != (S<"<!--">()))
                        {
                            py_raise((B<"AssertionError">())(S<"unexpected call to parse_comment()">()));
                        }
                        match = (G<"_markupbase._commentclose">()).method("search", { rawdata, i + (I<4>()) });
                        if (!match.truthy())
                        {
                            return I<-1>();
                        }
                        if (report.truthy())
                        {
                            j = match.method("start", { I<0>() });
                            self.method("handle_comment", { rawdata[B<"slice">()(i + (I<4>()), j, none())] });
                        }
                        return match.method("end", { I<0>() });
                    }) },
                { "_parse_doctype_subset", w.method("ParserBase", "_parse_doctype_subset", { py_positional("i"), py_positional("declstartpos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py declstartpos = a[2];
                        py c;
                        py j;
                        py meth;
                        py n;
                        py name;
                        py rawdata;
                        py s;
                        rawdata = self.attr("rawdata");
                        n = py{ rawdata.size() };
                        j = i;
                        while (j < n)
                        {
                            c = rawdata[j];
                            if (c == (S<"<">()))
                            {
                                s = rawdata[B<"slice">()(j, j + (I<2>()), none())];
                                if (s == (S<"<">()))
                                {
                                    // end of buffer; incomplete
                                    return I<-1>();
                                }
                                if (s != (S<"<!">()))
                                {
                                    self.method("updatepos", { declstartpos, j + (I<1>()) });
                                    py_raise((B<"AssertionError">())((S<"unexpected char in internal subset (in %r)">()) % s));
                                }
                                if ((j + (I<2>())) == n)
                                {
                                    // end of buffer; incomplete
                                    return I<-1>();
                                }
                                if ((j + (I<4>())) > n)
                                {
                                    // end of buffer; incomplete
                                    return I<-1>();
                                }
                                if (rawdata[B<"slice">()(j, j + (I<4>()), none())] == (S<"<!--">()))
                                {
                                    j = self.method("parse_comment", { j }, { { "report", I<0>() } });
                                    if (j < (I<0>()))
                                    {
                                        return j;
                                    }
                                    continue;
                                }
                                auto const items6 = unpack(self.method("_scan_name", { j + (I<2>()), declstartpos }), 2);
                                name = items6[0];
                                j = items6[1];
                                if (j == (I<-1>()))
                                {
                                    return I<-1>();
                                }
                                if (!set_of({ S<"attlist">(), S<"element">(), S<"entity">(), S<"notation">() }).contains(name))
                                {
                                    self.method("updatepos", { declstartpos, j + (I<2>()) });
                                    py_raise((B<"AssertionError">())((S<"unknown declaration %r in internal subset">()) % name));
                                }
                                // handle the individual names
                                meth = (B<"getattr">())(self, (S<"_parse_doctype_">()) + name);
                                j = meth(j, declstartpos);
                                if (j < (I<0>()))
                                {
                                    return j;
                                }
                            }
                            else if (c == (S<"%">()))
                            {
                                // parameter entity reference
                                if ((j + (I<1>())) == n)
                                {
                                    // end of buffer; incomplete
                                    return I<-1>();
                                }
                                auto const items7 = unpack(self.method("_scan_name", { j + (I<1>()), declstartpos }), 2);
                                s = items7[0];
                                j = items7[1];
                                if (j < (I<0>()))
                                {
                                    return j;
                                }
                                if (rawdata[j] == (S<";">()))
                                {
                                    j = j + (I<1>());
                                }
                            }
                            else if (c == (S<"]">()))
                            {
                                j = j + (I<1>());
                                while (((j < n) && rawdata[j].method("isspace").truthy()))
                                {
                                    j = j + (I<1>());
                                }
                                if (j < n)
                                {
                                    if (rawdata[j] == (S<">">()))
                                    {
                                        return j;
                                    }
                                    self.method("updatepos", { declstartpos, j });
                                    py_raise((B<"AssertionError">())(S<"unexpected char after internal subset">()));
                                }
                                else
                                {
                                    return I<-1>();
                                }
                            }
                            else if (c.method("isspace").truthy())
                            {
                                j = j + (I<1>());
                            }
                            else
                            {
                                self.method("updatepos", { declstartpos, j });
                                py_raise((B<"AssertionError">())((S<"unexpected char %r in internal subset">()) % c));
                            }
                        }
                        // end of buffer reached
                        return I<-1>();
                    }) },
                { "_parse_doctype_element", w.method("ParserBase", "_parse_doctype_element", { py_positional("i"), py_positional("declstartpos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py declstartpos = a[2];
                        py j;
                        py name;
                        py rawdata;
                        auto const items9 = unpack(self.method("_scan_name", { i, declstartpos }), 2);
                        name = items9[0];
                        j = items9[1];
                        if (j == (I<-1>()))
                        {
                            return I<-1>();
                        }
                        // style content model; just skip until '>'
                        rawdata = self.attr("rawdata");
                        if (rawdata[B<"slice">()(j, none(), none())].contains((S<">">())))
                        {
                            return rawdata.method("find", { S<">">(), j }) + (I<1>());
                        }
                        return I<-1>();
                    }) },
                { "_parse_doctype_attlist", w.method("ParserBase", "_parse_doctype_attlist", { py_positional("i"), py_positional("declstartpos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py declstartpos = a[2];
                        py c;
                        py j;
                        py m;
                        py name;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        auto const items10 = unpack(self.method("_scan_name", { i, declstartpos }), 2);
                        name = items10[0];
                        j = items10[1];
                        c = rawdata[B<"slice">()(j, j + (I<1>()), none())];
                        if (c == (S<"">()))
                        {
                            return I<-1>();
                        }
                        if (c == (S<">">()))
                        {
                            return j + (I<1>());
                        }
                        while ((I<1>()).truthy())
                        {
                            // scan a series of attribute descriptions; simplified:
                            // name type [value] [#constraint]
                            auto const items12 = unpack(self.method("_scan_name", { j, declstartpos }), 2);
                            name = items12[0];
                            j = items12[1];
                            if (j < (I<0>()))
                            {
                                return j;
                            }
                            c = rawdata[B<"slice">()(j, j + (I<1>()), none())];
                            if (c == (S<"">()))
                            {
                                return I<-1>();
                            }
                            if (c == (S<"(">()))
                            {
                                // an enumerated type; look for ')'
                                if (rawdata[B<"slice">()(j, none(), none())].contains((S<")">())))
                                {
                                    j = rawdata.method("find", { S<")">(), j }) + (I<1>());
                                }
                                else
                                {
                                    return I<-1>();
                                }
                                while (rawdata[B<"slice">()(j, j + (I<1>()), none())].method("isspace").truthy())
                                {
                                    j = j + (I<1>());
                                }
                                if (!rawdata[B<"slice">()(j, none(), none())].truthy())
                                {
                                    // end of buffer, incomplete
                                    return I<-1>();
                                }
                            }
                            else
                            {
                                auto const items14 = unpack(self.method("_scan_name", { j, declstartpos }), 2);
                                name = items14[0];
                                j = items14[1];
                            }
                            c = rawdata[B<"slice">()(j, j + (I<1>()), none())];
                            if (!c.truthy())
                            {
                                return I<-1>();
                            }
                            if ((S<"'\"">()).contains(c))
                            {
                                m = (G<"_markupbase._declstringlit_match">())(rawdata, j);
                                if (m.truthy())
                                {
                                    j = m.method("end");
                                }
                                else
                                {
                                    return I<-1>();
                                }
                                c = rawdata[B<"slice">()(j, j + (I<1>()), none())];
                                if (!c.truthy())
                                {
                                    return I<-1>();
                                }
                            }
                            if (c == (S<"#">()))
                            {
                                if (rawdata[B<"slice">()(j, none(), none())] == (S<"#">()))
                                {
                                    // end of buffer
                                    return I<-1>();
                                }
                                auto const items15 = unpack(self.method("_scan_name", { j + (I<1>()), declstartpos }), 2);
                                name = items15[0];
                                j = items15[1];
                                if (j < (I<0>()))
                                {
                                    return j;
                                }
                                c = rawdata[B<"slice">()(j, j + (I<1>()), none())];
                                if (!c.truthy())
                                {
                                    return I<-1>();
                                }
                            }
                            if (c == (S<">">()))
                            {
                                // all done
                                return j + (I<1>());
                            }
                        }
                        return none();
                    }) },
                { "_parse_doctype_notation", w.method("ParserBase", "_parse_doctype_notation", { py_positional("i"), py_positional("declstartpos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py declstartpos = a[2];
                        py c;
                        py j;
                        py m;
                        py name;
                        py rawdata;
                        auto const items16 = unpack(self.method("_scan_name", { i, declstartpos }), 2);
                        name = items16[0];
                        j = items16[1];
                        if (j < (I<0>()))
                        {
                            return j;
                        }
                        rawdata = self.attr("rawdata");
                        while ((I<1>()).truthy())
                        {
                            c = rawdata[B<"slice">()(j, j + (I<1>()), none())];
                            if (!c.truthy())
                            {
                                // end of buffer; incomplete
                                return I<-1>();
                            }
                            if (c == (S<">">()))
                            {
                                return j + (I<1>());
                            }
                            if ((S<"'\"">()).contains(c))
                            {
                                m = (G<"_markupbase._declstringlit_match">())(rawdata, j);
                                if (!m.truthy())
                                {
                                    return I<-1>();
                                }
                                j = m.method("end");
                            }
                            else
                            {
                                auto const items18 = unpack(self.method("_scan_name", { j, declstartpos }), 2);
                                name = items18[0];
                                j = items18[1];
                                if (j < (I<0>()))
                                {
                                    return j;
                                }
                            }
                        }
                        return none();
                    }) },
                { "_parse_doctype_entity", w.method("ParserBase", "_parse_doctype_entity", { py_positional("i"), py_positional("declstartpos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py declstartpos = a[2];
                        py c;
                        py j;
                        py m;
                        py name;
                        py rawdata;
                        rawdata = self.attr("rawdata");
                        if (rawdata[B<"slice">()(i, i + (I<1>()), none())] == (S<"%">()))
                        {
                            j = i + (I<1>());
                            while ((I<1>()).truthy())
                            {
                                c = rawdata[B<"slice">()(j, j + (I<1>()), none())];
                                if (!c.truthy())
                                {
                                    return I<-1>();
                                }
                                if (c.method("isspace").truthy())
                                {
                                    j = j + (I<1>());
                                }
                                else
                                {
                                    break;
                                }
                            }
                        }
                        else
                        {
                            j = i;
                        }
                        auto const items20 = unpack(self.method("_scan_name", { j, declstartpos }), 2);
                        name = items20[0];
                        j = items20[1];
                        if (j < (I<0>()))
                        {
                            return j;
                        }
                        while ((I<1>()).truthy())
                        {
                            c = self.attr("rawdata")[B<"slice">()(j, j + (I<1>()), none())];
                            if (!c.truthy())
                            {
                                return I<-1>();
                            }
                            if ((S<"'\"">()).contains(c))
                            {
                                m = (G<"_markupbase._declstringlit_match">())(rawdata, j);
                                if (m.truthy())
                                {
                                    j = m.method("end");
                                }
                                else
                                {
                                    return I<-1>();
                                }
                            }
                            else if (c == (S<">">()))
                            {
                                return j + (I<1>());
                            }
                            else
                            {
                                auto const items22 = unpack(self.method("_scan_name", { j, declstartpos }), 2);
                                name = items22[0];
                                j = items22[1];
                                if (j < (I<0>()))
                                {
                                    return j;
                                }
                            }
                        }
                        return none();
                    }) },
                { "_scan_name", w.method("ParserBase", "_scan_name", { py_positional("i"), py_positional("declstartpos") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i = a[1];
                        py declstartpos = a[2];
                        py m;
                        py n;
                        py name;
                        py rawdata;
                        py s;
                        rawdata = self.attr("rawdata");
                        n = py{ rawdata.size() };
                        if (i == n)
                        {
                            return py_tuple({ none(), I<-1>() });
                        }
                        m = (G<"_markupbase._declname_match">())(rawdata, i);
                        if (m.truthy())
                        {
                            s = m.method("group");
                            name = s.method("strip");
                            if ((i + py{ s.size() }) == n)
                            {
                                return py_tuple({ none(), I<-1>() });
                            }
                            return py_tuple({ name.method("lower"), m.method("end") });
                        }
                        else
                        {
                            self.method("updatepos", { declstartpos, i });
                            py_raise((B<"AssertionError">())((S<"expected name token at %r">()) % rawdata[B<"slice">()(declstartpos, declstartpos + (I<20>()), none())]));
                        }
                        return none();
                    }) },
                { "unknown_decl", w.method("ParserBase", "unknown_decl", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        return none();
                    }) }, }));
        }

        bool _markupbase_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make__markupbase();
                }
                if (auto const doc = markupbase_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("_markupbase", _markupbase_module);
    }
}
