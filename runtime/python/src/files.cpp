/*
  files.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/fnmatch.py, Lib/glob.py, Lib/shutil.py and Lib/tempfile.py),
  Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software
  Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos
  Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules fnmatch, glob, shutil and tempfile (CPython 3.13's algorithms, behaviour and messages); their file operations are
// those of the module os

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // --- helpers

            ref own(object* aObject) { return ref{ aObject }; }
            ref str_ref(std::string aText) { return ref{ make_str(std::move(aText)) }; }

            // (a function of os (or of os.path: "path.name"))
            ref os_function(char const* aName)
            {
                ref const os{ fn_import("os") };
                if (!os)
                    return {};
                if (std::strncmp(aName, "path.", 5u) == 0)
                {
                    ref const path{ getattr(os.get(), "path") };
                    return path ? ref{ getattr(path.get(), aName + 5) } : ref{};
                }
                return ref{ getattr(os.get(), aName) };
            }

            // (a function of os (or os.path): called (steals the arguments))
            ref os_call(char const* aName, std::vector<object*> aArgs, object* aKwargs = nullptr)
            {
                ref const function{ os_function(aName) };
                if (!function)
                {
                    for (auto a : aArgs)
                        release(a);
                    return {};
                }
                return ref{ invoke(function.get(), std::move(aArgs), aKwargs) };
            }

            // (a path argument: str (or bytes, decoded as UTF-8: aBytes) from str, bytes or os.PathLike)
            bool path_text(object* aValue, std::string& aResult, bool* aBytes = nullptr)
            {
                ref const p{ os_call("fspath", { new_reference(aValue) }) };
                if (!p)
                    return false;
                if (auto const s = str_of_value(p.get()))
                {
                    aResult = *s;
                    if (aBytes != nullptr)
                        *aBytes = false;
                    return true;
                }
                std::string_view data;
                if (!bytes_like(p.get(), data))
                    return (error("TypeError", "expected str, bytes or os.PathLike object"), false);
                aResult = std::string{ data };
                if (aBytes != nullptr)
                    *aBytes = true;
                return true;
            }

            object* path_value(std::string aPath, bool aBytes) { return aBytes ? make_bytes(std::move(aPath)) : make_str(std::move(aPath)); }

            // --- fnmatch

            // (re.escape of a character)
            void regex_escape(std::u32string& aResult, char32_t aCharacter)
            {
                static std::u32string_view const sSpecial = U"()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
                if (sSpecial.find(aCharacter) != std::u32string_view::npos)
                    aResult += U'\\';
                aResult += aCharacter;
            }

            std::u32string replace_all(std::u32string aText, std::u32string_view aFrom, std::u32string_view aTo)
            {
                std::u32string result;
                std::size_t start = 0u;
                for (;;)
                {
                    auto const at = aText.find(aFrom, start);
                    if (at == std::u32string::npos)
                        break;
                    result.append(aText, start, at - start);
                    result += aTo;
                    start = at + aFrom.size();
                }
                result.append(aText, start);
                return result;
            }

            // (fnmatch._translate: the parts of a pattern's regular expression (a star: nullopt))
            struct translated_part
            {
                bool star = false;
                std::u32string text;
            };

            std::vector<translated_part> translate_parts(std::u32string const& aPattern, std::u32string const& aQuestionMark)
            {
                std::vector<translated_part> result;
                auto const add = [&](std::u32string aText) { result.push_back({ false, std::move(aText) }); };
                std::size_t i = 0u;
                auto const n = aPattern.size();
                while (i < n)
                {
                    auto const c = aPattern[i++];
                    if (c == U'*')
                    {
                        // (consecutive stars: one)
                        if (result.empty() || !result.back().star)
                            result.push_back({ true, {} });
                    }
                    else if (c == U'?')
                        add(aQuestionMark);
                    else if (c == U'[')
                    {
                        auto j = i;
                        if (j < n && aPattern[j] == U'!')
                            ++j;
                        if (j < n && aPattern[j] == U']')
                            ++j;
                        while (j < n && aPattern[j] != U']')
                            ++j;
                        if (j >= n)
                            add(U"\\[");
                        else
                        {
                            auto stuff = aPattern.substr(i, j - i);
                            if (stuff.find(U'-') == std::u32string::npos)
                                stuff = replace_all(stuff, U"\\", U"\\\\");
                            else
                            {
                                std::vector<std::u32string> chunks;
                                auto k = aPattern[i] == U'!' ? i + 2u : i + 1u;
                                for (;;)
                                {
                                    k = k < j ? aPattern.find(U'-', k) : std::u32string::npos;
                                    if (k == std::u32string::npos || k >= j)
                                        break;
                                    chunks.push_back(aPattern.substr(i, k - i));
                                    i = k + 1u;
                                    k = k + 3u;
                                }
                                auto const chunk = aPattern.substr(i, j - i);
                                if (!chunk.empty())
                                    chunks.push_back(chunk);
                                else
                                    chunks.back() += U'-';
                                // (empty ranges are removed: they are invalid in a regular expression)
                                for (auto k2 = chunks.size() - 1u; k2 > 0u; --k2)
                                    if (!chunks[k2 - 1u].empty() && !chunks[k2].empty() && chunks[k2 - 1u].back() > chunks[k2].front())
                                    {
                                        chunks[k2 - 1u] = chunks[k2 - 1u].substr(0u, chunks[k2 - 1u].size() - 1u) + chunks[k2].substr(1u);
                                        chunks.erase(chunks.begin() + static_cast<std::ptrdiff_t>(k2));
                                    }
                                // (backslashes and hyphens are escaped for set difference (--); hyphens that make ranges are not)
                                stuff.clear();
                                for (std::size_t c2 = 0u; c2 < chunks.size(); ++c2)
                                    stuff += (c2 == 0u ? U"" : U"-") + replace_all(replace_all(chunks[c2], U"\\", U"\\\\"), U"-", U"\\-");
                            }
                            // (set operations (&&, ~~ and ||) are escaped)
                            std::u32string escaped;
                            for (auto x : stuff)
                            {
                                if (x == U'&' || x == U'~' || x == U'|')
                                    escaped += U'\\';
                                escaped += x;
                            }
                            stuff = escaped;
                            i = j + 1u;
                            if (stuff.empty())
                                add(U"(?!)");      // (an empty range: never matches)
                            else if (stuff == U"!")
                                add(U".");         // (a negated empty range: any character)
                            else
                            {
                                if (stuff[0] == U'!')
                                    stuff = U"^" + stuff.substr(1u);
                                else if (stuff[0] == U'^' || stuff[0] == U'[')
                                    stuff = U"\\" + stuff;
                                add(U"[" + stuff + U"]");
                            }
                        }
                    }
                    else
                    {
                        std::u32string e;
                        regex_escape(e, c);
                        add(e);
                    }
                }
                return result;
            }

            std::u32string join_translated_parts(std::vector<translated_part> const& aParts)
            {
                std::u32string result;
                std::size_t i = 0u;
                auto const n = aParts.size();
                while (i < n && !aParts[i].star)
                    result += aParts[i++].text;
                // (STAR fixed STAR fixed ...: an interior "STAR fixed" is a minimal match of .*? then fixed, without backtracking)
                while (i < n)
                {
                    ++i;
                    if (i == n)
                    {
                        result += U".*";
                        break;
                    }
                    std::u32string fixed;
                    while (i < n && !aParts[i].star)
                        fixed += aParts[i++].text;
                    if (i == n)
                        result += U".*" + fixed;
                    else
                        result += U"(?>.*?" + fixed + U")";
                }
                return U"(?s:" + result + U")\\Z";
            }

            std::string fnmatch_translate(std::string const& aPattern)
            {
                return from_code_points(join_translated_parts(translate_parts(to_code_points(aPattern), U".")));
            }

            // (a pattern's compiled regular expression's match method (cached))
            object* pattern_matcher(std::string const& aPattern)
            {
                static std::map<std::string, object*> sCache;
                if (auto const m = sCache.find(aPattern); m != sCache.end())
                    return new_reference(m->second);
                ref const compile{ module_attribute("re", "compile") };
                if (!compile)
                    return nullptr;
                ref const compiled{ invoke(compile.get(), { make_str(fnmatch_translate(aPattern)) }) };
                if (!compiled)
                    return nullptr;
                auto const match = getattr(compiled.get(), "match");
                if (match == nullptr)
                    return nullptr;
                if (sCache.size() >= 32768u)
                {
                    for (auto& c : sCache)
                        release(c.second);
                    sCache.clear();
                }
                sCache[aPattern] = new_reference(match);
                return match;
            }

            // (a name or a pattern: its text (bytes: as latin-1, as fnmatch translates them))
            bool match_text(object* aValue, std::string& aResult, bool& aBytes)
            {
                if (auto const s = str_of_value(aValue))
                {
                    aResult = *s;
                    aBytes = false;
                    return true;
                }
                std::string_view data;
                if (bytes_like(aValue, data))
                {
                    aResult = from_code_points(std::u32string{ data.begin(), data.end() });
                    aBytes = true;
                    return true;
                }
                std::string text;
                return path_text(aValue, aResult, &aBytes);
            }

            // (fnmatchcase(name, pattern): 1, 0 or -1 (an exception))
            int match_case(object* aName, object* aPattern)
            {
                std::string name, pattern;
                bool nameBytes = false, patternBytes = false;
                if (!match_text(aPattern, pattern, patternBytes))
                    return -1;
                ref const matcher{ pattern_matcher(pattern) };
                if (!matcher)
                    return -1;
                std::string_view data;
                bool const nameIsBytes = bytes_like(aName, data);
                if (kind_of(aName) != NEOS_PY_STR && !nameIsBytes)
                    return (error("TypeError", std::string{ "expected string or bytes-like object, got '" } + type_name(aName) + "'"), -1);
                if (patternBytes != nameIsBytes)
                    return (error("TypeError", patternBytes ? "cannot use a bytes pattern on a string-like object" : "cannot use a string pattern on a bytes-like object"), -1);
                if (!match_text(aName, name, nameBytes))
                    return -1;
                ref const m{ invoke(matcher.get(), { make_str(name) }) };
                if (!m)
                    return -1;
                return kind_of(m.get()) != NEOS_PY_NONE ? 1 : 0;
            }

            ref normcase(object* aPath) { return os_call("path.normcase", { new_reference(aPath) }); }

            object* fnmatch_filter(object* aNames, object* aPattern)
            {
                ref const pattern{ normcase(aPattern) };
                if (!pattern)
                    return nullptr;
                std::vector<object*> names;
                if (!collect(aNames, names))
                    return nullptr;
                std::vector<object*> result;
                for (auto n : names)
                {
                    ref const name{ n };
                    ref const normal{ normcase(n) };
                    auto const r = normal ? match_case(normal.get(), pattern.get()) : -1;
                    if (r < 0)
                    {
                        for (auto x : result)
                            release(x);
                        return nullptr;
                    }
                    if (r == 1)
                        result.push_back(new_reference(n));
                }
                return make_list(std::move(result));
            }

            builtin_entry const sFnmatchFunctions[] =
            {
                { "fnmatch", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("fnmatch", aArgs, aKwargs, { "name", "pat" }, 2u, 0u, a))
                            return nullptr;
                        ref const name{ normcase(a[0]) };
                        ref const pattern{ name ? normcase(a[1]) : ref{} };
                        if (!pattern)
                            return nullptr;
                        auto const r = match_case(name.get(), pattern.get());
                        return r < 0 ? nullptr : make_bool(r == 1);
                    } },
                { "fnmatchcase", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("fnmatchcase", aArgs, aKwargs, { "name", "pat" }, 2u, 0u, a))
                            return nullptr;
                        auto const r = match_case(a[0], a[1]);
                        return r < 0 ? nullptr : make_bool(r == 1);
                    } },
                { "filter", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("filter", aArgs, aKwargs, { "names", "pat" }, 2u, 0u, a))
                            return nullptr;
                        return fnmatch_filter(a[0], a[1]);
                    } },
                { "translate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("translate", aArgs, aKwargs, { "pat" }, 1u, 0u, a))
                            return nullptr;
                        auto const s = str_of_value(a[0]);
                        if (s == nullptr)
                            return error("TypeError", std::string{ "expected str, not " } + type_name(a[0]));
                        return make_str(fnmatch_translate(*s));
                    } }
            };

            bool fnmatch_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sFnmatchFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("__all__", make_list({ make_str("filter"), make_str("fnmatch"), make_str("fnmatchcase"), make_str("translate") }));
                return true;
            }

            // --- glob

            bool has_magic(std::string const& aText) { return aText.find_first_of("*?[") != std::string::npos; }
            bool is_hidden(std::string const& aName) { return !aName.empty() && aName[0] == '.'; }
            bool is_recursive(std::string const& aPattern) { return aPattern == "**"; }

            struct glob_state
            {
                bool recursive = false;
                bool includeHidden = false;
                bool bytes = false;
            };

            bool path_join(std::string const& aDirectory, std::string const& aName, std::string& aResult)
            {
                ref const r{ os_call("path.join", { make_str(aDirectory), make_str(aName) }) };
                if (!r)
                    return false;
                aResult = as<str_object>(r.get())->value;
                return true;
            }

            std::string glob_join(std::string const& aDirectory, std::string const& aName)
            {
                if (aDirectory.empty() || aName.empty())
                    return aDirectory.empty() ? aName : aDirectory;
                std::string result;
                if (!path_join(aDirectory, aName, result))
                    clear_error();
                return result;
            }

            bool path_test(char const* aFunction, std::string const& aPath)
            {
                ref const r{ os_call(aFunction, { make_str(aPath) }) };
                if (!r)
                {
                    clear_error();
                    return false;
                }
                return truth(r.get()) == 1;
            }

            // (_listdir: the names of a directory's entries (directories only: aDirectoriesOnly); none if it cannot be read)
            std::vector<std::string> list_directory(std::string const& aDirectory, bool aDirectoriesOnly)
            {
                std::vector<std::string> result;
                ref const it{ os_call("scandir", { make_str(aDirectory.empty() ? std::string{ "." } : aDirectory) }) };
                if (!it)
                {
                    clear_error();
                    return result;
                }
                for (;;)
                {
                    ref const entry{ next(it.get()) };
                    if (!entry)
                    {
                        clear_error();
                        break;
                    }
                    if (aDirectoriesOnly)
                    {
                        ref const isDir{ call_method_named(entry.get(), "is_dir", {}) };
                        if (!isDir)
                        {
                            clear_error();
                            continue;
                        }
                        if (truth(isDir.get()) != 1)
                            continue;
                    }
                    ref const name{ getattr(entry.get(), "name") };
                    if (name && kind_of(name.get()) == NEOS_PY_STR)
                        result.push_back(as<str_object>(name.get())->value);
                    clear_error();
                }
                return result;
            }

            bool glob1(std::string const& aDirectory, std::string const& aPattern, bool aDirectoriesOnly, glob_state const& aState, std::vector<std::string>& aResult)
            {
                auto names = list_directory(aDirectory, aDirectoriesOnly);
                if (!(aState.includeHidden || is_hidden(aPattern)))
                    names.erase(std::remove_if(names.begin(), names.end(), is_hidden), names.end());
                ref const pattern{ normcase(ref{ make_str(aPattern) }.get()) };
                if (!pattern)
                    return false;
                for (auto const& n : names)
                {
                    ref const name{ normcase(ref{ make_str(n) }.get()) };
                    auto const r = name ? match_case(name.get(), pattern.get()) : -1;
                    if (r < 0)
                        return false;
                    if (r == 1)
                        aResult.push_back(n);
                }
                return true;
            }

            void glob0(std::string const& aDirectory, std::string const& aBasename, std::vector<std::string>& aResult)
            {
                if (!aBasename.empty())
                {
                    if (path_test("path.lexists", glob_join(aDirectory, aBasename)))
                        aResult.push_back(aBasename);
                }
                else if (path_test("path.isdir", aDirectory))
                    aResult.push_back(aBasename);
            }

            void rlistdir(std::string const& aDirectory, bool aDirectoriesOnly, glob_state const& aState, std::vector<std::string>& aResult)
            {
                for (auto const& x : list_directory(aDirectory, aDirectoriesOnly))
                    if (aState.includeHidden || !is_hidden(x))
                    {
                        aResult.push_back(x);
                        auto const path = aDirectory.empty() ? x : glob_join(aDirectory, x);
                        std::vector<std::string> inner;
                        rlistdir(path, aDirectoriesOnly, aState, inner);
                        for (auto const& y : inner)
                            aResult.push_back(glob_join(x, y));
                    }
            }

            void glob2(std::string const& aDirectory, bool aDirectoriesOnly, glob_state const& aState, std::vector<std::string>& aResult)
            {
                if (aDirectory.empty() || path_test("path.isdir", aDirectory))
                    aResult.push_back({});
                rlistdir(aDirectory, aDirectoriesOnly, aState, aResult);
            }

            bool split_path(std::string const& aPath, std::string& aDirectory, std::string& aBasename)
            {
                ref const r{ os_call("path.split", { make_str(aPath) }) };
                if (!r || kind_of(r.get()) != NEOS_PY_TUPLE)
                    return false;
                aDirectory = as<str_object>(as<tuple_object>(r.get())->items[0])->value;
                aBasename = as<str_object>(as<tuple_object>(r.get())->items[1])->value;
                return true;
            }

            // (_iglob)
            bool iglob(std::string const& aPathname, std::string const& aRoot, bool aDirectoriesOnly, glob_state const& aState, std::vector<std::string>& aResult)
            {
                std::string directory, basename;
                if (!split_path(aPathname, directory, basename))
                    return false;
                if (!has_magic(aPathname))
                {
                    if (!basename.empty())
                    {
                        if (path_test("path.lexists", glob_join(aRoot, aPathname)))
                            aResult.push_back(aPathname);
                    }
                    else if (path_test("path.isdir", glob_join(aRoot, directory)))
                        // (a pattern that ends with a separator matches only directories)
                        aResult.push_back(aPathname);
                    return true;
                }
                if (directory.empty())
                {
                    if (aState.recursive && is_recursive(basename))
                        glob2(aRoot, aDirectoriesOnly, aState, aResult);
                    else
                        return glob1(aRoot, basename, aDirectoriesOnly, aState, aResult);
                    return true;
                }
                std::vector<std::string> directories;
                if (directory != aPathname && has_magic(directory))
                {
                    if (!iglob(directory, aRoot, true, aState, directories))
                        return false;
                }
                else
                    directories.push_back(directory);
                for (auto const& d : directories)
                {
                    std::vector<std::string> names;
                    auto const in = glob_join(aRoot, d);
                    if (has_magic(basename))
                    {
                        if (aState.recursive && is_recursive(basename))
                            glob2(in, aDirectoriesOnly, aState, names);
                        else if (!glob1(in, basename, aDirectoriesOnly, aState, names))
                            return false;
                    }
                    else
                        glob0(in, basename, names);
                    for (auto const& n : names)
                    {
                        std::string joined;
                        if (!path_join(d, n, joined))
                            return false;
                        aResult.push_back(joined);
                    }
                }
                return true;
            }

            object* glob_list(std::vector<object*> const& aArgs, object* aKwargs, char const* aFunction)
            {
                std::vector<object*> a;
                if (!parse_arguments(aFunction, aArgs, aKwargs, { "pathname", "root_dir", "dir_fd", "recursive", "include_hidden" }, 1u, 1u, a))
                    return nullptr;
                glob_state state;
                std::string pathname, root;
                if (!path_text(a[0], pathname, &state.bytes))
                    return nullptr;
                if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE && !path_text(a[1], root))
                    return nullptr;
                state.recursive = a[3] != nullptr && truth(a[3]) == 1;
                state.includeHidden = a[4] != nullptr && truth(a[4]) == 1;
                std::vector<std::string> results;
                if (!iglob(pathname, root, false, state, results))
                    return nullptr;
                // (the empty string that a recursive pattern's first match is: skipped)
                if ((pathname.empty() || (state.recursive && is_recursive(pathname.substr(0u, 2u)))) && !results.empty() && results.front().empty())
                    results.erase(results.begin());
                std::vector<object*> items;
                for (auto& r : results)
                    items.push_back(path_value(std::move(r), state.bytes));
                return make_list(std::move(items));
            }

            // (glob.translate)
            std::string glob_translate(std::string const& aPattern, bool aRecursive, bool aIncludeHidden, std::vector<std::string> const& aSeparators)
            {
                std::u32string escapedSeparators;
                for (auto const& s : aSeparators)
                    for (auto c : to_code_points(s))
                        regex_escape(escapedSeparators, c);
                auto const anySep = aSeparators.size() > 1u ? U"[" + escapedSeparators + U"]" : escapedSeparators;
                auto const notSep = U"[^" + escapedSeparators + U"]";
                std::u32string oneLastSegment, oneSegment, anySegments, anyLastSegments;
                if (aIncludeHidden)
                {
                    oneLastSegment = notSep + U"+";
                    oneSegment = oneLastSegment + anySep;
                    anySegments = U"(?:.+" + anySep + U")?";
                    anyLastSegments = U".*";
                }
                else
                {
                    oneLastSegment = U"[^" + escapedSeparators + U".]" + notSep + U"*";
                    oneSegment = oneLastSegment + anySep;
                    anySegments = U"(?:" + oneSegment + U")*";
                    anyLastSegments = anySegments + U"(?:" + oneLastSegment + U")?";
                }
                // (the pattern's parts: split at its separators)
                std::vector<std::u32string> parts{ {} };
                std::u32string separatorCharacters;
                for (auto const& s : aSeparators)
                    separatorCharacters += to_code_points(s);
                for (auto c : to_code_points(aPattern))
                    if (separatorCharacters.find(c) != std::u32string::npos)
                        parts.emplace_back();
                    else
                        parts.back() += c;
                std::u32string result;
                auto const last = parts.size() - 1u;
                for (std::size_t idx = 0u; idx < parts.size(); ++idx)
                {
                    auto const& part = parts[idx];
                    if (part == U"*")
                        result += idx < last ? oneSegment : oneLastSegment;
                    else if (aRecursive && part == U"**")
                    {
                        if (idx < last)
                        {
                            if (parts[idx + 1u] != U"**")
                                result += anySegments;
                        }
                        else
                            result += anyLastSegments;
                    }
                    else
                    {
                        if (!part.empty())
                        {
                            if (!aIncludeHidden && (part[0] == U'*' || part[0] == U'?'))
                                result += U"(?!\\.)";
                            for (auto const& p : translate_parts(part, notSep))
                                result += p.star ? notSep + U"*" : p.text;
                        }
                        if (idx < last)
                            result += anySep;
                    }
                }
                return from_code_points(U"(?s:" + result + U")\\Z");
            }

            builtin_entry const sGlobFunctions[] =
            {
                { "glob", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return glob_list(aArgs, aKwargs, "glob"); } },
                { "iglob", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        ref const list{ glob_list(aArgs, aKwargs, "iglob") };
                        return list ? iter(list.get()) : nullptr;
                    } },
                { "has_magic", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text;
                        bool bytes = false;
                        if (!parse_arguments("has_magic", aArgs, aKwargs, { "s" }, 1u, 0u, a) || !match_text(a[0], text, bytes))
                            return nullptr;
                        return make_bool(has_magic(text));
                    } },
                { "escape", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("escape", aArgs, aKwargs, { "pathname" }, 1u, 0u, a))
                            return nullptr;
                        ref const parts{ os_call("path.splitdrive", { new_reference(a[0]) }) };
                        if (!parts)
                            return nullptr;
                        std::string drive, rest;
                        bool bytes = false;
                        if (!match_text(as<tuple_object>(parts.get())->items[0], drive, bytes) || !match_text(as<tuple_object>(parts.get())->items[1], rest, bytes))
                            return nullptr;
                        std::string escaped;
                        for (auto c : rest)
                        {
                            if (c == '*' || c == '?' || c == '[')
                                escaped += std::string{ "[" } + c + "]";
                            else
                                escaped += c;
                        }
                        if (bytes)
                        {
                            // (bytes: each code point is a byte)
                            auto const points = to_code_points(drive + escaped);
                            std::string data;
                            data.reserve(points.size());
                            for (auto const point : points)
                                data.push_back(static_cast<char>(point));
                            return make_bytes(std::move(data));
                        }
                        return make_str(drive + escaped);
                    } },
                { "translate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("translate", aArgs, aKwargs, { "pat", "recursive", "include_hidden", "seps" }, 1u, 1u, a))
                            return nullptr;
                        auto const pattern = str_of_value(a[0]);
                        if (pattern == nullptr)
                            return error("TypeError", std::string{ "expected str, not " } + type_name(a[0]));
                        std::vector<std::string> separators;
                        if (a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE && truth(a[3]) == 1)
                        {
                            if (auto const s = str_of_value(a[3]))
                                for (auto c : to_code_points(*s))
                                    separators.push_back(from_code_points(std::u32string(1u, c)));
                            else
                            {
                                std::vector<object*> items;
                                if (!collect(a[3], items))
                                    return nullptr;
                                for (auto i : items)
                                {
                                    separators.push_back(text_of(i));
                                    release(i);
                                }
                            }
                        }
                        else
                        {
                            ref const os{ fn_import("os") };
                            ref const path{ os ? getattr(os.get(), "path") : nullptr };
                            ref const s{ path ? getattr(path.get(), "sep") : nullptr };
                            ref const alt{ path ? getattr(path.get(), "altsep") : nullptr };
                            if (!s || !alt)
                                return nullptr;
                            separators.push_back(text_of(s.get()));
                            if (kind_of(alt.get()) == NEOS_PY_STR)
                                separators.push_back(text_of(alt.get()));
                        }
                        return make_str(glob_translate(*pattern, a[1] != nullptr && truth(a[1]) == 1, a[2] != nullptr && truth(a[2]) == 1, separators));
                    } },
                { "glob0", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string directory, pattern;
                        if (!parse_arguments("glob0", aArgs, aKwargs, { "dirname", "pattern" }, 2u, 0u, a) || !path_text(a[0], directory) || !path_text(a[1], pattern))
                            return nullptr;
                        std::vector<std::string> names;
                        glob0(directory, pattern, names);
                        std::vector<object*> items;
                        for (auto& n : names)
                            items.push_back(make_str(std::move(n)));
                        return make_list(std::move(items));
                    } },
                { "glob1", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string directory, pattern;
                        if (!parse_arguments("glob1", aArgs, aKwargs, { "dirname", "pattern" }, 2u, 0u, a) || !path_text(a[0], directory) || !path_text(a[1], pattern))
                            return nullptr;
                        std::vector<std::string> names;
                        if (!glob1(directory, pattern, false, glob_state{}, names))
                            return nullptr;
                        std::vector<object*> items;
                        for (auto& n : names)
                            items.push_back(make_str(std::move(n)));
                        return make_list(std::move(items));
                    } }
            };

            bool glob_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sGlobFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("__all__", make_list({ make_str("escape"), make_str("glob"), make_str("iglob"), make_str("translate") }));
                ref const compile{ module_attribute("re", "compile") };
                if (compile)
                {
                    aAttributes.emplace_back("magic_check", invoke(compile.get(), { make_str("([*?[])") }));
                    aAttributes.emplace_back("magic_check_bytes", make_str("([*?[])"));
                }
                clear_error();
                return true;
            }

            // --- shutil

            object* shutil_error(char const* aName)
            {
                if (std::strcmp(aName, "Error") == 0)
                    return module_exception("shutil.Error", { "OSError" });
                if (std::strcmp(aName, "SameFileError") == 0)
                {
                    ref const base{ shutil_error("Error") };
                    return module_exception("shutil.SameFileError", { "shutil.Error" });
                }
                if (std::strcmp(aName, "RegistryError") == 0)
                    return module_exception("shutil.RegistryError", { "Exception" });
                return module_exception((std::string{ "shutil." } + aName).c_str(), { "OSError" });
            }

            void raise_shutil(char const* aName, std::vector<object*> aArgs)   // (steals the arguments)
            {
                ref const cls{ shutil_error(aName) };
                ref const e{ cls ? invoke(cls.get(), std::move(aArgs)) : nullptr };
                if (e)
                    raise_exception(e.get());
            }

            bool is_dir_entry(object* aObject) { return aObject != nullptr && std::string_view{ type_name(aObject) } == "DirEntry"; }

            ref stat_of(object* aPath, bool aFollow = true)
            {
                if (is_dir_entry(aPath))
                {
                    ref const follow{ make_bool(aFollow) };
                    ref const kw{ keywords({ { "follow_symlinks", follow.get() } }) };
                    ref const method{ getattr(aPath, "stat") };
                    return method ? ref{ invoke(method.get(), {}, kw.get()) } : ref{};
                }
                return os_call(aFollow ? "stat" : "lstat", { new_reference(aPath) });
            }

            bool is_link(object* aPath)
            {
                if (is_dir_entry(aPath))
                {
                    ref const r{ call_method_named(aPath, "is_symlink", {}) };
                    return r && truth(r.get()) == 1;
                }
                ref const r{ os_call("path.islink", { new_reference(aPath) }) };
                if (!r)
                    clear_error();
                return r && truth(r.get()) == 1;
            }

            ref entry_path(object* aPath)
            {
                if (is_dir_entry(aPath))
                    return ref{ getattr(aPath, "path") };
                return ref{ new_reference(aPath) };
            }

            bool same_file(object* aSource, object* aDestination)
            {
                ref const source{ entry_path(aSource) };
                ref const r{ os_call("path.samefile", { new_reference(source.get()), new_reference(aDestination) }) };
                if (!r)
                {
                    clear_error();
                    return false;
                }
                return truth(r.get()) == 1;
            }

            long long st_mode(object* aStat)
            {
                ref const mode{ getattr(aStat, "st_mode") };
                long long m = 0;
                if (!mode || !to_index(mode.get(), m))
                    clear_error();
                return m;
            }

            constexpr long long kModeType = 0170000, kModeFifo = 0010000, kModeLink = 0120000;

            object* copy_file_object(object* aSource, object* aDestination, long long aLength)
            {
                if (aLength <= 0)
                    aLength = 256 * 1024;
                ref const read{ getattr(aSource, "read") };
                ref const write{ getattr(aDestination, "write") };
                if (!read || !write)
                    return nullptr;
                for (;;)
                {
                    ref const buffer{ invoke(read.get(), { make_int(aLength) }) };
                    if (!buffer)
                        return nullptr;
                    auto const t = truth(buffer.get());
                    if (t < 0)
                        return nullptr;
                    if (t == 0)
                        break;
                    ref const w{ invoke(write.get(), { new_reference(buffer.get()) }) };
                    if (!w)
                        return nullptr;
                }
                return new_reference(none());
            }

            // (copyfile(src, dst, *, follow_symlinks=True))
            object* copy_file(object* aSource, object* aDestination, bool aFollow)
            {
                if (same_file(aSource, aDestination))
                {
                    ref const s{ entry_path(aSource) };
                    raise_shutil("SameFileError", { make_str(repr_text(s.get()) + " and " + repr_text(aDestination) + " are the same file") });
                    return nullptr;
                }
                for (auto fn : { aSource, aDestination })
                {
                    ref const st{ stat_of(fn) };
                    if (!st)
                    {
                        clear_error();
                        continue;
                    }
                    if ((st_mode(st.get()) & kModeType) == kModeFifo)
                    {
                        ref const p{ entry_path(fn) };
                        raise_shutil("SpecialFileError", { make_str("`" + text_of(p.get()) + "` is a named pipe") });
                        return nullptr;
                    }
                }
                if (!aFollow && is_link(aSource))
                {
                    ref const target{ os_call("readlink", { new_reference(aSource) }) };
                    if (!target)
                        return nullptr;
                    ref const r{ os_call("symlink", { new_reference(target.get()), new_reference(aDestination) }) };
                    return r ? new_reference(aDestination) : nullptr;
                }
                ref const source{ entry_path(aSource) };
                ref const in{ b_open({ source.get(), ref{ make_str("rb") }.get() }, nullptr) };
                if (!in)
                    return nullptr;
                ref const out{ b_open({ aDestination, ref{ make_str("wb") }.get() }, nullptr) };
                if (!out)
                {
                    ref const e{ take_exception() };
                    ref const closed{ call_method_named(in.get(), "close", {}) };
                    // (a directory that does not exist: a less confusing exception)
                    if (e && exception_is(e.get(), "IsADirectoryError"))
                    {
                        ref const exists{ os_call("path.exists", { new_reference(aDestination) }) };
                        if (exists && truth(exists.get()) == 0)
                        {
                            ref const cls{ exception_class("FileNotFoundError") };
                            ref const n{ invoke(cls.get(), { make_str("Directory does not exist: " + text_of(aDestination)) }) };
                            if (n)
                            {
                                setattr(n.get(), "__cause__", e.get());
                                raise_exception(n.get());
                            }
                            return nullptr;
                        }
                    }
                    raise_exception(e.get());
                    return nullptr;
                }
                ref const copied{ copy_file_object(in.get(), out.get(), 0) };
                ref const e{ copied ? ref{} : take_exception() };
                ref const c1{ call_method_named(out.get(), "close", {}) };
                ref const c2{ call_method_named(in.get(), "close", {}) };
                if (e)
                {
                    raise_exception(e.get());
                    return nullptr;
                }
                if (!c1 || !c2)
                    return nullptr;
                return new_reference(aDestination);
            }

            object* copy_mode(object* aSource, object* aDestination, bool aFollow)
            {
                ref st;
                if (!aFollow && is_link(aSource) && is_link(aDestination))
                    return new_reference(none());   // (lchmod is not available)
                st = stat_of(aSource, true);
                if (!st)
                    return nullptr;
                ref const r{ os_call("chmod", { new_reference(aDestination), make_int(st_mode(st.get()) & 07777) }) };
                return r ? new_reference(none()) : nullptr;
            }

            object* copy_stat(object* aSource, object* aDestination, bool aFollowSymlinks)
            {
                bool const follow = aFollowSymlinks || !(is_link(aSource) && is_link(aDestination));
                if (!follow)
                    return new_reference(none());
                ref const st{ stat_of(aSource, follow) };
                if (!st)
                    return nullptr;
                ref const atime{ getattr(st.get(), "st_atime_ns") };
                ref const mtime{ getattr(st.get(), "st_mtime_ns") };
                if (!atime || !mtime)
                    return nullptr;
                ref const ns{ make_tuple({ new_reference(atime.get()), new_reference(mtime.get()) }) };
                ref const kw{ keywords({ { "ns", ns.get() } }) };
                ref const u{ os_call("utime", { new_reference(aDestination) }, kw.get()) };
                if (!u)
                    return nullptr;
                ref const c{ os_call("chmod", { new_reference(aDestination), make_int(st_mode(st.get()) & 07777) }) };
                return c ? new_reference(none()) : nullptr;
            }

            // (copy and copy2: the destination may be a directory)
            object* copy_into(object* aSource, object* aDestination, bool aFollow, bool aMetadata)
            {
                ref destination{ new_reference(aDestination) };
                ref const isDir{ os_call("path.isdir", { new_reference(aDestination) }) };
                if (!isDir)
                    return nullptr;
                if (truth(isDir.get()) == 1)
                {
                    ref const source{ entry_path(aSource) };
                    ref const base{ os_call("path.basename", { new_reference(source.get()) }) };
                    if (!base)
                        return nullptr;
                    destination = os_call("path.join", { new_reference(aDestination), new_reference(base.get()) });
                    if (!destination)
                        return nullptr;
                }
                ref const copied{ copy_file(aSource, destination.get(), aFollow) };
                if (!copied)
                    return nullptr;
                ref const r{ aMetadata ? copy_stat(aSource, destination.get(), aFollow) : copy_mode(aSource, destination.get(), aFollow) };
                return r ? destination.release_ownership() : nullptr;
            }

            bool follow_keyword(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<object*>& a)
            {
                if (!parse_arguments(aFunction, aArgs, aKwargs, { "src", "dst", "follow_symlinks" }, 2u, 0u, a))
                    return false;
                if (aArgs.size() > 2u)
                    return (error("TypeError", std::string{ aFunction } + "() takes 2 positional arguments but " + std::to_string(aArgs.size()) + " were given"), false);
                return true;
            }

            object* shutil_function(char const* aName)
            {
                return module_attribute("shutil", aName);
            }

            // (copytree)
            object* copy_tree(object* aSource, object* aDestination, bool aSymlinks, object* aIgnore, object* aCopyFunction, bool aIgnoreDangling, bool aDirsExistOk)
            {
                ref const it{ os_call("scandir", { new_reference(aSource) }) };
                if (!it)
                    return nullptr;
                std::vector<object*> entryItems;
                if (!collect(it.get(), entryItems))
                    return nullptr;
                std::vector<ref> entries;
                for (auto e : entryItems)
                    entries.emplace_back(e);
                ref ignored{ fn_set() };
                if (aIgnore != nullptr && kind_of(aIgnore) != NEOS_PY_NONE)
                {
                    std::vector<object*> names;
                    for (auto const& e : entries)
                        names.push_back(getattr(e.get(), "name"));
                    ref const fspath{ os_call("fspath", { new_reference(aSource) }) };
                    if (!fspath)
                        return nullptr;
                    ignored = ref{ invoke(aIgnore, { new_reference(fspath.get()), make_list(std::move(names)) }) };
                    if (!ignored)
                        return nullptr;
                }
                ref const existOk{ make_bool(aDirsExistOk) };
                ref const kw{ keywords({ { "exist_ok", existOk.get() } }) };
                ref const made{ os_call("makedirs", { new_reference(aDestination) }, kw.get()) };
                if (!made)
                    return nullptr;
                std::vector<object*> errors;
                ref const copy2{ shutil_function("copy2") };
                ref const copy1{ shutil_function("copy") };
                bool const useEntry = aCopyFunction == copy2.get() || aCopyFunction == copy1.get();
                for (auto const& entry : entries)
                {
                    ref const name{ getattr(entry.get(), "name") };
                    if (!name)
                        return nullptr;
                    auto const skip = contains(ignored.get(), name.get());
                    if (skip < 0)
                        return nullptr;
                    if (skip == 1)
                        continue;
                    ref const sourceName{ os_call("path.join", { new_reference(aSource), new_reference(name.get()) }) };
                    ref const destinationName{ os_call("path.join", { new_reference(aDestination), new_reference(name.get()) }) };
                    if (!sourceName || !destinationName)
                        return nullptr;
                    auto const sourceObject = useEntry ? entry.get() : sourceName.get();
                    auto const step = [&]() -> bool
                    {
                        ref const symlink{ call_method_named(entry.get(), "is_symlink", {}) };
                        if (!symlink)
                            return false;
                        auto const recurse = [&]() -> bool
                        {
                            ref const r{ copy_tree(sourceObject, destinationName.get(), aSymlinks, aIgnore, aCopyFunction, aIgnoreDangling, aDirsExistOk) };
                            return static_cast<bool>(r);
                        };
                        if (truth(symlink.get()) == 1)
                        {
                            ref const target{ os_call("readlink", { new_reference(sourceName.get()) }) };
                            if (!target)
                                return false;
                            if (aSymlinks)
                            {
                                ref const linked{ os_call("symlink", { new_reference(target.get()), new_reference(destinationName.get()) }) };
                                if (!linked)
                                    return false;
                                ref const st{ copy_stat(sourceObject, destinationName.get(), !aSymlinks) };
                                return static_cast<bool>(st);
                            }
                            ref const exists{ os_call("path.exists", { new_reference(target.get()) }) };
                            if (!exists)
                                return false;
                            if (truth(exists.get()) == 0 && aIgnoreDangling)
                                return true;
                            ref const isDir{ call_method_named(entry.get(), "is_dir", {}) };
                            if (!isDir)
                                return false;
                            if (truth(isDir.get()) == 1)
                                return recurse();
                            ref const r{ invoke(aCopyFunction, { new_reference(sourceObject), new_reference(destinationName.get()) }) };
                            return static_cast<bool>(r);
                        }
                        ref const isDir{ call_method_named(entry.get(), "is_dir", {}) };
                        if (!isDir)
                            return false;
                        if (truth(isDir.get()) == 1)
                            return recurse();
                        ref const r{ invoke(aCopyFunction, { new_reference(sourceObject), new_reference(destinationName.get()) }) };
                        return static_cast<bool>(r);
                    };
                    if (!step())
                    {
                        ref const e{ take_exception() };
                        if (e && exception_is(e.get(), "shutil.Error"))
                        {
                            ref const args{ getattr(e.get(), "args") };
                            std::vector<object*> items;
                            if (args && !as<tuple_object>(args.get())->items.empty() && collect(as<tuple_object>(args.get())->items[0], items))
                                errors.insert(errors.end(), items.begin(), items.end());
                            clear_error();
                        }
                        else if (e && exception_is(e.get(), "OSError"))
                            errors.push_back(make_tuple({ new_reference(sourceName.get()), new_reference(destinationName.get()), make_str(text_of(e.get())) }));
                        else
                        {
                            for (auto x : errors)
                                release(x);
                            if (e)
                                raise_exception(e.get());
                            return nullptr;
                        }
                    }
                }
                if (ref const st{ copy_stat(aSource, aDestination, true) }; !st)
                {
                    ref const e{ take_exception() };
                    if (e && exception_is(e.get(), "OSError"))
                        errors.push_back(make_tuple({ new_reference(aSource), new_reference(aDestination), make_str(text_of(e.get())) }));
                    else
                    {
                        for (auto x : errors)
                            release(x);
                        if (e)
                            raise_exception(e.get());
                        return nullptr;
                    }
                }
                if (!errors.empty())
                {
                    raise_shutil("Error", { make_list(std::move(errors)) });
                    return nullptr;
                }
                return new_reference(aDestination);
            }

            // (rmtree's error handler: onexc(function, path, exception); false: the exception is raised)
            struct rmtree_handler
            {
                bool ignore = false;
                object* onexc = nullptr;
                object* onerror = nullptr;
                bool handle(char const* aFunction, object* aPath, object* aException)
                {
                    if (ignore)
                        return true;
                    if (onexc == nullptr && onerror == nullptr)
                    {
                        raise_exception(aException);
                        return false;
                    }
                    ref const function{ os_function(aFunction) };
                    if (!function)
                        clear_error();
                    ref r;
                    if (onexc != nullptr)
                        r = ref{ invoke(onexc, { new_reference(function ? function.get() : none()), new_reference(aPath), new_reference(aException) }) };
                    else
                    {
                        ref const cls{ class_of(aException) };
                        ref const tb{ getattr(aException, "__traceback__") };
                        clear_error();
                        ref const info{ make_tuple({ new_reference(cls.get()), new_reference(aException), new_reference(tb ? tb.get() : none()) }) };
                        r = ref{ invoke(onerror, { new_reference(function ? function.get() : none()), new_reference(aPath), new_reference(info.get()) }) };
                    }
                    return static_cast<bool>(r);
                }
            };

            // (the result of an os function: false if it raised an exception that was not handled)
            bool rm_step(rmtree_handler& aHandler, char const* aFunction, object* aPath, bool aMissingOk)
            {
                ref const r{ os_call(aFunction, { new_reference(aPath) }) };
                if (r)
                    return true;
                ref const e{ take_exception() };
                if (aMissingOk && e && exception_is(e.get(), "FileNotFoundError"))
                    return true;
                return e && aHandler.handle(aFunction, aPath, e.get());
            }

            bool rmtree_unsafe(object* aPath, rmtree_handler& aHandler)
            {
                ref const it{ os_call("scandir", { new_reference(aPath) }) };
                if (!it)
                {
                    ref const e{ take_exception() };
                    if (e && exception_is(e.get(), "FileNotFoundError"))
                        return true;
                    if (!e || !aHandler.handle("scandir", aPath, e.get()))
                        return false;
                }
                else
                {
                    std::vector<object*> items;
                    if (!collect(it.get(), items))
                        return false;
                    std::vector<ref> entries;
                    for (auto i : items)
                        entries.emplace_back(i);
                    for (auto const& entry : entries)
                    {
                        ref const name{ getattr(entry.get(), "name") };
                        ref const full{ name ? os_call("path.join", { new_reference(aPath), new_reference(name.get()) }) : ref{} };
                        if (!full)
                            return false;
                        ref const follow{ make_bool(false) };
                        ref const kw{ keywords({ { "follow_symlinks", follow.get() } }) };
                        ref const isDirMethod{ getattr(entry.get(), "is_dir") };
                        ref const isDir{ isDirMethod ? invoke(isDirMethod.get(), {}, kw.get()) : nullptr };
                        if (!isDir)
                        {
                            clear_error();
                            continue;
                        }
                        if (truth(isDir.get()) == 1)
                        {
                            if (!rmtree_unsafe(full.get(), aHandler))
                                return false;
                        }
                        else if (!rm_step(aHandler, "unlink", full.get(), true))
                            return false;
                    }
                }
                return rm_step(aHandler, "rmdir", aPath, true);
            }

            object* rmtree(object* aPath, rmtree_handler& aHandler)
            {
                ref const st{ os_call("lstat", { new_reference(aPath) }) };
                if (!st)
                {
                    ref const e{ take_exception() };
                    return e && aHandler.handle("lstat", aPath, e.get()) ? new_reference(none()) : nullptr;
                }
                if ((st_mode(st.get()) & kModeType) == kModeLink)
                {
                    // (symbolic links to directories are forbidden)
                    ref const cls{ exception_class("OSError") };
                    ref const e{ invoke(cls.get(), { make_str("Cannot call rmtree on a symbolic link") }) };
                    return e && aHandler.handle("path.islink", aPath, e.get()) ? new_reference(none()) : nullptr;
                }
                return rmtree_unsafe(aPath, aHandler) ? new_reference(none()) : nullptr;
            }

            bool dest_in_source(object* aSource, object* aDestination)
            {
                ref const s{ os_call("path.realpath", { new_reference(aSource) }) };
                ref const d{ os_call("path.realpath", { new_reference(aDestination) }) };
                ref const os{ fn_import("os") };
                ref const path{ os ? getattr(os.get(), "path") : nullptr };
                ref const sep{ path ? getattr(path.get(), "sep") : nullptr };
                if (!s || !d || !sep)
                {
                    clear_error();
                    return false;
                }
                auto src = text_of(s.get()), dst = text_of(d.get());
                auto const separator = text_of(sep.get());
                if (!src.ends_with(separator))
                    src += separator;
                if (!dst.ends_with(separator))
                    dst += separator;
                return dst.starts_with(src);
            }

            object* move(object* aSource, object* aDestination, object* aCopyFunction)
            {
                ref realDestination{ new_reference(aDestination) };
                ref const isDir{ os_call("path.isdir", { new_reference(aDestination) }) };
                if (!isDir)
                    return nullptr;
                if (truth(isDir.get()) == 1)
                {
                    if (same_file(aSource, aDestination) && !is_link(aSource))
                    {
                        // (a case-insensitive file system: renamed anyway)
                        ref const r{ os_call("rename", { new_reference(aSource), new_reference(aDestination) }) };
                        return r ? new_reference(none()) : nullptr;
                    }
                    // (the source's last component, without a trailing separator)
                    ref const fspath{ os_call("fspath", { new_reference(aSource) }) };
                    if (!fspath)
                        return nullptr;
                    auto source = text_of(fspath.get());
                    while (!source.empty() && (source.back() == '/'
#if defined(_WIN32)
                        || source.back() == '\\'
#endif
                        ))
                        source.pop_back();
                    ref const base{ os_call("path.basename", { make_str(source) }) };
                    realDestination = base ? os_call("path.join", { new_reference(aDestination), new_reference(base.get()) }) : ref{};
                    if (!realDestination)
                        return nullptr;
                    ref const exists{ os_call("path.exists", { new_reference(realDestination.get()) }) };
                    if (!exists)
                        return nullptr;
                    if (truth(exists.get()) == 1)
                    {
                        raise_shutil("Error", { make_str("Destination path '" + text_of(realDestination.get()) + "' already exists") });
                        return nullptr;
                    }
                }
                if (ref const renamed{ os_call("rename", { new_reference(aSource), new_reference(realDestination.get()) }) })
                    return realDestination.release_ownership();
                ref const e{ take_exception() };
                if (!e || !exception_is(e.get(), "OSError"))
                {
                    if (e)
                        raise_exception(e.get());
                    return nullptr;
                }
                if (is_link(aSource))
                {
                    ref const target{ os_call("readlink", { new_reference(aSource) }) };
                    ref const linked{ target ? os_call("symlink", { new_reference(target.get()), new_reference(realDestination.get()) }) : ref{} };
                    ref const unlinked{ linked ? os_call("unlink", { new_reference(aSource) }) : ref{} };
                    return unlinked ? realDestination.release_ownership() : nullptr;
                }
                ref const sourceIsDir{ os_call("path.isdir", { new_reference(aSource) }) };
                if (!sourceIsDir)
                    return nullptr;
                if (truth(sourceIsDir.get()) == 1)
                {
                    if (dest_in_source(aSource, aDestination))
                    {
                        raise_shutil("Error", { make_str("Cannot move a directory '" + text_of(aSource) + "' into itself '" + text_of(aDestination) + "'.") });
                        return nullptr;
                    }
                    ref const copied{ copy_tree(aSource, realDestination.get(), true, nullptr, aCopyFunction, false, false) };
                    if (!copied)
                        return nullptr;
                    rmtree_handler handler;
                    ref const removed{ rmtree(aSource, handler) };
                    return removed ? realDestination.release_ownership() : nullptr;
                }
                ref const copied{ invoke(aCopyFunction, { new_reference(aSource), new_reference(realDestination.get()) }) };
                ref const unlinked{ copied ? os_call("unlink", { new_reference(aSource) }) : ref{} };
                return unlinked ? realDestination.release_ownership() : nullptr;
            }

            // (which(cmd, mode=os.F_OK | os.X_OK, path=None))
            object* which(object* aCommand, long long aMode, object* aPath)
            {
                std::string command;
                bool bytes = false;
                if (!path_text(aCommand, command, &bytes))
                    return nullptr;
                auto const accessible = [&](std::string const& aFile) -> bool
                {
                    ref const exists{ os_call("path.exists", { make_str(aFile) }) };
                    ref const isDir{ os_call("path.isdir", { make_str(aFile) }) };
                    ref const access{ os_call("access", { make_str(aFile), make_int(aMode) }) };
                    if (!exists || !isDir || !access)
                    {
                        clear_error();
                        return false;
                    }
                    return truth(exists.get()) == 1 && truth(isDir.get()) == 0 && truth(access.get()) == 1;
                };
#if defined(_WIN32)
                char const separator = ';';
                bool const hasDirectory = command.find_first_of("/\\") != std::string::npos;
#else
                char const separator = ':';
                bool const hasDirectory = command.find('/') != std::string::npos;
#endif
                if (hasDirectory)
                    return accessible(command) ? path_value(command, bytes) : new_reference(none());
                std::string pathList;
                if (aPath == nullptr || kind_of(aPath) == NEOS_PY_NONE)
                {
                    ref const getenv{ os_call("getenv", { make_str("PATH") }) };
                    if (!getenv)
                        return nullptr;
                    if (kind_of(getenv.get()) == NEOS_PY_NONE)
                    {
#if defined(_WIN32)
                        pathList = "";
#else
                        pathList = "/bin:/usr/bin";
#endif
                    }
                    else
                        pathList = text_of(getenv.get());
                }
                else if (!path_text(aPath, pathList))
                    return nullptr;
                if (pathList.empty())
                    return new_reference(none());
                std::vector<std::string> directories;
                std::size_t start = 0u;
                for (;;)
                {
                    auto const end = pathList.find(separator, start);
                    directories.push_back(pathList.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    if (end == std::string::npos)
                        break;
                    start = end + 1u;
                }
                std::vector<std::string> files{ command };
#if defined(_WIN32)
                {
                    // (the extensions of PATHEXT)
                    ref const extensions{ os_call("getenv", { make_str("PATHEXT"), make_str(".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WS;.MSC") }) };
                    std::string list = extensions ? text_of(extensions.get()) : std::string{};
                    clear_error();
                    std::vector<std::string> withExtension;
                    std::size_t s = 0u;
                    for (;;)
                    {
                        auto const e = list.find(';', s);
                        auto const ext = list.substr(s, e == std::string::npos ? std::string::npos : e - s);
                        if (!ext.empty())
                            withExtension.push_back(command + ext);
                        if (e == std::string::npos)
                            break;
                        s = e + 1u;
                    }
                    files.insert(files.end(), withExtension.begin(), withExtension.end());
                }
#endif
                std::vector<std::string> seen;
                for (auto const& d : directories)
                {
                    ref const normal{ os_call("path.normcase", { make_str(d) }) };
                    auto const key = normal ? text_of(normal.get()) : d;
                    clear_error();
                    if (std::find(seen.begin(), seen.end(), key) != seen.end())
                        continue;
                    seen.push_back(key);
                    for (auto const& f : files)
                    {
                        std::string name;
                        if (!path_join(d, f, name))
                            return nullptr;
                        if (accessible(name))
                            return path_value(name, bytes);
                    }
                }
                return new_reference(none());
            }

            builtin_entry const sShutilFunctions[] =
            {
                { "copyfileobj", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("copyfileobj", aArgs, aKwargs, { "fsrc", "fdst", "length" }, 2u, 0u, a))
                            return nullptr;
                        long long length = 0;
                        if (a[2] != nullptr && !to_index(a[2], length))
                            return nullptr;
                        return copy_file_object(a[0], a[1], length);
                    } },
                { "copyfile", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!follow_keyword("copyfile", aArgs, aKwargs, a))
                            return nullptr;
                        return copy_file(a[0], a[1], a[2] == nullptr || truth(a[2]) == 1);
                    } },
                { "copymode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!follow_keyword("copymode", aArgs, aKwargs, a))
                            return nullptr;
                        return copy_mode(a[0], a[1], a[2] == nullptr || truth(a[2]) == 1);
                    } },
                { "copystat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!follow_keyword("copystat", aArgs, aKwargs, a))
                            return nullptr;
                        return copy_stat(a[0], a[1], a[2] == nullptr || truth(a[2]) == 1);
                    } },
                { "copy", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!follow_keyword("copy", aArgs, aKwargs, a))
                            return nullptr;
                        return copy_into(a[0], a[1], a[2] == nullptr || truth(a[2]) == 1, false);
                    } },
                { "copy2", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!follow_keyword("copy2", aArgs, aKwargs, a))
                            return nullptr;
                        return copy_into(a[0], a[1], a[2] == nullptr || truth(a[2]) == 1, true);
                    } },
                { "copytree", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("copytree", aArgs, aKwargs, { "src", "dst", "symlinks", "ignore", "copy_function", "ignore_dangling_symlinks",
                            "dirs_exist_ok" }, 2u, 0u, a))
                            return nullptr;
                        ref const copyFunction{ a[4] != nullptr ? new_reference(a[4]) : shutil_function("copy2") };
                        if (!copyFunction)
                            return nullptr;
                        return copy_tree(a[0], a[1], a[2] != nullptr && truth(a[2]) == 1, a[3], copyFunction.get(), a[5] != nullptr && truth(a[5]) == 1,
                            a[6] != nullptr && truth(a[6]) == 1);
                    } },
                { "ignore_patterns", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "ignore_patterns"))
                            return nullptr;
                        static builtin_entry const sIgnore{ "_ignore_patterns", [](std::vector<object*> const& aArgs2, object*) -> object*
                            {
                                // (self (the patterns), path, names)
                                if (aArgs2.size() != 3u)
                                    return error("TypeError", "_ignore_patterns() takes 2 positional arguments");
                                ref const result{ fn_set() };
                                for (auto p : as<tuple_object>(aArgs2[0])->items)
                                {
                                    ref const matched{ fnmatch_filter(aArgs2[2], p) };
                                    if (!matched)
                                        return nullptr;
                                    fn_set_update(result.get(), matched.get());
                                    if (failed())
                                        return nullptr;
                                }
                                return new_reference(result.get());
                            } };
                        ref const patterns{ make_tuple({}) };
                        for (auto p : aArgs)
                            as<tuple_object>(patterns.get())->items.push_back(new_reference(p));
                        return make_function("_ignore_patterns", call_builtin, const_cast<builtin_entry*>(&sIgnore), patterns.get());
                    } },
                { "rmtree", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("rmtree", aArgs, aKwargs, { "path", "ignore_errors", "onerror", "onexc", "dir_fd" }, 1u, 0u, a))
                            return nullptr;
                        if (aArgs.size() > 3u)
                            return error("TypeError", "rmtree() takes from 1 to 3 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        rmtree_handler handler;
                        handler.ignore = a[1] != nullptr && truth(a[1]) == 1;
                        handler.onerror = a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE ? a[2] : nullptr;
                        handler.onexc = a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE ? a[3] : nullptr;
                        if (a[4] != nullptr && kind_of(a[4]) != NEOS_PY_NONE)
                            return error("NotImplementedError", "dir_fd unavailable on this platform");
                        return rmtree(a[0], handler);
                    } },
                { "move", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("move", aArgs, aKwargs, { "src", "dst", "copy_function" }, 2u, 0u, a))
                            return nullptr;
                        ref const copyFunction{ a[2] != nullptr ? new_reference(a[2]) : shutil_function("copy2") };
                        return copyFunction ? move(a[0], a[1], copyFunction.get()) : nullptr;
                    } },
                { "which", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("which", aArgs, aKwargs, { "cmd", "mode", "path" }, 1u, 0u, a))
                            return nullptr;
                        long long mode = 1;     // (os.F_OK | os.X_OK)
                        if (a[1] != nullptr && !to_index(a[1], mode))
                            return nullptr;
                        return which(a[0], mode, a[2]);
                    } },
                { "disk_usage", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string path;
                        if (!parse_arguments("disk_usage", aArgs, aKwargs, { "path" }, 1u, 0u, a) || !path_text(a[0], path))
                            return nullptr;
                        std::error_code ec;
                        auto const space = std::filesystem::space(std::filesystem::path{ path }, ec);
                        if (ec)
                            return raise_os_error(ec.value(), a[0]);
                        auto const& t = structseq_type("shutil.usage", { "total", "used", "free" }, 3u);
                        return make_structseq(t, { make_int(static_cast<long long>(space.capacity)), make_int(static_cast<long long>(space.capacity - space.free)),
                            make_int(static_cast<long long>(space.available)) });
                    } },
                { "chown", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("chown", aArgs, aKwargs, { "path", "user", "group", "dir_fd", "follow_symlinks" }, 1u, 0u, a))
                            return nullptr;
                        if ((a[1] == nullptr || kind_of(a[1]) == NEOS_PY_NONE) && (a[2] == nullptr || kind_of(a[2]) == NEOS_PY_NONE))
                            return error("ValueError", "user and/or group must be set");
                        auto const id = [](object* aValue) -> object* { return aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE ? make_int(-1) : new_reference(aValue); };
                        ref const r{ os_call("chown", { new_reference(a[0]), id(a[1]), id(a[2]) }) };
                        return r ? new_reference(none()) : nullptr;
                    } },
                { "get_terminal_size", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get_terminal_size", aArgs, aKwargs, { "fallback" }, 0u, 0u, a))
                            return nullptr;
                        long long columns = 0, lines = 0;
                        auto const env = [](char const* aName) -> long long
                        {
                            ref const v{ os_call("getenv", { make_str(aName) }) };
                            long long n = 0;
                            if (v && kind_of(v.get()) == NEOS_PY_STR)
                                try { n = std::stoll(as<str_object>(v.get())->value); } catch (...) { n = 0; }
                            clear_error();
                            return n;
                        };
                        columns = env("COLUMNS");
                        lines = env("LINES");
                        if (columns <= 0 || lines <= 0)
                        {
                            ref const size{ os_call("get_terminal_size", { make_int(1) }) };
                            if (size)
                            {
                                ref const c{ getattr(size.get(), "columns") };
                                ref const l{ getattr(size.get(), "lines") };
                                long long x = 0, y = 0;
                                if (c && l && to_index(c.get(), x) && to_index(l.get(), y))
                                {
                                    if (columns <= 0) columns = x;
                                    if (lines <= 0) lines = y;
                                }
                            }
                            clear_error();
                        }
                        if (columns <= 0 || lines <= 0)
                        {
                            long long fallback[2] = { 80, 24 };
                            if (a[0] != nullptr)
                            {
                                std::vector<object*> items;
                                if (!collect(a[0], items))
                                    return nullptr;
                                for (std::size_t i = 0u; i < items.size() && i < 2u; ++i)
                                    if (!to_index(items[i], fallback[i]))
                                    {
                                        for (auto x : items)
                                            release(x);
                                        return nullptr;
                                    }
                                for (auto x : items)
                                    release(x);
                            }
                            if (columns <= 0) columns = fallback[0];
                            if (lines <= 0) lines = fallback[1];
                        }
                        auto const& t = structseq_type("os.terminal_size", { "columns", "lines" }, 2u);
                        return make_structseq(t, { make_int(columns), make_int(lines) });
                    } },
                { "get_archive_formats", [](std::vector<object*> const&, object*) -> object* { return make_list(); } },
                { "get_unpack_formats", [](std::vector<object*> const&, object*) -> object* { return make_list(); } },
                { "make_archive", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::string format = aArgs.size() > 1u ? text_of(aArgs[1]) : std::string{};
                        return error("ValueError", "unknown archive format '" + format + "'");
                    } },
                { "unpack_archive", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::string name = aArgs.empty() ? std::string{} : text_of(aArgs[0]);
                        raise_shutil("ReadError", { make_str("Unknown archive format '" + name + "'") });
                        return nullptr;
                    } }
            };

            bool shutil_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sShutilFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                for (auto name : { "Error", "SameFileError", "SpecialFileError", "ExecError", "ReadError", "RegistryError" })
                    aAttributes.emplace_back(name, shutil_error(name));
#if defined(_WIN32)
                aAttributes.emplace_back("COPY_BUFSIZE", make_int(1024 * 1024));
#else
                aAttributes.emplace_back("COPY_BUFSIZE", make_int(256 * 1024));
#endif
                return true;
            }

            // --- tempfile

            std::mt19937_64& random_engine()
            {
                static std::mt19937_64 sEngine{ std::random_device{}() };
                return sEngine;
            }

            // (_RandomNameSequence's next name)
            std::string random_name()
            {
                static char const sCharacters[] = "abcdefghijklmnopqrstuvwxyz0123456789_";
                std::uniform_int_distribution<int> pick{ 0, static_cast<int>(sizeof(sCharacters)) - 2 };
                std::string result;
                for (int i = 0; i < 8; ++i)
                    result += sCharacters[pick(random_engine())];
                return result;
            }

            std::string sTempDirectory;     ///< (gettempdir's: found when first needed)

            object* tempfile_module_object() { return fn_import("tempfile"); }

            // (gettempdir: the tempfile module's tempdir, or the first candidate in which a file can be made)
            bool temp_directory(std::string& aResult)
            {
                ref const module{ tempfile_module_object() };
                ref const set{ module ? getattr(module.get(), "tempdir") : nullptr };
                clear_error();
                if (set && kind_of(set.get()) != NEOS_PY_NONE)
                {
                    bool bytes = false;
                    return path_text(set.get(), aResult, &bytes);
                }
                if (sTempDirectory.empty())
                {
                    std::vector<std::string> candidates;
                    for (auto name : { "TMPDIR", "TEMP", "TMP" })
                    {
                        ref const v{ os_call("getenv", { make_str(name) }) };
                        if (v && kind_of(v.get()) == NEOS_PY_STR)
                            candidates.push_back(as<str_object>(v.get())->value);
                        clear_error();
                    }
#if defined(_WIN32)
                    for (auto d : { "~\\AppData\\Local\\Temp", "%SYSTEMROOT%\\Temp", "c:\\temp", "c:\\tmp", "\\temp", "\\tmp" })
                    {
                        ref const expanded{ os_call("path.expanduser", { make_str(d) }) };
                        ref const vars{ expanded ? os_call("path.expandvars", { new_reference(expanded.get()) }) : ref{} };
                        if (vars)
                            candidates.push_back(text_of(vars.get()));
                        clear_error();
                    }
#else
                    for (auto d : { "/tmp", "/var/tmp", "/usr/tmp" })
                        candidates.push_back(d);
#endif
                    ref const cwd{ os_call("getcwd", {}) };
                    candidates.push_back(cwd ? text_of(cwd.get()) : std::string{ "." });
                    clear_error();
                    for (auto& d : candidates)
                    {
                        if (d != ".")
                        {
                            ref const absolute{ os_call("path.abspath", { make_str(d) }) };
                            if (absolute)
                                d = text_of(absolute.get());
                            clear_error();
                        }
                        // (a file can be made and written in it)
                        for (int seq = 0; seq < 100; ++seq)
                        {
                            std::string file;
                            if (!path_join(d, random_name(), file))
                            {
                                clear_error();
                                break;
                            }
                            ref const fd{ os_call("open", { make_str(file), make_int(O_RDWR | O_CREAT | O_EXCL), make_int(0600) }) };
                            if (!fd)
                            {
                                ref const e{ take_exception() };
                                if (e && exception_is(e.get(), "FileExistsError"))
                                    continue;
                                break;
                            }
                            ref const written{ os_call("write", { new_reference(fd.get()), make_bytes("blat") }) };
                            ref const closed{ os_call("close", { new_reference(fd.get()) }) };
                            ref const removed{ os_call("unlink", { make_str(file) }) };
                            clear_error();
                            if (written)
                            {
                                sTempDirectory = d;
                                break;
                            }
                            break;
                        }
                        if (!sTempDirectory.empty())
                            break;
                    }
                    if (sTempDirectory.empty())
                    {
                        ref const cls{ exception_class("FileNotFoundError") };
                        ref const e{ invoke(cls.get(), { make_int(ENOENT), make_str("No usable temporary directory found in " + [&]()
                            {
                                std::vector<object*> items;
                                for (auto const& c : candidates)
                                    items.push_back(make_str(c));
                                ref const list{ make_list(std::move(items)) };
                                return repr_text(list.get());
                            }()) }) };
                        if (e)
                            raise_exception(e.get());
                        return false;
                    }
                    // (tempfile.tempdir is set)
                    if (module)
                    {
                        ref const value{ make_str(sTempDirectory) };
                        setattr(module.get(), "tempdir", value.get());
                        clear_error();
                    }
                }
                aResult = sTempDirectory;
                return true;
            }

            // (_sanitize_params: prefix, suffix, dir (and whether the result is bytes))
            struct temp_parameters
            {
                std::string prefix = "tmp";
                std::string suffix;
                std::string directory;
                bool bytes = false;
            };

            bool temp_parameters_of(object* aSuffix, object* aPrefix, object* aDirectory, temp_parameters& aResult)
            {
                int kinds = 0;  // (1: str, 2: bytes)
                for (auto v : { aPrefix, aSuffix, aDirectory })
                {
                    if (v == nullptr || kind_of(v) == NEOS_PY_NONE)
                        continue;
                    std::string text;
                    bool bytes = false;
                    if (!path_text(v, text, &bytes))
                        return false;
                    if ((kinds == 1 && bytes) || (kinds == 2 && !bytes))
                        return (error("TypeError", "Can't mix bytes and non-bytes in path components."), false);
                    kinds = bytes ? 2 : 1;
                }
                aResult.bytes = kinds == 2;
                if (aSuffix != nullptr && kind_of(aSuffix) != NEOS_PY_NONE && !path_text(aSuffix, aResult.suffix))
                    return false;
                if (aPrefix != nullptr && kind_of(aPrefix) != NEOS_PY_NONE && !path_text(aPrefix, aResult.prefix))
                    return false;
                if (aDirectory != nullptr && kind_of(aDirectory) != NEOS_PY_NONE)
                    return path_text(aDirectory, aResult.directory);
                return temp_directory(aResult.directory);
            }

            // (_mkstemp_inner: a new file's descriptor and path)
            bool make_temp_file(temp_parameters const& aParameters, long long& aDescriptor, std::string& aPath)
            {
                long long flags = O_RDWR | O_CREAT | O_EXCL;
#if defined(O_NOFOLLOW)
                flags |= O_NOFOLLOW;
#endif
#if defined(_WIN32)
                flags |= O_BINARY | O_NOINHERIT;
#endif
                for (int seq = 0; seq < 10000; ++seq)
                {
                    std::string file;
                    if (!path_join(aParameters.directory, aParameters.prefix + random_name() + aParameters.suffix, file))
                        return false;
                    ref const fd{ os_call("open", { make_str(file), make_int(flags), make_int(0600) }) };
                    if (!fd)
                    {
                        ref const e{ take_exception() };
                        if (e && exception_is(e.get(), "FileExistsError"))
                            continue;
#if defined(_WIN32)
                        // (a directory of the same name: tried again)
                        if (e && exception_is(e.get(), "PermissionError"))
                        {
                            ref const isDir{ os_call("path.isdir", { make_str(aParameters.directory) }) };
                            ref const access{ os_call("access", { make_str(aParameters.directory), make_int(2) }) };
                            clear_error();
                            if (isDir && truth(isDir.get()) == 1 && access && truth(access.get()) == 1)
                                continue;
                        }
#endif
                        if (e)
                            raise_exception(e.get());
                        return false;
                    }
                    if (!to_index(fd.get(), aDescriptor))
                        return false;
                    ref const absolute{ os_call("path.abspath", { make_str(file) }) };
                    if (!absolute)
                        return false;
                    aPath = text_of(absolute.get());
                    return true;
                }
                ref const cls{ exception_class("FileExistsError") };
                ref const e{ invoke(cls.get(), { make_int(EEXIST), make_str("No usable temporary file name found") }) };
                if (e)
                    raise_exception(e.get());
                return false;
            }

            object* temp_file_class();
            object* temp_directory_class();
            object* spooled_class();

            // (NamedTemporaryFile's arguments)
            std::vector<char const*> const sNamedNames = { "mode", "buffering", "encoding", "newline", "suffix", "prefix", "dir", "delete", "errors", "delete_on_close" };

            object* open_descriptor(long long aDescriptor, object* aMode, object* aBuffering, object* aEncoding, object* aNewline, object* aErrors)
            {
                ref const mode{ aMode != nullptr ? new_reference(aMode) : make_str("w+b") };
                ref const kw{ keywords({ { "mode", mode.get() }, { "buffering", aBuffering }, { "encoding", aEncoding }, { "newline", aNewline },
                    { "errors", aErrors } }) };
                ref const fd{ make_int(aDescriptor) };
                return b_open({ fd.get() }, kw.get());
            }

            object* named_temporary_file(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("NamedTemporaryFile", aArgs, aKwargs, sNamedNames, 0u, 0u, a))
                    return nullptr;
                temp_parameters p;
                if (!temp_parameters_of(a[4], a[5], a[6], p))
                    return nullptr;
                long long fd = -1;
                std::string path;
                if (!make_temp_file(p, fd, path))
                    return nullptr;
                ref const file{ open_descriptor(fd, a[0], a[1], a[2], a[3], a[8]) };
                if (!file)
                {
                    ref const e{ take_exception() };
                    ref const removed{ os_call("unlink", { make_str(path) }) };
                    ref const closed{ os_call("close", { make_int(fd) }) };
                    clear_error();
                    if (e)
                        raise_exception(e.get());
                    return nullptr;
                }
                ref const cls{ temp_file_class() };
                if (!cls)
                    return nullptr;
                ref const name{ path_value(path, p.bytes) };
                ref const del{ make_bool(a[7] == nullptr || truth(a[7]) == 1) };
                ref const delOnClose{ make_bool(a[9] == nullptr || truth(a[9]) == 1) };
                return invoke(cls.get(), { new_reference(file.get()), new_reference(name.get()), new_reference(del.get()), new_reference(delOnClose.get()) });
            }

            // (_TemporaryFileWrapper(file, name, delete=True, delete_on_close=True))
            builtin_entry const sTempFileMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_TemporaryFileWrapper", aArgs, aKwargs, { "file", "name", "delete", "delete_on_close" }, 2u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const del{ a[2] != nullptr ? new_reference(a[2]) : make_bool(true) };
                        ref const delOnClose{ a[3] != nullptr ? new_reference(a[3]) : make_bool(true) };
                        ref const closed{ make_bool(false) };
                        if (setattr(self, "file", a[0]) < 0 || setattr(self, "name", a[1]) < 0 || setattr(self, "delete", del.get()) < 0 ||
                            setattr(self, "_delete_on_close", delOnClose.get()) < 0 || setattr(self, "_closed", closed.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "__getattr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u || kind_of(aArgs[1]) != NEOS_PY_STR)
                            return error("TypeError", "__getattr__ expected 1 argument");
                        ref const file{ getattr(aArgs[0], "file") };
                        return file ? getattr(file.get(), as<str_object>(aArgs[1])->value.c_str()) : nullptr;
                    } },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const file{ getattr(aArgs[0], "file") };
                        ref const r{ file ? call_method_named(file.get(), "__enter__", {}) : nullptr };
                        return r ? new_reference(aArgs[0]) : nullptr;
                    } },
                { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const file{ getattr(aArgs[0], "file") };
                        if (!file)
                            return nullptr;
                        std::vector<object*> args;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            args.push_back(new_reference(aArgs[i]));
                        ref const r{ call_method_named(file.get(), "__exit__", std::move(args)) };
                        if (!r)
                            return nullptr;
                        // (the file is deleted (if delete is true): its closing has done it unless delete_on_close is false)
                        ref const del{ getattr(aArgs[0], "delete") };
                        ref const name{ getattr(aArgs[0], "name") };
                        ref const closedAlready{ getattr(aArgs[0], "_removed") };
                        clear_error();
                        if (del && name && truth(del.get()) == 1 && !closedAlready)
                        {
                            ref const removed{ os_call("unlink", { new_reference(name.get()) }) };
                            if (!removed)
                            {
                                ref const e{ take_exception() };
                                if (e && !exception_is(e.get(), "FileNotFoundError"))
                                {
                                    raise_exception(e.get());
                                    return nullptr;
                                }
                            }
                            ref const t{ make_bool(true) };
                            setattr(aArgs[0], "_removed", t.get());
                        }
                        return new_reference(r.get());
                    } },
                { "close", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const file{ getattr(self, "file") };
                        if (!file)
                            return nullptr;
                        ref const r{ call_method_named(file.get(), "close", {}) };
                        if (!r)
                            return nullptr;
                        ref const del{ getattr(self, "delete") };
                        ref const onClose{ getattr(self, "_delete_on_close") };
                        ref const name{ getattr(self, "name") };
                        ref const removedAlready{ getattr(self, "_removed") };
                        clear_error();
                        if (del && onClose && name && truth(del.get()) == 1 && truth(onClose.get()) == 1 && !removedAlready)
                        {
                            ref const removed{ os_call("unlink", { new_reference(name.get()) }) };
                            if (!removed)
                            {
                                ref const e{ take_exception() };
                                if (e && !exception_is(e.get(), "FileNotFoundError"))
                                {
                                    raise_exception(e.get());
                                    return nullptr;
                                }
                            }
                            ref const t{ make_bool(true) };
                            setattr(self, "_removed", t.get());
                        }
                        return new_reference(none());
                    } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const file{ getattr(aArgs[0], "file") };
                        return file ? iter(file.get()) : nullptr;
                    } }
            };

            object* sTempFileClass = nullptr;
            object* temp_file_class()
            {
                if (sTempFileClass == nullptr)
                    sTempFileClass = make_class_of("tempfile", "_TemporaryFileWrapper", {}, sTempFileMethods);
                return sTempFileClass != nullptr ? new_reference(sTempFileClass) : nullptr;
            }

            // (TemporaryDirectory(suffix=None, prefix=None, dir=None, ignore_cleanup_errors=False, *, delete=True))
            builtin_entry const sTempDirectoryMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("TemporaryDirectory", aArgs, aKwargs, { "suffix", "prefix", "dir", "ignore_cleanup_errors", "delete" }, 0u, 0u, a))
                            return nullptr;
                        ref const mkdtemp{ module_attribute("tempfile", "mkdtemp") };
                        ref const name{ mkdtemp ? invoke(mkdtemp.get(), { new_reference(a[0] != nullptr ? a[0] : none()), new_reference(a[1] != nullptr ? a[1] : none()),
                            new_reference(a[2] != nullptr ? a[2] : none()) }) : nullptr };
                        if (!name)
                            return nullptr;
                        ref const ignore{ make_bool(a[3] != nullptr && truth(a[3]) == 1) };
                        ref const del{ make_bool(a[4] == nullptr || truth(a[4]) == 1) };
                        if (setattr(aArgs[0], "name", name.get()) < 0 || setattr(aArgs[0], "_ignore_cleanup_errors", ignore.get()) < 0 ||
                            setattr(aArgs[0], "_delete", del.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const name{ getattr(aArgs[0], "name") };
                        ref const cls{ class_of(aArgs[0]) };
                        ref const className{ getattr(cls.get(), "__name__") };
                        if (!name || !className)
                            return nullptr;
                        return make_str("<" + text_of(className.get()) + " " + repr_text(name.get()) + ">");
                    } },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object* { return getattr(aArgs[0], "name"); } },
                { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const del{ getattr(aArgs[0], "_delete") };
                        if (!del)
                            return nullptr;
                        if (truth(del.get()) == 1)
                        {
                            ref const r{ call_method_named(aArgs[0], "cleanup", {}) };
                            if (!r)
                                return nullptr;
                        }
                        return new_reference(none());
                    } },
                { "cleanup", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const name{ getattr(aArgs[0], "name") };
                        ref const ignore{ getattr(aArgs[0], "_ignore_cleanup_errors") };
                        ref const done{ getattr(aArgs[0], "_cleaned") };
                        clear_error();
                        if (!name || done)
                            return new_reference(none());
                        ref const exists{ os_call("path.exists", { new_reference(name.get()) }) };
                        if (exists && truth(exists.get()) == 1)
                        {
                            rmtree_handler handler;
                            handler.ignore = ignore && truth(ignore.get()) == 1;
                            ref const r{ rmtree(name.get(), handler) };
                            if (!r)
                                return nullptr;
                        }
                        ref const t{ make_bool(true) };
                        setattr(aArgs[0], "_cleaned", t.get());
                        return new_reference(none());
                    } }
            };

            object* sTempDirectoryClass = nullptr;
            object* temp_directory_class()
            {
                if (sTempDirectoryClass == nullptr)
                {
                    sTempDirectoryClass = make_class_of("tempfile", "TemporaryDirectory", {}, sTempDirectoryMethods);
                }
                return sTempDirectoryClass != nullptr ? new_reference(sTempDirectoryClass) : nullptr;
            }

            // (SpooledTemporaryFile(max_size=0, mode='w+b', buffering=-1, encoding=None, newline=None, suffix=None, prefix=None, dir=None, *, errors=None):
            // an io.BytesIO or io.StringIO until it is larger than max_size (or fileno() is called), then a TemporaryFile)
            builtin_entry const sSpooledMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("SpooledTemporaryFile", aArgs, aKwargs, { "max_size", "mode", "buffering", "encoding", "newline", "suffix", "prefix",
                            "dir", "errors" }, 0u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const mode{ a[1] != nullptr ? new_reference(a[1]) : make_str("w+b") };
                        bool const binary = text_of(mode.get()).find('b') != std::string::npos;
                        ref const file{ binary ? invoke(ref{ module_attribute("io", "BytesIO") }.get(), {}) :
                            invoke(ref{ module_attribute("io", "StringIO") }.get(), {}, ref{ keywords({ { "newline", ref{ make_str("\n") }.get() } }) }.get()) };
                        if (!file)
                            return nullptr;
                        ref const maxSize{ a[0] != nullptr ? new_reference(a[0]) : make_int(0) };
                        ref const rolled{ make_bool(false) };
                        ref const args{ make_dict() };
                        for (std::size_t i = 1u; i < a.size(); ++i)
                            if (a[i] != nullptr)
                            {
                                ref const key{ make_str(std::vector<char const*>{ "max_size", "mode", "buffering", "encoding", "newline", "suffix", "prefix", "dir", "errors" }[i]) };
                                dict_set(args.get(), key.get(), a[i]);
                            }
                        if (!a[1])
                            dict_set(args.get(), ref{ make_str("mode") }.get(), mode.get());
                        if (setattr(self, "_file", file.get()) < 0 || setattr(self, "_max_size", maxSize.get()) < 0 || setattr(self, "_rolled", rolled.get()) < 0 ||
                            setattr(self, "_TemporaryFileArgs", args.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "rollover", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const rolled{ getattr(self, "_rolled") };
                        if (!rolled)
                            return nullptr;
                        if (truth(rolled.get()) == 1)
                            return new_reference(none());
                        ref const file{ getattr(self, "_file") };
                        ref const args{ getattr(self, "_TemporaryFileArgs") };
                        ref const temporaryFile{ module_attribute("tempfile", "TemporaryFile") };
                        if (!file || !args || !temporaryFile)
                            return nullptr;
                        ref const newFile{ invoke(temporaryFile.get(), {}, args.get()) };
                        if (!newFile)
                            return nullptr;
                        ref const value{ call_method_named(file.get(), "getvalue", {}) };
                        ref const written{ value ? call_method_named(newFile.get(), "write", { new_reference(value.get()) }) : nullptr };
                        ref const position{ written ? call_method_named(file.get(), "tell", {}) : nullptr };
                        ref const sought{ position ? call_method_named(newFile.get(), "seek", { new_reference(position.get()), make_int(0) }) : nullptr };
                        if (!sought)
                            return nullptr;
                        ref const t{ make_bool(true) };
                        if (setattr(self, "_file", newFile.get()) < 0 || setattr(self, "_rolled", t.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "write", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "write() takes exactly one argument");
                        auto const self = aArgs[0];
                        ref const file{ getattr(self, "_file") };
                        ref const r{ file ? call_method_named(file.get(), "write", { new_reference(aArgs[1]) }) : nullptr };
                        if (!r)
                            return nullptr;
                        // (_check: rolled over when it is larger than its maximum size)
                        ref const maxSize{ getattr(self, "_max_size") };
                        ref const rolled{ getattr(self, "_rolled") };
                        long long m = 0;
                        if (!maxSize || !rolled || !to_index(maxSize.get(), m))
                            return nullptr;
                        if (truth(rolled.get()) == 0 && m > 0)
                        {
                            ref const position{ call_method_named(file.get(), "tell", {}) };
                            long long p = 0;
                            if (!position || !to_index(position.get(), p))
                                return nullptr;
                            if (p > m)
                            {
                                ref const rolledOver{ call_method_named(self, "rollover", {}) };
                                if (!rolledOver)
                                    return nullptr;
                            }
                        }
                        return new_reference(r.get());
                    } },
                { "fileno", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const r{ call_method_named(aArgs[0], "rollover", {}) };
                        ref const file{ r ? getattr(aArgs[0], "_file") : nullptr };
                        return file ? call_method_named(file.get(), "fileno", {}) : nullptr;
                    } },
                { "__getattr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u || kind_of(aArgs[1]) != NEOS_PY_STR || as<str_object>(aArgs[1])->value.starts_with("_"))
                            return error("AttributeError", "'SpooledTemporaryFile' object has no attribute '" + (aArgs.size() == 2u ? text_of(aArgs[1]) : std::string{}) + "'");
                        ref const file{ getattr(aArgs[0], "_file") };
                        return file ? getattr(file.get(), as<str_object>(aArgs[1])->value.c_str()) : nullptr;
                    } },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const file{ getattr(aArgs[0], "_file") };
                        ref const r{ file ? call_method_named(file.get(), "close", {}) : nullptr };
                        return r ? new_reference(none()) : nullptr;
                    } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const file{ getattr(aArgs[0], "_file") };
                        return file ? iter(file.get()) : nullptr;
                    } }
            };

            object* sSpooledClass = nullptr;
            object* spooled_class()
            {
                if (sSpooledClass == nullptr)
                    sSpooledClass = make_class_of("tempfile", "SpooledTemporaryFile", {}, sSpooledMethods);
                return sSpooledClass != nullptr ? new_reference(sSpooledClass) : nullptr;
            }

            builtin_entry const sTempfileFunctions[] =
            {
                { "gettempdir", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::string d;
                        return temp_directory(d) ? make_str(d) : nullptr;
                    } },
                { "gettempdirb", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::string d;
                        return temp_directory(d) ? make_bytes(d) : nullptr;
                    } },
                { "gettempprefix", [](std::vector<object*> const&, object*) -> object* { return make_str("tmp"); } },
                { "gettempprefixb", [](std::vector<object*> const&, object*) -> object* { return make_bytes("tmp"); } },
                { "mkstemp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("mkstemp", aArgs, aKwargs, { "suffix", "prefix", "dir", "text" }, 0u, 0u, a))
                            return nullptr;
                        temp_parameters p;
                        if (!temp_parameters_of(a[0], a[1], a[2], p))
                            return nullptr;
                        long long fd = -1;
                        std::string path;
                        if (!make_temp_file(p, fd, path))
                            return nullptr;
                        return make_tuple({ make_int(fd), path_value(path, p.bytes) });
                    } },
                { "mkdtemp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("mkdtemp", aArgs, aKwargs, { "suffix", "prefix", "dir" }, 0u, 0u, a))
                            return nullptr;
                        temp_parameters p;
                        if (!temp_parameters_of(a[0], a[1], a[2], p))
                            return nullptr;
                        for (int seq = 0; seq < 10000; ++seq)
                        {
                            std::string file;
                            if (!path_join(p.directory, p.prefix + random_name() + p.suffix, file))
                                return nullptr;
                            ref const made{ os_call("mkdir", { make_str(file), make_int(0700) }) };
                            if (!made)
                            {
                                ref const e{ take_exception() };
                                if (e && exception_is(e.get(), "FileExistsError"))
                                    continue;
                                if (e)
                                    raise_exception(e.get());
                                return nullptr;
                            }
                            ref const absolute{ os_call("path.abspath", { make_str(file) }) };
                            if (!absolute)
                                return nullptr;
                            return path_value(text_of(absolute.get()), p.bytes);
                        }
                        ref const cls{ exception_class("FileExistsError") };
                        ref const e{ invoke(cls.get(), { make_int(EEXIST), make_str("No usable temporary directory name found") }) };
                        if (e)
                            raise_exception(e.get());
                        return nullptr;
                    } },
                { "mktemp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("mktemp", aArgs, aKwargs, { "suffix", "prefix", "dir" }, 0u, 0u, a))
                            return nullptr;
                        temp_parameters p;
                        p.suffix = "";
                        if (a[0] != nullptr && !path_text(a[0], p.suffix))
                            return nullptr;
                        if (a[1] != nullptr && !path_text(a[1], p.prefix))
                            return nullptr;
                        if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE ? !path_text(a[2], p.directory) : !temp_directory(p.directory))
                            return nullptr;
                        for (int seq = 0; seq < 10000; ++seq)
                        {
                            std::string file;
                            if (!path_join(p.directory, p.prefix + random_name() + p.suffix, file))
                                return nullptr;
                            ref const exists{ os_call("path.lexists", { make_str(file) }) };
                            if (!exists)
                                return nullptr;
                            if (truth(exists.get()) == 0)
                                return make_str(file);
                        }
                        ref const cls{ exception_class("FileExistsError") };
                        ref const e{ invoke(cls.get(), { make_int(EEXIST), make_str("No usable temporary filename found") }) };
                        if (e)
                            raise_exception(e.get());
                        return nullptr;
                    } },
                { "NamedTemporaryFile", named_temporary_file },
                { "TemporaryFile", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
#if defined(_WIN32)
                        return named_temporary_file(aArgs, aKwargs);
#else
                        // (a file without a name: unlinked once it is made)
                        std::vector<object*> a;
                        if (!parse_arguments("TemporaryFile", aArgs, aKwargs, { "mode", "buffering", "encoding", "newline", "suffix", "prefix", "dir", "errors" }, 0u, 0u, a))
                            return nullptr;
                        temp_parameters p;
                        if (!temp_parameters_of(a[4], a[5], a[6], p))
                            return nullptr;
                        long long fd = -1;
                        std::string path;
                        if (!make_temp_file(p, fd, path))
                            return nullptr;
                        ref const removed{ os_call("unlink", { make_str(path) }) };
                        if (!removed)
                        {
                            ref const closed{ os_call("close", { make_int(fd) }) };
                            return nullptr;
                        }
                        ref file{ open_descriptor(fd, a[0], a[1], a[2], a[3], a[7]) };
                        if (!file)
                        {
                            ref const e{ take_exception() };
                            ref const closed{ os_call("close", { make_int(fd) }) };
                            clear_error();
                            if (e)
                                raise_exception(e.get());
                            return nullptr;
                        }
                        // (its name is its descriptor)
                        return file.release_ownership();
#endif
                    } }
            };

            bool tempfile_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sTempfileFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("TemporaryDirectory", temp_directory_class());
                aAttributes.emplace_back("SpooledTemporaryFile", spooled_class());
                aAttributes.emplace_back("_TemporaryFileWrapper", temp_file_class());
                aAttributes.emplace_back("TMP_MAX", make_int(10000));
                aAttributes.emplace_back("template", make_str("tmp"));
                aAttributes.emplace_back("tempdir", new_reference(none()));
                aAttributes.emplace_back("__all__", make_list({ make_str("NamedTemporaryFile"), make_str("TemporaryFile"), make_str("SpooledTemporaryFile"),
                    make_str("TemporaryDirectory"), make_str("mkstemp"), make_str("mkdtemp"), make_str("mktemp"), make_str("TMP_MAX"), make_str("gettempprefix"),
                    make_str("tempdir"), make_str("gettempdir"), make_str("gettempprefixb"), make_str("gettempdirb") }));
                return !failed();
            }

            inline bool const sFnmatch = register_module("fnmatch", fnmatch_module);
            inline bool const sGlob = register_module("glob", glob_module);
            inline bool const sShutil = register_module("shutil", shutil_module);
            inline bool const sTempfile = register_module("tempfile", tempfile_module);
        }
    }
}
