/*
  unittest_loader.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/loader.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/loader.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.loader (CPython 3.13's Lib/unittest/loader.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_loader_docs.hpp"

namespace neos::python_runtime::detail::unittest_loader_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.loader.name"))
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
                py_raise("SystemError", std::string{ "unittest.loader: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.loader.name">()))
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
            static module_writer const sWriter{ "unittest.loader", unittest_loader_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.loader (Lib/unittest/loader.py)

        void make_unittest_loader()
        {
            static module_writer const& w = writer("unittest.loader");
            define("unittest.loader", "__unittest", boolean(true));
            // what about .pyc (etc)
            // we would need to avoid loading the same tests multiple times
            // from '.py', *and* '.pyc'
            define("unittest.loader", "VALID_MODULE_NAME", (Mod<"re">()).method("compile", { S<"[_a-z]\\w*\\.py$">(), Mod<"re">().attr("IGNORECASE") }));
            define("unittest.loader", "_FailedTest", w.make_class("_FailedTest", { Mod<"unittest.case">().attr("TestCase") }, {
                { "_testMethodName", none() },
                { "__init__", w.method("_FailedTest", "__init__", { py_positional("method_name"), py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method_name = a[1];
                        py exception = a[2];
                        self.set_attr("_exception", exception);
                        (B<"super">())(G<"unittest.loader._FailedTest">(), self).method("__init__", { method_name });
                        return none();
                    }) },
                { "__getattr__", w.method("_FailedTest", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py name = a[1];
                        py testFailure;
                        if (name != (*self).attr("_testMethodName"))
                        {
                            return (B<"super">())(G<"unittest.loader._FailedTest">(), (*self)).method("__getattr__", { name });
                        }
                        testFailure = w.function("testFailure", "_FailedTest.__getattr__.<locals>.testFailure", {  }, [self](arguments a) -> py
    {
        py_raise((*self).attr("_exception"));
    });
                        return testFailure;
                    }) }, }));

            define("unittest.loader", "_make_failed_import_test", w.function("_make_failed_import_test", "_make_failed_import_test", { py_positional("name"), py_positional("suiteClass") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py suiteClass = a[1];
                    py message;
                    message = (S<"Failed to import test module: %s\n%s">()) % py_tuple({ name, (Mod<"traceback">().attr("format_exc"))() });
                    return (G<"unittest.loader._make_failed_test">())(name, (B<"ImportError">())(message), suiteClass, message);
                }));

            define("unittest.loader", "_make_failed_load_tests", w.function("_make_failed_load_tests", "_make_failed_load_tests", { py_positional("name"), py_positional("exception"), py_positional("suiteClass") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py exception = a[1];
                    py suiteClass = a[2];
                    py message;
                    message = (S<"Failed to call load_tests:\n%s">()) % py_tuple({ (Mod<"traceback">().attr("format_exc"))() });
                    return (G<"unittest.loader._make_failed_test">())(name, exception, suiteClass, message);
                }));

            define("unittest.loader", "_make_failed_test", w.function("_make_failed_test", "_make_failed_test", { py_positional("methodname"), py_positional("exception"), py_positional("suiteClass"), py_positional("message") }, [](arguments a) -> py
                {
                    py methodname = a[0];
                    py exception = a[1];
                    py suiteClass = a[2];
                    py message = a[3];
                    py test;
                    test = (G<"unittest.loader._FailedTest">())(methodname, exception);
                    return py_tuple({ suiteClass(py_tuple({ test })), message });
                }));

            define("unittest.loader", "_make_skipped_test", w.function("_make_skipped_test", "_make_skipped_test", { py_positional("methodname"), py_positional("exception"), py_positional("suiteClass") }, [](arguments a) -> py
                {
                    py methodname = a[0];
                    auto const exception = std::make_shared<py>(a[1]);
                    py suiteClass = a[2];
                    py TestClass;
                    py attrs;
                    py testSkipped;
                    testSkipped = (Mod<"unittest.case">().attr("skip"))((B<"str">())((*exception)))(w.function("testSkipped", "_make_skipped_test.<locals>.testSkipped", { py_positional("self") }, [exception](arguments a) -> py
    {
        py self = a[0];
        return none();
    }));
                    attrs = dict_of({ { methodname, testSkipped } });
                    TestClass = (B<"type">())(S<"ModuleSkipped">(), py_tuple({ Mod<"unittest.case">().attr("TestCase") }), attrs);
                    return suiteClass(py_tuple({ TestClass(methodname) }));
                }));

            define("unittest.loader", "_splitext", w.function("_splitext", "_splitext", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    return (Mod<"os">().attr("path")).method("splitext", { path })[I<0>()];
                }));

            define("unittest.loader", "TestLoader", w.make_class("TestLoader", { B<"object">() }, {
                { "testMethodPrefix", S<"test">() },
                { "sortTestMethodsUsing", (B<"staticmethod">())(Mod<"unittest.util">().attr("three_way_cmp")) },
                { "testNamePatterns", none() },
                { "suiteClass", Mod<"unittest.suite">().attr("TestSuite") },
                { "_top_level_dir", none() },
                { "__init__", w.method("TestLoader", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"super">())(G<"unittest.loader.TestLoader">(), self).method("__init__");
                        self.set_attr("errors", py_list());
                        // Tracks packages which we have called into via load_tests, to
                        // avoid infinite re-entrancy.
                        self.set_attr("_loading_packages", (B<"set">())());
                        return none();
                    }) },
                { "loadTestsFromTestCase", w.method("TestLoader", "loadTestsFromTestCase", { py_positional("testCaseClass") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py testCaseClass = a[1];
                        py loaded_suite;
                        py testCaseNames;
                        if (issubclass(testCaseClass, Mod<"unittest.suite">().attr("TestSuite")))
                        {
                            py_raise((B<"TypeError">())(S<"Test cases should not be derived from TestSuite. Maybe you meant to derive from TestCase?">()));
                        }
                        if ((py_tuple({ Mod<"unittest.case">().attr("TestCase"), Mod<"unittest.case">().attr("FunctionTestCase") }).contains(testCaseClass) || (Mod<"inspect">().attr("isabstract"))(testCaseClass).truthy()))
                        {
                            // We don't load any tests from base types that should not be loaded,
                            // and abstract base classes that can't be instantiated
                            testCaseNames = py_list();
                        }
                        else
                        {
                            testCaseNames = self.method("getTestCaseNames", { testCaseClass });
                            if (((!testCaseNames.truthy()) && has_attr(testCaseClass, "runTest")))
                            {
                                testCaseNames = py_list({ S<"runTest">() });
                            }
                        }
                        loaded_suite = self.method("suiteClass", { (B<"map">())(testCaseClass, testCaseNames) });
                        return loaded_suite;
                    }) },
                { "loadTestsFromModule", w.method("TestLoader", "loadTestsFromModule", { py_positional("module"), py_keyword("pattern", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py module = a[1];
                        py pattern = a[2];
                        py e;
                        py error_case;
                        py error_message;
                        py load_tests;
                        py name;
                        py obj;
                        py tests;
                        tests = py_list();
                        for (py const& item : iterate{ (B<"dir">())(module) })
                        {
                            name = item;
                            obj = (B<"getattr">())(module, name);
                            if ((isinstance(obj, B<"type">()) && issubclass(obj, Mod<"unittest.case">().attr("TestCase")) && (!py_tuple({ Mod<"unittest.case">().attr("TestCase"), Mod<"unittest.case">().attr("FunctionTestCase") }).contains(obj)) && (!(Mod<"inspect">().attr("isabstract"))(obj).truthy())))
                            {
                                tests.method("append", { self.method("loadTestsFromTestCase", { obj }) });
                            }
                        }
                        load_tests = (B<"getattr">())(module, S<"load_tests">(), none());
                        tests = self.method("suiteClass", { tests });
                        if (!load_tests.is_none())
                        {
                            try
                            {
                                return load_tests(self, tests, pattern);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, B<"Exception">()))
                                {
                                    e = handler2.exception;
                                    auto const items3 = unpack((G<"unittest.loader._make_failed_load_tests">())(module.attr("__name__"), e, self.attr("suiteClass")), 2);
                                    error_case = items3[0];
                                    error_message = items3[1];
                                    self.attr("errors").method("append", { error_message });
                                    return error_case;
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                        }
                        return tests;
                    }) },
                { "loadTestsFromName", w.method("TestLoader", "loadTestsFromName", { py_positional("name"), py_positional("module", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py module = a[2];
                        py e;
                        py error_case;
                        py error_message;
                        py inst;
                        py module_name;
                        py next_attribute;
                        py obj;
                        py parent;
                        py part;
                        py parts;
                        py parts_copy;
                        py test;
                        parts = name.method("split", { S<".">() });
                        auto const items4 = unpack(py_tuple({ none(), none() }), 2);
                        error_case = items4[0];
                        error_message = items4[1];
                        if (module.is_none())
                        {
                            parts_copy = parts[B<"slice">()(none(), none(), none())];
                            while (parts_copy.truthy())
                            {
                                try
                                {
                                    module_name = (S<".">()).method("join", { parts_copy });
                                    module = (B<"__import__">())(module_name);
                                    break;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler6;
                                    if (matches(handler6.exception, B<"ImportError">()))
                                    {
                                        next_attribute = parts_copy.method("pop");
                                        // Last error so we can give it to the user if needed.
                                        auto const items7 = unpack((G<"unittest.loader._make_failed_import_test">())(next_attribute, self.attr("suiteClass")), 2);
                                        error_case = items7[0];
                                        error_message = items7[1];
                                        if (!parts_copy.truthy())
                                        {
                                            // Even the top level import failed: report that error.
                                            self.attr("errors").method("append", { error_message });
                                            return error_case;
                                        }
                                    }
                                    else
                                        py_reraise(handler6.exception);
                                }
                            }
                            parts = parts[B<"slice">()(I<1>(), none(), none())];
                        }
                        obj = module;
                        for (py const& item : iterate{ parts })
                        {
                            part = item;
                            try
                            {
                                auto const items10 = unpack(py_tuple({ obj, (B<"getattr">())(obj, part) }), 2);
                                parent = items10[0];
                                obj = items10[1];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler9;
                                if (matches(handler9.exception, B<"AttributeError">()))
                                {
                                    e = handler9.exception;
                                    // We can't traverse some part of the name.
                                    if (((!(B<"getattr">())(obj, S<"__path__">(), none()).is_none()) && (!error_case.is_none())))
                                    {
                                        // This is a package (no __path__ per importlib docs), and we
                                        // encountered an error importing something. We cannot tell
                                        // the difference between package.WrongNameTestClass and
                                        // package.wrong_module_name so we just report the
                                        // ImportError - it is more informative.
                                        self.attr("errors").method("append", { error_message });
                                        return error_case;
                                    }
                                    else
                                    {
                                        // Otherwise, we signal that an AttributeError has occurred.
                                        auto const items11 = unpack((G<"unittest.loader._make_failed_test">())(part, e, self.attr("suiteClass"), (S<"Failed to access attribute:\n%s">()) % py_tuple({ (Mod<"traceback">().attr("format_exc"))() })), 2);
                                        error_case = items11[0];
                                        error_message = items11[1];
                                        self.attr("errors").method("append", { error_message });
                                        return error_case;
                                    }
                                }
                                else
                                    py_reraise(handler9.exception);
                            }
                        }
                        if (isinstance(obj, Mod<"types">().attr("ModuleType")))
                        {
                            return self.method("loadTestsFromModule", { obj });
                        }
                        else if ((isinstance(obj, B<"type">()) && issubclass(obj, Mod<"unittest.case">().attr("TestCase")) && (!py_tuple({ Mod<"unittest.case">().attr("TestCase"), Mod<"unittest.case">().attr("FunctionTestCase") }).contains(obj))))
                        {
                            return self.method("loadTestsFromTestCase", { obj });
                        }
                        else if ((isinstance(obj, Mod<"types">().attr("FunctionType")) && isinstance(parent, B<"type">()) && issubclass(parent, Mod<"unittest.case">().attr("TestCase"))))
                        {
                            if ((Mod<"inspect">().attr("isabstract"))(parent).truthy())
                            {
                                py_raise((B<"TypeError">())((S<"Cannot instantiate abstract test case %s">()) % parent.attr("__name__")));
                            }
                            name = parts[I<-1>()];
                            inst = parent(name);
                            // static methods follow a different path
                            if (!isinstance((B<"getattr">())(inst, name), Mod<"types">().attr("FunctionType")))
                            {
                                return self.method("suiteClass", { py_list({ inst }) });
                            }
                        }
                        else if (isinstance(obj, Mod<"unittest.suite">().attr("TestSuite")))
                        {
                            return obj;
                        }
                        if (callable(obj))
                        {
                            test = obj();
                            if (isinstance(test, Mod<"unittest.suite">().attr("TestSuite")))
                            {
                                return test;
                            }
                            else if (isinstance(test, Mod<"unittest.case">().attr("TestCase")))
                            {
                                return self.method("suiteClass", { py_list({ test }) });
                            }
                            else
                            {
                                py_raise((B<"TypeError">())((S<"calling %s returned %s, not a test">()) % py_tuple({ obj, test })));
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"don't know how to make test from: %s">()) % obj));
                        }
                        return none();
                    }) },
                { "loadTestsFromNames", w.method("TestLoader", "loadTestsFromNames", { py_positional("names"), py_positional("module", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py names = a[1];
                        py module = a[2];
                        py suites;
                        suites = [&]() -> py { py const comprehension12 = py_list(); py name; for (py const& item : iterate{ names }) { name = item; comprehension12.method("append", { self.method("loadTestsFromName", { name, module }) }); } return comprehension12; }();
                        return self.method("suiteClass", { suites });
                    }) },
                { "getTestCaseNames", w.method("TestLoader", "getTestCaseNames", { py_positional("testCaseClass") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const testCaseClass = std::make_shared<py>(a[1]);
                        py shouldIncludeMethod;
                        py testFnNames;
                        shouldIncludeMethod = w.function("shouldIncludeMethod", "TestLoader.getTestCaseNames.<locals>.shouldIncludeMethod", { py_positional("attrname") }, [self, testCaseClass](arguments a) -> py
    {
        py attrname = a[0];
        py fullName;
        py testFunc;
        if (!attrname.method("startswith", { (*self).attr("testMethodPrefix") }).truthy())
        {
            return boolean(false);
        }
        testFunc = (B<"getattr">())((*testCaseClass), attrname);
        if (!callable(testFunc))
        {
            return boolean(false);
        }
        fullName = S("%s.%s.%s") % py_tuple({ (*testCaseClass).attr("__module__"), (*testCaseClass).attr("__qualname__"), attrname });
        return [&]() -> py { py const t = boolean((*self).attr("testNamePatterns").is_none()); return t.truthy() ? t : (B<"any">())([&]() -> py { py const comprehension13 = py_list(); py pattern; for (py const& item : iterate{ (*self).attr("testNamePatterns") }) { pattern = item; comprehension13.method("append", { (Mod<"fnmatch">().attr("fnmatchcase"))(fullName, pattern) }); } return comprehension13; }()); }();
    });
                        testFnNames = (B<"list">())((B<"filter">())(shouldIncludeMethod, (B<"dir">())((*testCaseClass))));
                        if ((*self).attr("sortTestMethodsUsing").truthy())
                        {
                            testFnNames.method("sort", {  }, { { "key", (Mod<"functools">().attr("cmp_to_key"))((*self).attr("sortTestMethodsUsing")) } });
                        }
                        return testFnNames;
                    }) },
                { "discover", w.method("TestLoader", "discover", { py_positional("start_dir"), py_positional("pattern", S<"test*.py">()), py_positional("top_level_dir", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py start_dir = a[1];
                        py pattern = a[2];
                        py top_level_dir = a[3];
                        py is_not_importable;
                        py original_top_level_dir;
                        py set_implicit_top;
                        py tests;
                        py the_module;
                        py top_part;
                        original_top_level_dir = self.attr("_top_level_dir");
                        set_implicit_top = boolean(false);
                        if ((top_level_dir.is_none() && (!self.attr("_top_level_dir").is_none())))
                        {
                            // make top_level_dir optional if called from load_tests in a package
                            top_level_dir = self.attr("_top_level_dir");
                        }
                        else if (top_level_dir.is_none())
                        {
                            set_implicit_top = boolean(true);
                            top_level_dir = start_dir;
                        }
                        top_level_dir = (Mod<"os">().attr("path")).method("abspath", { top_level_dir });
                        if (!(Mod<"sys">().attr("path")).contains(top_level_dir))
                        {
                            // all test modules must be importable from the top level directory
                            // should we *unconditionally* put the start directory in first
                            // in sys.path to minimise likelihood of conflicts between installed
                            // modules and development versions?
                            (Mod<"sys">().attr("path")).method("insert", { I<0>(), top_level_dir });
                        }
                        self.set_attr("_top_level_dir", top_level_dir);
                        is_not_importable = boolean(false);
                        if ((Mod<"os">().attr("path")).method("isdir", { (Mod<"os">().attr("path")).method("abspath", { start_dir }) }).truthy())
                        {
                            start_dir = (Mod<"os">().attr("path")).method("abspath", { start_dir });
                            if (start_dir != top_level_dir)
                            {
                                is_not_importable = boolean(!(Mod<"os">().attr("path")).method("isfile", { (Mod<"os">().attr("path")).method("join", { start_dir, S<"__init__.py">() }) }).truthy());
                            }
                        }
                        else
                        {
                            // support for discovery from dotted module names
                            {
                                bool otherwise14 = false;
                                try
                                {
                                    (B<"__import__">())(start_dir);
                                    otherwise14 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler14;
                                    if (matches(handler14.exception, B<"ImportError">()))
                                    {
                                        is_not_importable = boolean(true);
                                    }
                                    else
                                        py_reraise(handler14.exception);
                                }
                                if (otherwise14)
                                {
                                    the_module = (Mod<"sys">().attr("modules"))[start_dir];
                                    top_part = start_dir.method("split", { S<".">() })[I<0>()];
                                    try
                                    {
                                        start_dir = (Mod<"os">().attr("path")).method("abspath", { (Mod<"os">().attr("path")).method("dirname", { the_module.attr("__file__") }) });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler15;
                                        if (matches(handler15.exception, B<"AttributeError">()))
                                        {
                                            if ((Mod<"sys">().attr("builtin_module_names")).contains(the_module.attr("__name__")))
                                            {
                                                // builtin module
                                                fn_raise_from((B<"TypeError">())(S<"Can not use builtin modules as dotted module names">()).get(), none().get());
                                                throw raised{};
                                            }
                                            else
                                            {
                                                fn_raise_from((B<"TypeError">())(S(std::string{ "don't know how to discover from " } + the_module.repr())).get(), none().get());
                                                throw raised{};
                                            }
                                        }
                                        else
                                            py_reraise(handler15.exception);
                                    }
                                    if (set_implicit_top.truthy())
                                    {
                                        self.set_attr("_top_level_dir", self.method("_get_directory_containing_module", { top_part }));
                                        (Mod<"sys">().attr("path")).method("remove", { top_level_dir });
                                    }
                                }
                            }
                        }
                        if (is_not_importable.truthy())
                        {
                            py_raise((B<"ImportError">())((S<"Start directory is not importable: %r">()) % start_dir));
                        }
                        tests = (B<"list">())(self.method("_find_tests", { start_dir, pattern }));
                        self.set_attr("_top_level_dir", original_top_level_dir);
                        return self.method("suiteClass", { tests });
                    }) },
                { "_get_directory_containing_module", w.method("TestLoader", "_get_directory_containing_module", { py_positional("module_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py module_name = a[1];
                        py full_path;
                        py module;
                        module = (Mod<"sys">().attr("modules"))[module_name];
                        full_path = (Mod<"os">().attr("path")).method("abspath", { module.attr("__file__") });
                        if ((Mod<"os">().attr("path")).method("basename", { full_path }).method("lower").method("startswith", { S<"__init__.py">() }).truthy())
                        {
                            return (Mod<"os">().attr("path")).method("dirname", { (Mod<"os">().attr("path")).method("dirname", { full_path }) });
                        }
                        else
                        {
                            // here we have been given a module rather than a package - so
                            // all we can do is search the *same* directory the module is in
                            // should an exception be raised instead
                            return (Mod<"os">().attr("path")).method("dirname", { full_path });
                        }
                        return none();
                    }) },
                { "_get_name_from_path", w.method("TestLoader", "_get_name_from_path", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py _relpath;
                        py name;
                        if (path == self.attr("_top_level_dir"))
                        {
                            return S<".">();
                        }
                        path = (G<"unittest.loader._splitext">())((Mod<"os">().attr("path")).method("normpath", { path }));
                        _relpath = (Mod<"os">().attr("path")).method("relpath", { path, self.attr("_top_level_dir") });
                        if ((Mod<"os">().attr("path")).method("isabs", { _relpath }).truthy())
                            py_raise(B<"AssertionError">()(S<"Path must be within the project">()));
                        if (_relpath.method("startswith", { S<"..">() }).truthy())
                            py_raise(B<"AssertionError">()(S<"Path must be within the project">()));
                        name = _relpath.method("replace", { (Mod<"os">().attr("path")).attr("sep"), S<".">() });
                        return name;
                    }) },
                { "_get_module_from_name", w.method("TestLoader", "_get_module_from_name", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        (B<"__import__">())(name);
                        return (Mod<"sys">().attr("modules"))[name];
                    }) },
                { "_match_path", w.method("TestLoader", "_match_path", { py_positional("path"), py_positional("full_path"), py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py full_path = a[2];
                        py pattern = a[3];
                        return (Mod<"fnmatch">().attr("fnmatch"))(path, pattern);
                    }) },
                { "_find_tests", w.method("TestLoader", "_find_tests", { py_positional("start_dir"), py_positional("pattern") }, [](arguments a) -> py
                    {
                        return make_native_generator("_find_tests", "TestLoader._find_tests", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py start_dir = a[1];
                            py pattern = a[2];
                            py full_path;
                            py name;
                            py path;
                            py paths;
                            py should_recurse;
                            py tests;
                            // Handle the __init__ in this package
                            name = self.method("_get_name_from_path", { start_dir });
                            // name is '.' when start_dir == top_level_dir (and top_level_dir is by
                            // definition not a package).
                            if (((name != (S<".">())) && (!self.attr("_loading_packages").contains(name))))
                            {
                                // name is in self._loading_packages while we have called into
                                // loadTestsFromModule with name.
                                auto const items16 = unpack(self.method("_find_test_path", { start_dir, pattern }), 2);
                                tests = items16[0];
                                should_recurse = items16[1];
                                if (!tests.is_none())
                                {
                                    py_yield(tests);
                                }
                                if (!should_recurse.truthy())
                                {
                                    // Either an error occurred, or load_tests was used by the
                                    // package.
                                    return none();
                                }
                            }
                            // Handle the contents.
                            paths = (B<"sorted">())((Mod<"os">().attr("listdir"))(start_dir));
                            for (py const& item : iterate{ paths })
                            {
                                path = item;
                                full_path = (Mod<"os">().attr("path")).method("join", { start_dir, path });
                                auto const items18 = unpack(self.method("_find_test_path", { full_path, pattern }), 2);
                                tests = items18[0];
                                should_recurse = items18[1];
                                if (!tests.is_none())
                                {
                                    py_yield(tests);
                                }
                                if (should_recurse.truthy())
                                {
                                    // we found a package that didn't use load_tests.
                                    name = self.method("_get_name_from_path", { full_path });
                                    self.attr("_loading_packages").method("add", { name });
                                    {
                                        std::optional<py> pending19;
                                        [[maybe_unused]] int flow19 = 0;
                                        [[maybe_unused]] py result19;
                                        try
                                        {
                                            yield_from(self.method("_find_tests", { full_path, pattern }));
                                        }
                                        catch (raised const&)
                                        {
                                            pending19 = py_take_exception();
                                        }
                                        finally19:
                                        self.attr("_loading_packages").method("discard", { name });
                                        if (pending19)
                                            py_reraise(*pending19);
                                        if (flow19 == 1)
                                            return result19;
                                        if (flow19 == 2)
                                            break;
                                        if (flow19 == 3)
                                            continue;
                                    }
                                }
                            }
                            return none();
                        });
                    }) },
                { "_find_test_path", w.method("TestLoader", "_find_test_path", { py_positional("full_path"), py_positional("pattern") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py full_path = a[1];
                        py pattern = a[2];
                        py basename;
                        py e;
                        py error_case;
                        py error_message;
                        py expected_dir;
                        py fullpath_noext;
                        py load_tests;
                        py mod_file;
                        py mod_name;
                        py module;
                        py module_dir;
                        py msg;
                        py name;
                        py package;
                        py realpath;
                        py tests;
                        basename = (Mod<"os">().attr("path")).method("basename", { full_path });
                        if ((Mod<"os">().attr("path")).method("isfile", { full_path }).truthy())
                        {
                            if (!(G<"unittest.loader.VALID_MODULE_NAME">()).method("match", { basename }).truthy())
                            {
                                // valid Python identifiers only
                                return py_tuple({ none(), boolean(false) });
                            }
                            if (!self.method("_match_path", { basename, full_path, pattern }).truthy())
                            {
                                return py_tuple({ none(), boolean(false) });
                            }
                            // if the test file matches, load it
                            name = self.method("_get_name_from_path", { full_path });
                            {
                                bool otherwise20 = false;
                                try
                                {
                                    module = self.method("_get_module_from_name", { name });
                                    otherwise20 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler20;
                                    if (matches(handler20.exception, Mod<"unittest.case">().attr("SkipTest")))
                                    {
                                        e = handler20.exception;
                                        return py_tuple({ (G<"unittest.loader._make_skipped_test">())(name, e, self.attr("suiteClass")), boolean(false) });
                                    }
                                    else
                                    {
                                        auto const items21 = unpack((G<"unittest.loader._make_failed_import_test">())(name, self.attr("suiteClass")), 2);
                                        error_case = items21[0];
                                        error_message = items21[1];
                                        self.attr("errors").method("append", { error_message });
                                        return py_tuple({ error_case, boolean(false) });
                                    }
                                }
                                if (otherwise20)
                                {
                                    mod_file = (Mod<"os">().attr("path")).method("abspath", { (B<"getattr">())(module, S<"__file__">(), full_path) });
                                    realpath = (G<"unittest.loader._splitext">())((Mod<"os">().attr("path")).method("realpath", { mod_file }));
                                    fullpath_noext = (G<"unittest.loader._splitext">())((Mod<"os">().attr("path")).method("realpath", { full_path }));
                                    if (realpath.method("lower") != fullpath_noext.method("lower"))
                                    {
                                        module_dir = (Mod<"os">().attr("path")).method("dirname", { realpath });
                                        mod_name = (G<"unittest.loader._splitext">())((Mod<"os">().attr("path")).method("basename", { full_path }));
                                        expected_dir = (Mod<"os">().attr("path")).method("dirname", { full_path });
                                        msg = S<"%r module incorrectly imported from %r. Expected %r. Is this module globally installed?">();
                                        py_raise((B<"ImportError">())(msg % py_tuple({ mod_name, module_dir, expected_dir })));
                                    }
                                    return py_tuple({ self.method("loadTestsFromModule", { module }, { { "pattern", pattern } }), boolean(false) });
                                }
                            }
                        }
                        else if ((Mod<"os">().attr("path")).method("isdir", { full_path }).truthy())
                        {
                            if (!(Mod<"os">().attr("path")).method("isfile", { (Mod<"os">().attr("path")).method("join", { full_path, S<"__init__.py">() }) }).truthy())
                            {
                                return py_tuple({ none(), boolean(false) });
                            }
                            load_tests = none();
                            tests = none();
                            name = self.method("_get_name_from_path", { full_path });
                            {
                                bool otherwise22 = false;
                                try
                                {
                                    package = self.method("_get_module_from_name", { name });
                                    otherwise22 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler22;
                                    if (matches(handler22.exception, Mod<"unittest.case">().attr("SkipTest")))
                                    {
                                        e = handler22.exception;
                                        return py_tuple({ (G<"unittest.loader._make_skipped_test">())(name, e, self.attr("suiteClass")), boolean(false) });
                                    }
                                    else
                                    {
                                        auto const items23 = unpack((G<"unittest.loader._make_failed_import_test">())(name, self.attr("suiteClass")), 2);
                                        error_case = items23[0];
                                        error_message = items23[1];
                                        self.attr("errors").method("append", { error_message });
                                        return py_tuple({ error_case, boolean(false) });
                                    }
                                }
                                if (otherwise22)
                                {
                                    load_tests = (B<"getattr">())(package, S<"load_tests">(), none());
                                    // Mark this package as being in load_tests (possibly ;))
                                    self.attr("_loading_packages").method("add", { name });
                                    {
                                        std::optional<py> pending24;
                                        [[maybe_unused]] int flow24 = 0;
                                        [[maybe_unused]] py result24;
                                        try
                                        {
                                            tests = self.method("loadTestsFromModule", { package }, { { "pattern", pattern } });
                                            if (!load_tests.is_none())
                                            {
                                                // loadTestsFromModule(package) has loaded tests for us.
                                                result24 = py_tuple({ tests, boolean(false) });
                                                flow24 = 1;
                                                goto finally24;
                                            }
                                            result24 = py_tuple({ tests, boolean(true) });
                                            flow24 = 1;
                                            goto finally24;
                                        }
                                        catch (raised const&)
                                        {
                                            pending24 = py_take_exception();
                                        }
                                        finally24:
                                        self.attr("_loading_packages").method("discard", { name });
                                        if (pending24)
                                            py_reraise(*pending24);
                                        if (flow24 == 1)
                                            return result24;
                                    }
                                }
                            }
                        }
                        else
                        {
                            return py_tuple({ none(), boolean(false) });
                        }
                        return none();
                    }) }, }));

            define("unittest.loader", "defaultTestLoader", (G<"unittest.loader.TestLoader">())());
        }

        bool unittest_loader_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_loader();
                }
                if (auto const doc = unittest_loader_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("inspect", py{ fn_import("inspect") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("traceback", py{ fn_import("traceback") }.release());
                aAttributes.emplace_back("types", py{ fn_import("types") }.release());
                aAttributes.emplace_back("functools", py{ fn_import("functools") }.release());
                aAttributes.emplace_back("fnmatch", import_attribute("fnmatch", "fnmatch").release());
                aAttributes.emplace_back("fnmatchcase", import_attribute("fnmatch", "fnmatchcase").release());
                aAttributes.emplace_back("case", py{ fn_import("unittest.case") }.release());
                aAttributes.emplace_back("suite", py{ fn_import("unittest.suite") }.release());
                aAttributes.emplace_back("util", py{ fn_import("unittest.util") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.loader", unittest_loader_module);
    }
}
