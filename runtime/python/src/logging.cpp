/*
  logging.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/logging/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).

  Lib/logging/__init__.py's copyright and permission notice:

  Copyright 2001-2022 by Vinay Sajip. All Rights Reserved.

  Permission to use, copy, modify, and distribute this software and its
  documentation for any purpose and without fee is hereby granted,
  provided that the above copyright notice appear in all copies and that
  both that copyright notice and this permission notice appear in
  supporting documentation, and that the name of Vinay Sajip
  not be used in advertising or publicity pertaining to distribution
  of the software without specific, written prior permission.
  VINAY SAJIP DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
  ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
  VINAY SAJIP BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
  ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
  IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
  OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

// The module logging (CPython 3.13's logging/__init__.py): LogRecord, the formatters (and their styles), Filter and Filterer, the handlers
// (Handler, StreamHandler, FileHandler, NullHandler), Logger, RootLogger, Manager and LoggerAdapter, basicConfig and the module's
// functions; its classes are made (by type) of closures, so a program's subclasses override their methods. The module's variables that a
// program can rebind (e.g. raiseExceptions, lastResort, _srcfile) are read from the module. Its own functions have no frames (they are
// not Python's), so findCaller's frames are the program's: the innermost is the caller's.

#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "logging";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;     ///< (the module's classes, by name)
            std::map<std::string, py, std::less<>> globals;     ///< (its variables, as they are made (before the module exists))
            py lock;                ///< (_lock)
            py levelToName;         ///< (_levelToName)
            py nameToLevel;         ///< (_nameToLevel)
            py handlers;            ///< (_handlers)
            py handlerList;         ///< (_handlerList)
            py removeHandlerRef;    ///< (_removeHandlerRef)
            py atForkWeakset;       ///< (_at_fork_reinit_lock_weakset)
            py styles;              ///< (_STYLES)
            py strFormatter;        ///< (_str_formatter)
            py startTime;           ///< (_startTime)
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py cls(char const* aName)
        {
            auto const c = state().classes.find(aName);
            if (c == state().classes.end())
                py_raise("NameError", "name '" + std::string{ aName } + "' is not defined");
            return c->second;
        }

        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py S(char const* aText) { return py{ aText }; }
        bool is_str(py const& aValue) { return isinstance(aValue, builtin("str")); }

        py attr_or(py const& aObject, char const* aName, py const& aDefault)
        {
            try
            {
                return aObject.attr(aName);
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                return aDefault;
            }
        }
        bool has_attr(py const& aObject, py const& aName) { return builtin("hasattr")(aObject, aName).truthy(); }
        py get_attr(py const& aObject, py const& aName, py const& aDefault) { return builtin("getattr")(aObject, aName, aDefault); }

        // (the pending exception, if it is an instance of aClass: taken (aException); otherwise it remains raised)
        bool caught_instance(py const& aClass, py& aException)
        {
            py const e = py_take_exception();
            if (isinstance(e, aClass))
            {
                aException = e;
                return true;
            }
            reraise_exception(e.get());
            return false;
        }

        py slice(py const& aSequence, std::optional<long long> aStart, std::optional<long long> aStop = std::nullopt)
        {
            py const start = aStart ? py{ *aStart } : none();
            py const stop = aStop ? py{ *aStop } : none();
            return aSequence[py{ make_slice(start.get(), stop.get(), nullptr) }];
        }
        // (f(*aArgs, **aKwargs) (aKwargs: a dict, or empty))
        py call_kw(py const& aCallable, std::vector<py> const& aArgs, py const& aKwargs)
        {
            std::vector<object*> args;
            for (auto const& a : aArgs)
                args.push_back(a.reference());
            return py{ invoke(aCallable.get(), std::move(args), aKwargs.empty() ? nullptr : aKwargs.get()) };
        }
        // (super(aClass, self))
        py super_of(char const* aClass, py const& aSelf) { return builtin("super")(cls(aClass), aSelf); }
        py join(char const* aSeparator, py const& aItems) { return py{ aSeparator }.method("join", { aItems }); }
        py list_of(py const& aIterable) { return builtin("list")(aIterable); }
        py sys_attr(char const* aName) { return import_attribute("sys", aName); }

        // (a closure as a function of the module)
        py function(std::string const& aName, std::string const& aQualname, native_body aBody, char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, std::move(aBody));
        }

        using body_type = std::function<py(std::vector<py> const&)>;

        // (a function of its parameters (aVarArgs: *args, appended; aVarKeywords: **kwargs, appended))
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false, char const* aDoc = "")
        {
            return function(aName, aQualname, [aQualname, aParameters, aBody, aVarArgs, aVarKeywords](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                py extra;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, aVarKeywords ? &extra : nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                if (aVarKeywords)
                    a.push_back(extra);
                return aBody(a);
            }, aDoc);
        }
        // (a method: its parameters after self (aClass: its class's qualified name))
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false, bool aVarKeywords = false,
            char const* aDoc = "")
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            return bound_function(aName, std::string{ aClass } + "." + aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords, aDoc);
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: type(aName, aBases, namespace) (aQualname: its qualified name, if it is nested))
        py make_logging_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, char const* aDoc = nullptr,
            char const* aQualname = nullptr)
        {
            py const ns = py_dict();
            ns.set_item(py{ "__module__" }, py{ sModule });
            ns.set_item(py{ "__qualname__" }, py{ aQualname != nullptr ? aQualname : aName });
            if (aDoc != nullptr)
                ns.set_item(py{ "__doc__" }, py{ aDoc });
            for (auto const& [name, value] : aNamespace)
                ns.set_item(py{ name }, value);
            std::vector<py> const bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            py meta = builtin("type");
            for (auto const& b : bases)
            {
                py const t = type_of(b);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            py const result = meta(py{ aName }, py_tuple(bases), ns);
            state().classes[aName] = result;
            return result;
        }

        // (a variable of the module (that the program can rebind): the module's, once it exists)
        py G(char const* aName)
        {
            py const modules = sys_attr("modules");
            py const module = modules.method("get", { S(sModule) });
            if (!module.is_none())
                return module.attr(aName);
            return state().globals.at(aName);
        }
        void set_G(char const* aName, py const& aValue)
        {
            state().globals[aName] = aValue;
            py const module = sys_attr("modules").method("get", { S(sModule) });
            if (!module.is_none())
                module.set_attr(aName, aValue);
        }

        // (with aLock: aBody())
        template <typename F>
        auto with_lock(py const& aLock, F&& aBody) -> decltype(aBody())
        {
            aLock.method("__enter__");
            try
            {
                if constexpr (std::is_void_v<decltype(aBody())>)
                {
                    aBody();
                    aLock.method("__exit__", { none(), none(), none() });
                }
                else
                {
                    auto result = aBody();
                    aLock.method("__exit__", { none(), none(), none() });
                    return result;
                }
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                aLock.method("__exit__", { type_of(e), e, attr_or(e, "__traceback__", none()) });
                py_reraise(e);
            }
        }
        template <typename F>
        auto with_module_lock(F&& aBody) { return with_lock(state().lock, std::forward<F>(aBody)); }

        // (except: the pending exception (raised) handled by aBody if aMatches it (sys.exception() is it while aBody runs); otherwise it remains raised)
        void handle_exception(std::function<bool(py const&)> const& aMatches, std::function<void(py const&)> const& aBody)
        {
            std::string const text = error_text();
            clear_error();
            ref const caughtException{ fn_caught(text.c_str()) };
            py const e = py::borrowed(caughtException.get());
            if (!aMatches(e))
            {
                fn_handled();
                py_reraise(e);
            }
            try
            {
                aBody(e);
            }
            catch (raised const&)
            {
                fn_handled();
                throw;
            }
            fn_handled();
        }
        // (an Exception that is not a RecursionError (logging's handlers' "except RecursionError: raise; except Exception: ..."))
        bool handled_error(py const& aException)
        {
            return isinstance(aException, builtin("Exception")) && !isinstance(aException, builtin("RecursionError"));
        }

        // (the arguments aLeading, then the items of aRest)
        std::vector<py> args_of(std::vector<py> aLeading, py const& aRest)
        {
            for (auto const& r : aRest.items())
                aLeading.push_back(r);
            return aLeading;
        }

        py os_path(char const* aName) { return py{ fn_import("os") }.attr("path").attr(aName); }

        py check_level(py const& aLevel)
        {
            if (isinstance(aLevel, builtin("int")))
                return aLevel;
            if (builtin("str")(aLevel) == aLevel)
            {
                if (!state().nameToLevel.contains(aLevel))
                    py_raise("ValueError", (S("Unknown level: %r") % py_tuple({ aLevel })).str());
                return state().nameToLevel[aLevel];
            }
            py_raise("TypeError", (S("Level not an integer or a valid string: %r") % py_tuple({ aLevel })).str());
        }

        py get_level_name(py const& aLevel)
        {
            auto const& s = state();
            py result = s.levelToName.method("get", { aLevel });
            if (!result.is_none())
                return result;
            result = s.nameToLevel.method("get", { aLevel });
            if (!result.is_none())
                return result;
            return S("Level %s") % py_tuple({ aLevel });
        }

        bool is_internal_frame(py const& aFrame)
        {
            py const filename = os_path("normcase")(aFrame.attr("f_code").attr("co_filename"));
            return filename == G("_srcfile") || (filename.contains(S("importlib")) && filename.contains(S("_bootstrap")));
        }

        // --- the logging record

        py log_record_init(std::vector<py> const& a)
        {
            auto const& self = a[0];
            py const time = py{ fn_import("time") };
            py const os = py{ fn_import("os") };
            py const ct = time.attr("time_ns")();
            self.set_attr("name", a[1]);
            self.set_attr("msg", a[5]);
            py args = a[6];
            if (args.truthy() && args.size() == 1 && isinstance(args[py{ 0 }], import_attribute("collections.abc", "Mapping")) && args[py{ 0 }].truthy())
                args = args[py{ 0 }];
            self.set_attr("args", args);
            self.set_attr("levelname", get_level_name(a[2]));
            self.set_attr("levelno", a[2]);
            self.set_attr("pathname", a[3]);
            try
            {
                self.set_attr("filename", os.attr("path").attr("basename")(a[3]));
                self.set_attr("module", os.attr("path").attr("splitext")(self.attr("filename"))[py{ 0 }]);
            }
            catch (raised const&)
            {
                if (!caught({ "TypeError", "ValueError", "AttributeError" }))
                    throw;
                self.set_attr("filename", a[3]);
                self.set_attr("module", S("Unknown module"));
            }
            self.set_attr("exc_info", a[7]);
            self.set_attr("exc_text", none());
            self.set_attr("stack_info", a[9]);
            self.set_attr("lineno", a[4]);
            self.set_attr("funcName", a[8]);
            self.set_attr("created", ct / py{ 1e9 });
            self.set_attr("msecs", floordiv(ct % py{ 1000000000LL }, py{ 1000000LL }) + py{ 0.0 });
            if (self.attr("msecs") == py{ 999.0 } && builtin("int")(self.attr("created")) != floordiv(ct, py{ 1000000000LL }))
                self.set_attr("msecs", py{ 0.0 });
            self.set_attr("relativeCreated", (ct - state().startTime) / py{ 1e6 });
            if (G("logThreads").truthy())
            {
                py const threading = py{ fn_import("threading") };
                self.set_attr("thread", threading.attr("get_ident")());
                self.set_attr("threadName", threading.attr("current_thread")().attr("name"));
            }
            else
            {
                self.set_attr("thread", none());
                self.set_attr("threadName", none());
            }
            if (!G("logMultiprocessing").truthy())
                self.set_attr("processName", none());
            else
            {
                self.set_attr("processName", S("MainProcess"));
                py const mp = sys_attr("modules").method("get", { S("multiprocessing") });
                if (!mp.is_none())
                {
                    try
                    {
                        self.set_attr("processName", mp.attr("current_process")().attr("name"));
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "Exception" }))
                            throw;
                    }
                }
            }
            if (G("logProcesses").truthy() && has_attr(os, S("getpid")))
                self.set_attr("process", os.attr("getpid")());
            else
                self.set_attr("process", none());
            self.set_attr("taskName", none());
            if (G("logAsyncioTasks").truthy())
            {
                py const asyncio = sys_attr("modules").method("get", { S("asyncio") });
                if (asyncio.truthy())
                {
                    try
                    {
                        self.set_attr("taskName", asyncio.attr("current_task")().method("get_name"));
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "Exception" }))
                            throw;
                    }
                }
            }
            return none();
        }

        void make_log_record()
        {
            make_logging_class("LogRecord", {}, {
                { "__init__", method("LogRecord", "__init__", { py_positional("name"), py_positional("level"), py_positional("pathname"), py_positional("lineno"),
                    py_positional("msg"), py_positional("args"), py_positional("exc_info"), py_positional("func", none()), py_positional("sinfo", none()) },
                    [](std::vector<py> const& a) { return log_record_init(a); }, false, true, "\n        Initialize a logging record with interesting information.\n        ") },
                { "__repr__", method("LogRecord", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        return S("<LogRecord: %s, %s, %s, %s, \"%s\">") % py_tuple({ self.attr("name"), self.attr("levelno"), self.attr("pathname"),
                            self.attr("lineno"), self.attr("msg") });
                    }) },
                { "getMessage", method("LogRecord", "getMessage", {}, [](std::vector<py> const& a)
                    {
                        py msg = builtin("str")(a[0].attr("msg"));
                        if (a[0].attr("args").truthy())
                            msg = msg % a[0].attr("args");
                        return msg;
                    }, false, false, "\n        Return the message for this LogRecord.\n\n        Return the message for this LogRecord after merging any user-supplied\n"
                    "        arguments with the message.\n        ") } },
                "\n    A LogRecord instance represents an event being logged.\n\n    LogRecord instances are created every time something is logged. They\n"
                "    contain all the information pertinent to the event being logged. The\n    main information passed in is in msg and args, which are combined\n"
                "    using str(msg) % args to create the message field of the record. The\n    record also includes information such as when the record was created,\n"
                "    the source line where the logging call was made, and any exception\n    information to be logged.\n    ");
        }

        // --- formatter classes and functions

        // (a style's values: its defaults updated by the record's attributes)
        py style_values(py const& self, py const& aRecord)
        {
            py const defaults = self.attr("_defaults");
            if (defaults.truthy())
                return defaults | aRecord.attr("__dict__");
            return aRecord.attr("__dict__");
        }

        void make_styles()
        {
            py const re = py{ fn_import("re") };
            make_logging_class("PercentStyle", {}, {
                { "default_format", S("%(message)s") },
                { "asctime_format", S("%(asctime)s") },
                { "asctime_search", S("%(asctime)") },
                { "validation_pattern", re.attr("compile")(S("%\\(\\w+\\)[#0+ -]*(\\*|\\d+)?(\\.(\\*|\\d+))?[diouxefgcrsa%]"), re.attr("I")) },
                { "__init__", method("PercentStyle", "__init__", { py_positional("fmt"), py_keyword("defaults", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_fmt", a[1].truthy() ? a[1] : a[0].attr("default_format"));
                        a[0].set_attr("_defaults", a[2]);
                        return none();
                    }) },
                { "usesTime", method("PercentStyle", "usesTime", {}, [](std::vector<py> const& a)
                    {
                        return boolean(a[0].attr("_fmt").method("find", { a[0].attr("asctime_search") }) >= py{ 0 });
                    }) },
                { "validate", method("PercentStyle", "validate", {}, [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("validation_pattern").method("search", { a[0].attr("_fmt") }).truthy())
                            py_raise("ValueError", (S("Invalid format '%s' for '%s' style") % py_tuple({ a[0].attr("_fmt"), a[0].attr("default_format")[py{ 0 }] })).str());
                        return none();
                    }, false, false, "Validate the input format, ensure it matches the correct style") },
                { "_format", method("PercentStyle", "_format", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("_fmt") % style_values(a[0], a[1]);
                    }) },
                { "format", method("PercentStyle", "format", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        try
                        {
                            return a[0].method("_format", { a[1] });
                        }
                        catch (raised const&)
                        {
                            py e;
                            if (!caught_instance(builtin("KeyError"), e))
                                throw;
                            py_raise("ValueError", (S("Formatting field not found in record: %s") % py_tuple({ e })).str());
                        }
                    }) } });
            make_logging_class("StrFormatStyle", { cls("PercentStyle") }, {
                { "default_format", S("{message}") },
                { "asctime_format", S("{asctime}") },
                { "asctime_search", S("{asctime") },
                { "fmt_spec", re.attr("compile")(S("^(.?[<>=^])?[+ -]?#?0?(\\d+|{\\w+})?[,_]?(\\.(\\d+|{\\w+}))?[bcdefgnosx%]?$"), re.attr("I")) },
                { "field_spec", re.attr("compile")(S("^(\\d+|\\w+)(\\.\\w+|\\[[^]]+\\])*$")) },
                { "_format", method("StrFormatStyle", "_format", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        return call_kw(a[0].attr("_fmt").attr("format"), {}, builtin("dict")(style_values(a[0], a[1])));
                    }) },
                { "validate", method("StrFormatStyle", "validate", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const fields = builtin("set")();
                        try
                        {
                            for (auto const& item : state().strFormatter.method("parse", { self.attr("_fmt") }).items())
                            {
                                auto const parts = item.items();
                                py const& fieldname = parts.at(1);
                                py const& spec = parts.at(2);
                                py const& conversion = parts.at(3);
                                if (fieldname.truthy())
                                {
                                    if (!self.attr("field_spec").method("match", { fieldname }).truthy())
                                        py_raise("ValueError", (S("invalid field name/expression: %r") % py_tuple({ fieldname })).str());
                                    fields.method("add", { fieldname });
                                }
                                if (conversion.truthy() && !S("rsa").contains(conversion))
                                    py_raise("ValueError", (S("invalid conversion: %r") % py_tuple({ conversion })).str());
                                if (spec.truthy() && !self.attr("fmt_spec").method("match", { spec }).truthy())
                                    py_raise("ValueError", (S("bad specifier: %r") % py_tuple({ spec })).str());
                            }
                        }
                        catch (raised const&)
                        {
                            py e;
                            if (!caught_instance(builtin("ValueError"), e))
                                throw;
                            py_raise("ValueError", (S("invalid format: %s") % py_tuple({ e })).str());
                        }
                        if (!fields.truthy())
                            py_raise("ValueError", "invalid format: no fields");
                        return none();
                    }, false, false, "Validate the input format, ensure it is the correct string formatting style") } });
            make_logging_class("StringTemplateStyle", { cls("PercentStyle") }, {
                { "default_format", S("${message}") },
                { "asctime_format", S("${asctime}") },
                { "asctime_search", S("${asctime}") },
                { "__init__", method("StringTemplateStyle", "__init__", {}, [](std::vector<py> const& a)
                    {
                        call_kw(super_of("StringTemplateStyle", a[0]).attr("__init__"), a[1].items(), a[2]);
                        a[0].set_attr("_tpl", import_attribute("string", "Template")(a[0].attr("_fmt")));
                        return none();
                    }, true, true) },
                { "usesTime", method("StringTemplateStyle", "usesTime", {}, [](std::vector<py> const& a)
                    {
                        py const fmt = a[0].attr("_fmt");
                        return boolean(fmt.method("find", { S("$asctime") }) >= py{ 0 } || fmt.method("find", { a[0].attr("asctime_search") }) >= py{ 0 });
                    }) },
                { "validate", method("StringTemplateStyle", "validate", {}, [](std::vector<py> const& a)
                    {
                        py const pattern = import_attribute("string", "Template").attr("pattern");
                        py const fields = builtin("set")();
                        for (auto const& m : pattern.method("finditer", { a[0].attr("_fmt") }).items())
                        {
                            py const d = m.method("groupdict");
                            if (d[S("named")].truthy())
                                fields.method("add", { d[S("named")] });
                            else if (d[S("braced")].truthy())
                                fields.method("add", { d[S("braced")] });
                            else if (m.method("group", { py{ 0 } }) == S("$"))
                                py_raise("ValueError", "invalid format: bare '$' not allowed");
                        }
                        if (!fields.truthy())
                            py_raise("ValueError", "invalid format: no fields");
                        return none();
                    }) },
                { "_format", method("StringTemplateStyle", "_format", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        return call_kw(a[0].attr("_tpl").attr("substitute"), {}, builtin("dict")(style_values(a[0], a[1])));
                    }) } });
        }

        py format_time(py const& self, py const& aRecord, py const& aDatefmt)
        {
            py const time = py{ fn_import("time") };
            py const ct = self.attr("converter")(aRecord.attr("created"));
            py s;
            if (aDatefmt.truthy())
                s = time.attr("strftime")(aDatefmt, ct);
            else
            {
                s = time.attr("strftime")(self.attr("default_time_format"), ct);
                if (self.attr("default_msec_format").truthy())
                    s = self.attr("default_msec_format") % py_tuple({ s, aRecord.attr("msecs") });
            }
            return s;
        }

        py format_record(py const& self, py const& aRecord)
        {
            aRecord.set_attr("message", aRecord.method("getMessage"));
            if (self.method("usesTime").truthy())
                aRecord.set_attr("asctime", self.method("formatTime", { aRecord, self.attr("datefmt") }));
            py s = self.method("formatMessage", { aRecord });
            if (aRecord.attr("exc_info").truthy())
                if (!aRecord.attr("exc_text").truthy())
                    aRecord.set_attr("exc_text", self.method("formatException", { aRecord.attr("exc_info") }));
            if (aRecord.attr("exc_text").truthy())
            {
                if (slice(s, -1) != S("\n"))
                    s = s + S("\n");
                s = s + aRecord.attr("exc_text");
            }
            if (aRecord.attr("stack_info").truthy())
            {
                if (slice(s, -1) != S("\n"))
                    s = s + S("\n");
                s = s + self.method("formatStack", { aRecord.attr("stack_info") });
            }
            return s;
        }

        void make_formatters()
        {
            auto& s = state();
            s.styles = py_dict();
            s.globals["BASIC_FORMAT"] = S("%(levelname)s:%(name)s:%(message)s");
            s.styles.set_item(S("%"), py_tuple({ cls("PercentStyle"), s.globals["BASIC_FORMAT"] }));
            s.styles.set_item(S("{"), py_tuple({ cls("StrFormatStyle"), S("{levelname}:{name}:{message}") }));
            s.styles.set_item(S("$"), py_tuple({ cls("StringTemplateStyle"), S("${levelname}:${name}:${message}") }));
            make_logging_class("Formatter", {}, {
                { "converter", import_attribute("time", "localtime") },
                { "__init__", method("Formatter", "__init__", { py_positional("fmt", none()), py_positional("datefmt", none()), py_positional("style", S("%")),
                    py_positional("validate", boolean(true)), py_keyword("defaults", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const styles = state().styles;
                        if (!styles.contains(a[3]))
                            py_raise("ValueError", (S("Style must be one of: %s") % py_tuple({ join(",", styles.method("keys")) })).str());
                        self.set_attr("_style", styles[a[3]][py{ 0 }].call({ a[1] }, { { "defaults", a[5] } }));
                        if (a[4].truthy())
                            self.attr("_style").method("validate");
                        self.set_attr("_fmt", self.attr("_style").attr("_fmt"));
                        self.set_attr("datefmt", a[2]);
                        return none();
                    }, false, false, "\n        Initialize the formatter with specified format strings.\n\n"
                    "        Initialize the formatter either with the specified format string, or a\n        default as described above. Allow for specialized date formatting with\n"
                    "        the optional datefmt argument. If datefmt is omitted, you get an\n        ISO8601-like (or RFC 3339-like) format.\n\n"
                    "        Use a style parameter of '%', '{' or '$' to specify that you want to\n        use one of %-formatting, :meth:`str.format` (``{}``) formatting or\n"
                    "        :class:`string.Template` formatting in your format string.\n\n        .. versionchanged:: 3.2\n           Added the ``style`` parameter.\n        ") },
                { "default_time_format", S("%Y-%m-%d %H:%M:%S") },
                { "default_msec_format", S("%s,%03d") },
                { "formatTime", method("Formatter", "formatTime", { py_positional("record"), py_positional("datefmt", none()) }, [](std::vector<py> const& a)
                    {
                        return format_time(a[0], a[1], a[2]);
                    }) },
                { "formatException", method("Formatter", "formatException", { py_positional("ei") }, [](std::vector<py> const& a)
                    {
                        py const sio = import_attribute("io", "StringIO")();
                        py const tb = a[1][py{ 2 }];
                        import_attribute("traceback", "print_exception").call({ a[1][py{ 0 }], a[1][py{ 1 }], tb }, { { "limit", none() }, { "file", sio } });
                        py s = sio.method("getvalue");
                        sio.method("close");
                        if (slice(s, -1) == S("\n"))
                            s = slice(s, std::nullopt, -1);
                        return s;
                    }) },
                { "usesTime", method("Formatter", "usesTime", {}, [](std::vector<py> const& a) { return a[0].attr("_style").method("usesTime"); }) },
                { "formatMessage", method("Formatter", "formatMessage", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("_style").method("format", { a[1] });
                    }) },
                { "formatStack", method("Formatter", "formatStack", { py_positional("stack_info") }, [](std::vector<py> const& a) { return a[1]; }) },
                { "format", method("Formatter", "format", { py_positional("record") }, [](std::vector<py> const& a) { return format_record(a[0], a[1]); }) } },
                "\n    Formatter instances are used to convert a LogRecord to text.\n\n    Formatters need to know how a LogRecord is constructed. They are\n"
                "    responsible for converting a LogRecord to (usually) a string which can\n    be interpreted by either a human or an external system. The base Formatter\n"
                "    allows a formatting string to be specified. If none is supplied, the\n    style-dependent default value, \"%(message)s\", \"{message}\", or\n"
                "    \"${message}\", is used.\n    ");
            s.globals["_defaultFormatter"] = cls("Formatter")();

            make_logging_class("BufferingFormatter", {}, {
                { "__init__", method("BufferingFormatter", "__init__", { py_positional("linefmt", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("linefmt", a[1].truthy() ? a[1] : state().globals.at("_defaultFormatter"));
                        return none();
                    }) },
                { "formatHeader", method("BufferingFormatter", "formatHeader", { py_positional("records") }, [](std::vector<py> const&) { return S(""); }) },
                { "formatFooter", method("BufferingFormatter", "formatFooter", { py_positional("records") }, [](std::vector<py> const&) { return S(""); }) },
                { "format", method("BufferingFormatter", "format", { py_positional("records") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py rv = S("");
                        if (a[1].size() > 0)
                        {
                            rv = rv + self.method("formatHeader", { a[1] });
                            for (auto const& record : a[1].items())
                                rv = rv + self.attr("linefmt").method("format", { record });
                            rv = rv + self.method("formatFooter", { a[1] });
                        }
                        return rv;
                    }) } },
                "\n    A formatter suitable for formatting a number of records.\n    ");
        }

        // --- filter classes and functions

        void make_filters()
        {
            make_logging_class("Filter", {}, {
                { "__init__", method("Filter", "__init__", { py_positional("name", S("")) }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("name", a[1]);
                        a[0].set_attr("nlen", py{ a[1].size() });
                        return none();
                    }) },
                { "filter", method("Filter", "filter", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const name = a[1].attr("name");
                        if (self.attr("nlen") == py{ 0 })
                            return boolean(true);
                        if (self.attr("name") == name)
                            return boolean(true);
                        if (name.method("find", { self.attr("name"), py{ 0 }, self.attr("nlen") }) != py{ 0 })
                            return boolean(false);
                        return boolean(name[self.attr("nlen")] == S("."));
                    }) } },
                "\n    Filter instances are used to perform arbitrary filtering of LogRecords.\n\n    Loggers and Handlers can optionally use Filter instances to filter\n"
                "    records as desired. The base filter class only allows events which are\n    below a certain point in the logger hierarchy. For example, a filter\n"
                "    initialized with \"A.B\" will allow events logged by loggers \"A.B\",\n    \"A.B.C\", \"A.B.C.D\", \"A.B.D\" etc. but not \"A.BB\", \"B.A.B\" etc. If\n"
                "    initialized with the empty string, all events are passed.\n    ");
            make_logging_class("Filterer", {}, {
                { "__init__", method("Filterer", "__init__", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("filters", py_list());
                        return none();
                    }) },
                { "addFilter", method("Filterer", "addFilter", { py_positional("filter") }, [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("filters").contains(a[1]))
                            a[0].attr("filters").method("append", { a[1] });
                        return none();
                    }) },
                { "removeFilter", method("Filterer", "removeFilter", { py_positional("filter") }, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("filters").contains(a[1]))
                            a[0].attr("filters").method("remove", { a[1] });
                        return none();
                    }) },
                { "filter", method("Filterer", "filter", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        py record = a[1];
                        for (auto const& f : a[0].attr("filters").items())
                        {
                            py const result = has_attr(f, S("filter")) ? f.method("filter", { record }) : f(record);
                            if (!result.truthy())
                                return boolean(false);
                            if (isinstance(result, cls("LogRecord")))
                                record = result;
                        }
                        return record;
                    }) } },
                "\n    A base class for loggers and handlers which allows them to share\n    common code.\n    ");
        }

        // --- handler classes and functions

        void add_handler_ref(py const& aHandler)
        {
            with_module_lock([&]()
            {
                state().handlerList.method("append", { import_attribute("weakref", "ref")(aHandler, state().removeHandlerRef) });
            });
        }

        void register_at_fork_reinit_lock(py const& aInstance)
        {
            if (state().atForkWeakset.empty())
                return;
            with_module_lock([&]() { state().atForkWeakset.method("add", { aInstance }); });
        }

        // (Handler.handleError)
        void handle_error(py const& self, py const& aRecord)
        {
            py const stderr_ = sys_attr("stderr");
            if (!(G("raiseExceptions").truthy() && stderr_.truthy()))
                return;
            py const exc = sys_attr("exception")();
            try
            {
                stderr_.method("write", { S("--- Logging error ---\n") });
                import_attribute("traceback", "print_exception").call({ exc }, { { "limit", none() }, { "file", stderr_ } });
                stderr_.method("write", { S("Call stack:\n") });
                py frame = none();
                if (!exc.is_none())
                {
                    py const tb = attr_or(exc, "__traceback__", none());
                    if (!tb.is_none())
                        frame = tb.attr("tb_frame");
                }
                // (the module's own functions have no frames: the frame is the program's)
                if (frame.truthy())
                    import_attribute("traceback", "print_stack").call({ frame }, { { "file", stderr_ } });
                else
                    stderr_.method("write", { S("Logged from file %s, line %s\n") % py_tuple({ aRecord.attr("filename"), aRecord.attr("lineno") }) });
                try
                {
                    stderr_.method("write", { S("Message: %r\nArguments: %s\n") % py_tuple({ aRecord.attr("msg"), aRecord.attr("args") }) });
                }
                catch (raised const&)
                {
                    handle_exception(handled_error, [&](py const&)
                    {
                        stderr_.method("write", { S("Unable to print the message and arguments - possible formatting error.\nUse the traceback above to help find the error.\n") });
                    });
                }
            }
            catch (raised const&)
            {
                if (!caught({ "OSError" }))
                    throw;
            }
        }

        void stream_handler_emit(py const& self, py const& aRecord)
        {
            try
            {
                py const msg = self.method("format", { aRecord });
                py const stream = self.attr("stream");
                stream.method("write", { msg + self.attr("terminator") });
                self.method("flush");
            }
            catch (raised const&)
            {
                handle_exception(handled_error, [&](py const&) { self.method("handleError", { aRecord }); });
            }
        }

        void make_handlers()
        {
            auto& s = state();
            py const filterer = cls("Filterer");
            make_logging_class("Handler", { filterer }, {
                { "__init__", method("Handler", "__init__", { py_positional("level", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        cls("Filterer").attr("__init__")(self);
                        self.set_attr("_name", none());
                        self.set_attr("level", check_level(a[1]));
                        self.set_attr("formatter", none());
                        self.set_attr("_closed", boolean(false));
                        add_handler_ref(self);
                        self.method("createLock");
                        return none();
                    }, false, false, "\n        Initializes the instance - basically setting the formatter to None\n        and the filter list to empty.\n        ") },
                { "get_name", method("Handler", "get_name", {}, [](std::vector<py> const& a) { return a[0].attr("_name"); }) },
                { "set_name", method("Handler", "set_name", { py_positional("name") }, [](std::vector<py> const& a)
                    {
                        with_module_lock([&]()
                        {
                            py const handlers = state().handlers;
                            if (handlers.contains(a[0].attr("_name")))
                                handlers.method("__delitem__", { a[0].attr("_name") });
                            a[0].set_attr("_name", a[1]);
                            if (a[1].truthy())
                                handlers.set_item(a[1], a[0]);
                        });
                        return none();
                    }) },
                { "createLock", method("Handler", "createLock", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("lock", import_attribute("threading", "RLock")());
                        register_at_fork_reinit_lock(a[0]);
                        return none();
                    }, false, false, "\n        Acquire a thread lock for serializing access to the underlying I/O.\n        ") },
                { "_at_fork_reinit", method("Handler", "_at_fork_reinit", {}, [](std::vector<py> const& a)
                    {
                        a[0].attr("lock").method("_at_fork_reinit");
                        return none();
                    }) },
                { "acquire", method("Handler", "acquire", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("lock").truthy())
                            a[0].attr("lock").method("acquire");
                        return none();
                    }, false, false, "\n        Acquire the I/O thread lock.\n        ") },
                { "release", method("Handler", "release", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("lock").truthy())
                            a[0].attr("lock").method("release");
                        return none();
                    }, false, false, "\n        Release the I/O thread lock.\n        ") },
                { "setLevel", method("Handler", "setLevel", { py_positional("level") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("level", check_level(a[1]));
                        return none();
                    }, false, false, "\n        Set the logging level of this handler.  level must be an int or a str.\n        ") },
                { "format", method("Handler", "format", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        py const fmt = a[0].attr("formatter").truthy() ? a[0].attr("formatter") : G("_defaultFormatter");
                        return fmt.method("format", { a[1] });
                    }) },
                { "emit", method("Handler", "emit", { py_positional("record") }, [](std::vector<py> const&) -> py
                    {
                        py_raise("NotImplementedError", "emit must be implemented by Handler subclasses");
                    }) },
                { "handle", method("Handler", "handle", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py record = a[1];
                        py const rv = self.method("filter", { record });
                        if (isinstance(rv, cls("LogRecord")))
                            record = rv;
                        if (rv.truthy())
                            with_lock(self.attr("lock"), [&]() { self.method("emit", { record }); });
                        return rv;
                    }) },
                { "setFormatter", method("Handler", "setFormatter", { py_positional("fmt") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("formatter", a[1]);
                        return none();
                    }) },
                { "flush", method("Handler", "flush", {}, [](std::vector<py> const&) { return none(); }) },
                { "close", method("Handler", "close", {}, [](std::vector<py> const& a)
                    {
                        with_module_lock([&]()
                        {
                            a[0].set_attr("_closed", boolean(true));
                            py const name = a[0].attr("_name");
                            if (name.truthy() && state().handlers.contains(name))
                                state().handlers.method("__delitem__", { name });
                        });
                        return none();
                    }) },
                { "handleError", method("Handler", "handleError", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        handle_error(a[0], a[1]);
                        return none();
                    }) },
                { "__repr__", method("Handler", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const levelName = get_level_name(a[0].attr("level"));
                        return S("<%s (%s)>") % py_tuple({ a[0].attr("__class__").attr("__name__"), levelName });
                    }) } },
                "\n    Handler instances dispatch logging events to specific destinations.\n\n    The base handler class. Acts as a placeholder which defines the Handler\n"
                "    interface. Handlers can optionally use Formatter instances to format\n    records as desired. By default, no formatter is specified; in this case,\n"
                "    the 'raw' message as determined by record.message is logged.\n    ");
            py const handler = cls("Handler");
            handler.set_attr("name", builtin("property")(handler.attr("get_name"), handler.attr("set_name")));

            make_logging_class("StreamHandler", { handler }, {
                { "terminator", S("\n") },
                { "__init__", method("StreamHandler", "__init__", { py_positional("stream", none()) }, [](std::vector<py> const& a)
                    {
                        cls("Handler").attr("__init__")(a[0]);
                        a[0].set_attr("stream", a[1].is_none() ? sys_attr("stderr") : a[1]);
                        return none();
                    }) },
                { "flush", method("StreamHandler", "flush", {}, [](std::vector<py> const& a)
                    {
                        with_lock(a[0].attr("lock"), [&]()
                        {
                            py const stream = a[0].attr("stream");
                            if (stream.truthy() && has_attr(stream, S("flush")))
                                stream.method("flush");
                        });
                        return none();
                    }, false, false, "\n        Flushes the stream.\n        ") },
                { "emit", method("StreamHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        stream_handler_emit(a[0], a[1]);
                        return none();
                    }) },
                { "setStream", method("StreamHandler", "setStream", { py_positional("stream") }, [](std::vector<py> const& a)
                    {
                        if (a[1].is(a[0].attr("stream")))
                            return none();
                        py const result = a[0].attr("stream");
                        with_lock(a[0].attr("lock"), [&]()
                        {
                            a[0].method("flush");
                            a[0].set_attr("stream", a[1]);
                        });
                        return result;
                    }) },
                { "__repr__", method("StreamHandler", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const levelName = get_level_name(a[0].attr("level"));
                        py name = builtin("str")(get_attr(a[0].attr("stream"), S("name"), S("")));
                        if (name.truthy())
                            name = name + S(" ");
                        return S("<%s %s(%s)>") % py_tuple({ a[0].attr("__class__").attr("__name__"), name, levelName });
                    }) },
                { "__class_getitem__", builtin("classmethod")(import_attribute("types", "GenericAlias")) } },
                "\n    A handler class which writes logging records, appropriately formatted,\n    to a stream. Note that this class does not close the stream, as\n"
                "    sys.stdout or sys.stderr may be used.\n    ");
            py const streamHandler = cls("StreamHandler");

            make_logging_class("FileHandler", { streamHandler }, {
                { "__init__", method("FileHandler", "__init__", { py_positional("filename"), py_positional("mode", S("a")), py_positional("encoding", none()),
                    py_positional("delay", boolean(false)), py_positional("errors", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const filename = import_attribute("os", "fspath")(a[1]);
                        self.set_attr("baseFilename", os_path("abspath")(filename));
                        self.set_attr("mode", a[2]);
                        self.set_attr("encoding", a[3]);
                        if (!a[2].contains(S("b")))
                            self.set_attr("encoding", import_attribute("io", "text_encoding")(a[3]));
                        self.set_attr("errors", a[5]);
                        self.set_attr("delay", a[4]);
                        self.set_attr("_builtin_open", builtin("open"));
                        if (a[4].truthy())
                        {
                            cls("Handler").attr("__init__")(self);
                            self.set_attr("stream", none());
                        }
                        else
                            cls("StreamHandler").attr("__init__")(self, self.method("_open"));
                        return none();
                    }, false, false, "\n        Open the specified file and use it as the stream for logging.\n        ") },
                { "close", method("FileHandler", "close", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        with_lock(self.attr("lock"), [&]()
                        {
                            try
                            {
                                if (self.attr("stream").truthy())
                                {
                                    try
                                    {
                                        self.method("flush");
                                    }
                                    catch (raised const&)
                                    {
                                        py const e = py_take_exception();
                                        py const stream = self.attr("stream");
                                        self.set_attr("stream", none());
                                        if (has_attr(stream, S("close")))
                                            stream.method("close");
                                        py_reraise(e);
                                    }
                                    py const stream = self.attr("stream");
                                    self.set_attr("stream", none());
                                    if (has_attr(stream, S("close")))
                                        stream.method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                cls("StreamHandler").attr("close")(self);
                                py_reraise(e);
                            }
                            cls("StreamHandler").attr("close")(self);
                        });
                        return none();
                    }, false, false, "\n        Closes the stream.\n        ") },
                { "_open", method("FileHandler", "_open", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        return self.attr("_builtin_open").call({ self.attr("baseFilename"), self.attr("mode") }, { { "encoding", self.attr("encoding") },
                            { "errors", self.attr("errors") } });
                    }) },
                { "emit", method("FileHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("stream").is_none())
                        {
                            if (self.attr("mode") != S("w") || !self.attr("_closed").truthy())
                            {
                                try
                                {
                                    self.set_attr("stream", self.method("_open"));
                                }
                                catch (raised const&)
                                {
                                    handle_exception([](py const& e) { return isinstance(e, builtin("Exception")); }, [&](py const&) { self.method("handleError", { a[1] }); });
                                    return none();
                                }
                            }
                        }
                        if (self.attr("stream").truthy())
                            cls("StreamHandler").attr("emit")(self, a[1]);
                        return none();
                    }) },
                { "__repr__", method("FileHandler", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const levelName = get_level_name(a[0].attr("level"));
                        return S("<%s %s (%s)>") % py_tuple({ a[0].attr("__class__").attr("__name__"), a[0].attr("baseFilename"), levelName });
                    }) } },
                "\n    A handler class which writes formatted logging records to disk files.\n    ");

            make_logging_class("_StderrHandler", { streamHandler }, {
                { "__init__", method("_StderrHandler", "__init__", { py_positional("level", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        cls("Handler").attr("__init__")(a[0], a[1]);
                        return none();
                    }) },
                { "stream", builtin("property")(method("_StderrHandler", "stream", {}, [](std::vector<py> const&) { return sys_attr("stderr"); })) } },
                "\n    This class is like a StreamHandler using sys.stderr, but always uses\n    whatever sys.stderr is currently set to rather than the value of\n"
                "    sys.stderr at handler construction time.\n    ");
            s.globals["_defaultLastResort"] = cls("_StderrHandler")(py{ 30 });
            s.globals["lastResort"] = s.globals["_defaultLastResort"];

            make_logging_class("NullHandler", { handler }, {
                { "handle", method("NullHandler", "handle", { py_positional("record") }, [](std::vector<py> const&) { return none(); }, false, false, "Stub.") },
                { "emit", method("NullHandler", "emit", { py_positional("record") }, [](std::vector<py> const&) { return none(); }, false, false, "Stub.") },
                { "createLock", method("NullHandler", "createLock", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("lock", none());
                        return none();
                    }) },
                { "_at_fork_reinit", method("NullHandler", "_at_fork_reinit", {}, [](std::vector<py> const&) { return none(); }) } },
                "\n    This handler does nothing. It's intended to be used to avoid the\n    \"No handlers could be found for logger XXX\" one-off warning. This is\n"
                "    important for library code, which may contain code to log events. If a user\n    of the library does not configure logging, the one-off warning might be\n"
                "    produced; to avoid this, the library developer simply needs to instantiate\n    a NullHandler and add it to the top-level logger of the library module or\n"
                "    package.\n    ");
        }

        // --- manager classes and functions

        void check_logger_class(py const& aClass)
        {
            if (aClass != cls("Logger"))
                if (!issubclass(aClass, cls("Logger")))
                    py_raise("TypeError", "logger not derived from logging.Logger: " + aClass.attr("__name__").str());
        }

        py manager_get_logger(py const& self, py const& aName)
        {
            if (!is_str(aName))
                py_raise("TypeError", "A logger name must be a string");
            return with_module_lock([&]() -> py
            {
                py rv;
                py const loggerDict = self.attr("loggerDict");
                py const loggerClass = self.attr("loggerClass").truthy() ? self.attr("loggerClass") : G("_loggerClass");
                if (loggerDict.contains(aName))
                {
                    rv = loggerDict[aName];
                    if (isinstance(rv, cls("PlaceHolder")))
                    {
                        py const ph = rv;
                        rv = loggerClass(aName);
                        rv.set_attr("manager", self);
                        loggerDict.set_item(aName, rv);
                        self.method("_fixupChildren", { ph, rv });
                        self.method("_fixupParents", { rv });
                    }
                }
                else
                {
                    rv = loggerClass(aName);
                    rv.set_attr("manager", self);
                    loggerDict.set_item(aName, rv);
                    self.method("_fixupParents", { rv });
                }
                return rv;
            });
        }

        void manager_fixup_parents(py const& self, py const& aLogger)
        {
            py const name = aLogger.attr("name");
            long long i = name.method("rfind", { S(".") }).as_index();
            py rv = none();
            py const loggerDict = self.attr("loggerDict");
            while (i > 0 && !rv.truthy())
            {
                py const substr = slice(name, std::nullopt, i);
                if (!loggerDict.contains(substr))
                    loggerDict.set_item(substr, cls("PlaceHolder")(aLogger));
                else
                {
                    py const obj = loggerDict[substr];
                    if (isinstance(obj, cls("Logger")))
                        rv = obj;
                    else
                    {
                        if (!isinstance(obj, cls("PlaceHolder")))
                            py_raise("AssertionError", "");
                        obj.method("append", { aLogger });
                    }
                }
                i = name.method("rfind", { S("."), py{ 0 }, py{ i - 1 } }).as_index();
            }
            if (!rv.truthy())
                rv = self.attr("root");
            aLogger.set_attr("parent", rv);
        }

        void make_manager()
        {
            make_logging_class("PlaceHolder", {}, {
                { "__init__", method("PlaceHolder", "__init__", { py_positional("alogger") }, [](std::vector<py> const& a)
                    {
                        py const loggerMap = py_dict();
                        loggerMap.set_item(a[1], none());
                        a[0].set_attr("loggerMap", loggerMap);
                        return none();
                    }, false, false, "\n        Initialize with the specified logger being a child of this placeholder.\n        ") },
                { "append", method("PlaceHolder", "append", { py_positional("alogger") }, [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("loggerMap").contains(a[1]))
                            a[0].attr("loggerMap").set_item(a[1], none());
                        return none();
                    }, false, false, "\n        Add the specified logger as a child of this placeholder.\n        ") } },
                "\n    PlaceHolder instances are used in the Manager logger hierarchy to take\n    the place of nodes for which no loggers have been defined. This class is\n"
                "    intended for internal use only and not as part of the public API.\n    ");

            py const getDisable = method("Manager", "disable", {}, [](std::vector<py> const& a) { return a[0].attr("_disable"); });
            py const setDisable = method("Manager", "disable", { py_positional("value") }, [](std::vector<py> const& a)
            {
                a[0].set_attr("_disable", check_level(a[1]));
                return none();
            });
            make_logging_class("Manager", {}, {
                { "__init__", method("Manager", "__init__", { py_positional("rootnode") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("root", a[1]);
                        self.set_attr("disable", py{ 0 });
                        self.set_attr("emittedNoHandlerWarning", boolean(false));
                        self.set_attr("loggerDict", py_dict());
                        self.set_attr("loggerClass", none());
                        self.set_attr("logRecordFactory", none());
                        return none();
                    }, false, false, "\n        Initialize the manager with the root node of the logger hierarchy.\n        ") },
                { "disable", builtin("property")(getDisable).method("setter", { setDisable }) },
                { "getLogger", method("Manager", "getLogger", { py_positional("name") }, [](std::vector<py> const& a) { return manager_get_logger(a[0], a[1]); }) },
                { "setLoggerClass", method("Manager", "setLoggerClass", { py_positional("klass") }, [](std::vector<py> const& a)
                    {
                        check_logger_class(a[1]);
                        a[0].set_attr("loggerClass", a[1]);
                        return none();
                    }) },
                { "setLogRecordFactory", method("Manager", "setLogRecordFactory", { py_positional("factory") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("logRecordFactory", a[1]);
                        return none();
                    }) },
                { "_fixupParents", method("Manager", "_fixupParents", { py_positional("alogger") }, [](std::vector<py> const& a)
                    {
                        manager_fixup_parents(a[0], a[1]);
                        return none();
                    }) },
                { "_fixupChildren", method("Manager", "_fixupChildren", { py_positional("ph"), py_positional("alogger") }, [](std::vector<py> const& a)
                    {
                        py const name = a[2].attr("name");
                        long long const namelen = name.size();
                        for (auto const& c : list_of(a[1].attr("loggerMap").method("keys")).items())
                            if (slice(c.attr("parent").attr("name"), std::nullopt, namelen) != name)
                            {
                                a[2].set_attr("parent", c.attr("parent"));
                                c.set_attr("parent", a[2]);
                            }
                        return none();
                    }) },
                { "_clear_cache", method("Manager", "_clear_cache", {}, [](std::vector<py> const& a)
                    {
                        with_module_lock([&]()
                        {
                            for (auto const& logger : a[0].attr("loggerDict").method("values").items())
                                if (isinstance(logger, cls("Logger")))
                                    logger.attr("_cache").method("clear");
                            a[0].attr("root").attr("_cache").method("clear");
                        });
                        return none();
                    }) } },
                "\n    There is [under normal circumstances] just one Manager instance, which\n    holds the hierarchy of loggers.\n    ");
        }

        // --- logger classes and functions

        py find_caller(py const& self, py const& aStackInfo, py const& aStackLevel)
        {
            (void)self;
            // (the module's functions have no frames: the innermost frame is the caller's)
            py f;
            try
            {
                f = sys_attr("_getframe")(py{ 0 });
            }
            catch (raised const&)
            {
                if (!caught({ "ValueError" }))
                    throw;
                f = none();
            }
            if (f.is_none())
                return py_tuple({ S("(unknown file)"), py{ 0 }, S("(unknown function)"), none() });
            while (!f.is_none() && is_internal_frame(f))
            {
                py const next = f.attr("f_back");
                if (next.is_none())
                    break;
                f = next;
            }
            long long stacklevel = aStackLevel.as_index() - 1;
            while (stacklevel > 0)
            {
                py const next = f.attr("f_back");
                if (next.is_none())
                    break;
                f = next;
                if (!is_internal_frame(f))
                    --stacklevel;
            }
            py const co = f.attr("f_code");
            py sinfo = none();
            if (aStackInfo.truthy())
            {
                py const sio = import_attribute("io", "StringIO")();
                sio.method("write", { S("Stack (most recent call last):\n") });
                import_attribute("traceback", "print_stack").call({ f }, { { "file", sio } });
                sinfo = sio.method("getvalue");
                if (slice(sinfo, -1) == S("\n"))
                    sinfo = slice(sinfo, std::nullopt, -1);
                sio.method("close");
            }
            return py_tuple({ co.attr("co_filename"), f.attr("f_lineno"), co.attr("co_name"), sinfo });
        }

        void logger_log(py const& self, py const& aLevel, py const& aMsg, py const& aArgs, py aExcInfo, py const& aExtra, py const& aStackInfo,
            py const& aStackLevel)
        {
            py sinfo = none();
            py fn, lno, func;
            if (G("_srcfile").truthy())
            {
                try
                {
                    auto const caller = self.method("findCaller", { aStackInfo, aStackLevel }).items();
                    if (caller.size() != 4u)
                        py_raise("ValueError", "not enough values to unpack (expected 4, got " + std::to_string(caller.size()) + ")");
                    fn = caller[0];
                    lno = caller[1];
                    func = caller[2];
                    sinfo = caller[3];
                }
                catch (raised const&)
                {
                    if (!caught({ "ValueError" }))
                        throw;
                    fn = S("(unknown file)");
                    lno = py{ 0 };
                    func = S("(unknown function)");
                }
            }
            else
            {
                fn = S("(unknown file)");
                lno = py{ 0 };
                func = S("(unknown function)");
            }
            if (aExcInfo.truthy())
            {
                if (isinstance(aExcInfo, builtin("BaseException")))
                    aExcInfo = py_tuple({ type_of(aExcInfo), aExcInfo, aExcInfo.attr("__traceback__") });
                else if (!isinstance(aExcInfo, builtin("tuple")))
                    aExcInfo = sys_attr("exc_info")();
            }
            py const record = self.method("makeRecord", { self.attr("name"), aLevel, fn, lno, aMsg, aArgs, aExcInfo, func, aExtra, sinfo });
            self.method("handle", { record });
        }

        void call_handlers(py const& self, py const& aRecord)
        {
            py c = self;
            long long found = 0;
            while (c.truthy())
            {
                for (auto const& hdlr : c.attr("handlers").items())
                {
                    ++found;
                    if (aRecord.attr("levelno") >= hdlr.attr("level"))
                        hdlr.method("handle", { aRecord });
                }
                if (!c.attr("propagate").truthy())
                    c = none();
                else
                    c = c.attr("parent");
            }
            if (found == 0)
            {
                py const lastResort = G("lastResort");
                if (lastResort.truthy())
                {
                    if (aRecord.attr("levelno") >= lastResort.attr("level"))
                        lastResort.method("handle", { aRecord });
                }
                else if (G("raiseExceptions").truthy() && !self.attr("manager").attr("emittedNoHandlerWarning").truthy())
                {
                    sys_attr("stderr").method("write", { S("No handlers could be found for logger \"%s\"\n") % py_tuple({ self.attr("name") }) });
                    self.attr("manager").set_attr("emittedNoHandlerWarning", boolean(true));
                }
            }
        }

        py is_enabled_for(py const& self, py const& aLevel)
        {
            if (self.attr("disabled").truthy())
                return boolean(false);
            py const cache = self.attr("_cache");
            try
            {
                return cache[aLevel];
            }
            catch (raised const&)
            {
                if (!caught({ "KeyError" }))
                    throw;
                return with_module_lock([&]()
                {
                    py isEnabled;
                    if (self.attr("manager").attr("disable") >= aLevel)
                        isEnabled = boolean(false);
                    else
                        isEnabled = boolean(aLevel >= self.method("getEffectiveLevel"));
                    cache.set_item(aLevel, isEnabled);
                    return isEnabled;
                });
            }
        }

        // (Logger's debug, info, ...: if self.isEnabledFor(level): self._log(level, msg, args, **kwargs))
        py level_method(char const* aName, long long aLevel, char const* aDoc)
        {
            return method("Logger", aName, { py_positional("msg") }, [aLevel](std::vector<py> const& a)
            {
                py const level{ aLevel };
                if (a[0].method("isEnabledFor", { level }).truthy())
                    call_kw(a[0].attr("_log"), { level, a[1], a[2] }, a[3]);
                return none();
            }, true, true, aDoc);
        }

        void make_loggers()
        {
            py const filterer = cls("Filterer");
            make_logging_class("Logger", { filterer }, {
                { "__init__", method("Logger", "__init__", { py_positional("name"), py_positional("level", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        cls("Filterer").attr("__init__")(self);
                        self.set_attr("name", a[1]);
                        self.set_attr("level", check_level(a[2]));
                        self.set_attr("parent", none());
                        self.set_attr("propagate", boolean(true));
                        self.set_attr("handlers", py_list());
                        self.set_attr("disabled", boolean(false));
                        self.set_attr("_cache", py_dict());
                        return none();
                    }, false, false, "\n        Initialize the logger with a name and an optional level.\n        ") },
                { "setLevel", method("Logger", "setLevel", { py_positional("level") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("level", check_level(a[1]));
                        a[0].attr("manager").method("_clear_cache");
                        return none();
                    }, false, false, "\n        Set the logging level of this logger.  level must be an int or a str.\n        ") },
                { "debug", level_method("debug", 10, "\n        Log 'msg % args' with severity 'DEBUG'.\n\n        To pass exception information, use the keyword argument exc_info with\n"
                    "        a true value, e.g.\n\n        logger.debug(\"Houston, we have a %s\", \"thorny problem\", exc_info=True)\n        ") },
                { "info", level_method("info", 20, "\n        Log 'msg % args' with severity 'INFO'.\n\n        To pass exception information, use the keyword argument exc_info with\n"
                    "        a true value, e.g.\n\n        logger.info(\"Houston, we have a %s\", \"notable problem\", exc_info=True)\n        ") },
                { "warning", level_method("warning", 30, "\n        Log 'msg % args' with severity 'WARNING'.\n\n        To pass exception information, use the keyword argument exc_info with\n"
                    "        a true value, e.g.\n\n        logger.warning(\"Houston, we have a %s\", \"bit of a problem\", exc_info=True)\n        ") },
                { "warn", method("Logger", "warn", { py_positional("msg") }, [](std::vector<py> const& a)
                    {
                        import_attribute("warnings", "warn")(S("The 'warn' method is deprecated, use 'warning' instead"), builtin("DeprecationWarning"), py{ 2 });
                        call_kw(a[0].attr("warning"), args_of({ a[1] }, a[2]), a[3]);
                        return none();
                    }, true, true) },
                { "error", level_method("error", 40, "\n        Log 'msg % args' with severity 'ERROR'.\n\n        To pass exception information, use the keyword argument exc_info with\n"
                    "        a true value, e.g.\n\n        logger.error(\"Houston, we have a %s\", \"major problem\", exc_info=True)\n        ") },
                { "exception", method("Logger", "exception", { py_positional("msg"), py_keyword("exc_info", boolean(true)) }, [](std::vector<py> const& a)
                    {
                        py const kwargs = builtin("dict")(a[4]);
                        kwargs.set_item(S("exc_info"), a[2]);
                        call_kw(a[0].attr("error"), args_of({ a[1] }, a[3]), kwargs);
                        return none();
                    }, true, true, "\n        Convenience method for logging an ERROR with exception information.\n        ") },
                { "critical", level_method("critical", 50, "\n        Log 'msg % args' with severity 'CRITICAL'.\n\n        To pass exception information, use the keyword argument exc_info with\n"
                    "        a true value, e.g.\n\n        logger.critical(\"Houston, we have a %s\", \"major disaster\", exc_info=True)\n        ") },
                { "fatal", method("Logger", "fatal", { py_positional("msg") }, [](std::vector<py> const& a)
                    {
                        call_kw(a[0].attr("critical"), args_of({ a[1] }, a[2]), a[3]);
                        return none();
                    }, true, true, "\n        Don't use this method, use critical() instead.\n        ") },
                { "log", method("Logger", "log", { py_positional("level"), py_positional("msg") }, [](std::vector<py> const& a)
                    {
                        if (!isinstance(a[1], builtin("int")))
                        {
                            if (G("raiseExceptions").truthy())
                                py_raise("TypeError", "level must be an integer");
                            return none();
                        }
                        if (a[0].method("isEnabledFor", { a[1] }).truthy())
                            call_kw(a[0].attr("_log"), { a[1], a[2], a[3] }, a[4]);
                        return none();
                    }, true, true, "\n        Log 'msg % args' with the integer severity 'level'.\n\n        To pass exception information, use the keyword argument exc_info with\n"
                    "        a true value, e.g.\n\n        logger.log(level, \"We have a %s\", \"mysterious problem\", exc_info=True)\n        ") },
                { "findCaller", method("Logger", "findCaller", { py_positional("stack_info", boolean(false)), py_positional("stacklevel", py{ 1 }) },
                    [](std::vector<py> const& a) { return find_caller(a[0], a[1], a[2]); }, false, false,
                    "\n        Find the stack frame of the caller so that we can note the source\n        file name, line number and function name.\n        ") },
                { "makeRecord", method("Logger", "makeRecord", { py_positional("name"), py_positional("level"), py_positional("fn"), py_positional("lno"),
                    py_positional("msg"), py_positional("args"), py_positional("exc_info"), py_positional("func", none()), py_positional("extra", none()),
                    py_positional("sinfo", none()) }, [](std::vector<py> const& a)
                    {
                        py const rv = G("_logRecordFactory")(a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[10]);
                        py const extra = a[9];
                        if (!extra.is_none())
                            for (auto const& key : extra.items())
                            {
                                if (py_list({ S("message"), S("asctime") }).contains(key) || rv.attr("__dict__").contains(key))
                                    py_raise("KeyError", (S("Attempt to overwrite %r in LogRecord") % py_tuple({ key })).str());
                                rv.attr("__dict__").set_item(key, extra[key]);
                            }
                        return rv;
                    }, false, false, "\n        A factory method which can be overridden in subclasses to create\n        specialized LogRecords.\n        ") },
                { "_log", method("Logger", "_log", { py_positional("level"), py_positional("msg"), py_positional("args"), py_positional("exc_info", none()),
                    py_positional("extra", none()), py_positional("stack_info", boolean(false)), py_positional("stacklevel", py{ 1 }) }, [](std::vector<py> const& a)
                    {
                        logger_log(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
                        return none();
                    }, false, false, "\n        Low-level logging routine which creates a LogRecord and then calls\n        all the handlers of this logger to handle the record.\n        ") },
                { "handle", method("Logger", "handle", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("disabled").truthy())
                            return none();
                        py record = a[1];
                        py const maybeRecord = self.method("filter", { record });
                        if (!maybeRecord.truthy())
                            return none();
                        if (isinstance(maybeRecord, cls("LogRecord")))
                            record = maybeRecord;
                        self.method("callHandlers", { record });
                        return none();
                    }) },
                { "addHandler", method("Logger", "addHandler", { py_positional("hdlr") }, [](std::vector<py> const& a)
                    {
                        with_module_lock([&]()
                        {
                            if (!a[0].attr("handlers").contains(a[1]))
                                a[0].attr("handlers").method("append", { a[1] });
                        });
                        return none();
                    }, false, false, "\n        Add the specified handler to this logger.\n        ") },
                { "removeHandler", method("Logger", "removeHandler", { py_positional("hdlr") }, [](std::vector<py> const& a)
                    {
                        with_module_lock([&]()
                        {
                            if (a[0].attr("handlers").contains(a[1]))
                            {
                                py const handlers = a[0].attr("handlers").method("copy");
                                handlers.method("remove", { a[1] });
                                a[0].set_attr("handlers", handlers);
                            }
                        });
                        return none();
                    }, false, false, "\n        Remove the specified handler from this logger.\n        ") },
                { "hasHandlers", method("Logger", "hasHandlers", {}, [](std::vector<py> const& a)
                    {
                        py c = a[0];
                        bool rv = false;
                        while (c.truthy())
                        {
                            if (c.attr("handlers").truthy())
                            {
                                rv = true;
                                break;
                            }
                            if (!c.attr("propagate").truthy())
                                break;
                            c = c.attr("parent");
                        }
                        return boolean(rv);
                    }) },
                { "callHandlers", method("Logger", "callHandlers", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        call_handlers(a[0], a[1]);
                        return none();
                    }) },
                { "getEffectiveLevel", method("Logger", "getEffectiveLevel", {}, [](std::vector<py> const& a)
                    {
                        py logger = a[0];
                        while (logger.truthy())
                        {
                            if (logger.attr("level").truthy())
                                return logger.attr("level");
                            logger = logger.attr("parent");
                        }
                        return py{ 0 };
                    }) },
                { "isEnabledFor", method("Logger", "isEnabledFor", { py_positional("level") }, [](std::vector<py> const& a) { return is_enabled_for(a[0], a[1]); },
                    false, false, "\n        Is this logger enabled for level 'level'?\n        ") },
                { "getChild", method("Logger", "getChild", { py_positional("suffix") }, [](std::vector<py> const& a)
                    {
                        py suffix = a[1];
                        if (!a[0].attr("root").is(a[0]))
                            suffix = join(".", py_tuple({ a[0].attr("name"), suffix }));
                        return a[0].attr("manager").method("getLogger", { suffix });
                    }) },
                { "getChildren", method("Logger", "getChildren", {}, [](std::vector<py> const& a)
                    {
                        auto const hierlevel = [](py const& aLogger) -> long long
                        {
                            if (aLogger.is(aLogger.attr("manager").attr("root")))
                                return 0;
                            return 1 + aLogger.attr("name").method("count", { S(".") }).as_index();
                        };
                        py const d = a[0].attr("manager").attr("loggerDict");
                        return with_module_lock([&]()
                        {
                            py const result = builtin("set")();
                            for (auto const& item : d.method("values").items())
                                if (isinstance(item, cls("Logger")) && item.attr("parent").is(a[0]) && hierlevel(item) == 1 + hierlevel(item.attr("parent")))
                                    result.method("add", { item });
                            return result;
                        });
                    }) },
                { "__repr__", method("Logger", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const levelName = get_level_name(a[0].method("getEffectiveLevel"));
                        return S("<%s %s (%s)>") % py_tuple({ a[0].attr("__class__").attr("__name__"), a[0].attr("name"), levelName });
                    }) },
                { "__reduce__", method("Logger", "__reduce__", {}, [](std::vector<py> const& a)
                    {
                        if (!G("getLogger")(a[0].attr("name")).is(a[0]))
                            py_raise(import_attribute("pickle", "PicklingError")(S("logger cannot be pickled")));
                        return py_tuple({ G("getLogger"), py_tuple({ a[0].attr("name") }) });
                    }) } },
                "\n    Instances of the Logger class represent a single logging channel. A\n    \"logging channel\" indicates an area of an application. Exactly how an\n"
                "    \"area\" is defined is up to the application developer. Since an\n    application can have any number of areas, logging channels are identified\n"
                "    by a unique string. Application areas can be nested (e.g. an area\n    of \"input processing\" might include sub-areas \"read CSV files\", \"read\n"
                "    XLS files\" and \"read Gnumeric files\"). To cater for this natural nesting,\n    channel names are organized into a namespace hierarchy where levels are\n"
                "    separated by periods, much like the Java or Python package namespace. So\n    in the instance given above, channel names might be \"input\" for the upper\n"
                "    level, and \"input.csv\", \"input.xls\" and \"input.gnu\" for the sub-levels.\n    There is no arbitrary limit to the depth of nesting.\n    ");

            make_logging_class("RootLogger", { cls("Logger") }, {
                { "__init__", method("RootLogger", "__init__", { py_positional("level") }, [](std::vector<py> const& a)
                    {
                        cls("Logger").attr("__init__")(a[0], S("root"), a[1]);
                        return none();
                    }, false, false, "\n        Initialize the logger with the name \"root\".\n        ") },
                { "__reduce__", method("RootLogger", "__reduce__", {}, [](std::vector<py> const&)
                    {
                        return py_tuple({ G("getLogger"), py_tuple() });
                    }) } },
                "\n    A root logger is not that different to any other logger, except that\n    it must have a logging level and there is only one instance of it in\n"
                "    the hierarchy.\n    ");

            // (LoggerAdapter)
            auto const adapterLevel = [](char const* aName, long long aLevel, char const* aDoc)
            {
                return method("LoggerAdapter", aName, { py_positional("msg") }, [aLevel](std::vector<py> const& a)
                {
                    call_kw(a[0].attr("log"), args_of({ py{ aLevel }, a[1] }, a[2]), a[3]);
                    return none();
                }, true, true, aDoc);
            };
            make_logging_class("LoggerAdapter", {}, {
                { "__init__", method("LoggerAdapter", "__init__", { py_positional("logger"), py_positional("extra", none()), py_positional("merge_extra", boolean(false)) },
                    [](std::vector<py> const& a)
                    {
                        a[0].set_attr("logger", a[1]);
                        a[0].set_attr("extra", a[2]);
                        a[0].set_attr("merge_extra", a[3]);
                        return none();
                    }) },
                { "process", method("LoggerAdapter", "process", { py_positional("msg"), py_positional("kwargs") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const kwargs = a[2];
                        if (self.attr("merge_extra").truthy() && !kwargs.method("get", { S("extra") }).is_none())
                        {
                            if (!self.attr("extra").is_none())
                            {
                                py const merged = builtin("dict")(self.attr("extra"));
                                merged.method("update", { kwargs[S("extra")] });
                                kwargs.set_item(S("extra"), merged);
                            }
                        }
                        else
                            kwargs.set_item(S("extra"), self.attr("extra"));
                        return py_tuple({ a[1], kwargs });
                    }, false, false, "\n        Process the logging message and keyword arguments passed in to\n"
                    "        a logging call to insert contextual information. You can either\n        manipulate the message itself, the keyword args or both. Return\n"
                    "        the message and kwargs modified (or not) to suit your needs.\n\n        Normally, you'll only need to override this one method in a\n"
                    "        LoggerAdapter subclass for your specific needs.\n        ") },
                { "debug", adapterLevel("debug", 10, "\n        Delegate a debug call to the underlying logger.\n        ") },
                { "info", adapterLevel("info", 20, "\n        Delegate an info call to the underlying logger.\n        ") },
                { "warning", adapterLevel("warning", 30, "\n        Delegate a warning call to the underlying logger.\n        ") },
                { "warn", method("LoggerAdapter", "warn", { py_positional("msg") }, [](std::vector<py> const& a)
                    {
                        import_attribute("warnings", "warn")(S("The 'warn' method is deprecated, use 'warning' instead"), builtin("DeprecationWarning"), py{ 2 });
                        call_kw(a[0].attr("warning"), args_of({ a[1] }, a[2]), a[3]);
                        return none();
                    }, true, true) },
                { "error", adapterLevel("error", 40, "\n        Delegate an error call to the underlying logger.\n        ") },
                { "exception", method("LoggerAdapter", "exception", { py_positional("msg"), py_keyword("exc_info", boolean(true)) }, [](std::vector<py> const& a)
                    {
                        py const kwargs = builtin("dict")(a[4]);
                        kwargs.set_item(S("exc_info"), a[2]);
                        call_kw(a[0].attr("log"), args_of({ py{ 40 }, a[1] }, a[3]), kwargs);
                        return none();
                    }, true, true, "\n        Delegate an exception call to the underlying logger.\n        ") },
                { "critical", adapterLevel("critical", 50, "\n        Delegate a critical call to the underlying logger.\n        ") },
                { "log", method("LoggerAdapter", "log", { py_positional("level"), py_positional("msg") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.method("isEnabledFor", { a[1] }).truthy())
                        {
                            py const result = self.method("process", { a[2], a[4] });
                            call_kw(self.attr("logger").attr("log"), args_of({ a[1], result[py{ 0 }] }, a[3]), result[py{ 1 }]);
                        }
                        return none();
                    }, true, true, "\n        Delegate a log call to the underlying logger, after adding\n        contextual information from this adapter instance.\n        ") },
                { "isEnabledFor", method("LoggerAdapter", "isEnabledFor", { py_positional("level") }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("logger").method("isEnabledFor", { a[1] });
                    }) },
                { "setLevel", method("LoggerAdapter", "setLevel", { py_positional("level") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("logger").method("setLevel", { a[1] });
                        return none();
                    }) },
                { "getEffectiveLevel", method("LoggerAdapter", "getEffectiveLevel", {}, [](std::vector<py> const& a)
                    {
                        return a[0].attr("logger").method("getEffectiveLevel");
                    }) },
                { "hasHandlers", method("LoggerAdapter", "hasHandlers", {}, [](std::vector<py> const& a) { return a[0].attr("logger").method("hasHandlers"); }) },
                { "_log", method("LoggerAdapter", "_log", { py_positional("level"), py_positional("msg"), py_positional("args") }, [](std::vector<py> const& a)
                    {
                        return call_kw(a[0].attr("logger").attr("_log"), { a[1], a[2], a[3] }, a[4]);
                    }, false, true) },
                { "manager", builtin("property")(method("LoggerAdapter", "manager", {}, [](std::vector<py> const& a) { return a[0].attr("logger").attr("manager"); }))
                    .method("setter", { method("LoggerAdapter", "manager", { py_positional("value") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("logger").set_attr("manager", a[1]);
                        return none();
                    }) }) },
                { "name", builtin("property")(method("LoggerAdapter", "name", {}, [](std::vector<py> const& a) { return a[0].attr("logger").attr("name"); })) },
                { "__repr__", method("LoggerAdapter", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const logger = a[0].attr("logger");
                        py const levelName = get_level_name(logger.method("getEffectiveLevel"));
                        return S("<%s %s (%s)>") % py_tuple({ a[0].attr("__class__").attr("__name__"), logger.attr("name"), levelName });
                    }) },
                { "__class_getitem__", builtin("classmethod")(import_attribute("types", "GenericAlias")) } },
                "\n    An adapter for loggers which makes it easier to specify contextual\n    information in logging output.\n    ");
        }

        // --- configuration, and the module's functions

        void basic_config(py const& aKwargs)
        {
            with_module_lock([&]()
            {
                auto const& s = state();
                py const root = G("root");
                py const force = aKwargs.method("pop", { S("force"), boolean(false) });
                py encoding = aKwargs.method("pop", { S("encoding"), none() });
                py errors = aKwargs.method("pop", { S("errors"), S("backslashreplace") });
                if (force.truthy())
                    for (auto const& h : slice(root.attr("handlers"), std::nullopt).items())
                    {
                        root.method("removeHandler", { h });
                        h.method("close");
                    }
                if (root.attr("handlers").size() == 0)
                {
                    py handlers = aKwargs.method("pop", { S("handlers"), none() });
                    if (handlers.is_none())
                    {
                        if (aKwargs.contains(S("stream")) && aKwargs.contains(S("filename")))
                            py_raise("ValueError", "'stream' and 'filename' should not be specified together");
                    }
                    else if (aKwargs.contains(S("stream")) || aKwargs.contains(S("filename")))
                        py_raise("ValueError", "'stream' or 'filename' should not be specified together with 'handlers'");
                    if (handlers.is_none())
                    {
                        py const filename = aKwargs.method("pop", { S("filename"), none() });
                        py const mode = aKwargs.method("pop", { S("filemode"), S("a") });
                        py h;
                        if (filename.truthy())
                        {
                            if (mode.contains(S("b")))
                                errors = none();
                            else
                                encoding = import_attribute("io", "text_encoding")(encoding);
                            h = cls("FileHandler").call({ filename, mode }, { { "encoding", encoding }, { "errors", errors } });
                        }
                        else
                        {
                            py const stream = aKwargs.method("pop", { S("stream"), none() });
                            h = cls("StreamHandler")(stream);
                        }
                        handlers = py_list({ h });
                    }
                    py const dfs = aKwargs.method("pop", { S("datefmt"), none() });
                    py const style = aKwargs.method("pop", { S("style"), S("%") });
                    if (!s.styles.contains(style))
                        py_raise("ValueError", (S("Style must be one of: %s") % py_tuple({ join(",", s.styles.method("keys")) })).str());
                    py const fs = aKwargs.method("pop", { S("format"), s.styles[style][py{ 1 }] });
                    py const fmt = cls("Formatter")(fs, dfs, style);
                    for (auto const& h : handlers.items())
                    {
                        if (h.attr("formatter").is_none())
                            h.method("setFormatter", { fmt });
                        root.method("addHandler", { h });
                    }
                    py const level = aKwargs.method("pop", { S("level"), none() });
                    if (!level.is_none())
                        root.method("setLevel", { level });
                    if (aKwargs.truthy())
                        py_raise("ValueError", (S("Unrecognised argument(s): %s") % py_tuple({ join(", ", aKwargs.method("keys")) })).str());
                }
            });
        }

        // (critical, error, ...: the root logger's (basicConfig first, if it has no handlers))
        py root_level_function(char const* aName, char const* aMethod, char const* aDoc)
        {
            std::string const methodName = aMethod;
            return bound_function(aName, aName, { py_positional("msg") }, [methodName](std::vector<py> const& a)
            {
                py const root = G("root");
                if (root.attr("handlers").size() == 0)
                    basic_config(py_dict());
                call_kw(root.attr(methodName.c_str()), args_of({ a[0] }, a[1]), a[2]);
                return none();
            }, true, true, aDoc);
        }

        void shutdown(py const& aHandlerList)
        {
            auto const items = builtin("reversed")(slice(aHandlerList, std::nullopt)).items();
            for (auto const& wr : items)
            {
                try
                {
                    py const h = wr();
                    if (h.truthy())
                    {
                        try
                        {
                            h.method("acquire");
                            try
                            {
                                if (get_attr(h, S("flushOnClose"), boolean(true)).truthy())
                                    h.method("flush");
                                h.method("close");
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "OSError", "ValueError" }))
                                    throw;
                            }
                        }
                        catch (raised const&)
                        {
                            py const e = py_take_exception();
                            h.method("release");
                            py_reraise(e);
                        }
                        h.method("release");
                    }
                }
                catch (raised const&)
                {
                    if (G("raiseExceptions").truthy())
                        throw;
                    py_take_exception();
                }
            }
        }

        bool logging_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                auto& g = s.globals;
                if (!s.made)
                {
                    s.made = true;
                    g["raiseExceptions"] = boolean(true);
                    g["logThreads"] = boolean(true);
                    g["logMultiprocessing"] = boolean(true);
                    g["logProcesses"] = boolean(true);
                    g["logAsyncioTasks"] = boolean(true);
                    s.startTime = import_attribute("time", "time_ns")();
                    s.levelToName = py_dict();
                    s.nameToLevel = py_dict();
                    for (auto const& [name, value] : std::initializer_list<std::pair<char const*, long long>>{ { "CRITICAL", 50 }, { "ERROR", 40 }, { "WARNING", 30 },
                        { "INFO", 20 }, { "DEBUG", 10 }, { "NOTSET", 0 } })
                        s.levelToName.set_item(py{ value }, S(name));
                    for (auto const& [name, value] : std::initializer_list<std::pair<char const*, long long>>{ { "CRITICAL", 50 }, { "FATAL", 50 }, { "ERROR", 40 },
                        { "WARN", 30 }, { "WARNING", 30 }, { "INFO", 20 }, { "DEBUG", 10 }, { "NOTSET", 0 } })
                        s.nameToLevel.set_item(S(name), py{ value });
                    // (the module's functions are not Python's: no frame is its source's)
                    g["_srcfile"] = os_path("normcase")(S("<frozen logging/__init__>"));
                    s.lock = import_attribute("threading", "RLock")();
                    s.handlers = import_attribute("weakref", "WeakValueDictionary")();
                    s.handlerList = py_list();
                    s.removeHandlerRef = bound_function("_removeHandlerRef", "_removeHandlerRef", { py_positional("wr") }, [](std::vector<py> const& a)
                    {
                        py const handlers = state().handlerList;
                        py const lock = state().lock;
                        if (lock.truthy() && handlers.truthy())
                            with_lock(lock, [&]()
                            {
                                try
                                {
                                    handlers.method("remove", { a[0] });
                                }
                                catch (raised const&)
                                {
                                    if (!caught({ "ValueError" }))
                                        throw;
                                }
                            });
                        return none();
                    }, false, false, "\n    Remove a handler reference from the internal cleanup list.\n    ");
                    if (has_attr(py{ fn_import("os") }, S("register_at_fork")))
                        s.atForkWeakset = import_attribute("weakref", "WeakSet")();
                    s.strFormatter = import_attribute("string", "Formatter")();
                    make_log_record();
                    g["_logRecordFactory"] = cls("LogRecord");
                    make_styles();
                    make_formatters();
                    make_filters();
                    make_handlers();
                    make_manager();
                    make_loggers();
                    g["_loggerClass"] = cls("Logger");
                    g["root"] = cls("RootLogger")(py{ 30 });
                    cls("Logger").set_attr("root", g["root"]);
                    cls("Logger").set_attr("manager", cls("Manager")(g["root"]));
                    g["_warnings_showwarning"] = none();
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                auto const function_ = [&](char const* aName, std::vector<py_parameter> aParameters, body_type aBody, char const* aDoc = "", bool aVarArgs = false,
                    bool aVarKeywords = false)
                {
                    add(aName, bound_function(aName, aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords, aDoc));
                };
                add("__doc__", S("\nLogging package for Python. Based on PEP 282 and comments thereto in\ncomp.lang.python.\n\n"
                    "Copyright (C) 2001-2022 Vinay Sajip. All Rights Reserved.\n\nTo use, simply 'import logging' and log away!\n"));
                std::vector<py> all;
                for (auto n : { "BASIC_FORMAT", "BufferingFormatter", "CRITICAL", "DEBUG", "ERROR", "FATAL", "FileHandler", "Filter", "Formatter", "Handler", "INFO",
                    "LogRecord", "Logger", "LoggerAdapter", "NOTSET", "NullHandler", "StreamHandler", "WARN", "WARNING", "addLevelName", "basicConfig",
                    "captureWarnings", "critical", "debug", "disable", "error", "exception", "fatal", "getLevelName", "getLogger", "getLoggerClass", "info", "log",
                    "makeLogRecord", "setLoggerClass", "shutdown", "warn", "warning", "getLogRecordFactory", "setLogRecordFactory", "lastResort",
                    "raiseExceptions", "getLevelNamesMapping", "getHandlerByName", "getHandlerNames" })
                    all.emplace_back(n);
                add("__all__", py_list(all));
                add("__author__", S("Vinay Sajip <vinay_sajip@red-dove.com>"));
                add("__status__", S("production"));
                add("__version__", S("0.5.1.2"));
                add("__date__", S("07 February 2010"));
                for (auto const& [name, value] : g)
                    add(name.c_str(), value);
                add("_startTime", s.startTime);
                for (auto const& [name, value] : std::initializer_list<std::pair<char const*, long long>>{ { "CRITICAL", 50 }, { "FATAL", 50 }, { "ERROR", 40 },
                    { "WARNING", 30 }, { "WARN", 30 }, { "INFO", 20 }, { "DEBUG", 10 }, { "NOTSET", 0 } })
                    add(name, py{ value });
                add("_levelToName", s.levelToName);
                add("_nameToLevel", s.nameToLevel);
                add("_lock", s.lock);
                add("_handlers", s.handlers);
                add("_handlerList", s.handlerList);
                add("_removeHandlerRef", s.removeHandlerRef);
                add("_STYLES", s.styles);
                add("_str_formatter", s.strFormatter);
                for (auto const& [name, c] : s.classes)
                    add(name.c_str(), c);
                function_("getLevelNamesMapping", {}, [](std::vector<py> const&) { return state().nameToLevel.method("copy"); });
                function_("getLevelName", { py_positional("level") }, [](std::vector<py> const& a) { return get_level_name(a[0]); },
                    "\n    Return the textual or numeric representation of logging level 'level'.\n\n    If the level is one of the predefined levels (CRITICAL, ERROR, WARNING,\n"
                    "    INFO, DEBUG) then you get the corresponding string. If you have\n    associated levels with names using addLevelName then the name you have\n"
                    "    associated with 'level' is returned.\n\n    If a numeric value corresponding to one of the defined levels is passed\n"
                    "    in, the corresponding string representation is returned.\n\n    If a string representation of the level is passed in, the corresponding\n"
                    "    numeric value is returned.\n\n    If no matching numeric or string value is passed in, the string\n    'Level %s' % level is returned.\n    ");
                function_("addLevelName", { py_positional("level"), py_positional("levelName") }, [](std::vector<py> const& a)
                {
                    with_module_lock([&]()
                    {
                        state().levelToName.set_item(a[0], a[1]);
                        state().nameToLevel.set_item(a[1], a[0]);
                    });
                    return none();
                }, "\n    Associate 'levelName' with 'level'.\n\n    This is used when converting levels to text during message formatting.\n    ");
                add("currentframe", bound_function("<lambda>", "<lambda>", {}, [](std::vector<py> const&) { return sys_attr("_getframe")(py{ 0 }); }));
                function_("_is_internal_frame", { py_positional("frame") }, [](std::vector<py> const& a) { return boolean(is_internal_frame(a[0])); },
                    "Signal whether the frame is a CPython or logging module internal.");
                function_("_checkLevel", { py_positional("level") }, [](std::vector<py> const& a) { return check_level(a[0]); });
                function_("_prepareFork", {}, [](std::vector<py> const&)
                {
                    try
                    {
                        state().lock.method("acquire");
                    }
                    catch (raised const&)
                    {
                        py const e = py_take_exception();
                        state().lock.method("release");
                        py_reraise(e);
                    }
                    return none();
                }, "\n    Prepare to fork a new child process by acquiring the module-level lock.\n\n    This should be used in conjunction with _afterFork().\n    ");
                function_("_afterFork", {}, [](std::vector<py> const&)
                {
                    state().lock.method("release");
                    return none();
                }, "\n    After a new child process has been forked, release the module-level lock.\n\n    This should be used in conjunction with _prepareFork().\n    ");
                function_("_register_at_fork_reinit_lock", { py_positional("instance") }, [](std::vector<py> const& a)
                {
                    register_at_fork_reinit_lock(a[0]);
                    return none();
                });
                if (!s.atForkWeakset.empty())
                    add("_at_fork_reinit_lock_weakset", s.atForkWeakset);
                function_("setLogRecordFactory", { py_positional("factory") }, [](std::vector<py> const& a)
                {
                    set_G("_logRecordFactory", a[0]);
                    return none();
                }, "\n    Set the factory to be used when instantiating a log record.\n\n    :param factory: A callable which will be called to instantiate\n    a log record.\n    ");
                function_("getLogRecordFactory", {}, [](std::vector<py> const&) { return G("_logRecordFactory"); },
                    "\n    Return the factory to be used when instantiating a log record.\n    ");
                function_("makeLogRecord", { py_positional("dict") }, [](std::vector<py> const& a)
                {
                    py const rv = G("_logRecordFactory")(none(), none(), S(""), py{ 0 }, S(""), py_tuple(), none(), none());
                    rv.attr("__dict__").method("update", { a[0] });
                    return rv;
                }, "\n    Make a LogRecord whose attributes are defined by the specified dictionary,\n    This function is useful for converting a logging event received over\n"
                    "    a socket connection (which is sent as a dictionary) into a LogRecord\n    instance.\n    ");
                function_("_addHandlerRef", { py_positional("handler") }, [](std::vector<py> const& a)
                {
                    add_handler_ref(a[0]);
                    return none();
                }, "\n    Add a handler to the internal cleanup list using a weak reference.\n    ");
                function_("getHandlerByName", { py_positional("name") }, [](std::vector<py> const& a) { return state().handlers.method("get", { a[0] }); },
                    "\n    Get a handler with the specified *name*, or None if there isn't one with\n    that name.\n    ");
                function_("getHandlerNames", {}, [](std::vector<py> const&) { return builtin("frozenset")(state().handlers); },
                    "\n    Return all known handler names as an immutable set.\n    ");
                function_("setLoggerClass", { py_positional("klass") }, [](std::vector<py> const& a)
                {
                    check_logger_class(a[0]);
                    set_G("_loggerClass", a[0]);
                    return none();
                }, "\n    Set the class to be used when instantiating a logger. The class should\n    define __init__() such that only a name argument is required, and the\n"
                    "    __init__() should call Logger.__init__()\n    ");
                function_("getLoggerClass", {}, [](std::vector<py> const&) { return G("_loggerClass"); },
                    "\n    Return the class to be used when instantiating a logger.\n    ");
                function_("basicConfig", {}, [](std::vector<py> const& a)
                {
                    basic_config(a[0]);
                    return none();
                }, "\n    Do basic configuration for the logging system.\n\n    This function does nothing if the root logger already has handlers\n"
                    "    configured, unless the keyword argument *force* is set to ``True``.\n    It is a convenience method intended for use by simple scripts\n"
                    "    to do one-shot configuration of the logging package.\n    ", false, true);
                function_("getLogger", { py_positional("name", none()) }, [](std::vector<py> const& a)
                {
                    py const root = G("root");
                    if (!a[0].truthy() || (is_str(a[0]) && a[0] == root.attr("name")))
                        return root;
                    return cls("Logger").attr("manager").method("getLogger", { a[0] });
                }, "\n    Return a logger with the specified name, creating it if necessary.\n\n    If no name is specified, return the root logger.\n    ");
                add("critical", root_level_function("critical", "critical", "\n    Log a message with severity 'CRITICAL' on the root logger. If the logger\n"
                    "    has no handlers, call basicConfig() to add a console handler with a\n    pre-defined format.\n    "));
                add("fatal", root_level_function("fatal", "critical", "\n    Don't use this function, use critical() instead.\n    "));
                add("error", root_level_function("error", "error", "\n    Log a message with severity 'ERROR' on the root logger. If the logger has\n"
                    "    no handlers, call basicConfig() to add a console handler with a pre-defined\n    format.\n    "));
                function_("exception", { py_positional("msg"), py_keyword("exc_info", boolean(true)) }, [](std::vector<py> const& a)
                {
                    py const kwargs = builtin("dict")(a[3]);
                    kwargs.set_item(S("exc_info"), a[1]);
                    call_kw(G("error"), args_of({ a[0] }, a[2]), kwargs);
                    return none();
                }, "\n    Log a message with severity 'ERROR' on the root logger, with exception\n    information. If the logger has no handlers, basicConfig() is called to add\n"
                    "    a console handler with a pre-defined format.\n    ", true, true);
                add("warning", root_level_function("warning", "warning", "\n    Log a message with severity 'WARNING' on the root logger. If the logger has\n"
                    "    no handlers, call basicConfig() to add a console handler with a pre-defined\n    format.\n    "));
                function_("warn", { py_positional("msg") }, [](std::vector<py> const& a)
                {
                    import_attribute("warnings", "warn")(S("The 'warn' function is deprecated, use 'warning' instead"), builtin("DeprecationWarning"), py{ 2 });
                    call_kw(G("warning"), args_of({ a[0] }, a[1]), a[2]);
                    return none();
                }, "", true, true);
                add("info", root_level_function("info", "info", "\n    Log a message with severity 'INFO' on the root logger. If the logger has\n"
                    "    no handlers, call basicConfig() to add a console handler with a pre-defined\n    format.\n    "));
                add("debug", root_level_function("debug", "debug", "\n    Log a message with severity 'DEBUG' on the root logger. If the logger has\n"
                    "    no handlers, call basicConfig() to add a console handler with a pre-defined\n    format.\n    "));
                function_("log", { py_positional("level"), py_positional("msg") }, [](std::vector<py> const& a)
                {
                    py const root = G("root");
                    if (root.attr("handlers").size() == 0)
                        basic_config(py_dict());
                    call_kw(root.attr("log"), args_of({ a[0], a[1] }, a[2]), a[3]);
                    return none();
                }, "\n    Log 'msg % args' with the integer severity 'level' on the root logger. If\n    the logger has no handlers, call basicConfig() to add a console handler\n"
                    "    with a pre-defined format.\n    ", true, true);
                function_("disable", { py_positional("level", py{ 50 }) }, [](std::vector<py> const& a)
                {
                    py const root = G("root");
                    root.attr("manager").set_attr("disable", a[0]);
                    root.attr("manager").method("_clear_cache");
                    return none();
                }, "\n    Disable all logging calls of severity 'level' and below.\n    ");
                py const shutdownFunction = bound_function("shutdown", "shutdown", { py_positional("handlerList", s.handlerList) }, [](std::vector<py> const& a)
                {
                    shutdown(a[0]);
                    return none();
                }, false, false, "\n    Perform any cleanup actions in the logging system (e.g. flushing\n    buffers).\n\n    Should be called at application exit.\n    ");
                add("shutdown", shutdownFunction);
                function_("_showwarning", { py_positional("message"), py_positional("category"), py_positional("filename"), py_positional("lineno"),
                    py_positional("file", none()), py_positional("line", none()) }, [](std::vector<py> const& a)
                {
                    if (!a[4].is_none())
                    {
                        py const original = G("_warnings_showwarning");
                        if (!original.is_none())
                            original(a[0], a[1], a[2], a[3], a[4], a[5]);
                    }
                    else
                    {
                        py const text = import_attribute("warnings", "formatwarning")(a[0], a[1], a[2], a[3], a[5]);
                        py const logger = G("getLogger")(S("py.warnings"));
                        if (!logger.attr("handlers").truthy())
                            logger.method("addHandler", { cls("NullHandler")() });
                        logger.method("warning", { builtin("str")(text) });
                    }
                    return none();
                }, "\n    Implementation of showwarnings which redirects to logging, which will first\n    check to see if the file parameter is None. If a file is specified, it will\n"
                    "    delegate to the original warnings implementation of showwarning. Otherwise,\n    it will call warnings.formatwarning and will log the resulting string to a\n"
                    "    warnings logger named \"py.warnings\" with level logging.WARNING.\n    ");
                function_("captureWarnings", { py_positional("capture") }, [](std::vector<py> const& a)
                {
                    py const warnings = py{ fn_import("warnings") };
                    if (a[0].truthy())
                    {
                        if (G("_warnings_showwarning").is_none())
                        {
                            set_G("_warnings_showwarning", warnings.attr("showwarning"));
                            warnings.set_attr("showwarning", G("_showwarning"));
                        }
                    }
                    else if (!G("_warnings_showwarning").is_none())
                    {
                        warnings.set_attr("showwarning", G("_warnings_showwarning"));
                        set_G("_warnings_showwarning", none());
                    }
                    return none();
                }, "\n    If capture is true, redirect all warnings to the logging package.\n    If capture is False, ensure that warnings are not redirected to logging\n"
                    "    but to their original destinations.\n    ");
                for (auto module : { "sys", "os", "time", "io", "re", "traceback", "warnings", "weakref", "collections", "threading", "atexit" })
                    add(module, py{ fn_import(module) });
                add("GenericAlias", import_attribute("types", "GenericAlias"));
                add("Template", import_attribute("string", "Template"));
                // (shutdown on exit (once))
                static bool sRegistered = false;
                if (!sRegistered)
                {
                    sRegistered = true;
                    import_attribute("atexit", "register")(shutdownFunction);
                    if (!s.atForkWeakset.empty())
                    {
                        py const afterChild = bound_function("_after_at_fork_child_reinit_locks", "_after_at_fork_child_reinit_locks", {}, [](std::vector<py> const&)
                        {
                            for (auto const& handler : state().atForkWeakset.items())
                                handler.method("_at_fork_reinit");
                            state().lock.method("_at_fork_reinit");
                            return none();
                        });
                        py const prepare = bound_function("_prepareFork", "_prepareFork", {}, [](std::vector<py> const&)
                        {
                            state().lock.method("acquire");
                            return none();
                        });
                        py const after = bound_function("_afterFork", "_afterFork", {}, [](std::vector<py> const&)
                        {
                            state().lock.method("release");
                            return none();
                        });
                        import_attribute("os", "register_at_fork").call({}, { { "before", prepare }, { "after_in_child", afterChild }, { "after_in_parent", after } });
                    }
                }
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sLogging = register_module("logging", logging_module);
    }
}
