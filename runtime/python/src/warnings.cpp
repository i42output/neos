/*
  warnings.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/warnings.py and Python/_warnings.c), Copyright (c) 2001-2024
  Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2
  (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++ for the neos Python runtime plugin and
  adapted to its object model (see runtime/python/NOTICE).
*/

// The module warnings (CPython 3.13's warnings.py and _warnings: their behaviour and messages). The functions look up the module's
// attributes (filters, defaultaction, onceregistry, showwarning, _showwarnmsg_impl, ...) when they are called, as CPython's do, so
// catch_warnings and a program's replacements of them work. (This runtime has no frames: a warning's location is the program's file.)

#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            object* sRegistry = nullptr;        // (the program's __warningregistry__)

            object* warnings_attribute(char const* aName) { return module_attribute("warnings", aName); }

            int set_warnings_attribute(char const* aName, object* aValue)
            {
                ref const module{ fn_import("warnings") };
                return module ? setattr(module.get(), aName, aValue) : -1;
            }

            object* call_warnings(char const* aName, std::vector<object*> aArgs, object* aKwargs = nullptr)  // (steals the arguments)
            {
                ref const f{ warnings_attribute(aName) };
                if (!f)
                {
                    for (auto a : aArgs)
                        release(a);
                    return nullptr;
                }
                return invoke(f.get(), std::move(aArgs), aKwargs);
            }

            int is_instance_of(object* aObject, object* aClass)
            {
                ref const r{ b_isinstance({ aObject, aClass }, nullptr) };
                return r ? truth(r.get()) : -1;
            }

            int is_subclass_of(object* aClass, object* aBase)
            {
                ref const r{ b_issubclass({ aClass, aBase }, nullptr) };
                return r ? truth(r.get()) : -1;
            }

            bool is_type(object* aObject) { return is_class(aObject) || is_type_object(aObject); }

            // (a warning category: a subclass of Warning)
            int is_category(object* aObject)
            {
                if (!is_type(aObject))
                    return 0;
                ref const warning{ exception_class("Warning") };
                return is_subclass_of(aObject, warning.get());
            }

            std::string class_name_of(object* aObject)
            {
                ref const cls{ class_of(aObject) };
                ref const name{ getattr(cls.get(), "__name__") };
                return name ? text_of(name.get()) : std::string{};
            }

            object* main_filename()     // (the program's file: sys.argv[0])
            {
                ref const argv{ module_attribute("sys", "argv") };
                if (argv && kind_of(argv.get()) == NEOS_PY_LIST && !as<list_object>(argv.get())->items.empty())
                    if (auto const first = as<list_object>(argv.get())->items[0]; kind_of(first) != NEOS_PY_STR || !as<str_object>(first)->value.empty())
                        return new_reference(first);
                clear_error();
                return make_str("<sys>");
            }

            std::string source_line(std::string const& aFilename, long long aLine)   // (linecache.getline)
            {
                if (aLine < 1)
                    return {};
                std::ifstream file{ aFilename, std::ios::binary };
                std::string line;
                for (long long i = 0; i < aLine; ++i)
                    if (!std::getline(file, line))
                        return {};
                return line + "\n";
            }

            std::string stripped(std::string const& aText)
            {
                auto points = to_code_points(aText);
                std::size_t start = 0u;
                while (start < points.size() && char_is_space(points[start]))
                    ++start;
                auto end = points.size();
                while (end > start && char_is_space(points[end - 1u]))
                    --end;
                return from_code_points(std::u32string_view{ points }.substr(start, end - start));
            }

            bool valid_action(object* aAction)
            {
                auto const text = str_of_value(aAction);
                if (text != nullptr)
                    for (auto a : { "error", "ignore", "always", "default", "module", "once" })
                        if (*text == a)
                            return true;
                error("ValueError", "invalid action: " + repr_text(aAction));
                return false;
            }

            bool valid_lineno(object* aLineno)
            {
                if (!is_int_like(aLineno))
                    return (error("TypeError", "lineno must be an int"), false);
                ref const zero{ make_int(0) };
                ref const negative{ compare(NEOS_PY_LT, aLineno, zero.get()) };
                if (!negative)
                    return false;
                if (truth(negative.get()) == 1)
                    return (error("ValueError", "lineno must be an int >= 0"), false);
                return true;
            }

            bool filters_mutated()
            {
                ref const version{ warnings_attribute("_filters_version") };
                if (!version)
                    return false;
                ref const one{ make_int(1) };
                ref const next{ binary(NEOS_PY_ADD, version.get(), one.get(), false) };
                return next && set_warnings_attribute("_filters_version", next.get()) >= 0;
            }

            // (_add_filter(*item, append))
            bool add_filter(std::vector<object*> aItem, bool aAppend)  // (steals the item's values)
            {
                ref const item{ make_tuple(std::move(aItem)) };
                ref const filters{ warnings_attribute("filters") };
                if (!filters)
                    return false;
                if (!aAppend)
                {
                    ref const removed{ call_method_named(filters.get(), "remove", { new_reference(item.get()) }) };
                    if (!removed)
                    {
                        ref const e{ take_exception() };
                        if (!exception_is(e.get(), "ValueError"))
                            return (raise_exception(e.get()), false);
                    }
                    ref const inserted{ call_method_named(filters.get(), "insert", { make_int(0), new_reference(item.get()) }) };
                    if (!inserted)
                        return false;
                }
                else
                {
                    auto const present = contains(filters.get(), item.get());
                    if (present < 0)
                        return false;
                    if (present == 0)
                    {
                        ref const appended{ call_method_named(filters.get(), "append", { new_reference(item.get()) }) };
                        if (!appended)
                            return false;
                    }
                }
                return filters_mutated();
            }

            object* compiled(object* aPattern, bool aIgnoreCase)    // (re.compile(pattern, re.I))
            {
                ref const compile{ module_attribute("re", "compile") };
                return compile ? invoke(compile.get(), { new_reference(aPattern), make_int(aIgnoreCase ? 2 : 0) }) : nullptr;
            }

            // (the WarningMessage of a warning)
            object* warning_message(object* aMessage, object* aCategory, object* aFilename, object* aLineno, object* aFile, object* aLine, object* aSource)
            {
                ref const cls{ warnings_attribute("WarningMessage") };
                if (!cls)
                    return nullptr;
                auto const value = [](object* aValue) { return new_reference(aValue != nullptr ? aValue : none()); };
                return invoke(cls.get(), { value(aMessage), value(aCategory), value(aFilename), value(aLineno), value(aFile), value(aLine), value(aSource) });
            }

            // (_formatwarnmsg_impl(msg))
            object* format_message(object* aMessage)
            {
                ref const category{ getattr(aMessage, "category") };
                ref const categoryName{ category ? getattr(category.get(), "__name__") : nullptr };
                ref const filename{ categoryName ? getattr(aMessage, "filename") : nullptr };
                ref const lineno{ filename ? getattr(aMessage, "lineno") : nullptr };
                ref const message{ lineno ? getattr(aMessage, "message") : nullptr };
                ref const line{ message ? getattr(aMessage, "line") : nullptr };
                ref const source{ line ? getattr(aMessage, "source") : nullptr };
                if (!source)
                    return nullptr;
                std::string name, file, number, text;
                if (!str_of(categoryName.get(), name) || !str_of(filename.get(), file) || !str_of(lineno.get(), number) || !str_of(message.get(), text))
                    return nullptr;
                std::string result = file + ":" + number + ": " + name + ": " + text + "\n";
                std::string sourceLine;
                if (kind_of(line.get()) == NEOS_PY_NONE)
                {
                    long long n = 0;
                    if (to_index(lineno.get(), n))
                        sourceLine = source_line(file, n);
                    clear_error();
                }
                else if (!str_of(line.get(), sourceLine))
                    return nullptr;
                if (!sourceLine.empty())
                    result += "  " + stripped(sourceLine) + "\n";
                // (tracemalloc does not trace: its suggestion)
                if (kind_of(source.get()) != NEOS_PY_NONE)
                    result += name + ": Enable tracemalloc to get the object allocation traceback\n";
                return make_str(result);
            }

            // (_formatwarnmsg(msg): formatwarning if it has been replaced)
            object* format_warning_message(object* aMessage)
            {
                ref const fw{ warnings_attribute("formatwarning") };
                ref const original{ fw ? warnings_attribute("_formatwarning_orig") : nullptr };
                if (!original)
                    clear_error();
                else if (fw.get() != original.get())
                {
                    std::vector<object*> args;
                    for (auto n : { "message", "category", "filename", "lineno", "line" })
                    {
                        auto const v = getattr(aMessage, n);
                        if (v == nullptr)
                        {
                            for (auto a : args)
                                release(a);
                            return nullptr;
                        }
                        args.push_back(v);
                    }
                    return invoke(fw.get(), std::move(args));
                }
                return call_warnings("_formatwarnmsg_impl", { new_reference(aMessage) });
            }

            // (_showwarnmsg_impl(msg))
            object* show_message(object* aMessage)
            {
                ref file{ getattr(aMessage, "file") };
                if (!file)
                    return nullptr;
                if (kind_of(file.get()) == NEOS_PY_NONE)
                {
                    file = ref{ module_attribute("sys", "stderr") };
                    if (!file)
                        return nullptr;
                    if (kind_of(file.get()) == NEOS_PY_NONE)
                        return new_reference(none());
                }
                ref const text{ call_warnings("_formatwarnmsg", { new_reference(aMessage) }) };
                if (!text)
                    return nullptr;
                ref const written{ call_method_named(file.get(), "write", { new_reference(text.get()) }) };
                if (!written)
                {
                    ref const e{ take_exception() };
                    if (!exception_is(e.get(), "OSError"))
                        return (raise_exception(e.get()), nullptr);
                }
                return new_reference(none());
            }

            // (_showwarnmsg(msg): showwarning if it has been replaced)
            object* show_warning_message(object* aMessage)
            {
                ref const sw{ warnings_attribute("showwarning") };
                ref const original{ sw ? warnings_attribute("_showwarning_orig") : nullptr };
                if (!original)
                    clear_error();
                else if (sw.get() != original.get())
                {
                    if (!is_callable(sw.get()))
                        return error("TypeError", "warnings.showwarning() must be set to a function or method");
                    std::vector<object*> args;
                    for (auto n : { "message", "category", "filename", "lineno", "file", "line" })
                    {
                        auto const v = getattr(aMessage, n);
                        if (v == nullptr)
                        {
                            for (auto a : args)
                                release(a);
                            return nullptr;
                        }
                        args.push_back(v);
                    }
                    ref const result{ invoke(sw.get(), std::move(args)) };
                    return result ? new_reference(none()) : nullptr;
                }
                return call_warnings("_showwarnmsg_impl", { new_reference(aMessage) });
            }

            // (a registry's value for a key (None if absent): borrowed)
            object* registry_get(object* aRegistry, object* aKey)
            {
                if (kind_of(aRegistry) == NEOS_PY_DICT)
                {
                    auto const position = dict_find(aRegistry, aKey);
                    if (position == -2)
                        return nullptr;
                    return position >= 0 ? as<dict_object>(aRegistry)->items[static_cast<std::size_t>(position)].second : none();
                }
                return error("TypeError", "'registry' must be a dict or None");
            }

            // (a filter's pattern matches: None, a str (equal) or a compiled pattern (match))
            int filter_matches(object* aPattern, object* aText)
            {
                if (kind_of(aPattern) == NEOS_PY_NONE)
                    return 1;
                if (kind_of(aPattern) == NEOS_PY_STR)
                    return kind_of(aText) == NEOS_PY_STR && as<str_object>(aPattern)->value == as<str_object>(aText)->value ? 1 : 0;
                ref const m{ call_method_named(aPattern, "match", { new_reference(aText) }) };
                return m ? truth(m.get()) : -1;
            }

            // (warn_explicit(message, category, filename, lineno, module, registry, module_globals, source))
            object* warn_explicit(object* aMessage, object* aCategory, object* aFilename, object* aLineno, object* aModule, object* aRegistry, object* aSource)
            {
                ref const intFunction{ fn_builtin("int") };
                ref const lineno{ invoke(intFunction.get(), { new_reference(aLineno) }) };
                if (!lineno)
                    return nullptr;
                ref module;
                if (aModule == nullptr || kind_of(aModule) == NEOS_PY_NONE)
                {
                    std::string name = "<unknown>";
                    if (auto const f = str_of_value(aFilename); f != nullptr && !f->empty())
                        name = *f;
                    if (name.size() >= 3u)
                    {
                        auto tail = name.substr(name.size() - 3u);
                        for (auto& c : tail)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (tail == ".py")
                            name.erase(name.size() - 3u);
                    }
                    module = ref{ make_str(name) };
                }
                else
                    module = ref{ new_reference(aModule) };
                ref registry{ aRegistry != nullptr && kind_of(aRegistry) != NEOS_PY_NONE ? new_reference(aRegistry) : make_dict() };
                ref const version{ warnings_attribute("_filters_version") };
                if (!version)
                    return nullptr;
                {
                    ref const versionKey{ make_str("version") };
                    auto const current = registry_get(registry.get(), versionKey.get());
                    if (current == nullptr)
                        return nullptr;
                    auto const same = kind_of(current) == NEOS_PY_NONE ? 0 : equal(current, version.get());
                    if (same < 0)
                        return nullptr;
                    if (same == 0)
                    {
                        ref const cleared{ call_method_named(registry.get(), "clear", {}) };
                        if (!cleared || dict_set(registry.get(), versionKey.get(), version.get()) < 0)
                            return nullptr;
                    }
                }
                ref const warning{ exception_class("Warning") };
                ref text, message, category;
                auto const isWarning = is_instance_of(aMessage, warning.get());
                if (isWarning < 0)
                    return nullptr;
                if (isWarning == 1)
                {
                    std::string t;
                    if (!str_of(aMessage, t))
                        return nullptr;
                    text = ref{ make_str(t) };
                    category = ref{ class_of(aMessage) };
                    message = ref{ new_reference(aMessage) };
                }
                else
                {
                    text = ref{ new_reference(aMessage) };
                    category = ref{ new_reference(aCategory) };
                    message = ref{ invoke(aCategory, { new_reference(aMessage) }) };
                    if (!message)
                        return nullptr;
                }
                ref const key{ make_tuple({ new_reference(text.get()), new_reference(category.get()), new_reference(lineno.get()) }) };
                {
                    auto const seen = registry_get(registry.get(), key.get());
                    if (seen == nullptr)
                        return nullptr;
                    auto const t = truth(seen);
                    if (t < 0)
                        return nullptr;
                    if (t == 1)
                        return new_reference(none());
                }
                // (the first filter that matches: its action; else defaultaction)
                ref const filters{ warnings_attribute("filters") };
                if (!filters)
                    return nullptr;
                if (kind_of(filters.get()) != NEOS_PY_LIST)
                    return error("ValueError", "_warnings.filters must be a list");
                ref action, item;
                std::vector<object*> const filterItems = as<list_object>(filters.get())->items;
                for (std::size_t i = 0u; i < filterItems.size() && !action; ++i)
                {
                    ref const f{ new_reference(filterItems[i]) };
                    if (kind_of(f.get()) != NEOS_PY_TUPLE || as<tuple_object>(f.get())->items.size() != 5u)
                        return error("ValueError", "_warnings.filters item " + std::to_string(i) + " isn't a 5-tuple");
                    auto const& parts = as<tuple_object>(f.get())->items;
                    if (kind_of(parts[0]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "action must be a string, not '" } + type_name(parts[0]) + "'");
                    auto r = filter_matches(parts[1], text.get());
                    if (r == 1)
                        r = is_subclass_of(category.get(), parts[2]);
                    if (r == 1)
                        r = filter_matches(parts[3], module.get());
                    if (r == 1)
                    {
                        ref const zero{ make_int(0) };
                        auto const any = equal(parts[4], zero.get());
                        r = any < 0 ? -1 : any == 1 ? 1 : equal(lineno.get(), parts[4]);
                    }
                    if (r < 0)
                        return nullptr;
                    if (r == 1)
                    {
                        action = ref{ new_reference(parts[0]) };
                        item = ref{ new_reference(f.get()) };
                    }
                }
                if (!action)
                {
                    action = ref{ warnings_attribute("defaultaction") };
                    if (!action)
                        return nullptr;
                }
                std::string const actionText = text_of(action.get());
                if (actionText == "ignore")
                    return new_reference(none());
                if (actionText == "error")
                {
                    fn_raise(message.get());
                    return nullptr;
                }
                ref const one{ make_int(1) };
                if (actionText == "once")
                {
                    if (dict_set(registry.get(), key.get(), one.get()) < 0)
                        return nullptr;
                    ref const onceKey{ make_tuple({ new_reference(text.get()), new_reference(category.get()) }) };
                    ref const once{ warnings_attribute("onceregistry") };
                    if (!once)
                        return nullptr;
                    auto const seen = registry_get(once.get(), onceKey.get());
                    if (seen == nullptr)
                        return nullptr;
                    if (truth(seen) == 1)
                        return new_reference(none());
                    if (dict_set(once.get(), onceKey.get(), one.get()) < 0)
                        return nullptr;
                }
                else if (actionText == "always")
                    ;
                else if (actionText == "module")
                {
                    if (dict_set(registry.get(), key.get(), one.get()) < 0)
                        return nullptr;
                    ref const zero{ make_int(0) };
                    ref const altKey{ make_tuple({ new_reference(text.get()), new_reference(category.get()), new_reference(zero.get()) }) };
                    auto const seen = registry_get(registry.get(), altKey.get());
                    if (seen == nullptr)
                        return nullptr;
                    if (truth(seen) == 1)
                        return new_reference(none());
                    if (dict_set(registry.get(), altKey.get(), one.get()) < 0)
                        return nullptr;
                }
                else if (actionText == "default")
                {
                    if (dict_set(registry.get(), key.get(), one.get()) < 0)
                        return nullptr;
                }
                else
                    return error("RuntimeError", "Unrecognized action (" + repr_text(action.get()) + ") in warnings.filters:\n " + text_of(item.get()));
                ref const msg{ warning_message(message.get(), category.get(), aFilename, lineno.get(), nullptr, nullptr, aSource) };
                if (!msg)
                    return nullptr;
                ref const shown{ call_warnings("_showwarnmsg", { new_reference(msg.get()) }) };
                return shown ? new_reference(none()) : nullptr;
            }

            // (warn(message, category=None, stacklevel=1, source=None, *, skip_file_prefixes=()))
            object* warn(object* aMessage, object* aCategory, object* aSource, object* aSkip)
            {
                ref const warning{ exception_class("Warning") };
                ref category{ aCategory != nullptr ? new_reference(aCategory) : new_reference(none()) };
                auto const isWarning = is_instance_of(aMessage, warning.get());
                if (isWarning < 0)
                    return nullptr;
                if (isWarning == 1)
                    category = ref{ class_of(aMessage) };
                if (kind_of(category.get()) == NEOS_PY_NONE)
                    category = ref{ exception_class("UserWarning") };
                auto const valid = is_category(category.get());
                if (valid < 0)
                    return nullptr;
                if (valid == 0)
                    return error("TypeError", "category must be a Warning subclass, not '" + class_name_of(category.get()) + "'");
                if (aSkip != nullptr && kind_of(aSkip) != NEOS_PY_TUPLE)
                    return error("TypeError", "skip_file_prefixes must be a tuple of strs.");
                if (sRegistry == nullptr)
                    sRegistry = make_dict();
                ref const filename{ main_filename() };
                ref const lineno{ make_int(0) };
                ref const module{ make_str("__main__") };
                return warn_explicit(aMessage, category.get(), filename.get(), lineno.get(), module.get(), sRegistry, aSource);
            }

            // --- -W options (sys.warnoptions)

            std::nullptr_t option_error(std::string const& aMessage)
            {
                ref const cls{ warnings_attribute("_OptionError") };
                if (cls)
                    if (ref const e{ invoke(cls.get(), { make_str(aMessage) }) })
                        fn_raise(e.get());
                return nullptr;
            }

            object* get_action(std::string const& aAction)
            {
                if (aAction.empty())
                    return make_str("default");
                if (aAction == "all")
                    return make_str("always");
                for (std::string a : { "default", "always", "ignore", "module", "once", "error" })
                    if (a.starts_with(aAction))
                        return make_str(a);
                ref const action{ make_str(aAction) };
                return option_error("invalid action: " + repr_text(action.get()));
            }

            object* get_category(std::string const& aCategory)
            {
                if (aCategory.empty())
                    return exception_class("Warning");
                ref module;
                std::string klass;
                ref const categoryText{ make_str(aCategory) };
                if (aCategory.find('.') == std::string::npos)
                {
                    module = ref{ fn_import("builtins") };
                    klass = aCategory;
                }
                else
                {
                    auto const dot = aCategory.rfind('.');
                    auto const moduleName = aCategory.substr(0u, dot);
                    klass = aCategory.substr(dot + 1u);
                    module = ref{ fn_import(moduleName.c_str()) };
                    if (!module)
                    {
                        ref const e{ take_exception() };
                        if (!exception_is(e.get(), "ImportError"))
                            return (raise_exception(e.get()), nullptr);
                        ref const name{ make_str(moduleName) };
                        return option_error("invalid module name: " + repr_text(name.get()));
                    }
                }
                if (!module)
                    return nullptr;
                ref const cat{ getattr(module.get(), klass.c_str()) };
                if (!cat)
                {
                    ref const e{ take_exception() };
                    if (!exception_is(e.get(), "AttributeError"))
                        return (raise_exception(e.get()), nullptr);
                    return option_error("unknown warning category: " + repr_text(categoryText.get()));
                }
                auto const valid = is_category(cat.get());
                if (valid < 0)
                    return nullptr;
                if (valid == 0)
                    return option_error("invalid warning category: " + repr_text(categoryText.get()));
                return new_reference(cat.get());
            }

            object* re_escape(std::string const& aText)
            {
                ref const escape{ module_attribute("re", "escape") };
                return escape ? invoke(escape.get(), { make_str(aText) }) : nullptr;
            }

            object* set_option(object* aArgument)
            {
                std::string arg;
                if (!str_of(aArgument, arg))
                    return nullptr;
                std::vector<std::string> parts;
                std::size_t start = 0u;
                for (;;)
                {
                    auto const colon = arg.find(':', start);
                    parts.push_back(arg.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
                    if (colon == std::string::npos)
                        break;
                    start = colon + 1u;
                }
                if (parts.size() > 5u)
                {
                    return option_error("too many fields (max 5): " + repr_text(aArgument));
                }
                while (parts.size() < 5u)
                    parts.emplace_back();
                for (auto& p : parts)
                    p = stripped(p);
                ref const action{ get_action(parts[0]) };
                if (!action)
                    return nullptr;
                ref const category{ get_category(parts[2]) };
                if (!category)
                    return nullptr;
                ref message{ make_str("") };
                if (!parts[1].empty())
                    message = ref{ re_escape(parts[1]) };
                ref module{ make_str("") };
                if (!parts[3].empty())
                {
                    ref const escaped{ re_escape(parts[3]) };
                    module = ref{ escaped ? make_str(text_of(escaped.get()) + "\\Z") : nullptr };
                }
                if (!message || !module)
                    return nullptr;
                ref lineno{ make_int(0) };
                if (!parts[4].empty())
                {
                    ref const text{ make_str(parts[4]) };
                    ref const intFunction{ fn_builtin("int") };
                    lineno = ref{ invoke(intFunction.get(), { new_reference(text.get()) }) };
                    long long n = 0;
                    if (!lineno || !to_index(lineno.get(), n) || n < 0)
                    {
                        clear_error();
                        return option_error("invalid lineno " + repr_text(text.get()));
                    }
                }
                return call_warnings("filterwarnings", { new_reference(action.get()), new_reference(message.get()), new_reference(category.get()),
                    new_reference(module.get()), new_reference(lineno.get()) });
            }

            // --- deprecated (the wrappers of what it decorates)

            // (a function's wrapper: it warns, then calls the function (its __wrapped__))
            builtin_entry const sDeprecatedWrapperMethods[] =
            {
                { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const msg{ getattr(self, "__deprecated__") };
                        ref const category{ msg ? getattr(self, "_deprecated_category") : nullptr };
                        ref const wrapped{ category ? getattr(self, "__wrapped__") : nullptr };
                        if (!wrapped)
                            return nullptr;
                        ref const warned{ warn(msg.get(), category.get(), nullptr, nullptr) };
                        if (!warned)
                            return nullptr;
                        ref const args{ make_tuple({ aArgs.begin() + 1, aArgs.end() }) };
                        for (auto a : as<tuple_object>(args.get())->items)
                            retain(a);
                        return call(wrapped.get(), args.get(), aKwargs);
                    } },
                { "__get__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() < 2u || kind_of(aArgs[1]) == NEOS_PY_NONE)
                            return new_reference(aArgs[0]);
                        return make_bound_method(aArgs[0], aArgs[1]);
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const name{ getattr(aArgs[0], "__qualname__") };
                        clear_error();
                        return make_str("<function " + (name ? text_of(name.get()) : std::string{ "wrapper" }) + " at " + object_address(aArgs[0]) + ">");
                    } }
            };

            // (a class's __new__: it warns (for the class itself), then calls the class's original __new__)
            builtin_entry const sDeprecatedNewMethods[] =
            {
                { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const self = aArgs[0];
                        if (aArgs.size() < 2u)
                            return error("TypeError", "__new__() missing 1 required positional argument: 'cls'");
                        auto const cls = aArgs[1];
                        ref const arg{ getattr(self, "_deprecated_class") };
                        ref const msg{ arg ? getattr(self, "__deprecated__") : nullptr };
                        ref const category{ msg ? getattr(self, "_deprecated_category") : nullptr };
                        ref const original{ category ? getattr(self, "_deprecated_new") : nullptr };
                        if (!original)
                            return nullptr;
                        if (cls == arg.get())
                        {
                            ref const warned{ warn(msg.get(), category.get(), nullptr, nullptr) };
                            if (!warned)
                                return nullptr;
                        }
                        ref const objectNew{ class_function("object", "__new__") };
                        bool const hasArguments = aArgs.size() > 2u || (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty());
                        if (original.get() != objectNew.get())
                        {
                            std::vector<object*> args;
                            for (std::size_t i = 1u; i < aArgs.size(); ++i)
                                args.push_back(new_reference(aArgs[i]));
                            return invoke(original.get(), std::move(args), aKwargs);
                        }
                        ref const init{ getattr(cls, "__init__") };
                        ref const objectInit{ class_function("object", "__init__") };
                        if (init && init.get() == objectInit.get() && hasArguments)
                        {
                            ref const name{ getattr(cls, "__name__") };
                            return error("TypeError", text_of(name.get()) + "() takes no arguments");
                        }
                        clear_error();
                        return invoke(original.get(), { new_reference(cls) });
                    } }
            };

            // (a class's __init_subclass__ (a classmethod's function): it warns, then calls the class's own, or its base's)
            builtin_entry const sDeprecatedInitSubclassMethods[] =
            {
                { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const self = aArgs[0];
                        if (aArgs.size() < 2u)
                            return error("TypeError", "__init_subclass__() missing 1 required positional argument: 'cls'");
                        auto const cls = aArgs[1];
                        ref const msg{ getattr(self, "__deprecated__") };
                        ref const category{ msg ? getattr(self, "_deprecated_category") : nullptr };
                        ref const original{ category ? getattr(self, "_deprecated_original") : nullptr };
                        ref const arg{ original ? getattr(self, "_deprecated_class") : nullptr };
                        if (!arg)
                            return nullptr;
                        ref const warned{ warn(msg.get(), category.get(), nullptr, nullptr) };
                        if (!warned)
                            return nullptr;
                        std::vector<object*> rest;
                        for (std::size_t i = 2u; i < aArgs.size(); ++i)
                            rest.push_back(new_reference(aArgs[i]));
                        if (kind_of(original.get()) != NEOS_PY_NONE)
                        {
                            rest.insert(rest.begin(), new_reference(cls));
                            return invoke(original.get(), std::move(rest), aKwargs);
                        }
                        // (super(arg, cls).__init_subclass__(*args, **kwargs))
                        ref const super{ fn_builtin("super") };
                        ref const proxy{ invoke(super.get(), { new_reference(arg.get()), new_reference(cls) }) };
                        ref const base{ proxy ? getattr(proxy.get(), "__init_subclass__") : nullptr };
                        if (!base)
                        {
                            for (auto r : rest)
                                release(r);
                            return nullptr;
                        }
                        return invoke(base.get(), std::move(rest), aKwargs);
                    } }
            };

            object* sDeprecatedWrapper = nullptr;
            object* sDeprecatedNew = nullptr;
            object* sDeprecatedInitSubclass = nullptr;

            bool set_all(object* aObject, std::initializer_list<std::pair<char const*, object*>> aAttributes)
            {
                for (auto const& a : aAttributes)
                    if (setattr(aObject, a.first, a.second) < 0)
                        return false;
                return true;
            }

            object* deprecate(object* aSelf, object* aArg)
            {
                ref const msg{ getattr(aSelf, "message") };
                ref const category{ msg ? getattr(aSelf, "category") : nullptr };
                ref const stacklevel{ category ? getattr(aSelf, "stacklevel") : nullptr };
                if (!stacklevel)
                    return nullptr;
                if (kind_of(category.get()) == NEOS_PY_NONE)
                    return set_all(aArg, { { "__deprecated__", msg.get() } }) ? new_reference(aArg) : nullptr;
                if (is_type(aArg))
                {
                    ref const originalNew{ getattr(aArg, "__new__") };
                    if (!originalNew)
                        return nullptr;
                    ref const newWrapper{ call(sDeprecatedNew, nullptr, nullptr) };
                    if (!newWrapper || !set_all(newWrapper.get(), { { "_deprecated_class", aArg }, { "_deprecated_new", originalNew.get() },
                        { "__deprecated__", msg.get() }, { "_deprecated_category", category.get() } }))
                        return nullptr;
                    ref const update{ module_attribute("functools", "update_wrapper") };
                    ref const updated{ update ? invoke(update.get(), { new_reference(newWrapper.get()), new_reference(originalNew.get()) }) : nullptr };
                    if (!updated)
                        clear_error();
                    ref const staticNew{ b_staticmethod({ newWrapper.get() }, nullptr) };
                    if (!staticNew || setattr(aArg, "__new__", staticNew.get()) < 0)
                        return nullptr;
                    // (its own __init_subclass__, if it has one (as a function))
                    ref const ns{ getattr(aArg, "__dict__") };
                    ref const key{ make_str("__init_subclass__") };
                    auto const own = ns ? contains(ns.get(), key.get()) : -1;
                    if (own < 0)
                        return nullptr;
                    ref original{ new_reference(none()) };
                    if (own == 1)
                    {
                        original = ref{ getattr(aArg, "__init_subclass__") };
                        if (!original)
                            return nullptr;
                        if (ref const f{ getattr(original.get(), "__func__") })
                            original = ref{ new_reference(f.get()) };
                        else
                            clear_error();
                    }
                    ref const initWrapper{ call(sDeprecatedInitSubclass, nullptr, nullptr) };
                    if (!initWrapper || !set_all(initWrapper.get(), { { "_deprecated_class", aArg }, { "_deprecated_original", original.get() },
                        { "__deprecated__", msg.get() }, { "_deprecated_category", category.get() } }))
                        return nullptr;
                    ref const classInit{ b_classmethod({ initWrapper.get() }, nullptr) };
                    if (!classInit || setattr(aArg, "__init_subclass__", classInit.get()) < 0 || !set_all(aArg, { { "__deprecated__", msg.get() } }))
                        return nullptr;
                    return new_reference(aArg);
                }
                if (is_callable(aArg))
                {
                    ref const wrapper{ call(sDeprecatedWrapper, nullptr, nullptr) };
                    if (!wrapper)
                        return nullptr;
                    ref const update{ module_attribute("functools", "update_wrapper") };
                    ref const updated{ update ? invoke(update.get(), { new_reference(wrapper.get()), new_reference(aArg) }) : nullptr };
                    if (!updated)
                        return nullptr;
                    if (!set_all(wrapper.get(), { { "__wrapped__", aArg }, { "_deprecated_category", category.get() }, { "__deprecated__", msg.get() } }))
                        return nullptr;
                    if (setattr(aArg, "__deprecated__", msg.get()) < 0)
                        clear_error();
                    return new_reference(wrapper.get());
                }
                return error("TypeError", "@deprecated decorator with non-None category must be applied to a class or callable, not " + repr_text(aArg));
            }

            builtin_entry const sDeprecatedMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("deprecated.__init__", aArgs, aKwargs, { "message", "category", "stacklevel" }, 1u, 1u, a))
                            return nullptr;
                        if (aArgs.size() > 2u)
                            return error("TypeError", "deprecated.__init__() takes 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", "Expected an object of type str for 'message', not " + repr_text(ref{ make_str(class_name_of(a[0])) }.get()));
                        ref const category{ a[1] != nullptr ? new_reference(a[1]) : exception_class("DeprecationWarning") };
                        ref const stacklevel{ a[2] != nullptr ? new_reference(a[2]) : make_int(1) };
                        return set_all(aArgs[0], { { "message", a[0] }, { "category", category.get() }, { "stacklevel", stacklevel.get() } }) ? new_reference(none()) : nullptr;
                    } },
                { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("deprecated.__call__", aArgs, aKwargs, { "arg" }, 1u, 1u, a))
                            return nullptr;
                        return deprecate(aArgs[0], a[0]);
                    } }
            };

            // --- WarningMessage and catch_warnings

            builtin_entry const sWarningMessageMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("WarningMessage.__init__", aArgs, aKwargs, { "message", "category", "filename", "lineno", "file", "line", "source" }, 4u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        char const* const names[] = { "message", "category", "filename", "lineno", "file", "line", "source" };
                        for (std::size_t i = 0u; i < a.size(); ++i)
                            if (setattr(self, names[i], a[i] != nullptr ? a[i] : none()) < 0)
                                return nullptr;
                        ref categoryName{ new_reference(none()) };
                        auto const t = truth(a[1]);
                        if (t < 0)
                            return nullptr;
                        if (t == 1)
                        {
                            categoryName = ref{ getattr(a[1], "__name__") };
                            if (!categoryName)
                                return nullptr;
                        }
                        return setattr(self, "_category_name", categoryName.get()) < 0 ? nullptr : new_reference(none());
                    } },
                { "__str__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const message{ getattr(self, "message") };
                        ref const category{ message ? getattr(self, "_category_name") : nullptr };
                        ref const filename{ category ? getattr(self, "filename") : nullptr };
                        ref const lineno{ filename ? getattr(self, "lineno") : nullptr };
                        ref const line{ lineno ? getattr(self, "line") : nullptr };
                        if (!line)
                            return nullptr;
                        std::string m, c, f, n, l;
                        if (!repr_of(message.get(), m) || !repr_of(category.get(), c) || !repr_of(filename.get(), f) || !str_of(lineno.get(), n) || !repr_of(line.get(), l))
                            return nullptr;
                        return make_str("{message : " + m + ", category : " + c + ", filename : " + f + ", lineno : " + n + ", line : " + l + "}");
                    } }
            };

            builtin_entry const sCatchWarningsMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("catch_warnings.__init__", aArgs, aKwargs, { "record", "module", "action", "category", "lineno", "append" }, 0u, 0u, a))
                            return nullptr;
                        if (aArgs.size() > 1u)
                            return error("TypeError", "catch_warnings.__init__() takes 1 positional argument but " + std::to_string(aArgs.size()) + " were given");
                        auto const self = aArgs[0];
                        ref const record{ a[0] != nullptr ? new_reference(a[0]) : make_bool(false) };
                        ref const module{ a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE ? new_reference(a[1]) : fn_import("warnings") };
                        if (!module)
                            return nullptr;
                        ref filter{ new_reference(none()) };
                        if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE)
                            filter = ref{ make_tuple({ new_reference(a[2]), a[3] != nullptr ? new_reference(a[3]) : exception_class("Warning"),
                                a[4] != nullptr ? new_reference(a[4]) : make_int(0), a[5] != nullptr ? new_reference(a[5]) : make_bool(false) }) };
                        ref const no{ make_bool(false) };
                        return set_all(self, { { "_record", record.get() }, { "_module", module.get() }, { "_entered", no.get() }, { "_filter", filter.get() } }) ?
                            new_reference(none()) : nullptr;
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        std::vector<std::string> args;
                        auto const record = attribute_truth(self, "_record");
                        if (record < 0)
                            return nullptr;
                        if (record == 1)
                            args.push_back("record=True");
                        ref const module{ getattr(self, "_module") };
                        ref const warnings{ fn_import("warnings") };
                        if (!module || !warnings)
                            return nullptr;
                        if (module.get() != warnings.get())
                            args.push_back("module=" + repr_text(module.get()));
                        std::string text;
                        for (std::size_t i = 0u; i < args.size(); ++i)
                            text += (i == 0u ? "" : ", ") + args[i];
                        return make_str(class_name_of(self) + "(" + text + ")");
                    } },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        auto const entered = attribute_truth(self, "_entered");
                        if (entered < 0)
                            return nullptr;
                        if (entered == 1)
                            return error("RuntimeError", "Cannot enter " + repr_text(self) + " twice");
                        ref const yes{ make_bool(true) };
                        ref const module{ getattr(self, "_module") };
                        ref const filters{ module ? getattr(module.get(), "filters") : nullptr };
                        if (!filters || setattr(self, "_entered", yes.get()) < 0 || setattr(self, "_filters", filters.get()) < 0)
                            return nullptr;
                        ref const all{ fn_builtin("slice") };
                        ref const everything{ invoke(all.get(), { new_reference(none()) }) };
                        ref const copy{ everything ? getitem(filters.get(), everything.get()) : nullptr };
                        if (!copy || setattr(module.get(), "filters", copy.get()) < 0)
                            return nullptr;
                        ref const mutated{ call_method_named(module.get(), "_filters_mutated", {}) };
                        ref const showwarning{ mutated ? getattr(module.get(), "showwarning") : nullptr };
                        ref const impl{ showwarning ? getattr(module.get(), "_showwarnmsg_impl") : nullptr };
                        if (!impl || setattr(self, "_showwarning", showwarning.get()) < 0 || setattr(self, "_showwarnmsg_impl", impl.get()) < 0)
                            return nullptr;
                        ref const filter{ getattr(self, "_filter") };
                        if (!filter)
                            return nullptr;
                        if (kind_of(filter.get()) != NEOS_PY_NONE)
                        {
                            ref const simplefilter{ warnings_attribute("simplefilter") };
                            ref const done{ simplefilter ? call(simplefilter.get(), filter.get(), nullptr) : nullptr };
                            if (!done)
                                return nullptr;
                        }
                        auto const record = attribute_truth(self, "_record");
                        if (record < 0)
                            return nullptr;
                        if (record == 1)
                        {
                            ref const log{ make_list() };
                            ref const append{ getattr(log.get(), "append") };
                            ref const original{ getattr(module.get(), "_showwarning_orig") };
                            if (!append || !original || setattr(module.get(), "_showwarnmsg_impl", append.get()) < 0 || setattr(module.get(), "showwarning", original.get()) < 0)
                                return nullptr;
                            return new_reference(log.get());
                        }
                        return new_reference(none());
                    } },
                { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        auto const entered = attribute_truth(self, "_entered");
                        if (entered < 0)
                            return nullptr;
                        if (entered == 0)
                            return error("RuntimeError", "Cannot exit " + repr_text(self) + " without entering first");
                        ref const module{ getattr(self, "_module") };
                        ref const filters{ module ? getattr(self, "_filters") : nullptr };
                        if (!filters || setattr(module.get(), "filters", filters.get()) < 0)
                            return nullptr;
                        ref const mutated{ call_method_named(module.get(), "_filters_mutated", {}) };
                        ref const showwarning{ mutated ? getattr(self, "_showwarning") : nullptr };
                        ref const impl{ showwarning ? getattr(self, "_showwarnmsg_impl") : nullptr };
                        if (!impl || setattr(module.get(), "showwarning", showwarning.get()) < 0 || setattr(module.get(), "_showwarnmsg_impl", impl.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } }
            };

            // --- the module's functions

            char const* const sDeprecatedMessage = "{name!r} is deprecated and slated for removal in Python {remove}";

            builtin_entry const sWarningsFunctions[] =
            {
                { "_deprecated", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (_deprecated(name, message=_DEPRECATED_MSG, *, remove, _version=sys.version_info))
                        return py_guarded([&]()
                        {
                            if (aArgs.size() > 2u)
                                py_raise("TypeError", "_deprecated() takes from 1 to 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                            std::vector<object*> a;
                            py_checked(parse_arguments("_deprecated", aArgs, aKwargs, { "name", "message", "remove", "_version" }, 1u, 0u, a));
                            if (a[2] == nullptr)
                                py_raise("TypeError", "_deprecated() missing 1 required keyword-only argument: 'remove'");
                            py const name = py::borrowed(a[0]);
                            py const message = a[1] != nullptr ? py::borrowed(a[1]) : py{ sDeprecatedMessage };
                            py const remove = py::borrowed(a[2]);
                            py const version = a[3] != nullptr ? py::borrowed(a[3]) : import_attribute("sys", "version_info");
                            py const removeFormatted{ remove[py{ 0 }].str() + "." + remove[py{ 1 }].str() };
                            py const head = version[builtin("slice")(py::none(), py{ 2 })];
                            py const tuple = builtin("tuple");
                            if (tuple(head) > tuple(remove) || (tuple(head) == tuple(remove) && version[py{ 3 }] != py{ "alpha" }))
                                py_raise("RuntimeError", name.repr() + " was slated for removal after Python " + removeFormatted.str() + " alpha");
                            py const msg = message.method("format", {}, { { "name", name }, { "remove", removeFormatted } });
                            import_attribute("warnings", "warn").call({ msg, builtin("DeprecationWarning") }, { { "stacklevel", py{ 3 } } });
                            return py::none();
                        });
                    } },
                { "warn", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("warn", aArgs, aKwargs, { "message", "category", "stacklevel", "source", "skip_file_prefixes" }, 1u, 0u, a))
                            return nullptr;
                        if (aArgs.size() > 4u)
                            return error("TypeError", "warn() takes at most 4 positional arguments (" + std::to_string(aArgs.size()) + " given)");
                        long long stacklevel = 1;
                        if (a[2] != nullptr && !to_index(a[2], stacklevel))
                            return nullptr;
                        return warn(a[0], a[1], a[3] != nullptr ? a[3] : none(), a[4]);
                    } },
                { "warn_explicit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("warn_explicit", aArgs, aKwargs, { "message", "category", "filename", "lineno", "module", "registry", "module_globals", "source" }, 4u, 0u, a))
                            return nullptr;
                        return warn_explicit(a[0], a[1], a[2], a[3], a[4], a[5], a[7] != nullptr ? a[7] : none());
                    } },
                { "_filters_mutated", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_filters_mutated", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        return filters_mutated() ? new_reference(none()) : nullptr;
                    } },
                { "showwarning", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("showwarning", aArgs, aKwargs, { "message", "category", "filename", "lineno", "file", "line" }, 4u, 0u, a))
                            return nullptr;
                        ref const msg{ warning_message(a[0], a[1], a[2], a[3], a[4], a[5], nullptr) };
                        ref const shown{ msg ? call_warnings("_showwarnmsg_impl", { new_reference(msg.get()) }) : nullptr };
                        return shown ? new_reference(none()) : nullptr;
                    } },
                { "formatwarning", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("formatwarning", aArgs, aKwargs, { "message", "category", "filename", "lineno", "line" }, 4u, 0u, a))
                            return nullptr;
                        ref const msg{ warning_message(a[0], a[1], a[2], a[3], nullptr, a[4], nullptr) };
                        return msg ? call_warnings("_formatwarnmsg_impl", { new_reference(msg.get()) }) : nullptr;
                    } },
                { "_showwarnmsg_impl", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_showwarnmsg_impl", aArgs, aKwargs, { "msg" }, 1u, 0u, a))
                            return nullptr;
                        return show_message(a[0]);
                    } },
                { "_formatwarnmsg_impl", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_formatwarnmsg_impl", aArgs, aKwargs, { "msg" }, 1u, 0u, a))
                            return nullptr;
                        return format_message(a[0]);
                    } },
                { "_showwarnmsg", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_showwarnmsg", aArgs, aKwargs, { "msg" }, 1u, 0u, a))
                            return nullptr;
                        return show_warning_message(a[0]);
                    } },
                { "_formatwarnmsg", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_formatwarnmsg", aArgs, aKwargs, { "msg" }, 1u, 0u, a))
                            return nullptr;
                        return format_warning_message(a[0]);
                    } },
                { "filterwarnings", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("filterwarnings", aArgs, aKwargs, { "action", "message", "category", "module", "lineno", "append" }, 1u, 0u, a))
                            return nullptr;
                        ref const empty{ make_str("") };
                        ref const warning{ exception_class("Warning") };
                        ref const zero{ make_int(0) };
                        auto const message = a[1] != nullptr ? a[1] : empty.get();
                        auto const category = a[2] != nullptr ? a[2] : warning.get();
                        auto const module = a[3] != nullptr ? a[3] : empty.get();
                        auto const lineno = a[4] != nullptr ? a[4] : zero.get();
                        if (!valid_action(a[0]))
                            return nullptr;
                        if (kind_of(message) != NEOS_PY_STR)
                            return error("TypeError", "message must be a string");
                        auto const valid = is_category(category);
                        if (valid < 0)
                            return nullptr;
                        if (valid == 0)
                            return error("TypeError", "category must be a Warning subclass");
                        if (kind_of(module) != NEOS_PY_STR)
                            return error("TypeError", "module must be a string");
                        if (!valid_lineno(lineno))
                            return nullptr;
                        ref const messagePattern{ as<str_object>(message)->value.empty() ? new_reference(none()) : compiled(message, true) };
                        ref const modulePattern{ as<str_object>(module)->value.empty() ? new_reference(none()) : compiled(module, false) };
                        if (!messagePattern || !modulePattern)
                            return nullptr;
                        auto const append = a[5] != nullptr ? truth(a[5]) : 0;
                        if (append < 0)
                            return nullptr;
                        return add_filter({ new_reference(a[0]), new_reference(messagePattern.get()), new_reference(category), new_reference(modulePattern.get()),
                            new_reference(lineno) }, append == 1) ? new_reference(none()) : nullptr;
                    } },
                { "simplefilter", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("simplefilter", aArgs, aKwargs, { "action", "category", "lineno", "append" }, 1u, 0u, a))
                            return nullptr;
                        ref const warning{ exception_class("Warning") };
                        ref const zero{ make_int(0) };
                        auto const category = a[1] != nullptr ? a[1] : warning.get();
                        auto const lineno = a[2] != nullptr ? a[2] : zero.get();
                        if (!valid_action(a[0]) || !valid_lineno(lineno))
                            return nullptr;
                        auto const append = a[3] != nullptr ? truth(a[3]) : 0;
                        if (append < 0)
                            return nullptr;
                        return add_filter({ new_reference(a[0]), new_reference(none()), new_reference(category), new_reference(none()), new_reference(lineno) }, append == 1) ?
                            new_reference(none()) : nullptr;
                    } },
                { "_add_filter", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aKwargs == nullptr || dict_find(aKwargs, ref{ make_str("append") }.get()) < 0)
                            return error("TypeError", "_add_filter() missing 1 required keyword-only argument: 'append'");
                        auto const append = truth(as<dict_object>(aKwargs)->items[static_cast<std::size_t>(dict_find(aKwargs, ref{ make_str("append") }.get()))].second);
                        std::vector<object*> item;
                        for (auto a : aArgs)
                            item.push_back(new_reference(a));
                        return append >= 0 && add_filter(std::move(item), append == 1) ? new_reference(none()) : nullptr;
                    } },
                { "resetwarnings", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("resetwarnings", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref const filters{ warnings_attribute("filters") };
                        ref const cleared{ filters ? call_method_named(filters.get(), "clear", {}) : nullptr };
                        return cleared && filters_mutated() ? new_reference(none()) : nullptr;
                    } },
                { "_processoptions", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_processoptions", aArgs, aKwargs, { "args" }, 1u, 0u, a))
                            return nullptr;
                        std::vector<object*> options;
                        if (!collect(a[0], options))
                            return nullptr;
                        bool ok = true;
                        for (auto option : options)
                        {
                            if (!ok)
                                continue;
                            ref const r{ set_option(option) };
                            if (!r)
                            {
                                ref const e{ take_exception() };
                                ref const cls{ warnings_attribute("_OptionError") };
                                if (cls && is_instance_of(e.get(), cls.get()) == 1)
                                {
                                    ref const stderrFile{ module_attribute("sys", "stderr") };
                                    ref const print{ fn_builtin("print") };
                                    ref const kw{ keywords({ { "file", stderrFile.get() } }) };
                                    ref const printed{ invoke(print.get(), { make_str("Invalid -W option ignored:"), new_reference(e.get()) }, kw.get()) };
                                    ok = static_cast<bool>(printed);
                                }
                                else
                                {
                                    raise_exception(e.get());
                                    ok = false;
                                }
                            }
                        }
                        for (auto o : options)
                            release(o);
                        return ok ? new_reference(none()) : nullptr;
                    } },
                { "_setoption", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_setoption", aArgs, aKwargs, { "arg" }, 1u, 0u, a))
                            return nullptr;
                        return set_option(a[0]);
                    } },
                { "_getaction", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string action;
                        if (!parse_arguments("_getaction", aArgs, aKwargs, { "action" }, 1u, 0u, a) || !str_of(a[0], action))
                            return nullptr;
                        return get_action(action);
                    } },
                { "_getcategory", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string category;
                        if (!parse_arguments("_getcategory", aArgs, aKwargs, { "category" }, 1u, 0u, a) || !str_of(a[0], category))
                            return nullptr;
                        return get_category(category);
                    } },
                { "_deprecated", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_deprecated", aArgs, aKwargs, { "name", "message", "remove", "_version" }, 1u, 0u, a))
                            return nullptr;
                        if (a[2] == nullptr)
                            return error("TypeError", "_deprecated() missing 1 required keyword-only argument: 'remove'");
                        ref version{ a[3] != nullptr ? new_reference(a[3]) : module_attribute("sys", "version_info") };
                        ref const zero{ make_int(0) };
                        ref const one{ make_int(1) };
                        ref const major{ getitem(a[2], zero.get()) };
                        ref const minor{ major ? getitem(a[2], one.get()) : nullptr };
                        if (!minor || !version)
                            return nullptr;
                        auto const removeText = text_of(major.get()) + "." + text_of(minor.get());
                        ref const slice{ fn_builtin("slice") };
                        ref const two{ invoke(slice.get(), { make_int(2) }) };
                        ref const head{ two ? getitem(version.get(), two.get()) : nullptr };
                        ref const headTuple{ head ? fn_tuple_of(head.get()) : nullptr };
                        ref const removeTuple{ headTuple ? fn_tuple_of(a[2]) : nullptr };
                        ref const after{ removeTuple ? compare(NEOS_PY_GT, headTuple.get(), removeTuple.get()) : nullptr };
                        ref const same{ after ? compare(NEOS_PY_EQ, headTuple.get(), removeTuple.get()) : nullptr };
                        ref const three{ make_int(3) };
                        ref const level{ same ? getitem(version.get(), three.get()) : nullptr };
                        if (!level)
                            return nullptr;
                        if (truth(after.get()) == 1 || (truth(same.get()) == 1 && text_of(level.get()) != "alpha"))
                            return error("RuntimeError", repr_text(a[0]) + " was slated for removal after Python " + removeText + " alpha");
                        ref const message{ a[1] != nullptr ? new_reference(a[1]) : make_str("{name!r} is deprecated and slated for removal in Python {remove}") };
                        ref const format{ getattr(message.get(), "format") };
                        ref const removeValue{ make_str(removeText) };
                        ref const kw{ keywords({ { "name", a[0] }, { "remove", removeValue.get() } }) };
                        ref const msg{ format ? invoke(format.get(), {}, kw.get()) : nullptr };
                        if (!msg)
                            return nullptr;
                        ref const category{ exception_class("DeprecationWarning") };
                        return warn(msg.get(), category.get(), none(), nullptr);
                    } }
            };

            bool warnings_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const object_{ type_object_for("object", "object") };
                if (sDeprecatedWrapper == nullptr)
                {
                    sDeprecatedWrapper = make_class_of("warnings", "_deprecated_wrapper", {}, sDeprecatedWrapperMethods);
                    sDeprecatedNew = make_class_of("warnings", "_deprecated_new", {}, sDeprecatedNewMethods);
                    sDeprecatedInitSubclass = make_class_of("warnings", "_deprecated_init_subclass", {}, sDeprecatedInitSubclassMethods);
                    if (sDeprecatedWrapper == nullptr || sDeprecatedNew == nullptr || sDeprecatedInitSubclass == nullptr)
                        return false;
                }
                ref const details{ make_tuple({ make_str("message"), make_str("category"), make_str("filename"), make_str("lineno"), make_str("file"), make_str("line"),
                    make_str("source") }) };
                ref const warningMessage{ make_class_of("warnings", "WarningMessage", {}, sWarningMessageMethods, { { "_WARNING_DETAILS", new_reference(details.get()) } }) };
                ref const catchWarnings{ make_class_of("warnings", "catch_warnings", {}, sCatchWarningsMethods) };
                ref const deprecated{ make_class_of("warnings", "deprecated", {}, sDeprecatedMethods) };
                ref const optionError{ module_exception("warnings._OptionError", { "Exception" }) };
                if (!warningMessage || !catchWarnings || !deprecated || !optionError)
                    return false;
                aAttributes.emplace_back("WarningMessage", new_reference(warningMessage.get()));
                aAttributes.emplace_back("catch_warnings", new_reference(catchWarnings.get()));
                aAttributes.emplace_back("deprecated", new_reference(deprecated.get()));
                aAttributes.emplace_back("_OptionError", new_reference(optionError.get()));
                for (auto const& f : sWarningsFunctions)
                {
                    auto const function = make_builtin(f);
                    if (std::strcmp(f.name, "showwarning") == 0)
                        aAttributes.emplace_back("_showwarning_orig", new_reference(function));
                    if (std::strcmp(f.name, "formatwarning") == 0)
                        aAttributes.emplace_back("_formatwarning_orig", new_reference(function));
                    aAttributes.emplace_back(f.name, function);
                }
                // (the default filters (CPython's, of a release build): sys.warnoptions's are added when the module has been made)
                std::vector<object*> filters;
                filters.push_back(make_tuple({ make_str("default"), new_reference(none()), exception_class("DeprecationWarning"), make_str("__main__"), make_int(0) }));
                for (auto c : { "DeprecationWarning", "PendingDeprecationWarning", "ImportWarning", "ResourceWarning" })
                    filters.push_back(make_tuple({ make_str("ignore"), new_reference(none()), exception_class(c), new_reference(none()), make_int(0) }));
                aAttributes.emplace_back("filters", make_list(std::move(filters)));
                aAttributes.emplace_back("defaultaction", make_str("default"));
                aAttributes.emplace_back("onceregistry", make_dict());
                aAttributes.emplace_back("_filters_version", make_int(1));
                aAttributes.emplace_back("_DEPRECATED_MSG", make_str("{name!r} is deprecated and slated for removal in Python {remove}"));
                aAttributes.emplace_back("_DEPRECATED_MSG", make_str(sDeprecatedMessage));
                aAttributes.emplace_back("__all__", make_list({ make_str("warn"), make_str("warn_explicit"), make_str("showwarning"), make_str("formatwarning"),
                    make_str("filterwarnings"), make_str("simplefilter"), make_str("resetwarnings"), make_str("catch_warnings"), make_str("deprecated") }));
                return true;
            }

            inline bool const sWarnings = register_module("warnings", warnings_module);
        }
    }
}
