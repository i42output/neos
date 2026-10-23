/*
  winapi.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_winapi.c and PC/msvcrtmodule.c), Copyright (c) 2001-2024
  Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2
  (see runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (the functions and constants that subprocess uses) (see runtime/python/NOTICE).
*/

// The modules _winapi and msvcrt of Windows (CPython 3.13's Modules/_winapi.c and PC/msvcrtmodule.c): the functions and constants that
// subprocess.Popen uses (Windows) to make its child process and its pipes: CreateProcess (its environment block sorted, as CPython's,
// and its handle list (STARTUPINFOEX)), CreatePipe, DuplicateHandle, CloseHandle, GetStdHandle, GetFileType, WaitForSingleObject,
// GetExitCodeProcess, TerminateProcess and GetCurrentProcess; msvcrt's open_osfhandle and get_osfhandle. Handles are ints.

#if defined(_WIN32)

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "sockets.hpp"
#include <io.h>
#include <conio.h>
#include <fcntl.h>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        std::wstring wide(std::string const& aText)
        {
            if (aText.empty())
                return {};
            int const size = ::MultiByteToWideChar(CP_UTF8, 0, aText.data(), static_cast<int>(aText.size()), nullptr, 0);
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, aText.data(), static_cast<int>(aText.size()), result.data(), size);
            return result;
        }

        [[noreturn]] void raise_last_error()
        {
            sockets::raise_windows_error(static_cast<int>(::GetLastError()));
            throw raised{};
        }

        // (an int (a handle's value may be negative: e.g. GetCurrentProcess's))
        bool to_signed(py const& aValue, long long& aResult)
        {
            return to_index(aValue.get(), aResult);
        }
        HANDLE handle_of(py const& aValue)
        {
            if (aValue.is_none())
                return nullptr;
            long long v = 0;
            if (!to_signed(aValue, v))
                throw raised{};
            return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(v));
        }
        py handle_value(HANDLE aHandle)
        {
            return py{ static_cast<long long>(reinterpret_cast<std::intptr_t>(aHandle)) };
        }
        unsigned long dword_of(py const& aValue)
        {
            long long v = 0;
            if (!to_signed(aValue, v))
                throw raised{};
            return static_cast<unsigned long>(v);
        }
        std::string text_of(py const& aValue, char const* aWhat)
        {
            if (!isinstance(aValue, builtin("str")))
                py_raise("TypeError", std::string{ aWhat } + " must be str, not " + type_name(aValue.get()));
            std::string const text = aValue.str();
            if (text.find('\0') != std::string::npos)
                py_raise("ValueError", "embedded null character");
            return text;
        }

        // (getenvironment: the environment block of a mapping of strs: its keys sorted (case-insensitively, as CompareStringOrdinal
        // compares them), the last of those that differ only in case kept; "K=V\0...\0\0")
        std::wstring environment_block(py const& aEnvironment)
        {
            struct variable { std::wstring key; std::wstring value; };
            std::vector<variable> variables;
            py const keys = builtin("list")(aEnvironment.method("keys"));
            py const values = builtin("list")(aEnvironment.method("values"));
            auto const k = keys.items();
            auto const v = values.items();
            if (k.size() != v.size())
                py_raise("RuntimeError", "environment changed size during iteration");
            for (std::size_t i = 0u; i < k.size(); ++i)
            {
                if (!isinstance(k[i], builtin("str")) || !isinstance(v[i], builtin("str")))
                    py_raise("TypeError", "environment can only contain strings");
                std::string const key = k[i].str();
                std::string const value = v[i].str();
                if (key.find('\0') != std::string::npos || value.find('\0') != std::string::npos)
                    py_raise("ValueError", "embedded null character");
                // (a key cannot be empty, nor contain '=' (but for its first character: e.g. "=C:"))
                if (key.empty() || key.find('=', 1u) != std::string::npos)
                    py_raise("ValueError", "illegal environment variable name");
                variables.push_back({ wide(key), wide(value) });
            }
            auto const compare = [](std::wstring const& a, std::wstring const& b)
            {
                return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE);
            };
            std::stable_sort(variables.begin(), variables.end(), [&](variable const& a, variable const& b) { return compare(a.key, b.key) == CSTR_LESS_THAN; });
            std::vector<variable> unique;
            for (auto& x : variables)
                if (!unique.empty() && compare(unique.back().key, x.key) == CSTR_EQUAL)
                    unique.back() = std::move(x);
                else
                    unique.push_back(std::move(x));
            std::wstring block;
            for (auto const& x : unique)
            {
                block += x.key;
                block += L'=';
                block += x.value;
                block += L'\0';
            }
            if (block.empty())
                block += L'\0';
            block += L'\0';
            return block;
        }

        py native(char const* aName, char const* aDoc, native_body aBody, char const* aModule = "_winapi")
        {
            return make_native_function(aName, aName, aDoc, aModule, std::move(aBody));
        }

        bool winapi_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("CloseHandle", native("CloseHandle", "Close handle.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("CloseHandle", { py_positional("handle") }, aArgs, aKwargs);
                    auto const h = handle_of(a[0]);
                    gil_release();
                    BOOL const ok = ::CloseHandle(h);
                    gil_acquire();
                    if (!ok)
                        raise_last_error();
                    return py::none();
                }));
                add("CreatePipe", native("CreatePipe", "Create an anonymous pipe.\n\n  pipe_attrs\n    Ignored internally, can be None.\n\nReturns a 2-tuple of handles, to the read and write ends of the pipe.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("CreatePipe", { py_positional("pipe_attrs"), py_positional("size") }, aArgs, aKwargs);
                    auto const size = dword_of(a[1]);
                    HANDLE read = nullptr;
                    HANDLE write = nullptr;
                    gil_release();
                    BOOL const ok = ::CreatePipe(&read, &write, nullptr, size);
                    gil_acquire();
                    if (!ok)
                        raise_last_error();
                    return py_tuple({ handle_value(read), handle_value(write) });
                }));
                add("CreateProcess", native("CreateProcess", "Create a new process and its primary thread.\n\n  command_line\n    Can be str or None\n  proc_attrs\n    Ignored internally, can be None.\n  thread_attrs\n    Ignored internally, can be None.\n\nThe return value is a tuple of the process handle, thread handle,\nprocess ID, and thread ID.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("CreateProcess", { py_positional("application_name"), py_positional("command_line"), py_positional("proc_attrs"),
                        py_positional("thread_attrs"), py_positional("inherit_handles"), py_positional("creation_flags"), py_positional("env_mapping"),
                        py_positional("current_directory"), py_positional("startup_info") }, aArgs, aKwargs);
                    std::wstring const application = a[0].is_none() ? std::wstring{} : wide(text_of(a[0], "CreateProcess() argument 'application_name'"));
                    std::wstring commandLine = a[1].is_none() ? std::wstring{} : wide(text_of(a[1], "CreateProcess() argument 'command_line'"));
                    BOOL const inherit = a[4].truthy() ? TRUE : FALSE;
                    DWORD flags = dword_of(a[5]);
                    std::wstring const directory = a[7].is_none() ? std::wstring{} : wide(text_of(a[7], "CreateProcess() argument 'current_directory'"));
                    py const& startupInfo = a[8];
                    STARTUPINFOEXW si{};
                    si.StartupInfo.cb = sizeof(si);
                    // (the startup information's attributes (as CPython's getulong and gethandle))
                    auto const optional = [&](char const* aName) -> py
                    {
                        return builtin("getattr")(startupInfo, py{ aName }, py::none());
                    };
                    if (py const v = optional("dwFlags"); !v.is_none())
                        si.StartupInfo.dwFlags = dword_of(v);
                    if (py const v = optional("wShowWindow"); !v.is_none())
                        si.StartupInfo.wShowWindow = static_cast<WORD>(dword_of(v));
                    si.StartupInfo.hStdInput = handle_of(optional("hStdInput"));
                    si.StartupInfo.hStdOutput = handle_of(optional("hStdOutput"));
                    si.StartupInfo.hStdError = handle_of(optional("hStdError"));
                    // (its attribute list's handle list: the only handles the child inherits)
                    std::vector<HANDLE> handles;
                    std::vector<unsigned char> attributeList;
                    if (py const list = optional("lpAttributeList"); !list.is_none())
                    {
                        if (!isinstance(list, builtin("dict")))
                            py_raise("TypeError", "lpAttributeList must be a dict");
                        py const handleList = list.method("get", { py{ "handle_list" } });
                        if (!handleList.is_none())
                            for (py const& h : handleList.items())
                                handles.push_back(handle_of(h));
                        if (!handles.empty())
                        {
                            SIZE_T size = 0;
                            ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
                            attributeList.resize(size);
                            auto const attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeList.data());
                            if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &size) ||
                                !::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(), handles.size() * sizeof(HANDLE), nullptr, nullptr))
                                raise_last_error();
                            si.lpAttributeList = attributes;
                            flags |= EXTENDED_STARTUPINFO_PRESENT;
                        }
                    }
                    std::wstring environment;
                    if (!a[6].is_none())
                    {
                        environment = environment_block(a[6]);
                        flags |= CREATE_UNICODE_ENVIRONMENT;
                    }
                    PROCESS_INFORMATION pi{};
                    gil_release();
                    BOOL const ok = ::CreateProcessW(application.empty() ? nullptr : application.c_str(), commandLine.empty() ? nullptr : commandLine.data(),
                        nullptr, nullptr, inherit, flags, environment.empty() ? nullptr : environment.data(), directory.empty() ? nullptr : directory.c_str(),
                        &si.StartupInfo, &pi);
                    auto const error = ::GetLastError();
                    gil_acquire();
                    if (si.lpAttributeList != nullptr)
                        ::DeleteProcThreadAttributeList(si.lpAttributeList);
                    if (!ok)
                    {
                        ::SetLastError(error);
                        raise_last_error();
                    }
                    return py_tuple({ handle_value(pi.hProcess), handle_value(pi.hThread), py{ static_cast<long long>(pi.dwProcessId) },
                        py{ static_cast<long long>(pi.dwThreadId) } });
                }));
                add("DuplicateHandle", native("DuplicateHandle", "Return a duplicate handle object.\n\nThe duplicate handle refers to the same object as the original\nhandle. Therefore, any changes to the object are reflected\nthrough both handles.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("DuplicateHandle", { py_positional("source_process_handle"), py_positional("source_handle"), py_positional("target_process_handle"),
                        py_positional("desired_access"), py_positional("inherit_handle"), py_positional("options", py{ 0 }) }, aArgs, aKwargs);
                    HANDLE target = nullptr;
                    auto const sourceProcess = handle_of(a[0]);
                    auto const source = handle_of(a[1]);
                    auto const targetProcess = handle_of(a[2]);
                    auto const access = dword_of(a[3]);
                    BOOL const inherit = a[4].truthy() ? TRUE : FALSE;
                    auto const options = dword_of(a[5]);
                    gil_release();
                    BOOL const ok = ::DuplicateHandle(sourceProcess, source, targetProcess, &target, access, inherit, options);
                    gil_acquire();
                    if (!ok)
                        raise_last_error();
                    return handle_value(target);
                }));
                add("GetCurrentProcess", native("GetCurrentProcess", "Return a handle object for the current process.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("GetCurrentProcess", {}, aArgs, aKwargs);
                    return handle_value(::GetCurrentProcess());
                }));
                add("GetExitCodeProcess", native("GetExitCodeProcess", "Return the termination status of the specified process.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("GetExitCodeProcess", { py_positional("process") }, aArgs, aKwargs);
                    DWORD code = 0;
                    if (!::GetExitCodeProcess(handle_of(a[0]), &code))
                        raise_last_error();
                    return py{ static_cast<long long>(code) };
                }));
                add("GetFileType", native("GetFileType", "", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("GetFileType", { py_positional("handle") }, aArgs, aKwargs);
                    auto const h = handle_of(a[0]);
                    gil_release();
                    auto const type = ::GetFileType(h);
                    auto const error = ::GetLastError();
                    gil_acquire();
                    if (type == FILE_TYPE_UNKNOWN && error != NO_ERROR)
                    {
                        ::SetLastError(error);
                        raise_last_error();
                    }
                    return py{ static_cast<long long>(type) };
                }));
                add("GetStdHandle", native("GetStdHandle", "Return a handle to the specified standard device.\n\n  std_handle\n    One of STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, or STD_ERROR_HANDLE.\n\nThe integer associated with the handle object is returned.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("GetStdHandle", { py_positional("std_handle") }, aArgs, aKwargs);
                    gil_release();
                    auto const h = ::GetStdHandle(dword_of(a[0]));
                    gil_acquire();
                    if (h == INVALID_HANDLE_VALUE)
                        raise_last_error();
                    if (h == nullptr)
                        return py::none();
                    return handle_value(h);
                }));
                add("TerminateProcess", native("TerminateProcess", "Terminate the specified process and all of its threads.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("TerminateProcess", { py_positional("handle"), py_positional("exit_code") }, aArgs, aKwargs);
                    if (!::TerminateProcess(handle_of(a[0]), dword_of(a[1])))
                        raise_last_error();
                    return py::none();
                }));
                add("WaitForSingleObject", native("WaitForSingleObject", "Wait for a single object.\n\nWait until the specified object is in the signaled state or\nthe time-out interval elapses. The timeout value is specified\nin milliseconds.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("WaitForSingleObject", { py_positional("handle"), py_positional("milliseconds") }, aArgs, aKwargs);
                    auto const h = handle_of(a[0]);
                    auto const milliseconds = dword_of(a[1]);
                    gil_release();
                    auto const r = ::WaitForSingleObject(h, milliseconds);
                    gil_acquire();
                    if (r == WAIT_FAILED)
                        raise_last_error();
                    return py{ static_cast<long long>(r) };
                }));
                for (auto const& c : { std::pair<char const*, long long>{ "CREATE_NEW_CONSOLE", CREATE_NEW_CONSOLE }, { "CREATE_NEW_PROCESS_GROUP", CREATE_NEW_PROCESS_GROUP },
                    { "STD_INPUT_HANDLE", static_cast<long long>(static_cast<int>(STD_INPUT_HANDLE)) }, { "STD_OUTPUT_HANDLE", static_cast<long long>(static_cast<int>(STD_OUTPUT_HANDLE)) },
                    { "STD_ERROR_HANDLE", static_cast<long long>(static_cast<int>(STD_ERROR_HANDLE)) }, { "SW_HIDE", SW_HIDE },
                    { "STARTF_USESTDHANDLES", STARTF_USESTDHANDLES }, { "STARTF_USESHOWWINDOW", STARTF_USESHOWWINDOW },
                    { "STARTF_FORCEONFEEDBACK", STARTF_FORCEONFEEDBACK }, { "STARTF_FORCEOFFFEEDBACK", STARTF_FORCEOFFFEEDBACK },
                    { "ABOVE_NORMAL_PRIORITY_CLASS", ABOVE_NORMAL_PRIORITY_CLASS }, { "BELOW_NORMAL_PRIORITY_CLASS", BELOW_NORMAL_PRIORITY_CLASS },
                    { "HIGH_PRIORITY_CLASS", HIGH_PRIORITY_CLASS }, { "IDLE_PRIORITY_CLASS", IDLE_PRIORITY_CLASS }, { "NORMAL_PRIORITY_CLASS", NORMAL_PRIORITY_CLASS },
                    { "REALTIME_PRIORITY_CLASS", REALTIME_PRIORITY_CLASS }, { "CREATE_NO_WINDOW", CREATE_NO_WINDOW }, { "DETACHED_PROCESS", DETACHED_PROCESS },
                    { "CREATE_DEFAULT_ERROR_MODE", CREATE_DEFAULT_ERROR_MODE }, { "CREATE_BREAKAWAY_FROM_JOB", CREATE_BREAKAWAY_FROM_JOB },
                    { "CREATE_UNICODE_ENVIRONMENT", CREATE_UNICODE_ENVIRONMENT }, { "DUPLICATE_SAME_ACCESS", DUPLICATE_SAME_ACCESS },
                    { "DUPLICATE_CLOSE_SOURCE", DUPLICATE_CLOSE_SOURCE }, { "FILE_TYPE_UNKNOWN", FILE_TYPE_UNKNOWN }, { "FILE_TYPE_DISK", FILE_TYPE_DISK },
                    { "FILE_TYPE_CHAR", FILE_TYPE_CHAR }, { "FILE_TYPE_PIPE", FILE_TYPE_PIPE }, { "FILE_TYPE_REMOTE", FILE_TYPE_REMOTE },
                    { "INFINITE", static_cast<long long>(INFINITE) }, { "WAIT_OBJECT_0", WAIT_OBJECT_0 }, { "WAIT_ABANDONED_0", WAIT_ABANDONED_0 },
                    { "WAIT_TIMEOUT", WAIT_TIMEOUT }, { "STILL_ACTIVE", STILL_ACTIVE }, { "NULL", 0 }, { "ERROR_ACCESS_DENIED", ERROR_ACCESS_DENIED },
                    { "ERROR_BROKEN_PIPE", ERROR_BROKEN_PIPE }, { "ERROR_NO_DATA", ERROR_NO_DATA }, { "ERROR_PIPE_BUSY", ERROR_PIPE_BUSY },
                    { "ERROR_PIPE_CONNECTED", ERROR_PIPE_CONNECTED }, { "ERROR_IO_PENDING", ERROR_IO_PENDING }, { "ERROR_MORE_DATA", ERROR_MORE_DATA },
                    { "ERROR_OPERATION_ABORTED", ERROR_OPERATION_ABORTED }, { "ERROR_NETNAME_DELETED", ERROR_NETNAME_DELETED }, { "ERROR_SEM_TIMEOUT", ERROR_SEM_TIMEOUT },
                    { "ERROR_ALREADY_EXISTS", ERROR_ALREADY_EXISTS }, { "ERROR_FILE_NOT_FOUND", ERROR_FILE_NOT_FOUND }, { "ERROR_INVALID_HANDLE", ERROR_INVALID_HANDLE },
                    { "GENERIC_READ", static_cast<long long>(GENERIC_READ) }, { "GENERIC_WRITE", GENERIC_WRITE }, { "OPEN_EXISTING", OPEN_EXISTING },
                    { "FILE_FLAG_OVERLAPPED", FILE_FLAG_OVERLAPPED }, { "FILE_FLAG_FIRST_PIPE_INSTANCE", FILE_FLAG_FIRST_PIPE_INSTANCE },
                    { "PIPE_ACCESS_DUPLEX", PIPE_ACCESS_DUPLEX }, { "PIPE_ACCESS_INBOUND", PIPE_ACCESS_INBOUND }, { "PIPE_ACCESS_OUTBOUND", PIPE_ACCESS_OUTBOUND },
                    { "PIPE_READMODE_MESSAGE", PIPE_READMODE_MESSAGE }, { "PIPE_TYPE_MESSAGE", PIPE_TYPE_MESSAGE }, { "PIPE_WAIT", PIPE_WAIT },
                    { "PIPE_UNLIMITED_INSTANCES", PIPE_UNLIMITED_INSTANCES }, { "NMPWAIT_WAIT_FOREVER", static_cast<long long>(NMPWAIT_WAIT_FOREVER) },
                    { "PROCESS_ALL_ACCESS", PROCESS_ALL_ACCESS }, { "PROCESS_DUP_HANDLE", PROCESS_DUP_HANDLE }, { "SYNCHRONIZE", SYNCHRONIZE } })
                    add(c.first, py{ c.second });
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        bool msvcrt_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("open_osfhandle", native("open_osfhandle", "Create a C runtime file descriptor from the file handle handle.\n\nThe flags parameter should be a bitwise OR of os.O_APPEND, os.O_RDONLY,\nand os.O_TEXT. The returned file descriptor may be used as a parameter\nto os.fdopen() to create a file object.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("open_osfhandle", { py_positional("handle"), py_positional("flags") }, aArgs, aKwargs);
                    auto const h = handle_of(a[0]);
                    long long flags = 0;
                    if (!to_signed(a[1], flags))
                        throw raised{};
                    auto const fd = ::_open_osfhandle(reinterpret_cast<std::intptr_t>(h), static_cast<int>(flags));
                    if (fd == -1)
                    {
                        raise_os_error(errno);
                        throw raised{};
                    }
                    return py{ static_cast<long long>(fd) };
                }, "msvcrt"));
                add("get_osfhandle", native("get_osfhandle", "Return the file handle for the file descriptor fd.\n\nRaises OSError if fd is not recognized.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("get_osfhandle", { py_positional("fd") }, aArgs, aKwargs);
                    long long fd = 0;
                    if (!to_signed(a[0], fd))
                        throw raised{};
                    auto const h = ::_get_osfhandle(static_cast<int>(fd));
                    if (h == -1)
                    {
                        raise_os_error(EBADF);
                        throw raised{};
                    }
                    return py{ static_cast<long long>(h) };
                }, "msvcrt"));
                // (the console's keys: kbhit, getch, getwch, getche, getwche, putch and putwch (PC/msvcrtmodule.c's))
                add("kbhit", native("kbhit", "Returns a nonzero value if a keypress is waiting to be read. Otherwise, return 0.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("kbhit", {}, aArgs, aKwargs);
                    return py{ static_cast<long long>(::_kbhit()) };
                }, "msvcrt"));
                add("getch", native("getch", "Read a keypress and return the resulting character as a byte string.\n\nNothing is echoed to the console. This call will block if a keypress is\nnot already available, but will not wait for Enter to be pressed. If the\npressed key was a special function key, this will return '\\000' or\n'\\xe0'; the next call will return the keycode. The Control-C keypress\ncannot be read with this function.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("getch", {}, aArgs, aKwargs);
                    gil_release();
                    int const c = ::_getch();
                    gil_acquire();
                    return py{ make_bytes(std::string(1u, static_cast<char>(c))) };
                }, "msvcrt"));
                add("getwch", native("getwch", "Wide char variant of getch(), returning a Unicode value.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("getwch", {}, aArgs, aKwargs);
                    gil_release();
                    wint_t const c = ::_getwch();
                    gil_acquire();
                    return builtin("chr")(py{ static_cast<long long>(c) });
                }, "msvcrt"));
                add("getche", native("getche", "Similar to getch(), but the keypress will be echoed if possible.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("getche", {}, aArgs, aKwargs);
                    gil_release();
                    int const c = ::_getche();
                    gil_acquire();
                    return py{ make_bytes(std::string(1u, static_cast<char>(c))) };
                }, "msvcrt"));
                add("getwche", native("getwche", "Wide char variant of getche(), returning a Unicode value.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("getwche", {}, aArgs, aKwargs);
                    gil_release();
                    wint_t const c = ::_getwche();
                    gil_acquire();
                    return builtin("chr")(py{ static_cast<long long>(c) });
                }, "msvcrt"));
                add("putch", native("putch", "Print the byte string char to the console without buffering.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("putch", { py_positional_only("char") }, aArgs, aKwargs);
                    std::string_view b;
                    if (!isinstance(a[0], builtin("bytes")) || !bytes_like(a[0].get(), b) || b.size() != 1u)
                        py_raise("TypeError", "putch() argument must be a byte string of length 1");
                    ::_putch(static_cast<unsigned char>(b[0]));
                    return py::none();
                }, "msvcrt"));
                add("putwch", native("putwch", "Wide char variant of putch(), accepting a Unicode value.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("putwch", { py_positional_only("unicode_char") }, aArgs, aKwargs);
                    long long length = 0;
                    if (!isinstance(a[0], builtin("str")) || !to_signed(builtin("len")(a[0]), length) || length != 1)
                        py_raise("TypeError", "putwch() argument must be a unicode character");
                    long long ordinal = 0;
                    if (!to_signed(builtin("ord")(a[0]), ordinal))
                        throw raised{};
                    auto const code = static_cast<std::uint32_t>(ordinal);
                    // (a character beyond the BMP: its UTF-16 surrogates)
                    if (code > 0xFFFFu)
                    {
                        ::_putwch(static_cast<wchar_t>(0xD800u + ((code - 0x10000u) >> 10u)));
                        ::_putwch(static_cast<wchar_t>(0xDC00u + ((code - 0x10000u) & 0x3FFu)));
                    }
                    else
                        ::_putwch(static_cast<wchar_t>(code));
                    return py::none();
                }, "msvcrt"));
                for (auto const& c : { std::pair<char const*, long long>{ "LK_UNLCK", 0 }, { "LK_LOCK", 1 }, { "LK_NBLCK", 2 }, { "LK_RLCK", 3 }, { "LK_NBRLCK", 4 } })
                    add(c.first, py{ c.second });
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sWinapi = register_module("_winapi", winapi_module);
        inline bool const sMsvcrt = register_module("msvcrt", msvcrt_module);
    }
}

#endif
