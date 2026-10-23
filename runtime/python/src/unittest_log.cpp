/*
  unittest_log.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/_log.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/_log.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest._log (CPython 3.13's Lib/unittest/_log.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_log_docs.hpp"

namespace neos::python_runtime::detail::unittest__log_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest._log.name"))
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
                py_raise("SystemError", std::string{ "unittest._log: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest._log.name">()))
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
            static module_writer const sWriter{ "unittest._log", unittest_log_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest._log (Lib/unittest/_log.py)

        void make_unittest__log()
        {
            static module_writer const& w = writer("unittest._log");
            define("unittest._log", "_LoggingWatcher", (Mod<"collections">()).method("namedtuple", { S<"_LoggingWatcher">(), py_list({ S<"records">(), S<"output">() }) }));
            define("unittest._log", "_CapturingHandler", w.make_class("_CapturingHandler", { Mod<"logging">().attr("Handler") }, {
                { "__init__", w.method("_CapturingHandler", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (Mod<"logging">().attr("Handler")).method("__init__", { self });
                        self.set_attr("watcher", (G<"unittest._log._LoggingWatcher">())(py_list(), py_list()));
                        return none();
                    }) },
                { "flush", w.method("_CapturingHandler", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "emit", w.method("_CapturingHandler", "emit", { py_positional("record") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py record = a[1];
                        py msg;
                        self.attr("watcher").attr("records").method("append", { record });
                        msg = self.method("format", { record });
                        self.attr("watcher").attr("output").method("append", { msg });
                        return none();
                    }) }, }));

            define("unittest._log", "_AssertLogsContext", w.make_class("_AssertLogsContext", { Mod<"unittest.case">().attr("_BaseTestCaseContext") }, {
                { "LOGGING_FORMAT", S<"%(levelname)s:%(name)s:%(message)s">() },
                { "__init__", w.method("_AssertLogsContext", "__init__", { py_positional("test_case"), py_positional("logger_name"), py_positional("level"), py_positional("no_logs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py test_case = a[1];
                        py logger_name = a[2];
                        py level = a[3];
                        py no_logs = a[4];
                        (Mod<"unittest.case">().attr("_BaseTestCaseContext")).method("__init__", { self, test_case });
                        self.set_attr("logger_name", logger_name);
                        if (level.truthy())
                        {
                            self.set_attr("level", (Mod<"logging">().attr("_nameToLevel")).method("get", { level, level }));
                        }
                        else
                        {
                            self.set_attr("level", Mod<"logging">().attr("INFO"));
                        }
                        self.set_attr("msg", none());
                        self.set_attr("no_logs", no_logs);
                        return none();
                    }) },
                { "__enter__", w.method("_AssertLogsContext", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py formatter;
                        py handler;
                        py logger;
                        if (isinstance(self.attr("logger_name"), Mod<"logging">().attr("Logger")))
                        {
                            py const value1 = self.attr("logger_name");
                            logger = value1;
                            self.set_attr("logger", value1);
                        }
                        else
                        {
                            py const value2 = (Mod<"logging">().attr("getLogger"))(self.attr("logger_name"));
                            logger = value2;
                            self.set_attr("logger", value2);
                        }
                        formatter = (Mod<"logging">().attr("Formatter"))(self.attr("LOGGING_FORMAT"));
                        handler = (G<"unittest._log._CapturingHandler">())();
                        handler.method("setLevel", { self.attr("level") });
                        handler.method("setFormatter", { formatter });
                        self.set_attr("watcher", handler.attr("watcher"));
                        self.set_attr("old_handlers", logger.attr("handlers")[B<"slice">()(none(), none(), none())]);
                        self.set_attr("old_level", logger.attr("level"));
                        self.set_attr("old_propagate", logger.attr("propagate"));
                        logger.set_attr("handlers", py_list({ handler }));
                        logger.method("setLevel", { self.attr("level") });
                        logger.set_attr("propagate", boolean(false));
                        if (self.attr("no_logs").truthy())
                        {
                            return none();
                        }
                        return handler.attr("watcher");
                    }) },
                { "__exit__", w.method("_AssertLogsContext", "__exit__", { py_positional("exc_type"), py_positional("exc_value"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_value = a[2];
                        py tb = a[3];
                        self.attr("logger").set_attr("handlers", self.attr("old_handlers"));
                        self.attr("logger").set_attr("propagate", self.attr("old_propagate"));
                        self.attr("logger").method("setLevel", { self.attr("old_level") });
                        if (!exc_type.is_none())
                        {
                            // let unexpected exceptions pass through
                            return boolean(false);
                        }
                        if (self.attr("no_logs").truthy())
                        {
                            // assertNoLogs
                            if (py{ self.attr("watcher").attr("records").size() } > (I<0>()))
                            {
                                self.method("_raiseFailure", { (S<"Unexpected logs found: {!r}">()).method("format", { self.attr("watcher").attr("output") }) });
                            }
                        }
                        // assertLogs
                        else if (py{ self.attr("watcher").attr("records").size() } == (I<0>()))
                        {
                            self.method("_raiseFailure", { (S<"no logs of level {} or higher triggered on {}">()).method("format", { (Mod<"logging">().attr("getLevelName"))(self.attr("level")), self.attr("logger").attr("name") }) });
                        }
                        return none();
                    }) }, }));
        }

        bool unittest__log_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest__log();
                }
                if (auto const doc = unittest_log_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("logging", py{ fn_import("logging") }.release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                aAttributes.emplace_back("_BaseTestCaseContext", import_attribute("unittest.case", "_BaseTestCaseContext").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest._log", unittest__log_module);
    }
}
