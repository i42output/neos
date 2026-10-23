/*
  unittest.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/__init__.py's copyright: Copyright (c) 1999-2003 Steve
  Purcell; Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest (CPython 3.13's Lib/unittest/__init__.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_docs.hpp"

namespace neos::python_runtime::detail::unittest_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.name"))
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
                py_raise("SystemError", std::string{ "unittest: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.name">()))
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
            static module_writer const sWriter{ "unittest", unittest_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest (Lib/unittest/__init__.py)

        void make_unittest()
        {
            static module_writer const& w = writer("unittest");
            define("unittest", "__all__", py_list({ S<"TestResult">(), S<"TestCase">(), S<"IsolatedAsyncioTestCase">(), S<"TestSuite">(), S<"TextTestRunner">(), S<"TestLoader">(), S<"FunctionTestCase">(), S<"main">(), S<"defaultTestLoader">(), S<"SkipTest">(), S<"skip">(), S<"skipIf">(), S<"skipUnless">(), S<"expectedFailure">(), S<"TextTestResult">(), S<"installHandler">(), S<"registerResult">(), S<"removeResult">(), S<"removeHandler">(), S<"addModuleCleanup">(), S<"doModuleCleanups">(), S<"enterModuleContext">() }));
            define("unittest", "__unittest", boolean(true));
            // IsolatedAsyncioTestCase will be imported lazily.
            // Lazy import of IsolatedAsyncioTestCase from .async_case
            // It imports asyncio, which is relatively heavy, but most tests
            // do not need it.
            define("unittest", "__getattr__", w.function("__getattr__", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    if (name == (S<"IsolatedAsyncioTestCase">()))
                    {
                        set_global("unittest", "IsolatedAsyncioTestCase", M("unittest.async_case", "IsolatedAsyncioTestCase"));
                        return G<"unittest.IsolatedAsyncioTestCase">();
                    }
                    py_raise((B<"AttributeError">())(S(std::string{ "module " } + (S<"unittest">()).repr() + " has no attribute " + name.repr())));
                }));
        }

        bool unittest_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest();
                }
                if (auto const doc = unittest_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("TestResult", import_attribute("unittest.result", "TestResult").release());
                aAttributes.emplace_back("addModuleCleanup", import_attribute("unittest.case", "addModuleCleanup").release());
                aAttributes.emplace_back("TestCase", import_attribute("unittest.case", "TestCase").release());
                aAttributes.emplace_back("FunctionTestCase", import_attribute("unittest.case", "FunctionTestCase").release());
                aAttributes.emplace_back("SkipTest", import_attribute("unittest.case", "SkipTest").release());
                aAttributes.emplace_back("skip", import_attribute("unittest.case", "skip").release());
                aAttributes.emplace_back("skipIf", import_attribute("unittest.case", "skipIf").release());
                aAttributes.emplace_back("skipUnless", import_attribute("unittest.case", "skipUnless").release());
                aAttributes.emplace_back("expectedFailure", import_attribute("unittest.case", "expectedFailure").release());
                aAttributes.emplace_back("doModuleCleanups", import_attribute("unittest.case", "doModuleCleanups").release());
                aAttributes.emplace_back("enterModuleContext", import_attribute("unittest.case", "enterModuleContext").release());
                aAttributes.emplace_back("BaseTestSuite", import_attribute("unittest.suite", "BaseTestSuite").release());
                aAttributes.emplace_back("TestSuite", import_attribute("unittest.suite", "TestSuite").release());
                aAttributes.emplace_back("TestLoader", import_attribute("unittest.loader", "TestLoader").release());
                aAttributes.emplace_back("defaultTestLoader", import_attribute("unittest.loader", "defaultTestLoader").release());
                aAttributes.emplace_back("TestProgram", import_attribute("unittest.main", "TestProgram").release());
                aAttributes.emplace_back("main", import_attribute("unittest.main", "main").release());
                aAttributes.emplace_back("TextTestRunner", import_attribute("unittest.runner", "TextTestRunner").release());
                aAttributes.emplace_back("TextTestResult", import_attribute("unittest.runner", "TextTestResult").release());
                aAttributes.emplace_back("installHandler", import_attribute("unittest.signals", "installHandler").release());
                aAttributes.emplace_back("registerResult", import_attribute("unittest.signals", "registerResult").release());
                aAttributes.emplace_back("removeResult", import_attribute("unittest.signals", "removeResult").release());
                aAttributes.emplace_back("removeHandler", import_attribute("unittest.signals", "removeHandler").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest", unittest_module);
    }
}
