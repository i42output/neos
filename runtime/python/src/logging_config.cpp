/*
  logging_config.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/logging/config.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).

  Lib/logging/config.py's copyright and permission notice:

  Copyright 2001-2023 by Vinay Sajip. All Rights Reserved.

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


// The module logging.config (CPython 3.13's Lib/logging/config.py): dictConfig (BaseConfigurator, DictConfigurator and the ConvertingXXX
// wrappers), fileConfig, listen and stopListening; its classes are made (by type) of closures, so a program's subclasses override their
// methods. The module's variables that a program can rebind (dictConfigClass, _listener) are read from the module. configparser is imported
// when fileConfig is called (as config.py imports it); _is_queue_like_object does not check for a multiprocessing queue if multiprocessing cannot
// be imported (the runtime does not have it: there can be no such queue).

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "logging_config_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "logging.config";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;     ///< (the module's classes, by name)
            std::map<std::string, py, std::less<>> globals;     ///< (its variables, as they are made (before the module exists))
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
            auto const doc = logging_config_docs::doc(aName);
            return doc != nullptr ? doc : "";
        }

        // (a module's attribute (as config.py's "logging.X", "os.X", ...: looked up when it is used))
        py M(char const* aModule, char const* aName) { return import_attribute(aModule, aName); }
        py logging_(char const* aName) { return M("logging", aName); }
        py handlers_(char const* aName) { return M("logging.handlers", aName); }
        bool callable(py const& aValue) { return builtin("callable")(aValue).truthy(); }
        bool has_attr(py const& aObject, char const* aName) { return builtin("hasattr")(aObject, S(aName)).truthy(); }
        py slice(py const& aSequence, py const& aStart, py const& aStop)
        {
            return aSequence[py{ make_slice(aStart.get(), aStop.get(), nullptr) }];
        }
        // (f(*aArgs, **aKwargs) (aKwargs: a dict, or empty))
        py call_kw(py const& aCallable, std::vector<py> const& aArgs, py const& aKwargs)
        {
            std::vector<object*> args;
            for (auto const& a : aArgs)
                args.push_back(a.reference());
            return py{ invoke(aCallable.get(), std::move(args), aKwargs.empty() ? nullptr : aKwargs.get()) };
        }
        // (raise aClass(aMessage) from aCause)
        [[noreturn]] void raise_from(char const* aClass, py const& aMessage, py const& aCause)
        {
            py const e = builtin(aClass)(aMessage);
            fn_raise_from(e.get(), aCause.get());
            throw raised{};
        }
        py repr_format(char const* aFormat, std::vector<py> const& aValues) { return S(aFormat) % py_tuple(aValues); }

        // (a variable of the module (that the program can rebind): the module's, once it exists)
        py G(char const* aName)
        {
            py const module = import_attribute("sys", "modules").method("get", { S(sModule) });
            if (!module.is_none())
                return module.attr(aName);
            return state().globals.at(aName);
        }
        void set_G(char const* aName, py const& aValue)
        {
            state().globals[aName] = aValue;
            py const module = import_attribute("sys", "modules").method("get", { S(sModule) });
            if (!module.is_none())
                module.set_attr(aName, aValue);
        }

        // (a closure as a function of the module)
        using body_type = std::function<py(std::vector<py> const&)>;
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false, char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, [aQualname, aParameters, aBody, aVarArgs, aVarKeywords](std::vector<py> const& aArgs,
                object* aKwargs) -> py
            {
                py rest;
                py extra;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, aVarKeywords ? &extra : nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                if (aVarKeywords)
                    a.push_back(extra);
                return aBody(a);
            });
        }
        py function(char const* aName, std::vector<py_parameter> aParameters, body_type aBody)
        {
            return bound_function(aName, aName, std::move(aParameters), std::move(aBody), false, false, D(aName));
        }
        // (a method: its parameters after self (aClass: its class's qualified name); its docstring: config.py's)
        py method(std::string const& aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false)
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            std::string const qualname = aClass + "." + aName;
            return bound_function(aName, qualname, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords, D(qualname.c_str()));
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: type(aName, aBases, namespace) (aQualname: its qualified name, if it is not its name (e.g. a class that a
        // function defines)); its docstring: config.py's; aKeep: one of the module's classes)
        py make_config_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, char const* aQualname = nullptr, bool aKeep = true)
        {
            py const ns = py_dict();
            char const* const qualname = aQualname != nullptr ? aQualname : aName;
            ns.set_item(S("__module__"), S(sModule));
            ns.set_item(S("__qualname__"), S(qualname));
            if (auto const doc = logging_config_docs::doc(qualname))
                ns.set_item(S("__doc__"), S(doc));
            for (auto const& [name, value] : aNamespace)
                ns.set_item(S(name), value);
            std::vector<py> const bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            py meta = builtin("type");
            for (auto const& b : bases)
            {
                py const t = type_of(b);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            py const result = meta(S(aName), py_tuple(bases), ns);
            if (aKeep)
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

        // (try: aBody() except aClass (a class or a tuple of classes) as e: aHandler(e) (sys.exception() is e while it runs); another
        // exception remains raised)
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
        // (try: aBody() except Exception as e: raise ValueError(aMessage) from e)
        template <typename F>
        void or_value_error(py const& aMessage, F&& aBody)
        {
            try_except(std::forward<F>(aBody), builtin("Exception"), [&](py const& e) { raise_from("ValueError", aMessage, e); });
        }

        // (the module's functions' bodies (as they are named in config.py))

        // (_resolve: a dotted name's global object)
        py resolve_name(py const& aName)
        {
            py const name = aName.method("split", { S(".") });
            py used = name.method("pop", { py{ 0 } });
            py found = builtin("__import__")(used);
            for (py const& n : name.items())
            {
                used = used + S(".") + n;
                try_except([&]() { found = builtin("getattr")(found, n); }, builtin("AttributeError"), [&](py const&)
                {
                    builtin("__import__")(used);
                    found = builtin("getattr")(found, n);
                });
            }
            return found;
        }
        py strip_spaces(py const& aList) { return builtin("map")(builtin("str").attr("strip"), aList); }

        py valid_ident(py const& aText)
        {
            py const m = G("IDENTIFIER").method("match", { aText });
            if (!m.truthy())
                py_raise("ValueError", repr_format("Not a valid Python identifier: %r", { aText }).str());
            return boolean(true);
        }

        // ({k: config[k] for k in config if (k != '.' and valid_ident(k))})
        py identifier_kwargs(py const& aConfig)
        {
            py const kwargs = py_dict();
            for (py const& k : aConfig.items())
                if (k != S(".") && valid_ident(k).truthy())
                    kwargs.set_item(k, aConfig[k]);
            return kwargs;
        }

        // (_create_formatters: the formatters of a ConfigParser's configuration)
        py create_formatters(py const& cp)
        {
            py flist = cp[S("formatters")][S("keys")];
            if (!flist.size())
                return py_dict();
            flist = flist.method("split", { S(",") });
            flist = strip_spaces(flist);
            py const formatters = py_dict();
            for (py const& form : flist.items())
            {
                py const sectname = S("formatter_%s") % py_tuple({ form });
                py const fs = cp.method("get", { sectname, S("format") }, { { "raw", boolean(true) }, { "fallback", none() } });
                py const dfs = cp.method("get", { sectname, S("datefmt") }, { { "raw", boolean(true) }, { "fallback", none() } });
                py const stl = cp.method("get", { sectname, S("style") }, { { "raw", boolean(true) }, { "fallback", S("%") } });
                py defaults = cp.method("get", { sectname, S("defaults") }, { { "raw", boolean(true) }, { "fallback", none() } });
                py c = logging_("Formatter");
                py const className = cp[sectname].method("get", { S("class") });
                if (className.truthy())
                    c = resolve_name(className);
                py f;
                if (!defaults.is_none())
                {
                    defaults = builtin("eval")(defaults, builtin("vars")(py{ fn_import("logging") }));
                    f = c.call({ fs, dfs, stl }, { { "defaults", defaults } });
                }
                else
                    f = c(fs, dfs, stl);
                formatters.set_item(form, f);
            }
            return formatters;
        }

        // (_install_handlers: the handlers of a ConfigParser's configuration, installed)
        py install_handlers(py const& cp, py const& aFormatters)
        {
            py hlist = cp[S("handlers")][S("keys")];
            if (!hlist.size())
                return py_dict();
            hlist = hlist.method("split", { S(",") });
            hlist = strip_spaces(hlist);
            py const handlers = py_dict();
            std::vector<std::pair<py, py>> fixups;  // (the references between handlers)
            py const logging{ fn_import("logging") };
            for (py const& hand : hlist.items())
            {
                py const section = cp[S("handler_%s") % py_tuple({ hand })];
                py klass = section[S("class")];
                py const fmt = section.method("get", { S("formatter"), S("") });
                try_except([&]() { klass = builtin("eval")(klass, builtin("vars")(logging)); }, py_tuple({ builtin("AttributeError"), builtin("NameError") }),
                    [&](py const&) { klass = resolve_name(klass); });
                py args = section.method("get", { S("args"), S("()") });
                args = builtin("eval")(args, builtin("vars")(logging));
                py kwargs = section.method("get", { S("kwargs"), S("{}") });
                kwargs = builtin("eval")(kwargs, builtin("vars")(logging));
                py const h = call_kw(klass, args.items(), kwargs);
                h.set_attr("name", hand);
                if (section.contains(S("level")))
                    h.method("setLevel", { section[S("level")] });
                if (fmt.size())
                    h.method("setFormatter", { aFormatters[fmt] });
                if (issubclass(klass, handlers_("MemoryHandler")))
                {
                    py const target = section.method("get", { S("target"), S("") });
                    if (target.size())     // (the target handler may not be loaded yet: it is set later)
                        fixups.emplace_back(h, target);
                }
                handlers.set_item(hand, h);
            }
            // (all of the handlers are loaded: the references between them)
            for (auto const& [h, t] : fixups)
                h.method("setTarget", { handlers[t] });
            return handlers;
        }

        // (_handle_existing_loggers: the loggers of the previous configuration that are not in the new one are disabled (they are not deleted:
        // other threads may refer to them), except the children of named loggers (that is probably not intended), unless aDisableExisting is
        // false)
        void handle_existing_loggers(py const& aExisting, py const& aChildLoggers, py const& aDisableExisting)
        {
            py const root = logging_("root");
            for (py const& log : aExisting.items())
            {
                py const logger = root.attr("manager").attr("loggerDict")[log];
                if (aChildLoggers.contains(log))
                {
                    if (!isinstance(logger, logging_("PlaceHolder")))
                    {
                        logger.method("setLevel", { logging_("NOTSET") });
                        logger.set_attr("handlers", py_list());
                        logger.set_attr("propagate", boolean(true));
                    }
                }
                else
                    logger.set_attr("disabled", aDisableExisting);
            }
        }

        // (the loggers that exist, sorted (so that the children of a named logger follow it): those that a configuration does not configure
        // are aExisting's at the end, and the children of those that it does are aChildLoggers's)
        void note_configured(py const& aExisting, py const& aChildLoggers, py const& aName)
        {
            if (!aExisting.contains(aName))
                return;
            py i = aExisting.method("index", { aName }) + py{ 1 };   // (the entry after the name)
            py const prefixed = aName + S(".");
            py const pflen = py{ prefixed.size() };
            py const numExisting = py{ aExisting.size() };
            while (i < numExisting)
            {
                if (slice(aExisting[i], none(), pflen) == prefixed)
                    aChildLoggers.method("append", { aExisting[i] });
                i = i + py{ 1 };
            }
            aExisting.method("remove", { aName });
        }

        // (_install_loggers: the loggers of a ConfigParser's configuration, created and installed)
        void install_loggers(py const& cp, py const& aHandlers, py const& aDisableExisting)
        {
            // (the root first)
            py llist = cp[S("loggers")][S("keys")];
            llist = llist.method("split", { S(",") });
            llist = builtin("list")(strip_spaces(llist));
            llist.method("remove", { S("root") });
            py section = cp[S("logger_root")];
            py const root = logging_("root");
            py const log = root;
            if (section.contains(S("level")))
                log.method("setLevel", { section[S("level")] });
            for (py const& h : slice(root.attr("handlers"), none(), none()).items())
                root.method("removeHandler", { h });
            py hlist = section[S("handlers")];
            if (hlist.size())
            {
                hlist = strip_spaces(hlist.method("split", { S(",") }));
                for (py const& hand : hlist.items())
                    log.method("addHandler", { aHandlers[hand] });
            }
            // (the others: the existing loggers are not lost (other threads may refer to them))
            py const existing = builtin("list")(root.attr("manager").attr("loggerDict").method("keys"));
            existing.method("sort");
            py const childLoggers = py_list();
            for (py const& name : llist.items())
            {
                section = cp[S("logger_%s") % py_tuple({ name })];
                py const qn = section[S("qualname")];
                py const propagate = section.method("getint", { S("propagate") }, { { "fallback", py{ 1 } } });
                py const logger = logging_("getLogger")(qn);
                note_configured(existing, childLoggers, qn);
                if (section.contains(S("level")))
                    logger.method("setLevel", { section[S("level")] });
                for (py const& h : slice(logger.attr("handlers"), none(), none()).items())
                    logger.method("removeHandler", { h });
                logger.set_attr("propagate", propagate);
                logger.set_attr("disabled", py{ 0 });
                hlist = section[S("handlers")];
                if (hlist.size())
                {
                    hlist = strip_spaces(hlist.method("split", { S(",") }));
                    for (py const& hand : hlist.items())
                        logger.method("addHandler", { aHandlers[hand] });
                }
            }
            handle_existing_loggers(existing, childLoggers, aDisableExisting);
        }

        // (_clearExistingHandlers: the existing handlers, cleared and closed)
        void clear_existing_handlers()
        {
            logging_("_handlers").method("clear");
            py const handlerList = logging_("_handlerList");
            logging_("shutdown")(slice(handlerList, none(), none()));
            handlerList.method("__delitem__", { py{ make_slice(none().get(), none().get(), nullptr) } });
        }

        // (fileConfig: the configuration of a ConfigParser-format file)
        void file_config(py const& aFname, py const& aDefaults, py const& aDisableExistingLoggers, py const& aEncoding)
        {
            py const configparser{ fn_import("configparser") };
            if (isinstance(aFname, builtin("str")))
            {
                if (!M("os", "path").attr("exists")(aFname).truthy())
                    py_raise("FileNotFoundError", aFname.str() + " doesn't exist");
                else if (!M("os", "path").attr("getsize")(aFname).truthy())
                    py_raise("RuntimeError", aFname.str() + " is an empty file");
            }
            py cp;
            if (isinstance(aFname, configparser.attr("RawConfigParser")))
                cp = aFname;
            else
                try_except([&]()
                {
                    cp = configparser.attr("ConfigParser")(aDefaults);
                    if (has_attr(aFname, "readline"))
                        cp.method("read_file", { aFname });
                    else
                    {
                        py const encoding = M("io", "text_encoding")(aEncoding);
                        cp.method("read", { aFname }, { { "encoding", encoding } });
                    }
                }, configparser.attr("ParsingError"), [&](py const& e)
                {
                    py_raise("RuntimeError", builtin("str")(aFname).str() + " is invalid: " + builtin("str")(e).str());
                });
            py const formatters = create_formatters(cp);
            // (critical section)
            with_lock(logging_("_lock"), [&]()
            {
                clear_existing_handlers();
                // (the handlers add themselves to logging._handlers)
                py const handlers = install_handlers(cp, formatters);
                install_loggers(cp, handlers, aDisableExistingLoggers);
            });
        }

        // (_is_queue_like_object: whether an object implements the Queue API)
        bool is_queue_like_object(py const& aObject)
        {
            if (isinstance(aObject, py_tuple({ M("queue", "Queue"), M("queue", "SimpleQueue") })))
                return true;
            // (multiprocessing is imported as late as possible (and, if it cannot be imported, there are no queues of its))
            bool mpQueue = false;
            try_except([&]()
            {
                py const MPQueue = import_attribute("multiprocessing.queues", "Queue");
                mpQueue = isinstance(aObject, MPQueue);
            }, builtin("ImportError"), [](py const&) {});
            if (mpQueue)
                return true;
            // (a protocol check (an object implementing the Queue API is all that is needed: see gh-119819 and gh-121723): only put_nowait and
            // get are required by the queue handler and the queue listener (gh-124653))
            for (auto m : { "put_nowait", "get" })
                if (!callable(builtin("getattr")(aObject, S(m), none())))
                    return false;
            return true;
        }

        void make_converting_classes()
        {
            make_config_class("ConvertingMixin", {}, {
                { "convert_with_key", method("ConvertingMixin", "convert_with_key", { py_positional("key"), py_positional("value"), py_positional("replace", boolean(true)) },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const result = self.attr("configurator").method("convert", { a[2] });
                        // (a converted value is kept for next time)
                        if (!a[2].is(result))
                        {
                            if (a[3].truthy())
                                self.set_item(a[1], result);
                            if (py_tuple({ cls("ConvertingDict"), cls("ConvertingList"), cls("ConvertingTuple") }).contains(type_of(result)))
                            {
                                result.set_attr("parent", self);
                                result.set_attr("key", a[1]);
                            }
                        }
                        return result;
                    }) },
                { "convert", method("ConvertingMixin", "convert", { py_positional("value") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const result = self.attr("configurator").method("convert", { a[1] });
                        if (!a[1].is(result))
                            if (py_tuple({ cls("ConvertingDict"), cls("ConvertingList"), cls("ConvertingTuple") }).contains(type_of(result)))
                                result.set_attr("parent", self);
                        return result;
                    }) } });
            py const mixin = cls("ConvertingMixin");
            // (the ConvertingXXX classes: wrappers of the containers that convert their values (dicts, lists and tuples to wrappers, and strs of a
            // conversion format as it specifies); their configurator does the conversion)
            make_config_class("ConvertingDict", { builtin("dict"), mixin }, {
                { "__getitem__", method("ConvertingDict", "__getitem__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        py const value = builtin("dict").attr("__getitem__")(a[0], a[1]);
                        return a[0].method("convert_with_key", { a[1], value });
                    }) },
                { "get", method("ConvertingDict", "get", { py_positional("key"), py_positional("default", none()) }, [](std::vector<py> const& a)
                    {
                        py const value = builtin("dict").attr("get")(a[0], a[1], a[2]);
                        return a[0].method("convert_with_key", { a[1], value });
                    }) },
                { "pop", method("ConvertingDict", "pop", { py_positional("key"), py_positional("default", none()) }, [](std::vector<py> const& a)
                    {
                        py const value = builtin("dict").attr("pop")(a[0], a[1], a[2]);
                        return a[0].method("convert_with_key", { a[1], value }, { { "replace", boolean(false) } });
                    }) } });
            make_config_class("ConvertingList", { builtin("list"), mixin }, {
                { "__getitem__", method("ConvertingList", "__getitem__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        py const value = builtin("list").attr("__getitem__")(a[0], a[1]);
                        return a[0].method("convert_with_key", { a[1], value });
                    }) },
                { "pop", method("ConvertingList", "pop", { py_positional("idx", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        py const value = builtin("list").attr("pop")(a[0], a[1]);
                        return a[0].method("convert", { value });
                    }) } });
            make_config_class("ConvertingTuple", { builtin("tuple"), mixin }, {
                { "__getitem__", method("ConvertingTuple", "__getitem__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        py const value = builtin("tuple").attr("__getitem__")(a[0], a[1]);
                        // (a tuple's item cannot be replaced)
                        return a[0].method("convert_with_key", { a[1], value }, { { "replace", boolean(false) } });
                    }) } });
        }

        void make_configurators()
        {
            py const compile = M("re", "compile");
            py const valueConverters = py_dict();
            valueConverters.set_item(S("ext"), S("ext_convert"));
            valueConverters.set_item(S("cfg"), S("cfg_convert"));
            make_config_class("BaseConfigurator", {}, {
                { "CONVERT_PATTERN", compile(S(R"(^(?P<prefix>[a-z]+)://(?P<suffix>.*)$)")) },
                { "WORD_PATTERN", compile(S(R"(^\s*(\w+)\s*)")) },
                { "DOT_PATTERN", compile(S(R"(^\.\s*(\w+)\s*)")) },
                { "INDEX_PATTERN", compile(S(R"(^\[([^\[\]]*)\]\s*)")) },
                { "DIGIT_PATTERN", compile(S(R"(^\d+$)")) },
                { "value_converters", valueConverters },
                // (another could be used, e.g. importlib's)
                { "importer", builtin("staticmethod")(builtin("__import__")) },
                { "__init__", method("BaseConfigurator", "__init__", { py_positional("config") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("config", cls("ConvertingDict")(a[1]));
                        a[0].attr("config").set_attr("configurator", a[0]);
                        return none();
                    }) },
                { "resolve", method("BaseConfigurator", "resolve", { py_positional("s") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py found;
                        try_except([&]()
                        {
                            py const name = a[1].method("split", { S(".") });
                            py used = name.method("pop", { py{ 0 } });
                            found = self.method("importer", { used });
                            for (py const& frag : name.items())
                            {
                                used = used + S(".") + frag;
                                try_except([&]() { found = builtin("getattr")(found, frag); }, builtin("AttributeError"), [&](py const&)
                                {
                                    self.method("importer", { used });
                                    found = builtin("getattr")(found, frag);
                                });
                            }
                        }, builtin("ImportError"), [&](py const& e)
                        {
                            raise_from("ValueError", repr_format("Cannot resolve %r: %s", { a[1], e }), e);
                        });
                        return found;
                    }) },
                { "ext_convert", method("BaseConfigurator", "ext_convert", { py_positional("value") }, [](std::vector<py> const& a)
                    {
                        return a[0].method("resolve", { a[1] });
                    }) },
                { "cfg_convert", method("BaseConfigurator", "cfg_convert", { py_positional("value") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const value = a[1];
                        py rest = value;
                        py m = self.attr("WORD_PATTERN").method("match", { rest });
                        py d;
                        if (m.is_none())
                            py_raise("ValueError", repr_format("Unable to convert %r", { value }).str());
                        else
                        {
                            rest = slice(rest, m.method("end"), none());
                            d = self.attr("config")[m.method("groups")[py{ 0 }]];
                            while (rest.truthy())
                            {
                                m = self.attr("DOT_PATTERN").method("match", { rest });
                                if (m.truthy())
                                    d = d[m.method("groups")[py{ 0 }]];
                                else
                                {
                                    m = self.attr("INDEX_PATTERN").method("match", { rest });
                                    if (m.truthy())
                                    {
                                        py const idx = m.method("groups")[py{ 0 }];
                                        if (!self.attr("DIGIT_PATTERN").method("match", { idx }).truthy())
                                            d = d[idx];
                                        else
                                            try_except([&]()
                                            {
                                                py const n = builtin("int")(idx);   // (as a number first (most likely))
                                                d = d[n];
                                            }, builtin("TypeError"), [&](py const&) { d = d[idx]; });
                                    }
                                }
                                if (m.truthy())
                                    rest = slice(rest, m.method("end"), none());
                                else
                                    py_raise("ValueError", repr_format("Unable to convert %r at %r", { value, rest }).str());
                            }
                        }
                        // (rest is empty)
                        return d;
                    }) },
                { "convert", method("BaseConfigurator", "convert", { py_positional("value") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py value = a[1];
                        if (!isinstance(value, cls("ConvertingDict")) && isinstance(value, builtin("dict")))
                        {
                            value = cls("ConvertingDict")(value);
                            value.set_attr("configurator", self);
                        }
                        else if (!isinstance(value, cls("ConvertingList")) && isinstance(value, builtin("list")))
                        {
                            value = cls("ConvertingList")(value);
                            value.set_attr("configurator", self);
                        }
                        else if (!isinstance(value, cls("ConvertingTuple")) && isinstance(value, builtin("tuple")) && !has_attr(value, "_fields"))
                        {
                            value = cls("ConvertingTuple")(value);
                            value.set_attr("configurator", self);
                        }
                        else if (isinstance(value, builtin("str")))
                        {
                            py const m = self.attr("CONVERT_PATTERN").method("match", { value });
                            if (m.truthy())
                            {
                                py const d = m.method("groupdict");
                                py const prefix = d[S("prefix")];
                                py converter = self.attr("value_converters").method("get", { prefix, none() });
                                if (converter.truthy())
                                {
                                    py const suffix = d[S("suffix")];
                                    converter = builtin("getattr")(self, converter);
                                    value = converter(suffix);
                                }
                            }
                        }
                        return value;
                    }) },
                { "configure_custom", method("BaseConfigurator", "configure_custom", { py_positional("config") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& config = a[1];
                        py c = config.method("pop", { S("()") });
                        if (!callable(c))
                            c = self.method("resolve", { c });
                        // (the keyword arguments: valid identifiers)
                        py const kwargs = identifier_kwargs(config);
                        py const result = call_kw(c, {}, kwargs);
                        py const props = config.method("pop", { S("."), none() });
                        if (props.truthy())
                            for (py const& item : props.method("items").items())
                                builtin("setattr")(result, item[py{ 0 }], item[py{ 1 }]);
                        return result;
                    }) },
                { "as_tuple", method("BaseConfigurator", "as_tuple", { py_positional("value") }, [](std::vector<py> const& a)
                    {
                        if (isinstance(a[1], builtin("list")))
                            return builtin("tuple")(a[1]);
                        return a[1];
                    }) } });

            make_config_class("DictConfigurator", { cls("BaseConfigurator") }, {
                { "configure", method("DictConfigurator", "configure", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const config = self.attr("config");
                        if (!config.contains(S("version")))
                            py_raise("ValueError", "dictionary doesn't specify a version");
                        if (config[S("version")] != py{ 1 })
                            py_raise("ValueError", (S("Unsupported version: %s") % py_tuple({ config[S("version")] })).str());
                        py const incremental = config.method("pop", { S("incremental"), boolean(false) });
                        py const emptyDict = py_dict();
                        with_lock(logging_("_lock"), [&]()
                        {
                            if (incremental.truthy())
                            {
                                py const handlers = config.method("get", { S("handlers"), emptyDict });
                                for (py const& name : handlers.items())
                                {
                                    if (!logging_("_handlers").contains(name))
                                        py_raise("ValueError", repr_format("No handler found with name %r", { name }).str());
                                    else
                                        or_value_error(repr_format("Unable to configure handler %r", { name }), [&]()
                                        {
                                            py const handler = logging_("_handlers")[name];
                                            py const handlerConfig = handlers[name];
                                            py const level = handlerConfig.method("get", { S("level"), none() });
                                            if (level.truthy())
                                                handler.method("setLevel", { logging_("_checkLevel")(level) });
                                        });
                                }
                                py const loggers = config.method("get", { S("loggers"), emptyDict });
                                for (py const& name : loggers.items())
                                    or_value_error(repr_format("Unable to configure logger %r", { name }), [&]()
                                    {
                                        self.method("configure_logger", { name, loggers[name], boolean(true) });
                                    });
                                py const root = config.method("get", { S("root"), none() });
                                if (root.truthy())
                                    or_value_error(S("Unable to configure root logger"), [&]() { self.method("configure_root", { root, boolean(true) }); });
                            }
                            else
                            {
                                py const disableExisting = config.method("pop", { S("disable_existing_loggers"), boolean(true) });
                                clear_existing_handlers();
                                // (the formatters first: they refer to nothing else)
                                py const formatters = config.method("get", { S("formatters"), emptyDict });
                                for (py const& name : formatters.items())
                                    or_value_error(repr_format("Unable to configure formatter %r", { name }), [&]()
                                    {
                                        formatters.set_item(name, self.method("configure_formatter", { formatters[name] }));
                                    });
                                // (the filters: they refer to nothing else, either)
                                py const filters = config.method("get", { S("filters"), emptyDict });
                                for (py const& name : filters.items())
                                    or_value_error(repr_format("Unable to configure filter %r", { name }), [&]()
                                    {
                                        filters.set_item(name, self.method("configure_filter", { filters[name] }));
                                    });
                                // (the handlers: they refer to formatters and filters (and they can refer to other handlers: their keys are sorted,
                                // so that the order of their configuration is deterministic))
                                py const handlers = config.method("get", { S("handlers"), emptyDict });
                                py const deferred = py_list();
                                for (py const& name : builtin("sorted")(handlers).items())
                                    try_except([&]()
                                    {
                                        py const handler = self.method("configure_handler", { handlers[name] });
                                        handler.set_attr("name", name);
                                        handlers.set_item(name, handler);
                                    }, builtin("Exception"), [&](py const& e)
                                    {
                                        if (builtin("str")(e.attr("__cause__")).contains(S(" not configured yet")))
                                            deferred.method("append", { name });
                                        else
                                            raise_from("ValueError", repr_format("Unable to configure handler %r", { name }), e);
                                    });
                                // (those that were deferred)
                                for (py const& name : deferred.items())
                                    or_value_error(repr_format("Unable to configure handler %r", { name }), [&]()
                                    {
                                        py const handler = self.method("configure_handler", { handlers[name] });
                                        handler.set_attr("name", name);
                                        handlers.set_item(name, handler);
                                    });
                                // (the loggers: they refer to handlers and filters (the existing loggers are not lost: other threads may refer to
                                // them))
                                py const root = logging_("root");
                                py const existing = builtin("list")(root.attr("manager").attr("loggerDict").method("keys"));
                                existing.method("sort");
                                py const childLoggers = py_list();
                                py const loggers = config.method("get", { S("loggers"), emptyDict });
                                for (py const& name : loggers.items())
                                {
                                    note_configured(existing, childLoggers, name);
                                    or_value_error(repr_format("Unable to configure logger %r", { name }), [&]()
                                    {
                                        self.method("configure_logger", { name, loggers[name] });
                                    });
                                }
                                handle_existing_loggers(existing, childLoggers, disableExisting);
                                // (finally, the root logger)
                                py const rootConfig = config.method("get", { S("root"), none() });
                                if (rootConfig.truthy())
                                    or_value_error(S("Unable to configure root logger"), [&]() { self.method("configure_root", { rootConfig }); });
                            }
                        });
                        return none();
                    }) },
                { "configure_formatter", method("DictConfigurator", "configure_formatter", { py_positional("config") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& config = a[1];
                        py result;
                        if (config.contains(S("()")))
                        {
                            py const factory = config[S("()")];     // (for the exception handler)
                            try_except([&]() { result = self.method("configure_custom", { config }); }, builtin("TypeError"), [&](py const& te)
                            {
                                if (!builtin("str")(te).contains(S("'format'")))
                                    py_reraise(te);
                                // (logging.Formatter and its subclasses expect the fmt parameter, not format: retried with fmt)
                                config.set_item(S("fmt"), config.method("pop", { S("format") }));
                                config.set_item(S("()"), factory);
                                result = self.method("configure_custom", { config });
                            });
                        }
                        else
                        {
                            py const fmt = config.method("get", { S("format"), none() });
                            py const dfmt = config.method("get", { S("datefmt"), none() });
                            py const style = config.method("get", { S("style"), S("%") });
                            py const cname = config.method("get", { S("class"), none() });
                            py const defaults = config.method("get", { S("defaults"), none() });
                            py const c = !cname.truthy() ? logging_("Formatter") : resolve_name(cname);
                            py const kwargs = py_dict();
                            // (defaults only if it is given (formatter callables that do not take it would raise TypeError))
                            if (!defaults.is_none())
                                kwargs.set_item(S("defaults"), defaults);
                            // (validate only if it is given (formatter callables that do not take it would raise TypeError))
                            if (config.contains(S("validate")))
                                result = call_kw(c, { fmt, dfmt, style, config[S("validate")] }, kwargs);
                            else
                                result = call_kw(c, { fmt, dfmt, style }, kwargs);
                        }
                        return result;
                    }) },
                { "configure_filter", method("DictConfigurator", "configure_filter", { py_positional("config") }, [](std::vector<py> const& a)
                    {
                        if (a[1].contains(S("()")))
                            return a[0].method("configure_custom", { a[1] });
                        py const name = a[1].method("get", { S("name"), S("") });
                        return logging_("Filter")(name);
                    }) },
                { "add_filters", method("DictConfigurator", "add_filters", { py_positional("filterer"), py_positional("filters") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        for (py const& f : a[2].items())
                            or_value_error(repr_format("Unable to add filter %r", { f }), [&]()
                            {
                                py filter;
                                if (callable(f) || callable(builtin("getattr")(f, S("filter"), none())))
                                    filter = f;
                                else
                                    filter = self.attr("config")[S("filters")][f];
                                a[1].method("addFilter", { filter });
                            });
                        return none();
                    }) },
                { "_configure_queue_handler", method("DictConfigurator", "_configure_queue_handler", { py_positional("klass") }, [](std::vector<py> const& a)
                    {
                        py const kwargs = a[2];
                        py q;
                        if (kwargs.contains(S("queue")))
                            q = kwargs.method("pop", { S("queue") });
                        else
                            q = M("queue", "Queue")();     // (unbounded)
                        py const rhl = kwargs.method("pop", { S("respect_handler_level"), boolean(false) });
                        py const lklass = kwargs.method("pop", { S("listener"), handlers_("QueueListener") });
                        py const handlers = kwargs.method("pop", { S("handlers"), py_list() });
                        std::vector<py> args{ q };
                        for (py const& h : handlers.items())
                            args.push_back(h);
                        py const listenerKwargs = py_dict();
                        listenerKwargs.set_item(S("respect_handler_level"), rhl);
                        py const listener = call_kw(lklass, args, listenerKwargs);
                        py const handler = call_kw(a[1], { q }, kwargs);
                        handler.set_attr("listener", listener);
                        return handler;
                    }, false, true) },
                { "configure_handler", method("DictConfigurator", "configure_handler", { py_positional("config") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& config = a[1];
                        py const configCopy = builtin("dict")(config);     // (restored if there is an error)
                        py formatter = config.method("pop", { S("formatter"), none() });
                        if (formatter.truthy())
                            or_value_error(repr_format("Unable to set formatter %r", { formatter }), [&]()
                            {
                                formatter = self.attr("config")[S("formatters")][formatter];
                            });
                        py const level = config.method("pop", { S("level"), none() });
                        py const filters = config.method("pop", { S("filters"), none() });
                        py factory;
                        if (config.contains(S("()")))
                        {
                            py c = config.method("pop", { S("()") });
                            if (!callable(c))
                                c = self.method("resolve", { c });
                            factory = c;
                        }
                        else
                        {
                            py const cname = config.method("pop", { S("class") });
                            py klass;
                            if (callable(cname))
                                klass = cname;
                            else
                                klass = self.method("resolve", { cname });
                            if (issubclass(klass, handlers_("MemoryHandler")))
                            {
                                if (config.contains(S("flushLevel")))
                                    config.set_item(S("flushLevel"), logging_("_checkLevel")(config[S("flushLevel")]));
                                if (config.contains(S("target")))
                                {
                                    // (a handler which refers to another handler)
                                    py tn;
                                    try_except([&]()
                                    {
                                        tn = config[S("target")];
                                        py const th = self.attr("config")[S("handlers")][tn];
                                        if (!isinstance(th, logging_("Handler")))
                                        {
                                            config.method("update", { configCopy });   // (restored for the deferred configuration)
                                            py_raise("TypeError", "target not configured yet");
                                        }
                                        config.set_item(S("target"), th);
                                    }, builtin("Exception"), [&](py const& e)
                                    {
                                        raise_from("ValueError", repr_format("Unable to set target handler %r", { tn }), e);
                                    });
                                }
                            }
                            else if (issubclass(klass, handlers_("QueueHandler")))
                            {
                                // (a handler which refers to other handlers)
                                if (config.contains(S("queue")))
                                {
                                    py const qspec = config[S("queue")];
                                    if (isinstance(qspec, builtin("str")))
                                    {
                                        py const q = self.method("resolve", { qspec });
                                        if (!callable(q))
                                            py_raise("TypeError", repr_format("Invalid queue specifier %r", { qspec }).str());
                                        config.set_item(S("queue"), q());
                                    }
                                    else if (isinstance(qspec, builtin("dict")))
                                    {
                                        if (!qspec.contains(S("()")))
                                            py_raise("TypeError", repr_format("Invalid queue specifier %r", { qspec }).str());
                                        config.set_item(S("queue"), self.method("configure_custom", { builtin("dict")(qspec) }));
                                    }
                                    else if (!is_queue_like_object(qspec))
                                        py_raise("TypeError", repr_format("Invalid queue specifier %r", { qspec }).str());
                                }
                                if (config.contains(S("listener")))
                                {
                                    py const lspec = config[S("listener")];
                                    if (isinstance(lspec, builtin("type")))
                                    {
                                        if (!issubclass(lspec, handlers_("QueueListener")))
                                            py_raise("TypeError", repr_format("Invalid listener specifier %r", { lspec }).str());
                                    }
                                    else
                                    {
                                        py listener;
                                        if (isinstance(lspec, builtin("str")))
                                        {
                                            listener = self.method("resolve", { lspec });
                                            if (isinstance(listener, builtin("type")) && !issubclass(listener, handlers_("QueueListener")))
                                                py_raise("TypeError", repr_format("Invalid listener specifier %r", { lspec }).str());
                                        }
                                        else if (isinstance(lspec, builtin("dict")))
                                        {
                                            if (!lspec.contains(S("()")))
                                                py_raise("TypeError", repr_format("Invalid listener specifier %r", { lspec }).str());
                                            listener = self.method("configure_custom", { builtin("dict")(lspec) });
                                        }
                                        else
                                            py_raise("TypeError", repr_format("Invalid listener specifier %r", { lspec }).str());
                                        if (!callable(listener))
                                            py_raise("TypeError", repr_format("Invalid listener specifier %r", { lspec }).str());
                                        config.set_item(S("listener"), listener);
                                    }
                                }
                                if (config.contains(S("handlers")))
                                {
                                    py const hlist = py_list();
                                    std::optional<py> hn;
                                    try_except([&]()
                                    {
                                        for (py const& h : config[S("handlers")].items())
                                        {
                                            hn = h;
                                            py const handler = self.attr("config")[S("handlers")][h];
                                            if (!isinstance(handler, logging_("Handler")))
                                            {
                                                config.method("update", { configCopy });   // (restored for the deferred configuration)
                                                py_raise("TypeError", repr_format("Required handler %r is not configured yet", { h }).str());
                                            }
                                            hlist.method("append", { handler });
                                        }
                                    }, builtin("Exception"), [&](py const& e)
                                    {
                                        if (!hn)
                                            py_raise("UnboundLocalError", "cannot access local variable 'hn' where it is not associated with a value");
                                        raise_from("ValueError", repr_format("Unable to set required handler %r", { *hn }), e);
                                    });
                                    config.set_item(S("handlers"), hlist);
                                }
                            }
                            else if (issubclass(klass, handlers_("SMTPHandler")) && config.contains(S("mailhost")))
                                config.set_item(S("mailhost"), self.method("as_tuple", { config[S("mailhost")] }));
                            else if (issubclass(klass, handlers_("SysLogHandler")) && config.contains(S("address")))
                                config.set_item(S("address"), self.method("as_tuple", { config[S("address")] }));
                            if (issubclass(klass, handlers_("QueueHandler")))
                                factory = M("functools", "partial")(self.attr("_configure_queue_handler"), klass);
                            else
                                factory = klass;
                        }
                        py const kwargs = identifier_kwargs(config);
                        py result;
                        try_except([&]() { result = call_kw(factory, {}, kwargs); }, builtin("TypeError"), [&](py const& te)
                        {
                            if (!builtin("str")(te).contains(S("'stream'")))
                                py_reraise(te);
                            // (the argument was strm (it is now stream): retried with the old name (so that code works with older Python
                            // versions, e.g. Django's))
                            kwargs.set_item(S("strm"), kwargs.method("pop", { S("stream") }));
                            result = call_kw(factory, {}, kwargs);
                        });
                        if (formatter.truthy())
                            result.method("setFormatter", { formatter });
                        if (!level.is_none())
                            result.method("setLevel", { logging_("_checkLevel")(level) });
                        if (filters.truthy())
                            self.method("add_filters", { result, filters });
                        py const props = config.method("pop", { S("."), none() });
                        if (props.truthy())
                            for (py const& item : props.method("items").items())
                                builtin("setattr")(result, item[py{ 0 }], item[py{ 1 }]);
                        return result;
                    }) },
                { "add_handlers", method("DictConfigurator", "add_handlers", { py_positional("logger"), py_positional("handlers") }, [](std::vector<py> const& a)
                    {
                        for (py const& h : a[2].items())
                            or_value_error(repr_format("Unable to add handler %r", { h }), [&]()
                            {
                                a[1].method("addHandler", { a[0].attr("config")[S("handlers")][h] });
                            });
                        return none();
                    }) },
                { "common_logger_config", method("DictConfigurator", "common_logger_config", { py_positional("logger"), py_positional("config"),
                    py_positional("incremental", boolean(false)) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& logger = a[1];
                        auto const& config = a[2];
                        py const level = config.method("get", { S("level"), none() });
                        if (!level.is_none())
                            logger.method("setLevel", { logging_("_checkLevel")(level) });
                        if (!a[3].truthy())
                        {
                            // (the existing handlers, removed)
                            for (py const& h : slice(logger.attr("handlers"), none(), none()).items())
                                logger.method("removeHandler", { h });
                            py const handlers = config.method("get", { S("handlers"), none() });
                            if (handlers.truthy())
                                self.method("add_handlers", { logger, handlers });
                            py const filters = config.method("get", { S("filters"), none() });
                            if (filters.truthy())
                                self.method("add_filters", { logger, filters });
                        }
                        return none();
                    }) },
                { "configure_logger", method("DictConfigurator", "configure_logger", { py_positional("name"), py_positional("config"),
                    py_positional("incremental", boolean(false)) }, [](std::vector<py> const& a)
                    {
                        py const logger = logging_("getLogger")(a[1]);
                        a[0].method("common_logger_config", { logger, a[2], a[3] });
                        logger.set_attr("disabled", boolean(false));
                        py const propagate = a[2].method("get", { S("propagate"), none() });
                        if (!propagate.is_none())
                            logger.set_attr("propagate", propagate);
                        return none();
                    }) },
                { "configure_root", method("DictConfigurator", "configure_root", { py_positional("config"), py_positional("incremental", boolean(false)) },
                    [](std::vector<py> const& a)
                    {
                        py const root = logging_("getLogger")();
                        a[0].method("common_logger_config", { root, a[1], a[2] });
                        return none();
                    }) } });
        }

        // (listen's classes (as it defines them each time that it is called): a request's handler, the server and its thread)
        py listen(py const& aPort, py const& aVerify)
        {
            py const threadingTCPServer = G("ThreadingTCPServer");
            py const streamRequestHandler = G("StreamRequestHandler");
            char const* const handlerName = "listen.<locals>.ConfigStreamHandler";
            py const configStreamHandler = make_config_class("ConfigStreamHandler", { streamRequestHandler }, {
                { "handle", method(handlerName, "handle", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        try_except([&]()
                        {
                            py const conn = self.attr("connection");
                            py chunk = conn.method("recv", { py{ 4 } });
                            if (chunk.size() == 4)
                            {
                                py const slen = M("struct", "unpack")(S(">L"), chunk)[py{ 0 }];
                                chunk = self.attr("connection").method("recv", { slen });
                                while (py{ chunk.size() } < slen)
                                    chunk = chunk + conn.method("recv", { slen - py{ chunk.size() } });
                                if (!self.attr("server").attr("verify").is_none())
                                    chunk = self.attr("server").attr("verify")(chunk);
                                if (!chunk.is_none())   // (verified: it can be processed)
                                {
                                    chunk = chunk.method("decode", { S("utf-8") });
                                    try_except([&]()
                                    {
                                        py const d = M("json", "loads")(chunk);
                                        if (!isinstance(d, builtin("dict")))
                                            py_raise("AssertionError", "");
                                        G("dictConfig")(d);
                                    }, builtin("Exception"), [&](py const&)
                                    {
                                        // (the new configuration, applied)
                                        py const file = M("io", "StringIO")(chunk);
                                        try_except([&]() { G("fileConfig")(file); }, builtin("Exception"), [](py const&) { M("traceback", "print_exc")(); });
                                    });
                                }
                                if (self.attr("server").attr("ready").truthy())
                                    self.attr("server").attr("ready").method("set");
                            }
                        }, builtin("OSError"), [&](py const& e)
                        {
                            if (e.attr("errno") != G("RESET_ERROR"))
                                py_reraise(e);
                        });
                        return none();
                    }) } }, handlerName, false);
            char const* const receiverName = "listen.<locals>.ConfigSocketReceiver";
            py const configSocketReceiver = make_config_class("ConfigSocketReceiver", { threadingTCPServer }, {
                { "allow_reuse_address", py{ 1 } },
                { "__init__", method(receiverName, "__init__", { py_positional("host", S("localhost")), py_positional("port", py{ 9030 }), py_positional("handler", none()),
                    py_positional("ready", none()), py_positional("verify", none()) }, [threadingTCPServer](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (the host can have no IPv4 address (e.g. if "localhost" is only aliased to ::1): resolution errors are the server's)
                        py infos;
                        bool resolved = true;
                        try_except([&]() { infos = M("socket", "getaddrinfo").call({ a[1], a[2] }, { { "type", M("socket", "SOCK_STREAM") } }); },
                            builtin("OSError"), [&](py const&) { resolved = false; });
                        if (resolved)
                        {
                            bool inet = false;
                            for (py const& info : infos.items())
                                if (info[py{ 0 }] == M("socket", "AF_INET"))
                                    inet = true;
                            if (!inet)
                                self.set_attr("address_family", infos[py{ 0 }][py{ 0 }]);
                        }
                        threadingTCPServer.attr("__init__")(self, py_tuple({ a[1], a[2] }), a[3]);
                        with_lock(logging_("_lock"), [&]() { self.set_attr("abort", py{ 0 }); });
                        self.set_attr("timeout", py{ 1 });
                        self.set_attr("ready", a[4]);
                        self.set_attr("verify", a[5]);
                        return none();
                    }) },
                { "serve_until_stopped", method(receiverName, "serve_until_stopped", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const select{ fn_import("select") };
                        py abort{ 0 };
                        while (!abort.truthy())
                        {
                            py const ready = select.attr("select")(py_list({ self.attr("socket").method("fileno") }), py_list(), py_list(), self.attr("timeout"));
                            if (ready[py{ 0 }].truthy())
                                self.method("handle_request");
                            with_lock(logging_("_lock"), [&]() { abort = self.attr("abort"); });
                        }
                        self.method("server_close");
                        return none();
                    }) } }, receiverName, false);
            char const* const serverName = "listen.<locals>.Server";
            py const server = make_config_class("Server", { M("threading", "Thread") }, {}, serverName, false);
            server.set_attr("__init__", method(serverName, "__init__", { py_positional("rcvr"), py_positional("hdlr"), py_positional("port"), py_positional("verify") },
                [server](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    builtin("super")(server, self).method("__init__");
                    self.set_attr("rcvr", a[1]);
                    self.set_attr("hdlr", a[2]);
                    self.set_attr("port", a[3]);
                    self.set_attr("verify", a[4]);
                    self.set_attr("ready", M("threading", "Event")());
                    return none();
                }));
            server.set_attr("run", method(serverName, "run", {}, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py receiver;
                    try_except([&]()
                    {
                        receiver = self.attr("rcvr").call({}, { { "port", self.attr("port") }, { "handler", self.attr("hdlr") }, { "ready", self.attr("ready") },
                            { "verify", self.attr("verify") } });
                    }, builtin("BaseException"), [&](py const& e)
                    {
                        // (the caller is not left waiting for ready forever)
                        self.attr("ready").method("set");
                        py_reraise(e);
                    });
                    if (self.attr("port") == py{ 0 })
                        self.set_attr("port", receiver.attr("server_address")[py{ 1 }]);
                    self.attr("ready").method("set");
                    with_lock(logging_("_lock"), [&]() { set_G("_listener", receiver); });
                    receiver.method("serve_until_stopped");
                    return none();
                }));
            return server(configSocketReceiver, configStreamHandler, aPort, aVerify);
        }

        bool logging_config_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                auto& g = s.globals;
                if (!s.made)
                {
                    s.made = true;
                    // (the server listening for new configurations (see listen))
                    g["_listener"] = none();
                    g["IDENTIFIER"] = M("re", "compile")(S("^[a-z_][a-z0-9_]*$"), M("re", "I"));
                    g["RESET_ERROR"] = M("errno", "ECONNRESET");
                    make_converting_classes();
                    make_configurators();
                    g["dictConfigClass"] = cls("DictConfigurator");
                    g["fileConfig"] = function("fileConfig", { py_positional("fname"), py_positional("defaults", none()), py_positional("disable_existing_loggers",
                        boolean(true)), py_positional("encoding", none()) }, [](std::vector<py> const& a)
                    {
                        file_config(a[0], a[1], a[2], a[3]);
                        return none();
                    });
                    g["dictConfig"] = function("dictConfig", { py_positional("config") }, [](std::vector<py> const& a)
                    {
                        G("dictConfigClass")(a[0]).method("configure");
                        return none();
                    });
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S(D("")));
                for (auto module : { "errno", "functools", "io", "logging", "os", "queue", "re", "socket", "struct", "threading", "traceback" })
                    add(module, py{ fn_import(module) });
                py const handlers{ fn_import("logging.handlers") };    // (as config.py imports it)
                add("ThreadingTCPServer", M("socketserver", "ThreadingTCPServer"));
                add("StreamRequestHandler", M("socketserver", "StreamRequestHandler"));
                add("DEFAULT_LOGGING_CONFIG_PORT", py{ 9030 });
                for (auto const& [name, value] : g)
                    add(name.c_str(), value);
                for (auto const& [name, value] : s.classes)
                    add(name.c_str(), value);
                add("_resolve", function("_resolve", { py_positional("name") }, [](std::vector<py> const& a) { return resolve_name(a[0]); }));
                add("_strip_spaces", function("_strip_spaces", { py_positional("alist") }, [](std::vector<py> const& a) { return strip_spaces(a[0]); }));
                add("_create_formatters", function("_create_formatters", { py_positional("cp") }, [](std::vector<py> const& a) { return create_formatters(a[0]); }));
                add("_install_handlers", function("_install_handlers", { py_positional("cp"), py_positional("formatters") }, [](std::vector<py> const& a)
                {
                    return install_handlers(a[0], a[1]);
                }));
                add("_handle_existing_loggers", function("_handle_existing_loggers", { py_positional("existing"), py_positional("child_loggers"),
                    py_positional("disable_existing") }, [](std::vector<py> const& a)
                {
                    handle_existing_loggers(a[0], a[1], a[2]);
                    return none();
                }));
                add("_install_loggers", function("_install_loggers", { py_positional("cp"), py_positional("handlers"), py_positional("disable_existing") },
                    [](std::vector<py> const& a)
                {
                    install_loggers(a[0], a[1], a[2]);
                    return none();
                }));
                add("_clearExistingHandlers", function("_clearExistingHandlers", {}, [](std::vector<py> const&)
                {
                    clear_existing_handlers();
                    return none();
                }));
                add("valid_ident", function("valid_ident", { py_positional("s") }, [](std::vector<py> const& a) { return valid_ident(a[0]); }));
                add("_is_queue_like_object", function("_is_queue_like_object", { py_positional("obj") }, [](std::vector<py> const& a)
                {
                    return boolean(is_queue_like_object(a[0]));
                }));
                add("listen", function("listen", { py_positional("port", py{ 9030 }), py_positional("verify", none()) }, [](std::vector<py> const& a)
                {
                    return listen(a[0], a[1]);
                }));
                add("stopListening", function("stopListening", {}, [](std::vector<py> const&)
                {
                    with_lock(logging_("_lock"), [&]()
                    {
                        py const listener = G("_listener");
                        if (listener.truthy())
                        {
                            listener.set_attr("abort", py{ 1 });
                            set_G("_listener", none());
                        }
                    });
                    return none();
                }));
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sLoggingConfig = register_module("logging.config", logging_config_module);
    }
}
