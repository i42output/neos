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

  Portions of this file are derived from CPython 3.13 (Lib/posixpath.py, Lib/ntpath.py, Lib/genericpath.py,
  Modules/errnomodule.c, Lib/os.py (PathLike, _Environ, the exec and spawn functions, renames, _exists and _get_exports_list),
  Modules/posixmodule.c (execv's and execve's arguments, the uid and gid converters, pathconf, fpathconf and confstr and their
  names; the POSIX functions after sched_get_priority_max (abort ... pidfd_open, the xattr and timerfd functions) and the
  constants), Modules/resource.c (the resource module), Lib/stat.py (UF_*, SF_*, FILE_ATTRIBUTE_* and _filemode_table) and Lib/pathlib (_local.py
  and _abc.py: Path's lchmod, symlink_to, hardlink_to, readlink, is_block_device, is_char_device, is_fifo, is_socket, owner,
  group and from_uri)), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the
  Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++
  for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules os, os.path (posixpath and ntpath: CPython 3.13's algorithms) and stat

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
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
#include <spawn.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sched.h>
#include <climits>
#include <cstdio>
#include <dlfcn.h>
#include <sysexits.h>
#if defined(__linux__)
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <sys/sendfile.h>
#include <sys/xattr.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/timerfd.h>
#include <linux/limits.h>
#if __has_include(<linux/memfd.h>)
#include <linux/memfd.h>
#endif
#endif
#endif
#include <fcntl.h>
#include "objects.hpp"
#include "values.hpp"

#if defined(_WIN32)
extern "C" char** _environ;
#else
extern "C" char** environ;
#endif

namespace neos::python_runtime
{
    namespace detail
    {
        // (structseq.cpp)
        using structseq_constructor = object* (*)(structseq_descriptor const& aDescriptor, object* aArgs, object* aKwargs);
        void set_structseq_constructor(structseq_descriptor const& aDescriptor, structseq_constructor aConstructor, bool aItemArguments);
        object* structseq_construct(structseq_descriptor const& aDescriptor, object* aArgs, object* aKwargs);
        object* structseq_field(object* aObject, std::size_t aField);
        void structseq_set_field(object* aObject, std::size_t aField, object* aValue);

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
                        // (posix.environ's: bytes (os.environ decodes them); nt.environ's: str)
#if defined(_WIN32)
                        ref const key{ make_str(std::string{ entry.substr(0u, equals) }) };
                        ref const value{ make_str(std::string{ entry.substr(equals + 1u) }) };
#else
                        ref const key{ make_bytes(std::string{ entry.substr(0u, equals) }) };
                        ref const value{ make_bytes(std::string{ entry.substr(equals + 1u) }) };
#endif
                        dict_set(sEnviron, key.get(), value.get());
                    }
                }
                retain(sEnviron);
                return sEnviron;
            }

            std::optional<std::string> environment_value(std::string const& aName)
            {
                ref const env{ environment() };
#if defined(_WIN32)
                ref const key{ make_str(aName) };
#else
                ref const key{ make_bytes(aName) };
#endif
                auto const position = dict_find(env.get(), key.get());
                if (position < 0)
                    return {};
                auto const item = as<dict_object>(env.get())->items[static_cast<std::size_t>(position)].second;
                std::string value;
                std::string_view bytes;
                if (bytes_like(item, bytes))
                    value = std::string{ bytes };
                else
                    str_of(item, value);
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
                long long blksize = 0, blocks = 0, rdev = 0;    ///< (POSIX's: st_blksize, st_blocks and st_rdev)
            };

            // (a dir_fd argument: AT_FDCWD if it is absent or None (the current directory); NotImplementedError where the platform has no
            // *at functions (Windows: os.supports_dir_fd is empty))
            bool dir_fd_of(object* aValue, int& aResult)
            {
#if defined(_WIN32)
                aResult = -1;
#else
                aResult = AT_FDCWD;
#endif
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                    return true;
                if (!is_int_like(aValue) && (python_type(aValue) == nullptr || python_type(aValue)->index == nullptr))
                {
                    error("TypeError", std::string{ "argument should be integer or None, not " } + type_name(aValue));
                    return false;
                }
#if defined(_WIN32)
                error("NotImplementedError", "dir_fd unavailable on this platform");
                return false;
#else
                long long fd = 0;
                if (!to_index(aValue, fd))
                    return false;
                if (fd < std::numeric_limits<int>::min() || fd > std::numeric_limits<int>::max())
                {
                    error("OverflowError", "fd is greater than maximum");
                    return false;
                }
                aResult = static_cast<int>(fd);
                return true;
#endif
            }

            // (a follow_symlinks argument: true if it is absent)
            bool follow_of(object* aValue)
            {
                return aValue == nullptr || truth(aValue) == 1;
            }

            // (follow_symlinks=False where the platform cannot: NotImplementedError, as CPython's follow_symlinks_specified)
            object* follow_symlinks_unavailable(char const* aFunction)
            {
                return error("NotImplementedError", std::string{ aFunction } + ": follow_symlinks unavailable on this platform");
            }

#if defined(_WIN32)
            constexpr int kCurrentDirectoryFd = -1;
#else
            constexpr int kCurrentDirectoryFd = AT_FDCWD;
#endif
            bool do_stat(std::string const& aPath, bool aFollow, stat_values& aResult, int& aErrno, int aDirFd = kCurrentDirectoryFd)
            {
#if defined(_WIN32)
                (void)aFollow;
                (void)aDirFd;
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
                if (::fstatat(aDirFd, aPath.c_str(), &st, aFollow ? 0 : AT_SYMLINK_NOFOLLOW) != 0)
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
#if !defined(_WIN32)
                aResult.blksize = static_cast<long long>(st.st_blksize);
                aResult.blocks = static_cast<long long>(st.st_blocks);
                aResult.rdev = static_cast<long long>(st.st_rdev);
#endif
                aResult.atime = static_cast<double>(aResult.atime_ns) / 1e9;
                aResult.mtime = static_cast<double>(aResult.mtime_ns) / 1e9;
                aResult.ctime = static_cast<double>(aResult.ctime_ns) / 1e9;
                return true;
            }

            // (the struct sequences' types (made once: also the module's attributes, e.g. os.stat_result); os.stat_result: items 7 to 9 are the integer times (unnamed: repr shows them as st_atime etc.); the attributes are the floats (the 
            // later fields))
            structseq_descriptor const& stat_result_type()
            {
                // (POSIX's: also st_blksize, st_blocks and st_rdev (as CPython's on Linux))
                return structseq_type("os.stat_result", { "st_mode", "st_ino", "st_dev", "st_nlink", "st_uid", "st_gid", "st_size", "st_atime", "st_mtime", 
                    "st_ctime", "st_atime", "st_mtime", "st_ctime", "st_atime_ns", "st_mtime_ns", "st_ctime_ns"
#if !defined(_WIN32)
                    , "st_blksize", "st_blocks", "st_rdev"
#endif
                    }, 10u);
            }
            structseq_descriptor const& terminal_size_type() { return structseq_type("os.terminal_size", { "columns", "lines" }, 2u); }
            structseq_descriptor const& times_result_type()
            {
                return structseq_type("posix.times_result", { "user", "system", "children_user", "children_system", "elapsed" }, 5u);
            }
            structseq_descriptor const& uname_result_type()
            {
                return structseq_type("posix.uname_result", { "sysname", "nodename", "release", "version", "machine" }, 5u);
            }
            structseq_descriptor const& statvfs_result_type()
            {
                return structseq_type("os.statvfs_result", { "f_bsize", "f_frsize", "f_blocks", "f_bfree", "f_bavail", "f_files", "f_ffree", "f_favail",
                    "f_flag", "f_namemax", "f_fsid" }, 10u);
            }

            object* stat_result(stat_values const& s)
            {
                // (items 7 to 9 are the integer times, which repr shows as st_atime etc.; the attributes are the floats (the later fields))
                auto const& t = stat_result_type();
                return make_structseq(t, { make_int(s.mode), make_int(s.ino), make_int(s.dev), make_int(s.nlink), make_int(s.uid), make_int(s.gid),
                    make_int(s.size), make_int(s.atime_ns / 1000000000ll), make_int(s.mtime_ns / 1000000000ll), make_int(s.ctime_ns / 1000000000ll),
                    make_float(s.atime), make_float(s.mtime), make_float(s.ctime), make_int(s.atime_ns), make_int(s.mtime_ns), make_int(s.ctime_ns)
#if !defined(_WIN32)
                    , make_int(s.blksize), make_int(s.blocks), make_int(s.rdev)
#endif
                    });
            }

            constexpr long long kIFMT = 0170000, kIFDIR = 0040000, kIFREG = 0100000, kIFLNK = 0120000;

            // (a path argument: str, bytes or os.PathLike)
            // (a path argument (CPython's path_converter): a str or bytes (or a subclass's instance), or an os.PathLike's __fspath__() (a str or 
            // bytes); aInteger: the function also takes a file descriptor (its message says so))
            // aNul: no NUL (os.path's functions are of the text (a NUL is checked by the os functions that they call))
            bool path_argument(object* aValue, std::string& aResult, char const* aFunction, char const* aName = "path", bool aInteger = false, bool aNul = true)
            {
                // (no NUL: CPython's path_converter ("stat: embedded null character in path"), or its FSConverter's for an argument that is not 
                // a path (exec's argv and env, putenv's: "embedded null byte"))
                auto const nul_free = [&]() -> bool
                {
                    if (!aNul || aResult.find('\0') == std::string::npos)
                        return true;
                    std::string_view const name{ aName };
                    if (name == "argv" || name == "env" || name == "username")
                        error("ValueError", "embedded null byte");
                    else
                        error("ValueError", std::string{ aFunction } + ": embedded null character in " + aName);
                    return false;
                };
                auto const value = builtin_value(aValue);
                if (kind_of(value) == NEOS_PY_STR)
                {
                    aResult = as<str_object>(value)->value;
                    return nul_free();
                }
                std::string_view bytes;
                if (bytes_like(value, bytes))
                {
                    aResult = std::string{ bytes };
                    return nul_free();
                }
                ref const fspath{ getattr(aValue, "__fspath__") };
                if (fspath)
                {
                    ref const empty{ make_tuple() };
                    ref const p{ call(fspath.get(), empty.get(), nullptr) };
                    if (!p)
                        return false;
                    auto const result = builtin_value(p.get());
                    if (kind_of(result) != NEOS_PY_STR && std::string_view{ type_name(result) } != "bytes")
                    {
                        error("TypeError", std::string{ "expected " } + type_name(aValue) + ".__fspath__() to return str or bytes, not " + type_name(p.get()));
                        return false;
                    }
                    return path_argument(result, aResult, aFunction, aName, false, aNul);
                }
                clear_error();
                error("TypeError", std::string{ aFunction } + ": " + aName + " should be string, bytes" + (aInteger ? ", os.PathLike or integer" : " or os.PathLike") + 
                    ", not " + type_name(aValue));
                return false;
            }

            bool exists(std::string const& aPath, bool aFollow = true)
            {
                stat_values s;
                int e = 0;
                return do_stat(aPath, aFollow, s, e);
            }

            bool is_type(std::string const& aPath, long long aType, bool aFollow = true, int aDirFd = kCurrentDirectoryFd)
            {
                stat_values s;
                int e = 0;
                return do_stat(aPath, aFollow, s, e, aDirFd) && (s.mode & kIFMT) == aType;
            }

            // --- os.path functions

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
                if (!path_argument(a[0], aPath, aName, "p", false, false))
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
                        if (!path_argument(aArgs[0], first, "join", "a", false, false))
                        {
                            clear_error();
                            return error("TypeError", std::string{ "expected str, bytes or os.PathLike object, not " } + type_name(aArgs[0]));
                        }
                        std::vector<std::string> rest;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                        {
                            std::string p;
                            if (!path_argument(aArgs[i], p, "join", "path", false, false))
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
                        if (!path_argument(a[0], p, "realpath", "filename", false, false))
                            return nullptr;
#if !defined(_WIN32)
                        // (ntpath's (not on Windows): abspath's)
                        if (r.windows)
                            return path_result(aArgs, abspath(r, p));
#endif
                        // (a NUL: lstat's error (posixpath's realpath calls it for each part))
                        if (p.find('\0') != std::string::npos)
                            return error("ValueError", "lstat: embedded null character in path");
                        // (strict: the path must exist (OSError); os.path.ALLOW_MISSING: its missing parts are kept, but other errors raise)
                        bool allowMissing = false;
                        bool strict = false;
                        if (a[1] != nullptr)
                        {
                            ref const generic{ fn_import("genericpath") };
                            ref const allow{ generic ? getattr(generic.get(), "ALLOW_MISSING") : nullptr };
                            if (!allow)
                                return nullptr;
                            allowMissing = a[1] == allow.get();
                            if (!allowMissing)
                            {
                                auto const t = truth(a[1]);
                                if (t < 0)
                                    return nullptr;
                                strict = t == 1;
                            }
                        }
                        std::error_code ec;
                        auto const absolute = std::filesystem::path{ abspath(r, p) };
                        if (strict)
                        {
                            auto const resolved = std::filesystem::canonical(absolute, ec);
                            if (ec)
                                return raise_os_error(ec.value(), a[0]);
                            return path_result(aArgs, normpath(r, resolved.string()));
                        }
                        auto const resolved = std::filesystem::weakly_canonical(absolute, ec);
                        if (ec && allowMissing && ec != std::errc::no_such_file_or_directory)
                            return raise_os_error(ec.value(), a[0]);
                        return path_result(aArgs, ec ? abspath(r, p) : normpath(r, resolved.string()));
                    } },
                { "relpath", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("relpath", aArgs, aKwargs, { "path", "start" }, 1u, 0u, a))
                            return nullptr;
                        std::string path, start = ".";
                        if (!path_argument(a[0], path, "relpath", "path", false, false) || (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE && 
                            !path_argument(a[1], start, "relpath", "start", false, false)))
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
                            if (!path_argument(i, p, "commonpath", "path", false, false))
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
                        // (a NUL: os.stat's ValueError: False)
                        return make_bool(p.find('\0') == std::string::npos && exists(p));
                    } },
                { "lexists", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "lexists", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        // (a NUL: os.stat's ValueError: False)
                        return make_bool(p.find('\0') == std::string::npos && exists(p, false));
                    } },
                { "isfile", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "isfile", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        // (a NUL: os.stat's ValueError: False)
                        return make_bool(p.find('\0') == std::string::npos && is_type(p, kIFREG));
                    } },
                { "isdir", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "isdir", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        // (a NUL: os.stat's ValueError: False)
                        return make_bool(p.find('\0') == std::string::npos && is_type(p, kIFDIR));
                    } },
                { "islink", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "islink", p))
                        {
                            clear_error();
                            return make_bool(false);
                        }
                        // (a NUL: os.stat's ValueError: False)
                        return make_bool(p.find('\0') == std::string::npos && is_type(p, kIFLNK, false));
                    } },
                { "ismount", [](path_rules const& r, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "ismount", p))
                            return nullptr;
                        if (!r.windows)
                        {
                            // (posixpath.ismount: path/.. on a different device from path, or the same i-node (a NUL, or a path that does not 
                            // exist, or a symbolic link: not a mount point))
                            stat_values s1, s2;
                            int e = 0;
                            if (p.find('\0') != std::string::npos || !do_stat(p, false, s1, e) || (s1.mode & kIFMT) == kIFLNK)
                                return make_bool(false);
                            auto parent = join(r, p, std::vector<std::string>{ ".." });
                            if (!do_stat(parent, false, s2, e))
                            {
                                std::error_code ec;
                                auto const resolved = std::filesystem::weakly_canonical(std::filesystem::path{ abspath(r, parent) }, ec);
                                if (ec || !do_stat(normpath(r, resolved.string()), false, s2, e))
                                    return make_bool(false);
                            }
                            return make_bool(s1.dev != s2.dev || s1.ino == s2.ino);
                        }
                        auto const a = abspath(r, p);
                        auto const s = splitroot(r, a);
                        return make_bool(!s.root.empty() && s.tail.empty());
                    } },
                { "getsize", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::string p;
                        if (!one_path(aArgs, aKwargs, "getsize", p))
                            return nullptr;
                        if (p.find('\0') != std::string::npos)
                            return error("ValueError", "stat: embedded null character in path");
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
                        if (p.find('\0') != std::string::npos)
                            return error("ValueError", "stat: embedded null character in path");
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
                        if (p.find('\0') != std::string::npos)
                            return error("ValueError", "stat: embedded null character in path");
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
                        if (p.find('\0') != std::string::npos)
                            return error("ValueError", "stat: embedded null character in path");
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
                        if (!path_argument(a[0], p1, "samefile", "path", false, false) || !path_argument(a[1], p2, "samefile", "path", false, false))
                            return nullptr;
                        // (os.stat of each: a NUL is its error)
                        stat_values s1, s2;
                        int e = 0;
                        if (p1.find('\0') != std::string::npos)
                            return error("ValueError", "stat: embedded null character in path");
                        if (!do_stat(p1, true, s1, e))
                            return raise_os_error(e, a[0]);
                        if (p2.find('\0') != std::string::npos)
                            return error("ValueError", "stat: embedded null character in path");
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
                        if (!parse_arguments("isjunction", aArgs, aKwargs, { "path" }, 1u, 1u, a) || !path_argument(a[0], p, "isjunction", "path", false, false))
                            return nullptr;
                        return make_bool(false);
                    } },
                { "isdevdrive", [](path_rules const&, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("isdevdrive", aArgs, aKwargs, { "path" }, 1u, 1u, a) || !path_argument(a[0], p, "isdevdrive", "path", false, false))
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

            // (posixpath's and ntpath's: genericpath's ALLOW_MISSING)
            bool allow_missing(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const generic{ fn_import("genericpath") };
                if (!generic)
                    return false;
                auto const value = getattr(generic.get(), "ALLOW_MISSING");
                if (value == nullptr)
                    return false;
                aAttributes.emplace_back("ALLOW_MISSING", value);
                return true;
            }
            bool posixpath_module(std::vector<std::pair<std::string, object*>>& aAttributes) { return path_module(aAttributes, sPosix) && allow_missing(aAttributes); }

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
            bool ntpath_module(std::vector<std::pair<std::string, object*>>& aAttributes) { return path_module(aAttributes, sWindows) && allow_missing(aAttributes); }

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
                int dirFd = kCurrentDirectoryFd;    ///< (scandir(fd)'s: its directory's descriptor (its path is its name, as CPython's))
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
                case 0: return make_bool(is_type(e.path, kIFDIR, follow, e.dirFd));
                case 1: return make_bool(is_type(e.path, kIFREG, follow, e.dirFd));
                case 2: return make_bool(is_type(e.path, kIFLNK, false, e.dirFd));
                case 3:
                    {
                        stat_values s;
                        int err = 0;
                        if (!do_stat(e.path, follow, s, err, e.dirFd))
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
                        do_stat(e.path, false, s, err, e.dirFd);
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
            bool directory_entries(std::string const& aPath, std::vector<std::string>& aNames, int& aErrno);

#if !defined(_WIN32)
            // (a directory's names from a descriptor of it (as CPython's listdir(fd): a duplicate of it, fdopendir and rewinddir; the 
            // descriptor stays open))
            bool directory_entries(int aFd, std::vector<std::string>& aNames, int& aErrno)
            {
                auto const fd = ::dup(aFd);
                if (fd < 0)
                {
                    aErrno = errno;
                    return false;
                }
                DIR* const d = ::fdopendir(fd);
                if (d == nullptr)
                {
                    aErrno = errno;
                    ::close(fd);
                    return false;
                }
                ::rewinddir(d);
                errno = 0;
                while (auto const* entry = ::readdir(d))
                {
                    std::string const name = entry->d_name;
                    if (name != "." && name != "..")
                        aNames.push_back(name);
                }
                aErrno = errno;
                ::rewinddir(d);
                ::closedir(d);
                return aErrno == 0;
            }
#endif

            bool directory_entries(std::string const& aPath, std::vector<std::string>& aNames, int& aErrno)
            {
#if defined(_WIN32)
                std::error_code ec;
                std::string const path = aPath.empty() ? "." : aPath;
                std::filesystem::directory_iterator it{ std::filesystem::path{ std::u8string_view{ reinterpret_cast<char8_t const*>(path.data()), path.size() } }, ec };
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

            object* fd_stat(int aFd, object* aName);

            // (a path argument of a function that also takes a file descriptor (CPython's path_t with allow_fd: POSIX's, as CPython's supports_fd):
            // an integer (an object with __index__; a bool warns that it is used as a file descriptor, as _PyLong_FileDescriptor_Converter's): 
            // aFd; otherwise aFd is empty and aPath the path)
            bool path_or_fd_of(std::vector<object*> const& a, std::size_t aIndex, std::string& aPath, std::optional<int>& aFd, char const* aFunction)
            {
                aFd.reset();
#if !defined(_WIN32)
                auto const value = a[aIndex];
                bool integer = is_int_like(value);
                if (!integer && kind_of(builtin_value(value)) != NEOS_PY_STR && std::string_view{ type_name(builtin_value(value)) } != "bytes")
                {
                    ref const index{ getattr(value, "__index__") };
                    integer = static_cast<bool>(index);
                    clear_error();
                }
                if (integer)
                {
                    if (kind_of(value) == NEOS_PY_BOOL)
                    {
                        ref const warn{ module_attribute("warnings", "warn") };
                        ref category{ fn_builtin("RuntimeWarning") };
                        if (!warn || !category || !ref{ invoke(warn.get(), { make_str("bool is used as a file descriptor"), category.release_ownership() }) })
                            return false;
                    }
                    long long fd = 0;
                    if (!to_index(value, fd))
                        return false;
                    if (fd > std::numeric_limits<int>::max() || fd < std::numeric_limits<int>::min())
                    {
                        error("OverflowError", fd > 0 ? "fd is greater than maximum" : "fd is less than minimum");
                        return false;
                    }
                    aFd = static_cast<int>(fd);
                    return true;
                }
#endif
                return path_argument(a[aIndex], aPath, aFunction, "path", 
#if defined(_WIN32)
                    false);
#else
                    true);
#endif
            }

            // (a file descriptor with dir_fd or follow_symlinks=False: CPython's dir_fd_and_fd_invalid and fd_and_follow_symlinks_invalid)
            bool fd_alone(char const* aFunction, std::optional<int> const& aFd, int aDirFd, bool aFollow)
            {
                if (!aFd)
                    return true;
                if (aDirFd != kCurrentDirectoryFd)
                {
                    error("ValueError", std::string{ aFunction } + ": can't specify dir_fd without matching path");
                    return false;
                }
                if (!aFollow)
                {
                    error("ValueError", std::string{ aFunction } + ": cannot use fd and follow_symlinks together");
                    return false;
                }
                return true;
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

#if !defined(_WIN32)
            // (exec's argv: each item a path (str, bytes or os.PathLike, as CPython's fsconverter))
            bool exec_arguments(object* aArgv, std::vector<std::string>& aResult, char const* aFunction)
            {
                std::vector<object*> items;
                if (!collect(aArgv, items))
                    return false;
                bool ok = true;
                for (auto i : items)
                {
                    std::string text;
                    if (ok)
                        ok = path_argument(i, text, aFunction, "argv");
                    release(i);
                    if (ok)
                        aResult.push_back(std::move(text));
                }
                return ok;
            }
#endif

            // (a function of no arguments (CPython's METH_NOARGS): TypeError if it is given any, its name the platform module's 
            // ("posix.getpid() takes no arguments (1 given)"))
            bool no_arguments(char const* aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
#if defined(_WIN32)
                std::string const name = std::string{ "nt." } + aName;
#else
                std::string const name = std::string{ "posix." } + aName;
#endif
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                {
                    error("TypeError", name + "() takes no keyword arguments");
                    return false;
                }
                if (!aArgs.empty())
                {
                    error("TypeError", name + "() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                    return false;
                }
                return true;
            }

            // (remove and unlink (the same function, by its name))
            object* remove_path(char const* aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (!host_unrestricted("os.remove"))
                    return nullptr;
                std::vector<object*> a;
                std::string p;
                int dirFd = 0;
                if (!parse_arguments(aName, aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, aName) || !dir_fd_of(a[1], dirFd))
                    return nullptr;
#if !defined(_WIN32)
                if (::unlinkat(dirFd, p.c_str(), 0) != 0)
                    return raise_os_error(errno, a[0]);
                return none();
#endif
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
            }

            builtin_entry const sOsFunctions[] =
            {
                { "getcwd", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getcwd", aArgs, aKwargs)) return nullptr; return make_str(current_directory()); } },
                { "getcwdb", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getcwdb", aArgs, aKwargs)) return nullptr; return make_bytes(current_directory()); } },
                { "chdir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.chdir"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        std::optional<int> fd;
                        if (!parse_arguments("chdir", aArgs, aKwargs, { "path" }, 1u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "chdir"))
                            return nullptr;
#if !defined(_WIN32)
                        if (fd)
                            return ::fchdir(*fd) != 0 ? raise_os_error(errno, a[0]) : none();
#endif
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
                        std::optional<int> fd;
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                        {
                            if (!path_or_fd_of(a, 0u, p, fd, "listdir"))
                                return nullptr;
                            bytes = !fd && bytes_like(builtin_value(a[0]), b);
                        }
                        std::vector<std::string> names;
                        int e = 0;
#if !defined(_WIN32)
                        // (a descriptor's: str names; its error has no file name, as CPython's)
                        if (fd)
                        {
                            if (!directory_entries(*fd, names, e))
                                return raise_os_error(e);
                            std::vector<object*> items;
                            for (auto const& n : names)
                                items.push_back(make_str(n));
                            return make_list(std::move(items));
                        }
#endif
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
                        std::optional<int> fd;
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                        {
                            if (!path_or_fd_of(a, 0u, p, fd, "scandir"))
                                return nullptr;
                            bytes = !fd && bytes_like(builtin_value(a[0]), b);
                        }
                        std::vector<std::string> names;
                        int e = 0;
#if !defined(_WIN32)
                        // (a descriptor's: entries whose paths are their names, of the directory's descriptor)
                        if (fd)
                        {
                            if (!directory_entries(*fd, names, e))
                                return raise_os_error(e, a[0]);
                            std::vector<object*> entries;
                            for (auto const& n : names)
                            {
                                auto const entry = new dir_entry_object{ { { &sDirEntryType.base } }, n, n, false, *fd };
                                entries.push_back(&entry->h.base);
                            }
                            return &(new scandir_object{ { { &sScandirType.base } }, make_list(std::move(entries)) })->h.base;
                        }
#endif
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
                        int dirFd = 0;
                        if (!parse_arguments("mkdir", aArgs, aKwargs, { "path", "mode", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "mkdir") || !dir_fd_of(a[2], dirFd))
                            return nullptr;
                        long long mode = 0777;
                        if (a[1] != nullptr && !to_index(a[1], mode))
                            return nullptr;
#if defined(_WIN32)
                        if (make_directory(p, mode) != 0)
#else
                        if (::mkdirat(dirFd, p.c_str(), static_cast<mode_t>(mode)) != 0)
#endif
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
                        int dirFd = 0;
                        if (!parse_arguments("rmdir", aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "rmdir") || !dir_fd_of(a[1], dirFd))
                            return nullptr;
#if defined(_WIN32)
                        if (::_rmdir(p.c_str()) != 0)
#else
                        if (::unlinkat(dirFd, p.c_str(), AT_REMOVEDIR) != 0)
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
                { "remove", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return remove_path("remove", aArgs, aKwargs); } },
                { "rename", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.rename"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string from, to;
                        int fromFd = 0, toFd = 0;
                        if (!parse_arguments("rename", aArgs, aKwargs, { "src", "dst", "src_dir_fd", "dst_dir_fd" }, 2u, 0u, a) || !path_of(a, 0u, from, "rename", "src") ||
                            !path_of(a, 1u, to, "rename", "dst") || !dir_fd_of(a[2], fromFd) || !dir_fd_of(a[3], toFd))
                            return nullptr;
#if defined(_WIN32)
                        if (exists(to))
                            return raise_os_error(EEXIST, a[0], a[1]);
                        if (std::rename(from.c_str(), to.c_str()) != 0)
#else
                        if (::renameat(fromFd, from.c_str(), toFd, to.c_str()) != 0)
#endif
                            return raise_os_error(errno, a[0], a[1]);
                        return none();
                    } },
                { "replace", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.replace"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string from, to;
                        int fromFd = 0, toFd = 0;
                        if (!parse_arguments("replace", aArgs, aKwargs, { "src", "dst", "src_dir_fd", "dst_dir_fd" }, 2u, 0u, a) || !path_of(a, 0u, from, "replace", "src") ||
                            !path_of(a, 1u, to, "replace", "dst") || !dir_fd_of(a[2], fromFd) || !dir_fd_of(a[3], toFd))
                            return nullptr;
#if !defined(_WIN32)
                        // (POSIX rename replaces dst (as CPython's os.replace))
                        if (::renameat(fromFd, from.c_str(), toFd, to.c_str()) != 0)
                            return raise_os_error(errno, a[0], a[1]);
                        return none();
#endif
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
                        int dirFd = 0;
                        std::optional<int> fd;
                        if (!parse_arguments("stat", aArgs, aKwargs, { "path", "dir_fd", "follow_symlinks" }, 1u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "stat") || 
                            !dir_fd_of(a[1], dirFd) || !fd_alone("stat", fd, dirFd, follow_of(a[2])))
                            return nullptr;
                        if (fd)
                            return fd_stat(*fd, a[0]);
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, follow_of(a[2]), s, e, dirFd))
                            return raise_os_error(e, a[0]);
                        return stat_result(s);
                    } },
                { "lstat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        int dirFd = 0;
                        if (!parse_arguments("lstat", aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "lstat") || !dir_fd_of(a[1], dirFd))
                            return nullptr;
                        stat_values s;
                        int e = 0;
                        if (!do_stat(p, false, s, e, dirFd))
                            return raise_os_error(e, a[0]);
                        return stat_result(s);
                    } },
                { "access", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        int dirFd = 0;
                        if (!parse_arguments("access", aArgs, aKwargs, { "path", "mode", "dir_fd", "effective_ids", "follow_symlinks" }, 2u, 0u, a) || !path_of(a, 0u, p, "access") ||
                            !dir_fd_of(a[2], dirFd))
                            return nullptr;
                        long long mode = 0;
                        if (!to_index(a[1], mode))
                            return nullptr;
                        bool const effective = a[3] != nullptr && truth(a[3]) == 1;
#if defined(_WIN32)
                        if (effective)
                            return error("NotImplementedError", "access: effective_ids unavailable on this platform");
                        if (!follow_of(a[4]))
                            return follow_symlinks_unavailable("access");
                        return make_bool(::_access(p.c_str(), static_cast<int>(mode & 6)) == 0);
#else
                        return make_bool(::faccessat(dirFd, p.c_str(), static_cast<int>(mode), (effective ? AT_EACCESS : 0) | (follow_of(a[4]) ? 0 : AT_SYMLINK_NOFOLLOW)) == 0);
#endif
                    } },
                { "chmod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.chmod"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        int dirFd = 0;
                        std::optional<int> fd;
                        if (!parse_arguments("chmod", aArgs, aKwargs, { "path", "mode", "dir_fd", "follow_symlinks" }, 2u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "chmod") ||
                            !dir_fd_of(a[2], dirFd) || !fd_alone("chmod", fd, dirFd, follow_of(a[3])))
                            return nullptr;
                        long long mode = 0;
                        if (!to_index(a[1], mode))
                            return nullptr;
#if !defined(_WIN32)
                        if (fd)
                            return ::fchmod(*fd, static_cast<mode_t>(mode)) != 0 ? raise_os_error(errno, a[0]) : none();
#endif
#if defined(_WIN32)
                        if (!follow_of(a[3]))
                            return follow_symlinks_unavailable("chmod");
                        if (::_chmod(p.c_str(), static_cast<int>(mode) & (_S_IREAD | _S_IWRITE)) != 0)
                            return raise_os_error(errno, a[0]);
#else
                        // (not following a symbolic link: fchmodat's AT_SYMLINK_NOFOLLOW, which Linux does not support for links (as CPython's: 
                        // NotImplementedError))
                        if (::fchmodat(dirFd, p.c_str(), static_cast<mode_t>(mode), follow_of(a[3]) ? 0 : AT_SYMLINK_NOFOLLOW) != 0)
                        {
                            if (!follow_of(a[3]) && (errno == ENOTSUP || errno == EOPNOTSUPP))
                                return follow_symlinks_unavailable("chmod");
                            return raise_os_error(errno, a[0]);
                        }
#endif
                        return none();
                    } },
                { "utime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.utime"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        int dirFd = 0;
                        std::optional<int> fd;
                        if (!parse_arguments("utime", aArgs, aKwargs, { "path", "times", "ns", "dir_fd", "follow_symlinks" }, 1u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "utime") ||
                            !dir_fd_of(a[3], dirFd) || !fd_alone("utime", fd, dirFd, follow_of(a[4])))
                            return nullptr;
                        bool const times = a[1] != nullptr && a[1] != none();
                        if (times && a[2] != nullptr)
                            return error("ValueError", "utime: you may specify either 'times' or 'ns' but not both");
                        if (times && (kind_of(a[1]) != NEOS_PY_TUPLE || as<tuple_object>(a[1])->items.size() != 2u))
                            return error("TypeError", "utime: 'times' must be either a tuple of two ints or None");
                        if (a[2] != nullptr && (kind_of(a[2]) != NEOS_PY_TUPLE || as<tuple_object>(a[2])->items.size() != 2u))
                            return error("TypeError", "utime: 'ns' must be a tuple of two ints");
#if !defined(_WIN32)
                        // (as CPython's: utimensat, an int exact seconds, a float floored to nanoseconds; neither: now)
                        struct timespec when[2];
                        for (std::size_t i = 0u; i < 2u; ++i)
                        {
                            if (a[2] != nullptr)
                            {
                                long long n = 0;
                                if (!to_index(as<tuple_object>(a[2])->items[i], n))
                                    return nullptr;
                                when[i].tv_sec = static_cast<time_t>(n / 1000000000ll - (n % 1000000000ll < 0 ? 1 : 0));
                                when[i].tv_nsec = static_cast<long>(n % 1000000000ll + (n % 1000000000ll < 0 ? 1000000000ll : 0));
                            }
                            else if (times && kind_of(as<tuple_object>(a[1])->items[i]) == NEOS_PY_FLOAT)
                            {
                                double d = 0.0;
                                if (!to_double(as<tuple_object>(a[1])->items[i], d))
                                    return nullptr;
                                auto const whole = std::floor(d);
                                auto nanoseconds = static_cast<long>(std::floor((d - whole) * 1e9));
                                auto seconds = static_cast<time_t>(whole);
                                if (nanoseconds >= 1000000000l)
                                {
                                    nanoseconds -= 1000000000l;
                                    ++seconds;
                                }
                                when[i].tv_sec = seconds;
                                when[i].tv_nsec = nanoseconds;
                            }
                            else if (times)
                            {
                                long long n = 0;
                                if (!to_index(as<tuple_object>(a[1])->items[i], n))
                                    return nullptr;
                                when[i].tv_sec = static_cast<time_t>(n);
                                when[i].tv_nsec = 0;
                            }
                            else
                            {
                                when[i].tv_sec = 0;
                                when[i].tv_nsec = UTIME_NOW;
                            }
                        }
                        if ((fd ? ::futimens(*fd, when) : ::utimensat(dirFd, p.c_str(), when, follow_of(a[4]) ? 0 : AT_SYMLINK_NOFOLLOW)) != 0)
                            return raise_os_error(errno, a[0]);
                        return none();
#else
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
#endif
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
                        // (as CPython's os_putenv_impl: str or bytes (POSIX; Windows: str), a name without '=')
                        std::string name, value;
#if defined(_WIN32)
                        if (!str_of(a[0], name) || !str_of(a[1], value))
                            return nullptr;
                        if (name.empty() || name.find('=') != std::string::npos)
                            return error("ValueError", "illegal environment variable name");
                        ::_putenv_s(name.c_str(), value.c_str());
#else
                        if (!path_argument(a[0], name, "putenv", "env") || !path_argument(a[1], value, "putenv", "env"))
                            return nullptr;
                        if (name.find('=') != std::string::npos)
                            return error("ValueError", "illegal environment variable name");
                        if (::setenv(name.c_str(), value.c_str(), 1) != 0)
                            return raise_os_error(errno);
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
#if defined(_WIN32)
                        if (!str_of(a[0], name))
                            return nullptr;
                        if (name.empty() || name.find('=') != std::string::npos)
                            return error("ValueError", "illegal environment variable name");
                        ::_putenv_s(name.c_str(), "");
#else
                        if (!path_argument(a[0], name, "unsetenv", "env"))
                            return nullptr;
                        if (::unsetenv(name.c_str()) != 0)
                            return raise_os_error(errno);
#endif
                        return none();
                    } },
                { "getpid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getpid", aArgs, aKwargs)) return nullptr;
