/*
  colorsys.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/colorsys.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module colorsys (CPython 3.13's Lib/colorsys.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "colorsys_docs.hpp"

namespace neos::python_runtime::detail::colorsys_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "colorsys.name"))
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
                py_raise("SystemError", std::string{ "colorsys: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"colorsys.name">()))
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
            static module_writer const sWriter{ "colorsys", colorsys_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- colorsys (Lib/colorsys.py)

        void make_colorsys()
        {
            static module_writer const& w = writer("colorsys");
            // References:
            // http://en.wikipedia.org/wiki/YIQ
            // http://en.wikipedia.org/wiki/HLS_color_space
            // http://en.wikipedia.org/wiki/HSV_color_space
            define("colorsys", "__all__", py_list({ S<"rgb_to_yiq">(), S<"yiq_to_rgb">(), S<"rgb_to_hls">(), S<"hls_to_rgb">(), S<"rgb_to_hsv">(), S<"hsv_to_rgb">() }));
            // Some floating-point constants
            define("colorsys", "ONE_THIRD", (F<1.0>()) / (F<3.0>()));
            define("colorsys", "ONE_SIXTH", (F<1.0>()) / (F<6.0>()));
            define("colorsys", "TWO_THIRD", (F<2.0>()) / (F<3.0>()));
            // YIQ: used by composite video signals (linear combinations of RGB)
            // Y: perceived grey level (0.0 == black, 1.0 == white)
            // I, Q: color components
            // 
            // There are a great many versions of the constants used in these formulae.
            // The ones in this library uses constants from the FCC version of NTSC.
            define("colorsys", "rgb_to_yiq", w.function("rgb_to_yiq", "rgb_to_yiq", { py_positional("r"), py_positional("g"), py_positional("b") }, [](arguments a) -> py
                {
                    py r = a[0];
                    py g = a[1];
                    py b = a[2];
                    py i;
                    py q;
                    py y;
                    y = (((F<0.3>()) * r) + ((F<0.59>()) * g)) + ((F<0.11>()) * b);
                    i = ((F<0.74>()) * (r - y)) - ((F<0.27>()) * (b - y));
                    q = ((F<0.48>()) * (r - y)) + ((F<0.41>()) * (b - y));
                    return py_tuple({ y, i, q });
                }));

            define("colorsys", "yiq_to_rgb", w.function("yiq_to_rgb", "yiq_to_rgb", { py_positional("y"), py_positional("i"), py_positional("q") }, [](arguments a) -> py
                {
                    py y = a[0];
                    py i = a[1];
                    py q = a[2];
                    py b;
                    py g;
                    py r;
                    r = (y + ((F<0.9468822170900693>()) * i)) + ((F<0.6235565819861433>()) * q);
                    g = (y - ((F<0.27478764629897834>()) * i)) - ((F<0.6356910791873801>()) * q);
                    b = (y - ((F<1.1085450346420322>()) * i)) + ((F<1.7090069284064666>()) * q);
                    if (r < (F<0.0>()))
                    {
                        r = F<0.0>();
                    }
                    if (g < (F<0.0>()))
                    {
                        g = F<0.0>();
                    }
                    if (b < (F<0.0>()))
                    {
                        b = F<0.0>();
                    }
                    if (r > (F<1.0>()))
                    {
                        r = F<1.0>();
                    }
                    if (g > (F<1.0>()))
                    {
                        g = F<1.0>();
                    }
                    if (b > (F<1.0>()))
                    {
                        b = F<1.0>();
                    }
                    return py_tuple({ r, g, b });
                }));

            // HLS: Hue, Luminance, Saturation
            // H: position in the spectrum
            // L: color lightness
            // S: color saturation
            define("colorsys", "rgb_to_hls", w.function("rgb_to_hls", "rgb_to_hls", { py_positional("r"), py_positional("g"), py_positional("b") }, [](arguments a) -> py
                {
                    py r = a[0];
                    py g = a[1];
                    py b = a[2];
                    py bc;
                    py gc;
                    py h;
                    py l;
                    py maxc;
                    py minc;
                    py rangec;
                    py rc;
                    py s;
                    py sumc;
                    maxc = (B<"max">())(r, g, b);
                    minc = (B<"min">())(r, g, b);
                    sumc = maxc + minc;
                    rangec = maxc - minc;
                    l = sumc / (F<2.0>());
                    if (minc == maxc)
                    {
                        return py_tuple({ F<0.0>(), l, F<0.0>() });
                    }
                    if (l <= (F<0.5>()))
                    {
                        s = rangec / sumc;
                    }
                    else
                    {
                        s = rangec / (((F<2.0>()) - maxc) - minc);
                    }
                    rc = (maxc - r) / rangec;
                    gc = (maxc - g) / rangec;
                    bc = (maxc - b) / rangec;
                    if (r == maxc)
                    {
                        h = bc - gc;
                    }
                    else if (g == maxc)
                    {
                        h = ((F<2.0>()) + rc) - bc;
                    }
                    else
                    {
                        h = ((F<4.0>()) + gc) - rc;
                    }
                    h = (h / (F<6.0>())) % (F<1.0>());
                    return py_tuple({ h, l, s });
                }));

            define("colorsys", "hls_to_rgb", w.function("hls_to_rgb", "hls_to_rgb", { py_positional("h"), py_positional("l"), py_positional("s") }, [](arguments a) -> py
                {
                    py h = a[0];
                    py l = a[1];
                    py s = a[2];
                    py m1;
                    py m2;
                    if (s == (F<0.0>()))
                    {
                        return py_tuple({ l, l, l });
                    }
                    if (l <= (F<0.5>()))
                    {
                        m2 = l * ((F<1.0>()) + s);
                    }
                    else
                    {
                        m2 = (l + s) - (l * s);
                    }
                    m1 = ((F<2.0>()) * l) - m2;
                    return py_tuple({ (G<"colorsys._v">())(m1, m2, h + (G<"colorsys.ONE_THIRD">())), (G<"colorsys._v">())(m1, m2, h), (G<"colorsys._v">())(m1, m2, h - (G<"colorsys.ONE_THIRD">())) });
                }));

            define("colorsys", "_v", w.function("_v", "_v", { py_positional("m1"), py_positional("m2"), py_positional("hue") }, [](arguments a) -> py
                {
                    py m1 = a[0];
                    py m2 = a[1];
                    py hue = a[2];
                    hue = hue % (F<1.0>());
                    if (hue < (G<"colorsys.ONE_SIXTH">()))
                    {
                        return m1 + (((m2 - m1) * hue) * (F<6.0>()));
                    }
                    if (hue < (F<0.5>()))
                    {
                        return m2;
                    }
                    if (hue < (G<"colorsys.TWO_THIRD">()))
                    {
                        return m1 + (((m2 - m1) * ((G<"colorsys.TWO_THIRD">()) - hue)) * (F<6.0>()));
                    }
                    return m1;
                }));

            // HSV: Hue, Saturation, Value
            // H: position in the spectrum
            // S: color saturation ("purity")
            // V: color brightness
            define("colorsys", "rgb_to_hsv", w.function("rgb_to_hsv", "rgb_to_hsv", { py_positional("r"), py_positional("g"), py_positional("b") }, [](arguments a) -> py
                {
                    py r = a[0];
                    py g = a[1];
                    py b = a[2];
                    py bc;
                    py gc;
                    py h;
                    py maxc;
                    py minc;
                    py rangec;
                    py rc;
                    py s;
                    py v;
                    maxc = (B<"max">())(r, g, b);
                    minc = (B<"min">())(r, g, b);
                    rangec = maxc - minc;
                    v = maxc;
                    if (minc == maxc)
                    {
                        return py_tuple({ F<0.0>(), F<0.0>(), v });
                    }
                    s = rangec / maxc;
                    rc = (maxc - r) / rangec;
                    gc = (maxc - g) / rangec;
                    bc = (maxc - b) / rangec;
                    if (r == maxc)
                    {
                        h = bc - gc;
                    }
                    else if (g == maxc)
                    {
                        h = ((F<2.0>()) + rc) - bc;
                    }
                    else
                    {
                        h = ((F<4.0>()) + gc) - rc;
                    }
                    h = (h / (F<6.0>())) % (F<1.0>());
                    return py_tuple({ h, s, v });
                }));

            define("colorsys", "hsv_to_rgb", w.function("hsv_to_rgb", "hsv_to_rgb", { py_positional("h"), py_positional("s"), py_positional("v") }, [](arguments a) -> py
                {
                    py h = a[0];
                    py s = a[1];
                    py v = a[2];
                    py f;
                    py i;
                    py p;
                    py q;
                    py t;
                    if (s == (F<0.0>()))
                    {
                        return py_tuple({ v, v, v });
                    }
                    i = (B<"int">())(h * (F<6.0>()));
                    f = (h * (F<6.0>())) - i;
                    p = v * ((F<1.0>()) - s);
                    q = v * ((F<1.0>()) - (s * f));
                    t = v * ((F<1.0>()) - (s * ((F<1.0>()) - f)));
                    i = i % (I<6>());
                    if (i == (I<0>()))
                    {
                        return py_tuple({ v, t, p });
                    }
                    if (i == (I<1>()))
                    {
                        return py_tuple({ q, v, p });
                    }
                    if (i == (I<2>()))
                    {
                        return py_tuple({ p, v, t });
                    }
                    if (i == (I<3>()))
                    {
                        return py_tuple({ p, q, v });
                    }
                    if (i == (I<4>()))
                    {
                        return py_tuple({ t, p, v });
                    }
                    if (i == (I<5>()))
                    {
                        return py_tuple({ v, p, q });
                    }
                    return none();
                }));
        }

        bool colorsys_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_colorsys();
                }
                if (auto const doc = colorsys_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("colorsys", colorsys_module);
    }
}
