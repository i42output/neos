/*
  socketserver.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/pwdmodule.c, Modules/grpmodule.c and
  Modules/fcntlmodule.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the
  Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the
  neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules pwd, grp and fcntl of Unix (CPython 3.13's Modules/pwdmodule.c, grpmodule.c and fcntlmodule.c): the password and group 
// databases (their entries: struct sequences), and fcntl, ioctl, flock and lockf of file descriptors (and their constants).

#if !defined(_WIN32)

#include <cerrno>
#include <climits>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        py native(char const* aName, char const* aModule, char const* aDoc, native_body aBody)
        {
            return make_native_function(aName, aName, aDoc, aModule, std::move(aBody));
        }

        [[noreturn]] void raise_errno(int aErrno)
        {
            raise_os_error(aErrno);
            throw raised{};
        }

        long long index_of(py const& aValue)
        {
            long long v = 0;
            if (!to_index(aValue.get(), v))
                throw raised{};
            return v;
        }

        int int_of(py const& aValue)
        {
            auto const v = index_of(aValue);
            if (v < INT_MIN || v > INT_MAX)
                py_raise("OverflowError", v < 0 ? "signed integer is less than minimum" : "signed integer is greater than maximum");
            return static_cast<int>(v);
        }

        // (an int, or an object's fileno(), as a file descriptor (PyObject_AsFileDescriptor))
        int file_descriptor(py const& aObject)
        {
            int fd = 0;
            if (isinstance(aObject, builtin("int")))
                fd = int_of(aObject);
            else if (builtin("hasattr")(aObject, py{ "fileno" }).truthy())
            {
                py const fno = aObject.method("fileno");
                if (!isinstance(fno, builtin("int")))
                    py_raise("TypeError", "fileno() returned a non-integer");
                fd = int_of(fno);
            }
            else
                py_raise("TypeError", "argument must be an int, or have a fileno() method.");
            if (fd < 0)
                py_raise("ValueError", "file descriptor cannot be a negative integer (" + std::to_string(fd) + ")");
            return fd;
        }

        // --- pwd

        structseq_descriptor const& passwd_type()
        {
            return structseq_type("pwd.struct_passwd", { "pw_name", "pw_passwd", "pw_uid", "pw_gid", "pw_gecos", "pw_dir", "pw_shell" }, 7u);
        }

        py passwd_of(struct passwd const& p)
        {
            return py{ make_structseq(passwd_type(), { make_str(p.pw_name != nullptr ? p.pw_name : ""), make_str(p.pw_passwd != nullptr ? p.pw_passwd : ""),
                make_int(static_cast<long long>(p.pw_uid)), make_int(static_cast<long long>(p.pw_gid)), make_str(p.pw_gecos != nullptr ? p.pw_gecos : ""),
                make_str(p.pw_dir != nullptr ? p.pw_dir : ""), make_str(p.pw_shell != nullptr ? p.pw_shell : "") }) };
        }

        bool pwd_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", py{ "This module provides access to the Unix password database.\nIt is available on all Unix versions.\n\nPassword database entries are reported as 7-tuples containing the following\nitems from the password database (see `<pwd.h>'), in order:\npw_name, pw_passwd, pw_uid, pw_gid, pw_gecos, pw_dir, pw_shell.\nThe uid and gid items are integers, all others are strings. An\nexception is raised if the entry asked for cannot be found." });
                add("getpwuid", native("getpwuid", "pwd", "Return the password database entry for the given numeric user ID.\n\nSee `help(pwd)` for more on password database entries.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("getpwuid", { py_positional("uidobj") }, aArgs, aKwargs);
                    if (!isinstance(a[0], builtin("int")))
                        py_raise("TypeError", "uid should be integer, not " + type_of(a[0]).attr("__name__").str());
                    auto const uid = index_of(a[0]);
                    std::vector<char> buffer(16384u);
                    struct passwd entry{};
                    struct passwd* found = nullptr;
                    gil_release();
                    int e = 0;
                    while ((e = ::getpwuid_r(static_cast<uid_t>(uid), &entry, buffer.data(), buffer.size(), &found)) == ERANGE)
                        buffer.resize(buffer.size() * 2u);
                    gil_acquire();
                    if (found == nullptr)
                        py_raise(builtin("KeyError")(py{ "getpwuid(): uid not found: " + std::to_string(uid) }));
                    return passwd_of(entry);
                }));
                add("getpwnam", native("getpwnam", "pwd", "Return the password database entry for the given user name.\n\nSee `help(pwd)` for more on password database entries.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("getpwnam", { py_positional("name") }, aArgs, aKwargs);
                    if (!isinstance(a[0], builtin("str")))
                        py_raise("TypeError", "getpwnam() argument must be str, not " + type_of(a[0]).attr("__name__").str());
                    auto const name = a[0].str();
                    if (name.find('\0') != std::string::npos)
                        py_raise("ValueError", "embedded null character");
                    std::vector<char> buffer(16384u);
                    struct passwd entry{};
                    struct passwd* found = nullptr;
                    gil_release();
                    while (::getpwnam_r(name.c_str(), &entry, buffer.data(), buffer.size(), &found) == ERANGE)
                        buffer.resize(buffer.size() * 2u);
                    gil_acquire();
                    if (found == nullptr)
                        py_raise(builtin("KeyError")(py{ "getpwnam(): name not found: " + a[0].repr() }));
                    return passwd_of(entry);
                }));
                add("getpwall", native("getpwall", "pwd", "Return a list of all available password database entries, in arbitrary order.\n\nSee help(pwd) for more on password database entries.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("getpwall", {}, aArgs, aKwargs);
                    py const result = py_list();
                    ::setpwent();
                    while (struct passwd const* p = ::getpwent())
                        result.method("append", { passwd_of(*p) });
                    ::endpwent();
                    return result;
                }));
                passwd_type();
                add("struct_passwd", py{ type_object_for("struct_passwd", "pwd.struct_passwd") });
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        // --- grp

        structseq_descriptor const& group_type()
        {
            return structseq_type("grp.struct_group", { "gr_name", "gr_passwd", "gr_gid", "gr_mem" }, 4u);
        }

        py group_of(struct group const& g)
        {
            std::vector<object*> members;
            for (auto m = g.gr_mem; m != nullptr && *m != nullptr; ++m)
                members.push_back(make_str(*m));
            return py{ make_structseq(group_type(), { make_str(g.gr_name != nullptr ? g.gr_name : ""), make_str(g.gr_passwd != nullptr ? g.gr_passwd : ""),
                make_int(static_cast<long long>(g.gr_gid)), make_list(std::move(members)) }) };
        }

        bool grp_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", py{ "Access to the Unix group database.\n\nGroup entries are reported as 4-tuples containing the following fields\nfrom the group database, in order:\n\n  gr_name   - name of the group\n  gr_passwd - group password (encrypted); often empty\n  gr_gid    - numeric ID of the group\n  gr_mem    - list of members\n\nThe gid is an integer, name and password are strings.  (Note that most\nusers are not explicitly listed as members of the groups they are in\naccording to the password database.  Check both databases to get\ncomplete membership information.)" });
                add("getgrgid", native("getgrgid", "grp", "Return the group database entry for the given numeric group ID.\n\nIf id is not valid, raise KeyError.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("getgrgid", { py_positional("id") }, aArgs, aKwargs);
                    if (!isinstance(a[0], builtin("int")))
                        py_raise("TypeError", "gid should be integer, not " + type_of(a[0]).attr("__name__").str());
                    auto const gid = index_of(a[0]);
                    std::vector<char> buffer(16384u);
                    struct group entry{};
                    struct group* found = nullptr;
                    gil_release();
                    while (::getgrgid_r(static_cast<gid_t>(gid), &entry, buffer.data(), buffer.size(), &found) == ERANGE)
                        buffer.resize(buffer.size() * 2u);
                    gil_acquire();
                    if (found == nullptr)
                        py_raise(builtin("KeyError")(py{ "getgrgid(): gid not found: " + std::to_string(gid) }));
                    return group_of(entry);
                }));
                add("getgrnam", native("getgrnam", "grp", "Return the group database entry for the given group name.\n\nIf name is not valid, raise KeyError.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("getgrnam", { py_positional("name") }, aArgs, aKwargs);
                    if (!isinstance(a[0], builtin("str")))
                        py_raise("TypeError", "getgrnam() argument must be str, not " + type_of(a[0]).attr("__name__").str());
                    auto const name = a[0].str();
                    if (name.find('\0') != std::string::npos)
                        py_raise("ValueError", "embedded null character");
                    std::vector<char> buffer(16384u);
                    struct group entry{};
                    struct group* found = nullptr;
                    gil_release();
                    while (::getgrnam_r(name.c_str(), &entry, buffer.data(), buffer.size(), &found) == ERANGE)
                        buffer.resize(buffer.size() * 2u);
                    gil_acquire();
                    if (found == nullptr)
                        py_raise(builtin("KeyError")(py{ "getgrnam(): name not found: " + a[0].repr() }));
                    return group_of(entry);
                }));
                add("getgrall", native("getgrall", "grp", "Return a list of all available group entries, in arbitrary order.\n\nAn entry whose name starts with '+' or '-' represents an instruction\nto use YP/NIS and may not be accessible via getgrnam or getgrgid.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py_bind("getgrall", {}, aArgs, aKwargs);
                    py const result = py_list();
                    ::setgrent();
                    while (struct group const* g = ::getgrent())
                        result.method("append", { group_of(*g) });
                    ::endgrent();
                    return result;
                }));
                group_type();
                add("struct_group", py{ type_object_for("struct_group", "grp.struct_group") });
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        // --- fcntl

        // (the argument of fcntl and ioctl that is a buffer (bytes-like or str (UTF-8)): its bytes; none if it is an int)
        bool buffer_argument(py const& aArg, std::string& aBuffer, bool& aMutable)
        {
            aMutable = false;
            if (isinstance(aArg, builtin("str")))
            {
                aBuffer = aArg.str();
                return true;
            }
            std::string_view data;
            if (!isinstance(aArg, builtin("int")) && bytes_like(aArg.get(), data))
            {
                aBuffer = std::string{ data };
                aMutable = !isinstance(aArg, builtin("bytes"));
                return true;
            }
            return false;
        }

        bool fcntl_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", py{ "This module performs file control and I/O control on file\ndescriptors.  It is an interface to the fcntl() and ioctl() Unix\nroutines.  File descriptors can be obtained with the fileno() method of\na file or socket object." });
                add("fcntl", native("fcntl", "fcntl", "Perform the operation cmd on file descriptor fd.\n\nThe values used for cmd are operating system dependent, and are\navailable as constants in the fcntl module, using the same names as used\nin the relevant C header files.  The argument arg is optional, and\ndefaults to 0; it may be an integer, a bytes-like object or a string.\nIf arg is given as a string, it will be encoded to binary using the\nUTF-8 encoding.\n\nIf the arg given is an integer or if none is specified, the result value\nis an integer corresponding to the return value of the fcntl() call in\nthe C code.\n\nIf arg is given as a bytes-like object, the return value of fcntl() is a\nbytes object of that length, containing the resulting value put in the\narg buffer by the operating system.  The length of the arg buffer is not\nallowed to exceed 1024 bytes.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                        py_raise("TypeError", "fcntl() takes no keyword arguments");
                    if (aArgs.size() < 2u || aArgs.size() > 3u)
                        py_raise("TypeError", "fcntl expected at least 2 arguments, got " + std::to_string(aArgs.size()));
                    int const fd = file_descriptor(aArgs[0]);
                    int const code = int_of(aArgs[1]);
                    std::string buffer;
                    bool mutableBuffer = false;
                    if (aArgs.size() == 3u && buffer_argument(aArgs[2], buffer, mutableBuffer))
                    {
                        if (buffer.size() > 1024u)
                            py_raise("ValueError", "fcntl argument 2 is too long");
                        char area[1024];
                        std::memcpy(area, buffer.data(), buffer.size());
                        int r = 0;
                        do
                        {
                            gil_release();
                            r = ::fcntl(fd, code, area);
                            gil_acquire();
                        } while (r < 0 && errno == EINTR);
                        if (r < 0)
                            raise_errno(errno);
                        return py{ make_bytes(std::string{ area, buffer.size() }) };
                    }
                    long long arg = 0;
                    if (aArgs.size() == 3u)
                    {
                        if (!isinstance(aArgs[2], builtin("int")))
                            py_raise("TypeError", "fcntl() argument 3 must be an integer, a bytes-like object, or a string, not " + type_of(aArgs[2]).attr("__name__").str());
                        arg = index_of(aArgs[2]);
                    }
                    int r = 0;
                    do
                    {
                        gil_release();
                        r = ::fcntl(fd, code, static_cast<long>(arg));
                        gil_acquire();
                    } while (r < 0 && errno == EINTR);
                    if (r < 0)
                        raise_errno(errno);
                    return py{ static_cast<long long>(r) };
                }));
                add("ioctl", native("ioctl", "fcntl", "Perform the operation request on file descriptor fd.\n\nThe values used for request are operating system dependent, and are\navailable as constants in the fcntl or termios library modules, using\nthe same names as used in the relevant C header files.\n\nThe argument arg is optional, and defaults to 0; it may be an integer, a\nbytes-like object or a string.  If arg is given as a string, it will be\nencoded to binary using the UTF-8 encoding.\n\nIf the arg given is an integer or if none is specified, the result value\nis an integer corresponding to the return value of the ioctl() call in\nthe C code.\n\nIf the argument is a mutable buffer (such as a bytearray) and the\nmutate_flag argument is true (default) then the buffer is (in effect)\npassed to the operating system and changes made by the OS will be\nreflected in the contents of the buffer after the call has returned.\nThe return value is the integer returned by the ioctl() system call.\n\nIf the argument is a mutable buffer and the mutable_flag argument is\nfalse, the behavior is as if an immutable buffer had been passed.\n\nIf the argument is an immutable buffer then a copy of the buffer is\npassed to the operating system and the return value is a bytes object of\nthe same length containing whatever the operating system put in the\nbuffer.  The length of the arg buffer in this case is not allowed to\nexceed 1024 bytes.",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                        py_raise("TypeError", "ioctl() takes no keyword arguments");
                    if (aArgs.size() < 2u || aArgs.size() > 4u)
                        py_raise("TypeError", "ioctl expected at least 2 arguments, got " + std::to_string(aArgs.size()));
                    int const fd = file_descriptor(aArgs[0]);
                    auto const request = static_cast<unsigned long>(index_of(aArgs[1]));
                    bool const mutateFlag = aArgs.size() < 4u || aArgs[3].truthy();
                    std::string buffer;
                    bool mutableBuffer = false;
                    if (aArgs.size() >= 3u && buffer_argument(aArgs[2], buffer, mutableBuffer))
                    {
                        if (mutableBuffer && mutateFlag)
                        {
                            // (the buffer, changed as the system changes it)
                            std::vector<char> area(std::max<std::size_t>(buffer.size(), 1024u) + 1u, '\0');
                            std::memcpy(area.data(), buffer.data(), buffer.size());
                            gil_release();
                            int const r = ::ioctl(fd, request, area.data());
                            gil_acquire();
                            if (r < 0)
                                raise_errno(errno);
                            ref const first{ make_int(0) };
                            ref const last{ make_int(static_cast<long long>(buffer.size())) };
                            ref const slice{ make_slice(first.get(), last.get(), nullptr) };
                            ref const value{ make_bytes(std::string{ area.data(), buffer.size() }) };
                            py_checked(setitem(aArgs[2].get(), slice.get(), value.get()) == 0);
                            return py{ static_cast<long long>(r) };
                        }
                        if (buffer.size() > 1024u)
                            py_raise("ValueError", "ioctl string arg too long");
                        char area[1025] = {};
                        std::memcpy(area, buffer.data(), buffer.size());
                        gil_release();
                        int const r = ::ioctl(fd, request, area);
                        gil_acquire();
                        if (r < 0)
                            raise_errno(errno);
                        return py{ make_bytes(std::string{ area, buffer.size() }) };
                    }
                    long long arg = 0;
                    if (aArgs.size() >= 3u)
                    {
                        if (!isinstance(aArgs[2], builtin("int")))
                            py_raise("TypeError", "ioctl() argument 3 must be an integer, a bytes-like object, or a string, not " + type_of(aArgs[2]).attr("__name__").str());
                        arg = index_of(aArgs[2]);
                    }
                    gil_release();
                    int const r = ::ioctl(fd, request, static_cast<long>(arg));
                    gil_acquire();
                    if (r < 0)
                        raise_errno(errno);
                    return py{ static_cast<long long>(r) };
                }));
                add("flock", native("flock", "fcntl", "Perform the lock operation on file descriptor fd.\n\nSee the Unix manual page for flock(2) for details (On some systems, this\nfunction is emulated using fcntl()).",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                        py_raise("TypeError", "flock() takes no keyword arguments");
                    if (aArgs.size() != 2u)
                        py_raise("TypeError", "flock expected 2 arguments, got " + std::to_string(aArgs.size()));
                    int const fd = file_descriptor(aArgs[0]);
                    int const operation = int_of(aArgs[1]);
                    int r = 0;
                    do
                    {
                        gil_release();
                        r = ::flock(fd, operation);
                        gil_acquire();
                    } while (r < 0 && errno == EINTR);
                    if (r < 0)
                        raise_errno(errno);
                    return py::none();
                }));
                add("lockf", native("lockf", "fcntl", "A wrapper around the fcntl() locking calls.\n\nfd is the file descriptor of the file to lock or unlock, and operation\nis one of the following values:\n\n    LOCK_UN - unlock\n    LOCK_SH - acquire a shared lock\n    LOCK_EX - acquire an exclusive lock\n\nWhen operation is LOCK_SH or LOCK_EX, it can also be bitwise ORed with\nLOCK_NB to avoid blocking on lock acquisition.  If LOCK_NB is used and\nthe lock cannot be acquired, an OSError will be raised and the exception\nwill have an errno attribute set to EACCES or EAGAIN (depending on the\noperating system -- for portability, check for either value).\n\nlen is the number of bytes to lock, with the default meaning to lock to\nEOF.  start is the byte offset, relative to whence, to that the lock\nstarts.  whence is as with fileobj.seek(), specifically:\n\n    0 - relative to the start of the file (SEEK_SET)\n    1 - relative to the current buffer position (SEEK_CUR)\n    2 - relative to the end of the file (SEEK_END)",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                        py_raise("TypeError", "lockf() takes no keyword arguments");
                    if (aArgs.size() < 2u || aArgs.size() > 5u)
                        py_raise("TypeError", "lockf expected at least 2 arguments, got " + std::to_string(aArgs.size()));
                    int const fd = file_descriptor(aArgs[0]);
                    int const code = int_of(aArgs[1]);
                    struct flock l{};
                    if (code == LOCK_UN)
                        l.l_type = F_UNLCK;
                    else if ((code & LOCK_SH) != 0)
                        l.l_type = F_RDLCK;
                    else if ((code & LOCK_EX) != 0)
                        l.l_type = F_WRLCK;
                    else
                        py_raise("ValueError", "unrecognized lockf argument");
                    l.l_len = aArgs.size() > 2u ? static_cast<off_t>(index_of(aArgs[2])) : 0;
                    l.l_start = aArgs.size() > 3u ? static_cast<off_t>(index_of(aArgs[3])) : 0;
                    l.l_whence = static_cast<short>(aArgs.size() > 4u ? int_of(aArgs[4]) : 0);
                    int r = 0;
                    do
                    {
                        gil_release();
                        r = ::fcntl(fd, (code & LOCK_NB) != 0 ? F_SETLK : F_SETLKW, &l);
                        gil_acquire();
                    } while (r < 0 && errno == EINTR);
                    if (r < 0)
                        raise_errno(errno);
                    return py::none();
                }));
                std::vector<std::pair<char const*, long long>> const constants = {
                    { "FD_CLOEXEC", FD_CLOEXEC }, { "F_DUPFD", F_DUPFD }, { "F_GETFD", F_GETFD }, { "F_SETFD", F_SETFD }, { "F_GETFL", F_GETFL },
                    { "F_SETFL", F_SETFL }, { "F_GETLK", F_GETLK }, { "F_SETLK", F_SETLK }, { "F_SETLKW", F_SETLKW }, { "F_GETOWN", F_GETOWN },
                    { "F_SETOWN", F_SETOWN }, { "F_RDLCK", F_RDLCK }, { "F_WRLCK", F_WRLCK }, { "F_UNLCK", F_UNLCK },
                    { "LOCK_SH", LOCK_SH }, { "LOCK_EX", LOCK_EX }, { "LOCK_NB", LOCK_NB }, { "LOCK_UN", LOCK_UN },
#if defined(F_DUPFD_CLOEXEC)
                    { "F_DUPFD_CLOEXEC", F_DUPFD_CLOEXEC },
#endif
#if defined(FASYNC)
                    { "FASYNC", FASYNC },
#endif
#if defined(__linux__)
                    { "F_GETSIG", F_GETSIG }, { "F_SETSIG", F_SETSIG }, { "F_NOTIFY", F_NOTIFY }, { "F_GETLEASE", F_GETLEASE }, { "F_SETLEASE", F_SETLEASE },
                    { "F_GETPIPE_SZ", F_GETPIPE_SZ }, { "F_SETPIPE_SZ", F_SETPIPE_SZ }, { "F_OFD_GETLK", F_OFD_GETLK }, { "F_OFD_SETLK", F_OFD_SETLK },
                    { "F_OFD_SETLKW", F_OFD_SETLKW }, { "F_ADD_SEALS", F_ADD_SEALS }, { "F_GET_SEALS", F_GET_SEALS }, { "F_SEAL_SEAL", F_SEAL_SEAL },
                    { "F_SEAL_SHRINK", F_SEAL_SHRINK }, { "F_SEAL_GROW", F_SEAL_GROW }, { "F_SEAL_WRITE", F_SEAL_WRITE },
                    { "DN_ACCESS", DN_ACCESS }, { "DN_MODIFY", DN_MODIFY }, { "DN_CREATE", DN_CREATE }, { "DN_DELETE", DN_DELETE }, { "DN_RENAME", DN_RENAME },
                    { "DN_ATTRIB", DN_ATTRIB }, { "DN_MULTISHOT", static_cast<long long>(static_cast<unsigned>(DN_MULTISHOT)) },
                    { "F_EXLCK", 4 }, { "F_SHLCK", 8 },
#endif
                };
                for (auto const& [name, value] : constants)
                    add(name, py{ value });
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sPwd = register_module("pwd", pwd_module);
        inline bool const sGrp = register_module("grp", grp_module);
        inline bool const sFcntl = register_module("fcntl", fcntl_module);
    }
}

#endif
