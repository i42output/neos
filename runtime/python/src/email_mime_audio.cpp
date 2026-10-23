/*
  email_mime_audio.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/mime/audio.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.mime.audio (CPython 3.13's Lib/email/mime/audio.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_mime_audio_docs.hpp"

namespace neos::python_runtime::detail::email_mime_audio_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.mime.audio.name"))
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
                py_raise("SystemError", std::string{ "email.mime.audio: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.mime.audio.name">()))
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
            if (modules_imported("email.mime.audio"))
                return py{ fn_import("email.mime.audio") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.mime.audio" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.mime.audio", email_mime_audio_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.mime.audio (Lib/email/mime/audio.py)

        void make_email_mime_audio()
        {
            static module_writer const& w = writer("email.mime.audio");
            // Copyright (C) 2001-2007 Python Software Foundation
            // Author: Anthony Baxter
            // Contact: email-sig@python.org
            define("email.mime.audio", "__all__", py_list({ S<"MIMEAudio">() }));
            define("email.mime.audio", "MIMEAudio", w.make_class("MIMEAudio", { Mod<"email.mime.nonmultipart">().attr("MIMENonMultipart") }, {
                { "__init__", w.method("MIMEAudio", "__init__", { py_positional("_audiodata"), py_positional("_subtype", none()), py_positional("_encoder", Mod<"email.encoders">().attr("encode_base64")), py_keyword("policy", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _audiodata = a[1];
                        py _subtype = a[2];
                        py _encoder = a[3];
                        py policy = a[4];
                        py _params = a[5];
                        if (_subtype.is_none())
                        {
                            _subtype = (G<"email.mime.audio._what">())(_audiodata);
                        }
                        if (_subtype.is_none())
                        {
                            py_raise((B<"TypeError">())(S<"Could not find audio MIME subtype">()));
                        }
                        call_ex((Mod<"email.mime.nonmultipart">().attr("MIMENonMultipart")).attr("__init__"), { self, S<"audio">(), _subtype }, py{}, _params, { { "policy", policy } });
                        self.method("set_payload", { _audiodata });
                        _encoder(self);
                        return none();
                    }, false, true) }, }));

            define("email.mime.audio", "_rules", py_list());
            // Originally from the sndhdr module.
            // 
            // There are others in sndhdr that don't have MIME types. :(
            // Additional ones to be added to sndhdr? midi, mp3, realaudio, wma??
            define("email.mime.audio", "_what", w.function("_what", "_what", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    py res;
                    py testfn;
                    bool broke1 = false;
                    for (py const& item : iterate{ G<"email.mime.audio._rules">() })
                    {
                        testfn = item;
                        if (([&]() -> py { res = testfn(data); return res; }()).truthy())
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

            define("email.mime.audio", "rule", w.function("rule", "rule", { py_positional("rulefunc") }, [](arguments a) -> py
                {
                    py rulefunc = a[0];
                    (G<"email.mime.audio._rules">()).method("append", { rulefunc });
                    return rulefunc;
                }));

            define("email.mime.audio", "_aiff", (G<"email.mime.audio.rule">())(w.function("_aiff", "_aiff", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (!h.method("startswith", { py{ make_bytes(std::string{ "\x46\x4f\x52\x4d", 4 }) } }).truthy())
                    {
                        return none();
                    }
                    if (set_of({ py{ make_bytes(std::string{ "\x41\x49\x46\x43", 4 }) }, py{ make_bytes(std::string{ "\x41\x49\x46\x46", 4 }) } }).contains(h[B<"slice">()(I<8>(), I<12>(), none())]))
                    {
                        return S<"x-aiff">();
                    }
                    else
                    {
                        return none();
                    }
                    return none();
                })));

            define("email.mime.audio", "_au", (G<"email.mime.audio.rule">())(w.function("_au", "_au", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (h.method("startswith", { py{ make_bytes(std::string{ "\x2e\x73\x6e\x64", 4 }) } }).truthy())
                    {
                        return S<"basic">();
                    }
                    else
                    {
                        return none();
                    }
                    return none();
                })));

            define("email.mime.audio", "_wav", (G<"email.mime.audio.rule">())(w.function("_wav", "_wav", { py_positional("h") }, [](arguments a) -> py
                {
                    py h = a[0];
                    if (((!h.method("startswith", { py{ make_bytes(std::string{ "\x52\x49\x46\x46", 4 }) } }).truthy()) || (h[B<"slice">()(I<8>(), I<12>(), none())] != py{ make_bytes(std::string{ "\x57\x41\x56\x45", 4 }) }) || (h[B<"slice">()(I<12>(), I<16>(), none())] != py{ make_bytes(std::string{ "\x66\x6d\x74\x20", 4 }) })))
                    {
                        return none();
                    }
                    else
                    {
                        return S<"x-wav">();
                    }
                    return none();
                })));
        }

        bool email_mime_audio_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_mime_audio();
                }
                if (auto const doc = email_mime_audio_docs::doc(""))
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

        inline bool const sModule = register_module("email.mime.audio", email_mime_audio_module);
    }
}
