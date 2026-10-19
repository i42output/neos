/*
  exec.cpp

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
*/

// exec, eval and compile: source that the program compiles while it runs is a program of its own (the host's load: the
// Python compiler's exec or eval mode), run on the calling thread with its namespaces (a stack of them: python::exec_lookup,
// exec_read and exec_store are its names'); its functions are called on its own program (see the program of a function
// object of the program).
//
// globals(): a module's namespace (a dict: the values of its names (its getter's), refreshed by each call; names that are
// bound in it that the module does not bind are found by its names' lookups (python::global_lookup))

#include <cstring>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct exec_namespace
            {
                object* globals;
                object* locals;     ///< (null: its globals)
                object* result = nullptr;
                void* program = nullptr;    ///< (the program running with it)
            };

            thread_local std::vector<exec_namespace> tNamespaces;
            thread_local void* tProgram = nullptr;      ///< (the program running (of exec): null for the program)
            thread_local void* tMainProgram = nullptr;  ///< (the program's context, once another has run)

            // (the code of source: compile's result)
            struct code_object
            {
                header h;
                std::string source;
                std::string filename;
                std::string mode;
                void* program = nullptr;    ///< (once loaded)
            };

            neos_py_type make_code_type()
            {
                auto type = make_type("code", "code", retain_object, release_object<code_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& c = *reinterpret_cast<code_object*>(aSelf);
                    return make_str("<code object <module> at " + object_address(aSelf) + ", file \"" + c.filename + "\", line 1>");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& c = *reinterpret_cast<code_object*>(aSelf);
                    std::string_view const name{ aName };
                    if (name == "co_filename")
                        return make_str(c.filename);
                    if (name == "co_name")
                        return make_str("<module>");
                    if (name == "co_qualname")
                        return make_str("<module>");
                    if (name == "co_firstlineno")
                        return make_int(1);
                    if (name == "co_argcount" || name == "co_posonlyargcount" || name == "co_kwonlyargcount" || name == "co_flags")
                        return make_int(0);
                    if (name == "co_names" || name == "co_varnames" || name == "co_consts" || name == "co_freevars" || name == "co_cellvars")
                        return make_tuple();
                    return error("AttributeError", std::string{ "'code' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            neos_py_type sCodeType = make_code_type();

            bool is_code(object* aObject) { return aObject != nullptr && aObject->type == &sCodeType.base; }
            code_object& as_code(object* aObject) { return *reinterpret_cast<code_object*>(aObject); }

            // (the source of compile's, exec's and eval's argument: str, or bytes (UTF-8))
            bool source_of(object* aObject, char const* aFunction, std::string& aSource)
            {
                if (kind_of(aObject) == NEOS_PY_STR)
                {
                    aSource = as<str_object>(aObject)->value;
                    return true;
                }
                std::string_view data;
                if (bytes_like(aObject, data))
                {
                    aSource.assign(data);
                    return true;
                }
                error("TypeError", std::string{ aFunction } + "() arg 1 must be a string, bytes or code object");
                return false;
            }

            // (a compile error of a source: SyntaxError (or IndentationError, TabError) with its location (the compiler's diagnostic: 
            // "file(line,offset[,end line,end offset]): type[#flags]: message"; its flags: t, its text is without its newline; c, it has no 
            // text (the compiler's); s, nor are its location and text its arguments (the symbol table's)))
            std::nullptr_t syntax_error(std::string const& aDiagnostic, std::string const& aFilename, std::string const& aSource)
            {
                static std::regex const sDiagnostic{ "^[\\s\\S]*?\\((\\d+),(\\d+)(?:,(\\d+),(\\d+))?\\): ([^:\\n]+): ([\\s\\S]*)$" };
                std::smatch match;
                if (!std::regex_match(aDiagnostic, match, sDiagnostic))
                {
                    std::string message = aDiagnostic;
                    if (auto const colon = message.rfind("error: "); colon != std::string::npos)
                        message = message.substr(colon + 7u);
                    return error("SyntaxError", message);
                }
                // (an offset: a 32-bit column (-1 is 4294967295))
                auto const number = [&](std::size_t aGroup) -> long long
                {
                    auto const value = std::stoll(match[aGroup].str());
                    return value > 0x7FFFFFFFll ? value - 0x100000000ll : value;
                };
                auto const line = number(1u);
                auto const offset = number(2u);
                auto const endLine = match[3].matched ? number(3u) : line;
                auto const endOffset = match[4].matched ? number(4u) : offset;
                std::string type = match[5].str();
                std::string flags;
                if (auto const hash = type.find('#'); hash != std::string::npos)
                {
                    flags = type.substr(hash + 1u);
                    type.erase(hash);
                }
                std::string message = match[6].str();
                if (type != "SyntaxError" && type != "IndentationError" && type != "TabError")
                {
                    if (type == "syntax error")
                        message = "invalid syntax";
                    type = "SyntaxError";
                }
                // (its text: its line)
                ref text;
                if (flags.find_first_of("cs") != std::string::npos)
                    text = ref{ (retain(none()), none()) };
                else
                {
                    std::size_t begin = 0u;
                    for (long long l = 1; l < line && begin != std::string::npos; ++l)
                    {
                        begin = aSource.find('\n', begin);
                        if (begin != std::string::npos)
                            ++begin;
                    }
                    std::string lineText;
                    if (begin != std::string::npos && begin <= aSource.size())
                    {
                        auto end = aSource.find('\n', begin);
                        lineText = aSource.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                        while (!lineText.empty() && lineText.back() == '\r')
                            lineText.pop_back();
                    }
                    if (flags.find('t') == std::string::npos)
                        lineText += "\n";
                    text = ref{ make_str(lineText) };
                }
                ref const filename{ make_str(aFilename) };
                ref const messageObject{ make_str(message) };
                ref exceptionClass{ fn_builtin(type.c_str()) };
                if (!exceptionClass)
                    return nullptr;
                ref const location{ make_tuple({ new_reference(filename.get()), make_int(line), make_int(offset), new_reference(text.get()), make_int(endLine), 
                    make_int(endOffset) }) };
                bool const located = flags.find('s') == std::string::npos;
                ref const args{ located ? make_tuple({ new_reference(messageObject.get()), new_reference(location.get()) }) : 
                    make_tuple({ new_reference(messageObject.get()) }) };
                ref const exception{ call(exceptionClass.get(), args.get(), nullptr) };
                if (!exception)
                    return nullptr;
                if (!located)
                {
                    static char const* const sFields[] = { "filename", "lineno", "offset", "text", "end_lineno", "end_offset" };
                    auto const& items = as<tuple_object>(location.get())->items;
                    for (std::size_t i = 0u; i < std::size(sFields); ++i)
                        if (setattr(exception.get(), sFields[i], items[i]) != 0)
                            return nullptr;
                }
                fn_raise(exception.get());
                return nullptr;
            }

            // (a code object's program (loaded once; the same source in the same mode is loaded once))
            void* program_of(code_object& aCode)
            {
                if (aCode.program != nullptr)
                    return aCode.program;
                static std::mutex sMutex;
                static std::map<std::pair<std::string, std::string>, void*> sPrograms;
                {
                    std::scoped_lock lock{ sMutex };
                    if (auto const existing = sPrograms.find({ aCode.mode, aCode.source }); existing != sPrograms.end())
                        return aCode.program = existing->second;
                }
                auto const* h = host();
                if (h == nullptr || h->version < NEOS_RUNTIME_PLUGIN_VERSION || h->load == nullptr)
                {
                    error("SystemError", "source cannot be compiled while the program runs");
                    return nullptr;
                }
                // (a program running in restricted mode does not compile source: a trap, as for its other restricted functions)
                if (!host_unrestricted(aCode.mode == "eval" ? "eval" : "exec"))
                    return nullptr;
                char const* failure = nullptr;
                auto const program = h->load("python", aCode.source.c_str(), aCode.filename.c_str(), aCode.mode == "eval" ? "eval" : "exec", &failure);
                if (program == nullptr)
                {
                    // (a program that runs without the neos compiler (an executable image): source cannot be compiled)
                    if (failure != nullptr && std::string_view{ failure } == "no compiler")
                    {
                        error("RuntimeError", "exec(), eval() and compile() need the neos compiler, which this executable image does not contain");
                        return nullptr;
                    }
                    // (a program running in restricted mode does not compile source)
                    if (failure != nullptr && std::string_view{ failure } == "restricted")
                    {
                        error("RuntimeError", "exec(), eval() and compile() are restricted: the program is running in restricted mode");
                        return nullptr;
                    }
                    // (a parse that failed (the compiler's "compilation failed"): invalid syntax)
                    syntax_error(failure != nullptr && std::string_view{ failure } != "compilation failed" ? failure : "invalid syntax", aCode.filename, aCode.source);
                    return nullptr;
                }
                std::scoped_lock lock{ sMutex };
                sPrograms[{ aCode.mode, aCode.source }] = program;
                return aCode.program = program;
            }

            object* make_code(std::string aSource, std::string aFilename, std::string aMode)
            {
                auto const c = new code_object{ { { &sCodeType.base } }, std::move(aSource), std::move(aFilename), std::move(aMode) };
                return &c->h.base;
            }

            // (run code with its namespaces: its result (eval's), or None)
            object* run_code(object* aCode, object* aGlobals, object* aLocals, char const* aFunction)
            {
                if (aGlobals != nullptr && kind_of(aGlobals) == NEOS_PY_NONE)
                    aGlobals = nullptr;
                if (aLocals != nullptr && kind_of(aLocals) == NEOS_PY_NONE)
                    aLocals = nullptr;
                if (aGlobals != nullptr && kind_of(builtin_value(aGlobals)) != NEOS_PY_DICT)
                    return error("TypeError", std::string{ aFunction } + "() globals must be a dict, not " + type_name(aGlobals));
                ref globals;
                if (aGlobals == nullptr)
                {
                    // (the namespace of the module __main__)
                    globals = ref{ fn_module_namespace("__main__", nullptr) };
                    if (!globals)
                        return nullptr;
                }
                else
                    globals = ref::borrowed(aGlobals);
                auto& code = as_code(aCode);
                auto const program = program_of(code);
                if (program == nullptr)
                    return nullptr;
                auto const* h = host();
                if (tProgram == nullptr && tMainProgram == nullptr)
                    tMainProgram = h->thread_context();
                tNamespaces.push_back(exec_namespace{ globals.get(), aLocals, nullptr, program });
                auto const previous = tProgram;
                tProgram = program;
                neos_rt_value result{};
                auto const trapped = h->run(program, NEOS_RT_VOID, &result);
                tProgram = previous;
                auto const ns = tNamespaces.back();
                tNamespaces.pop_back();
                ref value{ ns.result };
                if (trapped != nullptr)
                {
                    // (an exception that the code raised: "type: message")
                    if (!failed())
                    {
                        std::string const text = trapped;
                        auto const colon = text.find(": ");
                        if (colon == std::string::npos)
                            python_runtime::raise(text, "");
                        else
                            python_runtime::raise(text.substr(0u, colon), text.substr(colon + 2u));
                    }
                    return nullptr;
                }
                if (failed())
                    return nullptr;
                if (!value)
                    return (retain(none()), none());
                return value.release_ownership();
            }

            // (a mapping's item: null (no exception) if it has none)
            object* mapping_find(object* aMapping, char const* aName)
            {
                ref const key{ make_str(aName) };
                auto const value = builtin_value(aMapping);
                if (kind_of(value) == NEOS_PY_DICT)
                {
                    auto const position = dict_find(value, key.get());
                    if (position < 0)
                    {
                        if (position == -2)
                            return nullptr;
                        return nullptr;
                    }
                    auto const item = as<dict_object>(value)->items[static_cast<std::size_t>(position)].second;
                    retain(item);
                    return item;
                }
                auto const item = getitem(aMapping, key.get());
                if (item == nullptr && std::string_view{ error_text() }.starts_with("KeyError"))
                    clear_error();
                return item;
            }

            // (the getters of the modules' names: by their names)
            std::map<std::string, object*, std::less<>>& namespace_getters()
            {
                static std::map<std::string, object*, std::less<>> sGetters;
                return sGetters;
            }

            // (the namespaces of the modules (globals()): by their names)
            std::map<std::string, object*, std::less<>>& module_namespaces()
            {
                static std::map<std::string, object*, std::less<>> sNamespaces;
                return sNamespaces;
            }
        }

        // (a function of a program that exec loaded: called on its own program)
        void* current_program()
        {
            return tProgram;
        }

        void* program_context(void* aProgram)
        {
            // (the program: the context of the program that exec ran from, while an exec's program runs)
            if (aProgram == nullptr)
                return tProgram != nullptr ? tMainProgram : nullptr;
            return aProgram;
        }

        void* switch_program(void* aProgram)
        {
            auto const previous = tProgram;
            if (aProgram == previous)
                return previous;
            auto const* h = host();
            if (tProgram == nullptr && tMainProgram == nullptr)
                tMainProgram = h->thread_context();
            h->thread_attach(aProgram != nullptr ? aProgram : tMainProgram);
            tProgram = aProgram;
            return previous;
        }

        object* exec_current_globals()
        {
            return tNamespaces.empty() ? nullptr : tNamespaces.back().globals;
        }

        void exec_enter(object* aGlobals)
        {
            retain(aGlobals);
            tNamespaces.push_back(exec_namespace{ aGlobals, nullptr, nullptr, tProgram });
        }

        object* exec_globals_of(void* aProgram)
        {
            if (aProgram == nullptr)
                return nullptr;
            for (auto ns = tNamespaces.rbegin(); ns != tNamespaces.rend(); ++ns)
                if (ns->program == aProgram)
                    return ns->globals;
            return nullptr;
        }

        void exec_leave()
        {
            if (tNamespaces.empty())
                return;
            auto const ns = tNamespaces.back();
            tNamespaces.pop_back();
            release(ns.result);
            release(ns.globals);
        }

        // python::module_namespace(name, getter): globals()
        object* fn_module_namespace(char const* aName, object* aGetter)
        {
            auto& ns = module_namespaces()[aName];
            if (ns == nullptr)
            {
                ns = make_dict();
                auto const set = [&](char const* aKey, object* aValue)
                {
                    ref const key{ make_str(aKey) };
                    ref const value{ aValue };
                    dict_set(ns, key.get(), value.get());
                };
                set("__name__", make_str(aName));
                set("__doc__", (retain(none()), none()));
                set("__package__", std::strcmp(aName, "__main__") == 0 ? (retain(none()), none()) : make_str(""));
                set("__loader__", (retain(none()), none()));
                set("__spec__", (retain(none()), none()));
                set("__annotations__", make_dict());
                ref builtins{ fn_import("builtins") };
                if (!builtins)
                {
                    clear_error();
                    builtins = ref{ (retain(none()), none()) };
                }
                set("__builtins__", builtins.release_ownership());
                if (std::strcmp(aName, "__main__") == 0)
                    set("__cached__", (retain(none()), none()));
            }
            if (aGetter == nullptr || kind_of(aGetter) == NEOS_PY_NONE)
            {
                auto const registered = namespace_getters().find(aName);
                aGetter = registered != namespace_getters().end() ? registered->second : nullptr;
            }
            if (aGetter != nullptr && kind_of(aGetter) != NEOS_PY_NONE)
            {
                // (the module's names: their values (the namespace's items set without binding them); its items set: their 
                // names bound)
                auto& d = *as<dict_object>(ns);
                if (d.binder != aGetter)
                {
                    retain(aGetter);
                    release(d.binder);
                    d.binder = aGetter;
                }
                d.binder = nullptr;
                struct rebind { dict_object& d; object* getter; ~rebind() { d.binder = getter; } } const restore{ d, aGetter };
                ref const names{ call_module_getter(aGetter, -1) };
                if (!names)
                    return nullptr;
                auto const& items = as<tuple_object>(names.get())->items;
                for (std::size_t i = 0u; i < items.size(); ++i)
                {
                    ref const value{ call_module_getter(aGetter, static_cast<long long>(i)) };
                    if (!value)
                    {
                        if (failed())
                            return nullptr;
                        continue;
                    }
                    dict_set(ns, items[i], value.get());
                }
            }
            retain(ns);
            return ns;
        }

        bool namespace_bind(object* aGetter, object* aKey, object* aValue)
        {
            if (kind_of(aKey) != NEOS_PY_STR)
                return true;
            ref const names{ call_module_getter(aGetter, -1) };
            if (!names)
                return false;
            auto const& items = as<tuple_object>(names.get())->items;
            for (std::size_t i = 0u; i < items.size(); ++i)
                if (as<str_object>(items[i])->value == as<str_object>(aKey)->value)
                {
                    ref const result{ call_module_getter(aGetter, static_cast<long long>(i), aValue) };
                    return !!result || !failed();
                }
            return true;
        }

        // python::register_namespace(module, getter): the getter of a module's names (its namespace's)
        void fn_register_namespace(char const* aName, object* aGetter)
        {
            auto& g = namespace_getters()[aName];
            retain(aGetter);
            release(g);
            g = aGetter;
        }

        // python::global_lookup(module, name): a name that a module does not bind (one bound in its namespace (globals()), or a
        // builtin)
        object* fn_global_lookup(char const* aModule, char const* aName)
        {
            if (auto const ns = module_namespaces().find(aModule); ns != module_namespaces().end())
                if (auto const value = mapping_find(ns->second, aName))
                    return value;
            return fn_builtin(aName);
        }

        // python::import_star(module, from): from m import * of a module that is not a source of the program's (a runtime module): its 
        // public names (its __all__, or its names that do not begin with an underscore) bound in the module's namespace (the module 
        // finds them by its lookups (python::global_lookup))
        void fn_import_star(char const* aModule, object* aFrom)
        {
            ref const ns{ fn_module_namespace(aModule, nullptr) };
            if (!ns)
                return;
            release(py_guarded([&]()
            {
                py const from = py::borrowed(aFrom);
                py const nsDict = py::borrowed(ns.get());
                py names;
                try
                {
                    names = from.attr("__all__");
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                    py_take_exception();
                    std::vector<py> publicNames;
                    for (auto const& n : builtin("list")(from.attr("__dict__")).items())
                        if (!n.str().starts_with("_"))
                            publicNames.push_back(n);
                    names = py_list(publicNames);
                }
                for (auto const& n : names.items())
                    nsDict.set_item(n, builtin("getattr")(from, n));
                return py::none();
            }));
        }

        // python::exec_lookup(name): a name of code that exec runs (in its locals, its globals, then the builtins)
        object* fn_exec_lookup(char const* aName)
        {
            if (!tNamespaces.empty())
            {
                auto const& ns = tNamespaces.back();
                if (ns.locals != nullptr)
                {
                    if (auto const value = mapping_find(ns.locals, aName))
                        return value;
                    if (failed())
                        return nullptr;
                }
                if (auto const value = mapping_find(ns.globals, aName))
                    return value;
                if (failed())
                    return nullptr;
                // (its builtins: __builtins__ of its globals, if it is a dict (only its names))
                if (ref const builtins{ mapping_find(ns.globals, "__builtins__") }; builtins && kind_of(builtin_value(builtins.get())) == NEOS_PY_DICT)
                {
                    if (auto const value = mapping_find(builtins.get(), aName))
                        return value;
                    if (failed())
                        return nullptr;
                    // (the compiler's builtins (e.g. __exec_result) are not Python's)
                    std::string_view const name{ aName };
                    static std::set<std::string_view> const sPython = { "__import__", "__build_class__", "__debug__", "__doc__", "__loader__", "__name__",
                        "__package__", "__spec__" };
                    if (!name.starts_with("__") || sPython.contains(name))
                        return error("NameError", "name '" + std::string{ aName } + "' is not defined");
                }
            }
            return fn_builtin(aName);
        }

        // python::exec_globals(): globals() of code that exec runs
        object* fn_exec_globals()
        {
            if (tNamespaces.empty())
                return fn_module_namespace("__main__", nullptr);
            retain(tNamespaces.back().globals);
            return tNamespaces.back().globals;
        }

        // python::exec_read(name, value): a variable of code that exec runs (unbound (null): its namespaces')
        object* fn_exec_read(char const* aName, object* aValue)
        {
            if (aValue != nullptr)
            {
                retain(aValue);
                return aValue;
            }
            return fn_exec_lookup(aName);
        }

        // python::exec_store(name, value): a name that code that exec runs binds (its locals', or its globals')
        void fn_exec_store(char const* aName, object* aValue)
        {
            if (aValue == nullptr || tNamespaces.empty())
                return;
            auto const& ns = tNamespaces.back();
            auto const target = ns.locals != nullptr ? ns.locals : ns.globals;
            ref const key{ make_str(aName) };
            setitem(target, key.get(), aValue);
        }

        // __exec_result(value): eval's result
        object* b_exec_result(std::vector<object*> const& aArgs, object*)
        {
            if (tNamespaces.empty() || aArgs.size() != 1u)
                return error("SystemError", "__exec_result(value) outside of eval");
            retain(aArgs[0]);
            release(tNamespaces.back().result);
            tNamespaces.back().result = aArgs[0];
            return (retain(none()), none());
        }

        // compile(source, filename, mode, flags=0, dont_inherit=False, optimize=-1)
        object* b_compile(std::vector<object*> const& aArgs, object* aKwargs)
        {
            std::vector<object*> a;
            if (!parse_arguments("compile", aArgs, aKwargs, { "source", "filename", "mode", "flags", "dont_inherit", "optimize", "_feature_version" }, 3u, 0u, a))
                return nullptr;
            if (is_code(a[0]))
                return (retain(a[0]), a[0]);
            std::string source;
            if (!source_of(a[0], "compile", source))
                return nullptr;
            std::string filename;
            if (kind_of(a[1]) == NEOS_PY_STR)
                filename = as<str_object>(a[1])->value;
            else if (std::string_view data; bytes_like(a[1], data))
                filename.assign(data);
            else
                return error("TypeError", std::string{ "compile() argument 'filename' must be str, bytes or os.PathLike, not " } + type_name(a[1]));
            if (kind_of(a[2]) != NEOS_PY_STR)
                return error("TypeError", std::string{ "compile() argument 'mode' must be str, not " } + type_name(a[2]));
            auto const& mode = as<str_object>(a[2])->value;
            if (mode != "exec" && mode != "eval" && mode != "single")
                return error("ValueError", "compile() mode must be 'exec', 'eval' or 'single'");
            if (source.find('\0') != std::string::npos)
                return error("SyntaxError", "source code string cannot contain null bytes");
            ref code{ make_code(source, filename, mode == "single" ? "exec" : mode) };
            // (compiled now: SyntaxError)
            if (program_of(as_code(code.get())) == nullptr)
                return nullptr;
            return code.release_ownership();
        }

        object* exec_or_eval(std::vector<object*> const& aArgs, object* aKwargs, bool aEval)
        {
            char const* const function = aEval ? "eval" : "exec";
            std::vector<object*> a;
            if (!parse_arguments(function, aArgs, aKwargs, { "source", "globals", "locals", "closure" }, 1u, 1u, a))
                return nullptr;
            ref code;
            if (is_code(a[0]))
                code = ref::borrowed(a[0]);
            else
            {
                std::string source;
                if (!source_of(a[0], function, source))
                    return nullptr;
                if (aEval)
                {
                    // (eval: leading spaces and tabs are not indentation)
                    auto const first = source.find_first_not_of(" \t");
                    source = first == std::string::npos ? std::string{} : source.substr(first);
                }
                code = ref{ make_code(source, "<string>", aEval ? "eval" : "exec") };
            }
            auto const globals = a.size() > 1u ? a[1] : nullptr;
            auto const locals = a.size() > 2u ? a[2] : nullptr;
            ref const result{ run_code(code.get(), globals, locals, function) };
            if (!result)
                return nullptr;
            if (!aEval)
                return (retain(none()), none());
            return (retain(result.get()), result.get());
        }

        object* b_exec(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return exec_or_eval(aArgs, aKwargs, false);
        }

        object* b_eval(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return exec_or_eval(aArgs, aKwargs, true);
        }

        // (a dict's keys, sorted: dir())
        object* fn_sorted_keys(object* aMapping)
        {
            ref const keys{ make_list() };
            ref const it{ iter(aMapping) };
            if (!it)
                return nullptr;
            for (;;)
            {
                ref const key{ next(it.get()) };
                if (!key)
                {
                    if (failed())
                        return nullptr;
                    break;
                }
                as<list_object>(keys.get())->items.push_back((retain(key.get()), key.get()));
            }
            return b_sorted({ keys.get() }, nullptr);
        }
    }
}
