/*
  argparse.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Portions of this file are derived from CPython 3.13 (Lib/argparse.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module argparse (CPython 3.13's argparse.py): ArgumentParser, its actions (Action and the store, append, count, help, version,
// subparsers and extend actions, BooleanOptionalAction), the help formatters, Namespace, FileType, ArgumentError and ArgumentTypeError;
// its classes are made (by type) of closures, so a program's subclasses override their methods (their methods call each other through
// self); its messages are not translated (gettext's _ and ngettext are the identity)

#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "argparse";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;     ///< (the module's classes, by name)
            py SUPPRESS;
            py OPTIONAL;
            py ZERO_OR_MORE;
            py ONE_OR_MORE;
            py PARSER;
            py REMAINDER;
            py deprecatedDefault;   ///< (_deprecated_default)
            py getActionName;       ///< (_get_action_name)
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
        // (gettext's _ and ngettext: untranslated)
        py _(char const* aText) { return py{ aText }; }
        py ngettext(char const* aSingular, char const* aPlural, py const& aCount) { return py{ aCount == py{ 1 } ? aSingular : aPlural }; }
        bool is_str(py const& aValue) { return isinstance(aValue, builtin("str")); }
        bool is_suppress(py const& aValue) { return aValue.is(state().SUPPRESS); }
        bool callable(py const& aValue) { return builtin("callable")(aValue).truthy(); }

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
        py get_attr(py const& aObject, py const& aName) { return builtin("getattr")(aObject, aName); }
        py get_attr(py const& aObject, py const& aName, py const& aDefault) { return builtin("getattr")(aObject, aName, aDefault); }
        void set_attr(py const& aObject, py const& aName, py const& aValue) { builtin("setattr")(aObject, aName, aValue); }

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

        py dict_of(std::vector<std::pair<char const*, py>> const& aItems)
        {
            py const d = py_dict();
            for (auto const& [k, v] : aItems)
                d.set_item(py{ k }, v);
            return d;
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
        // (for x in aIterable: aBody(x) (lazily: a generator's items as it makes them))
        void for_each(py const& aIterable, std::function<void(py const&)> const& aBody)
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
        [[noreturn]] void raise_argument_error(py const& aArgument, py const& aMessage) { py_raise(cls("ArgumentError")(aArgument, aMessage)); }

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
        py make_argparse_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, char const* aDoc = nullptr,
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

        // --- utility functions and classes

        py copy_items(py const& aItems)
        {
            if (aItems.is_none())
                return py_list();
            if (type_of(aItems).is(builtin("list")))
                return slice(aItems, std::nullopt);
            return import_attribute("copy", "copy")(aItems);
        }

        void make_attribute_holder()
        {
            make_argparse_class("_AttributeHolder", {}, {
                { "__repr__", method("_AttributeHolder", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const typeName = type_of(self).attr("__name__");
                        py const argStrings = py_list();
                        py const starArgs = py_dict();
                        for (auto const& arg : self.method("_get_args").items())
                            argStrings.method("append", { builtin("repr")(arg) });
                        for (auto const& item : self.method("_get_kwargs").items())
                        {
                            auto const pair = item.items();
                            py const& name = pair.at(0);
                            py const& value = pair.at(1);
                            if (name.method("isidentifier").truthy())
                                argStrings.method("append", { S("%s=%r") % py_tuple({ name, value }) });
                            else
                                starArgs.set_item(name, value);
                        }
                        if (starArgs.truthy())
                            argStrings.method("append", { S("**%s") % py_tuple({ builtin("repr")(starArgs) }) });
                        return S("%s(%s)") % py_tuple({ typeName, join(", ", argStrings) });
                    }) },
                { "_get_kwargs", method("_AttributeHolder", "_get_kwargs", {}, [](std::vector<py> const& a)
                    {
                        return list_of(a[0].attr("__dict__").method("items"));
                    }) },
                { "_get_args", method("_AttributeHolder", "_get_args", {}, [](std::vector<py> const&) { return py_list(); }) } },
                "Abstract base class that provides __repr__.\n\n    The __repr__ method returns a string in the format::\n        ClassName(attr=name, attr=name, ...)\n"
                "    The attributes are determined either by a class-level attribute,\n    '_kwarg_names', or by inspecting the instance __dict__.\n    ");
        }

        // --- formatting help

        py format_usage_impl(py const& self, py aUsage, py const& aActions, py const& aGroups, py aPrefix)
        {
            if (aPrefix.is_none())
                aPrefix = _("usage: ");
            py const progDict = dict_of({ { "prog", self.attr("_prog") } });
            if (!aUsage.is_none())
                aUsage = aUsage % progDict;
            else if (aUsage.is_none() && !aActions.truthy())
                aUsage = S("%(prog)s") % progDict;
            else if (aUsage.is_none())
            {
                py const prog = S("%(prog)s") % progDict;
                py const optionals = py_list();
                py const positionals = py_list();
                for (auto const& action : aActions.items())
                {
                    if (action.attr("option_strings").truthy())
                        optionals.method("append", { action });
                    else
                        positionals.method("append", { action });
                }
                py const actionUsage = self.method("_format_actions_usage", { optionals + positionals, aGroups });
                {
                    py const parts = py_list();
                    for (auto const& s : { prog, actionUsage })
                        if (s.truthy())
                            parts.method("append", { s });
                    aUsage = join(" ", parts);
                }
                long long const textWidth = (self.attr("_width") - self.attr("_current_indent")).as_index();
                if (aPrefix.size() + aUsage.size() > textWidth)
                {
                    py const allActions = optionals + positionals;
                    py const result = self.method("_get_actions_usage_parts_with_split", { allActions, aGroups, py{ optionals.size() } });
                    py parts = result[py{ 0 }];
                    py const posStart = result[py{ 1 }];
                    py const optParts = parts[py{ make_slice(none().get(), posStart.get(), nullptr) }];
                    py const posParts = parts[py{ make_slice(posStart.get(), none().get(), nullptr) }];
                    // (helper for wrapping lines)
                    auto const getLines = [&](py const& aParts, py const& aIndent, py const& aLinePrefix = py{}) -> py
                    {
                        py const lines = py_list();
                        py line = py_list();
                        long long const indentLength = aIndent.size();
                        long long lineLen = !aLinePrefix.empty() && !aLinePrefix.is_none() ? aLinePrefix.size() - 1 : indentLength - 1;
                        for (auto const& part : aParts.items())
                        {
                            if (lineLen + 1 + part.size() > textWidth && line.truthy())
                            {
                                lines.method("append", { aIndent + join(" ", line) });
                                line = py_list();
                                lineLen = indentLength - 1;
                            }
                            line.method("append", { part });
                            lineLen += part.size() + 1;
                        }
                        if (line.truthy())
                            lines.method("append", { aIndent + join(" ", line) });
                        if (!aLinePrefix.empty() && !aLinePrefix.is_none())
                            lines.set_item(py{ 0 }, slice(lines[py{ 0 }], indentLength));
                        return lines;
                    };
                    py lines;
                    if (static_cast<double>(aPrefix.size() + prog.size()) <= 0.75 * static_cast<double>(textWidth))
                    {
                        py const indent = S(" ") * py{ aPrefix.size() + prog.size() + 1 };
                        if (optParts.truthy())
                        {
                            lines = getLines(py_list({ prog }) + optParts, indent, aPrefix);
                            lines.method("extend", { getLines(posParts, indent) });
                        }
                        else if (posParts.truthy())
                            lines = getLines(py_list({ prog }) + posParts, indent, aPrefix);
                        else
                            lines = py_list({ prog });
                    }
                    else
                    {
                        py const indent = S(" ") * py{ aPrefix.size() };
                        parts = optParts + posParts;
                        lines = getLines(parts, indent);
                        if (lines.size() > 1)
                        {
                            lines = py_list();
                            lines.method("extend", { getLines(optParts, indent) });
                            lines.method("extend", { getLines(posParts, indent) });
                        }
                        lines = py_list({ prog }) + lines;
                    }
                    aUsage = join("\n", lines);
                }
            }
            return S("%s%s\n\n") % py_tuple({ aPrefix, aUsage });
        }

        py actions_usage_parts_with_split(py const& self, py const& aActions, py const& aGroups, py const& aOptCount)
        {
            auto const& s = state();
            py const groupActions = builtin("set")();
            py const inserts = py_dict();
            for (auto const& group : aGroups.items())
            {
                py const groupActionsOfGroup = group.attr("_group_actions");
                if (!groupActionsOfGroup.truthy())
                    py_raise("ValueError", "empty group " + group.str());
                bool allSuppressed = true;
                for (auto const& action : groupActionsOfGroup.items())
                    if (!action.attr("help").is(s.SUPPRESS))
                    {
                        allSuppressed = false;
                        break;
                    }
                if (allSuppressed)
                    continue;
                py start;
                try
                {
                    start = aActions.method("index", { groupActionsOfGroup[py{ 0 }] });
                }
                catch (raised const&)
                {
                    if (!caught({ "ValueError" }))
                        throw;
                    continue;
                }
                py const end = start + py{ groupActionsOfGroup.size() };
                if (aActions[py{ make_slice(start.get(), end.get(), nullptr) }] == groupActionsOfGroup)
                {
                    groupActions.method("update", { groupActionsOfGroup });
                    inserts.set_item(py_tuple({ start, end }), group);
                }
            }
            // (collect all actions format strings)
            py const parts = py_list();
            for (auto const& action : aActions.items())
            {
                py part;
                if (action.attr("help").is(s.SUPPRESS))
                    part = none();
                else if (!action.attr("option_strings").truthy())
                {
                    py const defaultMetavar = self.method("_get_default_metavar_for_positional", { action });
                    part = self.method("_format_args", { action, defaultMetavar });
                    if (groupActions.contains(action))
                        if (part[py{ 0 }] == S("[") && part[py{ -1 }] == S("]"))
                            part = slice(part, 1, -1);
                }
                else
                {
                    py const optionString = action.attr("option_strings")[py{ 0 }];
                    if (action.attr("nargs") == py{ 0 })
                        part = action.method("format_usage");
                    else
                    {
                        py const defaultMetavar = self.method("_get_default_metavar_for_optional", { action });
                        py const argsString = self.method("_format_args", { action, defaultMetavar });
                        part = S("%s %s") % py_tuple({ optionString, argsString });
                    }
                    if (!action.attr("required").truthy() && !groupActions.contains(action))
                        part = S("[%s]") % py_tuple({ part });
                }
                parts.method("append", { part });
            }
            // (group mutually exclusive actions)
            py const insertedSeparatorsIndices = builtin("set")();
            for (auto const& key : builtin("sorted").call({ inserts }, { { "reverse", boolean(true) } }).items())
            {
                long long const start = key[py{ 0 }].as_index();
                long long const end = key[py{ 1 }].as_index();
                py const group = inserts[key];
                py const groupParts = py_list();
                for (auto const& item : slice(parts, start, end).items())
                    if (!item.is_none())
                        groupParts.method("append", { item });
                long long const groupSize = groupParts.size();
                py open, close;
                if (group.attr("required").truthy())
                {
                    open = S(groupSize > 1 ? "(" : "");
                    close = S(groupSize > 1 ? ")" : "");
                }
                else
                {
                    open = S("[");
                    close = S("]");
                }
                groupParts.set_item(py{ 0 }, open + groupParts[py{ 0 }]);
                groupParts.set_item(py{ -1 }, groupParts[py{ -1 }] + close);
                long long i = start;
                for (auto const& part : slice(groupParts, std::nullopt, -1).items())
                {
                    if (!insertedSeparatorsIndices.contains(py{ i }))
                    {
                        parts.set_item(py{ i }, part + S(" |"));
                        insertedSeparatorsIndices.method("add", { py{ i } });
                    }
                    ++i;
                }
                parts.set_item(py{ start + groupSize - 1 }, groupParts[py{ -1 }]);
                for (long long j = start + groupSize; j < end; ++j)
                    parts.set_item(py{ j }, none());
            }
            py posStart = none();
            if (!aOptCount.is_none())
            {
                long long n = 0;
                for (auto const& p : slice(parts, std::nullopt, aOptCount.as_index()).items())
                    if (!p.is_none())
                        ++n;
                posStart = py{ n };
            }
            py const result = py_list();
            for (auto const& item : parts.items())
                if (!item.is_none())
                    result.method("append", { item });
            return py_tuple({ result, posStart });
        }

        py format_action_impl(py const& self, py const& aAction)
        {
            long long const helpPosition = std::min((self.attr("_action_max_length") + py{ 2 }).as_index(), self.attr("_max_help_position").as_index());
            long long const helpWidth = std::max(self.attr("_width").as_index() - helpPosition, 11LL);
            long long const actionWidth = helpPosition - self.attr("_current_indent").as_index() - 2;
            py actionHeader = self.method("_format_action_invocation", { aAction });
            long long indentFirst = 0;
            py const help = aAction.attr("help");
            if (!help.truthy())
                actionHeader = S("%*s%s\n") % py_tuple({ self.attr("_current_indent"), S(""), actionHeader });
            else if (actionHeader.size() <= actionWidth)
            {
                actionHeader = S("%*s%-*s  ") % py_tuple({ self.attr("_current_indent"), S(""), py{ actionWidth }, actionHeader });
                indentFirst = 0;
            }
            else
            {
                actionHeader = S("%*s%s\n") % py_tuple({ self.attr("_current_indent"), S(""), actionHeader });
                indentFirst = helpPosition;
            }
            py const parts = py_list({ actionHeader });
            if (help.truthy() && help.method("strip").truthy())
            {
                py const helpText = self.method("_expand_help", { aAction });
                if (helpText.truthy())
                {
                    py const helpLines = self.method("_split_lines", { helpText, py{ helpWidth } });
                    parts.method("append", { S("%*s%s\n") % py_tuple({ py{ indentFirst }, S(""), helpLines[py{ 0 }] }) });
                    for (auto const& line : slice(helpLines, 1).items())
                        parts.method("append", { S("%*s%s\n") % py_tuple({ py{ helpPosition }, S(""), line }) });
                }
            }
            else if (!actionHeader.method("endswith", { S("\n") }).truthy())
                parts.method("append", { S("\n") });
            for_each(self.method("_iter_indented_subactions", { aAction }), [&](py const& subaction)
            {
                parts.method("append", { self.method("_format_action", { subaction }) });
            });
            return self.method("_join_parts", { parts });
        }

        // (_metavar_formatter's format: of a tuple size)
        py metavar_formatter(py const& aAction, py const& aDefaultMetavar)
        {
            py result;
            if (!aAction.attr("metavar").is_none())
                result = aAction.attr("metavar");
            else if (!aAction.attr("choices").is_none())
                result = S("{%s}") % py_tuple({ join(",", builtin("map")(builtin("str"), aAction.attr("choices"))) });
            else
                result = aDefaultMetavar;
            return function("format", "HelpFormatter._metavar_formatter.<locals>.format", [result](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("HelpFormatter._metavar_formatter.<locals>.format", { py_positional("tuple_size") }, aArgs, aKwargs);
                if (isinstance(result, builtin("tuple")))
                    return result;
                return py_tuple({ result }) * a[0];
            });
        }

        py format_args_impl(py const& self, py const& aAction, py const& aDefaultMetavar)
        {
            auto const& s = state();
            py const getMetavar = self.method("_metavar_formatter", { aAction, aDefaultMetavar });
            py const nargs = aAction.attr("nargs");
            if (nargs.is_none())
                return S("%s") % getMetavar(py{ 1 });
            if (nargs == s.OPTIONAL)
                return S("[%s]") % getMetavar(py{ 1 });
            if (nargs == s.ZERO_OR_MORE)
            {
                py const metavar = getMetavar(py{ 1 });
                if (metavar.size() == 2)
                    return S("[%s [%s ...]]") % metavar;
                return S("[%s ...]") % metavar;
            }
            if (nargs == s.ONE_OR_MORE)
                return S("%s [%s ...]") % getMetavar(py{ 2 });
            if (nargs == s.REMAINDER)
                return S("...");
            if (nargs == s.PARSER)
                return S("%s ...") % getMetavar(py{ 1 });
            if (nargs == s.SUPPRESS)
                return S("");
            py formats;
            try
            {
                formats = py_list();
                for (auto const& i : builtin("range")(nargs).items())
                {
                    (void)i;
                    formats.method("append", { S("%s") });
                }
            }
            catch (raised const&)
            {
                if (!caught({ "TypeError" }))
                    throw;
                py const e = builtin("ValueError")(S("invalid nargs value"));
                e.set_attr("__suppress_context__", boolean(true));
                py_raise(e);
            }
            return join(" ", formats) % getMetavar(nargs);
        }

        py expand_help_impl(py const& self, py const& aAction)
        {
            py const params = call_kw(builtin("dict"), { builtin("vars")(aAction) }, dict_of({ { "prog", self.attr("_prog") } }));
            for (auto const& name : list_of(params).items())
                if (params[name].is(state().SUPPRESS))
                    params.method("pop", { name });
            for (auto const& name : list_of(params).items())
                if (has_attr(params[name], S("__name__")))
                    params.set_item(name, params[name].attr("__name__"));
            if (!params.method("get", { S("choices") }).is_none())
                params.set_item(S("choices"), join(", ", builtin("map")(builtin("str"), params[S("choices")])));
            return self.method("_get_help_string", { aAction }) % params;
        }

        void make_help_formatters()
        {
            py const section = make_argparse_class("_Section", {}, {
                { "__init__", method("HelpFormatter._Section", "__init__", { py_positional("formatter"), py_positional("parent"), py_positional("heading", none()) },
                    [](std::vector<py> const& a)
                    {
                        a[0].set_attr("formatter", a[1]);
                        a[0].set_attr("parent", a[2]);
                        a[0].set_attr("heading", a[3]);
                        a[0].set_attr("items", py_list());
                        return none();
                    }) },
                { "format_help", method("HelpFormatter._Section", "format_help", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const formatter = self.attr("formatter");
                        if (!self.attr("parent").is_none())
                            formatter.method("_indent");
                        py const join_ = formatter.attr("_join_parts");
                        py const helps = py_list();
                        for (auto const& item : self.attr("items").items())
                        {
                            auto const pair = item.items();
                            helps.method("append", { call_kw(pair.at(0), pair.at(1).items(), py{}) });
                        }
                        py const itemHelp = join_(helps);
                        if (!self.attr("parent").is_none())
                            formatter.method("_dedent");
                        if (!itemHelp.truthy())
                            return S("");
                        py heading;
                        py const h = self.attr("heading");
                        if (!h.is(state().SUPPRESS) && !h.is_none())
                        {
                            py const currentIndent = formatter.attr("_current_indent");
                            py const headingText = _("%(heading)s:") % dict_of({ { "heading", h } });
                            heading = S("%*s%s\n") % py_tuple({ currentIndent, S(""), headingText });
                        }
                        else
                            heading = S("");
                        return join_(py_list({ S("\n"), heading, itemHelp, S("\n") }));
                    }) } },
                nullptr, "HelpFormatter._Section");
            state().classes.erase("_Section");

            make_argparse_class("HelpFormatter", {}, {
                { "__init__", method("HelpFormatter", "__init__", { py_positional("prog"), py_positional("indent_increment", py{ 2 }),
                    py_positional("max_help_position", py{ 24 }), py_positional("width", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py width = a[4];
                        if (width.is_none())
                        {
                            width = import_attribute("shutil", "get_terminal_size")().attr("columns");
                            width = width - py{ 2 };
                        }
                        self.set_attr("_prog", a[1]);
                        self.set_attr("_indent_increment", a[2]);
                        self.set_attr("_max_help_position", builtin("min")(a[3], builtin("max")(width - py{ 20 }, a[2] * py{ 2 })));
                        self.set_attr("_width", width);
                        self.set_attr("_current_indent", py{ 0 });
                        self.set_attr("_level", py{ 0 });
                        self.set_attr("_action_max_length", py{ 0 });
                        self.set_attr("_root_section", self.attr("_Section")(self, none()));
                        self.set_attr("_current_section", self.attr("_root_section"));
                        py const re = py{ fn_import("re") };
                        self.set_attr("_whitespace_matcher", re.attr("compile")(S("\\s+"), re.attr("ASCII")));
                        self.set_attr("_long_break_matcher", re.attr("compile")(S("\\n\\n\\n+")));
                        return none();
                    }) },
                // (section and indentation methods)
                { "_indent", method("HelpFormatter", "_indent", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_current_indent", a[0].attr("_current_indent") + a[0].attr("_indent_increment"));
                        a[0].set_attr("_level", a[0].attr("_level") + py{ 1 });
                        return none();
                    }) },
                { "_dedent", method("HelpFormatter", "_dedent", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_current_indent", a[0].attr("_current_indent") - a[0].attr("_indent_increment"));
                        if (!(a[0].attr("_current_indent") >= py{ 0 }))
                            py_raise("AssertionError", "Indent decreased below 0.");
                        a[0].set_attr("_level", a[0].attr("_level") - py{ 1 });
                        return none();
                    }) },
                { "_Section", section },
                { "_add_item", method("HelpFormatter", "_add_item", { py_positional("func"), py_positional("args") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("_current_section").attr("items").method("append", { py_tuple({ a[1], a[2] }) });
                        return none();
                    }) },
                // (message building methods)
                { "start_section", method("HelpFormatter", "start_section", { py_positional("heading") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.method("_indent");
                        py const sec = self.attr("_Section")(self, self.attr("_current_section"), a[1]);
                        self.method("_add_item", { sec.attr("format_help"), py_list() });
                        self.set_attr("_current_section", sec);
                        return none();
                    }) },
                { "end_section", method("HelpFormatter", "end_section", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_current_section", a[0].attr("_current_section").attr("parent"));
                        a[0].method("_dedent");
                        return none();
                    }) },
                { "add_text", method("HelpFormatter", "add_text", { py_positional("text") }, [](std::vector<py> const& a)
                    {
                        if (!a[1].is(state().SUPPRESS) && !a[1].is_none())
                            a[0].method("_add_item", { a[0].attr("_format_text"), py_list({ a[1] }) });
                        return none();
                    }) },
                { "add_usage", method("HelpFormatter", "add_usage", { py_positional("usage"), py_positional("actions"), py_positional("groups"),
                    py_positional("prefix", none()) }, [](std::vector<py> const& a)
                    {
                        if (!a[1].is(state().SUPPRESS))
                            a[0].method("_add_item", { a[0].attr("_format_usage"), py_tuple({ a[1], a[2], a[3], a[4] }) });
                        return none();
                    }) },
                { "add_argument", method("HelpFormatter", "add_argument", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& action = a[1];
                        if (!action.attr("help").is(state().SUPPRESS))
                        {
                            py const getInvocation = self.attr("_format_action_invocation");
                            py const invocationLengths = py_list({ py{ getInvocation(action).size() } + self.attr("_current_indent") });
                            for_each(self.method("_iter_indented_subactions", { action }), [&](py const& subaction)
                            {
                                invocationLengths.method("append", { py{ getInvocation(subaction).size() } + self.attr("_current_indent") });
                            });
                            py const actionLength = builtin("max")(invocationLengths);
                            self.set_attr("_action_max_length", builtin("max")(self.attr("_action_max_length"), actionLength));
                            self.method("_add_item", { self.attr("_format_action"), py_list({ action }) });
                        }
                        return none();
                    }) },
                { "add_arguments", method("HelpFormatter", "add_arguments", { py_positional("actions") }, [](std::vector<py> const& a)
                    {
                        for (auto const& action : a[1].items())
                            a[0].method("add_argument", { action });
                        return none();
                    }) },
                // (help-formatting methods)
                { "format_help", method("HelpFormatter", "format_help", {}, [](std::vector<py> const& a)
                    {
                        py help = a[0].attr("_root_section").method("format_help");
                        if (help.truthy())
                        {
                            help = a[0].attr("_long_break_matcher").method("sub", { S("\n\n"), help });
                            help = help.method("strip", { S("\n") }) + S("\n");
                        }
                        return help;
                    }) },
                { "_join_parts", method("HelpFormatter", "_join_parts", { py_positional("part_strings") }, [](std::vector<py> const& a)
                    {
                        py const parts = py_list();
                        for (auto const& part : a[1].items())
                            if (part.truthy() && !part.is(state().SUPPRESS))
                                parts.method("append", { part });
                        return join("", parts);
                    }) },
                { "_format_usage", method("HelpFormatter", "_format_usage", { py_positional("usage"), py_positional("actions"), py_positional("groups"),
                    py_positional("prefix") }, [](std::vector<py> const& a) { return format_usage_impl(a[0], a[1], a[2], a[3], a[4]); }) },
                { "_format_actions_usage", method("HelpFormatter", "_format_actions_usage", { py_positional("actions"), py_positional("groups") },
                    [](std::vector<py> const& a) { return join(" ", a[0].method("_get_actions_usage_parts", { a[1], a[2] })); }) },
                { "_get_actions_usage_parts", method("HelpFormatter", "_get_actions_usage_parts", { py_positional("actions"), py_positional("groups") },
                    [](std::vector<py> const& a) { return a[0].method("_get_actions_usage_parts_with_split", { a[1], a[2] })[py{ 0 }]; }) },
                { "_get_actions_usage_parts_with_split", method("HelpFormatter", "_get_actions_usage_parts_with_split", { py_positional("actions"),
                    py_positional("groups"), py_positional("opt_count", none()) }, [](std::vector<py> const& a) { return actions_usage_parts_with_split(a[0], a[1], a[2], a[3]); },
                    false, false, "Get usage parts with split index for optionals/positionals.\n\n        Returns (parts, pos_start) where pos_start is the index in parts\n"
                    "        where positionals begin. When opt_count is None, pos_start is None.\n        This preserves mutually exclusive group formatting across the\n"
                    "        optionals/positionals boundary (gh-75949).\n        ") },
                { "_format_text", method("HelpFormatter", "_format_text", { py_positional("text") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py text = a[1];
                        if (text.contains(S("%(prog)")))
                            text = text % dict_of({ { "prog", self.attr("_prog") } });
                        py const textWidth = builtin("max")(self.attr("_width") - self.attr("_current_indent"), py{ 11 });
                        py const indent = S(" ") * self.attr("_current_indent");
                        return self.method("_fill_text", { text, textWidth, indent }) + S("\n\n");
                    }) },
                { "_format_action", method("HelpFormatter", "_format_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        return format_action_impl(a[0], a[1]);
                    }) },
                { "_format_action_invocation", method("HelpFormatter", "_format_action_invocation", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& action = a[1];
                        if (!action.attr("option_strings").truthy())
                        {
                            py const defaultMetavar = self.method("_get_default_metavar_for_positional", { action });
                            return join(" ", self.method("_metavar_formatter", { action, defaultMetavar })(py{ 1 }));
                        }
                        if (action.attr("nargs") == py{ 0 })
                            return join(", ", action.attr("option_strings"));
                        py const defaultMetavar = self.method("_get_default_metavar_for_optional", { action });
                        py const argsString = self.method("_format_args", { action, defaultMetavar });
                        return join(", ", action.attr("option_strings")) + S(" ") + argsString;
                    }) },
                { "_metavar_formatter", method("HelpFormatter", "_metavar_formatter", { py_positional("action"), py_positional("default_metavar") },
                    [](std::vector<py> const& a) { return metavar_formatter(a[1], a[2]); }) },
                { "_format_args", method("HelpFormatter", "_format_args", { py_positional("action"), py_positional("default_metavar") },
                    [](std::vector<py> const& a) { return format_args_impl(a[0], a[1], a[2]); }) },
                { "_expand_help", method("HelpFormatter", "_expand_help", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        return expand_help_impl(a[0], a[1]);
                    }) },
                { "_iter_indented_subactions", method("HelpFormatter", "_iter_indented_subactions", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        py const self = a[0];
                        py const action = a[1];
                        return make_native_generator("_iter_indented_subactions", "HelpFormatter._iter_indented_subactions", sModule, [self, action]() -> py
                        {
                            py const getSubactions = attr_or(action, "_get_subactions", py{});
                            if (!getSubactions.empty())
                            {
                                self.method("_indent");
                                for (auto const& subaction : getSubactions().items())
                                    py_yield(subaction);
                                self.method("_dedent");
                            }
                            return none();
                        });
                    }) },
                { "_split_lines", method("HelpFormatter", "_split_lines", { py_positional("text"), py_positional("width") }, [](std::vector<py> const& a)
                    {
                        py const text = a[0].attr("_whitespace_matcher").method("sub", { S(" "), a[1] }).method("strip");
                        return import_attribute("textwrap", "wrap")(text, a[2]);
                    }) },
                { "_fill_text", method("HelpFormatter", "_fill_text", { py_positional("text"), py_positional("width"), py_positional("indent") },
                    [](std::vector<py> const& a)
                    {
                        py const text = a[0].attr("_whitespace_matcher").method("sub", { S(" "), a[1] }).method("strip");
                        return import_attribute("textwrap", "fill").call({ text, a[2] }, { { "initial_indent", a[3] }, { "subsequent_indent", a[3] } });
                    }) },
                { "_get_help_string", method("HelpFormatter", "_get_help_string", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        return a[1].attr("help");
                    }) },
                { "_get_default_metavar_for_optional", method("HelpFormatter", "_get_default_metavar_for_optional", { py_positional("action") },
                    [](std::vector<py> const& a) { return a[1].attr("dest").method("upper"); }) },
                { "_get_default_metavar_for_positional", method("HelpFormatter", "_get_default_metavar_for_positional", { py_positional("action") },
                    [](std::vector<py> const& a) { return a[1].attr("dest"); }) } },
                "Formatter for generating usage messages and argument help strings.\n\n    Only the name of this class is considered a public API. All the methods\n"
                "    provided by the class are considered an implementation detail.\n    ");

            make_argparse_class("RawDescriptionHelpFormatter", { cls("HelpFormatter") }, {
                { "_fill_text", method("RawDescriptionHelpFormatter", "_fill_text", { py_positional("text"), py_positional("width"), py_positional("indent") },
                    [](std::vector<py> const& a)
                    {
                        py const lines = py_list();
                        for (auto const& line : a[1].method("splitlines", {}, { { "keepends", boolean(true) } }).items())
                            lines.method("append", { a[3] + line });
                        return join("", lines);
                    }) } },
                "Help message formatter which retains any formatting in descriptions.\n\n    Only the name of this class is considered a public API. All the methods\n"
                "    provided by the class are considered an implementation detail.\n    ");
            make_argparse_class("RawTextHelpFormatter", { cls("RawDescriptionHelpFormatter") }, {
                { "_split_lines", method("RawTextHelpFormatter", "_split_lines", { py_positional("text"), py_positional("width") }, [](std::vector<py> const& a)
                    {
                        return a[1].method("splitlines");
                    }) } },
                "Help message formatter which retains formatting of all help text.\n\n    Only the name of this class is considered a public API. All the methods\n"
                "    provided by the class are considered an implementation detail.\n    ");
            make_argparse_class("ArgumentDefaultsHelpFormatter", { cls("HelpFormatter") }, {
                { "_get_help_string", method("ArgumentDefaultsHelpFormatter", "_get_help_string", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        auto const& s = state();
                        auto const& action = a[1];
                        py help = action.attr("help");
                        if (help.is_none())
                            help = S("");
                        if (!help.contains(S("%(default)")) && !action.attr("default").is(s.SUPPRESS) && !action.attr("required").truthy())
                        {
                            py const nargs = action.attr("nargs");
                            if (action.attr("option_strings").truthy() || py_tuple({ s.OPTIONAL, s.ZERO_OR_MORE }).contains(nargs))
                                help = help + _(" (default: %(default)s)");
                        }
                        return help;
                    }) } },
                "Help message formatter which adds default values to argument help.\n\n    Only the name of this class is considered a public API. All the methods\n"
                "    provided by the class are considered an implementation detail.\n    ");
            make_argparse_class("MetavarTypeHelpFormatter", { cls("HelpFormatter") }, {
                { "_get_default_metavar_for_optional", method("MetavarTypeHelpFormatter", "_get_default_metavar_for_optional", { py_positional("action") },
                    [](std::vector<py> const& a) { return a[1].attr("type").attr("__name__"); }) },
                { "_get_default_metavar_for_positional", method("MetavarTypeHelpFormatter", "_get_default_metavar_for_positional", { py_positional("action") },
                    [](std::vector<py> const& a) { return a[1].attr("type").attr("__name__"); }) } },
                "Help message formatter which uses the argument 'type' as the default\n    metavar value (instead of the argument 'dest')\n\n"
                "    Only the name of this class is considered a public API. All the methods\n    provided by the class are considered an implementation detail.\n    ");
        }

        // --- options and arguments

        py get_action_name(py const& aArgument)
        {
            auto const& s = state();
            if (aArgument.is_none())
                return none();
            if (aArgument.attr("option_strings").truthy())
                return join("/", aArgument.attr("option_strings"));
            py const metavar = aArgument.attr("metavar");
            if (!metavar.is_none() && !(metavar == s.SUPPRESS))
            {
                if (!isinstance(metavar, builtin("tuple")))
                    return metavar;
                py const nargs = aArgument.attr("nargs");
                if (nargs == s.ZERO_OR_MORE && metavar.size() == 2)
                    return S("%s[, %s]") % metavar;
                if (nargs == s.ONE_OR_MORE)
                    return S("%s[, %s]") % metavar;
                return join(", ", metavar);
            }
            py const dest = aArgument.attr("dest");
            if (!dest.is_none() && !(dest == s.SUPPRESS))
                return dest;
            if (aArgument.attr("choices").truthy())
                return S("{%s}") % py_tuple({ join(",", builtin("map")(builtin("str"), aArgument.attr("choices"))) });
            return none();
        }

        void make_exceptions()
        {
            make_argparse_class("ArgumentError", { builtin("Exception") }, {
                { "__init__", method("ArgumentError", "__init__", { py_positional("argument"), py_positional("message") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("argument_name", get_action_name(a[1]));
                        a[0].set_attr("message", a[2]);
                        return none();
                    }) },
                { "__str__", method("ArgumentError", "__str__", {}, [](std::vector<py> const& a)
                    {
                        py const format = a[0].attr("argument_name").is_none() ? S("%(message)s") : _("argument %(argument_name)s: %(message)s");
                        return format % dict_of({ { "message", a[0].attr("message") }, { "argument_name", a[0].attr("argument_name") } });
                    }) } },
                "An error from creating or using an argument (optional or positional).\n\n    The string value of this exception is the message, augmented with\n"
                "    information about the argument that caused it.\n    ");
            make_argparse_class("ArgumentTypeError", { builtin("Exception") }, {}, "An error from trying to convert a command line string to a type.");
        }

        // --- action classes

        // (Action.__init__'s parameters: those named (in Action's order), with their defaults)
        std::vector<py_parameter> action_parameters(std::vector<std::pair<char const*, py>> const& aParameters)
        {
            std::vector<py_parameter> result;
            for (auto const& [name, defaultValue] : aParameters)
                result.push_back(py_positional(name, defaultValue));
            return result;
        }

        // (super(aClass, self).__init__(**keywords))
        void super_init(char const* aClass, py const& aSelf, std::vector<std::pair<char const*, py>> const& aKeywords)
        {
            call_kw(super_of(aClass, aSelf).attr("__init__"), {}, dict_of(aKeywords));
        }

        void make_actions()
        {
            auto& s = state();
            py const attributeHolder = cls("_AttributeHolder");
            static char const* const sActionNames[] = { "option_strings", "dest", "nargs", "const", "default", "type", "choices", "required", "help", "metavar",
                "deprecated" };
            make_argparse_class("Action", { attributeHolder }, {
                { "__init__", method("Action", "__init__", action_parameters({ { "option_strings", {} }, { "dest", {} }, { "nargs", none() }, { "const", none() },
                    { "default", none() }, { "type", none() }, { "choices", none() }, { "required", boolean(false) }, { "help", none() }, { "metavar", none() },
                    { "deprecated", boolean(false) } }), [](std::vector<py> const& a)
                    {
                        for (std::size_t i = 0u; i < std::size(sActionNames); ++i)
                            a[0].set_attr(sActionNames[i], a[i + 1u]);
                        return none();
                    }) },
                { "_get_kwargs", method("Action", "_get_kwargs", {}, [](std::vector<py> const& a)
                    {
                        py const result = py_list();
                        for (auto name : sActionNames)
                            result.method("append", { py_tuple({ S(name), a[0].attr(name) }) });
                        return result;
                    }) },
                { "format_usage", method("Action", "format_usage", {}, [](std::vector<py> const& a) { return a[0].attr("option_strings")[py{ 0 }]; }) },
                { "__call__", method("Action", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const&) -> py
                    {
                        py_raise("NotImplementedError", ".__call__() not defined");
                    }) } },
                "Information about how to convert command line strings to Python objects.\n\n    Action objects are used by an ArgumentParser to represent the information\n"
                "    needed to parse a single argument from one or more strings from the\n    command line. The keyword arguments to the Action constructor are also\n"
                "    all attributes of Action instances.\n\n    Keyword Arguments:\n\n        - option_strings -- A list of command-line option strings which\n"
                "            should be associated with this action.\n\n        - dest -- The name of the attribute to hold the created object(s)\n\n"
                "        - nargs -- The number of command-line arguments that should be\n            consumed. By default, one argument will be consumed and a single\n"
                "            value will be produced.  Other values include:\n                - N (an integer) consumes N arguments (and produces a list)\n"
                "                - '?' consumes zero or one arguments\n                - '*' consumes zero or more arguments (and produces a list)\n"
                "                - '+' consumes one or more arguments (and produces a list)\n            Note that the difference between the default and nargs=1 is that\n"
                "            with the default, a single value will be produced, while with\n            nargs=1, a list containing a single value will be produced.\n\n"
                "        - const -- The value to be produced if the option is specified and the\n            option uses an action that takes no values.\n\n"
                "        - default -- The value to be produced if the option is not specified.\n\n        - type -- A callable that accepts a single string argument, and\n"
                "            returns the converted value.  The standard Python types str, int,\n            float, and complex are useful examples of such callables.  If None,\n"
                "            str is used.\n\n        - choices -- A container of values that should be allowed. If not None,\n"
                "            after a command-line argument has been converted to the appropriate\n            type, an exception will be raised if it is not a member of this\n"
                "            collection.\n\n        - required -- True if the action must always be specified at the\n            command line. This is only meaningful for optional command-line\n"
                "            arguments.\n\n        - help -- The help string describing the argument.\n\n        - metavar -- The name to be used for the option's argument with the\n"
                "            help string. If None, the 'dest' value will be used as the name.\n    ");
            py const action = cls("Action");

            s.deprecatedDefault = builtin("object")();
            make_argparse_class("BooleanOptionalAction", { action }, {
                { "__init__", method("BooleanOptionalAction", "__init__", action_parameters({ { "option_strings", {} }, { "dest", {} }, { "default", none() },
                    { "type", s.deprecatedDefault }, { "choices", s.deprecatedDefault }, { "required", boolean(false) }, { "help", none() },
                    { "metavar", s.deprecatedDefault }, { "deprecated", boolean(false) } }), [](std::vector<py> const& a)
                    {
                        auto const& st = state();
                        py const optionStrings = py_list();
                        for (auto optionString : a[1].items())
                        {
                            optionStrings.method("append", { optionString });
                            if (optionString.method("startswith", { S("--") }).truthy())
                            {
                                optionString = S("--no-") + slice(optionString, 2);
                                optionStrings.method("append", { optionString });
                            }
                        }
                        py type = a[4];
                        py choices = a[5];
                        py metavar = a[8];
                        for (auto const& [fieldName, value] : { std::pair<char const*, py>{ "type", type }, { "choices", choices }, { "metavar", metavar } })
                            if (!value.is(st.deprecatedDefault))
                                import_attribute("warnings", "_deprecated").call({ S(fieldName), S("{name!r} is deprecated as of Python 3.12 and will be "
                                    "removed in Python {remove}.") }, { { "remove", py_tuple({ py{ 3 }, py{ 14 } }) } });
                        if (type.is(st.deprecatedDefault))
                            type = none();
                        if (choices.is(st.deprecatedDefault))
                            choices = none();
                        if (metavar.is(st.deprecatedDefault))
                            metavar = none();
                        super_init("BooleanOptionalAction", a[0], { { "option_strings", optionStrings }, { "dest", a[2] }, { "nargs", py{ 0 } }, { "default", a[3] },
                            { "type", type }, { "choices", choices }, { "required", a[6] }, { "help", a[7] }, { "metavar", metavar }, { "deprecated", a[9] } });
                        return none();
                    }) },
                { "__call__", method("BooleanOptionalAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("option_strings").contains(a[4]))
                            set_attr(a[2], a[0].attr("dest"), boolean(!a[4].method("startswith", { S("--no-") }).truthy()));
                        return none();
                    }) },
                { "format_usage", method("BooleanOptionalAction", "format_usage", {}, [](std::vector<py> const& a)
                    {
                        return join(" | ", a[0].attr("option_strings"));
                    }) } });

            auto const store_like = [&](char const* aName, char const* aNargsMessage, bool aAppend)
            {
                std::string const name = aName;
                std::string const message = aNargsMessage;
                make_argparse_class(aName, { action }, {
                    { "__init__", method(aName, "__init__", action_parameters({ { "option_strings", {} }, { "dest", {} }, { "nargs", none() }, { "const", none() },
                        { "default", none() }, { "type", none() }, { "choices", none() }, { "required", boolean(false) }, { "help", none() }, { "metavar", none() },
                        { "deprecated", boolean(false) } }), [name, message](std::vector<py> const& a)
                        {
                            if (a[3] == py{ 0 })
                                py_raise("ValueError", message);
                            if (!a[4].is_none() && a[3] != state().OPTIONAL)
                                py_raise("ValueError", (S("nargs must be %r to supply const") % py_tuple({ state().OPTIONAL })).str());
                            super_init(name.c_str(), a[0], { { "option_strings", a[1] }, { "dest", a[2] }, { "nargs", a[3] }, { "const", a[4] }, { "default", a[5] },
                                { "type", a[6] }, { "choices", a[7] }, { "required", a[8] }, { "help", a[9] }, { "metavar", a[10] }, { "deprecated", a[11] } });
                            return none();
                        }) },
                    { "__call__", method(aName, "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                        py_positional("option_string", none()) }, [aAppend](std::vector<py> const& a)
                        {
                            if (aAppend)
                            {
                                py const items = copy_items(get_attr(a[2], a[0].attr("dest"), none()));
                                items.method("append", { a[3] });
                                set_attr(a[2], a[0].attr("dest"), items);
                            }
                            else
                                set_attr(a[2], a[0].attr("dest"), a[3]);
                            return none();
                        }) } });
            };
            store_like("_StoreAction", "nargs for store actions must be != 0; if you have nothing to store, actions such as store true or store const may be more "
                "appropriate", false);

            make_argparse_class("_StoreConstAction", { action }, {
                { "__init__", method("_StoreConstAction", "__init__", action_parameters({ { "option_strings", {} }, { "dest", {} }, { "const", none() },
                    { "default", none() }, { "required", boolean(false) }, { "help", none() }, { "metavar", none() }, { "deprecated", boolean(false) } }),
                    [](std::vector<py> const& a)
                    {
                        super_init("_StoreConstAction", a[0], { { "option_strings", a[1] }, { "dest", a[2] }, { "nargs", py{ 0 } }, { "const", a[3] },
                            { "default", a[4] }, { "required", a[5] }, { "help", a[6] }, { "deprecated", a[8] } });
                        return none();
                    }) },
                { "__call__", method("_StoreConstAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        set_attr(a[2], a[0].attr("dest"), a[0].attr("const"));
                        return none();
                    }) } });
            for (auto const& [name, value] : { std::pair<char const*, bool>{ "_StoreTrueAction", true }, { "_StoreFalseAction", false } })
            {
                std::string const className = name;
                bool const stored = value;
                make_argparse_class(name, { cls("_StoreConstAction") }, {
                    { "__init__", method(name, "__init__", action_parameters({ { "option_strings", {} }, { "dest", {} }, { "default", boolean(!value) },
                        { "required", boolean(false) }, { "help", none() }, { "deprecated", boolean(false) } }), [className, stored](std::vector<py> const& a)
                        {
                            super_init(className.c_str(), a[0], { { "option_strings", a[1] }, { "dest", a[2] }, { "const", boolean(stored) }, { "default", a[3] },
                                { "required", a[4] }, { "help", a[5] }, { "deprecated", a[6] } });
                            return none();
                        }) } });
            }
            store_like("_AppendAction", "nargs for append actions must be != 0; if arg strings are not supplying the value to append, the append const action may "
                "be more appropriate", true);

            make_argparse_class("_AppendConstAction", { action }, {
                { "__init__", method("_AppendConstAction", "__init__", action_parameters({ { "option_strings", {} }, { "dest", {} }, { "const", none() },
                    { "default", none() }, { "required", boolean(false) }, { "help", none() }, { "metavar", none() }, { "deprecated", boolean(false) } }),
                    [](std::vector<py> const& a)
                    {
                        super_init("_AppendConstAction", a[0], { { "option_strings", a[1] }, { "dest", a[2] }, { "nargs", py{ 0 } }, { "const", a[3] },
                            { "default", a[4] }, { "required", a[5] }, { "help", a[6] }, { "metavar", a[7] }, { "deprecated", a[8] } });
                        return none();
                    }) },
                { "__call__", method("_AppendConstAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        py const items = copy_items(get_attr(a[2], a[0].attr("dest"), none()));
                        items.method("append", { a[0].attr("const") });
                        set_attr(a[2], a[0].attr("dest"), items);
                        return none();
                    }) } });

            make_argparse_class("_CountAction", { action }, {
                { "__init__", method("_CountAction", "__init__", action_parameters({ { "option_strings", {} }, { "dest", {} }, { "default", none() },
                    { "required", boolean(false) }, { "help", none() }, { "deprecated", boolean(false) } }), [](std::vector<py> const& a)
                    {
                        super_init("_CountAction", a[0], { { "option_strings", a[1] }, { "dest", a[2] }, { "nargs", py{ 0 } }, { "default", a[3] },
                            { "required", a[4] }, { "help", a[5] }, { "deprecated", a[6] } });
                        return none();
                    }) },
                { "__call__", method("_CountAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        py count = get_attr(a[2], a[0].attr("dest"), none());
                        if (count.is_none())
                            count = py{ 0 };
                        set_attr(a[2], a[0].attr("dest"), count + py{ 1 });
                        return none();
                    }) } });

            make_argparse_class("_HelpAction", { action }, {
                { "__init__", method("_HelpAction", "__init__", action_parameters({ { "option_strings", {} }, { "dest", s.SUPPRESS }, { "default", s.SUPPRESS },
                    { "help", none() }, { "deprecated", boolean(false) } }), [](std::vector<py> const& a)
                    {
                        super_init("_HelpAction", a[0], { { "option_strings", a[1] }, { "dest", a[2] }, { "default", a[3] }, { "nargs", py{ 0 } }, { "help", a[4] },
                            { "deprecated", a[5] } });
                        return none();
                    }) },
                { "__call__", method("_HelpAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        a[1].method("print_help");
                        a[1].method("exit");
                        return none();
                    }) } });

            make_argparse_class("_VersionAction", { action }, {
                { "__init__", method("_VersionAction", "__init__", action_parameters({ { "option_strings", {} }, { "version", none() }, { "dest", s.SUPPRESS },
                    { "default", s.SUPPRESS }, { "help", none() }, { "deprecated", boolean(false) } }), [](std::vector<py> const& a)
                    {
                        py help = a[5];
                        if (help.is_none())
                            help = _("show program's version number and exit");
                        super_init("_VersionAction", a[0], { { "option_strings", a[1] }, { "dest", a[3] }, { "default", a[4] }, { "nargs", py{ 0 } }, { "help", help } });
                        a[0].set_attr("version", a[2]);
                        return none();
                    }) },
                { "__call__", method("_VersionAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& parser = a[1];
                        py version = a[0].attr("version");
                        if (version.is_none())
                            version = parser.attr("version");
                        py const formatter = parser.method("_get_formatter");
                        formatter.method("add_text", { version });
                        parser.method("_print_message", { formatter.method("format_help"), sys_attr("stdout") });
                        parser.method("exit");
                        return none();
                    }) } });

            py const choicesPseudoAction = make_argparse_class("_ChoicesPseudoAction", { action }, {
                { "__init__", method("_SubParsersAction._ChoicesPseudoAction", "__init__", { py_positional("name"), py_positional("aliases"), py_positional("help") },
                    [](std::vector<py> const& a)
                    {
                        py metavar = a[1];
                        py const dest = a[1];
                        if (a[2].truthy())
                            metavar = metavar + S(" (%s)") % py_tuple({ join(", ", a[2]) });
                        py const sup = builtin("super")(cls("_SubParsersAction").attr("_ChoicesPseudoAction"), a[0]);
                        call_kw(sup.attr("__init__"), {}, dict_of({ { "option_strings", py_list() }, { "dest", dest }, { "help", a[3] }, { "metavar", metavar } }));
                        return none();
                    }) } },
                nullptr, "_SubParsersAction._ChoicesPseudoAction");
            state().classes.erase("_ChoicesPseudoAction");

            make_argparse_class("_SubParsersAction", { action }, {
                { "_ChoicesPseudoAction", choicesPseudoAction },
                { "__init__", method("_SubParsersAction", "__init__", action_parameters({ { "option_strings", {} }, { "prog", {} }, { "parser_class", {} },
                    { "dest", s.SUPPRESS }, { "required", boolean(false) }, { "help", none() }, { "metavar", none() } }), [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("_prog_prefix", a[2]);
                        self.set_attr("_parser_class", a[3]);
                        self.set_attr("_name_parser_map", py_dict());
                        self.set_attr("_choices_actions", py_list());
                        self.set_attr("_deprecated", builtin("set")());
                        super_init("_SubParsersAction", self, { { "option_strings", a[1] }, { "dest", a[4] }, { "nargs", state().PARSER },
                            { "choices", self.attr("_name_parser_map") }, { "required", a[5] }, { "help", a[6] }, { "metavar", a[7] } });
                        return none();
                    }) },
                { "add_parser", method("_SubParsersAction", "add_parser", { py_positional("name"), py_keyword("deprecated", boolean(false)) },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& name = a[1];
                        py const kwargs = a[3];
                        if (kwargs.method("get", { S("prog") }).is_none())
                            kwargs.set_item(S("prog"), S("%s %s") % py_tuple({ self.attr("_prog_prefix"), name }));
                        py const aliases = kwargs.method("pop", { S("aliases"), py_tuple() });
                        py const nameParserMap = self.attr("_name_parser_map");
                        if (nameParserMap.contains(name))
                            raise_argument_error(self, _("conflicting subparser: %s") % name);
                        for (auto const& alias : aliases.items())
                            if (nameParserMap.contains(alias))
                                raise_argument_error(self, _("conflicting subparser alias: %s") % alias);
                        if (kwargs.contains(S("help")))
                        {
                            py const help = kwargs.method("pop", { S("help") });
                            py const choiceAction = self.attr("_ChoicesPseudoAction")(name, aliases, help);
                            self.attr("_choices_actions").method("append", { choiceAction });
                        }
                        py const parser = call_kw(self.attr("_parser_class"), {}, kwargs);
                        nameParserMap.set_item(name, parser);
                        for (auto const& alias : aliases.items())
                            nameParserMap.set_item(alias, parser);
                        if (a[2].truthy())
                        {
                            self.attr("_deprecated").method("add", { name });
                            self.attr("_deprecated").method("update", { aliases });
                        }
                        return parser;
                    }, false, true) },
                { "_get_subactions", method("_SubParsersAction", "_get_subactions", {}, [](std::vector<py> const& a) { return a[0].attr("_choices_actions"); }) },
                { "__call__", method("_SubParsersAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& parser = a[1];
                        auto const& namespace_ = a[2];
                        py const parserName = a[3][py{ 0 }];
                        py argStrings = slice(a[3], 1);
                        if (!self.attr("dest").is(state().SUPPRESS))
                            set_attr(namespace_, self.attr("dest"), parserName);
                        py subparser;
                        try
                        {
                            subparser = self.attr("_name_parser_map")[parserName];
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                            py const args = dict_of({ { "parser_name", parserName }, { "choices", join(", ", self.attr("_name_parser_map")) } });
                            py const msg = _("unknown parser %(parser_name)r (choices: %(choices)s)") % args;
                            raise_argument_error(self, msg);
                        }
                        if (self.attr("_deprecated").contains(parserName))
                            parser.method("_warning", { _("command '%(parser_name)s' is deprecated") % dict_of({ { "parser_name", parserName } }) });
                        py const result = subparser.method("parse_known_args", { argStrings, none() });
                        py const subnamespace = result[py{ 0 }];
                        argStrings = result[py{ 1 }];
                        for (auto const& item : builtin("vars")(subnamespace).method("items").items())
                            set_attr(namespace_, item[py{ 0 }], item[py{ 1 }]);
                        if (argStrings.truthy())
                        {
                            if (!has_attr(namespace_, S("_unrecognized_args")))
                                set_attr(namespace_, S("_unrecognized_args"), py_list());
                            get_attr(namespace_, S("_unrecognized_args")).method("extend", { argStrings });
                        }
                        return none();
                    }) } });

            make_argparse_class("_ExtendAction", { cls("_AppendAction") }, {
                { "__call__", method("_ExtendAction", "__call__", { py_positional("parser"), py_positional("namespace"), py_positional("values"),
                    py_positional("option_string", none()) }, [](std::vector<py> const& a)
                    {
                        py const items = copy_items(get_attr(a[2], a[0].attr("dest"), none()));
                        items.method("extend", { a[3] });
                        set_attr(a[2], a[0].attr("dest"), items);
                        return none();
                    }) } });
        }

        // --- type classes, and Namespace

        void make_file_type_and_namespace()
        {
            make_argparse_class("FileType", {}, {
                { "__init__", method("FileType", "__init__", { py_positional("mode", S("r")), py_positional("bufsize", py{ -1 }), py_positional("encoding", none()),
                    py_positional("errors", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_mode", a[1]);
                        a[0].set_attr("_bufsize", a[2]);
                        a[0].set_attr("_encoding", a[3]);
                        a[0].set_attr("_errors", a[4]);
                        return none();
                    }) },
                { "__call__", method("FileType", "__call__", { py_positional("string") }, [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        auto const& string = a[1];
                        py const mode = self.attr("_mode");
                        if (string == S("-"))
                        {
                            if (mode.contains(S("r")))
                                return mode.contains(S("b")) ? sys_attr("stdin").attr("buffer") : sys_attr("stdin");
                            if (mode.contains(S("w")) || mode.contains(S("a")) || mode.contains(S("x")))
                                return mode.contains(S("b")) ? sys_attr("stdout").attr("buffer") : sys_attr("stdout");
                            py_raise("ValueError", (_("argument \"-\" with mode %r") % mode).str());
                        }
                        try
                        {
                            return builtin("open")(string, mode, self.attr("_bufsize"), self.attr("_encoding"), self.attr("_errors"));
                        }
                        catch (raised const&)
                        {
                            py e;
                            if (!caught_instance(builtin("OSError"), e))
                                throw;
                            py const args = dict_of({ { "filename", string }, { "error", e } });
                            py_raise(cls("ArgumentTypeError")(_("can't open '%(filename)s': %(error)s") % args));
                        }
                    }) },
                { "__repr__", method("FileType", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const parts = py_list();
                        for (auto const& arg : { self.attr("_mode"), self.attr("_bufsize") })
                            if (arg != py{ -1 })
                                parts.method("append", { builtin("repr")(arg) });
                        for (auto const& [kw, arg] : { std::pair<char const*, py>{ "encoding", self.attr("_encoding") }, { "errors", self.attr("_errors") } })
                            if (!arg.is_none())
                                parts.method("append", { S("%s=%r") % py_tuple({ S(kw), arg }) });
                        return S("%s(%s)") % py_tuple({ type_of(self).attr("__name__"), join(", ", parts) });
                    }) } },
                "Factory for creating file object types\n\n    Instances of FileType are typically passed as type= arguments to the\n    ArgumentParser add_argument() method.\n\n"
                "    Keyword Arguments:\n        - mode -- A string indicating how the file is to be opened. Accepts the\n            same values as the builtin open() function.\n"
                "        - bufsize -- The file's desired buffer size. Accepts the same values as\n            the builtin open() function.\n"
                "        - encoding -- The file's encoding. Accepts the same values as the\n            builtin open() function.\n"
                "        - errors -- A string indicating how encoding and decoding errors are to\n            be handled. Accepts the same value as the builtin open() function.\n    ");

            make_argparse_class("Namespace", { cls("_AttributeHolder") }, {
                { "__init__", method("Namespace", "__init__", {}, [](std::vector<py> const& a)
                    {
                        for (auto const& name : a[1].items())
                            set_attr(a[0], name, a[1][name]);
                        return none();
                    }, false, true) },
                { "__eq__", method("Namespace", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (!isinstance(a[1], cls("Namespace")))
                            return py::borrowed(not_implemented());
                        return boolean(builtin("vars")(a[0]) == builtin("vars")(a[1]));
                    }) },
                { "__contains__", method("Namespace", "__contains__", { py_positional("key") }, [](std::vector<py> const& a)
                    {
                        return boolean(a[0].attr("__dict__").contains(a[1]));
                    }) } },
                "Simple object for storing attributes.\n\n    Implements equality by attribute names and values, and provides a simple\n    string representation.\n    ");
        }

        // --- _ActionsContainer, _ArgumentGroup and _MutuallyExclusiveGroup

        py add_argument_impl(py const& self, py const& aArgs, py aKwargs)
        {
            auto const& s = state();
            py const chars = self.attr("prefix_chars");
            if (!aArgs.truthy() || (aArgs.size() == 1 && !chars.contains(aArgs[py{ 0 }][py{ 0 }])))
            {
                if (aArgs.truthy() && aKwargs.contains(S("dest")))
                    py_raise("ValueError", "dest supplied twice for positional argument");
                aKwargs = call_kw(self.attr("_get_positional_kwargs"), aArgs.items(), aKwargs);
            }
            else
                aKwargs = call_kw(self.attr("_get_optional_kwargs"), aArgs.items(), aKwargs);
            if (!aKwargs.contains(S("default")))
            {
                py const dest = aKwargs[S("dest")];
                if (self.attr("_defaults").contains(dest))
                    aKwargs.set_item(S("default"), self.attr("_defaults")[dest]);
                else if (!self.attr("argument_default").is_none())
                    aKwargs.set_item(S("default"), self.attr("argument_default"));
            }
            py const actionClass = self.method("_pop_action_class", { aKwargs });
            if (!callable(actionClass))
                py_raise("ValueError", (S("unknown action \"%s\"") % py_tuple({ actionClass })).str());
            py const action = call_kw(actionClass, {}, aKwargs);
            py const typeFunc = self.method("_registry_get", { S("type"), action.attr("type"), action.attr("type") });
            if (!callable(typeFunc))
                py_raise("ValueError", (S("%r is not callable") % py_tuple({ typeFunc })).str());
            if (typeFunc.is(cls("FileType")))
                py_raise("ValueError", (S("%r is a FileType class object, instance of it must be passed") % py_tuple({ typeFunc })).str());
            if (has_attr(self, S("_get_formatter")))
            {
                try
                {
                    self.method("_get_formatter").method("_format_args", { action, none() });
                }
                catch (raised const&)
                {
                    if (!caught({ "TypeError" }))
                        throw;
                    py_raise("ValueError", "length of metavar tuple does not match nargs");
                }
            }
            (void)s;
            return self.method("_add_action", { action });
        }

        void add_container_actions(py const& self, py const& aContainer)
        {
            py const titleGroupMap = py_dict();
            for (auto const& group : self.attr("_action_groups").items())
            {
                if (titleGroupMap.contains(group.attr("title")))
                    py_raise("ValueError", (_("cannot merge actions - two groups are named %r") % group.attr("title")).str());
                titleGroupMap.set_item(group.attr("title"), group);
            }
            py const groupMap = py_dict();
            for (auto const& group : aContainer.attr("_action_groups").items())
            {
                if (!titleGroupMap.contains(group.attr("title")))
                    titleGroupMap.set_item(group.attr("title"), self.method("add_argument_group", {}, { { "title", group.attr("title") },
                        { "description", group.attr("description") }, { "conflict_handler", group.attr("conflict_handler") } }));
                for (auto const& action : group.attr("_group_actions").items())
                    groupMap.set_item(action, titleGroupMap[group.attr("title")]);
            }
            for (auto const& group : aContainer.attr("_mutually_exclusive_groups").items())
            {
                py const cont = group.attr("_container").is(aContainer) ? self : titleGroupMap[group.attr("_container").attr("title")];
                py const mutexGroup = cont.method("add_mutually_exclusive_group", {}, { { "required", group.attr("required") } });
                for (auto const& action : group.attr("_group_actions").items())
                    groupMap.set_item(action, mutexGroup);
            }
            for (auto const& action : aContainer.attr("_actions").items())
                groupMap.method("get", { action, self }).method("_add_action", { action });
        }

        void make_containers()
        {
            static char const* const sActions[][2] = { { nullptr, "_StoreAction" }, { "store", "_StoreAction" }, { "store_const", "_StoreConstAction" },
                { "store_true", "_StoreTrueAction" }, { "store_false", "_StoreFalseAction" }, { "append", "_AppendAction" }, { "append_const", "_AppendConstAction" },
                { "count", "_CountAction" }, { "help", "_HelpAction" }, { "version", "_VersionAction" }, { "parsers", "_SubParsersAction" },
                { "extend", "_ExtendAction" } };
            make_argparse_class("_ActionsContainer", {}, {
                { "__init__", method("_ActionsContainer", "__init__", { py_positional("description"), py_positional("prefix_chars"), py_positional("argument_default"),
                    py_positional("conflict_handler") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        super_of("_ActionsContainer", self).method("__init__");
                        self.set_attr("description", a[1]);
                        self.set_attr("argument_default", a[3]);
                        self.set_attr("prefix_chars", a[2]);
                        self.set_attr("conflict_handler", a[4]);
                        self.set_attr("_registries", py_dict());
                        for (auto const& [value, actionClass] : sActions)
                            self.method("register", { S("action"), value == nullptr ? none() : S(value), cls(actionClass) });
                        self.method("_get_handler");
                        self.set_attr("_actions", py_list());
                        self.set_attr("_option_string_actions", py_dict());
                        self.set_attr("_action_groups", py_list());
                        self.set_attr("_mutually_exclusive_groups", py_list());
                        self.set_attr("_defaults", py_dict());
                        self.set_attr("_negative_number_matcher", import_attribute("re", "compile")(S("^-\\d+$|^-\\d*\\.\\d+$")));
                        self.set_attr("_has_negative_number_optionals", py_list());
                        return none();
                    }) },
                // (registration methods)
                { "register", method("_ActionsContainer", "register", { py_positional("registry_name"), py_positional("value"), py_positional("object") },
                    [](std::vector<py> const& a)
                    {
                        py const registry = a[0].attr("_registries").method("setdefault", { a[1], py_dict() });
                        registry.set_item(a[2], a[3]);
                        return none();
                    }) },
                { "_registry_get", method("_ActionsContainer", "_registry_get", { py_positional("registry_name"), py_positional("value"),
                    py_positional("default", none()) }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("_registries")[a[1]].method("get", { a[2], a[3] });
                    }) },
                // (namespace default accessor methods)
                { "set_defaults", method("_ActionsContainer", "set_defaults", {}, [](std::vector<py> const& a)
                    {
                        a[0].attr("_defaults").method("update", { a[1] });
                        for (auto const& action : a[0].attr("_actions").items())
                            if (a[1].contains(action.attr("dest")))
                                action.set_attr("default", a[1][action.attr("dest")]);
                        return none();
                    }, false, true) },
                { "get_default", method("_ActionsContainer", "get_default", { py_positional("dest") }, [](std::vector<py> const& a)
                    {
                        for (auto const& action : a[0].attr("_actions").items())
                            if (action.attr("dest") == a[1] && !action.attr("default").is_none())
                                return action.attr("default");
                        return a[0].attr("_defaults").method("get", { a[1], none() });
                    }) },
                // (adding argument actions)
                { "add_argument", method("_ActionsContainer", "add_argument", {}, [](std::vector<py> const& a) { return add_argument_impl(a[0], a[1], a[2]); },
                    true, true, "\n        add_argument(dest, ..., name=value, ...)\n        add_argument(option_string, option_string, ..., name=value, ...)\n        ") },
                { "add_argument_group", method("_ActionsContainer", "add_argument_group", {}, [](std::vector<py> const& a)
                    {
                        std::vector<py> args{ a[0] };
                        for (auto const& x : a[1].items())
                            args.push_back(x);
                        py const group = call_kw(cls("_ArgumentGroup"), args, a[2]);
                        a[0].attr("_action_groups").method("append", { group });
                        return group;
                    }, true, true) },
                { "add_mutually_exclusive_group", method("_ActionsContainer", "add_mutually_exclusive_group", {}, [](std::vector<py> const& a)
                    {
                        py const group = call_kw(cls("_MutuallyExclusiveGroup"), { a[0] }, a[1]);
                        a[0].attr("_mutually_exclusive_groups").method("append", { group });
                        return group;
                    }, false, true) },
                { "_add_action", method("_ActionsContainer", "_add_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& action = a[1];
                        self.method("_check_conflict", { action });
                        self.attr("_actions").method("append", { action });
                        action.set_attr("container", self);
                        for (auto const& optionString : action.attr("option_strings").items())
                            self.attr("_option_string_actions").set_item(optionString, action);
                        for (auto const& optionString : action.attr("option_strings").items())
                            if (self.attr("_negative_number_matcher").method("match", { optionString }).truthy())
                                if (!self.attr("_has_negative_number_optionals").truthy())
                                    self.attr("_has_negative_number_optionals").method("append", { boolean(true) });
                        return action;
                    }) },
                { "_remove_action", method("_ActionsContainer", "_remove_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("_actions").method("remove", { a[1] });
                        return none();
                    }) },
                { "_add_container_actions", method("_ActionsContainer", "_add_container_actions", { py_positional("container") }, [](std::vector<py> const& a)
                    {
                        add_container_actions(a[0], a[1]);
                        return none();
                    }) },
                { "_get_positional_kwargs", method("_ActionsContainer", "_get_positional_kwargs", { py_positional("dest") }, [](std::vector<py> const& a)
                    {
                        auto const& s = state();
                        py const kwargs = a[2];
                        if (kwargs.contains(S("required")))
                            py_raise("TypeError", "'required' is an invalid argument for positionals");
                        py const nargs = kwargs.method("get", { S("nargs") });
                        if (!py_list({ s.OPTIONAL, s.ZERO_OR_MORE, s.REMAINDER, s.SUPPRESS, py{ 0 } }).contains(nargs))
                            kwargs.set_item(S("required"), boolean(true));
                        return call_kw(builtin("dict"), { kwargs }, dict_of({ { "dest", a[1] }, { "option_strings", py_list() } }));
                    }, false, true) },
                { "_get_optional_kwargs", method("_ActionsContainer", "_get_optional_kwargs", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const kwargs = a[2];
                        py const optionStrings = py_list();
                        py const longOptionStrings = py_list();
                        py const prefixChars = self.attr("prefix_chars");
                        py optionString;
                        for (auto const& o : a[1].items())
                        {
                            optionString = o;
                            if (!prefixChars.contains(optionString[py{ 0 }]))
                            {
                                py const args = dict_of({ { "option", optionString }, { "prefix_chars", prefixChars } });
                                py_raise("ValueError", (_("invalid option string %(option)r: must start with a character %(prefix_chars)r") % args).str());
                            }
                            optionStrings.method("append", { optionString });
                            if (optionString.size() > 1 && prefixChars.contains(optionString[py{ 1 }]))
                                longOptionStrings.method("append", { optionString });
                        }
                        py dest = kwargs.method("pop", { S("dest"), none() });
                        if (dest.is_none())
                        {
                            py const destOptionString = longOptionStrings.truthy() ? longOptionStrings[py{ 0 }] : optionStrings[py{ 0 }];
                            dest = destOptionString.method("lstrip", { prefixChars });
                            if (!dest.truthy())
                                py_raise("ValueError", (_("dest= is required for options like %r") % optionString).str());
                            dest = dest.method("replace", { S("-"), S("_") });
                        }
                        return call_kw(builtin("dict"), { kwargs }, dict_of({ { "dest", dest }, { "option_strings", optionStrings } }));
                    }, true, true) },
                { "_pop_action_class", method("_ActionsContainer", "_pop_action_class", { py_positional("kwargs"), py_positional("default", none()) },
                    [](std::vector<py> const& a)
                    {
                        py const action = a[1].method("pop", { S("action"), a[2] });
                        return a[0].method("_registry_get", { S("action"), action, action });
                    }) },
                { "_get_handler", method("_ActionsContainer", "_get_handler", {}, [](std::vector<py> const& a)
                    {
                        py const handlerFuncName = S("_handle_conflict_%s") % a[0].attr("conflict_handler");
                        try
                        {
                            return get_attr(a[0], handlerFuncName);
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "AttributeError" }))
                                throw;
                            py_raise("ValueError", (_("invalid conflict_resolution value: %r") % a[0].attr("conflict_handler")).str());
                        }
                    }) },
                { "_check_conflict", method("_ActionsContainer", "_check_conflict", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        py const conflOptionals = py_list();
                        py const optionStringActions = a[0].attr("_option_string_actions");
                        for (auto const& optionString : a[1].attr("option_strings").items())
                            if (optionStringActions.contains(optionString))
                                conflOptionals.method("append", { py_tuple({ optionString, optionStringActions[optionString] }) });
                        if (conflOptionals.truthy())
                            a[0].method("_get_handler")(a[1], conflOptionals);
                        return none();
                    }) },
                { "_handle_conflict_error", method("_ActionsContainer", "_handle_conflict_error", { py_positional("action"), py_positional("conflicting_actions") },
                    [](std::vector<py> const& a) -> py
                    {
                        py const message = ngettext("conflicting option string: %s", "conflicting option strings: %s", py{ a[2].size() });
                        py const strings = py_list();
                        for (auto const& item : a[2].items())
                            strings.method("append", { item[py{ 0 }] });
                        raise_argument_error(a[1], message % join(", ", strings));
                    }) },
                { "_handle_conflict_resolve", method("_ActionsContainer", "_handle_conflict_resolve", { py_positional("action"),
                    py_positional("conflicting_actions") }, [](std::vector<py> const& a)
                    {
                        for (auto const& item : a[2].items())
                        {
                            py const optionString = item[py{ 0 }];
                            py const action = item[py{ 1 }];
                            action.attr("option_strings").method("remove", { optionString });
                            a[0].attr("_option_string_actions").method("pop", { optionString, none() });
                            if (!action.attr("option_strings").truthy())
                                action.attr("container").method("_remove_action", { action });
                        }
                        return none();
                    }) } });

            make_argparse_class("_ArgumentGroup", { cls("_ActionsContainer") }, {
                { "__init__", method("_ArgumentGroup", "__init__", { py_positional("container"), py_positional("title", none()), py_positional("description", none()) },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& container = a[1];
                        py const kwargs = a[4];
                        kwargs.method("setdefault", { S("conflict_handler"), container.attr("conflict_handler") });
                        kwargs.method("setdefault", { S("prefix_chars"), container.attr("prefix_chars") });
                        kwargs.method("setdefault", { S("argument_default"), container.attr("argument_default") });
                        py const allKwargs = call_kw(builtin("dict"), {}, dict_of({ { "description", a[3] } }));
                        allKwargs.method("update", { kwargs });
                        call_kw(super_of("_ArgumentGroup", self).attr("__init__"), {}, allKwargs);
                        self.set_attr("title", a[2]);
                        self.set_attr("_group_actions", py_list());
                        for (auto name : { "_registries", "_actions", "_option_string_actions", "_defaults", "_has_negative_number_optionals",
                            "_mutually_exclusive_groups" })
                            self.set_attr(name, container.attr(name));
                        return none();
                    }, false, true) },
                { "_add_action", method("_ArgumentGroup", "_add_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        py const action = super_of("_ArgumentGroup", a[0]).method("_add_action", { a[1] });
                        a[0].attr("_group_actions").method("append", { action });
                        return action;
                    }) },
                { "_remove_action", method("_ArgumentGroup", "_remove_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        super_of("_ArgumentGroup", a[0]).method("_remove_action", { a[1] });
                        a[0].attr("_group_actions").method("remove", { a[1] });
                        return none();
                    }) },
                { "add_argument_group", method("_ArgumentGroup", "add_argument_group", {}, [](std::vector<py> const& a)
                    {
                        import_attribute("warnings", "warn").call({ S("Nesting argument groups is deprecated.") }, { { "category", builtin("DeprecationWarning") },
                            { "stacklevel", py{ 2 } } });
                        return call_kw(super_of("_ArgumentGroup", a[0]).attr("add_argument_group"), a[1].items(), a[2]);
                    }, true, true) } });

            make_argparse_class("_MutuallyExclusiveGroup", { cls("_ArgumentGroup") }, {
                { "__init__", method("_MutuallyExclusiveGroup", "__init__", { py_positional("container"), py_positional("required", boolean(false)) },
                    [](std::vector<py> const& a)
                    {
                        super_of("_MutuallyExclusiveGroup", a[0]).method("__init__", { a[1] });
                        a[0].set_attr("required", a[2]);
                        a[0].set_attr("_container", a[1]);
                        return none();
                    }) },
                { "_add_action", method("_MutuallyExclusiveGroup", "_add_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        if (a[1].attr("required").truthy())
                            py_raise("ValueError", "mutually exclusive arguments must be optional");
                        py const action = a[0].attr("_container").method("_add_action", { a[1] });
                        a[0].attr("_group_actions").method("append", { action });
                        return action;
                    }) },
                { "_remove_action", method("_MutuallyExclusiveGroup", "_remove_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("_container").method("_remove_action", { a[1] });
                        a[0].attr("_group_actions").method("remove", { a[1] });
                        return none();
                    }) },
                { "add_mutually_exclusive_group", method("_MutuallyExclusiveGroup", "add_mutually_exclusive_group", {}, [](std::vector<py> const& a)
                    {
                        import_attribute("warnings", "warn").call({ S("Nesting mutually exclusive groups is deprecated.") }, { { "category",
                            builtin("DeprecationWarning") }, { "stacklevel", py{ 2 } } });
                        return call_kw(super_of("_MutuallyExclusiveGroup", a[0]).attr("add_mutually_exclusive_group"), a[1].items(), a[2]);
                    }, true, true) } });
        }

        // --- ArgumentParser

        py parse_known_args2(py const& self, py aArgs, py aNamespace, py const& aIntermixed)
        {
            auto const& s = state();
            if (aArgs.is_none())
                aArgs = slice(sys_attr("argv"), 1);
            else
                aArgs = list_of(aArgs);
            if (aNamespace.is_none())
                aNamespace = cls("Namespace")();
            for (auto const& action : self.attr("_actions").items())
                if (!action.attr("dest").is(s.SUPPRESS))
                    if (!has_attr(aNamespace, action.attr("dest")))
                        if (!action.attr("default").is(s.SUPPRESS))
                            set_attr(aNamespace, action.attr("dest"), action.attr("default"));
            for (auto const& dest : self.attr("_defaults").items())
                if (!has_attr(aNamespace, dest))
                    set_attr(aNamespace, dest, self.attr("_defaults")[dest]);
            py result;
            if (self.attr("exit_on_error").truthy())
            {
                try
                {
                    result = self.method("_parse_known_args", { aArgs, aNamespace, aIntermixed });
                }
                catch (raised const&)
                {
                    py err;
                    if (!caught_instance(cls("ArgumentError"), err))
                        throw;
                    self.method("error", { builtin("str")(err) });
                    // (an error that returns (a subclass's): the namespace and the arguments as they were)
                    result = py_tuple({ aNamespace, aArgs });
                }
            }
            else
                result = self.method("_parse_known_args", { aArgs, aNamespace, aIntermixed });
            aNamespace = result[py{ 0 }];
            aArgs = result[py{ 1 }];
            if (has_attr(aNamespace, S("_unrecognized_args")))
            {
                aArgs.method("extend", { get_attr(aNamespace, S("_unrecognized_args")) });
                builtin("delattr")(aNamespace, S("_unrecognized_args"));
            }
            return py_tuple({ aNamespace, aArgs });
        }

        py parse_known_args_impl(py const& self, py argStrings, py const& aNamespace, bool aIntermixed)
        {
            auto const& s = state();
            if (!self.attr("fromfile_prefix_chars").is_none())
                argStrings = self.method("_read_args_from_files", { argStrings });
            // (map all mutually exclusive arguments to the other arguments they can't occur with)
            py const actionConflicts = py_dict();
            for (auto const& mutexGroup : self.attr("_mutually_exclusive_groups").items())
            {
                py const groupActions = mutexGroup.attr("_group_actions");
                long long i = 0;
                for (auto const& mutexAction : groupActions.items())
                {
                    py const conflicts = actionConflicts.method("setdefault", { mutexAction, py_list() });
                    conflicts.method("extend", { slice(groupActions, std::nullopt, i) });
                    conflicts.method("extend", { slice(groupActions, i + 1) });
                    ++i;
                }
            }
            // (find all option indices, and determine the arg_string_pattern)
            py const optionStringIndices = py_dict();
            py const argStringPatternParts = py_list();
            {
                auto const strings = argStrings.items();
                for (std::size_t i = 0u; i < strings.size(); ++i)
                {
                    auto const& argString = strings[i];
                    if (argString == S("--"))
                    {
                        argStringPatternParts.method("append", { S("-") });
                        for (++i; i < strings.size(); ++i)
                            argStringPatternParts.method("append", { S("A") });
                        break;
                    }
                    py const optionTuples = self.method("_parse_optional", { argString });
                    py pattern;
                    if (optionTuples.is_none())
                        pattern = S("A");
                    else
                    {
                        optionStringIndices.set_item(py{ i }, optionTuples);
                        pattern = S("O");
                    }
                    argStringPatternParts.method("append", { pattern });
                }
            }
            py argStringsPattern = join("", argStringPatternParts);

            // (converts arg strings to the appropriate and then takes the action)
            py const seenActions = builtin("set")();
            py const seenNonDefaultActions = builtin("set")();
            py const warned = builtin("set")();

            auto const takeAction = [&](py const& aAction, py const& aArgumentStrings, py const& aOptionString)
            {
                seenActions.method("add", { aAction });
                py const argumentValues = self.method("_get_values", { aAction, aArgumentStrings });
                if (aAction.attr("option_strings").truthy() || aArgumentStrings.truthy())
                {
                    seenNonDefaultActions.method("add", { aAction });
                    for (auto const& conflictAction : actionConflicts.method("get", { aAction, py_list() }).items())
                        if (seenNonDefaultActions.contains(conflictAction))
                        {
                            py const msg = _("not allowed with argument %s");
                            py const actionName = get_action_name(conflictAction);
                            raise_argument_error(aAction, msg % actionName);
                        }
                }
                if (!argumentValues.is(s.SUPPRESS))
                    aAction(self, aNamespace, argumentValues, aOptionString);
            };

            py extras = py_list();
            py extrasPattern = py_list();

            // (function to convert arg_strings into an optional action)
            auto const consumeOptional = [&](long long aStartIndex) -> long long
            {
                py const optionTuples = optionStringIndices[py{ aStartIndex }];
                if (optionTuples.size() > 1)
                {
                    py const strings = py_list();
                    for (auto const& t : optionTuples.items())
                        strings.method("append", { t[py{ 1 }] });
                    py const args = dict_of({ { "option", argStrings[py{ aStartIndex }] }, { "matches", join(", ", strings) } });
                    raise_argument_error(none(), _("ambiguous option: %(option)s could match %(matches)s") % args);
                }
                auto const first = optionTuples[py{ 0 }].items();
                py action = first.at(0);
                py optionString = first.at(1);
                py sep = first.at(2);
                py explicitArg = first.at(3);
                py const matchArgument = self.attr("_match_argument");
                std::vector<std::tuple<py, py, py>> actionTuples;
                long long stop = 0;
                for (;;)
                {
                    if (action.is_none())
                    {
                        extras.method("append", { argStrings[py{ aStartIndex }] });
                        extrasPattern.method("append", { S("O") });
                        return aStartIndex + 1;
                    }
                    if (!explicitArg.is_none())
                    {
                        long long const argCount = matchArgument(action, S("A")).as_index();
                        py const chars = self.attr("prefix_chars");
                        if (argCount == 0 && !chars.contains(optionString[py{ 1 }]) && explicitArg != S(""))
                        {
                            if (sep.truthy() || chars.contains(explicitArg[py{ 0 }]))
                                raise_argument_error(action, _("ignored explicit argument %r") % explicitArg);
                            actionTuples.emplace_back(action, py_list(), optionString);
                            py const charPrefix = optionString[py{ 0 }];
                            optionString = charPrefix + explicitArg[py{ 0 }];
                            py const optionalsMap = self.attr("_option_string_actions");
                            if (optionalsMap.contains(optionString))
                            {
                                action = optionalsMap[optionString];
                                explicitArg = slice(explicitArg, 1);
                                if (!explicitArg.truthy())
                                    sep = explicitArg = none();
                                else if (explicitArg[py{ 0 }] == S("="))
                                {
                                    sep = S("=");
                                    explicitArg = slice(explicitArg, 1);
                                }
                                else
                                    sep = S("");
                            }
                            else
                            {
                                extras.method("append", { charPrefix + explicitArg });
                                extrasPattern.method("append", { S("O") });
                                stop = aStartIndex + 1;
                                break;
                            }
                        }
                        else if (argCount == 1)
                        {
                            stop = aStartIndex + 1;
                            actionTuples.emplace_back(action, py_list({ explicitArg }), optionString);
                            break;
                        }
                        else
                            raise_argument_error(action, _("ignored explicit argument %r") % explicitArg);
                    }
                    else
                    {
                        long long const start = aStartIndex + 1;
                        py const selectedPatterns = slice(argStringsPattern, start);
                        long long const argCount = matchArgument(action, selectedPatterns).as_index();
                        stop = start + argCount;
                        py const args = slice(argStrings, start, stop);
                        actionTuples.emplace_back(action, args, optionString);
                        break;
                    }
                }
                if (actionTuples.empty())
                    py_raise("AssertionError", "");
                for (auto const& [tupleAction, args, tupleOptionString] : actionTuples)
                {
                    if (tupleAction.attr("deprecated").truthy() && !warned.contains(tupleOptionString))
                    {
                        self.method("_warning", { _("option '%(option)s' is deprecated") % dict_of({ { "option", tupleOptionString } }) });
                        warned.method("add", { tupleOptionString });
                    }
                    takeAction(tupleAction, args, tupleOptionString);
                }
                return stop;
            };

            // (the list of Positionals left to be parsed; this is modified by consume_positionals())
            py const positionals = self.method("_get_positional_actions");

            auto const consumePositionals = [&](long long aStartIndex) -> long long
            {
                py const matchPartial = self.attr("_match_arguments_partial");
                py const selectedPattern = slice(argStringsPattern, aStartIndex);
                py const argCounts = matchPartial(positionals, selectedPattern);
                auto const positionalItems = positionals.items();
                auto const counts = argCounts.items();
                for (std::size_t i = 0u; i < positionalItems.size() && i < counts.size(); ++i)
                {
                    auto const& action = positionalItems[i];
                    long long const argCount = counts[i].as_index();
                    py const args = slice(argStrings, aStartIndex, aStartIndex + argCount);
                    if (action.attr("nargs") == s.PARSER)
                    {
                        if (argStringsPattern[py{ aStartIndex }] == S("-"))
                        {
                            if (!(args[py{ 0 }] == S("--")))
                                py_raise("AssertionError", "");
                            args.method("remove", { S("--") });
                        }
                    }
                    else if (action.attr("nargs") != s.REMAINDER)
                    {
                        if (argStringsPattern.method("find", { S("-"), py{ aStartIndex }, py{ aStartIndex + argCount } }) >= py{ 0 })
                            args.method("remove", { S("--") });
                    }
                    aStartIndex += argCount;
                    if (args.truthy() && action.attr("deprecated").truthy() && !warned.contains(action.attr("dest")))
                    {
                        self.method("_warning", { _("argument '%(argument_name)s' is deprecated") % dict_of({ { "argument_name", action.attr("dest") } }) });
                        warned.method("add", { action.attr("dest") });
                    }
                    takeAction(action, args, none());
                }
                positionals.set_item(py{ make_slice(none().get(), none().get(), nullptr) }, slice(positionals, argCounts.size()));
                return aStartIndex;
            };

            // (consume Positionals and Optionals alternately, until we have passed the last option string)
            long long startIndex = 0;
            long long const maxOptionStringIndex = optionStringIndices.truthy() ? builtin("max")(optionStringIndices).as_index() : -1;
            while (startIndex <= maxOptionStringIndex)
            {
                long long nextOptionStringIndex = startIndex;
                while (nextOptionStringIndex <= maxOptionStringIndex)
                {
                    if (optionStringIndices.contains(py{ nextOptionStringIndex }))
                        break;
                    ++nextOptionStringIndex;
                }
                if (!aIntermixed && startIndex != nextOptionStringIndex)
                {
                    long long const positionalsEndIndex = consumePositionals(startIndex);
                    if (positionalsEndIndex > startIndex)
                    {
                        startIndex = positionalsEndIndex;
                        continue;
                    }
                    startIndex = positionalsEndIndex;
                }
                if (!optionStringIndices.contains(py{ startIndex }))
                {
                    py const strings = slice(argStrings, startIndex, nextOptionStringIndex);
                    extras.method("extend", { strings });
                    extrasPattern.method("extend", { slice(argStringsPattern, startIndex, nextOptionStringIndex) });
                    startIndex = nextOptionStringIndex;
                }
                startIndex = consumeOptional(startIndex);
            }

            if (!aIntermixed)
            {
                long long const stopIndex = consumePositionals(startIndex);
                extras.method("extend", { slice(argStrings, stopIndex) });
            }
            else
            {
                extras.method("extend", { slice(argStrings, startIndex) });
                extrasPattern.method("extend", { slice(argStringsPattern, startIndex) });
                py const pattern = join("", extrasPattern);
                if (pattern.size() != extras.size())
                    py_raise("AssertionError", "");
                py const strings = py_list();
                {
                    auto const extraItems = extras.items();
                    auto const patternItems = pattern.items();
                    for (std::size_t i = 0u; i < extraItems.size(); ++i)
                        if (patternItems[i] != S("O"))
                            strings.method("append", { extraItems[i] });
                }
                argStrings = strings;
                argStringsPattern = pattern.method("replace", { S("O"), S("") });
                long long stopIndex = consumePositionals(0);
                {
                    long long i = 0;
                    for (auto const& c : pattern.items())
                    {
                        if (stopIndex == 0)
                            break;
                        if (c != S("O"))
                        {
                            --stopIndex;
                            extras.set_item(py{ i }, none());
                        }
                        ++i;
                    }
                }
                py const remaining = py_list();
                for (auto const& e : extras.items())
                    if (!e.is_none())
                        remaining.method("append", { e });
                extras = remaining;
            }

            // (make sure all required actions were present and also convert action defaults which were not given as arguments)
            py const requiredActions = py_list();
            for (auto const& action : self.attr("_actions").items())
                if (!seenActions.contains(action))
                {
                    if (action.attr("required").truthy())
                        requiredActions.method("append", { get_action_name(action) });
                    else
                    {
                        py const default_ = action.attr("default");
                        if (!default_.is_none() && is_str(default_) && has_attr(aNamespace, action.attr("dest")) &&
                            default_.is(get_attr(aNamespace, action.attr("dest"))))
                            set_attr(aNamespace, action.attr("dest"), self.method("_get_value", { action, default_ }));
                    }
                }
            if (requiredActions.truthy())
                raise_argument_error(none(), _("the following arguments are required: %s") % join(", ", requiredActions));
            for (auto const& group : self.attr("_mutually_exclusive_groups").items())
                if (group.attr("required").truthy())
                {
                    bool found = false;
                    for (auto const& action : group.attr("_group_actions").items())
                        if (seenNonDefaultActions.contains(action))
                        {
                            found = true;
                            break;
                        }
                    if (!found)
                    {
                        py const names = py_list();
                        for (auto const& action : group.attr("_group_actions").items())
                            if (!action.attr("help").is(s.SUPPRESS))
                                names.method("append", { get_action_name(action) });
                        raise_argument_error(none(), _("one of the arguments %s is required") % join(" ", names));
                    }
                }
            return py_tuple({ aNamespace, extras });
        }

        py read_args_from_files(py const& self, py const& aArgStrings)
        {
            py const newArgStrings = py_list();
            for (auto const& argString : aArgStrings.items())
            {
                if (!argString.truthy() || !self.attr("fromfile_prefix_chars").contains(argString[py{ 0 }]))
                    newArgStrings.method("append", { argString });
                else
                {
                    try
                    {
                        py const argsFile = builtin("open").call({ slice(argString, 1) }, { { "encoding", sys_attr("getfilesystemencoding")() },
                            { "errors", sys_attr("getfilesystemencodeerrors")() } });
                        py content;
                        try
                        {
                            content = argsFile.method("read");
                        }
                        catch (raised const&)
                        {
                            py const e = py_take_exception();
                            argsFile.method("close");
                            py_reraise(e);
                        }
                        argsFile.method("close");
                        py argStrings = py_list();
                        for (auto const& argLine : content.method("splitlines").items())
                            for (auto const& arg : self.method("convert_arg_line_to_args", { argLine }).items())
                                argStrings.method("append", { arg });
                        argStrings = self.method("_read_args_from_files", { argStrings });
                        newArgStrings.method("extend", { argStrings });
                    }
                    catch (raised const&)
                    {
                        py err;
                        if (!caught_instance(builtin("OSError"), err))
                            throw;
                        raise_argument_error(none(), builtin("str")(err));
                    }
                }
            }
            return newArgStrings;
        }

        py nargs_pattern(py const& aAction)
        {
            auto const& s = state();
            py const nargs = aAction.attr("nargs");
            bool const option = aAction.attr("option_strings").truthy();
            if (nargs.is_none())
                return S(option ? "([A])" : "(-*A-*)");
            if (nargs == s.OPTIONAL)
                return S(option ? "(A?)" : "(-*A?-*)");
            if (nargs == s.ZERO_OR_MORE)
                return S(option ? "(A*)" : "(-*[A-]*)");
            if (nargs == s.ONE_OR_MORE)
                return S(option ? "(A+)" : "(-*A[A-]*)");
            if (nargs == s.REMAINDER)
                return S(option ? "([AO]*)" : "(.*)");
            if (nargs == s.PARSER)
                return S(option ? "(A[AO]*)" : "(-*A[-AO]*)");
            if (nargs == s.SUPPRESS)
                return S(option ? "()" : "(-*)");
            return option ? S("([AO]{%d})") % nargs : S("((?:-*A){%d}-*)") % nargs;
        }

        py get_values_impl(py const& self, py const& aAction, py const& aArgStrings)
        {
            auto const& s = state();
            py const nargs = aAction.attr("nargs");
            py value;
            if (!aArgStrings.truthy() && nargs == s.OPTIONAL)
            {
                value = aAction.attr("option_strings").truthy() ? aAction.attr("const") : aAction.attr("default");
                if (is_str(value) && !value.is(s.SUPPRESS))
                {
                    value = self.method("_get_value", { aAction, value });
                    self.method("_check_value", { aAction, value });
                }
            }
            else if (!aArgStrings.truthy() && nargs == s.ZERO_OR_MORE && !aAction.attr("option_strings").truthy())
            {
                if (!aAction.attr("default").is_none())
                {
                    value = aAction.attr("default");
                    self.method("_check_value", { aAction, value });
                }
                else
                    value = aArgStrings;
            }
            else if (aArgStrings.size() == 1 && (nargs.is_none() || nargs == s.OPTIONAL))
            {
                py const argString = aArgStrings[py{ 0 }];
                value = self.method("_get_value", { aAction, argString });
                self.method("_check_value", { aAction, value });
            }
            else if (nargs == s.REMAINDER)
            {
                value = py_list();
                for (auto const& v : aArgStrings.items())
                    value.method("append", { self.method("_get_value", { aAction, v }) });
            }
            else if (nargs == s.PARSER)
            {
                value = py_list();
                for (auto const& v : aArgStrings.items())
                    value.method("append", { self.method("_get_value", { aAction, v }) });
                self.method("_check_value", { aAction, value[py{ 0 }] });
            }
            else if (nargs == s.SUPPRESS)
                value = s.SUPPRESS;
            else
            {
                value = py_list();
                for (auto const& v : aArgStrings.items())
                    value.method("append", { self.method("_get_value", { aAction, v }) });
                for (auto const& v : value.items())
                    self.method("_check_value", { aAction, v });
            }
            return value;
        }

        py get_value_impl(py const& self, py const& aAction, py const& aArgString)
        {
            py const typeFunc = self.method("_registry_get", { S("type"), aAction.attr("type"), aAction.attr("type") });
            if (!callable(typeFunc))
                raise_argument_error(aAction, _("%r is not callable") % py_tuple({ typeFunc }));
            try
            {
                return typeFunc(aArgString);
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                if (isinstance(e, cls("ArgumentTypeError")))
                    raise_argument_error(aAction, builtin("str")(e));
                if (isinstance(e, builtin("TypeError")) || isinstance(e, builtin("ValueError")))
                {
                    py const name = get_attr(aAction.attr("type"), S("__name__"), builtin("repr")(aAction.attr("type")));
                    py const args = dict_of({ { "type", name }, { "value", aArgString } });
                    raise_argument_error(aAction, _("invalid %(type)s value: %(value)r") % args);
                }
                py_reraise(e);
            }
        }

        void check_value_impl(py const& aAction, py const& aValue)
        {
            py choices = aAction.attr("choices");
            if (!choices.is_none())
            {
                if (is_str(choices))
                    choices = builtin("iter")(choices);
                if (!choices.contains(aValue))
                {
                    py const reprs = py_list();
                    for (auto const& choice : aAction.attr("choices").items())
                        reprs.method("append", { builtin("repr")(builtin("str")(choice)) });
                    py const args = dict_of({ { "value", builtin("str")(aValue) }, { "choices", join(", ", reprs) } });
                    raise_argument_error(aAction, _("invalid choice: %(value)r (choose from %(choices)s)") % args);
                }
            }
        }

        void make_argument_parser()
        {
            auto& s = state();
            make_argparse_class("ArgumentParser", { cls("_AttributeHolder"), cls("_ActionsContainer") }, {
                { "__init__", method("ArgumentParser", "__init__", { py_positional("prog", none()), py_positional("usage", none()),
                    py_positional("description", none()), py_positional("epilog", none()), py_positional("parents", py_list()),
                    py_positional("formatter_class", cls("HelpFormatter")), py_positional("prefix_chars", S("-")), py_positional("fromfile_prefix_chars", none()),
                    py_positional("argument_default", none()), py_positional("conflict_handler", S("error")), py_positional("add_help", boolean(true)),
                    py_positional("allow_abbrev", boolean(true)), py_positional("exit_on_error", boolean(true)) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py prog = a[1];
                        py const prefixChars = a[7];
                        call_kw(super_of("ArgumentParser", self).attr("__init__"), {}, dict_of({ { "description", a[3] }, { "prefix_chars", prefixChars },
                            { "argument_default", a[9] }, { "conflict_handler", a[10] } }));
                        if (prog.is_none())
                            prog = py{ fn_import("os") }.attr("path").attr("basename")(sys_attr("argv")[py{ 0 }]);
                        self.set_attr("prog", prog);
                        self.set_attr("usage", a[2]);
                        self.set_attr("epilog", a[4]);
                        self.set_attr("formatter_class", a[6]);
                        self.set_attr("fromfile_prefix_chars", a[8]);
                        self.set_attr("add_help", a[11]);
                        self.set_attr("allow_abbrev", a[12]);
                        self.set_attr("exit_on_error", a[13]);
                        py const addGroup = self.attr("add_argument_group");
                        self.set_attr("_positionals", addGroup(_("positional arguments")));
                        self.set_attr("_optionals", addGroup(_("options")));
                        self.set_attr("_subparsers", none());
                        py const identity = function("identity", "ArgumentParser.__init__.<locals>.identity", [](std::vector<py> const& aArgs, object* aKwargs)
                        {
                            return py_bind("ArgumentParser.__init__.<locals>.identity", { py_positional("string") }, aArgs, aKwargs)[0];
                        });
                        self.method("register", { S("type"), none(), identity });
                        py const defaultPrefix = prefixChars.contains(S("-")) ? S("-") : prefixChars[py{ 0 }];
                        if (self.attr("add_help").truthy())
                            self.method("add_argument", { defaultPrefix + S("h"), defaultPrefix * py{ 2 } + S("help") }, { { "action", S("help") },
                                { "default", state().SUPPRESS }, { "help", _("show this help message and exit") } });
                        for (auto const& parent : a[5].items())
                        {
                            if (!isinstance(parent, cls("ArgumentParser")))
                                py_raise("TypeError", "parents must be a list of ArgumentParser");
                            self.method("_add_container_actions", { parent });
                            py const defaults = parent.attr("_defaults");
                            self.attr("_defaults").method("update", { defaults });
                        }
                        return none();
                    }) },
                // (pretty __repr__ methods)
                { "_get_kwargs", method("ArgumentParser", "_get_kwargs", {}, [](std::vector<py> const& a)
                    {
                        py const result = py_list();
                        for (auto name : { "prog", "usage", "description", "formatter_class", "conflict_handler", "add_help" })
                            result.method("append", { py_tuple({ S(name), a[0].attr(name) }) });
                        return result;
                    }) },
                // (optional/positional adding methods)
                { "add_subparsers", method("ArgumentParser", "add_subparsers", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const kwargs = a[1];
                        if (!self.attr("_subparsers").is_none())
                            raise_argument_error(none(), _("cannot have multiple subparser arguments"));
                        kwargs.method("setdefault", { S("parser_class"), type_of(self) });
                        if (kwargs.contains(S("title")) || kwargs.contains(S("description")))
                        {
                            py const title = kwargs.method("pop", { S("title"), _("subcommands") });
                            py const description = kwargs.method("pop", { S("description"), none() });
                            self.set_attr("_subparsers", self.method("add_argument_group", { title, description }));
                        }
                        else
                            self.set_attr("_subparsers", self.attr("_positionals"));
                        if (kwargs.method("get", { S("prog") }).is_none())
                        {
                            py const formatter = self.method("_get_formatter");
                            py const positionals = self.method("_get_positional_actions");
                            py const groups = self.attr("_mutually_exclusive_groups");
                            formatter.method("add_usage", { self.attr("usage"), positionals, groups, S("") });
                            kwargs.set_item(S("prog"), formatter.method("format_help").method("strip"));
                        }
                        py const parsersClass = self.method("_pop_action_class", { kwargs, S("parsers") });
                        py const allKwargs = dict_of({ { "option_strings", py_list() } });
                        allKwargs.method("update", { kwargs });
                        py const action = call_kw(parsersClass, {}, allKwargs);
                        self.attr("_subparsers").method("_add_action", { action });
                        return action;
                    }, false, true) },
                { "_add_action", method("ArgumentParser", "_add_action", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        if (a[1].attr("option_strings").truthy())
                            a[0].attr("_optionals").method("_add_action", { a[1] });
                        else
                            a[0].attr("_positionals").method("_add_action", { a[1] });
                        return a[1];
                    }) },
                { "_get_optional_actions", method("ArgumentParser", "_get_optional_actions", {}, [](std::vector<py> const& a)
                    {
                        py const result = py_list();
                        for (auto const& action : a[0].attr("_actions").items())
                            if (action.attr("option_strings").truthy())
                                result.method("append", { action });
                        return result;
                    }) },
                { "_get_positional_actions", method("ArgumentParser", "_get_positional_actions", {}, [](std::vector<py> const& a)
                    {
                        py const result = py_list();
                        for (auto const& action : a[0].attr("_actions").items())
                            if (!action.attr("option_strings").truthy())
                                result.method("append", { action });
                        return result;
                    }) },
                // (command line argument parsing methods)
                { "parse_args", method("ArgumentParser", "parse_args", { py_positional("args", none()), py_positional("namespace", none()) },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const result = self.method("parse_known_args", { a[1], a[2] });
                        py const argv = result[py{ 1 }];
                        if (argv.truthy())
                        {
                            py const msg = _("unrecognized arguments: %s") % join(" ", argv);
                            if (self.attr("exit_on_error").truthy())
                                self.method("error", { msg });
                            else
                                raise_argument_error(none(), msg);
                        }
                        return result[py{ 0 }];
                    }) },
                { "parse_known_args", method("ArgumentParser", "parse_known_args", { py_positional("args", none()), py_positional("namespace", none()) },
                    [](std::vector<py> const& a)
                    {
                        return a[0].method("_parse_known_args2", { a[1], a[2] }, { { "intermixed", boolean(false) } });
                    }) },
                { "_parse_known_args2", method("ArgumentParser", "_parse_known_args2", { py_positional("args"), py_positional("namespace"),
                    py_positional("intermixed") }, [](std::vector<py> const& a) { return parse_known_args2(a[0], a[1], a[2], a[3]); }) },
                { "_parse_known_args", method("ArgumentParser", "_parse_known_args", { py_positional("arg_strings"), py_positional("namespace"),
                    py_positional("intermixed") }, [](std::vector<py> const& a) { return parse_known_args_impl(a[0], a[1], a[2], a[3].truthy()); }) },
                { "_read_args_from_files", method("ArgumentParser", "_read_args_from_files", { py_positional("arg_strings") }, [](std::vector<py> const& a)
                    {
                        return read_args_from_files(a[0], a[1]);
                    }) },
                { "convert_arg_line_to_args", method("ArgumentParser", "convert_arg_line_to_args", { py_positional("arg_line") }, [](std::vector<py> const& a)
                    {
                        return py_list({ a[1] });
                    }) },
                { "_match_argument", method("ArgumentParser", "_match_argument", { py_positional("action"), py_positional("arg_strings_pattern") },
                    [](std::vector<py> const& a)
                    {
                        auto const& st = state();
                        auto const& action = a[1];
                        py const nargsPattern = a[0].method("_get_nargs_pattern", { action });
                        py const match = import_attribute("re", "match")(nargsPattern, a[2]);
                        if (match.is_none())
                        {
                            py const nargsErrors = py_dict();
                            nargsErrors.set_item(none(), _("expected one argument"));
                            nargsErrors.set_item(st.OPTIONAL, _("expected at most one argument"));
                            nargsErrors.set_item(st.ONE_OR_MORE, _("expected at least one argument"));
                            py msg = nargsErrors.method("get", { action.attr("nargs") });
                            if (msg.is_none())
                                msg = ngettext("expected %s argument", "expected %s arguments", action.attr("nargs")) % action.attr("nargs");
                            raise_argument_error(action, msg);
                        }
                        return py{ match.method("group", { py{ 1 } }).size() };
                    }) },
                { "_match_arguments_partial", method("ArgumentParser", "_match_arguments_partial", { py_positional("actions"), py_positional("arg_strings_pattern") },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& argStringsPattern = a[2];
                        for (long long i = a[1].size(); i > 0; --i)
                        {
                            py const actionsSlice = slice(a[1], std::nullopt, i);
                            py const patterns = py_list();
                            for (auto const& action : actionsSlice.items())
                                patterns.method("append", { self.method("_get_nargs_pattern", { action }) });
                            py const pattern = join("", patterns);
                            py const match = import_attribute("re", "match")(pattern, argStringsPattern);
                            if (!match.is_none())
                            {
                                py const result = py_list();
                                for (auto const& string : match.method("groups").items())
                                    result.method("append", { py{ string.size() } });
                                py const end = match.method("end");
                                if (end < py{ argStringsPattern.size() } && argStringsPattern[end] == S("O"))
                                    while (result.truthy() && !result[py{ -1 }].truthy())
                                        result.method("pop");
                                return result;
                            }
                        }
                        return py_list();
                    }) },
                { "_parse_optional", method("ArgumentParser", "_parse_optional", { py_positional("arg_string") }, [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        auto const& argString = a[1];
                        if (!argString.truthy())
                            return none();
                        if (!self.attr("prefix_chars").contains(argString[py{ 0 }]))
                            return none();
                        py const optionStringActions = self.attr("_option_string_actions");
                        if (optionStringActions.contains(argString))
                            return py_list({ py_tuple({ optionStringActions[argString], argString, none(), none() }) });
                        if (argString.size() == 1)
                            return none();
                        py const parts = argString.method("partition", { S("=") });
                        py const optionString = parts[py{ 0 }];
                        py const sep = parts[py{ 1 }];
                        py const explicitArg = parts[py{ 2 }];
                        if (sep.truthy() && optionStringActions.contains(optionString))
                            return py_list({ py_tuple({ optionStringActions[optionString], optionString, sep, explicitArg }) });
                        py const optionTuples = self.method("_get_option_tuples", { argString });
                        if (optionTuples.truthy())
                            return optionTuples;
                        if (self.attr("_negative_number_matcher").method("match", { argString }).truthy())
                            if (!self.attr("_has_negative_number_optionals").truthy())
                                return none();
                        if (argString.contains(S(" ")))
                            return none();
                        return py_list({ py_tuple({ none(), argString, none(), none() }) });
                    }) },
                { "_get_option_tuples", method("ArgumentParser", "_get_option_tuples", { py_positional("option_string") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const optionString = a[1];
                        py const result = py_list();
                        py const chars = self.attr("prefix_chars");
                        py const optionStringActions = self.attr("_option_string_actions");
                        if (chars.contains(optionString[py{ 0 }]) && chars.contains(optionString[py{ 1 }]))
                        {
                            if (self.attr("allow_abbrev").truthy())
                            {
                                py const parts = optionString.method("partition", { S("=") });
                                py const optionPrefix = parts[py{ 0 }];
                                py sep = parts[py{ 1 }];
                                py explicitArg = parts[py{ 2 }];
                                if (!sep.truthy())
                                    sep = explicitArg = none();
                                for (auto const& candidate : optionStringActions.items())
                                    if (candidate.method("startswith", { optionPrefix }).truthy())
                                        result.method("append", { py_tuple({ optionStringActions[candidate], candidate, sep, explicitArg }) });
                            }
                        }
                        else if (chars.contains(optionString[py{ 0 }]) && !chars.contains(optionString[py{ 1 }]))
                        {
                            py const parts = optionString.method("partition", { S("=") });
                            py const optionPrefix = parts[py{ 0 }];
                            py sep = parts[py{ 1 }];
                            py explicitArg = parts[py{ 2 }];
                            if (!sep.truthy())
                                sep = explicitArg = none();
                            py const shortOptionPrefix = slice(optionString, std::nullopt, 2);
                            py const shortExplicitArg = slice(optionString, 2);
                            for (auto const& candidate : optionStringActions.items())
                            {
                                if (candidate == shortOptionPrefix)
                                    result.method("append", { py_tuple({ optionStringActions[candidate], candidate, S(""), shortExplicitArg }) });
                                else if (self.attr("allow_abbrev").truthy() && candidate.method("startswith", { optionPrefix }).truthy())
                                    result.method("append", { py_tuple({ optionStringActions[candidate], candidate, sep, explicitArg }) });
                            }
                        }
                        else
                            raise_argument_error(none(), _("unexpected option string: %s") % optionString);
                        return result;
                    }) },
                { "_get_nargs_pattern", method("ArgumentParser", "_get_nargs_pattern", { py_positional("action") }, [](std::vector<py> const& a)
                    {
                        return nargs_pattern(a[1]);
                    }) },
                // (alt command line argument parsing, allowing free intermix)
                { "parse_intermixed_args", method("ArgumentParser", "parse_intermixed_args", { py_positional("args", none()), py_positional("namespace", none()) },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const result = self.method("parse_known_intermixed_args", { a[1], a[2] });
                        py const argv = result[py{ 1 }];
                        if (argv.truthy())
                        {
                            py const msg = _("unrecognized arguments: %s") % join(" ", argv);
                            if (self.attr("exit_on_error").truthy())
                                self.method("error", { msg });
                            else
                                raise_argument_error(none(), msg);
                        }
                        return result[py{ 0 }];
                    }) },
                { "parse_known_intermixed_args", method("ArgumentParser", "parse_known_intermixed_args", { py_positional("args", none()),
                    py_positional("namespace", none()) }, [](std::vector<py> const& a)
                    {
                        auto const& st = state();
                        auto const& self = a[0];
                        for (auto const& action : self.method("_get_positional_actions").items())
                            if (py_list({ st.PARSER, st.REMAINDER }).contains(action.attr("nargs")))
                                py_raise("TypeError", (S("parse_intermixed_args: positional arg with nargs=%s") % py_tuple({ action.attr("nargs") })).str());
                        return self.method("_parse_known_args2", { a[1], a[2] }, { { "intermixed", boolean(true) } });
                    }) },
                // (value conversion methods)
                { "_get_values", method("ArgumentParser", "_get_values", { py_positional("action"), py_positional("arg_strings") }, [](std::vector<py> const& a)
                    {
                        return get_values_impl(a[0], a[1], a[2]);
                    }) },
                { "_get_value", method("ArgumentParser", "_get_value", { py_positional("action"), py_positional("arg_string") }, [](std::vector<py> const& a)
                    {
                        return get_value_impl(a[0], a[1], a[2]);
                    }) },
                { "_check_value", method("ArgumentParser", "_check_value", { py_positional("action"), py_positional("value") }, [](std::vector<py> const& a)
                    {
                        check_value_impl(a[1], a[2]);
                        return none();
                    }) },
                // (help-formatting methods)
                { "format_usage", method("ArgumentParser", "format_usage", {}, [](std::vector<py> const& a)
                    {
                        py const formatter = a[0].method("_get_formatter");
                        formatter.method("add_usage", { a[0].attr("usage"), a[0].attr("_actions"), a[0].attr("_mutually_exclusive_groups") });
                        return formatter.method("format_help");
                    }) },
                { "format_help", method("ArgumentParser", "format_help", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const formatter = self.method("_get_formatter");
                        formatter.method("add_usage", { self.attr("usage"), self.attr("_actions"), self.attr("_mutually_exclusive_groups") });
                        formatter.method("add_text", { self.attr("description") });
                        for (auto const& actionGroup : self.attr("_action_groups").items())
                        {
                            formatter.method("start_section", { actionGroup.attr("title") });
                            formatter.method("add_text", { actionGroup.attr("description") });
                            formatter.method("add_arguments", { actionGroup.attr("_group_actions") });
                            formatter.method("end_section");
                        }
                        formatter.method("add_text", { self.attr("epilog") });
                        return formatter.method("format_help");
                    }) },
                { "_get_formatter", method("ArgumentParser", "_get_formatter", {}, [](std::vector<py> const& a)
                    {
                        return a[0].attr("formatter_class").call({}, { { "prog", a[0].attr("prog") } });
                    }) },
                // (help-printing methods)
                { "print_usage", method("ArgumentParser", "print_usage", { py_positional("file", none()) }, [](std::vector<py> const& a)
                    {
                        py const file = a[1].is_none() ? sys_attr("stdout") : a[1];
                        a[0].method("_print_message", { a[0].method("format_usage"), file });
                        return none();
                    }) },
                { "print_help", method("ArgumentParser", "print_help", { py_positional("file", none()) }, [](std::vector<py> const& a)
                    {
                        py const file = a[1].is_none() ? sys_attr("stdout") : a[1];
                        a[0].method("_print_message", { a[0].method("format_help"), file });
                        return none();
                    }) },
                { "_print_message", method("ArgumentParser", "_print_message", { py_positional("message"), py_positional("file", none()) },
                    [](std::vector<py> const& a)
                    {
                        if (a[1].truthy())
                        {
                            py const file = a[2].truthy() ? a[2] : sys_attr("stderr");
                            try
                            {
                                file.method("write", { a[1] });
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "AttributeError", "OSError" }))
                                    throw;
                            }
                        }
                        return none();
                    }) },
                // (exiting methods)
                { "exit", method("ArgumentParser", "exit", { py_positional("status", py{ 0 }), py_positional("message", none()) }, [](std::vector<py> const& a)
                    {
                        if (a[2].truthy())
                            a[0].method("_print_message", { a[2], sys_attr("stderr") });
                        sys_attr("exit")(a[1]);
                        return none();
                    }) },
                { "error", method("ArgumentParser", "error", { py_positional("message") }, [](std::vector<py> const& a)
                    {
                        a[0].method("print_usage", { sys_attr("stderr") });
                        py const args = dict_of({ { "prog", a[0].attr("prog") }, { "message", a[1] } });
                        a[0].method("exit", { py{ 2 }, _("%(prog)s: error: %(message)s\n") % args });
                        return none();
                    }, false, false, "error(message: string)\n\n        Prints a usage message incorporating the message to stderr and\n        exits.\n\n"
                    "        If you override this in a subclass, it should not return -- it\n        should either exit or raise an exception.\n        ") },
                { "_warning", method("ArgumentParser", "_warning", { py_positional("message") }, [](std::vector<py> const& a)
                    {
                        py const args = dict_of({ { "prog", a[0].attr("prog") }, { "message", a[1] } });
                        a[0].method("_print_message", { _("%(prog)s: warning: %(message)s\n") % args, sys_attr("stderr") });
                        return none();
                    }) } },
                "Object for parsing command line strings into Python objects.\n\n    Keyword Arguments:\n        - prog -- The name of the program (default:\n"
                "            ``os.path.basename(sys.argv[0])``)\n        - usage -- A usage message (default: auto-generated from arguments)\n"
                "        - description -- A description of what the program does\n        - epilog -- Text following the argument descriptions\n"
                "        - parents -- Parsers whose arguments should be copied into this one\n        - formatter_class -- HelpFormatter class for printing help messages\n"
                "        - prefix_chars -- Characters that prefix optional arguments\n        - fromfile_prefix_chars -- Characters that prefix files containing\n"
                "            additional arguments\n        - argument_default -- The default value for all arguments\n"
                "        - conflict_handler -- String indicating how to handle conflicts\n        - add_help -- Add a -h/-help option\n"
                "        - allow_abbrev -- Allow long options to be abbreviated unambiguously\n        - exit_on_error -- Determines whether or not ArgumentParser exits with\n"
                "            error info when an error occurs\n    ");
            (void)s;
        }

        // --- the module

        bool argparse_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (!s.made)
                {
                    s.made = true;
                    s.SUPPRESS = S("==SUPPRESS==");
                    s.OPTIONAL = S("?");
                    s.ZERO_OR_MORE = S("*");
                    s.ONE_OR_MORE = S("+");
                    s.PARSER = S("A...");
                    s.REMAINDER = S("...");
                    make_attribute_holder();
                    make_help_formatters();
                    make_exceptions();
                    make_actions();
                    make_file_type_and_namespace();
                    make_containers();
                    make_argument_parser();
                    s.getActionName = bound_function("_get_action_name", "_get_action_name", { py_positional("argument") }, [](std::vector<py> const& a)
                    {
                        return get_action_name(a[0]);
                    });
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S("Command-line parsing library\n\nThis module is an optparse-inspired command-line parsing library that:\n\n"
                    "    - handles both optional and positional arguments\n    - produces highly informative usage messages\n"
                    "    - supports parsers that dispatch to sub-parsers\n\nThe following is a simple usage example that sums integers from the\n"
                    "command-line and writes the result to a file::\n\n    parser = argparse.ArgumentParser(\n        description='sum the integers at the command line')\n"
                    "    parser.add_argument(\n        'integers', metavar='int', nargs='+', type=int,\n        help='an integer to be summed')\n"
                    "    parser.add_argument(\n        '--log', default=sys.stdout, type=argparse.FileType('w'),\n"
                    "        help='the file where the sum should be written')\n    args = parser.parse_args()\n    args.log.write('%s' % sum(args.integers))\n"
                    "    args.log.close()\n\nThe module contains the following public classes:\n\n"
                    "    - ArgumentParser -- The main entry point for command-line parsing. As the\n        example above shows, the add_argument() method is used to populate\n"
                    "        the parser with actions for optional and positional arguments. Then\n        the parse_args() method is invoked to convert the args at the\n"
                    "        command-line into an object with attributes.\n\n    - ArgumentError -- The exception raised by ArgumentParser objects when\n"
                    "        there are errors with the parser's actions. Errors raised while\n        parsing the command-line are caught by ArgumentParser and emitted\n"
                    "        as command-line messages.\n\n    - FileType -- A factory for defining types of files to be created. As the\n"
                    "        example above shows, instances of FileType are typically passed as\n        the type= argument of add_argument() calls.\n\n"
                    "    - Action -- The base class for parser actions. Typically actions are\n        selected by passing strings like 'store_true' or 'append_const' to\n"
                    "        the action= argument of add_argument(). However, for greater\n        customization of ArgumentParser actions, subclasses of Action may\n"
                    "        be defined and passed as the action= argument.\n\n    - HelpFormatter, RawDescriptionHelpFormatter, RawTextHelpFormatter,\n"
                    "        ArgumentDefaultsHelpFormatter -- Formatter classes which\n        may be passed as the formatter_class= argument to the\n"
                    "        ArgumentParser constructor. HelpFormatter is the default,\n        RawDescriptionHelpFormatter and RawTextHelpFormatter tell the parser\n"
                    "        not to change the formatting for help text, and\n        ArgumentDefaultsHelpFormatter adds information about argument defaults\n"
                    "        to the help.\n\nAll other classes in this module are considered implementation details.\n"
                    "(Also note that HelpFormatter and RawDescriptionHelpFormatter are only\nconsidered public as object names -- the API of the formatter objects is\n"
                    "still considered an implementation detail.)\n"));
                add("__version__", S("1.1"));
                std::vector<py> all;
                for (auto n : { "ArgumentParser", "ArgumentError", "ArgumentTypeError", "BooleanOptionalAction", "FileType", "HelpFormatter",
                    "ArgumentDefaultsHelpFormatter", "RawDescriptionHelpFormatter", "RawTextHelpFormatter", "MetavarTypeHelpFormatter", "Namespace", "Action",
                    "ONE_OR_MORE", "OPTIONAL", "PARSER", "REMAINDER", "SUPPRESS", "ZERO_OR_MORE" })
                    all.emplace_back(n);
                add("__all__", py_list(all));
                add("SUPPRESS", s.SUPPRESS);
                add("OPTIONAL", s.OPTIONAL);
                add("ZERO_OR_MORE", s.ZERO_OR_MORE);
                add("ONE_OR_MORE", s.ONE_OR_MORE);
                add("PARSER", s.PARSER);
                add("REMAINDER", s.REMAINDER);
                add("_UNRECOGNIZED_ARGS_ATTR", S("_unrecognized_args"));
                add("_deprecated_default", s.deprecatedDefault);
                add("_get_action_name", s.getActionName);
                add("_copy_items", bound_function("_copy_items", "_copy_items", { py_positional("items") }, [](std::vector<py> const& a) { return copy_items(a[0]); }));
                add("_", bound_function("gettext", "gettext", { py_positional("message") }, [](std::vector<py> const& a) { return a[0]; }));
                add("ngettext", bound_function("ngettext", "ngettext", { py_positional("msgid1"), py_positional("msgid2"), py_positional("n") },
                    [](std::vector<py> const& a) { return a[3] == py{ 1 } ? a[1] : a[2]; }));
                for (auto const& [name, c] : s.classes)
                    add(name.c_str(), c);
                add("_os", py{ fn_import("os") });
                add("_re", py{ fn_import("re") });
                add("_sys", py{ fn_import("sys") });
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sArgparse = register_module("argparse", argparse_module);
    }
}
