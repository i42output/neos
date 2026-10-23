/*
  exec.cpp

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

  Portions of this file are derived from CPython 3.13 (Python/bltinmodule.c's __import__, Python/import.c's
  PyImport_ImportModuleLevelObject and resolve_name, and Lib/importlib/_bootstrap.py's _handle_fromlist), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos Python runtime
  plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// exec, eval and compile: source that the program compiles while it runs is a program of its own (the host's load: the
// Python compiler's exec or eval mode), run on the calling thread with its namespaces (a stack of them: python::exec_lookup,
// exec_read and exec_store are its names'); its functions are called on its own program (see the program of a function
// object of the program).
//
// globals(): a module's namespace (a dict: the values of its names (its getter's), refreshed by each call; names that are
// bound in it that the module does not bind are found by its names' lookups (python::global_lookup))

#include <algorithm>
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
                bool display = false;       ///< ('single' mode's expression: its value is shown (sys.displayhook))
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
                if (code.display)
                {
                    // ('single' mode's expression statement: its value shown)
                    ref const hook{ module_attribute("sys", "displayhook") };
                    if (!hook)
                        return nullptr;
                    ref const shown{ invoke(hook.get(), { value ? value.release_ownership() : (retain(none()), none()) }) };
                    if (!shown)
                        return nullptr;
                    return (retain(none()), none());
                }
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
            long long flags = 0;
            if (a[3] != nullptr && !to_index(a[3], flags))
                return nullptr;
            bool const incompleteInput = mode == "single" && (flags & 0x4000) != 0;    // (PyCF_ALLOW_INCOMPLETE_INPUT)
            bool const impliedDedent = (flags & 0x200) == 0;                            // (PyCF_DONT_IMPLY_DEDENT)
            // (the statement of 'single' mode: its first line's)
            std::string_view first;
            for (std::size_t line = 0u; line < source.size();)
            {
                auto const end = std::min(source.find('\n', line), source.size());
                auto text = std::string_view{ source }.substr(line, end - line);
                auto const start = text.find_first_not_of(" \t\r\f");
                if (start != std::string_view::npos && text[start] != '#')
                {
                    first = text.substr(start);
                    break;
                }
                line = end + 1u;
            }
            // (input that is incomplete (PyCF_ALLOW_INCOMPLETE_INPUT, codeop's): _IncompleteInputError)
            auto const incomplete = [&](object* aException) -> std::nullptr_t
            {
                ref const location{ make_tuple({ make_str(filename), aException ? getattr(aException, "lineno") : make_int(1),
                    aException ? getattr(aException, "offset") : make_int(1), aException ? getattr(aException, "text") : (retain(none()), none()),
                    aException ? getattr(aException, "end_lineno") : (retain(none()), none()),
                    aException ? getattr(aException, "end_offset") : (retain(none()), none()) }) };
                clear_error();
                ref const exceptionClass{ fn_builtin("_IncompleteInputError") };
                ref const args{ make_tuple({ make_str("incomplete input"), new_reference(location.get()) }) };
                ref const exception{ exceptionClass ? call(exceptionClass.get(), args.get(), nullptr) : nullptr };
                if (exception)
                    fn_raise(exception.get());
                return nullptr;
            };
            // (a compound statement (or a decorator): complete once its input ends with a newline (its block's dedent not implied))
            static std::regex const sCompound{ "^(?:(?:if|while|for|def|class|try|with|async)\\b|@|match\\b.*:\\s*(?:#.*)?$)" };
            bool const compound = std::regex_search(first.begin(), first.end(), sCompound);
            // ('single' mode: one statement (a line at column 0 outside brackets and strings, after the first, is another statement, 
            // unless it continues a compound statement))
            if (mode == "single" && !first.empty())
            {
                int depth = 0;
                char quote = 0;
                bool triple = false;
                bool statement = false;
                bool lineStart = true;
                for (std::size_t i = 0u; i < source.size(); ++i)
                {
                    auto const c = source[i];
                    if (quote != 0)
                    {
                        if (c == '\\')
                            ++i;
                        else if (c == quote && (!triple || source.compare(i, 3u, std::string(3u, quote)) == 0))
                        {
                            if (triple)
                                i += 2u;
                            quote = 0;
                        }
                        else if (c == '\n' && !triple)
                            quote = 0;
                        continue;
                    }
                    if (lineStart && depth == 0 && c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '#')
                    {
                        auto const rest = std::string_view{ source }.substr(i);
                        static std::regex const sContinuation{ "^(?:else|elif|except|finally|case)\\b" };
                        if (statement && !std::regex_search(rest.begin(), rest.end(), sContinuation))
                            return error("SyntaxError", compound ? "invalid syntax" : "multiple statements found while compiling a single statement");
                        statement = true;
                    }
                    lineStart = false;
                    if (c == '#')
                        while (i + 1u < source.size() && source[i + 1u] != '\n')
                            ++i;
                    else if (c == '\'' || c == '"')
                    {
                        quote = c;
                        triple = source.compare(i, 3u, std::string(3u, c)) == 0;
                        if (triple)
                            i += 2u;
                    }
                    else if (c == '(' || c == '[' || c == '{')
                        ++depth;
                    else if ((c == ')' || c == ']' || c == '}') && depth > 0)
                        --depth;
                    else if (c == '\\' && i + 1u < source.size() && source[i + 1u] == '\n')
                        ++i;
                    else if (c == '\n')
                        lineStart = true;
                }
            }
            // ('single' mode's expression: shown when the code is run)
            if (mode == "single" && !first.empty())
            {
                ref expression{ make_code(source, filename, "eval") };
                if (program_of(as_code(expression.get())) != nullptr)
                {
                    as_code(expression.get()).display = true;
                    return expression.release_ownership();
                }
                clear_error();
            }
            ref code{ make_code(source, filename, mode == "single" ? "exec" : mode) };
            // (compiled now: SyntaxError)
            if (program_of(as_code(code.get())) == nullptr)
            {
                if (!incompleteInput)
                    return nullptr;
                // (its input ended before its statement, bracket or string did)
                ref const exception{ take_exception() };
                ref const message{ exception ? getattr(exception.get(), "msg") : nullptr };
                auto const text = message && kind_of(message.get()) == NEOS_PY_STR ? as<str_object>(message.get())->value : std::string{};
                clear_error();
                // (or a compound statement whose input ended without a newline: what follows it can complete it (e.g. try's except))
                auto const lines = static_cast<long long>(std::count(source.begin(), source.end(), '\n')) + 1;
                long long line = 0;
                ref const lineno{ exception ? getattr(exception.get(), "lineno") : nullptr };
                if (!lineno || !to_index(lineno.get(), line))
                    clear_error();
                if (text.starts_with("expected an indented block") || text.ends_with("was never closed") ||
                    text.starts_with("unterminated triple-quoted string literal") || text.starts_with("unexpected EOF") ||
                    (compound && first.front() == '@') || (compound && !impliedDedent && ((!source.ends_with('\n') && line >= lines) ||
                    (text == "expected 'except' or 'finally' block" && !source.ends_with("\n\n")))))
                    return incomplete(exception.get());
                if (exception)
                    reraise_exception(exception.get());
                return nullptr;
            }
            if (incompleteInput && !impliedDedent && compound && !source.ends_with('\n'))
                return incomplete(nullptr);
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

        // --- __import__ (Python/bltinmodule.c's builtin___import__, which is Python/import.c's PyImport_ImportModuleLevelObject and
        // resolve_name, and Lib/importlib/_bootstrap.py's _handle_fromlist): a module that the program has not imported is imported
        // by an import statement run as exec's source (the compiler finds its source and compiles it); a name that is not a dotted
        // name of identifiers is no module's (ModuleNotFoundError: it is not source)
        namespace
        {
            bool module_name(std::string const& aName)
            {
                static std::set<std::string_view> const sKeywords = { "False", "None", "True", "and", "as", "assert", "async", "await",
                    "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import",
                    "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield" };
                std::size_t start = 0u;
                for (;;)
                {
                    auto const dot = std::min(aName.find('.', start), aName.size());
                    auto const part = std::string_view{ aName }.substr(start, dot - start);
                    if (part.empty() || sKeywords.contains(part) || !py{ std::string{ part } }.method("isidentifier").truthy())
                        return false;
                    if (dot == aName.size())
                        return true;
                    start = dot + 1u;
                }
            }

            [[noreturn]] void module_not_found(std::string const& aMessage, std::string const& aName)
            {
                py_raise(builtin("ModuleNotFoundError").call({ py{ aMessage } }, { { "name", py{ aName } } }));
            }

            // (import_get_module and import_find_and_load: an absolute name's module (its parent's first))
            py find_and_load(std::string const& aName)
            {
                py const modules = import_attribute("sys", "modules");
                py const key{ aName };
                auto const existing = [&]() -> py
                {
                    if (!modules.contains(key))
                        return {};
                    py m = modules[key];
                    if (m.is_none())
                        module_not_found("import of " + aName + " halted; None in sys.modules", aName);
                    return m;
                };
                if (auto m = existing(); !m.empty())
                    return m;
                auto const dot = aName.rfind('.');
                py parent;
                if (dot != std::string::npos && dot > 0u)
                    parent = find_and_load(aName.substr(0u, dot));
                if (auto m = existing(); !m.empty())
                    return m;
                if (!module_name(aName))
                    module_not_found("No module named " + key.repr(), aName);
                try
                {
                    builtin("exec")(py{ "import " + aName }, py_dict());
                }
                catch (raised const&)
                {
                    // (its traceback: without the import statement's frame (as CPython's remove_importlib_frames))
                    py const e = py_take_exception();
                    py tb = e.attr("__traceback__");
                    while (!tb.is_none() && tb.attr("tb_frame").attr("f_code").attr("co_filename").str() != "<string>")
                        tb = tb.attr("tb_next");
                    if (!tb.is_none())
                        e.method("with_traceback", { tb.attr("tb_next") });
                    py_reraise(e);
                }
                if (auto m = existing(); !m.empty())
                    return m;
                // (a module that is its parent's (e.g. os.path) but not in sys.modules)
                if (!parent.empty())
                {
                    py const child = builtin("getattr")(parent, py{ aName.substr(dot + 1u) }, py::none());
                    if (isinstance(child, type_of(parent)))
                        return child;
                }
                module_not_found("No module named " + key.repr(), aName);
            }

            // (a dict's item: empty if it has none)
            py item_of(py const& aDict, char const* aKey)
            {
                py const key{ aKey };
                return aDict.contains(key) ? aDict[key] : py{};
            }

            // (resolve_name: a relative import's absolute name)
            std::string resolve_name(std::string const& aName, py const& aGlobals, long long aLevel)
            {
                if (aGlobals.empty())
                    py_raise(builtin("KeyError")(py{ "'__name__' not in globals" }));
                if (!isinstance(aGlobals, builtin("dict")))
                    py_raise("TypeError", "globals must be a dict");
                py package = item_of(aGlobals, "__package__");
                if (!package.empty() && package.is_none())
                    package = py{};
                py const spec = item_of(aGlobals, "__spec__");
                py const str = builtin("str");
                auto const warn = [](char const* aMessage, char const* aCategory)
                {
                    import_attribute("warnings", "warn")(py{ aMessage }, builtin(aCategory));
                };
                if (!package.empty())
                {
                    if (!isinstance(package, str))
                        py_raise("TypeError", "package must be a string");
                    else if (!spec.empty() && !spec.is_none() && package != spec.attr("parent"))
                        warn("__package__ != __spec__.parent", "DeprecationWarning");
                }
                else if (!spec.empty() && !spec.is_none())
                {
                    package = spec.attr("parent");
                    if (!isinstance(package, str))
                        py_raise("TypeError", "__spec__.parent must be a string");
                }
                else
                {
                    warn("can't resolve package from __spec__ or __package__, falling back on __name__ and __path__", "ImportWarning");
                    package = item_of(aGlobals, "__name__");
                    if (package.empty())
                        py_raise(builtin("KeyError")(py{ "'__name__' not in globals" }));
                    if (!isinstance(package, str))
                        py_raise("TypeError", "__name__ must be a string");
                    if (!aGlobals.contains(py{ "__path__" }))
                    {
                        auto const name = package.str();
                        auto const dot = name.rfind('.');
                        if (dot == std::string::npos)
                            py_raise("ImportError", "attempted relative import with no known parent package");
                        package = py{ name.substr(0u, dot) };
                    }
                }
                auto const base = package.str();
                auto lastDot = base.size();
                if (lastDot == 0u)
                    py_raise("ImportError", "attempted relative import with no known parent package");
                for (long long levelUp = 1; levelUp < aLevel; ++levelUp)
                {
                    auto const dot = lastDot == 0u ? std::string::npos : base.rfind('.', lastDot - 1u);
                    if (dot == std::string::npos)
                        py_raise("ImportError", "attempted relative import beyond top-level package");
                    lastDot = dot;
                }
                return aName.empty() ? base.substr(0u, lastDot) : base.substr(0u, lastDot) + "." + aName;
            }

            // (a package (its submodules can be in a fromlist): it has __path__, or its source is a package's (__init__.py))
            bool is_package(py const& aModule)
            {
                if (builtin("hasattr")(aModule, py{ "__path__" }).truthy())
                    return true;
                py const file = builtin("getattr")(aModule, py{ "__file__" }, py::none());
                if (!isinstance(file, builtin("str")))
                    return false;
                auto const path = file.str();
                auto const slash = path.find_last_of("/\\");
                return path.substr(slash == std::string::npos ? 0u : slash + 1u) == "__init__.py";
            }

            // (_handle_fromlist)
            void handle_fromlist(py const& aModule, py const& aFromlist, bool aRecursive)
            {
                for (auto const& x : aFromlist.items())
                {
                    if (!isinstance(x, builtin("str")))
                    {
                        auto const where = aRecursive ? aModule.attr("__name__").str() + ".__all__" : std::string{ "``from list''" };
                        py_raise("TypeError", "Item in " + where + " must be str, not " + type_of(x).attr("__name__").str());
                    }
                    else if (x.str() == "*")
                    {
                        if (!aRecursive && builtin("hasattr")(aModule, py{ "__all__" }).truthy())
                            handle_fromlist(aModule, aModule.attr("__all__"), true);
                    }
                    else if (!builtin("hasattr")(aModule, x).truthy())
                    {
                        auto const fromName = aModule.attr("__name__").str() + "." + x.str();
                        try
                        {
                            find_and_load(fromName);
                        }
                        catch (raised const&)
                        {
                            // (backwards-compatibility dictates that failed imports triggered by fromlist for modules that don't exist
                            // are ignored)
                            py const e = py_take_exception();
                            py const modules = import_attribute("sys", "modules");
                            py const key{ fromName };
                            if (!exception_is(e.get(), "ModuleNotFoundError") || e.attr("name") != key ||
                                (modules.contains(key) && modules[key].is_none()))
                                py_reraise(e);
                        }
                    }
                }
            }
        }

        // (an absolute name's module, imported if it is not (its parent's first): importlib's _find_and_load)
        py import_find_and_load(std::string const& aName)
        {
            return find_and_load(aName);
        }

        object* b_import(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return py_guarded([&]() -> py
            {
                std::vector<object*> a;
                py_checked(parse_arguments("__import__", aArgs, aKwargs, { "name", "globals", "locals", "fromlist", "level" }, 1u, 0u, a));
                py const name = py::borrowed(a[0]);
                py const globals = a[1] != nullptr ? py::borrowed(a[1]) : py{};
                py const fromlist = a[3] != nullptr ? py::borrowed(a[3]) : py{};
                long long const level = a[4] != nullptr ? py::borrowed(a[4]).as_index() : 0;
                if (!isinstance(name, builtin("str")))
                    py_raise("TypeError", "module name must be a string");
                if (level < 0)
                    py_raise("ValueError", "level must be >= 0");
                auto const text = name.str();
                std::string absName;
                if (level > 0)
                    absName = resolve_name(text, globals, level);
                else
                {
                    if (text.empty())
                        py_raise("ValueError", "Empty module name");
                    absName = text;
                }
                py const module = find_and_load(absName);
                bool const hasFrom = !fromlist.empty() && !fromlist.is_none() && fromlist.truthy();
                if (!hasFrom)
                {
                    if (level == 0 || !text.empty())
                    {
                        auto const dot = text.find('.');
                        if (dot == std::string::npos)
                            return module;
                        if (level == 0)
                            return find_and_load(text.substr(0u, dot));
                        auto const cutOff = text.size() - dot;
                        auto const toReturn = absName.substr(0u, absName.size() - cutOff);
                        py const modules = import_attribute("sys", "modules");
                        py const key{ toReturn };
                        if (!modules.contains(key))
                            py_raise(builtin("KeyError")(py{ key.repr() + " not in sys.modules as expected" }));
                        return modules[key];
                    }
                    return module;
                }
                if (is_package(module))
                    handle_fromlist(module, fromlist, false);
                return module;
            });
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
