/*
  html.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/html/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module html (CPython 3.13's Lib/html/__init__.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "html_docs.hpp"

namespace neos::python_runtime::detail::html_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "html.name"))
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
                py_raise("SystemError", std::string{ "html: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"html.name">()))
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
            static module_writer const sWriter{ "html", html_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- html (Lib/html/__init__.py)

        void make_html()
        {
            static module_writer const& w = writer("html");
            define("html", "__all__", py_list({ S<"escape">(), S<"unescape">() }));
            define("html", "escape", w.function("escape", "escape", { py_positional("s"), py_positional("quote", boolean(true)) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py quote = a[1];
                    s = s.method("replace", { S<"&">(), S<"&amp;">() });
                    s = s.method("replace", { S<"<">(), S<"&lt;">() });
                    s = s.method("replace", { S<">">(), S<"&gt;">() });
                    if (quote.truthy())
                    {
                        s = s.method("replace", { S<"\"">(), S<"&quot;">() });
                        s = s.method("replace", { S<"'">(), S<"&#x27;">() });
                    }
                    return s;
                }));

            // see https://html.spec.whatwg.org/multipage/parsing.html#numeric-character-reference-end-state
            define("html", "_invalid_charrefs", dict_of({ { I<0>(), S<"�">() }, { I<13>(), S<"\r">() }, { I<128>(), S<"€">() }, { I<129>(), S<"">() }, { I<130>(), S<"‚">() }, { I<131>(), S<"ƒ">() }, { I<132>(), S<"„">() }, { I<133>(), S<"…">() }, { I<134>(), S<"†">() }, { I<135>(), S<"‡">() }, { I<136>(), S<"ˆ">() }, { I<137>(), S<"‰">() }, { I<138>(), S<"Š">() }, { I<139>(), S<"‹">() }, { I<140>(), S<"Œ">() }, { I<141>(), S<"">() }, { I<142>(), S<"Ž">() }, { I<143>(), S<"">() }, { I<144>(), S<"">() }, { I<145>(), S<"‘">() }, { I<146>(), S<"’">() }, { I<147>(), S<"“">() }, { I<148>(), S<"”">() }, { I<149>(), S<"•">() }, { I<150>(), S<"–">() }, { I<151>(), S<"—">() }, { I<152>(), S<"˜">() }, { I<153>(), S<"™">() }, { I<154>(), S<"š">() }, { I<155>(), S<"›">() }, { I<156>(), S<"œ">() }, { I<157>(), S<"">() }, { I<158>(), S<"ž">() }, { I<159>(), S<"Ÿ">() } }));
            define("html", "_invalid_codepoints", set_of({ I<1>(), I<2>(), I<3>(), I<4>(), I<5>(), I<6>(), I<7>(), I<8>(), I<14>(), I<15>(), I<16>(), I<17>(), I<18>(), I<19>(), I<20>(), I<21>(), I<22>(), I<23>(), I<24>(), I<25>(), I<26>(), I<27>(), I<28>(), I<29>(), I<30>(), I<31>(), I<127>(), I<128>(), I<129>(), I<130>(), I<131>(), I<132>(), I<133>(), I<134>(), I<135>(), I<136>(), I<137>(), I<138>(), I<139>(), I<140>(), I<141>(), I<142>(), I<143>(), I<144>(), I<145>(), I<146>(), I<147>(), I<148>(), I<149>(), I<150>(), I<151>(), I<152>(), I<153>(), I<154>(), I<155>(), I<156>(), I<157>(), I<158>(), I<159>(), I<64976>(), I<64977>(), I<64978>(), I<64979>(), I<64980>(), I<64981>(), I<64982>(), I<64983>(), I<64984>(), I<64985>(), I<64986>(), I<64987>(), I<64988>(), I<64989>(), I<64990>(), I<64991>(), I<64992>(), I<64993>(), I<64994>(), I<64995>(), I<64996>(), I<64997>(), I<64998>(), I<64999>(), I<65000>(), I<65001>(), I<65002>(), I<65003>(), I<65004>(), I<65005>(), I<65006>(), I<65007>(), I<11>(), I<65534>(), I<65535>(), I<131070>(), I<131071>(), I<196606>(), I<196607>(), I<262142>(), I<262143>(), I<327678>(), I<327679>(), I<393214>(), I<393215>(), I<458750>(), I<458751>(), I<524286>(), I<524287>(), I<589822>(), I<589823>(), I<655358>(), I<655359>(), I<720894>(), I<720895>(), I<786430>(), I<786431>(), I<851966>(), I<851967>(), I<917502>(), I<917503>(), I<983038>(), I<983039>(), I<1048574>(), I<1048575>(), I<1114110>(), I<1114111>() }));
            // 0x0001 to 0x0008
            // 0x000E to 0x001F
            // 0x007F to 0x009F
            // 0xFDD0 to 0xFDEF
            // others
            define("html", "_replace_charref", w.function("_replace_charref", "_replace_charref", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py num;
                    py x;
                    s = s.method("group", { I<1>() });
                    if (s[I<0>()] == (S<"#">()))
                    {
                        // numeric charref
                        if ((S<"xX">()).contains(s[I<1>()]))
                        {
                            num = (B<"int">())(s[B<"slice">()(I<2>(), none(), none())].method("rstrip", { S<";">() }), I<16>());
                        }
                        else
                        {
                            num = (B<"int">())(s[B<"slice">()(I<1>(), none(), none())].method("rstrip", { S<";">() }));
                        }
                        if ((G<"html._invalid_charrefs">()).contains(num))
                        {
                            return (G<"html._invalid_charrefs">())[num];
                        }
                        if (((((I<55296>()) <= num) && (num <= (I<57343>()))) || (num > (I<1114111>()))))
                        {
                            return S<"�">();
                        }
                        if ((G<"html._invalid_codepoints">()).contains(num))
                        {
                            return S<"">();
                        }
                        return (B<"chr">())(num);
                    }
                    else
                    {
                        // named charref
                        if ((Mod<"html.entities">().attr("html5")).contains(s))
                        {
                            return (Mod<"html.entities">().attr("html5"))[s];
                        }
                        // find the longest matching name (as defined by the standard)
                        bool broke1 = false;
                        for (py const& item : iterate{ (B<"range">())(py{ s.size() } - (I<1>()), I<1>(), I<-1>()) })
                        {
                            x = item;
                            if ((Mod<"html.entities">().attr("html5")).contains(s[B<"slice">()(none(), x, none())]))
                            {
                                return (Mod<"html.entities">().attr("html5"))[s[B<"slice">()(none(), x, none())]] + s[B<"slice">()(x, none(), none())];
                            }
                        }
                        if (!broke1)
                        {
                            return (S<"&">()) + s;
                        }
                    }
                    return none();
                }));

            define("html", "_charref", (Mod<"re">()).method("compile", { S<"&(#[0-9]+;?|#[xX][0-9a-fA-F]+;?|[^\\t\\n\\f <&#;]{1,32};?)">() }));
            define("html", "unescape", w.function("unescape", "unescape", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    if (!s.contains((S<"&">())))
                    {
                        return s;
                    }
                    return (G<"html._charref">()).method("sub", { G<"html._replace_charref">(), s });
                }));
        }

        bool html_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_html();
                }
                if (auto const doc = html_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("_re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("_html5", import_attribute("html.entities", "html5").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("html", html_module);
    }
}
