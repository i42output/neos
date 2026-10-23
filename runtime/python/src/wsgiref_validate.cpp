/*
  wsgiref_validate.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/wsgiref/validate.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/wsgiref/validate.py's copyright: (c) 2005 Ian Bicking and
  contributors; written for Paste (http://pythonpaste.org). Licensed under the MIT license (see runtime/python/NOTICE);
  licensed to PSF under a Contributor Agreement.
*/

// The module wsgiref.validate (CPython 3.13's Lib/wsgiref/validate.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "wsgiref_validate_docs.hpp"

namespace neos::python_runtime::detail::wsgiref_validate_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "wsgiref.validate.name"))
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
                py_raise("SystemError", std::string{ "wsgiref.validate: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"wsgiref.validate.name">()))
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
            if (modules_imported("wsgiref.validate"))
                return py{ fn_import("wsgiref.validate") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "wsgiref.validate" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "wsgiref.validate", wsgiref_validate_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- wsgiref.validate (Lib/wsgiref/validate.py)

        void make_wsgiref_validate()
        {
            static module_writer const& w = writer("wsgiref.validate");
            // (c) 2005 Ian Bicking and contributors; written for Paste (http://pythonpaste.org)
            // Licensed under the MIT license: https://opensource.org/licenses/mit-license.php
            // Also licenced under the Apache License, 2.0: https://opensource.org/licenses/apache2.0.php
            // Licensed to PSF under a Contributor Agreement
            define("wsgiref.validate", "__all__", py_list({ S<"validator">() }));
            define("wsgiref.validate", "header_re", (Mod<"re">()).method("compile", { S<"^[a-zA-Z][a-zA-Z0-9\\-_]*$">() }));
            define("wsgiref.validate", "bad_header_value_re", (Mod<"re">()).method("compile", { S<"[\\000-\\037]">() }));
            define("wsgiref.validate", "WSGIWarning", w.make_class("WSGIWarning", { B<"Warning">() }, {
 }));

            define("wsgiref.validate", "assert_", w.function("assert_", "assert_", { py_positional("cond") }, [](arguments a) -> py
                {
                    py cond = a[0];
                    py args = a[1];
                    if (!cond.truthy())
                    {
                        py_raise(call_ex((B<"AssertionError">()), {  }, args, py{}));
                    }
                    return none();
                }, true));

            define("wsgiref.validate", "check_string_type", w.function("check_string_type", "check_string_type", { py_positional("value"), py_positional("title") }, [](arguments a) -> py
                {
                    py value = a[0];
                    py title = a[1];
                    if ((B<"type">())(value).is((B<"str">())))
                    {
                        return value;
                    }
                    py_raise((B<"AssertionError">())((S<"{0} must be of type str (got {1})">()).method("format", { title, S(value.repr()) })));
                }));

            define("wsgiref.validate", "validator", w.function("validator", "validator", { py_positional("application") }, [](arguments a) -> py
                {
                    auto const application = std::make_shared<py>(a[0]);
                    py lint_app;
                    lint_app = w.function("lint_app", "validator.<locals>.lint_app", {  }, [application](arguments a) -> py
    {
        py args = a[0];
        py kw = a[1];
        py environ_;
        py iterator;
        auto const start_response = std::make_shared<py>();
        auto const start_response_started = std::make_shared<py>();
        py start_response_wrapper;
        (G<"wsgiref.validate.assert_">())(boolean(py{ args.size() } == (I<2>())), S<"Two arguments required">());
        (G<"wsgiref.validate.assert_">())(boolean(!kw.truthy()), S<"No keyword arguments allowed">());
        auto const items1 = unpack(args, 2);
        environ_ = items1[0];
        *start_response = items1[1];
        (G<"wsgiref.validate.check_environ">())(environ_);
        // We use this to check if the application returns without
        // calling start_response:
        *start_response_started = py_list();
        start_response_wrapper = w.function("start_response_wrapper", "validator.<locals>.lint_app.<locals>.start_response_wrapper", {  }, [start_response, start_response_started](arguments a) -> py
    {
        py args = a[0];
        py kw = a[1];
        py exc_info;
        py headers;
        py status;
        (G<"wsgiref.validate.assert_">())([&]() -> py { py const t = boolean(py{ args.size() } == (I<2>())); return t.truthy() ? t : boolean(py{ args.size() } == (I<3>())); }(), (S<"Invalid number of arguments: %s">()) % py_tuple({ args }));
        (G<"wsgiref.validate.assert_">())(boolean(!kw.truthy()), S<"No keyword arguments allowed">());
        status = args[I<0>()];
        headers = args[I<1>()];
        if (py{ args.size() } == (I<3>()))
        {
            exc_info = args[I<2>()];
        }
        else
        {
            exc_info = none();
        }
        (G<"wsgiref.validate.check_status">())(status);
        (G<"wsgiref.validate.check_headers">())(headers);
        (G<"wsgiref.validate.check_content_type">())(status, headers);
        (G<"wsgiref.validate.check_exc_info">())(exc_info);
        (*start_response_started).method("append", { none() });
        return (G<"wsgiref.validate.WriteWrapper">())(call_ex((*start_response), {  }, args, py{}));
    }, true, true);
        environ_.set_item(S<"wsgi.input">(), (G<"wsgiref.validate.InputWrapper">())(environ_[S<"wsgi.input">()]));
        environ_.set_item(S<"wsgi.errors">(), (G<"wsgiref.validate.ErrorWrapper">())(environ_[S<"wsgi.errors">()]));
        iterator = (*application)(environ_, start_response_wrapper);
        (G<"wsgiref.validate.assert_">())([&]() -> py { py const t = boolean(!iterator.is_none()); return !t.truthy() ? t : boolean(iterator != boolean(false)); }(), S<"The application must return an iterator, if only an empty list">());
        (G<"wsgiref.validate.check_iterator">())(iterator);
        return (G<"wsgiref.validate.IteratorWrapper">())(iterator, (*start_response_started));
    }, true, true);
                    return lint_app;
                }));

            define("wsgiref.validate", "InputWrapper", w.make_class("InputWrapper", {  }, {
                { "__init__", w.method("InputWrapper", "__init__", { py_positional("wsgi_input") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wsgi_input = a[1];
                        self.set_attr("input", wsgi_input);
                        return none();
                    }) },
                { "read", w.method("InputWrapper", "read", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py v;
                        (G<"wsgiref.validate.assert_">())(boolean(py{ args.size() } == (I<1>())));
                        v = call_ex(self.attr("input").attr("read"), {  }, args, py{});
                        (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(v).is((B<"bytes">()))));
                        return v;
                    }, true) },
                { "readline", w.method("InputWrapper", "readline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py v;
                        (G<"wsgiref.validate.assert_">())(boolean(py{ args.size() } <= (I<1>())));
                        v = call_ex(self.attr("input").attr("readline"), {  }, args, py{});
                        (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(v).is((B<"bytes">()))));
                        return v;
                    }, true) },
                { "readlines", w.method("InputWrapper", "readlines", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py line;
                        py lines;
                        (G<"wsgiref.validate.assert_">())(boolean(py{ args.size() } <= (I<1>())));
                        lines = call_ex(self.attr("input").attr("readlines"), {  }, args, py{});
                        (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(lines).is((B<"list">()))));
                        for (py const& item : iterate{ lines })
                        {
                            line = item;
                            (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(line).is((B<"bytes">()))));
                        }
                        return lines;
                    }, true) },
                { "__iter__", w.method("InputWrapper", "__iter__", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("__iter__", "InputWrapper.__iter__", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py line;
                            while (([&]() -> py { line = self.method("readline"); return line; }()).truthy())
                            {
                                py_yield(line);
                            }
                            return none();
                        });
                    }) },
                { "close", w.method("InputWrapper", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (G<"wsgiref.validate.assert_">())(I<0>(), S<"input.close() must not be called">());
                        return none();
                    }) }, }));

            define("wsgiref.validate", "ErrorWrapper", w.make_class("ErrorWrapper", {  }, {
                { "__init__", w.method("ErrorWrapper", "__init__", { py_positional("wsgi_errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wsgi_errors = a[1];
                        self.set_attr("errors", wsgi_errors);
                        return none();
                    }) },
                { "write", w.method("ErrorWrapper", "write", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(s).is((B<"str">()))));
                        self.attr("errors").method("write", { s });
                        return none();
                    }) },
                { "flush", w.method("ErrorWrapper", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("errors").method("flush");
                        return none();
                    }) },
                { "writelines", w.method("ErrorWrapper", "writelines", { py_positional("seq") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py seq = a[1];
                        py line;
                        for (py const& item : iterate{ seq })
                        {
                            line = item;
                            self.method("write", { line });
                        }
                        return none();
                    }) },
                { "close", w.method("ErrorWrapper", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (G<"wsgiref.validate.assert_">())(I<0>(), S<"errors.close() must not be called">());
                        return none();
                    }) }, }));

            define("wsgiref.validate", "WriteWrapper", w.make_class("WriteWrapper", {  }, {
                { "__init__", w.method("WriteWrapper", "__init__", { py_positional("wsgi_writer") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wsgi_writer = a[1];
                        self.set_attr("writer", wsgi_writer);
                        return none();
                    }) },
                { "__call__", w.method("WriteWrapper", "__call__", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(s).is((B<"bytes">()))));
                        self.method("writer", { s });
                        return none();
                    }) }, }));

            define("wsgiref.validate", "PartialIteratorWrapper", w.make_class("PartialIteratorWrapper", {  }, {
                { "__init__", w.method("PartialIteratorWrapper", "__init__", { py_positional("wsgi_iterator") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wsgi_iterator = a[1];
                        self.set_attr("iterator", wsgi_iterator);
                        return none();
                    }) },
                { "__iter__", w.method("PartialIteratorWrapper", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"wsgiref.validate.IteratorWrapper">())(self.attr("iterator"), none());
                    }) }, }));

            define("wsgiref.validate", "IteratorWrapper", w.make_class("IteratorWrapper", {  }, {
                { "__init__", w.method("IteratorWrapper", "__init__", { py_positional("wsgi_iterator"), py_positional("check_start_response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wsgi_iterator = a[1];
                        py check_start_response = a[2];
                        self.set_attr("original_iterator", wsgi_iterator);
                        self.set_attr("iterator", (B<"iter">())(wsgi_iterator));
                        self.set_attr("closed", boolean(false));
                        self.set_attr("check_start_response", check_start_response);
                        return none();
                    }) },
                { "__iter__", w.method("IteratorWrapper", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__next__", w.method("IteratorWrapper", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py v;
                        (G<"wsgiref.validate.assert_">())(boolean(!self.attr("closed").truthy()), S<"Iterator read after closed">());
                        v = (B<"next">())(self.attr("iterator"));
                        if (!(B<"type">())(v).is((B<"bytes">())))
                        {
                            (G<"wsgiref.validate.assert_">())(boolean(false), (S<"Iterator yielded non-bytestring (%r)">()) % py_tuple({ v }));
                        }
                        if (!self.attr("check_start_response").is_none())
                        {
                            (G<"wsgiref.validate.assert_">())(self.attr("check_start_response"), S<"The application returns and we started iterating over its body, but start_response has not yet been called">());
                            self.set_attr("check_start_response", none());
                        }
                        return v;
                    }) },
                { "close", w.method("IteratorWrapper", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("closed", boolean(true));
                        if (has_attr(self.attr("original_iterator"), "close"))
                        {
                            self.attr("original_iterator").method("close");
                        }
                        return none();
                    }) },
                { "__del__", w.method("IteratorWrapper", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("closed").truthy())
                        {
                            (Mod<"sys">().attr("stderr")).method("write", { S<"Iterator garbage collected without being closed">() });
                        }
                        (G<"wsgiref.validate.assert_">())(self.attr("closed"), S<"Iterator garbage collected without being closed">());
                        return none();
                    }) }, }));

            define("wsgiref.validate", "check_environ", w.function("check_environ", "check_environ", { py_positional("environ") }, [](arguments a) -> py
                {
                    py environ_ = a[0];
                    py key;
                    (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(environ_).is((B<"dict">()))), (S<"Environment is not of the right type: %r (environment: %r)">()) % py_tuple({ (B<"type">())(environ_), environ_ }));
                    for (py const& item : iterate{ py_list({ S<"REQUEST_METHOD">(), S<"SERVER_NAME">(), S<"SERVER_PORT">(), S<"wsgi.version">(), S<"wsgi.input">(), S<"wsgi.errors">(), S<"wsgi.multithread">(), S<"wsgi.multiprocess">(), S<"wsgi.run_once">() }) })
                    {
                        key = item;
                        (G<"wsgiref.validate.assert_">())(boolean(environ_.contains(key)), (S<"Environment missing required key: %r">()) % py_tuple({ key }));
                    }
                    for (py const& item : iterate{ py_list({ S<"HTTP_CONTENT_TYPE">(), S<"HTTP_CONTENT_LENGTH">() }) })
                    {
                        key = item;
                        (G<"wsgiref.validate.assert_">())(boolean(!environ_.contains(key)), (S<"Environment should not have the key: %s (use %s instead)">()) % py_tuple({ key, key[B<"slice">()(I<5>(), none(), none())] }));
                    }
                    if (!environ_.contains((S<"QUERY_STRING">())))
                    {
                        (Mod<"warnings">().attr("warn"))(S<"QUERY_STRING is not in the WSGI environment; the cgi module will use sys.argv when this variable is missing, so application errors are more likely">(), G<"wsgiref.validate.WSGIWarning">());
                    }
                    for (py const& item : iterate{ environ_.method("keys") })
                    {
                        key = item;
                        if (key.contains((S<".">())))
                        {
                            // Extension, we don't care about its type
                            continue;
                        }
                        (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(environ_[key]).is((B<"str">()))), (S<"Environmental variable %s is not a string: %r (value: %r)">()) % py_tuple({ key, (B<"type">())(environ_[key]), environ_[key] }));
                    }
                    (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(environ_[S<"wsgi.version">()]).is((B<"tuple">()))), (S<"wsgi.version should be a tuple (%r)">()) % py_tuple({ environ_[S<"wsgi.version">()] }));
                    (G<"wsgiref.validate.assert_">())(boolean(py_tuple({ S<"http">(), S<"https">() }).contains(environ_[S<"wsgi.url_scheme">()])), (S<"wsgi.url_scheme unknown: %r">()) % environ_[S<"wsgi.url_scheme">()]);
                    (G<"wsgiref.validate.check_input">())(environ_[S<"wsgi.input">()]);
                    (G<"wsgiref.validate.check_errors">())(environ_[S<"wsgi.errors">()]);
                    // @@: these need filling out:
                    if (!py_tuple({ S<"GET">(), S<"HEAD">(), S<"POST">(), S<"OPTIONS">(), S<"PATCH">(), S<"PUT">(), S<"DELETE">(), S<"TRACE">() }).contains(environ_[S<"REQUEST_METHOD">()]))
                    {
                        (Mod<"warnings">().attr("warn"))((S<"Unknown REQUEST_METHOD: %r">()) % environ_[S<"REQUEST_METHOD">()], G<"wsgiref.validate.WSGIWarning">());
                    }
                    (G<"wsgiref.validate.assert_">())([&]() -> py { py const t = boolean(!environ_.method("get", { S<"SCRIPT_NAME">() }).truthy()); return t.truthy() ? t : environ_[S<"SCRIPT_NAME">()].method("startswith", { S<"/">() }); }(), (S<"SCRIPT_NAME doesn't start with /: %r">()) % environ_[S<"SCRIPT_NAME">()]);
                    (G<"wsgiref.validate.assert_">())([&]() -> py { py const t = boolean(!environ_.method("get", { S<"PATH_INFO">() }).truthy()); return t.truthy() ? t : environ_[S<"PATH_INFO">()].method("startswith", { S<"/">() }); }(), (S<"PATH_INFO doesn't start with /: %r">()) % environ_[S<"PATH_INFO">()]);
                    if (environ_.method("get", { S<"CONTENT_LENGTH">() }).truthy())
                    {
                        (G<"wsgiref.validate.assert_">())(boolean((B<"int">())(environ_[S<"CONTENT_LENGTH">()]) >= (I<0>())), (S<"Invalid CONTENT_LENGTH: %r">()) % environ_[S<"CONTENT_LENGTH">()]);
                    }
                    if (!environ_.method("get", { S<"SCRIPT_NAME">() }).truthy())
                    {
                        (G<"wsgiref.validate.assert_">())(boolean(environ_.contains((S<"PATH_INFO">()))), S<"One of SCRIPT_NAME or PATH_INFO are required (PATH_INFO should at least be '/' if SCRIPT_NAME is empty)">());
                    }
                    (G<"wsgiref.validate.assert_">())(boolean(environ_.method("get", { S<"SCRIPT_NAME">() }) != (S<"/">())), S<"SCRIPT_NAME cannot be '/'; it should instead be '', and PATH_INFO should be '/'">());
                    return none();
                }));

            define("wsgiref.validate", "check_input", w.function("check_input", "check_input", { py_positional("wsgi_input") }, [](arguments a) -> py
                {
                    py wsgi_input = a[0];
                    py attr;
                    for (py const& item : iterate{ py_list({ S<"read">(), S<"readline">(), S<"readlines">(), S<"__iter__">() }) })
                    {
                        attr = item;
                        (G<"wsgiref.validate.assert_">())((B<"hasattr">())(wsgi_input, attr), (S<"wsgi.input (%r) doesn't have the attribute %s">()) % py_tuple({ wsgi_input, attr }));
                    }
                    return none();
                }));

            define("wsgiref.validate", "check_errors", w.function("check_errors", "check_errors", { py_positional("wsgi_errors") }, [](arguments a) -> py
                {
                    py wsgi_errors = a[0];
                    py attr;
                    for (py const& item : iterate{ py_list({ S<"flush">(), S<"write">(), S<"writelines">() }) })
                    {
                        attr = item;
                        (G<"wsgiref.validate.assert_">())((B<"hasattr">())(wsgi_errors, attr), (S<"wsgi.errors (%r) doesn't have the attribute %s">()) % py_tuple({ wsgi_errors, attr }));
                    }
                    return none();
                }));

            define("wsgiref.validate", "check_status", w.function("check_status", "check_status", { py_positional("status") }, [](arguments a) -> py
                {
                    py status = a[0];
                    py status_code;
                    py status_int;
                    status = (G<"wsgiref.validate.check_string_type">())(status, S<"Status">());
                    // Implicitly check that we can turn it into an integer:
                    status_code = status.method("split", { none(), I<1>() })[I<0>()];
                    (G<"wsgiref.validate.assert_">())(boolean(py{ status_code.size() } == (I<3>())), (S<"Status codes must be three characters: %r">()) % status_code);
                    status_int = (B<"int">())(status_code);
                    (G<"wsgiref.validate.assert_">())(boolean(status_int >= (I<100>())), (S<"Status code is invalid: %r">()) % status_int);
                    if (((py{ status.size() } < (I<4>())) || (status[I<3>()] != (S<" ">()))))
                    {
                        (Mod<"warnings">().attr("warn"))((S<"The status string (%r) should be a three-digit integer followed by a single space and a status explanation">()) % status, G<"wsgiref.validate.WSGIWarning">());
                    }
                    return none();
                }));

            define("wsgiref.validate", "check_headers", w.function("check_headers", "check_headers", { py_positional("headers") }, [](arguments a) -> py
                {
                    py headers = a[0];
                    py item_;
                    py name;
                    py value;
                    (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(headers).is((B<"list">()))), (S<"Headers (%r) must be of type list: %r">()) % py_tuple({ headers, (B<"type">())(headers) }));
                    for (py const& item : iterate{ headers })
                    {
                        item_ = item;
                        (G<"wsgiref.validate.assert_">())(boolean((B<"type">())(item_).is((B<"tuple">()))), (S<"Individual headers (%r) must be of type tuple: %r">()) % py_tuple({ item_, (B<"type">())(item_) }));
                        (G<"wsgiref.validate.assert_">())(boolean(py{ item_.size() } == (I<2>())));
                        auto const items11 = unpack(item_, 2);
                        name = items11[0];
                        value = items11[1];
                        name = (G<"wsgiref.validate.check_string_type">())(name, S<"Header name">());
                        value = (G<"wsgiref.validate.check_string_type">())(value, S<"Header value">());
                        (G<"wsgiref.validate.assert_">())(boolean(name.method("lower") != (S<"status">())), (S<"The Status header cannot be used; it conflicts with CGI script, and HTTP status is not given through headers (value: %r).">()) % value);
                        (G<"wsgiref.validate.assert_">())([&]() -> py { py const t = boolean(!name.contains((S<"\n">()))); return !t.truthy() ? t : boolean(!name.contains((S<":">()))); }(), (S<"Header names may not contain ':' or '\\n': %r">()) % name);
                        (G<"wsgiref.validate.assert_">())((G<"wsgiref.validate.header_re">()).method("search", { name }), (S<"Bad header name: %r">()) % name);
                        (G<"wsgiref.validate.assert_">())([&]() -> py { py const t = boolean(!name.method("endswith", { S<"-">() }).truthy()); return !t.truthy() ? t : boolean(!name.method("endswith", { S<"_">() }).truthy()); }(), (S<"Names may not end in '-' or '_': %r">()) % name);
                        if ((G<"wsgiref.validate.bad_header_value_re">()).method("search", { value }).truthy())
                        {
                            (G<"wsgiref.validate.assert_">())(I<0>(), (S<"Bad header value: %r (bad char: %r)">()) % py_tuple({ value, (G<"wsgiref.validate.bad_header_value_re">()).method("search", { value }).method("group", { I<0>() }) }));
                        }
                    }
                    return none();
                }));

            define("wsgiref.validate", "check_content_type", w.function("check_content_type", "check_content_type", { py_positional("status"), py_positional("headers") }, [](arguments a) -> py
                {
                    py status = a[0];
                    py headers = a[1];
                    py NO_MESSAGE_BODY;
                    py code;
                    py name;
                    py value;
                    status = (G<"wsgiref.validate.check_string_type">())(status, S<"Status">());
                    code = (B<"int">())(status.method("split", { none(), I<1>() })[I<0>()]);
                    // @@: need one more person to verify this interpretation of RFC 2616
                    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
                    NO_MESSAGE_BODY = py_tuple({ I<204>(), I<304>() });
                    for (py const& item : iterate{ headers })
                    {
                        auto const items13 = unpack(item, 2);
                        name = items13[0];
                        value = items13[1];
                        name = (G<"wsgiref.validate.check_string_type">())(name, S<"Header name">());
                        if (name.method("lower") == (S<"content-type">()))
                        {
                            if (!NO_MESSAGE_BODY.contains(code))
                            {
                                return none();
                            }
                            (G<"wsgiref.validate.assert_">())(I<0>(), (S<"Content-Type header found in a %s response, which must not return content.">()) % code);
                        }
                    }
                    if (!NO_MESSAGE_BODY.contains(code))
                    {
                        (G<"wsgiref.validate.assert_">())(I<0>(), (S<"No Content-Type header found in headers (%s)">()) % headers);
                    }
                    return none();
                }));

            define("wsgiref.validate", "check_exc_info", w.function("check_exc_info", "check_exc_info", { py_positional("exc_info") }, [](arguments a) -> py
                {
                    py exc_info = a[0];
                    (G<"wsgiref.validate.assert_">())([&]() -> py { py const t = boolean(exc_info.is_none()); return t.truthy() ? t : boolean((B<"type">())(exc_info).is((B<"tuple">()))); }(), (S<"exc_info (%r) is not a tuple: %r">()) % py_tuple({ exc_info, (B<"type">())(exc_info) }));
                    return none();
                }));

            // More exc_info checks?
            define("wsgiref.validate", "check_iterator", w.function("check_iterator", "check_iterator", { py_positional("iterator") }, [](arguments a) -> py
                {
                    py iterator = a[0];
                    (G<"wsgiref.validate.assert_">())(boolean(!isinstance(iterator, py_tuple({ B<"str">(), B<"bytes">() }))), S<"You should not return a string as your application iterator, instead return a single-item list containing a bytestring.">());
                    return none();
                }));
        }

        bool wsgiref_validate_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_wsgiref_validate();
                }
                if (auto const doc = wsgiref_validate_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("wsgiref.validate", wsgiref_validate_module);
    }
}
