/*
  traceback.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/linecache.py, Lib/traceback.py, Lib/_colorize.py and
  Python/suggestions.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the
  Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to
  C++ for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules linecache, traceback, _colorize and _suggestions (CPython 3.13's linecache.py, traceback.py, _colorize.py, _suggestions.c and
// Python/suggestions.c; the source files' encodings as tokenize.open detects them). The program's frames are the host's (see support.cpp): 
// their positions are their lines (a traceback's tb_lasti is -1: no columns, so no carets), and a function's frame has no locals (f_locals 
// is empty)

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        // --- helpers

        // (a function's arguments: its positional parameters aNames (the first aRequired are required), its keyword-only parameters, and
        // (aExtra) a dict of the other keyword arguments)
        struct parsed
        {
            std::vector<py> values;
            py extra;
            py const& operator[](std::size_t aIndex) const { return values[aIndex]; }
            py get(std::size_t aIndex, py const& aDefault) const { return values[aIndex].empty() ? aDefault : values[aIndex]; }
        };

        parsed parse(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames, std::size_t aRequired,
            std::vector<char const*> const& aKeywordOnly = {}, bool aExtra = false, std::size_t aPositionalOnly = 0u)
        {
            if (aArgs.size() > aNames.size())
                py_raise("TypeError", std::string{ aFunction } + "() takes " + std::to_string(aNames.size()) + " positional argument" + (aNames.size() == 1u ? "" : "s") +
                    " but " + std::to_string(aArgs.size()) + (aArgs.size() == 1u ? " was" : " were") + " given");
            std::vector<char const*> names = aNames;
            names.insert(names.end(), aKeywordOnly.begin(), aKeywordOnly.end());
            parsed result;
            if (aExtra)
                result.extra = py_dict();
            ref kwargs;
            if (aKwargs != nullptr)
            {
                kwargs = ref{ make_dict() };
                for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                {
                    auto const* name = str_of_value(key);
                    bool const known = name != nullptr && std::find_if(names.begin(), names.end(), [&](char const* n) { return *name == n; }) != names.end();
                    if (known || !aExtra)
                        py_checked(dict_set(kwargs.get(), key, value) == 0);
                    else
                        result.extra.set_item(py::borrowed(key), py::borrowed(value));
                }
            }
            std::vector<object*> a;
            py_checked(parse_arguments(aFunction, aArgs, kwargs.get(), names, aRequired, aPositionalOnly, a));
            for (auto v : a)
                result.values.push_back(v != nullptr ? py::borrowed(v) : py{});
            return result;
        }

        // (the pending exception: the position of the first of aClasses that it is an instance of (it is handled); otherwise it remains raised
        // (raised is thrown))
        std::size_t caught_which(std::initializer_list<char const*> aClasses)
        {
            ref const e = take_exception();
            std::size_t i = 0u;
            for (auto c : aClasses)
            {
                if (exception_is(e.get(), c))
                    return i;
                ++i;
            }
            reraise_exception(e.get());
            throw raised{};
        }

        // (a call of aBody (the pending exception, if any, is set aside while it runs, then raised again))
        template <typename F>
        void preserving_exception(F&& aBody)
        {
            ref const e = take_exception();
            release(py_guarded([&]() { aBody(); return py::none(); }));
            clear_error();
            reraise_exception(e.get());
        }

        bool is_str(py const& aValue) { return kind_of(aValue.get()) == NEOS_PY_STR; }
        std::string const& text_of(py const& aValue) { return as<str_object>(aValue.get())->value; }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py slice(py const& aSequence, py const& aStart, py const& aStop) { return aSequence[builtin("slice")(aStart, aStop)]; }
        py join(std::vector<py> const& aParts, char const* aSeparator = "") { return py{ aSeparator }.method("join", { py_list(aParts) }); }
        py id_of(py const& aObject) { return builtin("id")(aObject); }
        py keywords_get(py const& aExtra, char const* aName, py const& aDefault) { return aExtra.method("get", { py{ aName }, aDefault }); }

        // (lambda line: True)
        py always_true()
        {
            static object* sFunction = nullptr;
            if (sFunction == nullptr)
                sFunction = make_native_function("<lambda>", "<lambda>", "", "traceback", [](std::vector<py> const&, object*) { return py::boolean(true); }).release();
            return py::borrowed(sFunction);
        }

        py text_indent(py const& aText, py const& aPrefix, bool aEveryLine)
        {
            auto const indent = import_attribute("textwrap", "indent");
            return aEveryLine ? indent(aText, aPrefix, always_true()) : indent(aText, aPrefix);
        }

        py text_dedent(py const& aText) { return import_attribute("textwrap", "dedent")(aText); }

        // (print(aText, file=aFile, end=""))
        void print_to(py const& aText, py const& aFile)
        {
            builtin("print").call({ aText }, { { "file", aFile }, { "end", py{ "" } } });
        }

        // --- linecache

        object* sCache = nullptr;               // (linecache.cache: filenames' entries ((size, mtime, lines, fullname), or (a getter of their source,)))
        object* sInteractiveCache = nullptr;    // (linecache._interactive_cache)

        py line_cache()
        {
            if (sCache == nullptr)
                sCache = make_dict();
            return py::borrowed(sCache);
        }

        py interactive_cache()
        {
            if (sInteractiveCache == nullptr)
                sInteractiveCache = make_dict();
            return py::borrowed(sInteractiveCache);
        }

        // (not filename or (filename.startswith('<') and filename.endswith('>')))
        bool uncacheable(py const& aFilename)
        {
            return !aFilename.truthy() || (aFilename.method("startswith", { py{ "<" } }).truthy() && aFilename.method("endswith", { py{ ">" } }).truthy());
        }

        void clearcache() { line_cache().method("clear"); }

        // (the entry of a file whose module has a loader that has get_source: (a getter of its lines,); empty: none)
        py make_lazycache_entry(py const& aFilename, py const& aModuleGlobals)
        {
            if (uncacheable(aFilename))
                return {};
            if (aModuleGlobals.empty() || aModuleGlobals.is_none() || !aModuleGlobals.truthy() || !aModuleGlobals.contains(py{ "__name__" }))
                return {};
            auto const getattr_ = builtin("getattr");
            py const spec = aModuleGlobals.method("get", { py{ "__spec__" } });
            py name = getattr_(spec, py{ "name" }, none());
            if (!name.truthy())
                name = aModuleGlobals[py{ "__name__" }];
            py loader = getattr_(spec, py{ "loader" }, none());
            if (loader.is_none())
                loader = aModuleGlobals.method("get", { py{ "__loader__" } });
            py const getSource = getattr_(loader, py{ "get_source" }, none());
            if (name.truthy() && getSource.truthy())
            {
                // (def get_lines(name=name, *args, **kwargs): return get_source(name, *args, **kwargs))
                auto const getLines = make_native_function("get_lines", "_make_lazycache_entry.<locals>.get_lines", "", "linecache",
                    [name, getSource](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        std::vector<object*> args;
                        args.push_back((aArgs.empty() ? name : aArgs[0]).reference());
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            args.push_back(aArgs[i].reference());
                        return py{ invoke(getSource.get(), std::move(args), aKwargs) };
                    });
                return py_tuple({ getLines });
            }
            return {};
        }

        bool lazycache(py const& aFilename, py const& aModuleGlobals)
        {
            py const entry = line_cache().method("get", { aFilename, none() });
            if (!entry.is_none())
                return entry.size() == 1;
            py const lazy = make_lazycache_entry(aFilename, aModuleGlobals);
            if (!lazy.empty())
            {
                line_cache().set_item(aFilename, lazy);
                return true;
            }
            return false;
        }

        // (tokenize._get_normal_name)
        std::string normal_encoding_name(std::string const& aEncoding)
        {
            std::string e = aEncoding.substr(0u, 12u);
            for (auto& c : e)
                c = c == '_' ? '-' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            if (e == "utf-8" || e.starts_with("utf-8-"))
                return "utf-8";
            for (auto latin : { "latin-1", "iso-8859-1", "iso-latin-1" })
                if (e == latin || e.starts_with(std::string{ latin } + "-"))
                    return "iso-8859-1";
            return aEncoding;
        }

        // (tokenize's cookie_re: ^[ \t\f]*#.*?coding[:=][ \t]*([-\w.]+) (ASCII))
        std::optional<std::string> coding_cookie(std::string_view aLine)
        {
            std::size_t i = 0u;
            while (i < aLine.size() && (aLine[i] == ' ' || aLine[i] == '\t' || aLine[i] == '\f'))
                ++i;
            if (i >= aLine.size() || aLine[i] != '#')
                return std::nullopt;
            auto const word = [](char c) { return c == '-' || c == '.' || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
            for (auto p = i + 1u; p < aLine.size() && aLine[p] != '\n'; ++p)
            {
                if (aLine.substr(p, 6u) != "coding" || p + 6u >= aLine.size() || (aLine[p + 6u] != ':' && aLine[p + 6u] != '='))
                    continue;
                auto q = p + 7u;
                while (q < aLine.size() && (aLine[q] == ' ' || aLine[q] == '\t'))
                    ++q;
                auto const start = q;
                while (q < aLine.size() && word(aLine[q]))
                    ++q;
                if (q > start)
                    return std::string{ aLine.substr(start, q - start) };
            }
            return std::nullopt;
        }

        // (tokenize's blank_re: ^[ \t\f]*(?:[#\r\n]|$))
        bool blank_line(std::string_view aLine)
        {
            std::size_t i = 0u;
            while (i < aLine.size() && (aLine[i] == ' ' || aLine[i] == '\t' || aLine[i] == '\f'))
                ++i;
            return i == aLine.size() || aLine[i] == '#' || aLine[i] == '\r' || aLine[i] == '\n';
        }

        // (tokenize.detect_encoding of a file's first lines)
        std::string detect_encoding(py const& aReadline, py const& aFilename)
        {
            bool bom = false;
            std::string fallback = "utf-8";
            auto const read = [&]() -> std::string
            {
                py const line = aReadline();
                std::string_view data;
                if (!bytes_like(line.get(), data))
                    py_raise("TypeError", "readline() should return bytes");
                return std::string{ data };
            };
            auto const cookie = [&](std::string const& aLine) -> std::optional<std::string>
            {
                py const bytes{ make_bytes(aLine) };
                py lineString;
                try
                {
                    lineString = bytes.method("decode", { py{ "utf-8" } });
                }
                catch (raised const&)
                {
                    caught_which({ "UnicodeDecodeError" });
                    py_raise("SyntaxError", "invalid or missing encoding declaration for " + aFilename.repr());
                }
                auto const found = coding_cookie(text_of(lineString));
                if (!found)
                    return std::nullopt;
                auto encoding = normal_encoding_name(*found);
                try
                {
                    import_attribute("codecs", "lookup")(py{ encoding });
                }
                catch (raised const&)
                {
                    caught_which({ "LookupError" });
                    py_raise("SyntaxError", "unknown encoding for " + aFilename.repr() + ": " + encoding);
                }
                if (bom)
                {
                    if (encoding != "utf-8")
                        py_raise("SyntaxError", "encoding problem for " + aFilename.repr() + ": utf-8");
                    encoding += "-sig";
                }
                return encoding;
            };
            auto first = read();
            if (first.starts_with("\xEF\xBB\xBF"))
            {
                bom = true;
                first = first.substr(3u);
                fallback = "utf-8-sig";
            }
            if (first.empty())
                return fallback;
            if (auto const e = cookie(first))
                return *e;
            if (!blank_line(first))
                return fallback;
            auto const second = read();
            if (second.empty())
                return fallback;
            if (auto const e = cookie(second))
                return *e;
            return fallback;
        }

        // (tokenize.open(aFilename).readlines())
        py read_source_lines(py const& aFilename)
        {
            auto const open = builtin("open");
            std::string encoding;
            {
                py const buffer = open(aFilename, py{ "rb" });
                try
                {
                    encoding = detect_encoding(buffer.attr("readline"), aFilename);
                }
                catch (raised const&)
                {
                    preserving_exception([&]() { buffer.method("close"); });
                    throw;
                }
                buffer.method("close");
            }
            py const file = open.call({ aFilename, py{ "r" } }, { { "encoding", py{ encoding } } });
            py lines;
            try
            {
                lines = file.method("readlines");
            }
            catch (raised const&)
            {
                preserving_exception([&]() { file.method("close"); });
                throw;
            }
            file.method("close");
            return lines;
        }

        py updatecache(py const& aFilename, py const& aModuleGlobals)
        {
            py entry = line_cache().method("pop", { aFilename, none() });
            if (uncacheable(aFilename))
                return py_list();
            auto const os = import_attribute("os", "stat");
            auto const path = import_attribute("os", "path");
            py fullname = aFilename;
            py stat;
            bool statFailed = false;
            try
            {
                stat = os(fullname);
            }
            catch (raised const&)
            {
                if (caught_which({ "OSError", "ValueError" }) == 1u)
                    return py_list();
                statFailed = true;
            }
            if (statFailed)
            {
                py const basename = aFilename;
                // (a lazy loader's lookup, if there is one)
                py lazy = !entry.is_none() && entry.size() == 1 ? entry : py{};
                if (lazy.empty())
                    lazy = make_lazycache_entry(aFilename, aModuleGlobals);
                if (!lazy.empty())
                {
                    py data;
                    try
                    {
                        data = lazy[py{ 0 }]();
                    }
                    catch (raised const&)
                    {
                        caught_which({ "ImportError", "OSError" });
                    }
                    if (!data.empty())
                    {
                        if (data.is_none())
                            return py_list();
                        std::vector<py> lines;
                        for (auto const& l : data.method("splitlines").items())
                            lines.push_back(l + py{ "\n" });
                        py const e = py_tuple({ py{ data.size() }, none(), py_list(lines), fullname });
                        line_cache().set_item(aFilename, e);
                        return e[py{ 2 }];
                    }
                }
                // (the module search path, for a relative filename)
                if (path.attr("isabs")(aFilename).truthy())
                    return py_list();
                bool found = false;
                for (auto const& directory : import_attribute("sys", "path").items())
                {
                    try
                    {
                        fullname = path.attr("join")(directory, basename);
                    }
                    catch (raised const&)
                    {
                        caught_which({ "TypeError", "AttributeError" });
                        continue;
                    }
                    try
                    {
                        stat = os(fullname);
                        found = true;
                        break;
                    }
                    catch (raised const&)
                    {
                        caught_which({ "OSError", "ValueError" });
                    }
                }
                if (!found)
                    return py_list();
            }
            py lines;
            try
            {
                lines = read_source_lines(fullname);
            }
            catch (raised const&)
            {
                caught_which({ "OSError", "UnicodeDecodeError", "SyntaxError" });
                return py_list();
            }
            if (lines.size() == 0)
                lines = py_list({ py{ "\n" } });
            else if (!lines[py{ -1 }].method("endswith", { py{ "\n" } }).truthy())
                lines.set_item(py{ -1 }, lines[py{ -1 }] + py{ "\n" });
            line_cache().set_item(aFilename, py_tuple({ stat.attr("st_size"), stat.attr("st_mtime"), lines, fullname }));
            return lines;
        }

        py getlines(py const& aFilename, py const& aModuleGlobals)
        {
            py const entry = line_cache().method("get", { aFilename, none() });
            if (!entry.is_none() && entry.size() != 1)
                return entry[py{ 2 }];
            try
            {
                return updatecache(aFilename, aModuleGlobals);
            }
            catch (raised const&)
            {
                caught_which({ "MemoryError" });
                clearcache();
                return py_list();
            }
        }

        py getline(py const& aFilename, py const& aLineno, py const& aModuleGlobals)
        {
            py const lines = getlines(aFilename, aModuleGlobals);
            if (py{ 1 } <= aLineno && aLineno <= py{ lines.size() })
                return lines[aLineno - py{ 1 }];
            return py{ "" };
        }

        void checkcache(py const& aFilename)
        {
            std::vector<py> filenames;
            if (aFilename.empty() || aFilename.is_none())
                filenames = line_cache().method("copy").method("keys").items();
            else
                filenames.push_back(aFilename);
            for (auto const& filename : filenames)
            {
                py const entry = line_cache().method("get", { filename, none() });
                if (entry.is_none() || entry.size() == 1)
                    continue;   // (a lazy entry stays lazy)
                py const size = entry[py{ 0 }];
                py const mtime = entry[py{ 1 }];
                py const fullname = entry[py{ 3 }];
                if (mtime.is_none())
                    continue;   // (loaded via a __loader__)
                py stat;
                try
                {
                    stat = import_attribute("os", "stat")(fullname);
                }
                catch (raised const&)
                {
                    caught_which({ "OSError", "ValueError" });
                    line_cache().method("pop", { filename, none() });
                    continue;
                }
                if (size != stat.attr("st_size") || mtime != stat.attr("st_mtime"))
                    line_cache().method("pop", { filename, none() });
            }
        }

        py code_key(py const& aCode) { return py_tuple({ aCode.attr("co_filename"), aCode.attr("co_qualname"), aCode.attr("co_firstlineno") }); }

        py getlines_from_code(py const& aCode)
        {
            py const entry = interactive_cache().method("get", { code_key(aCode), none() });
            if (!entry.is_none() && entry.size() != 1)
                return entry[py{ 2 }];
            return py_list();
        }

        py getline_from_code(py const& aCode, py const& aLineno)
        {
            py const lines = getlines_from_code(aCode);
            if (py{ 1 } <= aLineno && aLineno <= py{ lines.size() })
                return lines[aLineno - py{ 1 }];
            return py{ "" };
        }

        builtin_entry const sLinecacheFunctions[] =
        {
            { "clearcache", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { parse("clearcache", aArgs, aKwargs, {}, 0u); clearcache(); return none(); }); } },
            { "getline", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("getline", aArgs, aKwargs, { "filename", "lineno", "module_globals" }, 2u);
                        return getline(a[0], a[1], a.get(2, none()));
                    });
                } },
            { "getlines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("getlines", aArgs, aKwargs, { "filename", "module_globals" }, 1u);
                        return getlines(a[0], a.get(1, none()));
                    });
                } },
            { "checkcache", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("checkcache", aArgs, aKwargs, { "filename" }, 0u);
                        checkcache(a.get(0, none()));
                        return none();
                    });
                } },
            { "updatecache", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("updatecache", aArgs, aKwargs, { "filename", "module_globals" }, 1u);
                        return updatecache(a[0], a.get(1, none()));
                    });
                } },
            { "lazycache", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("lazycache", aArgs, aKwargs, { "filename", "module_globals" }, 2u);
                        return boolean(lazycache(a[0], a[1]));
                    });
                } },
            { "_make_lazycache_entry", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_make_lazycache_entry", aArgs, aKwargs, { "filename", "module_globals" }, 2u);
                        auto const e = make_lazycache_entry(a[0], a[1]);
                        return e.empty() ? none() : e;
                    });
                } },
            { "_getline_from_code", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_getline_from_code", aArgs, aKwargs, { "filename", "lineno" }, 2u);
                        return getline_from_code(a[0], a[1]);
                    });
                } },
            { "_getlines_from_code", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_getlines_from_code", aArgs, aKwargs, { "code" }, 1u);
                        return getlines_from_code(a[0]);
                    });
                } },
            { "_make_key", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_make_key", aArgs, aKwargs, { "code" }, 1u);
                        return code_key(a[0]);
                    });
                } },
            { "_register_code", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_register_code", aArgs, aKwargs, { "code", "string", "name" }, 3u);
                        std::vector<py> lines;
                        for (auto const& l : a[1].method("splitlines").items())
                            lines.push_back(l + py{ "\n" });
                        py const entry = py_tuple({ py{ a[1].size() }, none(), py_list(lines), a[2] });
                        std::vector<py> stack{ a[0] };
                        while (!stack.empty())
                        {
                            py const code = stack.back();
                            stack.pop_back();
                            for (auto const& c : code.attr("co_consts").items())
                                if (isinstance(c, type_of(code)))
                                    stack.push_back(c);
                            interactive_cache().set_item(code_key(code), entry);
                        }
                        return none();
                    });
                } }
        };

        bool linecache_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            for (auto const& f : sLinecacheFunctions)
                aAttributes.emplace_back(f.name, make_builtin(f));
            aAttributes.emplace_back("cache", line_cache().reference());
            aAttributes.emplace_back("_interactive_cache", interactive_cache().reference());
            std::vector<object*> all;
            for (auto name : { "getline", "clearcache", "checkcache", "lazycache" })
                all.push_back(make_str(name));
            aAttributes.emplace_back("__all__", make_list(std::move(all)));
            return true;
        }

        inline bool const sLinecache = register_module("linecache", linecache_module);

        // --- _colorize

        struct ansi_color { char const* name; char const* code; };
        ansi_color const sAnsiColors[] =
        {
            { "RESET", "\x1b[0m" },
            { "BLACK", "\x1b[30m" }, { "BLUE", "\x1b[34m" }, { "CYAN", "\x1b[36m" }, { "GREEN", "\x1b[32m" }, { "MAGENTA", "\x1b[35m" }, { "RED", "\x1b[31m" },
            { "WHITE", "\x1b[37m" }, { "YELLOW", "\x1b[33m" },
            { "BOLD_BLACK", "\x1b[1;30m" }, { "BOLD_BLUE", "\x1b[1;34m" }, { "BOLD_CYAN", "\x1b[1;36m" }, { "BOLD_GREEN", "\x1b[1;32m" },
            { "BOLD_MAGENTA", "\x1b[1;35m" }, { "BOLD_RED", "\x1b[1;31m" }, { "BOLD_WHITE", "\x1b[1;37m" }, { "BOLD_YELLOW", "\x1b[1;33m" },
            { "INTENSE_BLACK", "\x1b[90m" }, { "INTENSE_BLUE", "\x1b[94m" }, { "INTENSE_CYAN", "\x1b[96m" }, { "INTENSE_GREEN", "\x1b[92m" },
            { "INTENSE_MAGENTA", "\x1b[95m" }, { "INTENSE_RED", "\x1b[91m" }, { "INTENSE_WHITE", "\x1b[97m" }, { "INTENSE_YELLOW", "\x1b[93m" },
            { "BACKGROUND_BLACK", "\x1b[40m" }, { "BACKGROUND_BLUE", "\x1b[44m" }, { "BACKGROUND_CYAN", "\x1b[46m" }, { "BACKGROUND_GREEN", "\x1b[42m" },
            { "BACKGROUND_MAGENTA", "\x1b[45m" }, { "BACKGROUND_RED", "\x1b[41m" }, { "BACKGROUND_WHITE", "\x1b[47m" }, { "BACKGROUND_YELLOW", "\x1b[43m" },
            { "INTENSE_BACKGROUND_BLACK", "\x1b[100m" }, { "INTENSE_BACKGROUND_BLUE", "\x1b[104m" }, { "INTENSE_BACKGROUND_CYAN", "\x1b[106m" },
            { "INTENSE_BACKGROUND_GREEN", "\x1b[102m" }, { "INTENSE_BACKGROUND_MAGENTA", "\x1b[105m" }, { "INTENSE_BACKGROUND_RED", "\x1b[101m" },
            { "INTENSE_BACKGROUND_WHITE", "\x1b[107m" }, { "INTENSE_BACKGROUND_YELLOW", "\x1b[103m" }
        };

        char const* ansi(char const* aName, bool aColorize)
        {
            if (!aColorize)
                return "";
            for (auto const& c : sAnsiColors)
                if (std::strcmp(c.name, aName) == 0)
                    return c.code;
            return "";
        }

        bool can_colorize(py aFile)
        {
            auto const environment = import_attribute("os", "environ"); // (not "environ": a macro of some C libraries)
            auto const getenv = [&](char const* aName) -> py
            {
                try
                {
                    return environment.method("get", { py{ aName }, none() });
                }
                catch (raised const&)
                {
                    caught_which({ "Exception" });
                    return none();
                }
            };
            if (aFile.empty() || aFile.is_none())
                aFile = import_attribute("sys", "stdout");
            if (!import_attribute("sys", "flags").attr("ignore_environment").truthy())
            {
                if (getenv("PYTHON_COLORS") == py{ "0" })
                    return false;
                if (getenv("PYTHON_COLORS") == py{ "1" })
                    return true;
            }
            if (getenv("NO_COLOR").truthy())
                return false;
            if (!import_attribute("_colorize", "COLORIZE").truthy())
                return false;
            if (getenv("FORCE_COLOR").truthy())
                return true;
            if (getenv("TERM") == py{ "dumb" })
                return false;
            auto const hasattr = builtin("hasattr");
            if (!hasattr(aFile, py{ "fileno" }).truthy())
                return false;
            if (import_attribute("sys", "platform") == py{ "win32" })
            {
                try
                {
                    if (!import_attribute("nt", "_supports_virtual_terminal")().truthy())
                        return false;
                }
                catch (raised const&)
                {
                    caught_which({ "ImportError", "AttributeError" });
                    return false;
                }
            }
            try
            {
                return import_attribute("os", "isatty")(aFile.method("fileno")).truthy();
            }
            catch (raised const&)
            {
                caught_which({ "OSError" });
                return hasattr(aFile, py{ "isatty" }).truthy() && aFile.method("isatty").truthy();
            }
        }

        object* sAnsiColorsClass = nullptr;
        object* sNoColors = nullptr;

        builtin_entry const sColorizeFunctions[] =
        {
            { "can_colorize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("can_colorize", aArgs, aKwargs, {}, 0u, { "file" });
                        return boolean(can_colorize(a.get(0, none())));
                    });
                } },
            { "get_colors", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("get_colors", aArgs, aKwargs, { "colorize" }, 0u, { "file" });
                        if (a.get(0, boolean(false)).truthy() || can_colorize(a.get(1, none())))
                            return py::borrowed(sAnsiColorsClass)();
                        return py::borrowed(sNoColors);
                    });
                } }
        };

        bool colorize_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            for (auto const& f : sColorizeFunctions)
                aAttributes.emplace_back(f.name, make_builtin(f));
            if (sAnsiColorsClass == nullptr)
            {
                std::vector<std::pair<std::string, object*>> colors;
                for (auto const& c : sAnsiColors)
                    colors.emplace_back(c.name, make_str(c.code));
                sAnsiColorsClass = make_class("_colorize", "ANSIColors", {}, colors);
                for (auto& c : colors)
                    release(c.second);
                if (sAnsiColorsClass == nullptr)
                    return false;
                auto const noColors = py_guarded([&]()
                {
                    py const instance = py::borrowed(sAnsiColorsClass)();
                    for (auto const& c : sAnsiColors)
                        instance.set_attr(c.name, py{ "" });
                    return instance;
                });
                if (noColors == nullptr)
                    return false;
                sNoColors = noColors;
            }
            aAttributes.emplace_back("ANSIColors", new_reference(sAnsiColorsClass));
            aAttributes.emplace_back("NoColors", new_reference(sNoColors));
            aAttributes.emplace_back("COLORIZE", make_bool(true));
            return true;
        }

        inline bool const sColorize = register_module("_colorize", colorize_module);

        // --- suggestions (Python/suggestions.c: the edit distance of names' UTF-8)

        std::size_t const sMaxCandidateItems = 750u;
        std::size_t const sMaxStringSize = 40u;
        std::size_t const sMoveCost = 2u;
        std::size_t const sCaseCost = 1u;

        std::size_t utf8_substitution_cost(char a, char b)
        {
            if ((a & 31) != (b & 31))
                return sMoveCost;
            if (a == b)
                return 0u;
            if ('A' <= a && a <= 'Z')
                a = static_cast<char>(a + ('a' - 'A'));
            if ('A' <= b && b <= 'Z')
                b = static_cast<char>(b + ('a' - 'A'));
            return a == b ? sCaseCost : sMoveCost;
        }

        std::size_t utf8_levenshtein_distance(std::string_view a, std::string_view b, std::size_t aMaxCost)
        {
            while (!a.empty() && !b.empty() && a.front() == b.front())
            {
                a.remove_prefix(1u);
                b.remove_prefix(1u);
            }
            while (!a.empty() && !b.empty() && a.back() == b.back())
            {
                a.remove_suffix(1u);
                b.remove_suffix(1u);
            }
            if (a.empty() || b.empty())
                return (a.size() + b.size()) * sMoveCost;
            if (a.size() > sMaxStringSize || b.size() > sMaxStringSize)
                return aMaxCost + 1u;
            if (b.size() < a.size())
                std::swap(a, b);
            if ((b.size() - a.size()) * sMoveCost > aMaxCost)
                return aMaxCost + 1u;
            std::size_t buffer[sMaxStringSize];
            std::size_t tmp = sMoveCost;
            for (std::size_t i = 0u; i < a.size(); ++i)
            {
                buffer[i] = tmp;
                tmp += sMoveCost;
            }
            std::size_t result = 0u;
            for (std::size_t bIndex = 0u; bIndex < b.size(); ++bIndex)
            {
                char const code = b[bIndex];
                std::size_t distance = result = bIndex * sMoveCost;
                std::size_t minimum = std::numeric_limits<std::size_t>::max();
                for (std::size_t index = 0u; index < a.size(); ++index)
                {
                    std::size_t const substitute = distance + utf8_substitution_cost(code, a[index]);
                    distance = buffer[index];
                    std::size_t const insertDelete = std::min(result, distance) + sMoveCost;
                    result = std::min(insertDelete, substitute);
                    buffer[index] = result;
                    if (result < minimum)
                        minimum = result;
                }
                if (minimum > aMaxCost)
                    return aMaxCost + 1u;
            }
            return result;
        }

        // (_Py_CalculateSuggestions: the candidate (a list of strs) closest to aName; empty: none)
        py calculate_suggestions(py const& aCandidates, std::string const& aName)
        {
            auto const& items = as<list_object>(aCandidates.get())->items;
            if (items.size() >= sMaxCandidateItems)
                return {};
            long long suggestionDistance = std::numeric_limits<long long>::max();
            object* suggestion = nullptr;
            auto const nameSize = static_cast<long long>(aName.size());
            for (auto item : std::vector<object*>{ items })
            {
                auto const& itemText = as<str_object>(item)->value;
                if (itemText == aName)
                    continue;
                long long maxDistance = (nameSize + static_cast<long long>(itemText.size()) + 3) * static_cast<long long>(sMoveCost) / 6;
                maxDistance = std::min(maxDistance, suggestionDistance - 1);
                auto const currentDistance = static_cast<long long>(utf8_levenshtein_distance(aName, itemText, static_cast<std::size_t>(maxDistance)));
                if (currentDistance > maxDistance)
                    continue;
                if (suggestion == nullptr || currentDistance < suggestionDistance)
                {
                    suggestion = item;
                    suggestionDistance = currentDistance;
                }
            }
            return suggestion != nullptr ? py::borrowed(suggestion) : py{};
        }

        builtin_entry const sSuggestionsFunctions[] =
        {
            { "_generate_suggestions", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_generate_suggestions", aArgs, aKwargs, { "candidates", "item" }, 2u, {}, false, 2u);
                        if (kind_of(a[0].get()) != NEOS_PY_LIST || is_instance(a[0].get()))
                            py_raise("TypeError", "candidates must be a list");
                        if (!is_str(a[1]))
                            py_raise("TypeError", std::string{ "_generate_suggestions() argument 2 must be str, not " } + type_name(a[1].get()));
                        for (auto item : as<list_object>(a[0].get())->items)
                            if (kind_of(item) != NEOS_PY_STR)
                                py_raise("TypeError", "all elements in 'candidates' must be strings");
                        auto const result = calculate_suggestions(a[0], text_of(a[1]));
                        return result.empty() ? none() : result;
                    });
                } }
        };

        bool suggestions_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            for (auto const& f : sSuggestionsFunctions)
                aAttributes.emplace_back(f.name, make_builtin(f));
            return true;
        }

        inline bool const sSuggestions = register_module("_suggestions", suggestions_module);

        // (traceback._get_safe___dir__)
        py safe_dir(py const& aObject)
        {
            py d;
            try
            {
                d = aObject.method("__dir__");
            }
            catch (raised const&)
            {
                caught_which({ "TypeError" });
                d = type_of(aObject).attr("__dir__")(aObject);
            }
            std::vector<py> names;
            for (auto const& x : d.items())
                if (isinstance(x, builtin("str")))
                    names.push_back(x);
            return builtin("sorted")(py_list(names));
        }

        // (names that do not begin with an underscore)
        py public_names(py const& aNames)
        {
            std::vector<py> result;
            for (auto const& x : aNames.items())
                if (!(slice(x, none(), py{ 1 }) == py{ "_" }))
                    result.push_back(x);
            return py_list(result);
        }

        // (traceback._compute_suggestion_error: a NameError's candidates are its frame's names (a function's frame has no locals), else (it has
        // no frame) the builtins' names)
        py compute_suggestion_error(py const& aException, py aTraceback, py const& aWrongName)
        {
            if (aWrongName.is_none() || !isinstance(aWrongName, builtin("str")))
                return none();
            py d;
            if (isinstance(aException, builtin("AttributeError")))
            {
                py const obj = aException.attr("obj");
                try
                {
                    d = safe_dir(obj);
                    bool hideUnderscored = !(slice(aWrongName, none(), py{ 1 }) == py{ "_" });
                    if (hideUnderscored && !aTraceback.is_none())
                    {
                        while (!aTraceback.attr("tb_next").is_none())
                            aTraceback = aTraceback.attr("tb_next");
                        py const frame = aTraceback.attr("tb_frame");
                        if (!frame.is_none())
                        {
                            py const locals = frame.attr("f_locals");
                            if (locals.contains(py{ "self" }) && locals[py{ "self" }].is(obj))
                                hideUnderscored = false;
                        }
                    }
                    if (hideUnderscored)
                        d = public_names(d);
                }
                catch (raised const&)
                {
                    caught_which({ "Exception" });
                    return none();
                }
            }
            else if (isinstance(aException, builtin("ImportError")))
            {
                try
                {
                    // (__import__(name): the top-level package)
                    auto const name = aException.attr("name").str();
                    py{ fn_import(name.c_str()) };
                    py const module{ fn_import(name.substr(0u, name.find('.')).c_str()) };
                    d = safe_dir(module);
                    if (!(slice(aWrongName, none(), py{ 1 }) == py{ "_" }))
                        d = public_names(d);
                }
                catch (raised const&)
                {
                    caught_which({ "Exception" });
                    return none();
                }
            }
            else
            {
                if (aTraceback.is_none())
                    return none();
                while (!aTraceback.attr("tb_next").is_none())
                    aTraceback = aTraceback.attr("tb_next");
                py const frame = aTraceback.attr("tb_frame");
                std::vector<py> names;
                if (!frame.is_none())
                {
                    for (auto const& n : frame.attr("f_locals").items())
                        names.push_back(n);
                    for (auto const& n : frame.attr("f_globals").items())
                        names.push_back(n);
                    for (auto const& n : frame.attr("f_builtins").items())
                        names.push_back(n);
                }
                else
                    for (auto const& n : import_attribute("builtins", "__dict__").items())
                        names.push_back(n);
                std::vector<py> strs;
                for (auto const& n : names)
                    if (isinstance(n, builtin("str")))
                        strs.push_back(n);
                d = py_list(strs);
                if (!frame.is_none())
                {
                    py const locals = frame.attr("f_locals");
                    if (locals.contains(py{ "self" }))
                    {
                        bool hasWrongName = false;
                        try
                        {
                            hasWrongName = builtin("hasattr")(locals[py{ "self" }], aWrongName).truthy();
                        }
                        catch (raised const&)
                        {
                            caught_which({ "Exception" });
                        }
                        if (hasWrongName)
                            return py{ "self." + aWrongName.str() };
                    }
                }
            }
            auto const result = calculate_suggestions(d, text_of(aWrongName));
            return result.empty() ? none() : result;
        }

        // (traceback._levenshtein_distance: Python's (of characters))
        std::size_t substitution_cost(py const& a, py const& b)
        {
            if (a == b)
                return 0u;
            if (a.method("lower") == b.method("lower"))
                return sCaseCost;
            return sMoveCost;
        }

        long long levenshtein_distance(py const& aA, py const& aB, long long aMaxCost)
        {
            if (aA == aB)
                return 0;
            auto a = aA.items();
            auto b = aB.items();
            std::size_t pre = 0u;
            while (pre < a.size() && pre < b.size() && a[pre] == b[pre])
                ++pre;
            a.erase(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(pre));
            b.erase(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(pre));
            while (!a.empty() && !b.empty() && a.back() == b.back())
            {
                a.pop_back();
                b.pop_back();
            }
            if (a.empty() || b.empty())
                return static_cast<long long>(sMoveCost * (a.size() + b.size()));
            if (a.size() > sMaxStringSize || b.size() > sMaxStringSize)
                return aMaxCost + 1;
            if (b.size() < a.size())
                std::swap(a, b);
            if (static_cast<long long>((b.size() - a.size()) * sMoveCost) > aMaxCost)
                return aMaxCost + 1;
            std::vector<long long> row;
            for (std::size_t i = 1u; i <= a.size(); ++i)
                row.push_back(static_cast<long long>(i * sMoveCost));
            long long result = 0;
            for (std::size_t bIndex = 0u; bIndex < b.size(); ++bIndex)
            {
                long long distance = result = static_cast<long long>(bIndex * sMoveCost);
                long long minimum = std::numeric_limits<long long>::max();
                for (std::size_t index = 0u; index < a.size(); ++index)
                {
                    long long const substitute = distance + static_cast<long long>(substitution_cost(b[bIndex], a[index]));
                    distance = row[index];
                    long long const insertDelete = std::min(result, distance) + static_cast<long long>(sMoveCost);
                    result = std::min(insertDelete, substitute);
                    row[index] = result;
                    if (result < minimum)
                        minimum = result;
                }
                if (minimum > aMaxCost)
                    return aMaxCost + 1;
            }
            return result;
        }

        // --- traceback

        char const* const sCauseMessage = "\nThe above exception was the direct cause of the following exception:\n\n";
        char const* const sContextMessage = "\nDuring handling of the above exception, another exception occurred:\n\n";
        long long const sRecursiveCutoff = 3;

        object* sFrameSummary = nullptr;
        object* sStackSummary = nullptr;
        object* sPrintContext = nullptr;
        object* sTracebackException = nullptr;
        object* sSentinel = nullptr;            // (_sentinel)
        object* sBuiltinExceptionLimit = nullptr;   // (BUILTIN_EXCEPTION_LIMIT)

        py frame_summary_class() { return py::borrowed(sFrameSummary); }
        py stack_summary_class() { return py::borrowed(sStackSummary); }
        py traceback_exception_class() { return py::borrowed(sTracebackException); }

        // (_safe_string(value, what, func=str))
        py safe_string(py const& aValue, std::string const& aWhat, py aFunction = {})
        {
            if (aFunction.empty())
                aFunction = builtin("str");
            try
            {
                return aFunction(aValue);
            }
            catch (raised const&)
            {
                take_exception();
                return py{ "<" + aWhat + " " + aFunction.attr("__name__").str() + "() failed>" };
            }
        }

        // (_format_final_exc_line(etype, value, *, insert_final_newline=True, colorize=False))
        py format_final_exc_line(py const& aType, py const& aValue, bool aInsertFinalNewline, bool aColorize)
        {
            py const valueText = safe_string(aValue, "exception");
            std::string const end = aInsertFinalNewline ? "\n" : "";
            std::string const type = aType.str();
            if (aValue.is_none() || !valueText.truthy())
                return py{ std::string{ ansi("BOLD_MAGENTA", aColorize) } + type + ansi("RESET", aColorize) + end };
            return py{ std::string{ ansi("BOLD_MAGENTA", aColorize) } + type + ansi("RESET", aColorize) + ": " + ansi("MAGENTA", aColorize) + valueText.str() +
                ansi("RESET", aColorize) + end };
        }

        // (_byte_offset_to_character_offset(str, offset))
        py byte_offset_to_character_offset(py const& aText, py const& aOffset)
        {
            py const utf8 = aText.method("encode", { py{ "utf-8" } });
            return py{ slice(utf8, none(), aOffset).method("decode", { py{ "utf-8" } }, { { "errors", py{ "replace" } } }).size() };
        }

        // (_display_width(line, offset=None))
        long long display_width(py const& aLine, py aOffset = {})
        {
            if (aOffset.empty() || aOffset.is_none())
                aOffset = py{ aLine.size() };
            if (aLine.method("isascii").truthy())
                return aOffset.as_index();
            auto const width = import_attribute("unicodedata", "east_asian_width");
            long long result = 0;
            for (auto const& c : slice(aLine, none(), aOffset).items())
            {
                auto const w = width(c).str();
                result += w == "W" || w == "F" ? 2 : 1;
            }
            return result;
        }

        std::optional<std::size_t> attribute_call_function_end(std::string const& aSegment, std::string const& aName);

        // (_walk_tb_with_full_positions(tb): (frame, (lineno, end_lineno, colno, end_colno)) for each of a traceback's frames (a traceback
        // whose tb_frame is None (one that has not been caught) has none); aException: the exception (an AttributeError that a method 
        // call's lookup of its method raised: the position of its innermost frame is the lookup's (o.name), as CPython's (its LOAD_ATTR's)))
        py walk_tb_with_full_positions(py aTraceback, py const& aException = {})
        {
            std::vector<py> result;
            while (!aTraceback.is_none())
            {
                py const frame = aTraceback.attr("tb_frame");
                if (frame.is_none())
                    break;
                py positions = py_tuple({ none(), none(), none(), none() });
                py const lasti = aTraceback.attr("tb_lasti");
                // (a traceback of the program's: the position of the expression that its frame evaluates (if the compiler gave one))
                if (auto const* f = traceback_frame(aTraceback.get()); f != nullptr && f->column != 0u)
                    positions = py_tuple({ py{ static_cast<long long>(f->line) }, py{ static_cast<long long>(f->endLine) }, py{ static_cast<long long>(f->column - 1u) },
                        py{ static_cast<long long>(f->endColumn - 1u) } });
                else if (lasti >= py{ 0 })
                {
                    py const positionsGen = frame.attr("f_code").method("co_positions");
                    positions = builtin("next")(import_attribute("itertools", "islice")(positionsGen, floordiv(lasti, py{ 2 }), none()));
                }
                if (!aException.empty() && aTraceback.attr("tb_next").is_none() && !positions[py{ 2 }].is_none() && positions[py{ 0 }] == positions[py{ 1 }] &&
                    builtin("isinstance")(aException, builtin("AttributeError")).truthy())
                {
                    py const name = builtin("getattr")(aException, py{ "name" }, none());
                    py const line = import_attribute("linecache", "getline")(frame.attr("f_code").attr("co_filename"), positions[py{ 0 }]);
                    if (kind_of(name.get()) == NEOS_PY_STR && kind_of(line.get()) == NEOS_PY_STR)
                    {
                        auto const text = line.str();
                        auto const column = static_cast<std::size_t>(positions[py{ 2 }].as_index());
                        auto const endColumn = static_cast<std::size_t>(positions[py{ 3 }].as_index());
                        if (column < endColumn && endColumn <= text.size())
                            if (auto const end = attribute_call_function_end(text.substr(column, endColumn - column), name.str()))
                                positions = py_tuple({ positions[py{ 0 }], positions[py{ 1 }], positions[py{ 2 }], py{ static_cast<long long>(column + *end) } });
                    }
                }
                if (positions[py{ 0 }].is_none())
                    result.push_back(py_tuple({ frame, py_tuple({ aTraceback.attr("tb_lineno") }) + slice(positions, py{ 1 }, none()) }));
                else
                    result.push_back(py_tuple({ frame, positions }));
                aTraceback = aTraceback.attr("tb_next");
            }
            return builtin("iter")(py_list(result));
        }

        // (walk_tb(tb): (frame, lineno) for each of a traceback's frames)
        py walk_tb(py aTraceback)
        {
            std::vector<py> result;
            while (!aTraceback.is_none())
            {
                py const frame = aTraceback.attr("tb_frame");
                if (frame.is_none())
                    break;
                result.push_back(py_tuple({ frame, aTraceback.attr("tb_lineno") }));
                aTraceback = aTraceback.attr("tb_next");
            }
            return builtin("iter")(py_list(result));
        }

        // (the frame of the program that called the module's function (sys._getframe() as the function's caller sees it): None if it has none)
        py current_frame()
        {
            auto const stack = capture_frames();
            return stack->empty() ? none() : py{ make_frame(stack, 0u) };
        }

        // (walk_stack(f): (frame, lineno) for each frame from f (None: the frame of the program that called it))
        py walk_stack(py aFrame)
        {
            if (aFrame.is_none())
                aFrame = current_frame();
            std::vector<py> result;
            while (!aFrame.is_none())
            {
                result.push_back(py_tuple({ aFrame, aFrame.attr("f_lineno") }));
                aFrame = aFrame.attr("f_back");
            }
            return builtin("iter")(py_list(result));
        }

        // (StackSummary._extract_from_extended_frame_gen)
        py extract_from_extended_frame_gen(py const& aClass, py aFrames, py aLimit, py const& aLookupLines, py const& aCaptureLocals, bool aExtended)
        {
            bool const builtinLimit = aLimit.get() == sBuiltinExceptionLimit;
            if (aLimit.is_none() || builtinLimit)
            {
                aLimit = builtin("getattr")(py{ fn_import("sys") }, py{ "tracebacklimit" }, none());
                if (!aLimit.is_none() && aLimit < py{ 0 })
                    aLimit = py{ 0 };
            }
            std::vector<py> frames;
            if (!aLimit.is_none())
            {
                if (builtinLimit)
                {
                    frames = aFrames.items();
                    auto const keep = static_cast<std::size_t>(std::max(0ll, aLimit.as_index()));
                    if (frames.size() > keep)
                        frames.erase(frames.begin(), frames.end() - static_cast<std::ptrdiff_t>(keep));
                }
                else if (aLimit >= py{ 0 })
                    frames = import_attribute("itertools", "islice")(aFrames, aLimit).items();
                else
                    frames = import_attribute("collections", "deque").call({ aFrames }, { { "maxlen", -aLimit } }).items();
            }
            else
                frames = aFrames.items();
            py const result = aClass();
            std::vector<py> filenames;
            for (auto const& item : frames)
            {
                py const f = item[py{ 0 }];
                py const positions = aExtended ? item[py{ 1 }] : py_tuple({ item[py{ 1 }], none(), none(), none() });
                py const code = f.attr("f_code");
                py const filename = code.attr("co_filename");
                py const name = code.attr("co_name");
                filenames.push_back(filename);
                import_attribute("linecache", "lazycache")(filename, f.attr("f_globals"));
                py const locals = aCaptureLocals.truthy() ? f.attr("f_locals") : none();
                result.method("append", { frame_summary_class().call({ filename, positions[py{ 0 }], name }, { { "lookup_line", boolean(false) },
                    { "locals", locals }, { "end_lineno", positions[py{ 1 }] }, { "colno", positions[py{ 2 }] }, { "end_colno", positions[py{ 3 }] },
                    { "_code", code } }) });
            }
            for (auto const& filename : builtin("set")(py_list(filenames)).items())
                import_attribute("linecache", "checkcache")(filename);
            if (aLookupLines.truthy())
                for (auto const& f : result.items())
                    f.attr("line");
            return result;
        }

        // (StackSummary.from_list)
        py stack_summary_from_list(py const& aList)
        {
            py const result = stack_summary_class()();
            for (auto const& frame : aList.items())
            {
                if (isinstance(frame, frame_summary_class()))
                    result.method("append", { frame });
                else
                {
                    auto const fields = frame.items();
                    if (fields.size() != 4u)
                        py_raise("ValueError", fields.size() < 4u ? "not enough values to unpack (expected 4, got " + std::to_string(fields.size()) + ")" :
                            "too many values to unpack (expected 4)");
                    result.method("append", { frame_summary_class().call({ fields[0], fields[1], fields[2] }, { { "line", fields[3] } }) });
                }
            }
            return result;
        }

        // (extract_tb(tb, limit=None))
        py extract_tb(py const& aTraceback, py const& aLimit)
        {
            return stack_summary_class().method("_extract_from_extended_frame_gen", { walk_tb_with_full_positions(aTraceback) }, { { "limit", aLimit } });
        }

        // (extract_stack(f=None, limit=None): without f, from the frame of the program that called it)
        py extract_stack(py const& aFrame, py const& aLimit)
        {
            py const stack = stack_summary_class().method("extract", { walk_stack(aFrame.empty() ? none() : aFrame) }, { { "limit", aLimit } });
            stack.method("reverse");
            return stack;
        }

        // (FrameSummary._set_lines)
        void frame_summary_set_lines(py const& aSelf)
        {
            py const lineno = aSelf.attr("lineno");
            py const endLineno = aSelf.attr("end_lineno");
            if (!aSelf.attr("_lines").is_none() || lineno.is_none() || endLineno.is_none())
                return;
            py const filename = aSelf.attr("filename");
            py const code = aSelf.attr("_code");
            std::vector<py> lines;
            for (auto const& n : builtin("range")(lineno, endLineno + py{ 1 }).items())
            {
                py line = import_attribute("linecache", "getline")(filename, n).method("rstrip");
                if (!line.truthy() && !code.is_none() && filename.method("startswith", { py{ "<" } }).truthy())
                    line = import_attribute("linecache", "_getline_from_code")(code, n).method("rstrip");
                lines.push_back(line);
            }
            aSelf.set_attr("_lines", join(lines, "\n") + py{ "\n" });
        }

        py frame_summary_line(py const& aSelf)
        {
            aSelf.method("_set_lines");
            py const lines = aSelf.attr("_lines");
            if (lines.is_none())
                return none();
            return lines.method("partition", { py{ "\n" } })[py{ 0 }].method("strip");
        }

        py frame_summary_dedented_lines(py const& aSelf)
        {
            aSelf.method("_set_lines");
            if (aSelf.attr("_lines_dedented").is_none() && !aSelf.attr("_lines").is_none())
                aSelf.set_attr("_lines_dedented", text_dedent(aSelf.attr("_lines")));
            return aSelf.attr("_lines_dedented");
        }

        py frame_summary_original_lines(py const& aSelf)
        {
            aSelf.method("_set_lines");
            return aSelf.attr("_lines");
        }

        builtin_entry const sFrameSummaryMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("FrameSummary.__init__", aArgs, aKwargs, { "self", "filename", "lineno", "name" }, 4u,
                            { "lookup_line", "locals", "line", "end_lineno", "colno", "end_colno" }, true);
                        py const self = a[0];
                        py const lineno = a[2];
                        py const endLineno = a.get(7, none());
                        self.set_attr("filename", a[1]);
                        self.set_attr("lineno", lineno);
                        self.set_attr("end_lineno", endLineno.is_none() ? lineno : endLineno);
                        self.set_attr("colno", a.get(8, none()));
                        self.set_attr("end_colno", a.get(9, none()));
                        self.set_attr("name", a[3]);
                        self.set_attr("_code", keywords_get(a.extra, "_code", none()));
                        self.set_attr("_lines", a.get(6, none()));
                        self.set_attr("_lines_dedented", none());
                        if (a.get(4, boolean(true)).truthy())
                            self.attr("line");
                        py const locals = a.get(5, none());
                        if (locals.truthy())
                        {
                            py const d = py_dict();
                            for (auto const& item : locals.method("items").items())
                                d.set_item(item[py{ 0 }], safe_string(item[py{ 1 }], "local", builtin("repr")));
                            self.set_attr("locals", d);
                        }
                        else
                            self.set_attr("locals", none());
                        return none();
                    });
                } },
            { "__eq__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("FrameSummary.__eq__", aArgs, aKwargs, { "self", "other" }, 2u);
                        py const self = a[0];
                        py const other = a[1];
                        if (isinstance(other, frame_summary_class()))
                            return boolean(self.attr("filename") == other.attr("filename") && self.attr("lineno") == other.attr("lineno") &&
                                self.attr("name") == other.attr("name") && self.attr("locals") == other.attr("locals"));
                        if (isinstance(other, builtin("tuple")))
                            return py{ compare(NEOS_PY_EQ, py_tuple({ self.attr("filename"), self.attr("lineno"), self.attr("name"), self.attr("line") }).get(), other.get()) };
                        return py::borrowed(not_implemented());
                    });
                } },
            { "__getitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("FrameSummary.__getitem__", aArgs, aKwargs, { "self", "pos" }, 2u);
                        py const self = a[0];
                        return py_tuple({ self.attr("filename"), self.attr("lineno"), self.attr("name"), self.attr("line") })[a[1]];
                    });
                } },
            { "__iter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("FrameSummary.__iter__", aArgs, aKwargs, { "self" }, 1u);
                        py const self = a[0];
                        return builtin("iter")(py_list({ self.attr("filename"), self.attr("lineno"), self.attr("name"), self.attr("line") }));
                    });
                } },
            { "__repr__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("FrameSummary.__repr__", aArgs, aKwargs, { "self" }, 1u);
                        py const self = a[0];
                        return py{ "<FrameSummary file {filename}, line {lineno} in {name}>" }.method("format", {}, { { "filename", self.attr("filename") },
                            { "lineno", self.attr("lineno") }, { "name", self.attr("name") } });
                    });
                } },
            { "__len__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { parse("FrameSummary.__len__", aArgs, aKwargs, { "self" }, 1u); return py{ 4 }; }); } },
            { "_set_lines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("FrameSummary._set_lines", aArgs, aKwargs, { "self" }, 1u);
                        frame_summary_set_lines(a[0]);
                        return none();
                    });
                } },
            { "_original_lines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return frame_summary_original_lines(parse("_original_lines", aArgs, aKwargs, { "self" }, 1u)[0]); }); } },
            { "_dedented_lines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return frame_summary_dedented_lines(parse("_dedented_lines", aArgs, aKwargs, { "self" }, 1u)[0]); }); } },
            { "line", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return frame_summary_line(parse("line", aArgs, aKwargs, { "self" }, 1u)[0]); }); } }
        };

        // --- the anchors of carets (traceback._extract_caret_anchors_from_line_segment and the use of ast by 
        // StackSummary._should_show_carets): the module ast cannot be imported, so an expression's source is scanned here for 
        // what they need (its top-level operation: a binary operation, a subscript or a call; where its operands are)

        struct expression_scan
        {
            enum kind_t { Other, BinOp, Subscript, Call, Name };
            kind_t kind = Other;
            std::size_t start = 0u;
            std::size_t end = 0u;
            std::size_t leftEnd = 0u;       ///< (BinOp: its left operand's end; Subscript: its value's; Call: its function's)
            std::size_t rightStart = 0u;    ///< (BinOp: its right operand's start)
            kind_t funcKind = Other;        ///< (Call: its function's kind)
        };

        class expression_scanner
        {
        public:
            struct token
            {
                enum kind_t { Name, Number, String, Op, End } kind;
                std::size_t start;
                std::size_t end;
                std::string_view text;
            };
        public:
            explicit expression_scanner(std::string_view aText) : iText{ aText }
            {
                tokenize();
            }
        public:
            bool ok() const { return iOk; }
            std::vector<token> const& tokens() const { return iTokens; }
            std::size_t position() const { return iNext; }
            void seek(std::size_t aPosition) { iNext = aPosition; }
            bool at_end() const { return iTokens[iNext].kind == token::End; }
            // (an expression (as Python's grammar's "expression" (no tuple, no star)): its scan; none if it is not one)
            std::optional<expression_scan> expression()
            {
                auto const start = iNext;
                // (lambda, yield and starred: not scanned)
                if (is_name("lambda") || is_name("yield") || is_op("*") || is_op("**"))
                    return {};
                auto result = disjunction();
                if (!result)
                    return {};
                if (is_name("if"))
                {
                    ++iNext;
                    if (!disjunction() || !is_name("else"))
                        return {};
                    ++iNext;
                    auto const orelse = expression();
                    if (!orelse)
                        return {};
                    return expression_scan{ expression_scan::Other, iTokens[start].start, orelse->end };
                }
                return result;
            }
        private:
            bool is_name(std::string_view aName) const { return iTokens[iNext].kind == token::Name && iTokens[iNext].text == aName; }
            bool is_op(std::string_view aOp) const { return iTokens[iNext].kind == token::Op && iTokens[iNext].text == aOp; }
            std::optional<expression_scan> disjunction()
            {
                auto const start = iNext;
                auto result = conjunction();
                while (result && is_name("or"))
                {
                    ++iNext;
                    auto const right = conjunction();
                    if (!right)
                        return {};
                    result = expression_scan{ expression_scan::Other, iTokens[start].start, right->end };
                }
                return result;
            }
            std::optional<expression_scan> conjunction()
            {
                auto const start = iNext;
                auto result = inversion();
                while (result && is_name("and"))
                {
                    ++iNext;
                    auto const right = inversion();
                    if (!right)
                        return {};
                    result = expression_scan{ expression_scan::Other, iTokens[start].start, right->end };
                }
                return result;
            }
            std::optional<expression_scan> inversion()
            {
                if (is_name("not"))
                {
                    auto const start = iTokens[iNext++].start;
                    auto const operand = inversion();
                    if (!operand)
                        return {};
                    return expression_scan{ expression_scan::Other, start, operand->end };
                }
                return comparison();
            }
            bool comparison_operator()
            {
                static std::string_view const sOps[] = { "==", "!=", "<", ">", "<=", ">=" };
                for (auto op : sOps)
                    if (is_op(op))
                        return ++iNext, true;
                if (is_name("in"))
                    return ++iNext, true;
                if (is_name("not") && iTokens[iNext + 1u].kind == token::Name && iTokens[iNext + 1u].text == "in")
                    return iNext += 2u, true;
                if (is_name("is"))
                {
                    ++iNext;
                    if (is_name("not"))
                        ++iNext;
                    return true;
                }
                return false;
            }
            std::optional<expression_scan> comparison()
            {
                auto const start = iNext;
                auto result = binary(0);
                while (result && comparison_operator())
                {
                    auto const right = binary(0);
                    if (!right)
                        return {};
                    result = expression_scan{ expression_scan::Other, iTokens[start].start, right->end };
                }
                return result;
            }
            // (binary operators: |, ^, &, shifts, + -, * / // % @ (left associative); then unary - + ~; then **, which is right 
            // associative and binds more tightly than a unary operator on its left)
            static int precedence(std::string_view aOp)
            {
                if (aOp == "|") return 1;
                if (aOp == "^") return 2;
                if (aOp == "&") return 3;
                if (aOp == "<<" || aOp == ">>") return 4;
                if (aOp == "+" || aOp == "-") return 5;
                if (aOp == "*" || aOp == "/" || aOp == "//" || aOp == "%" || aOp == "@") return 6;
                return 0;
            }
            std::optional<expression_scan> binary(int aMinimum)
            {
                auto result = unary();
                while (result && iTokens[iNext].kind == token::Op && precedence(iTokens[iNext].text) > aMinimum)
                {
                    auto const p = precedence(iTokens[iNext].text);
                    ++iNext;
                    // (left associative: the right operand binds more tightly)
                    auto const right = binary(p);
                    if (!right)
                        return {};
                    result = expression_scan{ expression_scan::BinOp, result->start, right->end, result->end, right->start };
                }
                return result;
            }
            std::optional<expression_scan> unary()
            {
                if (is_op("-") || is_op("+") || is_op("~"))
                {
                    auto const start = iTokens[iNext++].start;
                    auto const operand = unary();
                    if (!operand)
                        return {};
                    return expression_scan{ expression_scan::Other, start, operand->end };
                }
                return power();
            }
            std::optional<expression_scan> power()
            {
                auto result = primary();
                if (result && is_op("**"))
                {
                    ++iNext;
                    auto const right = unary();
                    if (!right)
                        return {};
                    result = expression_scan{ expression_scan::BinOp, result->start, right->end, result->end, right->start };
                }
                return result;
            }
            // (the end of a bracketed group that starts at the token (its closing bracket's end); none if it is not closed)
            std::optional<std::size_t> skip_group()
            {
                int depth = 0;
                while (iTokens[iNext].kind != token::End)
                {
                    auto const& t = iTokens[iNext++];
                    if (t.kind != token::Op)
                        continue;
                    if (t.text == "(" || t.text == "[" || t.text == "{")
                        ++depth;
                    else if (t.text == ")" || t.text == "]" || t.text == "}")
                    {
                        if (--depth == 0)
                            return t.end;
                    }
                }
                return {};
            }
            std::optional<expression_scan> primary()
            {
                if (is_name("await"))
                {
                    auto const start = iTokens[iNext++].start;
                    auto const operand = primary();
                    if (!operand)
                        return {};
                    return expression_scan{ expression_scan::Other, start, operand->end };
                }
                std::optional<expression_scan> result;
                auto const& t = iTokens[iNext];
                if (t.kind == token::Name)
                {
                    static std::string_view const sKeywords[] = { "if", "else", "and", "or", "not", "in", "is", "for", "lambda", "yield", "await", "async", 
                        "def", "class", "return", "import", "from", "as", "with", "while", "pass", "del", "global", "nonlocal", "raise", "try", "except", 
                        "finally", "assert", "break", "continue", "elif" };
                    if (std::find(std::begin(sKeywords), std::end(sKeywords), t.text) != std::end(sKeywords))
                        return {};
                    ++iNext;
                    result = expression_scan{ expression_scan::Name, t.start, t.end };
                }
                else if (t.kind == token::Number)
                {
                    ++iNext;
                    result = expression_scan{ expression_scan::Other, t.start, t.end };
                }
                else if (t.kind == token::String)
                {
                    auto const start = t.start;
                    auto end = t.end;
                    while (iTokens[++iNext].kind == token::String)
                        end = iTokens[iNext].end;
                    result = expression_scan{ expression_scan::Other, start, end };
                }
                else if (is_op("(") || is_op("[") || is_op("{"))
                {
                    auto const start = t.start;
                    auto const end = skip_group();
                    if (!end)
                        return {};
                    result = expression_scan{ expression_scan::Other, start, *end };
                }
                else if (is_op("..."))
                {
                    ++iNext;
                    result = expression_scan{ expression_scan::Other, t.start, t.end };
                }
                else
                    return {};
                // (its trailers: attributes, calls and subscripts)
                for (;;)
                {
                    if (is_op(".") && iTokens[iNext + 1u].kind == token::Name)
                    {
                        result = expression_scan{ expression_scan::Other, result->start, iTokens[iNext + 1u].end };
                        iNext += 2u;
                    }
                    else if (is_op("("))
                    {
                        auto const function = *result;
                        auto const end = skip_group();
                        if (!end)
                            return {};
                        result = expression_scan{ expression_scan::Call, function.start, *end, function.end, 0u, function.kind };
                    }
                    else if (is_op("["))
                    {
                        auto const value = *result;
                        auto const end = skip_group();
                        if (!end)
                            return {};
                        result = expression_scan{ expression_scan::Subscript, value.start, *end, value.end };
                    }
                    else
                        break;
                }
                return result;
            }
            void tokenize()
            {
                static std::string_view const sOps[] = { "**=", "//=", ">>=", "<<=", "...", "->", "**", "//", "<<", ">>", "<=", ">=", "==", "!=", ":=",
                    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "@=" };
                auto const name_character = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80u; };
                std::size_t i = 0u;
                auto const n = iText.size();
                while (i < n)
                {
                    char const c = iText[i];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f')
                    {
                        ++i;
                        continue;
                    }
                    if (c == '\\')
                    {
                        ++i;
                        continue;
                    }
                    if (c == '#')
                    {
                        while (i < n && iText[i] != '\n')
                            ++i;
                        continue;
                    }
                    auto const start = i;
                    // (a string (with its prefix))
                    std::size_t q = i;
                    while (q < n && q - i < 3u && std::strchr("rRbBuUfF", iText[q]) != nullptr && iText[q] != '\0')
                        ++q;
                    if (q < n && (iText[q] == '\'' || iText[q] == '"') && (q == i || !name_character(iText[q - 1u]) || std::strchr("rRbBuUfF", iText[q - 1u]) != nullptr))
                    {
                        char const quote = iText[q];
                        bool const triple = q + 2u < n && iText[q + 1u] == quote && iText[q + 2u] == quote;
                        std::size_t j = q + (triple ? 3u : 1u);
                        for (; j < n; ++j)
                        {
                            if (iText[j] == '\\')
                                ++j;
                            else if (iText[j] == quote && (!triple || (j + 2u < n && iText[j + 1u] == quote && iText[j + 2u] == quote)))
                                break;
                        }
                        if (j >= n)
                        {
                            iOk = false;
                            break;
                        }
                        i = j + (triple ? 3u : 1u);
                        iTokens.push_back(token{ token::String, start, i, iText.substr(start, i - start) });
                        continue;
                    }
                    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1u < n && std::isdigit(static_cast<unsigned char>(iText[i + 1u]))))
                    {
                        while (i < n && (name_character(iText[i]) || iText[i] == '.' || ((iText[i] == '+' || iText[i] == '-') && (iText[i - 1u] == 'e' || iText[i - 1u] == 'E'))))
                            ++i;
                        iTokens.push_back(token{ token::Number, start, i, iText.substr(start, i - start) });
                        continue;
                    }
                    if (name_character(c))
                    {
                        while (i < n && name_character(iText[i]))
                            ++i;
                        iTokens.push_back(token{ token::Name, start, i, iText.substr(start, i - start) });
                        continue;
                    }
                    std::size_t length = 1u;
                    for (auto op : sOps)
                        if (iText.substr(i, op.size()) == op)
                        {
                            length = op.size();
                            break;
                        }
                    i += length;
                    iTokens.push_back(token{ token::Op, start, i, iText.substr(start, length) });
                }
                iTokens.push_back(token{ token::End, n, n, {} });
            }
        private:
            std::string_view iText;
            std::vector<token> iTokens;
            std::size_t iNext = 0u;
            bool iOk = true;
        };

        // (_Anchors: (left_end_lineno, left_end_offset, right_start_lineno, right_start_offset), in characters)
        struct caret_anchors
        {
            long long leftLine;
            long long leftOffset;
            long long rightLine;
            long long rightOffset;
        };

        // (_extract_caret_anchors_from_line_segment)
        std::optional<caret_anchors> extract_caret_anchors(std::string const& aSegment)
        {
            expression_scanner scanner{ aSegment };
            if (!scanner.ok())
                return {};
            auto const e = scanner.expression();
            if (!e || !scanner.at_end())
                return {};
            // (lines: the segment's, by byte offset; positions as (line, character offset))
            std::vector<std::size_t> starts{ 0u };
            for (std::size_t i = 0u; i < aSegment.size(); ++i)
                if (aSegment[i] == '\n')
                    starts.push_back(i + 1u);
            auto const line_end = [&](std::size_t aLine) { return aLine + 1u < starts.size() ? starts[aLine + 1u] - 1u : aSegment.size(); };
            auto const line_of = [&](std::size_t aOffset) { return static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), aOffset) - starts.begin()) - 1u; };
            auto const characters = [&](std::size_t aLine, std::size_t aOffset)
            {
                long long n = 0;
                for (std::size_t i = starts[aLine]; i < aOffset && i < aSegment.size(); ++i)
                    if ((static_cast<unsigned char>(aSegment[i]) & 0xC0u) != 0x80u)
                        ++n;
                return n;
            };
            // (next_valid_char, increment_until: by byte offset)
            auto const valid = [&](std::size_t aOffset) -> std::optional<std::size_t>
            {
                auto line = line_of(std::min(aOffset, aSegment.size()));
                while (line < starts.size() && aOffset >= line_end(line))
                {
                    ++line;
                    if (line == starts.size())
                        return {};
                    aOffset = starts[line];
                }
                return aOffset;
            };
            auto const increment_until = [&](std::size_t aOffset, auto aStop) -> std::optional<std::size_t>
            {
                auto p = valid(aOffset);
                while (p)
                {
                    char const ch = aSegment[*p];
                    if (ch == '\\' || ch == '#')
                    {
                        auto const line = line_of(*p);
                        if (line + 1u >= starts.size())
                            return {};
                        p = valid(starts[line + 1u]);
                    }
                    else if (!aStop(ch))
                        p = valid(*p + 1u);
                    else
                        break;
                }
                return p;
            };
            switch (e->kind)
            {
            case expression_scan::BinOp:
                {
                    auto const op = increment_until(e->leftEnd, [](char ch) { return !std::isspace(static_cast<unsigned char>(ch)) && ch != ')'; });
                    if (!op)
                        return {};
                    auto const line = line_of(*op);
                    auto right = *op + 1u;
                    if (right < line_end(line) && (line_of(e->rightStart) > line || right < e->rightStart) && !std::isspace(static_cast<unsigned char>(aSegment[right])) &&
                        aSegment[right] != '\\' && aSegment[right] != '#')
                        ++right;
                    return caret_anchors{ static_cast<long long>(line), characters(line, *op), static_cast<long long>(line), characters(line, right) };
                }
            case expression_scan::Subscript:
            case expression_scan::Call:
                {
                    char const open = e->kind == expression_scan::Call ? '(' : '[';
                    auto const left = increment_until(e->leftEnd, [open](char ch) { return ch == open; });
                    if (!left)
                        return {};
                    auto const leftLine = line_of(*left);
                    auto const rightLine = line_of(e->end == 0u ? 0u : e->end - 1u);
                    return caret_anchors{ static_cast<long long>(leftLine), characters(leftLine, *left), static_cast<long long>(rightLine), characters(rightLine, e->end) };
                }
            default:
                return {};
            }
        }

        // (a call of a method (o.name(...)): the end of its function (o.name))
        std::optional<std::size_t> attribute_call_function_end(std::string const& aSegment, std::string const& aName)
        {
            expression_scanner scanner{ aSegment };
            if (!scanner.ok())
                return {};
            auto const e = scanner.expression();
            if (!e || !scanner.at_end() || e->kind != expression_scan::Call)
                return {};
            auto const function = std::string_view{ aSegment }.substr(e->start, e->leftEnd - e->start);
            if (!function.ends_with(aName) || function.size() <= aName.size())
                return {};
            auto before = function.substr(0u, function.size() - aName.size());
            while (!before.empty() && std::isspace(static_cast<unsigned char>(before.back())))
                before.remove_suffix(1u);
            if (before.empty() || before.back() != '.')
                return {};
            return e->leftEnd;
        }

        // (the ast cases of StackSummary._should_show_carets: "return f(...)" and "x = f(...)" (f a name) whose call is all of the lines 
        // (aStart and aEnd: its character offsets in the first and last lines))
        bool call_is_whole_statement(std::vector<std::string> const& aLines, long long aStart, long long aEnd)
        {
            std::string text;
            for (std::size_t i = 0u; i < aLines.size(); ++i)
                text += (i > 0u ? "\n" : "") + aLines[i];
            expression_scanner scanner{ text };
            if (!scanner.ok())
                return false;
            auto const& tokens = scanner.tokens();
            std::size_t first = 0u;
            if (tokens.size() > 1u && tokens[0].kind == expression_scanner::token::Name && tokens[0].text == "return")
                first = 1u;
            else if (tokens.size() > 2u && tokens[0].kind == expression_scanner::token::Name && tokens[1].kind == expression_scanner::token::Op && tokens[1].text == "=")
                first = 2u;
            else
                return false;
            scanner.seek(first);
            auto const e = scanner.expression();
            // (return f(...): its function a name; x = ...(...): any call's)
            if (!e || !scanner.at_end() || e->kind != expression_scan::Call || (first == 1u && e->funcKind != expression_scan::Name))
                return false;
            // (its span in characters: on the first line and the last)
            auto const characters = [&](std::size_t aFrom, std::size_t aTo)
            {
                long long n = 0;
                for (std::size_t i = aFrom; i < aTo; ++i)
                    if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u)
                        ++n;
                return n;
            };
            auto const lastLineStart = text.rfind('\n') == std::string::npos ? 0u : text.rfind('\n') + 1u;
            return text.find('\n') > e->start && characters(0u, e->start) == aStart && e->end >= lastLineStart && characters(lastLineStart, e->end) == aEnd;
        }

        // (StackSummary._should_show_carets (its use of ast: see call_is_whole_statement))
        bool should_show_carets(long long aStartOffset, long long aEndOffset, std::vector<py> const& aAllLines, py const& aAnchors)
        {
            {
                std::vector<std::string> lines;
                for (auto const& line : aAllLines)
                    lines.push_back(line.str());
                if (!lines.empty() && call_is_whole_statement(lines, aStartOffset, aEndOffset))
                    return false;
            }
            if (!aAnchors.empty() && aAnchors.truthy())
                return true;
            if (slice(aAllLines.front(), none(), py{ aStartOffset }).method("lstrip").truthy() || slice(aAllLines.back(), py{ aEndOffset }, none()).method("rstrip").truthy())
                return true;
            return false;
        }

        // (StackSummary.format_frame_summary)
        py format_frame_summary(py const& aSelf, py const& aFrameSummary, bool aColorize)
        {
            std::vector<py> row;
            py filename = aFrameSummary.attr("filename");
            if (filename.method("startswith", { py{ "<stdin-" } }).truthy() && filename.method("endswith", { py{ ">" } }).truthy())
                filename = py{ "<stdin>" };
            if (aColorize)
                row.push_back(py{ "  File {}\"{}\"{}, line {}{}{}, in {}{}{}\n" }.method("format", { py{ ansi("MAGENTA", true) }, filename, py{ ansi("RESET", true) },
                    py{ ansi("MAGENTA", true) }, aFrameSummary.attr("lineno"), py{ ansi("RESET", true) }, py{ ansi("MAGENTA", true) }, aFrameSummary.attr("name"),
                    py{ ansi("RESET", true) } }));
            else
                row.push_back(py{ "  File \"{}\", line {}, in {}\n" }.method("format", { filename, aFrameSummary.attr("lineno"), aFrameSummary.attr("name") }));
            py const dedented = aFrameSummary.attr("_dedented_lines");
            if (dedented.truthy() && dedented.method("strip").truthy())
            {
                py const colno = aFrameSummary.attr("colno");
                py const endColno = aFrameSummary.attr("end_colno");
                if (colno.is_none() || endColno.is_none())
                    row.push_back(text_indent(aFrameSummary.attr("line"), py{ "    " }, false) + py{ "\n" });
                else
                {
                    auto const allLinesOriginal = aFrameSummary.attr("_original_lines").method("splitlines").items();
                    py const firstLine = allLinesOriginal.front();
                    auto const span = (aFrameSummary.attr("end_lineno") - aFrameSummary.attr("lineno")).as_index();
                    py const lastLine = py_list(allLinesOriginal)[py{ span }];
                    long long startOffset = byte_offset_to_character_offset(firstLine, colno).as_index();
                    long long endOffset = byte_offset_to_character_offset(lastLine, endColno).as_index();
                    auto allLines = slice(aFrameSummary.attr("_dedented_lines").method("splitlines"), none(), py{ span + 1 }).items();
                    long long const dedentCharacters = firstLine.size() - allLines.front().size();
                    startOffset = std::max(0ll, startOffset - dedentCharacters);
                    endOffset = std::max(0ll, endOffset - dedentCharacters);
                    long long const dpStartOffset = display_width(allLines.front(), py{ startOffset });
                    long long const dpEndOffset = display_width(allLines.back(), py{ endOffset });
                    // (the code's segment (the instruction's): its anchors)
                    py segment = py{ "\n" }.method("join", { py_list(allLines) });
                    segment = slice(segment, py{ startOffset }, py{ static_cast<long long>(segment.size()) - (static_cast<long long>(allLines.back().size()) - endOffset) });
                    std::optional<caret_anchors> anchors;
                    try
                    {
                        anchors = extract_caret_anchors(segment.str());
                    }
                    catch (...)
                    {
                    }
                    bool const showCarets = aSelf.method("_should_show_carets", { py{ startOffset }, py{ endOffset }, py_list(allLines), 
                        anchors ? py{ 1 } : none() }).truthy();
                    std::vector<py> result;
                    std::set<long long> significant{ 0, static_cast<long long>(allLines.size()) - 1 };
                    long long anchorsLeftEnd = 0;
                    long long anchorsRightStart = 0;
                    std::string primary = "^";
                    std::string secondary = "^";
                    if (anchors)
                    {
                        anchorsLeftEnd = anchors->leftOffset + (anchors->leftLine == 0 ? startOffset : 0);
                        anchorsRightStart = anchors->rightOffset + (anchors->rightLine == 0 ? startOffset : 0);
                        if (anchors->leftLine < static_cast<long long>(allLines.size()))
                            anchorsLeftEnd = display_width(allLines[static_cast<std::size_t>(anchors->leftLine)], py{ anchorsLeftEnd });
                        if (anchors->rightLine < static_cast<long long>(allLines.size()))
                            anchorsRightStart = display_width(allLines[static_cast<std::size_t>(anchors->rightLine)], py{ anchorsRightStart });
                        primary = "~";
                        secondary = "^";
                        for (long long l = anchors->leftLine - 1; l <= anchors->leftLine + 1; ++l)
                            significant.insert(l);
                        for (long long l = anchors->rightLine - 1; l <= anchors->rightLine + 1; ++l)
                            significant.insert(l);
                    }
                    significant.erase(-1);
                    significant.erase(static_cast<long long>(allLines.size()));
                    auto const outputLine = [&](long long aLine)
                    {
                        py const& line = allLines[static_cast<std::size_t>(aLine)];
                        result.push_back(line + py{ "\n" });
                        if (!showCarets)
                            return;
                        long long const spaces = line.size() - line.method("lstrip").size();
                        std::vector<std::string> carets;
                        long long const count = aLine == static_cast<long long>(allLines.size()) - 1 ? dpEndOffset : display_width(line);
                        for (long long col = 0; col < count; ++col)
                        {
                            if (col < spaces || (aLine == 0 && col < dpStartOffset))
                                carets.push_back(" ");
                            else if (anchors && (aLine > anchors->leftLine || (aLine == anchors->leftLine && col >= anchorsLeftEnd)) &&
                                (aLine < anchors->rightLine || (aLine == anchors->rightLine && col < anchorsRightStart)))
                                carets.push_back(secondary);
                            else
                                carets.push_back(primary);
                        }
                        if (aColorize)
                        {
                            auto const chars = result.back().items();
                            std::string lineParts;
                            std::string caretParts;
                            std::size_t const n = std::max(chars.size(), carets.size());
                            std::size_t i = 0u;
                            while (i < n)
                            {
                                std::string const key = i < carets.size() ? carets[i] : "";
                                std::string lineGroup;
                                std::string caretGroup;
                                while (i < n && (i < carets.size() ? carets[i] : "") == key)
                                {
                                    lineGroup += i < chars.size() ? chars[i].str() : "";
                                    caretGroup += key;
                                    ++i;
                                }
                                if (key == "^")
                                {
                                    lineParts += ansi("BOLD_RED", true) + lineGroup + ansi("RESET", true);
                                    caretParts += ansi("BOLD_RED", true) + caretGroup + ansi("RESET", true);
                                }
                                else if (key == "~")
                                {
                                    lineParts += ansi("RED", true) + lineGroup + ansi("RESET", true);
                                    caretParts += ansi("RED", true) + caretGroup + ansi("RESET", true);
                                }
                                else
                                {
                                    lineParts += lineGroup;
                                    caretParts += caretGroup;
                                }
                            }
                            result.back() = py{ lineParts };
                            result.push_back(py{ caretParts + "\n" });
                        }
                        else
                        {
                            std::string text;
                            for (auto const& c : carets)
                                text += c;
                            result.push_back(py{ text + "\n" });
                        }
                    };
                    std::vector<long long> const lines{ significant.begin(), significant.end() };
                    for (std::size_t i = 0u; i < lines.size(); ++i)
                    {
                        if (i > 0u)
                        {
                            auto const difference = lines[i] - lines[i - 1u];
                            if (difference == 2)
                                outputLine(lines[i] - 1);
                            else if (difference > 2)
                                result.push_back(py{ "...<" + std::to_string(difference - 1) + " lines>...\n" });
                        }
                        outputLine(lines[i]);
                    }
                    row.push_back(text_indent(text_dedent(join(result)), py{ "    " }, true));
                }
            }
            py const locals = aFrameSummary.attr("locals");
            if (locals.truthy())
                for (auto const& item : builtin("sorted")(locals.method("items")).items())
                    row.push_back(py{ "    {name} = {value}\n" }.method("format", {}, { { "name", item[py{ 0 }] }, { "value", item[py{ 1 }] } }));
            return join(row);
        }

        // (StackSummary.format)
        py stack_summary_format(py const& aSelf, bool aColorize)
        {
            std::vector<py> result;
            py lastFile = none();
            py lastLine = none();
            py lastName = none();
            long long count = 0;
            auto const repeated = [&]()
            {
                if (count > sRecursiveCutoff)
                {
                    count -= sRecursiveCutoff;
                    result.push_back(py{ "  [Previous line repeated " + std::to_string(count) + " more time" + (count > 1 ? "s" : "") + "]\n" });
                }
            };
            for (auto const& frameSummary : aSelf.items())
            {
                py const formatted = aSelf.method("format_frame_summary", { frameSummary }, { { "colorize", boolean(aColorize) } });
                if (formatted.is_none())
                    continue;
                if (lastFile.is_none() || lastFile != frameSummary.attr("filename") || lastLine.is_none() || lastLine != frameSummary.attr("lineno") ||
                    lastName.is_none() || lastName != frameSummary.attr("name"))
                {
                    repeated();
                    lastFile = frameSummary.attr("filename");
                    lastLine = frameSummary.attr("lineno");
                    lastName = frameSummary.attr("name");
                    count = 0;
                }
                ++count;
                if (count > sRecursiveCutoff)
                    continue;
                result.push_back(formatted);
            }
            repeated();
            return py_list(result);
        }

        bool colorize_of(py const& aExtra) { return keywords_get(aExtra, "colorize", boolean(false)).truthy(); }

        builtin_entry const sStackSummaryMethods[] =
        {
            { "extract", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("extract", aArgs, aKwargs, { "klass", "frame_gen" }, 2u, { "limit", "lookup_lines", "capture_locals" });
                        return extract_from_extended_frame_gen(a[0], a[1], a.get(2, none()), a.get(3, boolean(true)), a.get(4, boolean(false)), false);
                    });
                } },
            { "_extract_from_extended_frame_gen", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_extract_from_extended_frame_gen", aArgs, aKwargs, { "klass", "frame_gen" }, 2u, { "limit", "lookup_lines", "capture_locals" });
                        return extract_from_extended_frame_gen(a[0], a[1], a.get(2, none()), a.get(3, boolean(true)), a.get(4, boolean(false)), true);
                    });
                } },
            { "from_list", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("from_list", aArgs, aKwargs, { "klass", "a_list" }, 2u);
                        return stack_summary_from_list(a[1]);
                    });
                } },
            { "format_frame_summary", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("StackSummary.format_frame_summary", aArgs, aKwargs, { "self", "frame_summary" }, 2u, {}, true);
                        return format_frame_summary(a[0], a[1], colorize_of(a.extra));
                    });
                } },
            { "_should_show_carets", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("StackSummary._should_show_carets", aArgs, aKwargs, { "self", "start_offset", "end_offset", "all_lines", "anchors" }, 5u);
                        return boolean(should_show_carets(a[1].as_index(), a[2].as_index(), a[3].items(), a[4]));
                    });
                } },
            { "format", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("StackSummary.format", aArgs, aKwargs, { "self" }, 1u, {}, true);
                        return stack_summary_format(a[0], colorize_of(a.extra));
                    });
                } }
        };

        // --- _ExceptionPrintContext

        py print_context_indent(py const& aContext) { return py{ " " } * (py{ 2 } * aContext.attr("exception_group_depth")); }

        py print_context_emit(py const& aContext, py const& aText, py aMargin)
        {
            if (aMargin.empty() || aMargin.is_none())
                aMargin = py{ "|" };
            py indent = aContext.method("indent");
            if (aContext.attr("exception_group_depth").truthy())
                indent = indent + aMargin + py{ " " };
            std::vector<py> result;
            if (isinstance(aText, builtin("str")))
                result.push_back(text_indent(aText, indent, true));
            else
                for (auto const& text : aText.items())
                    result.push_back(text_indent(text, indent, true));
            return builtin("iter")(py_list(result));
        }

        builtin_entry const sPrintContextMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_ExceptionPrintContext.__init__", aArgs, aKwargs, { "self" }, 1u);
                        a[0].set_attr("seen", builtin("set")());
                        a[0].set_attr("exception_group_depth", py{ 0 });
                        a[0].set_attr("need_close", boolean(false));
                        return none();
                    });
                } },
            { "indent", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return print_context_indent(parse("_ExceptionPrintContext.indent", aArgs, aKwargs, { "self" }, 1u)[0]); }); } },
            { "emit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_ExceptionPrintContext.emit", aArgs, aKwargs, { "self", "text_gen", "margin_char" }, 2u);
                        return print_context_emit(a[0], a[1], a.get(2, none()));
                    });
                } }
        };

        // --- TracebackException

        // (the keyword arguments of the TracebackExceptions of an exception's cause, context and exceptions)
        struct traceback_options
        {
            py limit;
            py lookupLines;
            py captureLocals;
            py maxGroupWidth;
            py maxGroupDepth;
        };

        py make_traceback_exception(py const& aException, traceback_options const& aOptions, py const& aSeen)
        {
            return traceback_exception_class().call({ type_of(aException), aException, aException.attr("__traceback__") }, { { "limit", aOptions.limit },
                { "lookup_lines", aOptions.lookupLines }, { "capture_locals", aOptions.captureLocals }, { "max_group_width", aOptions.maxGroupWidth },
                { "max_group_depth", aOptions.maxGroupDepth }, { "_seen", aSeen } });
        }

        void traceback_exception_init(parsed const& a)
        {
            py const self = a[0];
            py const excType = a[1];
            py const excValue = a[2];
            py const excTraceback = a[3];
            traceback_options const options{ a.get(4, none()), a.get(5, boolean(true)), a.get(6, boolean(false)), a.get(8, py{ 15 }), a.get(9, py{ 10 }) };
            bool const compact = a.get(7, boolean(false)).truthy();
            bool const saveExcType = a.get(10, boolean(true)).truthy();
            py seen = a.get(11, none());
            bool const recursive = !seen.is_none();
            if (seen.is_none())
                seen = builtin("set")();
            seen.method("add", { id_of(excValue) });
            self.set_attr("max_group_width", options.maxGroupWidth);
            self.set_attr("max_group_depth", options.maxGroupDepth);
            self.set_attr("stack", stack_summary_class().method("_extract_from_extended_frame_gen", { walk_tb_with_full_positions(excTraceback, excValue) },
                { { "limit", options.limit }, { "lookup_lines", options.lookupLines }, { "capture_locals", options.captureLocals } }));
            // (an exception that has a traceback (one that has no frame (it has not been caught) is shown as its header))
            self.set_attr("_has_traceback", boolean(!excTraceback.is_none()));
            self.set_attr("_exc_type", saveExcType ? excType : none());
            self.set_attr("_str", safe_string(excValue, "exception"));
            try
            {
                self.set_attr("__notes__", builtin("getattr")(excValue, py{ "__notes__" }, none()));
            }
            catch (raised const&)
            {
                ref const e = take_exception();
                if (!exception_is(e.get(), "Exception"))
                {
                    reraise_exception(e.get());
                    throw;
                }
                self.set_attr("__notes__", py_list({ py{ "Ignored error getting __notes__: " + safe_string(py::borrowed(e.get()), "__notes__", builtin("repr")).str() } }));
            }
            self.set_attr("_is_syntax_error", boolean(false));
            self.set_attr("_have_exc_type", boolean(!excType.is_none()));
            if (!excType.is_none())
            {
                self.set_attr("exc_type_qualname", excType.attr("__qualname__"));
                self.set_attr("exc_type_module", excType.attr("__module__"));
            }
            else
            {
                self.set_attr("exc_type_qualname", none());
                self.set_attr("exc_type_module", none());
            }
            auto const getattr_ = builtin("getattr");
            if (excType.truthy() && issubclass(excType, builtin("SyntaxError")))
            {
                self.set_attr("filename", excValue.attr("filename"));
                py const lno = excValue.attr("lineno");
                self.set_attr("lineno", lno.is_none() ? none() : builtin("str")(lno));
                py const endLno = excValue.attr("end_lineno");
                self.set_attr("end_lineno", endLno.is_none() ? none() : builtin("str")(endLno));
                self.set_attr("text", excValue.attr("text"));
                self.set_attr("offset", excValue.attr("offset"));
                self.set_attr("end_offset", excValue.attr("end_offset"));
                self.set_attr("msg", excValue.attr("msg"));
                self.set_attr("_is_syntax_error", boolean(true));
            }
            else if (excType.truthy() && issubclass(excType, builtin("ImportError")) && !getattr_(excValue, py{ "name_from" }, none()).is_none())
            {
                py const wrongName = getattr_(excValue, py{ "name_from" }, none());
                py const suggestion = compute_suggestion_error(excValue, excTraceback, wrongName);
                if (suggestion.truthy())
                    self.set_attr("_str", self.attr("_str") + py{ ". Did you mean: '" + suggestion.str() + "'?" });
            }
            else if (excType.truthy() && issubclass(excType, py_tuple({ builtin("NameError"), builtin("AttributeError") })) &&
                !getattr_(excValue, py{ "name" }, none()).is_none())
            {
                py const wrongName = getattr_(excValue, py{ "name" }, none());
                py const suggestion = compute_suggestion_error(excValue, excTraceback, wrongName);
                if (suggestion.truthy())
                    self.set_attr("_str", self.attr("_str") + py{ ". Did you mean: '" + suggestion.str() + "'?" });
                if (issubclass(excType, builtin("NameError")))
                {
                    py const name = getattr_(excValue, py{ "name" }, none());
                    if (!name.is_none() && import_attribute("sys", "stdlib_module_names").contains(name))
                    {
                        if (suggestion.truthy())
                            self.set_attr("_str", self.attr("_str") + py{ " Or did you forget to import '" + name.str() + "'?" });
                        else
                            self.set_attr("_str", self.attr("_str") + py{ ". Did you forget to import '" + name.str() + "'?" });
                    }
                }
            }
            if (options.lookupLines.truthy())
                self.method("_load_lines");
            self.set_attr("__suppress_context__", !excValue.is_none() ? excValue.attr("__suppress_context__") : boolean(false));
            if (recursive)
                return;
            // (the cause, context and exceptions as TracebackExceptions (a queue rather than recursion))
            std::vector<std::pair<py, py>> queue{ { self, excValue } };
            while (!queue.empty())
            {
                auto const [te, e] = queue.back();
                queue.pop_back();
                py cause = none();
                if (!e.is_none() && !e.attr("__cause__").is_none() && !seen.contains(id_of(e.attr("__cause__"))))
                    cause = make_traceback_exception(e.attr("__cause__"), options, seen);
                bool const needContext = compact ? cause.is_none() && !e.is_none() && !e.attr("__suppress_context__").truthy() : true;
                py context = none();
                if (!e.is_none() && !e.attr("__context__").is_none() && needContext && !seen.contains(id_of(e.attr("__context__"))))
                    context = make_traceback_exception(e.attr("__context__"), options, seen);
                py exceptions = none();
                if (!e.is_none() && isinstance(e, builtin("BaseExceptionGroup")))
                {
                    std::vector<py> items;
                    for (auto const& exc : e.attr("exceptions").items())
                        items.push_back(make_traceback_exception(exc, options, seen));
                    exceptions = py_list(items);
                }
                te.set_attr("__cause__", cause);
                te.set_attr("__context__", context);
                te.set_attr("exceptions", exceptions);
                if (cause.truthy())
                    queue.emplace_back(te.attr("__cause__"), e.attr("__cause__"));
                if (context.truthy())
                    queue.emplace_back(te.attr("__context__"), e.attr("__context__"));
                if (exceptions.truthy())
                {
                    auto const tes = te.attr("exceptions").items();
                    auto const es = e.attr("exceptions").items();
                    for (std::size_t i = 0u; i < std::min(tes.size(), es.size()); ++i)
                        queue.emplace_back(tes[i], es[i]);
                }
            }
        }

        py exc_type_str(py const& aSelf)
        {
            if (!aSelf.attr("_have_exc_type").truthy())
                return none();
            py type = aSelf.attr("exc_type_qualname");
            py module = aSelf.attr("exc_type_module");
            if (module != py{ "__main__" } && module != py{ "builtins" })
            {
                if (!isinstance(module, builtin("str")))
                    module = py{ "<unknown>" };
                type = module + py{ "." } + type;
            }
            return type;
        }

        // (TracebackException._format_syntax_error)
        py format_syntax_error(py const& aSelf, py const& aType, bool aColorize)
        {
            std::vector<py> result;
            std::string filenameSuffix;
            py const filename = aSelf.attr("filename");
            py const lineno = aSelf.attr("lineno");
            if (!lineno.is_none())
            {
                py const shown = filename.truthy() ? filename : py{ "<string>" };
                if (aColorize)
                    result.push_back(py{ "  File {}\"{}\"{}, line {}{}{}\n" }.method("format", { py{ ansi("MAGENTA", true) }, shown, py{ ansi("RESET", true) },
                        py{ ansi("MAGENTA", true) }, lineno, py{ ansi("RESET", true) } }));
                else
                    result.push_back(py{ "  File \"{}\", line {}\n" }.method("format", { shown, lineno }));
            }
            else if (!filename.is_none())
                filenameSuffix = py{ " ({})" }.method("format", { filename }).str();
            py const text = aSelf.attr("text");
            if (isinstance(text, builtin("str")))
            {
                py const rtext = text.method("rstrip", { py{ "\n" } });
                py ltext = rtext.method("lstrip", { py{ " \n\f" } });
                long long const spaces = rtext.size() - ltext.size();
                py const offsetValue = aSelf.attr("offset");
                if (offsetValue.is_none())
                    result.push_back(py{ "    " } + ltext + py{ "\n" });
                else if (isinstance(offsetValue, builtin("int")))
                {
                    long long offset = offsetValue.as_index();
                    long long endOffset = 0;
                    if (lineno == aSelf.attr("end_lineno"))
                    {
                        py const e = aSelf.attr("end_offset");
                        endOffset = isinstance(e, builtin("int")) && e != py{ 0 } ? e.as_index() : offset;
                    }
                    else
                        endOffset = rtext.size() + 1;
                    if (text.truthy() && offset > text.size())
                        offset = rtext.size() + 1;
                    if (text.truthy() && endOffset > text.size())
                        endOffset = rtext.size() + 1;
                    if (offset >= endOffset || endOffset < 0)
                        endOffset = offset + 1;
                    long long const colno = offset - 1 - spaces;
                    long long const endColno = endOffset - 1 - spaces;
                    if (colno >= 0)
                    {
                        std::string caretSpace;
                        for (auto const& c : slice(ltext, none(), py{ colno }).items())
                            caretSpace += c.method("isspace").truthy() ? c.str() : " ";
                        std::string startColor;
                        std::string endColor;
                        if (aColorize)
                        {
                            ltext = slice(ltext, none(), py{ colno }) + py{ ansi("BOLD_RED", true) } + slice(ltext, py{ colno }, py{ endColno }) +
                                py{ ansi("RESET", true) } + slice(ltext, py{ endColno }, none());
                            startColor = ansi("BOLD_RED", true);
                            endColor = ansi("RESET", true);
                        }
                        result.push_back(py{ "    " } + ltext + py{ "\n" });
                        result.push_back(py{ "    " + caretSpace + startColor + std::string(static_cast<std::size_t>(std::max(0ll, endColno - colno)), '^') + endColor + "\n" });
                    }
                    else
                        result.push_back(py{ "    " } + ltext + py{ "\n" });
                }
            }
            py const msgValue = aSelf.attr("msg");
            py const msg = msgValue.truthy() ? msgValue : py{ "<no detail available>" };
            if (aColorize)
                result.push_back(py{ std::string{ ansi("BOLD_MAGENTA", true) } + aType.str() + ansi("RESET", true) + ": " + ansi("MAGENTA", true) + msg.str() +
                    ansi("RESET", true) + filenameSuffix + "\n" });
            else
                result.push_back(py{ "{}: {}{}\n" }.method("format", { aType, msg, py{ filenameSuffix } }));
            return builtin("iter")(py_list(result));
        }

        // (TracebackException.format_exception_only)
        py format_exception_only(py const& aSelf, bool aShowGroup, long long aDepth, bool aColorize)
        {
            std::vector<py> result;
            std::string const indent(static_cast<std::size_t>(3 * aDepth), ' ');
            if (!aSelf.attr("_have_exc_type").truthy())
            {
                result.push_back(py{ indent } + format_final_exc_line(none(), aSelf.attr("_str"), true, aColorize));
                return builtin("iter")(py_list(result));
            }
            py const type = aSelf.attr("exc_type_str");
            if (!aSelf.attr("_is_syntax_error").truthy())
            {
                if (aDepth > 0)
                {
                    for (auto const& l : format_final_exc_line(type, aSelf.attr("_str"), false, aColorize).method("split", { py{ "\n" } }).items())
                        result.push_back(py{ indent } + l + py{ "\n" });
                }
                else
                    result.push_back(format_final_exc_line(type, aSelf.attr("_str"), true, aColorize));
            }
            else
                for (auto const& l : aSelf.method("_format_syntax_error", { type }, { { "colorize", boolean(aColorize) } }).items())
                    result.push_back(py{ indent } + l);
            py const notes = aSelf.attr("__notes__");
            if (isinstance(notes, import_attribute("collections.abc", "Sequence")) && !isinstance(notes, py_tuple({ builtin("str"), builtin("bytes") })))
            {
                for (auto const& n : notes.items())
                    for (auto const& l : safe_string(n, "note").method("split", { py{ "\n" } }).items())
                        result.push_back(py{ indent } + l + py{ "\n" });
            }
            else if (!notes.is_none())
                result.push_back(py{ indent } + safe_string(notes, "__notes__", builtin("repr")) + py{ "\n" });
            py const exceptions = aSelf.attr("exceptions");
            if (exceptions.truthy() && aShowGroup)
                for (auto const& ex : exceptions.items())
                    for (auto const& l : ex.method("format_exception_only", {}, { { "show_group", boolean(aShowGroup) }, { "_depth", py{ aDepth + 1 } },
                        { "colorize", boolean(aColorize) } }).items())
                        result.push_back(l);
            return builtin("iter")(py_list(result));
        }

        bool has_traceback(py const& aException)
        {
            return aException.attr("stack").truthy() || builtin("getattr")(aException, py{ "_has_traceback" }, boolean(false)).truthy();
        }

        // (TracebackException.format)
        py traceback_exception_format(py const& aSelf, bool aChain, py aContext, bool aColorize)
        {
            if (aContext.empty() || aContext.is_none())
                aContext = py::borrowed(sPrintContext)();
            std::vector<std::pair<py, py>> output;
            py exc = aSelf;
            if (aChain)
            {
                while (exc.truthy())
                {
                    py message = none();
                    py chained = none();
                    if (!exc.attr("__cause__").is_none())
                    {
                        message = py{ sCauseMessage };
                        chained = exc.attr("__cause__");
                    }
                    else if (!exc.attr("__context__").is_none() && !exc.attr("__suppress_context__").truthy())
                    {
                        message = py{ sContextMessage };
                        chained = exc.attr("__context__");
                    }
                    output.emplace_back(message, exc);
                    exc = chained;
                }
            }
            else
                output.emplace_back(none(), exc);
            std::vector<py> result;
            auto const emit = [&](py const& aText, py const& aMargin = none())
            {
                for (auto const& t : aContext.method("emit", { aText, aMargin }).items())
                    result.push_back(t);
            };
            auto const colorize = [&]() -> std::vector<std::pair<char const*, py>> { return { { "colorize", boolean(aColorize) } }; };
            for (auto i = output.rbegin(); i != output.rend(); ++i)
            {
                auto const& [message, e] = *i;
                if (!message.is_none())
                    emit(message);
                py const exceptions = e.attr("exceptions");
                if (exceptions.is_none())
                {
                    if (has_traceback(e))
                    {
                        emit(py{ "Traceback (most recent call last):\n" });
                        emit(e.attr("stack").method("format", {}, colorize()));
                    }
                    emit(e.method("format_exception_only", {}, colorize()));
                }
                else if (aContext.attr("exception_group_depth") > aSelf.attr("max_group_depth"))
                    emit(py{ "... (max_group_depth is " } + builtin("str")(aSelf.attr("max_group_depth")) + py{ ")\n" });
                else
                {
                    bool const topLevel = aContext.attr("exception_group_depth") == py{ 0 };
                    if (topLevel)
                        aContext.set_attr("exception_group_depth", aContext.attr("exception_group_depth") + py{ 1 });
                    if (has_traceback(e))
                    {
                        emit(py{ "Exception Group Traceback (most recent call last):\n" }, topLevel ? py{ "+" } : none());
                        emit(e.attr("stack").method("format", {}, colorize()));
                    }
                    emit(e.method("format_exception_only", {}, colorize()));
                    long long const numExceptions = exceptions.size();
                    py const maxWidth = aSelf.attr("max_group_width");
                    long long const n = py{ numExceptions } <= maxWidth ? numExceptions : (maxWidth + py{ 1 }).as_index();
                    aContext.set_attr("need_close", boolean(false));
                    for (long long j = 0; j < n; ++j)
                    {
                        bool const last = j == n - 1;
                        if (last)
                            aContext.set_attr("need_close", boolean(true));
                        bool const truncated = !maxWidth.is_none() && py{ j } >= maxWidth;
                        std::string const title = truncated ? "..." : std::to_string(j + 1);
                        result.push_back(aContext.method("indent") + py{ std::string{ j == 0 ? "+-" : "  " } + "+---------------- " + title + " ----------------\n" });
                        aContext.set_attr("exception_group_depth", aContext.attr("exception_group_depth") + py{ 1 });
                        if (!truncated)
                        {
                            for (auto const& t : exceptions[py{ j }].method("format", {}, { { "chain", boolean(aChain) }, { "_ctx", aContext },
                                { "colorize", boolean(aColorize) } }).items())
                                result.push_back(t);
                        }
                        else
                        {
                            auto const remaining = numExceptions - maxWidth.as_index();
                            emit(py{ "and " + std::to_string(remaining) + " more exception" + (remaining > 1 ? "s" : "") + "\n" });
                        }
                        if (last && aContext.attr("need_close").truthy())
                        {
                            result.push_back(aContext.method("indent") + py{ "+------------------------------------\n" });
                            aContext.set_attr("need_close", boolean(false));
                        }
                        aContext.set_attr("exception_group_depth", aContext.attr("exception_group_depth") - py{ 1 });
                    }
                    if (topLevel)
                        aContext.set_attr("exception_group_depth", py{ 0 });
                }
            }
            return builtin("iter")(py_list(result));
        }

        builtin_entry const sTracebackExceptionMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        traceback_exception_init(parse("TracebackException.__init__", aArgs, aKwargs, { "self", "exc_type", "exc_value", "exc_traceback" }, 4u,
                            { "limit", "lookup_lines", "capture_locals", "compact", "max_group_width", "max_group_depth", "save_exc_type", "_seen" }));
                        return none();
                    });
                } },
            { "from_exception", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        if (aArgs.size() < 2u)
                            py_raise("TypeError", "TracebackException.from_exception() missing 1 required positional argument: 'exc'");
                        py const exc = py::borrowed(aArgs[1]);
                        std::vector<object*> args{ type_of(exc).reference(), exc.reference(), exc.attr("__traceback__").reference() };
                        for (std::size_t i = 2u; i < aArgs.size(); ++i)
                            args.push_back(new_reference(aArgs[i]));
                        return py{ invoke(aArgs[0], std::move(args), aKwargs) };
                    });
                } },
            { "exc_type", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("exc_type", aArgs, aKwargs, { "self" }, 1u);
                        import_attribute("warnings", "warn").call({ py{ "Deprecated in 3.13. Use exc_type_str instead." }, builtin("DeprecationWarning") },
                            { { "stacklevel", py{ 2 } } });
                        return a[0].attr("_exc_type");
                    });
                } },
            { "exc_type_str", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return exc_type_str(parse("exc_type_str", aArgs, aKwargs, { "self" }, 1u)[0]); }); } },
            { "_load_lines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("TracebackException._load_lines", aArgs, aKwargs, { "self" }, 1u);
                        for (auto const& frame : a[0].attr("stack").items())
                            frame.attr("line");
                        return none();
                    });
                } },
            { "__eq__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("TracebackException.__eq__", aArgs, aKwargs, { "self", "other" }, 2u);
                        if (isinstance(a[1], traceback_exception_class()))
                            return py{ compare(NEOS_PY_EQ, a[0].attr("__dict__").get(), a[1].attr("__dict__").get()) };
                        return py::borrowed(not_implemented());
                    });
                } },
            { "__str__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return parse("TracebackException.__str__", aArgs, aKwargs, { "self" }, 1u)[0].attr("_str"); }); } },
            { "format_exception_only", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("TracebackException.format_exception_only", aArgs, aKwargs, { "self" }, 1u, { "show_group", "_depth" }, true);
                        return format_exception_only(a[0], a.get(1, boolean(false)).truthy(), a.get(2, py{ 0 }).as_index(), colorize_of(a.extra));
                    });
                } },
            { "_format_syntax_error", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("TracebackException._format_syntax_error", aArgs, aKwargs, { "self", "stype" }, 2u, {}, true);
                        return format_syntax_error(a[0], a[1], colorize_of(a.extra));
                    });
                } },
            { "format", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("TracebackException.format", aArgs, aKwargs, { "self" }, 1u, { "chain", "_ctx" }, true);
                        return traceback_exception_format(a[0], a.get(1, boolean(true)).truthy(), a.get(2, none()), colorize_of(a.extra));
                    });
                } },
            { "print", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("TracebackException.print", aArgs, aKwargs, { "self" }, 1u, { "file", "chain" }, true);
                        py file = a.get(1, none());
                        if (file.is_none())
                            file = import_attribute("sys", "stderr");
                        for (auto const& line : a[0].method("format", {}, { { "chain", a.get(2, boolean(true)) }, { "colorize", boolean(colorize_of(a.extra)) } }).items())
                            print_to(line, file);
                        return none();
                    });
                } }
        };

        // --- the module's functions

        // (_parse_value_tb(exc, value, tb))
        std::pair<py, py> parse_value_tb(py const& aException, py const& aValue, py const& aTraceback)
        {
            bool const noValue = aValue.get() == sSentinel;
            bool const noTraceback = aTraceback.get() == sSentinel;
            if (noValue != noTraceback)
                py_raise("ValueError", "Both or neither of value and tb must be given");
            if (noValue && noTraceback)
            {
                if (!aException.is_none())
                {
                    if (isinstance(aException, builtin("BaseException")))
                        return { aException, aException.attr("__traceback__") };
                    py_raise("TypeError", "Exception expected for value, " + type_of(aException).attr("__name__").str() + " found");
                }
                return { none(), none() };
            }
            return { aValue, aTraceback };
        }

        py sentinel_or(parsed const& a, std::size_t aIndex) { return a.get(aIndex, py::borrowed(sSentinel)); }

        void print_exception(py const& aException, py const& aValue, py const& aTraceback, py const& aLimit, py const& aFile, py const& aChain, bool aColorize)
        {
            auto const [value, tb] = parse_value_tb(aException, aValue, aTraceback);
            py const te = traceback_exception_class().call({ type_of(value), value, tb }, { { "limit", aLimit }, { "compact", boolean(true) } });
            te.method("print", {}, { { "file", aFile }, { "chain", aChain }, { "colorize", boolean(aColorize) } });
        }

        py format_exception(py const& aException, py const& aValue, py const& aTraceback, py const& aLimit, py const& aChain, bool aColorize)
        {
            auto const [value, tb] = parse_value_tb(aException, aValue, aTraceback);
            py const te = traceback_exception_class().call({ type_of(value), value, tb }, { { "limit", aLimit }, { "compact", boolean(true) } });
            return builtin("list")(te.method("format", {}, { { "chain", aChain }, { "colorize", boolean(aColorize) } }));
        }

        void print_list(py const& aList, py aFile)
        {
            if (aFile.empty() || aFile.is_none())
                aFile = import_attribute("sys", "stderr");
            for (auto const& item : stack_summary_from_list(aList).method("format").items())
                print_to(item, aFile);
        }

        builtin_entry const sTracebackFunctions[] =
        {
            { "print_list", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("print_list", aArgs, aKwargs, { "extracted_list", "file" }, 1u);
                        print_list(a[0], a.get(1, none()));
                        return none();
                    });
                } },
            { "format_list", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("format_list", aArgs, aKwargs, { "extracted_list" }, 1u);
                        return stack_summary_from_list(a[0]).method("format");
                    });
                } },
            { "print_tb", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("print_tb", aArgs, aKwargs, { "tb", "limit", "file" }, 1u);
                        print_list(extract_tb(a[0], a.get(1, none())), a.get(2, none()));
                        return none();
                    });
                } },
            { "format_tb", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("format_tb", aArgs, aKwargs, { "tb", "limit" }, 1u);
                        return extract_tb(a[0], a.get(1, none())).method("format");
                    });
                } },
            { "extract_tb", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("extract_tb", aArgs, aKwargs, { "tb", "limit" }, 1u);
                        return extract_tb(a[0], a.get(1, none()));
                    });
                } },
            { "_parse_value_tb", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_parse_value_tb", aArgs, aKwargs, { "exc", "value", "tb" }, 3u);
                        auto const [value, tb] = parse_value_tb(a[0], a[1], a[2]);
                        return py_tuple({ value, tb });
                    });
                } },
            { "print_exception", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("print_exception", aArgs, aKwargs, { "exc", "value", "tb", "limit", "file", "chain" }, 1u, {}, true, 1u);
                        print_exception(a[0], sentinel_or(a, 1), sentinel_or(a, 2), a.get(3, none()), a.get(4, none()), a.get(5, boolean(true)), colorize_of(a.extra));
                        return none();
                    });
                } },
            { "_print_exception_bltin", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_print_exception_bltin", aArgs, aKwargs, { "exc" }, 1u, {}, false, 1u);
                        py file = import_attribute("sys", "stderr");
                        if (file.is_none())
                            file = import_attribute("sys", "__stderr__");
                        bool const colorize = import_attribute("_colorize", "can_colorize").call({}, { { "file", file } }).truthy();
                        print_exception(a[0], py::borrowed(sSentinel), py::borrowed(sSentinel), py::borrowed(sBuiltinExceptionLimit), file, boolean(true), colorize);
                        return none();
                    });
                } },
            { "format_exception", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("format_exception", aArgs, aKwargs, { "exc", "value", "tb", "limit", "chain" }, 1u, {}, true, 1u);
                        return format_exception(a[0], sentinel_or(a, 1), sentinel_or(a, 2), a.get(3, none()), a.get(4, boolean(true)), colorize_of(a.extra));
                    });
                } },
            { "format_exception_only", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("format_exception_only", aArgs, aKwargs, { "exc", "value" }, 1u, { "show_group" }, true, 1u);
                        py value = sentinel_or(a, 1);
                        if (value.get() == sSentinel)
                            value = a[0];
                        py const te = traceback_exception_class().call({ type_of(value), value, none() }, { { "compact", boolean(true) } });
                        return builtin("list")(te.method("format_exception_only", {}, { { "show_group", a.get(2, boolean(false)) },
                            { "colorize", boolean(colorize_of(a.extra)) } }));
                    });
                } },
            { "_format_final_exc_line", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_format_final_exc_line", aArgs, aKwargs, { "etype", "value" }, 2u, { "insert_final_newline", "colorize" });
                        return format_final_exc_line(a[0], a[1], a.get(2, boolean(true)).truthy(), a.get(3, boolean(false)).truthy());
                    });
                } },
            { "_safe_string", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_safe_string", aArgs, aKwargs, { "value", "what", "func" }, 2u);
                        return safe_string(a[0], a[1].str(), a.get(2, builtin("str")));
                    });
                } },
            { "print_exc", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("print_exc", aArgs, aKwargs, { "limit", "file", "chain" }, 0u);
                        print_exception(import_attribute("sys", "exception")(), py::borrowed(sSentinel), py::borrowed(sSentinel), a.get(0, none()), a.get(1, none()),
                            a.get(2, boolean(true)), false);
                        return none();
                    });
                } },
            { "format_exc", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("format_exc", aArgs, aKwargs, { "limit", "chain" }, 0u);
                        return py{ "" }.method("join", { format_exception(import_attribute("sys", "exception")(), py::borrowed(sSentinel), py::borrowed(sSentinel),
                            a.get(0, none()), a.get(1, boolean(true)), false) });
                    });
                } },
            { "print_last", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("print_last", aArgs, aKwargs, { "limit", "file", "chain" }, 0u);
                        py const sys{ fn_import("sys") };
                        auto const hasattr = builtin("hasattr");
                        if (!hasattr(sys, py{ "last_exc" }).truthy() && !hasattr(sys, py{ "last_type" }).truthy())
                            py_raise("ValueError", "no last exception");
                        if (hasattr(sys, py{ "last_exc" }).truthy())
                            print_exception(sys.attr("last_exc"), py::borrowed(sSentinel), py::borrowed(sSentinel), a.get(0, none()), a.get(1, none()), a.get(2, boolean(true)), false);
                        else
                            print_exception(sys.attr("last_type"), sys.attr("last_value"), sys.attr("last_traceback"), a.get(0, none()), a.get(1, none()),
                                a.get(2, boolean(true)), false);
                        return none();
                    });
                } },
            { "print_stack", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("print_stack", aArgs, aKwargs, { "f", "limit", "file" }, 0u);
                        print_list(extract_stack(a.get(0, none()), a.get(1, none())), a.get(2, none()));
                        return none();
                    });
                } },
            { "format_stack", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("format_stack", aArgs, aKwargs, { "f", "limit" }, 0u);
                        return stack_summary_from_list(extract_stack(a.get(0, none()), a.get(1, none()))).method("format");
                    });
                } },
            { "extract_stack", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("extract_stack", aArgs, aKwargs, { "f", "limit" }, 0u);
                        return extract_stack(a.get(0, none()), a.get(1, none()));
                    });
                } },
            { "clear_frames", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("clear_frames", aArgs, aKwargs, { "tb" }, 1u);
                        py tb = a[0];
                        while (!tb.is_none())
                        {
                            py const frame = tb.attr("tb_frame");
                            if (!frame.is_none())
                            {
                                try
                                {
                                    frame.method("clear");
                                }
                                catch (raised const&)
                                {
                                    caught_which({ "RuntimeError" });
                                }
                            }
                            tb = tb.attr("tb_next");
                        }
                        return none();
                    });
                } },
            { "walk_stack", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return walk_stack(parse("walk_stack", aArgs, aKwargs, { "f" }, 1u)[0]); }); } },
            { "walk_tb", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return walk_tb(parse("walk_tb", aArgs, aKwargs, { "tb" }, 1u)[0]); }); } },
            { "_walk_tb_with_full_positions", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return walk_tb_with_full_positions(parse("_walk_tb_with_full_positions", aArgs, aKwargs, { "tb" }, 1u)[0]); }); } },
            { "_get_code_position", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_get_code_position", aArgs, aKwargs, { "code", "instruction_index" }, 2u);
                        if (a[1] < py{ 0 })
                            return py_tuple({ none(), none(), none(), none() });
                        return builtin("next")(import_attribute("itertools", "islice")(a[0].method("co_positions"), floordiv(a[1], py{ 2 }), none()));
                    });
                } },
            { "_byte_offset_to_character_offset", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_byte_offset_to_character_offset", aArgs, aKwargs, { "str", "offset" }, 2u);
                        return byte_offset_to_character_offset(a[0], a[1]);
                    });
                } },
            { "_display_width", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_display_width", aArgs, aKwargs, { "line", "offset" }, 1u);
                        return py{ display_width(a[0], a.get(1, none())) };
                    });
                } },
            { "_substitution_cost", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_substitution_cost", aArgs, aKwargs, { "ch_a", "ch_b" }, 2u);
                        return py{ substitution_cost(a[0], a[1]) };
                    });
                } },
            { "_get_safe___dir__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return safe_dir(parse("_get_safe___dir__", aArgs, aKwargs, { "obj" }, 1u)[0]); }); } },
            { "_compute_suggestion_error", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_compute_suggestion_error", aArgs, aKwargs, { "exc_value", "tb", "wrong_name" }, 3u);
                        return compute_suggestion_error(a[0], a[1], a[2]);
                    });
                } },
            { "_levenshtein_distance", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = parse("_levenshtein_distance", aArgs, aKwargs, { "a", "b", "max_cost" }, 3u);
                        return py{ levenshtein_distance(a[0], a[1], a[2].as_index()) };
                    });
                } }
        };

        builtin_entry const sSentinelMethods[] =
        {
            { "__repr__", [](std::vector<object*> const&, object*) -> object* { return make_str("<implicit>"); } }
        };

        // (a class's attribute aName (a method) as a property or a classmethod)
        void make_property(py const& aClass, char const* aName)
        {
            py const p{ b_property({ aClass.attr(aName).get() }, nullptr) };
            py_checked(class_setattr_raw(aClass.get(), aName, p.get()) == 0);
        }

        void make_classmethod(py const& aClass, char const* aName)
        {
            py const function = aClass.attr("__dict__")[py{ aName }];
            py const m{ b_classmethod({ function.get() }, nullptr) };
            py_checked(class_setattr_raw(aClass.get(), aName, m.get()) == 0);
        }

        bool make_traceback_classes()
        {
            if (sTracebackException != nullptr)
                return true;
            auto const result = py_guarded([&]()
            {
                py const frameSummary{ make_class_of("traceback", "FrameSummary", {}, sFrameSummaryMethods, { { "__doc__", make_str(
                    "Information about a single frame from a traceback.\n\n"
                    "    - :attr:`filename` The filename for the frame.\n"
                    "    - :attr:`lineno` The line within filename for the frame that was\n"
                    "      active when the frame was captured.\n"
                    "    - :attr:`name` The name of the function or method that was executing\n"
                    "      when the frame was captured.\n"
                    "    - :attr:`line` The text from the linecache module for the line\n"
                    "      of code that was running when the frame was captured.\n"
                    "    - :attr:`locals` Either None if locals were not supplied, or a dict\n"
                    "      mapping the name to the repr() of the variable.\n    ") } }) };
                for (auto name : { "_original_lines", "_dedented_lines", "line" })
                    make_property(frameSummary, name);
                py const stackSummary{ make_class_of("traceback", "StackSummary", { builtin("list").get() }, sStackSummaryMethods,
                    { { "__doc__", make_str("A list of FrameSummary objects, representing a stack of frames.") } }) };
                for (auto name : { "extract", "_extract_from_extended_frame_gen", "from_list" })
                    make_classmethod(stackSummary, name);
                py const printContext{ make_class_of("traceback", "_ExceptionPrintContext", {}, sPrintContextMethods) };
                py const tracebackException{ make_class_of("traceback", "TracebackException", {}, sTracebackExceptionMethods, { { "__doc__", make_str(
                    "An exception ready for rendering.\n\n"
                    "    The traceback module captures enough attributes from the original exception\n"
                    "    to this intermediary form to ensure that no references are held, while\n"
                    "    still being able to fully print or format it.\n\n"
                    "    max_group_width and max_group_depth control the formatting of exception\n"
                    "    groups. The depth refers to the nesting level of the group, and the width\n"
                    "    refers to the size of a single exception group's exceptions array. The\n"
                    "    formatted output is truncated when either limit is exceeded.\n\n"
                    "    Use `from_exception` to create TracebackException instances from exception\n"
                    "    objects, or the constructor to create TracebackException instances from\n"
                    "    individual components.\n    ") } }) };
                make_classmethod(tracebackException, "from_exception");
                for (auto name : { "exc_type", "exc_type_str" })
                    make_property(tracebackException, name);
                py const sentinelClass{ make_class_of("traceback", "_Sentinel", {}, sSentinelMethods) };
                sSentinel = sentinelClass().release();
                sBuiltinExceptionLimit = builtin("object")().release();
                sFrameSummary = frameSummary.reference();
                sStackSummary = stackSummary.reference();
                sPrintContext = printContext.reference();
                sTracebackException = tracebackException.reference();
                py_checked(class_setattr_raw(sentinelClass.get(), "__module__", py{ "traceback" }.get()) == 0);
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        bool traceback_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            if (!make_traceback_classes())
                return false;
            for (auto const& f : sTracebackFunctions)
                aAttributes.emplace_back(f.name, make_builtin(f));
            aAttributes.emplace_back("FrameSummary", new_reference(sFrameSummary));
            aAttributes.emplace_back("StackSummary", new_reference(sStackSummary));
            aAttributes.emplace_back("_ExceptionPrintContext", new_reference(sPrintContext));
            aAttributes.emplace_back("TracebackException", new_reference(sTracebackException));
            aAttributes.emplace_back("_sentinel", new_reference(sSentinel));
            aAttributes.emplace_back("_Sentinel", class_of(sSentinel));
            aAttributes.emplace_back("BUILTIN_EXCEPTION_LIMIT", new_reference(sBuiltinExceptionLimit));
            aAttributes.emplace_back("_cause_message", make_str(sCauseMessage));
            aAttributes.emplace_back("_context_message", make_str(sContextMessage));
            aAttributes.emplace_back("_RECURSIVE_CUTOFF", make_int(sRecursiveCutoff));
            aAttributes.emplace_back("_WIDE_CHAR_SPECIFIERS", make_str("WF"));
            aAttributes.emplace_back("_MAX_CANDIDATE_ITEMS", make_int(static_cast<long long>(sMaxCandidateItems)));
            aAttributes.emplace_back("_MAX_STRING_SIZE", make_int(static_cast<long long>(sMaxStringSize)));
            aAttributes.emplace_back("_MOVE_COST", make_int(static_cast<long long>(sMoveCost)));
            aAttributes.emplace_back("_CASE_COST", make_int(static_cast<long long>(sCaseCost)));
            for (auto module : { "sys", "linecache", "textwrap", "warnings", "itertools", "collections" })
            {
                auto const m = fn_import(module);
                if (m == nullptr)
                    return false;
                aAttributes.emplace_back(module, m);
            }
            std::vector<object*> all;
            for (auto name : { "extract_stack", "extract_tb", "format_exception", "format_exception_only", "format_list", "format_stack", "format_tb",
                "print_exc", "format_exc", "print_exception", "print_last", "print_stack", "print_tb", "clear_frames", "FrameSummary", "StackSummary",
                "TracebackException", "walk_stack", "walk_tb" })
                all.push_back(make_str(name));
            aAttributes.emplace_back("__all__", make_list(std::move(all)));
            return true;
        }

        inline bool const sTraceback = register_module("traceback", traceback_module);
    }

    // (an exception's report as traceback.format_exception formats it (aColorize: with ANSI colors); false if the module traceback could not
    // format it (no exception remains raised))
    bool traceback_report(object* aException, bool aColorize, std::string& aResult)
    {
        thread_local bool tBusy = false;
        if (tBusy)
            return false;
        tBusy = true;
        struct reset { ~reset() { tBusy = false; } } const resetBusy;
        auto const lines = py_guarded([&]()
        {
            return import_attribute("traceback", "format_exception").call({ py::borrowed(aException) }, { { "colorize", py::boolean(aColorize) } });
        });
        if (lines == nullptr)
        {
            clear_error();
            return false;
        }
        ref const owner{ lines };
        aResult.clear();
        std::vector<object*> items;
        if (!collect(lines, items))
        {
            clear_error();
            return false;
        }
        bool ok = true;
        for (auto i : items)
        {
            if (kind_of(i) == NEOS_PY_STR)
                aResult += as<str_object>(i)->value;
            else
                ok = false;
            release(i);
        }
        return ok;
    }

    // (whether reports of unhandled exceptions are colored (as _colorize.can_colorize(file=sys.stderr)))
    bool traceback_colorize()
    {
        auto const result = py_guarded([&]()
        {
            return py::boolean(import_attribute("_colorize", "can_colorize").call({}, { { "file", import_attribute("sys", "stderr") } }).truthy());
        });
        if (result == nullptr)
        {
            clear_error();
            return false;
        }
        ref const owner{ result };
        return truth(result) == 1;
    }
}
