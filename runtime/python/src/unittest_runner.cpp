/*
  unittest_runner.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/runner.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/runner.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.runner (CPython 3.13's Lib/unittest/runner.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_runner_docs.hpp"

namespace neos::python_runtime::detail::unittest_runner_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.runner.name"))
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
                py_raise("SystemError", std::string{ "unittest.runner: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.runner.name">()))
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
            static module_writer const sWriter{ "unittest.runner", unittest_runner_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.runner (Lib/unittest/runner.py)

        void make_unittest_runner()
        {
            static module_writer const& w = writer("unittest.runner");
            define("unittest.runner", "__unittest", boolean(true));
            define("unittest.runner", "_WritelnDecorator", w.make_class("_WritelnDecorator", { B<"object">() }, {
                { "__init__", w.method("_WritelnDecorator", "__init__", { py_positional("stream") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        self.set_attr("stream", stream);
                        return none();
                    }) },
                { "__getattr__", w.method("_WritelnDecorator", "__getattr__", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        if (py_tuple({ S<"stream">(), S<"__getstate__">() }).contains(attr))
                        {
                            py_raise((B<"AttributeError">())(attr));
                        }
                        return (B<"getattr">())(self.attr("stream"), attr);
                    }) },
                { "writeln", w.method("_WritelnDecorator", "writeln", { py_positional("arg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        if (arg.truthy())
                        {
                            self.method("write", { arg });
                        }
                        self.method("write", { S<"\n">() });
                        return none();
                    }) }, }));

            define("unittest.runner", "TextTestResult", w.make_class("TextTestResult", { Mod<"unittest.result">().attr("TestResult") }, {
                { "separator1", (S<"=">()) * (I<70>()) },
                { "separator2", (S<"-">()) * (I<70>()) },
                { "__init__", w.method("TextTestResult", "__init__", { py_positional("stream"), py_positional("descriptions"), py_positional("verbosity"), py_keyword("durations", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        py descriptions = a[2];
                        py verbosity = a[3];
                        py durations = a[4];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("__init__", { stream, descriptions, verbosity });
                        self.set_attr("stream", stream);
                        self.set_attr("showAll", boolean(verbosity > (I<1>())));
                        self.set_attr("dots", boolean(verbosity == (I<1>())));
                        self.set_attr("descriptions", descriptions);
                        self.set_attr("_newline", boolean(true));
                        self.set_attr("durations", durations);
                        return none();
                    }) },
                { "getDescription", w.method("TextTestResult", "getDescription", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py doc_first_line;
                        doc_first_line = test.method("shortDescription");
                        if ((self.attr("descriptions").truthy() && doc_first_line.truthy()))
                        {
                            return (S<"\n">()).method("join", { py_tuple({ (B<"str">())(test), doc_first_line }) });
                        }
                        else
                        {
                            return (B<"str">())(test);
                        }
                        return none();
                    }) },
                { "startTest", w.method("TextTestResult", "startTest", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("startTest", { test });
                        if (self.attr("showAll").truthy())
                        {
                            self.attr("stream").method("write", { self.method("getDescription", { test }) });
                            self.attr("stream").method("write", { S<" ... ">() });
                            self.attr("stream").method("flush");
                            self.set_attr("_newline", boolean(false));
                        }
                        return none();
                    }) },
                { "_write_status", w.method("TextTestResult", "_write_status", { py_positional("test"), py_positional("status") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py status = a[2];
                        py is_subtest;
                        is_subtest = boolean(isinstance(test, Mod<"unittest.case">().attr("_SubTest")));
                        if ((is_subtest.truthy() || self.attr("_newline").truthy()))
                        {
                            if (!self.attr("_newline").truthy())
                            {
                                self.attr("stream").method("writeln");
                            }
                            if (is_subtest.truthy())
                            {
                                self.attr("stream").method("write", { S<"  ">() });
                            }
                            self.attr("stream").method("write", { self.method("getDescription", { test }) });
                            self.attr("stream").method("write", { S<" ... ">() });
                        }
                        self.attr("stream").method("writeln", { status });
                        self.attr("stream").method("flush");
                        self.set_attr("_newline", boolean(true));
                        return none();
                    }) },
                { "addSubTest", w.method("TextTestResult", "addSubTest", { py_positional("test"), py_positional("subtest"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py subtest = a[2];
                        py err = a[3];
                        if (!err.is_none())
                        {
                            if (self.attr("showAll").truthy())
                            {
                                if (issubclass(err[I<0>()], subtest.attr("failureException")))
                                {
                                    self.method("_write_status", { subtest, S<"FAIL">() });
                                }
                                else
                                {
                                    self.method("_write_status", { subtest, S<"ERROR">() });
                                }
                            }
                            else if (self.attr("dots").truthy())
                            {
                                if (issubclass(err[I<0>()], subtest.attr("failureException")))
                                {
                                    self.attr("stream").method("write", { S<"F">() });
                                }
                                else
                                {
                                    self.attr("stream").method("write", { S<"E">() });
                                }
                                self.attr("stream").method("flush");
                            }
                        }
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("addSubTest", { test, subtest, err });
                        return none();
                    }) },
                { "addSuccess", w.method("TextTestResult", "addSuccess", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("addSuccess", { test });
                        if (self.attr("showAll").truthy())
                        {
                            self.method("_write_status", { test, S<"ok">() });
                        }
                        else if (self.attr("dots").truthy())
                        {
                            self.attr("stream").method("write", { S<".">() });
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) },
                { "addError", w.method("TextTestResult", "addError", { py_positional("test"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py err = a[2];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("addError", { test, err });
                        if (self.attr("showAll").truthy())
                        {
                            self.method("_write_status", { test, S<"ERROR">() });
                        }
                        else if (self.attr("dots").truthy())
                        {
                            self.attr("stream").method("write", { S<"E">() });
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) },
                { "addFailure", w.method("TextTestResult", "addFailure", { py_positional("test"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py err = a[2];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("addFailure", { test, err });
                        if (self.attr("showAll").truthy())
                        {
                            self.method("_write_status", { test, S<"FAIL">() });
                        }
                        else if (self.attr("dots").truthy())
                        {
                            self.attr("stream").method("write", { S<"F">() });
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) },
                { "addSkip", w.method("TextTestResult", "addSkip", { py_positional("test"), py_positional("reason") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py reason = a[2];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("addSkip", { test, reason });
                        if (self.attr("showAll").truthy())
                        {
                            self.method("_write_status", { test, (S<"skipped {0!r}">()).method("format", { reason }) });
                        }
                        else if (self.attr("dots").truthy())
                        {
                            self.attr("stream").method("write", { S<"s">() });
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) },
                { "addExpectedFailure", w.method("TextTestResult", "addExpectedFailure", { py_positional("test"), py_positional("err") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py err = a[2];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("addExpectedFailure", { test, err });
                        if (self.attr("showAll").truthy())
                        {
                            self.attr("stream").method("writeln", { S<"expected failure">() });
                            self.attr("stream").method("flush");
                        }
                        else if (self.attr("dots").truthy())
                        {
                            self.attr("stream").method("write", { S<"x">() });
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) },
                { "addUnexpectedSuccess", w.method("TextTestResult", "addUnexpectedSuccess", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        (B<"super">())(G<"unittest.runner.TextTestResult">(), self).method("addUnexpectedSuccess", { test });
                        if (self.attr("showAll").truthy())
                        {
                            self.attr("stream").method("writeln", { S<"unexpected success">() });
                            self.attr("stream").method("flush");
                        }
                        else if (self.attr("dots").truthy())
                        {
                            self.attr("stream").method("write", { S<"u">() });
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) },
                { "printErrors", w.method("TextTestResult", "printErrors", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test;
                        py unexpectedSuccesses;
                        if ((self.attr("dots").truthy() || self.attr("showAll").truthy()))
                        {
                            self.attr("stream").method("writeln");
                            self.attr("stream").method("flush");
                        }
                        self.method("printErrorList", { S<"ERROR">(), self.attr("errors") });
                        self.method("printErrorList", { S<"FAIL">(), self.attr("failures") });
                        unexpectedSuccesses = (B<"getattr">())(self, S<"unexpectedSuccesses">(), py_tuple());
                        if (unexpectedSuccesses.truthy())
                        {
                            self.attr("stream").method("writeln", { self.attr("separator1") });
                            for (py const& item : iterate{ unexpectedSuccesses })
                            {
                                test = item;
                                self.attr("stream").method("writeln", { S(std::string{ "UNEXPECTED SUCCESS: " } + self.method("getDescription", { test }).str()) });
                            }
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) },
                { "printErrorList", w.method("TextTestResult", "printErrorList", { py_positional("flavour"), py_positional("errors") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flavour = a[1];
                        py errors = a[2];
                        py err;
                        py test;
                        for (py const& item : iterate{ errors })
                        {
                            auto const items3 = unpack(item, 2);
                            test = items3[0];
                            err = items3[1];
                            self.attr("stream").method("writeln", { self.attr("separator1") });
                            self.attr("stream").method("writeln", { (S<"%s: %s">()) % py_tuple({ flavour, self.method("getDescription", { test }) }) });
                            self.attr("stream").method("writeln", { self.attr("separator2") });
                            self.attr("stream").method("writeln", { (S<"%s">()) % err });
                            self.attr("stream").method("flush");
                        }
                        return none();
                    }) }, }));

            define("unittest.runner", "TextTestRunner", w.make_class("TextTestRunner", { B<"object">() }, {
                { "resultclass", G<"unittest.runner.TextTestResult">() },
                { "__init__", w.method("TextTestRunner", "__init__", { py_positional("stream", none()), py_positional("descriptions", boolean(true)), py_positional("verbosity", I<1>()), py_positional("failfast", boolean(false)), py_positional("buffer", boolean(false)), py_positional("resultclass", none()), py_positional("warnings", none()), py_keyword("tb_locals", boolean(false)), py_keyword("durations", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        py descriptions = a[2];
                        py verbosity = a[3];
                        py failfast = a[4];
                        py buffer = a[5];
                        py resultclass = a[6];
                        py warnings = a[7];
                        py tb_locals = a[8];
                        py durations = a[9];
                        if (stream.is_none())
                        {
                            stream = Mod<"sys">().attr("stderr");
                        }
                        self.set_attr("stream", (G<"unittest.runner._WritelnDecorator">())(stream));
                        self.set_attr("descriptions", descriptions);
                        self.set_attr("verbosity", verbosity);
                        self.set_attr("failfast", failfast);
                        self.set_attr("buffer", buffer);
                        self.set_attr("tb_locals", tb_locals);
                        self.set_attr("durations", durations);
                        self.set_attr("warnings", warnings);
                        if (!resultclass.is_none())
                        {
                            self.set_attr("resultclass", resultclass);
                        }
                        return none();
                    }) },
                { "_makeResult", w.method("TextTestRunner", "_makeResult", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            return self.method("resultclass", { self.attr("stream"), self.attr("descriptions"), self.attr("verbosity") }, { { "durations", self.attr("durations") } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, B<"TypeError">()))
                            {
                                // didn't accept the durations argument
                                return self.method("resultclass", { self.attr("stream"), self.attr("descriptions"), self.attr("verbosity") });
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                        return none();
                    }) },
                { "_printDurations", w.method("TextTestRunner", "_printDurations", { py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py elapsed;
                        py hidden;
                        py ls;
                        py test;
                        if (!result.attr("collectedDurations").truthy())
                        {
                            return none();
                        }
                        ls = (B<"sorted">()).call({ result.attr("collectedDurations") }, { { "key", w.function("<lambda>", "TextTestRunner._printDurations.<locals>.<lambda>", { py_positional("x") }, [](arguments a) -> py
    {
        py x = a[0];
        return x[I<1>()];
    }) }, { "reverse", boolean(true) } });
                        if (self.attr("durations") > (I<0>()))
                        {
                            ls = ls[B<"slice">()(none(), self.attr("durations"), none())];
                        }
                        self.attr("stream").method("writeln", { S<"Slowest test durations">() });
                        if (has_attr(result, "separator2"))
                        {
                            self.attr("stream").method("writeln", { result.attr("separator2") });
                        }
                        hidden = boolean(false);
                        for (py const& item : iterate{ ls })
                        {
                            auto const items6 = unpack(item, 2);
                            test = items6[0];
                            elapsed = items6[1];
                            if (((self.attr("verbosity") < (I<2>())) && (elapsed < (F<0.001>()))))
                            {
                                hidden = boolean(true);
                                continue;
                            }
                            self.attr("stream").method("writeln", { (S<"%-10s %s">()) % py_tuple({ (S<"%.3fs">()) % elapsed, test }) });
                        }
                        if (hidden.truthy())
                        {
                            self.attr("stream").method("writeln", { S<"\n(durations < 0.001s were hidden; use -v to show these durations)">() });
                        }
                        else
                        {
                            self.attr("stream").method("writeln", { S<"">() });
                        }
                        return none();
                    }) },
                { "run", w.method("TextTestRunner", "run", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py errored;
                        py expectedFails;
                        py failed;
                        py infos;
                        py result;
                        py results;
                        py run;
                        py skipped;
                        py startTestRun;
                        py startTime;
                        py stopTestRun;
                        py stopTime;
                        py timeTaken;
                        py unexpectedSuccesses;
                        result = self.method("_makeResult");
                        (Mod<"unittest.signals">().attr("registerResult"))(result);
                        result.set_attr("failfast", self.attr("failfast"));
                        result.set_attr("buffer", self.attr("buffer"));
                        result.set_attr("tb_locals", self.attr("tb_locals"));
                        {
                            py const manager7 = (Mod<"warnings">().attr("catch_warnings"))();
                            py const exit7 = pyclass::special_method(manager7, "__exit__");
                            py const value7 = pyclass::special_method(manager7, "__enter__")(manager7);
                            [[maybe_unused]] int flow7 = 0;
                            [[maybe_unused]] py result7;
                            bool exited7 = false;
                            try
                            {
                                if (self.attr("warnings").truthy())
                                {
                                    // if self.warnings is set, use it to filter all the warnings
                                    (Mod<"warnings">().attr("simplefilter"))(self.attr("warnings"));
                                }
                                startTime = (Mod<"time">().attr("perf_counter"))();
                                startTestRun = (B<"getattr">())(result, S<"startTestRun">(), none());
                                if (!startTestRun.is_none())
                                {
                                    startTestRun();
                                }
                                {
                                    std::optional<py> pending8;
                                    [[maybe_unused]] int flow8 = 0;
                                    [[maybe_unused]] py result8;
                                    try
                                    {
                                        test(result);
                                    }
                                    catch (raised const&)
                                    {
                                        pending8 = py_take_exception();
                                    }
                                    finally8:
                                    stopTestRun = (B<"getattr">())(result, S<"stopTestRun">(), none());
                                    if (!stopTestRun.is_none())
                                    {
                                        stopTestRun();
                                    }
                                    if (pending8)
                                        py_reraise(*pending8);
                                    if (flow8 == 1)
                                        { result7 = result8; flow7 = 1; goto finally7; }
                                }
                                stopTime = (Mod<"time">().attr("perf_counter"))();
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler7;
                                exited7 = true;
                                if (!exit7(manager7, type_of(handler7.exception), handler7.exception, handler7.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler7.exception);
                            }
                            finally7:
                            if (!exited7)
                                exit7(manager7, none(), none(), none());
                            if (flow7 == 1)
                                return result7;
                        }
                        timeTaken = stopTime - startTime;
                        result.method("printErrors");
                        if (!self.attr("durations").is_none())
                        {
                            self.method("_printDurations", { result });
                        }
                        if (has_attr(result, "separator2"))
                        {
                            self.attr("stream").method("writeln", { result.attr("separator2") });
                        }
                        run = result.attr("testsRun");
                        self.attr("stream").method("writeln", { (S<"Ran %d test%s in %.3fs">()) % py_tuple({ run, [&]() -> py { py const t = [&]() -> py { py const t = boolean(run != (I<1>())); return !t.truthy() ? t : S<"s">(); }(); return t.truthy() ? t : S<"">(); }(), timeTaken }) });
                        self.attr("stream").method("writeln");
                        py const value9 = I<0>();
                        expectedFails = value9;
                        unexpectedSuccesses = value9;
                        skipped = value9;
                        {
                            bool otherwise10 = false;
                            try
                            {
                                results = (B<"map">())(B<"len">(), py_tuple({ result.attr("expectedFailures"), result.attr("unexpectedSuccesses"), result.attr("skipped") }));
                                otherwise10 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                if (matches(handler10.exception, B<"AttributeError">()))
                                {
                                }
                                else
                                    py_reraise(handler10.exception);
                            }
                            if (otherwise10)
                            {
                                auto const items11 = unpack(results, 3);
                                expectedFails = items11[0];
                                unexpectedSuccesses = items11[1];
                                skipped = items11[2];
                            }
                        }
                        infos = py_list();
                        if (!result.method("wasSuccessful").truthy())
                        {
                            self.attr("stream").method("write", { S<"FAILED">() });
                            auto const items12 = unpack(py_tuple({ py{ result.attr("failures").size() }, py{ result.attr("errors").size() } }), 2);
                            failed = items12[0];
                            errored = items12[1];
                            if (failed.truthy())
                            {
                                infos.method("append", { (S<"failures=%d">()) % failed });
                            }
                            if (errored.truthy())
                            {
                                infos.method("append", { (S<"errors=%d">()) % errored });
                            }
                        }
                        else if (((run == (I<0>())) && (!skipped.truthy())))
                        {
                            self.attr("stream").method("write", { S<"NO TESTS RAN">() });
                        }
                        else
                        {
                            self.attr("stream").method("write", { S<"OK">() });
                        }
                        if (skipped.truthy())
                        {
                            infos.method("append", { (S<"skipped=%d">()) % skipped });
                        }
                        if (expectedFails.truthy())
                        {
                            infos.method("append", { (S<"expected failures=%d">()) % expectedFails });
                        }
                        if (unexpectedSuccesses.truthy())
                        {
                            infos.method("append", { (S<"unexpected successes=%d">()) % unexpectedSuccesses });
                        }
                        if (infos.truthy())
                        {
                            self.attr("stream").method("writeln", { (S<" (%s)">()) % py_tuple({ (S<", ">()).method("join", { infos }) }) });
                        }
                        else
                        {
                            self.attr("stream").method("write", { S<"\n">() });
                        }
                        self.attr("stream").method("flush");
                        return result;
                    }) }, }));
        }

        bool unittest_runner_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_runner();
                }
                if (auto const doc = unittest_runner_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("result", py{ fn_import("unittest.result") }.release());
                aAttributes.emplace_back("_SubTest", import_attribute("unittest.case", "_SubTest").release());
                aAttributes.emplace_back("registerResult", import_attribute("unittest.signals", "registerResult").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.runner", unittest_runner_module);
    }
}
