/*
  logging_handlers.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/logging/handlers.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).

  Lib/logging/handlers.py's copyright and permission notice:

  Copyright 2001-2021 by Vinay Sajip. All Rights Reserved.

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

// The module logging.handlers (CPython 3.13's Lib/logging/handlers.py): BaseRotatingHandler, RotatingFileHandler, TimedRotatingFileHandler,
// WatchedFileHandler, SocketHandler, DatagramHandler, SysLogHandler, SMTPHandler, NTEventLogHandler, HTTPHandler, BufferingHandler,
// MemoryHandler, QueueHandler and QueueListener; its classes are made (by type) of closures, so a program's subclasses override their methods.
// The modules that handlers.py imports are the module's attributes (as they are its), except pickle (which the runtime does not have yet):
// SocketHandler.makePickle imports it when it is called. The modules that SMTPHandler, HTTPHandler and NTEventLogHandler import are
// imported when they are used (as handlers.py imports them).

#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "logging_handlers_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "logging.handlers";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;     ///< (the module's classes, by name)
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py cls(char const* aName) { return state().classes.at(aName); }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py S(char const* aText) { return py{ aText }; }
        char const* D(char const* aName)
        {
            auto const doc = logging_handlers_docs::doc(aName);
            return doc != nullptr ? doc : "";
        }

        // (a module's attribute (as handlers.py's "logging.X", "os.X", ...: looked up when it is used))
        py M(char const* aModule, char const* aName) { return import_attribute(aModule, aName); }
        py logging_(char const* aName) { return M("logging", aName); }
        py os_path(char const* aName) { return py{ fn_import("os") }.attr("path").attr(aName); }
        bool callable(py const& aValue) { return builtin("callable")(aValue).truthy(); }
        bool has_attr(py const& aObject, char const* aName) { return builtin("hasattr")(aObject, S(aName)).truthy(); }
        py slice(py const& aSequence, py const& aStart, py const& aStop)
        {
            return aSequence[py{ make_slice(aStart.get(), aStop.get(), nullptr) }];
        }

        // (a closure as a function of the module)
        using body_type = std::function<py(std::vector<py> const&)>;
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, [aQualname, aParameters, aBody, aVarArgs](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                return aBody(a);
            });
        }
        // (a method: its parameters after self (aClass: its class's name); its docstring: handlers.py's)
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false)
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            std::string const qualname = std::string{ aClass } + "." + aName;
            return bound_function(aName, qualname, std::move(aParameters), std::move(aBody), aVarArgs, D(qualname.c_str()));
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: type(aName, aBases, namespace); its docstring: handlers.py's)
        py make_handlers_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace)
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S(sModule));
            ns.set_item(S("__qualname__"), S(aName));
            if (auto const doc = logging_handlers_docs::doc(aName))
                ns.set_item(S("__doc__"), S(doc));
            for (auto const& [name, value] : aNamespace)
                ns.set_item(S(name), value);
            py const result = builtin("type")(S(aName), py_tuple(aBases), ns);
            state().classes[aName] = result;
            return result;
        }

        // (with aLock: aBody())
        template <typename F>
        void with_lock(py const& aLock, F&& aBody)
        {
            aLock.method("__enter__");
            try
            {
                aBody();
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                aLock.method("__exit__", { type_of(e), e, e.attr("__traceback__") });
                py_reraise(e);
            }
            aLock.method("__exit__", { none(), none(), none() });
        }

        // (try: aBody() finally: aFinally())
        template <typename F, typename G>
        void try_finally(F&& aBody, G&& aFinally)
        {
            try
            {
                aBody();
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                aFinally();
                py_reraise(e);
            }
            aFinally();
        }

        // (try: aBody() except aClass: aHandler(e) (sys.exception() is e while it runs); another exception remains raised)
        template <typename F, typename H>
        void try_except(F&& aBody, py const& aClass, H&& aHandler)
        {
            try
            {
                aBody();
            }
            catch (raised const&)
            {
                std::string const text = error_text();
                clear_error();
                ref const caughtException{ fn_caught(text.c_str()) };
                py const e = py::borrowed(caughtException.get());
                if (!isinstance(e, aClass))
                {
                    fn_handled();
                    py_reraise(e);
                }
                try
                {
                    aHandler(e);
                }
                catch (raised const&)
                {
                    fn_handled();
                    throw;
                }
                fn_handled();
            }
        }
        // (try: aBody() except Exception: self.handleError(record))
        template <typename F>
        void or_handle_error(py const& aSelf, py const& aRecord, F&& aBody)
        {
            try_except(std::forward<F>(aBody), builtin("Exception"), [&](py const&) { aSelf.method("handleError", { aRecord }); });
        }

        constexpr long long Midnight = 24 * 60 * 60;    // (_MIDNIGHT: the number of seconds in a day)

        void make_rotating_handlers()
        {
            py const fileHandler = logging_("FileHandler");
            make_handlers_class("BaseRotatingHandler", { fileHandler }, {
                { "namer", none() },
                { "rotator", none() },
                { "__init__", method("BaseRotatingHandler", "__init__", { py_positional("filename"), py_positional("mode"), py_positional("encoding", none()),
                    py_positional("delay", boolean(false)), py_positional("errors", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        logging_("FileHandler").attr("__init__").call({ self, a[1] }, { { "mode", a[2] }, { "encoding", a[3] }, { "delay", a[4] }, { "errors", a[5] } });
                        self.set_attr("mode", a[2]);
                        self.set_attr("encoding", a[3]);
                        self.set_attr("errors", a[5]);
                        return none();
                    }) },
                { "emit", method("BaseRotatingHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        or_handle_error(self, a[1], [&]()
                        {
                            if (self.method("shouldRollover", { a[1] }).truthy())
                                self.method("doRollover");
                            logging_("FileHandler").attr("emit")(self, a[1]);
                        });
                        return none();
                    }) },
                { "rotation_filename", method("BaseRotatingHandler", "rotation_filename", { py_positional("default_name") }, [](std::vector<py> const& a)
                    {
                        py const namer = a[0].attr("namer");
                        if (!callable(namer))
                            return a[1];
                        return namer(a[1]);
                    }) },
                { "rotate", method("BaseRotatingHandler", "rotate", { py_positional("source"), py_positional("dest") }, [](std::vector<py> const& a)
                    {
                        py const rotator = a[0].attr("rotator");
                        if (!callable(rotator))
                        {
                            // (Issue 18940: a file may not have been created if delay is True)
                            if (os_path("exists")(a[1]).truthy())
                                M("os", "rename")(a[1], a[2]);
                        }
                        else
                            rotator(a[1], a[2]);
                        return none();
                    }) } });
            py const baseRotatingHandler = cls("BaseRotatingHandler");

            make_handlers_class("RotatingFileHandler", { baseRotatingHandler }, {
                { "__init__", method("RotatingFileHandler", "__init__", { py_positional("filename"), py_positional("mode", S("a")), py_positional("maxBytes", py{ 0 }),
                    py_positional("backupCount", py{ 0 }), py_positional("encoding", none()), py_positional("delay", boolean(false)), py_positional("errors", none()) },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (rollover: in mode 'a' (a mode such as 'w' would lose the logs of a program's previous runs))
                        py mode = a[2];
                        py encoding = a[5];
                        if (a[3] > py{ 0 })
                            mode = S("a");
                        if (!mode.contains(S("b")))
                            encoding = M("io", "text_encoding")(encoding);
                        cls("BaseRotatingHandler").attr("__init__").call({ self, a[1], mode }, { { "encoding", encoding }, { "delay", a[6] }, { "errors", a[7] } });
                        self.set_attr("maxBytes", a[3]);
                        self.set_attr("backupCount", a[4]);
                        return none();
                    }) },
                { "doRollover", method("RotatingFileHandler", "doRollover", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("stream").truthy())
                        {
                            self.attr("stream").method("close");
                            self.set_attr("stream", none());
                        }
                        py const backupCount = self.attr("backupCount");
                        if (backupCount > py{ 0 })
                        {
                            py const exists = os_path("exists");
                            for (py const& i : builtin("range")(backupCount - py{ 1 }, py{ 0 }, py{ -1 }).items())
                            {
                                py const sfn = self.method("rotation_filename", { S("%s.%d") % py_tuple({ self.attr("baseFilename"), i }) });
                                py const dfn = self.method("rotation_filename", { S("%s.%d") % py_tuple({ self.attr("baseFilename"), i + py{ 1 } }) });
                                if (exists(sfn).truthy())
                                {
                                    if (exists(dfn).truthy())
                                        M("os", "remove")(dfn);
                                    M("os", "rename")(sfn, dfn);
                                }
                            }
                            py const dfn = self.method("rotation_filename", { self.attr("baseFilename") + S(".1") });
                            if (exists(dfn).truthy())
                                M("os", "remove")(dfn);
                            self.method("rotate", { self.attr("baseFilename"), dfn });
                        }
                        if (!self.attr("delay").truthy())
                            self.set_attr("stream", self.method("_open"));
                        return none();
                    }) },
                { "shouldRollover", method("RotatingFileHandler", "shouldRollover", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("stream").is_none())     // (delay was set)
                            self.set_attr("stream", self.method("_open"));
                        if (self.attr("maxBytes") > py{ 0 })     // (rolling over)
                        {
                            py pos;
                            bool unsupported = false;
                            try_except([&]() { pos = self.attr("stream").method("tell"); }, M("io", "UnsupportedOperation"), [&](py const&) { unsupported = true; });
                            // (gh-143237: a named pipe is never rolled over)
                            if (unsupported)
                                return boolean(false);
                            // (gh-116263: an empty file is never rolled over)
                            if (!pos.truthy())
                                return boolean(false);
                            py const msg = S("%s\n") % py_tuple({ self.method("format", { a[1] }) });
                            if (pos + py{ msg.size() } >= self.attr("maxBytes"))
                            {
                                // (bpo-45401: only a regular file is rolled over)
                                if (os_path("exists")(self.attr("baseFilename")).truthy() && !os_path("isfile")(self.attr("baseFilename")).truthy())
                                    return boolean(false);
                                return boolean(true);
                            }
                        }
                        return boolean(false);
                    }) } });

            make_handlers_class("TimedRotatingFileHandler", { baseRotatingHandler }, {
                { "__init__", method("TimedRotatingFileHandler", "__init__", { py_positional("filename"), py_positional("when", S("h")), py_positional("interval", py{ 1 }),
                    py_positional("backupCount", py{ 0 }), py_positional("encoding", none()), py_positional("delay", boolean(false)), py_positional("utc", boolean(false)),
                    py_positional("atTime", none()), py_positional("errors", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const encoding = M("io", "text_encoding")(a[5]);
                        cls("BaseRotatingHandler").attr("__init__").call({ self, a[1], S("a") }, { { "encoding", encoding }, { "delay", a[6] }, { "errors", a[9] } });
                        self.set_attr("when", a[2].method("upper"));
                        self.set_attr("backupCount", a[4]);
                        self.set_attr("utc", a[7]);
                        self.set_attr("atTime", a[8]);
                        // (the rollover interval (the number of seconds between rollovers) and the suffix of a rolled over file's name: by 'when'
                        // (S: seconds, M: minutes, H: hours, D: days, midnight: at midnight, W{0-6}: on a day of the week (0: Monday)), in
                        // either case)
                        py const when = self.attr("when");
                        char const* extMatch = nullptr;
                        if (when == S("S"))
                        {
                            self.set_attr("interval", py{ 1 });
                            self.set_attr("suffix", S("%Y-%m-%d_%H-%M-%S"));
                            extMatch = R"((?<!\d)\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}(?!\d))";
                        }
                        else if (when == S("M"))
                        {
                            self.set_attr("interval", py{ 60 });
                            self.set_attr("suffix", S("%Y-%m-%d_%H-%M"));
                            extMatch = R"((?<!\d)\d{4}-\d{2}-\d{2}_\d{2}-\d{2}(?!\d))";
                        }
                        else if (when == S("H"))
                        {
                            self.set_attr("interval", py{ 60 * 60 });
                            self.set_attr("suffix", S("%Y-%m-%d_%H"));
                            extMatch = R"((?<!\d)\d{4}-\d{2}-\d{2}_\d{2}(?!\d))";
                        }
                        else if (when == S("D") || when == S("MIDNIGHT"))
                        {
                            self.set_attr("interval", py{ 60 * 60 * 24 });
                            self.set_attr("suffix", S("%Y-%m-%d"));
                            extMatch = R"((?<!\d)\d{4}-\d{2}-\d{2}(?!\d))";
                        }
                        else if (when.method("startswith", { S("W") }).truthy())
                        {
                            self.set_attr("interval", py{ 60 * 60 * 24 * 7 });
                            if (when.size() != 2)
                                py_raise("ValueError", (S("You must specify a day for weekly rollover from 0 to 6 (0 is Monday): %s") % py_tuple({ when })).str());
                            if (when[py{ 1 }] < S("0") || when[py{ 1 }] > S("6"))
                                py_raise("ValueError", (S("Invalid day specified for weekly rollover: %s") % py_tuple({ when })).str());
                            self.set_attr("dayOfWeek", builtin("int")(when[py{ 1 }]));
                            self.set_attr("suffix", S("%Y-%m-%d"));
                            extMatch = R"((?<!\d)\d{4}-\d{2}-\d{2}(?!\d))";
                        }
                        else
                            py_raise("ValueError", (S("Invalid rollover interval specified: %s") % py_tuple({ when })).str());
                        // (extMatch: the pattern of a datetime suffix of a file's name (after custom naming, it is not necessarily separated from the
                        // rest of the name by periods: (?<!\d) and (?!\d) ensure that the suffix (which starts and ends with digits) is not preceded or
                        // followed by digits))
                        self.set_attr("extMatch", M("re", "compile")(S(extMatch), M("re", "ASCII")));
                        self.set_attr("interval", self.attr("interval") * a[3]);     // (in the units requested)
                        // (baseFilename: a str (the file name can be a path object: see Issue #27493))
                        py const filename = self.attr("baseFilename");
                        py t;
                        if (os_path("exists")(filename).truthy())
                            t = builtin("int")(M("os", "stat")(filename).attr("st_mtime"));
                        else
                            t = builtin("int")(M("time", "time")());
                        self.set_attr("rolloverAt", self.method("computeRollover", { t }));
                        return none();
                    }) },
                { "computeRollover", method("TimedRotatingFileHandler", "computeRollover", { py_positional("currentTime") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const currentTime = a[1];
                        py result = currentTime + self.attr("interval");
                        // (rolling over at midnight or weekly: the interval is known, but when the next one starts is not (it starts at midnight,
                        // not now): rolloverAt is adjusted for the first rollover (the interval for the others) (leap seconds are not considered))
                        py const when = self.attr("when");
                        if (when == S("MIDNIGHT") || when.method("startswith", { S("W") }).truthy())
                        {
                            py const t = self.attr("utc").truthy() ? M("time", "gmtime")(currentTime) : M("time", "localtime")(currentTime);
                            py const currentHour = t[py{ 3 }];
                            py const currentMinute = t[py{ 4 }];
                            py const currentSecond = t[py{ 5 }];
                            py currentDay = t[py{ 6 }];
                            // (r: the number of seconds from now until the next rotation)
                            py rotateTs;
                            if (self.attr("atTime").is_none())
                                rotateTs = py{ Midnight };
                            else
                            {
                                py const atTime = self.attr("atTime");
                                rotateTs = (atTime.attr("hour") * py{ 60 } + atTime.attr("minute")) * py{ 60 } + atTime.attr("second");
                            }
                            py r = rotateTs - ((currentHour * py{ 60 } + currentMinute) * py{ 60 } + currentSecond);
                            if (r <= py{ 0 })
                            {
                                // (the rotation time is before the current time (e.g. rotateAt is 13:45 and it is now 14:15): it is tomorrow)
                                r = r + py{ Midnight };
                                currentDay = (currentDay + py{ 1 }) % py{ 7 };
                            }
                            result = currentTime + r;
                            // (rolling over on a day: the number of days until the next rollover, less 1 (the time until the next day starts is
                            // calculated above): none if the day is today; if it is later in the week (e.g. today is day 2 (Wednesday) and the
                            // rollover is on day 6 (Sunday)), 6 - 2 - 1 (3); if it is earlier (e.g. today is day 5 (Saturday) and the rollover is
                            // on day 3 (Thursday)), 6 - 5 + 3 (4) (the days left in the week, then those until the day); a day is added in the
                            // latter cases (the time above is that of midnight: the start of the next day))
                            if (when.method("startswith", { S("W") }).truthy())
                            {
                                py const day = currentDay;   // (0: Monday)
                                py const dayOfWeek = self.attr("dayOfWeek");
                                if (day != dayOfWeek)
                                {
                                    py daysToWait;
                                    if (day < dayOfWeek)
                                        daysToWait = dayOfWeek - day;
                                    else
                                        daysToWait = py{ 6 } - day + dayOfWeek + py{ 1 };
                                    result = result + daysToWait * py{ Midnight };
                                }
                                result = result + self.attr("interval") - py{ Midnight * 7 };
                            }
                            else
                                result = result + self.attr("interval") - py{ Midnight };
                            if (!self.attr("utc").truthy())
                            {
                                py const dstNow = t[py{ -1 }];
                                py const dstAtRollover = M("time", "localtime")(result)[py{ -1 }];
                                if (dstNow != dstAtRollover)
                                {
                                    py addend;
                                    if (!dstNow.truthy())    // (DST starts before the next rollover: an hour is deducted)
                                    {
                                        addend = py{ -3600 };
                                        if (!M("time", "localtime")(result - py{ 3600 })[py{ -1 }].truthy())
                                            addend = py{ 0 };
                                    }
                                    else                    // (DST ends before the next rollover: an hour is added)
                                        addend = py{ 3600 };
                                    result = result + addend;
                                }
                            }
                        }
                        return result;
                    }) },
                { "shouldRollover", method("TimedRotatingFileHandler", "shouldRollover", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const t = builtin("int")(M("time", "time")());
                        if (t >= self.attr("rolloverAt"))
                        {
                            // (#89564: only a regular file is rolled over (its next rollover time is set, so that it is not checked repeatedly))
                            if (os_path("exists")(self.attr("baseFilename")).truthy() && !os_path("isfile")(self.attr("baseFilename")).truthy())
                            {
                                self.set_attr("rolloverAt", self.method("computeRollover", { t }));
                                return boolean(false);
                            }
                            return boolean(true);
                        }
                        return boolean(false);
                    }) },
                { "getFilesToDelete", method("TimedRotatingFileHandler", "getFilesToDelete", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const split = os_path("split")(self.attr("baseFilename"));
                        py const dirName = split[py{ 0 }];
                        py const baseName = split[py{ 1 }];
                        py const fileNames = M("os", "listdir")(dirName);
                        py result = py_list();
                        py const join = os_path("join");
                        if (self.attr("namer").is_none())
                        {
                            py const prefix = baseName + S(".");
                            py const plen = py{ prefix.size() };
                            for (py const& fileName : fileNames.items())
                                if (slice(fileName, none(), plen) == prefix)
                                {
                                    py const suffix = slice(fileName, plen, none());
                                    if (self.attr("extMatch").method("fullmatch", { suffix }).truthy())
                                        result.method("append", { join(dirName, fileName) });
                                }
                        }
                        else
                        {
                            for (py const& fileName : fileNames.items())
                            {
                                // (a file's name could be almost anything after custom naming, but it contains the datetime suffix: the suffix is
                                // found, and the name verified as one that the handler would make)
                                py m = self.attr("extMatch").method("search", { fileName });
                                while (m.truthy())
                                {
                                    py const dfn = self.attr("namer")(self.attr("baseFilename") + S(".") + m[py{ 0 }]);
                                    if (os_path("basename")(dfn) == fileName)
                                    {
                                        result.method("append", { join(dirName, fileName) });
                                        break;
                                    }
                                    m = self.attr("extMatch").method("search", { fileName, m.method("start") + py{ 1 } });
                                }
                            }
                        }
                        if (py{ result.size() } < self.attr("backupCount"))
                            result = py_list();
                        else
                        {
                            result.method("sort");
                            result = slice(result, none(), py{ result.size() } - self.attr("backupCount"));
                        }
                        return result;
                    }) },
                { "doRollover", method("TimedRotatingFileHandler", "doRollover", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (the time that the sequence started at, as a time tuple)
                        py const currentTime = builtin("int")(M("time", "time")());
                        py const t = self.attr("rolloverAt") - self.attr("interval");
                        py timeTuple;
                        if (self.attr("utc").truthy())
                            timeTuple = M("time", "gmtime")(t);
                        else
                        {
                            timeTuple = M("time", "localtime")(t);
                            py const dstNow = M("time", "localtime")(currentTime)[py{ -1 }];
                            py const dstThen = timeTuple[py{ -1 }];
                            if (dstNow != dstThen)
                            {
                                py const addend = dstNow.truthy() ? py{ 3600 } : py{ -3600 };
                                timeTuple = M("time", "localtime")(t + addend);
                            }
                        }
                        py const dfn = self.method("rotation_filename", { self.attr("baseFilename") + S(".") + M("time", "strftime")(self.attr("suffix"), timeTuple) });
                        if (os_path("exists")(dfn).truthy())
                            return none();      // (already rolled over)
                        if (self.attr("stream").truthy())
                        {
                            self.attr("stream").method("close");
                            self.set_attr("stream", none());
                        }
                        self.method("rotate", { self.attr("baseFilename"), dfn });
                        if (self.attr("backupCount") > py{ 0 })
                            for (py const& s : self.method("getFilesToDelete").items())
                                M("os", "remove")(s);
                        if (!self.attr("delay").truthy())
                            self.set_attr("stream", self.method("_open"));
                        self.set_attr("rolloverAt", self.method("computeRollover", { currentTime }));
                        return none();
                    }) } });

            make_handlers_class("WatchedFileHandler", { fileHandler }, {
                { "__init__", method("WatchedFileHandler", "__init__", { py_positional("filename"), py_positional("mode", S("a")), py_positional("encoding", none()),
                    py_positional("delay", boolean(false)), py_positional("errors", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py encoding = a[3];
                        if (!a[2].contains(S("b")))
                            encoding = M("io", "text_encoding")(encoding);
                        logging_("FileHandler").attr("__init__").call({ self, a[1] }, { { "mode", a[2] }, { "encoding", encoding }, { "delay", a[4] }, { "errors", a[5] } });
                        self.set_attr("dev", py{ -1 });
                        self.set_attr("ino", py{ -1 });
                        self.method("_statstream");
                        return none();
                    }) },
                { "_statstream", method("WatchedFileHandler", "_statstream", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("stream").is_none())
                            return none();
                        py const sres = M("os", "fstat")(self.attr("stream").method("fileno"));
                        self.set_attr("dev", sres.attr("st_dev"));
                        self.set_attr("ino", sres.attr("st_ino"));
                        return none();
                    }) },
                { "reopenIfNeeded", method("WatchedFileHandler", "reopenIfNeeded", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("stream").is_none())
                            return none();
                        // (the file is stat'ed by its path once, then the new descriptor fstat'ed if a new stream is opened: fewer races (issue #14632))
                        bool reopen = false;
                        try_except([&]()
                        {
                            // (the file's stat (it exists), compared with that of the stream's file)
                            py const sres = M("os", "stat")(self.attr("baseFilename"));
                            reopen = sres.attr("st_dev") != self.attr("dev") || sres.attr("st_ino") != self.attr("ino");
                        }, builtin("FileNotFoundError"), [&](py const&) { reopen = true; });
                        if (!reopen)
                            return none();
                        // (the open stream is closed)
                        self.attr("stream").method("flush");
                        self.attr("stream").method("close");
                        self.set_attr("stream", none());   // (Issue #21742: _open() might fail)
                        // (a new stream, and its stat)
                        self.set_attr("stream", self.method("_open"));
                        self.method("_statstream");
                        return none();
                    }) },
                { "emit", method("WatchedFileHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (an error reopening the file is reported as emit's errors are)
                        bool reopened = false;
                        or_handle_error(self, a[1], [&]()
                        {
                            self.method("reopenIfNeeded");
                            reopened = true;
                        });
                        if (reopened)
                            logging_("FileHandler").attr("emit")(self, a[1]);
                        return none();
                    }) } });
        }

        void make_socket_handlers()
        {
            py const handler = logging_("Handler");
            make_handlers_class("SocketHandler", { handler }, {
                { "__init__", method("SocketHandler", "__init__", { py_positional("host"), py_positional("port") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        logging_("Handler").attr("__init__")(self);
                        self.set_attr("host", a[1]);
                        self.set_attr("port", a[2]);
                        if (a[2].is_none())
                            self.set_attr("address", a[1]);
                        else
                            self.set_attr("address", py_tuple({ a[1], a[2] }));
                        self.set_attr("sock", none());
                        self.set_attr("closeOnError", boolean(false));
                        self.set_attr("retryTime", none());
                        // (exponential backoff)
                        self.set_attr("retryStart", py{ 1.0 });
                        self.set_attr("retryMax", py{ 30.0 });
                        self.set_attr("retryFactor", py{ 2.0 });
                        return none();
                    }) },
                { "makeSocket", method("SocketHandler", "makeSocket", { py_positional("timeout", py{ 1 }) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (!self.attr("port").is_none())
                            return M("socket", "create_connection").call({ self.attr("address") }, { { "timeout", a[1] } });
                        py const result = M("socket", "socket")(M("socket", "AF_UNIX"), M("socket", "SOCK_STREAM"));
                        result.method("settimeout", { a[1] });
                        try_except([&]() { result.method("connect", { self.attr("address") }); }, builtin("OSError"), [&](py const& e)
                        {
                            result.method("close");     // (Issue 19182)
                            py_reraise(e);
                        });
                        return result;
                    }) },
                { "createSocket", method("SocketHandler", "createSocket", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const now = M("time", "time")();
                        // (retryTime is None the first time after a disconnect; otherwise, it has been waited for)
                        bool const attempt = self.attr("retryTime").is_none() || now >= self.attr("retryTime");
                        if (attempt)
                            try_except([&]()
                            {
                                self.set_attr("sock", self.method("makeSocket"));
                                self.set_attr("retryTime", none());     // (next time, no delay before trying)
                            }, builtin("OSError"), [&](py const&)
                            {
                                // (creation failed: the retry time is set)
                                if (self.attr("retryTime").is_none())
                                    self.set_attr("retryPeriod", self.attr("retryStart"));
                                else
                                {
                                    self.set_attr("retryPeriod", self.attr("retryPeriod") * self.attr("retryFactor"));
                                    if (self.attr("retryPeriod") > self.attr("retryMax"))
                                        self.set_attr("retryPeriod", self.attr("retryMax"));
                                }
                                self.set_attr("retryTime", now + self.attr("retryPeriod"));
                            });
                        return none();
                    }) },
                { "send", method("SocketHandler", "send", { py_positional("s") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("sock").is_none())
                            self.method("createSocket");
                        // (sock can be None: the retry time has not been reached, or it has and the retry failed)
                        if (self.attr("sock").truthy())
                            try_except([&]() { self.attr("sock").method("sendall", { a[1] }); }, builtin("OSError"), [&](py const&)
                            {
                                self.attr("sock").method("close");
                                self.set_attr("sock", none());     // (createSocket is called next time)
                            });
                        return none();
                    }) },
                { "makePickle", method("SocketHandler", "makePickle", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& record = a[1];
                        // (the traceback's text in record.exc_text)
                        if (record.attr("exc_info").truthy())
                            self.method("format", { record });
                        // (issue #14436: msg % args as a str (msg and args, if they are objects, may not be available where the record is
                        // received))
                        py const d = builtin("dict")(record.attr("__dict__"));
                        d.set_item(S("msg"), record.method("getMessage"));
                        d.set_item(S("args"), none());
                        d.set_item(S("exc_info"), none());
                        // (issue #25685: 'message' is msg's)
                        d.method("pop", { S("message"), none() });
                        py const s = py{ fn_import("pickle") }.attr("dumps")(d, py{ 1 });
                        py const slen = M("struct", "pack")(S(">L"), py{ s.size() });
                        return slen + s;
                    }) },
                { "handleError", method("SocketHandler", "handleError", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (most likely, the connection was lost: the socket is closed, so that the next event retries)
                        if (self.attr("closeOnError").truthy() && self.attr("sock").truthy())
                        {
                            self.attr("sock").method("close");
                            self.set_attr("sock", none());
                        }
                        else
                            logging_("Handler").attr("handleError")(self, a[1]);
                        return none();
                    }) },
                { "emit", method("SocketHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        or_handle_error(self, a[1], [&]()
                        {
                            py const s = self.method("makePickle", { a[1] });
                            self.method("send", { s });
                        });
                        return none();
                    }) },
                { "close", method("SocketHandler", "close", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        with_lock(self.attr("lock"), [&]()
                        {
                            py const sock = self.attr("sock");
                            if (sock.truthy())
                            {
                                self.set_attr("sock", none());
                                sock.method("close");
                            }
                            logging_("Handler").attr("close")(self);
                        });
                        return none();
                    }) } });

            make_handlers_class("DatagramHandler", { cls("SocketHandler") }, {
                { "__init__", method("DatagramHandler", "__init__", { py_positional("host"), py_positional("port") }, [](std::vector<py> const& a)
                    {
                        cls("SocketHandler").attr("__init__")(a[0], a[1], a[2]);
                        a[0].set_attr("closeOnError", boolean(false));
                        return none();
                    }) },
                { "makeSocket", method("DatagramHandler", "makeSocket", {}, [](std::vector<py> const& a)
                    {
                        py const family = a[0].attr("port").is_none() ? M("socket", "AF_UNIX") : M("socket", "AF_INET");
                        return M("socket", "socket")(family, M("socket", "SOCK_DGRAM"));
                    }) },
                { "send", method("DatagramHandler", "send", { py_positional("s") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("sock").is_none())
                            self.method("createSocket");
                        self.attr("sock").method("sendto", { a[1], self.attr("address") });
                        return none();
                    }) } });
        }

        void make_syslog_handler()
        {
            // (from <linux/sys/syslog.h>: a priority and a facility are encoded in 32 bits: the priority (0-7) in the bottom 3 bits, the
            // facility (0 to a big number) in the top 28; their names are roughly as syslogd(8)'s source's)
            namespace_type ns;
            static std::pair<char const*, long long> const sCodes[] =
            {
                // (priorities (in order))
                { "LOG_EMERG", 0 }, { "LOG_ALERT", 1 }, { "LOG_CRIT", 2 }, { "LOG_ERR", 3 }, { "LOG_WARNING", 4 }, { "LOG_NOTICE", 5 }, { "LOG_INFO", 6 },
                { "LOG_DEBUG", 7 },
                // (facility codes (others through 15 are reserved for the system))
                { "LOG_KERN", 0 }, { "LOG_USER", 1 }, { "LOG_MAIL", 2 }, { "LOG_DAEMON", 3 }, { "LOG_AUTH", 4 }, { "LOG_SYSLOG", 5 }, { "LOG_LPR", 6 },
                { "LOG_NEWS", 7 }, { "LOG_UUCP", 8 }, { "LOG_CRON", 9 }, { "LOG_AUTHPRIV", 10 }, { "LOG_FTP", 11 }, { "LOG_NTP", 12 }, { "LOG_SECURITY", 13 },
                { "LOG_CONSOLE", 14 }, { "LOG_SOLCRON", 15 },
                // (reserved for local use)
                { "LOG_LOCAL0", 16 }, { "LOG_LOCAL1", 17 }, { "LOG_LOCAL2", 18 }, { "LOG_LOCAL3", 19 }, { "LOG_LOCAL4", 20 }, { "LOG_LOCAL5", 21 },
                { "LOG_LOCAL6", 22 }, { "LOG_LOCAL7", 23 }
            };
            for (auto const& [name, value] : sCodes)
                ns.emplace_back(name, py{ value });
            py const priorityNames = py_dict();
            for (auto const& [name, value] : std::initializer_list<std::pair<char const*, long long>>{ { "alert", 1 }, { "crit", 2 }, { "critical", 2 },
                { "debug", 7 }, { "emerg", 0 }, { "err", 3 }, { "error", 3 }, { "info", 6 }, { "notice", 5 }, { "panic", 0 }, { "warn", 4 }, { "warning", 4 } })
                priorityNames.set_item(S(name), py{ value });  // (error, panic and warn: deprecated)
            ns.emplace_back("priority_names", priorityNames);
            py const facilityNames = py_dict();
            for (auto const& [name, value] : std::initializer_list<std::pair<char const*, long long>>{ { "auth", 4 }, { "authpriv", 10 }, { "console", 14 },
                { "cron", 9 }, { "daemon", 3 }, { "ftp", 11 }, { "kern", 0 }, { "lpr", 6 }, { "mail", 2 }, { "news", 7 }, { "ntp", 12 }, { "security", 13 },
                { "solaris-cron", 15 }, { "syslog", 5 }, { "user", 1 }, { "uucp", 8 }, { "local0", 16 }, { "local1", 17 }, { "local2", 18 }, { "local3", 19 },
                { "local4", 20 }, { "local5", 21 }, { "local6", 22 }, { "local7", 23 } })
                facilityNames.set_item(S(name), py{ value });
            ns.emplace_back("facility_names", facilityNames);
            // (originally a workaround for GH-43683 (unnecessary since GH-50043): kept for backwards compatibility)
            py const priorityMap = py_dict();
            for (auto const& [level, name] : std::initializer_list<std::pair<char const*, char const*>>{ { "DEBUG", "debug" }, { "INFO", "info" },
                { "WARNING", "warning" }, { "ERROR", "error" }, { "CRITICAL", "critical" } })
                priorityMap.set_item(S(level), S(name));
            ns.emplace_back("priority_map", priorityMap);
            ns.emplace_back("__init__", method("SysLogHandler", "__init__", { py_positional("address", py_tuple({ S("localhost"), py{ 514 } })),
                py_positional("facility", py{ 1 }), py_positional("socktype", none()) }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    logging_("Handler").attr("__init__")(self);
                    self.set_attr("address", a[1]);
                    self.set_attr("facility", a[2]);
                    self.set_attr("socktype", a[3]);
                    self.set_attr("socket", none());
                    // (the address is resolved again when an event is emitted)
                    try_except([&]() { self.method("createSocket"); }, M("socket", "gaierror"), [](py const&) {});
                    return none();
                }));
            ns.emplace_back("_connect_unixsocket", method("SysLogHandler", "_connect_unixsocket", { py_positional("address") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py useSocktype = self.attr("socktype");
                    if (useSocktype.is_none())
                        useSocktype = M("socket", "SOCK_DGRAM");
                    self.set_attr("socket", M("socket", "socket")(M("socket", "AF_UNIX"), useSocktype));
                    try_except([&]()
                    {
                        self.attr("socket").method("connect", { a[1] });
                        // (it worked: the type used)
                        self.set_attr("socktype", useSocktype);
                    }, builtin("OSError"), [&](py const& e)
                    {
                        self.attr("socket").method("close");
                        if (!self.attr("socktype").is_none())
                            py_reraise(e);      // (falling back was not specified: it fails)
                        useSocktype = M("socket", "SOCK_STREAM");
                        self.set_attr("socket", M("socket", "socket")(M("socket", "AF_UNIX"), useSocktype));
                        try_except([&]()
                        {
                            self.attr("socket").method("connect", { a[1] });
                            self.set_attr("socktype", useSocktype);
                        }, builtin("OSError"), [&](py const& e2)
                        {
                            self.attr("socket").method("close");
                            py_reraise(e2);
                        });
                    });
                    return none();
                }));
            ns.emplace_back("createSocket", method("SysLogHandler", "createSocket", {}, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py const address = self.attr("address");
                    py socktype = self.attr("socktype");
                    if (isinstance(address, builtin("str")))
                    {
                        self.set_attr("unixsocket", boolean(true));
                        // (the syslog server may be unavailable when the handler is initialised: C's openlog() also ignores connection errors (and
                        // they are ignored while logging))
                        try_except([&]() { self.method("_connect_unixsocket", { address }); }, builtin("OSError"), [](py const&) {});
                    }
                    else
                    {
                        self.set_attr("unixsocket", boolean(false));
                        if (socktype.is_none())
                            socktype = M("socket", "SOCK_DGRAM");
                        auto const hostPort = address.items();
                        if (hostPort.size() != 2u)
                            py_raise("ValueError", hostPort.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(hostPort.size()) + ")" :
                                "too many values to unpack (expected 2)");
                        py const ress = M("socket", "getaddrinfo")(hostPort[0], hostPort[1], py{ 0 }, socktype);
                        if (!ress.truthy())
                            py_raise("OSError", "getaddrinfo returns an empty list");
                        py err = none();
                        py sock = none();
                        for (py const& res : ress.items())
                        {
                            auto const parts = res.items();
                            py const af = parts.at(0);
                            socktype = parts.at(1);
                            py const proto = parts.at(2);
                            py const sa = parts.at(4);
                            err = none();
                            sock = none();
                            bool connected = false;
                            try_except([&]()
                            {
                                sock = M("socket", "socket")(af, socktype, proto);
                                if (socktype == M("socket", "SOCK_STREAM"))
                                    sock.method("connect", { sa });
                                connected = true;
                            }, builtin("OSError"), [&](py const& exc)
                            {
                                err = exc;
                                if (!sock.is_none())
                                    sock.method("close");
                            });
                            if (connected)
                                break;
                        }
                        if (!err.is_none())
                            py_raise(err);
                        self.set_attr("socket", sock);
                        self.set_attr("socktype", socktype);
                    }
                    return none();
                }));
            ns.emplace_back("encodePriority", method("SysLogHandler", "encodePriority", { py_positional("facility"), py_positional("priority") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py facility = a[1];
                    py priority = a[2];
                    if (isinstance(facility, builtin("str")))
                        facility = self.attr("facility_names")[facility];
                    if (isinstance(priority, builtin("str")))
                        priority = self.attr("priority_names")[priority];
                    return (facility << py{ 3 }) | priority;
                }));
            ns.emplace_back("close", method("SysLogHandler", "close", {}, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    with_lock(self.attr("lock"), [&]()
                    {
                        py const sock = self.attr("socket");
                        if (sock.truthy())
                        {
                            self.set_attr("socket", none());
                            sock.method("close");
                        }
                        logging_("Handler").attr("close")(self);
                    });
                    return none();
                }));
            ns.emplace_back("mapPriority", method("SysLogHandler", "mapPriority", { py_positional("levelName") }, [](std::vector<py> const& a)
                {
                    return a[0].attr("priority_map").method("get", { a[1], S("warning") });
                }));
            ns.emplace_back("ident", S(""));                 // (prepended to all messages)
            ns.emplace_back("append_nul", boolean(true));    // (some old syslog daemons expect a NUL terminator)
            ns.emplace_back("emit", method("SysLogHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    or_handle_error(self, a[1], [&]()
                    {
                        py msg = self.method("format", { a[1] });
                        if (self.attr("ident").truthy())
                            msg = self.attr("ident") + msg;
                        if (self.attr("append_nul").truthy())
                            msg = msg + py{ std::string(1u, '\0') };
                        // (the record's level in lower case)
                        py prio = S("<%d>") % py_tuple({ self.method("encodePriority", { self.attr("facility"), self.method("mapPriority", { a[1].attr("levelname") }) }) });
                        prio = prio.method("encode", { S("utf-8") });
                        // (the message as bytes (as RFC 5424 requires))
                        msg = msg.method("encode", { S("utf-8") });
                        msg = prio + msg;
                        if (!self.attr("socket").truthy())
                            self.method("createSocket");
                        if (self.attr("unixsocket").truthy())
                            try_except([&]() { self.attr("socket").method("send", { msg }); }, builtin("OSError"), [&](py const&)
                            {
                                self.attr("socket").method("close");
                                self.method("_connect_unixsocket", { self.attr("address") });
                                self.attr("socket").method("send", { msg });
                            });
                        else if (self.attr("socktype") == M("socket", "SOCK_DGRAM"))
                            self.attr("socket").method("sendto", { msg, self.attr("address") });
                        else
                            self.attr("socket").method("sendall", { msg });
                    });
                    return none();
                }));
            make_handlers_class("SysLogHandler", { logging_("Handler") }, ns);
        }

        void make_network_handlers()
        {
            py const handler = logging_("Handler");
            py const listOrTuple = py_tuple({ builtin("list"), builtin("tuple") });
            make_handlers_class("SMTPHandler", { handler }, {
                { "__init__", method("SMTPHandler", "__init__", { py_positional("mailhost"), py_positional("fromaddr"), py_positional("toaddrs"), py_positional("subject"),
                    py_positional("credentials", none()), py_positional("secure", none()), py_positional("timeout", py{ 5.0 }) }, [listOrTuple](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        logging_("Handler").attr("__init__")(self);
                        auto const pair = [](py const& aValue)
                        {
                            auto const items = aValue.items();
                            if (items.size() != 2u)
                                py_raise("ValueError", items.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(items.size()) + ")" :
                                    "too many values to unpack (expected 2)");
                            return items;
                        };
                        if (isinstance(a[1], listOrTuple))
                        {
                            auto const hostPort = pair(a[1]);
                            self.set_attr("mailhost", hostPort[0]);
                            self.set_attr("mailport", hostPort[1]);
                        }
                        else
                        {
                            self.set_attr("mailhost", a[1]);
                            self.set_attr("mailport", none());
                        }
                        if (isinstance(a[5], listOrTuple))
                        {
                            auto const credentials = pair(a[5]);
                            self.set_attr("username", credentials[0]);
                            self.set_attr("password", credentials[1]);
                        }
                        else
                            self.set_attr("username", none());
                        self.set_attr("fromaddr", a[2]);
                        py toaddrs = a[3];
                        if (isinstance(toaddrs, builtin("str")))
                            toaddrs = py_list({ toaddrs });
                        self.set_attr("toaddrs", toaddrs);
                        self.set_attr("subject", a[4]);
                        self.set_attr("secure", a[6]);
                        self.set_attr("timeout", a[7]);
                        return none();
                    }) },
                { "getSubject", method("SMTPHandler", "getSubject", { py_positional("record") }, [](std::vector<py> const& a) { return a[0].attr("subject"); }) },
                { "emit", method("SMTPHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        or_handle_error(self, a[1], [&]()
                        {
                            py const smtplib{ fn_import("smtplib") };
                            py const emailMessage = import_attribute("email.message", "EmailMessage");
                            py const emailUtils{ fn_import("email.utils") };
                            py port = self.attr("mailport");
                            if (!port.truthy())
                                port = smtplib.attr("SMTP_PORT");
                            py const smtp = smtplib.attr("SMTP").call({ self.attr("mailhost"), port }, { { "timeout", self.attr("timeout") } });
                            py const msg = emailMessage();
                            msg.set_item(S("From"), self.attr("fromaddr"));
                            msg.set_item(S("To"), S(",").method("join", { self.attr("toaddrs") }));
                            msg.set_item(S("Subject"), self.method("getSubject", { a[1] }));
                            msg.set_item(S("Date"), emailUtils.attr("localtime")());
                            msg.method("set_content", { self.method("format", { a[1] }) });
                            if (self.attr("username").truthy())
                            {
                                if (!self.attr("secure").is_none())
                                {
                                    py const ssl{ fn_import("ssl") };
                                    py keyfile = none();
                                    py certfile = none();
                                    try_except([&]() { keyfile = self.attr("secure")[py{ 0 }]; }, builtin("IndexError"), [&](py const&) { keyfile = none(); });
                                    try_except([&]() { certfile = self.attr("secure")[py{ 1 }]; }, builtin("IndexError"), [&](py const&) { certfile = none(); });
                                    py const context = ssl.attr("_create_stdlib_context").call({}, { { "certfile", certfile }, { "keyfile", keyfile } });
                                    smtp.method("ehlo");
                                    smtp.method("starttls", {}, { { "context", context } });
                                    smtp.method("ehlo");
                                }
                                smtp.method("login", { self.attr("username"), self.attr("password") });
                            }
                            smtp.method("send_message", { msg });
                            smtp.method("quit");
                        });
                        return none();
                    }) } });

            make_handlers_class("NTEventLogHandler", { handler }, {
                { "__init__", method("NTEventLogHandler", "__init__", { py_positional("appname"), py_positional("dllname", none()), py_positional("logtype", S("Application")) },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        logging_("Handler").attr("__init__")(self);
                        try_except([&]()
                        {
                            py const welu{ fn_import("win32evtlogutil") };
                            py const evtlog{ fn_import("win32evtlog") };
                            self.set_attr("appname", a[1]);
                            self.set_attr("_welu", welu);
                            py dllname = a[2];
                            if (!dllname.truthy())
                            {
                                dllname = os_path("split")(welu.attr("__file__"));
                                dllname = os_path("split")(dllname[py{ 0 }]);
                                dllname = os_path("join")(dllname[py{ 0 }], S("win32service.pyd"));
                            }
                            self.set_attr("dllname", dllname);
                            self.set_attr("logtype", a[3]);
                            // (adding a source to the registry needs administrative privileges, which a user who adds to an existing source may not
                            // have: an "access denied" error is ignored)
                            try_except([&]() { welu.method("AddSourceToRegistry", { a[1], dllname, a[3] }); }, builtin("Exception"), [&](py const& e)
                            {
                                // (probably a pywintypes.error)
                                if (builtin("getattr")(e, S("winerror"), none()) != py{ 5 })     // (not access denied)
                                    py_reraise(e);
                            });
                            self.set_attr("deftype", evtlog.attr("EVENTLOG_ERROR_TYPE"));
                            py const typemap = py_dict();
                            typemap.set_item(logging_("DEBUG"), evtlog.attr("EVENTLOG_INFORMATION_TYPE"));
                            typemap.set_item(logging_("INFO"), evtlog.attr("EVENTLOG_INFORMATION_TYPE"));
                            typemap.set_item(logging_("WARNING"), evtlog.attr("EVENTLOG_WARNING_TYPE"));
                            typemap.set_item(logging_("ERROR"), evtlog.attr("EVENTLOG_ERROR_TYPE"));
                            typemap.set_item(logging_("CRITICAL"), evtlog.attr("EVENTLOG_ERROR_TYPE"));
                            self.set_attr("typemap", typemap);
                        }, builtin("ImportError"), [&](py const&)
                        {
                            builtin("print")(S("The Python Win32 extensions for NT (service, event logging) appear not to be available."));
                            self.set_attr("_welu", none());
                        });
                        return none();
                    }) },
                { "getMessageID", method("NTEventLogHandler", "getMessageID", { py_positional("record") }, [](std::vector<py> const&) { return py{ 1 }; }) },
                { "getEventCategory", method("NTEventLogHandler", "getEventCategory", { py_positional("record") }, [](std::vector<py> const&) { return py{ 0 }; }) },
                { "getEventType", method("NTEventLogHandler", "getEventType", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("typemap").method("get", { a[1].attr("levelno"), a[0].attr("deftype") });
                    }) },
                { "emit", method("NTEventLogHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("_welu").truthy())
                            or_handle_error(self, a[1], [&]()
                            {
                                py const id = self.method("getMessageID", { a[1] });
                                py const cat = self.method("getEventCategory", { a[1] });
                                py const type = self.method("getEventType", { a[1] });
                                py const msg = self.method("format", { a[1] });
                                self.attr("_welu").method("ReportEvent", { self.attr("appname"), id, cat, type, py_list({ msg }) });
                            });
                        return none();
                    }) },
                { "close", method("NTEventLogHandler", "close", {}, [](std::vector<py> const& a)
                    {
                        // (the application's name could be removed from the registry as a source of event log entries, but then the Event Log
                        // Viewer could not see its events (it needs the registry's DLL name))
                        logging_("Handler").attr("close")(a[0]);
                        return none();
                    }) } });

            make_handlers_class("HTTPHandler", { handler }, {
                { "__init__", method("HTTPHandler", "__init__", { py_positional("host"), py_positional("url"), py_positional("method", S("GET")),
                    py_positional("secure", boolean(false)), py_positional("credentials", none()), py_positional("context", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        logging_("Handler").attr("__init__")(self);
                        py const method = a[3].method("upper");
                        if (!py_list({ S("GET"), S("POST") }).contains(method))
                            py_raise("ValueError", "method must be GET or POST");
                        if (!a[4].truthy() && !a[6].is_none())
                            py_raise("ValueError", "context parameter only makes sense with secure=True");
                        self.set_attr("host", a[1]);
                        self.set_attr("url", a[2]);
                        self.set_attr("method", method);
                        self.set_attr("secure", a[4]);
                        self.set_attr("credentials", a[5]);
                        self.set_attr("context", a[6]);
                        return none();
                    }) },
                { "mapLogRecord", method("HTTPHandler", "mapLogRecord", { py_positional("record") }, [](std::vector<py> const& a) { return a[1].attr("__dict__"); }) },
                { "getConnection", method("HTTPHandler", "getConnection", { py_positional("host"), py_positional("secure") }, [](std::vector<py> const& a)
                    {
                        py const client{ fn_import("http.client") };
                        if (a[2].truthy())
                            return client.attr("HTTPSConnection").call({ a[1] }, { { "context", a[0].attr("context") } });
                        return client.attr("HTTPConnection")(a[1]);
                    }) },
                { "emit", method("HTTPHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        or_handle_error(self, a[1], [&]()
                        {
                            py const parse{ fn_import("urllib.parse") };
                            py host = self.attr("host");
                            py const h = self.method("getConnection", { host, self.attr("secure") });
                            py url = self.attr("url");
                            py const data = parse.attr("urlencode")(self.method("mapLogRecord", { a[1] }));
                            if (self.attr("method") == S("GET"))
                            {
                                py const sep = url.method("find", { S("?") }) >= py{ 0 } ? S("&") : S("?");
                                url = url + S("%c%s") % py_tuple({ sep, data });
                            }
                            h.method("putrequest", { self.attr("method"), url });
                            // (several hosts on one IP address: the host without its optional port (the Host header: putrequest's (issue #30904)))
                            py const i = host.method("find", { S(":") });
                            if (i >= py{ 0 })
                                host = slice(host, none(), i);
                            if (self.attr("method") == S("POST"))
                            {
                                h.method("putheader", { S("Content-type"), S("application/x-www-form-urlencoded") });
                                h.method("putheader", { S("Content-length"), builtin("str")(py{ data.size() }) });
                            }
                            if (self.attr("credentials").truthy())
                            {
                                py const base64{ fn_import("base64") };
                                py s = (S("%s:%s") % self.attr("credentials")).method("encode", { S("utf-8") });
                                s = S("Basic ") + base64.attr("b64encode")(s).method("strip").method("decode", { S("ascii") });
                                h.method("putheader", { S("Authorization"), s });
                            }
                            h.method("endheaders");
                            if (self.attr("method") == S("POST"))
                                h.method("send", { data.method("encode", { S("utf-8") }) });
                            h.method("getresponse");     // (its result is not used)
                        });
                        return none();
                    }) } });
        }

        void make_queue_handlers()
        {
            py const handler = logging_("Handler");
            make_handlers_class("BufferingHandler", { handler }, {
                { "__init__", method("BufferingHandler", "__init__", { py_positional("capacity") }, [](std::vector<py> const& a)
                    {
                        logging_("Handler").attr("__init__")(a[0]);
                        a[0].set_attr("capacity", a[1]);
                        a[0].set_attr("buffer", py_list());
                        return none();
                    }) },
                { "shouldFlush", method("BufferingHandler", "shouldFlush", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        return boolean(py{ a[0].attr("buffer").size() } >= a[0].attr("capacity"));
                    }) },
                { "emit", method("BufferingHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("buffer").method("append", { a[1] });
                        if (a[0].method("shouldFlush", { a[1] }).truthy())
                            a[0].method("flush");
                        return none();
                    }) },
                { "flush", method("BufferingHandler", "flush", {}, [](std::vector<py> const& a)
                    {
                        with_lock(a[0].attr("lock"), [&]() { a[0].attr("buffer").method("clear"); });
                        return none();
                    }) },
                { "close", method("BufferingHandler", "close", {}, [](std::vector<py> const& a)
                    {
                        try_finally([&]() { a[0].method("flush"); }, [&]() { logging_("Handler").attr("close")(a[0]); });
                        return none();
                    }) } });

            make_handlers_class("MemoryHandler", { cls("BufferingHandler") }, {
                { "__init__", method("MemoryHandler", "__init__", { py_positional("capacity"), py_positional("flushLevel", py{ 40 }), py_positional("target", none()),
                    py_positional("flushOnClose", boolean(true)) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        cls("BufferingHandler").attr("__init__")(self, a[1]);
                        self.set_attr("flushLevel", a[2]);
                        self.set_attr("target", a[3]);
                        // (Issue #26559)
                        self.set_attr("flushOnClose", a[4]);
                        return none();
                    }) },
                { "shouldFlush", method("MemoryHandler", "shouldFlush", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        if (py{ a[0].attr("buffer").size() } >= a[0].attr("capacity"))
                            return boolean(true);
                        return py_compare(NEOS_PY_GE, a[1].attr("levelno"), a[0].attr("flushLevel"));
                    }) },
                { "setTarget", method("MemoryHandler", "setTarget", { py_positional("target") }, [](std::vector<py> const& a)
                    {
                        with_lock(a[0].attr("lock"), [&]() { a[0].set_attr("target", a[1]); });
                        return none();
                    }) },
                { "flush", method("MemoryHandler", "flush", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        with_lock(self.attr("lock"), [&]()
                        {
                            if (self.attr("target").truthy())
                            {
                                for (py const& record : self.attr("buffer").items())
                                    self.attr("target").method("handle", { record });
                                self.attr("buffer").method("clear");
                            }
                        });
                        return none();
                    }) },
                { "close", method("MemoryHandler", "close", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        try_finally([&]()
                        {
                            if (self.attr("flushOnClose").truthy())
                                self.method("flush");
                        }, [&]()
                        {
                            with_lock(self.attr("lock"), [&]()
                            {
                                self.set_attr("target", none());
                                cls("BufferingHandler").attr("close")(self);
                            });
                        });
                        return none();
                    }) } });

            make_handlers_class("QueueHandler", { handler }, {
                { "__init__", method("QueueHandler", "__init__", { py_positional("queue") }, [](std::vector<py> const& a)
                    {
                        logging_("Handler").attr("__init__")(a[0]);
                        a[0].set_attr("queue", a[1]);
                        a[0].set_attr("listener", none());     // (the listener, if dictConfig() configures it)
                        return none();
                    }) },
                { "enqueue", method("QueueHandler", "enqueue", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("queue").method("put_nowait", { a[1] });
                        return none();
                    }) },
                { "prepare", method("QueueHandler", "prepare", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        // (formatting puts the traceback's text (if there is one) in record.exc_text, and returns the message, which replaces msg
                        // and args (they might be unpicklable); exc_info, exc_text and stack_info are not needed (and, if not None, are typically
                        // unpicklable))
                        py const msg = a[0].method("format", { a[1] });
                        // (bpo-35726: a copy of the record (the other handlers in the chain are not affected))
                        py const record = M("copy", "copy")(a[1]);
                        record.set_attr("message", msg);
                        record.set_attr("msg", msg);
                        record.set_attr("args", none());
                        record.set_attr("exc_info", none());
                        record.set_attr("exc_text", none());
                        record.set_attr("stack_info", none());
                        return record;
                    }) },
                { "emit", method("QueueHandler", "emit", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        or_handle_error(self, a[1], [&]() { self.method("enqueue", { self.method("prepare", { a[1] }) }); });
                        return none();
                    }) } });

            make_handlers_class("QueueListener", { builtin("object") }, {
                { "_sentinel", none() },
                { "__init__", method("QueueListener", "__init__", { py_positional("queue"), py_keyword("respect_handler_level", boolean(false)) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("queue", a[1]);
                        self.set_attr("handlers", a[3]);
                        self.set_attr("_thread", none());
                        self.set_attr("respect_handler_level", a[2]);
                        return none();
                    }, true) },
                { "dequeue", method("QueueListener", "dequeue", { py_positional("block") }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("queue").method("get", { a[1] });
                    }) },
                { "start", method("QueueListener", "start", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (!self.attr("_thread").is_none())
                            py_raise("RuntimeError", "Listener already started");
                        py const t = M("threading", "Thread").call({}, { { "target", self.attr("_monitor") } });
                        self.set_attr("_thread", t);
                        t.set_attr("daemon", boolean(true));
                        t.method("start");
                        return none();
                    }) },
                { "prepare", method("QueueListener", "prepare", { py_positional("record") }, [](std::vector<py> const& a) { return a[1]; }) },
                { "handle", method("QueueListener", "handle", { py_positional("record") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const record = self.method("prepare", { a[1] });
                        for (py const& handler : self.attr("handlers").items())
                        {
                            bool const process = !self.attr("respect_handler_level").truthy() || record.attr("levelno") >= handler.attr("level");
                            if (process)
                                handler.method("handle", { record });
                        }
                        return none();
                    }) },
                { "_monitor", method("QueueListener", "_monitor", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (on a thread of its own: it ends when it sees the sentinel in the queue)
                        py const q = self.attr("queue");
                        bool const hasTaskDone = has_attr(q, "task_done");
                        for (;;)
                        {
                            bool stop = false;
                            try_except([&]()
                            {
                                py const record = self.method("dequeue", { boolean(true) });
                                if (record.is(self.attr("_sentinel")))
                                {
                                    if (hasTaskDone)
                                        q.method("task_done");
                                    stop = true;
                                    return;
                                }
                                self.method("handle", { record });
                                if (hasTaskDone)
                                    q.method("task_done");
                            }, M("queue", "Empty"), [&](py const&) { stop = true; });
                            if (stop)
                                break;
                        }
                        return none();
                    }) },
                { "enqueue_sentinel", method("QueueListener", "enqueue_sentinel", {}, [](std::vector<py> const& a)
                    {
                        a[0].attr("queue").method("put_nowait", { a[0].attr("_sentinel") });
                        return none();
                    }) },
                { "stop", method("QueueListener", "stop", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("_thread").truthy())     // (gh-114706: it can be called more than once)
                        {
                            self.method("enqueue_sentinel");
                            self.attr("_thread").method("join");
                            self.set_attr("_thread", none());
                        }
                        return none();
                    }) } });
        }

        bool logging_handlers_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (!s.made)
                {
                    s.made = true;
                    make_rotating_handlers();
                    make_socket_handlers();
                    make_syslog_handler();
                    make_network_handlers();
                    make_queue_handlers();
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S(D("")));
                for (auto module : { "copy", "io", "logging", "os", "queue", "re", "socket", "struct", "threading", "time" })
                    add(module, py{ fn_import(module) });
                add("DEFAULT_TCP_LOGGING_PORT", py{ 9020 });
                add("DEFAULT_UDP_LOGGING_PORT", py{ 9021 });
                add("DEFAULT_HTTP_LOGGING_PORT", py{ 9022 });
                add("DEFAULT_SOAP_LOGGING_PORT", py{ 9023 });
                add("SYSLOG_UDP_PORT", py{ 514 });
                add("SYSLOG_TCP_PORT", py{ 514 });
                add("_MIDNIGHT", py{ Midnight });
                for (auto const& [name, value] : s.classes)
                    add(name.c_str(), value);
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sLoggingHandlers = register_module("logging.handlers", logging_handlers_module);
    }
}
