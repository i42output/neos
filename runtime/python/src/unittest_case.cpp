/*
  unittest_case.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/case.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/case.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.case (CPython 3.13's Lib/unittest/case.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_case_docs.hpp"

namespace neos::python_runtime::detail::unittest_case_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.case.name"))
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
                py_raise("SystemError", std::string{ "unittest.case: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.case.name">()))
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
            static module_writer const sWriter{ "unittest.case", unittest_case_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.case (Lib/unittest/case.py)

        void make_unittest_case()
        {
            static module_writer const& w = writer("unittest.case");
            define("unittest.case", "__unittest", boolean(true));
            define("unittest.case", "_subtest_msg_sentinel", (B<"object">())());
            define("unittest.case", "DIFF_OMITTED", S<"\nDiff is %s characters long. Set self.maxDiff to None to see it.">());
            define("unittest.case", "SkipTest", w.make_class("SkipTest", { B<"Exception">() }, {
 }));

            define("unittest.case", "_ShouldStop", w.make_class("_ShouldStop", { B<"Exception">() }, {
 }));

            define("unittest.case", "_UnexpectedSuccess", w.make_class("_UnexpectedSuccess", { B<"Exception">() }, {
 }));

            define("unittest.case", "_Outcome", w.make_class("_Outcome", { B<"object">() }, {
                { "__init__", w.method("_Outcome", "__init__", { py_positional("result", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        self.set_attr("expecting_failure", boolean(false));
                        self.set_attr("result", result);
                        self.set_attr("result_supports_subtests", boolean(has_attr(result, "addSubTest")));
                        self.set_attr("success", boolean(true));
                        self.set_attr("expectedFailure", none());
                        return none();
                    }) },
                { "testPartExecutor", (Mod<"contextlib">().attr("contextmanager"))(w.method("_Outcome", "testPartExecutor", { py_positional("test_case"), py_positional("subTest", boolean(false)) }, [](arguments a) -> py
                    {
                        return make_native_generator("testPartExecutor", "_Outcome.testPartExecutor", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py test_case = a[1];
                            py subTest = a[2];
                            py e;
                            py exc_info;
                            py old_success;
                            old_success = self.attr("success");
                            self.set_attr("success", boolean(true));
                            {
                                std::optional<py> pending1;
                                [[maybe_unused]] int flow1 = 0;
                                [[maybe_unused]] py result1;
                                try
                                {
                                    bool otherwise1 = false;
                                    try
                                    {
                                        py_yield(none());
                                        otherwise1 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler1;
                                        if (matches(handler1.exception, B<"KeyboardInterrupt">()))
                                        {
                                            py_reraise(handler1.exception);
                                        }
                                        else if (matches(handler1.exception, G<"unittest.case.SkipTest">()))
                                        {
                                            e = handler1.exception;
                                            self.set_attr("success", boolean(false));
                                            (G<"unittest.case._addSkip">())(self.attr("result"), test_case, (B<"str">())(e));
                                        }
                                        else if (matches(handler1.exception, G<"unittest.case._ShouldStop">()))
                                        {
                                        }
                                        else
                                        {
                                            exc_info = (Mod<"sys">().attr("exc_info"))();
                                            if (self.attr("expecting_failure").truthy())
                                            {
                                                self.set_attr("expectedFailure", exc_info);
                                            }
                                            else
                                            {
                                                self.set_attr("success", boolean(false));
                                                if (subTest.truthy())
                                                {
                                                    self.attr("result").method("addSubTest", { test_case.attr("test_case"), test_case, exc_info });
                                                }
                                                else
                                                {
                                                    (G<"unittest.case._addError">())(self.attr("result"), test_case, exc_info);
                                                }
                                            }
                                            // explicitly break a reference cycle:
                                            // exc_info -> frame -> exc_info
                                            exc_info = none();
                                        }
                                    }
                                    if (otherwise1)
                                    {
                                        if ((subTest.truthy() && self.attr("success").truthy()))
                                        {
                                            self.attr("result").method("addSubTest", { test_case.attr("test_case"), test_case, none() });
                                        }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending1 = py_take_exception();
                                }
                                finally1:
                                self.set_attr("success", [&]() -> py { py const t = self.attr("success"); return !t.truthy() ? t : old_success; }());
                                if (pending1)
                                    py_reraise(*pending1);
                                if (flow1 == 1)
                                    return result1;
                            }
                            return none();
                        });
                    })) }, }));

            define("unittest.case", "_addSkip", w.function("_addSkip", "_addSkip", { py_positional("result"), py_positional("test_case"), py_positional("reason") }, [](arguments a) -> py
                {
                    py result = a[0];
                    py test_case = a[1];
                    py reason = a[2];
                    py addSkip;
                    addSkip = (B<"getattr">())(result, S<"addSkip">(), none());
                    if (!addSkip.is_none())
                    {
                        addSkip(test_case, reason);
                    }
                    else
                    {
                        (Mod<"warnings">().attr("warn"))(S<"TestResult has no addSkip method, skips not reported">(), B<"RuntimeWarning">(), I<2>());
                        result.method("addSuccess", { test_case });
                    }
                    return none();
                }));

            define("unittest.case", "_addError", w.function("_addError", "_addError", { py_positional("result"), py_positional("test"), py_positional("exc_info") }, [](arguments a) -> py
                {
                    py result = a[0];
                    py test = a[1];
                    py exc_info = a[2];
                    if (((!result.is_none()) && (!exc_info.is_none())))
                    {
                        if (issubclass(exc_info[I<0>()], test.attr("failureException")))
                        {
                            result.method("addFailure", { test, exc_info });
                        }
                        else
                        {
                            result.method("addError", { test, exc_info });
                        }
                    }
                    return none();
                }));

            define("unittest.case", "_id", w.function("_id", "_id", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    return obj;
                }));

            define("unittest.case", "_enter_context", w.function("_enter_context", "_enter_context", { py_positional("cm"), py_positional("addcleanup") }, [](arguments a) -> py
                {
                    py cm = a[0];
                    py addcleanup = a[1];
                    py cls;
                    py enter;
                    py exit;
                    py result;
                    cls = (B<"type">())(cm);
                    try
                    {
                        enter = cls.attr("__enter__");
                        exit = cls.attr("__exit__");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler2;
                        if (matches(handler2.exception, B<"AttributeError">()))
                        {
                            fn_raise_from((B<"TypeError">())(S(std::string{ "'" } + cls.attr("__module__").str() + "." + cls.attr("__qualname__").str() + "' object does not support the context manager protocol")).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler2.exception);
                    }
                    result = enter(cm);
                    addcleanup(exit, cm, none(), none(), none());
                    return result;
                }));

            define("unittest.case", "_module_cleanups", py_list());
            define("unittest.case", "addModuleCleanup", w.function("addModuleCleanup", "addModuleCleanup", { py_positional_only("function") }, [](arguments a) -> py
                {
                    py function = a[0];
                    py args = a[1];
                    py kwargs = a[2];
                    (G<"unittest.case._module_cleanups">()).method("append", { py_tuple({ function, args, kwargs }) });
                    return none();
                }, true, true));

            define("unittest.case", "enterModuleContext", w.function("enterModuleContext", "enterModuleContext", { py_positional("cm") }, [](arguments a) -> py
                {
                    py cm = a[0];
                    return (G<"unittest.case._enter_context">())(cm, G<"unittest.case.addModuleCleanup">());
                }));

            define("unittest.case", "doModuleCleanups", w.function("doModuleCleanups", "doModuleCleanups", {  }, [](arguments a) -> py
                {
                    py args;
                    py exc;
                    py exceptions;
                    py function;
                    py kwargs;
                    exceptions = py_list();
                    while ((G<"unittest.case._module_cleanups">()).truthy())
                    {
                        auto const items4 = unpack((G<"unittest.case._module_cleanups">()).method("pop"), 3);
                        function = items4[0];
                        args = items4[1];
                        kwargs = items4[2];
                        try
                        {
                            call_ex(function, {  }, args, kwargs);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, B<"Exception">()))
                            {
                                exc = handler5.exception;
                                exceptions.method("append", { exc });
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                    }
                    if (exceptions.truthy())
                    {
                        // Swallows all but first exception. If a multi-exception handler
                        // gets written we should use that here instead.
                        py_raise(exceptions[I<0>()]);
                    }
                    return none();
                }));

            define("unittest.case", "skip", w.function("skip", "skip", { py_positional("reason") }, [](arguments a) -> py
                {
                    auto const reason = std::make_shared<py>(a[0]);
                    py decorator;
                    auto const test_item = std::make_shared<py>();
                    decorator = w.function("decorator", "skip.<locals>.decorator", { py_positional("test_item") }, [reason](arguments a) -> py
    {
        auto const test_item = std::make_shared<py>(a[0]);
        py skip_wrapper;
        if (!isinstance((*test_item), B<"type">()))
        {
            skip_wrapper = (Mod<"functools">().attr("wraps"))((*test_item))(w.function("skip_wrapper", "skip.<locals>.decorator.<locals>.skip_wrapper", {  }, [reason, test_item](arguments a) -> py
        {
            py args = a[0];
            py kwargs = a[1];
            py_raise((G<"unittest.case.SkipTest">())((*reason)));
        }, true, true));
            *test_item = skip_wrapper;
        }
        (*test_item).set_attr("__unittest_skip__", boolean(true));
        (*test_item).set_attr("__unittest_skip_why__", (*reason));
        return (*test_item);
    });
                    if (isinstance((*reason), Mod<"types">().attr("FunctionType")))
                    {
                        *test_item = (*reason);
                        *reason = S<"">();
                        return decorator((*test_item));
                    }
                    return decorator;
                }));

            define("unittest.case", "skipIf", w.function("skipIf", "skipIf", { py_positional("condition"), py_positional("reason") }, [](arguments a) -> py
                {
                    py condition = a[0];
                    py reason = a[1];
                    if (condition.truthy())
                    {
                        return (G<"unittest.case.skip">())(reason);
                    }
                    return G<"unittest.case._id">();
                }));

            define("unittest.case", "skipUnless", w.function("skipUnless", "skipUnless", { py_positional("condition"), py_positional("reason") }, [](arguments a) -> py
                {
                    py condition = a[0];
                    py reason = a[1];
                    if (!condition.truthy())
                    {
                        return (G<"unittest.case.skip">())(reason);
                    }
                    return G<"unittest.case._id">();
                }));

            define("unittest.case", "expectedFailure", w.function("expectedFailure", "expectedFailure", { py_positional("test_item") }, [](arguments a) -> py
                {
                    py test_item = a[0];
                    test_item.set_attr("__unittest_expecting_failure__", boolean(true));
                    return test_item;
                }));

            define("unittest.case", "_is_subtype", w.function("_is_subtype", "_is_subtype", { py_positional("expected"), py_positional("basetype") }, [](arguments a) -> py
                {
                    py expected = a[0];
                    py basetype = a[1];
                    if (isinstance(expected, B<"tuple">()))
                    {
                        return (B<"all">())([&]() -> py { py const comprehension6 = py_list(); py e; for (py const& item : iterate{ expected }) { e = item; comprehension6.method("append", { (G<"unittest.case._is_subtype">())(e, basetype) }); } return comprehension6; }());
                    }
                    return [&]() -> py { py const t = boolean(isinstance(expected, B<"type">())); return !t.truthy() ? t : boolean(issubclass(expected, basetype)); }();
                }));

            define("unittest.case", "_BaseTestCaseContext", w.make_class("_BaseTestCaseContext", {  }, {
                { "__init__", w.method("_BaseTestCaseContext", "__init__", { py_positional("test_case") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test_case = a[1];
                        self.set_attr("test_case", test_case);
                        return none();
                    }) },
                { "_raiseFailure", w.method("_BaseTestCaseContext", "_raiseFailure", { py_positional("standardMsg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py standardMsg = a[1];
                        py msg;
                        msg = self.attr("test_case").method("_formatMessage", { self.attr("msg"), standardMsg });
                        py_raise(self.attr("test_case").method("failureException", { msg }));
                    }) }, }));

            define("unittest.case", "_AssertRaisesBaseContext", w.make_class("_AssertRaisesBaseContext", { G<"unittest.case._BaseTestCaseContext">() }, {
                { "__init__", w.method("_AssertRaisesBaseContext", "__init__", { py_positional("expected"), py_positional("test_case"), py_positional("expected_regex", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected = a[1];
                        py test_case = a[2];
                        py expected_regex = a[3];
                        (G<"unittest.case._BaseTestCaseContext">()).method("__init__", { self, test_case });
                        self.set_attr("expected", expected);
                        self.set_attr("test_case", test_case);
                        if (!expected_regex.is_none())
                        {
                            expected_regex = (Mod<"re">().attr("compile"))(expected_regex);
                        }
                        self.set_attr("expected_regex", expected_regex);
                        self.set_attr("obj_name", none());
                        self.set_attr("msg", none());
                        return none();
                    }) },
                { "handle", w.method("_AssertRaisesBaseContext", "handle", { py_positional("name"), py_positional("args"), py_positional("kwargs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        py callable_obj;
                        {
                            std::optional<py> pending7;
                            [[maybe_unused]] int flow7 = 0;
                            [[maybe_unused]] py result7;
                            try
                            {
                                if (!(G<"unittest.case._is_subtype">())(self.attr("expected"), self.attr("_base_type")).truthy())
                                {
                                    py_raise((B<"TypeError">())((S<"%s() arg 1 must be %s">()) % py_tuple({ name, self.attr("_base_type_str") })));
                                }
                                if (!args.truthy())
                                {
                                    self.set_attr("msg", kwargs.method("pop", { S<"msg">(), none() }));
                                    if (kwargs.truthy())
                                    {
                                        py_raise((B<"TypeError">())((S<"%r is an invalid keyword argument for this function">()) % py_tuple({ (B<"next">())((B<"iter">())(kwargs)) })));
                                    }
                                    result7 = self;
                                    flow7 = 1;
                                    goto finally7;
                                }
                                auto const items8 = B<"list">()(args).items();
                                if (items8.size() < 1u)
                                    py_raise("ValueError", "not enough values to unpack (expected at least 1, got " + std::to_string(items8.size()) + ")");
                                callable_obj = items8[0];
                                args = py_list(std::vector<py>(items8.begin() + 1, items8.end() - 0));
                                try
                                {
                                    self.set_attr("obj_name", callable_obj.attr("__name__"));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler9;
                                    if (matches(handler9.exception, B<"AttributeError">()))
                                    {
                                        self.set_attr("obj_name", (B<"str">())(callable_obj));
                                    }
                                    else
                                        py_reraise(handler9.exception);
                                }
                                {
                                    py const manager10 = self;
                                    py const exit10 = pyclass::special_method(manager10, "__exit__");
                                    py const value10 = pyclass::special_method(manager10, "__enter__")(manager10);
                                    [[maybe_unused]] int flow10 = 0;
                                    [[maybe_unused]] py result10;
                                    bool exited10 = false;
                                    try
                                    {
                                        call_ex(callable_obj, {  }, args, kwargs);
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler10;
                                        exited10 = true;
                                        if (!exit10(manager10, type_of(handler10.exception), handler10.exception, handler10.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler10.exception);
                                    }
                                    finally10:
                                    if (!exited10)
                                        exit10(manager10, none(), none(), none());
                                    if (flow10 == 1)
                                        { result7 = result10; flow7 = 1; goto finally7; }
                                }
                            }
                            catch (raised const&)
                            {
                                pending7 = py_take_exception();
                            }
                            finally7:
                            // bpo-23890: manually break a reference cycle
                            self = none();
                            if (pending7)
                                py_reraise(*pending7);
                            if (flow7 == 1)
                                return result7;
                        }
                        return none();
                    }) }, }));

            define("unittest.case", "_AssertRaisesContext", w.make_class("_AssertRaisesContext", { G<"unittest.case._AssertRaisesBaseContext">() }, {
                { "_base_type", B<"BaseException">() },
                { "_base_type_str", S<"an exception type or tuple of exception types">() },
                { "__enter__", w.method("_AssertRaisesContext", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("_AssertRaisesContext", "__exit__", { py_positional("exc_type"), py_positional("exc_value"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_value = a[2];
                        py tb = a[3];
                        py exc_name;
                        py expected_regex;
                        if (exc_type.is_none())
                        {
                            try
                            {
                                exc_name = self.attr("expected").attr("__name__");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler11;
                                if (matches(handler11.exception, B<"AttributeError">()))
                                {
                                    exc_name = (B<"str">())(self.attr("expected"));
                                }
                                else
                                    py_reraise(handler11.exception);
                            }
                            if (self.attr("obj_name").truthy())
                            {
                                self.method("_raiseFailure", { (S<"{} not raised by {}">()).method("format", { exc_name, self.attr("obj_name") }) });
                            }
                            else
                            {
                                self.method("_raiseFailure", { (S<"{} not raised">()).method("format", { exc_name }) });
                            }
                        }
                        else
                        {
                            (Mod<"traceback">().attr("clear_frames"))(tb);
                        }
                        if (!issubclass(exc_type, self.attr("expected")))
                        {
                            // let unexpected exceptions pass through
                            return boolean(false);
                        }
                        // store exception, without traceback, for later retrieval
                        self.set_attr("exception", exc_value.method("with_traceback", { none() }));
                        if (self.attr("expected_regex").is_none())
                        {
                            return boolean(true);
                        }
                        expected_regex = self.attr("expected_regex");
                        if (!expected_regex.method("search", { (B<"str">())(exc_value) }).truthy())
                        {
                            self.method("_raiseFailure", { (S<"\"{}\" does not match \"{}\"">()).method("format", { expected_regex.attr("pattern"), (B<"str">())(exc_value) }) });
                        }
                        return boolean(true);
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

            define("unittest.case", "_AssertWarnsContext", w.make_class("_AssertWarnsContext", { G<"unittest.case._AssertRaisesBaseContext">() }, {
                { "_base_type", B<"Warning">() },
                { "_base_type_str", S<"a warning type or tuple of warning types">() },
                { "__enter__", w.method("_AssertWarnsContext", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py v;
                        for (py const& item : iterate{ (B<"list">())((Mod<"sys">().attr("modules")).method("values")) })
                        {
                            v = item;
                            if ((B<"getattr">())(v, S<"__warningregistry__">(), none()).truthy())
                            {
                                v.set_attr("__warningregistry__", py_dict());
                            }
                        }
                        self.set_attr("warnings_manager", (Mod<"warnings">().attr("catch_warnings")).call({  }, { { "record", boolean(true) } }));
                        self.set_attr("warnings", self.attr("warnings_manager").method("__enter__"));
                        (Mod<"warnings">().attr("simplefilter"))(S<"always">(), self.attr("expected"));
                        return self;
                    }) },
                { "__exit__", w.method("_AssertWarnsContext", "__exit__", { py_positional("exc_type"), py_positional("exc_value"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_value = a[2];
                        py tb = a[3];
                        py exc_name;
                        py first_matching;
                        py m;
                        py w;
                        self.attr("warnings_manager").method("__exit__", { exc_type, exc_value, tb });
                        if (!exc_type.is_none())
                        {
                            // let unexpected exceptions pass through
                            return none();
                        }
                        try
                        {
                            exc_name = self.attr("expected").attr("__name__");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler13;
                            if (matches(handler13.exception, B<"AttributeError">()))
                            {
                                exc_name = (B<"str">())(self.attr("expected"));
                            }
                            else
                                py_reraise(handler13.exception);
                        }
                        first_matching = none();
                        for (py const& item : iterate{ self.attr("warnings") })
                        {
                            m = item;
                            w = m.attr("message");
                            if (!isinstance(w, self.attr("expected")))
                            {
                                continue;
                            }
                            if (first_matching.is_none())
                            {
                                first_matching = w;
                            }
                            if (((!self.attr("expected_regex").is_none()) && (!self.attr("expected_regex").method("search", { (B<"str">())(w) }).truthy())))
                            {
                                continue;
                            }
                            // store warning for later retrieval
                            self.set_attr("warning", w);
                            self.set_attr("filename", m.attr("filename"));
                            self.set_attr("lineno", m.attr("lineno"));
                            return none();
                        }
                        // Now we simply try to choose a helpful failure message
                        if (!first_matching.is_none())
                        {
                            self.method("_raiseFailure", { (S<"\"{}\" does not match \"{}\"">()).method("format", { self.attr("expected_regex").attr("pattern"), (B<"str">())(first_matching) }) });
                        }
                        if (self.attr("obj_name").truthy())
                        {
                            self.method("_raiseFailure", { (S<"{} not triggered by {}">()).method("format", { exc_name, self.attr("obj_name") }) });
                        }
                        else
                        {
                            self.method("_raiseFailure", { (S<"{} not triggered">()).method("format", { exc_name }) });
                        }
                        return none();
                    }) }, }));

            define("unittest.case", "_AssertNotWarnsContext", w.make_class("_AssertNotWarnsContext", { G<"unittest.case._AssertWarnsContext">() }, {
                { "__exit__", w.method("_AssertNotWarnsContext", "__exit__", { py_positional("exc_type"), py_positional("exc_value"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_value = a[2];
                        py tb = a[3];
                        py exc_name;
                        py m;
                        py w;
                        self.attr("warnings_manager").method("__exit__", { exc_type, exc_value, tb });
                        if (!exc_type.is_none())
                        {
                            // let unexpected exceptions pass through
                            return none();
                        }
                        try
                        {
                            exc_name = self.attr("expected").attr("__name__");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler15;
                            if (matches(handler15.exception, B<"AttributeError">()))
                            {
                                exc_name = (B<"str">())(self.attr("expected"));
                            }
                            else
                                py_reraise(handler15.exception);
                        }
                        for (py const& item : iterate{ self.attr("warnings") })
                        {
                            m = item;
                            w = m.attr("message");
                            if (isinstance(w, self.attr("expected")))
                            {
                                self.method("_raiseFailure", { S(exc_name.str() + " triggered") });
                            }
                        }
                        return none();
                    }) }, }));

            define("unittest.case", "_OrderedChainMap", w.make_class("_OrderedChainMap", { Mod<"collections">().attr("ChainMap") }, {
                { "__iter__", w.method("_OrderedChainMap", "__iter__", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("__iter__", "_OrderedChainMap.__iter__", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py k;
                            py mapping;
                            py seen;
                            seen = (B<"set">())();
                            for (py const& item : iterate{ self.attr("maps") })
                            {
                                mapping = item;
                                for (py const& item : iterate{ mapping })
                                {
                                    k = item;
                                    if (!seen.contains(k))
                                    {
                                        seen.method("add", { k });
                                        py_yield(k);
                                    }
                                }
                            }
                            return none();
                        });
                    }) }, }));

                // If a string is longer than _diffThreshold, use normal comparison instead
                // of difflib.  See #11763.
            define("unittest.case", "TestCase", w.make_class("TestCase", { B<"object">() }, {
                { "failureException", B<"AssertionError">() },
                { "longMessage", boolean(true) },
                { "maxDiff", (I<80>()) * (I<8>()) },
                { "_diffThreshold", power((I<2>()), (I<16>())) },
                { "__init_subclass__", w.method("TestCase", "__init_subclass__", {  }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        cls.set_attr("_classSetupFailed", boolean(false));
                        cls.set_attr("_class_cleanups", py_list());
                        call_ex((B<"super">()(G<"unittest.case.TestCase">(), cls)).attr("__init_subclass__"), {  }, args, kwargs);
                        return none();
                    }, true, true) },
                { "__init__", w.method("TestCase", "__init__", { py_positional("methodName", S<"runTest">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py methodName = a[1];
                        py testMethod;
                        self.set_attr("_testMethodName", methodName);
                        self.set_attr("_outcome", none());
                        self.set_attr("_testMethodDoc", S<"No test">());
                        {
                            bool otherwise19 = false;
                            try
                            {
                                testMethod = (B<"getattr">())(self, methodName);
                                otherwise19 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler19;
                                if (matches(handler19.exception, B<"AttributeError">()))
                                {
                                    if (methodName != (S<"runTest">()))
                                    {
                                        // we allow instantiation with no explicit method name
                                        // but not an *incorrect* or missing method name
                                        py_raise((B<"ValueError">())((S<"no such test method in %s: %s">()) % py_tuple({ self.attr("__class__"), methodName })));
                                    }
                                }
                                else
                                    py_reraise(handler19.exception);
                            }
                            if (otherwise19)
                            {
                                self.set_attr("_testMethodDoc", testMethod.attr("__doc__"));
                            }
                        }
                        self.set_attr("_cleanups", py_list());
                        self.set_attr("_subtest", none());
                        // Map types to custom assertEqual functions that will compare
                        // instances of said type in more detail to generate a more useful
                        // error message.
                        self.set_attr("_type_equality_funcs", py_dict());
                        self.method("addTypeEqualityFunc", { B<"dict">(), S<"assertDictEqual">() });
                        self.method("addTypeEqualityFunc", { B<"list">(), S<"assertListEqual">() });
                        self.method("addTypeEqualityFunc", { B<"tuple">(), S<"assertTupleEqual">() });
                        self.method("addTypeEqualityFunc", { B<"set">(), S<"assertSetEqual">() });
                        self.method("addTypeEqualityFunc", { B<"frozenset">(), S<"assertSetEqual">() });
                        self.method("addTypeEqualityFunc", { B<"str">(), S<"assertMultiLineEqual">() });
                        return none();
                    }) },
                { "addTypeEqualityFunc", w.method("TestCase", "addTypeEqualityFunc", { py_positional("typeobj"), py_positional("function") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py typeobj = a[1];
                        py function = a[2];
                        self.attr("_type_equality_funcs").set_item(typeobj, function);
                        return none();
                    }) },
                { "addCleanup", w.method("TestCase", "addCleanup", { py_positional_only("function") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py function = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        self.attr("_cleanups").method("append", { py_tuple({ function, args, kwargs }) });
                        return none();
                    }, true, true) },
                { "enterContext", w.method("TestCase", "enterContext", { py_positional("cm") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cm = a[1];
                        return (G<"unittest.case._enter_context">())(cm, self.attr("addCleanup"));
                    }) },
                { "addClassCleanup", w.class_method("TestCase", "addClassCleanup", { py_positional_only("function") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py function = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        cls.attr("_class_cleanups").method("append", { py_tuple({ function, args, kwargs }) });
                        return none();
                    }, true, true) },
                { "enterClassContext", w.class_method("TestCase", "enterClassContext", { py_positional("cm") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py cm = a[1];
                        return (G<"unittest.case._enter_context">())(cm, cls.attr("addClassCleanup"));
                    }) },
                { "setUp", w.method("TestCase", "setUp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "tearDown", w.method("TestCase", "tearDown", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "setUpClass", w.class_method("TestCase", "setUpClass", {  }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        return none();
                    }) },
                { "tearDownClass", w.class_method("TestCase", "tearDownClass", {  }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        return none();
                    }) },
                { "countTestCases", w.method("TestCase", "countTestCases", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return I<1>();
                    }) },
                { "defaultTestResult", w.method("TestCase", "defaultTestResult", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"unittest.result">().attr("TestResult"))();
                    }) },
                { "shortDescription", w.method("TestCase", "shortDescription", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py doc;
                        doc = self.attr("_testMethodDoc");
                        return (doc.truthy() ? doc.method("strip").method("split", { S<"\n">() })[I<0>()].method("strip") : none());
                    }) },
                { "id", w.method("TestCase", "id", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s.%s">()) % py_tuple({ (Mod<"unittest.util">().attr("strclass"))(self.attr("__class__")), self.attr("_testMethodName") });
                    }) },
                { "__eq__", w.method("TestCase", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!(B<"type">())(self).is((B<"type">())(other)))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(self.attr("_testMethodName") == other.attr("_testMethodName"));
                    }) },
                { "__hash__", w.method("TestCase", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(py_tuple({ (B<"type">())(self), self.attr("_testMethodName") }));
                    }) },
                { "__str__", w.method("TestCase", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s (%s.%s)">()) % py_tuple({ self.attr("_testMethodName"), (Mod<"unittest.util">().attr("strclass"))(self.attr("__class__")), self.attr("_testMethodName") });
                    }) },
                { "__repr__", w.method("TestCase", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s testMethod=%s>">()) % py_tuple({ (Mod<"unittest.util">().attr("strclass"))(self.attr("__class__")), self.attr("_testMethodName") });
                    }) },
                { "subTest", (Mod<"contextlib">().attr("contextmanager"))(w.method("TestCase", "subTest", { py_positional("msg", G<"unittest.case._subtest_msg_sentinel">()) }, [](arguments a) -> py
                    {
                        return make_native_generator("subTest", "TestCase.subTest", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py msg = a[1];
                            py params = a[2];
                            py params_map;
                            py parent;
                            py result;
                            if ((self.attr("_outcome").is_none() || (!self.attr("_outcome").attr("result_supports_subtests").truthy())))
                            {
                                py_yield(none());
                                return none();
                            }
                            parent = self.attr("_subtest");
                            if (parent.is_none())
                            {
                                params_map = (G<"unittest.case._OrderedChainMap">())(params);
                            }
                            else
                            {
                                params_map = parent.attr("params").method("new_child", { params });
                            }
                            self.set_attr("_subtest", (G<"unittest.case._SubTest">())(self, msg, params_map));
                            {
                                std::optional<py> pending20;
                                [[maybe_unused]] int flow20 = 0;
                                [[maybe_unused]] py result20;
                                try
                                {
                                    {
                                        py const manager21 = self.attr("_outcome").method("testPartExecutor", { self.attr("_subtest") }, { { "subTest", boolean(true) } });
                                        py const exit21 = pyclass::special_method(manager21, "__exit__");
                                        py const value21 = pyclass::special_method(manager21, "__enter__")(manager21);
                                        [[maybe_unused]] int flow21 = 0;
                                        [[maybe_unused]] py result21;
                                        bool exited21 = false;
                                        try
                                        {
                                            py_yield(none());
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler21;
                                            exited21 = true;
                                            if (!exit21(manager21, type_of(handler21.exception), handler21.exception, handler21.exception.attr("__traceback__")).truthy())
                                                py_reraise(handler21.exception);
                                        }
                                        finally21:
                                        if (!exited21)
                                            exit21(manager21, none(), none(), none());
                                        if (flow21 == 1)
                                            { result20 = result21; flow20 = 1; goto finally20; }
                                    }
                                    if (!self.attr("_outcome").attr("success").truthy())
                                    {
                                        result = self.attr("_outcome").attr("result");
                                        if (((!result.is_none()) && result.attr("failfast").truthy()))
                                        {
                                            py_raise(G<"unittest.case._ShouldStop">());
                                        }
                                    }
                                    else if (self.attr("_outcome").attr("expectedFailure").truthy())
                                    {
                                        // If the test is expecting a failure, we really want to
                                        // stop now and register the expected failure.
                                        py_raise(G<"unittest.case._ShouldStop">());
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending20 = py_take_exception();
                                }
                                finally20:
                                self.set_attr("_subtest", parent);
                                if (pending20)
                                    py_reraise(*pending20);
                                if (flow20 == 1)
                                    return result20;
                            }
                            return none();
                        });
                    }, false, true)) },
                { "_addExpectedFailure", w.method("TestCase", "_addExpectedFailure", { py_positional("result"), py_positional("exc_info") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py exc_info = a[2];
                        py addExpectedFailure;
                        {
                            bool otherwise22 = false;
                            try
                            {
                                addExpectedFailure = result.attr("addExpectedFailure");
                                otherwise22 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler22;
                                if (matches(handler22.exception, B<"AttributeError">()))
                                {
                                    (Mod<"warnings">().attr("warn"))(S<"TestResult has no addExpectedFailure method, reporting as passes">(), B<"RuntimeWarning">());
                                    result.method("addSuccess", { self });
                                }
                                else
                                    py_reraise(handler22.exception);
                            }
                            if (otherwise22)
                            {
                                addExpectedFailure(self, exc_info);
                            }
                        }
                        return none();
                    }) },
                { "_addUnexpectedSuccess", w.method("TestCase", "_addUnexpectedSuccess", { py_positional("result") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py addUnexpectedSuccess;
                        {
                            bool otherwise23 = false;
                            try
                            {
                                addUnexpectedSuccess = result.attr("addUnexpectedSuccess");
                                otherwise23 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler23;
                                if (matches(handler23.exception, B<"AttributeError">()))
                                {
                                    (Mod<"warnings">().attr("warn"))(S<"TestResult has no addUnexpectedSuccess method, reporting as failure">(), B<"RuntimeWarning">());
                                    // We need to pass an actual exception and traceback to addFailure,
                                    // otherwise the legacy result can choke.
                                    try
                                    {
                                        fn_raise_from((G<"unittest.case._UnexpectedSuccess">()).get(), none().get());
                                        throw raised{};
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler24;
                                        if (matches(handler24.exception, G<"unittest.case._UnexpectedSuccess">()))
                                        {
                                            result.method("addFailure", { self, (Mod<"sys">().attr("exc_info"))() });
                                        }
                                        else
                                            py_reraise(handler24.exception);
                                    }
                                }
                                else
                                    py_reraise(handler23.exception);
                            }
                            if (otherwise23)
                            {
                                addUnexpectedSuccess(self);
                            }
                        }
                        return none();
                    }) },
                { "_addDuration", w.method("TestCase", "_addDuration", { py_positional("result"), py_positional("elapsed") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py elapsed = a[2];
                        py addDuration;
                        {
                            bool otherwise25 = false;
                            try
                            {
                                addDuration = result.attr("addDuration");
                                otherwise25 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler25;
                                if (matches(handler25.exception, B<"AttributeError">()))
                                {
                                    (Mod<"warnings">().attr("warn"))(S<"TestResult has no addDuration method">(), B<"RuntimeWarning">());
                                }
                                else
                                    py_reraise(handler25.exception);
                            }
                            if (otherwise25)
                            {
                                addDuration(self, elapsed);
                            }
                        }
                        return none();
                    }) },
                { "_callSetUp", w.method("TestCase", "_callSetUp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("setUp");
                        return none();
                    }) },
                { "_callTestMethod", w.method("TestCase", "_callTestMethod", { py_positional("method") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method = a[1];
                        if (!method().is_none())
                        {
                            (Mod<"warnings">().attr("warn")).call({ S(std::string{ "It is deprecated to return a value that is not None from a test case (" } + method.str() + ")"), B<"DeprecationWarning">() }, { { "stacklevel", I<2>() } });
                        }
                        return none();
                    }) },
                { "_callTearDown", w.method("TestCase", "_callTearDown", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("tearDown");
                        return none();
                    }) },
                { "_callCleanup", w.method("TestCase", "_callCleanup", { py_positional_only("function") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py function = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        call_ex(function, {  }, args, kwargs);
                        return none();
                    }, true, true) },
                { "run", w.method("TestCase", "run", { py_positional("result", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        py expecting_failure;
                        py outcome;
                        py skip_why;
                        py startTestRun;
                        py start_time;
                        py stopTestRun;
                        py testMethod;
                        if (result.is_none())
                        {
                            result = self.method("defaultTestResult");
                            startTestRun = (B<"getattr">())(result, S<"startTestRun">(), none());
                            stopTestRun = (B<"getattr">())(result, S<"stopTestRun">(), none());
                            if (!startTestRun.is_none())
                            {
                                startTestRun();
                            }
                        }
                        else
                        {
                            stopTestRun = none();
                        }
                        result.method("startTest", { self });
                        {
                            std::optional<py> pending26;
                            [[maybe_unused]] int flow26 = 0;
                            [[maybe_unused]] py result26;
                            try
                            {
                                testMethod = (B<"getattr">())(self, self.attr("_testMethodName"));
                                if (((B<"getattr">())(self.attr("__class__"), S<"__unittest_skip__">(), boolean(false)).truthy() || (B<"getattr">())(testMethod, S<"__unittest_skip__">(), boolean(false)).truthy()))
                                {
                                    // If the class or method was skipped.
                                    skip_why = [&]() -> py { py const t = (B<"getattr">())(self.attr("__class__"), S<"__unittest_skip_why__">(), S<"">()); return t.truthy() ? t : (B<"getattr">())(testMethod, S<"__unittest_skip_why__">(), S<"">()); }();
                                    (G<"unittest.case._addSkip">())(result, self, skip_why);
                                    result26 = result;
                                    flow26 = 1;
                                    goto finally26;
                                }
                                expecting_failure = [&]() -> py { py const t = (B<"getattr">())(self, S<"__unittest_expecting_failure__">(), boolean(false)); return t.truthy() ? t : (B<"getattr">())(testMethod, S<"__unittest_expecting_failure__">(), boolean(false)); }();
                                outcome = (G<"unittest.case._Outcome">())(result);
                                start_time = (Mod<"time">().attr("perf_counter"))();
                                {
                                    std::optional<py> pending27;
                                    [[maybe_unused]] int flow27 = 0;
                                    [[maybe_unused]] py result27;
                                    try
                                    {
                                        self.set_attr("_outcome", outcome);
                                        {
                                            py const manager28 = outcome.method("testPartExecutor", { self });
                                            py const exit28 = pyclass::special_method(manager28, "__exit__");
                                            py const value28 = pyclass::special_method(manager28, "__enter__")(manager28);
                                            [[maybe_unused]] int flow28 = 0;
                                            [[maybe_unused]] py result28;
                                            bool exited28 = false;
                                            try
                                            {
                                                self.method("_callSetUp");
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler28;
                                                exited28 = true;
                                                if (!exit28(manager28, type_of(handler28.exception), handler28.exception, handler28.exception.attr("__traceback__")).truthy())
                                                    py_reraise(handler28.exception);
                                            }
                                            finally28:
                                            if (!exited28)
                                                exit28(manager28, none(), none(), none());
                                            if (flow28 == 1)
                                                { result27 = result28; flow27 = 1; goto finally27; }
                                        }
                                        if (outcome.attr("success").truthy())
                                        {
                                            outcome.set_attr("expecting_failure", expecting_failure);
                                            {
                                                py const manager29 = outcome.method("testPartExecutor", { self });
                                                py const exit29 = pyclass::special_method(manager29, "__exit__");
                                                py const value29 = pyclass::special_method(manager29, "__enter__")(manager29);
                                                [[maybe_unused]] int flow29 = 0;
                                                [[maybe_unused]] py result29;
                                                bool exited29 = false;
                                                try
                                                {
                                                    self.method("_callTestMethod", { testMethod });
                                                }
                                                catch (raised const&)
                                                {
                                                    pyclass::handler_scope handler29;
                                                    exited29 = true;
                                                    if (!exit29(manager29, type_of(handler29.exception), handler29.exception, handler29.exception.attr("__traceback__")).truthy())
                                                        py_reraise(handler29.exception);
                                                }
                                                finally29:
                                                if (!exited29)
                                                    exit29(manager29, none(), none(), none());
                                                if (flow29 == 1)
                                                    { result27 = result29; flow27 = 1; goto finally27; }
                                            }
                                            outcome.set_attr("expecting_failure", boolean(false));
                                            {
                                                py const manager30 = outcome.method("testPartExecutor", { self });
                                                py const exit30 = pyclass::special_method(manager30, "__exit__");
                                                py const value30 = pyclass::special_method(manager30, "__enter__")(manager30);
                                                [[maybe_unused]] int flow30 = 0;
                                                [[maybe_unused]] py result30;
                                                bool exited30 = false;
                                                try
                                                {
                                                    self.method("_callTearDown");
                                                }
                                                catch (raised const&)
                                                {
                                                    pyclass::handler_scope handler30;
                                                    exited30 = true;
                                                    if (!exit30(manager30, type_of(handler30.exception), handler30.exception, handler30.exception.attr("__traceback__")).truthy())
                                                        py_reraise(handler30.exception);
                                                }
                                                finally30:
                                                if (!exited30)
                                                    exit30(manager30, none(), none(), none());
                                                if (flow30 == 1)
                                                    { result27 = result30; flow27 = 1; goto finally27; }
                                            }
                                        }
                                        self.method("doCleanups");
                                        self.method("_addDuration", { result, (Mod<"time">().attr("perf_counter"))() - start_time });
                                        if (outcome.attr("success").truthy())
                                        {
                                            if (expecting_failure.truthy())
                                            {
                                                if (outcome.attr("expectedFailure").truthy())
                                                {
                                                    self.method("_addExpectedFailure", { result, outcome.attr("expectedFailure") });
                                                }
                                                else
                                                {
                                                    self.method("_addUnexpectedSuccess", { result });
                                                }
                                            }
                                            else
                                            {
                                                result.method("addSuccess", { self });
                                            }
                                        }
                                        result27 = result;
                                        flow27 = 1;
                                        goto finally27;
                                    }
                                    catch (raised const&)
                                    {
                                        pending27 = py_take_exception();
                                    }
                                    finally27:
                                    // explicitly break reference cycle:
                                    // outcome.expectedFailure -> frame -> outcome -> outcome.expectedFailure
                                    outcome.set_attr("expectedFailure", none());
                                    outcome = none();
                                    // clear the outcome, no more needed
                                    self.set_attr("_outcome", none());
                                    if (pending27)
                                        py_reraise(*pending27);
                                    if (flow27 == 1)
                                        { result26 = result27; flow26 = 1; goto finally26; }
                                }
                            }
                            catch (raised const&)
                            {
                                pending26 = py_take_exception();
                            }
                            finally26:
                            result.method("stopTest", { self });
                            if (!stopTestRun.is_none())
                            {
                                stopTestRun();
                            }
                            if (pending26)
                                py_reraise(*pending26);
                            if (flow26 == 1)
                                return result26;
                        }
                        return none();
                    }) },
                { "doCleanups", w.method("TestCase", "doCleanups", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        py function;
                        py kwargs;
                        py outcome;
                        outcome = [&]() -> py { py const t = self.attr("_outcome"); return t.truthy() ? t : (G<"unittest.case._Outcome">())(); }();
                        while (self.attr("_cleanups").truthy())
                        {
                            auto const items32 = unpack(self.attr("_cleanups").method("pop"), 3);
                            function = items32[0];
                            args = items32[1];
                            kwargs = items32[2];
                            {
                                py const manager33 = outcome.method("testPartExecutor", { self });
                                py const exit33 = pyclass::special_method(manager33, "__exit__");
                                py const value33 = pyclass::special_method(manager33, "__enter__")(manager33);
                                [[maybe_unused]] int flow33 = 0;
                                [[maybe_unused]] py result33;
                                bool exited33 = false;
                                try
                                {
                                    call_ex(self.attr("_callCleanup"), { function }, args, kwargs);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler33;
                                    exited33 = true;
                                    if (!exit33(manager33, type_of(handler33.exception), handler33.exception, handler33.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler33.exception);
                                }
                                finally33:
                                if (!exited33)
                                    exit33(manager33, none(), none(), none());
                                if (flow33 == 1)
                                    return result33;
                                if (flow33 == 2)
                                    break;
                                if (flow33 == 3)
                                    continue;
                            }
                        }
                        // return this for backwards compatibility
                        // even though we no longer use it internally
                        return outcome.attr("success");
                    }) },
                { "doClassCleanups", w.class_method("TestCase", "doClassCleanups", {  }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py args;
                        py function;
                        py kwargs;
                        cls.set_attr("tearDown_exceptions", py_list());
                        while (cls.attr("_class_cleanups").truthy())
                        {
                            auto const items35 = unpack(cls.attr("_class_cleanups").method("pop"), 3);
                            function = items35[0];
                            args = items35[1];
                            kwargs = items35[2];
                            try
                            {
                                call_ex(function, {  }, args, kwargs);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler36;
                                if (matches(handler36.exception, B<"Exception">()))
                                {
                                    cls.attr("tearDown_exceptions").method("append", { (Mod<"sys">().attr("exc_info"))() });
                                }
                                else
                                    py_reraise(handler36.exception);
                            }
                        }
                        return none();
                    }) },
                { "__call__", w.method("TestCase", "__call__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwds = a[2];
                        return call_ex(self.attr("run"), {  }, args, kwds);
                    }, true, true) },
                { "debug", w.method("TestCase", "debug", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        py function;
                        py kwargs;
                        py skip_why;
                        py testMethod;
                        testMethod = (B<"getattr">())(self, self.attr("_testMethodName"));
                        if (((B<"getattr">())(self.attr("__class__"), S<"__unittest_skip__">(), boolean(false)).truthy() || (B<"getattr">())(testMethod, S<"__unittest_skip__">(), boolean(false)).truthy()))
                        {
                            // If the class or method was skipped.
                            skip_why = [&]() -> py { py const t = (B<"getattr">())(self.attr("__class__"), S<"__unittest_skip_why__">(), S<"">()); return t.truthy() ? t : (B<"getattr">())(testMethod, S<"__unittest_skip_why__">(), S<"">()); }();
                            py_raise((G<"unittest.case.SkipTest">())(skip_why));
                        }
                        self.method("_callSetUp");
                        self.method("_callTestMethod", { testMethod });
                        self.method("_callTearDown");
                        while (self.attr("_cleanups").truthy())
                        {
                            auto const items38 = unpack(self.attr("_cleanups").method("pop"), 3);
                            function = items38[0];
                            args = items38[1];
                            kwargs = items38[2];
                            call_ex(self.attr("_callCleanup"), { function }, args, kwargs);
                        }
                        return none();
                    }) },
                { "skipTest", w.method("TestCase", "skipTest", { py_positional("reason") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py reason = a[1];
                        py_raise((G<"unittest.case.SkipTest">())(reason));
                    }) },
                { "fail", w.method("TestCase", "fail", { py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py_raise(self.method("failureException", { msg }));
                    }) },
                { "assertFalse", w.method("TestCase", "assertFalse", { py_positional("expr"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expr = a[1];
                        py msg = a[2];
                        if (expr.truthy())
                        {
                            msg = self.method("_formatMessage", { msg, (S<"%s is not false">()) % (Mod<"unittest.util">().attr("safe_repr"))(expr) });
                            py_raise(self.method("failureException", { msg }));
                        }
                        return none();
                    }) },
                { "assertTrue", w.method("TestCase", "assertTrue", { py_positional("expr"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expr = a[1];
                        py msg = a[2];
                        if (!expr.truthy())
                        {
                            msg = self.method("_formatMessage", { msg, (S<"%s is not true">()) % (Mod<"unittest.util">().attr("safe_repr"))(expr) });
                            py_raise(self.method("failureException", { msg }));
                        }
                        return none();
                    }) },
                { "_formatMessage", w.method("TestCase", "_formatMessage", { py_positional("msg"), py_positional("standardMsg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py standardMsg = a[2];
                        if (!self.attr("longMessage").truthy())
                        {
                            return [&]() -> py { py const t = msg; return t.truthy() ? t : standardMsg; }();
                        }
                        if (msg.is_none())
                        {
                            return standardMsg;
                        }
                        try
                        {
                            // don't switch to '{}' formatting in Python 2.X
                            // it changes the way unicode input is handled
                            return (S<"%s : %s">()) % py_tuple({ standardMsg, msg });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler39;
                            if (matches(handler39.exception, B<"UnicodeDecodeError">()))
                            {
                                return (S<"%s : %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(standardMsg), (Mod<"unittest.util">().attr("safe_repr"))(msg) });
                            }
                            else
                                py_reraise(handler39.exception);
                        }
                        return none();
                    }) },
                { "assertRaises", w.method("TestCase", "assertRaises", { py_positional("expected_exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_exception = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        py context;
                        context = (G<"unittest.case._AssertRaisesContext">())(expected_exception, self);
                        {
                            std::optional<py> pending40;
                            [[maybe_unused]] int flow40 = 0;
                            [[maybe_unused]] py result40;
                            try
                            {
                                result40 = context.method("handle", { S<"assertRaises">(), args, kwargs });
                                flow40 = 1;
                                goto finally40;
                            }
                            catch (raised const&)
                            {
                                pending40 = py_take_exception();
                            }
                            finally40:
                            // bpo-23890: manually break a reference cycle
                            context = none();
                            if (pending40)
                                py_reraise(*pending40);
                            if (flow40 == 1)
                                return result40;
                        }
                        return none();
                    }, true, true) },
                { "assertWarns", w.method("TestCase", "assertWarns", { py_positional("expected_warning") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_warning = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        py context;
                        context = (G<"unittest.case._AssertWarnsContext">())(expected_warning, self);
                        return context.method("handle", { S<"assertWarns">(), args, kwargs });
                    }, true, true) },
                { "_assertNotWarns", w.method("TestCase", "_assertNotWarns", { py_positional("expected_warning") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_warning = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        py context;
                        context = (G<"unittest.case._AssertNotWarnsContext">())(expected_warning, self);
                        return context.method("handle", { S<"_assertNotWarns">(), args, kwargs });
                    }, true, true) },
                { "assertLogs", w.method("TestCase", "assertLogs", { py_positional("logger", none()), py_positional("level", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py logger = a[1];
                        py level = a[2];
                        py _AssertLogsContext;
                        // Lazy import to avoid importing logging if it is not needed.
                        _AssertLogsContext = M("unittest._log", "_AssertLogsContext");
                        return _AssertLogsContext.call({ self, logger, level }, { { "no_logs", boolean(false) } });
                    }) },
                { "assertNoLogs", w.method("TestCase", "assertNoLogs", { py_positional("logger", none()), py_positional("level", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py logger = a[1];
                        py level = a[2];
                        py _AssertLogsContext;
                        _AssertLogsContext = M("unittest._log", "_AssertLogsContext");
                        return _AssertLogsContext.call({ self, logger, level }, { { "no_logs", boolean(true) } });
                    }) },
                { "_getAssertEqualityFunc", w.method("TestCase", "_getAssertEqualityFunc", { py_positional("first"), py_positional("second") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py asserter;
                        // 
                        // NOTE(gregory.p.smith): I considered isinstance(first, type(second))
                        // and vice versa.  I opted for the conservative approach in case
                        // subclasses are not intended to be compared in detail to their super
                        // class instances using a type equality func.  This means testing
                        // subtypes won't automagically use the detailed comparison.  Callers
                        // should use their type specific assertSpamEqual method to compare
                        // subclasses if the detailed comparison is desired and appropriate.
                        // See the discussion in http://bugs.python.org/issue2578.
                        // 
                        if ((B<"type">())(first).is((B<"type">())(second)))
                        {
                            asserter = self.attr("_type_equality_funcs").method("get", { (B<"type">())(first) });
                            if (!asserter.is_none())
                            {
                                if (isinstance(asserter, B<"str">()))
                                {
                                    asserter = (B<"getattr">())(self, asserter);
                                }
                                return asserter;
                            }
                        }
                        return self.attr("_baseAssertEqual");
                    }) },
                { "_baseAssertEqual", w.method("TestCase", "_baseAssertEqual", { py_positional("first"), py_positional("second"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!(first == second))
                        {
                            standardMsg = (S<"%s != %s">()) % (Mod<"unittest.util">().attr("_common_shorten_repr"))(first, second);
                            msg = self.method("_formatMessage", { msg, standardMsg });
                            py_raise(self.method("failureException", { msg }));
                        }
                        return none();
                    }) },
                { "assertEqual", w.method("TestCase", "assertEqual", { py_positional("first"), py_positional("second"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py msg = a[3];
                        py assertion_func;
                        assertion_func = self.method("_getAssertEqualityFunc", { first, second });
                        assertion_func.call({ first, second }, { { "msg", msg } });
                        return none();
                    }) },
                { "assertNotEqual", w.method("TestCase", "assertNotEqual", { py_positional("first"), py_positional("second"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py msg = a[3];
                        if (!(first != second))
                        {
                            msg = self.method("_formatMessage", { msg, (S<"%s == %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(first), (Mod<"unittest.util">().attr("safe_repr"))(second) }) });
                            py_raise(self.method("failureException", { msg }));
                        }
                        return none();
                    }) },
                { "assertAlmostEqual", w.method("TestCase", "assertAlmostEqual", { py_positional("first"), py_positional("second"), py_positional("places", none()), py_positional("msg", none()), py_positional("delta", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py places = a[3];
                        py msg = a[4];
                        py delta = a[5];
                        py diff;
                        py standardMsg;
                        if (first == second)
                        {
                            // shortcut
                            return none();
                        }
                        if (((!delta.is_none()) && (!places.is_none())))
                        {
                            py_raise((B<"TypeError">())(S<"specify delta or places not both">()));
                        }
                        diff = (B<"abs">())(first - second);
                        if (!delta.is_none())
                        {
                            if (diff <= delta)
                            {
                                return none();
                            }
                            standardMsg = (S<"%s != %s within %s delta (%s difference)">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(first), (Mod<"unittest.util">().attr("safe_repr"))(second), (Mod<"unittest.util">().attr("safe_repr"))(delta), (Mod<"unittest.util">().attr("safe_repr"))(diff) });
                        }
                        else
                        {
                            if (places.is_none())
                            {
                                places = I<7>();
                            }
                            if ((B<"round">())(diff, places) == (I<0>()))
                            {
                                return none();
                            }
                            standardMsg = (S<"%s != %s within %r places (%s difference)">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(first), (Mod<"unittest.util">().attr("safe_repr"))(second), places, (Mod<"unittest.util">().attr("safe_repr"))(diff) });
                        }
                        msg = self.method("_formatMessage", { msg, standardMsg });
                        py_raise(self.method("failureException", { msg }));
                    }) },
                { "assertNotAlmostEqual", w.method("TestCase", "assertNotAlmostEqual", { py_positional("first"), py_positional("second"), py_positional("places", none()), py_positional("msg", none()), py_positional("delta", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py places = a[3];
                        py msg = a[4];
                        py delta = a[5];
                        py diff;
                        py standardMsg;
                        if (((!delta.is_none()) && (!places.is_none())))
                        {
                            py_raise((B<"TypeError">())(S<"specify delta or places not both">()));
                        }
                        diff = (B<"abs">())(first - second);
                        if (!delta.is_none())
                        {
                            if (((!(first == second)) && (diff > delta)))
                            {
                                return none();
                            }
                            standardMsg = (S<"%s == %s within %s delta (%s difference)">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(first), (Mod<"unittest.util">().attr("safe_repr"))(second), (Mod<"unittest.util">().attr("safe_repr"))(delta), (Mod<"unittest.util">().attr("safe_repr"))(diff) });
                        }
                        else
                        {
                            if (places.is_none())
                            {
                                places = I<7>();
                            }
                            if (((!(first == second)) && ((B<"round">())(diff, places) != (I<0>()))))
                            {
                                return none();
                            }
                            standardMsg = (S<"%s == %s within %r places">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(first), (Mod<"unittest.util">().attr("safe_repr"))(second), places });
                        }
                        msg = self.method("_formatMessage", { msg, standardMsg });
                        py_raise(self.method("failureException", { msg }));
                    }) },
                { "assertSequenceEqual", w.method("TestCase", "assertSequenceEqual", { py_positional("seq1"), py_positional("seq2"), py_positional("msg", none()), py_positional("seq_type", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py seq1 = a[1];
                        py seq2 = a[2];
                        py msg = a[3];
                        py seq_type = a[4];
                        py diffMsg;
                        py differing;
                        py i;
                        py item1;
                        py item2;
                        py len1;
                        py len2;
                        py seq_type_name;
                        py standardMsg;
                        if (!seq_type.is_none())
                        {
                            seq_type_name = seq_type.attr("__name__");
                            if (!isinstance(seq1, seq_type))
                            {
                                py_raise(self.method("failureException", { (S<"First sequence is not a %s: %s">()) % py_tuple({ seq_type_name, (Mod<"unittest.util">().attr("safe_repr"))(seq1) }) }));
                            }
                            if (!isinstance(seq2, seq_type))
                            {
                                py_raise(self.method("failureException", { (S<"Second sequence is not a %s: %s">()) % py_tuple({ seq_type_name, (Mod<"unittest.util">().attr("safe_repr"))(seq2) }) }));
                            }
                        }
                        else
                        {
                            seq_type_name = S<"sequence">();
                        }
                        differing = none();
                        try
                        {
                            len1 = py{ seq1.size() };
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler41;
                            if (matches(handler41.exception, py_tuple({ B<"TypeError">(), B<"NotImplementedError">() })))
                            {
                                differing = (S<"First %s has no length.    Non-sequence?">()) % seq_type_name;
                            }
                            else
                                py_reraise(handler41.exception);
                        }
                        if (differing.is_none())
                        {
                            try
                            {
                                len2 = py{ seq2.size() };
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler42;
                                if (matches(handler42.exception, py_tuple({ B<"TypeError">(), B<"NotImplementedError">() })))
                                {
                                    differing = (S<"Second %s has no length.    Non-sequence?">()) % seq_type_name;
                                }
                                else
                                    py_reraise(handler42.exception);
                            }
                        }
                        if (differing.is_none())
                        {
                            if (seq1 == seq2)
                            {
                                return none();
                            }
                            differing = (S<"%ss differ: %s != %s\n">()) % (py_tuple({ seq_type_name.method("capitalize") }) + (Mod<"unittest.util">().attr("_common_shorten_repr"))(seq1, seq2));
                            bool broke43 = false;
                            for (py const& item : iterate{ (B<"range">())((B<"min">())(len1, len2)) })
                            {
                                i = item;
                                try
                                {
                                    item1 = seq1[i];
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler44;
                                    if (matches(handler44.exception, py_tuple({ B<"TypeError">(), B<"IndexError">(), B<"NotImplementedError">() })))
                                    {
                                        differing = inplace(NEOS_PY_ADD, differing, (S<"\nUnable to index element %d of first %s\n">()) % py_tuple({ i, seq_type_name }));
                                        broke43 = true;
                                        break;
                                    }
                                    else
                                        py_reraise(handler44.exception);
                                }
                                try
                                {
                                    item2 = seq2[i];
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler45;
                                    if (matches(handler45.exception, py_tuple({ B<"TypeError">(), B<"IndexError">(), B<"NotImplementedError">() })))
                                    {
                                        differing = inplace(NEOS_PY_ADD, differing, (S<"\nUnable to index element %d of second %s\n">()) % py_tuple({ i, seq_type_name }));
                                        broke43 = true;
                                        break;
                                    }
                                    else
                                        py_reraise(handler45.exception);
                                }
                                if (item1 != item2)
                                {
                                    differing = inplace(NEOS_PY_ADD, differing, (S<"\nFirst differing element %d:\n%s\n%s\n">()) % (py_tuple({ i }) + (Mod<"unittest.util">().attr("_common_shorten_repr"))(item1, item2)));
                                    broke43 = true;
                                    break;
                                }
                            }
                            if (!broke43)
                            {
                                if (((len1 == len2) && seq_type.is_none() && ((B<"type">())(seq1) != (B<"type">())(seq2))))
                                {
                                    // The sequences are the same, but have differing types.
                                    return none();
                                }
                            }
                            if (len1 > len2)
                            {
                                differing = inplace(NEOS_PY_ADD, differing, (S<"\nFirst %s contains %d additional elements.\n">()) % py_tuple({ seq_type_name, len1 - len2 }));
                                try
                                {
                                    differing = inplace(NEOS_PY_ADD, differing, (S<"First extra element %d:\n%s\n">()) % py_tuple({ len2, (Mod<"unittest.util">().attr("safe_repr"))(seq1[len2]) }));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler46;
                                    if (matches(handler46.exception, py_tuple({ B<"TypeError">(), B<"IndexError">(), B<"NotImplementedError">() })))
                                    {
                                        differing = inplace(NEOS_PY_ADD, differing, (S<"Unable to index element %d of first %s\n">()) % py_tuple({ len2, seq_type_name }));
                                    }
                                    else
                                        py_reraise(handler46.exception);
                                }
                            }
                            else if (len1 < len2)
                            {
                                differing = inplace(NEOS_PY_ADD, differing, (S<"\nSecond %s contains %d additional elements.\n">()) % py_tuple({ seq_type_name, len2 - len1 }));
                                try
                                {
                                    differing = inplace(NEOS_PY_ADD, differing, (S<"First extra element %d:\n%s\n">()) % py_tuple({ len1, (Mod<"unittest.util">().attr("safe_repr"))(seq2[len1]) }));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler47;
                                    if (matches(handler47.exception, py_tuple({ B<"TypeError">(), B<"IndexError">(), B<"NotImplementedError">() })))
                                    {
                                        differing = inplace(NEOS_PY_ADD, differing, (S<"Unable to index element %d of second %s\n">()) % py_tuple({ len1, seq_type_name }));
                                    }
                                    else
                                        py_reraise(handler47.exception);
                                }
                            }
                        }
                        standardMsg = differing;
                        diffMsg = (S<"\n">()) + (S<"\n">()).method("join", { (Mod<"difflib">().attr("ndiff"))((Mod<"pprint">().attr("pformat"))(seq1).method("splitlines"), (Mod<"pprint">().attr("pformat"))(seq2).method("splitlines")) });
                        standardMsg = self.method("_truncateMessage", { standardMsg, diffMsg });
                        msg = self.method("_formatMessage", { msg, standardMsg });
                        self.method("fail", { msg });
                        return none();
                    }) },
                { "_truncateMessage", w.method("TestCase", "_truncateMessage", { py_positional("message"), py_positional("diff") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py diff = a[2];
                        py max_diff;
                        max_diff = self.attr("maxDiff");
                        if ((max_diff.is_none() || (py{ diff.size() } <= max_diff)))
                        {
                            return message + diff;
                        }
                        return message + ((G<"unittest.case.DIFF_OMITTED">()) % py{ diff.size() });
                    }) },
                { "assertListEqual", w.method("TestCase", "assertListEqual", { py_positional("list1"), py_positional("list2"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py list1 = a[1];
                        py list2 = a[2];
                        py msg = a[3];
                        self.method("assertSequenceEqual", { list1, list2, msg }, { { "seq_type", B<"list">() } });
                        return none();
                    }) },
                { "assertTupleEqual", w.method("TestCase", "assertTupleEqual", { py_positional("tuple1"), py_positional("tuple2"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tuple1 = a[1];
                        py tuple2 = a[2];
                        py msg = a[3];
                        self.method("assertSequenceEqual", { tuple1, tuple2, msg }, { { "seq_type", B<"tuple">() } });
                        return none();
                    }) },
                { "assertSetEqual", w.method("TestCase", "assertSetEqual", { py_positional("set1"), py_positional("set2"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py set1 = a[1];
                        py set2 = a[2];
                        py msg = a[3];
                        py difference1;
                        py difference2;
                        py e;
                        py item_;
                        py lines;
                        py standardMsg;
                        try
                        {
                            difference1 = set1.method("difference", { set2 });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler48;
                            if (matches(handler48.exception, B<"TypeError">()))
                            {
                                e = handler48.exception;
                                self.method("fail", { (S<"invalid type when attempting set difference: %s">()) % e });
                            }
                            else if (matches(handler48.exception, B<"AttributeError">()))
                            {
                                e = handler48.exception;
                                self.method("fail", { (S<"first argument does not support set difference: %s">()) % e });
                            }
                            else
                                py_reraise(handler48.exception);
                        }
                        try
                        {
                            difference2 = set2.method("difference", { set1 });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler49;
                            if (matches(handler49.exception, B<"TypeError">()))
                            {
                                e = handler49.exception;
                                self.method("fail", { (S<"invalid type when attempting set difference: %s">()) % e });
                            }
                            else if (matches(handler49.exception, B<"AttributeError">()))
                            {
                                e = handler49.exception;
                                self.method("fail", { (S<"second argument does not support set difference: %s">()) % e });
                            }
                            else
                                py_reraise(handler49.exception);
                        }
                        if (!([&]() -> py { py const t = difference1; return t.truthy() ? t : difference2; }()).truthy())
                        {
                            return none();
                        }
                        lines = py_list();
                        if (difference1.truthy())
                        {
                            lines.method("append", { S<"Items in the first set but not the second:">() });
                            for (py const& item : iterate{ difference1 })
                            {
                                item_ = item;
                                lines.method("append", { S(item_.repr()) });
                            }
                        }
                        if (difference2.truthy())
                        {
                            lines.method("append", { S<"Items in the second set but not the first:">() });
                            for (py const& item : iterate{ difference2 })
                            {
                                item_ = item;
                                lines.method("append", { S(item_.repr()) });
                            }
                        }
                        standardMsg = (S<"\n">()).method("join", { lines });
                        self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        return none();
                    }) },
                { "assertIn", w.method("TestCase", "assertIn", { py_positional("member"), py_positional("container"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py container = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!container.contains(member))
                        {
                            standardMsg = (S<"%s not found in %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(member), (Mod<"unittest.util">().attr("safe_repr"))(container) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertNotIn", w.method("TestCase", "assertNotIn", { py_positional("member"), py_positional("container"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py member = a[1];
                        py container = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (container.contains(member))
                        {
                            standardMsg = (S<"%s unexpectedly found in %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(member), (Mod<"unittest.util">().attr("safe_repr"))(container) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertIs", w.method("TestCase", "assertIs", { py_positional("expr1"), py_positional("expr2"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expr1 = a[1];
                        py expr2 = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!expr1.is(expr2))
                        {
                            standardMsg = (S<"%s is not %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(expr1), (Mod<"unittest.util">().attr("safe_repr"))(expr2) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertIsNot", w.method("TestCase", "assertIsNot", { py_positional("expr1"), py_positional("expr2"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expr1 = a[1];
                        py expr2 = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (expr1.is(expr2))
                        {
                            standardMsg = (S<"unexpectedly identical: %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(expr1) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertDictEqual", w.method("TestCase", "assertDictEqual", { py_positional("d1"), py_positional("d2"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py d1 = a[1];
                        py d2 = a[2];
                        py msg = a[3];
                        py diff;
                        py standardMsg;
                        self.method("assertIsInstance", { d1, B<"dict">(), S<"First argument is not a dictionary">() });
                        self.method("assertIsInstance", { d2, B<"dict">(), S<"Second argument is not a dictionary">() });
                        if (d1 != d2)
                        {
                            standardMsg = (S<"%s != %s">()) % (Mod<"unittest.util">().attr("_common_shorten_repr"))(d1, d2);
                            diff = (S<"\n">()) + (S<"\n">()).method("join", { (Mod<"difflib">().attr("ndiff"))((Mod<"pprint">().attr("pformat"))(d1).method("splitlines"), (Mod<"pprint">().attr("pformat"))(d2).method("splitlines")) });
                            standardMsg = self.method("_truncateMessage", { standardMsg, diff });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertCountEqual", w.method("TestCase", "assertCountEqual", { py_positional("first"), py_positional("second"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py msg = a[3];
                        py diffMsg;
                        py differences;
                        py first_seq;
                        py lines;
                        py second_seq;
                        py standardMsg;
                        auto const items52 = unpack(py_tuple({ (B<"list">())(first), (B<"list">())(second) }), 2);
                        first_seq = items52[0];
                        second_seq = items52[1];
                        {
                            bool otherwise53 = false;
                            try
                            {
                                first = (Mod<"collections">().attr("Counter"))(first_seq);
                                second = (Mod<"collections">().attr("Counter"))(second_seq);
                                otherwise53 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler53;
                                if (matches(handler53.exception, B<"TypeError">()))
                                {
                                    // Handle case with unhashable elements
                                    differences = (Mod<"unittest.util">().attr("_count_diff_all_purpose"))(first_seq, second_seq);
                                }
                                else
                                    py_reraise(handler53.exception);
                            }
                            if (otherwise53)
                            {
                                if (first == second)
                                {
                                    return none();
                                }
                                differences = (Mod<"unittest.util">().attr("_count_diff_hashable"))(first_seq, second_seq);
                            }
                        }
                        if (differences.truthy())
                        {
                            standardMsg = S<"Element counts were not equal:\n">();
                            lines = [&]() -> py { py const comprehension54 = py_list(); py diff; for (py const& item : iterate{ differences }) { diff = item; comprehension54.method("append", { (S<"First has %d, Second has %d:  %r">()) % diff }); } return comprehension54; }();
                            diffMsg = (S<"\n">()).method("join", { lines });
                            standardMsg = self.method("_truncateMessage", { standardMsg, diffMsg });
                            msg = self.method("_formatMessage", { msg, standardMsg });
                            self.method("fail", { msg });
                        }
                        return none();
                    }) },
                { "assertMultiLineEqual", w.method("TestCase", "assertMultiLineEqual", { py_positional("first"), py_positional("second"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py first = a[1];
                        py second = a[2];
                        py msg = a[3];
                        py diff;
                        py first_presplit;
                        py firstlines;
                        py second_presplit;
                        py secondlines;
                        py standardMsg;
                        self.method("assertIsInstance", { first, B<"str">(), S<"First argument is not a string">() });
                        self.method("assertIsInstance", { second, B<"str">(), S<"Second argument is not a string">() });
                        if (first != second)
                        {
                            // Don't use difflib if the strings are too long
                            if (((py{ first.size() } > self.attr("_diffThreshold")) || (py{ second.size() } > self.attr("_diffThreshold"))))
                            {
                                self.method("_baseAssertEqual", { first, second, msg });
                            }
                            // Append \n to both strings if either is missing the \n.
                            // This allows the final ndiff to show the \n difference. The
                            // exception here is if the string is empty, in which case no
                            // \n should be added
                            first_presplit = first;
                            second_presplit = second;
                            if ((first.truthy() && second.truthy()))
                            {
                                if (((first[I<-1>()] != (S<"\n">())) || (second[I<-1>()] != (S<"\n">()))))
                                {
                                    first_presplit = inplace(NEOS_PY_ADD, first_presplit, S<"\n">());
                                    second_presplit = inplace(NEOS_PY_ADD, second_presplit, S<"\n">());
                                }
                            }
                            else if ((second.truthy() && (second[I<-1>()] != (S<"\n">()))))
                            {
                                second_presplit = inplace(NEOS_PY_ADD, second_presplit, S<"\n">());
                            }
                            else if ((first.truthy() && (first[I<-1>()] != (S<"\n">()))))
                            {
                                first_presplit = inplace(NEOS_PY_ADD, first_presplit, S<"\n">());
                            }
                            firstlines = first_presplit.method("splitlines", {  }, { { "keepends", boolean(true) } });
                            secondlines = second_presplit.method("splitlines", {  }, { { "keepends", boolean(true) } });
                            // Generate the message and diff, then raise the exception
                            standardMsg = (S<"%s != %s">()) % (Mod<"unittest.util">().attr("_common_shorten_repr"))(first, second);
                            diff = (S<"\n">()) + (S<"">()).method("join", { (Mod<"difflib">().attr("ndiff"))(firstlines, secondlines) });
                            standardMsg = self.method("_truncateMessage", { standardMsg, diff });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertLess", w.method("TestCase", "assertLess", { py_positional("a"), py_positional("b"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!(a_ < b))
                        {
                            standardMsg = (S<"%s not less than %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(a_), (Mod<"unittest.util">().attr("safe_repr"))(b) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertLessEqual", w.method("TestCase", "assertLessEqual", { py_positional("a"), py_positional("b"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!(a_ <= b))
                        {
                            standardMsg = (S<"%s not less than or equal to %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(a_), (Mod<"unittest.util">().attr("safe_repr"))(b) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertGreater", w.method("TestCase", "assertGreater", { py_positional("a"), py_positional("b"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!(a_ > b))
                        {
                            standardMsg = (S<"%s not greater than %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(a_), (Mod<"unittest.util">().attr("safe_repr"))(b) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertGreaterEqual", w.method("TestCase", "assertGreaterEqual", { py_positional("a"), py_positional("b"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!(a_ >= b))
                        {
                            standardMsg = (S<"%s not greater than or equal to %s">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(a_), (Mod<"unittest.util">().attr("safe_repr"))(b) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertIsNone", w.method("TestCase", "assertIsNone", { py_positional("obj"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py msg = a[2];
                        py standardMsg;
                        if (!obj.is_none())
                        {
                            standardMsg = (S<"%s is not None">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(obj) });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertIsNotNone", w.method("TestCase", "assertIsNotNone", { py_positional("obj"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py msg = a[2];
                        py standardMsg;
                        if (obj.is_none())
                        {
                            standardMsg = S<"unexpectedly None">();
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertIsInstance", w.method("TestCase", "assertIsInstance", { py_positional("obj"), py_positional("cls"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py cls = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (!isinstance(obj, cls))
                        {
                            standardMsg = (S<"%s is not an instance of %r">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(obj), cls });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertNotIsInstance", w.method("TestCase", "assertNotIsInstance", { py_positional("obj"), py_positional("cls"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py cls = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (isinstance(obj, cls))
                        {
                            standardMsg = (S<"%s is an instance of %r">()) % py_tuple({ (Mod<"unittest.util">().attr("safe_repr"))(obj), cls });
                            self.method("fail", { self.method("_formatMessage", { msg, standardMsg }) });
                        }
                        return none();
                    }) },
                { "assertRaisesRegex", w.method("TestCase", "assertRaisesRegex", { py_positional("expected_exception"), py_positional("expected_regex") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_exception = a[1];
                        py expected_regex = a[2];
                        py args = a[3];
                        py kwargs = a[4];
                        py context;
                        context = (G<"unittest.case._AssertRaisesContext">())(expected_exception, self, expected_regex);
                        return context.method("handle", { S<"assertRaisesRegex">(), args, kwargs });
                    }, true, true) },
                { "assertWarnsRegex", w.method("TestCase", "assertWarnsRegex", { py_positional("expected_warning"), py_positional("expected_regex") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_warning = a[1];
                        py expected_regex = a[2];
                        py args = a[3];
                        py kwargs = a[4];
                        py context;
                        context = (G<"unittest.case._AssertWarnsContext">())(expected_warning, self, expected_regex);
                        return context.method("handle", { S<"assertWarnsRegex">(), args, kwargs });
                    }, true, true) },
                { "assertRegex", w.method("TestCase", "assertRegex", { py_positional("text"), py_positional("expected_regex"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py expected_regex = a[2];
                        py msg = a[3];
                        py standardMsg;
                        if (isinstance(expected_regex, py_tuple({ B<"str">(), B<"bytes">() })))
                        {
                            if (!expected_regex.truthy())
                                py_raise(B<"AssertionError">()(S<"expected_regex must not be empty.">()));
                            expected_regex = (Mod<"re">().attr("compile"))(expected_regex);
                        }
                        if (!expected_regex.method("search", { text }).truthy())
                        {
                            standardMsg = (S<"Regex didn't match: %r not found in %r">()) % py_tuple({ expected_regex.attr("pattern"), text });
                            // _formatMessage ensures the longMessage option is respected
                            msg = self.method("_formatMessage", { msg, standardMsg });
                            py_raise(self.method("failureException", { msg }));
                        }
                        return none();
                    }) },
                { "assertNotRegex", w.method("TestCase", "assertNotRegex", { py_positional("text"), py_positional("unexpected_regex"), py_positional("msg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py unexpected_regex = a[2];
                        py msg = a[3];
                        py match;
                        py standardMsg;
                        if (isinstance(unexpected_regex, py_tuple({ B<"str">(), B<"bytes">() })))
                        {
                            unexpected_regex = (Mod<"re">().attr("compile"))(unexpected_regex);
                        }
                        match = unexpected_regex.method("search", { text });
                        if (match.truthy())
                        {
                            standardMsg = (S<"Regex matched: %r matches %r in %r">()) % py_tuple({ text[B<"slice">()(match.method("start"), match.method("end"), none())], unexpected_regex.attr("pattern"), text });
                            // _formatMessage ensures the longMessage option is respected
                            msg = self.method("_formatMessage", { msg, standardMsg });
                            py_raise(self.method("failureException", { msg }));
                        }
                        return none();
                    }) }, }));

            define("unittest.case", "FunctionTestCase", w.make_class("FunctionTestCase", { G<"unittest.case.TestCase">() }, {
                { "__init__", w.method("FunctionTestCase", "__init__", { py_positional("testFunc"), py_positional("setUp", none()), py_positional("tearDown", none()), py_positional("description", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py testFunc = a[1];
                        py setUp = a[2];
                        py tearDown = a[3];
                        py description = a[4];
                        (B<"super">())(G<"unittest.case.FunctionTestCase">(), self).method("__init__");
                        self.set_attr("_setUpFunc", setUp);
                        self.set_attr("_tearDownFunc", tearDown);
                        self.set_attr("_testFunc", testFunc);
                        self.set_attr("_description", description);
                        return none();
                    }) },
                { "setUp", w.method("FunctionTestCase", "setUp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_setUpFunc").is_none())
                        {
                            self.method("_setUpFunc");
                        }
                        return none();
                    }) },
                { "tearDown", w.method("FunctionTestCase", "tearDown", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_tearDownFunc").is_none())
                        {
                            self.method("_tearDownFunc");
                        }
                        return none();
                    }) },
                { "runTest", w.method("FunctionTestCase", "runTest", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_testFunc");
                        return none();
                    }) },
                { "id", w.method("FunctionTestCase", "id", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_testFunc").attr("__name__");
                    }) },
                { "__eq__", w.method("FunctionTestCase", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, self.attr("__class__")))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return [&]() -> py { py const t = boolean(self.attr("_setUpFunc") == other.attr("_setUpFunc")); return !t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("_tearDownFunc") == other.attr("_tearDownFunc")); return !t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("_testFunc") == other.attr("_testFunc")); return !t.truthy() ? t : boolean(self.attr("_description") == other.attr("_description")); }(); }(); }();
                    }) },
                { "__hash__", w.method("FunctionTestCase", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(py_tuple({ (B<"type">())(self), self.attr("_setUpFunc"), self.attr("_tearDownFunc"), self.attr("_testFunc"), self.attr("_description") }));
                    }) },
                { "__str__", w.method("FunctionTestCase", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s (%s)">()) % py_tuple({ (Mod<"unittest.util">().attr("strclass"))(self.attr("__class__")), self.attr("_testFunc").attr("__name__") });
                    }) },
                { "__repr__", w.method("FunctionTestCase", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s tec=%s>">()) % py_tuple({ (Mod<"unittest.util">().attr("strclass"))(self.attr("__class__")), self.attr("_testFunc") });
                    }) },
                { "shortDescription", w.method("FunctionTestCase", "shortDescription", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py doc;
                        if (!self.attr("_description").is_none())
                        {
                            return self.attr("_description");
                        }
                        doc = self.attr("_testFunc").attr("__doc__");
                        return [&]() -> py { py const t = [&]() -> py { py const t = doc; return !t.truthy() ? t : doc.method("split", { S<"\n">() })[I<0>()].method("strip"); }(); return t.truthy() ? t : none(); }();
                    }) }, }));

            define("unittest.case", "_SubTest", w.make_class("_SubTest", { G<"unittest.case.TestCase">() }, {
                { "__init__", w.method("_SubTest", "__init__", { py_positional("test_case"), py_positional("message"), py_positional("params") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test_case = a[1];
                        py message = a[2];
                        py params = a[3];
                        (B<"super">()(G<"unittest.case._SubTest">(), self)).method("__init__");
                        self.set_attr("_message", message);
                        self.set_attr("test_case", test_case);
                        self.set_attr("params", params);
                        self.set_attr("failureException", test_case.attr("failureException"));
                        return none();
                    }) },
                { "runTest", w.method("_SubTest", "runTest", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"subtests cannot be run directly">()));
                    }) },
                { "_subDescription", w.method("_SubTest", "_subDescription", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py params_desc;
                        py parts;
                        parts = py_list();
                        if (!self.attr("_message").is((G<"unittest.case._subtest_msg_sentinel">())))
                        {
                            parts.method("append", { (S<"[{}]">()).method("format", { self.attr("_message") }) });
                        }
                        if (self.attr("params").truthy())
                        {
                            params_desc = (S<", ">()).method("join", { [&]() -> py { py const comprehension55 = py_list(); py k; py v; for (py const& item : iterate{ self.attr("params").method("items") }) { auto const items56 = unpack(item, 2); k = items56[0]; v = items56[1]; comprehension55.method("append", { (S<"{}={!r}">()).method("format", { k, v }) }); } return comprehension55; }() });
                            parts.method("append", { (S<"({})">()).method("format", { params_desc }) });
                        }
                        return [&]() -> py { py const t = (S<" ">()).method("join", { parts }); return t.truthy() ? t : S<"(<subtest>)">(); }();
                    }) },
                { "id", w.method("_SubTest", "id", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"{} {}">()).method("format", { self.attr("test_case").method("id"), self.method("_subDescription") });
                    }) },
                { "shortDescription", w.method("_SubTest", "shortDescription", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("test_case").method("shortDescription");
                    }) },
                { "__str__", w.method("_SubTest", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"{} {}">()).method("format", { self.attr("test_case"), self.method("_subDescription") });
                    }) }, }));
        }

        bool unittest_case_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_case();
                }
                if (auto const doc = unittest_case_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("functools", py{ fn_import("functools") }.release());
                aAttributes.emplace_back("difflib", py{ fn_import("difflib") }.release());
                aAttributes.emplace_back("pprint", py{ fn_import("pprint") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                aAttributes.emplace_back("contextlib", py{ fn_import("contextlib") }.release());
                aAttributes.emplace_back("traceback", py{ fn_import("traceback") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("types", py{ fn_import("types") }.release());
                aAttributes.emplace_back("result", py{ fn_import("unittest.result") }.release());
                aAttributes.emplace_back("strclass", import_attribute("unittest.util", "strclass").release());
                aAttributes.emplace_back("safe_repr", import_attribute("unittest.util", "safe_repr").release());
                aAttributes.emplace_back("_count_diff_all_purpose", import_attribute("unittest.util", "_count_diff_all_purpose").release());
                aAttributes.emplace_back("_count_diff_hashable", import_attribute("unittest.util", "_count_diff_hashable").release());
                aAttributes.emplace_back("_common_shorten_repr", import_attribute("unittest.util", "_common_shorten_repr").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.case", unittest_case_module);
    }
}