#if defined(_WIN32)
                        return make_int(::_getpid());
#else
                        return make_int(::getpid());
#endif
                    } },
                { "getppid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getppid", aArgs, aKwargs)) return nullptr;
#if defined(_WIN32)
                        return make_int(0);
#else
                        return make_int(::getppid());
#endif
                    } },
                { "cpu_count", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("cpu_count", aArgs, aKwargs)) return nullptr;
                        auto const n = std::thread::hardware_concurrency();
                        return n == 0u ? (retain(none()), none()) : make_int(n);
                    } },
                { "process_cpu_count", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("process_cpu_count", aArgs, aKwargs)) return nullptr;
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
                // (get_terminal_size(fd=STDOUT_FILENO, /): as CPython's (POSIX: the terminal's size by ioctl, or its error))
                { "get_terminal_size", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get_terminal_size", aArgs, aKwargs, { "fd" }, 0u, 1u, a))
                            return nullptr;
                        long long fd = 1;
                        if (a[0] != nullptr && !to_index(a[0], fd))
                            return nullptr;
                        long long columns = 80, lines = 24;
#if !defined(_WIN32)
                        struct winsize w{};
                        if (::ioctl(static_cast<int>(fd), TIOCGWINSZ, &w) != 0)
                            return raise_os_error(errno);
                        columns = w.ws_col;
                        lines = w.ws_row;
#else
                        if (fd < 0 || fd > 2)
                            return error("ValueError", "bad file descriptor");
#endif
                        auto const& t = terminal_size_type();
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
                { "getlogin", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getlogin", aArgs, aKwargs)) return nullptr;
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

            // (aName: the error's file name (os.stat(fd)'s: the descriptor), if any)
            object* fd_stat(int aFd, object* aName)
            {
                stat_values r;
#if defined(_WIN32)
                struct _stat64 st{};
                if (::_fstat64(aFd, &st) != 0)
                    return aName != nullptr ? raise_os_error(errno, aName) : raise_os_error(errno);
                r.atime_ns = static_cast<long long>(st.st_atime) * 1000000000ll;
                r.mtime_ns = static_cast<long long>(st.st_mtime) * 1000000000ll;
                r.ctime_ns = static_cast<long long>(st.st_ctime) * 1000000000ll;
#else
                struct stat st{};
                if (::fstat(aFd, &st) != 0)
                    return aName != nullptr ? raise_os_error(errno, aName) : raise_os_error(errno);
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
#if !defined(_WIN32)
                r.blksize = static_cast<long long>(st.st_blksize);
                r.blocks = static_cast<long long>(st.st_blocks);
                r.rdev = static_cast<long long>(st.st_rdev);
#endif
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

#if !defined(_WIN32)
            // (a uid or gid argument (CPython's _Py_Uid_Converter and _Py_Gid_Converter): an integer (an object with __index__) that a uid_t
            // (gid_t) holds, or -1; OverflowError otherwise: "less than minimum" for a negative value or one that a long holds but a uid_t 
            // does not, "greater than maximum" for one that a long does not hold (as CPython's on a 64-bit POSIX system))
            bool id_argument(object* aValue, long long& aResult, bool aGroup)
            {
                std::string const kind = aGroup ? "gid" : "uid";
                long long value = 0;
                if (!to_index(aValue, value))
                {
                    if (!std::string_view{ error_text() }.starts_with("OverflowError"))
                    {
                        clear_error();
                        error("TypeError", kind + " should be integer, not " + type_name(aValue));
                        return false;
                    }
                    clear_error();
                    ref const zero{ make_int(0) };
                    ref const negative{ compare(NEOS_PY_LT, aValue, zero.get()) };
                    if (!negative)
                        return false;
                    error("OverflowError", kind + (truth(negative.get()) == 1 ? " is less than minimum" : " is greater than maximum"));
                    return false;
                }
                if (value != -1 && (value < 0 || static_cast<long long>(static_cast<uid_t>(value)) != value))
                {
                    error("OverflowError", kind + " is less than minimum");
                    return false;
                }
                aResult = value;
                return true;
            }

            // (a function of ids (aCount: their number): aSet(ids), or OSError)
            template <typename F>
            object* set_ids(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, std::size_t aCount, F aSet)
            {
                static char const* const sNames[] = { "a", "b", "c" };
                std::vector<object*> a;
                if (!host_unrestricted((std::string{ "os." } + aName).c_str()))
                    return nullptr;
                if (aArgs.size() != aCount || (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty()))
                    return error("TypeError", std::string{ aName } + "() takes exactly " + std::to_string(aCount) + " argument" + (aCount == 1u ? "" : "s") + 
                        " (" + std::to_string(aArgs.size()) + " given)");
                (void)sNames;
                long long values[3] = {};
                bool const group = std::string_view{ aName }.find("gid") != std::string_view::npos;
                for (std::size_t i = 0u; i < aCount; ++i)
                    if (!id_argument(aArgs[i], values[i], group))
                        return nullptr;
                if (aSet(values) != 0)
                    return raise_os_error(errno);
                return none();
            }

            // (a mapping (CPython's PyMapping_Check: an object with __getitem__, not a sequence of builtin kind))
            bool is_mapping(object* aValue)
            {
                if (kind_of(builtin_value(aValue)) == NEOS_PY_DICT)
                    return true;
                if (kind_of(aValue) == NEOS_PY_LIST || kind_of(aValue) == NEOS_PY_TUPLE || kind_of(aValue) == NEOS_PY_STR)
                    return false;
                ref const keys{ getattr(aValue, "keys") };
                ref const getitemMethod{ keys ? getattr(aValue, "__getitem__") : nullptr };
                if (!keys || !getitemMethod)
                {
                    clear_error();
                    return false;
                }
                return true;
            }

            // (exec's and posix_spawn's environment (CPython's parse_envlist): "name=value" of each item; a name must not be empty nor
            // have '=' after its first character)
            bool environment_list(object* aEnv, std::vector<std::string>& aResult, char const* aFunction)
            {
                ref const keysMethod{ getattr(aEnv, "keys") };
                if (!keysMethod)
                    return false;
                ref const keys{ invoke(keysMethod.get(), {}) };
                std::vector<object*> names;
                if (!keys || !collect(keys.get(), names))
                    return false;
                bool ok = true;
                for (auto name : names)
                {
                    if (ok)
                    {
                        std::string key, value;
                        ref const v{ getitem(aEnv, name) };
                        ok = v && path_argument(name, key, aFunction, "env") && path_argument(v.get(), value, aFunction, "env");
                        if (ok && (key.empty() || key.find('=', 1u) != std::string::npos))
                        {
                            error("ValueError", "illegal environment variable name");
                            ok = false;
                        }
                        if (ok)
                            aResult.push_back(key + "=" + value);
                    }
                    release(name);
                }
                return ok;
            }

            // (a sigset argument (CPython's _Py_Sigset_Converter): an iterable of signal numbers in [1, NSIG); an invalid one that sigaddset
            // refuses warns (RuntimeWarning: valid_signals())
            bool sigset_of(object* aValue, sigset_t& aResult)
            {
                ::sigemptyset(&aResult);
                std::vector<object*> items;
                if (!collect(aValue, items))
                    return false;
                bool ok = true;
                for (auto item : items)
                {
                    if (ok)
                    {
                        long long number = 0;
                        if (!to_index(item, number))
                            ok = false;
                        else if (number <= 0 || number >= NSIG)
                        {
                            error("ValueError", "signal number " + std::to_string(number) + " out of range [1; " + std::to_string(NSIG - 1) + "]");
                            ok = false;
                        }
                        else if (::sigaddset(&aResult, static_cast<int>(number)) != 0)
                        {
                            ref const warn{ module_attribute("warnings", "warn") };
                            ref category{ fn_builtin("RuntimeWarning") };
                            ok = warn && category && ref{ invoke(warn.get(), { make_str("invalid signal number " + std::to_string(number) + 
                                ", please use valid_signals()"), category.release_ownership() }) };
                        }
                    }
                    release(item);
                }
                return ok;
            }

            // (os.POSIX_SPAWN_OPEN, POSIX_SPAWN_CLOSE and POSIX_SPAWN_DUP2: the tags of posix_spawn's file actions (CPython's values))
            constexpr long long POSIX_SPAWN_OPEN_TAG = 0, POSIX_SPAWN_CLOSE_TAG = 1, POSIX_SPAWN_DUP2_TAG = 2;
#if defined(__linux__)
            bool sched_param_of(object* aParam, struct sched_param& aResult);
#endif

            // (posix_spawn and posix_spawnp (CPython's py_posix_spawn, parse_file_actions and parse_posix_spawn_flags): path, argv and env
            // (positional), file_actions, setpgroup, resetids, setsid, setsigmask, setsigdef and scheduler (keywords): the child's pid)
            object* posix_spawn_call(bool aSearch, std::vector<object*> const& aArgs, object* aKwargs)
            {
                char const* const name = aSearch ? "posix_spawnp" : "posix_spawn";
                if (!host_unrestricted(aSearch ? "os.posix_spawnp" : "os.posix_spawn"))
                    return nullptr;
                if (aArgs.size() > 3u)
                    return error("TypeError", std::string{ name } + "() takes exactly 3 positional arguments (" + std::to_string(aArgs.size()) + " given)");
                std::vector<object*> a;
                if (!parse_arguments(name, aArgs, aKwargs, { "path", "argv", "env", "file_actions", "setpgroup", "resetids", "setsid", "setsigmask",
                    "setsigdef", "scheduler" }, 3u, 3u, a))
                    return nullptr;
                std::string path;
                if (!path_argument(a[0], path, name))
                    return nullptr;
                if (kind_of(a[1]) != NEOS_PY_TUPLE && kind_of(a[1]) != NEOS_PY_LIST)
                    return error("TypeError", std::string{ name } + ": argv must be a tuple or list");
                if ((kind_of(a[1]) == NEOS_PY_TUPLE ? as<tuple_object>(a[1])->items.size() : as<list_object>(a[1])->items.size()) < 1u)
                    return error("ValueError", std::string{ name } + ": argv must not be empty");
                if (a[2] != none() && !is_mapping(a[2]))
                    return error("TypeError", std::string{ name } + ": environment must be a mapping object or None");
                object* const scheduler = a[9] != nullptr && a[9] != none() ? a[9] : nullptr;
                if (scheduler != nullptr && kind_of(scheduler) != NEOS_PY_TUPLE)
                    return error("TypeError", std::string{ name } + ": scheduler must be a tuple or None");
                std::vector<std::string> args;
                if (!exec_arguments(a[1], args, name))
                    return nullptr;
                if (args[0].empty())
                    return error("ValueError", std::string{ name } + ": argv first element cannot be empty");
                std::vector<std::string> environment;
                bool const inherit = a[2] == none();
                if (!inherit && !environment_list(a[2], environment, name))
                    return nullptr;
                // (the file actions: open (POSIX_SPAWN_OPEN, fd, path, flags, mode), close (POSIX_SPAWN_CLOSE, fd) and dup2 (POSIX_SPAWN_DUP2, fd, fd2))
                posix_spawn_file_actions_t fileActions;
                bool haveFileActions = false;
                std::vector<std::string> openPaths;
                struct guard
                {
                    posix_spawn_file_actions_t* actions = nullptr;
                    posix_spawnattr_t* attributes = nullptr;
                    ~guard() { if (actions != nullptr) ::posix_spawn_file_actions_destroy(actions); if (attributes != nullptr) ::posix_spawnattr_destroy(attributes); }
                } resources;
                if (a[3] != nullptr && a[3] != none())
                {
                    std::vector<object*> actions;
                    if (!collect(a[3], actions))
                    {
                        clear_error();
                        return error("TypeError", "file_actions must be a sequence or None");
                    }
                    if (int const e = ::posix_spawn_file_actions_init(&fileActions); e != 0)
                    {
                        for (auto x : actions)
                            release(x);
                        return raise_os_error(e);
                    }
                    resources.actions = &fileActions;
                    haveFileActions = true;
                    openPaths.reserve(actions.size());
                    bool ok = true;
                    for (auto action : actions)
                    {
                        if (ok)
                        {
                            auto const* items = kind_of(action) == NEOS_PY_TUPLE ? &as<tuple_object>(action)->items : nullptr;
                            if (items == nullptr || items->empty())
                            {
                                error("TypeError", "Each file_actions element must be a non-empty tuple");
                                ok = false;
                            }
                            long long tag = 0;
                            if (ok && !to_index((*items)[0], tag))
                                ok = false;
                            auto const integer = [&](std::size_t aIndex, int& aResult) -> bool
                            {
                                long long v = 0;
                                if (!to_index((*items)[aIndex], v))
                                    return false;
                                if (v > std::numeric_limits<int>::max() || v < std::numeric_limits<int>::min())
                                {
                                    error("OverflowError", v > 0 ? "signed integer is greater than maximum" : "signed integer is less than minimum");
                                    return false;
                                }
                                aResult = static_cast<int>(v);
                                return true;
                            };
                            int e = 0;
                            if (ok)
                                switch (tag)
                                {
                                case POSIX_SPAWN_OPEN_TAG:
                                    {
                                        int fd = 0, flags = 0;
                                        long long mode = 0;
                                        std::string file;
                                        if (items->size() != 5u)
                                        {
                                            error("TypeError", "A open file_action tuple must have 5 elements");
                                            ok = false;
                                        }
                                        else if (!integer(1u, fd) || !path_argument((*items)[2], file, name) || !integer(3u, flags) || !to_index((*items)[4], mode))
                                            ok = false;
                                        else
                                        {
                                            openPaths.push_back(file);
                                            e = ::posix_spawn_file_actions_addopen(&fileActions, fd, openPaths.back().c_str(), flags, static_cast<mode_t>(mode));
                                        }
                                        break;
                                    }
                                case POSIX_SPAWN_CLOSE_TAG:
                                    {
                                        int fd = 0;
                                        if (items->size() != 2u)
                                        {
                                            error("TypeError", "A close file_action tuple must have 2 elements");
                                            ok = false;
                                        }
                                        else if (!integer(1u, fd))
                                            ok = false;
                                        else
                                            e = ::posix_spawn_file_actions_addclose(&fileActions, fd);
                                        break;
                                    }
                                case POSIX_SPAWN_DUP2_TAG:
                                    {
                                        int fd = 0, fd2 = 0;
                                        if (items->size() != 3u)
                                        {
                                            error("TypeError", "A dup2 file_action tuple must have 3 elements");
                                            ok = false;
                                        }
                                        else if (!integer(1u, fd) || !integer(2u, fd2))
                                            ok = false;
                                        else
                                            e = ::posix_spawn_file_actions_adddup2(&fileActions, fd, fd2);
                                        break;
                                    }
                                default:
                                    error("TypeError", "Unknown file_actions identifier");
                                    ok = false;
                                    break;
                                }
                            if (ok && e != 0)
                            {
                                raise_os_error(e);
                                ok = false;
                            }
                        }
                        release(action);
                    }
                    if (!ok)
                        return nullptr;
                }
                // (the attributes: the flags of setpgroup, resetids, setsid, setsigmask, setsigdef and scheduler)
                posix_spawnattr_t attributes;
                if (int const e = ::posix_spawnattr_init(&attributes); e != 0)
                    return raise_os_error(e);
                resources.attributes = &attributes;
                short flags = 0;
                if (a[4] != nullptr && a[4] != none())
                {
                    long long group = 0;
                    if (!to_index(a[4], group))
                        return nullptr;
                    if (int const e = ::posix_spawnattr_setpgroup(&attributes, static_cast<pid_t>(group)); e != 0)
                        return raise_os_error(e);
                    flags |= POSIX_SPAWN_SETPGROUP;
                }
                if (a[5] != nullptr && truth(a[5]) == 1)
                    flags |= POSIX_SPAWN_RESETIDS;
                if (a[6] != nullptr && truth(a[6]) == 1)
                {
#if defined(POSIX_SPAWN_SETSID)
                    flags |= POSIX_SPAWN_SETSID;
#elif defined(POSIX_SPAWN_SETSID_NP)
                    flags |= POSIX_SPAWN_SETSID_NP;
#else
                    return error("NotImplementedError", std::string{ name } + ": setsid is unavailable on this platform");
#endif
                }
                for (auto const& [argument, which] : { std::pair{ a[7], 0 }, std::pair{ a[8], 1 } })
                    if (argument != nullptr)
                    {
                        sigset_t set;
                        if (!sigset_of(argument, set))
                            return nullptr;
                        if (int const e = which == 0 ? ::posix_spawnattr_setsigmask(&attributes, &set) : ::posix_spawnattr_setsigdefault(&attributes, &set); e != 0)
                            return raise_os_error(e);
                        flags |= which == 0 ? POSIX_SPAWN_SETSIGMASK : POSIX_SPAWN_SETSIGDEF;
                    }
                if (scheduler != nullptr)
                {
#if defined(POSIX_SPAWN_SETSCHEDULER) && defined(__linux__)
                    auto const& items = as<tuple_object>(scheduler)->items;
                    if (items.size() != 2u)
                        return error("TypeError", "A scheduler tuple must have two elements");
                    struct sched_param parameters{};
                    if (!sched_param_of(items[1], parameters))
                        return nullptr;
                    if (items[0] != none())
                    {
                        long long policy = 0;
                        if (!to_index(items[0], policy))
                            return nullptr;
                        if (int const e = ::posix_spawnattr_setschedpolicy(&attributes, static_cast<int>(policy)); e != 0)
                            return raise_os_error(e);
                        flags |= POSIX_SPAWN_SETSCHEDULER;
                    }
                    if (int const e = ::posix_spawnattr_setschedparam(&attributes, &parameters); e != 0)
                        return raise_os_error(e);
                    flags |= POSIX_SPAWN_SETSCHEDPARAM;
#else
                    return error("NotImplementedError", "The scheduler option is not supported in this system.");
#endif
                }
                if (int const e = ::posix_spawnattr_setflags(&attributes, flags); e != 0)
                    return raise_os_error(e);
                std::vector<char*> argv;
                for (auto& x : args)
                    argv.push_back(x.data());
                argv.push_back(nullptr);
                std::vector<char*> envp;
                char** env = nullptr;
                if (inherit)
                {
#if defined(__APPLE__)
                    env = *::_NSGetEnviron();
#else
                    env = environ;
#endif
                }
                else
                {
                    for (auto& x : environment)
                        envp.push_back(x.data());
                    envp.push_back(nullptr);
                    env = envp.data();
                }
                pid_t pid = 0;
                std::fflush(nullptr);
                int const e = aSearch ? ::posix_spawnp(&pid, path.c_str(), haveFileActions ? &fileActions : nullptr, &attributes, argv.data(), env) :
                    ::posix_spawn(&pid, path.c_str(), haveFileActions ? &fileActions : nullptr, &attributes, argv.data(), env);
                if (e != 0)
                    return raise_os_error(e, a[0]);
                return make_int(static_cast<long long>(pid));
            }
#endif

#if defined(__linux__)
            // (os.sched_param: a struct sequence (sched_priority) made by sched_param(sched_priority))
            structseq_descriptor const& sched_param_type()
            {
                static auto const& sType = []() -> structseq_descriptor const&
                {
                    auto const& t = structseq_type("posix.sched_param", { "sched_priority" }, 1u);
                    set_structseq_constructor(t, [](structseq_descriptor const& aDescriptor, object* aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sched_param", tuple_items(aArgs), aKwargs, { "sched_priority" }, 1u, 0u, a))
                            return nullptr;
                        return make_structseq(aDescriptor, { new_reference(a[0]) });
                    }, true);
                    return t;
                }();
                return sType;
            }
            // (a sched_param argument's priority)
            bool sched_param_of(object* aParam, struct sched_param& aResult)
            {
                std::vector<object*> items;
                if (aParam == nullptr || aParam->type != &sched_param_type().type.base || !structseq_items(aParam, items) || items.empty())
                {
                    error("TypeError", "must have a sched_param object");
                    return false;
                }
                long long priority = 0;
                auto const k = kind_of(items[0]);
                if (k != NEOS_PY_INT && k != NEOS_PY_BOOL && !has_special_method(items[0], "__index__"))
                    return int_argument(items[0], priority, "sched_param");
                if (!to_index(items[0], priority))
                {
                    // (an int that a C long cannot hold)
                    clear_error();
                    error("OverflowError", "Python int too large to convert to C long");
                    return false;
                }
                if (priority < std::numeric_limits<int>::min() || priority > std::numeric_limits<int>::max())
                {
                    error("OverflowError", "sched_priority out of range");
                    return false;
                }
                aResult.sched_priority = static_cast<int>(priority);
                return true;
            }
#endif

            // (os.sysconf's names (CPython's posixmodule.c posix_constants_sysconf: those this system has))
            // (os.pathconf_names: as CPython 3.13's posix_constants_pathconf)
            std::vector<std::pair<char const*, int>> const sPathconfNames =
            {
#ifdef _PC_ABI_AIO_XFER_MAX
                { "PC_ABI_AIO_XFER_MAX", _PC_ABI_AIO_XFER_MAX },
#endif
#ifdef _PC_ABI_ASYNC_IO
                { "PC_ABI_ASYNC_IO", _PC_ABI_ASYNC_IO },
#endif
#ifdef _PC_ASYNC_IO
                { "PC_ASYNC_IO", _PC_ASYNC_IO },
#endif
#ifdef _PC_CHOWN_RESTRICTED
                { "PC_CHOWN_RESTRICTED", _PC_CHOWN_RESTRICTED },
#endif
#ifdef _PC_FILESIZEBITS
                { "PC_FILESIZEBITS", _PC_FILESIZEBITS },
#endif
#ifdef _PC_LAST
                { "PC_LAST", _PC_LAST },
#endif
#ifdef _PC_LINK_MAX
                { "PC_LINK_MAX", _PC_LINK_MAX },
#endif
#ifdef _PC_MAX_CANON
                { "PC_MAX_CANON", _PC_MAX_CANON },
#endif
#ifdef _PC_MAX_INPUT
                { "PC_MAX_INPUT", _PC_MAX_INPUT },
#endif
#ifdef _PC_NAME_MAX
                { "PC_NAME_MAX", _PC_NAME_MAX },
#endif
#ifdef _PC_NO_TRUNC
                { "PC_NO_TRUNC", _PC_NO_TRUNC },
#endif
#ifdef _PC_PATH_MAX
                { "PC_PATH_MAX", _PC_PATH_MAX },
#endif
#ifdef _PC_PIPE_BUF
                { "PC_PIPE_BUF", _PC_PIPE_BUF },
#endif
#ifdef _PC_PRIO_IO
                { "PC_PRIO_IO", _PC_PRIO_IO },
#endif
#ifdef _PC_SOCK_MAXBUF
                { "PC_SOCK_MAXBUF", _PC_SOCK_MAXBUF },
#endif
#ifdef _PC_SYNC_IO
                { "PC_SYNC_IO", _PC_SYNC_IO },
#endif
#ifdef _PC_VDISABLE
                { "PC_VDISABLE", _PC_VDISABLE },
#endif
#ifdef _PC_ACL_ENABLED
                { "PC_ACL_ENABLED", _PC_ACL_ENABLED },
#endif
#ifdef _PC_MIN_HOLE_SIZE
                { "PC_MIN_HOLE_SIZE", _PC_MIN_HOLE_SIZE },
#endif
#ifdef _PC_ALLOC_SIZE_MIN
                { "PC_ALLOC_SIZE_MIN", _PC_ALLOC_SIZE_MIN },
#endif
#ifdef _PC_REC_INCR_XFER_SIZE
                { "PC_REC_INCR_XFER_SIZE", _PC_REC_INCR_XFER_SIZE },
#endif
#ifdef _PC_REC_MAX_XFER_SIZE
                { "PC_REC_MAX_XFER_SIZE", _PC_REC_MAX_XFER_SIZE },
#endif
#ifdef _PC_REC_MIN_XFER_SIZE
                { "PC_REC_MIN_XFER_SIZE", _PC_REC_MIN_XFER_SIZE },
#endif
#ifdef _PC_REC_XFER_ALIGN
                { "PC_REC_XFER_ALIGN", _PC_REC_XFER_ALIGN },
#endif
#ifdef _PC_SYMLINK_MAX
                { "PC_SYMLINK_MAX", _PC_SYMLINK_MAX },
#endif
#ifdef _PC_XATTR_ENABLED
                { "PC_XATTR_ENABLED", _PC_XATTR_ENABLED },
#endif
#ifdef _PC_XATTR_EXISTS
                { "PC_XATTR_EXISTS", _PC_XATTR_EXISTS },
#endif
            };

            // (os.confstr_names: as CPython 3.13's posix_constants_confstr)
            std::vector<std::pair<char const*, int>> const sConfstrNames =
            {
#ifdef _CS_ARCHITECTURE
                { "CS_ARCHITECTURE", _CS_ARCHITECTURE },
#endif
#ifdef _CS_GNU_LIBC_VERSION
                { "CS_GNU_LIBC_VERSION", _CS_GNU_LIBC_VERSION },
#endif
#ifdef _CS_GNU_LIBPTHREAD_VERSION
                { "CS_GNU_LIBPTHREAD_VERSION", _CS_GNU_LIBPTHREAD_VERSION },
#endif
#ifdef _CS_HOSTNAME
                { "CS_HOSTNAME", _CS_HOSTNAME },
#endif
#ifdef _CS_HW_PROVIDER
                { "CS_HW_PROVIDER", _CS_HW_PROVIDER },
#endif
#ifdef _CS_HW_SERIAL
                { "CS_HW_SERIAL", _CS_HW_SERIAL },
#endif
#ifdef _CS_INITTAB_NAME
                { "CS_INITTAB_NAME", _CS_INITTAB_NAME },
#endif
#ifdef _CS_LFS64_CFLAGS
                { "CS_LFS64_CFLAGS", _CS_LFS64_CFLAGS },
#endif
#ifdef _CS_LFS64_LDFLAGS
                { "CS_LFS64_LDFLAGS", _CS_LFS64_LDFLAGS },
#endif
#ifdef _CS_LFS64_LIBS
                { "CS_LFS64_LIBS", _CS_LFS64_LIBS },
#endif
#ifdef _CS_LFS64_LINTFLAGS
                { "CS_LFS64_LINTFLAGS", _CS_LFS64_LINTFLAGS },
#endif
#ifdef _CS_LFS_CFLAGS
                { "CS_LFS_CFLAGS", _CS_LFS_CFLAGS },
#endif
#ifdef _CS_LFS_LDFLAGS
                { "CS_LFS_LDFLAGS", _CS_LFS_LDFLAGS },
#endif
#ifdef _CS_LFS_LIBS
                { "CS_LFS_LIBS", _CS_LFS_LIBS },
#endif
#ifdef _CS_LFS_LINTFLAGS
                { "CS_LFS_LINTFLAGS", _CS_LFS_LINTFLAGS },
#endif
#ifdef _CS_MACHINE
                { "CS_MACHINE", _CS_MACHINE },
#endif
#ifdef _CS_PATH
                { "CS_PATH", _CS_PATH },
#endif
#ifdef _CS_RELEASE
                { "CS_RELEASE", _CS_RELEASE },
#endif
#ifdef _CS_SRPC_DOMAIN
                { "CS_SRPC_DOMAIN", _CS_SRPC_DOMAIN },
#endif
#ifdef _CS_SYSNAME
                { "CS_SYSNAME", _CS_SYSNAME },
#endif
#ifdef _CS_VERSION
                { "CS_VERSION", _CS_VERSION },
#endif
#ifdef _CS_XBS5_ILP32_OFF32_CFLAGS
                { "CS_XBS5_ILP32_OFF32_CFLAGS", _CS_XBS5_ILP32_OFF32_CFLAGS },
#endif
#ifdef _CS_XBS5_ILP32_OFF32_LDFLAGS
                { "CS_XBS5_ILP32_OFF32_LDFLAGS", _CS_XBS5_ILP32_OFF32_LDFLAGS },
#endif
#ifdef _CS_XBS5_ILP32_OFF32_LIBS
                { "CS_XBS5_ILP32_OFF32_LIBS", _CS_XBS5_ILP32_OFF32_LIBS },
#endif
#ifdef _CS_XBS5_ILP32_OFF32_LINTFLAGS
                { "CS_XBS5_ILP32_OFF32_LINTFLAGS", _CS_XBS5_ILP32_OFF32_LINTFLAGS },
#endif
#ifdef _CS_XBS5_ILP32_OFFBIG_CFLAGS
                { "CS_XBS5_ILP32_OFFBIG_CFLAGS", _CS_XBS5_ILP32_OFFBIG_CFLAGS },
#endif
#ifdef _CS_XBS5_ILP32_OFFBIG_LDFLAGS
                { "CS_XBS5_ILP32_OFFBIG_LDFLAGS", _CS_XBS5_ILP32_OFFBIG_LDFLAGS },
#endif
#ifdef _CS_XBS5_ILP32_OFFBIG_LIBS
                { "CS_XBS5_ILP32_OFFBIG_LIBS", _CS_XBS5_ILP32_OFFBIG_LIBS },
#endif
#ifdef _CS_XBS5_ILP32_OFFBIG_LINTFLAGS
                { "CS_XBS5_ILP32_OFFBIG_LINTFLAGS", _CS_XBS5_ILP32_OFFBIG_LINTFLAGS },
#endif
#ifdef _CS_XBS5_LP64_OFF64_CFLAGS
                { "CS_XBS5_LP64_OFF64_CFLAGS", _CS_XBS5_LP64_OFF64_CFLAGS },
#endif
#ifdef _CS_XBS5_LP64_OFF64_LDFLAGS
                { "CS_XBS5_LP64_OFF64_LDFLAGS", _CS_XBS5_LP64_OFF64_LDFLAGS },
#endif
#ifdef _CS_XBS5_LP64_OFF64_LIBS
                { "CS_XBS5_LP64_OFF64_LIBS", _CS_XBS5_LP64_OFF64_LIBS },
#endif
#ifdef _CS_XBS5_LP64_OFF64_LINTFLAGS
                { "CS_XBS5_LP64_OFF64_LINTFLAGS", _CS_XBS5_LP64_OFF64_LINTFLAGS },
#endif
#ifdef _CS_XBS5_LPBIG_OFFBIG_CFLAGS
                { "CS_XBS5_LPBIG_OFFBIG_CFLAGS", _CS_XBS5_LPBIG_OFFBIG_CFLAGS },
#endif
#ifdef _CS_XBS5_LPBIG_OFFBIG_LDFLAGS
                { "CS_XBS5_LPBIG_OFFBIG_LDFLAGS", _CS_XBS5_LPBIG_OFFBIG_LDFLAGS },
#endif
#ifdef _CS_XBS5_LPBIG_OFFBIG_LIBS
                { "CS_XBS5_LPBIG_OFFBIG_LIBS", _CS_XBS5_LPBIG_OFFBIG_LIBS },
#endif
#ifdef _CS_XBS5_LPBIG_OFFBIG_LINTFLAGS
                { "CS_XBS5_LPBIG_OFFBIG_LINTFLAGS", _CS_XBS5_LPBIG_OFFBIG_LINTFLAGS },
#endif
            };

            std::vector<std::pair<char const*, int>> const sSysconfNames =
            {
#ifdef _SC_2_CHAR_TERM
                { "SC_2_CHAR_TERM", _SC_2_CHAR_TERM },
#endif
#ifdef _SC_2_C_BIND
                { "SC_2_C_BIND", _SC_2_C_BIND },
#endif
#ifdef _SC_2_C_DEV
                { "SC_2_C_DEV", _SC_2_C_DEV },
#endif
#ifdef _SC_2_C_VERSION
                { "SC_2_C_VERSION", _SC_2_C_VERSION },
#endif
#ifdef _SC_2_FORT_DEV
                { "SC_2_FORT_DEV", _SC_2_FORT_DEV },
#endif
#ifdef _SC_2_FORT_RUN
                { "SC_2_FORT_RUN", _SC_2_FORT_RUN },
#endif
#ifdef _SC_2_LOCALEDEF
                { "SC_2_LOCALEDEF", _SC_2_LOCALEDEF },
#endif
#ifdef _SC_2_SW_DEV
                { "SC_2_SW_DEV", _SC_2_SW_DEV },
#endif
#ifdef _SC_2_UPE
                { "SC_2_UPE", _SC_2_UPE },
#endif
#ifdef _SC_2_VERSION
                { "SC_2_VERSION", _SC_2_VERSION },
#endif
#ifdef _SC_AIO_LISTIO_MAX
                { "SC_AIO_LISTIO_MAX", _SC_AIO_LISTIO_MAX },
#endif
#ifdef _SC_AIO_MAX
                { "SC_AIO_MAX", _SC_AIO_MAX },
#endif
#ifdef _SC_AIO_PRIO_DELTA_MAX
                { "SC_AIO_PRIO_DELTA_MAX", _SC_AIO_PRIO_DELTA_MAX },
#endif
#ifdef _SC_ARG_MAX
                { "SC_ARG_MAX", _SC_ARG_MAX },
#endif
#ifdef _SC_ASYNCHRONOUS_IO
                { "SC_ASYNCHRONOUS_IO", _SC_ASYNCHRONOUS_IO },
#endif
#ifdef _SC_ATEXIT_MAX
                { "SC_ATEXIT_MAX", _SC_ATEXIT_MAX },
#endif
#ifdef _SC_AVPHYS_PAGES
                { "SC_AVPHYS_PAGES", _SC_AVPHYS_PAGES },
#endif
#ifdef _SC_BC_BASE_MAX
                { "SC_BC_BASE_MAX", _SC_BC_BASE_MAX },
#endif
#ifdef _SC_BC_DIM_MAX
                { "SC_BC_DIM_MAX", _SC_BC_DIM_MAX },
#endif
#ifdef _SC_BC_SCALE_MAX
                { "SC_BC_SCALE_MAX", _SC_BC_SCALE_MAX },
#endif
#ifdef _SC_BC_STRING_MAX
                { "SC_BC_STRING_MAX", _SC_BC_STRING_MAX },
#endif
#ifdef _SC_CHARCLASS_NAME_MAX
                { "SC_CHARCLASS_NAME_MAX", _SC_CHARCLASS_NAME_MAX },
#endif
#ifdef _SC_CHAR_BIT
                { "SC_CHAR_BIT", _SC_CHAR_BIT },
#endif
#ifdef _SC_CHAR_MAX
                { "SC_CHAR_MAX", _SC_CHAR_MAX },
#endif
#ifdef _SC_CHAR_MIN
                { "SC_CHAR_MIN", _SC_CHAR_MIN },
#endif
#ifdef _SC_CHILD_MAX
                { "SC_CHILD_MAX", _SC_CHILD_MAX },
#endif
#ifdef _SC_CLK_TCK
                { "SC_CLK_TCK", _SC_CLK_TCK },
#endif
#ifdef _SC_COLL_WEIGHTS_MAX
                { "SC_COLL_WEIGHTS_MAX", _SC_COLL_WEIGHTS_MAX },
#endif
#ifdef _SC_DELAYTIMER_MAX
                { "SC_DELAYTIMER_MAX", _SC_DELAYTIMER_MAX },
#endif
#ifdef _SC_EQUIV_CLASS_MAX
                { "SC_EQUIV_CLASS_MAX", _SC_EQUIV_CLASS_MAX },
#endif
#ifdef _SC_EXPR_NEST_MAX
                { "SC_EXPR_NEST_MAX", _SC_EXPR_NEST_MAX },
#endif
#ifdef _SC_FSYNC
                { "SC_FSYNC", _SC_FSYNC },
#endif
#ifdef _SC_GETGR_R_SIZE_MAX
                { "SC_GETGR_R_SIZE_MAX", _SC_GETGR_R_SIZE_MAX },
#endif
#ifdef _SC_GETPW_R_SIZE_MAX
                { "SC_GETPW_R_SIZE_MAX", _SC_GETPW_R_SIZE_MAX },
#endif
#ifdef _SC_INT_MAX
                { "SC_INT_MAX", _SC_INT_MAX },
#endif
#ifdef _SC_INT_MIN
                { "SC_INT_MIN", _SC_INT_MIN },
#endif
#ifdef _SC_IOV_MAX
                { "SC_IOV_MAX", _SC_IOV_MAX },
#endif
#ifdef _SC_JOB_CONTROL
                { "SC_JOB_CONTROL", _SC_JOB_CONTROL },
#endif
#ifdef _SC_LINE_MAX
                { "SC_LINE_MAX", _SC_LINE_MAX },
#endif
#ifdef _SC_LOGIN_NAME_MAX
                { "SC_LOGIN_NAME_MAX", _SC_LOGIN_NAME_MAX },
#endif
#ifdef _SC_LONG_BIT
                { "SC_LONG_BIT", _SC_LONG_BIT },
#endif
#ifdef _SC_MAPPED_FILES
                { "SC_MAPPED_FILES", _SC_MAPPED_FILES },
#endif
#ifdef _SC_MB_LEN_MAX
                { "SC_MB_LEN_MAX", _SC_MB_LEN_MAX },
#endif
#ifdef _SC_MEMLOCK
                { "SC_MEMLOCK", _SC_MEMLOCK },
#endif
#ifdef _SC_MEMLOCK_RANGE
                { "SC_MEMLOCK_RANGE", _SC_MEMLOCK_RANGE },
#endif
#ifdef _SC_MEMORY_PROTECTION
                { "SC_MEMORY_PROTECTION", _SC_MEMORY_PROTECTION },
#endif
#ifdef _SC_MESSAGE_PASSING
                { "SC_MESSAGE_PASSING", _SC_MESSAGE_PASSING },
#endif
#ifdef _SC_MQ_OPEN_MAX
                { "SC_MQ_OPEN_MAX", _SC_MQ_OPEN_MAX },
#endif
#ifdef _SC_MQ_PRIO_MAX
                { "SC_MQ_PRIO_MAX", _SC_MQ_PRIO_MAX },
#endif
#ifdef _SC_NGROUPS_MAX
                { "SC_NGROUPS_MAX", _SC_NGROUPS_MAX },
#endif
#ifdef _SC_NL_ARGMAX
                { "SC_NL_ARGMAX", _SC_NL_ARGMAX },
#endif
#ifdef _SC_NL_LANGMAX
                { "SC_NL_LANGMAX", _SC_NL_LANGMAX },
#endif
#ifdef _SC_NL_MSGMAX
                { "SC_NL_MSGMAX", _SC_NL_MSGMAX },
#endif
#ifdef _SC_NL_NMAX
                { "SC_NL_NMAX", _SC_NL_NMAX },
#endif
#ifdef _SC_NL_SETMAX
                { "SC_NL_SETMAX", _SC_NL_SETMAX },
#endif
#ifdef _SC_NL_TEXTMAX
                { "SC_NL_TEXTMAX", _SC_NL_TEXTMAX },
#endif
#ifdef _SC_NPROCESSORS_CONF
                { "SC_NPROCESSORS_CONF", _SC_NPROCESSORS_CONF },
#endif
#ifdef _SC_NPROCESSORS_ONLN
                { "SC_NPROCESSORS_ONLN", _SC_NPROCESSORS_ONLN },
#endif
#ifdef _SC_NZERO
                { "SC_NZERO", _SC_NZERO },
#endif
#ifdef _SC_OPEN_MAX
                { "SC_OPEN_MAX", _SC_OPEN_MAX },
#endif
#ifdef _SC_PAGESIZE
                { "SC_PAGESIZE", _SC_PAGESIZE },
#endif
#ifdef _SC_PAGE_SIZE
                { "SC_PAGE_SIZE", _SC_PAGE_SIZE },
#endif
#ifdef _SC_PASS_MAX
                { "SC_PASS_MAX", _SC_PASS_MAX },
#endif
#ifdef _SC_PHYS_PAGES
                { "SC_PHYS_PAGES", _SC_PHYS_PAGES },
#endif
#ifdef _SC_PII
                { "SC_PII", _SC_PII },
#endif
#ifdef _SC_PII_INTERNET
                { "SC_PII_INTERNET", _SC_PII_INTERNET },
#endif
#ifdef _SC_PII_INTERNET_DGRAM
                { "SC_PII_INTERNET_DGRAM", _SC_PII_INTERNET_DGRAM },
#endif
#ifdef _SC_PII_INTERNET_STREAM
                { "SC_PII_INTERNET_STREAM", _SC_PII_INTERNET_STREAM },
#endif
#ifdef _SC_PII_OSI
                { "SC_PII_OSI", _SC_PII_OSI },
#endif
#ifdef _SC_PII_OSI_CLTS
                { "SC_PII_OSI_CLTS", _SC_PII_OSI_CLTS },
#endif
#ifdef _SC_PII_OSI_COTS
                { "SC_PII_OSI_COTS", _SC_PII_OSI_COTS },
#endif
#ifdef _SC_PII_OSI_M
                { "SC_PII_OSI_M", _SC_PII_OSI_M },
#endif
#ifdef _SC_PII_SOCKET
                { "SC_PII_SOCKET", _SC_PII_SOCKET },
#endif
#ifdef _SC_PII_XTI
                { "SC_PII_XTI", _SC_PII_XTI },
#endif
#ifdef _SC_POLL
                { "SC_POLL", _SC_POLL },
#endif
#ifdef _SC_PRIORITIZED_IO
                { "SC_PRIORITIZED_IO", _SC_PRIORITIZED_IO },
#endif
#ifdef _SC_PRIORITY_SCHEDULING
                { "SC_PRIORITY_SCHEDULING", _SC_PRIORITY_SCHEDULING },
#endif
#ifdef _SC_REALTIME_SIGNALS
                { "SC_REALTIME_SIGNALS", _SC_REALTIME_SIGNALS },
#endif
#ifdef _SC_RE_DUP_MAX
                { "SC_RE_DUP_MAX", _SC_RE_DUP_MAX },
#endif
#ifdef _SC_RTSIG_MAX
                { "SC_RTSIG_MAX", _SC_RTSIG_MAX },
#endif
#ifdef _SC_SAVED_IDS
                { "SC_SAVED_IDS", _SC_SAVED_IDS },
#endif
#ifdef _SC_SCHAR_MAX
                { "SC_SCHAR_MAX", _SC_SCHAR_MAX },
#endif
#ifdef _SC_SCHAR_MIN
                { "SC_SCHAR_MIN", _SC_SCHAR_MIN },
#endif
#ifdef _SC_SELECT
                { "SC_SELECT", _SC_SELECT },
#endif
#ifdef _SC_SEMAPHORES
                { "SC_SEMAPHORES", _SC_SEMAPHORES },
#endif
#ifdef _SC_SEM_NSEMS_MAX
                { "SC_SEM_NSEMS_MAX", _SC_SEM_NSEMS_MAX },
#endif
#ifdef _SC_SEM_VALUE_MAX
                { "SC_SEM_VALUE_MAX", _SC_SEM_VALUE_MAX },
#endif
#ifdef _SC_SHARED_MEMORY_OBJECTS
                { "SC_SHARED_MEMORY_OBJECTS", _SC_SHARED_MEMORY_OBJECTS },
#endif
#ifdef _SC_SHRT_MAX
                { "SC_SHRT_MAX", _SC_SHRT_MAX },
#endif
#ifdef _SC_SHRT_MIN
                { "SC_SHRT_MIN", _SC_SHRT_MIN },
#endif
#ifdef _SC_SIGQUEUE_MAX
                { "SC_SIGQUEUE_MAX", _SC_SIGQUEUE_MAX },
#endif
#ifdef _SC_SSIZE_MAX
                { "SC_SSIZE_MAX", _SC_SSIZE_MAX },
#endif
#ifdef _SC_STREAM_MAX
                { "SC_STREAM_MAX", _SC_STREAM_MAX },
#endif
#ifdef _SC_SYNCHRONIZED_IO
                { "SC_SYNCHRONIZED_IO", _SC_SYNCHRONIZED_IO },
#endif
#ifdef _SC_THREADS
                { "SC_THREADS", _SC_THREADS },
#endif
#ifdef _SC_THREAD_ATTR_STACKADDR
                { "SC_THREAD_ATTR_STACKADDR", _SC_THREAD_ATTR_STACKADDR },
#endif
#ifdef _SC_THREAD_ATTR_STACKSIZE
                { "SC_THREAD_ATTR_STACKSIZE", _SC_THREAD_ATTR_STACKSIZE },
#endif
#ifdef _SC_THREAD_DESTRUCTOR_ITERATIONS
                { "SC_THREAD_DESTRUCTOR_ITERATIONS", _SC_THREAD_DESTRUCTOR_ITERATIONS },
#endif
#ifdef _SC_THREAD_KEYS_MAX
                { "SC_THREAD_KEYS_MAX", _SC_THREAD_KEYS_MAX },
#endif
#ifdef _SC_THREAD_PRIORITY_SCHEDULING
                { "SC_THREAD_PRIORITY_SCHEDULING", _SC_THREAD_PRIORITY_SCHEDULING },
#endif
#ifdef _SC_THREAD_PRIO_INHERIT
                { "SC_THREAD_PRIO_INHERIT", _SC_THREAD_PRIO_INHERIT },
#endif
#ifdef _SC_THREAD_PRIO_PROTECT
                { "SC_THREAD_PRIO_PROTECT", _SC_THREAD_PRIO_PROTECT },
#endif
#ifdef _SC_THREAD_PROCESS_SHARED
                { "SC_THREAD_PROCESS_SHARED", _SC_THREAD_PROCESS_SHARED },
#endif
#ifdef _SC_THREAD_SAFE_FUNCTIONS
                { "SC_THREAD_SAFE_FUNCTIONS", _SC_THREAD_SAFE_FUNCTIONS },
#endif
#ifdef _SC_THREAD_STACK_MIN
                { "SC_THREAD_STACK_MIN", _SC_THREAD_STACK_MIN },
#endif
#ifdef _SC_THREAD_THREADS_MAX
                { "SC_THREAD_THREADS_MAX", _SC_THREAD_THREADS_MAX },
#endif
#ifdef _SC_TIMERS
                { "SC_TIMERS", _SC_TIMERS },
#endif
#ifdef _SC_TIMER_MAX
                { "SC_TIMER_MAX", _SC_TIMER_MAX },
#endif
#ifdef _SC_TTY_NAME_MAX
                { "SC_TTY_NAME_MAX", _SC_TTY_NAME_MAX },
#endif
#ifdef _SC_TZNAME_MAX
                { "SC_TZNAME_MAX", _SC_TZNAME_MAX },
#endif
#ifdef _SC_T_IOV_MAX
                { "SC_T_IOV_MAX", _SC_T_IOV_MAX },
#endif
#ifdef _SC_UCHAR_MAX
                { "SC_UCHAR_MAX", _SC_UCHAR_MAX },
#endif
#ifdef _SC_UINT_MAX
                { "SC_UINT_MAX", _SC_UINT_MAX },
#endif
#ifdef _SC_UIO_MAXIOV
                { "SC_UIO_MAXIOV", _SC_UIO_MAXIOV },
#endif
#ifdef _SC_ULONG_MAX
                { "SC_ULONG_MAX", _SC_ULONG_MAX },
#endif
#ifdef _SC_USHRT_MAX
                { "SC_USHRT_MAX", _SC_USHRT_MAX },
#endif
#ifdef _SC_VERSION
                { "SC_VERSION", _SC_VERSION },
#endif
#ifdef _SC_WORD_BIT
                { "SC_WORD_BIT", _SC_WORD_BIT },
#endif
#ifdef _SC_XBS5_ILP32_OFF32
                { "SC_XBS5_ILP32_OFF32", _SC_XBS5_ILP32_OFF32 },
#endif
#ifdef _SC_XBS5_ILP32_OFFBIG
                { "SC_XBS5_ILP32_OFFBIG", _SC_XBS5_ILP32_OFFBIG },
#endif
#ifdef _SC_XBS5_LP64_OFF64
                { "SC_XBS5_LP64_OFF64", _SC_XBS5_LP64_OFF64 },
#endif
#ifdef _SC_XBS5_LPBIG_OFFBIG
                { "SC_XBS5_LPBIG_OFFBIG", _SC_XBS5_LPBIG_OFFBIG },
#endif
#ifdef _SC_XOPEN_CRYPT
                { "SC_XOPEN_CRYPT", _SC_XOPEN_CRYPT },
#endif
#ifdef _SC_XOPEN_ENH_I18N
                { "SC_XOPEN_ENH_I18N", _SC_XOPEN_ENH_I18N },
#endif
#ifdef _SC_XOPEN_LEGACY
                { "SC_XOPEN_LEGACY", _SC_XOPEN_LEGACY },
#endif
#ifdef _SC_XOPEN_REALTIME
                { "SC_XOPEN_REALTIME", _SC_XOPEN_REALTIME },
#endif
#ifdef _SC_XOPEN_REALTIME_THREADS
                { "SC_XOPEN_REALTIME_THREADS", _SC_XOPEN_REALTIME_THREADS },
#endif
#ifdef _SC_XOPEN_SHM
                { "SC_XOPEN_SHM", _SC_XOPEN_SHM },
#endif
#ifdef _SC_XOPEN_UNIX
                { "SC_XOPEN_UNIX", _SC_XOPEN_UNIX },
#endif
#ifdef _SC_XOPEN_VERSION
                { "SC_XOPEN_VERSION", _SC_XOPEN_VERSION },
#endif
#ifdef _SC_XOPEN_XCU_VERSION
                { "SC_XOPEN_XCU_VERSION", _SC_XOPEN_XCU_VERSION },
#endif
#ifdef _SC_XOPEN_XPG2
                { "SC_XOPEN_XPG2", _SC_XOPEN_XPG2 },
#endif
#ifdef _SC_XOPEN_XPG3
                { "SC_XOPEN_XPG3", _SC_XOPEN_XPG3 },
#endif
#ifdef _SC_XOPEN_XPG4
                { "SC_XOPEN_XPG4", _SC_XOPEN_XPG4 },
#endif
            };

            // (a configuration name (sysconf's, pathconf's or confstr's): a str of aNames or an integer (CPython's conv_confname))
            bool conf_name(object* aValue, std::vector<std::pair<char const*, int>> const& aNames, int& aResult)
            {
                if (kind_of(aValue) == NEOS_PY_STR)
                {
                    auto const& n = as<str_object>(aValue)->value;
                    auto const found = std::find_if(aNames.begin(), aNames.end(), [&](auto const& e) { return n == e.first; });
                    if (found == aNames.end())
                    {
                        error("ValueError", "unrecognized configuration name");
                        return false;
                    }
                    aResult = found->second;
                    return true;
                }
                long long value = 0;
                if (!is_int_like(aValue) || !to_index(aValue, value))
                {
                    clear_error();
                    error("TypeError", "configuration names must be strings or integers");
                    return false;
                }
                aResult = static_cast<int>(value);
                return true;
            }

#if !defined(_WIN32)
            object* statvfs_result(struct statvfs const& v)
            {
                auto const& t = statvfs_result_type();
                return make_structseq(t, { make_int(static_cast<long long>(v.f_bsize)), make_int(static_cast<long long>(v.f_frsize)), make_int(static_cast<long long>(v.f_blocks)),
                    make_int(static_cast<long long>(v.f_bfree)), make_int(static_cast<long long>(v.f_bavail)), make_int(static_cast<long long>(v.f_files)),
                    make_int(static_cast<long long>(v.f_ffree)), make_int(static_cast<long long>(v.f_favail)), make_int(static_cast<long long>(v.f_flag)),
                    make_int(static_cast<long long>(v.f_namemax)), make_int(static_cast<long long>(v.f_fsid)) });
            }

            // (a file descriptor argument (PyObject_AsFileDescriptor): an int, or the result of an object's fileno())
            bool fd_or_fileno(object* aValue, long long& aResult, char const* aFunction)
            {
                (void)aFunction;
                if (is_int_like(aValue))
                {
                    if (!to_index(aValue, aResult))
                        return false;
                }
                else
                {
                    ref const fileno{ getattr(aValue, "fileno") };
                    if (!fileno)
                    {
                        clear_error();
                        error("TypeError", "argument must be an int, or have a fileno() method.");
                        return false;
                    }
                    ref const r{ invoke(fileno.get(), {}) };
                    if (!r)
                        return false;
                    if (!is_int_like(r.get()))
                    {
                        error("TypeError", "fileno() returned a non-integer");
                        return false;
                    }
                    if (!to_index(r.get(), aResult))
                        return false;
                }
                if (aResult < 0)
                {
                    error("ValueError", "file descriptor cannot be a negative integer (" + std::to_string(aResult) + ")");
                    return false;
                }
                return true;
            }

            // (a function's keyword-only parameters: at most aCount positional arguments (aExactly: all of them required))
            bool positional_at_most(char const* aFunction, std::vector<object*> const& aArgs, std::size_t aCount, bool aExactly)
            {
                if (aArgs.size() <= aCount)
                    return true;
                error("TypeError", std::string{ aFunction } + "() takes " + (aExactly ? "exactly " : "at most ") + std::to_string(aCount) + " positional argument" + 
                    (aCount == 1u ? "" : "s") + " (" + std::to_string(aArgs.size()) + " given)");
                return false;
            }

            // (a signal's handlers, after a call that EINTR interrupted (PEP 475: the call is made again unless one raised))
            bool check_signals() { return signals_check(); }

            // (os.register_at_fork's functions: before, after_in_child and after_in_parent)
            std::vector<object*>* fork_hooks()
            {
                static std::vector<object*> sHooks[3];
                return sHooks;
            }

            // (fork's hooks of a kind (0: before (the last registered first), 1: after_in_child, 2: after_in_parent): an exception is 
            // unraisable)
            void run_fork_hooks(int aKind)
            {
                std::vector<ref> hooks;
                for (auto h : fork_hooks()[aKind])
                    hooks.emplace_back(new_reference(h));
                if (aKind == 0)
                    std::reverse(hooks.begin(), hooks.end());
                for (auto const& h : hooks)
                    if (!ref{ invoke(h.get(), {}) })
                        write_unraisable(h.get());
            }

            // (fork() with os.register_at_fork's hooks (PyOS_BeforeFork, PyOS_AfterFork_Child and PyOS_AfterFork_Parent))
            pid_t fork_process()
            {
                run_fork_hooks(0);
                std::fflush(nullptr);
                auto const pid = ::fork();
                auto const e = errno;
                if (pid == 0)
                    run_fork_hooks(1);
                else if (pid > 0)
                    run_fork_hooks(2);
                errno = e;
                return pid;
            }

            // (login_tty: a new session whose controlling terminal is the terminal, and the standard streams; false: errno)
            bool login_terminal(int aFd)
            {
                if (::setsid() < 0)
                    return false;
#if defined(TIOCSCTTY)
                if (::ioctl(aFd, TIOCSCTTY, 0) < 0)
                    return false;
#endif
                for (int i = 0; i < 3; ++i)
                    if (::dup2(aFd, i) < 0)
                        return false;
                if (aFd > 2 && ::close(aFd) < 0)
                    return false;
                return true;
            }

            // (resource.struct_rusage)
            structseq_descriptor const& rusage_type()
            {
                return structseq_type("resource.struct_rusage", { "ru_utime", "ru_stime", "ru_maxrss", "ru_ixrss", "ru_idrss", "ru_isrss", "ru_minflt", "ru_majflt",
                    "ru_nswap", "ru_inblock", "ru_oublock", "ru_msgsnd", "ru_msgrcv", "ru_nsignals", "ru_nvcsw", "ru_nivcsw" }, 16u);
            }
            object* rusage_result(struct rusage const& r)
            {
                auto const seconds = [](timeval const& t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) * 1e-6; };
                return make_structseq(rusage_type(), { make_float(seconds(r.ru_utime)), make_float(seconds(r.ru_stime)), make_int(r.ru_maxrss), make_int(r.ru_ixrss),
                    make_int(r.ru_idrss), make_int(r.ru_isrss), make_int(r.ru_minflt), make_int(r.ru_majflt), make_int(r.ru_nswap), make_int(r.ru_inblock),
                    make_int(r.ru_oublock), make_int(r.ru_msgsnd), make_int(r.ru_msgrcv), make_int(r.ru_nsignals), make_int(r.ru_nvcsw), make_int(r.ru_nivcsw) });
            }

            // (wait3 and wait4: (pid, status, resource usage))
            object* wait_with_usage(pid_t aPid, int aOptions, bool aFour)
            {
                (void)aFour;
                int status = 0;
                struct rusage usage{};
                pid_t r = 0;
                int e = 0;
                do
                {
                    gil_release();
                    r = ::wait4(aPid, &status, aOptions, &usage);
                    e = errno;
                    gil_acquire();
                } while (r < 0 && e == EINTR && check_signals());
                if (r < 0)
                    return failed() ? nullptr : raise_os_error(e);
                return make_tuple({ make_int(r), make_int(status), rusage_result(usage) });
            }

            // (an unsigned 64-bit argument)
            bool unsigned_argument(object* aValue, std::uint64_t& aResult)
            {
                long long value = 0;
                if (!to_index(aValue, value))
                    return false;
                if (value < 0)
                {
                    error("OverflowError", "can't convert negative int to unsigned");
                    return false;
                }
                aResult = static_cast<std::uint64_t>(value);
                return true;
            }

            bool has_attribute(object* aObject, char const* aName)
            {
                ref const a{ getattr(aObject, aName) };
                if (!a)
                    clear_error();
                return static_cast<bool>(a);
            }

            object* decode_filename(std::string const& aName) { return make_str(aName); }

#if defined(__linux__)
            // (the file of an extended attribute: a path (follow_symlinks: its link's, else the link's own), or a file descriptor)
            struct xattr_target
            {
                int fd = -1;
                std::string path;
                bool follow = true;
                ssize_t get(std::string const& aName, char* aBuffer, std::size_t aSize) const
                {
                    return fd >= 0 ? ::fgetxattr(fd, aName.c_str(), aBuffer, aSize) : follow ? ::getxattr(path.c_str(), aName.c_str(), aBuffer, aSize) :
                        ::lgetxattr(path.c_str(), aName.c_str(), aBuffer, aSize);
                }
                int set(std::string const& aName, std::string_view aValue, int aFlags) const
                {
                    return fd >= 0 ? ::fsetxattr(fd, aName.c_str(), aValue.data(), aValue.size(), aFlags) : follow ? 
                        ::setxattr(path.c_str(), aName.c_str(), aValue.data(), aValue.size(), aFlags) : ::lsetxattr(path.c_str(), aName.c_str(), aValue.data(), aValue.size(), aFlags);
                }
                int remove(std::string const& aName) const
                {
                    return fd >= 0 ? ::fremovexattr(fd, aName.c_str()) : follow ? ::removexattr(path.c_str(), aName.c_str()) : ::lremovexattr(path.c_str(), aName.c_str());
                }
                ssize_t list(char* aBuffer, std::size_t aSize) const
                {
                    return fd >= 0 ? ::flistxattr(fd, aBuffer, aSize) : follow ? ::listxattr(path.c_str(), aBuffer, aSize) : ::llistxattr(path.c_str(), aBuffer, aSize);
                }
            };

            bool xattr_target_of(object* aPath, object* aFollow, char const* aFunction, xattr_target& aResult)
            {
                aResult.follow = aFollow == nullptr || truth(aFollow) == 1;
                if (is_int_like(aPath))
                {
                    long long fd = 0;
                    if (!to_index(aPath, fd))
                        return false;
                    if (!aResult.follow)
                    {
                        error("ValueError", std::string{ aFunction } + ": cannot use fd and follow_symlinks together");
                        return false;
                    }
                    aResult.fd = static_cast<int>(fd);
                    return true;
                }
                return path_argument(aPath, aResult.path, aFunction, "path", true);
            }
#endif
#if defined(__linux__)
            // (readv's and writev's buffers: a sequence of bytes-like objects (writable ones to read into))
            bool io_vectors(object* aBuffers, bool aWritable, char const* aFunction, std::vector<ref>& aHeld, std::vector<iovec>& aVectors)
            {
                if (!has_attribute(aBuffers, "__getitem__") || !has_attribute(aBuffers, "__len__"))
                {
                    error("TypeError", std::string{ aFunction } + "() arg 2 must be a sequence");
                    return false;
                }
                std::vector<object*> items;
                if (!collect(aBuffers, items))
                    return false;
                for (auto i : items)
                    aHeld.emplace_back(i);
                for (auto const& i : aHeld)
                {
                    // (PyObject_GetBuffer's errors: PyBUF_WRITABLE's of a read-only buffer, or not a bytes-like object)
                    buffer_info b;
                    if (!buffer_of(i.get(), b))
                    {
                        clear_error();
                        error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(i.get()) + "'");
                        return false;
                    }
                    if (aWritable && b.readonly)
                    {
                        error("BufferError", "Object is not writable.");
                        return false;
                    }
                    aVectors.push_back(iovec{ b.data, b.size });
                }
                return true;
            }

            // (readv, preadv (aOffset >= 0) and pwritev (aWrite): the bytes read or written)
            object* vector_io(int aFd, std::vector<iovec> const& aVectors, long long aOffset, int aFlags, bool aWrite)
            {
                ssize_t n = 0;
                int e = 0;
                do
                {
                    gil_release();
                    if (aOffset < 0)
                        n = aWrite ? ::writev(aFd, aVectors.data(), static_cast<int>(aVectors.size())) : ::readv(aFd, aVectors.data(), static_cast<int>(aVectors.size()));
                    else
                        n = aWrite ? ::pwritev2(aFd, aVectors.data(), static_cast<int>(aVectors.size()), static_cast<off_t>(aOffset), aFlags) :
                            ::preadv2(aFd, aVectors.data(), static_cast<int>(aVectors.size()), static_cast<off_t>(aOffset), aFlags);
                    e = errno;
                    gil_acquire();
                } while (n < 0 && e == EINTR && check_signals());
                if (n < 0)
                    return failed() ? nullptr : raise_os_error(e);
                return make_int(static_cast<long long>(n));
            }

            object* timer_values(itimerspec const& aValue, bool aNanoseconds);

            // (timerfd_settime(fd, *, flags=0, initial=0.0, interval=0.0) and timerfd_settime_ns: the previous (initial, interval))
            object* timerfd_set(std::vector<object*> const& aArgs, object* aKwargs, bool aNanoseconds)
            {
                char const* const name = aNanoseconds ? "timerfd_settime_ns" : "timerfd_settime";
                std::vector<object*> a;
                long long fd = 0, flags = 0;
                if (!parse_arguments(name, aArgs, aKwargs, { "fd", "flags", "initial", "interval" }, 1u, 1u, a) || !fd_or_fileno(a[0], fd, name) ||
                    (a[1] != nullptr && !int_argument(a[1], flags, name)))
                    return nullptr;
                if (aArgs.size() > 1u)
                    return error("TypeError", std::string{ name } + "() takes exactly 1 positional argument (" + std::to_string(aArgs.size()) + " given)");
                // (seconds (a float or int: not negative), or nanoseconds (an int))
                auto const spec = [&](object* aValue, timespec& aResult) -> bool
                {
                    aResult = {};
                    if (aValue == nullptr)
                        return true;
                    if (aNanoseconds)
                    {
                        long long ns = 0;
                        if (!to_index(aValue, ns))
                            return false;
                        if (ns < 0)
                        {
                            error("ValueError", aValue == a[2] ? "initial must be non-negative" : "interval must be non-negative");
                            return false;
                        }
                        aResult.tv_sec = static_cast<time_t>(ns / 1000000000ll);
                        aResult.tv_nsec = static_cast<long>(ns % 1000000000ll);
                        return true;
                    }
                    double seconds = 0.0;
                    if (!to_double(aValue, seconds))
                        return false;
                    if (seconds < 0.0)
                    {
                        error("ValueError", aValue == a[2] ? "initial must be non-negative" : "interval must be non-negative");
                        return false;
                    }
                    auto const whole = std::floor(seconds);
                    aResult.tv_sec = static_cast<time_t>(whole);
                    aResult.tv_nsec = static_cast<long>(std::llround((seconds - whole) * 1e9));
                    if (aResult.tv_nsec >= 1000000000l)
                    {
                        ++aResult.tv_sec;
                        aResult.tv_nsec -= 1000000000l;
                    }
                    return true;
                };
                itimerspec value{}, old{};
                if (!spec(a[2], value.it_value) || !spec(a[3], value.it_interval))
                    return nullptr;
                if (::timerfd_settime(static_cast<int>(fd), static_cast<int>(flags), &value, &old) != 0)
                    return raise_os_error(errno);
                return timer_values(old, aNanoseconds);
            }

            object* timer_values(itimerspec const& aValue, bool aNanoseconds)
            {
                auto const of = [&](timespec const& t) -> object*
                {
                    if (aNanoseconds)
                        return make_int(static_cast<long long>(t.tv_sec) * 1000000000ll + t.tv_nsec);
                    return make_float(static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9);
                };
                return make_tuple({ of(aValue.it_value), of(aValue.it_interval) });
            }

            object* timerfd_get(std::vector<object*> const& aArgs, object* aKwargs, bool aNanoseconds)
            {
                char const* const name = aNanoseconds ? "timerfd_gettime_ns" : "timerfd_gettime";
                std::vector<object*> a;
                long long fd = 0;
                if (!parse_arguments(name, aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !fd_or_fileno(a[0], fd, name))
                    return nullptr;
                itimerspec value{};
                if (::timerfd_gettime(static_cast<int>(fd), &value) != 0)
                    return raise_os_error(errno);
                return timer_values(value, aNanoseconds);
            }
#endif
#endif

            builtin_entry const sPosixFunctions[] =
            {
                { "open", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        long long flags = 0, mode = 0777;
                        int dirFd = 0;
                        if (!parse_arguments("open", aArgs, aKwargs, { "path", "flags", "mode", "dir_fd" }, 2u, 0u, a) || !path_of(a, 0u, p, "open") ||
                            !int_argument(a[1], flags, "open") || (a[2] != nullptr && !int_argument(a[2], mode, "open")) || !dir_fd_of(a[3], dirFd))
                            return nullptr;
                        // (a file opened to write (or created): restricted)
                        if ((flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0 && !host_unrestricted("os.open"))
                            return nullptr;
#if defined(_WIN32)
                        auto const fd = ::_open(p.c_str(), static_cast<int>(flags) | _O_BINARY, static_cast<int>(mode & 0600));
#else
                        auto const fd = ::openat(dirFd, p.c_str(), static_cast<int>(flags) | O_CLOEXEC, static_cast<mode_t>(mode));
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
                        return fd_stat(static_cast<int>(fd), nullptr);
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
#if !defined(_WIN32)
                // (Modules/posixmodule.c's os_openpty_impl: by posix_openpt, grantpt, unlockpt and ptsname; both ends not inheritable)
                { "openpty", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "openpty") || !aArgs.empty())
                            return error("TypeError", "openpty() takes no arguments");
                        int const master = ::posix_openpt(O_RDWR | O_NOCTTY);
                        if (master < 0)
                            return raise_os_error(errno);
                        char const* name = nullptr;
                        if (::grantpt(master) != 0 || ::unlockpt(master) != 0 || (name = ::ptsname(master)) == nullptr)
                        {
                            int const e = errno;
                            ::close(master);
                            return raise_os_error(e);
                        }
                        int const slave = ::open(name, O_RDWR | O_NOCTTY);
                        if (slave < 0)
                        {
                            int const e = errno;
                            ::close(master);
                            return raise_os_error(e);
                        }
                        ::fcntl(master, F_SETFD, FD_CLOEXEC);
                        ::fcntl(slave, F_SETFD, FD_CLOEXEC);
                        return make_tuple({ make_int(master), make_int(slave) });
                    } },
#endif
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
                        std::optional<int> fd;
                        if (!parse_arguments("truncate", aArgs, aKwargs, { "path", "length" }, 2u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "truncate") || 
                            !int_argument(a[1], length, "truncate"))
                            return nullptr;
#if !defined(_WIN32)
                        // (a file descriptor: ftruncate (its error has no file name, as CPython's))
                        if (fd)
                            return ::ftruncate(*fd, static_cast<off_t>(length)) != 0 ? raise_os_error(errno) : none();
#endif
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
                        // (a signal to itself: its handler called before kill returns (as CPython's, which checks at once))
                        if (pid == static_cast<long long>(::getpid()) || pid == 0 || pid == -static_cast<long long>(::getpgrp()))
                            signal_wait(static_cast<int>(signal));
                        if (!signals_check())
                            return nullptr;
#endif
                        return none();
                    } },
                { "times", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("times", aArgs, aKwargs))
                            return nullptr;
                        auto const& t = times_result_type();
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
                { "getuid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getuid", aArgs, aKwargs)) return nullptr; return make_int(static_cast<long long>(::getuid())); } },
                { "geteuid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("geteuid", aArgs, aKwargs)) return nullptr; return make_int(static_cast<long long>(::geteuid())); } },
                { "getgid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getgid", aArgs, aKwargs)) return nullptr; return make_int(static_cast<long long>(::getgid())); } },
                { "getegid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getegid", aArgs, aKwargs)) return nullptr; return make_int(static_cast<long long>(::getegid())); } },
                { "getpgrp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getpgrp", aArgs, aKwargs)) return nullptr; return make_int(static_cast<long long>(::getpgrp())); } },
                { "getgroups", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getgroups", aArgs, aKwargs)) return nullptr;
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
                { "setsid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("setsid", aArgs, aKwargs)) return nullptr; return ::setsid() < 0 ? raise_os_error(errno) : none(); } },
                // (the process's ids (setuid, setgid, seteuid, setegid; setreuid, setregid; setresuid, setresgid (-1: unchanged)), its 
                // groups, and its process group)
                { "setuid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setuid", aArgs, aKwargs, 1u, [](long long const* v) { return ::setuid(static_cast<uid_t>(v[0])); }); } },
                { "setgid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setgid", aArgs, aKwargs, 1u, [](long long const* v) { return ::setgid(static_cast<gid_t>(v[0])); }); } },
                { "seteuid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("seteuid", aArgs, aKwargs, 1u, [](long long const* v) { return ::seteuid(static_cast<uid_t>(v[0])); }); } },
                { "setegid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setegid", aArgs, aKwargs, 1u, [](long long const* v) { return ::setegid(static_cast<gid_t>(v[0])); }); } },
                { "setreuid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setreuid", aArgs, aKwargs, 2u, [](long long const* v) { return ::setreuid(static_cast<uid_t>(v[0]), static_cast<uid_t>(v[1])); }); } },
                { "setregid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setregid", aArgs, aKwargs, 2u, [](long long const* v) { return ::setregid(static_cast<gid_t>(v[0]), static_cast<gid_t>(v[1])); }); } },
