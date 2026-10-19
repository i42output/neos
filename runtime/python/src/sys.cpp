/*
  sys.cpp

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

  Portions of this file are derived from CPython 3.13 (Python/stdlib_module_names.h), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: its list of the standard library's module names reproduced as a C++ table for
  the neos Python runtime plugin (see runtime/python/NOTICE).
*/

// The sys module (CPython 3.13's: its attributes other than those of objects.cpp's sys)

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            long long sRecursionLimit = 1000;
        }

        long long recursion_limit()
        {
            return sRecursionLimit;
        }

        namespace
        {
            double sSwitchInterval = 0.005;

            std::string executable()
            {
#if defined(_WIN32)
                char buffer[MAX_PATH];
                auto const n = ::GetModuleFileNameA(nullptr, buffer, MAX_PATH);
                return std::string{ buffer, n };
#else
                std::error_code ec;
                auto const path = std::filesystem::read_symlink("/proc/self/exe", ec);
                return ec ? std::string{} : path.string();
#endif
            }

            // (sys.getsizeof: CPython's sizes (64-bit) for the core objects)
            long long size_of(object* aObject)
            {
                switch (kind_of(aObject))
                {
                case NEOS_PY_NONE: return 16;
                case NEOS_PY_BOOL: return 28;
                case NEOS_PY_INT:
                    {
                        long long bits = 0;
                        ref const b{ call_method_named(aObject, "bit_length", {}) };
                        if (b)
                            to_index(b.get(), bits);
                        auto const n = std::max(1ll, (bits + 29) / 30);
                        return 24 + 4 * n;
                    }
                case NEOS_PY_FLOAT: return 24;
                case NEOS_PY_COMPLEX: return 32;
                case NEOS_PY_STR:
                    {
                        auto const& text = as<str_object>(aObject)->value;
                        long long n = 0;
                        unsigned largest = 0;
                        for (std::size_t i = 0u; i < text.size();)
                        {
                            auto const c = static_cast<unsigned char>(text[i]);
                            std::size_t const k = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                            unsigned cp = k == 1u ? c : k == 2u ? (c & 0x1Fu) : k == 3u ? (c & 0x0Fu) : (c & 0x07u);
                            for (std::size_t j = 1u; j < k && i + j < text.size(); ++j)
                                cp = (cp << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3Fu);
                            largest = std::max(largest, cp);
                            ++n;
                            i += k;
                        }
                        if (largest < 0x80u)
                            return 40 + n + 1;
                        if (largest < 0x100u)
                            return 56 + n + 1;
                        if (largest < 0x10000u)
                            return 56 + 2 * (n + 1);
                        return 56 + 4 * (n + 1);
                    }
                case NEOS_PY_LIST: return 56 + 8 * length(aObject);
                case NEOS_PY_TUPLE: return 40 + 8 * length(aObject);
                case NEOS_PY_DICT:
                    {
                        auto const n = length(aObject);
                        if (n == 0)
                            return 64;
                        long long size = 8;
                        while (size * 2 / 3 < n)
                            size *= 2;
                        return 64 + 16 + size + (size * 2 / 3) * 24 - (size == 8 ? 0 : 0);
                    }
                case NEOS_PY_RANGE: return 48;
                case NEOS_PY_SLICE: return 56;
                default:
                    break;
                }
                std::string_view data;
                if (bytes_like(aObject, data))
                    return 33 + static_cast<long long>(data.size());
                return 56;
            }

            builtin_entry const sSysFunctions[] =
            {
                { "exit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("exit", aArgs, aKwargs, { "status" }, 0u, 1u, a))
                            return nullptr;
                        std::vector<object*> args;
                        if (a[0] != nullptr)
                            args.push_back((retain(a[0]), a[0]));
                        ref const e{ make_exception("SystemExit", std::move(args)) };
                        raise_exception(e.get());
                        return nullptr;
                    } },
                { "getrecursionlimit", [](std::vector<object*> const&, object*) -> object* { return make_int(sRecursionLimit); } },
                { "setrecursionlimit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("setrecursionlimit", aArgs, aKwargs, { "limit" }, 1u, 1u, a))
                            return nullptr;
                        long long limit = 0;
                        if (kind_of(a[0]) == NEOS_PY_FLOAT)
                            return error("TypeError", "'float' object cannot be interpreted as an integer");
                        if (!to_index(a[0], limit))
                            return nullptr;
                        if (limit < 1)
                            return error("ValueError", "recursion limit must be greater or equal than 1");
                        sRecursionLimit = limit;
                        return none();
                    } },
                { "getswitchinterval", [](std::vector<object*> const&, object*) -> object* { return make_float(sSwitchInterval); } },
                { "setswitchinterval", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("setswitchinterval", aArgs, aKwargs, { "interval" }, 1u, 1u, a))
                            return nullptr;
                        double interval = 0.0;
                        if (!to_double(a[0], interval))
                            return nullptr;
                        if (interval <= 0.0)
                            return error("ValueError", "switch interval must be strictly positive");
                        sSwitchInterval = interval;
                        return none();
                    } },
                { "getsizeof", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("getsizeof", aArgs, aKwargs, { "object", "default" }, 1u, 0u, a))
                            return nullptr;
                        return make_int(size_of(a[0]));
                    } },
                { "getrefcount", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", "sys.getrefcount() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                        if (python_type(aArgs[0]) == nullptr || aArgs[0]->type->retain == retain_static)
                            return make_int(4294967295ll);
                        return make_int(reinterpret_cast<header*>(aArgs[0])->references.load() + 1);
                    } },
                { "getdefaultencoding", [](std::vector<object*> const&, object*) -> object* { return make_str("utf-8"); } },
                { "getfilesystemencoding", [](std::vector<object*> const&, object*) -> object* { return make_str("utf-8"); } },
                { "getfilesystemencodeerrors", [](std::vector<object*> const&, object*) -> object*
                    {
#if defined(_WIN32)
                        return make_str("surrogatepass");
#else
                        return make_str("surrogateescape");
#endif
                    } },
                { "intern", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 1u || kind_of(aArgs[0]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "intern() argument must be str, not " } + (aArgs.empty() ? "nothing" : type_name(aArgs[0])));
                        retain(aArgs[0]);
                        return aArgs[0];
                    } },
                { "is_finalizing", [](std::vector<object*> const&, object*) -> object* { return make_bool(false); } },
                { "exc_info", [](std::vector<object*> const&, object*) -> object*
                    {
                        auto const e = handled_exception();
                        if (e == nullptr)
                            return make_tuple({ (retain(none()), none()), (retain(none()), none()), (retain(none()), none()) });
                        retain(e);
                        ref const traceback{ getattr(e, "__traceback__") };
                        if (!traceback)
                            clear_error();
                        auto const tb = traceback ? traceback.get() : none();
                        retain(tb);
                        return make_tuple({ class_of(e), e, tb });
                    } },
                { "_getframe", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (the frame of the program that calls it (depth 0), or one that called it)
                        std::vector<object*> a;
                        if (!parse_arguments("_getframe", aArgs, aKwargs, { "depth" }, 0u, 1u, a))
                            return nullptr;
                        long long depth = 0;
                        if (!a.empty() && a[0] != nullptr && !to_index(a[0], depth))
                            return nullptr;
                        auto const stack = capture_frames();
                        auto const index = static_cast<std::size_t>(std::max(0ll, depth));
                        if (index >= stack->size())
                            return error("ValueError", "call stack is not deep enough");
                        return make_frame(stack, index);
                    } },
                { "exception", [](std::vector<object*> const&, object*) -> object*
                    {
                        auto const e = handled_exception();
                        auto const result = e != nullptr ? e : none();
                        retain(result);
                        return result;
                    } },
                { "settrace", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "gettrace", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "setprofile", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "getprofile", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "audit", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "addaudithook", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "displayhook", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() == 1u && kind_of(aArgs[0]) != NEOS_PY_NONE)
                        {
                            std::string text;
                            if (!repr_of(aArgs[0], text))
                                return nullptr;
                            write_out(text + "\n");
                        }
                        return none();
                    } },
                { "excepthook", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (the exception's report (as the module traceback formats it) on sys.stderr)
                        if (!no_keywords(aKwargs, "excepthook"))
                            return nullptr;
                        if (aArgs.size() != 3u)
                            return error("TypeError", "excepthook expected 3 arguments, got " + std::to_string(aArgs.size()));
                        if (exception_type_name(aArgs[1]) == nullptr)
                            return error("TypeError", std::string{ "print_exception(): Exception expected for value, " } + type_name(aArgs[1]) + " found");
                        std::set<object*> seen;
                        auto const report = exception_report(aArgs[1], seen, traceback_colorize());
                        ref const err{ standard_stream(2) };
                        ref const stderrFile{ module_attribute("sys", "stderr") };
                        if (!stderrFile)
                            return nullptr;
                        if (kind_of(stderrFile.get()) == NEOS_PY_NONE)
                            return (retain(none()), none());
                        ref const r{ call_method_named(stderrFile.get(), "write", { make_str(report) }) };
                        if (!r)
                            return nullptr;
                        ref const f{ call_method_named(stderrFile.get(), "flush", {}) };
                        clear_error();
                        return (retain(none()), none());
                    } }
            };

            object* version_info()
            {
                auto const& t = structseq_type("sys.version_info", { "major", "minor", "micro", "releaselevel", "serial" }, 5u);
                return make_structseq(t, { make_int(3), make_int(13), make_int(0), make_str("final"), make_int(0) });
            }
        }

        // (sys's attributes (the rest are objects.cpp's))
        void extend_sys(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            for (auto const& f : sSysFunctions)
            {
                aAttributes.emplace_back(f.name, make_builtin(f));
                // (sys.__excepthook__: the original sys.excepthook)
                if (std::strcmp(f.name, "excepthook") == 0)
                    aAttributes.emplace_back("__excepthook__", new_reference(aAttributes.back().second));
            }
            aAttributes.emplace_back("version_info", version_info());
            aAttributes.emplace_back("hexversion", make_int(0x030D00F0));
            aAttributes.emplace_back("api_version", make_int(1013));
            aAttributes.emplace_back("maxunicode", make_int(1114111));
            aAttributes.emplace_back("copyright", make_str("Copyright (c) 2001-2024 Python Software Foundation.\nAll Rights Reserved."));
            auto const exe = executable();
            aAttributes.emplace_back("executable", make_str(exe));
            aAttributes.emplace_back("_base_executable", make_str(exe));
            auto const prefix = std::filesystem::path{ exe }.parent_path().string();
            for (char const* name : { "prefix", "base_prefix", "exec_prefix", "base_exec_prefix" })
                aAttributes.emplace_back(name, make_str(prefix));
            aAttributes.emplace_back("platlibdir", make_str("lib"));
            aAttributes.emplace_back("path", make_list({ make_str("") }));
            aAttributes.emplace_back("meta_path", make_list());
            aAttributes.emplace_back("path_hooks", make_list());
            aAttributes.emplace_back("path_importer_cache", make_dict());
            aAttributes.emplace_back("modules", imported_modules());
            aAttributes.emplace_back("warnoptions", make_list());
            aAttributes.emplace_back("orig_argv", make_list());
            aAttributes.emplace_back("_xoptions", make_dict());
            aAttributes.emplace_back("dont_write_bytecode", make_bool(false));
            aAttributes.emplace_back("pycache_prefix", (retain(none()), none()));
#if defined(_WIN32)
            aAttributes.emplace_back("winver", make_str("3.13"));
            aAttributes.emplace_back("dllhandle", make_int(0));
#else
            aAttributes.emplace_back("abiflags", make_str(""));
#endif
            for (int i = 0; i < 3; ++i)
            {
                static char const* const sNames[] = { "stdin", "stdout", "stderr" };
                static char const* const sOriginals[] = { "__stdin__", "__stdout__", "__stderr__" };
                aAttributes.emplace_back(sNames[i], standard_stream(i));
                aAttributes.emplace_back(sOriginals[i], standard_stream(i));
            }
            {
                std::vector<object*> names;
                for (char const* name : { "_thread", "builtins", "errno", "itertools", "math", "sys", "time", "cmath", "_io", "_collections", "_functools",
                    "_operator", "_heapq", "_bisect", "_struct", "_random", "_json", "binascii", "_queue" })
                    names.push_back(make_str(name));
                aAttributes.emplace_back("builtin_module_names", make_tuple(std::move(names)));
            }
            {
                // (CPython 3.13's (Python/stdlib_module_names.h))
                static char const* const sStandardModules[] =
                {
                "__future__", "_abc", "_aix_support", "_android_support", "_apple_support", "_ast", "_asyncio", "_bisect", "_blake2", "_bz2",
                "_codecs", "_codecs_cn", "_codecs_hk", "_codecs_iso2022", "_codecs_jp", "_codecs_kr", "_codecs_tw", "_collections",
                "_collections_abc", "_colorize", "_compat_pickle", "_compression", "_contextvars", "_csv", "_ctypes", "_curses", "_curses_panel",
                "_datetime", "_dbm", "_decimal", "_elementtree", "_frozen_importlib", "_frozen_importlib_external", "_functools", "_gdbm",
                "_hashlib", "_heapq", "_imp", "_interpchannels", "_interpqueues", "_interpreters", "_io", "_ios_support", "_json", "_locale",
                "_lsprof", "_lzma", "_markupbase", "_md5", "_multibytecodec", "_multiprocessing", "_opcode", "_opcode_metadata", "_operator",
                "_osx_support", "_overlapped", "_pickle", "_posixshmem", "_posixsubprocess", "_py_abc", "_pydatetime", "_pydecimal", "_pyio",
                "_pylong", "_pyrepl", "_queue", "_random", "_scproxy", "_sha1", "_sha2", "_sha3", "_signal", "_sitebuiltins", "_socket", "_sqlite3",
                "_sre", "_ssl", "_stat", "_statistics", "_string", "_strptime", "_struct", "_suggestions", "_symtable", "_sysconfig", "_thread",
                "_threading_local", "_tkinter", "_tokenize", "_tracemalloc", "_typing", "_uuid", "_warnings", "_weakref", "_weakrefset", "_winapi",
                "_wmi", "_zoneinfo", "abc", "antigravity", "argparse", "array", "ast", "asyncio", "atexit", "base64", "bdb", "binascii", "bisect",
                "builtins", "bz2", "cProfile", "calendar", "cmath", "cmd", "code", "codecs", "codeop", "collections", "colorsys", "compileall",
                "concurrent", "configparser", "contextlib", "contextvars", "copy", "copyreg", "csv", "ctypes", "curses", "dataclasses", "datetime",
                "dbm", "decimal", "difflib", "dis", "doctest", "email", "encodings", "ensurepip", "enum", "errno", "faulthandler", "fcntl",
                "filecmp", "fileinput", "fnmatch", "fractions", "ftplib", "functools", "gc", "genericpath", "getopt", "getpass", "gettext", "glob",
                "graphlib", "grp", "gzip", "hashlib", "heapq", "hmac", "html", "http", "idlelib", "imaplib", "importlib", "inspect", "io",
                "ipaddress", "itertools", "json", "keyword", "linecache", "locale", "logging", "lzma", "mailbox", "marshal", "math", "mimetypes",
                "mmap", "modulefinder", "msvcrt", "multiprocessing", "netrc", "nt", "ntpath", "nturl2path", "numbers", "opcode", "operator",
                "optparse", "os", "pathlib", "pdb", "pickle", "pickletools", "pkgutil", "platform", "plistlib", "poplib", "posix", "posixpath",
                "pprint", "profile", "pstats", "pty", "pwd", "py_compile", "pyclbr", "pydoc", "pydoc_data", "pyexpat", "queue", "quopri", "random",
                "re", "readline", "reprlib", "resource", "rlcompleter", "runpy", "sched", "secrets", "select", "selectors", "shelve", "shlex",
                "shutil", "signal", "site", "smtplib", "socket", "socketserver", "sqlite3", "sre_compile", "sre_constants", "sre_parse", "ssl",
                "stat", "statistics", "string", "stringprep", "struct", "subprocess", "symtable", "sys", "sysconfig", "syslog", "tabnanny",
                "tarfile", "tempfile", "termios", "textwrap", "this", "threading", "time", "timeit", "tkinter", "token", "tokenize", "tomllib",
                "trace", "traceback", "tracemalloc", "tty", "turtle", "turtledemo", "types", "typing", "unicodedata", "unittest", "urllib", "uuid",
                "venv", "warnings", "wave", "weakref", "webbrowser", "winreg", "winsound", "wsgiref", "xml", "xmlrpc", "zipapp", "zipfile",
                "zipimport", "zlib", "zoneinfo"
                };
                std::vector<object*> names;
                for (auto name : sStandardModules)
                    names.push_back(make_str(name));
                ref const list{ make_list(std::move(names)) };
                aAttributes.emplace_back("stdlib_module_names", b_frozenset({ list.get() }, nullptr));
            }
            {
                auto const& t = structseq_type("sys.float_info", { "max", "max_exp", "max_10_exp", "min", "min_exp", "min_10_exp", "dig", "mant_dig", "epsilon",
                    "radix", "rounds" }, 11u);
                aAttributes.emplace_back("float_info", make_structseq(t, { make_float(1.7976931348623157e+308), make_int(1024), make_int(308),
                    make_float(2.2250738585072014e-308), make_int(-1021), make_int(-307), make_int(15), make_int(53), make_float(2.220446049250313e-16),
                    make_int(2), make_int(1) }));
            }
            {
                auto const& t = structseq_type("sys.int_info", { "bits_per_digit", "sizeof_digit", "default_max_str_digits", "str_digits_check_threshold" }, 4u);
                aAttributes.emplace_back("int_info", make_structseq(t, { make_int(30), make_int(4), make_int(4300), make_int(640) }));
            }
            {
                auto const& t = structseq_type("sys.hash_info", { "width", "modulus", "inf", "nan", "imag", "algorithm", "hash_bits", "seed_bits", "cutoff" }, 9u);
                aAttributes.emplace_back("hash_info", make_structseq(t, { make_int(64), make_int(2305843009213693951ll), make_int(314159), make_int(0),
                    make_int(1000003), make_str("siphash13"), make_int(64), make_int(128), make_int(0) }));
            }
            {
                auto const& t = structseq_type("sys.flags", { "debug", "inspect", "interactive", "optimize", "dont_write_bytecode", "no_user_site", "no_site",
                    "ignore_environment", "verbose", "bytes_warning", "quiet", "hash_randomization", "isolated", "dev_mode", "utf8_mode", "warn_default_encoding",
                    "safe_path", "int_max_str_digits" }, 18u);
                std::vector<object*> values;
                for (int i = 0; i < 18; ++i)
                    values.push_back(i == 13 || i == 16 ? make_bool(false) : i == 11 ? make_int(1) : i == 17 ? make_int(4300) : make_int(0));
                aAttributes.emplace_back("flags", make_structseq(t, std::move(values)));
            }
            {
                auto const& t = structseq_type("sys.thread_info", { "name", "lock", "version" }, 3u);
#if defined(_WIN32)
                aAttributes.emplace_back("thread_info", make_structseq(t, { make_str("nt"), make_str("semaphore"), (retain(none()), none()) }));
#else
                aAttributes.emplace_back("thread_info", make_structseq(t, { make_str("pthread"), make_str("semaphore"), (retain(none()), none()) }));
#endif
            }
        }
    }
}
