/*
  email_base64mime.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/base64mime.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.base64mime (CPython 3.13's Lib/email/base64mime.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_base64mime_docs.hpp"

namespace neos::python_runtime::detail::email_base64mime_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.base64mime.name"))
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
                py_raise("SystemError", std::string{ "email.base64mime: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.base64mime.name">()))
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
            if (modules_imported("email.base64mime"))
                return py{ fn_import("email.base64mime") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.base64mime" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.base64mime", email_base64mime_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.base64mime (Lib/email/base64mime.py)

        void make_email_base64mime()
        {
            static module_writer const& w = writer("email.base64mime");
            // Copyright (C) 2002-2007 Python Software Foundation
            // Author: Ben Gertzfield
            // Contact: email-sig@python.org
            define("email.base64mime", "__all__", py_list({ S<"body_decode">(), S<"body_encode">(), S<"decode">(), S<"decodestring">(), S<"header_encode">(), S<"header_length">() }));
            define("email.base64mime", "CRLF", S<"\r\n">());
            define("email.base64mime", "NL", S<"\n">());
            define("email.base64mime", "EMPTYSTRING", S<"">());
            // See also Charset.py
            define("email.base64mime", "MISC_LEN", I<7>());
            // Helpers
            define("email.base64mime", "header_length", w.function("header_length", "header_length", { py_positional("bytearray") }, [](arguments a) -> py
                {
                    py bytearray = a[0];
                    py groups_of_3;
                    py leftover;
                    py n;
                    auto const items1 = unpack((B<"divmod">())(py{ bytearray.size() }, I<3>()), 2);
                    groups_of_3 = items1[0];
                    leftover = items1[1];
                    // 4 bytes out for each 3 bytes (or nonzero fraction thereof) in.
                    n = groups_of_3 * (I<4>());
                    if (leftover.truthy())
                    {
                        n = inplace(NEOS_PY_ADD, n, I<4>());
                    }
                    return n;
                }));

            define("email.base64mime", "header_encode", w.function("header_encode", "header_encode", { py_positional("header_bytes"), py_positional("charset", S<"iso-8859-1">()) }, [](arguments a) -> py
                {
                    py header_bytes = a[0];
                    py charset = a[1];
                    py encoded;
                    if (!header_bytes.truthy())
                    {
                        return S<"">();
                    }
                    if (isinstance(header_bytes, B<"str">()))
                    {
                        header_bytes = header_bytes.method("encode", { charset });
                    }
                    encoded = (Mod<"base64">().attr("b64encode"))(header_bytes).method("decode", { S<"ascii">() });
                    return (S<"=?%s?b?%s?=">()) % py_tuple({ charset, encoded });
                }));

            define("email.base64mime", "body_encode", w.function("body_encode", "body_encode", { py_positional("s"), py_positional("maxlinelen", I<76>()), py_positional("eol", G<"email.base64mime.NL">()) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py maxlinelen = a[1];
                    py eol = a[2];
                    py enc;
                    py encvec;
                    py i;
                    py max_unencoded;
                    if (!s.truthy())
                    {
                        return S<"">();
                    }
                    encvec = py_list();
                    max_unencoded = floordiv((maxlinelen * (I<3>())), (I<4>()));
                    for (py const& item : iterate{ (B<"range">())(I<0>(), py{ s.size() }, max_unencoded) })
                    {
                        i = item;
                        // BAW: should encode() inherit b2a_base64()'s dubious behavior in
                        // adding a newline to the encoded string?
                        enc = (Mod<"binascii">().attr("b2a_base64"))(s[B<"slice">()(i, i + max_unencoded, none())]).method("decode", { S<"ascii">() });
                        if ((enc.method("endswith", { G<"email.base64mime.NL">() }).truthy() && (eol != (G<"email.base64mime.NL">()))))
                        {
                            enc = enc[B<"slice">()(none(), I<-1>(), none())] + eol;
                        }
                        encvec.method("append", { enc });
                    }
                    return (G<"email.base64mime.EMPTYSTRING">()).method("join", { encvec });
                }));

            define("email.base64mime", "decode", w.function("decode", "decode", { py_positional("string") }, [](arguments a) -> py
                {
                    py string = a[0];
                    if (!string.truthy())
                    {
                        return (B<"bytes">())();
                    }
                    else if (isinstance(string, B<"str">()))
                    {
                        return (Mod<"binascii">().attr("a2b_base64"))(string.method("encode", { S<"raw-unicode-escape">() }));
                    }
                    else
                    {
                        return (Mod<"binascii">().attr("a2b_base64"))(string);
                    }
                    return none();
                }));

            // For convenience and backwards compatibility w/ standard base64 module
            define("email.base64mime", "body_decode", G<"email.base64mime.decode">());
            define("email.base64mime", "decodestring", G<"email.base64mime.decode">());
        }

        bool email_base64mime_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_base64mime();
                }
                if (auto const doc = email_base64mime_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("b64encode", import_attribute("base64", "b64encode").release());
                aAttributes.emplace_back("b2a_base64", import_attribute("binascii", "b2a_base64").release());
                aAttributes.emplace_back("a2b_base64", import_attribute("binascii", "a2b_base64").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.base64mime", email_base64mime_module);
    }
}