#if defined(__linux__)
                { "setresuid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setresuid", aArgs, aKwargs, 3u, [](long long const* v) { return ::setresuid(static_cast<uid_t>(v[0]), static_cast<uid_t>(v[1]), static_cast<uid_t>(v[2])); }); } },
                { "setresgid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setresgid", aArgs, aKwargs, 3u, [](long long const* v) { return ::setresgid(static_cast<gid_t>(v[0]), static_cast<gid_t>(v[1]), static_cast<gid_t>(v[2])); }); } },
                { "getresuid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getresuid", aArgs, aKwargs)) return nullptr;
                        uid_t r = 0, e = 0, s = 0;
                        if (::getresuid(&r, &e, &s) != 0)
                            return raise_os_error(errno);
                        return make_tuple({ make_int(static_cast<long long>(r)), make_int(static_cast<long long>(e)), make_int(static_cast<long long>(s)) });
                    } },
                { "getresgid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getresgid", aArgs, aKwargs)) return nullptr;
                        gid_t r = 0, e = 0, s = 0;
                        if (::getresgid(&r, &e, &s) != 0)
                            return raise_os_error(errno);
                        return make_tuple({ make_int(static_cast<long long>(r)), make_int(static_cast<long long>(e)), make_int(static_cast<long long>(s)) });
                    } },
