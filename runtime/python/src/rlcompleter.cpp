/*
  rlcompleter.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/rlcompleter.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module rlcompleter (CPython 3.13's Lib/rlcompleter.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "rlcompleter_docs.hpp"

namespace neos::python_runtime::detail::rlcompleter_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "rlcompleter.name"))
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
                py_raise("SystemError", std::string{ "rlcompleter: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"rlcompleter.name">()))
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
            if (modules_imported("rlcompleter"))
                return py{ fn_import("rlcompleter") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "rlcompleter" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "rlcompleter", rlcompleter_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- rlcompleter (Lib/rlcompleter.py)

        void make_rlcompleter()
        {
            static module_writer const& w = writer("rlcompleter");
            define("rlcompleter", "__all__", py_list({ S<"Completer">() }));
            define("rlcompleter", "Completer", w.make_class("Completer", {  }, {
                { "__init__", w.method("Completer", "__init__", { py_positional("namespace", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespace_ = a[1];
                        if ((namespace_.truthy() && (!isinstance(namespace_, B<"dict">()))))
                        {
                            py_raise((B<"TypeError">())(S<"namespace must be a dictionary">()));
                        }
                        // Don't bind to namespace quite yet, but flag whether the user wants a
                        // specific namespace or to use __main__.__dict__. This will allow us
                        // to bind to __main__.__dict__ at completion time, not now.
                        if (namespace_.is_none())
                        {
                            self.set_attr("use_main_ns", I<1>());
                        }
                        else
                        {
                            self.set_attr("use_main_ns", I<0>());
                            self.set_attr("namespace", namespace_);
                        }
                        return none();
                    }) },
                { "complete", w.method("Completer", "complete", { py_positional("text"), py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py state = a[2];
                        if (self.attr("use_main_ns").truthy())
                        {
                            self.set_attr("namespace", Mod<"__main__">().attr("__dict__"));
                        }
                        if (!text.method("strip").truthy())
                        {
                            if (state == (I<0>()))
                            {
                                if ((G<"rlcompleter._readline_available">()).truthy())
                                {
                                    (Mod<"readline">().attr("insert_text"))(S<"\t">());
                                    (Mod<"readline">().attr("redisplay"))();
                                    return S<"">();
                                }
                                else
                                {
                                    return S<"\t">();
                                }
                            }
                            else
                            {
                                return none();
                            }
                        }
                        if (state == (I<0>()))
                        {
                            {
                                py const manager1 = (Mod<"warnings">().attr("catch_warnings")).call({  }, { { "action", S<"ignore">() } });
                                py const exit1 = pyclass::special_method(manager1, "__exit__");
                                py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                                [[maybe_unused]] int flow1 = 0;
                                [[maybe_unused]] py result1;
                                bool exited1 = false;
                                try
                                {
                                    if (text.contains((S<".">())))
                                    {
                                        self.set_attr("matches", self.method("attr_matches", { text }));
                                    }
                                    else
                                    {
                                        self.set_attr("matches", self.method("global_matches", { text }));
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler1;
                                    exited1 = true;
                                    if (!exit1(manager1, type_of(handler1.exception), handler1.exception, handler1.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler1.exception);
                                }
                                finally1:
                                if (!exited1)
                                    exit1(manager1, none(), none(), none());
                                if (flow1 == 1)
                                    return result1;
                            }
                        }
                        try
                        {
                            return self.attr("matches")[state];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, B<"IndexError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                        return none();
                    }) },
                { "_callable_postfix", w.method("Completer", "_callable_postfix", { py_positional("val"), py_positional("word") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val = a[1];
                        py word = a[2];
                        if (callable(val))
                        {
                            word = inplace(NEOS_PY_ADD, word, S<"(">());
                            try
                            {
                                if (!(Mod<"inspect">().attr("signature"))(val).attr("parameters").truthy())
                                {
                                    word = inplace(NEOS_PY_ADD, word, S<")">());
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, B<"ValueError">()))
                                {
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                        }
                        return word;
                    }) },
                { "global_matches", w.method("Completer", "global_matches", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py matches_;
                        py n;
                        py nspace;
                        py seen;
                        py val;
                        py word;
                        matches_ = py_list();
                        seen = set_of({ S<"__builtins__">() });
                        n = py{ text.size() };
                        for (py const& item : iterate{ (Mod<"keyword">().attr("kwlist")) + (Mod<"keyword">().attr("softkwlist")) })
                        {
                            word = item;
                            if (word[B<"slice">()(none(), n, none())] == text)
                            {
                                seen.method("add", { word });
                                if (set_of({ S<"finally">(), S<"try">() }).contains(word))
                                {
                                    word = word + (S<":">());
                                }
                                else if (!set_of({ S<"False">(), S<"None">(), S<"True">(), S<"break">(), S<"continue">(), S<"pass">(), S<"else">(), S<"_">() }).contains(word))
                                {
                                    word = word + (S<" ">());
                                }
                                matches_.method("append", { word });
                            }
                        }
                        for (py const& item : iterate{ py_list({ self.attr("namespace"), Mod<"builtins">().attr("__dict__") }) })
                        {
                            nspace = item;
                            for (py const& item : iterate{ nspace.method("items") })
                            {
                                auto const items7 = unpack(item, 2);
                                word = items7[0];
                                val = items7[1];
                                if (((word[B<"slice">()(none(), n, none())] == text) && (!seen.contains(word))))
                                {
                                    seen.method("add", { word });
                                    matches_.method("append", { self.method("_callable_postfix", { val, word }) });
                                }
                            }
                        }
                        return matches_;
                    }) },
                { "attr_matches", w.method("Completer", "attr_matches", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py attr;
                        py class_attr;
                        py expr;
                        py m;
                        py match;
                        py matches_;
                        py n;
                        py noprefix;
                        py thisobject;
                        py value;
                        py word;
                        py words;
                        m = (Mod<"re">().attr("match"))(S<"(\\w+(\\.\\w+)*)\\.(\\w*)">(), text);
                        if (!m.truthy())
                        {
                            return py_list();
                        }
                        auto const items8 = unpack(m.method("group", { I<1>(), I<3>() }), 2);
                        expr = items8[0];
                        attr = items8[1];
                        try
                        {
                            thisobject = (B<"eval">())(expr, self.attr("namespace"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, B<"Exception">()))
                            {
                                return py_list();
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        // get the content of the object, except __builtins__
                        words = (B<"set">())((B<"dir">())(thisobject));
                        words.method("discard", { S<"__builtins__">() });
                        if (has_attr(thisobject, "__class__"))
                        {
                            words.method("add", { S<"__class__">() });
                            words.method("update", { (G<"rlcompleter.get_class_members">())(thisobject.attr("__class__")) });
                        }
                        matches_ = py_list();
                        n = py{ attr.size() };
                        if (attr == (S<"">()))
                        {
                            noprefix = S<"_">();
                        }
                        else if (attr == (S<"_">()))
                        {
                            noprefix = S<"__">();
                        }
                        else
                        {
                            noprefix = none();
                        }
                        while (boolean(true).truthy())
                        {
                            for (py const& item : iterate{ words })
                            {
                                word = item;
                                if (((word[B<"slice">()(none(), n, none())] == attr) && (!([&]() -> py { py const t = noprefix; return !t.truthy() ? t : boolean(word[B<"slice">()(none(), n + (I<1>()), none())] == noprefix); }()).truthy())))
                                {
                                    match = (S<"%s.%s">()) % py_tuple({ expr, word });
                                    class_attr = (B<"getattr">())((B<"type">())(thisobject), word, none());
                                    if ((isinstance(class_attr, py_tuple({ B<"property">(), Mod<"types">().attr("GetSetDescriptorType"), Mod<"types">().attr("MemberDescriptorType") })) || ([&]() -> py { py const t = boolean(has_attr(class_attr, "__get__")); return !t.truthy() ? t : boolean(!callable(class_attr)); }()).truthy()))
                                    {
                                        // Avoid evaluating descriptors, which could run
                                        // arbitrary code or raise exceptions.
                                        matches_.method("append", { match });
                                        continue;
                                    }
                                    if (!([&]() -> py { value = (B<"getattr">())(thisobject, word, none()); return value; }()).is_none())
                                    {
                                        matches_.method("append", { self.method("_callable_postfix", { value, match }) });
                                    }
                                    else
                                    {
                                        matches_.method("append", { match });
                                    }
                                }
                            }
                            if ((matches_.truthy() || (!noprefix.truthy())))
                            {
                                break;
                            }
                            if (noprefix == (S<"_">()))
                            {
                                noprefix = S<"__">();
                            }
                            else
                            {
                                noprefix = none();
                            }
                        }
                        matches_.method("sort");
                        return matches_;
                    }) }, }));

            define("rlcompleter", "get_class_members", w.function("get_class_members", "get_class_members", { py_positional("klass") }, [](arguments a) -> py
                {
                    py klass = a[0];
                    py base;
                    py ret;
                    ret = (B<"dir">())(klass);
                    if (has_attr(klass, "__bases__"))
                    {
                        for (py const& item : iterate{ klass.attr("__bases__") })
                        {
                            base = item;
                            ret = ret + (G<"rlcompleter.get_class_members">())(base);
                        }
                    }
                    return ret;
                }));

            define("rlcompleter", "_readline_available", boolean(false));
        }

        bool rlcompleter_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_rlcompleter();
                }
                if (auto const doc = rlcompleter_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("atexit", py{ fn_import("atexit") }.release());
                aAttributes.emplace_back("builtins", py{ fn_import("builtins") }.release());
                aAttributes.emplace_back("inspect", py{ fn_import("inspect") }.release());
                aAttributes.emplace_back("keyword", py{ fn_import("keyword") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("types", py{ fn_import("types") }.release());
                aAttributes.emplace_back("__main__", py{ fn_import("__main__") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("rlcompleter", rlcompleter_module);
    }
}
