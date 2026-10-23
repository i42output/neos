/*
  unittest_result.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/result.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/result.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.result (CPython 3.13's Lib/unittest/result.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_result_docs.hpp"

namespace neos::python_runtime::detail::unittest_result_module
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.result.name"))
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
                py_raise("SystemError", std::string{ "unittest.result: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.result.name">()))
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
            static module_writer const sWriter{ "unittest.result", unittest_result_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.result (Lib/unittest/result.py)

        void make_unittest_result()
        {
            static module_writer const& w = writer("unittest.result");
            define("unittest.result", "__unittest", boolean(true));
            define("unittest.result", "failfast", w.function("failfast", "failfast", { py_positional("method") }, [](arguments a) -> py
                {
                    auto const method = std::make_shared<py>(a[0]);
                    py inner;
                    inner = (Mod<"functools">().attr("wraps"))((*method))(w.function("inner", "failfast.<locals>.inner", { py_positional("self") }, [method](arguments a) -> py
    {
        py self = a[0];
        py args = a[1];
        py kw = a[2];
        if ((B<"getattr">())(self, S<"failfast">(), boolean(false)).truthy())
        {
            self.method("stop");
        }
        return call_ex((*method), { self }, args, kw);
    }, true, true));
                    return inner;
                }));

            define("unittest.result", "STDOUT_LINE", S<"\nStdout:\n%s">());
            define("unittest.result", "STDERR_LINE", S<"\nStderr:\n%s">());
            define("unittest.result", "TestResult", w.make_class("TestResult", { B<"object">() }, {
                { "_previousTestClass", none() },
                { "_testRunEntered", boolean(false) },
                { "_moduleSetUpFailed", boolean(false) },
                { "__init__", w.method("TestResult", "__init__", { py_positional("stream", none()), py_positional("descriptions", none()), py_positional("verbosity", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        py descriptions = a[2];
                        py verbosity = a[3];
                        self.set_attr("failfast", boolean(false));
                        self.set_attr("failures", py_list());
                        self.set_attr("errors", py_list());
                        self.set_attr("testsRun", I<0>());
                        self.set_attr("skipped", py_list());
                        self.set_attr("expectedFailures", py_list());
                        self.set_attr("unexpectedSuccesses", py_list());
                        self.set_attr("collectedDurations", py_list());
                        self.set_attr("shouldStop", boolean(false));
                        self.set_attr("buffer", boolean(false));
                        self.set_attr("tb_locals", boolean(false));
                        self.set_attr("_stdout_buffer", none());
                        self.set_attr("_stderr_buffer", none());
                        self.set_attr("_original_stdout", Mod<"sys">().attr("stdout"));
                        self.set_attr("_original_stderr", Mod<"sys">().attr("stderr"));
                        self.set_attr("_mirrorOutput", boolean(false));
                        return none();
                    }) },
                { "printErrors", w.method("TestResult", "printErrors", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "startTest", w.method("TestResult", "startTest", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        {
                            py const target1 = self;
                            target1.set_attr("testsRun", inplace(NEOS_PY_ADD, target1.attr("testsRun"), I<1>()));
                        }
                        self.set_attr("_mirrorOutput", boolean(false));
                        self.method("_setupStdout");
                        return none();
                    }) },
                { "_setupStdout", w.method("TestResult", "_setupStdout", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("buffer").truthy())
                        {
                            if (self.attr("_stderr_buffer").is_none())
                            {
                                self.set_attr("_stderr_buffer", (Mod<"io">().attr("StringIO"))());
                                self.set_attr("_stdout_buffer", (Mod<"io">().attr("StringIO"))());
                            }
                            (Mod<"sys">()).set_attr("stdout", self.attr("_stdout_buffer"));
                            (Mod<"sys">()).set_attr("stderr", self.attr("_stderr_buffer"));
                        }
                        return none();
                    }) },
                { "startTestRun", w.method("TestResult", "startTestRun", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "stopTest", w.method("TestResult", "stopTest", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        self.method("_restoreStdout");
                        self.set_attr("_mirrorOutput", boolean(false));
                        return none();
                    }) },
                { "_restoreStdout", w.method("TestResult", "_restoreStdout", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py error_;
                        py output;
                        if (self.attr("buffer").truthy())
                        {
                            if (self.attr("_mirrorOutput").truthy())
                            {
                                output = (Mod<"sys">().attr("stdout")).method("getvalue");
                                error_ = (Mod<"sys">().attr("stderr")).method("getvalue");
                                if (output.truthy())
                                {
                                    if (!output.method("endswith", { S<"\n">() }).truthy())
                                    {
                                        output = inplace(NEOS_PY_ADD, output, S<"\n">());
                                    }
                                    self.attr("_original_stdout").method("write", { (G<"unittest.result.STDOUT_LINE">()) % output });
                                }
                                if (error_.truthy())
                                {
                                    if (!error_.method("endswith", { S<"\n">() }).truthy())
                                    {
                                        error_ = inplace(NEOS_PY_ADD, error_, S<"\n">());
                                    }
                                    self.attr("_original_stderr").method("write", { (G<"unittest.result.STDERR_LINE">()) % error_ });
                                }
                            }
                            (Mod<"sys">()).set_attr("stdout", self.attr("_original_stdout"));
                            (Mod<"sys">()).set_attr("stderr", self.attr("_original_stderr"));
                            self.attr("_stdout_buffer").method("seek", { I<0>() });
                            self.attr("_stdout_buffer").method("truncate");
                            self.attr("_stderr_buffer").method("seek", { I<0>() });
                            self.attr("_stderr_buffer").method("truncate");
                        }
                        return none();
                    }) },
                { "stopTestRun", w.method("TestResult", "stopTestRun", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "addError", (G<"unittest.result.failfast">())(w.method("TestResult", "addError", { py_positional("test"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py err = a[2];
                        self.attr("errors").method("append", { py_tuple({ test, self.method("_exc_info_to_string", { err, test }) }) });
                        self.set_attr("_mirrorOutput", boolean(true));
                        return none();
                    })) },
                { "addFailure", (G<"unittest.result.failfast">())(w.method("TestResult", "addFailure", { py_positional("test"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py err = a[2];
                        self.attr("failures").method("append", { py_tuple({ test, self.method("_exc_info_to_string", { err, test }) }) });
                        self.set_attr("_mirrorOutput", boolean(true));
                        return none();
                    })) },
                { "addSubTest", w.method("TestResult", "addSubTest", { py_positional("test"), py_positional("subtest"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py subtest = a[2];
                        py err = a[3];
                        py errors;
                        // By default, we don't do anything with successful subtests, but
                        // more sophisticated test results might want to record them.
                        if (!err.is_none())
                        {
                            if ((B<"getattr">())(self, S<"failfast">(), boolean(false)).truthy())
                            {
                                self.method("stop");
                            }
                            if (issubclass(err[I<0>()], test.attr("failureException")))
                            {
                                errors = self.attr("failures");
                            }
                            else
                            {
                                errors = self.attr("errors");
                            }
                            errors.method("append", { py_tuple({ subtest, self.method("_exc_info_to_string", { err, test }) }) });
                            self.set_attr("_mirrorOutput", boolean(true));
                        }
                        return none();
                    }) },
                { "addSuccess", w.method("TestResult", "addSuccess", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        return none();
                    }) },
                { "addSkip", w.method("TestResult", "addSkip", { py_positional("test"), py_positional("reason") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py reason = a[2];
                        self.attr("skipped").method("append", { py_tuple({ test, reason }) });
                        return none();
                    }) },
                { "addExpectedFailure", w.method("TestResult", "addExpectedFailure", { py_positional("test"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py err = a[2];
                        self.attr("expectedFailures").method("append", { py_tuple({ test, self.method("_exc_info_to_string", { err, test }) }) });
                        return none();
                    }) },
                { "addUnexpectedSuccess", (G<"unittest.result.failfast">())(w.method("TestResult", "addUnexpectedSuccess", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        self.attr("unexpectedSuccesses").method("append", { test });
                        return none();
                    })) },
                { "addDuration", w.method("TestResult", "addDuration", { py_positional("test"), py_positional("elapsed") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py elapsed = a[2];
                        // support for a TextTestRunner using an old TestResult class
                        if (has_attr(self, "collectedDurations"))
                        {
                            // Pass test repr and not the test object itself to avoid resources leak
                            self.attr("collectedDurations").method("append", { py_tuple({ (B<"str">())(test), elapsed }) });
                        }
                        return none();
                    }) },
                { "wasSuccessful", w.method("TestResult", "wasSuccessful", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        // The hasattr check is for test_result's OldResult test.  That
                        // way this method works on objects that lack the attribute.
                        // (where would such result instances come from? old stored pickles?)
                        return [&]() -> py { py const t = boolean(((py{ self.attr("failures").size() } == py{ self.attr("errors").size() }) && (py{ self.attr("errors").size() } == (I<0>())))); return !t.truthy() ? t : [&]() -> py { py const t = boolean(!has_attr(self, "unexpectedSuccesses")); return t.truthy() ? t : boolean(py{ self.attr("unexpectedSuccesses").size() } == (I<0>())); }(); }();
                    }) },
                { "stop", w.method("TestResult", "stop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("shouldStop", boolean(true));
                        return none();
                    }) },
                { "_exc_info_to_string", w.method("TestResult", "_exc_info_to_string", { py_positional("err"), py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py err = a[1];
                        py test = a[2];
                        py error_;
                        py exctype;
                        py msgLines;
                        py output;
                        py tb;
                        py tb_e;
                        py value;
                        auto const items2 = unpack(err, 3);
                        exctype = items2[0];
                        value = items2[1];
                        tb = items2[2];
                        tb = self.method("_clean_tracebacks", { exctype, value, tb, test });
                        tb_e = (Mod<"traceback">().attr("TracebackException")).call({ exctype, value, tb }, { { "capture_locals", self.attr("tb_locals") }, { "compact", boolean(true) } });
                        msgLines = (B<"list">())(tb_e.method("format"));
                        if (self.attr("buffer").truthy())
                        {
                            output = (Mod<"sys">().attr("stdout")).method("getvalue");
                            error_ = (Mod<"sys">().attr("stderr")).method("getvalue");
                            if (output.truthy())
                            {
                                if (!output.method("endswith", { S<"\n">() }).truthy())
                                {
                                    output = inplace(NEOS_PY_ADD, output, S<"\n">());
                                }
                                msgLines.method("append", { (G<"unittest.result.STDOUT_LINE">()) % output });
                            }
                            if (error_.truthy())
                            {
                                if (!error_.method("endswith", { S<"\n">() }).truthy())
                                {
                                    error_ = inplace(NEOS_PY_ADD, error_, S<"\n">());
                                }
                                msgLines.method("append", { (G<"unittest.result.STDERR_LINE">()) % error_ });
                            }
                        }
                        return (S<"">()).method("join", { msgLines });
                    }) },
                { "_clean_tracebacks", w.method("TestResult", "_clean_tracebacks", { py_positional("exctype"), py_positional("value"), py_positional("tb"), py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exctype = a[1];
                        py value = a[2];
                        py tb = a[3];
                        py test = a[4];
                        py c;
                        py excs;
                        py first;
                        py ret;
                        py seen;
                        ret = none();
                        first = boolean(true);
                        excs = py_list({ py_tuple({ exctype, value, tb }) });
                        seen = set_of({ (B<"id">())(value) });
                        while (excs.truthy())
                        {
                            auto const items4 = unpack(excs.method("pop"), 3);
                            exctype = items4[0];
                            value = items4[1];
                            tb = items4[2];
                            // Skip test runner traceback levels
                            while ((tb.truthy() && self.method("_is_relevant_tb_level", { tb }).truthy()))
                            {
                                tb = tb.attr("tb_next");
                            }
                            // Skip assert*() traceback levels
                            if (exctype.is(test.attr("failureException")))
                            {
                                self.method("_remove_unittest_tb_frames", { tb });
                            }
                            if (first.truthy())
                            {
                                ret = tb;
                                first = boolean(false);
                            }
                            else
                            {
                                value.set_attr("__traceback__", tb);
                            }
                            if (!value.is_none())
                            {
                                for (py const& item : iterate{ py_tuple({ value.attr("__cause__"), value.attr("__context__") }) })
                                {
                                    c = item;
                                    if (((!c.is_none()) && (!seen.contains((B<"id">())(c)))))
                                    {
                                        excs.method("append", { py_tuple({ (B<"type">())(c), c, c.attr("__traceback__") }) });
                                        seen.method("add", { (B<"id">())(c) });
                                    }
                                }
                            }
                        }
                        return ret;
                    }) },
                { "_is_relevant_tb_level", w.method("TestResult", "_is_relevant_tb_level", { py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tb = a[1];
                        return [&]() -> py { py const t = boolean(tb.attr("tb_frame").is_none()); return t.truthy() ? t : boolean(tb.attr("tb_frame").attr("f_globals").contains((S<"__unittest">()))); }();
                    }) },
                { "_remove_unittest_tb_frames", w.method("TestResult", "_remove_unittest_tb_frames", { py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tb = a[1];
                        py prev;
                        prev = none();
                        while ((tb.truthy() && (!self.method("_is_relevant_tb_level", { tb }).truthy())))
                        {
                            prev = tb;
                            tb = tb.attr("tb_next");
                        }
                        if (!prev.is_none())
                        {
                            prev.set_attr("tb_next", none());
                        }
                        return none();
                    }) },
                { "__repr__", w.method("TestResult", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s run=%i errors=%i failures=%i>">()) % py_tuple({ (Mod<"unittest.util">().attr("strclass"))(self.attr("__class__")), self.attr("testsRun"), py{ self.attr("errors").size() }, py{ self.attr("failures").size() } });
                    }) }, }));
        }

        bool unittest_result_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_result();
                }
                if (auto const doc = unittest_result_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("traceback", py{ fn_import("traceback") }.release());
                aAttributes.emplace_back("util", py{ fn_import("unittest.util") }.release());
                aAttributes.emplace_back("wraps", import_attribute("functools", "wraps").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.result", unittest_result_module);
    }
}
