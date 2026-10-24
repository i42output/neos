/*
  email_encoders.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/encoders.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.encoders (CPython 3.13's Lib/email/encoders.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_encoders_docs.hpp"

namespace neos::python_runtime::detail::email_encoders_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.encoders.name"))
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
                py_raise("SystemError", std::string{ "email.encoders: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.encoders.name">()))
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
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("email.encoders"))
                return py{ fn_import("email.encoders") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.encoders" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.encoders", email_encoders_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.encoders (Lib/email/encoders.py)

        void make_email_encoders()
        {
            static module_writer const& w = writer("email.encoders");
            // Copyright (C) 2001-2006 Python Software Foundation
            // Author: Barry Warsaw
            // Contact: email-sig@python.org
            define("email.encoders", "__all__", py_list({ S<"encode_7or8bit">(), S<"encode_base64">(), S<"encode_noop">(), S<"encode_quopri">() }));
            define("email.encoders", "_qencode", w.function("_qencode", "_qencode", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py enc;
                    enc = (Mod<"quopri">().attr("encodestring")).call({ s }, { { "quotetabs", boolean(true) } });
                    // Must encode spaces, which quopri.encodestring() doesn't do
                    return enc.method("replace", { py{ make_bytes(std::string{ "\x20", 1 }) }, py{ make_bytes(std::string{ "\x3d\x32\x30", 3 }) } });
                }));

            define("email.encoders", "encode_base64", w.function("encode_base64", "encode_base64", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py encdata;
                    py orig;
                    orig = msg.method("get_payload", {  }, { { "decode", boolean(true) } });
                    encdata = (B<"str">())((Mod<"base64">().attr("encodebytes"))(orig), S<"ascii">());
                    msg.method("set_payload", { encdata });
                    msg.set_item(S<"Content-Transfer-Encoding">(), S<"base64">());
                    return none();
                }));

            define("email.encoders", "encode_quopri", w.function("encode_quopri", "encode_quopri", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py encdata;
                    py orig;
                    orig = msg.method("get_payload", {  }, { { "decode", boolean(true) } });
                    encdata = (G<"email.encoders._qencode">())(orig);
                    msg.method("set_payload", { encdata });
                    msg.set_item(S<"Content-Transfer-Encoding">(), S<"quoted-printable">());
                    return none();
                }));

            define("email.encoders", "encode_7or8bit", w.function("encode_7or8bit", "encode_7or8bit", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py orig;
                    orig = msg.method("get_payload", {  }, { { "decode", boolean(true) } });
                    if (orig.is_none())
                    {
                        // There's no payload.  For backwards compatibility we use 7bit
                        msg.set_item(S<"Content-Transfer-Encoding">(), S<"7bit">());
                        return none();
                    }
                    // We play a trick to make this go fast.  If decoding from ASCII succeeds,
                    // we know the data must be 7bit, otherwise treat it as 8bit.
                    {
                        bool otherwise1 = false;
                        try
                        {
                            orig.method("decode", { S<"ascii">() });
                            otherwise1 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"UnicodeError">()))
                            {
                                msg.set_item(S<"Content-Transfer-Encoding">(), S<"8bit">());
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        if (otherwise1)
                        {
                            msg.set_item(S<"Content-Transfer-Encoding">(), S<"7bit">());
                        }
                    }
                    return none();
                }));

            define("email.encoders", "encode_noop", w.function("encode_noop", "encode_noop", { py_positional("msg") }, [](arguments a) -> py
                {
                    py msg = a[0];
                    return none();
                }));
        }

        bool email_encoders_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_encoders();
                }
                if (auto const doc = email_encoders_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("_bencode", import_attribute("base64", "encodebytes").release());
                aAttributes.emplace_back("_encodestring", import_attribute("quopri", "encodestring").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.encoders", email_encoders_module);
    }
}