#endif
                { "setgroups", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.setgroups"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("setgroups", aArgs, aKwargs, { "groups" }, 1u, 1u, a))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[0], items))
                            return error("TypeError", "setgroups argument must be a sequence");
                        std::vector<gid_t> groups;
                        bool ok = true;
                        for (auto i : items)
                        {
                            long long g = 0;
                            if (ok && (kind_of(i) != NEOS_PY_INT || !id_argument(i, g, true)))
                                ok = false;
                            groups.push_back(static_cast<gid_t>(g));
                            release(i);
                        }
                        if (!ok)
                            return failed() ? nullptr : error("TypeError", "groups must be integers");
                        if (::setgroups(groups.size(), groups.empty() ? nullptr : groups.data()) != 0)
                            return raise_os_error(errno);
                        return none();
                    } },
                { "initgroups", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.initgroups"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string user;
                        long long group = 0;
                        if (!parse_arguments("initgroups", aArgs, aKwargs, { "username", "gid" }, 2u, 2u, a) || !path_argument(a[0], user, "initgroups", "username") ||
                            !id_argument(a[1], group, true))
                            return nullptr;
                        if (::initgroups(user.c_str(), static_cast<gid_t>(group)) != 0)
                            return raise_os_error(errno);
                        return none();
                    } },
                { "setpgid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return set_ids("setpgid", aArgs, aKwargs, 2u, [](long long const* v) { return ::setpgid(static_cast<pid_t>(v[0]), static_cast<pid_t>(v[1])); }); } },
                { "setpgrp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("setpgrp", aArgs, aKwargs)) return nullptr; return ::setpgid(0, 0) < 0 ? raise_os_error(errno) : none(); } },
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
                { "fork", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("fork", aArgs, aKwargs)) return nullptr;
                        if (!host_unrestricted("os.fork"))
                            return nullptr;
                        auto const pid = fork_process();
                        return pid < 0 ? (failed() ? nullptr : raise_os_error(errno)) : make_int(pid);
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
                { "wait", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("wait", aArgs, aKwargs)) return nullptr;
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
                        // (as CPython's os_execv_impl: a tuple or list, not empty, whose first item is not empty)
                        if (kind_of(a[1]) != NEOS_PY_TUPLE && kind_of(a[1]) != NEOS_PY_LIST)
                            return error("TypeError", "execv() arg 2 must be a tuple or list");
                        std::vector<std::string> args;
                        if (!exec_arguments(a[1], args, "execv"))
                            return nullptr;
                        if (args.empty())
                            return error("ValueError", "execv() arg 2 must not be empty");
                        if (args[0].empty())
                            return error("ValueError", "execv() arg 2 first element cannot be empty");
                        std::vector<char*> argv;
                        for (auto& x : args)
                            argv.push_back(x.data());
                        argv.push_back(nullptr);
                        std::fflush(nullptr);
                        ::execv(p.c_str(), argv.data());
                        return raise_os_error(errno, a[0]);
                    } },
                // (as CPython's os_execve_impl: a path (or a file descriptor: fexecve), argv (a tuple or list, not empty, whose first item is not
                // empty) and env (a mapping, whose names are not empty and have no '=' after their first character))
                { "execve", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.execve"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        std::optional<int> fd;
                        if (!parse_arguments("execve", aArgs, aKwargs, { "path", "argv", "env" }, 3u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "execve"))
                            return nullptr;
                        if (kind_of(a[1]) != NEOS_PY_TUPLE && kind_of(a[1]) != NEOS_PY_LIST)
                            return error("TypeError", "execve: argv must be a tuple or list");
                        std::vector<std::string> args;
                        if (!exec_arguments(a[1], args, "execve"))
                            return nullptr;
                        if (args.empty())
                            return error("ValueError", "execve: argv must not be empty");
                        if (!is_mapping(a[2]))
                            return error("TypeError", "execve: environment must be a mapping object");
                        if (args[0].empty())
                            return error("ValueError", "execve: argv first element cannot be empty");
                        std::vector<std::string> environment;
                        if (!environment_list(a[2], environment, "execve"))
                            return nullptr;
                        std::vector<char*> argv;
                        for (auto& x : args)
                            argv.push_back(x.data());
                        argv.push_back(nullptr);
                        std::vector<char*> envp;
                        for (auto& x : environment)
                            envp.push_back(x.data());
                        envp.push_back(nullptr);
                        std::fflush(nullptr);
                        if (fd)
                            ::fexecve(*fd, argv.data(), envp.data());
                        else
                            ::execve(p.c_str(), argv.data(), envp.data());
                        return raise_os_error(errno, a[0]);
                    } },
                { "posix_spawn", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return posix_spawn_call(false, aArgs, aKwargs); } },
                { "posix_spawnp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return posix_spawn_call(true, aArgs, aKwargs); } },
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
                { "uname", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("uname", aArgs, aKwargs)) return nullptr;
                        struct utsname u{};
                        if (::uname(&u) != 0)
                            return raise_os_error(errno);
                        auto const& t = uname_result_type();
                        return make_structseq(t, { make_str(u.sysname), make_str(u.nodename), make_str(u.release), make_str(u.version), make_str(u.machine) });
                    } },
                { "getloadavg", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("getloadavg", aArgs, aKwargs)) return nullptr;
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
                        int dirFd = 0;
                        if (!parse_arguments("symlink", aArgs, aKwargs, { "src", "dst", "target_is_directory", "dir_fd" }, 2u, 0u, a) || !path_of(a, 0u, from, "symlink", "src") ||
                            !path_of(a, 1u, to, "symlink", "dst") || !dir_fd_of(a[3], dirFd))
                            return nullptr;
                        if (::symlinkat(from.c_str(), dirFd, to.c_str()) != 0)
                            return raise_os_error(errno, a[0], a[1]);
                        return none();
                    } },
                { "readlink", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        int dirFd = 0;
                        if (!parse_arguments("readlink", aArgs, aKwargs, { "path", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "readlink") || !dir_fd_of(a[1], dirFd))
                            return nullptr;
                        std::string buffer(4096u, '\0');
                        auto const n = ::readlinkat(dirFd, p.c_str(), buffer.data(), buffer.size());
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
                        int fromFd = 0, toFd = 0;
                        if (!parse_arguments("link", aArgs, aKwargs, { "src", "dst", "src_dir_fd", "dst_dir_fd", "follow_symlinks" }, 2u, 0u, a) ||
                            !path_of(a, 0u, from, "link", "src") || !path_of(a, 1u, to, "link", "dst") || !dir_fd_of(a[2], fromFd) || !dir_fd_of(a[3], toFd))
                            return nullptr;
                        // (follow_symlinks (the default): a link to the file that src refers to, as CPython's linkat with AT_SYMLINK_FOLLOW)
                        if (::linkat(fromFd, from.c_str(), toFd, to.c_str(), follow_of(a[4]) ? AT_SYMLINK_FOLLOW : 0) != 0)
                            return raise_os_error(errno, a[0], a[1]);
                        return none();
                    } },
                // (fchmod, fchown, fchdir and fstatvfs (a descriptor's: errors without file names) and lchown (as CPython's))
                { "fchmod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.fchmod"))
                            return nullptr;
                        std::vector<object*> a;
                        long long fd = 0, mode = 0;
                        if (!parse_arguments("fchmod", aArgs, aKwargs, { "fd", "mode" }, 2u, 0u, a) || !int_argument(a[0], fd, "fchmod") || !int_argument(a[1], mode, "fchmod"))
                            return nullptr;
                        return ::fchmod(static_cast<int>(fd), static_cast<mode_t>(mode)) != 0 ? raise_os_error(errno) : none();
                    } },
                { "fchown", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.fchown"))
                            return nullptr;
                        std::vector<object*> a;
                        long long fd = 0, uid = 0, gid = 0;
                        if (!parse_arguments("fchown", aArgs, aKwargs, { "fd", "uid", "gid" }, 3u, 0u, a) || !int_argument(a[0], fd, "fchown") || !id_argument(a[1], uid, false) ||
                            !id_argument(a[2], gid, true))
                            return nullptr;
                        return ::fchown(static_cast<int>(fd), static_cast<uid_t>(uid), static_cast<gid_t>(gid)) != 0 ? raise_os_error(errno) : none();
                    } },
                { "fchdir", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.fchdir"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!parse_arguments("fchdir", aArgs, aKwargs, { "fd" }, 1u, 0u, a))
                            return nullptr;
                        // (an int, or an object with fileno() (CPython's fildes_converter))
                        long long fd = 0;
                        if (is_int_like(a[0]))
                        {
                            if (!to_index(a[0], fd))
                                return nullptr;
                        }
                        else
                        {
                            ref const fileno{ getattr(a[0], "fileno") };
                            if (!fileno)
                            {
                                clear_error();
                                return error("TypeError", "argument must be an int, or have a fileno() method.");
                            }
                            ref const number{ invoke(fileno.get(), {}) };
                            if (!number)
                                return nullptr;
                            if (!is_int_like(number.get()))
                                return error("TypeError", "fileno() returned a non-integer");
                            if (!to_index(number.get(), fd))
                                return nullptr;
                        }
                        if (fd < 0)
                            return error("ValueError", "file descriptor cannot be a negative integer (" + std::to_string(fd) + ")");
                        return ::fchdir(static_cast<int>(fd)) != 0 ? raise_os_error(errno) : none();
                    } },
                { "fstatvfs", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("fstatvfs", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !int_argument(a[0], fd, "fstatvfs"))
                            return nullptr;
                        struct statvfs v{};
                        if (::fstatvfs(static_cast<int>(fd), &v) != 0)
                            return raise_os_error(errno);
                        return statvfs_result(v);
                    } },
                { "lchown", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.lchown"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        long long uid = 0, gid = 0;
                        if (!parse_arguments("lchown", aArgs, aKwargs, { "path", "uid", "gid" }, 3u, 0u, a) || !path_of(a, 0u, p, "lchown") || !id_argument(a[1], uid, false) ||
                            !id_argument(a[2], gid, true))
                            return nullptr;
                        return ::lchown(p.c_str(), static_cast<uid_t>(uid), static_cast<gid_t>(gid)) != 0 ? raise_os_error(errno, a[0]) : none();
                    } },
                { "chown", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.chown"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        long long uid = 0, gid = 0;
                        int dirFd = 0;
                        std::optional<int> fd;
                        if (!parse_arguments("chown", aArgs, aKwargs, { "path", "uid", "gid", "dir_fd", "follow_symlinks" }, 3u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "chown") ||
                            !id_argument(a[1], uid, false) || !id_argument(a[2], gid, true) || !dir_fd_of(a[3], dirFd) || !fd_alone("chown", fd, dirFd, follow_of(a[4])))
                            return nullptr;
                        if (fd)
                            return ::fchown(*fd, static_cast<uid_t>(uid), static_cast<gid_t>(gid)) != 0 ? raise_os_error(errno, a[0]) : none();
                        if (::fchownat(dirFd, p.c_str(), static_cast<uid_t>(uid), static_cast<gid_t>(gid), follow_of(a[4]) ? 0 : AT_SYMLINK_NOFOLLOW) != 0)
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
                        int dirFd = 0;
                        if (!parse_arguments("mkfifo", aArgs, aKwargs, { "path", "mode", "dir_fd" }, 1u, 0u, a) || !path_of(a, 0u, p, "mkfifo") ||
                            (a[1] != nullptr && !int_argument(a[1], mode, "mkfifo")) || !dir_fd_of(a[2], dirFd))
                            return nullptr;
                        if (::mkfifoat(dirFd, p.c_str(), static_cast<mode_t>(mode)) != 0)
                            return raise_os_error(errno, a[0]);
                        return none();
                    } },
                { "sync", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { if (!no_arguments("sync", aArgs, aKwargs)) return nullptr; ::sync(); return none(); } },
                { "sysconf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sysconf", aArgs, aKwargs, { "name" }, 1u, 1u, a))
                            return nullptr;
                        long long name = 0;
                        if (kind_of(a[0]) == NEOS_PY_STR)
                        {
                            auto const& n = as<str_object>(a[0])->value;
                            auto const found = std::find_if(sSysconfNames.begin(), sSysconfNames.end(), [&](auto const& e) { return n == e.first; });
                            if (found == sSysconfNames.end())
                                return error("ValueError", "unrecognized configuration name");
                            name = found->second;
                        }
                        else if (is_int_like(a[0]))
                        {
                            if (!int_argument(a[0], name, "sysconf"))
                                return nullptr;
                        }
                        else
                            return error("TypeError", "configuration names must be strings or integers");
                        errno = 0;
                        auto const r = ::sysconf(static_cast<int>(name));
                        if (r == -1 && errno != 0)
                            return raise_os_error(errno);
                        return make_int(r);
                    } },
                // (as CPython's os_pathconf_impl, os_fpathconf_impl and os_confstr_impl: EINVAL (the path's or the name's) has no file name)
                { "pathconf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        std::optional<int> fd;
                        int name = 0;
                        if (!parse_arguments("pathconf", aArgs, aKwargs, { "path", "name" }, 2u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "pathconf") || 
                            !conf_name(a[1], sPathconfNames, name))
                            return nullptr;
                        errno = 0;
                        auto const r = fd ? ::fpathconf(*fd, name) : ::pathconf(p.c_str(), name);
                        if (r == -1 && errno != 0)
                            return errno == EINVAL ? raise_os_error(errno) : raise_os_error(errno, a[0]);
                        return make_int(r);
                    } },
                { "fpathconf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        std::optional<int> fd;
                        int name = 0;
                        if (!parse_arguments("fpathconf", aArgs, aKwargs, { "fd", "name" }, 2u, 2u, a) || !path_or_fd_of(a, 0u, p, fd, "fpathconf") || 
                            !conf_name(a[1], sPathconfNames, name))
                            return nullptr;
                        errno = 0;
                        auto const r = fd ? ::fpathconf(*fd, name) : ::pathconf(p.c_str(), name);
                        if (r == -1 && errno != 0)
                            return raise_os_error(errno);
                        return make_int(r);
                    } },
                { "confstr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int name = 0;
                        if (!parse_arguments("confstr", aArgs, aKwargs, { "name" }, 1u, 1u, a) || !conf_name(a[0], sConfstrNames, name))
                            return nullptr;
                        // (no value: None)
                        errno = 0;
                        auto const length = ::confstr(name, nullptr, 0u);
                        if (length == 0u)
                            return errno != 0 ? raise_os_error(errno) : none();
                        std::string value(length, '\0');
                        ::confstr(name, value.data(), value.size());
                        value.resize(length - 1u);
                        return make_str(value);
                    } },
                { "statvfs", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string p;
                        std::optional<int> fd;
                        if (!parse_arguments("statvfs", aArgs, aKwargs, { "path" }, 1u, 0u, a) || !path_or_fd_of(a, 0u, p, fd, "statvfs"))
                            return nullptr;
                        struct statvfs v{};
                        if ((fd ? ::fstatvfs(*fd, &v) : ::statvfs(p.c_str(), &v)) != 0)
                            return raise_os_error(errno, a[0]);
                        return statvfs_result(v);
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
                // --- the scheduler (sched.h)
                { "sched_yield", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sched_yield", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        gil_release();
                        auto const r = ::sched_yield();
                        gil_acquire();
                        return r != 0 ? raise_os_error(errno) : (retain(none()), none());
                    } },
                { "sched_get_priority_min", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long policy = 0;
                        if (!parse_arguments("sched_get_priority_min", aArgs, aKwargs, { "policy" }, 1u, 0u, a) || !int_argument(a[0], policy, "sched_get_priority_min"))
                            return nullptr;
                        auto const r = ::sched_get_priority_min(static_cast<int>(policy));
                        return r < 0 ? raise_os_error(errno) : make_int(r);
                    } },
                { "sched_get_priority_max", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long policy = 0;
                        if (!parse_arguments("sched_get_priority_max", aArgs, aKwargs, { "policy" }, 1u, 0u, a) || !int_argument(a[0], policy, "sched_get_priority_max"))
                            return nullptr;
                        auto const r = ::sched_get_priority_max(static_cast<int>(policy));
                        return r < 0 ? raise_os_error(errno) : make_int(r);
                    } },
                // (Modules/posixmodule.c's functions that follow: their arguments, errors and results)
                { "abort", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("abort", aArgs, aKwargs))
                            return nullptr;
                        std::abort();
                    } },
                { "chroot", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.chroot"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        if (!parse_arguments("chroot", aArgs, aKwargs, { "path" }, 1u, 0u, a) || !path_argument(a[0], p, "chroot"))
                            return nullptr;
                        if (::chroot(p.c_str()) != 0)
                            return raise_os_error(errno, a[0]);
                        return new_reference(none());
                    } },
                { "ctermid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("ctermid", aArgs, aKwargs))
                            return nullptr;
                        char buffer[L_ctermid] = {};
                        if (::ctermid(buffer) == nullptr)
                            return raise_os_error(errno);
                        return make_str(buffer);
                    } },
                { "fdatasync", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("fdatasync", aArgs, aKwargs, { "fd" }, 1u, 1u, a))
                            return nullptr;
                        // (a file descriptor, or an object's fileno())
                        if (!to_index(a[0], fd))
                        {
                            clear_error();
                            ref const fileno{ call_method_named(a[0], "fileno", {}) };
                            if (!fileno || !int_argument(fileno.get(), fd, "fdatasync"))
                                return nullptr;
                        }
                        gil_release();
