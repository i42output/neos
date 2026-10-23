/*
  unittest_suite.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/suite.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/suite.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.suite (CPython 3.13's Lib/unittest/suite.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_suite_docs.hpp"

namespace neos::python_runtime::detail::unittest_suite_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.suite.name"))
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
                py_raise("SystemError", std::string{ "unittest.suite: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.suite.name">()))
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
            static module_writer const sWriter{ "unittest.suite", unittest_suite_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.suite (Lib/unittest/suite.py)

        void make_unittest_suite()
        {
            static module_writer const& w = writer("unittest.suite");
            define("unittest.suite", "__unittest", boolean(true));
            define("unittest.suite", "_call_if_exists", w.function("_call_if_exists", "_call_if_exists", { py_positional("parent"), py_positional("attr") }, [](arguments a) -> py
                {
                    py parent = a[0];
                    py attr = a[1];
                    py func;
                    func = (B<"getattr">())(parent, attr, w.function("<lambda>", "_call_if_exists.<locals>.<lambda>", {  }, [](arguments a) -> py
    {
        return none();
    }));
                    func();
                    return none();
                }));

            define("unittest.suite", "BaseTestSuite", w.make_class("BaseTestSuite", { B<"object">() }, {
                { "_cleanup", boolean(true) },
                { "__init__", w.method("BaseTestSuite", "__init__", { py_positional("tests", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tests = a[1];
                        self.set_attr("_tests", py_list());
                        self.set_attr("_removed_tests", I<0>());
                        self.method("addTests", { tests });
                        return none();
                    }) },
                { "__repr__", w.method("BaseTestSuite", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s tests=%s>">()) % py_tuple({ (Mod<"unittest.util">().attr("strclass"))(self.attr("__class__")), (B<"list">())(self) });
                    }) },
                { "__eq__", w.method("BaseTestSuite", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, self.attr("__class__")))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean((B<"list">())(self) == (B<"list">())(other));
                    }) },
                { "__iter__", w.method("BaseTestSuite", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"iter">())(self.attr("_tests"));
                    }) },
                { "countTestCases", w.method("BaseTestSuite", "countTestCases", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cases;
                        py test;
                        cases = self.attr("_removed_tests");
                        for (py const& item : iterate{ self })
                        {
                            test = item;
                            if (test.truthy())
                            {
                                cases = inplace(NEOS_PY_ADD, cases, test.method("countTestCases"));
                            }
                        }
                        return cases;
                    }) },
                { "addTest", w.method("BaseTestSuite", "addTest", { py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        if (!callable(test))
                        {
                            py_raise((B<"TypeError">())((S<"{} is not callable">()).method("format", { S(test.repr()) })));
                        }
                        if ((isinstance(test, B<"type">()) && issubclass(test, py_tuple({ Mod<"unittest.case">().attr("TestCase"), G<"unittest.suite.TestSuite">() }))))
                        {
                            py_raise((B<"TypeError">())(S<"TestCases and TestSuites must be instantiated before passing them to addTest()">()));
                        }
                        self.attr("_tests").method("append", { test });
                        return none();
                    }) },
                { "addTests", w.method("BaseTestSuite", "addTests", { py_positional("tests") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tests = a[1];
                        py test;
                        if (isinstance(tests, B<"str">()))
                        {
                            py_raise((B<"TypeError">())(S<"tests must be an iterable of tests, not a string">()));
                        }
                        for (py const& item : iterate{ tests })
                        {
                            test = item;
                            self.method("addTest", { test });
                        }
                        return none();
                    }) },
                { "run", w.method("BaseTestSuite", "run", { py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py index;
                        py test;
                        for (py const& item : iterate{ (B<"enumerate">())(self) })
                        {
                            auto const items4 = unpack(item, 2);
                            index = items4[0];
                            test = items4[1];
                            if (result.attr("shouldStop").truthy())
                            {
                                break;
                            }
                            test(result);
                            if (self.attr("_cleanup").truthy())
                            {
                                self.method("_removeTestAtIndex", { index });
                            }
                        }
                        return result;
                    }) },
                { "_removeTestAtIndex", w.method("BaseTestSuite", "_removeTestAtIndex", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        py test;
                        {
                            bool otherwise5 = false;
                            try
                            {
                                test = self.attr("_tests")[index];
                                otherwise5 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler5;
                                if (matches(handler5.exception, B<"TypeError">()))
                                {
                                    // support for suite implementations that have overridden self._tests
                                }
                                else
                                    py_reraise(handler5.exception);
                            }
                            if (otherwise5)
                            {
                                // Some unittest tests add non TestCase/TestSuite objects to
                                // the suite.
                                if (has_attr(test, "countTestCases"))
                                {
                                    {
                                        py const target6 = self;
                                        target6.set_attr("_removed_tests", inplace(NEOS_PY_ADD, target6.attr("_removed_tests"), test.method("countTestCases")));
                                    }
                                }
                                self.attr("_tests").set_item(index, none());
                            }
                        }
                        return none();
                    }) },
                { "__call__", w.method("BaseTestSuite", "__call__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwds = a[2];
                        return call_ex(self.attr("run"), {  }, args, kwds);
                    }, true, true) },
                { "debug", w.method("BaseTestSuite", "debug", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test;
                        for (py const& item : iterate{ self })
                        {
                            test = item;
                            test.method("debug");
                        }
                        return none();
                    }) }, }));

                // ###############################
            define("unittest.suite", "TestSuite", w.make_class("TestSuite", { G<"unittest.suite.BaseTestSuite">() }, {
                { "run", w.method("TestSuite", "run", { py_positional("result"), py_positional("debug", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py debug = a[2];
                        py index;
                        py test;
                        py topLevel;
                        topLevel = boolean(false);
                        if ((B<"getattr">())(result, S<"_testRunEntered">(), boolean(false)).is(boolean(false)))
                        {
                            py const value8 = boolean(true);
                            result.set_attr("_testRunEntered", value8);
                            topLevel = value8;
                        }
                        for (py const& item : iterate{ (B<"enumerate">())(self) })
                        {
                            auto const items10 = unpack(item, 2);
                            index = items10[0];
                            test = items10[1];
                            if (result.attr("shouldStop").truthy())
                            {
                                break;
                            }
                            if ((G<"unittest.suite._isnotsuite">())(test).truthy())
                            {
                                self.method("_tearDownPreviousClass", { test, result });
                                self.method("_handleModuleFixture", { test, result });
                                self.method("_handleClassSetUp", { test, result });
                                result.set_attr("_previousTestClass", test.attr("__class__"));
                                if (((B<"getattr">())(test.attr("__class__"), S<"_classSetupFailed">(), boolean(false)).truthy() || (B<"getattr">())(result, S<"_moduleSetUpFailed">(), boolean(false)).truthy()))
                                {
                                    continue;
                                }
                            }
                            if (!debug.truthy())
                            {
                                test(result);
                            }
                            else
                            {
                                test.method("debug");
                            }
                            if (self.attr("_cleanup").truthy())
                            {
                                self.method("_removeTestAtIndex", { index });
                            }
                        }
                        if (topLevel.truthy())
                        {
                            self.method("_tearDownPreviousClass", { none(), result });
                            self.method("_handleModuleTearDown", { result });
                            result.set_attr("_testRunEntered", boolean(false));
                        }
                        return result;
                    }) },
                { "debug", w.method("TestSuite", "debug", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py debug;
                        debug = (G<"unittest.suite._DebugResult">())();
                        self.method("run", { debug, boolean(true) });
                        return none();
                    }) },
                { "_handleClassSetUp", w.method("TestSuite", "_handleClassSetUp", { py_positional("test"), py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py result = a[2];
                        py className;
                        py currentClass;
                        py doClassCleanups;
                        py e;
                        py exc_info;
                        py failed;
                        py previousClass;
                        py setUpClass;
                        previousClass = (B<"getattr">())(result, S<"_previousTestClass">(), none());
                        currentClass = test.attr("__class__");
                        if (currentClass == previousClass)
                        {
                            return none();
                        }
                        if (result.attr("_moduleSetUpFailed").truthy())
                        {
                            return none();
                        }
                        if ((B<"getattr">())(currentClass, S<"__unittest_skip__">(), boolean(false)).truthy())
                        {
                            return none();
                        }
                        failed = boolean(false);
                        try
                        {
                            currentClass.set_attr("_classSetupFailed", boolean(false));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            if (matches(handler11.exception, B<"TypeError">()))
                            {
                                // test may actually be a function
                                // so its class will be a builtin-type
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        setUpClass = (B<"getattr">())(currentClass, S<"setUpClass">(), none());
                        doClassCleanups = (B<"getattr">())(currentClass, S<"doClassCleanups">(), none());
                        if (!setUpClass.is_none())
                        {
                            (G<"unittest.suite._call_if_exists">())(result, S<"_setupStdout">());
                            {
                                std::optional<py> pending12;
                                [[maybe_unused]] int flow12 = 0;
                                [[maybe_unused]] py result12;
                                try
                                {
                                    try
                                    {
                                        setUpClass();
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler13;
                                        if (matches(handler13.exception, B<"Exception">()))
                                        {
                                            e = handler13.exception;
                                            if (isinstance(result, G<"unittest.suite._DebugResult">()))
                                            {
                                                py_reraise(handler13.exception);
                                            }
                                            failed = boolean(true);
                                            try
                                            {
                                                currentClass.set_attr("_classSetupFailed", boolean(true));
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler14;
                                                if (matches(handler14.exception, B<"TypeError">()))
                                                {
                                                }
                                                else
                                                    py_reraise(handler14.exception);
                                            }
                                            className = (Mod<"unittest.util">().attr("strclass"))(currentClass);
                                            self.method("_createClassOrModuleLevelException", { result, e, S<"setUpClass">(), className });
                                        }
                                        else
                                            py_reraise(handler13.exception);
                                    }
                                    if ((failed.truthy() && (!doClassCleanups.is_none())))
                                    {
                                        doClassCleanups();
                                        for (py const& item : iterate{ currentClass.attr("tearDown_exceptions") })
                                        {
                                            exc_info = item;
                                            self.method("_createClassOrModuleLevelException", { result, exc_info[I<1>()], S<"setUpClass">(), className }, { { "info", exc_info } });
                                        }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending12 = py_take_exception();
                                }
                                finally12:
                                (G<"unittest.suite._call_if_exists">())(result, S<"_restoreStdout">());
                                if (pending12)
                                    py_reraise(*pending12);
                                if (flow12 == 1)
                                    return result12;
                            }
                        }
                        return none();
                    }) },
                { "_get_previous_module", w.method("TestSuite", "_get_previous_module", { py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py previousClass;
                        py previousModule;
                        previousModule = none();
                        previousClass = (B<"getattr">())(result, S<"_previousTestClass">(), none());
                        if (!previousClass.is_none())
                        {
                            previousModule = previousClass.attr("__module__");
                        }
                        return previousModule;
                    }) },
                { "_handleModuleFixture", w.method("TestSuite", "_handleModuleFixture", { py_positional("test"), py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py result = a[2];
                        py currentModule;
                        py e;
                        py module;
                        py previousModule;
                        py setUpModule;
                        previousModule = self.method("_get_previous_module", { result });
                        currentModule = test.attr("__class__").attr("__module__");
                        if (currentModule == previousModule)
                        {
                            return none();
                        }
                        self.method("_handleModuleTearDown", { result });
                        result.set_attr("_moduleSetUpFailed", boolean(false));
                        try
                        {
                            module = (Mod<"sys">().attr("modules"))[currentModule];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler16;
                            if (matches(handler16.exception, B<"KeyError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler16.exception);
                        }
                        setUpModule = (B<"getattr">())(module, S<"setUpModule">(), none());
                        if (!setUpModule.is_none())
                        {
                            (G<"unittest.suite._call_if_exists">())(result, S<"_setupStdout">());
                            {
                                std::optional<py> pending17;
                                [[maybe_unused]] int flow17 = 0;
                                [[maybe_unused]] py result17;
                                try
                                {
                                    try
                                    {
                                        setUpModule();
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler18;
                                        if (matches(handler18.exception, B<"Exception">()))
                                        {
                                            e = handler18.exception;
                                            if (isinstance(result, G<"unittest.suite._DebugResult">()))
                                            {
                                                py_reraise(handler18.exception);
                                            }
                                            result.set_attr("_moduleSetUpFailed", boolean(true));
                                            self.method("_createClassOrModuleLevelException", { result, e, S<"setUpModule">(), currentModule });
                                        }
                                        else
                                            py_reraise(handler18.exception);
                                    }
                                    if (result.attr("_moduleSetUpFailed").truthy())
                                    {
                                        try
                                        {
                                            (Mod<"unittest.case">().attr("doModuleCleanups"))();
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler19;
                                            if (matches(handler19.exception, B<"Exception">()))
                                            {
                                                e = handler19.exception;
                                                self.method("_createClassOrModuleLevelException", { result, e, S<"setUpModule">(), currentModule });
                                            }
                                            else
                                                py_reraise(handler19.exception);
                                        }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending17 = py_take_exception();
                                }
                                finally17:
                                (G<"unittest.suite._call_if_exists">())(result, S<"_restoreStdout">());
                                if (pending17)
                                    py_reraise(*pending17);
                                if (flow17 == 1)
                                    return result17;
                            }
                        }
                        return none();
                    }) },
                { "_createClassOrModuleLevelException", w.method("TestSuite", "_createClassOrModuleLevelException", { py_positional("result"), py_positional("exc"), py_positional("method_name"), py_positional("parent"), py_positional("info", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py exc = a[2];
                        py method_name = a[3];
                        py parent = a[4];
                        py info = a[5];
                        py errorName;
                        errorName = S(method_name.str() + " (" + parent.str() + ")");
                        self.method("_addClassOrModuleLevelException", { result, exc, errorName, info });
                        return none();
                    }) },
                { "_addClassOrModuleLevelException", w.method("TestSuite", "_addClassOrModuleLevelException", { py_positional("result"), py_positional("exception"), py_positional("errorName"), py_positional("info", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py exception = a[2];
                        py errorName = a[3];
                        py info = a[4];
                        py addSkip;
                        py error_;
                        error_ = (G<"unittest.suite._ErrorHolder">())(errorName);
                        addSkip = (B<"getattr">())(result, S<"addSkip">(), none());
                        if (((!addSkip.is_none()) && isinstance(exception, Mod<"unittest.case">().attr("SkipTest"))))
                        {
                            addSkip(error_, (B<"str">())(exception));
                        }
                        else if (!info.truthy())
                        {
                            result.method("addError", { error_, (Mod<"sys">().attr("exc_info"))() });
                        }
                        else
                        {
                            result.method("addError", { error_, info });
                        }
                        return none();
                    }) },
                { "_handleModuleTearDown", w.method("TestSuite", "_handleModuleTearDown", { py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py e;
                        py module;
                        py previousModule;
                        py tearDownModule;
                        previousModule = self.method("_get_previous_module", { result });
                        if (previousModule.is_none())
                        {
                            return none();
                        }
                        if (result.attr("_moduleSetUpFailed").truthy())
                        {
                            return none();
                        }
                        try
                        {
                            module = (Mod<"sys">().attr("modules"))[previousModule];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler20;
                            if (matches(handler20.exception, B<"KeyError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler20.exception);
                        }
                        (G<"unittest.suite._call_if_exists">())(result, S<"_setupStdout">());
                        {
                            std::optional<py> pending21;
                            [[maybe_unused]] int flow21 = 0;
                            [[maybe_unused]] py result21;
                            try
                            {
                                tearDownModule = (B<"getattr">())(module, S<"tearDownModule">(), none());
                                if (!tearDownModule.is_none())
                                {
                                    try
                                    {
                                        tearDownModule();
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler22;
                                        if (matches(handler22.exception, B<"Exception">()))
                                        {
                                            e = handler22.exception;
                                            if (isinstance(result, G<"unittest.suite._DebugResult">()))
                                            {
                                                py_reraise(handler22.exception);
                                            }
                                            self.method("_createClassOrModuleLevelException", { result, e, S<"tearDownModule">(), previousModule });
                                        }
                                        else
                                            py_reraise(handler22.exception);
                                    }
                                }
                                try
                                {
                                    (Mod<"unittest.case">().attr("doModuleCleanups"))();
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler23;
                                    if (matches(handler23.exception, B<"Exception">()))
                                    {
                                        e = handler23.exception;
                                        if (isinstance(result, G<"unittest.suite._DebugResult">()))
                                        {
                                            py_reraise(handler23.exception);
                                        }
                                        self.method("_createClassOrModuleLevelException", { result, e, S<"tearDownModule">(), previousModule });
                                    }
                                    else
                                        py_reraise(handler23.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending21 = py_take_exception();
                            }
                            finally21:
                            (G<"unittest.suite._call_if_exists">())(result, S<"_restoreStdout">());
                            if (pending21)
                                py_reraise(*pending21);
                            if (flow21 == 1)
                                return result21;
                        }
                        return none();
                    }) },
                { "_tearDownPreviousClass", w.method("TestSuite", "_tearDownPreviousClass", { py_positional("test"), py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test = a[1];
                        py result = a[2];
                        py className;
                        py currentClass;
                        py doClassCleanups;
                        py e;
                        py exc_info;
                        py previousClass;
                        py tearDownClass;
                        previousClass = (B<"getattr">())(result, S<"_previousTestClass">(), none());
                        currentClass = test.attr("__class__");
                        if (((currentClass == previousClass) || previousClass.is_none()))
                        {
                            return none();
                        }
                        if ((B<"getattr">())(previousClass, S<"_classSetupFailed">(), boolean(false)).truthy())
                        {
                            return none();
                        }
                        if ((B<"getattr">())(result, S<"_moduleSetUpFailed">(), boolean(false)).truthy())
                        {
                            return none();
                        }
                        if ((B<"getattr">())(previousClass, S<"__unittest_skip__">(), boolean(false)).truthy())
                        {
                            return none();
                        }
                        tearDownClass = (B<"getattr">())(previousClass, S<"tearDownClass">(), none());
                        doClassCleanups = (B<"getattr">())(previousClass, S<"doClassCleanups">(), none());
                        if ((tearDownClass.is_none() && doClassCleanups.is_none()))
                        {
                            return none();
                        }
                        (G<"unittest.suite._call_if_exists">())(result, S<"_setupStdout">());
                        {
                            std::optional<py> pending24;
                            [[maybe_unused]] int flow24 = 0;
                            [[maybe_unused]] py result24;
                            try
                            {
                                if (!tearDownClass.is_none())
                                {
                                    try
                                    {
                                        tearDownClass();
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler25;
                                        if (matches(handler25.exception, B<"Exception">()))
                                        {
                                            e = handler25.exception;
                                            if (isinstance(result, G<"unittest.suite._DebugResult">()))
                                            {
                                                py_reraise(handler25.exception);
                                            }
                                            className = (Mod<"unittest.util">().attr("strclass"))(previousClass);
                                            self.method("_createClassOrModuleLevelException", { result, e, S<"tearDownClass">(), className });
                                        }
                                        else
                                            py_reraise(handler25.exception);
                                    }
                                }
                                if (!doClassCleanups.is_none())
                                {
                                    doClassCleanups();
                                    for (py const& item : iterate{ previousClass.attr("tearDown_exceptions") })
                                    {
                                        exc_info = item;
                                        if (isinstance(result, G<"unittest.suite._DebugResult">()))
                                        {
                                            py_raise(exc_info[I<1>()]);
                                        }
                                        className = (Mod<"unittest.util">().attr("strclass"))(previousClass);
                                        self.method("_createClassOrModuleLevelException", { result, exc_info[I<1>()], S<"tearDownClass">(), className }, { { "info", exc_info } });
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending24 = py_take_exception();
                            }
                            finally24:
                            (G<"unittest.suite._call_if_exists">())(result, S<"_restoreStdout">());
                            if (pending24)
                                py_reraise(*pending24);
                            if (flow24 == 1)
                                return result24;
                        }
                        return none();
                    }) }, }));

                // Inspired by the ErrorHolder from Twisted:
                // http://twistedmatrix.com/trac/browser/trunk/twisted/trial/runner.py
                // attribute used by TestResult._exc_info_to_string
            define("unittest.suite", "_ErrorHolder", w.make_class("_ErrorHolder", { B<"object">() }, {
                { "failureException", none() },
                { "__init__", w.method("_ErrorHolder", "__init__", { py_positional("description") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py description = a[1];
                        self.set_attr("description", description);
                        return none();
                    }) },
                { "id", w.method("_ErrorHolder", "id", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("description");
                    }) },
                { "shortDescription", w.method("_ErrorHolder", "shortDescription", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "__repr__", w.method("_ErrorHolder", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<ErrorHolder description=%r>">()) % py_tuple({ self.attr("description") });
                    }) },
                { "__str__", w.method("_ErrorHolder", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("id");
                    }) },
                { "run", w.method("_ErrorHolder", "run", { py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        return none();
                    }) },
                { "__call__", w.method("_ErrorHolder", "__call__", { py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        return self.method("run", { result });
                    }) },
                { "countTestCases", w.method("_ErrorHolder", "countTestCases", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return I<0>();
                    }) }, }));

            define("unittest.suite", "_isnotsuite", w.function("_isnotsuite", "_isnotsuite", { py_positional("test") }, [](arguments a) -> py
                {
                    py test = a[0];
                    try
                    {
                        (B<"iter">())(test);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler27;
                        if (matches(handler27.exception, B<"TypeError">()))
                        {
                            return boolean(true);
                        }
                        else
                            py_reraise(handler27.exception);
                    }
                    return boolean(false);
                }));

            define("unittest.suite", "_DebugResult", w.make_class("_DebugResult", { B<"object">() }, {
                { "_previousTestClass", none() },
                { "_moduleSetUpFailed", boolean(false) },
                { "shouldStop", boolean(false) }, }));
        }

        bool unittest_suite_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_suite();
                }
                if (auto const doc = unittest_suite_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("case", py{ fn_import("unittest.case") }.release());
                aAttributes.emplace_back("util", py{ fn_import("unittest.util") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.suite", unittest_suite_module);
    }
}
