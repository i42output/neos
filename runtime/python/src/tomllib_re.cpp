/*
  tomllib_re.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/tomllib/_re.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/tomllib/_re.py's copyright: SPDX-License-Identifier: MIT.
  SPDX-FileCopyrightText: 2021 Taneli Hukkinen. Licensed to PSF under a Contributor Agreement.
*/

// The module tomllib._re (CPython 3.13's Lib/tomllib/_re.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "tomllib_re_docs.hpp"

namespace neos::python_runtime::detail::tomllib__re_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "tomllib._re.name"))
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
                py_raise("SystemError", std::string{ "tomllib._re: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"tomllib._re.name">()))
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
            static module_writer const sWriter{ "tomllib._re", tomllib_re_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- tomllib._re (Lib/tomllib/_re.py)

        void make_tomllib__re()
        {
            static module_writer const& w = writer("tomllib._re");
            // SPDX-License-Identifier: MIT
            // SPDX-FileCopyrightText: 2021 Taneli Hukkinen
            // Licensed to PSF under a Contributor Agreement.
            // E.g.
            // - 00:32:00.999999
            // - 00:32:00
            define("tomllib._re", "_TIME_RE_STR", S<"([01][0-9]|2[0-3]):([0-5][0-9]):([0-5][0-9])(?:\\.([0-9]{1,6})[0-9]*)?">());
            define("tomllib._re", "RE_NUMBER", (Mod<"re">()).method("compile", { S<"\n0\n(?:\n    x[0-9A-Fa-f](?:_?[0-9A-Fa-f])*   # hex\n    |\n    b[01](?:_?[01])*                 # bin\n    |\n    o[0-7](?:_?[0-7])*               # oct\n)\n|\n[+-]?(?:0|[1-9](?:_?[0-9])*)         # dec, integer part\n(?P<floatpart>\n    (?:\\.[0-9](?:_?[0-9])*)?         # optional fractional part\n    (?:[eE][+-]?[0-9](?:_?[0-9])*)?  # optional exponent part\n)\n">() }, { { "flags", Mod<"re">().attr("VERBOSE") } }));
            define("tomllib._re", "RE_LOCALTIME", (Mod<"re">()).method("compile", { G<"tomllib._re._TIME_RE_STR">() }));
            define("tomllib._re", "RE_DATETIME", (Mod<"re">()).method("compile", { S(std::string{ "\n([0-9]{4})-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])  # date, e.g. 1988-10-27\n(?:\n    [Tt ]\n    " } + (G<"tomllib._re._TIME_RE_STR">()).str() + "\n    (?:([Zz])|([+-])([01][0-9]|2[0-3]):([0-5][0-9]))?  # optional time offset\n)?\n") }, { { "flags", Mod<"re">().attr("VERBOSE") } }));
            define("tomllib._re", "match_to_datetime", w.function("match_to_datetime", "match_to_datetime", { py_positional("match") }, [](arguments a) -> py
                {
                    py match = a[0];
                    py day;
                    py day_str;
                    py hour;
                    py hour_str;
                    py micros;
                    py micros_str;
                    py minute;
                    py minute_str;
                    py month;
                    py month_str;
                    py offset_hour_str;
                    py offset_minute_str;
                    py offset_sign_str;
                    py sec;
                    py sec_str;
                    py tz;
                    py year;
                    py year_str;
                    py zulu_time;
                    auto const items1 = unpack(match.method("groups"), 11);
                    year_str = items1[0];
                    month_str = items1[1];
                    day_str = items1[2];
                    hour_str = items1[3];
                    minute_str = items1[4];
                    sec_str = items1[5];
                    micros_str = items1[6];
                    zulu_time = items1[7];
                    offset_sign_str = items1[8];
                    offset_hour_str = items1[9];
                    offset_minute_str = items1[10];
                    auto const items2 = unpack(py_tuple({ (B<"int">())(year_str), (B<"int">())(month_str), (B<"int">())(day_str) }), 3);
                    year = items2[0];
                    month = items2[1];
                    day = items2[2];
                    if (hour_str.is_none())
                    {
                        return (Mod<"datetime">().attr("date"))(year, month, day);
                    }
                    auto const items3 = unpack(py_tuple({ (B<"int">())(hour_str), (B<"int">())(minute_str), (B<"int">())(sec_str) }), 3);
                    hour = items3[0];
                    minute = items3[1];
                    sec = items3[2];
                    micros = (micros_str.truthy() ? (B<"int">())(micros_str.method("ljust", { I<6>(), S<"0">() })) : I<0>());
                    if (offset_sign_str.truthy())
                    {
                        tz = (G<"tomllib._re.cached_tz">())(offset_hour_str, offset_minute_str, offset_sign_str);
                    }
                    else if (zulu_time.truthy())
                    {
                        tz = (Mod<"datetime">().attr("timezone")).attr("utc");
                    }
                    else
                    {
                        tz = none();
                    }
                    return (Mod<"datetime">().attr("datetime")).call({ year, month, day, hour, minute, sec, micros }, { { "tzinfo", tz } });
                }));

            define("tomllib._re", "cached_tz", (Mod<"functools">().attr("lru_cache")).call({  }, { { "maxsize", none() } })(w.function("cached_tz", "cached_tz", { py_positional("hour_str"), py_positional("minute_str"), py_positional("sign_str") }, [](arguments a) -> py
                {
                    py hour_str = a[0];
                    py minute_str = a[1];
                    py sign_str = a[2];
                    py sign;
                    sign = (sign_str == (S<"+">()) ? I<1>() : I<-1>());
                    return (Mod<"datetime">().attr("timezone"))((Mod<"datetime">().attr("timedelta")).call({  }, { { "hours", sign * (B<"int">())(hour_str) }, { "minutes", sign * (B<"int">())(minute_str) } }));
                })));

            define("tomllib._re", "match_to_localtime", w.function("match_to_localtime", "match_to_localtime", { py_positional("match") }, [](arguments a) -> py
                {
                    py match = a[0];
                    py hour_str;
                    py micros;
                    py micros_str;
                    py minute_str;
                    py sec_str;
                    auto const items4 = unpack(match.method("groups"), 4);
                    hour_str = items4[0];
                    minute_str = items4[1];
                    sec_str = items4[2];
                    micros_str = items4[3];
                    micros = (micros_str.truthy() ? (B<"int">())(micros_str.method("ljust", { I<6>(), S<"0">() })) : I<0>());
                    return (Mod<"datetime">().attr("time"))((B<"int">())(hour_str), (B<"int">())(minute_str), (B<"int">())(sec_str), micros);
                }));

            define("tomllib._re", "match_to_number", w.function("match_to_number", "match_to_number", { py_positional("match"), py_positional("parse_float") }, [](arguments a) -> py
                {
                    py match = a[0];
                    py parse_float = a[1];
                    if (match.method("group", { S<"floatpart">() }).truthy())
                    {
                        return parse_float(match.method("group"));
                    }
                    return (B<"int">())(match.method("group"), I<0>());
                }));
        }

        bool tomllib__re_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_tomllib__re();
                }
                if (auto const doc = tomllib_re_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("date", import_attribute("datetime", "date").release());
                aAttributes.emplace_back("datetime", import_attribute("datetime", "datetime").release());
                aAttributes.emplace_back("time", import_attribute("datetime", "time").release());
                aAttributes.emplace_back("timedelta", import_attribute("datetime", "timedelta").release());
                aAttributes.emplace_back("timezone", import_attribute("datetime", "timezone").release());
                aAttributes.emplace_back("tzinfo", import_attribute("datetime", "tzinfo").release());
                aAttributes.emplace_back("lru_cache", import_attribute("functools", "lru_cache").release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("Any", import_attribute("typing", "Any").release());
                aAttributes.emplace_back("ParseFloat", import_attribute("tomllib._types", "ParseFloat").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("tomllib._re", tomllib__re_module);
    }
}