#if defined(__APPLE__)
                        auto const r = ::fsync(static_cast<int>(fd));
#else
                        auto const r = ::fdatasync(static_cast<int>(fd));
#endif
                        auto const e = errno;
                        gil_acquire();
                        return r != 0 ? raise_os_error(e) : new_reference(none());
                    } },
                { "getpriority", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long which = 0, who = 0;
                        if (!parse_arguments("getpriority", aArgs, aKwargs, { "which", "who" }, 2u, 0u, a) || !int_argument(a[0], which, "getpriority") ||
                            !int_argument(a[1], who, "getpriority"))
                            return nullptr;
                        errno = 0;
                        auto const r = ::getpriority(static_cast<int>(which), static_cast<id_t>(who));
                        if (r == -1 && errno != 0)
                            return raise_os_error(errno);
                        return make_int(r);
                    } },
                { "setpriority", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.setpriority"))
                            return nullptr;
                        std::vector<object*> a;
                        long long which = 0, who = 0, priority = 0;
                        if (!parse_arguments("setpriority", aArgs, aKwargs, { "which", "who", "priority" }, 3u, 0u, a) || !int_argument(a[0], which, "setpriority") ||
                            !int_argument(a[1], who, "setpriority") || !int_argument(a[2], priority, "setpriority"))
                            return nullptr;
                        if (::setpriority(static_cast<int>(which), static_cast<id_t>(who), static_cast<int>(priority)) != 0)
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "lockf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, command = 0, length = 0;
                        if (!parse_arguments("lockf", aArgs, aKwargs, { "fd", "command", "length" }, 3u, 3u, a) || !int_argument(a[0], fd, "lockf") ||
                            !int_argument(a[1], command, "lockf") || !int_argument(a[2], length, "lockf"))
                            return nullptr;
                        gil_release();
                        auto const r = ::lockf(static_cast<int>(fd), static_cast<int>(command), static_cast<off_t>(length));
                        auto const e = errno;
                        gil_acquire();
                        return r != 0 ? raise_os_error(e) : new_reference(none());
                    } },
                { "major", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long device = 0;
                        if (!parse_arguments("major", aArgs, aKwargs, { "device" }, 1u, 1u, a) || !int_argument(a[0], device, "major"))
                            return nullptr;
                        return make_unsigned(static_cast<std::uint64_t>(major(static_cast<dev_t>(device))));
                    } },
                { "minor", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long device = 0;
                        if (!parse_arguments("minor", aArgs, aKwargs, { "device" }, 1u, 1u, a) || !int_argument(a[0], device, "minor"))
                            return nullptr;
                        return make_unsigned(static_cast<std::uint64_t>(minor(static_cast<dev_t>(device))));
                    } },
                { "makedev", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long majorNumber = 0, minorNumber = 0;
                        if (!parse_arguments("makedev", aArgs, aKwargs, { "major", "minor" }, 2u, 2u, a) || !int_argument(a[0], majorNumber, "makedev") ||
                            !int_argument(a[1], minorNumber, "makedev"))
                            return nullptr;
                        return make_unsigned(static_cast<std::uint64_t>(makedev(static_cast<unsigned>(majorNumber), static_cast<unsigned>(minorNumber))));
                    } },
                { "mknod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.mknod"))
                            return nullptr;
                        std::vector<object*> a;
                        std::string p;
                        long long mode = 0600, device = 0;
                        int dirFd = AT_FDCWD;
                        if (!positional_at_most("mknod", aArgs, 3u, false) || !parse_arguments("mknod", aArgs, aKwargs, { "path", "mode", "device", "dir_fd" }, 1u, 0u, a) || !path_argument(a[0], p, "mknod") ||
                            (a[1] != nullptr && !int_argument(a[1], mode, "mknod")) || (a[2] != nullptr && !int_argument(a[2], device, "mknod")) || !dir_fd_of(a[3], dirFd))
                            return nullptr;
                        if (::mknodat(dirFd, p.c_str(), static_cast<mode_t>(mode), static_cast<dev_t>(device)) != 0)
                            return raise_os_error(errno, a[0]);
                        return new_reference(none());
                    } },
                { "posix_fallocate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, offset = 0, length = 0;
                        if (!parse_arguments("posix_fallocate", aArgs, aKwargs, { "fd", "offset", "len" }, 3u, 3u, a) || !int_argument(a[0], fd, "posix_fallocate") ||
                            !int_argument(a[1], offset, "posix_fallocate") || !int_argument(a[2], length, "posix_fallocate"))
                            return nullptr;
#if defined(__APPLE__)
                        return raise_os_error(EOPNOTSUPP);
#else
                        gil_release();
                        auto const r = ::posix_fallocate(static_cast<int>(fd), static_cast<off_t>(offset), static_cast<off_t>(length));
                        gil_acquire();
                        return r != 0 ? raise_os_error(r) : new_reference(none());
#endif
                    } },
                { "pread", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, length = 0, offset = 0;
                        if (!parse_arguments("pread", aArgs, aKwargs, { "fd", "length", "offset" }, 3u, 3u, a) || !int_argument(a[0], fd, "pread") ||
                            !int_argument(a[1], length, "pread") || !int_argument(a[2], offset, "pread"))
                            return nullptr;
                        if (length < 0)
                            return raise_os_error(EINVAL);
                        std::string buffer(static_cast<std::size_t>(length), '\0');
                        ssize_t n = 0;
                        int e = 0;
                        do
                        {
                            gil_release();
                            n = ::pread(static_cast<int>(fd), buffer.data(), buffer.size(), static_cast<off_t>(offset));
                            e = errno;
                            gil_acquire();
                        } while (n < 0 && e == EINTR && check_signals());
                        if (n < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        buffer.resize(static_cast<std::size_t>(n));
                        return make_bytes(std::move(buffer));
                    } },
                { "pwrite", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, offset = 0;
                        std::string_view data;
                        if (!parse_arguments("pwrite", aArgs, aKwargs, { "fd", "str", "offset" }, 3u, 3u, a) || !int_argument(a[0], fd, "pwrite") ||
                            !int_argument(a[2], offset, "pwrite"))
                            return nullptr;
                        if (!bytes_like(a[1], data))
                            return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[1]) + "'");
                        ssize_t n = 0;
                        int e = 0;
                        do
                        {
                            gil_release();
                            n = ::pwrite(static_cast<int>(fd), data.data(), data.size(), static_cast<off_t>(offset));
                            e = errno;
                            gil_acquire();
                        } while (n < 0 && e == EINTR && check_signals());
                        if (n < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        return make_int(static_cast<long long>(n));
                    } },
                { "writev", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("writev", aArgs, aKwargs, { "fd", "buffers" }, 2u, 2u, a) || !int_argument(a[0], fd, "writev"))
                            return nullptr;
                        // (buffers: a sequence of bytes-like objects)
                        if (!has_attribute(a[1], "__getitem__") || !has_attribute(a[1], "__len__"))
                            return error("TypeError", "writev() arg 2 must be a sequence");
                        std::vector<object*> items;
                        if (!collect(a[1], items))
                            return nullptr;
                        std::vector<ref> held;
                        std::vector<iovec> vectors;
                        for (auto i : items)
                        {
                            held.emplace_back(i);
                            std::string_view data;
                            if (!bytes_like(i, data))
                                return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(i) + "'");
                            vectors.push_back(iovec{ const_cast<char*>(data.data()), data.size() });
                        }
                        ssize_t n = 0;
                        int e = 0;
                        do
                        {
                            gil_release();
                            n = ::writev(static_cast<int>(fd), vectors.data(), static_cast<int>(vectors.size()));
                            e = errno;
                            gil_acquire();
                        } while (n < 0 && e == EINTR && check_signals());
                        if (n < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        return make_int(static_cast<long long>(n));
                    } },
                { "tcgetpgrp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("tcgetpgrp", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !int_argument(a[0], fd, "tcgetpgrp"))
                            return nullptr;
                        auto const r = ::tcgetpgrp(static_cast<int>(fd));
                        return r < 0 ? raise_os_error(errno) : make_int(r);
                    } },
                { "tcsetpgrp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.tcsetpgrp"))
                            return nullptr;
                        std::vector<object*> a;
                        long long fd = 0, pgid = 0;
                        if (!parse_arguments("tcsetpgrp", aArgs, aKwargs, { "fd", "pgid" }, 2u, 2u, a) || !int_argument(a[0], fd, "tcsetpgrp") ||
                            !int_argument(a[1], pgid, "tcsetpgrp"))
                            return nullptr;
                        if (::tcsetpgrp(static_cast<int>(fd), static_cast<pid_t>(pgid)) != 0)
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "WCOREDUMP", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long s = 0;
                        if (!parse_arguments("WCOREDUMP", aArgs, aKwargs, { "status" }, 1u, 1u, a) || !int_argument(a[0], s, "WCOREDUMP"))
                            return nullptr;
                        return make_bool(WCOREDUMP(static_cast<int>(s)));
                    } },
                { "WIFCONTINUED", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long s = 0;
                        if (!parse_arguments("WIFCONTINUED", aArgs, aKwargs, { "status" }, 1u, 0u, a) || !int_argument(a[0], s, "WIFCONTINUED"))
                            return nullptr;
                        return make_bool(WIFCONTINUED(static_cast<int>(s)));
                    } },
                { "getgrouplist", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string user;
                        long long group = 0;
                        if (!parse_arguments("getgrouplist", aArgs, aKwargs, { "user", "group" }, 2u, 2u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "getgrouplist() argument 1 must be str, not " } + type_name(a[0]));
                        user = as<str_object>(a[0])->value;
                        if (!int_argument(a[1], group, "getgrouplist"))
                            return nullptr;
                        // (the list grown until it is large enough)
                        int count = 64;
                        std::vector<gid_t> groups;
                        for (;;)
                        {
                            groups.assign(static_cast<std::size_t>(count), 0);
                            int n = count;
#if defined(__APPLE__)
                            auto const r = ::getgrouplist(user.c_str(), static_cast<int>(group), reinterpret_cast<int*>(groups.data()), &n);
#else
                            auto const r = ::getgrouplist(user.c_str(), static_cast<gid_t>(group), groups.data(), &n);
#endif
                            if (r >= 0)
                            {
                                groups.resize(static_cast<std::size_t>(n));
                                break;
                            }
                            if (count >= 65536)
                                return raise_os_error(errno != 0 ? errno : EINVAL);
                            count = std::max(count * 2, n);
                        }
                        std::vector<object*> items;
                        for (auto g : groups)
                            items.push_back(make_int(static_cast<long long>(g)));
                        return make_list(std::move(items));
                    } },
                { "register_at_fork", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (keyword arguments only, at least one, each callable: called by fork (before: the last registered first))
                        if (!aArgs.empty())
                            return error("TypeError", "register_at_fork() takes no positional arguments");
                        std::vector<object*> a;
                        if (!parse_arguments("register_at_fork", aArgs, aKwargs, { "before", "after_in_child", "after_in_parent" }, 0u, 0u, a))
                            return nullptr;
                        if (a[0] == nullptr && a[1] == nullptr && a[2] == nullptr)
                            return error("TypeError", "At least one argument is required.");
                        char const* const names[] = { "before", "after_in_child", "after_in_parent" };
                        for (std::size_t i = 0u; i < 3u; ++i)
                            if (a[i] != nullptr && !is_callable(a[i]))
                                return error("TypeError", std::string{ "'" } + names[i] + "' must be callable, not " + type_name(a[i]));
                        auto* const hooks = fork_hooks();
                        for (std::size_t i = 0u; i < 3u; ++i)
                            if (a[i] != nullptr)
                                hooks[i].push_back(new_reference(a[i]));
                        return new_reference(none());
                    } },
                { "posix_openpt", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long flags = 0;
                        if (!parse_arguments("posix_openpt", aArgs, aKwargs, { "oflag" }, 1u, 1u, a) || !int_argument(a[0], flags, "posix_openpt"))
                            return nullptr;
                        // (not inheritable, as CPython's)
                        int const fd = ::posix_openpt(static_cast<int>(flags));
                        if (fd < 0)
                            return raise_os_error(errno);
                        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
                        return make_int(fd);
                    } },
                { "grantpt", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("grantpt", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !fd_or_fileno(a[0], fd, "grantpt"))
                            return nullptr;
                        if (::grantpt(static_cast<int>(fd)) != 0)
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "unlockpt", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("unlockpt", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !fd_or_fileno(a[0], fd, "unlockpt"))
                            return nullptr;
                        if (::unlockpt(static_cast<int>(fd)) != 0)
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "ptsname", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("ptsname", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !fd_or_fileno(a[0], fd, "ptsname"))
                            return nullptr;
                        char const* const name = ::ptsname(static_cast<int>(fd));
                        if (name == nullptr)
                            return raise_os_error(errno);
                        return make_str(name);
                    } },
                // (login_tty(fd): a new session, the terminal its controlling terminal and the standard streams (as CPython's fallback))
                { "login_tty", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.login_tty"))
                            return nullptr;
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("login_tty", aArgs, aKwargs, { "fd" }, 1u, 1u, a) || !fd_or_fileno(a[0], fd, "login_tty"))
                            return nullptr;
                        if (!login_terminal(static_cast<int>(fd)))
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "forkpty", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("forkpty", aArgs, aKwargs) || !host_unrestricted("os.fork"))
                            return nullptr;
                        // (a pseudo-terminal (openpty's), then fork: the child's terminal is its slave end (login_tty); (pid, master))
                        int const master = ::posix_openpt(O_RDWR | O_NOCTTY);
                        if (master < 0)
                            return raise_os_error(errno);
                        char const* name = nullptr;
                        if (::grantpt(master) != 0 || ::unlockpt(master) != 0 || (name = ::ptsname(master)) == nullptr)
                        {
                            int const e = errno;
                            ::close(master);
                            return raise_os_error(e);
                        }
                        int const slave = ::open(name, O_RDWR | O_NOCTTY);
                        if (slave < 0)
                        {
                            int const e = errno;
                            ::close(master);
                            return raise_os_error(e);
                        }
                        auto const pid = fork_process();
                        if (pid < 0)
                        {
                            int const e = errno;
                            ::close(master);
                            ::close(slave);
                            return failed() ? nullptr : raise_os_error(e);
                        }
                        if (pid == 0)
                        {
                            ::close(master);
                            if (!login_terminal(slave))
                                ::_exit(1);
                            return make_tuple({ make_int(0), make_int(-1) });
                        }
                        ::close(slave);
                        return make_tuple({ make_int(pid), make_int(master) });
                    } },
                { "wait3", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long options = 0;
                        if (!parse_arguments("wait3", aArgs, aKwargs, { "options" }, 1u, 0u, a) || !int_argument(a[0], options, "wait3"))
                            return nullptr;
                        return wait_with_usage(-1, static_cast<int>(options), false);
                    } },
                { "wait4", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0, options = 0;
                        if (!parse_arguments("wait4", aArgs, aKwargs, { "pid", "options" }, 2u, 0u, a) || !int_argument(a[0], pid, "wait4") ||
                            !int_argument(a[1], options, "wait4"))
                            return nullptr;
                        return wait_with_usage(static_cast<pid_t>(pid), static_cast<int>(options), true);
                    } },
                { "waitid", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long idtype = 0, id = 0, options = 0;
                        if (!parse_arguments("waitid", aArgs, aKwargs, { "idtype", "id", "options" }, 3u, 3u, a) || !int_argument(a[0], idtype, "waitid") ||
                            !int_argument(a[1], id, "waitid") || !int_argument(a[2], options, "waitid"))
                            return nullptr;
                        siginfo_t info{};
                        int r = 0, e = 0;
                        do
                        {
                            gil_release();
                            r = ::waitid(static_cast<idtype_t>(idtype), static_cast<id_t>(id), &info, static_cast<int>(options));
                            e = errno;
                            gil_acquire();
                        } while (r < 0 && e == EINTR && check_signals());
                        if (r < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        // (no child in a waitable state (WNOHANG): None)
                        if (info.si_pid == 0)
                            return new_reference(none());
                        auto const& t = structseq_type("posix.waitid_result", { "si_pid", "si_uid", "si_signo", "si_status", "si_code" }, 5u);
                        return make_structseq(t, { make_int(info.si_pid), make_int(static_cast<long long>(info.si_uid)), make_int(info.si_signo),
                            make_int(info.si_status), make_int(info.si_code) });
                    } },
