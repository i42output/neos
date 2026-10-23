/*
  unittest_main.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/main.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/main.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.main (CPython 3.13's Lib/unittest/main.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_main_docs.hpp"

namespace neos::python_runtime::detail::unittest_main_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.main.name"))
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
                py_raise("SystemError", std::string{ "unittest.main: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.main.name">()))
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
            static module_writer const sWriter{ "unittest.main", unittest_main_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.main (Lib/unittest/main.py)

        void make_unittest_main()
        {
            static module_writer const& w = writer("unittest.main");
            define("unittest.main", "__unittest", boolean(true));
            define("unittest.main", "_NO_TESTS_EXITCODE", I<5>());
            define("unittest.main", "MAIN_EXAMPLES", S<"Examples:\n  %(prog)s test_module               - run tests from test_module\n  %(prog)s module.TestClass          - run tests from module.TestClass\n  %(prog)s module.Class.test_method  - run specified test method\n  %(prog)s path/to/test_file.py      - run tests from test_file.py\n">());
            define("unittest.main", "MODULE_EXAMPLES", S<"Examples:\n  %(prog)s                           - run default set of tests\n  %(prog)s MyTestSuite               - run suite 'MyTestSuite'\n  %(prog)s MyTestCase.testSomething  - run MyTestCase.testSomething\n  %(prog)s MyTestCase                - run all 'test*' test methods\n                                       in MyTestCase\n">());
            define("unittest.main", "_convert_name", w.function("_convert_name", "_convert_name", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py rel_path;
                    if (((Mod<"os">().attr("path")).method("isfile", { name }).truthy() && name.method("lower").method("endswith", { S<".py">() }).truthy()))
                    {
                        if ((Mod<"os">().attr("path")).method("isabs", { name }).truthy())
                        {
                            rel_path = (Mod<"os">().attr("path")).method("relpath", { name, (Mod<"os">().attr("getcwd"))() });
                            if (((Mod<"os">().attr("path")).method("isabs", { rel_path }).truthy() || rel_path.method("startswith", { Mod<"os">().attr("pardir") }).truthy()))
                            {
                                return name;
                            }
                            name = rel_path;
                        }
                        // on Windows both '\' and '/' are used as path
                        // separators. Better to replace both than rely on os.path.sep
                        return (Mod<"os">().attr("path")).method("normpath", { name })[B<"slice">()(none(), I<-3>(), none())].method("replace", { S<"\\">(), S<".">() }).method("replace", { S<"/">(), S<".">() });
                    }
                    return name;
                }));

            define("unittest.main", "_convert_names", w.function("_convert_names", "_convert_names", { py_positional("names") }, [](arguments a) -> py
                {
                    py names = a[0];
                    return [&]() -> py { py const comprehension1 = py_list(); py name; for (py const& item : iterate{ names }) { name = item; comprehension1.method("append", { (G<"unittest.main._convert_name">())(name) }); } return comprehension1; }();
                }));

            define("unittest.main", "_convert_select_pattern", w.function("_convert_select_pattern", "_convert_select_pattern", { py_positional("pattern") }, [](arguments a) -> py
                {
                    py pattern = a[0];
                    if (!pattern.contains((S<"*">())))
                    {
                        pattern = (S<"*%s*">()) % pattern;
                    }
                    return pattern;
                }));

                // defaults for testing
            define("unittest.main", "TestProgram", w.make_class("TestProgram", { B<"object">() }, {
                { "module", none() },
                { "verbosity", I<1>() },
                { "failfast", none() },
                { "catchbreak", none() },
                { "buffer", none() },
                { "progName", none() },
                { "warnings", none() },
                { "testNamePatterns", none() },
                { "_discovery_parser", none() },
                { "__init__", w.method("TestProgram", "__init__", { py_positional("module", S<"__main__">()), py_positional("defaultTest", none()), py_positional("argv", none()), py_positional("testRunner", none()), py_positional("testLoader", Mod<"unittest.loader">().attr("defaultTestLoader")), py_positional("exit", boolean(true)), py_positional("verbosity", I<1>()), py_positional("failfast", none()), py_positional("catchbreak", none()), py_positional("buffer", none()), py_positional("warnings", none()), py_keyword("tb_locals", boolean(false)), py_keyword("durations", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py module = a[1];
                        py defaultTest = a[2];
                        py argv = a[3];
                        py testRunner = a[4];
                        py testLoader = a[5];
                        py exit = a[6];
                        py verbosity = a[7];
                        py failfast = a[8];
                        py catchbreak = a[9];
                        py buffer = a[10];
                        py warnings = a[11];
                        py tb_locals = a[12];
                        py durations = a[13];
                        py part;
                        if (isinstance(module, B<"str">()))
                        {
                            self.set_attr("module", (B<"__import__">())(module));
                            for (py const& item : iterate{ module.method("split", { S<".">() })[B<"slice">()(I<1>(), none(), none())] })
                            {
                                part = item;
                                self.set_attr("module", (B<"getattr">())(self.attr("module"), part));
                            }
                        }
                        else
                        {
                            self.set_attr("module", module);
                        }
                        if (argv.is_none())
                        {
                            argv = Mod<"sys">().attr("argv");
                        }
                        self.set_attr("exit", exit);
                        self.set_attr("failfast", failfast);
                        self.set_attr("catchbreak", catchbreak);
                        self.set_attr("verbosity", verbosity);
                        self.set_attr("buffer", buffer);
                        self.set_attr("tb_locals", tb_locals);
                        self.set_attr("durations", durations);
                        if ((warnings.is_none() && (!(Mod<"sys">().attr("warnoptions")).truthy())))
                        {
                            // even if DeprecationWarnings are ignored by default
                            // print them anyway unless other warnings settings are
                            // specified by the warnings arg or the -W python flag
                            self.set_attr("warnings", S<"default">());
                        }
                        else
                        {
                            // here self.warnings is set either to the value passed
                            // to the warnings args or to None.
                            // If the user didn't pass a value self.warnings will
                            // be None. This means that the behavior is unchanged
                            // and depends on the values passed to -W.
                            self.set_attr("warnings", warnings);
                        }
                        self.set_attr("defaultTest", defaultTest);
                        self.set_attr("testRunner", testRunner);
                        self.set_attr("testLoader", testLoader);
                        self.set_attr("progName", (Mod<"os">().attr("path")).method("basename", { argv[I<0>()] }));
                        self.method("parseArgs", { argv });
                        self.method("runTests");
                        return none();
                    }) },
                { "_print_help", w.method("TestProgram", "_print_help", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        if (self.attr("module").is_none())
                        {
                            (B<"print">())(self.attr("_main_parser").method("format_help"));
                            (B<"print">())((G<"unittest.main.MAIN_EXAMPLES">()) % dict_of({ { S<"prog">(), self.attr("progName") } }));
                            self.attr("_discovery_parser").method("print_help");
                        }
                        else
                        {
                            (B<"print">())(self.attr("_main_parser").method("format_help"));
                            (B<"print">())((G<"unittest.main.MODULE_EXAMPLES">()) % dict_of({ { S<"prog">(), self.attr("progName") } }));
                        }
                        return none();
                    }, true, true) },
                { "parseArgs", w.method("TestProgram", "parseArgs", { py_positional("argv") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py argv = a[1];
                        self.method("_initArgParsers");
                        if (self.attr("module").is_none())
                        {
                            if (((py{ argv.size() } > (I<1>())) && (argv[I<1>()].method("lower") == (S<"discover">()))))
                            {
                                self.method("_do_discovery", { argv[B<"slice">()(I<2>(), none(), none())] });
                                return none();
                            }
                            self.attr("_main_parser").method("parse_args", { argv[B<"slice">()(I<1>(), none(), none())], self });
                            if (!self.attr("tests").truthy())
                            {
                                // this allows "python -m unittest -v" to still work for
                                // test discovery.
                                self.method("_do_discovery", { py_list() });
                                return none();
                            }
                        }
                        else
                        {
                            self.attr("_main_parser").method("parse_args", { argv[B<"slice">()(I<1>(), none(), none())], self });
                        }
                        if (self.attr("tests").truthy())
                        {
                            self.set_attr("testNames", (G<"unittest.main._convert_names">())(self.attr("tests")));
                            if ((S<"unittest.main">()) == (S<"__main__">()))
                            {
                                // to support python -m unittest ...
                                self.set_attr("module", none());
                            }
                        }
                        else if (self.attr("defaultTest").is_none())
                        {
                            // createTests will load tests from self.module
                            self.set_attr("testNames", none());
                        }
                        else if (isinstance(self.attr("defaultTest"), B<"str">()))
                        {
                            self.set_attr("testNames", py_tuple({ self.attr("defaultTest") }));
                        }
                        else
                        {
                            self.set_attr("testNames", (B<"list">())(self.attr("defaultTest")));
                        }
                        self.method("createTests");
                        return none();
                    }) },
                { "createTests", w.method("TestProgram", "createTests", { py_positional("from_discovery", boolean(false)), py_positional("Loader", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py from_discovery = a[1];
                        py Loader = a[2];
                        py loader;
                        if (self.attr("testNamePatterns").truthy())
                        {
                            self.attr("testLoader").set_attr("testNamePatterns", self.attr("testNamePatterns"));
                        }
                        if (from_discovery.truthy())
                        {
                            loader = (Loader.is_none() ? self.attr("testLoader") : Loader());
                            self.set_attr("test", loader.method("discover", { self.attr("start"), self.attr("pattern"), self.attr("top") }));
                        }
                        else if (self.attr("testNames").is_none())
                        {
                            self.set_attr("test", self.attr("testLoader").method("loadTestsFromModule", { self.attr("module") }));
                        }
                        else
                        {
                            self.set_attr("test", self.attr("testLoader").method("loadTestsFromNames", { self.attr("testNames"), self.attr("module") }));
                        }
                        return none();
                    }) },
                { "_initArgParsers", w.method("TestProgram", "_initArgParsers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parent_parser;
                        parent_parser = self.method("_getParentArgParser");
                        self.set_attr("_main_parser", self.method("_getMainArgParser", { parent_parser }));
                        self.set_attr("_discovery_parser", self.method("_getDiscoveryArgParser", { parent_parser }));
                        return none();
                    }) },
                { "_getParentArgParser", w.method("TestProgram", "_getParentArgParser", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        parser = (Mod<"argparse">().attr("ArgumentParser")).call({  }, { { "add_help", boolean(false) } });
                        parser.method("add_argument", { S<"-v">(), S<"--verbose">() }, { { "dest", S<"verbosity">() }, { "action", S<"store_const">() }, { "const", I<2>() }, { "help", S<"Verbose output">() } });
                        parser.method("add_argument", { S<"-q">(), S<"--quiet">() }, { { "dest", S<"verbosity">() }, { "action", S<"store_const">() }, { "const", I<0>() }, { "help", S<"Quiet output">() } });
                        parser.method("add_argument", { S<"--locals">() }, { { "dest", S<"tb_locals">() }, { "action", S<"store_true">() }, { "help", S<"Show local variables in tracebacks">() } });
                        parser.method("add_argument", { S<"--durations">() }, { { "dest", S<"durations">() }, { "type", B<"int">() }, { "default", none() }, { "metavar", S<"N">() }, { "help", S<"Show the N slowest test cases (N=0 for all)">() } });
                        if (self.attr("failfast").is_none())
                        {
                            parser.method("add_argument", { S<"-f">(), S<"--failfast">() }, { { "dest", S<"failfast">() }, { "action", S<"store_true">() }, { "help", S<"Stop on first fail or error">() } });
                            self.set_attr("failfast", boolean(false));
                        }
                        if (self.attr("catchbreak").is_none())
                        {
                            parser.method("add_argument", { S<"-c">(), S<"--catch">() }, { { "dest", S<"catchbreak">() }, { "action", S<"store_true">() }, { "help", S<"Catch Ctrl-C and display results so far">() } });
                            self.set_attr("catchbreak", boolean(false));
                        }
                        if (self.attr("buffer").is_none())
                        {
                            parser.method("add_argument", { S<"-b">(), S<"--buffer">() }, { { "dest", S<"buffer">() }, { "action", S<"store_true">() }, { "help", S<"Buffer stdout and stderr during tests">() } });
                            self.set_attr("buffer", boolean(false));
                        }
                        if (self.attr("testNamePatterns").is_none())
                        {
                            parser.method("add_argument", { S<"-k">() }, { { "dest", S<"testNamePatterns">() }, { "action", S<"append">() }, { "type", G<"unittest.main._convert_select_pattern">() }, { "help", S<"Only run tests which match the given substring">() } });
                            self.set_attr("testNamePatterns", py_list());
                        }
                        return parser;
                    }) },
                { "_getMainArgParser", w.method("TestProgram", "_getMainArgParser", { py_positional("parent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parent = a[1];
                        py parser;
                        parser = (Mod<"argparse">().attr("ArgumentParser")).call({  }, { { "parents", py_list({ parent }) } });
                        parser.set_attr("prog", self.attr("progName"));
                        parser.set_attr("print_help", self.attr("_print_help"));
                        parser.method("add_argument", { S<"tests">() }, { { "nargs", S<"*">() }, { "help", S<"a list of any number of test modules, classes and test methods.">() } });
                        return parser;
                    }) },
                { "_getDiscoveryArgParser", w.method("TestProgram", "_getDiscoveryArgParser", { py_positional("parent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parent = a[1];
                        py arg;
                        py parser;
                        parser = (Mod<"argparse">().attr("ArgumentParser")).call({  }, { { "parents", py_list({ parent }) } });
                        parser.set_attr("prog", (S<"%s discover">()) % self.attr("progName"));
                        parser.set_attr("epilog", S<"For test discovery all test modules must be importable from the top level directory of the project.">());
                        parser.method("add_argument", { S<"-s">(), S<"--start-directory">() }, { { "dest", S<"start">() }, { "help", S<"Directory to start discovery ('.' default)">() } });
                        parser.method("add_argument", { S<"-p">(), S<"--pattern">() }, { { "dest", S<"pattern">() }, { "help", S<"Pattern to match tests ('test*.py' default)">() } });
                        parser.method("add_argument", { S<"-t">(), S<"--top-level-directory">() }, { { "dest", S<"top">() }, { "help", S<"Top level directory of project (defaults to start directory)">() } });
                        for (py const& item : iterate{ py_tuple({ S<"start">(), S<"pattern">(), S<"top">() }) })
                        {
                            arg = item;
                            parser.method("add_argument", { arg }, { { "nargs", S<"?">() }, { "default", Mod<"argparse">().attr("SUPPRESS") }, { "help", Mod<"argparse">().attr("SUPPRESS") } });
                        }
                        return parser;
                    }) },
                { "_do_discovery", w.method("TestProgram", "_do_discovery", { py_positional("argv"), py_positional("Loader", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py argv = a[1];
                        py Loader = a[2];
                        self.set_attr("start", S<".">());
                        self.set_attr("pattern", S<"test*.py">());
                        self.set_attr("top", none());
                        if (!argv.is_none())
                        {
                            // handle command line args for test discovery
                            if (self.attr("_discovery_parser").is_none())
                            {
                                // for testing
                                self.method("_initArgParsers");
                            }
                            self.attr("_discovery_parser").method("parse_args", { argv, self });
                        }
                        self.method("createTests", {  }, { { "from_discovery", boolean(true) }, { "Loader", Loader } });
                        return none();
                    }) },
                { "runTests", w.method("TestProgram", "runTests", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py testRunner;
                        if (self.attr("catchbreak").truthy())
                        {
                            (Mod<"unittest.signals">().attr("installHandler"))();
                        }
                        if (self.attr("testRunner").is_none())
                        {
                            self.set_attr("testRunner", Mod<"unittest.runner">().attr("TextTestRunner"));
                        }
                        if (isinstance(self.attr("testRunner"), B<"type">()))
                        {
                            try
                            {
                                try
                                {
                                    testRunner = self.method("testRunner", {  }, { { "verbosity", self.attr("verbosity") }, { "failfast", self.attr("failfast") }, { "buffer", self.attr("buffer") }, { "warnings", self.attr("warnings") }, { "tb_locals", self.attr("tb_locals") }, { "durations", self.attr("durations") } });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler5;
                                    if (matches(handler5.exception, B<"TypeError">()))
                                    {
                                        // didn't accept the tb_locals or durations argument
                                        testRunner = self.method("testRunner", {  }, { { "verbosity", self.attr("verbosity") }, { "failfast", self.attr("failfast") }, { "buffer", self.attr("buffer") }, { "warnings", self.attr("warnings") } });
                                    }
                                    else
                                        py_reraise(handler5.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler4;
                                if (matches(handler4.exception, B<"TypeError">()))
                                {
                                    // didn't accept the verbosity, buffer or failfast arguments
                                    testRunner = self.method("testRunner");
                                }
                                else
                                    py_reraise(handler4.exception);
                            }
                        }
                        else
                        {
                            // it is assumed to be a TestRunner instance
                            testRunner = self.attr("testRunner");
                        }
                        self.set_attr("result", testRunner.method("run", { self.attr("test") }));
                        if (self.attr("exit").truthy())
                        {
                            if (!self.attr("result").method("wasSuccessful").truthy())
                            {
                                (Mod<"sys">().attr("exit"))(I<1>());
                            }
                            else if (((self.attr("result").attr("testsRun") == (I<0>())) && (py{ self.attr("result").attr("skipped").size() } == (I<0>()))))
                            {
                                (Mod<"sys">().attr("exit"))(G<"unittest.main._NO_TESTS_EXITCODE">());
                            }
                            else
                            {
                                (Mod<"sys">().attr("exit"))(I<0>());
                            }
                        }
                        return none();
                    }) }, }));

            define("unittest.main", "main", G<"unittest.main.TestProgram">());
        }

        bool unittest_main_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_main();
                }
                if (auto const doc = unittest_main_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("argparse", py{ fn_import("argparse") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("loader", py{ fn_import("unittest.loader") }.release());
                aAttributes.emplace_back("runner", py{ fn_import("unittest.runner") }.release());
                aAttributes.emplace_back("installHandler", import_attribute("unittest.signals", "installHandler").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.main", unittest_main_module);
    }
}
