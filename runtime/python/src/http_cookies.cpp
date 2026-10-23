/*
  http_cookies.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/http/cookies.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/http/cookies.py's copyright: Copyright 2000 by Timothy O'Malley
  <timo@alum.mit.edu>. All Rights Reserved. Used under its permission notice (see runtime/python/NOTICE).
*/

// The module http.cookies (CPython 3.13's Lib/http/cookies.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "http_cookies_docs.hpp"

namespace neos::python_runtime::detail::http_cookies_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "http.cookies.name"))
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
                py_raise("SystemError", std::string{ "http.cookies: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"http.cookies.name">()))
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
            if (modules_imported("http.cookies"))
                return py{ fn_import("http.cookies") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "http.cookies" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "http.cookies", http_cookies_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- http.cookies (Lib/http/cookies.py)

        void make_http_cookies()
        {
            static module_writer const& w = writer("http.cookies");
            // ###
            // Copyright 2000 by Timothy O'Malley <timo@alum.mit.edu>
            // 
            // All Rights Reserved
            // 
            // Permission to use, copy, modify, and distribute this software
            // and its documentation for any purpose and without fee is hereby
            // granted, provided that the above copyright notice appear in all
            // copies and that both that copyright notice and this permission
            // notice appear in supporting documentation, and that the name of
            // Timothy O'Malley  not be used in advertising or publicity
            // pertaining to distribution of the software without specific, written
            // prior permission.
            // 
            // Timothy O'Malley DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
            // SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
            // AND FITNESS, IN NO EVENT SHALL Timothy O'Malley BE LIABLE FOR
            // ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
            // WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
            // WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
            // ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
            // PERFORMANCE OF THIS SOFTWARE.
            // 
            // ###
            // 
            // Id: Cookie.py,v 2.29 2000/08/23 05:28:49 timo Exp
            // by Timothy O'Malley <timo@alum.mit.edu>
            // 
            // Cookie.py is a Python module for the handling of HTTP
            // cookies as a Python dictionary.  See RFC 2109 for more
            // information on cookies.
            // 
            // The original idea to treat Cookies as a dictionary came from
            // Dave Mitchell (davem@magnet.com) in 1995, when he released the
            // first version of nscookie.py.
            // 
            // ###
            // 
            // Import our required modules
            // 
            define("http.cookies", "__all__", py_list({ S<"CookieError">(), S<"BaseCookie">(), S<"SimpleCookie">() }));
            define("http.cookies", "_nulljoin", (S<"">()).attr("join"));
            define("http.cookies", "_semispacejoin", (S<"; ">()).attr("join"));
            define("http.cookies", "_spacejoin", (S<" ">()).attr("join"));
            // 
            // Define an exception visible to External modules
            // 
            define("http.cookies", "CookieError", w.make_class("CookieError", { B<"Exception">() }, {
 }));

            // These quoting routines conform to the RFC2109 specification, which in
            // turn references the character definitions from RFC2068.  They provide
            // a two-way quoting algorithm.  Any non-text character is translated
            // into a 4 character sequence: a forward-slash followed by the
            // three-digit octal equivalent of the character.  Any '\' or '"' is
            // quoted with a preceding '\' slash.
            // Because of the way browsers really handle cookies (as opposed to what
            // the RFC says) we also encode "," and ";".
            // 
            // These are taken from RFC2068 and RFC2109.
            // _LegalChars       is the list of chars which don't require "'s
            // _Translator       hash-table for fast quoting
            // 
            define("http.cookies", "_LegalChars", ((Mod<"string">().attr("ascii_letters")) + (Mod<"string">().attr("digits"))) + (S<"!#$%&'*+-.^_`|~:">()));
            define("http.cookies", "_UnescapedChars", (G<"http.cookies._LegalChars">()) + (S<" ()/<=>?@[]{}">()));
            define("http.cookies", "_Translator", [&]() -> py { py const comprehension1 = py_dict(); py n; for (py const& item : iterate{ (B<"set">())((B<"range">())(I<256>())) - (B<"set">())((B<"map">())(B<"ord">(), G<"http.cookies._UnescapedChars">())) }) { n = item; comprehension1.set_item(n, (S<"\\%03o">()) % n); } return comprehension1; }());
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (G<"http.cookies._Translator">()).method("update", { dict_of({ { (B<"ord">())(S<"\"">()), S<"\\\"">() }, { (B<"ord">())(S<"\\">()), S<"\\\\">() } }) });
                    return none();
                }))();
            define("http.cookies", "_is_legal_key", (Mod<"re">()).method("compile", { (S<"[%s]+">()) % (Mod<"re">()).method("escape", { G<"http.cookies._LegalChars">() }) }).attr("fullmatch"));
            define("http.cookies", "_control_character_re", (Mod<"re">()).method("compile", { S<"[\\x00-\\x1F\\x7F]">() }));
            define("http.cookies", "_has_control_character", w.function("_has_control_character", "_has_control_character", {  }, [](arguments a) -> py
                {
                    py val = a[0];
                    return (B<"any">())([&]() -> py { py const comprehension2 = py_list(); py v; for (py const& item : iterate{ val }) { v = item; comprehension2.method("append", { (G<"http.cookies._control_character_re">()).method("search", { (B<"str">())(v) }) }); } return comprehension2; }());
                }, true));

            define("http.cookies", "_quote", w.function("_quote", "_quote", { py_positional("str") }, [](arguments a) -> py
                {
                    py str = a[0];
                    if ((str.is_none() || (G<"http.cookies._is_legal_key">())(str).truthy()))
                    {
                        return str;
                    }
                    else
                    {
                        return ((S<"\"">()) + str.method("translate", { G<"http.cookies._Translator">() })) + (S<"\"">());
                    }
                    return none();
                }));

            define("http.cookies", "_unquote_sub", (Mod<"re">()).method("compile", { S<"\\\\(?:([0-3][0-7][0-7])|(.))">() }).attr("sub"));
            define("http.cookies", "_unquote_replace", w.function("_unquote_replace", "_unquote_replace", { py_positional("m") }, [](arguments a) -> py
                {
                    py m = a[0];
                    if (m[I<1>()].truthy())
                    {
                        return (B<"chr">())((B<"int">())(m[I<1>()], I<8>()));
                    }
                    else
                    {
                        return m[I<2>()];
                    }
                    return none();
                }));

            define("http.cookies", "_unquote", w.function("_unquote", "_unquote", { py_positional("str") }, [](arguments a) -> py
                {
                    py str = a[0];
                    if ((str.is_none() || (py{ str.size() } < (I<2>()))))
                    {
                        return str;
                    }
                    if (((str[I<0>()] != (S<"\"">())) || (str[I<-1>()] != (S<"\"">()))))
                    {
                        return str;
                    }
                    // We have to assume that we must decode this string.
                    // Down to work.
                    // Remove the "s
                    str = str[B<"slice">()(I<1>(), I<-1>(), none())];
                    // Check for special sequences.  Examples:
                    // \012 --> \n
                    // \"   --> "
                    // 
                    return (G<"http.cookies._unquote_sub">())(G<"http.cookies._unquote_replace">(), str);
                }));

            // The _getdate() routine is used to set the expiration time in the cookie's HTTP
            // header.  By default, _getdate() returns the current time in the appropriate
            // "expires" format for a Set-Cookie header.  The one optional argument is an
            // offset from now, in seconds.  For example, an offset of -3600 means "one hour
            // ago".  The offset may be a floating-point number.
            // 
            define("http.cookies", "_weekdayname", py_list({ S<"Mon">(), S<"Tue">(), S<"Wed">(), S<"Thu">(), S<"Fri">(), S<"Sat">(), S<"Sun">() }));
            define("http.cookies", "_monthname", py_list({ none(), S<"Jan">(), S<"Feb">(), S<"Mar">(), S<"Apr">(), S<"May">(), S<"Jun">(), S<"Jul">(), S<"Aug">(), S<"Sep">(), S<"Oct">(), S<"Nov">(), S<"Dec">() }));
            define("http.cookies", "_getdate", w.function("_getdate", "_getdate", { py_positional("future", I<0>()), py_positional("weekdayname", G<"http.cookies._weekdayname">()), py_positional("monthname", G<"http.cookies._monthname">()) }, [](arguments a) -> py
                {
                    py future = a[0];
                    py weekdayname = a[1];
                    py monthname = a[2];
                    py day;
                    py gmtime;
                    py hh;
                    py mm;
                    py month;
                    py now;
                    py ss;
                    py time;
                    py wd;
                    py y;
                    py year;
                    py z;
                    gmtime = M("time", "gmtime");
                    time = M("time", "time");
                    now = time();
                    auto const items3 = unpack(gmtime(now + future), 9);
                    year = items3[0];
                    month = items3[1];
                    day = items3[2];
                    hh = items3[3];
                    mm = items3[4];
                    ss = items3[5];
                    wd = items3[6];
                    y = items3[7];
                    z = items3[8];
                    return (S<"%s, %02d %3s %4d %02d:%02d:%02d GMT">()) % py_tuple({ weekdayname[wd], day, monthname[month], year, hh, mm, ss });
                }));

                // RFC 2109 lists these attributes as reserved:
                // path       comment         domain
                // max-age    secure      version
                // 
                // For historical reasons, these attributes are also reserved:
                // expires
                // 
                // This is an extension from Microsoft:
                // httponly
                // 
                // This dictionary provides a mapping from the lowercase
                // variant on the left to the appropriate traditional
                // formatting on the right.
            define("http.cookies", "Morsel", w.make_class("Morsel", { B<"dict">() }, {
                { "_reserved", dict_of({ { S<"expires">(), S<"expires">() }, { S<"path">(), S<"Path">() }, { S<"comment">(), S<"Comment">() }, { S<"domain">(), S<"Domain">() }, { S<"max-age">(), S<"Max-Age">() }, { S<"secure">(), S<"Secure">() }, { S<"httponly">(), S<"HttpOnly">() }, { S<"version">(), S<"Version">() }, { S<"samesite">(), S<"SameSite">() } }) },
                { "_flags", set_of({ S<"secure">(), S<"httponly">() }) },
                { "__init__", w.method("Morsel", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key;
                        py const value4 = none();
                        self.set_attr("_key", value4);
                        self.set_attr("_value", value4);
                        self.set_attr("_coded_value", value4);
                        // Set default attributes
                        for (py const& item : iterate{ self.attr("_reserved") })
                        {
                            key = item;
                            (B<"dict">()).method("__setitem__", { self, key, S<"">() });
                        }
                        return none();
                    }) },
                { "key", B<"property">()(w.method("Morsel", "key", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_key");
                    })) },
                { "value", B<"property">()(w.method("Morsel", "value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_value");
                    })) },
                { "coded_value", B<"property">()(w.method("Morsel", "coded_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_coded_value");
                    })) },
                { "__setitem__", w.method("Morsel", "__setitem__", { py_positional("K"), py_positional("V") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py K = a[1];
                        py V = a[2];
                        K = K.method("lower");
                        if (!self.attr("_reserved").contains(K))
                        {
                            py_raise((G<"http.cookies.CookieError">())((S<"Invalid attribute %r">()) % py_tuple({ K })));
                        }
                        if ((G<"http.cookies._has_control_character">())(K, V).truthy())
                        {
                            py_raise((G<"http.cookies.CookieError">())(S(std::string{ "Control characters are not allowed in cookies " } + K.repr() + " " + V.repr())));
                        }
                        (B<"dict">()).method("__setitem__", { self, K, V });
                        return none();
                    }) },
                { "setdefault", w.method("Morsel", "setdefault", { py_positional("key"), py_positional("val", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py val = a[2];
                        key = key.method("lower");
                        if (!self.attr("_reserved").contains(key))
                        {
                            py_raise((G<"http.cookies.CookieError">())((S<"Invalid attribute %r">()) % py_tuple({ key })));
                        }
                        if ((G<"http.cookies._has_control_character">())(key, val).truthy())
                        {
                            py_raise((G<"http.cookies.CookieError">())((S<"Control characters are not allowed in cookies %r %r">()) % py_tuple({ key, val })));
                        }
                        return (B<"dict">()).method("setdefault", { self, key, val });
                    }) },
                { "__eq__", w.method("Morsel", "__eq__", { py_positional("morsel") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py morsel = a[1];
                        if (!isinstance(morsel, G<"http.cookies.Morsel">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return [&]() -> py { py const t = (B<"dict">()).method("__eq__", { self, morsel }); return !t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("_value") == morsel.attr("_value")); return !t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("_key") == morsel.attr("_key")); return !t.truthy() ? t : boolean(self.attr("_coded_value") == morsel.attr("_coded_value")); }(); }(); }();
                    }) },
                { "__ne__", (B<"object">()).attr("__ne__") },
                { "copy", w.method("Morsel", "copy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py morsel;
                        morsel = (G<"http.cookies.Morsel">())();
                        (B<"dict">()).method("update", { morsel, self });
                        morsel.attr("__dict__").method("update", { self.attr("__dict__") });
                        return morsel;
                    }) },
                { "update", w.method("Morsel", "update", { py_positional("values") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py values = a[1];
                        py data;
                        py key;
                        py val;
                        data = py_dict();
                        for (py const& item : iterate{ (B<"dict">())(values).method("items") })
                        {
                            auto const items7 = unpack(item, 2);
                            key = items7[0];
                            val = items7[1];
                            key = key.method("lower");
                            if (!self.attr("_reserved").contains(key))
                            {
                                py_raise((G<"http.cookies.CookieError">())((S<"Invalid attribute %r">()) % py_tuple({ key })));
                            }
                            if ((G<"http.cookies._has_control_character">())(key, val).truthy())
                            {
                                py_raise((G<"http.cookies.CookieError">())(S(std::string{ "Control characters are not allowed in cookies " } + key.repr() + " " + val.repr())));
                            }
                            data.set_item(key, val);
                        }
                        (B<"dict">()).method("update", { self, data });
                        return none();
                    }) },
                { "__ior__", w.method("Morsel", "__ior__", { py_positional("values") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py values = a[1];
                        self.method("update", { values });
                        return self;
                    }) },
                { "isReservedKey", w.method("Morsel", "isReservedKey", { py_positional("K") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py K = a[1];
                        return boolean(self.attr("_reserved").contains(K.method("lower")));
                    }) },
                { "set", w.method("Morsel", "set", { py_positional("key"), py_positional("val"), py_positional("coded_val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py val = a[2];
                        py coded_val = a[3];
                        if (self.attr("_reserved").contains(key.method("lower")))
                        {
                            py_raise((G<"http.cookies.CookieError">())((S<"Attempt to set a reserved key %r">()) % py_tuple({ key })));
                        }
                        if (!(G<"http.cookies._is_legal_key">())(key).truthy())
                        {
                            py_raise((G<"http.cookies.CookieError">())((S<"Illegal key %r">()) % py_tuple({ key })));
                        }
                        if ((G<"http.cookies._has_control_character">())(key, val, coded_val).truthy())
                        {
                            py_raise((G<"http.cookies.CookieError">())((S<"Control characters are not allowed in cookies %r %r %r">()) % py_tuple({ key, val, coded_val })));
                        }
                        // It's a good key, so save it.
                        self.set_attr("_key", key);
                        self.set_attr("_value", val);
                        self.set_attr("_coded_value", coded_val);
                        return none();
                    }) },
                { "__getstate__", w.method("Morsel", "__getstate__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return dict_of({ { S<"key">(), self.attr("_key") }, { S<"value">(), self.attr("_value") }, { S<"coded_value">(), self.attr("_coded_value") } });
                    }) },
                { "__setstate__", w.method("Morsel", "__setstate__", { py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py state = a[1];
                        py coded_value;
                        py key;
                        py value;
                        key = state[S<"key">()];
                        value = state[S<"value">()];
                        coded_value = state[S<"coded_value">()];
                        if ((G<"http.cookies._has_control_character">())(key, value, coded_value).truthy())
                        {
                            py_raise((G<"http.cookies.CookieError">())(S(std::string{ "Control characters are not allowed in cookies " } + key.repr() + " " + value.repr() + " " + coded_value.repr())));
                        }
                        self.set_attr("_key", key);
                        self.set_attr("_value", value);
                        self.set_attr("_coded_value", coded_value);
                        return none();
                    }) },
                { "output", w.method("Morsel", "output", { py_positional("attrs", none()), py_positional("header", S<"Set-Cookie:">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py header = a[2];
                        return (S<"%s %s">()) % py_tuple({ header, self.method("OutputString", { attrs }) });
                    }) },
                { "__str__", w.method("Morsel", "output", { py_positional("attrs", none()), py_positional("header", S<"Set-Cookie:">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py header = a[2];
                        return (S<"%s %s">()) % py_tuple({ header, self.method("OutputString", { attrs }) });
                    }) },
                { "__repr__", w.method("Morsel", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s: %s>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.method("OutputString") });
                    }) },
                { "js_output", w.method("Morsel", "js_output", { py_positional("attrs", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py output_encoded;
                        py output_string;
                        py urllib;
                        urllib = module_of("urllib");
                        // Print javascript
                        output_string = self.method("OutputString", { attrs });
                        if ((G<"http.cookies._has_control_character">())(output_string).truthy())
                        {
                            py_raise((G<"http.cookies.CookieError">())(S<"Control characters are not allowed in cookies">()));
                        }
                        // Base64-encode value to avoid template
                        // injection in cookie values.
                        output_encoded = urllib.attr("parse").method("quote", { output_string }, { { "safe", S<"">() }, { "encoding", S<"utf-8">() } });
                        return (S<"\n        <script type=\"text/javascript\">\n        <!-- begin hiding\n        document.cookie = decodeURIComponent(\"%s\");\n        // end hiding -->\n        </script>\n        ">()) % py_tuple({ output_encoded });
                    }) },
                { "OutputString", w.method("Morsel", "OutputString", { py_positional("attrs", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py append;
                        py items;
                        py key;
                        py result;
                        py value;
                        result = py_list();
                        append = result.attr("append");
                        // First, the key=value pair
                        append((S<"%s=%s">()) % py_tuple({ self.attr("key"), self.attr("coded_value") }));
                        // Now add any defined attributes
                        if (attrs.is_none())
                        {
                            attrs = self.attr("_reserved");
                        }
                        items = (B<"sorted">())(self.method("items"));
                        for (py const& item : iterate{ items })
                        {
                            auto const items9 = unpack(item, 2);
                            key = items9[0];
                            value = items9[1];
                            if (value == (S<"">()))
                            {
                                continue;
                            }
                            if (!attrs.contains(key))
                            {
                                continue;
                            }
                            if (((key == (S<"expires">())) && isinstance(value, B<"int">())))
                            {
                                append((S<"%s=%s">()) % py_tuple({ self.attr("_reserved")[key], (G<"http.cookies._getdate">())(value) }));
                            }
                            else if (((key == (S<"max-age">())) && isinstance(value, B<"int">())))
                            {
                                append((S<"%s=%d">()) % py_tuple({ self.attr("_reserved")[key], value }));
                            }
                            else if (((key == (S<"comment">())) && isinstance(value, B<"str">())))
                            {
                                append((S<"%s=%s">()) % py_tuple({ self.attr("_reserved")[key], (G<"http.cookies._quote">())(value) }));
                            }
                            else if (self.attr("_flags").contains(key))
                            {
                                if (value.truthy())
                                {
                                    append((B<"str">())(self.attr("_reserved")[key]));
                                }
                            }
                            else
                            {
                                append((S<"%s=%s">()) % py_tuple({ self.attr("_reserved")[key], value }));
                            }
                        }
                        // Return the result
                        return (G<"http.cookies._semispacejoin">())(result);
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

            // 
            // Pattern for finding cookie
            // 
            // This used to be strict parsing based on the RFC2109 and RFC2068
            // specifications.  I have since discovered that MSIE 3.0x doesn't
            // follow the character rules outlined in those specs.  As a
            // result, the parsing rules here are less strict.
            // 
            define("http.cookies", "_LegalKeyChars", S<"\\w\\d!#%&'~_`><@,:/\\$\\*\\+\\-\\.\\^\\|\\)\\(\\?\\}\\{\\=">());
            define("http.cookies", "_LegalValueChars", (G<"http.cookies._LegalKeyChars">()) + (S<"\\[\\]">()));
            define("http.cookies", "_CookiePattern", (Mod<"re">()).method("compile", { ((((S<"\n    \\s*                            # Optional whitespace at start of cookie\n    (?P<key>                       # Start of group 'key'\n    [">()) + (G<"http.cookies._LegalKeyChars">())) + (S<"]+?   # Any word of at least one letter\n    )                              # End of group 'key'\n    (                              # Optional group: there may not be a value.\n    \\s*=\\s*                          # Equal Sign\n    (?P<val>                         # Start of group 'val'\n    \"(?:[^\\\\\"]|\\\\.)*\"                  # Any double-quoted string\n    |                                  # or\n    # Special case for \"expires\" attr\n    (\\w{3,6}day|\\w{3}),\\s              # Day of the week or abbreviated day\n    [\\w\\d\\s-]{9,11}\\s[\\d:]{8}\\sGMT     # Date and time in specific format\n    |                                  # or\n    [">())) + (G<"http.cookies._LegalValueChars">())) + (S<"]*      # Any word or empty string\n    )                                # End of group 'val'\n    )?                             # End of optional value group\n    \\s*                            # Any number of spaces.\n    (\\s+|;|$)                      # Ending either at space, semicolon, or EOS.\n    ">()), (Mod<"re">().attr("ASCII")) | (Mod<"re">().attr("VERBOSE")) }));
            // At long last, here is the cookie class.  Using this class is almost just like
            // using a dictionary.  See this module's docstring for example usage.
            // 
            define("http.cookies", "BaseCookie", w.make_class("BaseCookie", { B<"dict">() }, {
                { "value_decode", w.method("BaseCookie", "value_decode", { py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val = a[1];
                        return py_tuple({ val, val });
                    }) },
                { "value_encode", w.method("BaseCookie", "value_encode", { py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val = a[1];
                        py strval;
                        strval = (B<"str">())(val);
                        return py_tuple({ strval, strval });
                    }) },
                { "__init__", w.method("BaseCookie", "__init__", { py_positional("input", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        if (input.truthy())
                        {
                            self.method("load", { input });
                        }
                        return none();
                    }) },
                { "_BaseCookie__set", w.method("BaseCookie", "__set", { py_positional("key"), py_positional("real_value"), py_positional("coded_value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py real_value = a[2];
                        py coded_value = a[3];
                        py M_;
                        M_ = self.method("get", { key, (G<"http.cookies.Morsel">())() });
                        M_.method("set", { key, real_value, coded_value });
                        (B<"dict">()).method("__setitem__", { self, key, M_ });
                        return none();
                    }) },
                { "__setitem__", w.method("BaseCookie", "__setitem__", { py_positional("key"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py value = a[2];
                        py cval;
                        py rval;
                        if (isinstance(value, G<"http.cookies.Morsel">()))
                        {
                            // allow assignment of constructed Morsels (e.g. for pickling)
                            (B<"dict">()).method("__setitem__", { self, key, value });
                        }
                        else
                        {
                            auto const items10 = unpack(self.method("value_encode", { value }), 2);
                            rval = items10[0];
                            cval = items10[1];
                            self.method("_BaseCookie__set", { key, rval, cval });
                        }
                        return none();
                    }) },
                { "output", w.method("BaseCookie", "output", { py_positional("attrs", none()), py_positional("header", S<"Set-Cookie:">()), py_positional("sep", S<"\r\n">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py header = a[2];
                        py sep = a[3];
                        py items;
                        py key;
                        py result;
                        py value;
                        py value_output;
                        result = py_list();
                        items = (B<"sorted">())(self.method("items"));
                        for (py const& item : iterate{ items })
                        {
                            auto const items12 = unpack(item, 2);
                            key = items12[0];
                            value = items12[1];
                            value_output = value.method("output", { attrs, header });
                            if ((G<"http.cookies._has_control_character">())(value_output).truthy())
                            {
                                py_raise((G<"http.cookies.CookieError">())(S<"Control characters are not allowed in cookies">()));
                            }
                            result.method("append", { value_output });
                        }
                        return sep.method("join", { result });
                    }) },
                { "__str__", w.method("BaseCookie", "output", { py_positional("attrs", none()), py_positional("header", S<"Set-Cookie:">()), py_positional("sep", S<"\r\n">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py header = a[2];
                        py sep = a[3];
                        py items;
                        py key;
                        py result;
                        py value;
                        py value_output;
                        result = py_list();
                        items = (B<"sorted">())(self.method("items"));
                        for (py const& item : iterate{ items })
                        {
                            auto const items12 = unpack(item, 2);
                            key = items12[0];
                            value = items12[1];
                            value_output = value.method("output", { attrs, header });
                            if ((G<"http.cookies._has_control_character">())(value_output).truthy())
                            {
                                py_raise((G<"http.cookies.CookieError">())(S<"Control characters are not allowed in cookies">()));
                            }
                            result.method("append", { value_output });
                        }
                        return sep.method("join", { result });
                    }) },
                { "__repr__", w.method("BaseCookie", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py items;
                        py key;
                        py l;
                        py value;
                        l = py_list();
                        items = (B<"sorted">())(self.method("items"));
                        for (py const& item : iterate{ items })
                        {
                            auto const items14 = unpack(item, 2);
                            key = items14[0];
                            value = items14[1];
                            l.method("append", { (S<"%s=%s">()) % py_tuple({ key, S(value.attr("value").repr()) }) });
                        }
                        return (S<"<%s: %s>">()) % py_tuple({ self.attr("__class__").attr("__name__"), (G<"http.cookies._spacejoin">())(l) });
                    }) },
                { "js_output", w.method("BaseCookie", "js_output", { py_positional("attrs", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py items;
                        py key;
                        py result;
                        py value;
                        result = py_list();
                        items = (B<"sorted">())(self.method("items"));
                        for (py const& item : iterate{ items })
                        {
                            auto const items16 = unpack(item, 2);
                            key = items16[0];
                            value = items16[1];
                            result.method("append", { value.method("js_output", { attrs }) });
                        }
                        return (G<"http.cookies._nulljoin">())(result);
                    }) },
                { "load", w.method("BaseCookie", "load", { py_positional("rawdata") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rawdata = a[1];
                        py key;
                        py value;
                        if (isinstance(rawdata, B<"str">()))
                        {
                            self.method("_BaseCookie__parse_string", { rawdata });
                        }
                        else
                        {
                            // self.update() wouldn't call our custom __setitem__
                            for (py const& item : iterate{ rawdata.method("items") })
                            {
                                auto const items18 = unpack(item, 2);
                                key = items18[0];
                                value = items18[1];
                                self.set_item(key, value);
                            }
                        }
                        return none();
                    }) },
                { "_BaseCookie__parse_string", w.method("BaseCookie", "__parse_string", { py_positional("str"), py_positional("patt", G<"http.cookies._CookiePattern">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py str = a[1];
                        py patt = a[2];
                        py M_;
                        py TYPE_ATTRIBUTE;
                        py TYPE_KEYVALUE;
                        py cval;
                        py i;
                        py key;
                        py match;
                        py morsel_seen;
                        py n;
                        py parsed_items;
                        py rval;
                        py tp;
                        py value;
                        i = I<0>();
                        n = py{ str.size() };
                        parsed_items = py_list();
                        morsel_seen = boolean(false);
                        TYPE_ATTRIBUTE = I<1>();
                        TYPE_KEYVALUE = I<2>();
                        // We first parse the whole cookie string and reject it if it's
                        // syntactically invalid (this helps avoid some classes of injection
                        // attacks).
                        while (((I<0>()) <= i) && (i < n))
                        {
                            // Start looking for a cookie
                            match = patt.method("match", { str, i });
                            if (!match.truthy())
                            {
                                // No more cookies
                                break;
                            }
                            auto const items20 = unpack(py_tuple({ match.method("group", { S<"key">() }), match.method("group", { S<"val">() }) }), 2);
                            key = items20[0];
                            value = items20[1];
                            i = match.method("end", { I<0>() });
                            if (key[I<0>()] == (S<"$">()))
                            {
                                if (!morsel_seen.truthy())
                                {
                                    // We ignore attributes which pertain to the cookie
                                    // mechanism as a whole, such as "$Version".
                                    // See RFC 2965. (Does anyone care?)
                                    continue;
                                }
                                parsed_items.method("append", { py_tuple({ TYPE_ATTRIBUTE, key[B<"slice">()(I<1>(), none(), none())], value }) });
                            }
                            else if ((G<"http.cookies.Morsel">()).attr("_reserved").contains(key.method("lower")))
                            {
                                if (!morsel_seen.truthy())
                                {
                                    // Invalid cookie string
                                    return none();
                                }
                                if (value.is_none())
                                {
                                    if ((G<"http.cookies.Morsel">()).attr("_flags").contains(key.method("lower")))
                                    {
                                        parsed_items.method("append", { py_tuple({ TYPE_ATTRIBUTE, key, boolean(true) }) });
                                    }
                                    else
                                    {
                                        // Invalid cookie string
                                        return none();
                                    }
                                }
                                else
                                {
                                    parsed_items.method("append", { py_tuple({ TYPE_ATTRIBUTE, key, (G<"http.cookies._unquote">())(value) }) });
                                }
                            }
                            else if (!value.is_none())
                            {
                                parsed_items.method("append", { py_tuple({ TYPE_KEYVALUE, key, self.method("value_decode", { value }) }) });
                                morsel_seen = boolean(true);
                            }
                            else
                            {
                                // Invalid cookie string
                                return none();
                            }
                        }
                        // The cookie string is valid, apply it.
                        M_ = none();
                        for (py const& item : iterate{ parsed_items })
                        {
                            auto const items22 = unpack(item, 3);
                            tp = items22[0];
                            key = items22[1];
                            value = items22[2];
                            if (tp == TYPE_ATTRIBUTE)
                            {
                                if (M_.is_none())
                                    py_raise(B<"AssertionError">()());
                                M_.set_item(key, value);
                            }
                            else
                            {
                                if (!(tp == TYPE_KEYVALUE))
                                    py_raise(B<"AssertionError">()());
                                auto const items23 = unpack(value, 2);
                                rval = items23[0];
                                cval = items23[1];
                                self.method("_BaseCookie__set", { key, rval, cval });
                                M_ = self[key];
                            }
                        }
                        return none();
                    }) }, }));

            define("http.cookies", "SimpleCookie", w.make_class("SimpleCookie", { G<"http.cookies.BaseCookie">() }, {
                { "value_decode", w.method("SimpleCookie", "value_decode", { py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val = a[1];
                        return py_tuple({ (G<"http.cookies._unquote">())(val), val });
                    }) },
                { "value_encode", w.method("SimpleCookie", "value_encode", { py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val = a[1];
                        py strval;
                        strval = (B<"str">())(val);
                        return py_tuple({ strval, (G<"http.cookies._quote">())(strval) });
                    }) }, }));
        }

        bool http_cookies_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_http_cookies();
                }
                if (auto const doc = http_cookies_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("string", py{ fn_import("string") }.release());
                aAttributes.emplace_back("types", py{ fn_import("types") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("http.cookies", http_cookies_module);
    }
}