#if defined(__linux__)
                { "readv", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("readv", aArgs, aKwargs, { "fd", "buffers" }, 2u, 2u, a) || !int_argument(a[0], fd, "readv"))
                            return nullptr;
                        std::vector<ref> held;
                        std::vector<iovec> vectors;
                        if (!io_vectors(a[1], true, "readv", held, vectors))
                            return nullptr;
                        return vector_io(static_cast<int>(fd), vectors, -1, 0, false);
                    } },
                { "preadv", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, offset = 0, flags = 0;
                        if (!parse_arguments("preadv", aArgs, aKwargs, { "fd", "buffers", "offset", "flags" }, 3u, 4u, a) || !int_argument(a[0], fd, "preadv") ||
                            !int_argument(a[2], offset, "preadv") || (a[3] != nullptr && !int_argument(a[3], flags, "preadv")))
                            return nullptr;
                        std::vector<ref> held;
                        std::vector<iovec> vectors;
                        if (!io_vectors(a[1], true, "preadv", held, vectors))
                            return nullptr;
                        return vector_io(static_cast<int>(fd), vectors, offset, static_cast<int>(flags), false);
                    } },
                { "pwritev", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, offset = 0, flags = 0;
                        if (!parse_arguments("pwritev", aArgs, aKwargs, { "fd", "buffers", "offset", "flags" }, 3u, 4u, a) || !int_argument(a[0], fd, "pwritev") ||
                            !int_argument(a[2], offset, "pwritev") || (a[3] != nullptr && !int_argument(a[3], flags, "pwritev")))
                            return nullptr;
                        std::vector<ref> held;
                        std::vector<iovec> vectors;
                        if (!io_vectors(a[1], false, "pwritev", held, vectors))
                            return nullptr;
                        return vector_io(static_cast<int>(fd), vectors, offset, static_cast<int>(flags), true);
                    } },
                { "timerfd_create", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long clock = 0, flags = 0;
                        if (!parse_arguments("timerfd_create", aArgs, aKwargs, { "clockid", "flags" }, 1u, 1u, a) || !int_argument(a[0], clock, "timerfd_create") ||
                            (a[1] != nullptr && !int_argument(a[1], flags, "timerfd_create")))
                            return nullptr;
                        if (aArgs.size() > 1u)
                            return error("TypeError", "timerfd_create() takes exactly 1 positional argument (" + std::to_string(aArgs.size()) + " given)");
                        auto const fd = ::timerfd_create(static_cast<int>(clock), static_cast<int>(flags));
                        return fd < 0 ? raise_os_error(errno) : make_int(fd);
                    } },
                { "timerfd_settime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return timerfd_set(aArgs, aKwargs, false); } },
                { "timerfd_settime_ns", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return timerfd_set(aArgs, aKwargs, true); } },
                { "timerfd_gettime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return timerfd_get(aArgs, aKwargs, false); } },
                { "timerfd_gettime_ns", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return timerfd_get(aArgs, aKwargs, true); } },
                { "pipe2", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long flags = 0;
                        if (!parse_arguments("pipe2", aArgs, aKwargs, { "flags" }, 1u, 1u, a) || !int_argument(a[0], flags, "pipe2"))
                            return nullptr;
                        int fds[2] = {};
                        if (::pipe2(fds, static_cast<int>(flags)) != 0)
                            return raise_os_error(errno);
                        return make_tuple({ make_int(fds[0]), make_int(fds[1]) });
                    } },
                { "posix_fadvise", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0, offset = 0, length = 0, advice = 0;
                        if (!parse_arguments("posix_fadvise", aArgs, aKwargs, { "fd", "offset", "len", "advice" }, 4u, 4u, a) || !int_argument(a[0], fd, "posix_fadvise") ||
                            !int_argument(a[1], offset, "posix_fadvise") || !int_argument(a[2], length, "posix_fadvise") || !int_argument(a[3], advice, "posix_fadvise"))
                            return nullptr;
                        gil_release();
                        auto const r = ::posix_fadvise(static_cast<int>(fd), static_cast<off_t>(offset), static_cast<off_t>(length), static_cast<int>(advice));
                        gil_acquire();
                        return r != 0 ? raise_os_error(r) : new_reference(none());
                    } },
                { "getrandom", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long size = 0, flags = 0;
                        if (!parse_arguments("getrandom", aArgs, aKwargs, { "size", "flags" }, 1u, 0u, a) || !int_argument(a[0], size, "getrandom") ||
                            (a[1] != nullptr && !int_argument(a[1], flags, "getrandom")))
                            return nullptr;
                        if (size < 0)
                            return raise_os_error(EINVAL);
                        std::string buffer(static_cast<std::size_t>(size), '\0');
                        ssize_t n = 0;
                        int e = 0;
                        do
                        {
                            gil_release();
                            n = static_cast<ssize_t>(::syscall(SYS_getrandom, buffer.data(), buffer.size(), static_cast<unsigned>(flags)));
                            e = errno;
                            gil_acquire();
                        } while (n < 0 && e == EINTR && check_signals());
                        if (n < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        buffer.resize(static_cast<std::size_t>(n));
                        return make_bytes(std::move(buffer));
                    } },
                { "eventfd", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long initval = 0, flags = EFD_CLOEXEC;
                        if (!parse_arguments("eventfd", aArgs, aKwargs, { "initval", "flags" }, 1u, 0u, a) || !int_argument(a[0], initval, "eventfd") ||
                            (a[1] != nullptr && !int_argument(a[1], flags, "eventfd")))
                            return nullptr;
                        if (initval < 0 || initval > static_cast<long long>(UINT_MAX))
                            return error("OverflowError", "Python int too large to convert to C unsigned long");
                        auto const fd = ::eventfd(static_cast<unsigned>(initval), static_cast<int>(flags));
                        return fd < 0 ? raise_os_error(errno) : make_int(fd);
                    } },
                { "eventfd_read", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("eventfd_read", aArgs, aKwargs, { "fd" }, 1u, 0u, a) || !fd_or_fileno(a[0], fd, "eventfd_read"))
                            return nullptr;
                        eventfd_t value = 0;
                        int r = 0, e = 0;
                        do
                        {
                            gil_release();
                            r = ::eventfd_read(static_cast<int>(fd), &value);
                            e = errno;
                            gil_acquire();
                        } while (r < 0 && e == EINTR && check_signals());
                        if (r < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        return make_unsigned(static_cast<std::uint64_t>(value));
                    } },
                { "eventfd_write", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long fd = 0;
                        if (!parse_arguments("eventfd_write", aArgs, aKwargs, { "fd", "value" }, 2u, 0u, a) || !fd_or_fileno(a[0], fd, "eventfd_write"))
                            return nullptr;
                        std::uint64_t value = 0;
                        if (!unsigned_argument(a[1], value))
                            return nullptr;
                        int r = 0, e = 0;
                        do
                        {
                            gil_release();
                            r = ::eventfd_write(static_cast<int>(fd), static_cast<eventfd_t>(value));
                            e = errno;
                            gil_acquire();
                        } while (r < 0 && e == EINTR && check_signals());
                        if (r < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        return new_reference(none());
                    } },
                { "memfd_create", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string name;
                        long long flags = MFD_CLOEXEC;
                        if (!parse_arguments("memfd_create", aArgs, aKwargs, { "name", "flags" }, 1u, 0u, a) || !path_argument(a[0], name, "memfd_create", "name") ||
                            (a[1] != nullptr && !int_argument(a[1], flags, "memfd_create")))
                            return nullptr;
                        auto const fd = static_cast<int>(::syscall(SYS_memfd_create, name.c_str(), static_cast<unsigned>(flags)));
                        return fd < 0 ? raise_os_error(errno) : make_int(fd);
                    } },
                { "sendfile", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long out = 0, in = 0, count = 0, offset = 0;
                        if (!parse_arguments("sendfile", aArgs, aKwargs, { "out_fd", "in_fd", "offset", "count" }, 4u, 0u, a) || !int_argument(a[0], out, "sendfile") ||
                            !int_argument(a[1], in, "sendfile") || !int_argument(a[3], count, "sendfile"))
                            return nullptr;
                        // (offset None: from the file's position)
                        bool const atPosition = kind_of(a[2]) == NEOS_PY_NONE;
                        if (!atPosition && !int_argument(a[2], offset, "sendfile"))
                            return nullptr;
                        if (count < 0)
                            return error("ValueError", "count cannot be negative");
                        off_t position = static_cast<off_t>(offset);
                        ssize_t n = 0;
                        int e = 0;
                        do
                        {
                            gil_release();
                            n = ::sendfile(static_cast<int>(out), static_cast<int>(in), atPosition ? nullptr : &position, static_cast<std::size_t>(count));
                            e = errno;
                            gil_acquire();
                        } while (n < 0 && e == EINTR && check_signals());
                        if (n < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        return make_int(static_cast<long long>(n));
                    } },
                { "copy_file_range", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long source = 0, destination = 0, count = 0, sourceOffset = 0, destinationOffset = 0;
                        if (!parse_arguments("copy_file_range", aArgs, aKwargs, { "src", "dst", "count", "offset_src", "offset_dst" }, 3u, 0u, a) ||
                            !int_argument(a[0], source, "copy_file_range") || !int_argument(a[1], destination, "copy_file_range") || !int_argument(a[2], count, "copy_file_range"))
                            return nullptr;
                        if (count < 0)
                            return error("ValueError", "negative count");
                        bool const hasSource = a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE, hasDestination = a[4] != nullptr && kind_of(a[4]) != NEOS_PY_NONE;
                        if ((hasSource && !int_argument(a[3], sourceOffset, "copy_file_range")) || (hasDestination && !int_argument(a[4], destinationOffset, "copy_file_range")))
                            return nullptr;
                        loff_t s = static_cast<loff_t>(sourceOffset), d = static_cast<loff_t>(destinationOffset);
                        ssize_t n = 0;
                        int e = 0;
                        do
                        {
                            gil_release();
                            n = static_cast<ssize_t>(::syscall(SYS_copy_file_range, static_cast<int>(source), hasSource ? &s : nullptr, static_cast<int>(destination),
                                hasDestination ? &d : nullptr, static_cast<std::size_t>(count), 0u));
                            e = errno;
                            gil_acquire();
                        } while (n < 0 && e == EINTR && check_signals());
                        if (n < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        return make_int(static_cast<long long>(n));
                    } },
                { "splice", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long source = 0, destination = 0, count = 0, sourceOffset = 0, destinationOffset = 0, flags = 0;
                        if (!parse_arguments("splice", aArgs, aKwargs, { "src", "dst", "count", "offset_src", "offset_dst", "flags" }, 3u, 0u, a) ||
                            !int_argument(a[0], source, "splice") || !int_argument(a[1], destination, "splice") || !int_argument(a[2], count, "splice") ||
                            (a[5] != nullptr && !int_argument(a[5], flags, "splice")))
                            return nullptr;
                        if (count < 0)
                            return error("ValueError", "negative count");
                        bool const hasSource = a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE, hasDestination = a[4] != nullptr && kind_of(a[4]) != NEOS_PY_NONE;
                        if ((hasSource && !int_argument(a[3], sourceOffset, "splice")) || (hasDestination && !int_argument(a[4], destinationOffset, "splice")))
                            return nullptr;
                        loff_t s = static_cast<loff_t>(sourceOffset), d = static_cast<loff_t>(destinationOffset);
                        ssize_t n = 0;
                        int e = 0;
                        do
                        {
                            gil_release();
                            n = ::splice(static_cast<int>(source), hasSource ? &s : nullptr, static_cast<int>(destination), hasDestination ? &d : nullptr,
                                static_cast<std::size_t>(count), static_cast<unsigned>(flags));
                            e = errno;
                            gil_acquire();
                        } while (n < 0 && e == EINTR && check_signals());
                        if (n < 0)
                            return failed() ? nullptr : raise_os_error(e);
                        return make_int(static_cast<long long>(n));
                    } },
                { "unshare", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.unshare"))
                            return nullptr;
                        std::vector<object*> a;
                        long long flags = 0;
                        if (!parse_arguments("unshare", aArgs, aKwargs, { "flags" }, 1u, 0u, a) || !int_argument(a[0], flags, "unshare"))
                            return nullptr;
                        if (::unshare(static_cast<int>(flags)) != 0)
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "setns", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.setns"))
                            return nullptr;
                        std::vector<object*> a;
                        long long fd = 0, nstype = 0;
                        if (!parse_arguments("setns", aArgs, aKwargs, { "fd", "nstype" }, 1u, 0u, a) || !fd_or_fileno(a[0], fd, "setns") ||
                            (a[1] != nullptr && !int_argument(a[1], nstype, "setns")))
                            return nullptr;
                        if (::setns(static_cast<int>(fd), static_cast<int>(nstype)) != 0)
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "pidfd_open", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0, flags = 0;
                        if (!parse_arguments("pidfd_open", aArgs, aKwargs, { "pid", "flags" }, 1u, 0u, a) || !int_argument(a[0], pid, "pidfd_open") ||
                            (a[1] != nullptr && !int_argument(a[1], flags, "pidfd_open")))
                            return nullptr;
                        auto const fd = static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), static_cast<unsigned>(flags)));
                        return fd < 0 ? raise_os_error(errno) : make_int(fd);
                    } },
                { "getxattr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!positional_at_most("getxattr", aArgs, 2u, true) || !parse_arguments("getxattr", aArgs, aKwargs, { "path", "attribute", "follow_symlinks" }, 2u, 0u, a))
                            return nullptr;
                        xattr_target target;
                        std::string attribute;
                        if (!xattr_target_of(a[0], a[2], "getxattr", target) || !path_argument(a[1], attribute, "getxattr", "attribute"))
                            return nullptr;
                        // (the value: its size found first, then read (a race that grows it: again))
                        for (int attempt = 0; attempt < 8; ++attempt)
                        {
                            auto const size = target.get(attribute, nullptr, 0u);
                            if (size < 0)
                                return raise_os_error(errno, a[0]);
                            std::string value(static_cast<std::size_t>(size), '\0');
                            auto const n = target.get(attribute, value.data(), value.size());
                            if (n >= 0)
                            {
                                value.resize(static_cast<std::size_t>(n));
                                return make_bytes(std::move(value));
                            }
                            if (errno != ERANGE)
                                return raise_os_error(errno, a[0]);
                        }
                        return raise_os_error(ERANGE, a[0]);
                    } },
                { "setxattr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.setxattr"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!positional_at_most("setxattr", aArgs, 4u, false) || !parse_arguments("setxattr", aArgs, aKwargs, { "path", "attribute", "value", "flags", "follow_symlinks" }, 3u, 0u, a))
                            return nullptr;
                        xattr_target target;
                        std::string attribute;
                        std::string_view value;
                        long long flags = 0;
                        if (!xattr_target_of(a[0], a[4], "setxattr", target) || !path_argument(a[1], attribute, "setxattr", "attribute") ||
                            (a[3] != nullptr && !int_argument(a[3], flags, "setxattr")))
                            return nullptr;
                        if (!bytes_like(a[2], value))
                            return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[2]) + "'");
                        if (target.set(attribute, value, static_cast<int>(flags)) != 0)
                            return raise_os_error(errno, a[0]);
                        return new_reference(none());
                    } },
                { "removexattr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("os.removexattr"))
                            return nullptr;
                        std::vector<object*> a;
                        if (!positional_at_most("removexattr", aArgs, 2u, true) || !parse_arguments("removexattr", aArgs, aKwargs, { "path", "attribute", "follow_symlinks" }, 2u, 0u, a))
                            return nullptr;
                        xattr_target target;
                        std::string attribute;
                        if (!xattr_target_of(a[0], a[2], "removexattr", target) || !path_argument(a[1], attribute, "removexattr", "attribute"))
                            return nullptr;
                        if (target.remove(attribute) != 0)
                            return raise_os_error(errno, a[0]);
                        return new_reference(none());
                    } },
                { "listxattr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!positional_at_most("listxattr", aArgs, 1u, false) || !parse_arguments("listxattr", aArgs, aKwargs, { "path", "follow_symlinks" }, 0u, 0u, a))
                            return nullptr;
                        // (path None: the current directory)
                        ref const current{ make_str(".") };
                        object* const path = a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE ? a[0] : current.get();
                        xattr_target target;
                        if (!xattr_target_of(path, a[1], "listxattr", target))
                            return nullptr;
                        for (int attempt = 0; attempt < 8; ++attempt)
                        {
                            auto const size = target.list(nullptr, 0u);
                            if (size < 0)
                                return raise_os_error(errno, path);
                            std::string names(static_cast<std::size_t>(size), '\0');
                            auto const n = target.list(names.data(), names.size());
                            if (n >= 0)
                            {
                                names.resize(static_cast<std::size_t>(n));
                                std::vector<object*> items;
                                for (std::size_t i = 0u; i < names.size();)
                                {
                                    auto const end = names.find('\0', i);
                                    auto const name = names.substr(i, end == std::string::npos ? std::string::npos : end - i);
                                    if (!name.empty())
                                        items.push_back(decode_filename(name));
                                    if (end == std::string::npos)
                                        break;
                                    i = end + 1u;
                                }
                                return make_list(std::move(items));
                            }
                            if (errno != ERANGE)
                                return raise_os_error(errno, path);
                        }
                        return raise_os_error(ERANGE, path);
                    } },

                { "sched_getscheduler", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        if (!parse_arguments("sched_getscheduler", aArgs, aKwargs, { "pid" }, 1u, 1u, a) || !int_argument(a[0], pid, "sched_getscheduler"))
                            return nullptr;
                        auto const r = ::sched_getscheduler(static_cast<pid_t>(pid));
                        return r < 0 ? raise_os_error(errno) : make_int(r);
                    } },
                { "sched_setscheduler", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        long long policy = 0;
                        struct sched_param param{};
                        if (!parse_arguments("sched_setscheduler", aArgs, aKwargs, { "pid", "policy", "param" }, 3u, 3u, a) || !int_argument(a[0], pid, "sched_setscheduler") ||
                            !int_argument(a[1], policy, "sched_setscheduler") || !sched_param_of(a[2], param))
                            return nullptr;
                        return ::sched_setscheduler(static_cast<pid_t>(pid), static_cast<int>(policy), &param) != 0 ? raise_os_error(errno) : (retain(none()), none());
                    } },
                { "sched_getparam", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        if (!parse_arguments("sched_getparam", aArgs, aKwargs, { "pid" }, 1u, 1u, a) || !int_argument(a[0], pid, "sched_getparam"))
                            return nullptr;
                        struct sched_param param{};
                        if (::sched_getparam(static_cast<pid_t>(pid), &param) != 0)
                            return raise_os_error(errno);
                        return make_structseq(sched_param_type(), { make_int(param.sched_priority) });
                    } },
                { "sched_setparam", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        struct sched_param param{};
                        if (!parse_arguments("sched_setparam", aArgs, aKwargs, { "pid", "param" }, 2u, 2u, a) || !int_argument(a[0], pid, "sched_setparam") ||
                            !sched_param_of(a[1], param))
                            return nullptr;
                        return ::sched_setparam(static_cast<pid_t>(pid), &param) != 0 ? raise_os_error(errno) : (retain(none()), none());
                    } },
                { "sched_rr_get_interval", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        if (!parse_arguments("sched_rr_get_interval", aArgs, aKwargs, { "pid" }, 1u, 1u, a) || !int_argument(a[0], pid, "sched_rr_get_interval"))
                            return nullptr;
                        struct timespec interval{};
                        if (::sched_rr_get_interval(static_cast<pid_t>(pid), &interval) != 0)
                            return raise_os_error(errno);
                        return make_float(static_cast<double>(interval.tv_sec) + 1e-9 * static_cast<double>(interval.tv_nsec));
                    } },
                { "sched_getaffinity", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        if (!parse_arguments("sched_getaffinity", aArgs, aKwargs, { "pid" }, 1u, 1u, a) || !int_argument(a[0], pid, "sched_getaffinity"))
                            return nullptr;
                        // (a set big enough for the CPUs: bigger until the kernel's is no bigger (EINVAL: too small))
                        for (int count = std::max(16, static_cast<int>(::sysconf(_SC_NPROCESSORS_CONF)));; count *= 2)
                        {
                            auto const set = CPU_ALLOC(count);
                            if (set == nullptr)
                                return error("MemoryError", "");
                            auto const size = CPU_ALLOC_SIZE(count);
                            if (::sched_getaffinity(static_cast<pid_t>(pid), size, set) != 0)
                            {
                                auto const e = errno;
                                CPU_FREE(set);
                                if (e == EINVAL && count < (1 << 24))
                                    continue;
                                return raise_os_error(e);
                            }
                            std::vector<object*> cpus;
                            for (int cpu = 0; cpu < count; ++cpu)
                                if (CPU_ISSET_S(cpu, size, set))
                                    cpus.push_back(make_int(cpu));
                            CPU_FREE(set);
                            ref const list{ make_list(std::move(cpus)) };
                            return b_set({ list.get() }, nullptr);
                        }
                    } },
                { "sched_setaffinity", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        if (!parse_arguments("sched_setaffinity", aArgs, aKwargs, { "pid", "mask" }, 2u, 2u, a) || !int_argument(a[0], pid, "sched_setaffinity"))
                            return nullptr;
                        // (the mask: an iterable of CPU numbers)
                        ref const it{ iter(a[1]) };
                        if (!it)
                            return nullptr;
                        std::vector<int> cpus;
                        int highest = -1;
                        for (;;)
                        {
                            ref const item{ next(it.get()) };
                            if (!item)
                            {
                                if (failed())
                                    return nullptr;
                                break;
                            }
                            auto const k = kind_of(item.get());
                            if (k != NEOS_PY_INT && k != NEOS_PY_BOOL && !has_special_method(item.get(), "__index__"))
                            {
                                ref const type{ class_of(item.get()) };
                                std::string text;
                                if (!repr_of(type.get(), text))
                                    return nullptr;
                                return error("TypeError", "expected an iterator of ints, but iterator yielded " + text);
                            }
                            long long cpu = 0;
                            if (!to_index(item.get(), cpu))
                            {
                                // (an int that a C long cannot hold)
                                clear_error();
                                return error("OverflowError", "Python int too large to convert to C long");
                            }
                            if (cpu < 0)
                                return error("ValueError", "negative CPU number");
                            if (cpu > std::numeric_limits<int>::max() - 1)
                                return error("OverflowError", "CPU number too large");
                            cpus.push_back(static_cast<int>(cpu));
                            highest = std::max(highest, static_cast<int>(cpu));
                        }
                        auto const count = highest + 1;
                        auto const set = CPU_ALLOC(std::max(count, 1));
                        if (set == nullptr)
                            return error("MemoryError", "");
                        auto const size = CPU_ALLOC_SIZE(std::max(count, 1));
                        CPU_ZERO_S(size, set);
                        for (auto cpu : cpus)
                            CPU_SET_S(cpu, size, set);
                        auto const r = ::sched_setaffinity(static_cast<pid_t>(pid), size, set);
                        auto const e = errno;
                        CPU_FREE(set);
                        return r != 0 ? raise_os_error(e) : (retain(none()), none());
                    } },
#endif
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
                    { "WNOHANG", WNOHANG }, { "WUNTRACED", WUNTRACED }, { "EX_OK", 0 },
                    { "POSIX_SPAWN_OPEN", POSIX_SPAWN_OPEN_TAG }, { "POSIX_SPAWN_CLOSE", POSIX_SPAWN_CLOSE_TAG }, { "POSIX_SPAWN_DUP2", POSIX_SPAWN_DUP2_TAG }, { "F_LOCK", F_LOCK }, { "F_TLOCK", F_TLOCK }, { "F_ULOCK", F_ULOCK }, { "F_TEST", F_TEST },
                    { "PRIO_PROCESS", 0 }, { "PRIO_PGRP", 1 }, { "PRIO_USER", 2 },
                    { "SCHED_OTHER", SCHED_OTHER }, { "SCHED_FIFO", SCHED_FIFO }, { "SCHED_RR", SCHED_RR },
#if defined(__linux__)
                    { "SCHED_BATCH", SCHED_BATCH }, { "SCHED_IDLE", SCHED_IDLE }, { "SCHED_RESET_ON_FORK", SCHED_RESET_ON_FORK },
#endif
                    // (the constants that the platform defines (as Modules/posixmodule.c's all_ins))
#ifdef CLD_CONTINUED
                    { "CLD_CONTINUED", CLD_CONTINUED },
#endif
#ifdef CLD_DUMPED
                    { "CLD_DUMPED", CLD_DUMPED },
#endif
#ifdef CLD_EXITED
                    { "CLD_EXITED", CLD_EXITED },
#endif
#ifdef CLD_KILLED
                    { "CLD_KILLED", CLD_KILLED },
#endif
#ifdef CLD_STOPPED
                    { "CLD_STOPPED", CLD_STOPPED },
#endif
#ifdef CLD_TRAPPED
                    { "CLD_TRAPPED", CLD_TRAPPED },
#endif
#ifdef CLONE_FILES
                    { "CLONE_FILES", CLONE_FILES },
#endif
#ifdef CLONE_FS
                    { "CLONE_FS", CLONE_FS },
#endif
#ifdef CLONE_NEWIPC
                    { "CLONE_NEWIPC", CLONE_NEWIPC },
#endif
#ifdef CLONE_NEWNET
                    { "CLONE_NEWNET", CLONE_NEWNET },
#endif
#ifdef CLONE_NEWNS
                    { "CLONE_NEWNS", CLONE_NEWNS },
#endif
#ifdef CLONE_NEWPID
                    { "CLONE_NEWPID", CLONE_NEWPID },
#endif
#ifdef CLONE_NEWUSER
                    { "CLONE_NEWUSER", CLONE_NEWUSER },
#endif
#ifdef CLONE_NEWUTS
                    { "CLONE_NEWUTS", CLONE_NEWUTS },
#endif
#ifdef CLONE_SIGHAND
                    { "CLONE_SIGHAND", CLONE_SIGHAND },
#endif
#ifdef CLONE_SYSVSEM
                    { "CLONE_SYSVSEM", CLONE_SYSVSEM },
#endif
#ifdef CLONE_THREAD
                    { "CLONE_THREAD", CLONE_THREAD },
#endif
#ifdef CLONE_VM
                    { "CLONE_VM", CLONE_VM },
#endif
#ifdef EFD_CLOEXEC
                    { "EFD_CLOEXEC", EFD_CLOEXEC },
#endif
#ifdef EFD_NONBLOCK
                    { "EFD_NONBLOCK", EFD_NONBLOCK },
#endif
#ifdef EFD_SEMAPHORE
                    { "EFD_SEMAPHORE", EFD_SEMAPHORE },
#endif
#ifdef EX_CANTCREAT
                    { "EX_CANTCREAT", EX_CANTCREAT },
#endif
#ifdef EX_CONFIG
                    { "EX_CONFIG", EX_CONFIG },
#endif
#ifdef EX_DATAERR
                    { "EX_DATAERR", EX_DATAERR },
#endif
#ifdef EX_IOERR
                    { "EX_IOERR", EX_IOERR },
#endif
#ifdef EX_NOHOST
                    { "EX_NOHOST", EX_NOHOST },
#endif
#ifdef EX_NOINPUT
                    { "EX_NOINPUT", EX_NOINPUT },
#endif
#ifdef EX_NOPERM
                    { "EX_NOPERM", EX_NOPERM },
#endif
#ifdef EX_NOUSER
                    { "EX_NOUSER", EX_NOUSER },
#endif
#ifdef EX_OSERR
                    { "EX_OSERR", EX_OSERR },
#endif
#ifdef EX_OSFILE
                    { "EX_OSFILE", EX_OSFILE },
#endif
#ifdef EX_PROTOCOL
                    { "EX_PROTOCOL", EX_PROTOCOL },
#endif
#ifdef EX_SOFTWARE
                    { "EX_SOFTWARE", EX_SOFTWARE },
#endif
#ifdef EX_TEMPFAIL
                    { "EX_TEMPFAIL", EX_TEMPFAIL },
#endif
#ifdef EX_UNAVAILABLE
                    { "EX_UNAVAILABLE", EX_UNAVAILABLE },
#endif
#ifdef EX_USAGE
                    { "EX_USAGE", EX_USAGE },
#endif
#ifdef GRND_NONBLOCK
                    { "GRND_NONBLOCK", GRND_NONBLOCK },
#endif
#ifdef GRND_RANDOM
                    { "GRND_RANDOM", GRND_RANDOM },
#endif
#ifdef MFD_ALLOW_SEALING
                    { "MFD_ALLOW_SEALING", MFD_ALLOW_SEALING },
#endif
#ifdef MFD_CLOEXEC
                    { "MFD_CLOEXEC", MFD_CLOEXEC },
#endif
#ifdef MFD_HUGETLB
                    { "MFD_HUGETLB", MFD_HUGETLB },
#endif
#ifdef MFD_HUGE_16GB
                    { "MFD_HUGE_16GB", MFD_HUGE_16GB },
#endif
#ifdef MFD_HUGE_16MB
                    { "MFD_HUGE_16MB", MFD_HUGE_16MB },
#endif
#ifdef MFD_HUGE_1GB
                    { "MFD_HUGE_1GB", MFD_HUGE_1GB },
#endif
#ifdef MFD_HUGE_1MB
                    { "MFD_HUGE_1MB", MFD_HUGE_1MB },
#endif
#ifdef MFD_HUGE_256MB
                    { "MFD_HUGE_256MB", MFD_HUGE_256MB },
#endif
#ifdef MFD_HUGE_2GB
                    { "MFD_HUGE_2GB", MFD_HUGE_2GB },
#endif
#ifdef MFD_HUGE_2MB
                    { "MFD_HUGE_2MB", MFD_HUGE_2MB },
#endif
#ifdef MFD_HUGE_32MB
                    { "MFD_HUGE_32MB", MFD_HUGE_32MB },
#endif
#ifdef MFD_HUGE_512KB
                    { "MFD_HUGE_512KB", MFD_HUGE_512KB },
#endif
#ifdef MFD_HUGE_512MB
                    { "MFD_HUGE_512MB", MFD_HUGE_512MB },
#endif
#ifdef MFD_HUGE_64KB
                    { "MFD_HUGE_64KB", MFD_HUGE_64KB },
#endif
#ifdef MFD_HUGE_8MB
                    { "MFD_HUGE_8MB", MFD_HUGE_8MB },
#endif
#ifdef MFD_HUGE_MASK
                    { "MFD_HUGE_MASK", MFD_HUGE_MASK },
#endif
#ifdef MFD_HUGE_SHIFT
                    { "MFD_HUGE_SHIFT", MFD_HUGE_SHIFT },
#endif
#ifdef NGROUPS_MAX
                    { "NGROUPS_MAX", NGROUPS_MAX },
#endif
#ifdef O_ASYNC
                    { "O_ASYNC", O_ASYNC },
#endif
#ifdef O_DIRECT
                    { "O_DIRECT", O_DIRECT },
#endif
#ifdef O_FSYNC
                    { "O_FSYNC", O_FSYNC },
#endif
#ifdef O_LARGEFILE
                    { "O_LARGEFILE", O_LARGEFILE },
#endif
#ifdef O_NOATIME
                    { "O_NOATIME", O_NOATIME },
#endif
#ifdef O_PATH
                    { "O_PATH", O_PATH },
#endif
#ifdef O_RSYNC
                    { "O_RSYNC", O_RSYNC },
#endif
#ifdef O_TMPFILE
                    { "O_TMPFILE", O_TMPFILE },
#endif
#ifdef POSIX_FADV_DONTNEED
                    { "POSIX_FADV_DONTNEED", POSIX_FADV_DONTNEED },
#endif
#ifdef POSIX_FADV_NOREUSE
                    { "POSIX_FADV_NOREUSE", POSIX_FADV_NOREUSE },
#endif
#ifdef POSIX_FADV_NORMAL
                    { "POSIX_FADV_NORMAL", POSIX_FADV_NORMAL },
#endif
#ifdef POSIX_FADV_RANDOM
                    { "POSIX_FADV_RANDOM", POSIX_FADV_RANDOM },
#endif
#ifdef POSIX_FADV_SEQUENTIAL
                    { "POSIX_FADV_SEQUENTIAL", POSIX_FADV_SEQUENTIAL },
#endif
#ifdef POSIX_FADV_WILLNEED
                    { "POSIX_FADV_WILLNEED", POSIX_FADV_WILLNEED },
#endif
#ifdef RTLD_DEEPBIND
                    { "RTLD_DEEPBIND", RTLD_DEEPBIND },
#endif
#ifdef RTLD_GLOBAL
                    { "RTLD_GLOBAL", RTLD_GLOBAL },
#endif
#ifdef RTLD_LAZY
                    { "RTLD_LAZY", RTLD_LAZY },
#endif
#ifdef RTLD_LOCAL
                    { "RTLD_LOCAL", RTLD_LOCAL },
#endif
#ifdef RTLD_NODELETE
                    { "RTLD_NODELETE", RTLD_NODELETE },
#endif
#ifdef RTLD_NOLOAD
                    { "RTLD_NOLOAD", RTLD_NOLOAD },
#endif
#ifdef RTLD_NOW
                    { "RTLD_NOW", RTLD_NOW },
#endif
#ifdef SEEK_DATA
                    { "SEEK_DATA", SEEK_DATA },
#endif
#ifdef SEEK_HOLE
                    { "SEEK_HOLE", SEEK_HOLE },
#endif
#ifdef SPLICE_F_MORE
                    { "SPLICE_F_MORE", SPLICE_F_MORE },
#endif
#ifdef SPLICE_F_MOVE
                    { "SPLICE_F_MOVE", SPLICE_F_MOVE },
#endif
#ifdef SPLICE_F_NONBLOCK
                    { "SPLICE_F_NONBLOCK", SPLICE_F_NONBLOCK },
#endif
#ifdef ST_APPEND
                    { "ST_APPEND", ST_APPEND },
#endif
#ifdef ST_MANDLOCK
                    { "ST_MANDLOCK", ST_MANDLOCK },
#endif
#ifdef ST_NOATIME
                    { "ST_NOATIME", ST_NOATIME },
#endif
#ifdef ST_NODEV
                    { "ST_NODEV", ST_NODEV },
#endif
#ifdef ST_NODIRATIME
                    { "ST_NODIRATIME", ST_NODIRATIME },
#endif
#ifdef ST_NOEXEC
                    { "ST_NOEXEC", ST_NOEXEC },
#endif
#ifdef ST_NOSUID
                    { "ST_NOSUID", ST_NOSUID },
#endif
#ifdef ST_RDONLY
                    { "ST_RDONLY", ST_RDONLY },
#endif
#ifdef ST_RELATIME
                    { "ST_RELATIME", ST_RELATIME },
#endif
#ifdef ST_SYNCHRONOUS
                    { "ST_SYNCHRONOUS", ST_SYNCHRONOUS },
#endif
#ifdef ST_WRITE
                    { "ST_WRITE", ST_WRITE },
#endif
#ifdef TFD_CLOEXEC
                    { "TFD_CLOEXEC", TFD_CLOEXEC },
#endif
#ifdef TFD_NONBLOCK
                    { "TFD_NONBLOCK", TFD_NONBLOCK },
#endif
#ifdef TFD_TIMER_ABSTIME
                    { "TFD_TIMER_ABSTIME", TFD_TIMER_ABSTIME },
#endif
#ifdef TMP_MAX
                    { "TMP_MAX", TMP_MAX },
#endif
#ifdef WCONTINUED
                    { "WCONTINUED", WCONTINUED },
#endif
#ifdef WEXITED
                    { "WEXITED", WEXITED },
#endif
#ifdef WNOWAIT
                    { "WNOWAIT", WNOWAIT },
#endif
#ifdef WSTOPPED
                    { "WSTOPPED", WSTOPPED },
#endif
#ifdef XATTR_CREATE
                    { "XATTR_CREATE", XATTR_CREATE },
#endif
#ifdef XATTR_REPLACE
                    { "XATTR_REPLACE", XATTR_REPLACE },
#endif
#ifdef XATTR_SIZE_MAX
                    { "XATTR_SIZE_MAX", XATTR_SIZE_MAX },
#endif
#if defined(__linux__)
                    { "P_ALL", P_ALL }, { "P_PID", P_PID }, { "P_PGID", P_PGID },
#ifdef P_PIDFD
                    { "P_PIDFD", P_PIDFD },
#else
                    { "P_PIDFD", 3 },
#endif
#endif
#endif
                };
                for (auto const& c : constants)
                    aAttributes.emplace_back(c.first, make_int(c.second));
#if !defined(_WIN32)
                // (posix.waitid_result)
                structseq_type("posix.waitid_result", { "si_pid", "si_uid", "si_signo", "si_status", "si_code" }, 5u);
                aAttributes.emplace_back("waitid_result", type_object_for("waitid_result", "posix.waitid_result"));
#endif
#if defined(__linux__)
                sched_param_type();
                aAttributes.emplace_back("sched_param", type_object_for("sched_param", "posix.sched_param"));
#endif
                for (auto const& f : sPosixFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                // (os.sysconf_names)
                auto const names = make_dict();
                for (auto const& [n, v] : sSysconfNames)
                {
                    ref const key{ make_str(n) };
                    ref const value{ make_int(v) };
                    dict_set(names, key.get(), value.get());
                }
                aAttributes.emplace_back("sysconf_names", names);
                // (os.pathconf_names and os.confstr_names)
                for (auto const& [attribute, table] : { std::pair{ "pathconf_names", &sPathconfNames }, std::pair{ "confstr_names", &sConfstrNames } })
                {
                    auto const d = make_dict();
                    for (auto const& [n, v] : *table)
                    {
                        ref const key{ make_str(n) };
                        ref const value{ make_int(v) };
                        dict_set(d, key.get(), value.get());
                    }
                    aAttributes.emplace_back(attribute, d);
                }
            }

            bool os_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                posix_constants(aAttributes);
                for (auto const& f : sOsFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                static builtin_entry const sUnlink{ "unlink", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return remove_path("unlink", aArgs, aKwargs); } };
                aAttributes.emplace_back("unlink", make_builtin(sUnlink));
                aAttributes.emplace_back("environ", environment());
                aAttributes.emplace_back("path", fn_import(sPlatform.windows ? "ntpath" : "posixpath"));
                // (sys.modules['os.path']: its path module (as os.py sets it))
                if (ref const modules{ imported_modules() }; modules && aAttributes.back().second != nullptr && kind_of(builtin_value(modules.get())) == NEOS_PY_DICT)
                {
                    ref const key{ make_str("os.path") };
                    if (dict_find(builtin_value(modules.get()), key.get()) == -1 && dict_set(builtin_value(modules.get()), key.get(), aAttributes.back().second) != 0)
                        return false;
                }
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
                // (os.py's supports_dir_fd, supports_fd, supports_follow_symlinks and supports_effective_ids: the functions (of this module) whose
                // dir_fd, fd path, follow_symlinks=False and effective_ids arguments are supported (the *at functions on POSIX; none on Windows))
                auto const functions = [&](std::initializer_list<char const*> aNames) -> object*
                {
                    std::vector<object*> items;
                    for (auto name : aNames)
                        for (auto const& [n, v] : aAttributes)
                            if (n == name && v != nullptr)
                            {
                                items.push_back((retain(v), v));
                                break;
                            }
                    ref const list{ make_list(std::move(items)) };
                    return b_set({ list.get() }, nullptr);
                };
#if defined(_WIN32)
                aAttributes.emplace_back("supports_dir_fd", functions({}));
                aAttributes.emplace_back("supports_follow_symlinks", functions({}));
                aAttributes.emplace_back("supports_effective_ids", functions({}));
                aAttributes.emplace_back("supports_bytes_environ", make_bool(false));
#else
                aAttributes.emplace_back("supports_dir_fd", functions({ "access", "chmod", "chown", "link", "lstat", "mkdir", "mkfifo", "open", "readlink", "rename",
                    "rmdir", "stat", "symlink", "unlink", "utime" }));
                aAttributes.emplace_back("supports_follow_symlinks", functions({ "access", "chown", "link", "stat", "utime" }));
                aAttributes.emplace_back("supports_effective_ids", functions({ "access" }));
                aAttributes.emplace_back("supports_bytes_environ", make_bool(true));
#endif
#if defined(_WIN32)
                aAttributes.emplace_back("supports_fd", functions({}));
#else
                aAttributes.emplace_back("supports_fd", functions({ "chdir", "chmod", "chown", "execve", "listdir", "pathconf", "scandir", "stat", "statvfs", "truncate", 
                    "utime" }));
#endif
                // (the struct sequences' types; os.stat_result(sequence): its float times that the sequence does not give are its integer 
                // times (as CPython's statresult_new))
                set_structseq_constructor(stat_result_type(), [](structseq_descriptor const& aDescriptor, object* aArgs, object* aKwargs) -> object*
                {
                    ref result{ structseq_construct(aDescriptor, aArgs, aKwargs) };
                    if (!result)
                        return nullptr;
                    for (std::size_t i = 0u; i < 3u; ++i)
                        if (auto const f = structseq_field(result.get(), 10u + i); f != nullptr && kind_of(f) == NEOS_PY_NONE)
                            if (auto const integer = structseq_field(result.get(), 7u + i))
                                structseq_set_field(result.get(), 10u + i, new_reference(integer));
                    return result.release_ownership();
                }, false);
                aAttributes.emplace_back("stat_result", (stat_result_type(), type_object_for("stat_result", "os.stat_result")));
                aAttributes.emplace_back("terminal_size", (terminal_size_type(), type_object_for("terminal_size", "os.terminal_size")));
                aAttributes.emplace_back("times_result", (times_result_type(), type_object_for("times_result", "posix.times_result")));
