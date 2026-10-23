/*
  configparser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/configparser.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module configparser (CPython 3.13's Lib/configparser.py): the exceptions, the interpolations (Interpolation, BasicInterpolation and
// ExtendedInterpolation), RawConfigParser, ConfigParser, SectionProxy and ConverterMapping; its classes are made (by type, or abc.ABCMeta) of
// closures, so a program's subclasses override their methods. The module's variables that a program can rebind (e.g. MAX_INTERPOLATION_DEPTH)
// are read from the module.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "configparser_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "configparser";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;     ///< (the module's classes, by name)
            py unset;           ///< (_UNSET: the default of the getters' fallbacks (so that None can be a fallback))
            py unnamedSection;  ///< (UNNAMED_SECTION)
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
        py unset() { return state().unset; }
        char const* D(char const* aName)
        {
            auto const doc = configparser_docs::doc(aName);
            return doc != nullptr ? doc : "";
        }
        py M(char const* aModule, char const* aName) { return import_attribute(aModule, aName); }
        bool callable(py const& aValue) { return builtin("callable")(aValue).truthy(); }
        py slice(py const& aSequence, py const& aStart, py const& aStop)
        {
            return aSequence[py{ make_slice(aStart.get(), aStop.get(), nullptr) }];
        }
        // (f(*aArgs, **aKwargs) (aKwargs: a dict, or empty))
        py call_kw(py const& aCallable, std::vector<py> const& aArgs, py const& aKwargs, std::vector<std::pair<char const*, py>> const& aKeywords = {})
        {
            std::vector<object*> args;
            for (auto const& a : aArgs)
                args.push_back(a.reference());
            py kwargs = aKwargs.empty() ? py_dict() : builtin("dict")(aKwargs);
            for (auto const& [name, value] : aKeywords)
                kwargs.set_item(S(name), value);
            return py{ invoke(aCallable.get(), std::move(args), kwargs.size() == 0 ? nullptr : kwargs.get()) };
        }
        // (a variable of the module (that the program can rebind): the module's, once it exists)
        py G(char const* aName, py const& aDefault)
        {
            py const module = import_attribute("sys", "modules").method("get", { S(sModule) });
            if (!module.is_none())
                return module.attr(aName);
            return aDefault;
        }
        // (raise an exception (an instance) from aCause (None: none))
        [[noreturn]] void raise_from(py const& aException, py const& aCause)
        {
            fn_raise_from(aException.get(), aCause.get());
            throw raised{};
        }

        // (a closure as a function of the module)
        using body_type = std::function<py(std::vector<py> const&)>;
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs,
            bool aVarKeywords, char const* aDoc)
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
        // (a method: its parameters after self (aClass: its class's name); its docstring: configparser.py's; *args and **kwargs: appended)
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false, bool aVarKeywords = false)
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            std::string const qualname = std::string{ aClass } + "." + aName;
            return bound_function(aName, qualname, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords, D(qualname.c_str()));
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: its metaclass's (type's, or its bases' (e.g. ABCMeta)) instance; its docstring: configparser.py's)
        py make_parser_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace)
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S(sModule));
            ns.set_item(S("__qualname__"), S(aName));
            if (auto const doc = configparser_docs::doc(aName))
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
            state().classes[aName] = result;
            return result;
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
        // (with aManager as value: aBody(value))
        template <typename F>
        void with_(py const& aManager, F&& aBody)
        {
            py const value = aManager.method("__enter__");
            try
            {
                aBody(value);
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                if (!aManager.method("__exit__", { type_of(e), e, e.attr("__traceback__") }).truthy())
                    py_reraise(e);
                return;
            }
            aManager.method("__exit__", { none(), none(), none() });
        }
        // (for x in aIterable: aBody(x) (lazily: an iterator's items as it makes them))
        template <typename F>
        void for_each(py const& aIterable, F&& aBody)
        {
            py const it{ iter(aIterable.get()) };
            for (;;)
            {
                auto const item = next(it.get());
                if (item == nullptr)
                {
                    if (failed())
                        throw raised{};
                    break;
                }
                aBody(py{ item });
            }
        }
        // (a pair's items (a, b = value))
        std::pair<py, py> unpack2(py const& aValue)
        {
            auto const items = aValue.items();
            if (items.size() != 2u)
                py_raise("ValueError", items.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(items.size()) + ")" :
                    "too many values to unpack (expected 2)");
            return { items[0], items[1] };
        }

        // (Error.__init__(self, aMessage))
        void error_init(py const& self, py const& aMessage) { cls("Error").attr("__init__")(self, aMessage); }

        void make_exceptions()
        {
            py const errorRepr = method("Error", "__repr__", {}, [](std::vector<py> const& a) { return a[0].attr("message"); });
            make_parser_class("Error", { builtin("Exception") }, {
                { "__init__", method("Error", "__init__", { py_positional("msg", S("")) }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("message", a[1]);
                        builtin("Exception").attr("__init__")(a[0], a[1]);
                        return none();
                    }) },
                { "__repr__", errorRepr },
                { "__str__", errorRepr } });
            py const error = cls("Error");
            make_parser_class("NoSectionError", { error }, {
                { "__init__", method("NoSectionError", "__init__", { py_positional("section") }, [](std::vector<py> const& a)
                    {
                        error_init(a[0], S("No section: %r") % py_tuple({ a[1] }));
                        a[0].set_attr("section", a[1]);
                        a[0].set_attr("args", py_tuple({ a[1] }));
                        return none();
                    }) } });
            // (Duplicate(Section|Option)Error's message: "While reading from <source> [line <n>]: <kind> <aMsg>" or "<Kind> <aMsg>")
            auto const duplicate_message = [](std::vector<py> aMsg, char const* aKind, char const* aCapitalised, py const& aSource, py const& aLineno)
            {
                py msg = py_list(aMsg);
                if (!aSource.is_none())
                {
                    py const message = py_list({ S("While reading from "), builtin("repr")(aSource) });
                    if (!aLineno.is_none())
                        message.method("append", { S(" [line {0:2d}]").method("format", { aLineno }) });
                    message.method("append", { S(aKind) });
                    message.method("extend", { msg });
                    msg = message;
                }
                else
                    msg.method("insert", { py{ 0 }, S(aCapitalised) });
                return S("").method("join", { msg });
            };
            make_parser_class("DuplicateSectionError", { error }, {
                { "__init__", method("DuplicateSectionError", "__init__", { py_positional("section"), py_positional("source", none()), py_positional("lineno", none()) },
                    [duplicate_message](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        error_init(self, duplicate_message({ builtin("repr")(a[1]), S(" already exists") }, ": section ", "Section ", a[2], a[3]));
                        self.set_attr("section", a[1]);
                        self.set_attr("source", a[2]);
                        self.set_attr("lineno", a[3]);
                        self.set_attr("args", py_tuple({ a[1], a[2], a[3] }));
                        return none();
                    }) } });
            make_parser_class("DuplicateOptionError", { error }, {
                { "__init__", method("DuplicateOptionError", "__init__", { py_positional("section"), py_positional("option"), py_positional("source", none()),
                    py_positional("lineno", none()) }, [duplicate_message](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        error_init(self, duplicate_message({ builtin("repr")(a[2]), S(" in section "), builtin("repr")(a[1]), S(" already exists") }, ": option ",
                            "Option ", a[3], a[4]));
                        self.set_attr("section", a[1]);
                        self.set_attr("option", a[2]);
                        self.set_attr("source", a[3]);
                        self.set_attr("lineno", a[4]);
                        self.set_attr("args", py_tuple({ a[1], a[2], a[3], a[4] }));
                        return none();
                    }) } });
            make_parser_class("NoOptionError", { error }, {
                { "__init__", method("NoOptionError", "__init__", { py_positional("option"), py_positional("section") }, [](std::vector<py> const& a)
                    {
                        error_init(a[0], S("No option %r in section: %r") % py_tuple({ a[1], a[2] }));
                        a[0].set_attr("option", a[1]);
                        a[0].set_attr("section", a[2]);
                        a[0].set_attr("args", py_tuple({ a[1], a[2] }));
                        return none();
                    }) } });
            make_parser_class("InterpolationError", { error }, {
                { "__init__", method("InterpolationError", "__init__", { py_positional("option"), py_positional("section"), py_positional("msg") }, [](std::vector<py> const& a)
                    {
                        error_init(a[0], a[3]);
                        a[0].set_attr("option", a[1]);
                        a[0].set_attr("section", a[2]);
                        a[0].set_attr("args", py_tuple({ a[1], a[2], a[3] }));
                        return none();
                    }) } });
            py const interpolationError = cls("InterpolationError");
            make_parser_class("InterpolationMissingOptionError", { interpolationError }, {
                { "__init__", method("InterpolationMissingOptionError", "__init__", { py_positional("option"), py_positional("section"), py_positional("rawval"),
                    py_positional("reference") }, [](std::vector<py> const& a)
                    {
                        py const msg = S("Bad value substitution: option {!r} in section {!r} contains an interpolation key {!r} which is not a valid option name. "
                            "Raw value: {!r}").method("format", { a[1], a[2], a[4], a[3] });
                        cls("InterpolationError").attr("__init__")(a[0], a[1], a[2], msg);
                        a[0].set_attr("reference", a[4]);
                        a[0].set_attr("args", py_tuple({ a[1], a[2], a[3], a[4] }));
                        return none();
                    }) } });
            make_parser_class("InterpolationSyntaxError", { interpolationError }, {});
            make_parser_class("InterpolationDepthError", { interpolationError }, {
                { "__init__", method("InterpolationDepthError", "__init__", { py_positional("option"), py_positional("section"), py_positional("rawval") },
                    [](std::vector<py> const& a)
                    {
                        py const msg = S("Recursion limit exceeded in value substitution: option {!r} in section {!r} contains an interpolation key which cannot be "
                            "substituted in {} steps. Raw value: {!r}").method("format", { a[1], a[2], G("MAX_INTERPOLATION_DEPTH", py{ 10 }), a[3] });
                        cls("InterpolationError").attr("__init__")(a[0], a[1], a[2], msg);
                        a[0].set_attr("args", py_tuple({ a[1], a[2], a[3] }));
                        return none();
                    }) } });
            // ("\n\t[line <lineno:2d>]: <line!r>")
            auto const error_line = [](py const& aLineno, py const& aLine)
            {
                return S("\n\t[line ") + builtin("format")(aLineno, S("2d")) + S("]: ") + builtin("repr")(aLine);
            };
            make_parser_class("ParsingError", { error }, {
                { "__init__", method("ParsingError", "__init__", { py_positional("source") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        builtin("super")(cls("ParsingError"), self).method("__init__", { S("Source contains parsing errors: ") + builtin("repr")(a[1]) });
                        self.set_attr("source", a[1]);
                        self.set_attr("errors", py_list());
                        self.set_attr("args", py_tuple({ a[1] }));
                        if (a[2].truthy())
                            self.attr("append").call(a[2].items());
                        return none();
                    }, true) },
                { "append", method("ParsingError", "append", { py_positional("lineno"), py_positional("line") }, [error_line](std::vector<py> const& a)
                    {
                        a[0].attr("errors").method("append", { py_tuple({ a[1], a[2] }) });
                        a[0].set_attr("message", a[0].attr("message") + error_line(a[1], a[2]));
                        return none();
                    }) },
                { "combine", method("ParsingError", "combine", { py_positional("others") }, [error_line](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const messages = py_list({ self.attr("message") });
                        for_each(a[1], [&](py const& other)
                        {
                            for (py const& error : other.attr("errors").items())
                            {
                                auto const [lineno, line] = unpack2(error);
                                self.attr("errors").method("append", { py_tuple({ lineno, line }) });
                                messages.method("append", { error_line(lineno, line) });
                            }
                        });
                        self.set_attr("message", S("").method("join", { messages }));
                        return self;
                    }) },
                { "_raise_all", builtin("staticmethod")(bound_function("_raise_all", "ParsingError._raise_all", { py_positional("exceptions") }, [](std::vector<py> const& a)
                    {
                        // (any number of ParsingErrors combined into one, and raised)
                        py const exceptions = builtin("iter")(a[0]);
                        py first;
                        bool any = true;
                        try_except([&]() { first = builtin("next")(exceptions); }, builtin("StopIteration"), [&](py const&) { any = false; });
                        if (any)
                            py_raise(first.method("combine", { exceptions }));
                        return none();
                    }, false, false, D("ParsingError._raise_all"))) } });
            py const parsingError = cls("ParsingError");
            // (MissingSectionHeaderError's and MultilineContinuationError's __init__: their message, and their source, line number and line)
            auto const line_error_init = [](char const* aMessage)
            {
                return [aMessage](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    error_init(self, S(aMessage) % py_tuple({ a[1], a[2], a[3] }));
                    self.set_attr("source", a[1]);
                    self.set_attr("lineno", a[2]);
                    self.set_attr("line", a[3]);
                    self.set_attr("args", py_tuple({ a[1], a[2], a[3] }));
                    return none();
                };
            };
            make_parser_class("MissingSectionHeaderError", { parsingError }, {
                { "__init__", method("MissingSectionHeaderError", "__init__", { py_positional("filename"), py_positional("lineno"), py_positional("line") },
                    line_error_init("File contains no section headers.\nfile: %r, line: %d\n%r")) } });
            make_parser_class("MultilineContinuationError", { parsingError }, {
                { "__init__", method("MultilineContinuationError", "__init__", { py_positional("filename"), py_positional("lineno"), py_positional("line") },
                    line_error_init("Key without value continued with an indented line.\nfile: %r, line: %d\n%r")) } });
        }

        // (BasicInterpolation's and ExtendedInterpolation's before_set: the value, if its syntax is valid (aEscape: the escaped marker
        // (e.g. "%%"), aMarker: the marker))
        py interpolation_before_set(py const& self, py const& aValue, char const* aEscape, char const* aMarker)
        {
            py tmpValue = aValue.method("replace", { S(aEscape), S("") });     // (the escaped markers)
            tmpValue = self.attr("_KEYCRE").method("sub", { S(""), tmpValue });   // (valid syntax)
            if (tmpValue.contains(S(aMarker)))
                py_raise("ValueError", (S("invalid interpolation syntax in %r at position %d") % py_tuple({ aValue, tmpValue.method("find", { S(aMarker) }) })).str());
            return aValue;
        }

        // (the start of an interpolation's _interpolate_some: the raw value; InterpolationDepthError if the depth is too great)
        py interpolation_raw_value(py const& parser, py const& option, py const& rest, py const& section, py const& depth)
        {
            py const rawval = parser.method("get", { section, option }, { { "raw", boolean(true) }, { "fallback", rest } });
            if (depth > G("MAX_INTERPOLATION_DEPTH", py{ 10 }))
                py_raise(cls("InterpolationDepthError")(option, section, rawval));
            return rawval;
        }

        void make_interpolations()
        {
            make_parser_class("Interpolation", {}, {
                { "before_get", method("Interpolation", "before_get", { py_positional("parser"), py_positional("section"), py_positional("option"), py_positional("value"),
                    py_positional("defaults") }, [](std::vector<py> const& a) { return a[4]; }) },
                { "before_set", method("Interpolation", "before_set", { py_positional("parser"), py_positional("section"), py_positional("option"), py_positional("value") },
                    [](std::vector<py> const& a) { return a[4]; }) },
                { "before_read", method("Interpolation", "before_read", { py_positional("parser"), py_positional("section"), py_positional("option"), py_positional("value") },
                    [](std::vector<py> const& a) { return a[4]; }) },
                { "before_write", method("Interpolation", "before_write", { py_positional("parser"), py_positional("section"), py_positional("option"), py_positional("value") },
                    [](std::vector<py> const& a) { return a[4]; }) } });
            py const interpolation = cls("Interpolation");
            std::vector<py_parameter> const beforeGet{ py_positional("parser"), py_positional("section"), py_positional("option"), py_positional("value"),
                py_positional("defaults") };
            std::vector<py_parameter> const beforeSet{ py_positional("parser"), py_positional("section"), py_positional("option"), py_positional("value") };
            std::vector<py_parameter> const interpolateSome{ py_positional("parser"), py_positional("option"), py_positional("accum"), py_positional("rest"),
                py_positional("section"), py_positional("map"), py_positional("depth") };
            // (before_get: the value's interpolations, expanded)
            auto const before_get = [](std::vector<py> const& a)
            {
                py const L = py_list();
                a[0].method("_interpolate_some", { a[1], a[3], L, a[4], a[2], a[5], py{ 1 } });
                return S("").method("join", { L });
            };
            make_parser_class("BasicInterpolation", { interpolation }, {
                { "_KEYCRE", M("re", "compile")(S(R"(%\(([^)]+)\)s)")) },
                { "before_get", method("BasicInterpolation", "before_get", beforeGet, before_get) },
                { "before_set", method("BasicInterpolation", "before_set", beforeSet, [](std::vector<py> const& a)
                    {
                        return interpolation_before_set(a[0], a[4], "%%", "%");
                    }) },
                { "_interpolate_some", method("BasicInterpolation", "_interpolate_some", interpolateSome, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& parser = a[1];
                        auto const& option = a[2];
                        auto const& accum = a[3];
                        py rest = a[4];
                        auto const& section = a[5];
                        auto const& map = a[6];
                        py const rawval = interpolation_raw_value(parser, option, rest, section, a[7]);
                        while (rest.truthy())
                        {
                            py const p = rest.method("find", { S("%") });
                            if (p < py{ 0 })
                            {
                                accum.method("append", { rest });
                                return none();
                            }
                            if (p > py{ 0 })
                            {
                                accum.method("append", { slice(rest, none(), p) });
                                rest = slice(rest, p, none());
                            }
                            // (p is no longer used)
                            py const c = slice(rest, py{ 1 }, py{ 2 });
                            if (c == S("%"))
                            {
                                accum.method("append", { S("%") });
                                rest = slice(rest, py{ 2 }, none());
                            }
                            else if (c == S("("))
                            {
                                py const m = self.attr("_KEYCRE").method("match", { rest });
                                if (m.is_none())
                                    py_raise(cls("InterpolationSyntaxError")(option, section, S("bad interpolation variable reference %r") % py_tuple({ rest })));
                                py const var = parser.method("optionxform", { m.method("group", { py{ 1 } }) });
                                rest = slice(rest, m.method("end"), none());
                                py v;
                                try_except([&]() { v = map[var]; }, builtin("KeyError"), [&](py const&)
                                {
                                    raise_from(cls("InterpolationMissingOptionError")(option, section, rawval, var), none());
                                });
                                if (v.contains(S("%")))
                                    self.method("_interpolate_some", { parser, option, accum, v, section, map, a[7] + py{ 1 } });
                                else
                                    accum.method("append", { v });
                            }
                            else
                                py_raise(cls("InterpolationSyntaxError")(option, section, S("'%%' must be followed by '%%' or '(', found: %r") % py_tuple({ rest })));
                        }
                        return none();
                    }) } });
            make_parser_class("ExtendedInterpolation", { interpolation }, {
                { "_KEYCRE", M("re", "compile")(S(R"(\$\{([^}]+)\})")) },
                { "before_get", method("ExtendedInterpolation", "before_get", beforeGet, before_get) },
                { "before_set", method("ExtendedInterpolation", "before_set", beforeSet, [](std::vector<py> const& a)
                    {
                        return interpolation_before_set(a[0], a[4], "$$", "$");
                    }) },
                { "_interpolate_some", method("ExtendedInterpolation", "_interpolate_some", interpolateSome, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& parser = a[1];
                        auto const& option = a[2];
                        auto const& accum = a[3];
                        py rest = a[4];
                        auto const& section = a[5];
                        auto const& map = a[6];
                        py const rawval = interpolation_raw_value(parser, option, rest, section, a[7]);
                        while (rest.truthy())
                        {
                            py const p = rest.method("find", { S("$") });
                            if (p < py{ 0 })
                            {
                                accum.method("append", { rest });
                                return none();
                            }
                            if (p > py{ 0 })
                            {
                                accum.method("append", { slice(rest, none(), p) });
                                rest = slice(rest, p, none());
                            }
                            // (p is no longer used)
                            py const c = slice(rest, py{ 1 }, py{ 2 });
                            if (c == S("$"))
                            {
                                accum.method("append", { S("$") });
                                rest = slice(rest, py{ 2 }, none());
                            }
                            else if (c == S("{"))
                            {
                                py const m = self.attr("_KEYCRE").method("match", { rest });
                                if (m.is_none())
                                    py_raise(cls("InterpolationSyntaxError")(option, section, S("bad interpolation variable reference %r") % py_tuple({ rest })));
                                py const path = m.method("group", { py{ 1 } }).method("split", { S(":") });
                                rest = slice(rest, m.method("end"), none());
                                py sect = section;
                                py opt = option;
                                py v;
                                try_except([&]()
                                {
                                    if (path.size() == 1)
                                    {
                                        opt = parser.method("optionxform", { path[py{ 0 }] });
                                        v = map[opt];
                                    }
                                    else if (path.size() == 2)
                                    {
                                        sect = path[py{ 0 }];
                                        opt = parser.method("optionxform", { path[py{ 1 }] });
                                        v = parser.method("get", { sect, opt }, { { "raw", boolean(true) } });
                                    }
                                    else
                                        py_raise(cls("InterpolationSyntaxError")(option, section, S("More than one ':' found: %r") % py_tuple({ rest })));
                                }, py_tuple({ builtin("KeyError"), cls("NoSectionError"), cls("NoOptionError") }), [&](py const&)
                                {
                                    raise_from(cls("InterpolationMissingOptionError")(option, section, rawval, S(":").method("join", { path })), none());
                                });
                                if (v.is_none())
                                    continue;
                                if (v.contains(S("$")))
                                    self.method("_interpolate_some", { parser, opt, accum, v, sect, builtin("dict")(parser.method("items", { sect }, { { "raw", boolean(true) } })),
                                        a[7] + py{ 1 } });
                                else
                                    accum.method("append", { v });
                            }
                            else
                                py_raise(cls("InterpolationSyntaxError")(option, section, S("'$' must be followed by '$' or '{', found: %r") % py_tuple({ rest })));
                        }
                        return none();
                    }) } });
        }

        void make_line_classes()
        {
            make_parser_class("_UnnamedSection", {}, {
                { "__repr__", method("_UnnamedSection", "__repr__", {}, [](std::vector<py> const&) { return S("<UNNAMED_SECTION>"); }) } });
            make_parser_class("_ReadState", {}, {
                { "cursect", none() },
                { "sectname", none() },
                { "optname", none() },
                { "lineno", py{ 0 } },
                { "indent_level", py{ 0 } },
                { "__init__", method("_ReadState", "__init__", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("elements_added", builtin("set")());
                        a[0].set_attr("errors", builtin("list")());
                        return none();
                    }) } });
            make_parser_class("_Line", { builtin("str") }, {
                { "__new__", builtin("staticmethod")(bound_function("__new__", "_Line.__new__", { py_positional("cls"), py_positional("val") }, [](std::vector<py> const& a)
                    {
                        return builtin("str").attr("__new__")(a[0], a[1]);
                    }, true, true, "")) },
                { "__init__", method("_Line", "__init__", { py_positional("val"), py_positional("prefixes") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("prefixes", a[2]);
                        return none();
                    }) },
                { "clean", M("functools", "cached_property")(method("_Line", "clean", {}, [](std::vector<py> const& a)
                    {
                        // (self._strip_full() and self._strip_inline())
                        py const full = a[0].method("_strip_full");
                        if (!full.truthy())
                            return full;
                        return a[0].method("_strip_inline");
                    })) },
                { "has_comments", builtin("property")(method("_Line", "has_comments", {}, [](std::vector<py> const& a)
                    {
                        return boolean(a[0].method("strip") != a[0].attr("clean"));
                    })) },
                { "_strip_inline", method("_Line", "_strip_inline", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (the earliest prefix at the start of the line or after a space (none if there are no prefixes))
                        py const patterns = py_list();
                        for (py const& prefix : self.attr("prefixes").attr("inline").items())
                            patterns.method("append", { S(R"((^|\s)()") + M("re", "escape")(prefix) + S(")") });
                        py pattern = S("|").method("join", { patterns });
                        if (!pattern.truthy())
                            pattern = S("(?!)");
                        py const matcher = M("re", "compile")(pattern);
                        py const match = matcher.method("search", { self });
                        return slice(self, none(), match.truthy() ? match.method("start") : none()).method("strip");
                    }) },
                { "_strip_full", method("_Line", "_strip_full", {}, [](std::vector<py> const& a)
                    {
                        py const stripped = a[0].method("strip");
                        for (py const& prefix : a[0].attr("prefixes").attr("full").items())
                            if (stripped.method("startswith", { prefix }).truthy())
                                return S("");
                        return boolean(true);
                    }) } });
        }

        // (the regular expressions of RawConfigParser's section headers and options)
        char const* const sSectTemplate = R"(
        \[                                 # [
        (?P<header>.+)                     # very permissive!
        \]                                 # ]
        )";
        char const* const sOptTemplate = R"(
        (?P<option>                        # very permissive!
            (?:(?!{delim})\S)*             # non-delimiter non-whitespace
            (?:(?:(?!{delim})\s)+          # optionally more
             (?:(?!{delim})\S)+)*)         #   space-separated words
        \s*(?P<vi>{delim})\s*              # any number of space/tab,
                                           # followed by any of the
                                           # allowed delimiters,
                                           # followed by any space/tab
        (?P<value>.*)$                     # everything up to eol
        )";
        char const* const sOptNvTemplate = R"(
        (?P<option>                        # very permissive!
            (?:(?!{delim})\S)*             # non-delimiter non-whitespace
            (?:(?:(?!{delim})\s)+          # optionally more
             (?:(?!{delim})\S)+)*)         #   space-separated words
        \s*(?:                             # any number of space/tab,
        (?P<vi>{delim})\s*                 # optionally followed by
                                           # any of the allowed
                                           # delimiters, followed by any
                                           # space/tab
        (?P<value>.*))?$                   # everything up to eol
        )";

        // (the getters' parameters: section, option, *, raw=False, vars=None, fallback=_UNSET)
        std::vector<py_parameter> getter_parameters()
        {
            return { py_positional("section"), py_positional("option"), py_keyword("raw", boolean(false)), py_keyword("vars", none()), py_keyword("fallback", unset()) };
        }

        void make_parsers()
        {
            py const compile = M("re", "compile");
            py const verbose = M("re", "VERBOSE");
            py const booleanStates = py_dict();
            for (auto const& [name, value] : std::initializer_list<std::pair<char const*, bool>>{ { "1", true }, { "yes", true }, { "true", true }, { "on", true },
                { "0", false }, { "no", false }, { "false", false }, { "off", false } })
                booleanStates.set_item(S(name), boolean(value));
            namespace_type ns{
                // (the regular expressions of section headers and options)
                { "_SECT_TMPL", S(sSectTemplate) },
                { "_OPT_TMPL", S(sOptTemplate) },
                { "_OPT_NV_TMPL", S(sOptNvTemplate) },
                // (the interpolation if another is not specified)
                { "_DEFAULT_INTERPOLATION", cls("Interpolation")() },
                // (sections; options with the typical delimiters; options with optional values, with the typical delimiters; a line's leading
                // whitespace)
                { "SECTCRE", compile(S(sSectTemplate), verbose) },
                { "OPTCRE", compile(S(sOptTemplate).method("format", {}, { { "delim", S("=|:") } }), verbose) },
                { "OPTCRE_NV", compile(S(sOptNvTemplate).method("format", {}, { { "delim", S("=|:") } }), verbose) },
                { "NONSPACECRE", compile(S(R"(\S)")) },
                // (the possible boolean values)
                { "BOOLEAN_STATES", booleanStates } };
            ns.emplace_back("__init__", method("RawConfigParser", "__init__", { py_positional("defaults", none()), py_positional("dict_type", builtin("dict")),
                py_positional("allow_no_value", boolean(false)), py_keyword("delimiters", py_tuple({ S("="), S(":") })), py_keyword("comment_prefixes",
                py_tuple({ S("#"), S(";") })), py_keyword("inline_comment_prefixes", none()), py_keyword("strict", boolean(true)), py_keyword("empty_lines_in_values",
                boolean(true)), py_keyword("default_section", S("DEFAULT")), py_keyword("interpolation", unset()), py_keyword("converters", unset()),
                py_keyword("allow_unnamed_section", boolean(false)) }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py const delimiters = a[4];
                    py const defaultSection = a[9];
                    self.set_attr("_dict", a[2]);
                    self.set_attr("_sections", self.attr("_dict")());
                    self.set_attr("_defaults", self.attr("_dict")());
                    self.set_attr("_converters", cls("ConverterMapping")(self));
                    self.set_attr("_proxies", self.attr("_dict")());
                    self.attr("_proxies").set_item(defaultSection, cls("SectionProxy")(self, defaultSection));
                    self.set_attr("_delimiters", builtin("tuple")(delimiters));
                    if (delimiters == py_tuple({ S("="), S(":") }))
                        self.set_attr("_optcre", a[3].truthy() ? self.attr("OPTCRE_NV") : self.attr("OPTCRE"));
                    else
                    {
                        py const escaped = py_list();
                        for (py const& d : delimiters.items())
                            escaped.method("append", { M("re", "escape")(d) });
                        py const d = S("|").method("join", { escaped });
                        py const tmpl = a[3].truthy() ? self.attr("_OPT_NV_TMPL") : self.attr("_OPT_TMPL");
                        self.set_attr("_optcre", M("re", "compile")(tmpl.method("format", {}, { { "delim", d } }), M("re", "VERBOSE")));
                    }
                    py const commentPrefixes = a[5].truthy() ? a[5] : py_tuple();
                    py const inlineCommentPrefixes = a[6].truthy() ? a[6] : py_tuple();
                    self.set_attr("_prefixes", M("types", "SimpleNamespace").call({}, { { "full", builtin("tuple")(commentPrefixes) },
                        { "inline", builtin("tuple")(inlineCommentPrefixes) } }));
                    self.set_attr("_strict", a[7]);
                    self.set_attr("_allow_no_value", a[3]);
                    self.set_attr("_empty_lines_in_values", a[8]);
                    self.set_attr("default_section", defaultSection);
                    self.set_attr("_interpolation", a[10]);
                    if (self.attr("_interpolation").is(unset()))
                        self.set_attr("_interpolation", self.attr("_DEFAULT_INTERPOLATION"));
                    if (self.attr("_interpolation").is_none())
                        self.set_attr("_interpolation", cls("Interpolation")());
                    if (!isinstance(self.attr("_interpolation"), cls("Interpolation")))
                        py_raise("TypeError", "interpolation= must be None or an instance of Interpolation; got an object of type " +
                            builtin("str")(type_of(self.attr("_interpolation"))).str());
                    if (!a[11].is(unset()))
                        self.attr("_converters").method("update", { a[11] });
                    if (a[1].truthy())
                        self.method("_read_defaults", { a[1] });
                    self.set_attr("_allow_unnamed_section", a[12]);
                    return none();
                }));
            ns.emplace_back("defaults", method("RawConfigParser", "defaults", {}, [](std::vector<py> const& a) { return a[0].attr("_defaults"); }));
            ns.emplace_back("sections", method("RawConfigParser", "sections", {}, [](std::vector<py> const& a)
                {
                    // (_sections never has [DEFAULT])
                    return builtin("list")(a[0].attr("_sections").method("keys"));
                }));
            ns.emplace_back("add_section", method("RawConfigParser", "add_section", { py_positional("section") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    if (a[1] == self.attr("default_section"))
                        py_raise("ValueError", (S("Invalid section name: %r") % py_tuple({ a[1] })).str());
                    if (self.attr("_sections").contains(a[1]))
                        py_raise(cls("DuplicateSectionError")(a[1]));
                    self.attr("_sections").set_item(a[1], self.attr("_dict")());
                    self.attr("_proxies").set_item(a[1], cls("SectionProxy")(self, a[1]));
                    return none();
                }));
            ns.emplace_back("has_section", method("RawConfigParser", "has_section", { py_positional("section") }, [](std::vector<py> const& a)
                {
                    return boolean(a[0].attr("_sections").contains(a[1]));
                }));
            ns.emplace_back("options", method("RawConfigParser", "options", { py_positional("section") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py opts;
                    try_except([&]() { opts = self.attr("_sections")[a[1]].method("copy"); }, builtin("KeyError"), [&](py const&)
                    {
                        raise_from(cls("NoSectionError")(a[1]), none());
                    });
                    opts.method("update", { self.attr("_defaults") });
                    return builtin("list")(opts.method("keys"));
                }));
            ns.emplace_back("read", method("RawConfigParser", "read", { py_positional("filenames"), py_positional("encoding", none()) }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py filenames = a[1];
                    if (isinstance(filenames, py_tuple({ builtin("str"), builtin("bytes"), M("os", "PathLike") })))
                        filenames = py_list({ filenames });
                    py const encoding = M("io", "text_encoding")(a[2]);
                    py const readOk = py_list();
                    for_each(filenames, [&](py filename)
                    {
                        bool read = false;
                        try_except([&]()
                        {
                            with_(builtin("open").call({ filename }, { { "encoding", encoding } }), [&](py const& fp) { self.method("_read", { fp, filename }); });
                            read = true;
                        }, builtin("OSError"), [](py const&) {});
                        if (!read)
                            return;
                        if (isinstance(filename, M("os", "PathLike")))
                            filename = M("os", "fspath")(filename);
                        readOk.method("append", { filename });
                    });
                    return readOk;
                }));
            ns.emplace_back("read_file", method("RawConfigParser", "read_file", { py_positional("f"), py_positional("source", none()) }, [](std::vector<py> const& a)
                {
                    py source = a[2];
                    if (source.is_none())
                        try_except([&]() { source = a[1].attr("name"); }, builtin("AttributeError"), [&](py const&) { source = S("<?\?\?>"); });
                    a[0].method("_read", { a[1], source });
                    return none();
                }));
            ns.emplace_back("read_string", method("RawConfigParser", "read_string", { py_positional("string"), py_positional("source", S("<string>")) },
                [](std::vector<py> const& a)
                {
                    py const sfile = M("io", "StringIO")(a[1]);
                    a[0].method("read_file", { sfile, a[2] });
                    return none();
                }));
            ns.emplace_back("read_dict", method("RawConfigParser", "read_dict", { py_positional("dictionary"), py_positional("source", S("<dict>")) },
                [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py const elementsAdded = builtin("set")();
                    for (py const& item : a[1].method("items").items())
                    {
                        auto [section, keys] = unpack2(item);
                        section = builtin("str")(section);
                        try_except([&]() { self.method("add_section", { section }); }, py_tuple({ cls("DuplicateSectionError"), builtin("ValueError") }), [&](py const& e)
                        {
                            if (self.attr("_strict").truthy() && elementsAdded.contains(section))
                                py_reraise(e);
                        });
                        elementsAdded.method("add", { section });
                        for (py const& option : keys.method("items").items())
                        {
                            auto [key, value] = unpack2(option);
                            key = self.method("optionxform", { builtin("str")(key) });
                            if (!value.is_none())
                                value = builtin("str")(value);
                            if (self.attr("_strict").truthy() && elementsAdded.contains(py_tuple({ section, key })))
                                py_raise(cls("DuplicateOptionError")(section, key, a[2]));
                            elementsAdded.method("add", { py_tuple({ section, key }) });
                            self.method("set", { section, key, value });
                        }
                    }
                    return none();
                }));
            ns.emplace_back("get", method("RawConfigParser", "get", getter_parameters(), [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    auto const& section = a[1];
                    auto const& fallback = a[5];
                    py d;
                    bool noSection = false;
                    try_except([&]() { d = self.method("_unify_values", { section, a[4] }); }, cls("NoSectionError"), [&](py const& e)
                    {
                        if (fallback.is(unset()))
                            py_reraise(e);
                        noSection = true;
                    });
                    if (noSection)
                        return fallback;
                    py const option = self.method("optionxform", { a[2] });
                    py value;
                    bool noOption = false;
                    try_except([&]() { value = d[option]; }, builtin("KeyError"), [&](py const&)
                    {
                        if (fallback.is(unset()))
                            py_raise(cls("NoOptionError")(option, section));
                        noOption = true;
                    });
                    if (noOption)
                        return fallback;
                    if (a[3].truthy() || value.is_none())
                        return value;
                    return self.attr("_interpolation").method("before_get", { self, section, option, value, d });
                }));
            ns.emplace_back("_get", method("RawConfigParser", "_get", { py_positional("section"), py_positional("conv"), py_positional("option") }, [](std::vector<py> const& a)
                {
                    return a[2](call_kw(a[0].attr("get"), { a[1], a[3] }, a[4]));
                }, false, true));
            ns.emplace_back("_get_conv", method("RawConfigParser", "_get_conv", { py_positional("section"), py_positional("option"), py_positional("conv"),
                py_keyword("raw", boolean(false)), py_keyword("vars", none()), py_keyword("fallback", unset()) }, [](std::vector<py> const& a)
                {
                    py result;
                    bool missing = false;
                    try_except([&]() { result = call_kw(a[0].attr("_get"), { a[1], a[3], a[2] }, a[7], { { "raw", a[4] }, { "vars", a[5] } }); },
                        py_tuple({ cls("NoSectionError"), cls("NoOptionError") }), [&](py const& e)
                    {
                        if (a[6].is(unset()))
                            py_reraise(e);
                        missing = true;
                    });
                    return missing ? a[6] : result;
                }, false, true));
            // (getint, getfloat and getboolean: provided directly for backwards compatibility)
            auto const typed_getter = [](char const* aName, std::function<py(py const& self)> aConv)
            {
                return method("RawConfigParser", aName, getter_parameters(), [aConv](std::vector<py> const& a)
                {
                    return call_kw(a[0].attr("_get_conv"), { a[1], a[2], aConv(a[0]) }, a[6], { { "raw", a[3] }, { "vars", a[4] }, { "fallback", a[5] } });
                }, false, true);
            };
            ns.emplace_back("getint", typed_getter("getint", [](py const&) { return builtin("int"); }));
            ns.emplace_back("getfloat", typed_getter("getfloat", [](py const&) { return builtin("float"); }));
            ns.emplace_back("getboolean", typed_getter("getboolean", [](py const& self) { return self.attr("_convert_to_boolean"); }));
            ns.emplace_back("items", method("RawConfigParser", "items", { py_positional("section", unset()), py_positional("raw", boolean(false)), py_positional("vars", none()) },
                [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    auto const& section = a[1];
                    if (section.is(unset()))
                        return builtin("super")(cls("RawConfigParser"), self).method("items");
                    py const d = self.attr("_defaults").method("copy");
                    try_except([&]() { d.method("update", { self.attr("_sections")[section] }); }, builtin("KeyError"), [&](py const&)
                    {
                        if (section != self.attr("default_section"))
                            py_raise(cls("NoSectionError")(section));
                    });
                    py const origKeys = builtin("list")(d.method("keys"));
                    // (the entry's variables)
                    if (a[3].truthy())
                        for (py const& item : a[3].method("items").items())
                        {
                            auto const [key, value] = unpack2(item);
                            d.set_item(self.method("optionxform", { key }), value);
                        }
                    py const result = py_list();
                    for (py const& option : origKeys.items())
                    {
                        py const value = a[2].truthy() ? d[option] : self.attr("_interpolation").method("before_get", { self, section, option, d[option], d });
                        result.method("append", { py_tuple({ option, value }) });
                    }
                    return result;
                }));
            ns.emplace_back("popitem", method("RawConfigParser", "popitem", {}, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    for (py const& key : self.method("sections").items())
                    {
                        py const value = self[key];
                        self.method("__delitem__", { key });
                        return py_tuple({ key, value });
                    }
                    py_raise(builtin("KeyError"));
                }));
            ns.emplace_back("optionxform", method("RawConfigParser", "optionxform", { py_positional("optionstr") }, [](std::vector<py> const& a)
                {
                    return a[1].method("lower");
                }));
            ns.emplace_back("has_option", method("RawConfigParser", "has_option", { py_positional("section"), py_positional("option") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    if (!a[1].truthy() || a[1] == self.attr("default_section"))
                        return boolean(self.attr("_defaults").contains(self.method("optionxform", { a[2] })));
                    if (!self.attr("_sections").contains(a[1]))
                        return boolean(false);
                    py const option = self.method("optionxform", { a[2] });
                    return boolean(self.attr("_sections")[a[1]].contains(option) || self.attr("_defaults").contains(option));
                }));
            ns.emplace_back("set", method("RawConfigParser", "set", { py_positional("section"), py_positional("option"), py_positional("value", none()) },
                [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py value = a[3];
                    if (value.truthy())
                        value = self.attr("_interpolation").method("before_set", { self, a[1], a[2], value });
                    py sectdict;
                    if (!a[1].truthy() || a[1] == self.attr("default_section"))
                        sectdict = self.attr("_defaults");
                    else
                        try_except([&]() { sectdict = self.attr("_sections")[a[1]]; }, builtin("KeyError"), [&](py const&)
                        {
                            raise_from(cls("NoSectionError")(a[1]), none());
                        });
                    sectdict.set_item(self.method("optionxform", { a[2] }), value);
                    return none();
                }));
            ns.emplace_back("write", method("RawConfigParser", "write", { py_positional("fp"), py_positional("space_around_delimiters", boolean(true)) },
                [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py const d = a[2].truthy() ? S(" {} ").method("format", { self.attr("_delimiters")[py{ 0 }] }) : self.attr("_delimiters")[py{ 0 }];
                    if (self.attr("_defaults").truthy())
                        self.method("_write_section", { a[1], self.attr("default_section"), self.attr("_defaults").method("items"), d });
                    py const unnamed = state().unnamedSection;
                    if (self.attr("_sections").contains(unnamed))
                        self.method("_write_section", { a[1], unnamed, self.attr("_sections")[unnamed].method("items"), d }, { { "unnamed", boolean(true) } });
                    for (py const& section : self.attr("_sections").items())
                    {
                        if (section.is(unnamed))
                            continue;
                        self.method("_write_section", { a[1], section, self.attr("_sections")[section].method("items"), d });
                    }
                    return none();
                }));
            ns.emplace_back("_write_section", method("RawConfigParser", "_write_section", { py_positional("fp"), py_positional("section_name"), py_positional("section_items"),
                py_positional("delimiter"), py_positional("unnamed", boolean(false)) }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    auto const& fp = a[1];
                    if (!a[5].truthy())
                        fp.method("write", { S("[{}]\n").method("format", { a[2] }) });
                    for (py const& item : a[3].items())
                    {
                        auto [key, value] = unpack2(item);
                        value = self.attr("_interpolation").method("before_write", { self, a[2], key, value });
                        if (!value.is_none() || !self.attr("_allow_no_value").truthy())
                            // (all of the possible line endings, as "\n\t")
                            value = a[4] + builtin("str")(value).method("replace", { S("\r\n"), S("\n") }).method("replace", { S("\r"), S("\n") }).method("replace",
                                { S("\n"), S("\n\t") });
                        else
                            value = S("");
                        fp.method("write", { S("{}{}\n").method("format", { key, value }) });
                    }
                    fp.method("write", { S("\n") });
                    return none();
                }));
            ns.emplace_back("remove_option", method("RawConfigParser", "remove_option", { py_positional("section"), py_positional("option") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py sectdict;
                    if (!a[1].truthy() || a[1] == self.attr("default_section"))
                        sectdict = self.attr("_defaults");
                    else
                        try_except([&]() { sectdict = self.attr("_sections")[a[1]]; }, builtin("KeyError"), [&](py const&)
                        {
                            raise_from(cls("NoSectionError")(a[1]), none());
                        });
                    py const option = self.method("optionxform", { a[2] });
                    bool const existed = sectdict.contains(option);
                    if (existed)
                        sectdict.method("__delitem__", { option });
                    return boolean(existed);
                }));
            ns.emplace_back("remove_section", method("RawConfigParser", "remove_section", { py_positional("section") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    bool const existed = self.attr("_sections").contains(a[1]);
                    if (existed)
                    {
                        self.attr("_sections").method("__delitem__", { a[1] });
                        self.attr("_proxies").method("__delitem__", { a[1] });
                    }
                    return boolean(existed);
                }));
            ns.emplace_back("__getitem__", method("RawConfigParser", "__getitem__", { py_positional("key") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    if (a[1] != self.attr("default_section") && !self.method("has_section", { a[1] }).truthy())
                        py_raise(builtin("KeyError")(a[1]));
                    return self.attr("_proxies")[a[1]];
                }));
            ns.emplace_back("__setitem__", method("RawConfigParser", "__setitem__", { py_positional("key"), py_positional("value") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    // (as the mapping protocol has it: the section's existing values are overwritten (not atomically if read_dict fails, as no
                    // update method is))
                    if (self.contains(a[1]) && self[a[1]].is(a[2]))
                        return none();
                    if (a[1] == self.attr("default_section"))
                        self.attr("_defaults").method("clear");
                    else if (self.attr("_sections").contains(a[1]))
                        self.attr("_sections")[a[1]].method("clear");
                    py const dictionary = py_dict();
                    dictionary.set_item(a[1], a[2]);
                    self.method("read_dict", { dictionary });
                    return none();
                }));
            ns.emplace_back("__delitem__", method("RawConfigParser", "__delitem__", { py_positional("key") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    if (a[1] == self.attr("default_section"))
                        py_raise("ValueError", "Cannot remove the default section.");
                    if (!self.method("has_section", { a[1] }).truthy())
                        py_raise(builtin("KeyError")(a[1]));
                    self.method("remove_section", { a[1] });
                    return none();
                }));
            ns.emplace_back("__contains__", method("RawConfigParser", "__contains__", { py_positional("key") }, [](std::vector<py> const& a)
                {
                    return boolean(a[1] == a[0].attr("default_section") || a[0].method("has_section", { a[1] }).truthy());
                }));
            ns.emplace_back("__len__", method("RawConfigParser", "__len__", {}, [](std::vector<py> const& a)
                {
                    return py{ a[0].attr("_sections").size() + 1 };    // (the default section)
                }));
            ns.emplace_back("__iter__", method("RawConfigParser", "__iter__", {}, [](std::vector<py> const& a)
                {
                    return M("itertools", "chain")(py_tuple({ a[0].attr("default_section") }), a[0].attr("_sections").method("keys"));
                }));
            ns.emplace_back("_read", method("RawConfigParser", "_read", { py_positional("fp"), py_positional("fpname") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    try_finally([&]() { cls("ParsingError").attr("_raise_all")(self.method("_read_inner", { a[1], a[2] })); },
                        [&]() { self.method("_join_multiline_values"); });
                    return none();
                }));
            ns.emplace_back("_read_inner", method("RawConfigParser", "_read_inner", { py_positional("fp"), py_positional("fpname") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py const st = cls("_ReadState")();
                    py const Line = M("functools", "partial").call({ cls("_Line") }, { { "prefixes", self.attr("_prefixes") } });
                    for_each(builtin("enumerate").call({ builtin("map")(Line, a[1]) }, { { "start", py{ 1 } } }), [&](py const& item)
                    {
                        auto const [lineno, line] = unpack2(item);
                        st.set_attr("lineno", lineno);
                        if (!line.attr("clean").truthy())
                        {
                            if (self.attr("_empty_lines_in_values").truthy())
                            {
                                // (an empty line is the value's, but only if there was no comment on the line (the newlines are added when the
                                // lines are joined))
                                if (!line.attr("has_comments").truthy() && !st.attr("cursect").is_none() && st.attr("optname").truthy() &&
                                    !st.attr("cursect")[st.attr("optname")].is_none())
                                    st.attr("cursect")[st.attr("optname")].method("append", { S("") });
                            }
                            else
                                st.set_attr("indent_level", M("sys", "maxsize"));   // (an empty line ends the value)
                            return;
                        }
                        py const firstNonspace = self.attr("NONSPACECRE").method("search", { line });
                        st.set_attr("cur_indent_level", firstNonspace.truthy() ? firstNonspace.method("start") : py{ 0 });
                        if (self.method("_handle_continuation_line", { st, line, a[2] }).truthy())
                            return;
                        self.method("_handle_rest", { st, line, a[2] });
                    });
                    return st.attr("errors");
                }));
            ns.emplace_back("_handle_continuation_line", method("RawConfigParser", "_handle_continuation_line", { py_positional("st"), py_positional("line"),
                py_positional("fpname") }, [](std::vector<py> const& a)
                {
                    auto const& st = a[1];
                    // (a continuation line: (st.cursect is not None and st.optname and st.cur_indent_level > st.indent_level))
                    py isContinue = boolean(!st.attr("cursect").is_none());
                    if (isContinue.truthy())
                        isContinue = st.attr("optname");
                    if (isContinue.truthy())
                        isContinue = boolean(st.attr("cur_indent_level") > st.attr("indent_level"));
                    if (isContinue.truthy())
                    {
                        if (st.attr("cursect")[st.attr("optname")].is_none())
                            py_raise(cls("MultilineContinuationError")(a[3], st.attr("lineno"), a[2]));
                        st.attr("cursect")[st.attr("optname")].method("append", { a[2].attr("clean") });
                    }
                    return isContinue;
                }));
            ns.emplace_back("_handle_rest", method("RawConfigParser", "_handle_rest", { py_positional("st"), py_positional("line"), py_positional("fpname") },
                [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    auto const& st = a[1];
                    // (a section header or an option header)
                    if (self.attr("_allow_unnamed_section").truthy() && st.attr("cursect").is_none())
                        self.method("_handle_header", { st, state().unnamedSection, a[3] });
                    st.set_attr("indent_level", st.attr("cur_indent_level"));
                    // (a section header?)
                    py const mo = self.attr("SECTCRE").method("match", { a[2].attr("clean") });
                    if (!mo.truthy() && st.attr("cursect").is_none())
                        py_raise(cls("MissingSectionHeaderError")(a[3], st.attr("lineno"), a[2]));
                    if (mo.truthy())
                        self.method("_handle_header", { st, mo.method("group", { S("header") }), a[3] });
                    else
                        self.method("_handle_option", { st, a[2], a[3] });
                    return none();
                }));
            ns.emplace_back("_handle_header", method("RawConfigParser", "_handle_header", { py_positional("st"), py_positional("sectname"), py_positional("fpname") },
                [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    auto const& st = a[1];
                    st.set_attr("sectname", a[2]);
                    py const sectname = st.attr("sectname");
                    if (self.attr("_sections").contains(sectname))
                    {
                        if (self.attr("_strict").truthy() && st.attr("elements_added").contains(sectname))
                            py_raise(cls("DuplicateSectionError")(sectname, a[3], st.attr("lineno")));
                        st.set_attr("cursect", self.attr("_sections")[sectname]);
                        st.attr("elements_added").method("add", { sectname });
                    }
                    else if (sectname == self.attr("default_section"))
                        st.set_attr("cursect", self.attr("_defaults"));
                    else
                    {
                        st.set_attr("cursect", self.attr("_dict")());
                        self.attr("_sections").set_item(sectname, st.attr("cursect"));
                        self.attr("_proxies").set_item(sectname, cls("SectionProxy")(self, sectname));
                        st.attr("elements_added").method("add", { sectname });
                    }
                    // (so that a section cannot start with a continuation line)
                    st.set_attr("optname", none());
                    return none();
                }));
            ns.emplace_back("_handle_option", method("RawConfigParser", "_handle_option", { py_positional("st"), py_positional("line"), py_positional("fpname") },
                [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    auto const& st = a[1];
                    // (an option line?)
                    st.set_attr("indent_level", st.attr("cur_indent_level"));
                    py const mo = self.attr("_optcre").method("match", { a[2].attr("clean") });
                    if (!mo.truthy())
                    {
                        // (a non-fatal parsing error: the exception is raised at the end of the file (with all of the bogus lines))
                        st.attr("errors").method("append", { cls("ParsingError")(a[3], st.attr("lineno"), a[2]) });
                        return none();
                    }
                    auto const groups = mo.method("group", { S("option"), S("vi"), S("value") }).items();
                    st.set_attr("optname", groups.at(0));
                    py optval = groups.at(2);
                    if (!st.attr("optname").truthy())
                        st.attr("errors").method("append", { cls("ParsingError")(a[3], st.attr("lineno"), a[2]) });
                    st.set_attr("optname", self.method("optionxform", { st.attr("optname").method("rstrip") }));
                    if (self.attr("_strict").truthy() && st.attr("elements_added").contains(py_tuple({ st.attr("sectname"), st.attr("optname") })))
                        py_raise(cls("DuplicateOptionError")(st.attr("sectname"), st.attr("optname"), a[3], st.attr("lineno")));
                    st.attr("elements_added").method("add", { py_tuple({ st.attr("sectname"), st.attr("optname") }) });
                    // (OPTCRE cannot match if it would make optval None)
                    if (!optval.is_none())
                    {
                        optval = optval.method("strip");
                        st.attr("cursect").set_item(st.attr("optname"), py_list({ optval }));
                    }
                    else
                        st.attr("cursect").set_item(st.attr("optname"), none());     // (an option without a value)
                    return none();
                }));
            ns.emplace_back("_join_multiline_values", method("RawConfigParser", "_join_multiline_values", {}, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    std::vector<std::pair<py, py>> allSections{ { self.attr("default_section"), self.attr("_defaults") } };
                    for (py const& item : self.attr("_sections").method("items").items())
                    {
                        auto const [section, options] = unpack2(item);
                        allSections.emplace_back(section, options);
                    }
                    for (auto const& [section, options] : allSections)
                        for (py const& item : options.method("items").items())
                        {
                            auto [name, val] = unpack2(item);
                            if (isinstance(val, builtin("list")))
                                val = S("\n").method("join", { val }).method("rstrip");
                            options.set_item(name, self.attr("_interpolation").method("before_read", { self, section, name, val }));
                        }
                    return none();
                }));
            ns.emplace_back("_read_defaults", method("RawConfigParser", "_read_defaults", { py_positional("defaults") }, [](std::vector<py> const& a)
                {
                    // (its values can be non-strings)
                    for (py const& item : a[1].method("items").items())
                    {
                        auto const [key, value] = unpack2(item);
                        a[0].attr("_defaults").set_item(a[0].method("optionxform", { key }), value);
                    }
                    return none();
                }));
            ns.emplace_back("_unify_values", method("RawConfigParser", "_unify_values", { py_positional("section"), py_positional("vars") }, [](std::vector<py> const& a)
                {
                    auto const& self = a[0];
                    py sectiondict = py_dict();
                    try_except([&]() { sectiondict = self.attr("_sections")[a[1]]; }, builtin("KeyError"), [&](py const&)
                    {
                        if (a[1] != self.attr("default_section"))
                            raise_from(cls("NoSectionError")(a[1]), none());
                    });
                    // (the entry's variables)
                    py const vardict = py_dict();
                    if (a[2].truthy())
                        for (py const& item : a[2].method("items").items())
                        {
                            auto [key, value] = unpack2(item);
                            if (!value.is_none())
                                value = builtin("str")(value);
                            vardict.set_item(self.method("optionxform", { key }), value);
                        }
                    return M("collections", "ChainMap")(vardict, sectiondict, self.attr("_defaults"));
                }));
            ns.emplace_back("_convert_to_boolean", method("RawConfigParser", "_convert_to_boolean", { py_positional("value") }, [](std::vector<py> const& a)
                {
                    if (!a[0].attr("BOOLEAN_STATES").contains(a[1].method("lower")))
                        py_raise("ValueError", (S("Not a boolean: %s") % py_tuple({ a[1] })).str());
                    return a[0].attr("BOOLEAN_STATES")[a[1].method("lower")];
                }));
            ns.emplace_back("_validate_value_types", method("RawConfigParser", "_validate_value_types", { py_keyword("section", S("")), py_keyword("option", S("")),
                py_keyword("value", S("")) }, [](std::vector<py> const& a)
                {
                    if (!isinstance(a[1], builtin("str")))
                        py_raise("TypeError", "section names must be strings");
                    if (!isinstance(a[2], builtin("str")))
                        py_raise("TypeError", "option keys must be strings");
                    if (!a[0].attr("_allow_no_value").truthy() || a[3].truthy())
                        if (!isinstance(a[3], builtin("str")))
                            py_raise("TypeError", "option values must be strings");
                    return none();
                }));
            ns.emplace_back("converters", builtin("property")(method("RawConfigParser", "converters", {}, [](std::vector<py> const& a) { return a[0].attr("_converters"); })));
            make_parser_class("RawConfigParser", { M("collections.abc", "MutableMapping") }, ns);

            make_parser_class("ConfigParser", { cls("RawConfigParser") }, {
                { "_DEFAULT_INTERPOLATION", cls("BasicInterpolation")() },
                { "set", method("ConfigParser", "set", { py_positional("section"), py_positional("option"), py_positional("value", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_validate_value_types", {}, { { "option", a[2] }, { "value", a[3] } });
                        builtin("super")(cls("ConfigParser"), a[0]).method("set", { a[1], a[2], a[3] });
                        return none();
                    }) },
                { "add_section", method("ConfigParser", "add_section", { py_positional("section") }, [](std::vector<py> const& a)
                    {
                        a[0].method("_validate_value_types", {}, { { "section", a[1] } });
                        builtin("super")(cls("ConfigParser"), a[0]).method("add_section", { a[1] });
                        return none();
                    }) },
                { "_read_defaults", method("ConfigParser", "_read_defaults", { py_positional("defaults") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py holdInterpolation;
                        try_finally([&]()
                        {
                            holdInterpolation = self.attr("_interpolation");
                            self.set_attr("_interpolation", cls("Interpolation")());
                            py const dictionary = py_dict();
                            dictionary.set_item(self.attr("default_section"), a[1]);
                            self.method("read_dict", { dictionary });
                        }, [&]()
                        {
                            if (holdInterpolation.empty())
                                py_raise("UnboundLocalError", "cannot access local variable 'hold_interpolation' where it is not associated with a value");
                            self.set_attr("_interpolation", holdInterpolation);
                        });
                        return none();
                    }) } });
        }

        void make_mappings()
        {
            py const mutableMapping = M("collections.abc", "MutableMapping");
            make_parser_class("SectionProxy", { mutableMapping }, {
                { "__init__", method("SectionProxy", "__init__", { py_positional("parser"), py_positional("name") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("_parser", a[1]);
                        self.set_attr("_name", a[2]);
                        for (py const& conv : a[1].attr("converters").items())
                        {
                            py const key = S("get") + conv;
                            py const getter = M("functools", "partial").call({ self.attr("get") }, { { "_impl", builtin("getattr")(a[1], key) } });
                            builtin("setattr")(self, key, getter);
                        }
                        return none();
                    }) },
                { "__repr__", method("SectionProxy", "__repr__", {}, [](std::vector<py> const& a) { return S("<Section: {}>").method("format", { a[0].attr("_name") }); }) },
                { "__getitem__", method("SectionProxy", "__getitem__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        py const parser = a[0].attr("_parser");
                        if (!parser.method("has_option", { a[0].attr("_name"), a[1] }).truthy())
                            py_raise(builtin("KeyError")(a[1]));
                        return parser.method("get", { a[0].attr("_name"), a[1] });
                    }) },
                { "__setitem__", method("SectionProxy", "__setitem__", { py_positional("key"), py_positional("value") }, [](std::vector<py> const& a)
                    {
                        py const parser = a[0].attr("_parser");
                        parser.method("_validate_value_types", {}, { { "option", a[1] }, { "value", a[2] } });
                        return parser.method("set", { a[0].attr("_name"), a[1], a[2] });
                    }) },
                { "__delitem__", method("SectionProxy", "__delitem__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        py const parser = a[0].attr("_parser");
                        if (!(parser.method("has_option", { a[0].attr("_name"), a[1] }).truthy() && parser.method("remove_option", { a[0].attr("_name"), a[1] }).truthy()))
                            py_raise(builtin("KeyError")(a[1]));
                        return none();
                    }) },
                { "__contains__", method("SectionProxy", "__contains__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("_parser").method("has_option", { a[0].attr("_name"), a[1] });
                    }) },
                { "__len__", method("SectionProxy", "__len__", {}, [](std::vector<py> const& a) { return py{ a[0].method("_options").size() }; }) },
                { "__iter__", method("SectionProxy", "__iter__", {}, [](std::vector<py> const& a) { return a[0].method("_options").method("__iter__"); }) },
                { "_options", method("SectionProxy", "_options", {}, [](std::vector<py> const& a)
                    {
                        py const parser = a[0].attr("_parser");
                        if (a[0].attr("_name") != parser.attr("default_section"))
                            return parser.method("options", { a[0].attr("_name") });
                        return parser.method("defaults");
                    }) },
                // (the proxy's parser and its section's name: read-only)
                { "parser", builtin("property")(method("SectionProxy", "parser", {}, [](std::vector<py> const& a) { return a[0].attr("_parser"); })) },
                { "name", builtin("property")(method("SectionProxy", "name", {}, [](std::vector<py> const& a) { return a[0].attr("_name"); })) },
                { "get", method("SectionProxy", "get", { py_positional("option"), py_positional("fallback", none()), py_keyword("raw", boolean(false)),
                    py_keyword("vars", none()), py_keyword("_impl", none()) }, [](std::vector<py> const& a)
                    {
                        // (_impl: a getter of the parser that converts the value's type, if it is given)
                        py impl = a[5];
                        if (!impl.truthy())
                            impl = a[0].attr("_parser").attr("get");
                        return call_kw(impl, { a[0].attr("_name"), a[1] }, a[6], { { "raw", a[3] }, { "vars", a[4] }, { "fallback", a[2] } });
                    }, false, true) } });

            make_parser_class("ConverterMapping", { mutableMapping }, {
                { "GETTERCRE", M("re", "compile")(S(R"(^get(?P<name>.+)$)")) },
                { "__init__", method("ConverterMapping", "__init__", { py_positional("parser") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("_parser", a[1]);
                        self.set_attr("_data", py_dict());
                        for (py const& getter : builtin("dir")(a[1]).items())
                        {
                            py const m = self.attr("GETTERCRE").method("match", { getter });
                            if (!m.truthy() || !callable(builtin("getattr")(a[1], getter)))
                                continue;
                            self.attr("_data").set_item(m.method("group", { S("name") }), none());   // (see the class's docstring)
                        }
                        return none();
                    }) },
                { "__getitem__", method("ConverterMapping", "__getitem__", { py_positional("key") }, [](std::vector<py> const& a) { return a[0].attr("_data")[a[1]]; }) },
                { "__setitem__", method("ConverterMapping", "__setitem__", { py_positional("key"), py_positional("value") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py k;
                        try_except([&]() { k = S("get") + a[1]; }, builtin("TypeError"), [&](py const&)
                        {
                            py_raise("ValueError", S("Incompatible key: {} (type: {})").method("format", { a[1], type_of(a[1]) }).str());
                        });
                        if (k == S("get"))
                            py_raise("ValueError", "Incompatible key: cannot use \"\" as a name");
                        self.attr("_data").set_item(a[1], a[2]);
                        py const func = M("functools", "partial").call({ self.attr("_parser").attr("_get_conv") }, { { "conv", a[2] } });
                        func.set_attr("converter", a[2]);
                        builtin("setattr")(self.attr("_parser"), k, func);
                        for (py const& proxy : self.attr("_parser").method("values").items())
                        {
                            py const getter = M("functools", "partial").call({ proxy.attr("get") }, { { "_impl", func } });
                            builtin("setattr")(proxy, k, getter);
                        }
                        return none();
                    }) },
                { "__delitem__", method("ConverterMapping", "__delitem__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py k;
                        try_except([&]() { k = S("get") + (a[1].truthy() ? a[1] : none()); }, builtin("TypeError"), [&](py const&)
                        {
                            py_raise(builtin("KeyError")(a[1]));
                        });
                        self.attr("_data").method("__delitem__", { a[1] });
                        std::vector<py> instances{ self.attr("_parser") };
                        for (py const& proxy : self.attr("_parser").method("values").items())
                            instances.push_back(proxy);
                        for (auto const& inst : instances)
                            // (the entry was in _data: an absent attribute is not an error)
                            try_except([&]() { builtin("delattr")(inst, k); }, builtin("AttributeError"), [](py const&) {});
                        return none();
                    }) },
                { "__iter__", method("ConverterMapping", "__iter__", {}, [](std::vector<py> const& a) { return builtin("iter")(a[0].attr("_data")); }) },
                { "__len__", method("ConverterMapping", "__len__", {}, [](std::vector<py> const& a) { return py{ a[0].attr("_data").size() }; }) } });
        }

        bool configparser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (!s.made)
                {
                    s.made = true;
                    // (_UNSET: a getter's default when an option is not found (an exception), so that None can be a fallback)
                    s.unset = builtin("object")();
                    make_exceptions();
                    make_line_classes();
                    s.unnamedSection = cls("_UnnamedSection")();
                    make_interpolations();
                    make_mappings();
                    make_parsers();
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S(D("")));
                std::vector<py> all;
                for (auto n : { "NoSectionError", "DuplicateOptionError", "DuplicateSectionError", "NoOptionError", "InterpolationError", "InterpolationDepthError",
                    "InterpolationMissingOptionError", "InterpolationSyntaxError", "ParsingError", "MissingSectionHeaderError", "MultilineContinuationError",
                    "ConfigParser", "RawConfigParser", "Interpolation", "BasicInterpolation", "ExtendedInterpolation", "SectionProxy", "ConverterMapping",
                    "DEFAULTSECT", "MAX_INTERPOLATION_DEPTH", "UNNAMED_SECTION" })
                    all.emplace_back(n);
                add("__all__", py_tuple(all));
                add("Iterable", M("collections.abc", "Iterable"));
                add("MutableMapping", M("collections.abc", "MutableMapping"));
                add("_ChainMap", M("collections", "ChainMap"));
                for (auto module : { "contextlib", "functools", "io", "itertools", "os", "re", "sys", "types" })
                    add(module, py{ fn_import(module) });
                add("_default_dict", builtin("dict"));
                add("DEFAULTSECT", S("DEFAULT"));
                add("MAX_INTERPOLATION_DEPTH", py{ 10 });
                add("UNNAMED_SECTION", s.unnamedSection);
                add("_UNSET", s.unset);
                for (auto const& [name, value] : s.classes)
                    add(name.c_str(), value);
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sConfigparser = register_module("configparser", configparser_module);
    }
}
