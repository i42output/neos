/*
  sockets.hpp

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

  Portions of this file are derived from CPython 3.13 (Python/errors.c, PC/errmap.h and Objects/exceptions.c), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted
  to its object model (see runtime/python/NOTICE).
*/

#pragma once

// The sockets API of the platform (BSD sockets; Winsock on Windows) for the modules select and _socket (select.cpp and socket.cpp): included
// before any other header (on Windows, FD_SETSIZE is CPython's (512), set before winsock2.h is), and the errors of its functions as OSError
// (on Windows, an error of Winsock's is a Windows error: its errno, its message and winerror, as CPython's PyErr_SetExcFromWindowsErr)

#if defined(_WIN32)
#ifndef FD_SETSIZE
#define FD_SETSIZE 512
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstdio>
#include <string>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace sockets
        {
#if defined(_WIN32)
            using socket_type = SOCKET;
            inline constexpr socket_type kInvalidSocket = INVALID_SOCKET;
#else
            using socket_type = int;
            inline constexpr socket_type kInvalidSocket = -1;
#endif

#if defined(_WIN32)
            // (a Windows error code as an errno value (PC/errmap.h's winerror_to_errno: Winsock's codes are errno values))
            inline int winerror_to_errno(int aWinError)
            {
                if ((aWinError & 0xFFFF0000) == static_cast<int>(0x80070000))
                    aWinError &= 0x0000FFFF;
                if (aWinError >= 10000 && aWinError < 12000)
                {
                    switch (aWinError)
                    {
                    case WSAEINTR:
                    case WSAEBADF:
                    case WSAEACCES:
                    case WSAEFAULT:
                    case WSAEINVAL:
                    case WSAEMFILE:
                        return aWinError - 10000;
                    default:
                        return aWinError;
                    }
                }
                switch (aWinError)
                {
                case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_DRIVE: case ERROR_NO_MORE_FILES: case ERROR_BAD_NETPATH:
                case ERROR_BAD_NET_NAME: case ERROR_BAD_PATHNAME: case ERROR_FILENAME_EXCED_RANGE:
                    return ENOENT;
                case ERROR_BAD_ENVIRONMENT:
                    return E2BIG;
                case ERROR_BAD_FORMAT: case ERROR_INVALID_STARTING_CODESEG: case ERROR_INVALID_STACKSEG: case ERROR_INVALID_MODULETYPE:
                case ERROR_INVALID_EXE_SIGNATURE: case ERROR_EXE_MARKED_INVALID: case ERROR_BAD_EXE_FORMAT: case ERROR_ITERATED_DATA_EXCEEDS_64k:
                case ERROR_INVALID_MINALLOCSIZE: case ERROR_DYNLINK_FROM_INVALID_RING: case ERROR_IOPL_NOT_ENABLED: case ERROR_INVALID_SEGDPL:
                case ERROR_AUTODATASEG_EXCEEDS_64k: case ERROR_RING2SEG_MUST_BE_MOVABLE: case ERROR_RELOC_CHAIN_XEEDS_SEGLIM: case ERROR_INFLOOP_IN_RELOC_CHAIN:
                    return ENOEXEC;
                case ERROR_INVALID_HANDLE: case ERROR_INVALID_TARGET_HANDLE: case ERROR_DIRECT_ACCESS_HANDLE:
                    return EBADF;
                case ERROR_WAIT_NO_CHILDREN: case ERROR_CHILD_NOT_COMPLETE:
                    return ECHILD;
                case ERROR_NO_PROC_SLOTS: case ERROR_MAX_THRDS_REACHED: case ERROR_NESTING_NOT_ALLOWED:
                    return EAGAIN;
                case ERROR_ARENA_TRASHED: case ERROR_NOT_ENOUGH_MEMORY: case ERROR_INVALID_BLOCK: case ERROR_NOT_ENOUGH_QUOTA:
                    return ENOMEM;
                case ERROR_ACCESS_DENIED: case ERROR_CURRENT_DIRECTORY: case ERROR_WRITE_PROTECT: case ERROR_BAD_UNIT: case ERROR_NOT_READY:
                case ERROR_BAD_COMMAND: case ERROR_CRC: case ERROR_BAD_LENGTH: case ERROR_SEEK: case ERROR_NOT_DOS_DISK: case ERROR_SECTOR_NOT_FOUND:
                case ERROR_OUT_OF_PAPER: case ERROR_WRITE_FAULT: case ERROR_READ_FAULT: case ERROR_GEN_FAILURE: case ERROR_SHARING_VIOLATION:
                case ERROR_LOCK_VIOLATION: case ERROR_WRONG_DISK: case ERROR_SHARING_BUFFER_EXCEEDED: case ERROR_NETWORK_ACCESS_DENIED:
                case ERROR_CANNOT_MAKE: case ERROR_FAIL_I24: case ERROR_DRIVE_LOCKED: case ERROR_SEEK_ON_DEVICE: case ERROR_NOT_LOCKED:
                case ERROR_LOCK_FAILED:
                case 35: // (undefined)
                    return EACCES;
                case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS:
                    return EEXIST;
                case ERROR_NOT_SAME_DEVICE:
                    return EXDEV;
                case ERROR_DIRECTORY:
                    return ENOTDIR;
                case ERROR_TOO_MANY_OPEN_FILES:
                    return EMFILE;
                case ERROR_DISK_FULL:
                    return ENOSPC;
                case ERROR_BROKEN_PIPE: case ERROR_NO_DATA:
                    return EPIPE;
                case ERROR_DIR_NOT_EMPTY:
                    return ENOTEMPTY;
                case ERROR_NO_UNICODE_TRANSLATION:
                    return EILSEQ;
                case WAIT_TIMEOUT:
                    return ETIMEDOUT;
                case ERROR_INVALID_FUNCTION: case ERROR_INVALID_ACCESS: case ERROR_INVALID_DATA: case ERROR_INVALID_PARAMETER: case ERROR_NEGATIVE_SEEK:
                default:
                    return EINVAL;
                }
            }

            // (the subclass of OSError of an errno value (CPython's errnomap on Windows: its sockets' errors are Winsock's codes))
            inline char const* os_error_type(int aErrno)
            {
                switch (aErrno)
                {
                case EAGAIN: case WSAEALREADY: case WSAEINPROGRESS: case WSAEWOULDBLOCK: return "BlockingIOError";
                case EPIPE: case WSAESHUTDOWN: return "BrokenPipeError";
                case ECHILD: return "ChildProcessError";
                case WSAECONNABORTED: return "ConnectionAbortedError";
                case WSAECONNREFUSED: return "ConnectionRefusedError";
                case WSAECONNRESET: return "ConnectionResetError";
                case EEXIST: return "FileExistsError";
                case ENOENT: return "FileNotFoundError";
                case EISDIR: return "IsADirectoryError";
                case ENOTDIR: return "NotADirectoryError";
                case EINTR: return "InterruptedError";
                case EACCES: case EPERM: return "PermissionError";
                case ESRCH: return "ProcessLookupError";
                case ETIMEDOUT: case WSAETIMEDOUT: return "TimeoutError";
                default: return "OSError";
                }
            }

            // (a Windows error (e.g. Winsock's): OSError(errno, message, None, winerror) (the message is the system's, without its trailing
            // spaces and dots) (Python/errors.c's PyErr_SetExcFromWindowsErr))
            inline std::nullptr_t raise_windows_error(int aWinError)
            {
                wchar_t* buffer = nullptr;
                auto length = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                    static_cast<DWORD>(aWinError), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
                std::string message;
                if (length == 0)
                {
                    char text[32];
                    std::snprintf(text, sizeof(text), "Windows Error 0x%x", static_cast<unsigned>(aWinError));
                    message = text;
                }
                else
                {
                    while (length > 0 && (buffer[length - 1] <= L' ' || buffer[length - 1] == L'.'))
                        buffer[--length] = L'\0';
                    auto const size = ::WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
                    message.resize(static_cast<std::size_t>(size));
                    ::WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), message.data(), size, nullptr, nullptr);
                }
                if (buffer != nullptr)
                    ::LocalFree(buffer);
                int const errnoValue = winerror_to_errno(aWinError);
                ref const e{ make_exception(os_error_type(errnoValue), { make_int(errnoValue), make_str(message), (retain(none()), none()),
                    make_int(aWinError) }) };
                if (e)
                    raise_exception(e.get());
                return nullptr;
            }
#endif

            // (the error of the last socket function: OSError (CPython's socketmodule.c set_error: on Windows Winsock's error, if there is one))
            inline std::nullptr_t raise_socket_error()
            {
#if defined(_WIN32)
                int const e = ::WSAGetLastError();
                if (e != 0)
                    return raise_windows_error(e);
#endif
                return raise_os_error(errno);
            }
        }
    }
}