#if !defined(_WIN32)
                aAttributes.emplace_back("uname_result", (uname_result_type(), type_object_for("uname_result", "posix.uname_result")));
                aAttributes.emplace_back("statvfs_result", (statvfs_result_type(), type_object_for("statvfs_result", "os.statvfs_result")));
#endif
                // (os.DirEntry: the type of os.scandir's entries (isinstance; DirEntry[str]: a GenericAlias), which cannot be made directly)
                aAttributes.emplace_back("DirEntry", context()->type_new(context(), "DirEntry", "posix.DirEntry",
                    [](neos_py_context*, void*, object*, object*) -> object* { return error("TypeError", "cannot create 'posix.DirEntry' instances"); }, nullptr,
                    [](neos_py_context*, void*, object* aObject) -> int { return aObject != nullptr && aObject->type == &sDirEntryType.base ? 1 : 0; }));
                observed_types().try_emplace("posix.DirEntry", &sDirEntryType);
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
                // (doors, event ports and whiteouts (Solaris, BSD): never (as CPython's _stat on other systems))
                { "S_ISDOOR", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool(false) : nullptr; } },
                { "S_ISPORT", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool(false) : nullptr; } },
                { "S_ISWHT", [](std::vector<object*> const& aArgs, object*) -> object* { long long m = 0; return aArgs.size() == 1u && to_index(aArgs[0], m) ? make_bool(false) : nullptr; } },
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
                    { "ST_UID", 4 }, { "ST_GID", 5 }, { "ST_SIZE", 6 }, { "ST_ATIME", 7 }, { "ST_MTIME", 8 }, { "ST_CTIME", 9 },
                    // (the names CPython defines on every platform: S_IREAD, S_IWRITE and S_IEXEC (Windows's), S_ENFMT; Solaris's and BSD's types: 0)
                    { "S_IREAD", 0400 }, { "S_IWRITE", 0200 }, { "S_IEXEC", 0100 }, { "S_ENFMT", 02000 }, { "S_IFDOOR", 0 }, { "S_IFPORT", 0 }, { "S_IFWHT", 0 } })
                    aAttributes.emplace_back(c.first, make_int(c.second));
                // (Lib/stat.py's names for chflags (BSD's UF_* and SF_*) and Windows's file attributes: on every platform)
                for (auto const& c : std::initializer_list<std::pair<char const*, long long>>{ { "UF_SETTABLE", 0x0000ffff }, { "UF_NODUMP", 0x00000001 }, 
                    { "UF_IMMUTABLE", 0x00000002 }, { "UF_APPEND", 0x00000004 }, { "UF_OPAQUE", 0x00000008 }, { "UF_NOUNLINK", 0x00000010 }, { "UF_COMPRESSED", 0x00000020 }, 
                    { "UF_TRACKED", 0x00000040 }, { "UF_DATAVAULT", 0x00000080 }, { "UF_HIDDEN", 0x00008000 }, { "SF_SETTABLE", 0xffff0000ll }, { "SF_ARCHIVED", 0x00010000 }, 
                    { "SF_IMMUTABLE", 0x00020000 }, { "SF_APPEND", 0x00040000 }, { "SF_RESTRICTED", 0x00080000 }, { "SF_NOUNLINK", 0x00100000 }, { "SF_SNAPSHOT", 0x00200000 }, 
                    { "SF_FIRMLINK", 0x00800000 }, { "SF_DATALESS", 0x40000000 }, { "FILE_ATTRIBUTE_ARCHIVE", 32 }, { "FILE_ATTRIBUTE_COMPRESSED", 2048 }, 
                    { "FILE_ATTRIBUTE_DEVICE", 64 }, { "FILE_ATTRIBUTE_DIRECTORY", 16 }, { "FILE_ATTRIBUTE_ENCRYPTED", 16384 }, { "FILE_ATTRIBUTE_HIDDEN", 2 }, 
                    { "FILE_ATTRIBUTE_INTEGRITY_STREAM", 32768 }, { "FILE_ATTRIBUTE_NORMAL", 128 }, { "FILE_ATTRIBUTE_NOT_CONTENT_INDEXED", 8192 }, 
                    { "FILE_ATTRIBUTE_NO_SCRUB_DATA", 131072 }, { "FILE_ATTRIBUTE_OFFLINE", 4096 }, { "FILE_ATTRIBUTE_READONLY", 1 }, { "FILE_ATTRIBUTE_REPARSE_POINT", 1024 }, 
                    { "FILE_ATTRIBUTE_SPARSE_FILE", 512 }, { "FILE_ATTRIBUTE_SYSTEM", 4 }, { "FILE_ATTRIBUTE_TEMPORARY", 256 }, { "FILE_ATTRIBUTE_VIRTUAL", 65536 } })
                    aAttributes.emplace_back(c.first, make_int(c.second));
                // (_filemode_table: filemode's ((bits, character), ...) for each of its ten characters)
                {
                    static std::vector<std::vector<std::pair<long long, char const*>>> const sTable = { 
                        { { 0120000, "l" }, { 0140000, "s" }, { 0100000, "-" }, { 0060000, "b" }, { 0040000, "d" }, { 0020000, "c" }, { 0010000, "p" } },
                        { { 0400, "r" } }, { { 0200, "w" } }, { { 0100 | 04000, "s" }, { 04000, "S" }, { 0100, "x" } }, { { 040, "r" } }, { { 020, "w" } }, 
                        { { 010 | 02000, "s" }, { 02000, "S" }, { 010, "x" } }, { { 04, "r" } }, { { 02, "w" } }, { { 01 | 01000, "t" }, { 01000, "T" }, { 01, "x" } } };
                    std::vector<object*> rows;
                    for (auto const& row : sTable)
                    {
                        std::vector<object*> entries;
                        for (auto const& [bits, character] : row)
                            entries.push_back(make_tuple({ make_int(bits), make_str(character) }));
                        rows.push_back(make_tuple(std::move(entries)));
                    }
                    aAttributes.emplace_back("_filemode_table", make_tuple(std::move(rows)));
                }
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
                    // (errorcode: the first name of a code, as CPython's (EDEADLOCK, not EDEADLK; ENOTSUP, not EOPNOTSUPP))
#ifdef EDEADLOCK
                    { "EDEADLOCK", EDEADLOCK },
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
#ifdef ENOTSUP
                    { "ENOTSUP", ENOTSUP },
#endif
#ifdef EOPNOTSUPP
                    { "EOPNOTSUPP", EOPNOTSUPP },
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
                    // (the codes of some systems only (e.g. Linux's); those that Winsock has are Winsock's where the C runtime has none)
#ifdef EADV
                    { "EADV", EADV },
#endif
#ifdef EBADE
                    { "EBADE", EBADE },
#endif
#ifdef EBADFD
                    { "EBADFD", EBADFD },
#endif
#ifdef EBADR
                    { "EBADR", EBADR },
#endif
#ifdef EBADRQC
                    { "EBADRQC", EBADRQC },
#endif
#ifdef EBADSLT
                    { "EBADSLT", EBADSLT },
#endif
#ifdef EBFONT
                    { "EBFONT", EBFONT },
#endif
#ifdef ECHRNG
                    { "ECHRNG", ECHRNG },
#endif
#ifdef ECOMM
                    { "ECOMM", ECOMM },
#endif
#ifdef EDOTDOT
                    { "EDOTDOT", EDOTDOT },
#endif
#if defined(EDQUOT)
                    { "EDQUOT", EDQUOT },
#elif defined(WSAEDQUOT)
                    { "EDQUOT", WSAEDQUOT },
#endif
#if defined(EHOSTDOWN)
                    { "EHOSTDOWN", EHOSTDOWN },
#elif defined(WSAEHOSTDOWN)
                    { "EHOSTDOWN", WSAEHOSTDOWN },
#endif
#ifdef EISNAM
                    { "EISNAM", EISNAM },
#endif
#ifdef EKEYEXPIRED
                    { "EKEYEXPIRED", EKEYEXPIRED },
#endif
#ifdef EKEYREJECTED
                    { "EKEYREJECTED", EKEYREJECTED },
#endif
#ifdef EKEYREVOKED
                    { "EKEYREVOKED", EKEYREVOKED },
#endif
#ifdef EL2HLT
                    { "EL2HLT", EL2HLT },
#endif
#ifdef EL2NSYNC
                    { "EL2NSYNC", EL2NSYNC },
#endif
#ifdef EL3HLT
                    { "EL3HLT", EL3HLT },
#endif
#ifdef EL3RST
                    { "EL3RST", EL3RST },
#endif
#ifdef ELIBACC
                    { "ELIBACC", ELIBACC },
#endif
#ifdef ELIBBAD
                    { "ELIBBAD", ELIBBAD },
#endif
#ifdef ELIBEXEC
                    { "ELIBEXEC", ELIBEXEC },
#endif
#ifdef ELIBMAX
                    { "ELIBMAX", ELIBMAX },
#endif
#ifdef ELIBSCN
                    { "ELIBSCN", ELIBSCN },
#endif
#ifdef ELNRNG
                    { "ELNRNG", ELNRNG },
#endif
#ifdef EMEDIUMTYPE
                    { "EMEDIUMTYPE", EMEDIUMTYPE },
#endif
#ifdef EMULTIHOP
                    { "EMULTIHOP", EMULTIHOP },
#endif
#ifdef ENAVAIL
                    { "ENAVAIL", ENAVAIL },
#endif
#ifdef ENOANO
                    { "ENOANO", ENOANO },
#endif
#ifdef ENOCSI
                    { "ENOCSI", ENOCSI },
#endif
#ifdef ENOKEY
                    { "ENOKEY", ENOKEY },
#endif
#ifdef ENOMEDIUM
                    { "ENOMEDIUM", ENOMEDIUM },
#endif
#ifdef ENONET
                    { "ENONET", ENONET },
#endif
#ifdef ENOPKG
                    { "ENOPKG", ENOPKG },
#endif
#ifdef ENOTBLK
                    { "ENOTBLK", ENOTBLK },
#endif
#ifdef ENOTNAM
                    { "ENOTNAM", ENOTNAM },
#endif
#ifdef ENOTUNIQ
                    { "ENOTUNIQ", ENOTUNIQ },
#endif
#if defined(EPFNOSUPPORT)
                    { "EPFNOSUPPORT", EPFNOSUPPORT },
#elif defined(WSAEPFNOSUPPORT)
                    { "EPFNOSUPPORT", WSAEPFNOSUPPORT },
#endif
#ifdef EREMCHG
                    { "EREMCHG", EREMCHG },
#endif
#if defined(EREMOTE)
                    { "EREMOTE", EREMOTE },
#elif defined(WSAEREMOTE)
                    { "EREMOTE", WSAEREMOTE },
#endif
#ifdef EREMOTEIO
                    { "EREMOTEIO", EREMOTEIO },
#endif
#ifdef ERESTART
                    { "ERESTART", ERESTART },
#endif
#ifdef ERFKILL
                    { "ERFKILL", ERFKILL },
#endif
#if defined(ESHUTDOWN)
                    { "ESHUTDOWN", ESHUTDOWN },
#elif defined(WSAESHUTDOWN)
                    { "ESHUTDOWN", WSAESHUTDOWN },
#endif
#if defined(ESOCKTNOSUPPORT)
                    { "ESOCKTNOSUPPORT", ESOCKTNOSUPPORT },
#elif defined(WSAESOCKTNOSUPPORT)
                    { "ESOCKTNOSUPPORT", WSAESOCKTNOSUPPORT },
#endif
#ifdef ESRMNT
                    { "ESRMNT", ESRMNT },
#endif
#if defined(ESTALE)
                    { "ESTALE", ESTALE },
#elif defined(WSAESTALE)
                    { "ESTALE", WSAESTALE },
#endif
#ifdef ESTRPIPE
                    { "ESTRPIPE", ESTRPIPE },
#endif
#if defined(ETOOMANYREFS)
                    { "ETOOMANYREFS", ETOOMANYREFS },
#elif defined(WSAETOOMANYREFS)
                    { "ETOOMANYREFS", WSAETOOMANYREFS },
#endif
#ifdef EUCLEAN
                    { "EUCLEAN", EUCLEAN },
#endif
#ifdef EUNATCH
                    { "EUNATCH", EUNATCH },
#endif
#if defined(EUSERS)
                    { "EUSERS", EUSERS },
#elif defined(WSAEUSERS)
                    { "EUSERS", WSAEUSERS },
#endif
#ifdef EXFULL
                    { "EXFULL", EXFULL },
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

            // (os.<aName>(args): a builtin function of the module (POSIX's too), or the module's attribute (e.g. fsdecode))
            object* os_call(char const* aName, std::vector<object*> const& aArgs, object* aKwargs = nullptr)
            {
                for (auto const& f : sOsFunctions)
                    if (std::strcmp(f.name, aName) == 0)
                        return f.function(aArgs, aKwargs);
#if !defined(_WIN32)
                for (auto const& f : sPosixFunctions)
                    if (std::strcmp(f.name, aName) == 0)
                        return f.function(aArgs, aKwargs);
#endif
                ref const function{ module_attribute("os", aName) };
                if (!function)
                    return nullptr;
                std::vector<object*> args;
                for (auto a : aArgs)
                    args.push_back(new_reference(a));
                return invoke(function.get(), std::move(args), aKwargs);
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

            // (is_block_device etc. (PathBase's): whether its mode (stat's) is of the kind; False if it does not exist or is a broken symbolic link 
            // (_ignore_error's errors: ENOENT, ENOTDIR, EBADF and ELOOP))
            object* path_is_kind(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, long long aKind)
            {
                path_self s;
                std::vector<object*> a;
                if (!path_self_of(aArgs, s) || !parse_arguments(aName, s.args, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                stat_values st;
                int e = 0;
                if (!do_stat(s.text, true, st, e))
                {
                    if (e == ENOENT || e == ENOTDIR || e == EBADF || e == ELOOP)
                        return make_bool(false);
                    ref const filename{ make_str(s.text) };
                    return raise_os_error(e, filename.get());
                }
                return make_bool((st.mode & kIFMT) == aKind);
            }

            // (owner and group (Path's): the login name of its uid (pwd.getpwuid) or the name of its gid (grp.getgrgid))
            object* path_owner(std::vector<object*> const& aArgs, object* aKwargs, bool aGroup)
            {
                path_self s;
                std::vector<object*> a;
                char const* const name = aGroup ? "group" : "owner";
                if (!path_self_of(aArgs, s) || !parse_arguments(name, s.args, aKwargs, { "follow_symlinks" }, 0u, 0u, a))
                    return nullptr;
                ref const kwargs{ make_dict() };
                if (a[0] != nullptr)
                {
                    ref const key{ make_str("follow_symlinks") };
                    dict_set(kwargs.get(), key.get(), a[0]);
                }
                ref const st{ path_os_call(s, "stat", {}, kwargs.get()) };
                ref const id{ st ? getattr(st.get(), aGroup ? "st_gid" : "st_uid") : nullptr };
                ref const lookup{ id ? module_attribute(aGroup ? "grp" : "pwd", aGroup ? "getgrgid" : "getpwuid") : nullptr };
                ref const entry{ lookup ? invoke(lookup.get(), { new_reference(id.get()) }) : nullptr };
                return entry ? getattr(entry.get(), aGroup ? "gr_name" : "pw_name") : nullptr;
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
                        if (!path_self_of(aArgs, s) || !parse_arguments("chmod", s.args, aKwargs, { "mode", "follow_symlinks" }, 1u, 1u, a))
                            return nullptr;
                        if (a[1] == nullptr)
                            return path_os_call(s, "chmod", { a[0] });
                        ref const kwargs{ make_dict() };
                        ref const key{ make_str("follow_symlinks") };
                        dict_set(kwargs.get(), key.get(), a[1]);
                        return path_os_call(s, "chmod", { a[0] }, kwargs.get());
                    } },
                // (Lib/pathlib's lchmod, symlink_to, hardlink_to, readlink, is_block_device, is_char_device, is_fifo, is_socket, owner, group and 
                // from_uri (CPython 3.13.18's _local.py and _abc.py))
                { "lchmod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.lchmod"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("lchmod", s.args, aKwargs, { "mode" }, 1u, 0u, a))
                            return nullptr;
                        ref const kwargs{ make_dict() };
                        ref const key{ make_str("follow_symlinks") };
                        ref const no{ make_bool(false) };
                        dict_set(kwargs.get(), key.get(), no.get());
                        return path_os_call(s, "chmod", { a[0] }, kwargs.get());
                    } },
                { "symlink_to", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.symlink_to"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("symlink_to", s.args, aKwargs, { "target", "target_is_directory" }, 1u, 0u, a))
                            return nullptr;
                        ref const path{ make_str(s.text) };
                        ref const directory{ make_bool(a[1] != nullptr && truth(a[1]) == 1) };
                        return os_call("symlink", { a[0], path.get(), a[1] != nullptr ? a[1] : directory.get() }, nullptr);
                    } },
                { "hardlink_to", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("pathlib.Path.hardlink_to"))
                            return nullptr;
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("hardlink_to", s.args, aKwargs, { "target" }, 1u, 0u, a))
                            return nullptr;
                        ref const path{ make_str(s.text) };
                        return os_call("link", { a[0], path.get() }, nullptr);
                    } },
                { "readlink", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        path_self s;
                        std::vector<object*> a;
                        if (!path_self_of(aArgs, s) || !parse_arguments("readlink", s.args, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref target{ path_os_call(s, "readlink") };
                        if (!target)
                            return nullptr;
                        return new_path(s.self, { target.release_ownership() });
                    } },
                { "is_block_device", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return path_is_kind(aArgs, aKwargs, "is_block_device", 0060000); } },
                { "is_char_device", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return path_is_kind(aArgs, aKwargs, "is_char_device", 0020000); } },
                { "is_fifo", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return path_is_kind(aArgs, aKwargs, "is_fifo", 0010000); } },
                { "is_socket", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return path_is_kind(aArgs, aKwargs, "is_socket", 0140000); } },
                { "owner", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return path_owner(aArgs, aKwargs, false); } },
                { "group", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return path_owner(aArgs, aKwargs, true); } },
                { "from_uri", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (aArgs.empty())
                            return error("TypeError", "from_uri() missing cls");
                        if (!parse_arguments("from_uri", { aArgs.begin() + 1, aArgs.end() }, aKwargs, { "uri" }, 1u, 0u, a))
                            return nullptr;
                        if (kind_of(builtin_value(a[0])) != NEOS_PY_STR)
                            return error("AttributeError", std::string{ "'" } + type_name(a[0]) + "' object has no attribute 'startswith'");
                        std::string const uri = as<str_object>(builtin_value(a[0]))->value;
                        if (!uri.starts_with("file:"))
                            return error("ValueError", "URI does not start with 'file:': " + repr_string(uri));
                        auto path = uri.substr(5u);
                        if (path.substr(0u, 3u) == "///")
                            path = path.substr(2u);         // (an empty authority)
                        else if (path.substr(0u, 12u) == "//localhost/")
                            path = path.substr(11u);        // (localhost's)
                        // (a slash before a DOS device or UNC path)
                        if (path.substr(0u, 3u) == "///" || (path.substr(0u, 1u) == "/" && path.size() > 2u && (path[2] == ':' || path[2] == '|')))
                            path = path.substr(1u);
                        // (a bar for a DOS drive's colon)
                        if (path.size() > 1u && path[1] == '|')
                            path = path.substr(0u, 1u) + ":" + path.substr(2u);
                        ref const text{ make_str(path) };
                        ref const unquote{ module_attribute("urllib.parse", "unquote_to_bytes") };
                        ref const bytes{ unquote ? invoke(unquote.get(), { new_reference(text.get()) }) : nullptr };
                        ref const decoded{ bytes ? os_call("fsdecode", { bytes.get() }, nullptr) : nullptr };
                        ref const result{ decoded ? invoke(aArgs[0], { new_reference(decoded.get()) }) : nullptr };
                        if (!result)
                            return nullptr;
                        ref const absolute{ call_method_named(result.get(), "is_absolute", {}) };
                        if (!absolute)
                            return nullptr;
                        if (truth(absolute.get()) != 1)
                            return error("ValueError", "URI is not absolute: " + repr_string(uri));
                        return new_reference(result.get());
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
                        if (std::strcmp(m.name, "cwd") == 0 || std::strcmp(m.name, "home") == 0 || std::strcmp(m.name, "from_uri") == 0)
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

            // (os.PathLike (Lib/os.py's): the abstract base class of the file system path protocol (an object with __fspath__))
            bool os_only_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (!os_module(aAttributes))
                    return false;
                static object* const sPathLike = py_guarded([]()
                {
                    auto const native = [](char const* aName, char const* aQualname, char const* aDoc, native_body aBody)
                    {
                        return make_native_function(aName, aQualname, aDoc, "os", std::move(aBody));
                    };
                    py const ns = py_dict();
                    ns.set_item(py{ "__module__" }, py{ "os" });
                    ns.set_item(py{ "__qualname__" }, py{ "PathLike" });
                    ns.set_item(py{ "__doc__" }, py{ "Abstract base class for implementing the file system path protocol." });
                    ns.set_item(py{ "__slots__" }, py_tuple());
                    ns.set_item(py{ "__fspath__" }, import_attribute("abc", "abstractmethod")(native("__fspath__", "PathLike.__fspath__",
                        "Return the file system path representation of the object.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            py_bind("PathLike.__fspath__", { py_positional("self") }, aArgs, aKwargs);
                            py_raise(builtin("NotImplementedError"));
                        })));
                    ns.set_item(py{ "__subclasshook__" }, builtin("classmethod")(native("__subclasshook__", "PathLike.__subclasshook__", "",
                        [](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = py_bind("PathLike.__subclasshook__", { py_positional("cls"), py_positional("subclass") }, aArgs, aKwargs);
                            if (!a[0].is(import_attribute("os", "PathLike")))
                                return builtin("NotImplemented");
                            // (_collections_abc._check_methods(subclass, '__fspath__'))
                            for (py const& b : a[1].attr("__mro__").items())
                            {
                                py const d = b.attr("__dict__");
                                if (d.contains(py{ "__fspath__" }))
                                    return d[py{ "__fspath__" }].is_none() ? builtin("NotImplemented") : py::boolean(true);
                            }
                            return builtin("NotImplemented");
                        })));
                    ns.set_item(py{ "__class_getitem__" }, builtin("classmethod")(import_attribute("types", "GenericAlias")));
                    return import_attribute("abc", "ABCMeta")(py{ "PathLike" }, py_tuple({ import_attribute("abc", "ABC") }), ns);
                });
                if (sPathLike == nullptr)
                    return false;
                retain(sPathLike);
                aAttributes.emplace_back("PathLike", sPathLike);
                // (Lib/os.py's fsencode, fsdecode and get_exec_path: the file system's encoding is UTF-8 (its errors: surrogateescape))
                static object* const sFunctions = py_guarded([]()
                {
                    auto const native = [](char const* aName, char const* aDoc, native_body aBody)
                    {
                        return make_native_function(aName, aName, aDoc, "os", std::move(aBody));
                    };
                    auto const fspath = [](py const& aFilename) -> py
                    {
                        if (isinstance(aFilename, builtin("str")) || isinstance(aFilename, builtin("bytes")))
                            return aFilename;
                        return import_attribute("os", "fspath")(aFilename);
                    };
                    py const fsencode = native("fsencode", "Encode filename (an os.PathLike, bytes, or str) to the filesystem\nencoding with 'surrogateescape' error handler, return bytes unchanged.\nOn Windows, use 'strict' error handler if the file system encoding is\n'mbcs' (which is the default encoding).",
                        [fspath](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = py_bind("fsencode", { py_positional("filename") }, aArgs, aKwargs);
                            py const filename = fspath(a[0]);
                            if (isinstance(filename, builtin("str")))
                                return filename.method("encode", { py{ "utf-8" }, py{ "surrogateescape" } });
                            return filename;
                        });
                    py const fsdecode = native("fsdecode", "Decode filename (an os.PathLike, bytes, or str) from the filesystem\nencoding with 'surrogateescape' error handler, return str unchanged. On\nWindows, use 'strict' error handler if the file system encoding is\n'mbcs' (which is the default encoding).",
                        [fspath](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = py_bind("fsdecode", { py_positional("filename") }, aArgs, aKwargs);
                            py const filename = fspath(a[0]);
                            if (isinstance(filename, builtin("bytes")))
                                return filename.method("decode", { py{ "utf-8" }, py{ "surrogateescape" } });
                            return filename;
                        });
                    py const getExecPath = native("get_exec_path", "Returns the sequence of directories that will be searched for the\nnamed executable (similar to a shell) when launching a process.\n\n*env* must be an environment variable dict or None.  If *env* is None,\nos.environ will be used.",
                        [fsdecode](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = py_bind("get_exec_path", { py_positional("env", py::none()) }, aArgs, aKwargs);
                            py const env = a[0].is_none() ? import_attribute("os", "environ") : a[0];
                            // (env.get('PATH'), or env[b'PATH'] (not both))
                            py pathList = py::none();
                            try
                            {
                                pathList = env.method("get", { py{ "PATH" } });
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                if (!isinstance(e, builtin("TypeError")))
                                    py_reraise(e);
                            }
#if !defined(_WIN32)
                            try
                            {
                                py const pathListB = env[py{ make_bytes("PATH") }];
                                if (!pathList.is_none())
                                    py_raise("ValueError", "env cannot contain 'PATH' and b'PATH' keys");
                                pathList = pathListB;
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                if (!isinstance(e, py_tuple({ builtin("KeyError"), builtin("TypeError") })))
                                    py_reraise(e);
                            }
                            if (!pathList.is_none() && isinstance(pathList, builtin("bytes")))
                                pathList = fsdecode(pathList);
#endif
                            if (pathList.is_none())
                                pathList = import_attribute("os", "defpath");
                            return pathList.method("split", { import_attribute("os", "pathsep") });
                        });
                    return py_tuple({ fsencode, fsdecode, getExecPath });
                });
                if (sFunctions == nullptr)
                    return false;
                for (std::size_t i = 0u; i < 3u; ++i)
                {
                    auto const f = as<tuple_object>(sFunctions)->items[i];
                    aAttributes.emplace_back(i == 0u ? "fsencode" : i == 1u ? "fsdecode" : "get_exec_path", (retain(f), f));
                }
                // (os.environ (and, POSIX, os.environb): ported from CPython 3.13.18's Lib/os.py (_Environ, _createenviron): a mapping of the
                // process's environment (posix.environ: its data (str keys and values)) whose changes are its (putenv, unsetenv), so that child
                // processes and the C library (e.g. time.tzset's TZ) see them)
                static object* const sEnvironments = py_guarded([]()
                {
                    auto const native = [](char const* aName, char const* aDoc, native_body aBody)
                    {
                        return make_native_function(aName, std::string{ "_Environ." } + aName, aDoc, "os", std::move(aBody));
                    };
                    auto const self_of = [](char const* aName, std::vector<py> const& aArgs, object* aKwargs, std::vector<py_parameter> aParameters)
                    {
                        aParameters.insert(aParameters.begin(), py_positional("self"));
                        return py_bind(std::string{ "_Environ." } + aName, aParameters, aArgs, aKwargs);
                    };
                    py const ns = py_dict();
                    ns.set_item(py{ "__module__" }, py{ "os" });
                    ns.set_item(py{ "__qualname__" }, py{ "_Environ" });
                    ns.set_item(py{ "__init__" }, native("__init__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__init__", aArgs, aKwargs, { py_positional("data"), py_positional("encodekey"), py_positional("decodekey"),
                                py_positional("encodevalue"), py_positional("decodevalue") });
                            a[0].set_attr("encodekey", a[2]);
                            a[0].set_attr("decodekey", a[3]);
                            a[0].set_attr("encodevalue", a[4]);
                            a[0].set_attr("decodevalue", a[5]);
                            a[0].set_attr("_data", a[1]);
                            return py::none();
                        }));
                    ns.set_item(py{ "__getitem__" }, native("__getitem__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__getitem__", aArgs, aKwargs, { py_positional("key") });
                            py value;
                            try
                            {
                                value = a[0].attr("_data")[a[0].attr("encodekey")(a[1])];
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                if (!isinstance(e, builtin("KeyError")))
                                    py_reraise(e);
                                // (raise KeyError(key) from None)
                                py const error = builtin("KeyError")(a[1]);
                                error.set_attr("__suppress_context__", py::boolean(true));
                                py_raise(error);
                            }
                            return a[0].attr("decodevalue")(value);
                        }));
                    ns.set_item(py{ "__setitem__" }, native("__setitem__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__setitem__", aArgs, aKwargs, { py_positional("key"), py_positional("value") });
                            py const key = a[0].attr("encodekey")(a[1]);
                            py const value = a[0].attr("encodevalue")(a[2]);
                            import_attribute("os", "putenv")(key, value);
                            a[0].attr("_data").set_item(key, value);
                            return py::none();
                        }));
                    ns.set_item(py{ "__delitem__" }, native("__delitem__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__delitem__", aArgs, aKwargs, { py_positional("key") });
                            py const encodedKey = a[0].attr("encodekey")(a[1]);
                            import_attribute("os", "unsetenv")(encodedKey);
                            try
                            {
                                a[0].attr("_data").method("__delitem__", { encodedKey });
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                if (!isinstance(e, builtin("KeyError")))
                                    py_reraise(e);
                                py const error = builtin("KeyError")(a[1]);
                                error.set_attr("__suppress_context__", py::boolean(true));
                                py_raise(error);
                            }
                            return py::none();
                        }));
                    ns.set_item(py{ "__iter__" }, native("__iter__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            // (the keys when it is called, decoded: a list's iterator (as the generator's: its keys are listed first))
                            auto const a = self_of("__iter__", aArgs, aKwargs, {});
                            std::vector<py> keys;
                            py const decodekey = a[0].attr("decodekey");
                            for (py const& key : builtin("list")(a[0].attr("_data")).items())
                                keys.push_back(decodekey(key));
                            return builtin("iter")(py_list(keys));
                        }));
                    ns.set_item(py{ "__len__" }, native("__len__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__len__", aArgs, aKwargs, {});
                            return builtin("len")(a[0].attr("_data"));
                        }));
                    ns.set_item(py{ "__repr__" }, native("__repr__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__repr__", aArgs, aKwargs, {});
                            std::string items;
                            for (py const& item : a[0].attr("_data").method("items").items())
                            {
                                if (!items.empty())
                                    items += ", ";
                                items += a[0].attr("decodekey")(item[py{ 0 }]).repr() + ": " + a[0].attr("decodevalue")(item[py{ 1 }]).repr();
                            }
                            return py{ "environ({" + items + "})" };
                        }));
                    ns.set_item(py{ "copy" }, native("copy", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("copy", aArgs, aKwargs, {});
                            return builtin("dict")(a[0]);
                        }));
                    ns.set_item(py{ "setdefault" }, native("setdefault", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("setdefault", aArgs, aKwargs, { py_positional("key"), py_positional("value") });
                            if (!a[0].contains(a[1]))
                                a[0].set_item(a[1], a[2]);
                            return a[0][a[1]];
                        }));
                    ns.set_item(py{ "__ior__" }, native("__ior__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__ior__", aArgs, aKwargs, { py_positional("other") });
                            a[0].method("update", { a[1] });
                            return a[0];
                        }));
                    ns.set_item(py{ "__or__" }, native("__or__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__or__", aArgs, aKwargs, { py_positional("other") });
                            if (!isinstance(a[1], import_attribute("collections.abc", "Mapping")))
                                return builtin("NotImplemented");
                            py const result = builtin("dict")(a[0]);
                            result.method("update", { a[1] });
                            return result;
                        }));
                    ns.set_item(py{ "__ror__" }, native("__ror__", "", [self_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = self_of("__ror__", aArgs, aKwargs, { py_positional("other") });
                            if (!isinstance(a[1], import_attribute("collections.abc", "Mapping")))
                                return builtin("NotImplemented");
                            py const result = builtin("dict")(a[1]);
                            result.method("update", { a[0] });
                            return result;
                        }));
                    py const cls = builtin("type")(py{ "_Environ" }, py_tuple({ import_attribute("collections.abc", "MutableMapping") }), ns);
                    // (_createenviron: its data is the process's environment (str keys and values); keys and values must be str (environb's: 
                    // bytes, of the same data: decoded and encoded as the file system's (UTF-8, surrogateescape)))
                    // (_createenviron: keys and values must be str (encodekey, encodevalue); POSIX: the data is posix.environ (bytes: encoded 
                    // and decoded as the file system's, UTF-8 with surrogateescape), and environb is of the same data (bytes, _check_bytes); 
                    // Windows: a copy of nt.environ whose names are upper case)
                    auto const typed = [](char const* aName, char const* aType, bool aEncode, bool aUpper) -> py
                    {
                        std::string const type{ aType };
                        return make_native_function(aName, aName, "", "os", [type, aEncode, aUpper](std::vector<py> const& aArgs, object*) -> py
                            {
                                if (aArgs.size() != 1u)
                                    py_raise("TypeError", "expected 1 argument");
                                if (!isinstance(aArgs[0], builtin(type.c_str())))
                                    py_raise("TypeError", type + " expected, not " + std::string{ type_name(aArgs[0].get()) });
                                py value = aArgs[0];
                                if (aUpper)
                                    value = value.method("upper");
                                return aEncode ? value.method("encode", { py{ "utf-8" }, py{ "surrogateescape" } }) : value;
                            });
                    };
