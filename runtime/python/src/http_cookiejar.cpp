/*
  http_cookiejar.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/http/cookiejar.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module http.cookiejar (CPython 3.13's Lib/http/cookiejar.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "http_cookiejar_docs.hpp"

namespace neos::python_runtime::detail::http_cookiejar_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "http.cookiejar.name"))
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
                py_raise("SystemError", std::string{ "http.cookiejar: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"http.cookiejar.name">()))
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
            if (modules_imported("http.cookiejar"))
                return py{ fn_import("http.cookiejar") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "http.cookiejar" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "http.cookiejar", http_cookiejar_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- http.cookiejar (Lib/http/cookiejar.py)

        void make_http_cookiejar()
        {
            static module_writer const& w = writer("http.cookiejar");
            define("http.cookiejar", "__all__", py_list({ S<"Cookie">(), S<"CookieJar">(), S<"CookiePolicy">(), S<"DefaultCookiePolicy">(), S<"FileCookieJar">(), S<"LWPCookieJar">(), S<"LoadError">(), S<"MozillaCookieJar">() }));
            define("http.cookiejar", "debug", boolean(false));
            define("http.cookiejar", "logger", none());
            define("http.cookiejar", "_debug", w.function("_debug", "_debug", {  }, [](arguments a) -> py
                {
                    py args = a[0];
                    py logging;
                    if (!(G<"http.cookiejar.debug">()).truthy())
                    {
                        return none();
                    }
                    if (!(G<"http.cookiejar.logger">()).truthy())
                    {
                        logging = module_of("logging");
                        set_global("http.cookiejar", "logger", logging.method("getLogger", { S<"http.cookiejar">() }));
                    }
                    return call_ex((G<"http.cookiejar.logger">()).attr("debug"), {  }, args, py{});
                }, true));

            define("http.cookiejar", "HTTPONLY_ATTR", S<"HTTPOnly">());
            define("http.cookiejar", "HTTPONLY_PREFIX", S<"#HttpOnly_">());
            define("http.cookiejar", "DEFAULT_HTTP_PORT", (B<"str">())(Mod<"http.client">().attr("HTTP_PORT")));
            define("http.cookiejar", "NETSCAPE_MAGIC_RGX", (Mod<"re">()).method("compile", { S<"#( Netscape)? HTTP Cookie File">(), (Mod<"re">().attr("IGNORECASE")) | (Mod<"re">().attr("ASCII")) }));
            define("http.cookiejar", "MISSING_FILENAME_TEXT", S<"a filename was not supplied (nor was the CookieJar instance initialised with one)">());
            define("http.cookiejar", "NETSCAPE_HEADER_TEXT", S<"# Netscape HTTP Cookie File\n# http://curl.haxx.se/rfc/cookie_spec.html\n# This is a generated file!  Do not edit.\n\n">());
            define("http.cookiejar", "_warn_unhandled_exception", w.function("_warn_unhandled_exception", "_warn_unhandled_exception", {  }, [](arguments a) -> py
                {
                    py f;
                    py io;
                    py msg;
                    py traceback;
                    py warnings;
                    io = module_of("io");
                    warnings = module_of("warnings");
                    traceback = module_of("traceback");
                    f = io.method("StringIO");
                    traceback.method("print_exc", { none(), f });
                    msg = f.method("getvalue");
                    warnings.method("warn", { (S<"http.cookiejar bug!\n%s">()) % msg }, { { "stacklevel", I<1>() } });
                    return none();
                }));

            // Date/time conversion
            // -----------------------------------------------------------------------------
            define("http.cookiejar", "EPOCH_YEAR", I<1970>());
            define("http.cookiejar", "_timegm", w.function("_timegm", "_timegm", { py_positional("tt") }, [](arguments a) -> py
                {
                    py tt = a[0];
                    py hour;
                    py mday;
                    py min_;
                    py month;
                    py sec;
                    py year;
                    auto const items1 = unpack(tt[B<"slice">()(none(), I<6>(), none())], 6);
                    year = items1[0];
                    month = items1[1];
                    mday = items1[2];
                    hour = items1[3];
                    min_ = items1[4];
                    sec = items1[5];
                    if (((year >= (G<"http.cookiejar.EPOCH_YEAR">())) && (((I<1>()) <= month) && (month <= (I<12>()))) && (((I<1>()) <= mday) && (mday <= (I<31>()))) && (((I<0>()) <= hour) && (hour <= (I<24>()))) && (((I<0>()) <= min_) && (min_ <= (I<59>()))) && (((I<0>()) <= sec) && (sec <= (I<61>())))))
                    {
                        return (Mod<"calendar">().attr("timegm"))(tt);
                    }
                    else
                    {
                        return none();
                    }
                    return none();
                }));

            define("http.cookiejar", "DAYS", py_list({ S<"Mon">(), S<"Tue">(), S<"Wed">(), S<"Thu">(), S<"Fri">(), S<"Sat">(), S<"Sun">() }));
            define("http.cookiejar", "MONTHS", py_list({ S<"Jan">(), S<"Feb">(), S<"Mar">(), S<"Apr">(), S<"May">(), S<"Jun">(), S<"Jul">(), S<"Aug">(), S<"Sep">(), S<"Oct">(), S<"Nov">(), S<"Dec">() }));
            define("http.cookiejar", "MONTHS_LOWER", [&]() -> py { py const comprehension2 = py_list(); py month; for (py const& item : iterate{ G<"http.cookiejar.MONTHS">() }) { month = item; comprehension2.method("append", { month.method("lower") }); } return comprehension2; }());
            define("http.cookiejar", "time2isoz", w.function("time2isoz", "time2isoz", { py_positional("t", none()) }, [](arguments a) -> py
                {
                    py t = a[0];
                    py dt;
                    if (t.is_none())
                    {
                        dt = (Mod<"datetime">().attr("datetime")).method("now", {  }, { { "tz", Mod<"datetime">().attr("UTC") } });
                    }
                    else
                    {
                        dt = (Mod<"datetime">().attr("datetime")).method("fromtimestamp", { t }, { { "tz", Mod<"datetime">().attr("UTC") } });
                    }
                    return (S<"%04d-%02d-%02d %02d:%02d:%02dZ">()) % py_tuple({ dt.attr("year"), dt.attr("month"), dt.attr("day"), dt.attr("hour"), dt.attr("minute"), dt.attr("second") });
                }));

            define("http.cookiejar", "time2netscape", w.function("time2netscape", "time2netscape", { py_positional("t", none()) }, [](arguments a) -> py
                {
                    py t = a[0];
                    py dt;
                    if (t.is_none())
                    {
                        dt = (Mod<"datetime">().attr("datetime")).method("now", {  }, { { "tz", Mod<"datetime">().attr("UTC") } });
                    }
                    else
                    {
                        dt = (Mod<"datetime">().attr("datetime")).method("fromtimestamp", { t }, { { "tz", Mod<"datetime">().attr("UTC") } });
                    }
                    return (S<"%s, %02d-%s-%04d %02d:%02d:%02d GMT">()) % py_tuple({ (G<"http.cookiejar.DAYS">())[dt.method("weekday")], dt.attr("day"), (G<"http.cookiejar.MONTHS">())[dt.attr("month") - (I<1>())], dt.attr("year"), dt.attr("hour"), dt.attr("minute"), dt.attr("second") });
                }));

            define("http.cookiejar", "UTC_ZONES", dict_of({ { S<"GMT">(), none() }, { S<"UTC">(), none() }, { S<"UT">(), none() }, { S<"Z">(), none() } }));
            define("http.cookiejar", "TIMEZONE_RE", (Mod<"re">()).method("compile", { S<"^([-+])?(\\d\\d?):?(\\d\\d)?$">(), Mod<"re">().attr("ASCII") }));
            define("http.cookiejar", "offset_from_tz_string", w.function("offset_from_tz_string", "offset_from_tz_string", { py_positional("tz") }, [](arguments a) -> py
                {
                    py tz = a[0];
                    py m;
                    py offset;
                    offset = none();
                    if ((G<"http.cookiejar.UTC_ZONES">()).contains(tz))
                    {
                        offset = I<0>();
                    }
                    else
                    {
                        m = (G<"http.cookiejar.TIMEZONE_RE">()).method("search", { tz });
                        if (m.truthy())
                        {
                            offset = (I<3600>()) * (B<"int">())(m.method("group", { I<2>() }));
                            if (m.method("group", { I<3>() }).truthy())
                            {
                                offset = offset + ((I<60>()) * (B<"int">())(m.method("group", { I<3>() })));
                            }
                            if (m.method("group", { I<1>() }) == (S<"-">()))
                            {
                                offset = -offset;
                            }
                        }
                    }
                    return offset;
                }));

            define("http.cookiejar", "_str2time", w.function("_str2time", "_str2time", { py_positional("day"), py_positional("mon"), py_positional("yr"), py_positional("hr"), py_positional("min"), py_positional("sec"), py_positional("tz") }, [](arguments a) -> py
                {
                    py day = a[0];
                    py mon = a[1];
                    py yr = a[2];
                    py hr = a[3];
                    py min_ = a[4];
                    py sec = a[5];
                    py tz = a[6];
                    py cur_yr;
                    py imon;
                    py m;
                    py offset;
                    py t;
                    py tmp;
                    yr = (B<"int">())(yr);
                    if (yr > (Mod<"datetime">().attr("MAXYEAR")))
                    {
                        return none();
                    }
                    // translate month name to number
                    // month numbers start with 1 (January)
                    try
                    {
                        mon = (G<"http.cookiejar.MONTHS_LOWER">()).method("index", { mon.method("lower") }) + (I<1>());
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler3;
                        if (matches(handler3.exception, B<"ValueError">()))
                        {
                            // maybe it's already a number
                            try
                            {
                                imon = (B<"int">())(mon);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler4;
                                if (matches(handler4.exception, B<"ValueError">()))
                                {
                                    return none();
                                }
                                else
                                    py_reraise(handler4.exception);
                            }
                            if (((I<1>()) <= imon) && (imon <= (I<12>())))
                            {
                                mon = imon;
                            }
                            else
                            {
                                return none();
                            }
                        }
                        else
                            py_reraise(handler3.exception);
                    }
                    // make sure clock elements are defined
                    if (hr.is_none())
                    {
                        hr = I<0>();
                    }
                    if (min_.is_none())
                    {
                        min_ = I<0>();
                    }
                    if (sec.is_none())
                    {
                        sec = I<0>();
                    }
                    day = (B<"int">())(day);
                    hr = (B<"int">())(hr);
                    min_ = (B<"int">())(min_);
                    sec = (B<"int">())(sec);
                    if (yr < (I<1000>()))
                    {
                        // find "obvious" year
                        cur_yr = (Mod<"time">().attr("localtime"))((Mod<"time">().attr("time"))())[I<0>()];
                        m = cur_yr % (I<100>());
                        tmp = yr;
                        yr = (yr + cur_yr) - m;
                        m = m - tmp;
                        if ((B<"abs">())(m) > (I<50>()))
                        {
                            if (m > (I<0>()))
                            {
                                yr = yr + (I<100>());
                            }
                            else
                            {
                                yr = yr - (I<100>());
                            }
                        }
                    }
                    // convert UTC time tuple to seconds since epoch (not timezone-adjusted)
                    t = (G<"http.cookiejar._timegm">())(py_tuple({ yr, mon, day, hr, min_, sec, tz }));
                    if (!t.is_none())
                    {
                        // adjust time using timezone string, to get absolute time since epoch
                        if (tz.is_none())
                        {
                            tz = S<"UTC">();
                        }
                        tz = tz.method("upper");
                        offset = (G<"http.cookiejar.offset_from_tz_string">())(tz);
                        if (offset.is_none())
                        {
                            return none();
                        }
                        t = t - offset;
                    }
                    return t;
                }));

            define("http.cookiejar", "STRICT_DATE_RE", (Mod<"re">()).method("compile", { S<"^[SMTWF][a-z][a-z], (\\d\\d) ([JFMASOND][a-z][a-z]) (\\d\\d\\d\\d) (\\d\\d):(\\d\\d):(\\d\\d) GMT$">(), Mod<"re">().attr("ASCII") }));
            define("http.cookiejar", "WEEKDAY_RE", (Mod<"re">()).method("compile", { S<"^(?:Sun|Mon|Tue|Wed|Thu|Fri|Sat)[a-z]*,?\\s*">(), (Mod<"re">().attr("I")) | (Mod<"re">().attr("ASCII")) }));
            define("http.cookiejar", "LOOSE_HTTP_DATE_RE", (Mod<"re">()).method("compile", { S<"^\n    (\\d\\d?)            # day\n       (?:\\s+|[-\\/])\n    (\\w+)              # month\n        (?:\\s+|[-\\/])\n    (\\d+)              # year\n    (?:\n          (?:\\s+|:)    # separator before clock\n       (\\d\\d?):(\\d\\d)  # hour:min\n       (?::(\\d\\d))?    # optional seconds\n    )?                 # optional clock\n       \\s*\n    (?:\n       ([-+]?\\d{2,4}|(?![APap][Mm]\\b)[A-Za-z]+) # timezone\n       \\s*\n    )?\n    (?:\n       \\(\\w+\\)         # ASCII representation of timezone in parens.\n       \\s*\n    )?$">(), (Mod<"re">().attr("X")) | (Mod<"re">().attr("ASCII")) }));
            define("http.cookiejar", "http2time", w.function("http2time", "http2time", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    py day;
                    py g;
                    py hr;
                    py m;
                    py min_;
                    py mon;
                    py sec;
                    py tt;
                    py tz;
                    py yr;
                    // fast exit for strictly conforming string
                    m = (G<"http.cookiejar.STRICT_DATE_RE">()).method("search", { text });
                    if (m.truthy())
                    {
                        g = m.method("groups");
                        mon = (G<"http.cookiejar.MONTHS_LOWER">()).method("index", { g[I<1>()].method("lower") }) + (I<1>());
                        tt = py_tuple({ (B<"int">())(g[I<2>()]), mon, (B<"int">())(g[I<0>()]), (B<"int">())(g[I<3>()]), (B<"int">())(g[I<4>()]), (B<"float">())(g[I<5>()]) });
                        return (G<"http.cookiejar._timegm">())(tt);
                    }
                    // No, we need some messy parsing...
                    // clean up
                    text = text.method("lstrip");
                    text = (G<"http.cookiejar.WEEKDAY_RE">()).method("sub", { S<"">(), text, I<1>() });
                    // tz is time zone specifier string
                    auto const items5 = unpack(py_list({ none() }) * (I<7>()), 7);
                    day = items5[0];
                    mon = items5[1];
                    yr = items5[2];
                    hr = items5[3];
                    min_ = items5[4];
                    sec = items5[5];
                    tz = items5[6];
                    // loose regexp parse
                    m = (G<"http.cookiejar.LOOSE_HTTP_DATE_RE">()).method("search", { text });
                    if (!m.is_none())
                    {
                        auto const items6 = unpack(m.method("groups"), 7);
                        day = items6[0];
                        mon = items6[1];
                        yr = items6[2];
                        hr = items6[3];
                        min_ = items6[4];
                        sec = items6[5];
                        tz = items6[6];
                    }
                    else
                    {
                        return none();
                    }
                    return (G<"http.cookiejar._str2time">())(day, mon, yr, hr, min_, sec, tz);
                }));

            define("http.cookiejar", "ISO_DATE_RE", (Mod<"re">()).method("compile", { S<"^\n    (\\d{4})              # year\n       [-\\/]?\n    (\\d\\d?)              # numerical month\n       [-\\/]?\n    (\\d\\d?)              # day\n   (?:\n         (?:\\s+|[-:Tt])  # separator before clock\n      (\\d\\d?):?(\\d\\d)    # hour:min\n      (?::?(\\d\\d(?:\\.\\d*)?))?  # optional seconds (and fractional)\n   )?                    # optional clock\n      \\s*\n   (?:\n      ([-+]?\\d\\d?:?(:?\\d\\d)?\n       |Z|z)             # timezone  (Z is \"zero meridian\", i.e. GMT)\n      \\s*\n   )?$">(), (Mod<"re">().attr("X")) | (Mod<"re">().attr("ASCII")) }));
            define("http.cookiejar", "iso2time", w.function("iso2time", "iso2time", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    py _;
                    py day;
                    py hr;
                    py m;
                    py min_;
                    py mon;
                    py sec;
                    py tz;
                    py yr;
                    // clean up
                    text = text.method("lstrip");
                    // tz is time zone specifier string
                    auto const items7 = unpack(py_list({ none() }) * (I<7>()), 7);
                    day = items7[0];
                    mon = items7[1];
                    yr = items7[2];
                    hr = items7[3];
                    min_ = items7[4];
                    sec = items7[5];
                    tz = items7[6];
                    // loose regexp parse
                    m = (G<"http.cookiejar.ISO_DATE_RE">()).method("search", { text });
                    if (!m.is_none())
                    {
                        // XXX there's an extra bit of the timezone I'm ignoring here: is
                        // this the right thing to do?
                        auto const items8 = unpack(m.method("groups"), 8);
                        yr = items8[0];
                        mon = items8[1];
                        day = items8[2];
                        hr = items8[3];
                        min_ = items8[4];
                        sec = items8[5];
                        tz = items8[6];
                        _ = items8[7];
                    }
                    else
                    {
                        return none();
                    }
                    return (G<"http.cookiejar._str2time">())(day, mon, yr, hr, min_, sec, tz);
                }));

            // Header parsing
            // -----------------------------------------------------------------------------
            define("http.cookiejar", "unmatched", w.function("unmatched", "unmatched", { py_positional("match") }, [](arguments a) -> py
                {
                    py match = a[0];
                    py end;
                    py start;
                    auto const items9 = unpack(match.method("span", { I<0>() }), 2);
                    start = items9[0];
                    end = items9[1];
                    return match.attr("string")[B<"slice">()(none(), start, none())] + match.attr("string")[B<"slice">()(end, none(), none())];
                }));

            define("http.cookiejar", "HEADER_TOKEN_RE", (Mod<"re">()).method("compile", { S<"^\\s*([^=\\s;,]+)">() }));
            define("http.cookiejar", "HEADER_QUOTED_VALUE_RE", (Mod<"re">()).method("compile", { S<"^\\s*=\\s*\\\"([^\\\"\\\\]*(?:\\\\.[^\\\"\\\\]*)*)\\\"">() }));
            define("http.cookiejar", "HEADER_VALUE_RE", (Mod<"re">()).method("compile", { S<"^\\s*=\\s*([^\\s;,]*)">() }));
            define("http.cookiejar", "HEADER_ESCAPE_RE", (Mod<"re">()).method("compile", { S<"\\\\(.)">() }));
            define("http.cookiejar", "split_header_words", w.function("split_header_words", "split_header_words", { py_positional("header_values") }, [](arguments a) -> py
                {
                    py header_values = a[0];
                    py m;
                    py name;
                    py non_junk;
                    py nr_junk_chars;
                    py orig_text;
                    py pairs;
                    py result;
                    py text;
                    py value;
                    if (isinstance(header_values, B<"str">()))
                        py_raise(B<"AssertionError">()());
                    result = py_list();
                    for (py const& item : iterate{ header_values })
                    {
                        text = item;
                        orig_text = text;
                        pairs = py_list();
                        while (text.truthy())
                        {
                            m = (G<"http.cookiejar.HEADER_TOKEN_RE">()).method("search", { text });
                            if (m.truthy())
                            {
                                text = (G<"http.cookiejar.unmatched">())(m);
                                name = m.method("group", { I<1>() });
                                m = (G<"http.cookiejar.HEADER_QUOTED_VALUE_RE">()).method("search", { text });
                                if (m.truthy())
                                {
                                    text = (G<"http.cookiejar.unmatched">())(m);
                                    value = m.method("group", { I<1>() });
                                    value = (G<"http.cookiejar.HEADER_ESCAPE_RE">()).method("sub", { S<"\\1">(), value });
                                }
                                else
                                {
                                    m = (G<"http.cookiejar.HEADER_VALUE_RE">()).method("search", { text });
                                    if (m.truthy())
                                    {
                                        text = (G<"http.cookiejar.unmatched">())(m);
                                        value = m.method("group", { I<1>() });
                                        value = value.method("rstrip");
                                    }
                                    else
                                    {
                                        // no value, a lone token
                                        value = none();
                                    }
                                }
                                pairs.method("append", { py_tuple({ name, value }) });
                            }
                            else if (text.method("lstrip").method("startswith", { S<",">() }).truthy())
                            {
                                // concatenated headers, as per RFC 2616 section 4.2
                                text = text.method("lstrip")[B<"slice">()(I<1>(), none(), none())];
                                if (pairs.truthy())
                                {
                                    result.method("append", { pairs });
                                }
                                pairs = py_list();
                            }
                            else
                            {
                                // skip junk
                                auto const items12 = unpack((Mod<"re">().attr("subn"))(S<"^[=\\s;]*">(), S<"">(), text), 2);
                                non_junk = items12[0];
                                nr_junk_chars = items12[1];
                                if (!(nr_junk_chars > (I<0>())))
                                    py_raise(B<"AssertionError">()((S<"split_header_words bug: '%s', '%s', %s">()) % py_tuple({ orig_text, text, pairs })));
                                text = non_junk;
                            }
                        }
                        if (pairs.truthy())
                        {
                            result.method("append", { pairs });
                        }
                    }
                    return result;
                }));

            define("http.cookiejar", "HEADER_JOIN_TOKEN_RE", (Mod<"re">()).method("compile", { S<"[!#$%&'*+\\-.^_`|~0-9A-Za-z]+">() }));
            define("http.cookiejar", "HEADER_JOIN_ESCAPE_RE", (Mod<"re">()).method("compile", { S<"([\\\"\\\\])">() }));
            define("http.cookiejar", "join_header_words", w.function("join_header_words", "join_header_words", { py_positional("lists") }, [](arguments a) -> py
                {
                    py lists = a[0];
                    py attr;
                    py headers;
                    py k;
                    py pairs;
                    py v;
                    headers = py_list();
                    for (py const& item : iterate{ lists })
                    {
                        pairs = item;
                        attr = py_list();
                        for (py const& item : iterate{ pairs })
                        {
                            auto const items15 = unpack(item, 2);
                            k = items15[0];
                            v = items15[1];
                            if (!v.is_none())
                            {
                                if (!(G<"http.cookiejar.HEADER_JOIN_TOKEN_RE">()).method("fullmatch", { v }).truthy())
                                {
                                    v = (G<"http.cookiejar.HEADER_JOIN_ESCAPE_RE">()).method("sub", { S<"\\\\\\1">(), v });
                                    v = (S<"\"%s\"">()) % v;
                                }
                                k = (S<"%s=%s">()) % py_tuple({ k, v });
                            }
                            attr.method("append", { k });
                        }
                        if (attr.truthy())
                        {
                            headers.method("append", { (S<"; ">()).method("join", { attr }) });
                        }
                    }
                    return (S<", ">()).method("join", { headers });
                }));

            define("http.cookiejar", "strip_quotes", w.function("strip_quotes", "strip_quotes", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    if (text.method("startswith", { S<"\"">() }).truthy())
                    {
                        text = text[B<"slice">()(I<1>(), none(), none())];
                    }
                    if (text.method("endswith", { S<"\"">() }).truthy())
                    {
                        text = text[B<"slice">()(none(), I<-1>(), none())];
                    }
                    return text;
                }));

            define("http.cookiejar", "parse_ns_headers", w.function("parse_ns_headers", "parse_ns_headers", { py_positional("ns_headers") }, [](arguments a) -> py
                {
                    py ns_headers = a[0];
                    py ii;
                    py key;
                    py known_attrs;
                    py lc;
                    py ns_header;
                    py pairs;
                    py param;
                    py result;
                    py sep;
                    py val;
                    py version_set;
                    known_attrs = py_tuple({ S<"expires">(), S<"domain">(), S<"path">(), S<"secure">(), S<"version">(), S<"port">(), S<"max-age">() });
                    // RFC 2109 attrs (may turn up in Netscape cookies, too)
                    result = py_list();
                    for (py const& item : iterate{ ns_headers })
                    {
                        ns_header = item;
                        pairs = py_list();
                        version_set = boolean(false);
                        // XXX: The following does not strictly adhere to RFCs in that empty
                        // names and values are legal (the former will only appear once and will
                        // be overwritten if multiple occurrences are present). This is
                        // mostly to deal with backwards compatibility.
                        for (py const& item : iterate{ (B<"enumerate">())(ns_header.method("split", { S<";">() })) })
                        {
                            auto const items18 = unpack(item, 2);
                            ii = items18[0];
                            param = items18[1];
                            param = param.method("strip");
                            auto const items19 = unpack(param.method("partition", { S<"=">() }), 3);
                            key = items19[0];
                            sep = items19[1];
                            val = items19[2];
                            key = key.method("strip");
                            if (!key.truthy())
                            {
                                if (ii == (I<0>()))
                                {
                                    break;
                                }
                                else
                                {
                                    continue;
                                }
                            }
                            // allow for a distinction between present and empty and missing
                            // altogether
                            val = (sep.truthy() ? val.method("strip") : none());
                            if (ii != (I<0>()))
                            {
                                lc = key.method("lower");
                                if (known_attrs.contains(lc))
                                {
                                    key = lc;
                                }
                                if (key == (S<"version">()))
                                {
                                    // This is an RFC 2109 cookie.
                                    if (!val.is_none())
                                    {
                                        val = (G<"http.cookiejar.strip_quotes">())(val);
                                    }
                                    version_set = boolean(true);
                                }
                                else if (key == (S<"expires">()))
                                {
                                    // convert expires date to seconds since epoch
                                    if (!val.is_none())
                                    {
                                        val = (G<"http.cookiejar.http2time">())((G<"http.cookiejar.strip_quotes">())(val));
                                    }
                                }
                            }
                            pairs.method("append", { py_tuple({ key, val }) });
                        }
                        if (pairs.truthy())
                        {
                            if (!version_set.truthy())
                            {
                                pairs.method("append", { py_tuple({ S<"version">(), S<"0">() }) });
                            }
                            result.method("append", { pairs });
                        }
                    }
                    return result;
                }));

            define("http.cookiejar", "IPV4_RE", (Mod<"re">()).method("compile", { S<"\\.\\d+$">(), Mod<"re">().attr("ASCII") }));
            define("http.cookiejar", "is_HDN", w.function("is_HDN", "is_HDN", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    // XXX
                    // This may well be wrong.  Which RFC is HDN defined in, if any (for
                    // the purposes of RFC 2965)?
                    // For the current implementation, what about IPv6?  Remember to look
                    // at other uses of IPV4_RE also, if change this.
                    if ((G<"http.cookiejar.IPV4_RE">()).method("search", { text }).truthy())
                    {
                        return boolean(false);
                    }
                    if (text == (S<"">()))
                    {
                        return boolean(false);
                    }
                    if (((text[I<0>()] == (S<".">())) || (text[I<-1>()] == (S<".">()))))
                    {
                        return boolean(false);
                    }
                    return boolean(true);
                }));

            define("http.cookiejar", "domain_match", w.function("domain_match", "domain_match", { py_positional("A"), py_positional("B") }, [](arguments a) -> py
                {
                    py A = a[0];
                    py B_ = a[1];
                    py i;
                    // Note that, if A or B are IP addresses, the only relevant part of the
                    // definition of the domain-match algorithm is the direct string-compare.
                    A = A.method("lower");
                    B_ = B_.method("lower");
                    if (A == B_)
                    {
                        return boolean(true);
                    }
                    if (!(G<"http.cookiejar.is_HDN">())(A).truthy())
                    {
                        return boolean(false);
                    }
                    i = A.method("rfind", { B_ });
                    if (((i == (I<-1>())) || (i == (I<0>()))))
                    {
                        // A does not have form NB, or N is the empty string
                        return boolean(false);
                    }
                    if (!B_.method("startswith", { S<".">() }).truthy())
                    {
                        return boolean(false);
                    }
                    if (!(G<"http.cookiejar.is_HDN">())(B_[B<"slice">()(I<1>(), none(), none())]).truthy())
                    {
                        return boolean(false);
                    }
                    return boolean(true);
                }));

            define("http.cookiejar", "liberal_is_HDN", w.function("liberal_is_HDN", "liberal_is_HDN", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    if ((G<"http.cookiejar.IPV4_RE">()).method("search", { text }).truthy())
                    {
                        return boolean(false);
                    }
                    return boolean(true);
                }));

            define("http.cookiejar", "user_domain_match", w.function("user_domain_match", "user_domain_match", { py_positional("A"), py_positional("B") }, [](arguments a) -> py
                {
                    py A = a[0];
                    py B_ = a[1];
                    py initial_dot;
                    A = A.method("lower");
                    B_ = B_.method("lower");
                    if (!([&]() -> py { py const t = (G<"http.cookiejar.liberal_is_HDN">())(A); return !t.truthy() ? t : (G<"http.cookiejar.liberal_is_HDN">())(B_); }()).truthy())
                    {
                        if (A == B_)
                        {
                            // equal IP addresses
                            return boolean(true);
                        }
                        return boolean(false);
                    }
                    initial_dot = B_.method("startswith", { S<".">() });
                    if ((initial_dot.truthy() && A.method("endswith", { B_ }).truthy()))
                    {
                        return boolean(true);
                    }
                    if (((!initial_dot.truthy()) && (A == B_)))
                    {
                        return boolean(true);
                    }
                    return boolean(false);
                }));

            define("http.cookiejar", "cut_port_re", (Mod<"re">()).method("compile", { S<":\\d+$">(), Mod<"re">().attr("ASCII") }));
            define("http.cookiejar", "request_host", w.function("request_host", "request_host", { py_positional("request") }, [](arguments a) -> py
                {
                    py request = a[0];
                    py host;
                    py url;
                    url = request.method("get_full_url");
                    host = (Mod<"urllib.parse">().attr("urlparse"))(url)[I<1>()];
                    if (host == (S<"">()))
                    {
                        host = request.method("get_header", { S<"Host">(), S<"">() });
                    }
                    // remove port, if present
                    host = (G<"http.cookiejar.cut_port_re">()).method("sub", { S<"">(), host, I<1>() });
                    return host.method("lower");
                }));

            define("http.cookiejar", "eff_request_host", w.function("eff_request_host", "eff_request_host", { py_positional("request") }, [](arguments a) -> py
                {
                    py request = a[0];
                    py erhn;
                    py req_host;
                    py const value20 = (G<"http.cookiejar.request_host">())(request);
                    erhn = value20;
                    req_host = value20;
                    if (!req_host.contains((S<".">())))
                    {
                        erhn = req_host + (S<".local">());
                    }
                    return py_tuple({ req_host, erhn });
                }));

            define("http.cookiejar", "request_path", w.function("request_path", "request_path", { py_positional("request") }, [](arguments a) -> py
                {
                    py request = a[0];
                    py parts;
                    py path;
                    py url;
                    url = request.method("get_full_url");
                    parts = (Mod<"urllib.parse">().attr("urlsplit"))(url);
                    path = (G<"http.cookiejar.escape_path">())(parts.attr("path"));
                    if (!path.method("startswith", { S<"/">() }).truthy())
                    {
                        // fix bad RFC 2396 absoluteURI
                        path = (S<"/">()) + path;
                    }
                    return path;
                }));

            define("http.cookiejar", "request_port", w.function("request_port", "request_port", { py_positional("request") }, [](arguments a) -> py
                {
                    py request = a[0];
                    py host;
                    py i;
                    py port;
                    host = request.attr("host");
                    i = host.method("find", { S<":">() });
                    if (i >= (I<0>()))
                    {
                        port = host[B<"slice">()(i + (I<1>()), none(), none())];
                        try
                        {
                            (B<"int">())(port);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler21;
                            if (matches(handler21.exception, B<"ValueError">()))
                            {
                                (G<"http.cookiejar._debug">())(S<"nonnumeric port: '%s'">(), port);
                                return none();
                            }
                            else
                                py_reraise(handler21.exception);
                        }
                    }
                    else
                    {
                        port = G<"http.cookiejar.DEFAULT_HTTP_PORT">();
                    }
                    return port;
                }));

            // Characters in addition to A-Z, a-z, 0-9, '_', '.', and '-' that don't
            // need to be escaped to form a valid HTTP URL (RFCs 2396 and 1738).
            define("http.cookiejar", "HTTP_PATH_SAFE", S<"%/;:@&=+$,!~*'()">());
            define("http.cookiejar", "ESCAPED_CHAR_RE", (Mod<"re">()).method("compile", { S<"%([0-9a-fA-F][0-9a-fA-F])">() }));
            define("http.cookiejar", "uppercase_escaped_char", w.function("uppercase_escaped_char", "uppercase_escaped_char", { py_positional("match") }, [](arguments a) -> py
                {
                    py match = a[0];
                    return (S<"%%%s">()) % match.method("group", { I<1>() }).method("upper");
                }));

            define("http.cookiejar", "escape_path", w.function("escape_path", "escape_path", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    // There's no knowing what character encoding was used to create URLs
                    // containing %-escapes, but since we have to pick one to escape invalid
                    // path characters, we pick UTF-8, as recommended in the HTML 4.0
                    // specification:
                    // http://www.w3.org/TR/REC-html40/appendix/notes.html#h-B.2.1
                    // And here, kind of: draft-fielding-uri-rfc2396bis-03
                    // (And in draft IRI specification: draft-duerst-iri-05)
                    // (And here, for new URI schemes: RFC 2718)
                    path = (Mod<"urllib.parse">().attr("quote"))(path, G<"http.cookiejar.HTTP_PATH_SAFE">());
                    path = (G<"http.cookiejar.ESCAPED_CHAR_RE">()).method("sub", { G<"http.cookiejar.uppercase_escaped_char">(), path });
                    return path;
                }));

            define("http.cookiejar", "reach", w.function("reach", "reach", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    py b;
                    py i;
                    i = h.method("find", { S<".">() });
                    if (i >= (I<0>()))
                    {
                        // a = h[:i]  # this line is only here to show what a is
                        b = h[B<"slice">()(i + (I<1>()), none(), none())];
                        i = b.method("find", { S<".">() });
                        if (((G<"http.cookiejar.is_HDN">())(h).truthy() && ([&]() -> py { py const t = boolean(i >= (I<0>())); return t.truthy() ? t : boolean(b == (S<"local">())); }()).truthy()))
                        {
                            return (S<".">()) + b;
                        }
                    }
                    return h;
                }));

            define("http.cookiejar", "is_third_party", w.function("is_third_party", "is_third_party", { py_positional("request") }, [](arguments a) -> py
                {
                    py request = a[0];
                    py req_host;
                    req_host = (G<"http.cookiejar.request_host">())(request);
                    if (!(G<"http.cookiejar.domain_match">())(req_host, (G<"http.cookiejar.reach">())(request.attr("origin_req_host"))).truthy())
                    {
                        return boolean(true);
                    }
                    else
                    {
                        return boolean(false);
                    }
                    return none();
                }));

            define("http.cookiejar", "Cookie", w.make_class("Cookie", {  }, {
                { "__init__", w.method("Cookie", "__init__", { py_positional("version"), py_positional("name"), py_positional("value"), py_positional("port"), py_positional("port_specified"), py_positional("domain"), py_positional("domain_specified"), py_positional("domain_initial_dot"), py_positional("path"), py_positional("path_specified"), py_positional("secure"), py_positional("expires"), py_positional("discard"), py_positional("comment"), py_positional("comment_url"), py_positional("rest"), py_positional("rfc2109", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py version = a[1];
                        py name = a[2];
                        py value = a[3];
                        py port = a[4];
                        py port_specified = a[5];
                        py domain = a[6];
                        py domain_specified = a[7];
                        py domain_initial_dot = a[8];
                        py path = a[9];
                        py path_specified = a[10];
                        py secure = a[11];
                        py expires = a[12];
                        py discard = a[13];
                        py comment = a[14];
                        py comment_url = a[15];
                        py rest = a[16];
                        py rfc2109 = a[17];
                        if (!version.is_none())
                        {
                            version = (B<"int">())(version);
                        }
                        if (!expires.is_none())
                        {
                            expires = (B<"int">())((B<"float">())(expires));
                        }
                        if ((port.is_none() && port_specified.is(boolean(true))))
                        {
                            py_raise((B<"ValueError">())(S<"if port is None, port_specified must be false">()));
                        }
                        self.set_attr("version", version);
                        self.set_attr("name", name);
                        self.set_attr("value", value);
                        self.set_attr("port", port);
                        self.set_attr("port_specified", port_specified);
                        // normalise case, as per RFC 2965 section 3.3.3
                        self.set_attr("domain", domain.method("lower"));
                        self.set_attr("domain_specified", domain_specified);
                        // Sigh.  We need to know whether the domain given in the
                        // cookie-attribute had an initial dot, in order to follow RFC 2965
                        // (as clarified in draft errata).  Needed for the returned $Domain
                        // value.
                        self.set_attr("domain_initial_dot", domain_initial_dot);
                        self.set_attr("path", path);
                        self.set_attr("path_specified", path_specified);
                        self.set_attr("secure", secure);
                        self.set_attr("expires", expires);
                        self.set_attr("discard", discard);
                        self.set_attr("comment", comment);
                        self.set_attr("comment_url", comment_url);
                        self.set_attr("rfc2109", rfc2109);
                        self.set_attr("_rest", (Mod<"copy">().attr("copy"))(rest));
                        return none();
                    }) },
                { "has_nonstandard_attr", w.method("Cookie", "has_nonstandard_attr", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return boolean(self.attr("_rest").contains(name));
                    }) },
                { "get_nonstandard_attr", w.method("Cookie", "get_nonstandard_attr", { py_positional("name"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py default_ = a[2];
                        return self.attr("_rest").method("get", { name, default_ });
                    }) },
                { "set_nonstandard_attr", w.method("Cookie", "set_nonstandard_attr", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        self.attr("_rest").set_item(name, value);
                        return none();
                    }) },
                { "is_expired", w.method("Cookie", "is_expired", { py_positional("now", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py now = a[1];
                        if (now.is_none())
                        {
                            now = (Mod<"time">().attr("time"))();
                        }
                        if (((!self.attr("expires").is_none()) && (self.attr("expires") <= now)))
                        {
                            return boolean(true);
                        }
                        return boolean(false);
                    }) },
                { "__str__", w.method("Cookie", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py limit;
                        py namevalue;
                        py p;
                        if (self.attr("port").is_none())
                        {
                            p = S<"">();
                        }
                        else
                        {
                            p = (S<":">()) + self.attr("port");
                        }
                        limit = (self.attr("domain") + p) + self.attr("path");
                        if (!self.attr("value").is_none())
                        {
                            namevalue = (S<"%s=%s">()) % py_tuple({ self.attr("name"), self.attr("value") });
                        }
                        else
                        {
                            namevalue = self.attr("name");
                        }
                        return (S<"<Cookie %s for %s>">()) % py_tuple({ namevalue, limit });
                    }) },
                { "__repr__", w.method("Cookie", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        py attr;
                        py name;
                        args = py_list();
                        for (py const& item : iterate{ py_tuple({ S<"version">(), S<"name">(), S<"value">(), S<"port">(), S<"port_specified">(), S<"domain">(), S<"domain_specified">(), S<"domain_initial_dot">(), S<"path">(), S<"path_specified">(), S<"secure">(), S<"expires">(), S<"discard">(), S<"comment">(), S<"comment_url">() }) })
                        {
                            name = item;
                            attr = (B<"getattr">())(self, name);
                            args.method("append", { (S<"%s=%s">()) % py_tuple({ name, S(attr.repr()) }) });
                        }
                        args.method("append", { (S<"rest=%s">()) % S(self.attr("_rest").repr()) });
                        args.method("append", { (S<"rfc2109=%s">()) % S(self.attr("rfc2109").repr()) });
                        return (S<"%s(%s)">()) % py_tuple({ self.attr("__class__").attr("__name__"), (S<", ">()).method("join", { args }) });
                    }) }, }));

            define("http.cookiejar", "CookiePolicy", w.make_class("CookiePolicy", {  }, {
                { "set_ok", w.method("CookiePolicy", "set_ok", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "return_ok", w.method("CookiePolicy", "return_ok", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "domain_return_ok", w.method("CookiePolicy", "domain_return_ok", { py_positional("domain"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py domain = a[1];
                        py request = a[2];
                        return boolean(true);
                    }) },
                { "path_return_ok", w.method("CookiePolicy", "path_return_ok", { py_positional("path"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py request = a[2];
                        return boolean(true);
                    }) }, }));

            define("http.cookiejar", "DefaultCookiePolicy", w.make_class("DefaultCookiePolicy", { G<"http.cookiejar.CookiePolicy">() }, {
                { "DomainStrictNoDots", I<1>() },
                { "DomainStrictNonDomain", I<2>() },
                { "DomainRFC2965Match", I<4>() },
                { "DomainLiberal", I<0>() },
                { "DomainStrict", (I<1>()) | (I<2>()) },
                { "__init__", w.method("DefaultCookiePolicy", "__init__", { py_positional("blocked_domains", none()), py_positional("allowed_domains", none()), py_positional("netscape", boolean(true)), py_positional("rfc2965", boolean(false)), py_positional("rfc2109_as_netscape", none()), py_positional("hide_cookie2", boolean(false)), py_positional("strict_domain", boolean(false)), py_positional("strict_rfc2965_unverifiable", boolean(true)), py_positional("strict_ns_unverifiable", boolean(false)), py_positional("strict_ns_domain", (I<0>())), py_positional("strict_ns_set_initial_dollar", boolean(false)), py_positional("strict_ns_set_path", boolean(false)), py_positional("secure_protocols", py_tuple({ S<"https">(), S<"wss">() })) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py blocked_domains = a[1];
                        py allowed_domains = a[2];
                        py netscape = a[3];
                        py rfc2965 = a[4];
                        py rfc2109_as_netscape = a[5];
                        py hide_cookie2 = a[6];
                        py strict_domain = a[7];
                        py strict_rfc2965_unverifiable = a[8];
                        py strict_ns_unverifiable = a[9];
                        py strict_ns_domain = a[10];
                        py strict_ns_set_initial_dollar = a[11];
                        py strict_ns_set_path = a[12];
                        py secure_protocols = a[13];
                        self.set_attr("netscape", netscape);
                        self.set_attr("rfc2965", rfc2965);
                        self.set_attr("rfc2109_as_netscape", rfc2109_as_netscape);
                        self.set_attr("hide_cookie2", hide_cookie2);
                        self.set_attr("strict_domain", strict_domain);
                        self.set_attr("strict_rfc2965_unverifiable", strict_rfc2965_unverifiable);
                        self.set_attr("strict_ns_unverifiable", strict_ns_unverifiable);
                        self.set_attr("strict_ns_domain", strict_ns_domain);
                        self.set_attr("strict_ns_set_initial_dollar", strict_ns_set_initial_dollar);
                        self.set_attr("strict_ns_set_path", strict_ns_set_path);
                        self.set_attr("secure_protocols", secure_protocols);
                        if (!blocked_domains.is_none())
                        {
                            self.set_attr("_blocked_domains", (B<"tuple">())(blocked_domains));
                        }
                        else
                        {
                            self.set_attr("_blocked_domains", py_tuple());
                        }
                        if (!allowed_domains.is_none())
                        {
                            allowed_domains = (B<"tuple">())(allowed_domains);
                        }
                        self.set_attr("_allowed_domains", allowed_domains);
                        return none();
                    }) },
                { "blocked_domains", w.method("DefaultCookiePolicy", "blocked_domains", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_blocked_domains");
                    }) },
                { "set_blocked_domains", w.method("DefaultCookiePolicy", "set_blocked_domains", { py_positional("blocked_domains") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py blocked_domains = a[1];
                        self.set_attr("_blocked_domains", (B<"tuple">())(blocked_domains));
                        return none();
                    }) },
                { "is_blocked", w.method("DefaultCookiePolicy", "is_blocked", { py_positional("domain") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py domain = a[1];
                        py blocked_domain;
                        for (py const& item : iterate{ self.attr("_blocked_domains") })
                        {
                            blocked_domain = item;
                            if ((G<"http.cookiejar.user_domain_match">())(domain, blocked_domain).truthy())
                            {
                                return boolean(true);
                            }
                        }
                        return boolean(false);
                    }) },
                { "allowed_domains", w.method("DefaultCookiePolicy", "allowed_domains", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_allowed_domains");
                    }) },
                { "set_allowed_domains", w.method("DefaultCookiePolicy", "set_allowed_domains", { py_positional("allowed_domains") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py allowed_domains = a[1];
                        if (!allowed_domains.is_none())
                        {
                            allowed_domains = (B<"tuple">())(allowed_domains);
                        }
                        self.set_attr("_allowed_domains", allowed_domains);
                        return none();
                    }) },
                { "is_not_allowed", w.method("DefaultCookiePolicy", "is_not_allowed", { py_positional("domain") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py domain = a[1];
                        py allowed_domain;
                        if (self.attr("_allowed_domains").is_none())
                        {
                            return boolean(false);
                        }
                        for (py const& item : iterate{ self.attr("_allowed_domains") })
                        {
                            allowed_domain = item;
                            if ((G<"http.cookiejar.user_domain_match">())(domain, allowed_domain).truthy())
                            {
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "set_ok", w.method("DefaultCookiePolicy", "set_ok", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py fn;
                        py fn_name;
                        py n;
                        (G<"http.cookiejar._debug">())(S<" - checking cookie %s=%s">(), cookie.attr("name"), cookie.attr("value"));
                        if (cookie.attr("name").is_none())
                            py_raise(B<"AssertionError">()());
                        for (py const& item : iterate{ py_tuple({ S<"version">(), S<"verifiability">(), S<"name">(), S<"path">(), S<"domain">(), S<"port">() }) })
                        {
                            n = item;
                            fn_name = (S<"set_ok_">()) + n;
                            fn = (B<"getattr">())(self, fn_name);
                            if (!fn(cookie, request).truthy())
                            {
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "set_ok_version", w.method("DefaultCookiePolicy", "set_ok_version", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        if (cookie.attr("version").is_none())
                        {
                            // Version is always set to 0 by parse_ns_headers if it's a Netscape
                            // cookie, so this must be an invalid RFC 2965 cookie.
                            (G<"http.cookiejar._debug">())(S<"   Set-Cookie2 without version attribute (%s=%s)">(), cookie.attr("name"), cookie.attr("value"));
                            return boolean(false);
                        }
                        if (((cookie.attr("version") > (I<0>())) && (!self.attr("rfc2965").truthy())))
                        {
                            (G<"http.cookiejar._debug">())(S<"   RFC 2965 cookies are switched off">());
                            return boolean(false);
                        }
                        else if (((cookie.attr("version") == (I<0>())) && (!self.attr("netscape").truthy())))
                        {
                            (G<"http.cookiejar._debug">())(S<"   Netscape cookies are switched off">());
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "set_ok_verifiability", w.method("DefaultCookiePolicy", "set_ok_verifiability", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        if ((request.attr("unverifiable").truthy() && (G<"http.cookiejar.is_third_party">())(request).truthy()))
                        {
                            if (((cookie.attr("version") > (I<0>())) && self.attr("strict_rfc2965_unverifiable").truthy()))
                            {
                                (G<"http.cookiejar._debug">())(S<"   third-party RFC 2965 cookie during unverifiable transaction">());
                                return boolean(false);
                            }
                            else if (((cookie.attr("version") == (I<0>())) && self.attr("strict_ns_unverifiable").truthy()))
                            {
                                (G<"http.cookiejar._debug">())(S<"   third-party Netscape cookie during unverifiable transaction">());
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "set_ok_name", w.method("DefaultCookiePolicy", "set_ok_name", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        if (((cookie.attr("version") == (I<0>())) && self.attr("strict_ns_set_initial_dollar").truthy() && cookie.attr("name").method("startswith", { S<"$">() }).truthy()))
                        {
                            (G<"http.cookiejar._debug">())(S<"   illegal name (starts with '$'): '%s'">(), cookie.attr("name"));
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "set_ok_path", w.method("DefaultCookiePolicy", "set_ok_path", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py req_path;
                        if (cookie.attr("path_specified").truthy())
                        {
                            req_path = (G<"http.cookiejar.request_path">())(request);
                            if ((([&]() -> py { py const t = boolean(cookie.attr("version") > (I<0>())); return t.truthy() ? t : [&]() -> py { py const t = boolean(cookie.attr("version") == (I<0>())); return !t.truthy() ? t : self.attr("strict_ns_set_path"); }(); }()).truthy() && (!self.method("path_return_ok", { cookie.attr("path"), request }).truthy())))
                            {
                                (G<"http.cookiejar._debug">())(S<"   path attribute %s is not a prefix of request path %s">(), cookie.attr("path"), req_path);
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "set_ok_domain", w.method("DefaultCookiePolicy", "set_ok_domain", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py domain;
                        py embedded_dots;
                        py erhn;
                        py host_prefix;
                        py i;
                        py j;
                        py req_host;
                        py sld;
                        py tld;
                        py undotted_domain;
                        if (self.method("is_blocked", { cookie.attr("domain") }).truthy())
                        {
                            (G<"http.cookiejar._debug">())(S<"   domain %s is in user block-list">(), cookie.attr("domain"));
                            return boolean(false);
                        }
                        if (self.method("is_not_allowed", { cookie.attr("domain") }).truthy())
                        {
                            (G<"http.cookiejar._debug">())(S<"   domain %s is not in user allow-list">(), cookie.attr("domain"));
                            return boolean(false);
                        }
                        if (cookie.attr("domain_specified").truthy())
                        {
                            auto const items26 = unpack((G<"http.cookiejar.eff_request_host">())(request), 2);
                            req_host = items26[0];
                            erhn = items26[1];
                            domain = cookie.attr("domain");
                            if ((self.attr("strict_domain").truthy() && (domain.method("count", { S<".">() }) >= (I<2>()))))
                            {
                                // XXX This should probably be compared with the Konqueror
                                // (kcookiejar.cpp) and Mozilla implementations, but it's a
                                // losing battle.
                                i = domain.method("rfind", { S<".">() });
                                j = domain.method("rfind", { S<".">(), I<0>(), i });
                                if (j == (I<0>()))
                                {
                                    tld = domain[B<"slice">()(i + (I<1>()), none(), none())];
                                    sld = domain[B<"slice">()(j + (I<1>()), i, none())];
                                    if ((py_tuple({ S<"co">(), S<"ac">(), S<"com">(), S<"edu">(), S<"org">(), S<"net">(), S<"gov">(), S<"mil">(), S<"int">(), S<"aero">(), S<"biz">(), S<"cat">(), S<"coop">(), S<"info">(), S<"jobs">(), S<"mobi">(), S<"museum">(), S<"name">(), S<"pro">(), S<"travel">(), S<"eu">() }).contains(sld.method("lower")) && (py{ tld.size() } == (I<2>()))))
                                    {
                                        // domain like .co.uk
                                        (G<"http.cookiejar._debug">())(S<"   country-code second level domain %s">(), domain);
                                        return boolean(false);
                                    }
                                }
                            }
                            if (domain.method("startswith", { S<".">() }).truthy())
                            {
                                undotted_domain = domain[B<"slice">()(I<1>(), none(), none())];
                            }
                            else
                            {
                                undotted_domain = domain;
                            }
                            embedded_dots = boolean(undotted_domain.method("find", { S<".">() }) >= (I<0>()));
                            if (((!embedded_dots.truthy()) && (!erhn.method("endswith", { S<".local">() }).truthy())))
                            {
                                (G<"http.cookiejar._debug">())(S<"   non-local domain %s contains no embedded dot">(), domain);
                                return boolean(false);
                            }
                            if (cookie.attr("version") == (I<0>()))
                            {
                                if (((!([&]() -> py { py const t = erhn.method("endswith", { domain }); return t.truthy() ? t : erhn.method("endswith", { S(undotted_domain.str() + ".local") }); }()).truthy()) && ([&]() -> py { py const t = boolean(!erhn.method("startswith", { S<".">() }).truthy()); return !t.truthy() ? t : boolean(!((S<".">()) + erhn).method("endswith", { domain }).truthy()); }()).truthy()))
                                {
                                    (G<"http.cookiejar._debug">())(S<"   effective request-host %s (even with added initial dot) does not end with %s">(), erhn, domain);
                                    return boolean(false);
                                }
                            }
                            if (((cookie.attr("version") > (I<0>())) || (self.attr("strict_ns_domain") & self.attr("DomainRFC2965Match")).truthy()))
                            {
                                if (!(G<"http.cookiejar.domain_match">())(erhn, domain).truthy())
                                {
                                    (G<"http.cookiejar._debug">())(S<"   effective request-host %s does not domain-match %s">(), erhn, domain);
                                    return boolean(false);
                                }
                            }
                            if (((cookie.attr("version") > (I<0>())) || (self.attr("strict_ns_domain") & self.attr("DomainStrictNoDots")).truthy()))
                            {
                                host_prefix = req_host[B<"slice">()(none(), -py{ domain.size() }, none())];
                                if (((host_prefix.method("find", { S<".">() }) >= (I<0>())) && (!(G<"http.cookiejar.IPV4_RE">()).method("search", { req_host }).truthy())))
                                {
                                    (G<"http.cookiejar._debug">())(S<"   host prefix %s for domain %s contains a dot">(), host_prefix, domain);
                                    return boolean(false);
                                }
                            }
                        }
                        return boolean(true);
                    }) },
                { "set_ok_port", w.method("DefaultCookiePolicy", "set_ok_port", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py p;
                        py req_port;
                        if (cookie.attr("port_specified").truthy())
                        {
                            req_port = (G<"http.cookiejar.request_port">())(request);
                            if (req_port.is_none())
                            {
                                req_port = S<"80">();
                            }
                            else
                            {
                                req_port = (B<"str">())(req_port);
                            }
                            bool broke27 = false;
                            for (py const& item : iterate{ cookie.attr("port").method("split", { S<",">() }) })
                            {
                                p = item;
                                try
                                {
                                    (B<"int">())(p);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler28;
                                    if (matches(handler28.exception, B<"ValueError">()))
                                    {
                                        (G<"http.cookiejar._debug">())(S<"   bad port %s (not numeric)">(), p);
                                        return boolean(false);
                                    }
                                    else
                                        py_reraise(handler28.exception);
                                }
                                if (p == req_port)
                                {
                                    broke27 = true;
                                    break;
                                }
                            }
                            if (!broke27)
                            {
                                (G<"http.cookiejar._debug">())(S<"   request port (%s) not found in %s">(), req_port, cookie.attr("port"));
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "return_ok", w.method("DefaultCookiePolicy", "return_ok", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py fn;
                        py fn_name;
                        py n;
                        // Path has already been checked by .path_return_ok(), and domain
                        // blocking done by .domain_return_ok().
                        (G<"http.cookiejar._debug">())(S<" - checking cookie %s=%s">(), cookie.attr("name"), cookie.attr("value"));
                        for (py const& item : iterate{ py_tuple({ S<"version">(), S<"verifiability">(), S<"secure">(), S<"expires">(), S<"port">(), S<"domain">() }) })
                        {
                            n = item;
                            fn_name = (S<"return_ok_">()) + n;
                            fn = (B<"getattr">())(self, fn_name);
                            if (!fn(cookie, request).truthy())
                            {
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "return_ok_version", w.method("DefaultCookiePolicy", "return_ok_version", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        if (((cookie.attr("version") > (I<0>())) && (!self.attr("rfc2965").truthy())))
                        {
                            (G<"http.cookiejar._debug">())(S<"   RFC 2965 cookies are switched off">());
                            return boolean(false);
                        }
                        else if (((cookie.attr("version") == (I<0>())) && (!self.attr("netscape").truthy())))
                        {
                            (G<"http.cookiejar._debug">())(S<"   Netscape cookies are switched off">());
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "return_ok_verifiability", w.method("DefaultCookiePolicy", "return_ok_verifiability", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        if ((request.attr("unverifiable").truthy() && (G<"http.cookiejar.is_third_party">())(request).truthy()))
                        {
                            if (((cookie.attr("version") > (I<0>())) && self.attr("strict_rfc2965_unverifiable").truthy()))
                            {
                                (G<"http.cookiejar._debug">())(S<"   third-party RFC 2965 cookie during unverifiable transaction">());
                                return boolean(false);
                            }
                            else if (((cookie.attr("version") == (I<0>())) && self.attr("strict_ns_unverifiable").truthy()))
                            {
                                (G<"http.cookiejar._debug">())(S<"   third-party Netscape cookie during unverifiable transaction">());
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "return_ok_secure", w.method("DefaultCookiePolicy", "return_ok_secure", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        if ((cookie.attr("secure").truthy() && (!self.attr("secure_protocols").contains(request.attr("type")))))
                        {
                            (G<"http.cookiejar._debug">())(S<"   secure cookie with non-secure request">());
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "return_ok_expires", w.method("DefaultCookiePolicy", "return_ok_expires", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        if (cookie.method("is_expired", { self.attr("_now") }).truthy())
                        {
                            (G<"http.cookiejar._debug">())(S<"   cookie expired">());
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "return_ok_port", w.method("DefaultCookiePolicy", "return_ok_port", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py p;
                        py req_port;
                        if (cookie.attr("port").truthy())
                        {
                            req_port = (G<"http.cookiejar.request_port">())(request);
                            if (req_port.is_none())
                            {
                                req_port = S<"80">();
                            }
                            bool broke30 = false;
                            for (py const& item : iterate{ cookie.attr("port").method("split", { S<",">() }) })
                            {
                                p = item;
                                if (p == req_port)
                                {
                                    broke30 = true;
                                    break;
                                }
                            }
                            if (!broke30)
                            {
                                (G<"http.cookiejar._debug">())(S<"   request port %s does not match cookie port %s">(), req_port, cookie.attr("port"));
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "return_ok_domain", w.method("DefaultCookiePolicy", "return_ok_domain", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        py domain;
                        py dotdomain;
                        py erhn;
                        py req_host;
                        auto const items31 = unpack((G<"http.cookiejar.eff_request_host">())(request), 2);
                        req_host = items31[0];
                        erhn = items31[1];
                        domain = cookie.attr("domain");
                        if ((domain.truthy() && (!domain.method("startswith", { S<".">() }).truthy())))
                        {
                            dotdomain = (S<".">()) + domain;
                        }
                        else
                        {
                            dotdomain = domain;
                        }
                        // strict check of non-domain cookies: Mozilla does this, MSIE5 doesn't
                        if (((cookie.attr("version") == (I<0>())) && (self.attr("strict_ns_domain") & self.attr("DomainStrictNonDomain")).truthy() && (!cookie.attr("domain_specified").truthy()) && (domain != erhn)))
                        {
                            (G<"http.cookiejar._debug">())(S<"   cookie with unspecified domain does not string-compare equal to request domain">());
                            return boolean(false);
                        }
                        if (((cookie.attr("version") > (I<0>())) && (!(G<"http.cookiejar.domain_match">())(erhn, domain).truthy())))
                        {
                            (G<"http.cookiejar._debug">())(S<"   effective request-host name %s does not domain-match RFC 2965 cookie domain %s">(), erhn, domain);
                            return boolean(false);
                        }
                        if (((cookie.attr("version") == (I<0>())) && (!((S<".">()) + erhn).method("endswith", { dotdomain }).truthy())))
                        {
                            (G<"http.cookiejar._debug">())(S<"   request-host %s does not match Netscape cookie domain %s">(), req_host, domain);
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "domain_return_ok", w.method("DefaultCookiePolicy", "domain_return_ok", { py_positional("domain"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py domain = a[1];
                        py request = a[2];
                        py dotdomain;
                        py erhn;
                        py req_host;
                        auto const items32 = unpack((G<"http.cookiejar.eff_request_host">())(request), 2);
                        req_host = items32[0];
                        erhn = items32[1];
                        if (!req_host.method("startswith", { S<".">() }).truthy())
                        {
                            req_host = (S<".">()) + req_host;
                        }
                        if (!erhn.method("startswith", { S<".">() }).truthy())
                        {
                            erhn = (S<".">()) + erhn;
                        }
                        if ((domain.truthy() && (!domain.method("startswith", { S<".">() }).truthy())))
                        {
                            dotdomain = (S<".">()) + domain;
                        }
                        else
                        {
                            dotdomain = domain;
                        }
                        if (!([&]() -> py { py const t = req_host.method("endswith", { dotdomain }); return t.truthy() ? t : erhn.method("endswith", { dotdomain }); }()).truthy())
                        {
                            // _debug("   request domain %s does not match cookie domain %s",
                            // req_host, domain)
                            return boolean(false);
                        }
                        if (self.method("is_blocked", { domain }).truthy())
                        {
                            (G<"http.cookiejar._debug">())(S<"   domain %s is in user block-list">(), domain);
                            return boolean(false);
                        }
                        if (self.method("is_not_allowed", { domain }).truthy())
                        {
                            (G<"http.cookiejar._debug">())(S<"   domain %s is not in user allow-list">(), domain);
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "path_return_ok", w.method("DefaultCookiePolicy", "path_return_ok", { py_positional("path"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py request = a[2];
                        py pathlen;
                        py req_path;
                        (G<"http.cookiejar._debug">())(S<"- checking cookie path=%s">(), path);
                        req_path = (G<"http.cookiejar.request_path">())(request);
                        pathlen = py{ path.size() };
                        if (req_path == path)
                        {
                            return boolean(true);
                        }
                        else if ((req_path.method("startswith", { path }).truthy() && ([&]() -> py { py const t = path.method("endswith", { S<"/">() }); return t.truthy() ? t : boolean(req_path[B<"slice">()(pathlen, pathlen + (I<1>()), none())] == (S<"/">())); }()).truthy()))
                        {
                            return boolean(true);
                        }
                        (G<"http.cookiejar._debug">())(S<"  %s does not path-match %s">(), req_path, path);
                        return boolean(false);
                    }) }, }));

            define("http.cookiejar", "deepvalues", w.function("deepvalues", "deepvalues", { py_positional("mapping") }, [](arguments a) -> py
                {
                    return make_native_generator("deepvalues", "deepvalues", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py mapping = a[0];
                        py obj;
                        for (py const& item : iterate{ (B<"list">())(mapping.method("values")) })
                        {
                            obj = item;
                            mapping = boolean(false);
                            {
                                bool otherwise34 = false;
                                try
                                {
                                    obj.attr("items");
                                    otherwise34 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler34;
                                    if (matches(handler34.exception, B<"AttributeError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler34.exception);
                                }
                                if (otherwise34)
                                {
                                    mapping = boolean(true);
                                    yield_from((G<"http.cookiejar.deepvalues">())(obj));
                                }
                            }
                            if (!mapping.truthy())
                            {
                                py_yield(obj);
                            }
                        }
                        return none();
                    });
                }));

            // Used as second parameter to dict.get() method, to distinguish absent
            // dict key from one with a None value.
            define("http.cookiejar", "Absent", w.make_class("Absent", {  }, {
 }));

            define("http.cookiejar", "CookieJar", w.make_class("CookieJar", {  }, {
                { "non_word_re", (Mod<"re">()).method("compile", { S<"\\W">() }) },
                { "quote_re", (Mod<"re">()).method("compile", { S<"([\\\"\\\\])">() }) },
                { "strict_domain_re", (Mod<"re">()).method("compile", { S<"\\.?[^.]*">() }) },
                { "domain_re", (Mod<"re">()).method("compile", { S<"[^.]*">() }) },
                { "dots_re", (Mod<"re">()).method("compile", { S<"^\\.+">() }) },
                { "magic_re", (Mod<"re">()).method("compile", { S<"^\\#LWP-Cookies-(\\d+\\.\\d+)">(), Mod<"re">().attr("ASCII") }) },
                { "__init__", w.method("CookieJar", "__init__", { py_positional("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py policy = a[1];
                        if (policy.is_none())
                        {
                            policy = (G<"http.cookiejar.DefaultCookiePolicy">())();
                        }
                        self.set_attr("_policy", policy);
                        self.set_attr("_cookies_lock", (Mod<"threading">().attr("RLock"))());
                        self.set_attr("_cookies", py_dict());
                        return none();
                    }) },
                { "set_policy", w.method("CookieJar", "set_policy", { py_positional("policy") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py policy = a[1];
                        self.set_attr("_policy", policy);
                        return none();
                    }) },
                { "_cookies_for_domain", w.method("CookieJar", "_cookies_for_domain", { py_positional("domain"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py domain = a[1];
                        py request = a[2];
                        py cookie;
                        py cookies;
                        py cookies_by_name;
                        py cookies_by_path;
                        py path;
                        cookies = py_list();
                        if (!self.attr("_policy").method("domain_return_ok", { domain, request }).truthy())
                        {
                            return py_list();
                        }
                        (G<"http.cookiejar._debug">())(S<"Checking %s for cookies to return">(), domain);
                        cookies_by_path = self.attr("_cookies")[domain];
                        for (py const& item : iterate{ cookies_by_path.method("keys") })
                        {
                            path = item;
                            if (!self.attr("_policy").method("path_return_ok", { path, request }).truthy())
                            {
                                continue;
                            }
                            cookies_by_name = cookies_by_path[path];
                            for (py const& item : iterate{ cookies_by_name.method("values") })
                            {
                                cookie = item;
                                if (!self.attr("_policy").method("return_ok", { cookie, request }).truthy())
                                {
                                    (G<"http.cookiejar._debug">())(S<"   not returning cookie">());
                                    continue;
                                }
                                (G<"http.cookiejar._debug">())(S<"   it's a match">());
                                cookies.method("append", { cookie });
                            }
                        }
                        return cookies;
                    }) },
                { "_cookies_for_request", w.method("CookieJar", "_cookies_for_request", { py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        py cookies;
                        py domain;
                        cookies = py_list();
                        for (py const& item : iterate{ self.attr("_cookies").method("keys") })
                        {
                            domain = item;
                            cookies.method("extend", { self.method("_cookies_for_domain", { domain, request }) });
                        }
                        return cookies;
                    }) },
                { "_cookie_attrs", w.method("CookieJar", "_cookie_attrs", { py_positional("cookies") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookies = a[1];
                        py attrs;
                        py cookie;
                        py domain;
                        py p;
                        py value;
                        py version;
                        py version_set;
                        // add cookies in order of most specific (ie. longest) path first
                        cookies.method("sort", {  }, { { "key", w.function("<lambda>", "CookieJar._cookie_attrs.<locals>.<lambda>", { py_positional("a") }, [](arguments a) -> py
    {
        py a_ = a[0];
        return py{ a_.attr("path").size() };
    }) }, { "reverse", boolean(true) } });
                        version_set = boolean(false);
                        attrs = py_list();
                        for (py const& item : iterate{ cookies })
                        {
                            cookie = item;
                            // set version of Cookie header
                            // XXX
                            // What should it be if multiple matching Set-Cookie headers have
                            // different versions themselves?
                            // Answer: there is no answer; was supposed to be settled by
                            // RFC 2965 errata, but that may never appear...
                            version = cookie.attr("version");
                            if (!version_set.truthy())
                            {
                                version_set = boolean(true);
                                if (version > (I<0>()))
                                {
                                    attrs.method("append", { (S<"$Version=%s">()) % version });
                                }
                            }
                            // quote cookie value if necessary
                            // (not for Netscape protocol, which already has any quotes
                            // intact, due to the poorly-specified Netscape Cookie: syntax)
                            if (((!cookie.attr("value").is_none()) && self.attr("non_word_re").method("search", { cookie.attr("value") }).truthy() && (version > (I<0>()))))
                            {
                                value = self.attr("quote_re").method("sub", { S<"\\\\\\1">(), cookie.attr("value") });
                            }
                            else
                            {
                                value = cookie.attr("value");
                            }
                            // add cookie-attributes to be returned in Cookie header
                            if (cookie.attr("value").is_none())
                            {
                                attrs.method("append", { cookie.attr("name") });
                            }
                            else
                            {
                                attrs.method("append", { (S<"%s=%s">()) % py_tuple({ cookie.attr("name"), value }) });
                            }
                            if (version > (I<0>()))
                            {
                                if (cookie.attr("path_specified").truthy())
                                {
                                    attrs.method("append", { (S<"$Path=\"%s\"">()) % cookie.attr("path") });
                                }
                                if (cookie.attr("domain").method("startswith", { S<".">() }).truthy())
                                {
                                    domain = cookie.attr("domain");
                                    if (((!cookie.attr("domain_initial_dot").truthy()) && domain.method("startswith", { S<".">() }).truthy()))
                                    {
                                        domain = domain[B<"slice">()(I<1>(), none(), none())];
                                    }
                                    attrs.method("append", { (S<"$Domain=\"%s\"">()) % domain });
                                }
                                if (!cookie.attr("port").is_none())
                                {
                                    p = S<"$Port">();
                                    if (cookie.attr("port_specified").truthy())
                                    {
                                        p = p + ((S<"=\"%s\"">()) % cookie.attr("port"));
                                    }
                                    attrs.method("append", { p });
                                }
                            }
                        }
                        return attrs;
                    }) },
                { "add_cookie_header", w.method("CookieJar", "add_cookie_header", { py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        py attrs;
                        py cookie;
                        py cookies;
                        (G<"http.cookiejar._debug">())(S<"add_cookie_header">());
                        self.attr("_cookies_lock").method("acquire");
                        {
                            std::optional<py> pending39;
                            [[maybe_unused]] int flow39 = 0;
                            [[maybe_unused]] py result39;
                            try
                            {
                                py const value40 = (B<"int">())((Mod<"time">().attr("time"))());
                                self.attr("_policy").set_attr("_now", value40);
                                self.set_attr("_now", value40);
                                cookies = self.method("_cookies_for_request", { request });
                                attrs = self.method("_cookie_attrs", { cookies });
                                if (attrs.truthy())
                                {
                                    if (!request.method("has_header", { S<"Cookie">() }).truthy())
                                    {
                                        request.method("add_unredirected_header", { S<"Cookie">(), (S<"; ">()).method("join", { attrs }) });
                                    }
                                }
                                // if necessary, advertise that we know RFC 2965
                                if ((self.attr("_policy").attr("rfc2965").truthy() && (!self.attr("_policy").attr("hide_cookie2").truthy()) && (!request.method("has_header", { S<"Cookie2">() }).truthy())))
                                {
                                    for (py const& item : iterate{ cookies })
                                    {
                                        cookie = item;
                                        if (cookie.attr("version") != (I<1>()))
                                        {
                                            request.method("add_unredirected_header", { S<"Cookie2">(), S<"$Version=\"1\"">() });
                                            break;
                                        }
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending39 = py_take_exception();
                            }
                            finally39:
                            self.attr("_cookies_lock").method("release");
                            if (pending39)
                                py_reraise(*pending39);
                            if (flow39 == 1)
                                return result39;
                        }
                        self.method("clear_expired_cookies");
                        return none();
                    }) },
                { "_normalized_cookie_tuples", w.method("CookieJar", "_normalized_cookie_tuples", { py_positional("attrs_set") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs_set = a[1];
                        py bad_cookie;
                        py boolean_attrs;
                        py cookie_attrs;
                        py cookie_tuples;
                        py k;
                        py lc;
                        py max_age_set;
                        py name;
                        py rest;
                        py standard;
                        py v;
                        py value;
                        py value_attrs;
                        cookie_tuples = py_list();
                        boolean_attrs = py_tuple({ S<"discard">(), S<"secure">() });
                        value_attrs = py_tuple({ S<"version">(), S<"expires">(), S<"max-age">(), S<"domain">(), S<"path">(), S<"port">(), S<"comment">(), S<"commenturl">() });
                        for (py const& item : iterate{ attrs_set })
                        {
                            cookie_attrs = item;
                            auto const items43 = unpack(cookie_attrs[I<0>()], 2);
                            name = items43[0];
                            value = items43[1];
                            // Build dictionary of standard cookie-attributes (standard) and
                            // dictionary of other cookie-attributes (rest).
                            // Note: expiry time is normalised to seconds since epoch.  V0
                            // cookies should have the Expires cookie-attribute, and V1 cookies
                            // should have Max-Age, but since V1 includes RFC 2109 cookies (and
                            // since V0 cookies may be a mish-mash of Netscape and RFC 2109), we
                            // accept either (but prefer Max-Age).
                            max_age_set = boolean(false);
                            bad_cookie = boolean(false);
                            standard = py_dict();
                            rest = py_dict();
                            for (py const& item : iterate{ cookie_attrs[B<"slice">()(I<1>(), none(), none())] })
                            {
                                auto const items45 = unpack(item, 2);
                                k = items45[0];
                                v = items45[1];
                                lc = k.method("lower");
                                // don't lose case distinction for unknown fields
                                if ((value_attrs.contains(lc) || boolean_attrs.contains(lc)))
                                {
                                    k = lc;
                                }
                                if ((boolean_attrs.contains(k) && v.is_none()))
                                {
                                    // boolean cookie-attribute is present, but has no value
                                    // (like "discard", rather than "port=80")
                                    v = boolean(true);
                                }
                                if (standard.contains(k))
                                {
                                    // only first value is significant
                                    continue;
                                }
                                if (k == (S<"domain">()))
                                {
                                    if (v.is_none())
                                    {
                                        (G<"http.cookiejar._debug">())(S<"   missing value for domain attribute">());
                                        bad_cookie = boolean(true);
                                        break;
                                    }
                                    // RFC 2965 section 3.3.3
                                    v = v.method("lower");
                                }
                                if (k == (S<"expires">()))
                                {
                                    if (max_age_set.truthy())
                                    {
                                        // Prefer max-age to expires (like Mozilla)
                                        continue;
                                    }
                                    if (v.is_none())
                                    {
                                        (G<"http.cookiejar._debug">())(S<"   missing or invalid value for expires attribute: treating as session cookie">());
                                        continue;
                                    }
                                }
                                if (k == (S<"max-age">()))
                                {
                                    max_age_set = boolean(true);
                                    try
                                    {
                                        v = (B<"int">())(v);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler46;
                                        if (matches(handler46.exception, B<"ValueError">()))
                                        {
                                            (G<"http.cookiejar._debug">())(S<"   missing or invalid (non-numeric) value for max-age attribute">());
                                            bad_cookie = boolean(true);
                                            break;
                                        }
                                        else
                                            py_reraise(handler46.exception);
                                    }
                                    // convert RFC 2965 Max-Age to seconds since epoch
                                    // XXX Strictly you're supposed to follow RFC 2616
                                    // age-calculation rules.  Remember that zero Max-Age
                                    // is a request to discard (old and new) cookie, though.
                                    k = S<"expires">();
                                    v = self.attr("_now") + v;
                                }
                                if ((value_attrs.contains(k) || boolean_attrs.contains(k)))
                                {
                                    if ((v.is_none() && (!py_tuple({ S<"port">(), S<"comment">(), S<"commenturl">() }).contains(k))))
                                    {
                                        (G<"http.cookiejar._debug">())((S<"   missing value for %s attribute">()) % k);
                                        bad_cookie = boolean(true);
                                        break;
                                    }
                                    standard.set_item(k, v);
                                }
                                else
                                {
                                    rest.set_item(k, v);
                                }
                            }
                            if (bad_cookie.truthy())
                            {
                                continue;
                            }
                            cookie_tuples.method("append", { py_tuple({ name, value, standard, rest }) });
                        }
                        return cookie_tuples;
                    }) },
                { "_cookie_from_cookie_tuple", w.method("CookieJar", "_cookie_from_cookie_tuple", { py_positional("tup"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tup = a[1];
                        py request = a[2];
                        py comment;
                        py comment_url;
                        py discard;
                        py domain;
                        py domain_initial_dot;
                        py domain_specified;
                        py erhn;
                        py expires;
                        py i;
                        py name;
                        py path;
                        py path_specified;
                        py port;
                        py port_specified;
                        py req_host;
                        py rest;
                        py secure;
                        py standard;
                        py value;
                        py version;
                        auto const items47 = unpack(tup, 4);
                        name = items47[0];
                        value = items47[1];
                        standard = items47[2];
                        rest = items47[3];
                        domain = standard.method("get", { S<"domain">(), G<"http.cookiejar.Absent">() });
                        path = standard.method("get", { S<"path">(), G<"http.cookiejar.Absent">() });
                        port = standard.method("get", { S<"port">(), G<"http.cookiejar.Absent">() });
                        expires = standard.method("get", { S<"expires">(), G<"http.cookiejar.Absent">() });
                        // set the easy defaults
                        version = standard.method("get", { S<"version">(), none() });
                        if (!version.is_none())
                        {
                            try
                            {
                                version = (B<"int">())(version);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler48;
                                if (matches(handler48.exception, B<"ValueError">()))
                                {
                                    return none();
                                }
                                else
                                    py_reraise(handler48.exception);
                            }
                        }
                        secure = standard.method("get", { S<"secure">(), boolean(false) });
                        // (discard is also set if expires is Absent)
                        discard = standard.method("get", { S<"discard">(), boolean(false) });
                        comment = standard.method("get", { S<"comment">(), none() });
                        comment_url = standard.method("get", { S<"commenturl">(), none() });
                        // set default path
                        if (((!path.is((G<"http.cookiejar.Absent">()))) && (path != (S<"">()))))
                        {
                            path_specified = boolean(true);
                            path = (G<"http.cookiejar.escape_path">())(path);
                        }
                        else
                        {
                            path_specified = boolean(false);
                            path = (G<"http.cookiejar.request_path">())(request);
                            i = path.method("rfind", { S<"/">() });
                            if (i != (I<-1>()))
                            {
                                if (version == (I<0>()))
                                {
                                    // Netscape spec parts company from reality here
                                    path = path[B<"slice">()(none(), i, none())];
                                }
                                else
                                {
                                    path = path[B<"slice">()(none(), i + (I<1>()), none())];
                                }
                            }
                            if (py{ path.size() } == (I<0>()))
                            {
                                path = S<"/">();
                            }
                        }
                        // set default domain
                        domain_specified = boolean(!domain.is((G<"http.cookiejar.Absent">())));
                        // but first we have to remember whether it starts with a dot
                        domain_initial_dot = boolean(false);
                        if (domain_specified.truthy())
                        {
                            domain_initial_dot = (B<"bool">())(domain.method("startswith", { S<".">() }));
                        }
                        if (domain.is((G<"http.cookiejar.Absent">())))
                        {
                            auto const items49 = unpack((G<"http.cookiejar.eff_request_host">())(request), 2);
                            req_host = items49[0];
                            erhn = items49[1];
                            domain = erhn;
                        }
                        else if (!domain.method("startswith", { S<".">() }).truthy())
                        {
                            domain = (S<".">()) + domain;
                        }
                        // set default port
                        port_specified = boolean(false);
                        if (!port.is((G<"http.cookiejar.Absent">())))
                        {
                            if (port.is_none())
                            {
                                // Port attr present, but has no value: default to request port.
                                // Cookie should then only be sent back on that port.
                                port = (G<"http.cookiejar.request_port">())(request);
                            }
                            else
                            {
                                port_specified = boolean(true);
                                port = (Mod<"re">().attr("sub"))(S<"\\s+">(), S<"">(), port);
                            }
                        }
                        else
                        {
                            // No port attr present.  Cookie can be sent back on any port.
                            port = none();
                        }
                        // set default expires and discard
                        if (expires.is((G<"http.cookiejar.Absent">())))
                        {
                            expires = none();
                            discard = boolean(true);
                        }
                        else if (expires <= self.attr("_now"))
                        {
                            // Expiry date in past is request to delete cookie.  This can't be
                            // in DefaultCookiePolicy, because can't delete cookies there.
                            try
                            {
                                self.method("clear", { domain, path, name });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler50;
                                if (matches(handler50.exception, B<"KeyError">()))
                                {
                                }
                                else
                                    py_reraise(handler50.exception);
                            }
                            (G<"http.cookiejar._debug">())(S<"Expiring cookie, domain='%s', path='%s', name='%s'">(), domain, path, name);
                            return none();
                        }
                        return (G<"http.cookiejar.Cookie">())(version, name, value, port, port_specified, domain, domain_specified, domain_initial_dot, path, path_specified, secure, expires, discard, comment, comment_url, rest);
                    }) },
                { "_cookies_from_attrs_set", w.method("CookieJar", "_cookies_from_attrs_set", { py_positional("attrs_set"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs_set = a[1];
                        py request = a[2];
                        py cookie;
                        py cookie_tuples;
                        py cookies;
                        py tup;
                        cookie_tuples = self.method("_normalized_cookie_tuples", { attrs_set });
                        cookies = py_list();
                        for (py const& item : iterate{ cookie_tuples })
                        {
                            tup = item;
                            cookie = self.method("_cookie_from_cookie_tuple", { tup, request });
                            if (cookie.truthy())
                            {
                                cookies.method("append", { cookie });
                            }
                        }
                        return cookies;
                    }) },
                { "_process_rfc2109_cookies", w.method("CookieJar", "_process_rfc2109_cookies", { py_positional("cookies") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookies = a[1];
                        py cookie;
                        py rfc2109_as_ns;
                        rfc2109_as_ns = (B<"getattr">())(self.attr("_policy"), S<"rfc2109_as_netscape">(), none());
                        if (rfc2109_as_ns.is_none())
                        {
                            rfc2109_as_ns = boolean(!self.attr("_policy").attr("rfc2965").truthy());
                        }
                        for (py const& item : iterate{ cookies })
                        {
                            cookie = item;
                            if (cookie.attr("version") == (I<1>()))
                            {
                                cookie.set_attr("rfc2109", boolean(true));
                                if (rfc2109_as_ns.truthy())
                                {
                                    // treat 2109 cookies as Netscape cookies rather than
                                    // as RFC2965 cookies
                                    cookie.set_attr("version", I<0>());
                                }
                            }
                        }
                        return none();
                    }) },
                { "make_cookies", w.method("CookieJar", "make_cookies", { py_positional("response"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py response = a[1];
                        py request = a[2];
                        py cookie;
                        py cookies;
                        py headers;
                        py lookup;
                        py netscape;
                        py no_matching_rfc2965;
                        py ns_cookies;
                        py ns_hdrs;
                        py rfc2965;
                        py rfc2965_hdrs;
                        // get cookie-attributes for RFC 2965 and Netscape protocols
                        headers = response.method("info");
                        rfc2965_hdrs = headers.method("get_all", { S<"Set-Cookie2">(), py_list() });
                        ns_hdrs = headers.method("get_all", { S<"Set-Cookie">(), py_list() });
                        py const value53 = (B<"int">())((Mod<"time">().attr("time"))());
                        self.attr("_policy").set_attr("_now", value53);
                        self.set_attr("_now", value53);
                        rfc2965 = self.attr("_policy").attr("rfc2965");
                        netscape = self.attr("_policy").attr("netscape");
                        if ((([&]() -> py { py const t = boolean(!rfc2965_hdrs.truthy()); return !t.truthy() ? t : boolean(!ns_hdrs.truthy()); }()).truthy() || ([&]() -> py { py const t = boolean(!ns_hdrs.truthy()); return !t.truthy() ? t : boolean(!rfc2965.truthy()); }()).truthy() || ([&]() -> py { py const t = boolean(!rfc2965_hdrs.truthy()); return !t.truthy() ? t : boolean(!netscape.truthy()); }()).truthy() || ([&]() -> py { py const t = boolean(!netscape.truthy()); return !t.truthy() ? t : boolean(!rfc2965.truthy()); }()).truthy()))
                        {
                            return py_list();
                        }
                        try
                        {
                            cookies = self.method("_cookies_from_attrs_set", { (G<"http.cookiejar.split_header_words">())(rfc2965_hdrs), request });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler54;
                            if (matches(handler54.exception, B<"Exception">()))
                            {
                                (G<"http.cookiejar._warn_unhandled_exception">())();
                                cookies = py_list();
                            }
                            else
                                py_reraise(handler54.exception);
                        }
                        if ((ns_hdrs.truthy() && netscape.truthy()))
                        {
                            try
                            {
                                // RFC 2109 and Netscape cookies
                                ns_cookies = self.method("_cookies_from_attrs_set", { (G<"http.cookiejar.parse_ns_headers">())(ns_hdrs), request });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler55;
                                if (matches(handler55.exception, B<"Exception">()))
                                {
                                    (G<"http.cookiejar._warn_unhandled_exception">())();
                                    ns_cookies = py_list();
                                }
                                else
                                    py_reraise(handler55.exception);
                            }
                            self.method("_process_rfc2109_cookies", { ns_cookies });
                            // Look for Netscape cookies (from Set-Cookie headers) that match
                            // corresponding RFC 2965 cookies (from Set-Cookie2 headers).
                            // For each match, keep the RFC 2965 cookie and ignore the Netscape
                            // cookie (RFC 2965 section 9.1).  Actually, RFC 2109 cookies are
                            // bundled in with the Netscape cookies for this purpose, which is
                            // reasonable behaviour.
                            if (rfc2965.truthy())
                            {
                                lookup = py_dict();
                                for (py const& item : iterate{ cookies })
                                {
                                    cookie = item;
                                    lookup.set_item(py_tuple({ cookie.attr("domain"), cookie.attr("path"), cookie.attr("name") }), none());
                                }
                                no_matching_rfc2965 = w.function("no_matching_rfc2965", "CookieJar.make_cookies.<locals>.no_matching_rfc2965", { py_positional("ns_cookie"), py_positional("lookup", lookup) }, [](arguments a) -> py
            {
                py ns_cookie = a[0];
                py lookup = a[1];
                py key;
                key = py_tuple({ ns_cookie.attr("domain"), ns_cookie.attr("path"), ns_cookie.attr("name") });
                return boolean(!lookup.contains(key));
            });
                                ns_cookies = (B<"filter">())(no_matching_rfc2965, ns_cookies);
                            }
                            if (ns_cookies.truthy())
                            {
                                cookies.method("extend", { ns_cookies });
                            }
                        }
                        return cookies;
                    }) },
                { "set_cookie_if_ok", w.method("CookieJar", "set_cookie_if_ok", { py_positional("cookie"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py request = a[2];
                        self.attr("_cookies_lock").method("acquire");
                        {
                            std::optional<py> pending57;
                            [[maybe_unused]] int flow57 = 0;
                            [[maybe_unused]] py result57;
                            try
                            {
                                py const value58 = (B<"int">())((Mod<"time">().attr("time"))());
                                self.attr("_policy").set_attr("_now", value58);
                                self.set_attr("_now", value58);
                                if (self.attr("_policy").method("set_ok", { cookie, request }).truthy())
                                {
                                    self.method("set_cookie", { cookie });
                                }
                            }
                            catch (raised const&)
                            {
                                pending57 = py_take_exception();
                            }
                            finally57:
                            self.attr("_cookies_lock").method("release");
                            if (pending57)
                                py_reraise(*pending57);
                            if (flow57 == 1)
                                return result57;
                        }
                        return none();
                    }) },
                { "set_cookie", w.method("CookieJar", "set_cookie", { py_positional("cookie") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie = a[1];
                        py c;
                        py c2;
                        py c3;
                        c = self.attr("_cookies");
                        self.attr("_cookies_lock").method("acquire");
                        {
                            std::optional<py> pending59;
                            [[maybe_unused]] int flow59 = 0;
                            [[maybe_unused]] py result59;
                            try
                            {
                                if (!c.contains(cookie.attr("domain")))
                                {
                                    c.set_item(cookie.attr("domain"), py_dict());
                                }
                                c2 = c[cookie.attr("domain")];
                                if (!c2.contains(cookie.attr("path")))
                                {
                                    c2.set_item(cookie.attr("path"), py_dict());
                                }
                                c3 = c2[cookie.attr("path")];
                                c3.set_item(cookie.attr("name"), cookie);
                            }
                            catch (raised const&)
                            {
                                pending59 = py_take_exception();
                            }
                            finally59:
                            self.attr("_cookies_lock").method("release");
                            if (pending59)
                                py_reraise(*pending59);
                            if (flow59 == 1)
                                return result59;
                        }
                        return none();
                    }) },
                { "extract_cookies", w.method("CookieJar", "extract_cookies", { py_positional("response"), py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py response = a[1];
                        py request = a[2];
                        py cookie;
                        (G<"http.cookiejar._debug">())(S<"extract_cookies: %s">(), response.method("info"));
                        self.attr("_cookies_lock").method("acquire");
                        {
                            std::optional<py> pending60;
                            [[maybe_unused]] int flow60 = 0;
                            [[maybe_unused]] py result60;
                            try
                            {
                                for (py const& item : iterate{ self.method("make_cookies", { response, request }) })
                                {
                                    cookie = item;
                                    if (self.attr("_policy").method("set_ok", { cookie, request }).truthy())
                                    {
                                        (G<"http.cookiejar._debug">())(S<" setting cookie: %s">(), cookie);
                                        self.method("set_cookie", { cookie });
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending60 = py_take_exception();
                            }
                            finally60:
                            self.attr("_cookies_lock").method("release");
                            if (pending60)
                                py_reraise(*pending60);
                            if (flow60 == 1)
                                return result60;
                        }
                        return none();
                    }) },
                { "clear", w.method("CookieJar", "clear", { py_positional("domain", none()), py_positional("path", none()), py_positional("name", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py domain = a[1];
                        py path = a[2];
                        py name = a[3];
                        if (!name.is_none())
                        {
                            if ((domain.is_none() || path.is_none()))
                            {
                                py_raise((B<"ValueError">())(S<"domain and path must be given to remove a cookie by name">()));
                            }
                            del_item(self.attr("_cookies")[domain][path], name);
                        }
                        else if (!path.is_none())
                        {
                            if (domain.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"domain must be given to remove cookies by path">()));
                            }
                            del_item(self.attr("_cookies")[domain], path);
                        }
                        else if (!domain.is_none())
                        {
                            del_item(self.attr("_cookies"), domain);
                        }
                        else
                        {
                            self.set_attr("_cookies", py_dict());
                        }
                        return none();
                    }) },
                { "clear_session_cookies", w.method("CookieJar", "clear_session_cookies", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie;
                        self.attr("_cookies_lock").method("acquire");
                        {
                            std::optional<py> pending62;
                            [[maybe_unused]] int flow62 = 0;
                            [[maybe_unused]] py result62;
                            try
                            {
                                for (py const& item : iterate{ self })
                                {
                                    cookie = item;
                                    if (cookie.attr("discard").truthy())
                                    {
                                        self.method("clear", { cookie.attr("domain"), cookie.attr("path"), cookie.attr("name") });
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending62 = py_take_exception();
                            }
                            finally62:
                            self.attr("_cookies_lock").method("release");
                            if (pending62)
                                py_reraise(*pending62);
                            if (flow62 == 1)
                                return result62;
                        }
                        return none();
                    }) },
                { "clear_expired_cookies", w.method("CookieJar", "clear_expired_cookies", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie;
                        py now;
                        self.attr("_cookies_lock").method("acquire");
                        {
                            std::optional<py> pending64;
                            [[maybe_unused]] int flow64 = 0;
                            [[maybe_unused]] py result64;
                            try
                            {
                                now = (Mod<"time">().attr("time"))();
                                for (py const& item : iterate{ self })
                                {
                                    cookie = item;
                                    if (cookie.method("is_expired", { now }).truthy())
                                    {
                                        self.method("clear", { cookie.attr("domain"), cookie.attr("path"), cookie.attr("name") });
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending64 = py_take_exception();
                            }
                            finally64:
                            self.attr("_cookies_lock").method("release");
                            if (pending64)
                                py_reraise(*pending64);
                            if (flow64 == 1)
                                return result64;
                        }
                        return none();
                    }) },
                { "__iter__", w.method("CookieJar", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"http.cookiejar.deepvalues">())(self.attr("_cookies"));
                    }) },
                { "__len__", w.method("CookieJar", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie;
                        py i;
                        i = I<0>();
                        for (py const& item : iterate{ self })
                        {
                            cookie = item;
                            i = i + (I<1>());
                        }
                        return i;
                    }) },
                { "__repr__", w.method("CookieJar", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie;
                        py r;
                        r = py_list();
                        for (py const& item : iterate{ self })
                        {
                            cookie = item;
                            r.method("append", { S(cookie.repr()) });
                        }
                        return (S<"<%s[%s]>">()) % py_tuple({ self.attr("__class__").attr("__name__"), (S<", ">()).method("join", { r }) });
                    }) },
                { "__str__", w.method("CookieJar", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookie;
                        py r;
                        r = py_list();
                        for (py const& item : iterate{ self })
                        {
                            cookie = item;
                            r.method("append", { (B<"str">())(cookie) });
                        }
                        return (S<"<%s[%s]>">()) % py_tuple({ self.attr("__class__").attr("__name__"), (S<", ">()).method("join", { r }) });
                    }) }, }));

            // derives from OSError for backwards-compatibility with Python 2.4.0
            define("http.cookiejar", "LoadError", w.make_class("LoadError", { B<"OSError">() }, {
 }));

            define("http.cookiejar", "FileCookieJar", w.make_class("FileCookieJar", { G<"http.cookiejar.CookieJar">() }, {
                { "__init__", w.method("FileCookieJar", "__init__", { py_positional("filename", none()), py_positional("delayload", boolean(false)), py_positional("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py delayload = a[2];
                        py policy = a[3];
                        (G<"http.cookiejar.CookieJar">()).method("__init__", { self, policy });
                        if (!filename.is_none())
                        {
                            filename = (Mod<"os">().attr("fspath"))(filename);
                        }
                        self.set_attr("filename", filename);
                        self.set_attr("delayload", (B<"bool">())(delayload));
                        return none();
                    }) },
                { "save", w.method("FileCookieJar", "save", { py_positional("filename", none()), py_positional("ignore_discard", boolean(false)), py_positional("ignore_expires", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py ignore_discard = a[2];
                        py ignore_expires = a[3];
                        py_raise((B<"NotImplementedError">())());
                    }) },
                { "load", w.method("FileCookieJar", "load", { py_positional("filename", none()), py_positional("ignore_discard", boolean(false)), py_positional("ignore_expires", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py ignore_discard = a[2];
                        py ignore_expires = a[3];
                        py f;
                        if (filename.is_none())
                        {
                            if (!self.attr("filename").is_none())
                            {
                                filename = self.attr("filename");
                            }
                            else
                            {
                                py_raise((B<"ValueError">())(G<"http.cookiejar.MISSING_FILENAME_TEXT">()));
                            }
                        }
                        {
                            py const manager69 = (B<"open">())(filename);
                            py const exit69 = pyclass::special_method(manager69, "__exit__");
                            py const value69 = pyclass::special_method(manager69, "__enter__")(manager69);
                            f = value69;
                            [[maybe_unused]] int flow69 = 0;
                            [[maybe_unused]] py result69;
                            bool exited69 = false;
                            try
                            {
                                self.method("_really_load", { f, filename, ignore_discard, ignore_expires });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler69;
                                exited69 = true;
                                if (!exit69(manager69, type_of(handler69.exception), handler69.exception, handler69.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler69.exception);
                            }
                            finally69:
                            if (!exited69)
                                exit69(manager69, none(), none(), none());
                            if (flow69 == 1)
                                return result69;
                        }
                        return none();
                    }) },
                { "revert", w.method("FileCookieJar", "revert", { py_positional("filename", none()), py_positional("ignore_discard", boolean(false)), py_positional("ignore_expires", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py ignore_discard = a[2];
                        py ignore_expires = a[3];
                        py old_state;
                        if (filename.is_none())
                        {
                            if (!self.attr("filename").is_none())
                            {
                                filename = self.attr("filename");
                            }
                            else
                            {
                                py_raise((B<"ValueError">())(G<"http.cookiejar.MISSING_FILENAME_TEXT">()));
                            }
                        }
                        self.attr("_cookies_lock").method("acquire");
                        {
                            std::optional<py> pending70;
                            [[maybe_unused]] int flow70 = 0;
                            [[maybe_unused]] py result70;
                            try
                            {
                                old_state = (Mod<"copy">().attr("deepcopy"))(self.attr("_cookies"));
                                self.set_attr("_cookies", py_dict());
                                try
                                {
                                    self.method("load", { filename, ignore_discard, ignore_expires });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler71;
                                    if (matches(handler71.exception, B<"OSError">()))
                                    {
                                        self.set_attr("_cookies", old_state);
                                        py_reraise(handler71.exception);
                                    }
                                    else
                                        py_reraise(handler71.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending70 = py_take_exception();
                            }
                            finally70:
                            self.attr("_cookies_lock").method("release");
                            if (pending70)
                                py_reraise(*pending70);
                            if (flow70 == 1)
                                return result70;
                        }
                        return none();
                    }) }, }));

            define("http.cookiejar", "lwp_cookie_str", w.function("lwp_cookie_str", "lwp_cookie_str", { py_positional("cookie") }, [](arguments a) -> py
                {
                    py cookie = a[0];
                    py h;
                    py k;
                    py keys;
                    h = py_list({ py_tuple({ cookie.attr("name"), cookie.attr("value") }), py_tuple({ S<"path">(), cookie.attr("path") }), py_tuple({ S<"domain">(), cookie.attr("domain") }) });
                    if (!cookie.attr("port").is_none())
                    {
                        h.method("append", { py_tuple({ S<"port">(), cookie.attr("port") }) });
                    }
                    if (cookie.attr("path_specified").truthy())
                    {
                        h.method("append", { py_tuple({ S<"path_spec">(), none() }) });
                    }
                    if (cookie.attr("port_specified").truthy())
                    {
                        h.method("append", { py_tuple({ S<"port_spec">(), none() }) });
                    }
                    if (cookie.attr("domain_initial_dot").truthy())
                    {
                        h.method("append", { py_tuple({ S<"domain_dot">(), none() }) });
                    }
                    if (cookie.attr("secure").truthy())
                    {
                        h.method("append", { py_tuple({ S<"secure">(), none() }) });
                    }
                    if (cookie.attr("expires").truthy())
                    {
                        h.method("append", { py_tuple({ S<"expires">(), (G<"http.cookiejar.time2isoz">())((B<"float">())(cookie.attr("expires"))) }) });
                    }
                    if (cookie.attr("discard").truthy())
                    {
                        h.method("append", { py_tuple({ S<"discard">(), none() }) });
                    }
                    if (cookie.attr("comment").truthy())
                    {
                        h.method("append", { py_tuple({ S<"comment">(), cookie.attr("comment") }) });
                    }
                    if (cookie.attr("comment_url").truthy())
                    {
                        h.method("append", { py_tuple({ S<"commenturl">(), cookie.attr("comment_url") }) });
                    }
                    keys = (B<"sorted">())(cookie.attr("_rest").method("keys"));
                    for (py const& item : iterate{ keys })
                    {
                        k = item;
                        h.method("append", { py_tuple({ k, (B<"str">())(cookie.attr("_rest")[k]) }) });
                    }
                    h.method("append", { py_tuple({ S<"version">(), (B<"str">())(cookie.attr("version")) }) });
                    return (G<"http.cookiejar.join_header_words">())(py_list({ h }));
                }));

            define("http.cookiejar", "LWPCookieJar", w.make_class("LWPCookieJar", { G<"http.cookiejar.FileCookieJar">() }, {
                { "as_lwp_str", w.method("LWPCookieJar", "as_lwp_str", { py_positional("ignore_discard", boolean(true)), py_positional("ignore_expires", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ignore_discard = a[1];
                        py ignore_expires = a[2];
                        py cookie;
                        py now;
                        py r;
                        now = (Mod<"time">().attr("time"))();
                        r = py_list();
                        for (py const& item : iterate{ self })
                        {
                            cookie = item;
                            if (((!ignore_discard.truthy()) && cookie.attr("discard").truthy()))
                            {
                                continue;
                            }
                            if (((!ignore_expires.truthy()) && cookie.method("is_expired", { now }).truthy()))
                            {
                                continue;
                            }
                            r.method("append", { (S<"Set-Cookie3: %s">()) % (G<"http.cookiejar.lwp_cookie_str">())(cookie) });
                        }
                        return (S<"\n">()).method("join", { r + py_list({ S<"">() }) });
                    }) },
                { "save", w.method("LWPCookieJar", "save", { py_positional("filename", none()), py_positional("ignore_discard", boolean(false)), py_positional("ignore_expires", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py ignore_discard = a[2];
                        py ignore_expires = a[3];
                        py f;
                        if (filename.is_none())
                        {
                            if (!self.attr("filename").is_none())
                            {
                                filename = self.attr("filename");
                            }
                            else
                            {
                                py_raise((B<"ValueError">())(G<"http.cookiejar.MISSING_FILENAME_TEXT">()));
                            }
                        }
                        {
                            py const manager74 = (Mod<"os">().attr("fdopen"))((Mod<"os">().attr("open"))(filename, ((Mod<"os">().attr("O_CREAT")) | (Mod<"os">().attr("O_WRONLY"))) | (Mod<"os">().attr("O_TRUNC")), I<384>()), S<"w">());
                            py const exit74 = pyclass::special_method(manager74, "__exit__");
                            py const value74 = pyclass::special_method(manager74, "__enter__")(manager74);
                            f = value74;
                            [[maybe_unused]] int flow74 = 0;
                            [[maybe_unused]] py result74;
                            bool exited74 = false;
                            try
                            {
                                // There really isn't an LWP Cookies 2.0 format, but this indicates
                                // that there is extra information in here (domain_dot and
                                // port_spec) while still being compatible with libwww-perl, I hope.
                                f.method("write", { S<"#LWP-Cookies-2.0\n">() });
                                f.method("write", { self.method("as_lwp_str", { ignore_discard, ignore_expires }) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler74;
                                exited74 = true;
                                if (!exit74(manager74, type_of(handler74.exception), handler74.exception, handler74.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler74.exception);
                            }
                            finally74:
                            if (!exited74)
                                exit74(manager74, none(), none(), none());
                            if (flow74 == 1)
                                return result74;
                        }
                        return none();
                    }) },
                { "_really_load", w.method("LWPCookieJar", "_really_load", { py_positional("f"), py_positional("filename"), py_positional("ignore_discard"), py_positional("ignore_expires") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        py filename = a[2];
                        py ignore_discard = a[3];
                        py ignore_expires = a[4];
                        py boolean_attrs;
                        py c;
                        py data;
                        py discard;
                        py domain;
                        py domain_specified;
                        py expires;
                        py h;
                        py header;
                        py k;
                        py lc;
                        py line;
                        py magic;
                        py msg;
                        py name;
                        py now;
                        py rest;
                        py standard;
                        py v;
                        py value;
                        py value_attrs;
                        magic = f.method("readline");
                        if (!self.attr("magic_re").method("search", { magic }).truthy())
                        {
                            msg = (S<"%r does not look like a Set-Cookie3 (LWP) format file">()) % filename;
                            py_raise((G<"http.cookiejar.LoadError">())(msg));
                        }
                        now = (Mod<"time">().attr("time"))();
                        header = S<"Set-Cookie3:">();
                        boolean_attrs = py_tuple({ S<"port_spec">(), S<"path_spec">(), S<"domain_dot">(), S<"secure">(), S<"discard">() });
                        value_attrs = py_tuple({ S<"version">(), S<"port">(), S<"path">(), S<"domain">(), S<"expires">(), S<"comment">(), S<"commenturl">() });
                        try
                        {
                            while (([&]() -> py { line = f.method("readline"); return line; }()) != (S<"">()))
                            {
                                if (!line.method("startswith", { header }).truthy())
                                {
                                    continue;
                                }
                                line = line[B<"slice">()(py{ header.size() }, none(), none())].method("strip");
                                for (py const& item : iterate{ (G<"http.cookiejar.split_header_words">())(py_list({ line })) })
                                {
                                    data = item;
                                    auto const items78 = unpack(data[I<0>()], 2);
                                    name = items78[0];
                                    value = items78[1];
                                    standard = py_dict();
                                    rest = py_dict();
                                    for (py const& item : iterate{ boolean_attrs })
                                    {
                                        k = item;
                                        standard.set_item(k, boolean(false));
                                    }
                                    for (py const& item : iterate{ data[B<"slice">()(I<1>(), none(), none())] })
                                    {
                                        auto const items81 = unpack(item, 2);
                                        k = items81[0];
                                        v = items81[1];
                                        if (!k.is_none())
                                        {
                                            lc = k.method("lower");
                                        }
                                        else
                                        {
                                            lc = none();
                                        }
                                        // don't lose case distinction for unknown fields
                                        if ((value_attrs.contains(lc) || boolean_attrs.contains(lc)))
                                        {
                                            k = lc;
                                        }
                                        if (boolean_attrs.contains(k))
                                        {
                                            if (v.is_none())
                                            {
                                                v = boolean(true);
                                            }
                                            standard.set_item(k, v);
                                        }
                                        else if (value_attrs.contains(k))
                                        {
                                            standard.set_item(k, v);
                                        }
                                        else
                                        {
                                            rest.set_item(k, v);
                                        }
                                    }
                                    h = standard.attr("get");
                                    expires = h(S<"expires">());
                                    discard = h(S<"discard">());
                                    if (!expires.is_none())
                                    {
                                        expires = (G<"http.cookiejar.iso2time">())(expires);
                                    }
                                    if (expires.is_none())
                                    {
                                        discard = boolean(true);
                                    }
                                    domain = h(S<"domain">());
                                    domain_specified = domain.method("startswith", { S<".">() });
                                    c = (G<"http.cookiejar.Cookie">())(h(S<"version">()), name, value, h(S<"port">()), h(S<"port_spec">()), domain, domain_specified, h(S<"domain_dot">()), h(S<"path">()), h(S<"path_spec">()), h(S<"secure">()), expires, discard, h(S<"comment">()), h(S<"commenturl">()), rest);
                                    if (((!ignore_discard.truthy()) && c.attr("discard").truthy()))
                                    {
                                        continue;
                                    }
                                    if (((!ignore_expires.truthy()) && c.method("is_expired", { now }).truthy()))
                                    {
                                        continue;
                                    }
                                    self.method("set_cookie", { c });
                                }
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler75;
                            if (matches(handler75.exception, B<"OSError">()))
                            {
                                py_reraise(handler75.exception);
                            }
                            else if (matches(handler75.exception, B<"Exception">()))
                            {
                                (G<"http.cookiejar._warn_unhandled_exception">())();
                                py_raise((G<"http.cookiejar.LoadError">())((S<"invalid Set-Cookie3 format file %r: %r">()) % py_tuple({ filename, line })));
                            }
                            else
                                py_reraise(handler75.exception);
                        }
                        return none();
                    }) }, }));

            define("http.cookiejar", "MozillaCookieJar", w.make_class("MozillaCookieJar", { G<"http.cookiejar.FileCookieJar">() }, {
                { "_really_load", w.method("MozillaCookieJar", "_really_load", { py_positional("f"), py_positional("filename"), py_positional("ignore_discard"), py_positional("ignore_expires") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        py filename = a[2];
                        py ignore_discard = a[3];
                        py ignore_expires = a[4];
                        py c;
                        py discard;
                        py domain;
                        py domain_specified;
                        py expires;
                        py initial_dot;
                        py line;
                        py name;
                        py now;
                        py path;
                        py rest;
                        py secure;
                        py value;
                        now = (Mod<"time">().attr("time"))();
                        if (!(G<"http.cookiejar.NETSCAPE_MAGIC_RGX">()).method("match", { f.method("readline") }).truthy())
                        {
                            py_raise((G<"http.cookiejar.LoadError">())((S<"%r does not look like a Netscape format cookies file">()) % filename));
                        }
                        try
                        {
                            while (([&]() -> py { line = f.method("readline"); return line; }()) != (S<"">()))
                            {
                                rest = py_dict();
                                // httponly is a cookie flag as defined in rfc6265
                                // when encoded in a netscape cookie file,
                                // the line is prepended with "#HttpOnly_"
                                if (line.method("startswith", { G<"http.cookiejar.HTTPONLY_PREFIX">() }).truthy())
                                {
                                    rest.set_item(G<"http.cookiejar.HTTPONLY_ATTR">(), S<"">());
                                    line = line[B<"slice">()(py{ (G<"http.cookiejar.HTTPONLY_PREFIX">()).size() }, none(), none())];
                                }
                                // last field may be absent, so keep any trailing tab
                                if (line.method("endswith", { S<"\n">() }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-1>(), none())];
                                }
                                // skip comments and blank lines XXX what is $ for?
                                if ((line.method("strip").method("startswith", { py_tuple({ S<"#">(), S<"$">() }) }).truthy() || (line.method("strip") == (S<"">()))))
                                {
                                    continue;
                                }
                                auto const items84 = unpack(line.method("split", { S<"\t">() }), 7);
                                domain = items84[0];
                                domain_specified = items84[1];
                                path = items84[2];
                                secure = items84[3];
                                expires = items84[4];
                                name = items84[5];
                                value = items84[6];
                                secure = boolean(secure == (S<"TRUE">()));
                                domain_specified = boolean(domain_specified == (S<"TRUE">()));
                                if (name == (S<"">()))
                                {
                                    // cookies.txt regards 'Set-Cookie: foo' as a cookie
                                    // with no name, whereas http.cookiejar regards it as a
                                    // cookie with no value.
                                    name = value;
                                    value = none();
                                }
                                initial_dot = domain.method("startswith", { S<".">() });
                                if (!(domain_specified == initial_dot))
                                    py_raise(B<"AssertionError">()());
                                discard = boolean(false);
                                // curl and Wget set expires to 0 for session cookies.
                                if (((expires == (S<"0">())) || (expires == (S<"">()))))
                                {
                                    expires = none();
                                    discard = boolean(true);
                                }
                                // assume path_specified is false
                                c = (G<"http.cookiejar.Cookie">())(I<0>(), name, value, none(), boolean(false), domain, domain_specified, initial_dot, path, boolean(false), secure, expires, discard, none(), none(), rest);
                                if (((!ignore_discard.truthy()) && c.attr("discard").truthy()))
                                {
                                    continue;
                                }
                                if (((!ignore_expires.truthy()) && c.method("is_expired", { now }).truthy()))
                                {
                                    continue;
                                }
                                self.method("set_cookie", { c });
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler82;
                            if (matches(handler82.exception, B<"OSError">()))
                            {
                                py_reraise(handler82.exception);
                            }
                            else if (matches(handler82.exception, B<"Exception">()))
                            {
                                (G<"http.cookiejar._warn_unhandled_exception">())();
                                py_raise((G<"http.cookiejar.LoadError">())((S<"invalid Netscape format cookies file %r: %r">()) % py_tuple({ filename, line })));
                            }
                            else
                                py_reraise(handler82.exception);
                        }
                        return none();
                    }) },
                { "save", w.method("MozillaCookieJar", "save", { py_positional("filename", none()), py_positional("ignore_discard", boolean(false)), py_positional("ignore_expires", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py ignore_discard = a[2];
                        py ignore_expires = a[3];
                        py cookie;
                        py domain;
                        py expires;
                        py f;
                        py initial_dot;
                        py name;
                        py now;
                        py secure;
                        py value;
                        if (filename.is_none())
                        {
                            if (!self.attr("filename").is_none())
                            {
                                filename = self.attr("filename");
                            }
                            else
                            {
                                py_raise((B<"ValueError">())(G<"http.cookiejar.MISSING_FILENAME_TEXT">()));
                            }
                        }
                        {
                            py const manager85 = (Mod<"os">().attr("fdopen"))((Mod<"os">().attr("open"))(filename, ((Mod<"os">().attr("O_CREAT")) | (Mod<"os">().attr("O_WRONLY"))) | (Mod<"os">().attr("O_TRUNC")), I<384>()), S<"w">());
                            py const exit85 = pyclass::special_method(manager85, "__exit__");
                            py const value85 = pyclass::special_method(manager85, "__enter__")(manager85);
                            f = value85;
                            [[maybe_unused]] int flow85 = 0;
                            [[maybe_unused]] py result85;
                            bool exited85 = false;
                            try
                            {
                                f.method("write", { G<"http.cookiejar.NETSCAPE_HEADER_TEXT">() });
                                now = (Mod<"time">().attr("time"))();
                                for (py const& item : iterate{ self })
                                {
                                    cookie = item;
                                    domain = cookie.attr("domain");
                                    if (((!ignore_discard.truthy()) && cookie.attr("discard").truthy()))
                                    {
                                        continue;
                                    }
                                    if (((!ignore_expires.truthy()) && cookie.method("is_expired", { now }).truthy()))
                                    {
                                        continue;
                                    }
                                    if (cookie.attr("secure").truthy())
                                    {
                                        secure = S<"TRUE">();
                                    }
                                    else
                                    {
                                        secure = S<"FALSE">();
                                    }
                                    if (domain.method("startswith", { S<".">() }).truthy())
                                    {
                                        initial_dot = S<"TRUE">();
                                    }
                                    else
                                    {
                                        initial_dot = S<"FALSE">();
                                    }
                                    if (!cookie.attr("expires").is_none())
                                    {
                                        expires = (B<"str">())(cookie.attr("expires"));
                                    }
                                    else
                                    {
                                        expires = S<"">();
                                    }
                                    if (cookie.attr("value").is_none())
                                    {
                                        // cookies.txt regards 'Set-Cookie: foo' as a cookie
                                        // with no name, whereas http.cookiejar regards it as a
                                        // cookie with no value.
                                        name = S<"">();
                                        value = cookie.attr("name");
                                    }
                                    else
                                    {
                                        name = cookie.attr("name");
                                        value = cookie.attr("value");
                                    }
                                    if (cookie.method("has_nonstandard_attr", { G<"http.cookiejar.HTTPONLY_ATTR">() }).truthy())
                                    {
                                        domain = (G<"http.cookiejar.HTTPONLY_PREFIX">()) + domain;
                                    }
                                    f.method("write", { (S<"\t">()).method("join", { py_list({ domain, initial_dot, cookie.attr("path"), secure, expires, name, value }) }) + (S<"\n">()) });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler85;
                                exited85 = true;
                                if (!exit85(manager85, type_of(handler85.exception), handler85.exception, handler85.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler85.exception);
                            }
                            finally85:
                            if (!exited85)
                                exit85(manager85, none(), none(), none());
                            if (flow85 == 1)
                                return result85;
                        }
                        return none();
                    }) }, }));
        }

        bool http_cookiejar_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_http_cookiejar();
                }
                if (auto const doc = http_cookiejar_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("copy", py{ fn_import("copy") }.release());
                aAttributes.emplace_back("datetime", py{ fn_import("datetime") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("_threading", py{ fn_import("threading") }.release());
                aAttributes.emplace_back("http", py{ fn_import("http") }.release());
                aAttributes.emplace_back("timegm", import_attribute("calendar", "timegm").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("http.cookiejar", http_cookiejar_module);
    }
}
