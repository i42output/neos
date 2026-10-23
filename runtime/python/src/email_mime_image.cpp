/*
  email_mime_image.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/mime/image.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.mime.image (CPython 3.13's Lib/email/mime/image.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_mime_image_docs.hpp"

namespace neos::python_runtime::detail::email_mime_image_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.mime.image.name"))
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
                py_raise("SystemError", std::string{ "email.mime.image: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.mime.image.name">()))
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
            if (modules_imported("email.mime.image"))
                return py{ fn_import("email.mime.image") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.mime.image" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.mime.image", email_mime_image_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.mime.image (Lib/email/mime/image.py)

        void make_email_mime_image()
        {
            static module_writer const& w = writer("email.mime.image");
            // Copyright (C) 2001-2006 Python Software Foundation
            // Author: Barry Warsaw
            // Contact: email-sig@python.org
            define("email.mime.image", "__all__", py_list({ S<"MIMEImage">() }));
            define("email.mime.image", "MIMEImage", w.make_class("MIMEImage", { Mod<"email.mime.nonmultipart">().attr("MIMENonMultipart") }, {
                { "__init__", w.method("MIMEImage", "__init__", { py_positional("_imagedata"), py_positional("_subtype", none()), py_positional("_encoder", Mod<"email.encoders">().attr("encode_base64")), py_keyword("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _imagedata = a[1];
                        py _subtype = a[2];
                        py _encoder = a[3];
                        py policy = a[4];
                        py _params = a[5];
                        _subtype = (_subtype.is_none() ? (G<"email.mime.image._what">())(_imagedata) : _subtype);
                        if (_subtype.is_none())
                        {
                            py_raise((B<"TypeError">())(S<"Could not guess image MIME subtype">()));
                        }
                        call_ex((Mod<"email.mime.nonmultipart">().attr("MIMENonMultipart")).attr("__init__"), { self, S<"image">(), _subtype }, py{}, _params, { { "policy", policy } });
                        self.method("set_payload", { _imagedata });
                        _encoder(self);
                        return none();
                    }, false, true) }, }));

            define("email.mime.image", "_rules", py_list());
            // Originally from the imghdr module.
            define("email.mime.image", "_what", w.function("_what", "_what", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py res;
                    py rule;
                    bool broke1 = false;
                    for (py const& item : iterate{ G<"email.mime.image._rules">() })
                    {
                        rule = item;
                        if (([&]() -> py { res = rule(data); return res; }()).truthy())
                        {
                            return res;
                        }
                    }
                    if (!broke1)
                    {
                        return none();
                    }
                    return none();
                }));

            define("email.mime.image", "rule", w.function("rule", "rule", { py_positional("rulefunc") }, [](arguments a) -> py
                {
                    py rulefunc = a[0];
                    (G<"email.mime.image._rules">()).method("append", { rulefunc });
                    return rulefunc;
                }));

            define("email.mime.image", "_jpeg", (G<"email.mime.image.rule">())(w.function("_jpeg", "_jpeg", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (py_tuple({ py{ make_bytes(std::string{ "\x4a\x46\x49\x46", 4 }) }, py{ make_bytes(std::string{ "\x45\x78\x69\x66", 4 }) } }).contains(h[B<"slice">()(I<6>(), I<10>(), none())]))
                    {
                        return S<"jpeg">();
                    }
                    else if (h[B<"slice">()(none(), I<4>(), none())] == py{ make_bytes(std::string{ "\xff\xd8\xff\xdb", 4 }) })
                    {
                        return S<"jpeg">();
                    }
                    return none();
                })));

            define("email.mime.image", "_png", (G<"email.mime.image.rule">())(w.function("_png", "_png", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (h.method("startswith", { py{ make_bytes(std::string{ "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a", 8 }) } }).truthy())
                    {
                        return S<"png">();
                    }
                    return none();
                })));

            define("email.mime.image", "_gif", (G<"email.mime.image.rule">())(w.function("_gif", "_gif", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (py_tuple({ py{ make_bytes(std::string{ "\x47\x49\x46\x38\x37\x61", 6 }) }, py{ make_bytes(std::string{ "\x47\x49\x46\x38\x39\x61", 6 }) } }).contains(h[B<"slice">()(none(), I<6>(), none())]))
                    {
                        return S<"gif">();
                    }
                    return none();
                })));

            define("email.mime.image", "_tiff", (G<"email.mime.image.rule">())(w.function("_tiff", "_tiff", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (py_tuple({ py{ make_bytes(std::string{ "\x4d\x4d", 2 }) }, py{ make_bytes(std::string{ "\x49\x49", 2 }) } }).contains(h[B<"slice">()(none(), I<2>(), none())]))
                    {
                        return S<"tiff">();
                    }
                    return none();
                })));

            define("email.mime.image", "_rgb", (G<"email.mime.image.rule">())(w.function("_rgb", "_rgb", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (h.method("startswith", { py{ make_bytes(std::string{ "\x01\xda", 2 }) } }).truthy())
                    {
                        return S<"rgb">();
                    }
                    return none();
                })));

            define("email.mime.image", "_pbm", (G<"email.mime.image.rule">())(w.function("_pbm", "_pbm", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (((py{ h.size() } >= (I<3>())) && (h[I<0>()] == (B<"ord">())(py{ make_bytes(std::string{ "\x50", 1 }) })) && py{ make_bytes(std::string{ "\x31\x34", 2 }) }.contains(h[I<1>()]) && py{ make_bytes(std::string{ "\x20\x09\x0a\x0d", 4 }) }.contains(h[I<2>()])))
                    {
                        return S<"pbm">();
                    }
                    return none();
                })));

            define("email.mime.image", "_pgm", (G<"email.mime.image.rule">())(w.function("_pgm", "_pgm", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (((py{ h.size() } >= (I<3>())) && (h[I<0>()] == (B<"ord">())(py{ make_bytes(std::string{ "\x50", 1 }) })) && py{ make_bytes(std::string{ "\x32\x35", 2 }) }.contains(h[I<1>()]) && py{ make_bytes(std::string{ "\x20\x09\x0a\x0d", 4 }) }.contains(h[I<2>()])))
                    {
                        return S<"pgm">();
                    }
                    return none();
                })));

            define("email.mime.image", "_ppm", (G<"email.mime.image.rule">())(w.function("_ppm", "_ppm", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (((py{ h.size() } >= (I<3>())) && (h[I<0>()] == (B<"ord">())(py{ make_bytes(std::string{ "\x50", 1 }) })) && py{ make_bytes(std::string{ "\x33\x36", 2 }) }.contains(h[I<1>()]) && py{ make_bytes(std::string{ "\x20\x09\x0a\x0d", 4 }) }.contains(h[I<2>()])))
                    {
                        return S<"ppm">();
                    }
                    return none();
                })));

            define("email.mime.image", "_rast", (G<"email.mime.image.rule">())(w.function("_rast", "_rast", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (h.method("startswith", { py{ make_bytes(std::string{ "\x59\xa6\x6a\x95", 4 }) } }).truthy())
                    {
                        return S<"rast">();
                    }
                    return none();
                })));

            define("email.mime.image", "_xbm", (G<"email.mime.image.rule">())(w.function("_xbm", "_xbm", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (h.method("startswith", { py{ make_bytes(std::string{ "\x23\x64\x65\x66\x69\x6e\x65\x20", 8 }) } }).truthy())
                    {
                        return S<"xbm">();
                    }
                    return none();
                })));

            define("email.mime.image", "_bmp", (G<"email.mime.image.rule">())(w.function("_bmp", "_bmp", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (h.method("startswith", { py{ make_bytes(std::string{ "\x42\x4d", 2 }) } }).truthy())
                    {
                        return S<"bmp">();
                    }
                    return none();
                })));

            define("email.mime.image", "_webp", (G<"email.mime.image.rule">())(w.function("_webp", "_webp", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if ((h.method("startswith", { py{ make_bytes(std::string{ "\x52\x49\x46\x46", 4 }) } }).truthy() && (h[B<"slice">()(I<8>(), I<12>(), none())] == py{ make_bytes(std::string{ "\x57\x45\x42\x50", 4 }) })))
                    {
                        return S<"webp">();
                    }
                    return none();
                })));

            define("email.mime.image", "_exr", (G<"email.mime.image.rule">())(w.function("_exr", "_exr", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (h.method("startswith", { py{ make_bytes(std::string{ "\x76\x2f\x31\x01", 4 }) } }).truthy())
                    {
                        return S<"exr">();
                    }
                    return none();
                })));
        }

        bool email_mime_image_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_mime_image();
                }
                if (auto const doc = email_mime_image_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("encoders", import_attribute("email", "encoders").release());
                aAttributes.emplace_back("MIMENonMultipart", import_attribute("email.mime.nonmultipart", "MIMENonMultipart").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.mime.image", email_mime_image_module);
    }
}
