/*
  unittest_async_case.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/async_case.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/async_case.py's copyright: Copyright (c) 1999-2003 Steve
  Purcell; Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.async_case (CPython 3.13's Lib/unittest/async_case.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "asyncio.hpp"
#include "unittest_async_case_docs.hpp"

namespace neos::python_runtime::detail::unittest_async_case_module
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
        using asyncio::async_method;
        using asyncio::async_function;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.async_case.name"))
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
                py_raise("SystemError", std::string{ "unittest.async_case: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.async_case.name">()))
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
            static module_writer const sWriter{ "unittest.async_case", unittest_async_case_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.async_case (Lib/unittest/async_case.py)

        void make_unittest_async_case()
        {
            static module_writer const& w = writer("unittest.async_case");
            define("unittest.async_case", "__unittest", boolean(true));
                // Names intentionally have a long prefix
                // to reduce a chance of clashing with user-defined attributes
                // from inherited test case
                // 
                // The class doesn't call loop.run_until_complete(self.setUp()) and family
                // but uses a different approach:
                // 1. create a long-running task that reads self.setUp()
                // awaitable from queue along with a future
                // 2. await the awaitable object passing in and set the result
                // into the future object
                // 3. Outer code puts the awaitable and the future object into a queue
                // with waiting for the future
                // The trick is necessary because every run_until_complete() call
                // creates a new task with embedded ContextVar context.
                // To share contextvars between setUp(), test and tearDown() we need to execute
                // them inside the same task.
                // Note: the test case modifies event loop policy if the policy was not instantiated
                // yet, unless loop_factory=asyncio.EventLoop is set.
                // asyncio.get_event_loop_policy() creates a default policy on demand but never
                // returns None
                // I believe this is not an issue in user level tests but python itself for testing
                // should reset a policy in every test module
                // by calling asyncio.set_event_loop_policy(None) in tearDownModule()
                // or set loop_factory=asyncio.EventLoop
            define("unittest.async_case", "IsolatedAsyncioTestCase", w.make_class("IsolatedAsyncioTestCase", { Mod<"unittest.case">().attr("TestCase") }, {
                { "loop_factory", none() },
                { "__init__", w.method("IsolatedAsyncioTestCase", "__init__", { py_positional("methodName", S<"runTest">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py methodName = a[1];
                        (B<"super">()(G<"unittest.async_case.IsolatedAsyncioTestCase">(), self)).method("__init__", { methodName });
                        self.set_attr("_asyncioRunner", none());
                        self.set_attr("_asyncioTestContext", (Mod<"contextvars">().attr("copy_context"))());
                        return none();
                    }) },
                { "asyncSetUp", async_method(w, "IsolatedAsyncioTestCase", "asyncSetUp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "asyncTearDown", async_method(w, "IsolatedAsyncioTestCase", "asyncTearDown", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "addAsyncCleanup", w.method("IsolatedAsyncioTestCase", "addAsyncCleanup", { py_positional_only("func") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py func = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        call_ex(self.attr("addCleanup"), {  }, B<"tuple">()(((py_list() + py_list({ func })) + B<"list">()(args))), kwargs);
                        return none();
                    }, true, true) },
                { "enterAsyncContext", async_method(w, "IsolatedAsyncioTestCase", "enterAsyncContext", { py_positional("cm") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cm = a[1];
                        py cls;
                        py enter;
                        py exit;
                        py result;
                        // We look up the special methods on the type to match the with
                        // statement.
                        cls = (B<"type">())(cm);
                        try
                        {
                            enter = cls.attr("__aenter__");
                            exit = cls.attr("__aexit__");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"AttributeError">()))
                            {
                                fn_raise_from((B<"TypeError">())(S(std::string{ "'" } + cls.attr("__module__").str() + "." + cls.attr("__qualname__").str() + "' object does not support the asynchronous context manager protocol")).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        result = py_await(enter(cm));
                        self.method("addAsyncCleanup", { exit, cm, none(), none(), none() });
                        return result;
                    }) },
                { "_callSetUp", w.method("IsolatedAsyncioTestCase", "_callSetUp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_asyncioRunner").method("get_loop");
                        self.attr("_asyncioTestContext").method("run", { self.attr("setUp") });
                        self.method("_callAsync", { self.attr("asyncSetUp") });
                        return none();
                    }) },
                { "_callTestMethod", w.method("IsolatedAsyncioTestCase", "_callTestMethod", { py_positional("method") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method = a[1];
                        if (!self.method("_callMaybeAsync", { method }).is_none())
                        {
                            (Mod<"warnings">().attr("warn")).call({ S(std::string{ "It is deprecated to return a value that is not None from a test case (" } + method.str() + ")"), B<"DeprecationWarning">() }, { { "stacklevel", I<3>() } });
                        }
                        return none();
                    }) },
                { "_callTearDown", w.method("IsolatedAsyncioTestCase", "_callTearDown", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_callAsync", { self.attr("asyncTearDown") });
                        self.attr("_asyncioTestContext").method("run", { self.attr("tearDown") });
                        return none();
                    }) },
                { "_callCleanup", w.method("IsolatedAsyncioTestCase", "_callCleanup", { py_positional("function") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py function = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        call_ex(self.attr("_callMaybeAsync"), { function }, args, kwargs);
                        return none();
                    }, true, true) },
                { "_callAsync", w.method("IsolatedAsyncioTestCase", "_callAsync", { py_positional_only("func") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py func = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        if (self.attr("_asyncioRunner").is_none())
                            py_raise(B<"AssertionError">()(S<"asyncio runner is not initialized">()));
                        if (!(Mod<"inspect">().attr("iscoroutinefunction"))(func).truthy())
                            py_raise(B<"AssertionError">()(S(func.repr() + " is not an async function")));
                        return self.attr("_asyncioRunner").method("run", { call_ex(func, {  }, args, kwargs) }, { { "context", self.attr("_asyncioTestContext") } });
                    }, true, true) },
                { "_callMaybeAsync", w.method("IsolatedAsyncioTestCase", "_callMaybeAsync", { py_positional_only("func") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py func = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        if (self.attr("_asyncioRunner").is_none())
                            py_raise(B<"AssertionError">()(S<"asyncio runner is not initialized">()));
                        if ((Mod<"inspect">().attr("iscoroutinefunction"))(func).truthy())
                        {
                            return self.attr("_asyncioRunner").method("run", { call_ex(func, {  }, args, kwargs) }, { { "context", self.attr("_asyncioTestContext") } });
                        }
                        else
                        {
                            return call_ex(self.attr("_asyncioTestContext").attr("run"), { func }, args, kwargs);
                        }
                        return none();
                    }, true, true) },
                { "_setupAsyncioRunner", w.method("IsolatedAsyncioTestCase", "_setupAsyncioRunner", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py runner;
                        if (!self.attr("_asyncioRunner").is_none())
                            py_raise(B<"AssertionError">()(S<"asyncio runner is already initialized">()));
                        runner = (Mod<"asyncio">().attr("Runner")).call({  }, { { "debug", boolean(true) }, { "loop_factory", self.attr("loop_factory") } });
                        self.set_attr("_asyncioRunner", runner);
                        return none();
                    }) },
                { "_tearDownAsyncioRunner", w.method("IsolatedAsyncioTestCase", "_tearDownAsyncioRunner", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py runner;
                        runner = self.attr("_asyncioRunner");
                        runner.method("close");
                        return none();
                    }) },
                { "run", w.method("IsolatedAsyncioTestCase", "run", { py_positional("result", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result = a[1];
                        self.method("_setupAsyncioRunner");
                        {
                            std::optional<py> pending2;
                            [[maybe_unused]] int flow2 = 0;
                            [[maybe_unused]] py result2;
                            try
                            {
                                result2 = (B<"super">()(G<"unittest.async_case.IsolatedAsyncioTestCase">(), self)).method("run", { result });
                                flow2 = 1;
                                goto finally2;
                            }
                            catch (raised const&)
                            {
                                pending2 = py_take_exception();
                            }
                            finally2:
                            self.method("_tearDownAsyncioRunner");
                            if (pending2)
                                py_reraise(*pending2);
                            if (flow2 == 1)
                                return result2;
                        }
                        return none();
                    }) },
                { "debug", w.method("IsolatedAsyncioTestCase", "debug", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_setupAsyncioRunner");
                        (B<"super">()(G<"unittest.async_case.IsolatedAsyncioTestCase">(), self)).method("debug");
                        self.method("_tearDownAsyncioRunner");
                        return none();
                    }) },
                { "__del__", w.method("IsolatedAsyncioTestCase", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_asyncioRunner").is_none())
                        {
                            self.method("_tearDownAsyncioRunner");
                        }
                        return none();
                    }) }, }));
        }

        bool unittest_async_case_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_async_case();
                }
                if (auto const doc = unittest_async_case_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("asyncio", py{ fn_import("asyncio") }.release());
                aAttributes.emplace_back("contextvars", py{ fn_import("contextvars") }.release());
                aAttributes.emplace_back("inspect", py{ fn_import("inspect") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("TestCase", import_attribute("unittest.case", "TestCase").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.async_case", unittest_async_case_module);
    }
}
