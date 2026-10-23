/*
  email_utils.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/utils.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.utils (CPython 3.13's Lib/email/utils.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_utils_docs.hpp"

namespace neos::python_runtime::detail::email_utils_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.utils.name"))
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
                py_raise("SystemError", std::string{ "email.utils: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.utils.name">()))
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
            if (modules_imported("email.utils"))
                return py{ fn_import("email.utils") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.utils" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.utils", email_utils_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.utils (Lib/email/utils.py)

        void make_email_utils()
        {
            static module_writer const& w = writer("email.utils");
            // Copyright (C) 2001-2010 Python Software Foundation
            // Author: Barry Warsaw
            // Contact: email-sig@python.org
            define("email.utils", "__all__", py_list({ S<"collapse_rfc2231_value">(), S<"decode_params">(), S<"decode_rfc2231">(), S<"encode_rfc2231">(), S<"formataddr">(), S<"formatdate">(), S<"format_datetime">(), S<"getaddresses">(), S<"make_msgid">(), S<"mktime_tz">(), S<"parseaddr">(), S<"parsedate">(), S<"parsedate_tz">(), S<"parsedate_to_datetime">(), S<"unquote">() }));
            define("email.utils", "COMMASPACE", S<", ">());
            define("email.utils", "EMPTYSTRING", S<"">());
            define("email.utils", "UEMPTYSTRING", S<"">());
            define("email.utils", "CRLF", S<"\r\n">());
            define("email.utils", "TICK", S<"'">());
            define("email.utils", "specialsre", (Mod<"re">()).method("compile", { S<"[][\\\\()<>@,:;\".]">() }));
            define("email.utils", "escapesre", (Mod<"re">()).method("compile", { S<"[\\\\\"]">() }));
            define("email.utils", "_has_surrogates", w.function("_has_surrogates", "_has_surrogates", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    // This check is based on the fact that unless there are surrogates, utf8
                    // (Python's default encoding) can encode any string.  This is the fastest
                    // way to check for surrogates, see bpo-11454 (moved to gh-55663) for timings.
                    try
                    {
                        s.method("encode");
                        return boolean(false);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler1;
                        if (matches(handler1.exception, B<"UnicodeEncodeError">()))
                        {
                            return boolean(true);
                        }
                        else
                            py_reraise(handler1.exception);
                    }
                    return none();
                }));

            // How to deal with a string containing bytes before handing it to the
            // application through the 'normal' interface.
            define("email.utils", "_sanitize", w.function("_sanitize", "_sanitize", { py_positional("string") }, [](arguments a) -> py
                {
                    py string = a[0];
                    py original_bytes;
                    original_bytes = string.method("encode", { S<"utf-8">(), S<"surrogateescape">() });
                    return original_bytes.method("decode", { S<"utf-8">(), S<"replace">() });
                }));

            // Helpers
            define("email.utils", "formataddr", w.function("formataddr", "formataddr", { py_positional("pair"), py_positional("charset", S<"utf-8">()) }, [](arguments a) -> py
                {
                    py pair = a[0];
                    py charset = a[1];
                    py Charset;
                    py address;
                    py encoded_name;
                    py name;
                    py quotes;
                    auto const items2 = unpack(pair, 2);
                    name = items2[0];
                    address = items2[1];
                    // The address MUST (per RFC) be ascii, so raise a UnicodeError if it isn't.
                    address.method("encode", { S<"ascii">() });
                    if (name.truthy())
                    {
                        {
                            bool otherwise3 = false;
                            try
                            {
                                name.method("encode", { S<"ascii">() });
                                otherwise3 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, B<"UnicodeEncodeError">()))
                                {
                                    if (isinstance(charset, B<"str">()))
                                    {
                                        // lazy import to improve module import time
                                        Charset = M("email.charset", "Charset");
                                        charset = Charset(charset);
                                    }
                                    encoded_name = charset.method("header_encode", { name });
                                    return (S<"%s <%s>">()) % py_tuple({ encoded_name, address });
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                            if (otherwise3)
                            {
                                quotes = S<"">();
                                if ((G<"email.utils.specialsre">()).method("search", { name }).truthy())
                                {
                                    quotes = S<"\"">();
                                }
                                name = (G<"email.utils.escapesre">()).method("sub", { S<"\\\\\\g<0>">(), name });
                                return (S<"%s%s%s <%s>">()) % py_tuple({ quotes, name, quotes, address });
                            }
                        }
                    }
                    return address;
                }));

            define("email.utils", "_iter_escaped_chars", w.function("_iter_escaped_chars", "_iter_escaped_chars", { py_positional("addr") }, [](arguments a) -> py
                {
                    return make_native_generator("_iter_escaped_chars", "_iter_escaped_chars", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py addr = a[0];
                        py ch;
                        py escape;
                        py pos;
                        pos = I<0>();
                        escape = boolean(false);
                        for (py const& item : iterate{ (B<"enumerate">())(addr) })
                        {
                            auto const items5 = unpack(item, 2);
                            pos = items5[0];
                            ch = items5[1];
                            if (escape.truthy())
                            {
                                py_yield(py_tuple({ pos, (S<"\\">()) + ch }));
                                escape = boolean(false);
                            }
                            else if (ch == (S<"\\">()))
                            {
                                escape = boolean(true);
                            }
                            else
                            {
                                py_yield(py_tuple({ pos, ch }));
                            }
                        }
                        if (escape.truthy())
                        {
                            py_yield(py_tuple({ pos, S<"\\">() }));
                        }
                        return none();
                    });
                }));

            define("email.utils", "_strip_quoted_realnames", w.function("_strip_quoted_realnames", "_strip_quoted_realnames", { py_positional("addr") }, [](arguments a) -> py
                {
                    py addr = a[0];
                    py ch;
                    py open_pos;
                    py pos;
                    py result;
                    py start;
                    if (!addr.contains((S<"\"">())))
                    {
                        // Fast path
                        return addr;
                    }
                    start = I<0>();
                    open_pos = none();
                    result = py_list();
                    for (py const& item : iterate{ (G<"email.utils._iter_escaped_chars">())(addr) })
                    {
                        auto const items7 = unpack(item, 2);
                        pos = items7[0];
                        ch = items7[1];
                        if (ch == (S<"\"">()))
                        {
                            if (open_pos.is_none())
                            {
                                open_pos = pos;
                            }
                            else
                            {
                                if (start != open_pos)
                                {
                                    result.method("append", { addr[B<"slice">()(start, open_pos, none())] });
                                }
                                start = pos + (I<1>());
                                open_pos = none();
                            }
                        }
                    }
                    if (start < py{ addr.size() })
                    {
                        result.method("append", { addr[B<"slice">()(start, none(), none())] });
                    }
                    return (S<"">()).method("join", { result });
                }));

            define("email.utils", "supports_strict_parsing", boolean(true));
            define("email.utils", "getaddresses", w.function("getaddresses", "getaddresses", { py_positional("fieldvalues"), py_keyword("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py fieldvalues = a[0];
                    py strict = a[1];
                    py a_;
                    py addr;
                    py all;
                    py n;
                    py result;
                    py v;
                    // If strict is true, if the resulting list of parsed addresses is greater
                    // than the number of fieldvalues in the input list, a parsing error has
                    // occurred and consequently a list containing a single empty 2-tuple [('',
                    // '')] is returned in its place. This is done to avoid invalid output.
                    // 
                    // Malformed input: getaddresses(['alice@example.com <bob@example.com>'])
                    // Invalid output: [('', 'alice@example.com'), ('', 'bob@example.com')]
                    // Safe output: [('', '')]
                    if (!strict.truthy())
                    {
                        all = (G<"email.utils.COMMASPACE">()).method("join", { [&]() -> py { py const comprehension8 = py_list(); py v; for (py const& item : iterate{ fieldvalues }) { v = item; comprehension8.method("append", { (B<"str">())(v) }); } return comprehension8; }() });
                        a_ = (Mod<"email._parseaddr">().attr("AddressList"))(all);
                        return a_.attr("addresslist");
                    }
                    fieldvalues = [&]() -> py { py const comprehension9 = py_list(); py v; for (py const& item : iterate{ fieldvalues }) { v = item; comprehension9.method("append", { (B<"str">())(v) }); } return comprehension9; }();
                    fieldvalues = (G<"email.utils._pre_parse_validation">())(fieldvalues);
                    addr = (G<"email.utils.COMMASPACE">()).method("join", { fieldvalues });
                    a_ = (Mod<"email._parseaddr">().attr("AddressList"))(addr);
                    result = (G<"email.utils._post_parse_validation">())(a_.attr("addresslist"));
                    // Treat output as invalid if the number of addresses is not equal to the
                    // expected number of addresses.
                    n = I<0>();
                    for (py const& item : iterate{ fieldvalues })
                    {
                        v = item;
                        // When a comma is used in the Real Name part it is not a deliminator.
                        // So strip those out before counting the commas.
                        v = (G<"email.utils._strip_quoted_realnames">())(v);
                        // Expected number of addresses: 1 + number of commas
                        n = inplace(NEOS_PY_ADD, n, (I<1>()) + v.method("count", { S<",">() }));
                    }
                    if (py{ result.size() } != n)
                    {
                        return py_list({ py_tuple({ S<"">(), S<"">() }) });
                    }
                    return result;
                }));

            define("email.utils", "_check_parenthesis", w.function("_check_parenthesis", "_check_parenthesis", { py_positional("addr") }, [](arguments a) -> py
                {
                    py addr = a[0];
                    py ch;
                    py opens;
                    py pos;
                    addr = (G<"email.utils._strip_quoted_realnames">())(addr);
                    opens = I<0>();
                    for (py const& item : iterate{ (G<"email.utils._iter_escaped_chars">())(addr) })
                    {
                        auto const items12 = unpack(item, 2);
                        pos = items12[0];
                        ch = items12[1];
                        if (ch == (S<"(">()))
                        {
                            opens = inplace(NEOS_PY_ADD, opens, I<1>());
                        }
                        else if (ch == (S<")">()))
                        {
                            opens = inplace(NEOS_PY_SUBTRACT, opens, I<1>());
                            if (opens < (I<0>()))
                            {
                                return boolean(false);
                            }
                        }
                    }
                    return boolean(opens == (I<0>()));
                }));

            define("email.utils", "_pre_parse_validation", w.function("_pre_parse_validation", "_pre_parse_validation", { py_positional("email_header_fields") }, [](arguments a) -> py
                {
                    py email_header_fields = a[0];
                    py accepted_values;
                    py v;
                    accepted_values = py_list();
                    for (py const& item : iterate{ email_header_fields })
                    {
                        v = item;
                        if (!(G<"email.utils._check_parenthesis">())(v).truthy())
                        {
                            v = S<"('', '')">();
                        }
                        accepted_values.method("append", { v });
                    }
                    return accepted_values;
                }));

            define("email.utils", "_post_parse_validation", w.function("_post_parse_validation", "_post_parse_validation", { py_positional("parsed_email_header_tuples") }, [](arguments a) -> py
                {
                    py parsed_email_header_tuples = a[0];
                    py accepted_values;
                    py v;
                    accepted_values = py_list();
                    // The parser would have parsed a correctly formatted domain-literal
                    // The existence of an [ after parsing indicates a parsing failure
                    for (py const& item : iterate{ parsed_email_header_tuples })
                    {
                        v = item;
                        if (v[I<1>()].contains((S<"[">())))
                        {
                            v = py_tuple({ S<"">(), S<"">() });
                        }
                        accepted_values.method("append", { v });
                    }
                    return accepted_values;
                }));

            define("email.utils", "_format_timetuple_and_zone", w.function("_format_timetuple_and_zone", "_format_timetuple_and_zone", { py_positional("timetuple"), py_positional("zone") }, [](arguments a) -> py
                {
                    py timetuple = a[0];
                    py zone = a[1];
                    return (S<"%s, %02d %s %04d %02d:%02d:%02d %s">()) % py_tuple({ py_list({ S<"Mon">(), S<"Tue">(), S<"Wed">(), S<"Thu">(), S<"Fri">(), S<"Sat">(), S<"Sun">() })[timetuple[I<6>()]], timetuple[I<2>()], py_list({ S<"Jan">(), S<"Feb">(), S<"Mar">(), S<"Apr">(), S<"May">(), S<"Jun">(), S<"Jul">(), S<"Aug">(), S<"Sep">(), S<"Oct">(), S<"Nov">(), S<"Dec">() })[timetuple[I<1>()] - (I<1>())], timetuple[I<0>()], timetuple[I<3>()], timetuple[I<4>()], timetuple[I<5>()], zone });
                }));

            define("email.utils", "formatdate", w.function("formatdate", "formatdate", { py_positional("timeval", none()), py_positional("localtime", boolean(false)), py_positional("usegmt", boolean(false)) }, [](arguments a) -> py
                {
                    py timeval = a[0];
                    py localtime = a[1];
                    py usegmt = a[2];
                    py dt;
                    // Note: we cannot use strftime() because that honors the locale and RFC
                    // 2822 requires that day and month names be the English abbreviations.
                    if (timeval.is_none())
                    {
                        timeval = (Mod<"time">().attr("time"))();
                    }
                    dt = (Mod<"datetime">().attr("datetime")).method("fromtimestamp", { timeval, (Mod<"datetime">().attr("timezone")).attr("utc") });
                    if (localtime.truthy())
                    {
                        dt = dt.method("astimezone");
                        usegmt = boolean(false);
                    }
                    else if (!usegmt.truthy())
                    {
                        dt = dt.method("replace", {  }, { { "tzinfo", none() } });
                    }
                    return (G<"email.utils.format_datetime">())(dt, usegmt);
                }));

            define("email.utils", "format_datetime", w.function("format_datetime", "format_datetime", { py_positional("dt"), py_positional("usegmt", boolean(false)) }, [](arguments a) -> py
                {
                    py dt = a[0];
                    py usegmt = a[1];
                    py now;
                    py zone;
                    now = dt.method("timetuple");
                    if (usegmt.truthy())
                    {
                        if ((dt.attr("tzinfo").is_none() || (dt.attr("tzinfo") != (Mod<"datetime">().attr("timezone")).attr("utc"))))
                        {
                            py_raise((B<"ValueError">())(S<"usegmt option requires a UTC datetime">()));
                        }
                        zone = S<"GMT">();
                    }
                    else if (dt.attr("tzinfo").is_none())
                    {
                        zone = S<"-0000">();
                    }
                    else
                    {
                        zone = dt.method("strftime", { S<"%z">() });
                    }
                    return (G<"email.utils._format_timetuple_and_zone">())(now, zone);
                }));

            define("email.utils", "make_msgid", w.function("make_msgid", "make_msgid", { py_positional("idstring", none()), py_positional("domain", none()) }, [](arguments a) -> py
                {
                    py idstring = a[0];
                    py domain = a[1];
                    py msgid;
                    py pid;
                    py randint;
                    py random;
                    py socket;
                    py timeval;
                    // Lazy imports to speedup module import time
                    // (no other functions in email.utils need these modules)
                    random = module_of("random");
                    socket = module_of("socket");
                    timeval = (B<"int">())((Mod<"time">().attr("time"))() * (I<100>()));
                    pid = (Mod<"os">().attr("getpid"))();
                    randint = random.method("getrandbits", { I<64>() });
                    if (idstring.is_none())
                    {
                        idstring = S<"">();
                    }
                    else
                    {
                        idstring = (S<".">()) + idstring;
                    }
                    if (domain.is_none())
                    {
                        domain = socket.method("getfqdn");
                    }
                    msgid = (S<"<%d.%d.%d%s@%s>">()) % py_tuple({ timeval, pid, randint, idstring, domain });
                    return msgid;
                }));

            define("email.utils", "parsedate_to_datetime", w.function("parsedate_to_datetime", "parsedate_to_datetime", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py dtuple;
                    py exc;
                    py parsed_date_tz;
                    py tz;
                    parsed_date_tz = (Mod<"email._parseaddr">().attr("_parsedate_tz"))(data);
                    if (parsed_date_tz.is_none())
                    {
                        py_raise((B<"ValueError">())((S<"Invalid date value or format \"%s\"">()) % (B<"str">())(data)));
                    }
                    auto const items15 = B<"list">()(parsed_date_tz).items();
                    if (items15.size() < 1u)
                        py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items15.size()) + ")");
                    dtuple = py_list(std::vector<py>(items15.begin() + 0, items15.end() - 1));
                    tz = items15[items15.size() - 1];
                    try
                    {
                        if (tz.is_none())
                        {
                            return call_ex((Mod<"datetime">().attr("datetime")), {  }, dtuple[B<"slice">()(none(), I<6>(), none())], py{});
                        }
                        return call_ex((Mod<"datetime">().attr("datetime")), {  }, dtuple[B<"slice">()(none(), I<6>(), none())], py{}, { { "tzinfo", (Mod<"datetime">().attr("timezone"))((Mod<"datetime">().attr("timedelta")).call({  }, { { "seconds", tz } })) } });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler16;
                        if (matches(handler16.exception, B<"OverflowError">()))
                        {
                            exc = handler16.exception;
                            fn_raise_from((B<"ValueError">())((S<"Invalid date value or format \"%s\"">()) % (B<"str">())(data)).get(), exc.get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler16.exception);
                    }
                    return none();
                }));

            define("email.utils", "parseaddr", w.function("parseaddr", "parseaddr", { py_positional("addr"), py_keyword("strict", boolean(true)) }, [](arguments a) -> py
                {
                    py addr = a[0];
                    py strict = a[1];
                    py addrs;
                    if (!strict.truthy())
                    {
                        addrs = (Mod<"email._parseaddr">().attr("AddressList"))(addr).attr("addresslist");
                        if (!addrs.truthy())
                        {
                            return py_tuple({ S<"">(), S<"">() });
                        }
                        return addrs[I<0>()];
                    }
                    if (isinstance(addr, B<"list">()))
                    {
                        addr = addr[I<0>()];
                    }
                    if (!isinstance(addr, B<"str">()))
                    {
                        return py_tuple({ S<"">(), S<"">() });
                    }
                    addr = (G<"email.utils._pre_parse_validation">())(py_list({ addr }))[I<0>()];
                    addrs = (G<"email.utils._post_parse_validation">())((Mod<"email._parseaddr">().attr("AddressList"))(addr).attr("addresslist"));
                    if (((!addrs.truthy()) || (py{ addrs.size() } > (I<1>()))))
                    {
                        return py_tuple({ S<"">(), S<"">() });
                    }
                    return addrs[I<0>()];
                }));

            // rfc822.unquote() doesn't properly de-backslash-ify in Python pre-2.3.
            define("email.utils", "unquote", w.function("unquote", "unquote", { py_positional("str") }, [](arguments a) -> py
                {
                    py str = a[0];
                    if (py{ str.size() } > (I<1>()))
                    {
                        if ((str.method("startswith", { S<"\"">() }).truthy() && str.method("endswith", { S<"\"">() }).truthy()))
                        {
                            return str[B<"slice">()(I<1>(), I<-1>(), none())].method("replace", { S<"\\\\">(), S<"\\">() }).method("replace", { S<"\\\"">(), S<"\"">() });
                        }
                        if ((str.method("startswith", { S<"<">() }).truthy() && str.method("endswith", { S<">">() }).truthy()))
                        {
                            return str[B<"slice">()(I<1>(), I<-1>(), none())];
                        }
                    }
                    return str;
                }));

            // RFC2231-related functions - parameter encoding and decoding
            define("email.utils", "decode_rfc2231", w.function("decode_rfc2231", "decode_rfc2231", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py parts;
                    parts = s.method("split", { G<"email.utils.TICK">(), I<2>() });
                    if (py{ parts.size() } <= (I<2>()))
                    {
                        return py_tuple({ none(), none(), s });
                    }
                    return parts;
                }));

            define("email.utils", "encode_rfc2231", w.function("encode_rfc2231", "encode_rfc2231", { py_positional("s"), py_positional("charset", none()), py_positional("language", none()) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py charset = a[1];
                    py language = a[2];
                    s = (Mod<"urllib.parse">().attr("quote")).call({ s }, { { "safe", S<"">() }, { "encoding", [&]() -> py { py const t = charset; return t.truthy() ? t : S<"ascii">(); }() } });
                    if ((charset.is_none() && language.is_none()))
                    {
                        return s;
                    }
                    if (language.is_none())
                    {
                        language = S<"">();
                    }
                    return (S<"%s'%s'%s">()) % py_tuple({ charset, language, s });
                }));

            define("email.utils", "rfc2231_continuation", (Mod<"re">()).method("compile", { S<"^(?P<name>\\w+)\\*((?P<num>[0-9]+)\\*?)?$">(), Mod<"re">().attr("ASCII") }));
            define("email.utils", "decode_params", w.function("decode_params", "decode_params", { py_positional("params") }, [](arguments a) -> py
                {
                    py params = a[0];
                    py charset;
                    py continuations;
                    py encoded;
                    py extended;
                    py has_zero;
                    py language;
                    py mo;
                    py name;
                    py new_params;
                    py num;
                    py rfc2231_params;
                    py s;
                    py value;
                    new_params = py_list({ params[I<0>()] });
                    // Map parameter's name to a list of continuations.  The values are a
                    // 3-tuple of the continuation number, the string value, and a flag
                    // specifying whether a particular segment is %-encoded.
                    rfc2231_params = py_dict();
                    for (py const& item : iterate{ params[B<"slice">()(I<1>(), none(), none())] })
                    {
                        auto const items18 = unpack(item, 2);
                        name = items18[0];
                        value = items18[1];
                        encoded = name.method("endswith", { S<"*">() });
                        value = (G<"email.utils.unquote">())(value);
                        mo = (G<"email.utils.rfc2231_continuation">()).method("match", { name });
                        if (mo.truthy())
                        {
                            auto const items19 = unpack(mo.method("group", { S<"name">(), S<"num">() }), 2);
                            name = items19[0];
                            num = items19[1];
                            if (!num.is_none())
                            {
                                num = (B<"int">())(num);
                            }
                            rfc2231_params.method("setdefault", { name, py_list() }).method("append", { py_tuple({ num, value, encoded }) });
                        }
                        else
                        {
                            new_params.method("append", { py_tuple({ name, (S<"\"%s\"">()) % (Mod<"email._parseaddr">().attr("quote"))(value) }) });
                        }
                    }
                    if (rfc2231_params.truthy())
                    {
                        for (py const& item : iterate{ rfc2231_params.method("items") })
                        {
                            auto const items21 = unpack(item, 2);
                            name = items21[0];
                            continuations = items21[1];
                            value = py_list();
                            extended = boolean(false);
                            // Sort by number, treating None as 0 if there is no 0,
                            // and ignore it if there is already a 0.
                            has_zero = (B<"any">())([&]() -> py { py const comprehension22 = py_list(); py x; for (py const& item : iterate{ continuations }) { x = item; comprehension22.method("append", { boolean(x[I<0>()] == (I<0>())) }); } return comprehension22; }());
                            if (has_zero.truthy())
                            {
                                continuations = [&]() -> py { py const comprehension23 = py_list(); py x; for (py const& item : iterate{ continuations }) { x = item; if (x[I<0>()].is_none()) continue; comprehension23.method("append", { x }); } return comprehension23; }();
                            }
                            else
                            {
                                continuations = [&]() -> py { py const comprehension24 = py_list(); py x; for (py const& item : iterate{ continuations }) { x = item; comprehension24.method("append", { py_tuple({ [&]() -> py { py const t = x[I<0>()]; return t.truthy() ? t : I<0>(); }(), x[I<1>()], x[I<2>()] }) }); } return comprehension24; }();
                            }
                            continuations.method("sort", {  }, { { "key", w.function("<lambda>", "decode_params.<locals>.<lambda>", { py_positional("x") }, [](arguments a) -> py
            {
                py x = a[0];
                return x[I<0>()];
            }) } });
                            // And now append all values in numerical order, converting
                            // %-encodings for the encoded segments.  If any of the
                            // continuation names ends in a *, then the entire string, after
                            // decoding segments and concatenating, must have the charset and
                            // language specifiers at the beginning of the string.
                            for (py const& item : iterate{ continuations })
                            {
                                auto const items26 = unpack(item, 3);
                                num = items26[0];
                                s = items26[1];
                                encoded = items26[2];
                                if (encoded.truthy())
                                {
                                    // Decode as "latin-1", so the characters in s directly
                                    // represent the percent-encoded octet values.
                                    // collapse_rfc2231_value treats this as an octet sequence.
                                    s = (Mod<"urllib.parse">().attr("unquote")).call({ s }, { { "encoding", S<"latin-1">() } });
                                    extended = boolean(true);
                                }
                                value.method("append", { s });
                            }
                            value = (Mod<"email._parseaddr">().attr("quote"))((G<"email.utils.EMPTYSTRING">()).method("join", { value }));
                            if (extended.truthy())
                            {
                                auto const items27 = unpack((G<"email.utils.decode_rfc2231">())(value), 3);
                                charset = items27[0];
                                language = items27[1];
                                value = items27[2];
                                new_params.method("append", { py_tuple({ name, py_tuple({ charset, language, (S<"\"%s\"">()) % value }) }) });
                            }
                            else
                            {
                                new_params.method("append", { py_tuple({ name, (S<"\"%s\"">()) % value }) });
                            }
                        }
                    }
                    return new_params;
                }));

            define("email.utils", "collapse_rfc2231_value", w.function("collapse_rfc2231_value", "collapse_rfc2231_value", { py_positional("value"), py_positional("errors", S<"replace">()), py_positional("fallback_charset", S<"us-ascii">()) }, [](arguments a) -> py
                {
                    py value = a[0];
                    py errors = a[1];
                    py fallback_charset = a[2];
                    py charset;
                    py language;
                    py rawbytes;
                    py text;
                    if (((!isinstance(value, B<"tuple">())) || (py{ value.size() } != (I<3>()))))
                    {
                        return (G<"email.utils.unquote">())(value);
                    }
                    // While value comes to us as a string, we need it to be a bytes
                    // object.  We do not want bytes() normal utf-8 decoder, we want a straight
                    // interpretation of the string as character bytes.
                    auto const items28 = unpack(value, 3);
                    charset = items28[0];
                    language = items28[1];
                    text = items28[2];
                    if (charset.is_none())
                    {
                        // Issue 17369: if charset/lang is None, decode_rfc2231 couldn't parse
                        // the value, so use the fallback_charset.
                        charset = fallback_charset;
                    }
                    rawbytes = (B<"bytes">())(text, S<"raw-unicode-escape">());
                    try
                    {
                        return (B<"str">())(rawbytes, charset, errors);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler29;
                        if (matches(handler29.exception, B<"LookupError">()))
                        {
                            // charset is not a known codec.
                            return (G<"email.utils.unquote">())(text);
                        }
                        else
                            py_reraise(handler29.exception);
                    }
                    return none();
                }));

            // 
            // datetime doesn't provide a localtime function yet, so provide one.  Code
            // adapted from the patch in issue 9527.  This may not be perfect, but it is
            // better than not having it.
            // 
            define("email.utils", "localtime", w.function("localtime", "localtime", { py_positional("dt", none()), py_positional("isdst", none()) }, [](arguments a) -> py
                {
                    py dt = a[0];
                    py isdst = a[1];
                    py warnings;
                    if (!isdst.is_none())
                    {
                        warnings = module_of("warnings");
                        warnings.method("_deprecated", { S<"The 'isdst' parameter to 'localtime'">() }, { { "message", S<"{name} is deprecated and slated for removal in Python {remove}">() }, { "remove", py_tuple({ I<3>(), I<14>() }) } });
                    }
                    if (dt.is_none())
                    {
                        dt = (Mod<"datetime">().attr("datetime")).method("now");
                    }
                    return dt.method("astimezone");
                }));
        }

        bool email_utils_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_utils();
                }
                if (auto const doc = email_utils_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("datetime", py{ fn_import("datetime") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("quote", import_attribute("email._parseaddr", "quote").release());
                aAttributes.emplace_back("_AddressList", import_attribute("email._parseaddr", "AddressList").release());
                aAttributes.emplace_back("mktime_tz", import_attribute("email._parseaddr", "mktime_tz").release());
                aAttributes.emplace_back("parsedate", import_attribute("email._parseaddr", "parsedate").release());
                aAttributes.emplace_back("parsedate_tz", import_attribute("email._parseaddr", "parsedate_tz").release());
                aAttributes.emplace_back("_parsedate_tz", import_attribute("email._parseaddr", "_parsedate_tz").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.utils", email_utils_module);
    }
}
