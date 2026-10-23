/*
  email_parseaddr.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/_parseaddr.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email._parseaddr (CPython 3.13's Lib/email/_parseaddr.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_parseaddr_docs.hpp"

namespace neos::python_runtime::detail::email__parseaddr_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email._parseaddr.name"))
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
                py_raise("SystemError", std::string{ "email._parseaddr: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email._parseaddr.name">()))
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
            if (modules_imported("email._parseaddr"))
                return py{ fn_import("email._parseaddr") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email._parseaddr" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email._parseaddr", email_parseaddr_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email._parseaddr (Lib/email/_parseaddr.py)

        void make_email__parseaddr()
        {
            static module_writer const& w = writer("email._parseaddr");
            // Copyright (C) 2002-2007 Python Software Foundation
            // Contact: email-sig@python.org
            define("email._parseaddr", "__all__", py_list({ S<"mktime_tz">(), S<"parsedate">(), S<"parsedate_tz">(), S<"quote">() }));
            define("email._parseaddr", "SPACE", S<" ">());
            define("email._parseaddr", "EMPTYSTRING", S<"">());
            define("email._parseaddr", "COMMASPACE", S<", ">());
            // Parse a date field
            define("email._parseaddr", "_monthnames", py_list({ S<"jan">(), S<"feb">(), S<"mar">(), S<"apr">(), S<"may">(), S<"jun">(), S<"jul">(), S<"aug">(), S<"sep">(), S<"oct">(), S<"nov">(), S<"dec">(), S<"january">(), S<"february">(), S<"march">(), S<"april">(), S<"may">(), S<"june">(), S<"july">(), S<"august">(), S<"september">(), S<"october">(), S<"november">(), S<"december">() }));
            define("email._parseaddr", "_daynames", py_list({ S<"mon">(), S<"tue">(), S<"wed">(), S<"thu">(), S<"fri">(), S<"sat">(), S<"sun">() }));
            // The timezone table does not include the military time zones defined
            // in RFC822, other than Z.  According to RFC1123, the description in
            // RFC822 gets the signs wrong, so we can't rely on any such time
            // zones.  RFC1123 recommends that numeric timezone indicators be used
            // instead of timezone names.
            define("email._parseaddr", "_timezones", dict_of({ { S<"UT">(), I<0>() }, { S<"UTC">(), I<0>() }, { S<"GMT">(), I<0>() }, { S<"Z">(), I<0>() }, { S<"AST">(), I<-400>() }, { S<"ADT">(), I<-300>() }, { S<"EST">(), I<-500>() }, { S<"EDT">(), I<-400>() }, { S<"CST">(), I<-600>() }, { S<"CDT">(), I<-500>() }, { S<"MST">(), I<-700>() }, { S<"MDT">(), I<-600>() }, { S<"PST">(), I<-800>() }, { S<"PDT">(), I<-700>() } }));
            define("email._parseaddr", "parsedate_tz", w.function("parsedate_tz", "parsedate_tz", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py res;
                    res = (G<"email._parseaddr._parsedate_tz">())(data);
                    if (!res.truthy())
                    {
                        return none();
                    }
                    if (res[I<9>()].is_none())
                    {
                        res.set_item(I<9>(), I<0>());
                    }
                    return (B<"tuple">())(res);
                }));

            define("email._parseaddr", "_parsedate_tz", w.function("_parsedate_tz", "_parsedate_tz", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py dd;
                    py i;
                    py mm;
                    py s;
                    py stuff;
                    py thh;
                    py tm;
                    py tmm;
                    py tss;
                    py tz;
                    py tzoffset;
                    py tzsign;
                    py yy;
                    if (!data.truthy())
                    {
                        return none();
                    }
                    data = data.method("split");
                    if (!data.truthy())
                    {
                        return none();
                    }
                    // The FWS after the comma after the day-of-week is optional, so search and
                    // adjust for this.
                    if ((data[I<0>()].method("endswith", { S<",">() }).truthy() || (G<"email._parseaddr._daynames">()).contains(data[I<0>()].method("lower"))))
                    {
                        // There's a dayname here. Skip it
                        del_item(data, I<0>());
                    }
                    else
                    {
                        i = data[I<0>()].method("rfind", { S<",">() });
                        if (i >= (I<0>()))
                        {
                            data.set_item(I<0>(), data[I<0>()][B<"slice">()(i + (I<1>()), none(), none())]);
                        }
                    }
                    if (py{ data.size() } == (I<3>()))
                    {
                        stuff = data[I<0>()].method("split", { S<"-">() });
                        if (py{ stuff.size() } == (I<3>()))
                        {
                            data = stuff + data[B<"slice">()(I<1>(), none(), none())];
                        }
                    }
                    if (py{ data.size() } == (I<4>()))
                    {
                        s = data[I<3>()];
                        i = s.method("find", { S<"+">() });
                        if (i == (I<-1>()))
                        {
                            i = s.method("find", { S<"-">() });
                        }
                        if (i > (I<0>()))
                        {
                            data.set_item(B<"slice">()(I<3>(), none(), none()), py_list({ s[B<"slice">()(none(), i, none())], s[B<"slice">()(i, none(), none())] }));
                        }
                        else
                        {
                            data.method("append", { S<"">() });
                        }
                    }
                    if (py{ data.size() } < (I<5>()))
                    {
                        return none();
                    }
                    data = data[B<"slice">()(none(), I<5>(), none())];
                    auto const items1 = unpack(data, 5);
                    dd = items1[0];
                    mm = items1[1];
                    yy = items1[2];
                    tm = items1[3];
                    tz = items1[4];
                    if (!([&]() -> py { py const t = dd; return !t.truthy() ? t : [&]() -> py { py const t = mm; return !t.truthy() ? t : yy; }(); }()).truthy())
                    {
                        return none();
                    }
                    mm = mm.method("lower");
                    if (!(G<"email._parseaddr._monthnames">()).contains(mm))
                    {
                        auto const items2 = unpack(py_tuple({ mm, dd.method("lower") }), 2);
                        dd = items2[0];
                        mm = items2[1];
                        if (!(G<"email._parseaddr._monthnames">()).contains(mm))
                        {
                            return none();
                        }
                    }
                    mm = (G<"email._parseaddr._monthnames">()).method("index", { mm }) + (I<1>());
                    if (mm > (I<12>()))
                    {
                        mm = inplace(NEOS_PY_SUBTRACT, mm, I<12>());
                    }
                    if (dd[I<-1>()] == (S<",">()))
                    {
                        dd = dd[B<"slice">()(none(), I<-1>(), none())];
                    }
                    i = yy.method("find", { S<":">() });
                    if (i > (I<0>()))
                    {
                        auto const items3 = unpack(py_tuple({ tm, yy }), 2);
                        yy = items3[0];
                        tm = items3[1];
                    }
                    if (yy[I<-1>()] == (S<",">()))
                    {
                        yy = yy[B<"slice">()(none(), I<-1>(), none())];
                        if (!yy.truthy())
                        {
                            return none();
                        }
                    }
                    if (!yy[I<0>()].method("isdigit").truthy())
                    {
                        auto const items4 = unpack(py_tuple({ tz, yy }), 2);
                        yy = items4[0];
                        tz = items4[1];
                    }
                    if (tm[I<-1>()] == (S<",">()))
                    {
                        tm = tm[B<"slice">()(none(), I<-1>(), none())];
                    }
                    tm = tm.method("split", { S<":">() });
                    if (py{ tm.size() } == (I<2>()))
                    {
                        auto const items5 = unpack(tm, 2);
                        thh = items5[0];
                        tmm = items5[1];
                        tss = S<"0">();
                    }
                    else if (py{ tm.size() } == (I<3>()))
                    {
                        auto const items6 = unpack(tm, 3);
                        thh = items6[0];
                        tmm = items6[1];
                        tss = items6[2];
                    }
                    else if (((py{ tm.size() } == (I<1>())) && tm[I<0>()].contains((S<".">()))))
                    {
                        // Some non-compliant MUAs use '.' to separate time elements.
                        tm = tm[I<0>()].method("split", { S<".">() });
                        if (py{ tm.size() } == (I<2>()))
                        {
                            auto const items7 = unpack(tm, 2);
                            thh = items7[0];
                            tmm = items7[1];
                            tss = I<0>();
                        }
                        else if (py{ tm.size() } == (I<3>()))
                        {
                            auto const items8 = unpack(tm, 3);
                            thh = items8[0];
                            tmm = items8[1];
                            tss = items8[2];
                        }
                        else
                        {
                            return none();
                        }
                    }
                    else
                    {
                        return none();
                    }
                    try
                    {
                        yy = (B<"int">())(yy);
                        dd = (B<"int">())(dd);
                        thh = (B<"int">())(thh);
                        tmm = (B<"int">())(tmm);
                        tss = (B<"int">())(tss);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler9;
                        if (matches(handler9.exception, B<"ValueError">()))
                        {
                            return none();
                        }
                        else
                            py_reraise(handler9.exception);
                    }
                    // Check for a yy specified in two-digit format, then convert it to the
                    // appropriate four-digit format, according to the POSIX standard. RFC 822
                    // calls for a two-digit yy, but RFC 2822 (which obsoletes RFC 822) already
                    // mandated a 4-digit yy, and RFC 5322 (which obsoletes RFC 2822) continues
                    // this requirement. For more information, see the documentation for
                    // the time module.
                    if (yy < (I<100>()))
                    {
                        // The year is between 1969 and 1999 (inclusive).
                        if (yy > (I<68>()))
                        {
                            yy = inplace(NEOS_PY_ADD, yy, I<1900>());
                        }
                        else
                        {
                            // The year is between 2000 and 2068 (inclusive).
                            yy = inplace(NEOS_PY_ADD, yy, I<2000>());
                        }
                    }
                    tzoffset = none();
                    tz = tz.method("upper");
                    if ((G<"email._parseaddr._timezones">()).contains(tz))
                    {
                        tzoffset = (G<"email._parseaddr._timezones">())[tz];
                    }
                    else
                    {
                        try
                        {
                            tzoffset = (B<"int">())(tz);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler10;
                            if (matches(handler10.exception, B<"ValueError">()))
                            {
                            }
                            else
                                py_reraise(handler10.exception);
                        }
                        if (((tzoffset == (I<0>())) && tz.method("startswith", { S<"-">() }).truthy()))
                        {
                            tzoffset = none();
                        }
                    }
                    // Convert a timezone offset into seconds ; -0500 -> -18000
                    if (tzoffset.truthy())
                    {
                        if (tzoffset < (I<0>()))
                        {
                            tzsign = I<-1>();
                            tzoffset = -tzoffset;
                        }
                        else
                        {
                            tzsign = I<1>();
                        }
                        tzoffset = tzsign * ((floordiv(tzoffset, (I<100>())) * (I<3600>())) + ((tzoffset % (I<100>())) * (I<60>())));
                    }
                    // Daylight Saving Time flag is set to -1, since DST is unknown.
                    return py_list({ yy, mm, dd, thh, tmm, tss, I<0>(), I<1>(), I<-1>(), tzoffset });
                }));

            define("email._parseaddr", "parsedate", w.function("parsedate", "parsedate", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py t;
                    t = (G<"email._parseaddr.parsedate_tz">())(data);
                    if (isinstance(t, B<"tuple">()))
                    {
                        return t[B<"slice">()(none(), I<9>(), none())];
                    }
                    else
                    {
                        return t;
                    }
                    return none();
                }));

            define("email._parseaddr", "mktime_tz", w.function("mktime_tz", "mktime_tz", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py calendar;
                    py t;
                    if (data[I<9>()].is_none())
                    {
                        // No zone info, so localtime is better assumption than GMT
                        return (Mod<"time">().attr("mktime"))(data[B<"slice">()(none(), I<8>(), none())] + py_tuple({ I<-1>() }));
                    }
                    else
                    {
                        // Delay the import, since mktime_tz is rarely used
                        calendar = module_of("calendar");
                        t = calendar.method("timegm", { data });
                        return t - data[I<9>()];
                    }
                    return none();
                }));

            define("email._parseaddr", "quote", w.function("quote", "quote", { py_positional("str") }, [](arguments a) -> py
                {
                    py str = a[0];
                    return str.method("replace", { S<"\\">(), S<"\\\\">() }).method("replace", { S<"\"">(), S<"\\\"">() });
                }));

            define("email._parseaddr", "AddrlistClass", w.make_class("AddrlistClass", {  }, {
                { "__init__", w.method("AddrlistClass", "__init__", { py_positional("field") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py field = a[1];
                        self.set_attr("specials", S<"()<>@,:;.\"[]">());
                        self.set_attr("pos", I<0>());
                        self.set_attr("LWS", S<" \t">());
                        self.set_attr("CR", S<"\r\n">());
                        self.set_attr("FWS", self.attr("LWS") + self.attr("CR"));
                        self.set_attr("atomends", (self.attr("specials") + self.attr("LWS")) + self.attr("CR"));
                        // Note that RFC 2822 section 4.1 introduced '.' as obs-phrase to handle
                        // existing practice (periods in display names), even though it was not
                        // allowed in RFC 822. RFC 5322 section 4.1 (which obsoletes RFC 2822)
                        // continues this requirement. We must recognize obsolete syntax, so
                        // allow dots in phrases.
                        self.set_attr("phraseends", self.attr("atomends").method("replace", { S<".">(), S<"">() }));
                        self.set_attr("field", field);
                        self.set_attr("commentlist", py_list());
                        return none();
                    }) },
                { "gotonext", w.method("AddrlistClass", "gotonext", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wslist;
                        wslist = py_list();
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            if ((self.attr("LWS") + (S<"\n\r">())).contains(self.attr("field")[self.attr("pos")]))
                            {
                                if (!(S<"\n\r">()).contains(self.attr("field")[self.attr("pos")]))
                                {
                                    wslist.method("append", { self.attr("field")[self.attr("pos")] });
                                }
                                {
                                    py const target12 = self;
                                    target12.set_attr("pos", inplace(NEOS_PY_ADD, target12.attr("pos"), I<1>()));
                                }
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"(">()))
                            {
                                self.attr("commentlist").method("append", { self.method("getcomment") });
                            }
                            else
                            {
                                break;
                            }
                        }
                        return (G<"email._parseaddr.EMPTYSTRING">()).method("join", { wslist });
                    }) },
                { "getaddrlist", w.method("AddrlistClass", "getaddrlist", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ad;
                        py result;
                        result = py_list();
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            ad = self.method("getaddress");
                            if (ad.truthy())
                            {
                                result = inplace(NEOS_PY_ADD, result, ad);
                            }
                            else
                            {
                                result.method("append", { py_tuple({ S<"">(), S<"">() }) });
                            }
                        }
                        return result;
                    }) },
                { "getaddress", w.method("AddrlistClass", "getaddress", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py addrspec;
                        py fieldlen;
                        py oldcl;
                        py oldpos;
                        py plist;
                        py returnlist;
                        py routeaddr;
                        self.set_attr("commentlist", py_list());
                        self.method("gotonext");
                        oldpos = self.attr("pos");
                        oldcl = self.attr("commentlist");
                        plist = self.method("getphraselist");
                        self.method("gotonext");
                        returnlist = py_list();
                        if (self.attr("pos") >= py{ self.attr("field").size() })
                        {
                            // Bad email address technically, no domain.
                            if (plist.truthy())
                            {
                                returnlist = py_list({ py_tuple({ (G<"email._parseaddr.SPACE">()).method("join", { self.attr("commentlist") }), plist[I<0>()] }) });
                            }
                        }
                        else if ((S<".@">()).contains(self.attr("field")[self.attr("pos")]))
                        {
                            // email address is just an addrspec
                            // this isn't very efficient since we start over
                            self.set_attr("pos", oldpos);
                            self.set_attr("commentlist", oldcl);
                            addrspec = self.method("getaddrspec");
                            returnlist = py_list({ py_tuple({ (G<"email._parseaddr.SPACE">()).method("join", { self.attr("commentlist") }), addrspec }) });
                        }
                        else if (self.attr("field")[self.attr("pos")] == (S<":">()))
                        {
                            // address is a group
                            returnlist = py_list();
                            fieldlen = py{ self.attr("field").size() };
                            {
                                py const target14 = self;
                                target14.set_attr("pos", inplace(NEOS_PY_ADD, target14.attr("pos"), I<1>()));
                            }
                            while (self.attr("pos") < py{ self.attr("field").size() })
                            {
                                self.method("gotonext");
                                if (((self.attr("pos") < fieldlen) && (self.attr("field")[self.attr("pos")] == (S<";">()))))
                                {
                                    {
                                        py const target16 = self;
                                        target16.set_attr("pos", inplace(NEOS_PY_ADD, target16.attr("pos"), I<1>()));
                                    }
                                    break;
                                }
                                returnlist = returnlist + self.method("getaddress");
                            }
                        }
                        else if (self.attr("field")[self.attr("pos")] == (S<"<">()))
                        {
                            // Address is a phrase then a route addr
                            routeaddr = self.method("getrouteaddr");
                            if (self.attr("commentlist").truthy())
                            {
                                returnlist = py_list({ py_tuple({ (((G<"email._parseaddr.SPACE">()).method("join", { plist }) + (S<" (">())) + (S<" ">()).method("join", { self.attr("commentlist") })) + (S<")">()), routeaddr }) });
                            }
                            else
                            {
                                returnlist = py_list({ py_tuple({ (G<"email._parseaddr.SPACE">()).method("join", { plist }), routeaddr }) });
                            }
                        }
                        else if (plist.truthy())
                        {
                            returnlist = py_list({ py_tuple({ (G<"email._parseaddr.SPACE">()).method("join", { self.attr("commentlist") }), plist[I<0>()] }) });
                        }
                        else if (self.attr("specials").contains(self.attr("field")[self.attr("pos")]))
                        {
                            {
                                py const target17 = self;
                                target17.set_attr("pos", inplace(NEOS_PY_ADD, target17.attr("pos"), I<1>()));
                            }
                        }
                        self.method("gotonext");
                        if (((self.attr("pos") < py{ self.attr("field").size() }) && (self.attr("field")[self.attr("pos")] == (S<",">()))))
                        {
                            {
                                py const target18 = self;
                                target18.set_attr("pos", inplace(NEOS_PY_ADD, target18.attr("pos"), I<1>()));
                            }
                        }
                        return returnlist;
                    }) },
                { "getrouteaddr", w.method("AddrlistClass", "getrouteaddr", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py adlist;
                        py expectroute;
                        if (self.attr("field")[self.attr("pos")] != (S<"<">()))
                        {
                            return none();
                        }
                        expectroute = boolean(false);
                        {
                            py const target19 = self;
                            target19.set_attr("pos", inplace(NEOS_PY_ADD, target19.attr("pos"), I<1>()));
                        }
                        self.method("gotonext");
                        adlist = S<"">();
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            if (expectroute.truthy())
                            {
                                self.method("getdomain");
                                expectroute = boolean(false);
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<">">()))
                            {
                                {
                                    py const target21 = self;
                                    target21.set_attr("pos", inplace(NEOS_PY_ADD, target21.attr("pos"), I<1>()));
                                }
                                break;
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"@">()))
                            {
                                {
                                    py const target22 = self;
                                    target22.set_attr("pos", inplace(NEOS_PY_ADD, target22.attr("pos"), I<1>()));
                                }
                                expectroute = boolean(true);
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<":">()))
                            {
                                {
                                    py const target23 = self;
                                    target23.set_attr("pos", inplace(NEOS_PY_ADD, target23.attr("pos"), I<1>()));
                                }
                            }
                            else
                            {
                                adlist = self.method("getaddrspec");
                                {
                                    py const target24 = self;
                                    target24.set_attr("pos", inplace(NEOS_PY_ADD, target24.attr("pos"), I<1>()));
                                }
                                break;
                            }
                            self.method("gotonext");
                        }
                        return adlist;
                    }) },
                { "getaddrspec", w.method("AddrlistClass", "getaddrspec", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py aslist;
                        py domain;
                        py preserve_ws;
                        py ws;
                        aslist = py_list();
                        self.method("gotonext");
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            preserve_ws = boolean(true);
                            if (self.attr("field")[self.attr("pos")] == (S<".">()))
                            {
                                if ((aslist.truthy() && (!aslist[I<-1>()].method("strip").truthy())))
                                {
                                    aslist.method("pop");
                                }
                                aslist.method("append", { S<".">() });
                                {
                                    py const target26 = self;
                                    target26.set_attr("pos", inplace(NEOS_PY_ADD, target26.attr("pos"), I<1>()));
                                }
                                preserve_ws = boolean(false);
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"\"">()))
                            {
                                aslist.method("append", { (S<"\"%s\"">()) % (G<"email._parseaddr.quote">())(self.method("getquote")) });
                            }
                            else if (self.attr("atomends").contains(self.attr("field")[self.attr("pos")]))
                            {
                                if ((aslist.truthy() && (!aslist[I<-1>()].method("strip").truthy())))
                                {
                                    aslist.method("pop");
                                }
                                break;
                            }
                            else
                            {
                                aslist.method("append", { self.method("getatom") });
                            }
                            ws = self.method("gotonext");
                            if ((preserve_ws.truthy() && ws.truthy()))
                            {
                                aslist.method("append", { ws });
                            }
                        }
                        if (((self.attr("pos") >= py{ self.attr("field").size() }) || (self.attr("field")[self.attr("pos")] != (S<"@">()))))
                        {
                            return (G<"email._parseaddr.EMPTYSTRING">()).method("join", { aslist });
                        }
                        aslist.method("append", { S<"@">() });
                        {
                            py const target27 = self;
                            target27.set_attr("pos", inplace(NEOS_PY_ADD, target27.attr("pos"), I<1>()));
                        }
                        self.method("gotonext");
                        domain = self.method("getdomain");
                        if (!domain.truthy())
                        {
                            // Invalid domain, return an empty address instead of returning a
                            // local part to denote failed parsing.
                            return G<"email._parseaddr.EMPTYSTRING">();
                        }
                        return (G<"email._parseaddr.EMPTYSTRING">()).method("join", { aslist }) + domain;
                    }) },
                { "getdomain", w.method("AddrlistClass", "getdomain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sdlist;
                        sdlist = py_list();
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            if (self.attr("LWS").contains(self.attr("field")[self.attr("pos")]))
                            {
                                {
                                    py const target29 = self;
                                    target29.set_attr("pos", inplace(NEOS_PY_ADD, target29.attr("pos"), I<1>()));
                                }
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"(">()))
                            {
                                self.attr("commentlist").method("append", { self.method("getcomment") });
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"[">()))
                            {
                                sdlist.method("append", { self.method("getdomainliteral") });
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<".">()))
                            {
                                {
                                    py const target30 = self;
                                    target30.set_attr("pos", inplace(NEOS_PY_ADD, target30.attr("pos"), I<1>()));
                                }
                                sdlist.method("append", { S<".">() });
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"@">()))
                            {
                                // bpo-34155: Don't parse domains with two `@` like
                                // `a@malicious.org@important.com`.
                                return G<"email._parseaddr.EMPTYSTRING">();
                            }
                            else if (self.attr("atomends").contains(self.attr("field")[self.attr("pos")]))
                            {
                                break;
                            }
                            else
                            {
                                sdlist.method("append", { self.method("getatom") });
                            }
                        }
                        return (G<"email._parseaddr.EMPTYSTRING">()).method("join", { sdlist });
                    }) },
                { "getdelimited", w.method("AddrlistClass", "getdelimited", { py_positional("beginchar"), py_positional("endchars"), py_positional("allowcomments", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py beginchar = a[1];
                        py endchars = a[2];
                        py allowcomments = a[3];
                        py quote;
                        py slist;
                        if (self.attr("field")[self.attr("pos")] != beginchar)
                        {
                            return S<"">();
                        }
                        slist = py_list({ S<"">() });
                        quote = boolean(false);
                        {
                            py const target31 = self;
                            target31.set_attr("pos", inplace(NEOS_PY_ADD, target31.attr("pos"), I<1>()));
                        }
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            if (quote.truthy())
                            {
                                slist.method("append", { self.attr("field")[self.attr("pos")] });
                                quote = boolean(false);
                            }
                            else if (endchars.contains(self.attr("field")[self.attr("pos")]))
                            {
                                {
                                    py const target33 = self;
                                    target33.set_attr("pos", inplace(NEOS_PY_ADD, target33.attr("pos"), I<1>()));
                                }
                                break;
                            }
                            else if ((allowcomments.truthy() && (self.attr("field")[self.attr("pos")] == (S<"(">()))))
                            {
                                slist.method("append", { self.method("getcomment") });
                                continue;
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"\\">()))
                            {
                                quote = boolean(true);
                            }
                            else
                            {
                                slist.method("append", { self.attr("field")[self.attr("pos")] });
                            }
                            {
                                py const target34 = self;
                                target34.set_attr("pos", inplace(NEOS_PY_ADD, target34.attr("pos"), I<1>()));
                            }
                        }
                        return (G<"email._parseaddr.EMPTYSTRING">()).method("join", { slist });
                    }) },
                { "getquote", w.method("AddrlistClass", "getquote", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("getdelimited", { S<"\"">(), S<"\"\r">(), boolean(false) });
                    }) },
                { "getcomment", w.method("AddrlistClass", "getcomment", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("getdelimited", { S<"(">(), S<")\r">(), boolean(true) });
                    }) },
                { "getdomainliteral", w.method("AddrlistClass", "getdomainliteral", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"[%s]">()) % self.method("getdelimited", { S<"[">(), S<"]\r">(), boolean(false) });
                    }) },
                { "getatom", w.method("AddrlistClass", "getatom", { py_positional("atomends", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py atomends = a[1];
                        py atomlist;
                        atomlist = py_list({ S<"">() });
                        if (atomends.is_none())
                        {
                            atomends = self.attr("atomends");
                        }
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            if (atomends.contains(self.attr("field")[self.attr("pos")]))
                            {
                                break;
                            }
                            else
                            {
                                atomlist.method("append", { self.attr("field")[self.attr("pos")] });
                            }
                            {
                                py const target36 = self;
                                target36.set_attr("pos", inplace(NEOS_PY_ADD, target36.attr("pos"), I<1>()));
                            }
                        }
                        return (G<"email._parseaddr.EMPTYSTRING">()).method("join", { atomlist });
                    }) },
                { "getphraselist", w.method("AddrlistClass", "getphraselist", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py plist;
                        plist = py_list();
                        while (self.attr("pos") < py{ self.attr("field").size() })
                        {
                            if (self.attr("FWS").contains(self.attr("field")[self.attr("pos")]))
                            {
                                {
                                    py const target38 = self;
                                    target38.set_attr("pos", inplace(NEOS_PY_ADD, target38.attr("pos"), I<1>()));
                                }
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"\"">()))
                            {
                                plist.method("append", { self.method("getquote") });
                            }
                            else if (self.attr("field")[self.attr("pos")] == (S<"(">()))
                            {
                                self.attr("commentlist").method("append", { self.method("getcomment") });
                            }
                            else if (self.attr("phraseends").contains(self.attr("field")[self.attr("pos")]))
                            {
                                break;
                            }
                            else
                            {
                                plist.method("append", { self.method("getatom", { self.attr("phraseends") }) });
                            }
                        }
                        return plist;
                    }) }, }));

            define("email._parseaddr", "AddressList", w.make_class("AddressList", { G<"email._parseaddr.AddrlistClass">() }, {
                { "__init__", w.method("AddressList", "__init__", { py_positional("field") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py field = a[1];
                        (G<"email._parseaddr.AddrlistClass">()).method("__init__", { self, field });
                        if (field.truthy())
                        {
                            self.set_attr("addresslist", self.method("getaddrlist"));
                        }
                        else
                        {
                            self.set_attr("addresslist", py_list());
                        }
                        return none();
                    }) },
                { "__len__", w.method("AddressList", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("addresslist").size() };
                    }) },
                { "__add__", w.method("AddressList", "__add__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py newaddr;
                        py x;
                        newaddr = (G<"email._parseaddr.AddressList">())(none());
                        newaddr.set_attr("addresslist", self.attr("addresslist")[B<"slice">()(none(), none(), none())]);
                        for (py const& item : iterate{ other.attr("addresslist") })
                        {
                            x = item;
                            if (!self.attr("addresslist").contains(x))
                            {
                                newaddr.attr("addresslist").method("append", { x });
                            }
                        }
                        return newaddr;
                    }) },
                { "__iadd__", w.method("AddressList", "__iadd__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py x;
                        for (py const& item : iterate{ other.attr("addresslist") })
                        {
                            x = item;
                            if (!self.attr("addresslist").contains(x))
                            {
                                self.attr("addresslist").method("append", { x });
                            }
                        }
                        return self;
                    }) },
                { "__sub__", w.method("AddressList", "__sub__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py newaddr;
                        py x;
                        newaddr = (G<"email._parseaddr.AddressList">())(none());
                        for (py const& item : iterate{ self.attr("addresslist") })
                        {
                            x = item;
                            if (!other.attr("addresslist").contains(x))
                            {
                                newaddr.attr("addresslist").method("append", { x });
                            }
                        }
                        return newaddr;
                    }) },
                { "__isub__", w.method("AddressList", "__isub__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py x;
                        for (py const& item : iterate{ other.attr("addresslist") })
                        {
                            x = item;
                            if (self.attr("addresslist").contains(x))
                            {
                                self.attr("addresslist").method("remove", { x });
                            }
                        }
                        return self;
                    }) },
                { "__getitem__", w.method("AddressList", "__getitem__", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        return self.attr("addresslist")[index];
                    }) }, }));
        }

        bool email__parseaddr_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email__parseaddr();
                }
                if (auto const doc = email_parseaddr_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email._parseaddr", email__parseaddr_module);
    }
}
