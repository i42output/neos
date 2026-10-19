/*
  io.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_io/_iomodule.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// Files: open() and the io module (CPython 3.13's: text files (universal newlines; encodings: see bytes.cpp), binary
// files, StringIO and BytesIO), and the standard streams (sys.stdin, sys.stdout and sys.stderr)

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        std::nullptr_t raise_os_error(int aErrno, object* aFilename, object* aFilename2)
        {
            char const* type = "OSError";
            switch (aErrno)
            {
            case ENOENT: type = "FileNotFoundError"; break;
            case EEXIST: type = "FileExistsError"; break;
            case EACCES: case EPERM: type = "PermissionError"; break;
            case ENOTDIR: type = "NotADirectoryError"; break;
            case EISDIR: type = "IsADirectoryError"; break;
            case EINTR: type = "InterruptedError"; break;
            case EAGAIN: type = "BlockingIOError"; break;
            case ECHILD: type = "ChildProcessError"; break;
            case EPIPE: type = "BrokenPipeError"; break;
            case ECONNABORTED: type = "ConnectionAbortedError"; break;
            case ECONNREFUSED: type = "ConnectionRefusedError"; break;
            case ECONNRESET: type = "ConnectionResetError"; break;
            case ESRCH: type = "ProcessLookupError"; break;
            case ETIMEDOUT: type = "TimeoutError"; break;
            default: break;
            }
            // (an os.PathLike filename: its path)
            auto const filename = [](object* aName) -> object*
            {
                if (kind_of(aName) == NEOS_PY_OTHER && is_instance(aName))
                    if (ref const method{ special_method(aName, "__fspath__") })
                    {
                        ref const empty{ make_tuple() };
                        if (auto const path = call(method.get(), empty.get(), nullptr))
                            return path;
                        clear_error();
                    }
                retain(aName);
                return aName;
            };
            std::vector<object*> args{ make_int(aErrno), make_str(std::strerror(aErrno)) };
            if (aFilename != nullptr)
            {
                args.push_back(filename(aFilename));
                if (aFilename2 != nullptr)
                {
                    args.push_back((retain(none()), none()));
                    args.push_back(filename(aFilename2));
                }
            }
            ref const e{ make_exception(type, std::move(args)) };
            raise_exception(e.get());
            return nullptr;
        }

        namespace
        {
            // --- files

            struct file_object
            {
                header h;
                std::FILE* file = nullptr;
                object* name = nullptr;
                std::string mode;
                bool text = true;
                bool readable = false;
                bool writable = false;
                bool appending = false;
                bool closed = false;
                bool standard = false;          ///< (stdin, stdout or stderr: not closed)
                int stream = -1;                ///< (0, 1 or 2 for the standard streams)
                std::string encoding = "utf-8";
                std::string errors = "strict";
                object* newline = nullptr;      ///< (None: universal newlines; '' or a line ending)
                bool translated = true;         ///< (writes translate "\n" to the platform's line ending (Windows: "\r\n"))
                ~file_object()
                {
                    if (!closed && !standard && file != nullptr)
                        std::fclose(file);
                    release(name);
                    release(newline);
                }
            };

            neos_py_type make_file_type(char const* aQualifiedName, char const* aName);
            neos_py_type sTextFileType = make_file_type("_io.TextIOWrapper", "TextIOWrapper");
            neos_py_type sBufferedReaderType = make_file_type("_io.BufferedReader", "BufferedReader");
            neos_py_type sBufferedWriterType = make_file_type("_io.BufferedWriter", "BufferedWriter");
            neos_py_type sBufferedRandomType = make_file_type("_io.BufferedRandom", "BufferedRandom");

            file_object& as_file(object* aObject) { return *reinterpret_cast<file_object*>(aObject); }

            std::string newline_value(file_object const& f)
            {
                if (f.newline == nullptr || kind_of(f.newline) != NEOS_PY_STR)
                    return {};
                return as<str_object>(f.newline)->value;
            }

            bool universal(file_object const& f) { return f.newline == nullptr || kind_of(f.newline) == NEOS_PY_NONE; }

            bool check_open(file_object const& f)
            {
                if (f.closed)
                {
                    error("ValueError", "I/O operation on closed file.");
                    return false;
                }
                return true;
            }

            bool unsupported(char const* aMessage)
            {
                error("io.UnsupportedOperation", aMessage);
                return false;
            }

            // (the bytes up to and including the end of a line (universal newlines: \r\n and \r are \n); aLimit: bytes (-1: none))
            std::string read_line_bytes(file_object& f, long long aLimit)
            {
                std::string line;
                auto const ending = newline_value(f);
                for (;;)
                {
                    if (aLimit >= 0 && static_cast<long long>(line.size()) >= aLimit)
                        break;
                    int const c = std::fgetc(f.file);
                    if (c == EOF)
                        break;
                    if (f.text && universal(f) && c == '\r')
                    {
                        int const next = std::fgetc(f.file);
                        if (next != '\n' && next != EOF)
                            std::ungetc(next, f.file);
                        line += '\n';
                        break;
                    }
                    line += static_cast<char>(c);
                    if (c == '\n' && (!f.text || universal(f) || ending.empty() || ending == "\n" || (ending == "\r\n" && line.ends_with("\r\n"))))
                        break;
                    if (f.text && ending == "\r" && c == '\r')
                        break;
                }
                return line;
            }

            std::string read_all_bytes(file_object& f)
            {
                std::string data;
                char buffer[65536];
                for (;;)
                {
                    auto const n = std::fread(buffer, 1u, sizeof(buffer), f.file);
                    data.append(buffer, n);
                    if (n < sizeof(buffer))
                        break;
                }
                if (f.text && universal(f))
                {
                    std::string translated;
                    translated.reserve(data.size());
                    for (std::size_t i = 0u; i < data.size(); ++i)
                    {
                        if (data[i] == '\r')
                        {
                            translated += '\n';
                            if (i + 1u < data.size() && data[i + 1u] == '\n')
                                ++i;
                        }
                        else
                            translated += data[i];
                    }
                    return translated;
                }
                return data;
            }

            object* text_result(file_object& f, std::string aData)
            {
                if (!f.text)
                    return make_bytes(std::move(aData));
                std::string text;
                if (!decode(aData, f.encoding, f.errors, text))
                    return nullptr;
                return make_str(std::move(text));
            }

            // (the code points of UTF-8 text: its first aCount (a prefix's length in bytes))
            std::size_t utf8_prefix(std::string_view aText, std::size_t aCount)
            {
                std::size_t i = 0u;
                while (aCount > 0u && i < aText.size())
                {
                    auto const c = static_cast<unsigned char>(aText[i]);
                    i += c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                    --aCount;
                }
                return std::min(i, aText.size());
            }

            object* file_read(file_object& f, long long aSize)
            {
                if (!check_open(f))
                    return nullptr;
                if (!f.readable)
                    return (unsupported("not readable"), nullptr);
                if (aSize < 0)
                    return text_result(f, read_all_bytes(f));
                if (!f.text)
                {
                    std::string data(static_cast<std::size_t>(aSize), '\0');
                    auto const n = std::fread(data.data(), 1u, data.size(), f.file);
                    data.resize(n);
                    return make_bytes(std::move(data));
                }
                // (aSize characters: lines until there are enough)
                std::string data;
                std::size_t characters = 0u;
                while (characters < static_cast<std::size_t>(aSize))
                {
                    int const c = std::fgetc(f.file);
                    if (c == EOF)
                        break;
                    if (universal(f) && c == '\r')
                    {
                        int const next = std::fgetc(f.file);
                        if (next != '\n' && next != EOF)
                            std::ungetc(next, f.file);
                        data += '\n';
                    }
                    else
                    {
                        data += static_cast<char>(c);
                        // (a UTF-8 character's continuation bytes)
                        auto const lead = static_cast<unsigned char>(c);
                        std::size_t const more = lead < 0x80u ? 0u : lead < 0xE0u ? 1u : lead < 0xF0u ? 2u : 3u;
                        for (std::size_t k = 0u; k < more && f.encoding == "utf-8"; ++k)
                        {
                            int const b = std::fgetc(f.file);
                            if (b == EOF)
                                break;
                            data += static_cast<char>(b);
                        }
                    }
                    ++characters;
                }
                return text_result(f, std::move(data));
            }

            object* file_readline(file_object& f, long long aSize)
            {
                if (!check_open(f))
                    return nullptr;
                if (!f.readable)
                    return (unsupported("not readable"), nullptr);
                return text_result(f, read_line_bytes(f, aSize));
            }

            object* file_write(file_object& f, object* aData)
            {
                if (!check_open(f))
                    return nullptr;
                if (!f.writable)
                    return (unsupported("not writable"), nullptr);
                std::string data;
                long long count = 0;
                if (f.text)
                {
                    if (kind_of(aData) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "write() argument must be str, not " } + type_name(aData));
                    auto const& text = as<str_object>(aData)->value;
                    for (std::size_t i = 0u; i < text.size(); ++i)
                        if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u)
                            ++count;
                    std::string translated;
                    auto const ending = newline_value(f);
#if defined(_WIN32)
                    std::string const platform = "\r\n";
#else
                    std::string const platform = "\n";
#endif
                    std::string const lineEnding = universal(f) ? platform : ending.empty() ? std::string{ "\n" } : ending;
                    if (lineEnding != "\n" && !f.standard)
                    {
                        for (auto c : text)
                            if (c == '\n')
                                translated += lineEnding;
                            else
                                translated += c;
                    }
                    else
                        translated = text;
                    if (!encode(translated, f.encoding, f.errors, data))
                        return nullptr;
                }
                else
                {
                    std::string_view bytes;
                    if (!bytes_like(aData, bytes))
                        return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aData) + "'");
                    data = std::string{ bytes };
                    count = static_cast<long long>(data.size());
                }
                if (f.stream == 1)
                    write_out(data);
                else
                {
                    if (f.appending)
                        std::fseek(f.file, 0, SEEK_END);
                    if (std::fwrite(data.data(), 1u, data.size(), f.file) != data.size())
                        return raise_os_error(errno);
                    if (f.stream == 2)
                        std::fflush(f.file);
                }
                return make_int(count);
            }

            object* file_close(file_object& f)
            {
                if (f.closed)
                    return none();
                if (!f.standard && f.file != nullptr)
                    std::fclose(f.file);
                else if (f.file != nullptr)
                    std::fflush(f.file);
                f.closed = !f.standard;
                return none();
            }

            object* file_lines(file_object& f)
            {
                std::vector<object*> lines;
                for (;;)
                {
                    auto const line = file_readline(f, -1);
                    if (line == nullptr)
                    {
                        for (auto l : lines)
                            release(l);
                        return nullptr;
                    }
                    if (length(line) == 0)
                    {
                        release(line);
                        break;
                    }
                    lines.push_back(line);
                }
                return make_list(std::move(lines));
            }

            long long size_argument(object* aValue)
            {
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                    return -1;
                long long n = -1;
                if (!to_index(aValue, n))
                    return -2;
                return n;
            }

            struct file_method
            {
                char const* name;
                object* (*function)(file_object& f, object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            file_method const sFileMethods[] =
            {
                { "read", [](file_object& f, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("read", aArgs, aKwargs, { "size" }, 0u, 1u, a))
                            return nullptr;
                        auto const n = size_argument(a[0]);
                        return n == -2 ? nullptr : file_read(f, n);
                    } },
                { "readline", [](file_object& f, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("readline", aArgs, aKwargs, { "size" }, 0u, 1u, a))
                            return nullptr;
                        auto const n = size_argument(a[0]);
                        return n == -2 ? nullptr : file_readline(f, n);
                    } },
                { "readlines", [](file_object& f, object*, std::vector<object*> const&, object*) -> object*
                    {
                        if (!check_open(f))
                            return nullptr;
                        return file_lines(f);
                    } },
                { "write", [](file_object& f, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("write", aArgs, aKwargs, { "data" }, 1u, 1u, a))
                            return nullptr;
                        return file_write(f, a[0]);
                    } },
                { "writelines", [](file_object& f, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("writelines", aArgs, aKwargs, { "lines" }, 1u, 1u, a))
                            return nullptr;
                        ref const it{ iter(a[0]) };
                        if (!it)
                            return nullptr;
                        for (;;)
                        {
                            ref const line{ next(it.get()) };
                            if (!line)
                                return failed() ? nullptr : none();
                            ref const r{ file_write(f, line.get()) };
                            if (!r)
                                return nullptr;
                        }
                    } },
                { "close", [](file_object& f, object*, std::vector<object*> const&, object*) -> object* { return file_close(f); } },
                { "flush", [](file_object& f, object*, std::vector<object*> const&, object*) -> object*
                    {
                        if (!check_open(f))
                            return nullptr;
                        if (f.file != nullptr)
                            std::fflush(f.file);
                        return none();
                    } },
                { "seek", [](file_object& f, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("seek", aArgs, aKwargs, { "cookie", "whence" }, 1u, 2u, a))
                            return nullptr;
                        long long offset = 0, whence = 0;
                        if (!to_index(a[0], offset) || (a[1] != nullptr && !to_index(a[1], whence)))
                            return nullptr;
                        if (!check_open(f))
                            return nullptr;
                        if (f.text && whence != 0 && offset != 0)
                            return (unsupported(whence == 1 ? "can't do nonzero cur-relative seeks" : "can't do nonzero end-relative seeks"), nullptr);
                        if (whence < 0 || whence > 2)
                            return error("ValueError", "invalid whence (" + std::to_string(whence) + ", should be 0, 1 or 2)");
                        if (f.standard || std::fseek(f.file, static_cast<long>(offset), whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END) != 0)
                            return f.standard ? (unsupported("underlying stream is not seekable"), nullptr) : raise_os_error(errno);
                        return make_int(static_cast<long long>(std::ftell(f.file)));
                    } },
                { "tell", [](file_object& f, object*, std::vector<object*> const&, object*) -> object*
                    {
                        if (!check_open(f))
                            return nullptr;
                        if (f.standard)
                            return (unsupported("underlying stream is not seekable"), nullptr);
                        return make_int(static_cast<long long>(std::ftell(f.file)));
                    } },
                { "truncate", [](file_object& f, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("truncate", aArgs, aKwargs, { "pos" }, 0u, 1u, a))
                            return nullptr;
                        if (!check_open(f))
                            return nullptr;
                        if (!f.writable)
                            return (unsupported("File not open for writing"), nullptr);
                        std::fflush(f.file);
                        long long size = static_cast<long long>(std::ftell(f.file));
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE && !to_index(a[0], size))
                            return nullptr;
#if defined(_WIN32)
                        if (::_chsize_s(::_fileno(f.file), size) != 0)
#else
                        if (::ftruncate(::fileno(f.file), static_cast<off_t>(size)) != 0)
#endif
                            return raise_os_error(errno);
                        return make_int(size);
                    } },
                { "fileno", [](file_object& f, object*, std::vector<object*> const&, object*) -> object*
                    {
                        if (!check_open(f))
                            return nullptr;
#if defined(_WIN32)
                        return make_int(::_fileno(f.file));
#else
                        return make_int(::fileno(f.file));
#endif
                    } },
                { "isatty", [](file_object& f, object*, std::vector<object*> const&, object*) -> object*
                    {
                        if (!check_open(f))
                            return nullptr;
#if defined(_WIN32)
                        return make_bool(::_isatty(::_fileno(f.file)) != 0);
#else
                        return make_bool(::isatty(::fileno(f.file)) != 0);
#endif
                    } },
                { "readable", [](file_object& f, object*, std::vector<object*> const&, object*) -> object* { return check_open(f) ? make_bool(f.readable) : nullptr; } },
                { "writable", [](file_object& f, object*, std::vector<object*> const&, object*) -> object* { return check_open(f) ? make_bool(f.writable) : nullptr; } },
                { "seekable", [](file_object& f, object*, std::vector<object*> const&, object*) -> object* { return check_open(f) ? make_bool(!f.standard) : nullptr; } },
                { "__enter__", [](file_object& f, object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        if (!check_open(f))
                            return nullptr;
                        retain(aSelf);
                        return aSelf;
                    } },
                { "__exit__", [](file_object& f, object*, std::vector<object*> const&, object*) -> object* { return file_close(f); } },
                { "__iter__", [](file_object& f, object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        if (!check_open(f))
                            return nullptr;
                        retain(aSelf);
                        return aSelf;
                    } },
                { "__next__", [](file_object& f, object*, std::vector<object*> const&, object*) -> object*
                    {
                        ref line{ file_readline(f, -1) };
                        if (!line)
                            return nullptr;
                        if (length(line.get()) == 0)
                            return error("StopIteration", "");
                        return line.release_ownership();
                    } }
            };

            object* call_file_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<file_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                return method.function(as_file(items[0]), items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            neos_py_type make_file_type(char const* aQualifiedName, char const* aName)
            {
                auto type = make_type(aQualifiedName, aName, retain_object, release_object<file_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = as_file(aSelf);
                    std::string name;
                    if (!repr_of(f.name, name))
                        return nullptr;
                    std::string const typeName = aSelf->type->name;
                    if (f.text)
                        return make_str("<" + typeName + " name=" + name + " mode='" + f.mode + "' encoding='" + (f.encoding == "utf-8" ? std::string{ "UTF-8" } : f.encoding) + "'>");
                    return make_str("<" + typeName + " name=" + name + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& f = as_file(aSelf);
                    for (auto const& m : sFileMethods)
                        if (std::strcmp(m.name, aName) == 0)
                            return make_function(m.name, call_file_method, const_cast<file_method*>(&m), aSelf);
                    if (std::strcmp(aName, "name") == 0)
                        return (retain(f.name), f.name);
                    if (std::strcmp(aName, "mode") == 0)
                        return make_str(f.mode);
                    if (std::strcmp(aName, "closed") == 0)
                        return make_bool(f.closed);
                    if (f.text && std::strcmp(aName, "encoding") == 0)
                        return make_str(f.encoding);
                    if (f.text && std::strcmp(aName, "errors") == 0)
                        return make_str(f.errors);
                    if (f.text && std::strcmp(aName, "newlines") == 0)
                        return (retain(none()), none());
                    if (f.text && std::strcmp(aName, "line_buffering") == 0)
                        return make_bool(f.standard);
                    return error("AttributeError", std::string{ "'" } + aSelf->type->name + "' object has no attribute '" + aName + "'");
                };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    if (!check_open(as_file(aSelf)))
                        return nullptr;
                    retain(aSelf);
                    return aSelf;
                };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    ref line{ file_readline(as_file(aSelf), -1) };
                    if (!line)
                        return nullptr;
                    if (length(line.get()) == 0)
                        return nullptr;
                    return line.release_ownership();
                };
                return type;
            }

            object* standard_streams[3] = {};

            // --- StringIO and BytesIO

            struct memory_file_object
            {
                header h;
                bool text = true;
                std::u32string characters;      ///< (StringIO's)
                std::string bytes;              ///< (BytesIO's)
                std::size_t position = 0u;
                bool closed = false;
            };

            neos_py_type make_memory_file_type(char const* aQualifiedName, char const* aName);
            neos_py_type sStringIOType = make_memory_file_type("_io.StringIO", "StringIO");
            neos_py_type sBytesIOType = make_memory_file_type("_io.BytesIO", "BytesIO");

            memory_file_object& as_memory(object* aObject) { return *reinterpret_cast<memory_file_object*>(aObject); }

            std::u32string to_u32(std::string_view aText)
            {
                std::u32string result;
                for (std::size_t i = 0u; i < aText.size();)
                {
                    auto const c = static_cast<unsigned char>(aText[i]);
                    std::size_t const n = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                    char32_t cp = n == 1u ? c : n == 2u ? (c & 0x1Fu) : n == 3u ? (c & 0x0Fu) : (c & 0x07u);
                    for (std::size_t j = 1u; j < n && i + j < aText.size(); ++j)
                        cp = (cp << 6) | (static_cast<unsigned char>(aText[i + j]) & 0x3Fu);
                    result += cp;
                    i += n;
                }
                return result;
            }

            std::string to_utf8(std::u32string_view aText)
            {
                std::string result;
                for (auto cp : aText)
                {
                    if (cp < 0x80u)
                        result += static_cast<char>(cp);
                    else if (cp < 0x800u)
                    {
                        result += static_cast<char>(0xC0u | (cp >> 6));
                        result += static_cast<char>(0x80u | (cp & 0x3Fu));
                    }
                    else if (cp < 0x10000u)
                    {
                        result += static_cast<char>(0xE0u | (cp >> 12));
                        result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                        result += static_cast<char>(0x80u | (cp & 0x3Fu));
                    }
                    else
                    {
                        result += static_cast<char>(0xF0u | (cp >> 18));
                        result += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
                        result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                        result += static_cast<char>(0x80u | (cp & 0x3Fu));
                    }
                }
                return result;
            }

            std::size_t memory_size(memory_file_object const& m) { return m.text ? m.characters.size() : m.bytes.size(); }

            object* memory_slice(memory_file_object& m, std::size_t aBegin, std::size_t aEnd)
            {
                aEnd = std::min(aEnd, memory_size(m));
                aBegin = std::min(aBegin, aEnd);
                if (m.text)
                    return make_str(to_utf8(std::u32string_view{ m.characters }.substr(aBegin, aEnd - aBegin)));
                return make_bytes(m.bytes.substr(aBegin, aEnd - aBegin));
            }

            bool memory_open(memory_file_object const& m)
            {
                if (m.closed)
                {
                    error("ValueError", "I/O operation on closed file.");
                    return false;
                }
                return true;
            }

            object* memory_readline(memory_file_object& m, long long aLimit)
            {
                if (!memory_open(m))
                    return nullptr;
                auto const size = memory_size(m);
                auto end = m.position;
                while (end < size)
                {
                    auto const c = m.text ? m.characters[end] : static_cast<char32_t>(static_cast<unsigned char>(m.bytes[end]));
                    ++end;
                    if (c == U'\n')
                        break;
                }
                if (aLimit >= 0)
                    end = std::min(end, m.position + static_cast<std::size_t>(aLimit));
                auto const result = memory_slice(m, m.position, end);
                m.position = std::max(m.position, std::min(end, size));
                return result;
            }

            object* memory_write(memory_file_object& m, object* aData)
            {
                if (!memory_open(m))
                    return nullptr;
                if (m.text)
                {
                    if (kind_of(aData) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "string argument expected, got '" } + type_name(aData) + "'");
                    auto const text = to_u32(as<str_object>(aData)->value);
                    if (m.position > m.characters.size())
                        m.characters.resize(m.position, U'\0');
                    m.characters.replace(m.position, std::min(text.size(), m.characters.size() - m.position), text);
                    m.position += text.size();
                    return make_int(static_cast<long long>(text.size()));
                }
                std::string_view data;
                if (!bytes_like(aData, data))
                    return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aData) + "'");
                if (m.position > m.bytes.size())
                    m.bytes.resize(m.position, '\0');
                m.bytes.replace(m.position, std::min(data.size(), m.bytes.size() - m.position), data);
                m.position += data.size();
                return make_int(static_cast<long long>(data.size()));
            }

            struct memory_method
            {
                char const* name;
                object* (*function)(memory_file_object& m, object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            memory_method const sMemoryMethods[] =
            {
                { "getvalue", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object*
                    {
                        if (!memory_open(m))
                            return nullptr;
                        return memory_slice(m, 0u, memory_size(m));
                    } },
                { "read", [](memory_file_object& m, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("read", aArgs, aKwargs, { "size" }, 0u, 1u, a))
                            return nullptr;
                        auto const n = size_argument(a[0]);
                        if (n == -2 || !memory_open(m))
                            return nullptr;
                        auto const size = memory_size(m);
                        auto const end = n < 0 ? size : std::min(size, m.position + static_cast<std::size_t>(n));
                        auto const result = memory_slice(m, m.position, end);
                        m.position = std::max(m.position, end);
                        return result;
                    } },
                { "readline", [](memory_file_object& m, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("readline", aArgs, aKwargs, { "size" }, 0u, 1u, a))
                            return nullptr;
                        auto const n = size_argument(a[0]);
                        return n == -2 ? nullptr : memory_readline(m, n);
                    } },
                { "readlines", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object*
                    {
                        std::vector<object*> lines;
                        for (;;)
                        {
                            auto const line = memory_readline(m, -1);
                            if (line == nullptr)
                            {
                                for (auto l : lines)
                                    release(l);
                                return nullptr;
                            }
                            if (length(line) == 0)
                            {
                                release(line);
                                return make_list(std::move(lines));
                            }
                            lines.push_back(line);
                        }
                    } },
                { "write", [](memory_file_object& m, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("write", aArgs, aKwargs, { "s" }, 1u, 1u, a))
                            return nullptr;
                        return memory_write(m, a[0]);
                    } },
                { "writelines", [](memory_file_object& m, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("writelines", aArgs, aKwargs, { "lines" }, 1u, 1u, a))
                            return nullptr;
                        ref const it{ iter(a[0]) };
                        if (!it)
                            return nullptr;
                        for (;;)
                        {
                            ref const line{ next(it.get()) };
                            if (!line)
                                return failed() ? nullptr : none();
                            ref const r{ memory_write(m, line.get()) };
                            if (!r)
                                return nullptr;
                        }
                    } },
                { "seek", [](memory_file_object& m, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("seek", aArgs, aKwargs, { "pos", "whence" }, 1u, 2u, a))
                            return nullptr;
                        long long offset = 0, whence = 0;
                        if (!to_index(a[0], offset) || (a[1] != nullptr && !to_index(a[1], whence)) || !memory_open(m))
                            return nullptr;
                        if (whence == 0 && offset < 0)
                            return error("ValueError", "Negative seek value " + std::to_string(offset));
                        if (m.text && whence != 0 && offset != 0)
                            return error("OSError", whence == 1 ? "Can't do nonzero cur-relative seeks" : "Can't do nonzero end-relative seeks");
                        auto const base = whence == 0 ? 0ll : whence == 1 ? static_cast<long long>(m.position) : static_cast<long long>(memory_size(m));
                        m.position = static_cast<std::size_t>(std::max(0ll, base + offset));
                        return make_int(static_cast<long long>(m.position));
                    } },
                { "tell", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object*
                    {
                        return memory_open(m) ? make_int(static_cast<long long>(m.position)) : nullptr;
                    } },
                { "truncate", [](memory_file_object& m, object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("truncate", aArgs, aKwargs, { "pos" }, 0u, 1u, a))
                            return nullptr;
                        long long size = static_cast<long long>(m.position);
                        if ((a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE && !to_index(a[0], size)) || !memory_open(m))
                            return nullptr;
                        if (m.text)
                            m.characters.resize(std::min(m.characters.size(), static_cast<std::size_t>(size)));
                        else
                            m.bytes.resize(std::min(m.bytes.size(), static_cast<std::size_t>(size)));
                        return make_int(size);
                    } },
                { "close", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { m.closed = true; return none(); } },
                { "flush", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { return memory_open(m) ? none() : nullptr; } },
                { "readable", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { return memory_open(m) ? make_bool(true) : nullptr; } },
                { "writable", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { return memory_open(m) ? make_bool(true) : nullptr; } },
                { "seekable", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { return memory_open(m) ? make_bool(true) : nullptr; } },
                { "isatty", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { return memory_open(m) ? make_bool(false) : nullptr; } },
                { "__enter__", [](memory_file_object& m, object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        if (!memory_open(m))
                            return nullptr;
                        retain(aSelf);
                        return aSelf;
                    } },
                { "__exit__", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { m.closed = true; return none(); } },
                { "getbuffer", [](memory_file_object& m, object*, std::vector<object*> const&, object*) -> object* { return make_bytearray(m.bytes); } }
            };

            object* call_memory_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<memory_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                return method.function(as_memory(items[0]), items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            neos_py_type make_memory_file_type(char const* aQualifiedName, char const* aName)
            {
                auto type = make_type(aQualifiedName, aName, retain_object, release_object<memory_file_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    return make_str(std::string{ "<" } + aSelf->type->name + " object at " + object_address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& m = as_memory(aSelf);
                    for (auto const& method : sMemoryMethods)
                        if (std::strcmp(method.name, aName) == 0 && (m.text ? std::strcmp(aName, "getbuffer") != 0 : true))
                            return make_function(method.name, call_memory_method, const_cast<memory_method*>(&method), aSelf);
                    if (std::strcmp(aName, "closed") == 0)
                        return make_bool(m.closed);
                    if (m.text && std::strcmp(aName, "newlines") == 0)
                        return (retain(none()), none());
                    if (m.text && std::strcmp(aName, "encoding") == 0)
                        return (retain(none()), none());
                    return error("AttributeError", std::string{ "'_io." } + aSelf->type->name + "' object has no attribute '" + aName + "'");
                };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    if (!memory_open(as_memory(aSelf)))
                        return nullptr;
                    retain(aSelf);
                    return aSelf;
                };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    ref line{ memory_readline(as_memory(aSelf), -1) };
                    if (!line || length(line.get()) == 0)
                        return nullptr;
                    return line.release_ownership();
                };
                return type;
            }

            object* new_memory_file(bool aText, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (aText ? !parse_arguments("StringIO", aArgs, aKwargs, { "initial_value", "newline" }, 0u, 0u, a) :
                    !parse_arguments("BytesIO", aArgs, aKwargs, { "initial_bytes" }, 0u, 0u, a))
                    return nullptr;
                auto const m = new memory_file_object{ { { aText ? &sStringIOType.base : &sBytesIOType.base } } };
                ref const result{ &m->h.base };
                m->text = aText;
                if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                {
                    if (aText)
                    {
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "initial_value must be str or None, not " } + type_name(a[0]));
                        m->characters = to_u32(as<str_object>(a[0])->value);
                    }
                    else
                    {
                        std::string_view data;
                        if (!bytes_like(a[0], data))
                            return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[0]) + "'");
                        m->bytes = std::string{ data };
                    }
                }
                retain(result.get());
                return result.get();
            }

            // --- open()

            object* open_file(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("open", aArgs, aKwargs, { "file", "mode", "buffering", "encoding", "errors", "newline", "closefd", "opener" }, 1u, 0u, a))
                    return nullptr;
                std::string path;
                std::string_view bytesPath;
                long long descriptor = -1;
                if (is_int_like(a[0]))
                {
                    // (a file descriptor)
                    if (!to_index(a[0], descriptor))
                        return nullptr;
                    if (descriptor < 0)
                        return error("ValueError", "negative file descriptor");
                }
                else if (kind_of(a[0]) == NEOS_PY_STR)
                    path = as<str_object>(a[0])->value;
                else if (bytes_like(a[0], bytesPath))
                    path = std::string{ bytesPath };
                else
                {
                    ref const fspath{ getattr(a[0], "__fspath__") };
                    if (!fspath)
                    {
                        clear_error();
                        return error("TypeError", std::string{ "expected str, bytes or os.PathLike object, not " } + type_name(a[0]));
                    }
                    ref const empty{ make_tuple() };
                    ref const p{ call(fspath.get(), empty.get(), nullptr) };
                    if (!p || !str_of(p.get(), path))
                        return nullptr;
                }
                std::string mode = "r";
                if (a[1] != nullptr)
                {
                    if (kind_of(a[1]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "open() argument 'mode' must be str, not " } + type_name(a[1]));
                    mode = as<str_object>(a[1])->value;
                }
                // (the mode: one of r, w, x and a, with b or t and +)
                int creating = 0, reading = 0, writing = 0, appending = 0, updating = 0, text = 0, binary = 0;
                for (auto c : mode)
                {
                    int* flag = nullptr;
                    switch (c)
                    {
                    case 'x': flag = &creating; break;
                    case 'r': flag = &reading; break;
                    case 'w': flag = &writing; break;
                    case 'a': flag = &appending; break;
                    case '+': flag = &updating; break;
                    case 't': flag = &text; break;
                    case 'b': flag = &binary; break;
                    default: return error("ValueError", "invalid mode: '" + mode + "'");
                    }
                    if ((*flag)++ != 0)
                        return error("ValueError", "invalid mode: '" + mode + "'");
                }
                if (text != 0 && binary != 0)
                    return error("ValueError", "can't have text and binary mode at once");
                if (creating + reading + writing + appending != 1)
                    return error("ValueError", "must have exactly one of create/read/write/append mode");
                bool const isBinary = binary != 0;
                for (std::size_t i = 3u; i < 6u; ++i)
                    if (isBinary && a[i] != nullptr && kind_of(a[i]) != NEOS_PY_NONE)
                        return error("ValueError", std::string{ "binary mode doesn't take an " } + (i == 3u ? "encoding" : i == 4u ? "errors" : "newline") + " argument");
                std::string encoding = "utf-8", errorsMode = "strict";
                if (a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE)
                {
                    if (kind_of(a[3]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "open() argument 'encoding' must be str or None, not " } + type_name(a[3]));
                    encoding = as<str_object>(a[3])->value;
                    std::string probe;
                    if (!encode("", encoding, "strict", probe))
                        return nullptr;
                    for (auto& c : encoding)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (encoding == "utf8" || encoding == "utf_8")
                        encoding = "utf-8";
                }
                if (a[4] != nullptr && kind_of(a[4]) != NEOS_PY_NONE)
                {
                    if (kind_of(a[4]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "open() argument 'errors' must be str or None, not " } + type_name(a[4]));
                    errorsMode = as<str_object>(a[4])->value;
                }
                if (a[5] != nullptr && kind_of(a[5]) != NEOS_PY_NONE)
                {
                    if (kind_of(a[5]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "open() argument 'newline' must be str or None, not " } + type_name(a[5]));
                    auto const& n = as<str_object>(a[5])->value;
                    if (n != "" && n != "\n" && n != "\r" && n != "\r\n")
                        return error("ValueError", "illegal newline value: " + n);
                }
                // (a file opened to write (or created): restricted (a descriptor's is its opener's))
                if (descriptor < 0 && (writing != 0 || creating != 0 || appending != 0 || updating != 0) && !host_unrestricted("open"))
                    return nullptr;
                // (a directory)
#if defined(_WIN32)
                struct _stat64 st{};
                bool const directory = descriptor >= 0 ? ::_fstat64(static_cast<int>(descriptor), &st) == 0 && (st.st_mode & _S_IFDIR) != 0 :
                    ::_stat64(path.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
                struct stat st{};
                bool const directory = descriptor >= 0 ? ::fstat(static_cast<int>(descriptor), &st) == 0 && S_ISDIR(st.st_mode) :
                    ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
                if (directory)
                    return raise_os_error(EISDIR, a[0]);
                if (creating != 0 && descriptor < 0)
                {
                    if (std::FILE* existing = std::fopen(path.c_str(), "rb"))
                    {
                        std::fclose(existing);
                        return raise_os_error(EEXIST, a[0]);
                    }
                }
                std::string const cmode = reading != 0 ? (updating != 0 ? "r+b" : "rb") : writing != 0 || creating != 0 ? (updating != 0 ? "w+b" : "wb") :
                    (updating != 0 ? "a+b" : "ab");
                errno = 0;
                std::FILE* file = nullptr;
                if (descriptor >= 0)
                {
                    // (closefd=False: the descriptor stays open (the file has a duplicate of it))
                    bool const closefd = a[6] == nullptr || truth(a[6]) == 1;
                    // (fdopen's mode: "w" does not truncate an open file)
                    char const* const fdmode = reading != 0 && updating == 0 ? "rb" : appending != 0 ? (updating != 0 ? "a+b" : "ab") : 
                        updating != 0 ? "r+b" : "wb";
#if defined(_WIN32)
                    int const fd = closefd ? static_cast<int>(descriptor) : ::_dup(static_cast<int>(descriptor));
                    file = fd < 0 ? nullptr : ::_fdopen(fd, fdmode);
#else
                    int const fd = closefd ? static_cast<int>(descriptor) : ::dup(static_cast<int>(descriptor));
                    file = fd < 0 ? nullptr : ::fdopen(fd, fdmode);
#endif
                    if (file == nullptr)
                        return raise_os_error(errno != 0 ? errno : EBADF);
                }
                else
                    file = std::fopen(path.c_str(), cmode.c_str());
                if (file == nullptr)
                    return raise_os_error(errno != 0 ? errno : ENOENT, a[0]);
                auto* const type = !isBinary ? &sTextFileType : updating != 0 ? &sBufferedRandomType : reading != 0 ? &sBufferedReaderType : &sBufferedWriterType;
                auto const f = new file_object{ { { &type->base } } };
                f->file = file;
                retain(a[0]);
                f->name = a[0];
                f->mode = mode;
                f->text = !isBinary;
                f->readable = reading != 0 || updating != 0;
                f->writable = reading == 0 || updating != 0;
                f->appending = appending != 0;
                f->encoding = encoding;
                f->errors = errorsMode;
                if (a[5] != nullptr)
                {
                    retain(a[5]);
                    f->newline = a[5];
                }
                if (isBinary && mode.find('b') != std::string::npos)
                {
                    // (a binary file's mode: as CPython shows it)
                    f->mode = reading != 0 ? (updating != 0 ? "rb+" : "rb") : writing != 0 ? (updating != 0 ? "rb+" : "wb") : creating != 0 ? "xb" : (updating != 0 ? "ab+" : "ab");
                }
                return &f->h.base;
            }

            builtin_entry const sIOFunctions[] =
            {
                { "open", open_file },
                { "StringIO", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return new_memory_file(true, aArgs, aKwargs); } },
                { "BytesIO", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return new_memory_file(false, aArgs, aKwargs); } },
                { "text_encoding", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!aArgs.empty() && kind_of(aArgs[0]) != NEOS_PY_NONE)
                            return (retain(aArgs[0]), aArgs[0]);
                        return make_str("utf-8");
                    } }
            };

            bool io_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sIOFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("SEEK_SET", make_int(0));
                aAttributes.emplace_back("SEEK_CUR", make_int(1));
                aAttributes.emplace_back("SEEK_END", make_int(2));
                aAttributes.emplace_back("DEFAULT_BUFFER_SIZE", make_int(8192));
                if (!is_exception_class("io.UnsupportedOperation"))
                    user_exception_bases()["io.UnsupportedOperation"] = { "OSError", "ValueError" };
                aAttributes.emplace_back("UnsupportedOperation", exception_class("io.UnsupportedOperation"));
                aAttributes.emplace_back("TextIOWrapper", type_object_for("TextIOWrapper", "_io.TextIOWrapper"));
                aAttributes.emplace_back("BufferedReader", type_object_for("BufferedReader", "_io.BufferedReader"));
                aAttributes.emplace_back("BufferedWriter", type_object_for("BufferedWriter", "_io.BufferedWriter"));
                return true;
            }

            inline bool const sIO = register_module("io", io_module);
        }

        object* b_open(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!is_exception_class("io.UnsupportedOperation"))
                user_exception_bases()["io.UnsupportedOperation"] = { "OSError", "ValueError" };
            return open_file(aArgs, aKwargs);
        }

        object* standard_stream(int aStream)
        {
            if (standard_streams[aStream] == nullptr)
            {
                if (!is_exception_class("io.UnsupportedOperation"))
                    user_exception_bases()["io.UnsupportedOperation"] = { "OSError", "ValueError" };
                auto const f = new file_object{ { { &sTextFileType.base } } };
                f->file = aStream == 0 ? stdin : aStream == 1 ? stdout : stderr;
                f->name = make_str(aStream == 0 ? "<stdin>" : aStream == 1 ? "<stdout>" : "<stderr>");
                f->mode = aStream == 0 ? "r" : "w";
                f->readable = aStream == 0;
                f->writable = aStream != 0;
                f->standard = true;
                f->stream = aStream;
                f->errors = aStream == 2 ? "backslashreplace" : "strict";
                standard_streams[aStream] = &f->h.base;
            }
            retain(standard_streams[aStream]);
            return standard_streams[aStream];
        }

        bool is_standard_output(object* aFile)
        {
            return aFile != nullptr && aFile == standard_streams[1];
        }

        object* sys_stdout()
        {
            if (!modules_imported("sys"))
                return nullptr;
            ref const sys{ fn_import("sys") };
            if (!sys)
                return nullptr;
            auto const result = getattr(sys.get(), "stdout");
            if (result == nullptr)
                clear_error();
            return result;
        }
    }
}