#if defined(_WIN32)
                    py const encode = typed("check_str", "str", false, false);
                    py const encodekey = typed("encodekey", "str", false, true);
                    py const decode = builtin("str");
                    py const data = py_dict();
                    for (py const& item : py{ environment() }.method("items").items())
                        data.set_item(encodekey(item[py{ 0 }]), item[py{ 1 }]);
                    py const environMapping = cls(data, encodekey, decode, encode, decode);    // (not "environ": a macro of the Windows C runtime)
                    return py_tuple({ environMapping });
#else
                    py const data{ environment() };
                    py const encode = typed("encode", "str", true, false);
                    py const decode = make_native_function("decode", "decode", "", "os", [](std::vector<py> const& aArgs, object*) -> py
                        {
                            return aArgs.at(0).method("decode", { py{ "utf-8" }, py{ "surrogateescape" } });
                        });
                    py const environMapping = cls(data, encode, decode, encode, decode);
                    py const checkBytes = typed("_check_bytes", "bytes", false, false);
                    py const environb = cls(environMapping.attr("_data"), checkBytes, builtin("bytes"), checkBytes, builtin("bytes"));
                    return py_tuple({ environMapping, environb });
#endif
                });
                if (sEnvironments == nullptr)
                    return false;
                for (auto& a : aAttributes)
                    if (a.first == "environ")
                    {
                        release(a.second);
                        a.second = new_reference(as<tuple_object>(sEnvironments)->items[0]);
                    }
                if (as<tuple_object>(sEnvironments)->items.size() > 1u)
                    aAttributes.emplace_back("environb", new_reference(as<tuple_object>(sEnvironments)->items[1]));
                // (Lib/os.py's getenv and (POSIX) getenvb: environ.get(key, default) and environb.get(key, default))
                for (auto const& [name, mapping, doc] : { std::tuple{ "getenv", "environ", "Get an environment variable, return None if it doesn't exist.\n    "
                    "The optional second argument can specify an alternate default.\n    key, default and the result are str." }, std::tuple{ "getenvb", "environb",
                    "Get an environment variable, return None if it doesn't exist.\n        The optional second argument can specify an alternate default.\n        "
                    "key, default and the result are bytes." } })
                {
#if defined(_WIN32)
                    if (std::string_view{ name } == "getenvb")
                        continue;
#endif
                    std::string const functionName{ name };
                    std::string const mappingName{ mapping };
                    auto const function = py_guarded([&]()
                    {
                        return make_native_function(functionName, functionName, doc, "os", [functionName, mappingName](std::vector<py> const& aArgs, object* aKwargs) -> py
                            {
                                auto const a = py_bind(functionName, { py_positional("key"), py_positional("default", py::none()) }, aArgs, aKwargs);
                                return import_attribute("os", mappingName.c_str()).method("get", { a[0], a[1] });
                            });
                    });
                    if (function == nullptr)
                        return false;
                    auto const existing = std::find_if(aAttributes.begin(), aAttributes.end(), [&](auto const& a) { return a.first == functionName; });
                    if (existing != aAttributes.end())
                    {
                        release(existing->second);
                        existing->second = function;
                    }
                    else
                        aAttributes.emplace_back(functionName, function);
                }
#if !defined(_WIN32)
                // (Lib/os.py's execl, execle, execlp, execlpe, execvp, execvpe (_execvpe) and, of fork and execv, P_WAIT, P_NOWAIT, P_NOWAITO, 
                // spawnv, spawnve, spawnvp, spawnvpe (_spawnvef), spawnl, spawnle, spawnlp and spawnlpe (CPython 3.13.18's))
                static object* const sExecFunctions = py_guarded([]()
                {
                    auto const os = [](char const* aName) { return import_attribute("os", aName); };
                    // (a function of *args: its required positional arguments (as a Python function's, CPython's messages), then the rest)
                    auto const native = [](char const* aName, char const* aDoc, std::vector<char const*> aParameters, std::function<py(std::vector<py> const&, py const&)> aBody)
                    {
                        std::string const name{ aName };
                        return make_native_function(name, name, aDoc, "os", [name, aParameters, aBody](std::vector<py> const& aArgs, object* aKwargs) -> py
                            {
                                std::vector<py_parameter> parameters;
                                for (auto p : aParameters)
                                    parameters.push_back(py_positional(p));
                                py rest;
                                auto const a = py_bind(name, parameters, aArgs, aKwargs, &rest);
                                return aBody(a, rest);
                            });
                    };
                    auto const tail = [](py const& aTuple) -> py
                    {
                        // (args[-1] and args[:-1])
                        return aTuple[builtin("slice")(py::none(), py{ -1 })];
                    };
                    // (_execvpe(file, args, env=None))
                    auto const execvpe = [os](py const& aFile, py const& aArgs, py aEnv) -> py
                    {
                        py execFunction;
                        std::vector<py> argrest;
                        if (!aEnv.is_none())
                        {
                            execFunction = os("execve");
                            argrest = { aArgs, aEnv };
                        }
                        else
                        {
                            execFunction = os("execv");
                            argrest = { aArgs };
                            aEnv = os("environ");
                        }
                        py file = aFile;
                        if (os("path").attr("dirname")(file).truthy())
                        {
                            std::vector<py> callArgs{ file };
                            callArgs.insert(callArgs.end(), argrest.begin(), argrest.end());
                            execFunction.call(callArgs);
                            return py::none();
                        }
                        py savedException = py::none();
                        py lastException = py::none();
                        file = os("fsencode")(file);
                        for (py const& directory : os("get_exec_path")(aEnv).items())
                        {
                            py const fullname = os("path").attr("join")(os("fsencode")(directory), file);
                            try
                            {
                                std::vector<py> callArgs{ fullname };
                                callArgs.insert(callArgs.end(), argrest.begin(), argrest.end());
                                execFunction.call(callArgs);
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                if (isinstance(e, builtin("FileNotFoundError")) || isinstance(e, builtin("NotADirectoryError")))
                                    lastException = e;
                                else if (isinstance(e, builtin("OSError")))
                                {
                                    lastException = e;
                                    if (savedException.is_none())
                                        savedException = e;
                                }
                                else
                                    py_reraise(e);
                            }
                        }
                        if (!savedException.is_none())
                            py_raise(savedException);
                        py_raise(lastException);
                    };
                    std::vector<std::pair<std::string, py>> functions;
                    functions.emplace_back("execl", native("execl", "execl(file, *args)\n\n    Execute the executable file with argument list args, replacing the\n    current process. ",
                        { "file" }, [os](std::vector<py> const& a, py const& rest) -> py { return os("execv")(a[0], rest); }));
                    functions.emplace_back("execle", native("execle", "execle(file, *args, env)\n\n    Execute the executable file with argument list args and\n    environment env, replacing the current process. ",
                        { "file" }, [os, tail](std::vector<py> const& a, py const& rest) -> py { return os("execve")(a[0], tail(rest), rest[py{ -1 }]); }));
                    functions.emplace_back("execlp", native("execlp", "execlp(file, *args)\n\n    Execute the executable file (which is searched for along $PATH)\n    with argument list args, replacing the current process. ",
                        { "file" }, [os](std::vector<py> const& a, py const& rest) -> py { return os("execvp")(a[0], rest); }));
                    functions.emplace_back("execlpe", native("execlpe", "execlpe(file, *args, env)\n\n    Execute the executable file (which is searched for along $PATH)\n    with argument list args and environment env, replacing the current\n    process. ",
                        { "file" }, [os, tail](std::vector<py> const& a, py const& rest) -> py { return os("execvpe")(a[0], tail(rest), rest[py{ -1 }]); }));
                    functions.emplace_back("_execvpe", make_native_function("_execvpe", "_execvpe", "", "os", [execvpe](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = py_bind("_execvpe", { py_positional("file"), py_positional("args"), py_positional("env", py::none()) }, aArgs, aKwargs);
                            return execvpe(a[0], a[1], a[2]);
                        }));
                    functions.emplace_back("execvp", make_native_function("execvp", "execvp", "execvp(file, args)\n\n    Execute the executable file (which is searched for along $PATH)\n    with argument list args, replacing the current process.\n    args may be a list or tuple of strings. ", "os",
                        [execvpe](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = py_bind("execvp", { py_positional("file"), py_positional("args") }, aArgs, aKwargs);
                            return execvpe(a[0], a[1], py::none());
                        }));
                    functions.emplace_back("execvpe", make_native_function("execvpe", "execvpe", "execvpe(file, args, env)\n\n    Execute the executable file (which is searched for along $PATH)\n    with argument list args and environment env, replacing the\n    current process.\n    args may be a list or tuple of strings. ", "os",
                        [execvpe](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const a = py_bind("execvpe", { py_positional("file"), py_positional("args"), py_positional("env") }, aArgs, aKwargs);
                            return execvpe(a[0], a[1], a[2]);
                        }));
                    // (_spawnvef(mode, file, args, env, func))
                    auto const spawn = [os](py const& aMode, py const& aFile, py const& aArgs, py const& aEnv, char const* aFunction) -> py
                    {
                        if (!isinstance(aArgs, builtin("tuple")) && !isinstance(aArgs, builtin("list")))
                            py_raise("TypeError", "argv must be a tuple or a list");
                        if (!aArgs.truthy() || !aArgs[py{ 0 }].truthy())
                            py_raise("ValueError", "argv first element cannot be empty");
                        py const pid = os("fork")();
                        if (!pid.truthy())
                        {
                            // (the child)
                            try
                            {
                                if (aEnv.is_none())
                                    os(aFunction)(aFile, aArgs);
                                else
                                    os(aFunction)(aFile, aArgs, aEnv);
                            }
                            catch (raised const&)
                            {
                                py_take_exception();
                                os("_exit")(py{ 127 });
                            }
                            return py::none();
                        }
                        // (the parent)
                        if (aMode == os("P_NOWAIT"))
                            return pid;
                        for (;;)
                        {
                            py const result = os("waitpid")(pid, py{ 0 });
                            py const status = result[py{ 1 }];
                            if (os("WIFSTOPPED")(status).truthy())
                                continue;
                            return os("waitstatus_to_exitcode")(status);
                        }
                    };
                    struct spawner { char const* name; char const* function; bool env; char const* doc; };
                    static spawner const sSpawners[] =
                    {
                        { "spawnv", "execv", false, "spawnv(mode, file, args) -> integer\n\nExecute file with arguments from args in a subprocess.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. " },
                        { "spawnve", "execve", true, "spawnve(mode, file, args, env) -> integer\n\nExecute file with arguments from args in a subprocess with the\nspecified environment.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. " },
                        { "spawnvp", "execvp", false, "spawnvp(mode, file, args) -> integer\n\nExecute file (which is looked for along $PATH) with arguments from\nargs in a subprocess.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. " },
                        { "spawnvpe", "execvpe", true, "spawnvpe(mode, file, args, env) -> integer\n\nExecute file (which is looked for along $PATH) with arguments from\nargs in a subprocess with the supplied environment.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. " }
                    };
                    for (auto const& s : sSpawners)
                    {
                        auto const& spawnerEntry = s;
                        functions.emplace_back(s.name, make_native_function(s.name, s.name, s.doc, "os", [spawn, spawnerEntry](std::vector<py> const& aArgs, object* aKwargs) -> py
                            {
                                std::vector<py_parameter> parameters{ py_positional("mode"), py_positional("file"), py_positional("args") };
                                if (spawnerEntry.env)
                                    parameters.push_back(py_positional("env"));
                                auto const a = py_bind(spawnerEntry.name, parameters, aArgs, aKwargs);
                                return spawn(a[0], a[1], a[2], spawnerEntry.env ? a[3] : py::none(), spawnerEntry.function);
                            }));
                    }
                    functions.emplace_back("spawnl", native("spawnl", "spawnl(mode, file, *args) -> integer\n\nExecute file with arguments from args in a subprocess.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. ",
                        { "mode", "file" }, [os](std::vector<py> const& a, py const& rest) -> py { return os("spawnv")(a[0], a[1], rest); }));
                    functions.emplace_back("spawnle", native("spawnle", "spawnle(mode, file, *args, env) -> integer\n\nExecute file with arguments from args in a subprocess with the\nsupplied environment.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. ",
                        { "mode", "file" }, [os, tail](std::vector<py> const& a, py const& rest) -> py { return os("spawnve")(a[0], a[1], tail(rest), rest[py{ -1 }]); }));
                    functions.emplace_back("spawnlp", native("spawnlp", "spawnlp(mode, file, *args) -> integer\n\nExecute file (which is looked for along $PATH) with arguments from\nargs in a subprocess with the supplied environment.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. ",
                        { "mode", "file" }, [os](std::vector<py> const& a, py const& rest) -> py { return os("spawnvp")(a[0], a[1], rest); }));
                    functions.emplace_back("spawnlpe", native("spawnlpe", "spawnlpe(mode, file, *args, env) -> integer\n\nExecute file (which is looked for along $PATH) with arguments from\nargs in a subprocess with the supplied environment.\nIf mode == P_NOWAIT return the pid of the process.\nIf mode == P_WAIT return the process's exit code if it exits normally;\notherwise return -SIG, where SIG is the signal that killed it. ",
                        { "mode", "file" }, [os, tail](std::vector<py> const& a, py const& rest) -> py { return os("spawnvpe")(a[0], a[1], tail(rest), rest[py{ -1 }]); }));
                    std::vector<py> items;
                    for (auto const& f : functions)
                        items.push_back(py_tuple({ py{ f.first }, f.second }));
                    return py_tuple(items);
                });
                if (sExecFunctions == nullptr)
                    return false;
                for (auto const item : as<tuple_object>(sExecFunctions)->items)
                {
                    auto const& pair = as<tuple_object>(item)->items;
                    aAttributes.emplace_back(as<str_object>(pair[0])->value, new_reference(pair[1]));
                }
                aAttributes.emplace_back("P_WAIT", make_int(0));
                aAttributes.emplace_back("P_NOWAIT", make_int(1));
                aAttributes.emplace_back("P_NOWAITO", make_int(1));
#endif
                // (the names that Lib/os.py imports (abc, sys, st, GenericAlias, Mapping, MutableMapping and _check_methods) or defines (_fspath, 
                // _Environ (os.environ's class), _exists, _get_exports_list and renames))
                {
                    static object* const sOsNames = py_guarded([&]()
                    {
                        std::vector<py> items;
                        auto const add = [&](char const* aName, py const& aValue) { items.push_back(py_tuple({ py{ aName }, aValue })); };
                        add("abc", py{ fn_import("abc") });
                        add("sys", py{ fn_import("sys") });
                        add("st", py{ fn_import("stat") });
                        add("GenericAlias", import_attribute("types", "GenericAlias"));
                        add("Mapping", import_attribute("collections.abc", "Mapping"));
                        add("MutableMapping", import_attribute("collections.abc", "MutableMapping"));
                        add("_check_methods", import_attribute("_collections_abc", "_check_methods"));
                        add("_exists", make_native_function("_exists", "_exists", "", "os", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                            {
                                auto const a = py_bind("_exists", { py_positional("name") }, aArgs, aKwargs);
                                return py::boolean(py{ fn_import("os") }.attr("__dict__").method("__contains__", { a[0] }).truthy());
                            }));
                        add("_get_exports_list", make_native_function("_get_exports_list", "_get_exports_list", "", "os", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                            {
                                auto const a = py_bind("_get_exports_list", { py_positional("module") }, aArgs, aKwargs);
                                try
                                {
                                    return builtin("list")(a[0].attr("__all__"));
                                }
                                catch (raised const&)
                                {
                                    py const e = py_take_exception();
                                    if (!isinstance(e, builtin("AttributeError")))
                                        py_reraise(e);
                                }
                                std::vector<py> names;
                                for (py const& n : builtin("dir")(a[0]).items())
                                    if (!n.str().empty() && n.str()[0] != '_')
                                        names.push_back(n);
                                return py_list(names);
                            }));
                        add("renames", make_native_function("renames", "renames", "renames(old, new)\n\n    Super-rename; create directories as necessary and delete any left\n"
                            "    empty.  Works like rename, except creation of any intermediate\n    directories needed to make the new pathname good is attempted\n"
                            "    first.  After the rename, directories corresponding to rightmost\n    path segments of the old name will be pruned until either the\n"
                            "    whole path is consumed or a nonempty directory is found.\n\n    Note: this function can fail with the new directory structure made\n"
                            "    if you lack permissions needed to unlink the leaf directory or\n    file.\n\n    ", "os", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                            {
                                auto const a = py_bind("renames", { py_positional("old"), py_positional("new") }, aArgs, aKwargs);
                                py const os{ fn_import("os") };
                                py const path = os.attr("path");
                                py parts = path.attr("split")(a[1]);
                                py head = parts[py{ 0 }], tail = parts[py{ 1 }];
                                if (head.truthy() && tail.truthy() && !path.attr("exists")(head).truthy())
                                    os.attr("makedirs")(head);
                                os.attr("rename")(a[0], a[1]);
                                parts = path.attr("split")(a[0]);
                                head = parts[py{ 0 }];
                                tail = parts[py{ 1 }];
                                if (head.truthy() && tail.truthy())
                                {
                                    try
                                    {
                                        os.attr("removedirs")(head);
                                    }
                                    catch (raised const&)
                                    {
                                        py const e = py_take_exception();
                                        if (!isinstance(e, builtin("OSError")))
                                            py_reraise(e);
                                    }
                                }
                                return py::none();
                            }));
                        return py_tuple(items);
                    });
                    if (sOsNames == nullptr)
                        return false;
                    for (auto const item : as<tuple_object>(sOsNames)->items)
                    {
                        auto const& pair = as<tuple_object>(item)->items;
                        aAttributes.emplace_back(as<str_object>(pair[0])->value, new_reference(pair[1]));
                    }
                    for (auto const& [name, value] : std::vector<std::pair<std::string, object*>>{ aAttributes })
                    {
                        // (_fspath: os.py's (a function of its own, as fspath's))
                        if (name == "fspath")
                        {
                            object* const fspath = value;
                            ref const pure{ py_guarded([fspath]()
                            {
                                py const function = py::borrowed(fspath);
                                return make_native_function("_fspath", "_fspath", "Return the path representation of a path-like object.\n\n    If str or bytes is "
                                    "passed in, it is returned unchanged. Otherwise the\n    os.PathLike interface is used to get the path representation. If the\n    path "
                                    "representation is not str or bytes, TypeError is raised. If the\n    provided path is not str, bytes, or os.PathLike, TypeError is raised.\n    ", "os",
                                    [function](std::vector<py> const& aArgs, object* aKwargs) -> py
                                    {
                                        auto const a = py_bind("_fspath", { py_positional("path") }, aArgs, aKwargs);
                                        return function(a[0]);
                                    });
                            }) };
                            if (!pure)
                                return false;
                            aAttributes.emplace_back("_fspath", new_reference(pure.get()));
                        }
                        if (name == "environ")
                            aAttributes.emplace_back("_Environ", class_of(value));
                    }
                }
                // (__all__: as Lib/os.py's (its own names, posix's public names (_get_exports_list) and _exit): the public names but the 
                // modules and names that os.py imports (abc, sys, st, GenericAlias, Mapping, MutableMapping), PathLike, process_cpu_count and the
                // supports_* sets)
                {
                    static std::set<std::string_view> const sExcluded = { "GenericAlias", "Mapping", "MutableMapping", "PathLike", "abc", "process_cpu_count", "st", 
                        "supports_dir_fd", "supports_effective_ids", "supports_fd", "supports_follow_symlinks", "sys" };
                    std::vector<object*> names;
                    std::set<std::string> seen;
                    auto const add = [&](std::string const& aName)
                    {
                        if (seen.insert(aName).second)
                            names.push_back(make_str(aName));
                    };
                    for (auto const& a : aAttributes)
                        if (!a.first.empty() && a.first[0] != '_' && !sExcluded.contains(a.first))
                            add(a.first);
                    add("path");
                    if (std::any_of(aAttributes.begin(), aAttributes.end(), [](auto const& a) { return a.first == "_exit"; }))
                        add("_exit");
                    aAttributes.emplace_back("__all__", make_list(std::move(names)));
                }
                return true;
            }

            inline bool const sOs = register_module("os", os_only_module);
#if defined(_WIN32)
            inline bool const sPosixModule = register_module("nt", os_module);
#else
            inline bool const sPosixModule = register_module("posix", os_module);
#endif
#if !defined(_WIN32)
            // --- resource (Modules/resource.c: getrlimit, setrlimit, prlimit, getrusage and getpagesize; struct_rusage; RLIMIT_*, RUSAGE_* 
            // and RLIM_INFINITY (-1, as CPython converts it))

            object* rlimit_result(struct rlimit const& aLimit)
            {
                auto const value = [](rlim_t aValue) { return aValue == RLIM_INFINITY ? make_int(-1) : make_int(static_cast<long long>(aValue)); };
                return make_tuple({ value(aLimit.rlim_cur), value(aLimit.rlim_max) });
            }

            // (py2rlimit: a sequence of two ints (-1: RLIM_INFINITY))
            bool rlimit_argument(object* aLimits, struct rlimit& aResult)
            {
                std::vector<object*> items;
                ref const tuple{ fn_tuple_of(aLimits) };
                if (!tuple)
                {
                    clear_error();
                    error("ValueError", "expected a tuple of 2 integers");
                    return false;
                }
                auto const& parts = as<tuple_object>(tuple.get())->items;
                if (parts.size() != 2u)
                {
                    error("ValueError", "expected a tuple of 2 integers");
                    return false;
                }
                rlim_t* const targets[] = { &aResult.rlim_cur, &aResult.rlim_max };
                for (std::size_t i = 0u; i < 2u; ++i)
                {
                    long long value = 0;
                    if (!to_index(parts[i], value))
                        return false;
                    *targets[i] = value == -1 ? RLIM_INFINITY : static_cast<rlim_t>(value);
                }
                return true;
            }

            bool resource_kind(object* aResource, int& aResult, char const* aFunction)
            {
                long long value = 0;
                if (!int_argument(aResource, value, aFunction))
                    return false;
                if (value < 0 || value >= RLIM_NLIMITS)
                {
                    error("ValueError", "invalid resource specified");
                    return false;
                }
                aResult = static_cast<int>(value);
                return true;
            }

            builtin_entry const sResourceFunctions[] =
            {
                { "getrlimit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int kind = 0;
                        if (!parse_arguments("getrlimit", aArgs, aKwargs, { "resource" }, 1u, 1u, a) || !resource_kind(a[0], kind, "getrlimit"))
                            return nullptr;
                        struct rlimit limit{};
                        if (::getrlimit(kind, &limit) != 0)
                            return raise_os_error(errno);
                        return rlimit_result(limit);
                    } },
                { "setrlimit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!host_unrestricted("resource.setrlimit"))
                            return nullptr;
                        std::vector<object*> a;
                        int kind = 0;
                        struct rlimit limit{};
                        if (!parse_arguments("setrlimit", aArgs, aKwargs, { "resource", "limits" }, 2u, 2u, a) || !resource_kind(a[0], kind, "setrlimit") ||
                            !rlimit_argument(a[1], limit))
                            return nullptr;
                        if (::setrlimit(kind, &limit) != 0)
                        {
                            if (errno == EINVAL)
                                return error("ValueError", "current limit exceeds maximum limit");
                            if (errno == EPERM)
                                return error("ValueError", "not allowed to raise maximum limit");
                            return raise_os_error(errno);
                        }
                        return new_reference(none());
                    } },
#if defined(__linux__)
                { "prlimit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long pid = 0;
                        int kind = 0;
                        if (!parse_arguments("prlimit", aArgs, aKwargs, { "pid", "resource", "limits" }, 2u, 3u, a) || !int_argument(a[0], pid, "prlimit") ||
                            !resource_kind(a[1], kind, "prlimit"))
                            return nullptr;
                        struct rlimit newLimit{}, oldLimit{};
                        bool const setting = a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE;
                        if (setting && (!host_unrestricted("resource.prlimit") || !rlimit_argument(a[2], newLimit)))
                            return nullptr;
                        if (::prlimit(static_cast<pid_t>(pid), static_cast<enum __rlimit_resource>(kind), setting ? &newLimit : nullptr, &oldLimit) != 0)
                        {
                            if (errno == EINVAL)
                                return error("ValueError", "current limit exceeds maximum limit");
                            return raise_os_error(errno);
                        }
                        return rlimit_result(oldLimit);
                    } },
#endif
                { "getrusage", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long who = 0;
                        if (!parse_arguments("getrusage", aArgs, aKwargs, { "who" }, 1u, 1u, a) || !int_argument(a[0], who, "getrusage"))
                            return nullptr;
                        struct rusage usage{};
                        if (::getrusage(static_cast<int>(who), &usage) != 0)
                        {
                            if (errno == EINVAL)
                                return error("ValueError", "invalid who parameter");
                            return raise_os_error(errno);
                        }
                        return rusage_result(usage);
                    } },
                { "getpagesize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "getpagesize") || !aArgs.empty())
                            return error("TypeError", "resource.getpagesize() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                        return make_int(static_cast<long long>(::sysconf(_SC_PAGESIZE)));
                    } },
            };

            bool resource_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sResourceFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                rusage_type();
                aAttributes.emplace_back("struct_rusage", type_object_for("struct_rusage", "resource.struct_rusage"));
                aAttributes.emplace_back("error", exception_class("OSError"));
                std::vector<std::pair<char const*, long long>> const constants = {
                    { "RLIMIT_CPU", RLIMIT_CPU }, { "RLIMIT_FSIZE", RLIMIT_FSIZE }, { "RLIMIT_DATA", RLIMIT_DATA }, { "RLIMIT_STACK", RLIMIT_STACK }, 
                    { "RLIMIT_CORE", RLIMIT_CORE }, { "RLIMIT_NOFILE", RLIMIT_NOFILE }, { "RLIMIT_AS", RLIMIT_AS },
#ifdef RLIMIT_RSS
                    { "RLIMIT_RSS", RLIMIT_RSS },
#endif
#ifdef RLIMIT_NPROC
                    { "RLIMIT_NPROC", RLIMIT_NPROC },
#endif
#ifdef RLIMIT_MEMLOCK
                    { "RLIMIT_MEMLOCK", RLIMIT_MEMLOCK },
#endif
#ifdef RLIMIT_OFILE
                    { "RLIMIT_OFILE", RLIMIT_OFILE },
#endif
#ifdef RLIMIT_MSGQUEUE
                    { "RLIMIT_MSGQUEUE", RLIMIT_MSGQUEUE },
#endif
#ifdef RLIMIT_NICE
                    { "RLIMIT_NICE", RLIMIT_NICE },
#endif
#ifdef RLIMIT_RTPRIO
                    { "RLIMIT_RTPRIO", RLIMIT_RTPRIO },
#endif
#ifdef RLIMIT_RTTIME
                    { "RLIMIT_RTTIME", RLIMIT_RTTIME },
#endif
#ifdef RLIMIT_SIGPENDING
                    { "RLIMIT_SIGPENDING", RLIMIT_SIGPENDING },
#endif
                    { "RUSAGE_SELF", RUSAGE_SELF }, { "RUSAGE_CHILDREN", RUSAGE_CHILDREN },
#ifdef RUSAGE_THREAD
                    { "RUSAGE_THREAD", RUSAGE_THREAD },
#endif
                };
                for (auto const& c : constants)
                    aAttributes.emplace_back(c.first, make_int(c.second));
                aAttributes.emplace_back("RLIM_INFINITY", make_int(-1));
                return true;
            }

            inline bool const sResource = register_module("resource", resource_module);
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
