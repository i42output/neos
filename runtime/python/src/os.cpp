/*
  os.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/posixpath.py, Lib/ntpath.py, Lib/genericpath.py and
  Modules/errnomodule.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the
  Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++
  for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules os, os.path (posixpath and ntpath: CPython 3.13's algorithms) and stat

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <set>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#endif
#include <fcntl.h>
#include "objects.hpp"

#if defined(_WIN32)
extern "C" char** _environ;
#else
extern "C" char** environ;
#endif

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // --- paths (aWindows: ntpath's rules, else posixpath's)

            struct path_rules
            {
                bool windows;
                char sep() const { return windows ? '\\' : '/'; }
                bool is_sep(char c) const { return c == '/' || (windows && c == '\\'); }
                std::string seps() const { return windows ? "\\/" : "/"; }
            };

            struct root_split { std::string drive; std::string root; std::string tail; };

            root_split splitroot(path_rules const& r, std::string const& p)
            {
                if (!r.windows)
                {
                    // (posixpath.splitroot)
                    if (p.empty() || p[0] != '/')
                        return { "", "", p };
                    if (p.size() < 2u || p[1] != '/' || (p.size() >= 3u && p[2] == '/'))
                        return { "", "/", p.substr(1u) };
                    return { "", "//", p.substr(2u) };
                }
                std::string normp = p;
                std::replace(normp.begin(), normp.end(), '/', '\\');
                if (!normp.empty() && normp[0] == '\\')
                {
                    if (normp.size() > 1u && normp[1] == '\\')
                    {
                        std::string upper = normp.substr(0u, 8u);
                        for (auto& c : upper)
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        std::size_t const start = upper == "\\\\?\\UNC\\" ? 8u : 2u;
                        auto const index = normp.find('\\', start);
                        if (index == std::string::npos)
                            return { p, "", "" };
                        auto const index2 = normp.find('\\', index + 1u);
                        if (index2 == std::string::npos)
                            return { p, "", "" };
                        return { p.substr(0u, index2), p.substr(index2, 1u), p.substr(index2 + 1u) };
                    }
                    return { "", p.substr(0u, 1u), p.substr(1u) };
                }
                if (normp.size() > 1u && normp[1] == ':')
                {
                    if (normp.size() > 2u && normp[2] == '\\')
                        return { p.substr(0u, 2u), p.substr(2u, 1u), p.substr(3u) };
                    return { p.substr(0u, 2u), "", p.substr(2u) };
                }
                return { "", "", p };
            }

            bool isabs(path_rules const& r, std::string const& s)
            {
                if (!r.windows)
                    return !s.empty() && s[0] == '/';
                std::string t = s.substr(0u, 3u);
                std::replace(t.begin(), t.end(), '/', '\\');
                return (t.size() >= 3u && t[1] == ':' && t[2] == '\\') || t.starts_with("\\\\");
            }

            std::string lower(std::string s)
            {
                for (auto& c : s)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            }

            std::string join(path_rules const& r, std::string const& aPath, std::vector<std::string> const& aPaths)
            {
                if (!r.windows)
                {
                    std::string path = aPath;
                    for (auto const& b : aPaths)
                    {
                        if (b.starts_with("/") || path.empty())
                            path = b;
                        else if (path.ends_with("/"))
                            path += b;
                        else
                            path += "/" + b;
                    }
                    return path;
                }
                auto result = splitroot(r, aPath);
                for (auto const& p : aPaths)
                {
                    auto const s = splitroot(r, p);
                    if (!s.root.empty())
                    {
                        if (!s.drive.empty() || result.drive.empty())
                            result.drive = s.drive;
                        result.root = s.root;
                        result.tail = s.tail;
                        continue;
                    }
                    else if (!s.drive.empty() && s.drive != result.drive)
                    {
                        if (lower(s.drive) != lower(result.drive))
                        {
                            result = s;
                            continue;
                        }
                        result.drive = s.drive;
                    }
                    if (!result.tail.empty() && !r.is_sep(result.tail.back()))
                        result.tail += '\\';
                    result.tail += s.tail;
                }
                if (!result.tail.empty() && result.root.empty() && !result.drive.empty() && std::string{ ":\\/" }.find(result.drive.back()) == std::string::npos)
                    return result.drive + "\\" + result.tail;
                return result.drive + result.root + result.tail;
            }

            std::pair<std::string, std::string> split(path_rules const& r, std::string const& p)
            {
                if (!r.windows)
                {
                    auto const i = p.rfind('/') == std::string::npos ? 0u : p.rfind('/') + 1u;
                    std::string head = p.substr(0u, i);
                    std::string const tail = p.substr(i);
                    if (!head.empty() && head.find_first_not_of('/') != std::string::npos)
                        head.erase(head.find_last_not_of('/') + 1u);
                    return { head, tail };
                }
                auto const s = splitroot(r, p);
                auto i = s.tail.size();
                while (i > 0u && !r.is_sep(s.tail[i - 1u]))
                    --i;
                std::string head = s.tail.substr(0u, i);
                std::string const tail = s.tail.substr(i);
                auto const last = head.find_last_not_of("\\/");
                head = last == std::string::npos ? std::string{} : head.substr(0u, last + 1u);
                return { s.drive + s.root + head, tail };
            }

            std::pair<std::string, std::string> splitext(path_rules const& r, std::string const& p)
            {
                // (genericpath._splitext)
                std::size_t sepIndex = std::string::npos;
                for (std::size_t i = p.size(); i > 0u; --i)
                    if (r.is_sep(p[i - 1u]))
                    {
                        sepIndex = i - 1u;
                        break;
                    }
                auto const dotIndex = p.rfind('.');
                long long const s = sepIndex == std::string::npos ? -1 : static_cast<long long>(sepIndex);
                if (dotIndex != std::string::npos && static_cast<long long>(dotIndex) > s)
                {
                    for (auto f = static_cast<std::size_t>(s + 1); f < dotIndex; ++f)
                        if (p[f] != '.')
                            return { p.substr(0u, dotIndex), p.substr(dotIndex) };
                }
                return { p, "" };
            }

            std::vector<std::string> split_on(std::string const& aText, char aSeparator)
            {
                std::vector<std::string> parts;
                std::size_t start = 0u;
                for (;;)
                {
                    auto const end = aText.find(aSeparator, start);
                    parts.push_back(aText.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    if (end == std::string::npos)
                        return parts;
                    start = end + 1u;
                }
            }

            std::string normpath(path_rules const& r, std::string const& aPath)
            {
                if (!r.windows)
                {
                    if (aPath.empty())
                        return ".";
                    int initial = aPath.starts_with("/") ? 1 : 0;
                    if (initial != 0 && aPath.starts_with("//") && !aPath.starts_with("///"))
                        initial = 2;
                    std::vector<std::string> comps;
                    for (auto const& comp : split_on(aPath, '/'))
                    {
                        if (comp.empty() || comp == ".")
                            continue;
                        if (comp != ".." || (initial == 0 && comps.empty()) || (!comps.empty() && comps.back() == ".."))
                            comps.push_back(comp);
                        else if (!comps.empty())
                            comps.pop_back();
                    }
                    std::string path;
                    for (std::size_t i = 0u; i < comps.size(); ++i)
                        path += (i != 0u ? "/" : "") + comps[i];
                    path = std::string(static_cast<std::size_t>(initial), '/') + path;
                    return path.empty() ? "." : path;
                }
                std::string path = aPath;
                std::replace(path.begin(), path.end(), '/', '\\');
                auto const s = splitroot(r, path);
                auto const prefix = s.drive + s.root;
                auto comps = split_on(s.tail, '\\');
                std::size_t i = 0u;
                while (i < comps.size())
                {
                    if (comps[i].empty() || comps[i] == ".")
                        comps.erase(comps.begin() + static_cast<std::ptrdiff_t>(i));
                    else if (comps[i] == "..")
                    {
                        if (i > 0u && comps[i - 1u] != "..")
                        {
                            comps.erase(comps.begin() + static_cast<std::ptrdiff_t>(i - 1u), comps.begin() + static_cast<std::ptrdiff_t>(i + 1u));
                            --i;
                        }
                        else if (i == 0u && !s.root.empty())
                            comps.erase(comps.begin());
                        else
                            ++i;
                    }
                    else
                        ++i;
                }
                if (prefix.empty() && comps.empty())
                    comps.push_back(".");
                std::string result = prefix;
                for (std::size_t k = 0u; k < comps.size(); ++k)
                    result += (k != 0u ? "\\" : "") + comps[k];
                return result;
            }

            std::string current_directory()
            {
                std::error_code ec;
                auto const p = std::filesystem::current_path(ec);
                return ec ? std::string{} : p.string();
            }

            std::string abspath(path_rules const& r, std::string const& aPath)
            {
                std::string path = aPath;
                if (!isabs(r, path))
                    path = join(r, current_directory(), { path });
                return normpath(r, path);
            }

            // (the environment: os.environ)
            object* environment()
            {
                static object* sEnviron = nullptr;
                if (sEnviron == nullptr)
                {
                    sEnviron = make_dict();
#if defined(_WIN32)
                    char** env = _environ;
#else
                    char** env = environ;
#endif
                    for (; env != nullptr && *env != nullptr; ++env)
                    {
                        std::string_view const entry{ *env };
                        auto const equals = entry.find('=', entry.empty() || entry[0] != '=' ? 0u : 1u);
                        if (equals == std::string_view::npos)
                            continue;
                        ref const key{ make_str(std::string{ entry.substr(0u, equals) }) };
                        ref const value{ make_str(std::string{ entry.substr(equals + 1u) }) };
                        dict_set(sEnviron, key.get(), value.get());
                    }
                }
                retain(sEnviron);
                return sEnviron;
            }

            std::optional<std::string> environment_value(std::string const& aName)
            {
                ref const env{ environment() };
                ref const key{ make_str(aName) };
                auto const position = dict_find(env.get(), key.get());
                if (position < 0)
                    return {};
                std::string value;
                str_of(as<dict_object>(env.get())->items[static_cast<std::size_t>(position)].second, value);
                return value;
            }

            std::string expanduser(path_rules const& r, std::string const& aPath)
            {
                if (!aPath.starts_with("~"))
                    return aPath;
                std::size_t i = 1u;
                while (i < aPath.size() && !r.is_sep(aPath[i]))
                    ++i;
                std::string userhome;
                if (r.windows)
                {
                    if (auto const home = environment_value("USERPROFILE"))
                        userhome = *home;
                    else if (auto const homepath = environment_value("HOMEPATH"))
                        userhome = join(r, environment_value("HOMEDRIVE").value_or(""), { *homepath });
                    else
                        return aPath;
                    if (i != 1u)
                    {
                        auto const target = aPath.substr(1u, i - 1u);
                        auto const current = environment_value("USERNAME");
                        if (!current || target != *current)
                        {
                            if (current && split(r, userhome).second != *current)
                                return aPath;
                            userhome = join(r, split(r, userhome).first, { target });
                        }
                    }
                    return userhome + aPath.substr(i);
                }
                if (i != 1u)
                    return aPath;
                auto const home = environment_value("HOME");
                if (!home)
                    return aPath;
                userhome = *home;
                while (!userhome.empty() && userhome.back() == '/')
                    userhome.pop_back();
                auto const result = userhome + aPath.substr(i);
                return result.empty() ? "/" : result;
            }

            std::string expandvars(path_rules const& r, std::string const& aPath)
            {
                auto const word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
                std::string result;
                if (!r.windows)
                {
                    if (aPath.find('$') == std::string::npos)
                        return aPath;
                    for (std::size_t i = 0u; i < aPath.size();)
                    {
                        if (aPath[i] != '$' || i + 1u >= aPath.size())
                        {
                            result += aPath[i++];
                            continue;
                        }
                        // ($name or ${name})
                        std::size_t end = i + 1u;
                        std::string name;
                        bool braced = aPath[end] == '{';
                        if (braced)
                        {
                            auto const close = aPath.find('}', end);
                            if (close == std::string::npos)
                            {
                                result += aPath.substr(i);
                                break;
                            }
                            name = aPath.substr(end + 1u, close - end - 1u);
                            end = close + 1u;
                        }
                        else
                        {
                            while (end < aPath.size() && word(aPath[end]))
                                ++end;
                            name = aPath.substr(i + 1u, end - i - 1u);
                            if (name.empty())
                            {
                                result += aPath[i++];
                                continue;
                            }
                        }
                        if (auto const value = environment_value(name))
                            result += *value;
                        else
                            result += aPath.substr(i, end - i);
                        i = end;
                    }
                    return result;
                }
                // (ntpath: %name%, $name and ${name}; '' quotes)
                if (aPath.find('$') == std::string::npos && aPath.find('%') == std::string::npos)
                    return aPath;
                std::size_t index = 0u;
                auto const n = aPath.size();
                while (index < n)
                {
                    char const c = aPath[index];
                    if (c == '\'')
                    {
                        auto const close = aPath.find('\'', index + 1u);
                        if (close == std::string::npos)
                        {
                            result += aPath.substr(index);
                            break;
                        }
                        result += aPath.substr(index, close - index + 1u);
                        index = close;
                    }
                    else if (c == '%')
                    {
                        if (index + 1u < n && aPath[index + 1u] == '%')
                        {
                            result += c;
                            ++index;
                        }
                        else
                        {
                            auto const close = aPath.find('%', index + 1u);
                            if (close == std::string::npos)
                            {
                                result += aPath.substr(index);
                                break;
                            }
                            auto const name = aPath.substr(index + 1u, close - index - 1u);
                            auto const value = environment_value(name);
                            result += value ? *value : "%" + name + "%";
                            index = close;
                        }
                    }
                    else if (c == '$')
                    {
                        if (index + 1u < n && aPath[index + 1u] == '$')
                        {
                            result += c;
                            ++index;
                        }
                        else if (index + 1u < n && aPath[index + 1u] == '{')
                        {
                            auto const close = aPath.find('}', index + 2u);
                            if (close == std::string::npos)
                            {
                                result += aPath.substr(index);
                                break;
                            }
                            auto const name = aPath.substr(index + 2u, close - index - 2u);
                            auto const value = environment_value(name);
                            result += value ? *value : "${" + name + "}";
                            index = close;
                        }
                        else
                        {
                            auto end = index + 1u;
                            while (end < n && word(aPath[end]))
                                ++end;
                            auto const name = aPath.substr(index + 1u, end - index - 1u);
                            auto const value = name.empty() ? std::optional<std::string>{} : environment_value(name);
                            result += value ? *value : "$" + name;
                            index = end - 1u;
                        }
                    }
                    else
                        result += c;
                    ++index;
                }
                return result;
            }

            // --- stat

            struct stat_values
            {
                long long mode = 0, ino = 0, dev = 0, nlink = 0, uid = 0, gid = 0, size = 0;
                double atime = 0.0, mtime = 0.0, ctime = 0.0;
                long long atime_ns = 0, mtime_ns = 0, ctime_ns = 0;
            };

            bool do_stat(std::string const& aPath, bool aFollow, stat_values& aResult, int& aErrno)
            {
#if defined(_WIN32)
                (void)aFollow;
                struct _stat64 st{};
                if (::_stat64(aPath.c_str(), &st) != 0)
                {
                    aErrno = errno;
                    return false;
                }
                aResult.atime_ns = static_cast<long long>(st.st_atime) * 1000000000ll;
                aResult.mtime_ns = static_cast<long long>(st.st_mtime) * 1000000000ll;
                aResult.ctime_ns = static_cast<long long>(st.st_ctime) * 1000000000ll;
#else
                struct stat st{};
                if ((aFollow ? ::stat(aPath.c_str(), &st) : ::lstat(aPath.c_str(), &st)) != 0)
                {
                    aErrno = errno;
                    return false;
                }
#if defined(__APPLE__)
                aResult.atime_ns = static_cast<long long>(st.st_atimespec.tv_sec) * 1000000000ll + st.st_atimespec.tv_nsec;
                aResult.mtime_ns = static_cast<long long>(st.st_mtimespec.tv_sec) * 1000000000ll + st.st_mtimespec.tv_nsec;
                aResult.ctime_ns = static_cast<long long>(st.st_ctimespec.tv_sec) * 1000000000ll + st.st_ctimespec.tv_nsec;
#else
                aResult.atime_ns = static_cast<long long>(st.st_atim.tv_sec) * 1000000000ll + st.st_atim.tv_nsec;
                aResult.mtime_ns = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000ll + st.st_mtim.tv_nsec;
                aResult.ctime_ns = static_cast<long long>(st.st_ctim.tv_sec) * 1000000000ll + st.st_ctim.tv_nsec;
#endif
#endif
                aResult.mode = st.st_mode;
                aResult.ino = static_cast<long long>(st.st_ino);
                aResult.dev = static_cast<long long>(st.st_dev);
                aResult.nlink = static_cast<long long>(st.st_nlink);
                aResult.uid = static_cast<long long>(st.st_uid);
                aResult.gid = static_cast<long long>(st.st_gid);
                aResult.size = static_cast<long long>(st.st_size);
                aResult.atime = static_cast<double>(aResult.atime_ns) / 1e9;
                aResult.mtime = static_cast<double>(aResult.mtime_ns) / 1e9;
                aResult.ctime = static_cast<double>(aResult.ctime_ns) / 1e9;
                return true;
            }

            object* stat_result(stat_values const& s)
            {
                // (items 7 to 9 are the integer times, which repr shows as st_atime etc.; the attributes are the floats (the later fields))
                auto const& t = structseq_type("os.stat_result", { "st_mode", "st_ino", "st_dev", "st_nlink", "st_uid", "st_gid", "st_size", 
                    "st_atime", "st_mtime", "st_ctime", "st_atime", "st_mtime", "st_ctime", "st_atime_ns", "st_mtime_ns", "st_ctime_ns" }, 10u);
                return make_structseq(t, { make_int(s.mode), make_int(s.ino), make_int(s.dev), make_int(s.nlink), make_int(s.uid), make_int(s.gid),
                    make_int(s.size), make_int(s.atime_ns / 1000000000ll), make_int(s.mtime_ns / 1000000000ll), make_int(s.ctime_ns / 1000000000ll),
                    make_float(s.atime), make_float(s.mtime), make_float(s.ctime), make_int(s.atime_ns), make_int(s.mtime_ns), make_int(s.ctime_ns) });
            }

            constexpr long long kIFMT = 0170000, kIFDIR = 0040000, kIFREG = 0100000, kIFLNK = 0120000;

            // (a path argument: str, bytes or os.PathLike)
            bool path_argument(object* aValue, std::string& aResult, char const* aFunction, char const* aName = "path")
            {
                if (kind_of(aValue) == NEOS_PY_STR)
                {
                    aResult = as<str_object>(aValue)->value;
                    return true;
                }
                std::string_view bytes;
                if (bytes_like(aValue, bytes))
                {
                    aResult = std::string{ bytes };
                    return true;
                }
                ref const fspath{ getattr(aValue, "__fspath__") };
                if (fspath)
                {
                    ref const empty{ make_tuple() };
                    ref const p{ call(fspath.get(), empty.get(), nullptr) };
                    return p && path_argument(p.get(), aResult, aFunction, aName);
                }
                clear_error();
                error("TypeError", std::string{ aFunction } + ": " + aName + " should be string, bytes or os.PathLike, not " + type_name(aValue));
                return false;
            }

            bool exists(std::string const& aPath, bool aFollow = true)
            {
                stat_values s;
                int e = 0;
                return do_stat(aPath, aFollow, s, e);
            }

            bool is_type(std::string const& aPath, long long aType, bool aFollow = true)
            {
                stat_values s;
                int e = 0;
                return do_stat(aPath, aFollow, s, e) && (s.mode & kIFMT) == aType;
            }

            // --- os.path functions

            path_rules const& rules_of(void* aData) { return *static_cast<path_rules const*>(aData); }

            struct path_function
            {
                char const* name;
                object* (*function)(path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs);
            };

            object* one_path(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, std::string& aPath)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "p" }, 1u, 1u, a))
                    return nullptr;
                if (!path_argument(a[0], aPath, aName, "p"))
                    return nullptr;
                return none();
            }

            // (a str result, or bytes for a bytes argument)
            object* path_result(std::vector<object*> const& aArgs, std::string aValue)
            {
                std::string_view b;
                if (!aArgs.empty() && bytes_like(aArgs[0], b))
                    return make_bytes(std::move(aValue));
                return make_str(std::move(aValue));
            }

            path_function const sPathFunctions[] =
            {
                { "join", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aKwargs != nullptr)
                            return error("TypeError", "join() takes no keyword arguments");
                        if (aArgs.empty())
                            return error("TypeError", "join() missing 1 required positional argument: 'a'");
                        std::string first;
                        if (!path_argument(aArgs[0], first, "join", "a"))
                        {
                            clear_error();
                            return error("TypeError", std::string{ "expected str, bytes or os.PathLike object, not " } + type_name(aArgs[0]));
                        }
                        std::vector<std::string> rest;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                        {
                            std::string p;
                            if (!path_argument(aArgs[i], p, "join"))
                            {
                                clear_error();
                                return error("TypeError", std::string{ "join() argument must be str, bytes, or os.PathLike object, not '" } + type_name(aArgs[i]) + "'");
                            }
                            rest.push_back(p);
                        }
                        return path_result(aArgs, join(r, first, rest));
                    } },
                { "split", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "split", p))
                            return nullptr;
                        auto const s = split(r, p);
                        return make_tuple({ path_result(aArgs, s.first), path_result(aArgs, s.second) });
                    } },
                { "splitext", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "splitext", p))
                            return nullptr;
                        auto const s = splitext(r, p);
                        return make_tuple({ path_result(aArgs, s.first), path_result(aArgs, s.second) });
                    } },
                { "splitdrive", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "splitdrive", p))
                            return nullptr;
                        auto const s = splitroot(r, p);
                        return make_tuple({ path_result(aArgs, s.drive), path_result(aArgs, s.root + s.tail) });
                    } },
                { "splitroot", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "splitroot", p))
                            return nullptr;
                        auto const s = splitroot(r, p);
                        return make_tuple({ path_result(aArgs, s.drive), path_result(aArgs, s.root), path_result(aArgs, s.tail) });
                    } },
                { "basename", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "basename", p))
                            return nullptr;
                        return path_result(aArgs, split(r, p).second);
                    } },
                { "dirname", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "dirname", p))
                            return nullptr;
                        if (!r.windows)
                        {
                            auto const i = p.rfind('/') == std::string::npos ? 0u : p.rfind('/') + 1u;
                            std::string head = p.substr(0u, i);
                            if (!head.empty() && head.find_first_not_of('/') != std::string::npos)
                                head.erase(head.find_last_not_of('/') + 1u);
                            return path_result(aArgs, head);
                        }
                        return path_result(aArgs, split(r, p).first);
                    } },
                { "isabs", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "isabs", p))
                            return nullptr;
                        return make_bool(isabs(r, p));
                    } },
                { "normpath", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "normpath", p))
                            return nullptr;
                        return path_result(aArgs, normpath(r, p));
                    } },
                { "normcase", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "normcase", p))
                            return nullptr;
                        if (r.windows)
                        {
                            std::replace(p.begin(), p.end(), '/', '\\');
                            p = lower(p);
                        }
                        return path_result(aArgs, p);
                    } },
                { "abspath", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "abspath", p))
                            return nullptr;
                        return path_result(aArgs, abspath(r, p));
                    } },
                { "realpath", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("realpath", aArgs, aKwargs, { "filename", "strict" }, 1u, 1u, a))
                            return nullptr;
                        std::string p;
                        if (!path_argument(a[0], p, "realpath", "filename"))
                            return nullptr;
                        std::error_code ec;
                        auto const resolved = std::filesystem::weakly_canonical(std::filesystem::path{ abspath(r, p) }, ec);
                        return path_result(aArgs, ec ? abspath(r, p) : normpath(r, resolved.string()));
                    } },
                { "relpath", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("relpath", aArgs, aKwargs, { "path", "start" }, 1u, 0u, a))
                            return nullptr;
                        std::string path, start = ".";
                        if (!path_argument(a[0], path, "relpath") || (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE && !path_argument(a[1], start, "relpath", "start")))
                            return nullptr;
                        if (path.empty())
                            return error("ValueError", "no path specified");
                        auto const startAbs = splitroot(r, abspath(r, start));
                        auto const pathAbs = splitroot(r, abspath(r, path));
                        if (r.windows && lower(startAbs.drive) != lower(pathAbs.drive))
                            return error("ValueError", "path is on mount '" + pathAbs.drive + "', start on mount '" + startAbs.drive + "'");
                        auto const parts = [&](std::string const& aRest)
                        {
                            std::vector<std::string> result;
                            for (auto const& x : split_on(aRest, r.sep()))
                                if (!x.empty() || r.windows)
                                    result.push_back(x);
                            if (r.windows && aRest.empty())
                                result.clear();
                            return result;
                        };
                        auto const startList = parts(startAbs.tail);
                        auto const pathList = parts(pathAbs.tail);
                        std::size_t i = 0u;
                        while (i < startList.size() && i < pathList.size() && (r.windows ? lower(startList[i]) == lower(pathList[i]) : startList[i] == pathList[i]))
                            ++i;
                        std::vector<std::string> rel(startList.size() - i, "..");
                        rel.insert(rel.end(), pathList.begin() + static_cast<std::ptrdiff_t>(i), pathList.end());
                        if (rel.empty())
                            return path_result(aArgs, ".");
                        if (!r.windows)
                            return path_result(aArgs, join(r, rel[0], std::vector<std::string>{ rel.begin() + 1, rel.end() }));
                        std::string result;
                        for (std::size_t k = 0u; k < rel.size(); ++k)
                            result += (k != 0u ? "\\" : "") + rel[k];
                        return path_result(aArgs, result);
                    } },
                { "commonprefix", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("commonprefix", aArgs, aKwargs, { "m" }, 1u, 1u, a))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[0], items))
                            return nullptr;
                        ref const list{ make_list(items) };
                        if (items.empty())
                            return make_str("");
                        // (the common prefix of the smallest and largest (as min() and max() order them))
                        object* s1 = items[0];
                        object* s2 = items[0];
                        for (auto i : items)
                        {
                            ref const lt{ compare(NEOS_PY_LT, i, s1) };
                            ref const gt{ compare(NEOS_PY_GT, i, s2) };
                            if (!lt || !gt)
                                return nullptr;
                            if (truth(lt.get()) == 1)
                                s1 = i;
                            if (truth(gt.get()) == 1)
                                s2 = i;
                        }
                        auto const n = length(s1);
                        for (long long i = 0; i < n; ++i)
                        {
                            ref const index{ make_int(i) };
                            ref const c1{ getitem(s1, index.get()) };
                            ref const c2{ getitem(s2, index.get()) };
                            if (!c1 || !c2)
                                return nullptr;
                            ref const ne{ compare(NEOS_PY_NE, c1.get(), c2.get()) };
                            if (!ne)
                                return nullptr;
                            if (truth(ne.get()) == 1)
                            {
                                ref const sl{ make_slice(nullptr, index.get(), nullptr) };
                                return getitem(s1, sl.get());
                            }
                        }
                        retain(s1);
                        return s1;
                    } },
                { "commonpath", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("commonpath", aArgs, aKwargs, { "paths" }, 1u, 1u, a))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[0], items))
                            return nullptr;
                        ref const list{ make_list(items) };
                        if (items.empty())
                            return error("ValueError", "commonpath() arg is an empty iterable");
                        std::vector<std::string> paths;
                        for (auto i : items)
                        {
                            std::string p;
                            if (!path_argument(i, p, "commonpath"))
                                return nullptr;
                            paths.push_back(p);
                        }
                        auto const components = [&](std::string const& aTail)
                        {
                            std::vector<std::string> result;
                            for (auto const& c : split_on(aTail, r.sep()))
                                if (!c.empty() && c != ".")
                                    result.push_back(c);
                            return result;
                        };
                        std::vector<std::vector<std::string>> splits;
                        std::set<std::string> drives, roots;
                        for (auto p : paths)
                        {
                            if (r.windows)
                            {
                                std::replace(p.begin(), p.end(), '/', '\\');
                                p = lower(p);
                            }
                            auto const s = splitroot(r, p);
                            drives.insert(s.drive);
                            roots.insert(s.root);
                            splits.push_back(components(s.tail));
                        }
                        if (drives.size() != 1u)
                            return error("ValueError", "Paths don't have the same drive");
                        auto first = paths[0];
                        if (r.windows)
                            std::replace(first.begin(), first.end(), '/', '\\');
                        auto const s0 = splitroot(r, first);
                        if (roots.size() != 1u)
                            return error("ValueError", s0.drive.empty() && r.windows ? "Can't mix rooted and not-rooted paths" : "Can't mix absolute and relative paths");
                        auto common = components(s0.tail);
                        auto const s1 = *std::min_element(splits.begin(), splits.end());
                        auto const s2 = *std::max_element(splits.begin(), splits.end());
                        std::size_t n = s1.size();
                        for (std::size_t i = 0u; i < s1.size(); ++i)
                            if (s1[i] != s2[i])
                            {
                                n = i;
                                break;
                            }
                        common.resize(std::min(n, common.size()));
                        std::string result = s0.drive + s0.root;
                        if (!r.windows && !s0.root.empty())
                            result = "/";
                        for (std::size_t i = 0u; i < common.size(); ++i)
                            result += (i != 0u ? std::string(1u, r.sep()) : "") + common[i];
                        return path_result(aArgs, result);
                    } },
                { "expanduser", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "expanduser", p))
                            return nullptr;
                        return path_result(aArgs, expanduser(r, p));
                    } },
                { "expandvars", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "expandvars", p))
                            return nullptr;
                        return path_result(aArgs, expandvars(r, p));
                    } },
                { "exists", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "exists", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        return make_bool(exists(p));
                    } },
                { "lexists", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "lexists", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        return make_bool(exists(p, false));
                    } },
                { "isfile", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "isfile", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        return make_bool(is_type(p, kIFREG));
                    } },
                { "isdir", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "isdir", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        return make_bool(is_type(p, kIFDIR));
                    } },
                { "islink", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "islink", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        return make_bool(is_type(p, kIFLNK, false));
                    } },
                { "ismount", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "ismount", p))
                            return nullptr;
                        auto const a = abspath(r, p);
                        auto const s = splitroot(r, a);
                        return make_bool(!s.root.empty() && s.tail.empty());
                    } },
                { "getsize", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "getsize", p))
                            return nullptr;
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, true, s, e))
                            return raise_os_error(e, aArgs[0]);
                        return make_int(s.size);
                    } },
                { "getmtime", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "getmtime", p))
                            return nullptr;
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, true, s, e))
                            return raise_os_error(e, aArgs[0]);
                        return make_float(s.mtime);
                    } },
                { "getatime", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "getatime", p))
                            return nullptr;
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, true, s, e))
                            return raise_os_error(e, aArgs[0]);
                        return make_float(s.atime);
                    } },
                { "getctime", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "getctime", p))
                            return nullptr;
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, true, s, e))
                            return raise_os_error(e, aArgs[0]);
                        return make_float(s.ctime);
                    } },
                { "samefile", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("samefile", aArgs, aKwargs, { "f1", "f2" }, 2u, 2u, a))
                            return nullptr;
                        std::string p1, p2;
                        if (!path_argument(a[0], p1, "samefile") || !path_argument(a[1], p2, "samefile"))
                            return nullptr;
                        stat_values s1, s2;
                        int e = 0;
                        if (!do_stat(p1, true, s1, e))
                            return raise_os_error(e, a[0]);
                        if (!do_stat(p2, true, s2, e))
                            return raise_os_error(e, a[1]);
                        return make_bool(s1.ino == s2.ino && s1.dev == s2.dev);
                    } },
                { "samestat", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("samestat", aArgs, aKwargs, { "s1", "s2" }, 2u, 2u, a))
                            return nullptr;
                        long long values[4] = {};
                        int i = 0;
                        for (auto st : { a[0], a[1] })
                            for (auto field : { "st_ino", "st_dev" })
                            {
                                ref const v{ getattr(st, field) };
                                if (!v || !to_index(v.get(), values[i++]))
                                    return nullptr;
                            }
                        return make_bool(values[0] == values[2] && values[1] == values[3]);
                    } },
                { "sameopenfile", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sameopenfile", aArgs, aKwargs, { "fp1", "fp2" }, 2u, 2u, a))
                            return nullptr;
                        long long values[4] = {};
                        int i = 0;
                        for (auto fd : { a[0], a[1] })
                        {
                            ref const st{ invoke(ref{ module_attribute("os", "fstat") }.get(), { new_reference(fd) }) };
                            if (!st)
                                return nullptr;
                            for (auto field : { "st_ino", "st_dev" })
                            {
                                ref const v{ getattr(st.get(), field) };
                                if (!v || !to_index(v.get(), values[i++]))
                                    return nullptr;
                            }
                        }
                        return make_bool(values[0] == values[2] && values[1] == values[3]);
                    } },
                { "isjunction", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("isjunction", aArgs, aKwargs, { "path" }, 1u, 1u, a) || !path_argument(a[0], p, "isjunction"))
                            return nullptr;
                        return make_bool(false);
                    } },
                { "isdevdrive", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("isdevdrive", aArgs, aKwargs, { "path" }, 1u, 1u, a) || !path_argument(a[0], p, "isdevdrive"))
                            return nullptr;
                        return make_bool(false);
                    } }
            };

            object* call_path_function(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& entry = *static_cast<std::pair<path_function const*, path_rules const*> const*>(aData);
                return entry.first->function(*entry.second, as<tuple_object>(aArgs)->items, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            path_rules const sPosix{ false };
            path_rules const sWindows{ true };

            bool path_module(std::vector<std::pair<std::string, object*>>& aAttributes, path_rules const& r)
            {
                static std::vector<std::pair<path_function const*, path_rules const*>> sEntries[2];
                auto& entries = sEntries[r.windows ? 1 : 0];
                if (entries.empty())
                    for (auto const& f : sPathFunctions)
                        entries.emplace_back(&f, &r);
                for (auto const& e : entries)
                    aAttributes.emplace_back(e.first->name, make_function(e.first->name, call_path_function, const_cast<std::pair<path_function const*, path_rules const*>*>(&e)));
                aAttributes.emplace_back("sep", make_str(r.windows ? "\\" : "/"));
                aAttributes.emplace_back("altsep", r.windows ? make_str("/") : (retain(none()), none()));
                aAttributes.emplace_back("extsep", make_str("."));
                aAttributes.emplace_back("pathsep", make_str(r.windows ? ";" : ":"));
                aAttributes.emplace_back("curdir", make_str("."));
                aAttributes.emplace_back("pardir", make_str(".."));
                aAttributes.emplace_back("defpath", make_str(r.windows ? ".;C:\\bin" : "/bin:/usr/bin"));
                aAttributes.emplace_back("devnull", make_str(r.windows ? "nul" : "/dev/null"));
                aAttributes.emplace_back("supports_unicode_filenames", make_bool(r.windows));
                return true;
            }

            bool posixpath_module(std::vector<std::pair<std::string, object*>>& aAttributes) { return path_module(aAttributes, sPosix); }

            // (genericpath: the functions that posixpath and ntpath share)
            bool genericpath_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                static std::set<std::string, std::less<>> const sNames = { "commonprefix", "exists", "getatime", "getctime", "getmtime", "getsize", 
                    "isdevdrive", "isdir", "isfile", "isjunction", "islink", "lexists", "samefile", "sameopenfile", "samestat" };
                std::vector<std::pair<std::string, object*>> all;
                if (!path_module(all, sPosix))
                    return false;
                std::vector<object*> names;
                for (auto& a : all)
                    if (sNames.contains(a.first))
                        aAttributes.push_back(a);
                    else
                        release(a.second);
                for (auto const& n : sNames)
                    names.push_back(make_str(n));
                names.push_back(make_str("ALLOW_MISSING"));
                aAttributes.emplace_back("__all__", make_list(std::move(names)));
                // (ALLOW_MISSING: realpath's strict argument's special value)
                static builtin_entry const sRepr[] = { { "__repr__", [](std::vector<object*> const&, object*) -> object* { return make_str("os.path.ALLOW_MISSING"); } } };
                ref const cls{ make_class_of("genericpath", "_AllowMissing", {}, sRepr) };
                if (!cls)
                    return false;
                aAttributes.emplace_back("ALLOW_MISSING", invoke(cls.get(), {}));
                return true;
            }
            bool ntpath_module(std::vector<std::pair<std::string, object*>>& aAttributes) { return path_module(aAttributes, sWindows); }

#if defined(_WIN32)
            path_rules const& sPlatform = sWindows;
#else
            path_rules const& sPlatform = sPosix;
#endif

            // --- os.scandir: DirEntry objects

            struct dir_entry_object
            {
                header h;
                std::string name;
                std::string path;
                bool bytes = false;
            };

            neos_py_type make_dir_entry_type();
            neos_py_type sDirEntryType = make_dir_entry_type();
            dir_entry_object& as_entry(object* aObject) { return *reinterpret_cast<dir_entry_object*>(aObject); }

            object* entry_text(dir_entry_object const& e, std::string const& aValue)
            {
                return e.bytes ? make_bytes(aValue) : make_str(aValue);
            }

            object* entry_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const which = static_cast<int>(reinterpret_cast<std::uintptr_t>(aData));
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const& e = as_entry(items[0]);
                bool follow = true;
                if (aKwargs != nullptr)
                    for (auto const& item : as<dict_object>(aKwargs)->items)
                        if (as<str_object>(item.first)->value == "follow_symlinks")
                            follow = truth(item.second) == 1;
                switch (which)
                {
                case 0: return make_bool(is_type(e.path, kIFDIR, follow));
                case 1: return make_bool(is_type(e.path, kIFREG, follow));
                case 2: return make_bool(is_type(e.path, kIFLNK, false));
                case 3:
                    {
                        stat_values s;
                        int err = 0;
                        if (!do_stat(e.path, follow, s, err))
                        {
                            ref const p{ entry_text(e, e.path) };
                            return raise_os_error(err, p.get());
                        }
                        return stat_result(s);
                    }
                case 4:
                    {
                        stat_values s;
                        int err = 0;
                        do_stat(e.path, false, s, err);
                        return make_int(s.ino);
                    }
                case 5: return entry_text(e, e.path);
                default: return make_bool(false);
                }
            }

            neos_py_type make_dir_entry_type()
            {
                auto type = make_type("posix.DirEntry", "DirEntry", retain_object, release_object<dir_entry_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    ref const name{ entry_text(as_entry(aSelf), as_entry(aSelf).name) };
                    std::string text;
                    if (!repr_of(name.get(), text))
                        return nullptr;
                    return make_str("<DirEntry " + text + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& e = as_entry(aSelf);
                    if (std::strcmp(aName, "name") == 0)
                        return entry_text(e, e.name);
                    if (std::strcmp(aName, "path") == 0)
                        return entry_text(e, e.path);
                    static char const* const sMethods[] = { "is_dir", "is_file", "is_symlink", "stat", "inode", "__fspath__", "is_junction" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (std::strcmp(aName, sMethods[i]) == 0)
                            return make_function(sMethods[i], entry_method, reinterpret_cast<void*>(i), aSelf);
                    return error("AttributeError", std::string{ "'posix.DirEntry' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // (os.scandir's iterator: also a context manager)
            struct scandir_object
            {
                header h;
                object* entries;
                std::size_t next = 0u;
                ~scandir_object() { release(entries); }
            };

            neos_py_type make_scandir_type();
            neos_py_type sScandirType = make_scandir_type();

            object* scandir_method(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const self = as<tuple_object>(aArgs)->items[0];
                auto& s = *reinterpret_cast<scandir_object*>(self);
                switch (static_cast<int>(reinterpret_cast<std::uintptr_t>(aData)))
                {
                case 0:
                    retain(self);
                    return self;
                default:
                    s.next = as<list_object>(s.entries)->items.size();
                    return none();
                }
            }

            neos_py_type make_scandir_type()
            {
                auto type = make_type("posix.ScandirIterator", "ScandirIterator", retain_object, release_object<scandir_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& s = *reinterpret_cast<scandir_object*>(aSelf);
                    auto const& items = as<list_object>(s.entries)->items;
                    if (s.next >= items.size())
                        return nullptr;
                    retain(items[s.next]);
                    return items[s.next++];
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "__enter__") == 0)
                        return make_function(aName, scandir_method, reinterpret_cast<void*>(std::uintptr_t{ 0u }), aSelf);
                    if (std::strcmp(aName, "__exit__") == 0 || std::strcmp(aName, "close") == 0)
                        return make_function(aName, scandir_method, reinterpret_cast<void*>(std::uintptr_t{ 1u }), aSelf);
                    return error("AttributeError", std::string{ "'posix.ScandirIterator' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // (the entries of a directory (aPath: as given); null if it cannot be read)
            bool directory_entries(std::string const& aPath, std::vector<std::string>& aNames, int& aErrno)
            {
#if defined(_WIN32)
                std::error_code ec;
                std::filesystem::directory_iterator it{ std::filesystem::u8path(aPath.empty() ? "." : aPath), ec };
                if (ec)
                {
                    aErrno = ec.value() == static_cast<int>(std::errc::no_such_file_or_directory) || ec.value() == 2 || ec.value() == 3 ? ENOENT :
                        ec.value() == 267 ? ENOTDIR : EACCES;
                    return false;
                }
                for (auto const& entry : it)
                {
                    auto const name = entry.path().filename().u8string();
                    aNames.emplace_back(name.begin(), name.end());
                }
                return true;
#else
                DIR* const d = ::opendir(aPath.empty() ? "." : aPath.c_str());
                if (d == nullptr)
                {
                    aErrno = errno;
                    return false;
                }
                while (auto const* entry = ::readdir(d))
                {
                    std::string const name = entry->d_name;
                    if (name != "." && name != "..")
                        aNames.push_back(name);
                }
                ::closedir(d);
                return true;
#endif
            }

            // --- os.walk: an iterator (the directories that it descends into are those of the list it yielded, which can be changed)

            struct walk_object
            {
                header h;
                std::vector<std::string> stack;
                bool topdown = true;
                bool followlinks = false;
                bool bytes = false;
                object* onerror = nullptr;
                object* pendingDirs = nullptr;      ///< (the dirnames list last yielded (topdown))
                std::string pendingTop;
                // (bottom-up: the directories still to yield, and those listed)
                struct frame { std::string top; std::vector<std::string> dirs; std::vector<std::string> files; std::size_t next = 0u; };
                std::vector<frame> frames;
                bool started = false;
                ~walk_object() { release(onerror); release(pendingDirs); }
            };

            neos_py_type make_walk_type();
            neos_py_type sWalkType = make_walk_type();

            object* walk_text(walk_object const& w, std::string const& aValue) { return w.bytes ? make_bytes(aValue) : make_str(aValue); }

            object* walk_tuple(walk_object const& w, std::string const& aTop, object* aDirs, std::vector<std::string> const& aFiles)
            {
                std::vector<object*> files;
                for (auto const& f : aFiles)
                    files.push_back(walk_text(w, f));
                retain(aDirs);
                return make_tuple({ walk_text(w, aTop), aDirs, make_list(std::move(files)) });
            }

            neos_py_type make_walk_type()
            {
                auto type = make_type("generator", "generator", retain_object, release_object<walk_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& w = *reinterpret_cast<walk_object*>(aSelf);
                    if (w.topdown)
                    {
                        // (the directories that the last tuple listed (as the caller left them))
                        if (w.pendingDirs != nullptr)
                        {
                            std::vector<object*> dirs;
                            if (!collect(w.pendingDirs, dirs))
                                return nullptr;
                            for (auto d = dirs.rbegin(); d != dirs.rend(); ++d)
                            {
                                std::string name;
                                std::string_view b;
                                if (bytes_like(*d, b))
                                    name = std::string{ b };
                                else
                                    str_of(*d, name);
                                auto const path = join(sPlatform, w.pendingTop, { name });
                                if (w.followlinks || !is_type(path, kIFLNK, false))
                                    w.stack.push_back(path);
                            }
                            for (auto d : dirs)
                                release(d);
                            release(w.pendingDirs);
                            w.pendingDirs = nullptr;
                        }
                        while (!w.stack.empty())
                        {
                            auto const top = w.stack.back();
                            w.stack.pop_back();
                            std::vector<std::string> dirs, files;
                            std::vector<std::string> names;
                            int e = 0;
                            if (!directory_entries(top, names, e))
                            {
                                if (w.onerror != nullptr && kind_of(w.onerror) != NEOS_PY_NONE)
                                {
                                    ref const path{ walk_text(w, top) };
                                    std::vector<object*> args{ make_int(e), make_str(std::strerror(e)), (retain(path.get()), path.get()) };
                                    ref const exception{ make_exception(e == ENOENT ? "FileNotFoundError" : e == ENOTDIR ? "NotADirectoryError" :
                                        e == EACCES ? "PermissionError" : "OSError", std::move(args)) };
                                    ref const callArgs{ make_tuple({ (retain(exception.get()), exception.get()) }) };
                                    ref const r{ call(w.onerror, callArgs.get(), nullptr) };
                                    if (!r)
                                        return nullptr;
                                }
                                continue;
                            }
                            for (auto const& name : names)
                            {
                                auto const full = join(sPlatform, top, { name });
                                if (is_type(full, kIFDIR, true))
                                    dirs.push_back(name);
                                else
                                    files.push_back(name);
                            }
                            std::vector<object*> dirObjects;
                            for (auto const& d : dirs)
                                dirObjects.push_back(walk_text(w, d));
                            w.pendingDirs = make_list(std::move(dirObjects));
                            w.pendingTop = top;
                            return walk_tuple(w, top, w.pendingDirs, files);
                        }
                        return nullptr;
                    }
                    // (bottom-up: a directory after its subdirectories)
                    if (!w.started)
                    {
                        w.started = true;
                        walk_object::frame f;
                        f.top = w.stack.empty() ? std::string{ "." } : w.stack.back();
                        w.stack.clear();
                        std::vector<std::string> names;
                        int e = 0;
                        if (!directory_entries(f.top, names, e))
                            return nullptr;
                        for (auto const& name : names)
                            (is_type(join(sPlatform, f.top, { name }), kIFDIR, true) ? f.dirs : f.files).push_back(name);
                        w.frames.push_back(std::move(f));
                    }
                    while (!w.frames.empty())
                    {
                        auto& f = w.frames.back();
                        if (f.next < f.dirs.size())
                        {
                            auto const path = join(sPlatform, f.top, { f.dirs[f.next++] });
                            if (!w.followlinks && is_type(path, kIFLNK, false))
                                continue;
                            walk_object::frame child;
                            child.top = path;
                            std::vector<std::string> names;
                            int e = 0;
                            if (!directory_entries(path, names, e))
                                continue;
                            for (auto const& name : names)
                                (is_type(join(sPlatform, path, { name }), kIFDIR, true) ? child.dirs : child.files).push_back(name);
                            w.frames.push_back(std::move(child));
                            continue;
                        }
                        std::vector<object*> dirObjects;
                        for (auto const& d : f.dirs)
                            dirObjects.push_back(walk_text(w, d));
                        ref const dirs{ make_list(std::move(dirObjects)) };
                        auto const result = walk_tuple(w, f.top, dirs.get(), f.files);
                        w.frames.pop_back();
                        return result;
                    }
                    return nullptr;
                };
                return type;
            }

            // --- os functions

            bool path_of(std::vector<object*> const& a, std::size_t aIndex, std::string& aPath, char const* aFunction, char const* aName = "path")
            {
                return path_argument(a[aIndex], aPath, aFunction, aName);
            }

            int make_directory(std::string const& aPath, long long aMode)
            {
#if defined(_WIN32)
                (void)aMode;
                return ::_mkdir(aPath.c_str());
#else
                return ::mkdir(aPath.c_str(), static_cast<mode_t>(aMode));
#endif
            }

            builtin_entry const sOsFunctions[] =
            {
                { "getcwd", [](std::vector<object*> const&, object*) -> object* { return make_str(current_directory()); } },
                { "getcwdb", [](std::vector<object*> const&, object*) -> object* { return make_bytes(current_directory()); } },
                { "chdir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.chdir"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("chdir", aArgs, aKwargs, { "path" }, 1u, 0u, a) || !path_of(a, 0u, p, "chdir"))
                            return nullptr;
                        std::error_code ec;
                        std::filesystem::current_path(std::filesystem::path{ p }, ec);
                        if (ec)
                            return raise_os_error(is_type(p, kIFREG) ? ENOTDIR : exists(p) ? EACCES : ENOENT, a[0]);
                        return none();
                    } },
                { "listdir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("listdir", aArgs, aKwargs, { "path" }, 0u, 0u, a))
                            return nullptr;
                        std::string p = ".";
                        bool bytes = false;
                        std::string_view b;
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                        {
                            if (!path_of(a, 0u, p, "listdir"))
                                return nullptr;
                            bytes = bytes_like(a[0], b);
                        }
                        std::vector<std::string> names;
                        int e = 0;
                        if (!directory_entries(p, names, e))
                        {
                            ref const name{ a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE ? (retain(a[0]), a[0]) : make_str(".") };
                            return raise_os_error(e, name.get());
                        }
                        std::vector<object*> items;
                        for (auto const& n : names)
                            items.push_back(bytes ? make_bytes(n) : make_str(n));
                        return make_list(std::move(items));
                    } },
                { "scandir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("scandir", aArgs, aKwargs, { "path" }, 0u, 0u, a))
                            return nullptr;
                        std::string p = ".";
                        bool bytes = false;
                        std::string_view b;
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                        {
                            if (!path_of(a, 0u, p, "scandir"))
                                return nullptr;
                            bytes = bytes_like(a[0], b);
                        }
                        std::vector<std::string> names;
                        int e = 0;
                        if (!directory_entries(p, names, e))
                        {
                            ref const name{ a[0] != nullptr ? (retain(a[0]), a[0]) : make_str(".") };
                            return raise_os_error(e, name.get());
                        }
                        std::vector<object*> entries;
                        for (auto const& n : names)
                        {
                            auto const entry = new dir_entry_object{ { { &sDirEntryType.base } }, n, a[0] == nullptr || kind_of(a[0]) == NEOS_PY_NONE ? "./" + n : join(sPlatform, p, { n }), bytes };
                            entries.push_back(&entry->h.base);
                        }
                        return &(new scandir_object{ { { &sScandirType.base } }, make_list(std::move(entries)) })->h.base;
                    } },
                { "mkdir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.mkdir"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("mkdir", aArgs, aKwargs, { "path", "mode", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "mkdir"))
                            return nullptr;
                        long long mode = 0777;
                        if (a[1] != nullptr && !to_index(a[1], mode))
                            return nullptr;
                        if (make_directory(p, mode) != 0)
                            return raise_os_error(errno, a[0]);
                        return none();
                    } },
                { "makedirs", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.makedirs"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("makedirs", aArgs, aKwargs, { "name", "mode", "exist_ok" }, 1u, 0u, a) || !path_of(a, 0u, p, "makedirs", "name"))
                            return nullptr;
                        long long mode = 0777;
                        if (a[1] != nullptr && !to_index(a[1], mode))
                            return nullptr;
                        bool const existOk = a[2] != nullptr && truth(a[2]) == 1;
                        // (os.makedirs: the parents first)
                        std::vector<std::string> missing;
                        auto current = p;
                        while (!current.empty() && !exists(current))
                        {
                            missing.push_back(current);
                            auto const parent = split(sPlatform, current).first;
                            if (parent == current)
                                break;
                            current = parent;
                        }
                        if (missing.empty())
                        {
                            if (existOk && is_type(p, kIFDIR))
                                return none();
                            return raise_os_error(EEXIST, a[0]);
                        }
                        for (auto m = missing.rbegin(); m != missing.rend(); ++m)
                            if (split(sPlatform, *m).second != "." && make_directory(*m, mode) != 0 && !(errno == EEXIST && is_type(*m, kIFDIR)))
                            {
                                ref const name{ make_str(*m) };
                                return raise_os_error(errno, m == missing.rend() - 1 ? a[0] : name.get());
                            }
                        return none();
                    } },
                { "rmdir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.rmdir"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("rmdir", aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "rmdir"))
                            return nullptr;
#if defined(_WIN32)
                        if (::_rmdir(p.c_str()) != 0)
#else
                        if (::rmdir(p.c_str()) != 0)
#endif
                            return raise_os_error(errno, a[0]);
                        return none();
                    } },
                { "removedirs", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.removedirs"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("removedirs", aArgs, aKwargs, { "name" }, 1u, 0u, a) || !path_of(a, 0u, p, "removedirs", "name"))
                            return nullptr;
                        std::error_code ec;
                        if (!std::filesystem::remove(std::filesystem::path{ p }, ec) || ec)
                            return raise_os_error(ec ? (ec.value() == static_cast<int>(std::errc::directory_not_empty) ? ENOTEMPTY : ENOENT) : ENOENT, a[0]);
                        auto head = split(sPlatform, p);
                        if (head.second.empty())
                            head = split(sPlatform, head.first);
                        while (!head.first.empty() && !head.second.empty())
                        {
                            if (!std::filesystem::remove(std::filesystem::path{ head.first }, ec) || ec)
                                break;
                            head = split(sPlatform, head.first);
                        }
                        return none();
                    } },
                { "remove", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.remove"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("remove", aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "remove"))
                            return nullptr;
                        if (is_type(p, kIFDIR))
                            return raise_os_error(
#if defined(_WIN32)
                                EACCES,
#else
                                EISDIR,
#endif
                                a[0]);
                        if (std::remove(p.c_str()) != 0)
                            return raise_os_error(errno, a[0]);
                        return none();
                    } },
                { "rename", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.rename"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string from, to;
                        if (!parse_arguments("rename", aArgs, aKwargs, { "src", "dst" }, 2u, 0u, a) || !path_of(a, 0u, from, "rename", "src") || !path_of(a, 1u, to, "rename", "dst"))
                            return nullptr;
#if defined(_WIN32)
                        if (exists(to))
                            return raise_os_error(EEXIST, a[0], a[1]);
#endif
                        if (std::rename(from.c_str(), to.c_str()) != 0)
                            return raise_os_error(errno, a[0], a[1]);
                        return none();
                    } },
                { "replace", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.replace"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string from, to;
                        if (!parse_arguments("replace", aArgs, aKwargs, { "src", "dst" }, 2u, 0u, a) || !path_of(a, 0u, from, "replace", "src") || !path_of(a, 1u, to, "replace", "dst"))
                            return nullptr;
                        std::error_code ec;
                        std::filesystem::rename(std::filesystem::path{ from }, std::filesystem::path{ to }, ec);
                        if (ec)
                            return raise_os_error(exists(from) ? EACCES : ENOENT, a[0], a[1]);
                        return none();
                    } },
                { "stat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("stat", aArgs, aKwargs, { "path", "dir_fd", "follow_symlinks" }, 1u, 0u, a) || !path_of(a, 0u, p, "stat"))
                            return nullptr;
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, a[2] == nullptr || truth(a[2]) == 1, s, e))
                            return raise_os_error(e, a[0]);
                        return stat_result(s);
                    } },
                { "lstat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("lstat", aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "lstat"))
                            return nullptr;
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, false, s, e))
                            return raise_os_error(e, a[0]);
                        return stat_result(s);
                    } },
                { "access", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("access", aArgs, aKwargs, { "path", "mode", "dir_fd", "effective_ids", "follow_symlinks" }, 2u, 0u, a) || !path_of(a, 0u, p, "access"))
                            return nullptr;
                        long long mode = 0;
                        if (!to_index(a[1], mode))
                            return nullptr;
#if defined(_WIN32)
                        return make_bool(::_access(p.c_str(), static_cast<int>(mode & 6)) == 0);
#else
                        return make_bool(::access(p.c_str(), static_cast<int>(mode)) == 0);
#endif
                    } },
                { "chmod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.chmod"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("chmod", aArgs, aKwargs, { "path", "mode", "dir_fd", "follow_symlinks" }, 2u, 0u, a) || !path_of(a, 0u, p, "chmod"))
                            return nullptr;
                        long long mode = 0;
                        if (!to_index(a[1], mode))
                            return nullptr;
#if defined(_WIN32)
                        if (::_chmod(p.c_str(), static_cast<int>(mode) & (_S_IREAD | _S_IWRITE)) != 0)
#else
                        if (::chmod(p.c_str(), static_cast<mode_t>(mode)) != 0)
#endif
                            return raise_os_error(errno, a[0]);
                        return none();
                    } },
                { "utime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.utime"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("utime", aArgs, aKwargs, { "path", "times", "ns", "dir_fd", "follow_symlinks" }, 1u, 0u, a) || !path_of(a, 0u, p, "utime"))
                            return nullptr;
                        std::error_code ec;
                        auto when = std::filesystem::file_time_type::clock::now();
                        if (a[1] != nullptr && kind_of(a[1]) == NEOS_PY_TUPLE && as<tuple_object>(a[1])->items.size() == 2u)
                        {
                            double m = 0.0;
                            if (!to_double(as<tuple_object>(a[1])->items[1], m))
                                return nullptr;
                            auto const now = std::chrono::system_clock::now();
                            auto const target = std::chrono::system_clock::time_point{ std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::duration<double>{ m }) };
                            when = std::filesystem::file_time_type::clock::now() + std::chrono::duration_cast<std::filesystem::file_time_type::duration>(target - now);
                        }
                        std::filesystem::last_write_time(std::filesystem::path{ p }, when, ec);
                        if (ec)
                            return raise_os_error(ENOENT, a[0]);
                        return none();
                    } },
                { "walk", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("walk", aArgs, aKwargs, { "top", "topdown", "onerror", "followlinks" }, 1u, 0u, a) || !path_of(a, 0u, p, "walk", "top"))
                            return nullptr;
                        auto const w = new walk_object{ { { &sWalkType.base } } };
                        std::string_view b;
                        w->bytes = bytes_like(a[0], b);
                        w->topdown = a[1] == nullptr || truth(a[1]) == 1;
                        w->followlinks = a[3] != nullptr && truth(a[3]) == 1;
                        if (a[2] != nullptr)
                        {
                            retain(a[2]);
                            w->onerror = a[2];
                        }
                        w->stack.push_back(p);
                        return &w->h.base;
                    } },
                { "getenv", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("getenv", aArgs, aKwargs, { "key", "default" }, 1u, 0u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "str expected, not " } + type_name(a[0]));
                        if (auto const value = environment_value(as<str_object>(a[0])->value))
                            return make_str(*value);
                        auto const result = a[1] != nullptr ? a[1] : none();
                        retain(result);
                        return result;
                    } },
                { "putenv", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.putenv"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("putenv", aArgs, aKwargs, { "name", "value" }, 2u, 2u, a))
                            return nullptr;
                        std::string name, value;
                        if (!str_of(a[0], name) || !str_of(a[1], value))
                            return nullptr;
#if defined(_WIN32)
                        ::_putenv_s(name.c_str(), value.c_str());
#else
                        ::setenv(name.c_str(), value.c_str(), 1);
#endif
                        return none();
                    } },
                { "unsetenv", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.unsetenv"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("unsetenv", aArgs, aKwargs, { "name" }, 1u, 1u, a))
                            return nullptr;
                        std::string name;
                        if (!str_of(a[0], name))
                            return nullptr;
#if defined(_WIN32)
                        ::_putenv_s(name.c_str(), "");
#else
                        ::unsetenv(name.c_str());
#endif
                        return none();
                    } },
                { "getpid", [](std::vector<object*> const&, object*) -> object*
                    {
#if defined(_WIN32)
                        return make_int(::_getpid());
#else
                        return make_int(::getpid());
#endif
                    } },
                { "getppid", [](std::vector<object*> const&, object*) -> object*
                    {
#if defined(_WIN32)
                        return make_int(0);
#else
                        return make_int(::getppid());
#endif
                    } },
                { "cpu_count", [](std::vector<object*> const&, object*) -> object*
                    {
                        auto const n = std::thread::hardware_concurrency();
                        return n == 0u ? (retain(none()), none()) : make_int(n);
                    } },
                { "process_cpu_count", [](std::vector<object*> const&, object*) -> object*
                    {
                        auto const n = std::thread::hardware_concurrency();
                        return n == 0u ? (retain(none()), none()) : make_int(n);
                    } },
                { "urandom", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("urandom", aArgs, aKwargs, { "size" }, 1u, 1u, a))
                            return nullptr;
                        long long n = 0;
                        if (!to_index(a[0], n))
                            return nullptr;
                        if (n < 0)
                            return error("ValueError", "negative argument not allowed");
                        static std::random_device sDevice;
                        std::string data;
                        for (long long i = 0; i < n; ++i)
                            data += static_cast<char>(sDevice() & 0xFFu);
                        return make_bytes(std::move(data));
                    } },
                { "strerror", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("strerror", aArgs, aKwargs, { "code" }, 1u, 1u, a))
                            return nullptr;
                        long long code = 0;
                        if (!to_index(a[0], code))
                            return nullptr;
                        return make_str(std::strerror(static_cast<int>(code)));
                    } },
                { "fspath", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("fspath", aArgs, aKwargs, { "path" }, 1u, 1u, a))
                            return nullptr;
                        std::string_view b;
                        if (kind_of(a[0]) == NEOS_PY_STR || bytes_like(a[0], b))
                            return (retain(a[0]), a[0]);
                        ref const fspath{ getattr(a[0], "__fspath__") };
                        if (!fspath)
                        {
                            clear_error();
                            return error("TypeError", std::string{ "expected str, bytes or os.PathLike object, not " } + type_name(a[0]));
                        }
                        ref const empty{ make_tuple() };
                        return call(fspath.get(), empty.get(), nullptr);
                    } },
                { "get_terminal_size", [](std::vector<object*> const&, object*) -> object*
                    {
                        long long columns = 80, lines = 24;
#if !defined(_WIN32)
                        struct winsize w{};
                        if (::ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_col != 0)
                        {
                            columns = w.ws_col;
                            lines = w.ws_row;
                        }
                        else
                            return raise_os_error(ENOTTY);
#endif
                        auto const& t = structseq_type("os.terminal_size", { "columns", "lines" }, 2u);
                        return make_structseq(t, { make_int(columns), make_int(lines) });
                    } },
                { "umask", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.umask"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("umask", aArgs, aKwargs, { "mask" }, 1u, 1u, a))
                            return nullptr;
                        long long mask = 0;
                        if (!to_index(a[0], mask))
                            return nullptr;
#if defined(_WIN32)
                        return make_int(::_umask(static_cast<int>(mask)));
#else
                        return make_int(::umask(static_cast<mode_t>(mask)));
#endif
                    } },
                { "system", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.system"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("system", aArgs, aKwargs, { "command" }, 1u, 0u, a))
                            return nullptr;
                        std::string command;
                        if (!str_of(a[0], command))
                            return nullptr;
                        flush_standard(1);
                        gil_release();
                        auto const status = std::system(command.c_str());
                        gil_acquire();
                        return make_int(status);
                    } },
                { "getlogin", [](std::vector<object*> const&, object*) -> object*
                    {
                        if (auto const user = environment_value("USER"))
                            return make_str(*user);
                        if (auto const user = environment_value("USERNAME"))
                            return make_str(*user);
                        return raise_os_error(ENOENT);
                    } },
                // (fdopen(fd, *args, **kwargs): open(fd, ...))
                { "fdopen", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "fdopen() missing 1 required positional argument: 'fd'");
                        if (kind_of(aArgs[0]) != NEOS_PY_INT && kind_of(aArgs[0]) != NEOS_PY_BOOL)
                            return error("TypeError", std::string{ "invalid fd type (<class '" } + type_name(aArgs[0]) + "'>, expected integer)");
                        return b_open(aArgs, aKwargs);
                    } }
            };

            // --- the posix module's functions (file descriptors, processes, links, users): os's

            bool int_argument(object* aValue, long long& aResult, char const* aFunction)
            {
                if (to_index(aValue, aResult))
                    return true;
                clear_error();
                error("TypeError", std::string{ "'" } + type_name(aValue) + "' object cannot be interpreted as an integer");
                (void)aFunction;
                return false;
            }

            object* fd_stat(int aFd)
            {
                stat_values r;
#if defined(_WIN32)
                struct _stat64 st{};
                if (::_fstat64(aFd, &st) != 0)
                    return raise_os_error(errno);
                r.atime_ns = static_cast<long long>(st.st_atime) * 1000000000ll;
                r.mtime_ns = static_cast<long long>(st.st_mtime) * 1000000000ll;
                r.ctime_ns = static_cast<long long>(st.st_ctime) * 1000000000ll;
#else
                struct stat st{};
                if (::fstat(aFd, &st) != 0)
                    return raise_os_error(errno);
#if defined(__APPLE__)
                r.atime_ns = static_cast<long long>(st.st_atimespec.tv_sec) * 1000000000ll + st.st_atimespec.tv_nsec;
                r.mtime_ns = static_cast<long long>(st.st_mtimespec.tv_sec) * 1000000000ll + st.st_mtimespec.tv_nsec;
                r.ctime_ns = static_cast<long long>(st.st_ctimespec.tv_sec) * 1000000000ll + st.st_ctimespec.tv_nsec;
#else
                r.atime_ns = static_cast<long long>(st.st_atim.tv_sec) * 1000000000ll + st.st_atim.tv_nsec;
                r.mtime_ns = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000ll + st.st_mtim.tv_nsec;
                r.ctime_ns = static_cast<long long>(st.st_ctim.tv_sec) * 1000000000ll + st.st_ctim.tv_nsec;
#endif
#endif
                r.mode = st.st_mode;
                r.ino = static_cast<long long>(st.st_ino);
                r.dev = static_cast<long long>(st.st_dev);
                r.nlink = static_cast<long long>(st.st_nlink);
                r.uid = static_cast<long long>(st.st_uid);
                r.gid = static_cast<long long>(st.st_gid);
                r.size = static_cast<long long>(st.st_size);
                r.atime = static_cast<double>(r.atime_ns) / 1e9;
                r.mtime = static_cast<double>(r.mtime_ns) / 1e9;
                r.ctime = static_cast<double>(r.ctime_ns) / 1e9;
                return stat_result(r);
            }

            // (a function of one int argument (a file descriptor): the int it returns (-1: errno))
            template <typename F>
            object* fd_call(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, F aFunction)
            {
                std::vector<object*> a;
                long long fd = 0;
                if (!parse_arguments(aName, aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !int_argument(a[0], fd, aName))
                    return nullptr;
                auto const r = aFunction(static_cast<int>(fd));
                if (r < 0)
                    return raise_os_error(errno);
                return make_int(r);
            }

            builtin_entry const sPosixFunctions[] =
            {
                { "open", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        long long flags = 0, mode = 0777;
                        if (!parse_arguments("open", aArgs, aKwargs, { "path", "flags", "mode", "dir_fd" }, 2u, 0u, a) || !path_of(a, 0u, p, "open") ||
                            !int_argument(a[1], flags, "open") || (a[2] != nullptr && !int_argument(a[2], mode, "open")))
                            return nullptr;
                        // (a file opened to write (or created): restricted)
                        if ((flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0 && !host_unrestricted("os.open"))
                            return nullptr;
#if defined(_WIN32)
                        auto const fd = ::_open(p.c_str(), static_cast<int>(flags) | _O_BINARY, static_cast<int>(mode & 0600));
#else
                        auto const fd = ::open(p.c_str(), static_cast<int>(flags) | O_CLOEXEC, static_cast<mode_t>(mode));
#endif
                        if (fd < 0)
                            return raise_os_error(errno, a[0]);
                        return make_int(fd);
                    } },
                { "close", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
#if defined(_WIN32)
                        ref const r{ fd_call("close", aArgs, aKwargs, [](int fd) { return ::_close(fd); }) };
#else
                        ref const r{ fd_call("close", aArgs, aKwargs, [](int fd) { return ::close(fd); }) };
#endif
                        return r ? none() : nullptr;
                    } },
                { "closerange", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long low = 0, high = 0;
                        if (!parse_arguments("closerange", aArgs, aKwargs, { "fd_low", "fd_high" }, 2u, 2u, a) || !int_argument(a[0], low, "closerange") ||
                            !int_argument(a[1], high, "closerange"))
                            return nullptr;
                        for (auto fd = low; fd < high; ++fd)
#if defined(_WIN32)
                            ::_close(static_cast<int>(fd));
#else
                            ::close(static_cast<int>(fd));
#endif
                        return none();
                    } },
                { "read", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, n = 0;
                        if (!parse_arguments("read", aArgs, aKwargs, { "fd", "length" }, 2u, 2u, a) || !int_argument(a[0], fd, "read") || !int_argument(a[1], n, "read"))
                            return nullptr;
                        if (n < 0)
                            return error("ValueError", "negative buffersize in read");
                        std::string data(static_cast<std::size_t>(n), '\0');
                        gil_release();
                        // (the standard input: the running program's (see read_standard_some))
#if defined(_WIN32)
                        auto const r = fd == 0 ? static_cast<long long>(read_standard_some(data.data(), data.size())) : ::_read(static_cast<int>(fd), data.data(), static_cast<unsigned>(n));
#else
                        auto const r = fd == 0 ? static_cast<long long>(read_standard_some(data.data(), data.size())) : ::read(static_cast<int>(fd), data.data(), static_cast<std::size_t>(n));
#endif
                        auto const e = errno;
                        gil_acquire();
                        if (r < 0)
                            return raise_os_error(e);
                        data.resize(static_cast<std::size_t>(r));
                        return make_bytes(std::move(data));
                    } },
                { "write", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        std::string_view data;
                        if (!parse_arguments("write", aArgs, aKwargs, { "fd", "data" }, 2u, 2u, a) || !int_argument(a[0], fd, "write"))
                            return nullptr;
                        if (!bytes_like(a[1], data))
                            return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[1]) + "'");
                        std::string const copy{ data };
                        gil_release();
                        // (the standard output and error: the running program's (see write_standard))
#if defined(_WIN32)
                        auto const r = fd == 1 || fd == 2 ? (write_standard(static_cast<int>(fd), copy) ? static_cast<long long>(copy.size()) : (errno = EIO, -1)) : 
                            ::_write(static_cast<int>(fd), copy.data(), static_cast<unsigned>(copy.size()));
#else
                        auto const r = fd == 1 || fd == 2 ? (write_standard(static_cast<int>(fd), copy) ? static_cast<long long>(copy.size()) : (errno = EIO, -1)) : 
                            ::write(static_cast<int>(fd), copy.data(), copy.size());
#endif
                        auto const e = errno;
                        gil_acquire();
                        if (r < 0)
                            return raise_os_error(e);
                        return make_int(static_cast<long long>(r));
                    } },
                { "lseek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, position = 0, how = 0;
                        if (!parse_arguments("lseek", aArgs, aKwargs, { "fd", "position", "whence" }, 3u, 3u, a) || !int_argument(a[0], fd, "lseek") ||
                            !int_argument(a[1], position, "lseek") || !int_argument(a[2], how, "lseek"))
                            return nullptr;
#if defined(_WIN32)
                        auto const r = ::_lseeki64(static_cast<int>(fd), position, static_cast<int>(how));
#else
                        auto const r = ::lseek(static_cast<int>(fd), static_cast<off_t>(position), static_cast<int>(how));
#endif
                        if (r < 0)
                            return raise_os_error(errno);
                        return make_int(static_cast<long long>(r));
                    } },
                { "fstat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("fstat", aArgs, aKwargs, { "fd" }, 1u, 0u, a) || !int_argument(a[0], fd, "fstat"))
                            return nullptr;
                        return fd_stat(static_cast<int>(fd));
                    } },
                { "isatty", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("isatty", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !int_argument(a[0], fd, "isatty"))
                            return nullptr;
#if defined(_WIN32)
                        return make_bool(::_isatty(static_cast<int>(fd)) != 0);
#else
                        return make_bool(::isatty(static_cast<int>(fd)) != 0);
#endif
                    } },
                { "dup", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
#if defined(_WIN32)
                        return fd_call("dup", aArgs, aKwargs, [](int fd) { return ::_dup(fd); });
#else
                        return fd_call("dup", aArgs, aKwargs, [](int fd) { return ::fcntl(fd, F_DUPFD_CLOEXEC, 0); });
#endif
                    } },
                { "dup2", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, fd2 = 0;
                        if (!parse_arguments("dup2", aArgs, aKwargs, { "fd", "fd2", "inheritable" }, 2u, 0u, a) || !int_argument(a[0], fd, "dup2") || !int_argument(a[1], fd2, "dup2"))
                            return nullptr;
#if defined(_WIN32)
                        if (::_dup2(static_cast<int>(fd), static_cast<int>(fd2)) < 0)
#else
                        if (::dup2(static_cast<int>(fd), static_cast<int>(fd2)) < 0)
#endif
                            return raise_os_error(errno);
                        return make_int(fd2);
                    } },
                { "pipe", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "pipe") || !aArgs.empty())
                            return error("TypeError", "pipe() takes no arguments");
                        int fds[2] = {};
#if defined(_WIN32)
                        if (::_pipe(fds, 65536, _O_BINARY | _O_NOINHERIT) != 0)
#else
                        if (::pipe(fds) != 0)
#endif
                            return raise_os_error(errno);
#if !defined(_WIN32)
                        ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
                        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
                        return make_tuple({ make_int(fds[0]), make_int(fds[1]) });
                    } },
                { "fsync", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("fsync", aArgs, aKwargs, { "fd" }, 1u, 1u, a))
                            return nullptr;
                        if (!to_index(a[0], fd))
                        {
                            clear_error();
                            ref const fileno{ call_method_named(a[0], "fileno", {}) };
                            if (!fileno || !int_argument(fileno.get(), fd, "fsync"))
                                return nullptr;
                        }
#if defined(_WIN32)
                        if (::_commit(static_cast<int>(fd)) != 0)
#else
                        if (::fsync(static_cast<int>(fd)) != 0)
#endif
                            return raise_os_error(errno);
                        return none();
                    } },
                { "ftruncate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.ftruncate"))
                            return nullptr;
                        std::vector<object*> a;
                        long long fd = 0, length = 0;
                        if (!parse_arguments("ftruncate", aArgs, aKwargs, { "fd", "length" }, 2u, 2u, a) || !int_argument(a[0], fd, "ftruncate") || !int_argument(a[1], length, "ftruncate"))
                            return nullptr;
#if defined(_WIN32)
                        if (::_chsize_s(static_cast<int>(fd), length) != 0)
#else
                        if (::ftruncate(static_cast<int>(fd), static_cast<off_t>(length)) != 0)
#endif
                            return raise_os_error(errno);
                        return none();
                    } },
                { "truncate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.truncate"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        long long length = 0;
                        if (!parse_arguments("truncate", aArgs, aKwargs, { "path", "length" }, 2u, 0u, a) || !path_of(a, 0u, p, "truncate") || !int_argument(a[1], length, "truncate"))
                            return nullptr;
                        std::error_code ec;
                        std::filesystem::resize_file(std::filesystem::path{ p }, static_cast<std::uintmax_t>(length), ec);
                        if (ec)
                            return raise_os_error(ec.value(), a[0]);
                        return none();
                    } },
                { "get_inheritable", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("get_inheritable", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !int_argument(a[0], fd, "get_inheritable"))
                            return nullptr;
#if defined(_WIN32)
                        return make_bool(false);
#else
                        auto const flags = ::fcntl(static_cast<int>(fd), F_GETFD);
                        if (flags < 0)
                            return raise_os_error(errno);
                        return make_bool((flags & FD_CLOEXEC) == 0);
#endif
                    } },
                { "set_inheritable", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("set_inheritable", aArgs, aKwargs, { "fd", "inheritable" }, 2u, 2u, a) || !int_argument(a[0], fd, "set_inheritable"))
                            return nullptr;
#if !defined(_WIN32)
                        auto const flags = ::fcntl(static_cast<int>(fd), F_GETFD);
                        if (flags < 0 || ::fcntl(static_cast<int>(fd), F_SETFD, truth(a[1]) == 1 ? flags & ~FD_CLOEXEC : flags | FD_CLOEXEC) < 0)
                            return raise_os_error(errno);
#endif
                        return none();
                    } },
                { "get_blocking", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("get_blocking", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !int_argument(a[0], fd, "get_blocking"))
                            return nullptr;
#if defined(_WIN32)
                        return make_bool(true);
#else
                        auto const flags = ::fcntl(static_cast<int>(fd), F_GETFL);
                        if (flags < 0)
                            return raise_os_error(errno);
                        return make_bool((flags & O_NONBLOCK) == 0);
#endif
                    } },
                { "set_blocking", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("set_blocking", aArgs, aKwargs, { "fd", "blocking" }, 2u, 2u, a) || !int_argument(a[0], fd, "set_blocking"))
                            return nullptr;
#if !defined(_WIN32)
                        auto const flags = ::fcntl(static_cast<int>(fd), F_GETFL);
                        if (flags < 0 || ::fcntl(static_cast<int>(fd), F_SETFL, truth(a[1]) == 1 ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) < 0)
                            return raise_os_error(errno);
#endif
                        return none();
                    } },
                { "device_encoding", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("device_encoding", aArgs, aKwargs, { "fd" }, 1u, 0u, a) || !int_argument(a[0], fd, "device_encoding"))
                            return nullptr;
#if defined(_WIN32)
                        bool const tty = ::_isatty(static_cast<int>(fd)) != 0;
#else
                        bool const tty = ::isatty(static_cast<int>(fd)) != 0;
#endif
                        return tty ? make_str("utf-8") : none();
                    } },
                { "_exit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long status = 0;
                        if (!parse_arguments("_exit", aArgs, aKwargs, { "status" }, 1u, 0u, a) || !int_argument(a[0], status, "_exit"))
                            return nullptr;
                        std::fflush(nullptr);
                        std::_Exit(static_cast<int>(status));
                    } },
                { "kill", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.kill"))
                            return nullptr;
                        std::vector<object*> a;
                        long long pid = 0, signal = 0;
                        if (!parse_arguments("kill", aArgs, aKwargs, { "pid", "signal" }, 2u, 2u, a) || !int_argument(a[0], pid, "kill") || !int_argument(a[1], signal, "kill"))
                            return nullptr;
#if defined(_WIN32)
                        auto const process = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
                        if (process == nullptr || !::TerminateProcess(process, static_cast<UINT>(signal)))
                        {
                            if (process != nullptr)
                                ::CloseHandle(process);
                            return raise_os_error(ESRCH);
                        }
                        ::CloseHandle(process);
#else
                        if (::kill(static_cast<pid_t>(pid), static_cast<int>(signal)) != 0)
                            return raise_os_error(errno);
#endif
                        return none();
                    } },
                { "times", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!aArgs.empty())
                            return error("TypeError", "times() takes no arguments");
                        auto const& t = structseq_type("posix.times_result", { "user", "system", "children_user", "children_system", "elapsed" }, 5u);
#if defined(_WIN32)
                        FILETIME created, exited, kernel, user;
                        ::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user);
                        auto const seconds = [](FILETIME const& f) { return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) * 1e-7; };
                        return make_structseq(t, { make_float(seconds(user)), make_float(seconds(kernel)), make_float(0.0), make_float(0.0), make_float(0.0) });
#else
                        struct tms buffer{};
                        auto const elapsed = ::times(&buffer);
                        double const ticks = static_cast<double>(::sysconf(_SC_CLK_TCK));
                        return make_structseq(t, { make_float(static_cast<double>(buffer.tms_utime) / ticks), make_float(static_cast<double>(buffer.tms_stime) / ticks),
                            make_float(static_cast<double>(buffer.tms_cutime) / ticks), make_float(static_cast<double>(buffer.tms_cstime) / ticks),
                            make_float(static_cast<double>(elapsed) / ticks) });
#endif
                    } },
#if !defined(_WIN32)
                { "getuid", [](std::vector<object*> const&, object*) -> object* { return make_int(static_cast<long long>(::getuid())); } },
                { "geteuid", [](std::vector<object*> const&, object*) -> object* { return make_int(static_cast<long long>(::geteuid())); } },
                { "getgid", [](std::vector<object*> const&, object*) -> object* { return make_int(static_cast<long long>(::getgid())); } },
                { "getegid", [](std::vector<object*> const&, object*) -> object* { return make_int(static_cast<long long>(::getegid())); } },
                { "getpgrp", [](std::vector<object*> const&, object*) -> object* { return make_int(static_cast<long long>(::getpgrp())); } },
                { "getgroups", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::vector<gid_t> groups(static_cast<std::size_t>(std::max(0, ::getgroups(0, nullptr))));
                        auto const n = ::getgroups(static_cast<int>(groups.size()), groups.data());
                        if (n < 0)
                            return raise_os_error(errno);
                        std::vector<object*> items;
                        for (int i = 0; i < n; ++i)
                            items.push_back(make_int(static_cast<long long>(groups[static_cast<std::size_t>(i)])));
                        return make_list(std::move(items));
                    } },
                { "getpgid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        if (!parse_arguments("getpgid", aArgs, aKwargs, { "pid" }, 1u, 1u, a) || !int_argument(a[0], pid, "getpgid"))
                            return nullptr;
                        auto const r = ::getpgid(static_cast<pid_t>(pid));
                        return r < 0 ? raise_os_error(errno) : make_int(r);
                    } },
                { "getsid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        if (!parse_arguments("getsid", aArgs, aKwargs, { "pid" }, 1u, 1u, a) || !int_argument(a[0], pid, "getsid"))
                            return nullptr;
                        auto const r = ::getsid(static_cast<pid_t>(pid));
                        return r < 0 ? raise_os_error(errno) : make_int(r);
                    } },
                { "setsid", [](std::vector<object*> const&, object*) -> object* { return ::setsid() < 0 ? raise_os_error(errno) : none(); } },
                { "killpg", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.killpg"))
                            return nullptr;
                        std::vector<object*> a;
                        long long group = 0, signal = 0;
                        if (!parse_arguments("killpg", aArgs, aKwargs, { "pgid", "signal" }, 2u, 2u, a) || !int_argument(a[0], group, "killpg") || !int_argument(a[1], signal, "killpg"))
                            return nullptr;
                        if (::killpg(static_cast<pid_t>(group), static_cast<int>(signal)) != 0)
                            return raise_os_error(errno);
                        return none();
                    } },
                { "fork", [](std::vector<object*> const&, object*) -> object*
                    {
                        if (!host_unrestricted("os.fork"))
                            return nullptr;
                        std::fflush(nullptr);
                        auto const pid = ::fork();
                        return pid < 0 ? raise_os_error(errno) : make_int(pid);
                    } },
                { "waitpid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0, options = 0;
                        if (!parse_arguments("waitpid", aArgs, aKwargs, { "pid", "options" }, 2u, 2u, a) || !int_argument(a[0], pid, "waitpid") || !int_argument(a[1], options, "waitpid"))
                            return nullptr;
                        int status = 0;
                        gil_release();
                        auto const r = ::waitpid(static_cast<pid_t>(pid), &status, static_cast<int>(options));
                        auto const e = errno;
                        gil_acquire();
                        if (r < 0)
                            return raise_os_error(e);
                        return make_tuple({ make_int(r), make_int(status) });
                    } },
                { "wait", [](std::vector<object*> const&, object*) -> object*
                    {
                        int status = 0;
                        gil_release();
                        auto const r = ::wait(&status);
                        auto const e = errno;
                        gil_acquire();
                        if (r < 0)
                            return raise_os_error(e);
                        return make_tuple({ make_int(r), make_int(status) });
                    } },
                { "WIFEXITED", [](std::vector<object*> const& aArgs, object*) -> object* { long long s = 0; return aArgs.size() == 1u && int_argument(aArgs[0], s, "WIFEXITED") ? make_bool(WIFEXITED(static_cast<int>(s))) : nullptr; } },
                { "WEXITSTATUS", [](std::vector<object*> const& aArgs, object*) -> object* { long long s = 0; return aArgs.size() == 1u && int_argument(aArgs[0], s, "WEXITSTATUS") ? make_int(WEXITSTATUS(static_cast<int>(s))) : nullptr; } },
                { "WIFSIGNALED", [](std::vector<object*> const& aArgs, object*) -> object* { long long s = 0; return aArgs.size() == 1u && int_argument(aArgs[0], s, "WIFSIGNALED") ? make_bool(WIFSIGNALED(static_cast<int>(s))) : nullptr; } },
                { "WTERMSIG", [](std::vector<object*> const& aArgs, object*) -> object* { long long s = 0; return aArgs.size() == 1u && int_argument(aArgs[0], s, "WTERMSIG") ? make_int(WTERMSIG(static_cast<int>(s))) : nullptr; } },
                { "WIFSTOPPED", [](std::vector<object*> const& aArgs, object*) -> object* { long long s = 0; return aArgs.size() == 1u && int_argument(aArgs[0], s, "WIFSTOPPED") ? make_bool(WIFSTOPPED(static_cast<int>(s))) : nullptr; } },
                { "WSTOPSIG", [](std::vector<object*> const& aArgs, object*) -> object* { long long s = 0; return aArgs.size() == 1u && int_argument(aArgs[0], s, "WSTOPSIG") ? make_int(WSTOPSIG(static_cast<int>(s))) : nullptr; } },
                { "waitstatus_to_exitcode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long s = 0;
                        if (!parse_arguments("waitstatus_to_exitcode", aArgs, aKwargs, { "status" }, 1u, 0u, a) || !int_argument(a[0], s, "waitstatus_to_exitcode"))
                            return nullptr;
                        auto const status = static_cast<int>(s);
                        if (WIFEXITED(status))
                            return make_int(WEXITSTATUS(status));
                        if (WIFSIGNALED(status))
                            return make_int(-WTERMSIG(status));
                        return error("ValueError", "invalid wait status: " + std::to_string(s));
                    } },
                { "execv", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.execv"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("execv", aArgs, aKwargs, { "path", "argv" }, 2u, 2u, a) || !path_of(a, 0u, p, "execv"))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[1], items))
                            return nullptr;
                        std::vector<std::string> args;
                        for (auto i : items)
                        {
                            std::string text;
                            bool const ok = path_argument(i, text, "execv", "argv");
                            release(i);
                            if (!ok)
                                return nullptr;
                            args.push_back(text);
                        }
                        if (args.empty())
                            return error("ValueError", "execv() arg 2 must not be empty");
                        std::vector<char*> argv;
                        for (auto& x : args)
                            argv.push_back(x.data());
                        argv.push_back(nullptr);
                        std::fflush(nullptr);
                        ::execv(p.c_str(), argv.data());
                        return raise_os_error(errno);
                    } },
                { "nice", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.nice"))
                            return nullptr;
                        std::vector<object*> a;
                        long long increment = 0;
                        if (!parse_arguments("nice", aArgs, aKwargs, { "increment" }, 1u, 1u, a) || !int_argument(a[0], increment, "nice"))
                            return nullptr;
                        errno = 0;
                        auto const r = ::nice(static_cast<int>(increment));
                        if (r == -1 && errno != 0)
                            return raise_os_error(errno);
                        return make_int(r);
                    } },
                { "uname", [](std::vector<object*> const&, object*) -> object*
                    {
                        struct utsname u{};
                        if (::uname(&u) != 0)
                            return raise_os_error(errno);
                        auto const& t = structseq_type("posix.uname_result", { "sysname", "nodename", "release", "version", "machine" }, 5u);
                        return make_structseq(t, { make_str(u.sysname), make_str(u.nodename), make_str(u.release), make_str(u.version), make_str(u.machine) });
                    } },
                { "getloadavg", [](std::vector<object*> const&, object*) -> object*
                    {
                        double load[3] = {};
                        if (::getloadavg(load, 3) != 3)
                            return error("OSError", "Load averages are unobtainable");
                        return make_tuple({ make_float(load[0]), make_float(load[1]), make_float(load[2]) });
                    } },
                { "symlink", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.symlink"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string from, to;
                        if (!parse_arguments("symlink", aArgs, aKwargs, { "src", "dst", "target_is_directory", "dir_fd" }, 2u, 0u, a) || !path_of(a, 0u, from, "symlink", "src") ||
                            !path_of(a, 1u, to, "symlink", "dst"))
                            return nullptr;
                        if (::symlink(from.c_str(), to.c_str()) != 0)
                            return raise_os_error(errno, a[0], a[1]);
                        return none();
                    } },
                { "readlink", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("readlink", aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "readlink"))
                            return nullptr;
                        std::string buffer(4096u, '\0');
                        auto const n = ::readlink(p.c_str(), buffer.data(), buffer.size());
                        if (n < 0)
                            return raise_os_error(errno, a[0]);
                        buffer.resize(static_cast<std::size_t>(n));
                        std::string_view b;
                        return bytes_like(a[0], b) ? make_bytes(std::move(buffer)) : make_str(std::move(buffer));
                    } },
                { "link", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.link"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string from, to;
                        if (!parse_arguments("link", aArgs, aKwargs, { "src", "dst", "src_dir_fd", "dst_dir_fd", "follow_symlinks" }, 2u, 0u, a) ||
                            !path_of(a, 0u, from, "link", "src") || !path_of(a, 1u, to, "link", "dst"))
                            return nullptr;
                        if (::link(from.c_str(), to.c_str()) != 0)
                            return raise_os_error(errno, a[0], a[1]);
                        return none();
                    } },
                { "chown", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.chown"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        long long uid = 0, gid = 0;
                        if (!parse_arguments("chown", aArgs, aKwargs, { "path", "uid", "gid", "dir_fd", "follow_symlinks" }, 3u, 0u, a) || !path_of(a, 0u, p, "chown") ||
                            !int_argument(a[1], uid, "chown") || !int_argument(a[2], gid, "chown"))
                            return nullptr;
                        bool const follow = a[4] == nullptr || truth(a[4]) == 1;
                        if ((follow ? ::chown(p.c_str(), static_cast<uid_t>(uid), static_cast<gid_t>(gid)) : ::lchown(p.c_str(), static_cast<uid_t>(uid), static_cast<gid_t>(gid))) != 0)
                            return raise_os_error(errno, a[0]);
                        return none();
                    } },
                { "mkfifo", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.mkfifo"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        long long mode = 0666;
                        if (!parse_arguments("mkfifo", aArgs, aKwargs, { "path", "mode", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "mkfifo") ||
                            (a[1] != nullptr && !int_argument(a[1], mode, "mkfifo")))
                            return nullptr;
                        if (::mkfifo(p.c_str(), static_cast<mode_t>(mode)) != 0)
                            return raise_os_error(errno, a[0]);
                        return none();
                    } },
                { "sync", [](std::vector<object*> const&, object*) -> object* { ::sync(); return none(); } },
                { "sysconf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sysconf", aArgs, aKwargs, { "name" }, 1u, 1u, a))
                            return nullptr;
                        long long name = 0;
                        if (kind_of(a[0]) == NEOS_PY_STR)
                        {
                            auto const& n = as<str_object>(a[0])->value;
                            if (n == "SC_PAGE_SIZE" || n == "SC_PAGESIZE") name = _SC_PAGESIZE;
                            else if (n == "SC_NPROCESSORS_ONLN") name = _SC_NPROCESSORS_ONLN;
                            else if (n == "SC_NPROCESSORS_CONF") name = _SC_NPROCESSORS_CONF;
                            else if (n == "SC_CLK_TCK") name = _SC_CLK_TCK;
                            else if (n == "SC_OPEN_MAX") name = _SC_OPEN_MAX;
                            else if (n == "SC_PHYS_PAGES") name = _SC_PHYS_PAGES;
                            else if (n == "SC_ARG_MAX") name = _SC_ARG_MAX;
                            else
                                return error("ValueError", "unrecognized configuration name");
                        }
                        else if (!int_argument(a[0], name, "sysconf"))
                            return nullptr;
                        errno = 0;
                        auto const r = ::sysconf(static_cast<int>(name));
                        if (r == -1 && errno != 0)
                            return raise_os_error(errno);
                        return make_int(r);
                    } },
                { "statvfs", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("statvfs", aArgs, aKwargs, { "path" }, 1u, 0u, a) || !path_of(a, 0u, p, "statvfs"))
                            return nullptr;
                        struct statvfs v{};
                        if (::statvfs(p.c_str(), &v) != 0)
                            return raise_os_error(errno, a[0]);
                        auto const& t = structseq_type("os.statvfs_result", { "f_bsize", "f_frsize", "f_blocks", "f_bfree", "f_bavail", "f_files", "f_ffree", "f_favail",
                            "f_flag", "f_namemax", "f_fsid" }, 10u);
                        return make_structseq(t, { make_int(static_cast<long long>(v.f_bsize)), make_int(static_cast<long long>(v.f_frsize)), make_int(static_cast<long long>(v.f_blocks)),
                            make_int(static_cast<long long>(v.f_bfree)), make_int(static_cast<long long>(v.f_bavail)), make_int(static_cast<long long>(v.f_files)),
                            make_int(static_cast<long long>(v.f_ffree)), make_int(static_cast<long long>(v.f_favail)), make_int(static_cast<long long>(v.f_flag)),
                            make_int(static_cast<long long>(v.f_namemax)), make_int(static_cast<long long>(v.f_fsid)) });
                    } },
                { "ttyname", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("ttyname", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !int_argument(a[0], fd, "ttyname"))
                            return nullptr;
                        auto const name = ::ttyname(static_cast<int>(fd));
                        return name == nullptr ? raise_os_error(errno) : make_str(name);
                    } },
#endif
            };

            // (the posix module's constants)
            void posix_constants(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                std::vector<std::pair<char const*, long long>> constants = {
                    { "O_APPEND", O_APPEND }, { "O_CREAT", O_CREAT }, { "O_EXCL", O_EXCL }, { "O_TRUNC", O_TRUNC },
#if defined(_WIN32)
                    { "O_BINARY", O_BINARY }, { "O_TEXT", O_TEXT }, { "O_NOINHERIT", O_NOINHERIT }, { "O_TEMPORARY", O_TEMPORARY }, { "O_RANDOM", O_RANDOM }, { "O_SEQUENTIAL", O_SEQUENTIAL },
#else
                    { "O_NONBLOCK", O_NONBLOCK }, { "O_NDELAY", O_NDELAY }, { "O_SYNC", O_SYNC }, { "O_DSYNC", O_DSYNC }, { "O_NOCTTY", O_NOCTTY },
                    { "O_CLOEXEC", O_CLOEXEC }, { "O_DIRECTORY", O_DIRECTORY }, { "O_NOFOLLOW", O_NOFOLLOW }, { "O_ACCMODE", O_ACCMODE },
                    { "WNOHANG", WNOHANG }, { "WUNTRACED", WUNTRACED }, { "EX_OK", 0 }, { "F_LOCK", F_LOCK }, { "F_TLOCK", F_TLOCK }, { "F_ULOCK", F_ULOCK }, { "F_TEST", F_TEST },
                    { "PRIO_PROCESS", 0 }, { "PRIO_PGRP", 1 }, { "PRIO_USER", 2 },
#endif
                };
                for (auto const& c : constants)
                    aAttributes.emplace_back(c.first, make_int(c.second));
                for (auto const& f : sPosixFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
            }

            bool os_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                posix_constants(aAttributes);
                for (auto const& f : sOsFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("unlink", make_builtin(sOsFunctions[9]));
                aAttributes.emplace_back("environ", environment());
                aAttributes.emplace_back("path", fn_import(sPlatform.windows ? "ntpath" : "posixpath"));
#if defined(_WIN32)
                aAttributes.emplace_back("name", make_str("nt"));
                aAttributes.emplace_back("linesep", make_str("\r\n"));
#else
                aAttributes.emplace_back("name", make_str("posix"));
                aAttributes.emplace_back("linesep", make_str("\n"));
#endif
                aAttributes.emplace_back("sep", make_str(sPlatform.windows ? "\\" : "/"));
                aAttributes.emplace_back("altsep", sPlatform.windows ? make_str("/") : (retain(none()), none()));
                aAttributes.emplace_back("extsep", make_str("."));
                aAttributes.emplace_back("pathsep", make_str(sPlatform.windows ? ";" : ":"));
                aAttributes.emplace_back("curdir", make_str("."));
                aAttributes.emplace_back("pardir", make_str(".."));
                aAttributes.emplace_back("defpath", make_str(sPlatform.windows ? ".;C:\\bin" : "/bin:/usr/bin"));
                aAttributes.emplace_back("devnull", make_str(sPlatform.windows ? "nul" : "/dev/null"));
                aAttributes.emplace_back("error", exception_class("OSError"));
                for (auto const& c : { std::pair{ "F_OK", 0 }, { "R_OK", 4 }, { "W_OK", 2 }, { "X_OK", 1 }, { "SEEK_SET", 0 }, { "SEEK_CUR", 1 }, { "SEEK_END", 2 },
                    { "O_RDONLY", 0 }, { "O_WRONLY", 1 }, { "O_RDWR", 2 } })
                    aAttributes.emplace_back(c.first, make_int(c.second));
                return true;
            }

            // --- stat

            builtin_entry const sStatFunctions[] =
            {
                { "S_ISDIR", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool((m & kIFMT) == kIFDIR) : nullptr; } },
                { "S_ISREG", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool((m & kIFMT) == kIFREG) : nullptr; } },
                { "S_ISLNK", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool((m & kIFMT) == kIFLNK) : nullptr; } },
                { "S_ISCHR", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool((m & kIFMT) == 0020000) : nullptr; } },
                { "S_ISBLK", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool((m & kIFMT) == 0060000) : nullptr; } },
                { "S_ISFIFO", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool((m & kIFMT) == 0010000) : nullptr; } },
                { "S_ISSOCK", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool((m & kIFMT) == 0140000) : nullptr; } },
                { "S_IMODE", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_int(m & 07777) : nullptr; } },
                { "S_IFMT", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_int(m & kIFMT) : nullptr; } },
                { "filemode", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        long long m = 0;
                        if (aArgs.size() != 1u || !to_index(aArgs[0], m))
                            return nullptr;
                        std::string result;
                        auto const type = m & kIFMT;
                        result += type == kIFLNK ? 'l' : type == 0140000 ? 's' : type == kIFREG ? '-' : type == 0060000 ? 'b' : type == kIFDIR ? 'd' :
                            type == 0020000 ? 'c' : type == 0010000 ? 'p' : '?';
                        result += (m & 0400) ? 'r' : '-';
                        result += (m & 0200) ? 'w' : '-';
                        result += (m & 04000) ? ((m & 0100) ? 's' : 'S') : ((m & 0100) ? 'x' : '-');
                        result += (m & 040) ? 'r' : '-';
                        result += (m & 020) ? 'w' : '-';
                        result += (m & 02000) ? ((m & 010) ? 's' : 'S') : ((m & 010) ? 'x' : '-');
                        result += (m & 04) ? 'r' : '-';
                        result += (m & 02) ? 'w' : '-';
                        result += (m & 01000) ? ((m & 01) ? 't' : 'T') : ((m & 01) ? 'x' : '-');
                        return make_str(result);
                    } }
            };

            bool stat_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sStatFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                for (auto const& c : { std::pair{ "S_IFDIR", 0040000 }, { "S_IFREG", 0100000 }, { "S_IFLNK", 0120000 }, { "S_IFCHR", 0020000 }, { "S_IFBLK", 0060000 },
                    { "S_IFIFO", 0010000 }, { "S_IFSOCK", 0140000 }, { "S_ISUID", 04000 }, { "S_ISGID", 02000 }, { "S_ISVTX", 01000 }, { "S_IRWXU", 0700 },
                    { "S_IRUSR", 0400 }, { "S_IWUSR", 0200 }, { "S_IXUSR", 0100 }, { "S_IRWXG", 070 }, { "S_IRGRP", 040 }, { "S_IWGRP", 020 }, { "S_IXGRP", 010 },
                    { "S_IRWXO", 07 }, { "S_IROTH", 04 }, { "S_IWOTH", 02 }, { "S_IXOTH", 01 }, { "ST_MODE", 0 }, { "ST_INO", 1 }, { "ST_DEV", 2 }, { "ST_NLINK", 3 },
                    { "ST_UID", 4 }, { "ST_GID", 5 }, { "ST_SIZE", 6 }, { "ST_ATIME", 7 }, { "ST_MTIME", 8 }, { "ST_CTIME", 9 } })
                    aAttributes.emplace_back(c.first, make_int(c.second));
                return true;
            }


            // --- errno

            bool errno_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                static std::pair<char const*, int> const sCodes[] =
                {
#ifdef EPERM
                    { "EPERM", EPERM },
#endif
#ifdef ENOENT
                    { "ENOENT", ENOENT },
#endif
#ifdef ESRCH
                    { "ESRCH", ESRCH },
#endif
#ifdef EINTR
                    { "EINTR", EINTR },
#endif
#ifdef EIO
                    { "EIO", EIO },
#endif
#ifdef ENXIO
                    { "ENXIO", ENXIO },
#endif
#ifdef E2BIG
                    { "E2BIG", E2BIG },
#endif
#ifdef ENOEXEC
                    { "ENOEXEC", ENOEXEC },
#endif
#ifdef EBADF
                    { "EBADF", EBADF },
#endif
#ifdef ECHILD
                    { "ECHILD", ECHILD },
#endif
#ifdef EAGAIN
                    { "EAGAIN", EAGAIN },
#endif
#ifdef ENOMEM
                    { "ENOMEM", ENOMEM },
#endif
#ifdef EACCES
                    { "EACCES", EACCES },
#endif
#ifdef EFAULT
                    { "EFAULT", EFAULT },
#endif
#ifdef EBUSY
                    { "EBUSY", EBUSY },
#endif
#ifdef EEXIST
                    { "EEXIST", EEXIST },
#endif
#ifdef EXDEV
                    { "EXDEV", EXDEV },
#endif
#ifdef ENODEV
                    { "ENODEV", ENODEV },
#endif
#ifdef ENOTDIR
                    { "ENOTDIR", ENOTDIR },
#endif
#ifdef EISDIR
                    { "EISDIR", EISDIR },
#endif
#ifdef EINVAL
                    { "EINVAL", EINVAL },
#endif
#ifdef ENFILE
                    { "ENFILE", ENFILE },
#endif
#ifdef EMFILE
                    { "EMFILE", EMFILE },
#endif
#ifdef ENOTTY
                    { "ENOTTY", ENOTTY },
#endif
#ifdef EFBIG
                    { "EFBIG", EFBIG },
#endif
#ifdef ENOSPC
                    { "ENOSPC", ENOSPC },
#endif
#ifdef ESPIPE
                    { "ESPIPE", ESPIPE },
#endif
#ifdef EROFS
                    { "EROFS", EROFS },
#endif
#ifdef EMLINK
                    { "EMLINK", EMLINK },
#endif
#ifdef EPIPE
                    { "EPIPE", EPIPE },
#endif
#ifdef EDOM
                    { "EDOM", EDOM },
#endif
#ifdef ERANGE
                    { "ERANGE", ERANGE },
#endif
#ifdef EDEADLK
                    { "EDEADLK", EDEADLK },
#endif
#ifdef ENAMETOOLONG
                    { "ENAMETOOLONG", ENAMETOOLONG },
#endif
#ifdef ENOLCK
                    { "ENOLCK", ENOLCK },
#endif
#ifdef ENOSYS
                    { "ENOSYS", ENOSYS },
#endif
#ifdef ENOTEMPTY
                    { "ENOTEMPTY", ENOTEMPTY },
#endif
#ifdef ELOOP
                    { "ELOOP", ELOOP },
#endif
#ifdef EWOULDBLOCK
                    { "EWOULDBLOCK", EWOULDBLOCK },
#endif
#ifdef ENOMSG
                    { "ENOMSG", ENOMSG },
#endif
#ifdef EIDRM
                    { "EIDRM", EIDRM },
#endif
#ifdef ENOSTR
                    { "ENOSTR", ENOSTR },
#endif
#ifdef ENODATA
                    { "ENODATA", ENODATA },
#endif
#ifdef ETIME
                    { "ETIME", ETIME },
#endif
#ifdef ENOSR
                    { "ENOSR", ENOSR },
#endif
#ifdef ENOLINK
                    { "ENOLINK", ENOLINK },
#endif
#ifdef EPROTO
                    { "EPROTO", EPROTO },
#endif
#ifdef EBADMSG
                    { "EBADMSG", EBADMSG },
#endif
#ifdef EOVERFLOW
                    { "EOVERFLOW", EOVERFLOW },
#endif
#ifdef EILSEQ
                    { "EILSEQ", EILSEQ },
#endif
#ifdef ENOTSOCK
                    { "ENOTSOCK", ENOTSOCK },
#endif
#ifdef EDESTADDRREQ
                    { "EDESTADDRREQ", EDESTADDRREQ },
#endif
#ifdef EMSGSIZE
                    { "EMSGSIZE", EMSGSIZE },
#endif
#ifdef EPROTOTYPE
                    { "EPROTOTYPE", EPROTOTYPE },
#endif
#ifdef ENOPROTOOPT
                    { "ENOPROTOOPT", ENOPROTOOPT },
#endif
#ifdef EPROTONOSUPPORT
                    { "EPROTONOSUPPORT", EPROTONOSUPPORT },
#endif
#ifdef EOPNOTSUPP
                    { "EOPNOTSUPP", EOPNOTSUPP },
#endif
#ifdef ENOTSUP
                    { "ENOTSUP", ENOTSUP },
#endif
#ifdef EAFNOSUPPORT
                    { "EAFNOSUPPORT", EAFNOSUPPORT },
#endif
#ifdef EADDRINUSE
                    { "EADDRINUSE", EADDRINUSE },
#endif
#ifdef EADDRNOTAVAIL
                    { "EADDRNOTAVAIL", EADDRNOTAVAIL },
#endif
#ifdef ENETDOWN
                    { "ENETDOWN", ENETDOWN },
#endif
#ifdef ENETUNREACH
                    { "ENETUNREACH", ENETUNREACH },
#endif
#ifdef ENETRESET
                    { "ENETRESET", ENETRESET },
#endif
#ifdef ECONNABORTED
                    { "ECONNABORTED", ECONNABORTED },
#endif
#ifdef ECONNRESET
                    { "ECONNRESET", ECONNRESET },
#endif
#ifdef ENOBUFS
                    { "ENOBUFS", ENOBUFS },
#endif
#ifdef EISCONN
                    { "EISCONN", EISCONN },
#endif
#ifdef ENOTCONN
                    { "ENOTCONN", ENOTCONN },
#endif
#ifdef ETIMEDOUT
                    { "ETIMEDOUT", ETIMEDOUT },
#endif
#ifdef ECONNREFUSED
                    { "ECONNREFUSED", ECONNREFUSED },
#endif
#ifdef EHOSTUNREACH
                    { "EHOSTUNREACH", EHOSTUNREACH },
#endif
#ifdef EALREADY
                    { "EALREADY", EALREADY },
#endif
#ifdef EINPROGRESS
                    { "EINPROGRESS", EINPROGRESS },
#endif
#ifdef ECANCELED
                    { "ECANCELED", ECANCELED },
#endif
#ifdef EOWNERDEAD
                    { "EOWNERDEAD", EOWNERDEAD },
#endif
#ifdef ENOTRECOVERABLE
                    { "ENOTRECOVERABLE", ENOTRECOVERABLE },
#endif
#ifdef ETXTBSY
                    { "ETXTBSY", ETXTBSY },
#endif
                };
#if defined(_WIN32)
                // (Windows: as CPython's errno: the codes that have Winsock equivalents are Winsock's (not the C runtime's), and Winsock's own
                // codes (WSAE...))
                static std::pair<char const*, int> const sWinsockCodes[] =
                {
#ifdef WSAEADDRINUSE
                    { "EADDRINUSE", WSAEADDRINUSE },
#endif
#ifdef WSAEADDRNOTAVAIL
                    { "EADDRNOTAVAIL", WSAEADDRNOTAVAIL },
#endif
#ifdef WSAEAFNOSUPPORT
                    { "EAFNOSUPPORT", WSAEAFNOSUPPORT },
#endif
#ifdef WSAEALREADY
                    { "EALREADY", WSAEALREADY },
#endif
#ifdef WSAECONNABORTED
                    { "ECONNABORTED", WSAECONNABORTED },
#endif
#ifdef WSAECONNREFUSED
                    { "ECONNREFUSED", WSAECONNREFUSED },
#endif
#ifdef WSAECONNRESET
                    { "ECONNRESET", WSAECONNRESET },
#endif
#ifdef WSAEDESTADDRREQ
                    { "EDESTADDRREQ", WSAEDESTADDRREQ },
#endif
#ifdef WSAEHOSTUNREACH
                    { "EHOSTUNREACH", WSAEHOSTUNREACH },
#endif
#ifdef WSAEINPROGRESS
                    { "EINPROGRESS", WSAEINPROGRESS },
#endif
#ifdef WSAEISCONN
                    { "EISCONN", WSAEISCONN },
#endif
#ifdef WSAELOOP
                    { "ELOOP", WSAELOOP },
#endif
#ifdef WSAEMSGSIZE
                    { "EMSGSIZE", WSAEMSGSIZE },
#endif
#ifdef WSAENETDOWN
                    { "ENETDOWN", WSAENETDOWN },
#endif
#ifdef WSAENETRESET
                    { "ENETRESET", WSAENETRESET },
#endif
#ifdef WSAENETUNREACH
                    { "ENETUNREACH", WSAENETUNREACH },
#endif
#ifdef WSAENOBUFS
                    { "ENOBUFS", WSAENOBUFS },
#endif
#ifdef WSAENOPROTOOPT
                    { "ENOPROTOOPT", WSAENOPROTOOPT },
#endif
#ifdef WSAENOTCONN
                    { "ENOTCONN", WSAENOTCONN },
#endif
#ifdef WSAENOTSOCK
                    { "ENOTSOCK", WSAENOTSOCK },
#endif
#ifdef WSAEOPNOTSUPP
                    { "EOPNOTSUPP", WSAEOPNOTSUPP },
#endif
#ifdef WSAEPROTONOSUPPORT
                    { "EPROTONOSUPPORT", WSAEPROTONOSUPPORT },
#endif
#ifdef WSAEPROTOTYPE
                    { "EPROTOTYPE", WSAEPROTOTYPE },
#endif
#ifdef WSAETIMEDOUT
                    { "ETIMEDOUT", WSAETIMEDOUT },
#endif
#ifdef WSAEWOULDBLOCK
                    { "EWOULDBLOCK", WSAEWOULDBLOCK },
#endif
#ifdef WSAEHOSTDOWN
                    { "WSAEHOSTDOWN", WSAEHOSTDOWN },
#endif
#ifdef WSAENETDOWN
                    { "WSAENETDOWN", WSAENETDOWN },
#endif
#ifdef WSAENOTSOCK
                    { "WSAENOTSOCK", WSAENOTSOCK },
#endif
#ifdef WSAEHOSTUNREACH
                    { "WSAEHOSTUNREACH", WSAEHOSTUNREACH },
#endif
#ifdef WSAELOOP
                    { "WSAELOOP", WSAELOOP },
#endif
#ifdef WSAEMFILE
                    { "WSAEMFILE", WSAEMFILE },
#endif
#ifdef WSAESTALE
                    { "WSAESTALE", WSAESTALE },
#endif
#ifdef WSAVERNOTSUPPORTED
                    { "WSAVERNOTSUPPORTED", WSAVERNOTSUPPORTED },
#endif
#ifdef WSAENETUNREACH
                    { "WSAENETUNREACH", WSAENETUNREACH },
#endif
#ifdef WSAEPROCLIM
                    { "WSAEPROCLIM", WSAEPROCLIM },
#endif
#ifdef WSAEFAULT
                    { "WSAEFAULT", WSAEFAULT },
#endif
#ifdef WSANOTINITIALISED
                    { "WSANOTINITIALISED", WSANOTINITIALISED },
#endif
#ifdef WSAEUSERS
                    { "WSAEUSERS", WSAEUSERS },
#endif
#ifdef WSAENOPROTOOPT
                    { "WSAENOPROTOOPT", WSAENOPROTOOPT },
#endif
#ifdef WSAECONNABORTED
                    { "WSAECONNABORTED", WSAECONNABORTED },
#endif
#ifdef WSAENAMETOOLONG
                    { "WSAENAMETOOLONG", WSAENAMETOOLONG },
#endif
#ifdef WSAENOTEMPTY
                    { "WSAENOTEMPTY", WSAENOTEMPTY },
#endif
#ifdef WSAESHUTDOWN
                    { "WSAESHUTDOWN", WSAESHUTDOWN },
#endif
#ifdef WSAEAFNOSUPPORT
                    { "WSAEAFNOSUPPORT", WSAEAFNOSUPPORT },
#endif
#ifdef WSAETOOMANYREFS
                    { "WSAETOOMANYREFS", WSAETOOMANYREFS },
#endif
#ifdef WSAEACCES
                    { "WSAEACCES", WSAEACCES },
#endif
#ifdef WSABASEERR
                    { "WSABASEERR", WSABASEERR },
#endif
#ifdef WSAEMSGSIZE
                    { "WSAEMSGSIZE", WSAEMSGSIZE },
#endif
#ifdef WSAEBADF
                    { "WSAEBADF", WSAEBADF },
#endif
#ifdef WSAECONNRESET
                    { "WSAECONNRESET", WSAECONNRESET },
#endif
#ifdef WSAETIMEDOUT
                    { "WSAETIMEDOUT", WSAETIMEDOUT },
#endif
#ifdef WSAENOBUFS
                    { "WSAENOBUFS", WSAENOBUFS },
#endif
#ifdef WSAEDISCON
                    { "WSAEDISCON", WSAEDISCON },
#endif
#ifdef WSAEINTR
                    { "WSAEINTR", WSAEINTR },
#endif
#ifdef WSAEPROTOTYPE
                    { "WSAEPROTOTYPE", WSAEPROTOTYPE },
#endif
#ifdef WSAEADDRINUSE
                    { "WSAEADDRINUSE", WSAEADDRINUSE },
#endif
#ifdef WSAEADDRNOTAVAIL
                    { "WSAEADDRNOTAVAIL", WSAEADDRNOTAVAIL },
#endif
#ifdef WSAEALREADY
                    { "WSAEALREADY", WSAEALREADY },
#endif
#ifdef WSAEPROTONOSUPPORT
                    { "WSAEPROTONOSUPPORT", WSAEPROTONOSUPPORT },
#endif
#ifdef WSASYSNOTREADY
                    { "WSASYSNOTREADY", WSASYSNOTREADY },
#endif
#ifdef WSAEWOULDBLOCK
                    { "WSAEWOULDBLOCK", WSAEWOULDBLOCK },
#endif
#ifdef WSAEPFNOSUPPORT
                    { "WSAEPFNOSUPPORT", WSAEPFNOSUPPORT },
#endif
#ifdef WSAEOPNOTSUPP
                    { "WSAEOPNOTSUPP", WSAEOPNOTSUPP },
#endif
#ifdef WSAEISCONN
                    { "WSAEISCONN", WSAEISCONN },
#endif
#ifdef WSAEDQUOT
                    { "WSAEDQUOT", WSAEDQUOT },
#endif
#ifdef WSAENOTCONN
                    { "WSAENOTCONN", WSAENOTCONN },
#endif
#ifdef WSAEREMOTE
                    { "WSAEREMOTE", WSAEREMOTE },
#endif
#ifdef WSAEINVAL
                    { "WSAEINVAL", WSAEINVAL },
#endif
#ifdef WSAEINPROGRESS
                    { "WSAEINPROGRESS", WSAEINPROGRESS },
#endif
#ifdef WSAESOCKTNOSUPPORT
                    { "WSAESOCKTNOSUPPORT", WSAESOCKTNOSUPPORT },
#endif
#ifdef WSAEDESTADDRREQ
                    { "WSAEDESTADDRREQ", WSAEDESTADDRREQ },
#endif
#ifdef WSAECONNREFUSED
                    { "WSAECONNREFUSED", WSAECONNREFUSED },
#endif
#ifdef WSAENETRESET
                    { "WSAENETRESET", WSAENETRESET },
#endif
                };
#endif
                ref errorcode{ make_dict() };
                for (auto c : sCodes)
                {
#if defined(_WIN32)
                    for (auto const& w : sWinsockCodes)
                        if (std::strcmp(w.first, c.first) == 0)
                            c.second = w.second;
#endif
                    aAttributes.emplace_back(c.first, make_int(c.second));
                    ref const code{ make_int(c.second) };
                    // (errorcode: the first name of a code (EAGAIN before EWOULDBLOCK, as CPython))
                    if (dict_find(errorcode.get(), code.get()) < 0)
                    {
                        ref const name{ make_str(c.first) };
                        dict_set(errorcode.get(), code.get(), name.get());
                    }
                }
#if defined(_WIN32)
                for (auto const& w : sWinsockCodes)
                    if (std::strncmp(w.first, "WSA", 3u) == 0)
                    {
                        aAttributes.emplace_back(w.first, make_int(w.second));
                        ref const code{ make_int(w.second) };
                        if (dict_find(errorcode.get(), code.get()) < 0)
                        {
                            ref const name{ make_str(w.first) };
                            dict_set(errorcode.get(), code.get(), name.get());
                        }
                    }
#endif
                aAttributes.emplace_back("errorcode", errorcode.release_ownership());
                return true;
            }

            // --- pathlib (PurePath, PurePosixPath, PureWindowsPath, Path, PosixPath and WindowsPath: classes whose methods are builtin
            // functions; an instance's path is its attribute _str (normalized) (and _raw_paths: its arguments))

            object* sNtpathModule = nullptr;
            object* sPurePathClass = nullptr;
            object* sPathClass = nullptr;
            object* sPurePosixPathClass = nullptr;
            object* sPureWindowsPathClass = nullptr;
            object* sPosixPathClass = nullptr;
            object* sWindowsPathClass = nullptr;

            struct parsed_path
            {
                path_rules const* rules = &sPosix;
                std::string drv;
                std::string root;
                std::vector<std::string> tail;
                std::string anchor() const { return drv + root; }
                std::string name() const { return tail.empty() ? std::string{} : tail.back(); }
            };

            parsed_path parse_path(path_rules const& r, std::string aPath)
            {
                parsed_path result;
                result.rules = &r;
                if (aPath.empty())
                    return result;
                if (r.windows)
                    std::replace(aPath.begin(), aPath.end(), '/', '\\');
                auto split = splitroot(r, aPath);
                auto const sep = r.sep();
                if (split.root.empty() && !split.drive.empty() && split.drive[0] == sep && split.drive.back() != sep)
                {
                    auto const driveParts = split_on(split.drive, sep);
                    if ((driveParts.size() == 4u && driveParts[2] != "?" && driveParts[2] != ".") || driveParts.size() == 6u)
                        split.root = std::string(1u, sep);
                }
                result.drv = split.drive;
                result.root = split.root;
                for (auto const& part : split_on(split.tail, sep))
                    if (!part.empty() && part != ".")
                        result.tail.push_back(part);
                return result;
            }

            std::string format_parts(path_rules const& r, std::string const& aDrv, std::string const& aRoot, std::vector<std::string> const& aTail)
            {
                std::string joined;
                for (std::size_t i = 0u; i < aTail.size(); ++i)
                    joined += (i != 0u ? std::string(1u, r.sep()) : std::string{}) + aTail[i];
                if (!aDrv.empty() || !aRoot.empty())
                    return aDrv + aRoot + joined;
                if (!aTail.empty() && !splitroot(r, aTail[0]).drive.empty())
                    return std::string{ "." } + r.sep() + joined;
                return joined.empty() ? std::string{ "." } : joined;
            }

            std::string format_parts(parsed_path const& p) { return format_parts(*p.rules, p.drv, p.root, p.tail); }

            std::string normcase(path_rules const& r, std::string const& aText) { return r.windows ? lower(aText) : aText; }

            bool is_pure_path(object* aObject)
            {
                if (sPurePathClass == nullptr || !is_instance(aObject))
                    return false;
                ref const result{ b_isinstance({ aObject, sPurePathClass }, nullptr) };
                return result && result.get() == make_bool(true);
            }

            // (an instance's path)
            bool self_path(object* aSelf, parsed_path& aResult, std::string* aText = nullptr)
            {
                ref const text{ getattr(aSelf, "_str") };
                if (!text || kind_of(text.get()) != NEOS_PY_STR)
                {
                    if (text)
                        error("TypeError", "_str must be a str");
                    return false;
                }
                ref const parser{ getattr(aSelf, "parser") };
                if (!parser)
                    return false;
                auto const& r = parser.get() == sNtpathModule ? sWindows : sPosix;
                aResult = parse_path(r, as<str_object>(text.get())->value);
                if (aText != nullptr)
                    *aText = as<str_object>(text.get())->value;
                return true;
            }

            std::string self_text(object* aSelf)
            {
                ref const text{ getattr(aSelf, "_str") };
                return text && kind_of(text.get()) == NEOS_PY_STR ? as<str_object>(text.get())->value : std::string{};
            }

            // (a new path of an instance's class (its with_segments): steals the arguments)
            object* new_path(object* aSelf, std::vector<object*> aArgs)
            {
                ref const segments{ getattr(aSelf, "with_segments") };
                ref const args{ make_tuple(std::move(aArgs)) };
                if (!segments)
                    return nullptr;
                return call(segments.get(), args.get(), nullptr);
            }

            object* path_from_parts(object* aSelf, parsed_path const& aPath, std::string const& aDrv, std::string const& aRoot, std::vector<std::string> const& aTail)
            {
                return new_path(aSelf, { make_str(format_parts(*aPath.rules, aDrv, aRoot, aTail)) });
            }

            std::string repr_string(std::string const& aText)
            {
                ref const text{ make_str(aText) };
                std::string result;
                repr_of(text.get(), result);
                return result;
            }

            // (a path's repr: e.g. PosixPath('a/b'))
            std::string path_repr(object* aSelf, parsed_path const& aPath)
            {
                std::string posix = format_parts(aPath);
                if (aPath.rules->windows)
                    std::replace(posix.begin(), posix.end(), '\\', '/');
                ref const cls{ class_of(aSelf) };
                ref const name{ getattr(cls.get(), "__name__") };
                std::string className = name && kind_of(name.get()) == NEOS_PY_STR ? as<str_object>(name.get())->value : std::string{ "PurePath" };
                return className + "(" + repr_string(posix) + ")";
            }

            // (fnmatch's pattern matching of a name: *, ? and [...])
            bool wildcard_match(std::string_view aName, std::string_view aPattern, bool aCaseSensitive)
            {
                auto const fold = [&](char c) { return aCaseSensitive ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
                std::size_t n = 0u, p = 0u, starP = std::string_view::npos, starN = 0u;
                while (n < aName.size())
                {
                    if (p < aPattern.size())
                    {
                        auto const c = aPattern[p];
                        if (c == '*')
                        {
                            starP = p++;
                            starN = n;
                            continue;
                        }
                        if (c == '?')
                        {
                            ++p;
                            ++n;
                            continue;
                        }
                        if (c == '[')
                        {
                            auto const close = aPattern.find(']', p + 2u);
                            if (close != std::string_view::npos)
                            {
                                auto set = aPattern.substr(p + 1u, close - p - 1u);
                                bool negate = false;
                                if (!set.empty() && set[0] == '!')
                                {
                                    negate = true;
                                    set.remove_prefix(1u);
                                }
                                bool matched = false;
                                auto const ch = fold(aName[n]);
                                for (std::size_t i = 0u; i < set.size(); ++i)
                                {
                                    if (i + 2u < set.size() && set[i + 1u] == '-')
                                    {
                                        if (ch >= fold(set[i]) && ch <= fold(set[i + 2u]))
                                            matched = true;
                                        i += 2u;
                                    }
                                    else if (fold(set[i]) == ch)
                                        matched = true;
                                }
                                if (matched != negate)
                                {
                                    p = close + 1u;
                                    ++n;
                                    continue;
                                }
                            }
                            else if (aName[n] == '[')
                            {
                                ++p;
                                ++n;
                                continue;
                            }
                        }
                        else if (fold(c) == fold(aName[n]))
                        {
                            ++p;
                            ++n;
                            continue;
                        }
                    }
                    if (starP == std::string_view::npos)
                        return false;
                    p = starP + 1u;
                    n = ++starN;
                }
                while (p < aPattern.size() && aPattern[p] == '*')
                    ++p;
                return p == aPattern.size();
            }

            bool has_wildcards(std::string const& aText) { return aText.find_first_of("*?[") != std::string::npos; }

            // (full_match's segments: ** matches any number of segments)
            bool match_segments(std::vector<std::string> const& aPath, std::size_t aPathIndex, std::vector<std::string> const& aPattern, std::size_t aPatternIndex,
                bool aCaseSensitive)
            {
                if (aPatternIndex == aPattern.size())
                    return aPathIndex == aPath.size();
                if (aPattern[aPatternIndex] == "**")
                {
                    for (auto i = aPathIndex; i <= aPath.size(); ++i)
                        if (match_segments(aPath, i, aPattern, aPatternIndex + 1u, aCaseSensitive))
                            return true;
                    return false;
                }
                return aPathIndex < aPath.size() && wildcard_match(aPath[aPathIndex], aPattern[aPatternIndex], aCaseSensitive) &&
                    match_segments(aPath, aPathIndex + 1u, aPattern, aPatternIndex + 1u, aCaseSensitive);
            }

            std::vector<std::string> path_parts(parsed_path const& aPath)
            {
                std::vector<std::string> parts;
                if (!aPath.drv.empty() || !aPath.root.empty())
                    parts.push_back(aPath.anchor());
                parts.insert(parts.end(), aPath.tail.begin(), aPath.tail.end());
                return parts;
            }

            // (an argument of a path operation: a str or a path (its text); false: neither (no exception))
            bool segment_text(object* aValue, std::string& aResult)
            {
                if (kind_of(aValue) == NEOS_PY_STR)
                {
                    aResult = as<str_object>(aValue)->value;
                    return true;
                }
                if (is_pure_path(aValue))
                {
                    aResult = self_text(aValue);
                    return true;
                }
                if (kind_of(aValue) == NEOS_PY_OTHER)
                {
                    ref const fspath{ getattr(aValue, "__fspath__") };
                    if (fspath)
                    {
                        ref const empty{ make_tuple() };
                        ref const p{ call(fspath.get(), empty.get(), nullptr) };
                        if (p && kind_of(p.get()) == NEOS_PY_STR)
                        {
                            aResult = as<str_object>(p.get())->value;
                            return true;
                        }
                    }
                    clear_error();
                }
                return false;
            }

            object* os_call(char const* aName, std::vector<object*> const& aArgs, object* aKwargs = nullptr)
            {
                for (auto const& f : sOsFunctions)
                    if (std::strcmp(f.name, aName) == 0)
                        return f.function(aArgs, aKwargs);
                return error("AttributeError", std::string{ "module 'os' has no attribute '" } + aName + "'");
            }

            object* path_open(object* aSelf, char const* aMode, object* aEncoding, object* aErrors, object* aNewline)
            {
                ref const kwargs{ make_dict() };
                for (auto const& kw : { std::pair{ "encoding", aEncoding }, std::pair{ "errors", aErrors }, std::pair{ "newline", aNewline } })
                    if (kw.second != nullptr)
                    {
                        ref const key{ make_str(kw.first) };
                        dict_set(kwargs.get(), key.get(), kw.second);
                    }
                ref const mode{ make_str(aMode) };
                return b_open({ aSelf, mode.get() }, kwargs.get());
            }

            // (the paths that a glob pattern's segments match, from a directory)
            void glob_paths(parsed_path const& aBase, std::vector<std::string> const& aPattern, std::size_t aIndex, bool aCaseSensitive,
                std::vector<parsed_path>& aResults, bool aDirectoriesOnly)
            {
                if (aIndex == aPattern.size())
                {
                    auto const text = format_parts(aBase);
                    if (aDirectoriesOnly ? is_type(text, kIFDIR) : exists(text, false))
                        aResults.push_back(aBase);
                    return;
                }
                auto const& segment = aPattern[aIndex];
                auto const text = format_parts(aBase);
                if (segment == "**")
                {
                    // (this directory and its subdirectories (not following symbolic links))
                    if (!is_type(text, kIFDIR))
                        return;
                    if (aIndex + 1u == aPattern.size())
                    {
                        // (a final **: the directory and everything below it (only directories if the pattern ends with a separator))
                        std::vector<parsed_path> stack{ aBase };
                        while (!stack.empty())
                        {
                            auto current = stack.back();
                            stack.pop_back();
                            aResults.push_back(current);
                            std::vector<std::string> names;
                            int e = 0;
                            if (!directory_entries(format_parts(current), names, e))
                                continue;
                            std::vector<parsed_path> children;
                            for (auto const& n : names)
                            {
                                auto child = current;
                                child.tail.push_back(n);
                                if (is_type(format_parts(child), kIFDIR, false))
                                    children.push_back(child);
                                else if (!aDirectoriesOnly)
                                    aResults.push_back(child);
                            }
                            stack.insert(stack.end(), children.rbegin(), children.rend());
                        }
                        return;
                    }
                    glob_paths(aBase, aPattern, aIndex + 1u, aCaseSensitive, aResults, aDirectoriesOnly);
                    std::vector<std::string> names;
                    int e = 0;
                    if (!directory_entries(text, names, e))
                        return;
                    for (auto const& n : names)
                    {
                        auto child = aBase;
                        child.tail.push_back(n);
                        if (is_type(format_parts(child), kIFDIR, false))
                            glob_paths(child, aPattern, aIndex, aCaseSensitive, aResults, aDirectoriesOnly);
                    }
                    return;
                }
                if (!has_wildcards(segment))
                {
                    auto child = aBase;
                    child.tail.push_back(segment);
                    if (aIndex + 1u < aPattern.size() && !is_type(format_parts(child), kIFDIR))
                        return;
                    glob_paths(child, aPattern, aIndex + 1u, aCaseSensitive, aResults, aDirectoriesOnly);
                    return;
                }
                std::vector<std::string> names;
                int e = 0;
                if (!directory_entries(text, names, e))
                    return;
                for (auto const& n : names)
                    if (wildcard_match(n, segment, aCaseSensitive))
                    {
                        auto child = aBase;
                        child.tail.push_back(n);
                        if (aIndex + 1u < aPattern.size() && !is_type(format_parts(child), kIFDIR))
                            continue;
                        glob_paths(child, aPattern, aIndex + 1u, aCaseSensitive, aResults, aDirectoriesOnly);
                    }
            }

            object* glob_result(object* aSelf, std::string const& aPattern, object* aCaseSensitive, bool aRecursive)
            {
                parsed_path base;
                if (!self_path(aSelf, base))
                    return nullptr;
                if (aPattern.empty() && !aRecursive)
                    return error("ValueError", "Unacceptable pattern: " + repr_string(aPattern));
                auto const pattern = parse_path(*base.rules, aPattern);
                if (!pattern.drv.empty() || !pattern.root.empty())
                    return error("NotImplementedError", "Non-relative patterns are unsupported");
                auto segments = pattern.tail;
                bool const trailingSeparator = !aPattern.empty() && base.rules->is_sep(aPattern.back());
                if (aRecursive)
                    segments.insert(segments.begin(), "**");
                bool const directoriesOnly = trailingSeparator;
                bool caseSensitive = !base.rules->windows;
                if (aCaseSensitive != nullptr && kind_of(aCaseSensitive) != NEOS_PY_NONE)
                    caseSensitive = truth(aCaseSensitive) == 1;
                std::vector<parsed_path> results;
                if (segments.empty())
                    results.push_back(base);
                else
                    glob_paths(base, segments, 0u, caseSensitive, results, directoriesOnly);
                std::vector<object*> paths;
                for (auto const& r : results)
                {
                    auto const p = new_path(aSelf, { make_str(format_parts(r)) });
                    if (p == nullptr)
                    {
                        for (auto x : paths)
                            release(x);
                        return nullptr;
                    }
                    paths.push_back(p);
                }
                ref const list{ make_list(std::move(paths)) };
                return iter(list.get());
            }

            // (a method's self and its path)
            struct path_self
            {
                object* self = nullptr;
                parsed_path path;
                std::string text;
                std::vector<object*> args;
            };

            bool path_self_of(std::vector<object*> const& aArgs, path_self& aResult)
            {
                if (aArgs.empty())
                {
                    error("TypeError", "descriptor requires a path");
                    return false;
                }
                aResult.self = aArgs[0];
                aResult.args.assign(aArgs.begin() + 1, aArgs.end());
                return self_path(aResult.self, aResult.path, &aResult.text);
            }

            builtin_entry const sPurePathMethods[] =
            {
                { "__new__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "__new__() missing cls");
                        auto cls = aArgs[0];
                        if (cls == sPurePathClass)
                            cls = sPlatform.windows ? sPureWindowsPathClass : sPurePosixPathClass;
                        else if (cls == sPathClass)
                            cls = sPlatform.windows ? sWindowsPathClass : sPosixPathClass;
                        else if (cls == (sPlatform.windows ? sPosixPathClass : sWindowsPathClass))
                        {
                            ref const name{ getattr(cls, "__name__") };
                            return error("pathlib.UnsupportedOperation", "cannot instantiate " + repr_string(as<str_object>(name.get())->value) + " on your system");
                        }
                        ref const empty{ make_tuple() };
                        return new_instance(cls, empty.get(), nullptr);
                    } },
                { "__init__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "__init__() missing self");
                        auto const self = aArgs[0];
                        ref const parser{ getattr(self, "parser") };
                        if (!parser)
                            return nullptr;
                        auto const& r = parser.get() == sNtpathModule ? sWindows : sPosix;
                        std::vector<std::string> raw;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                        {
                            std::string text;
                            if (!segment_text(aArgs[i], text))
                            {
                                ref const path{ ref::borrowed(aArgs[i]) };
                                return error("TypeError", std::string{ "argument should be a str or an os.PathLike object where __fspath__ returns a str, not " } +
                                    repr_string(type_name(aArgs[i])));
                            }
                            if (is_pure_path(aArgs[i]) && r.windows != (parse_path(sPosix, "").rules == &sWindows))
                            {
                                ref const otherParser{ getattr(aArgs[i], "parser") };
                                bool const otherWindows = otherParser && otherParser.get() == sNtpathModule;
                                if (otherWindows && !r.windows)
                                    std::replace(text.begin(), text.end(), '\\', '/');
                            }
                            raw.push_back(text);
                        }
                        std::string joined;
                        if (raw.size() == 1u)
                            joined = raw[0];
                        else if (raw.size() > 1u)
                            joined = join(r, raw[0], { raw.begin() + 1, raw.end() });
                        auto const normalized = format_parts(parse_path(r, joined));
                        std::vector<object*> rawItems;
                        for (auto const& t : raw)
                            rawItems.push_back(make_str(t));
                        ref const rawList{ make_list(std::move(rawItems)) };
                        ref const text{ make_str(normalized) };
                        if (setattr(self, "_raw_paths", rawList.get()) < 0 || setattr(self, "_str", text.get()) < 0)
                            return nullptr;
                        return (retain(none()), none());
                    } },
                { "with_segments", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "with_segments() missing self");
                        ref const cls{ class_of(aArgs[0]) };
                        ref const args{ make_tuple(std::vector<object*>{}) };
                        std::vector<object*> rest;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            rest.push_back((retain(aArgs[i]), aArgs[i]));
                        ref const restTuple{ make_tuple(std::move(rest)) };
                        return call(cls.get(), restTuple.get(), aKwargs);
                    } },
                { "__str__", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_str(s.text) : nullptr; } },
                { "__fspath__", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_str(s.text) : nullptr; } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_str(path_repr(s.self, s.path)) : nullptr; } },
                { "__bytes__", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_bytes(s.text) : nullptr; } },
                { "as_posix", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.path.rules->windows)
                            std::replace(s.text.begin(), s.text.end(), '\\', '/');
                        return make_str(s.text);
                    } },
                { "as_uri", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        bool const absolute = s.path.rules->windows ? !s.path.drv.empty() && !s.path.root.empty() : !s.path.root.empty();
                        if (!absolute)
                            return error("ValueError", "relative path can't be expressed as a file URI");
                        std::string prefix = "file://";
                        std::string path = s.text;
                        if (s.path.rules->windows)
                        {
                            std::replace(path.begin(), path.end(), '\\', '/');
                            if (s.path.drv.size() == 2u && s.path.drv[1] == ':')
                            {
                                prefix = "file:///" + s.path.drv;
                                path = path.substr(2u);
                            }
                        }
                        std::string quotedPath;
                        static char const sHex[] = "0123456789ABCDEF";
                        for (unsigned char c : path)
                            if (std::isalnum(c) || std::strchr("_.-~/", c) != nullptr)
                                quotedPath += static_cast<char>(c);
                            else
                            {
                                quotedPath += '%';
                                quotedPath += sHex[c >> 4];
                                quotedPath += sHex[c & 15];
                            }
                        return make_str(prefix + quotedPath);
                    } },
                { "is_absolute", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.path.rules->windows)
                            return make_bool(!s.path.drv.empty() && !s.path.root.empty());
                        return make_bool(!s.path.root.empty());
                    } },
                { "is_reserved", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (!s.path.rules->windows || s.path.tail.empty())
                            return make_bool(false);
                        auto name = s.path.tail.back();
                        name = lower(name.substr(0u, name.find('.')));
                        while (!name.empty() && name.back() == ' ')
                            name.pop_back();
                        static char const* const sReserved[] = { "con", "prn", "aux", "nul", "conin$", "conout$" };
                        for (auto const r : sReserved)
                            if (name == r)
                                return make_bool(true);
                        if (name.size() == 4u && (name.starts_with("com") || name.starts_with("lpt")) && std::strchr("123456789\xb9\xb2\xb3", name[3]) != nullptr)
                            return make_bool(true);
                        return make_bool(false);
                    } },
                { "joinpath", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::vector<object*> args{ (retain(s.self), s.self) };
                        for (auto a : s.args)
                            args.push_back((retain(a), a));
                        return new_path(s.self, std::move(args));
                    } },
                { "__truediv__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::string text;
                        if (s.args.size() != 1u || !segment_text(s.args[0], text))
                            return (retain(not_implemented()), not_implemented());
                        return new_path(s.self, { (retain(s.self), s.self), (retain(s.args[0]), s.args[0]) });
                    } },
                { "__rtruediv__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::string text;
                        if (s.args.size() != 1u || !segment_text(s.args[0], text))
                            return (retain(not_implemented()), not_implemented());
                        return new_path(s.self, { (retain(s.args[0]), s.args[0]), (retain(s.self), s.self) });
                    } },
                { "__eq__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        parsed_path other;
                        std::string otherText;
                        if (s.args.size() != 1u || !is_pure_path(s.args[0]) || !self_path(s.args[0], other, &otherText))
                            return (retain(not_implemented()), not_implemented());
                        return make_bool(other.rules == s.path.rules && normcase(*s.path.rules, s.text) == normcase(*other.rules, otherText));
                    } },
                { "__hash__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        ref const text{ make_str(normcase(*s.path.rules, s.text)) };
                        long long h = 0;
                        return hash_of(text.get(), h) ? make_int(h) : nullptr;
                    } },
                { "parts", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::vector<object*> items;
                        for (auto const& p : path_parts(s.path))
                            items.push_back(make_str(p));
                        return make_tuple(std::move(items));
                    } },
                { "drive", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_str(s.path.drv) : nullptr; } },
                { "root", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_str(s.path.root) : nullptr; } },
                { "anchor", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_str(s.path.anchor()) : nullptr; } },
                { "name", [](std::vector<object*> const& aArgs, object*) -> object* { path_self s; return path_self_of(aArgs, s) ? make_str(s.path.name()) : nullptr; } },
                { "suffix", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        auto const name = s.path.name();
                        auto const i = name.rfind('.');
                        return make_str(i != std::string::npos && i > 0u && i + 1u < name.size() ? name.substr(i) : std::string{});
                    } },
                { "suffixes", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        auto name = s.path.name();
                        std::vector<object*> items;
                        if (name.empty() || name.back() == '.')
                            return make_list();
                        name.erase(0u, name.find_first_not_of('.') == std::string::npos ? name.size() : name.find_first_not_of('.'));
                        auto const parts = split_on(name, '.');
                        for (std::size_t i = 1u; i < parts.size(); ++i)
                            items.push_back(make_str("." + parts[i]));
                        return make_list(std::move(items));
                    } },
                { "stem", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        auto const name = s.path.name();
                        auto const i = name.rfind('.');
                        return make_str(i != std::string::npos && i > 0u && i + 1u < name.size() ? name.substr(0u, i) : name);
                    } },
                { "parent", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.path.tail.empty())
                            return (retain(s.self), s.self);
                        return path_from_parts(s.self, s.path, s.path.drv, s.path.root, { s.path.tail.begin(), s.path.tail.end() - 1 });
                    } },
                { "parents", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::vector<object*> items;
                        for (auto n = s.path.tail.size(); n > 0u; --n)
                        {
                            auto const p = path_from_parts(s.self, s.path, s.path.drv, s.path.root, { s.path.tail.begin(), s.path.tail.begin() + static_cast<std::ptrdiff_t>(n - 1u) });
                            if (p == nullptr)
                            {
                                for (auto x : items)
                                    release(x);
                                return nullptr;
                            }
                            items.push_back(p);
                        }
                        return make_tuple(std::move(items));
                    } },
                { "with_name", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.args.size() != 1u || kind_of(s.args[0]) != NEOS_PY_STR)
                            return error("TypeError", "with_name() argument must be str");
                        auto const& name = as<str_object>(s.args[0])->value;
                        if (s.path.name().empty())
                            return error("ValueError", path_repr(s.self, s.path) + " has an empty name");
                        if (name.empty() || name.find(s.path.rules->sep()) != std::string::npos || (s.path.rules->windows && name.find('/') != std::string::npos) || name == ".")
                            return error("ValueError", "Invalid name " + repr_string(name));
                        auto tail = s.path.tail;
                        tail.back() = name;
                        return path_from_parts(s.self, s.path, s.path.drv, s.path.root, tail);
                    } },
                { "with_stem", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.args.size() != 1u || kind_of(s.args[0]) != NEOS_PY_STR)
                            return error("TypeError", "with_stem() argument must be str");
                        auto const name = s.path.name();
                        auto const i = name.rfind('.');
                        auto const suffix = i != std::string::npos && i > 0u && i + 1u < name.size() ? name.substr(i) : std::string{};
                        auto const& stem = as<str_object>(s.args[0])->value;
                        if (stem.empty())
                            return error("ValueError", path_repr(s.self, s.path) + " has a non-empty suffix");
                        ref const newName{ make_str(stem + suffix) };
                        return call_method_named(s.self, "with_name", { (retain(newName.get()), newName.get()) });
                    } },
                { "with_suffix", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.args.size() != 1u || kind_of(s.args[0]) != NEOS_PY_STR)
                            return error("TypeError", "with_suffix() argument must be str");
                        auto const& suffix = as<str_object>(s.args[0])->value;
                        auto const name = s.path.name();
                        auto const i = name.rfind('.');
                        auto const stem = i != std::string::npos && i > 0u && i + 1u < name.size() ? name.substr(0u, i) : name;
                        if (stem.empty())
                            return error("ValueError", path_repr(s.self, s.path) + " has an empty name");
                        if (!suffix.empty() && (suffix[0] != '.' || suffix == "." || suffix.find(s.path.rules->sep()) != std::string::npos))
                            return error("ValueError", "Invalid suffix " + repr_string(suffix));
                        ref const newName{ make_str(stem + suffix) };
                        return call_method_named(s.self, "with_name", { (retain(newName.get()), newName.get()) });
                    } },
                { "relative_to", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("relative_to", s.args, aKwargs, { "other", "walk_up" }, 1u, 1u, a))
                            return nullptr;
                        bool const walkUp = a[1] != nullptr && truth(a[1]) == 1;
                        ref const other{ is_pure_path(a[0]) ? ref::borrowed(a[0]) : ref{ new_path(s.self, { (retain(a[0]), a[0]) }) } };
                        if (!other)
                            return nullptr;
                        parsed_path o;
                        std::string otherText;
                        if (!self_path(other.get(), o, &otherText))
                            return nullptr;
                        auto const& r = *s.path.rules;
                        auto const sameAnchor = normcase(r, o.anchor()) == normcase(r, s.path.anchor());
                        // (other, then its parents: the first that is self or one of self's parents)
                        for (std::size_t step = 0u; step <= o.tail.size(); ++step)
                        {
                            auto const length = o.tail.size() - step;
                            bool isPrefix = sameAnchor && length <= s.path.tail.size();
                            for (std::size_t i = 0u; isPrefix && i < length; ++i)
                                isPrefix = normcase(r, o.tail[i]) == normcase(r, s.path.tail[i]);
                            if (isPrefix)
                            {
                                std::vector<std::string> parts(step, "..");
                                parts.insert(parts.end(), s.path.tail.begin() + static_cast<std::ptrdiff_t>(length), s.path.tail.end());
                                return path_from_parts(s.self, s.path, "", "", parts);
                            }
                            if (!walkUp)
                                return error("ValueError", repr_string(s.text) + " is not in the subpath of " + repr_string(otherText));
                            if (length > 0u && o.tail[length - 1u] == "..")
                                return error("ValueError", "'..' segment in " + repr_string(otherText) + " cannot be walked");
                        }
                        return error("ValueError", repr_string(s.text) + " and " + repr_string(otherText) + " have different anchors");
                    } },
                { "is_relative_to", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.args.size() != 1u)
                            return error("TypeError", "is_relative_to() takes exactly one argument");
                        ref const other{ is_pure_path(s.args[0]) ? ref::borrowed(s.args[0]) : ref{ new_path(s.self, { (retain(s.args[0]), s.args[0]) }) } };
                        if (!other)
                            return nullptr;
                        parsed_path o;
                        if (!self_path(other.get(), o))
                            return nullptr;
                        auto const& r = *s.path.rules;
                        bool isPrefix = normcase(r, o.anchor()) == normcase(r, s.path.anchor()) && o.tail.size() <= s.path.tail.size();
                        for (std::size_t i = 0u; isPrefix && i < o.tail.size(); ++i)
                            isPrefix = normcase(r, o.tail[i]) == normcase(r, s.path.tail[i]);
                        return make_bool(isPrefix);
                    } },
                { "match", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("match", s.args, aKwargs, { "path_pattern", "case_sensitive" }, 1u, 1u, a))
                            return nullptr;
                        std::string patternText;
                        if (!segment_text(a[0], patternText))
                            return error("TypeError", std::string{ "argument should be a str or an os.PathLike object where __fspath__ returns a str, not " } + repr_string(type_name(a[0])));
                        auto const pattern = parse_path(*s.path.rules, patternText);
                        auto const patternParts = path_parts(pattern);
                        auto const parts = path_parts(s.path);
                        if (patternParts.empty())
                            return error("ValueError", "empty pattern");
                        bool caseSensitive = !s.path.rules->windows;
                        if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE)
                            caseSensitive = truth(a[1]) == 1;
                        if (parts.size() < patternParts.size())
                            return make_bool(false);
                        if (parts.size() > patternParts.size() && !pattern.anchor().empty())
                            return make_bool(false);
                        for (std::size_t i = 0u; i < patternParts.size(); ++i)
                            if (!wildcard_match(parts[parts.size() - 1u - i], patternParts[patternParts.size() - 1u - i], caseSensitive))
                                return make_bool(false);
                        return make_bool(true);
                    } },
                { "full_match", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("full_match", s.args, aKwargs, { "pattern", "case_sensitive" }, 1u, 1u, a))
                            return nullptr;
                        std::string patternText;
                        if (!segment_text(a[0], patternText))
                            return error("TypeError", std::string{ "argument should be a str or an os.PathLike object where __fspath__ returns a str, not " } + repr_string(type_name(a[0])));
                        bool caseSensitive = !s.path.rules->windows;
                        if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE)
                            caseSensitive = truth(a[1]) == 1;
                        return make_bool(match_segments(path_parts(s.path), 0u, path_parts(parse_path(*s.path.rules, patternText)), 0u, caseSensitive));
                    } }
            };

            // (ordering: the paths' parts; NotImplemented for other flavours)
            template <int Operation>
            object* path_order(std::vector<object*> const& aArgs, object*)
            {
                path_self s;
                if (!path_self_of(aArgs, s))
                    return nullptr;
                parsed_path other;
                std::string otherText;
                if (s.args.size() != 1u || !is_pure_path(s.args[0]) || !self_path(s.args[0], other, &otherText) || other.rules != s.path.rules)
                    return (retain(not_implemented()), not_implemented());
                auto const& r = *s.path.rules;
                auto const a = split_on(normcase(r, s.text), r.sep());
                auto const b = split_on(normcase(r, otherText), r.sep());
                auto const c = a < b ? -1 : b < a ? 1 : 0;
                switch (Operation)
                {
                case NEOS_PY_LT: return make_bool(c < 0);
                case NEOS_PY_LE: return make_bool(c <= 0);
                case NEOS_PY_GT: return make_bool(c > 0);
                default: return make_bool(c >= 0);
                }
            }

            builtin_entry const sPathOrderMethods[] =
            {
                { "__lt__", path_order<NEOS_PY_LT> }, { "__le__", path_order<NEOS_PY_LE> }, { "__gt__", path_order<NEOS_PY_GT> }, { "__ge__", path_order<NEOS_PY_GE> }
            };

            char const* const sPurePathProperties[] = { "parts", "drive", "root", "anchor", "name", "suffix", "suffixes", "stem", "parent", "parents" };

            // (a concrete path's os function: os.name(path, args...))
            object* path_os_call(path_self const& s, char const* aName, std::vector<object*> aExtra = {}, object* aKwargs = nullptr)
            {
                ref const path{ make_str(s.text) };
                std::vector<object*> args{ path.get() };
                args.insert(args.end(), aExtra.begin(), aExtra.end());
                return os_call(aName, args, aKwargs);
            }

            builtin_entry const sPathMethods[] =
            {
                { "cwd", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cwd{ make_str(current_directory()) };
                        ref const args{ make_tuple({ (retain(cwd.get()), cwd.get()) }) };
                        return call(aArgs[0], args.get(), nullptr);
                    } },
                { "home", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const home{ make_str(expanduser(sPlatform, "~")) };
                        ref const args{ make_tuple({ (retain(home.get()), home.get()) }) };
                        return call(aArgs[0], args.get(), nullptr);
                    } },
                { "stat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { path_self s; return path_self_of(aArgs, s) ? path_os_call(s, "stat", {}, aKwargs) : nullptr; } },
                { "lstat", [](std::vector<object*> const& aArgs, object*) -> object*
                    { path_self s; return path_self_of(aArgs, s) ? path_os_call(s, "lstat") : nullptr; } },
                { "exists", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("exists", s.args, aKwargs, { "follow_symlinks" }, 0u, 0u, a))
                            return nullptr;
                        return make_bool(exists(s.text, a[0] == nullptr || truth(a[0]) == 1));
                    } },
                { "is_file", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("is_file", s.args, aKwargs, { "follow_symlinks" }, 0u, 0u, a))
                            return nullptr;
                        return make_bool(is_type(s.text, kIFREG, a[0] == nullptr || truth(a[0]) == 1));
                    } },
                { "is_dir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("is_dir", s.args, aKwargs, { "follow_symlinks" }, 0u, 0u, a))
                            return nullptr;
                        return make_bool(is_type(s.text, kIFDIR, a[0] == nullptr || truth(a[0]) == 1));
                    } },
                { "is_symlink", [](std::vector<object*> const& aArgs, object*) -> object*
                    { path_self s; return path_self_of(aArgs, s) ? make_bool(is_type(s.text, kIFLNK, false)) : nullptr; } },
                { "is_junction", [](std::vector<object*> const& aArgs, object*) -> object*
                    { path_self s; return path_self_of(aArgs, s) ? make_bool(false) : nullptr; } },
                { "is_mount", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        ref const path{ make_str(s.text) };
                        for (auto const& f : sPathFunctions)
                            if (std::strcmp(f.name, "ismount") == 0)
                                return f.function(*s.path.rules, { path.get() }, nullptr);
                        return make_bool(false);
                    } },
                { "iterdir", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        ref const names{ path_os_call(s, "listdir") };
                        if (!names)
                            return nullptr;
                        std::vector<object*> children;
                        for (auto n : as<list_object>(names.get())->items)
                        {
                            auto child = s.path;
                            child.tail.push_back(as<str_object>(n)->value);
                            auto const p = new_path(s.self, { make_str(format_parts(child)) });
                            if (p == nullptr)
                            {
                                for (auto c : children)
                                    release(c);
                                return nullptr;
                            }
                            children.push_back(p);
                        }
                        ref const list{ make_list(std::move(children)) };
                        return iter(list.get());
                    } },
                { "glob", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("glob", s.args, aKwargs, { "pattern", "case_sensitive", "recurse_symlinks" }, 1u, 1u, a))
                            return nullptr;
                        std::string pattern;
                        if (!segment_text(a[0], pattern))
                            return error("TypeError", std::string{ "argument should be a str or an os.PathLike object where __fspath__ returns a str, not " } + repr_string(type_name(a[0])));
                        return glob_result(s.self, pattern, a[1], false);
                    } },
                { "rglob", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("rglob", s.args, aKwargs, { "pattern", "case_sensitive", "recurse_symlinks" }, 1u, 1u, a))
                            return nullptr;
                        std::string pattern;
                        if (!segment_text(a[0], pattern))
                            return error("TypeError", std::string{ "argument should be a str or an os.PathLike object where __fspath__ returns a str, not " } + repr_string(type_name(a[0])));
                        return glob_result(s.self, pattern, a[1], true);
                    } },
                { "walk", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("walk", s.args, aKwargs, { "top_down", "on_error", "follow_symlinks" }, 0u, 0u, a))
                            return nullptr;
                        ref const kwargs{ make_dict() };
                        for (auto const& kw : { std::pair{ "topdown", a[0] }, std::pair{ "onerror", a[1] }, std::pair{ "followlinks", a[2] } })
                            if (kw.second != nullptr)
                            {
                                ref const key{ make_str(kw.first) };
                                dict_set(kwargs.get(), key.get(), kw.second);
                            }
                        ref const walk{ path_os_call(s, "walk", {}, kwargs.get()) };
                        if (!walk)
                            return nullptr;
                        // (each (dirpath, dirnames, filenames): dirpath a path (dirnames: the same list (walk's pruning)))
                        std::vector<object*> items;
                        auto const convert = make_function("walk", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto const& args = as<tuple_object>(aArgs)->items;
                            auto const& t = as<tuple_object>(args[1])->items;
                            auto const p = new_path(args[0], { (retain(t[0]), t[0]) });
                            if (p == nullptr)
                                return nullptr;
                            return make_tuple({ p, (retain(t[1]), t[1]), (retain(t[2]), t[2]) });
                        }, nullptr, s.self);
                        ref const converter{ convert };
                        ref const map{ fn_builtin("map") };
                        ref const mapArgs{ make_tuple({ (retain(converter.get()), converter.get()), (retain(walk.get()), walk.get()) }) };
                        return call(map.get(), mapArgs.get(), nullptr);
                    } },
                { "mkdir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.mkdir"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("mkdir", s.args, aKwargs, { "mode", "parents", "exist_ok" }, 0u, 0u, a))
                            return nullptr;
                        long long mode = 0777;
                        if (a[0] != nullptr && !to_index(a[0], mode))
                            return nullptr;
                        bool const parents = a[1] != nullptr && truth(a[1]) == 1;
                        bool const existOk = a[2] != nullptr && truth(a[2]) == 1;
                        if (make_directory(s.text, mode) == 0)
                            return (retain(none()), none());
                        auto const e = errno;
                        if (e == ENOENT && parents && !s.path.tail.empty())
                        {
                            ref const parent{ getattr(s.self, "parent") };
                            if (!parent)
                                return nullptr;
                            ref const kwargs{ make_dict() };
                            ref const parentsKey{ make_str("parents") };
                            ref const existKey{ make_str("exist_ok") };
                            dict_set(kwargs.get(), parentsKey.get(), make_bool(true));
                            dict_set(kwargs.get(), existKey.get(), make_bool(true));
                            ref const method{ getattr(parent.get(), "mkdir") };
                            ref const empty{ make_tuple() };
                            if (!method)
                                return nullptr;
                            ref const r{ call(method.get(), empty.get(), kwargs.get()) };
                            if (!r)
                                return nullptr;
                            if (make_directory(s.text, mode) == 0)
                                return (retain(none()), none());
                            if (errno == EEXIST && existOk && is_type(s.text, kIFDIR))
                                return (retain(none()), none());
                            ref const filename{ make_str(s.text) };
                            return raise_os_error(errno, filename.get());
                        }
                        if (existOk && e != ENOENT && is_type(s.text, kIFDIR))
                            return (retain(none()), none());
                        ref const filename{ make_str(s.text) };
                        return raise_os_error(e, filename.get());
                    } },
                { "rmdir", [](std::vector<object*> const& aArgs, object*) -> object*
                    { path_self s; return path_self_of(aArgs, s) ? path_os_call(s, "rmdir") : nullptr; } },
                { "unlink", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.unlink"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("unlink", s.args, aKwargs, { "missing_ok" }, 0u, 0u, a))
                            return nullptr;
                        if (a[0] != nullptr && truth(a[0]) == 1 && !exists(s.text, false))
                            return (retain(none()), none());
                        return path_os_call(s, "remove");
                    } },
                { "rename", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.rename"))
                            return nullptr;
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.args.size() != 1u)
                            return error("TypeError", "rename() takes exactly one argument");
                        ref const r{ path_os_call(s, "rename", { s.args[0] }) };
                        if (!r)
                            return nullptr;
                        return new_path(s.self, { (retain(s.args[0]), s.args[0]) });
                    } },
                { "replace", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.replace"))
                            return nullptr;
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.args.size() != 1u)
                            return error("TypeError", "replace() takes exactly one argument");
                        ref const r{ path_os_call(s, "replace", { s.args[0] }) };
                        if (!r)
                            return nullptr;
                        return new_path(s.self, { (retain(s.args[0]), s.args[0]) });
                    } },
                { "touch", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.touch"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("touch", s.args, aKwargs, { "mode", "exist_ok" }, 0u, 0u, a))
                            return nullptr;
                        bool const existOk = a[1] == nullptr || truth(a[1]) == 1;
                        if (exists(s.text))
                        {
                            if (!existOk)
                            {
                                ref const filename{ make_str(s.text) };
                                return raise_os_error(EEXIST, filename.get());
                            }
                            return path_os_call(s, "utime", { none() });
                        }
                        ref const file{ path_open(s.self, "a", nullptr, nullptr, nullptr) };
                        if (!file)
                            return nullptr;
                        ref const closed{ call_method_named(file.get(), "close", {}) };
                        return closed ? (retain(none()), none()) : nullptr;
                    } },
                { "chmod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.chmod"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("chmod", s.args, aKwargs, { "mode", "follow_symlinks" }, 1u, 0u, a))
                            return nullptr;
                        return path_os_call(s, "chmod", { a[0] });
                    } },
                { "open", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "open() missing self");
                        return b_open(aArgs, aKwargs);
                    } },
                { "read_text", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("read_text", s.args, aKwargs, { "encoding", "errors", "newline" }, 0u, 0u, a))
                            return nullptr;
                        ref const file{ path_open(s.self, "r", a[0], a[1], a[2]) };
                        if (!file)
                            return nullptr;
                        ref const text{ call_method_named(file.get(), "read", {}) };
                        ref const closed{ call_method_named(file.get(), "close", {}) };
                        return text && closed ? (retain(text.get()), text.get()) : nullptr;
                    } },
                { "read_bytes", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        ref const file{ path_open(s.self, "rb", nullptr, nullptr, nullptr) };
                        if (!file)
                            return nullptr;
                        ref const data{ call_method_named(file.get(), "read", {}) };
                        ref const closed{ call_method_named(file.get(), "close", {}) };
                        return data && closed ? (retain(data.get()), data.get()) : nullptr;
                    } },
                { "write_text", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.write_text"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("write_text", s.args, aKwargs, { "data", "encoding", "errors", "newline" }, 1u, 0u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "data must be str, not " } + type_name(a[0]));
                        ref const file{ path_open(s.self, "w", a[1], a[2], a[3]) };
                        if (!file)
                            return nullptr;
                        ref const written{ call_method_named(file.get(), "write", { (retain(a[0]), a[0]) }) };
                        ref const closed{ call_method_named(file.get(), "close", {}) };
                        return written && closed ? (retain(written.get()), written.get()) : nullptr;
                    } },
                { "write_bytes", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.write_bytes"))
                            return nullptr;
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        std::string_view data;
                        if (s.args.size() != 1u || !bytes_like(s.args[0], data))
                            return error("TypeError", std::string{ "memoryview: a bytes-like object is required, not '" } + (s.args.empty() ? "NoneType" : type_name(s.args[0])) + "'");
                        ref const bytes{ make_bytes(std::string{ data }) };
                        ref const file{ path_open(s.self, "wb", nullptr, nullptr, nullptr) };
                        if (!file)
                            return nullptr;
                        ref const written{ call_method_named(file.get(), "write", { (retain(bytes.get()), bytes.get()) }) };
                        ref const closed{ call_method_named(file.get(), "close", {}) };
                        return written && closed ? (retain(written.get()), written.get()) : nullptr;
                    } },
                { "absolute", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (!s.path.root.empty() && (!s.path.rules->windows || !s.path.drv.empty()))
                            return (retain(s.self), s.self);
                        if (s.path.drv.empty() && s.path.root.empty() && s.path.tail.empty())
                            return new_path(s.self, { make_str(current_directory()) });
                        return new_path(s.self, { make_str(current_directory()), (retain(s.self), s.self) });
                    } },
                { "resolve", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("resolve", s.args, aKwargs, { "strict" }, 0u, 0u, a))
                            return nullptr;
                        if (a[0] != nullptr && truth(a[0]) == 1 && !exists(s.text))
                        {
                            ref const filename{ make_str(s.text) };
                            return raise_os_error(ENOENT, filename.get());
                        }
                        std::error_code ec;
                        auto const absolute = abspath(sPlatform, s.text);
                        auto resolved = std::filesystem::weakly_canonical(std::filesystem::path{ absolute }, ec);
                        std::string text = ec ? absolute : resolved.string();
                        if (text.size() > 1u && sPlatform.is_sep(text.back()))
                            text.pop_back();
                        return new_path(s.self, { make_str(text) });
                    } },
                { "expanduser", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.path.drv.empty() && s.path.root.empty() && !s.path.tail.empty() && s.path.tail[0].starts_with("~"))
                        {
                            auto const home = expanduser(*s.path.rules, s.path.tail[0]);
                            if (home.starts_with("~"))
                                return error("RuntimeError", "Could not determine home directory.");
                            auto expanded = parse_path(*s.path.rules, home);
                            expanded.tail.insert(expanded.tail.end(), s.path.tail.begin() + 1, s.path.tail.end());
                            return new_path(s.self, { make_str(format_parts(expanded)) });
                        }
                        return (retain(s.self), s.self);
                    } },
                { "samefile", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        path_self s;
                        if (!path_self_of(aArgs, s))
                            return nullptr;
                        if (s.args.size() != 1u)
                            return error("TypeError", "samefile() takes exactly one argument");
                        ref const a{ path_os_call(s, "stat") };
                        if (!a)
                            return nullptr;
                        ref const b{ os_call("stat", { s.args[0] }) };
                        if (!b)
                            return nullptr;
                        ref const ia{ getattr(a.get(), "st_ino") }, ib{ getattr(b.get(), "st_ino") }, da{ getattr(a.get(), "st_dev") }, db{ getattr(b.get(), "st_dev") };
                        return make_bool(equal(ia.get(), ib.get()) == 1 && equal(da.get(), db.get()) == 1);
                    } }
            };

            bool pathlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (sPurePathClass == nullptr)
                {
                    if (!is_exception_class("pathlib.UnsupportedOperation"))
                        user_exception_bases()["pathlib.UnsupportedOperation"] = { "NotImplementedError" };
                    sNtpathModule = fn_import("ntpath");
                    ref const posixpath{ fn_import("posixpath") };
                    if (sNtpathModule == nullptr || !posixpath)
                        return false;
                    auto const platformParser = sPlatform.windows ? sNtpathModule : posixpath.get();
                    std::vector<std::pair<std::string, object*>> ns;
                    for (auto const& m : sPurePathMethods)
                    {
                        auto const function = make_builtin(m);
                        bool property = false;
                        for (auto const p : sPurePathProperties)
                            if (std::strcmp(p, m.name) == 0)
                                property = true;
                        if (property)
                        {
                            ns.emplace_back(m.name, b_property({ function }, nullptr));
                            release(function);
                        }
                        else if (std::strcmp(m.name, "__new__") == 0)
                        {
                            ns.emplace_back(m.name, b_staticmethod({ function }, nullptr));
                            release(function);
                        }
                        else
                            ns.emplace_back(m.name, function);
                    }
                    for (auto const& m : sPathOrderMethods)
                        ns.emplace_back(m.name, make_builtin(m));
                    ns.emplace_back("parser", (retain(platformParser), platformParser));
                    auto const release_namespace = [](std::vector<std::pair<std::string, object*>>& aNamespace)
                    {
                        for (auto& n : aNamespace)
                            release(n.second);
                        aNamespace.clear();
                    };
                    sPurePathClass = make_class("pathlib", "PurePath", {}, ns);
                    release_namespace(ns);
                    if (sPurePathClass == nullptr)
                        return false;
                    ns.emplace_back("parser", (retain(posixpath.get()), posixpath.get()));
                    sPurePosixPathClass = make_class("pathlib", "PurePosixPath", { sPurePathClass }, ns);
                    release_namespace(ns);
                    ns.emplace_back("parser", (retain(sNtpathModule), sNtpathModule));
                    sPureWindowsPathClass = make_class("pathlib", "PureWindowsPath", { sPurePathClass }, ns);
                    release_namespace(ns);
                    for (auto const& m : sPathMethods)
                    {
                        auto const function = make_builtin(m);
                        if (std::strcmp(m.name, "cwd") == 0 || std::strcmp(m.name, "home") == 0)
                        {
                            ns.emplace_back(m.name, b_classmethod({ function }, nullptr));
                            release(function);
                        }
                        else
                            ns.emplace_back(m.name, function);
                    }
                    sPathClass = make_class("pathlib", "Path", { sPurePathClass }, ns);
                    release_namespace(ns);
                    if (sPurePosixPathClass == nullptr || sPureWindowsPathClass == nullptr || sPathClass == nullptr)
                        return false;
                    sPosixPathClass = make_class("pathlib", "PosixPath", { sPathClass, sPurePosixPathClass }, ns);
                    sWindowsPathClass = make_class("pathlib", "WindowsPath", { sPathClass, sPureWindowsPathClass }, ns);
                    if (sPosixPathClass == nullptr || sWindowsPathClass == nullptr)
                        return false;
                }
                for (auto const& c : { std::pair{ "PurePath", sPurePathClass }, std::pair{ "PurePosixPath", sPurePosixPathClass }, 
                    std::pair{ "PureWindowsPath", sPureWindowsPathClass }, std::pair{ "Path", sPathClass }, std::pair{ "PosixPath", sPosixPathClass }, 
                    std::pair{ "WindowsPath", sWindowsPathClass } })
                    aAttributes.emplace_back(c.first, (retain(c.second), c.second));
                aAttributes.emplace_back("UnsupportedOperation", exception_class("pathlib.UnsupportedOperation"));
                return true;
            }

            inline bool const sOs = register_module("os", os_module);
#if defined(_WIN32)
            inline bool const sPosixModule = register_module("nt", os_module);
#else
            inline bool const sPosixModule = register_module("posix", os_module);
#endif
            inline bool const sPathlib = register_module("pathlib", pathlib_module);
            inline bool const sErrno = register_module("errno", errno_module);
            inline bool const sPosixPath = register_module("posixpath", posixpath_module);
            inline bool const sNtPath = register_module("ntpath", ntpath_module);
            inline bool const sStat = register_module("stat", stat_module);
            inline bool const sGenericPath = register_module("genericpath", genericpath_module);
        }
    }
}
