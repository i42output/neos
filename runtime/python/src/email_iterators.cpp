/*
  email_iterators.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/iterators.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.iterators (CPython 3.13's Lib/email/iterators.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_iterators_docs.hpp"

namespace neos::python_runtime::detail::email_iterators_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.iterators.name"))
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
                py_raise("SystemError", std::string{ "email.iterators: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.iterators.name">()))
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
            if (modules_imported("email.iterators"))
                return py{ fn_import("email.iterators") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.iterators" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.iterators", email_iterators_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.iterators (Lib/email/iterators.py)

        void make_email_iterators()
        {
            static module_writer const& w = writer("email.iterators");
            // Copyright (C) 2001-2006 Python Software Foundation
            // Author: Barry Warsaw
            // Contact: email-sig@python.org
            define("email.iterators", "__all__", py_list({ S<"body_line_iterator">(), S<"typed_subpart_iterator">(), S<"walk">() }));
            // Do not include _structure() since it's part of the debugging API.
            // This function will become a method of the Message class
            define("email.iterators", "walk", w.function("walk", "walk", { py_positional("self") }, [](arguments a) -> py
                {
                    return make_native_generator("walk", "walk", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py self = a[0];
                        py subpart;
                        py_yield(self);
                        if (self.method("is_multipart").truthy())
                        {
                            for (py const& item : iterate{ self.method("get_payload") })
                            {
                                subpart = item;
                                yield_from(subpart.method("walk"));
                            }
                        }
                        return none();
                    });
                }));

            // These two functions are imported into the Iterators.py interface module.
            define("email.iterators", "body_line_iterator", w.function("body_line_iterator", "body_line_iterator", { py_positional("msg"), py_positional("decode", boolean(false)) }, [](arguments a) -> py
                {
                    return make_native_generator("body_line_iterator", "body_line_iterator", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py msg = a[0];
                        py decode = a[1];
                        py payload;
                        py subpart;
                        for (py const& item : iterate{ msg.method("walk") })
                        {
                            subpart = item;
                            payload = subpart.method("get_payload", {  }, { { "decode", decode } });
                            if (isinstance(payload, B<"str">()))
                            {
                                yield_from((Mod<"io">().attr("StringIO"))(payload));
                            }
                        }
                        return none();
                    });
                }));

            define("email.iterators", "typed_subpart_iterator", w.function("typed_subpart_iterator", "typed_subpart_iterator", { py_positional("msg"), py_positional("maintype", S<"text">()), py_positional("subtype", none()) }, [](arguments a) -> py
                {
                    return make_native_generator("typed_subpart_iterator", "typed_subpart_iterator", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py msg = a[0];
                        py maintype = a[1];
                        py subtype = a[2];
                        py subpart;
                        for (py const& item : iterate{ msg.method("walk") })
                        {
                            subpart = item;
                            if (subpart.method("get_content_maintype") == maintype)
                            {
                                if ((subtype.is_none() || (subpart.method("get_content_subtype") == subtype)))
                                {
                                    py_yield(subpart);
                                }
                            }
                        }
                        return none();
                    });
                }));

            define("email.iterators", "_structure", w.function("_structure", "_structure", { py_positional("msg"), py_positional("fp", none()), py_positional("level", I<0>()), py_positional("include_default", boolean(false)) }, [](arguments a) -> py
                {
                    py msg = a[0];
                    py fp = a[1];
                    py level = a[2];
                    py include_default = a[3];
                    py subpart;
                    py tab;
                    if (fp.is_none())
                    {
                        fp = Mod<"sys">().attr("stdout");
                    }
                    tab = (S<" ">()) * (level * (I<4>()));
                    (B<"print">()).call({ tab + msg.method("get_content_type") }, { { "end", S<"">() }, { "file", fp } });
                    if (include_default.truthy())
                    {
                        (B<"print">()).call({ (S<" [%s]">()) % msg.method("get_default_type") }, { { "file", fp } });
                    }
                    else
                    {
                        (B<"print">()).call({  }, { { "file", fp } });
                    }
                    if (msg.method("is_multipart").truthy())
                    {
                        for (py const& item : iterate{ msg.method("get_payload") })
                        {
                            subpart = item;
                            (G<"email.iterators._structure">())(subpart, fp, level + (I<1>()), include_default);
                        }
                    }
                    return none();
                }));
        }

        bool email_iterators_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_iterators();
                }
                if (auto const doc = email_iterators_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("StringIO", import_attribute("io", "StringIO").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.iterators", email_iterators_module);
    }
}
