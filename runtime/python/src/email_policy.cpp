/*
  email_policy.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/policy.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.policy (CPython 3.13's Lib/email/policy.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_policy_docs.hpp"

namespace neos::python_runtime::detail::email_policy_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.policy.name"))
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
                py_raise("SystemError", std::string{ "email.policy: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.policy.name">()))
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
            if (modules_imported("email.policy"))
                return py{ fn_import("email.policy") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.policy" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.policy", email_policy_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.policy (Lib/email/policy.py)

        void make_email_policy()
        {
            static module_writer const& w = writer("email.policy");
            define("email.policy", "__all__", py_list({ S<"Compat32">(), S<"compat32">(), S<"Policy">(), S<"EmailPolicy">(), S<"default">(), S<"strict">(), S<"SMTP">(), S<"HTTP">() }));
            define("email.policy", "linesep_splitter", (Mod<"re">()).method("compile", { S<"\\n|\\r\\n?">() }));
                // The logic of the next three methods is chosen such that it is possible to
                // switch a Message object between a Compat32 policy and a policy derived
                // from this class and have the results stay consistent.  This allows a
                // Message object constructed with this policy to be passed to a library
                // that only handles Compat32 objects, or to receive such an object and
                // convert it to use the newer style by just changing its policy.  It is
                // also chosen because it postpones the relatively expensive full rfc5322
                // parse until as late as possible when parsing from source, since in many
                // applications only a few headers will actually be inspected.
            define("email.policy", "EmailPolicy", (Mod<"email._policybase">().attr("_extend_docstrings"))(w.make_class("EmailPolicy", { Mod<"email._policybase">().attr("Policy") }, {
                { "message_factory", Mod<"email.message">().attr("EmailMessage") },
                { "utf8", boolean(false) },
                { "refold_source", S<"long">() },
                { "header_factory", (Mod<"email.headerregistry">().attr("HeaderRegistry"))() },
                { "content_manager", Mod<"email.contentmanager">().attr("raw_data_manager") },
                { "__init__", w.method("EmailPolicy", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kw = a[1];
                        if (!kw.contains((S<"header_factory">())))
                        {
                            (B<"object">()).method("__setattr__", { self, S<"header_factory">(), (Mod<"email.headerregistry">().attr("HeaderRegistry"))() });
                        }
                        call_ex((B<"super">()(G<"email.policy.EmailPolicy">(), self)).attr("__init__"), {  }, py{}, kw);
                        return none();
                    }, false, true) },
                { "header_max_count", w.method("EmailPolicy", "header_max_count", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.attr("header_factory")[name].attr("max_count");
                    }) },
                { "header_source_parse", w.method("EmailPolicy", "header_source_parse", { py_positional("sourcelines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sourcelines = a[1];
                        py name;
                        py value;
                        auto const items1 = unpack(sourcelines[I<0>()].method("split", { S<":">(), I<1>() }), 2);
                        name = items1[0];
                        value = items1[1];
                        value = (S<"">()).method("join", { B<"tuple">()(((py_list() + py_list({ value })) + B<"list">()(sourcelines[B<"slice">()(I<1>(), none(), none())]))) }).method("lstrip", { S<" \t\r\n">() });
                        return py_tuple({ name, value.method("rstrip", { S<"\r\n">() }) });
                    }) },
                { "header_store_parse", w.method("EmailPolicy", "header_store_parse", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        if ((has_attr(value, "name") && (value.attr("name").method("lower") == name.method("lower"))))
                        {
                            return py_tuple({ name, value });
                        }
                        if ((isinstance(value, B<"str">()) && (py{ value.method("splitlines").size() } > (I<1>()))))
                        {
                            // XXX this error message isn't quite right when we use splitlines
                            // (see issue 22233), but I'm not sure what should happen here.
                            py_raise((B<"ValueError">())(S<"Header values may not contain linefeed or carriage return characters">()));
                        }
                        return py_tuple({ name, self.method("header_factory", { name, value }) });
                    }) },
                { "header_fetch_parse", w.method("EmailPolicy", "header_fetch_parse", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        if (has_attr(value, "name"))
                        {
                            return value;
                        }
                        // We can't use splitlines here because it splits on more than \r and \n.
                        value = (S<"">()).method("join", { (G<"email.policy.linesep_splitter">()).method("split", { value }) });
                        return self.method("header_factory", { name, value });
                    }) },
                { "fold", w.method("EmailPolicy", "fold", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        return self.method("_fold", { name, value }, { { "refold_binary", boolean(true) } });
                    }) },
                { "fold_binary", w.method("EmailPolicy", "fold_binary", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py charset;
                        py folded;
                        folded = self.method("_fold", { name, value }, { { "refold_binary", boolean(self.attr("cte_type") == (S<"7bit">())) } });
                        charset = (self.attr("utf8").truthy() ? S<"utf8">() : S<"ascii">());
                        return folded.method("encode", { charset, S<"surrogateescape">() });
                    }) },
                { "_fold", w.method("EmailPolicy", "_fold", { py_positional("name"), py_positional("value"), py_positional("refold_binary", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py refold_binary = a[3];
                        py lines;
                        py maxlen;
                        py refold;
                        if (has_attr(value, "name"))
                        {
                            return value.method("fold", {  }, { { "policy", self } });
                        }
                        maxlen = (self.attr("max_line_length").truthy() ? self.attr("max_line_length") : Mod<"sys">().attr("maxsize"));
                        // We can't use splitlines here because it splits on more than \r and \n.
                        lines = (G<"email.policy.linesep_splitter">()).method("split", { value });
                        refold = [&]() -> py { py const t = boolean(self.attr("refold_source") == (S<"all">())); return t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("refold_source") == (S<"long">())); return !t.truthy() ? t : [&]() -> py { py const t = [&]() -> py { py const t = lines; return !t.truthy() ? t : boolean(((py{ lines[I<0>()].size() } + py{ name.size() }) + (I<2>())) > maxlen); }(); return t.truthy() ? t : (B<"any">())([&]() -> py { py const comprehension2 = py_list(); py x; for (py const& item : iterate{ lines[B<"slice">()(I<1>(), none(), none())] }) { x = item; comprehension2.method("append", { boolean(py{ x.size() } > maxlen) }); } return comprehension2; }()); }(); }(); }();
                        if (!refold.truthy())
                        {
                            if (!self.attr("utf8").truthy())
                            {
                                refold = boolean(!value.method("isascii").truthy());
                            }
                            else if (refold_binary.truthy())
                            {
                                refold = (Mod<"email.utils">().attr("_has_surrogates"))(value);
                            }
                        }
                        if (refold.truthy())
                        {
                            return self.method("header_factory", { name, (S<"">()).method("join", { lines }) }).method("fold", {  }, { { "policy", self } });
                        }
                        return ((name + (S<": ">())) + self.attr("linesep").method("join", { lines })) + self.attr("linesep");
                    }) }, })));

            define("email.policy", "default", (G<"email.policy.EmailPolicy">())());
            // Make the default policy use the class default header_factory
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    del_attr(G<"email.policy.default">(), "header_factory");
                    return none();
                }))();
            define("email.policy", "strict", (G<"email.policy.default">()).method("clone", {  }, { { "raise_on_defect", boolean(true) } }));
            define("email.policy", "SMTP", (G<"email.policy.default">()).method("clone", {  }, { { "linesep", S<"\r\n">() } }));
            define("email.policy", "HTTP", (G<"email.policy.default">()).method("clone", {  }, { { "linesep", S<"\r\n">() }, { "max_line_length", none() } }));
            define("email.policy", "SMTPUTF8", (G<"email.policy.SMTP">()).method("clone", {  }, { { "utf8", boolean(true) } }));
        }

        bool email_policy_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_policy();
                }
                if (auto const doc = email_policy_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("Policy", import_attribute("email._policybase", "Policy").release());
                aAttributes.emplace_back("Compat32", import_attribute("email._policybase", "Compat32").release());
                aAttributes.emplace_back("compat32", import_attribute("email._policybase", "compat32").release());
                aAttributes.emplace_back("_extend_docstrings", import_attribute("email._policybase", "_extend_docstrings").release());
                aAttributes.emplace_back("_has_surrogates", import_attribute("email.utils", "_has_surrogates").release());
                aAttributes.emplace_back("HeaderRegistry", import_attribute("email.headerregistry", "HeaderRegistry").release());
                aAttributes.emplace_back("raw_data_manager", import_attribute("email.contentmanager", "raw_data_manager").release());
                aAttributes.emplace_back("EmailMessage", import_attribute("email.message", "EmailMessage").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.policy", email_policy_module);
    }
}
