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

  Portions of this file are derived from CPython 3.13 (Modules/pwdmodule.c, Modules/grpmodule.c, Modules/fcntlmodule.c and
  Modules/termios.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the
  Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the
  neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules pwd, grp, fcntl and termios of Unix (CPython 3.13's Modules/pwdmodule.c, grpmodule.c, fcntlmodule.c and termios.c): the 
// password and group databases (their entries: struct sequences), fcntl, ioctl, flock and lockf of file descriptors (and their 
// constants), and terminals' attributes (tcgetattr, tcsetattr and their constants).

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
#include <termios.h>
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

        // --- termios

        // (termios.error: (errno, strerror))
        [[noreturn]] void termios_error(int aErrno)
        {
            if (!is_exception_class("termios.error"))
                user_exception_bases()["termios.error"] = { "Exception" };
            ref const exception{ make_exception("termios.error", { make_int(aErrno), make_str(std::strerror(aErrno)) }) };
            fn_raise(exception.get());
            throw raised{};
        }

        bool termios_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", py{ std::string{ "This module provides an interface to the Posix calls for tty I/O control.\nFor a complete description of these calls, see the Posix or Unix manual\npages. It is only available for those Unix versions that support Posix\ntermios style tty I/O control.\n\nAll functions in this module take a file descriptor fd as their first\nargument. This can be an integer file descriptor, such as returned by\nsys.stdin.fileno(), or a file object, such as sys.stdin itself." } });
                if (!is_exception_class("termios.error"))
                    user_exception_bases()["termios.error"] = { "Exception" };
                add("error", py{ exception_class("termios.error") });
                add("tcgetattr", native("tcgetattr", "termios", "Get the tty attributes for file descriptor fd.\n\nReturns a list [iflag, oflag, cflag, lflag, ispeed, ospeed, cc]\nwhere cc is a list of the tty special characters (each a string of\nlength 1, except the items with indices VMIN and VTIME, which are\nintegers when these fields are defined).  The interpretation of the\nflags and the speeds as well as the indexing in the cc array must be\ndone using the symbolic constants defined in this module.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcgetattr", { py_positional_only("fd") }, aArgs, aKwargs);
                    int const fd = file_descriptor(a[0]);
                    struct termios mode{};
                    if (::tcgetattr(fd, &mode) == -1)
                        termios_error(errno);
                    // (the control characters: bytes of length 1, but VMIN and VTIME (ints) in non-canonical mode)
                    std::vector<py> cc;
                    for (std::size_t i = 0u; i < NCCS; ++i)
                        cc.push_back(py{ make_bytes(std::string(1u, static_cast<char>(mode.c_cc[i]))) });
                    if ((mode.c_lflag & ICANON) == 0)
                    {
                        cc[VMIN] = py{ static_cast<long long>(mode.c_cc[VMIN]) };
                        cc[VTIME] = py{ static_cast<long long>(mode.c_cc[VTIME]) };
                    }
                    return py_list({ py{ static_cast<long long>(mode.c_iflag) }, py{ static_cast<long long>(mode.c_oflag) }, py{ static_cast<long long>(mode.c_cflag) },
                        py{ static_cast<long long>(mode.c_lflag) }, py{ static_cast<long long>(::cfgetispeed(&mode)) }, py{ static_cast<long long>(::cfgetospeed(&mode)) },
                        py_list(cc) });
                }));
                add("tcsetattr", native("tcsetattr", "termios", "Set the tty attributes for file descriptor fd.\n\nThe attributes to be set are taken from the attributes argument, which\nis a list like the one returned by tcgetattr(). The when argument\ndetermines when the attributes are changed: termios.TCSANOW to\nchange immediately, termios.TCSADRAIN to change after transmitting all\nqueued output, or termios.TCSAFLUSH to change after transmitting all\nqueued output and discarding all queued input.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcsetattr", { py_positional_only("fd"), py_positional_only("when"), py_positional_only("attributes") }, aArgs, aKwargs);
                    int const fd = file_descriptor(a[0]);
                    int const when = int_of(a[1]);
                    auto const& attributes = a[2];
                    if (!isinstance(attributes, builtin("list")) || index_of(builtin("len")(attributes)) != 7)
                        py_raise("TypeError", "tcsetattr, arg 3: must be 7 element list");
                    struct termios mode{};
                    if (::tcgetattr(fd, &mode) == -1)
                        termios_error(errno);
                    mode.c_iflag = static_cast<tcflag_t>(index_of(attributes[py{ 0 }]));
                    mode.c_oflag = static_cast<tcflag_t>(index_of(attributes[py{ 1 }]));
                    mode.c_cflag = static_cast<tcflag_t>(index_of(attributes[py{ 2 }]));
                    mode.c_lflag = static_cast<tcflag_t>(index_of(attributes[py{ 3 }]));
                    auto const ispeed = static_cast<speed_t>(index_of(attributes[py{ 4 }]));
                    auto const ospeed = static_cast<speed_t>(index_of(attributes[py{ 5 }]));
                    py const cc = attributes[py{ 6 }];
                    if (!isinstance(cc, builtin("list")) || index_of(builtin("len")(cc)) != NCCS)
                        py_raise("TypeError", "tcsetattr: attributes[6] must be " + std::to_string(NCCS) + " element list");
                    for (std::size_t i = 0u; i < NCCS; ++i)
                    {
                        py const v = cc[py{ static_cast<long long>(i) }];
                        std::string_view b;
                        if (isinstance(v, builtin("bytes")) && bytes_like(v.get(), b) && b.size() == 1u)
                            mode.c_cc[i] = static_cast<cc_t>(b[0]);
                        else if (isinstance(v, builtin("int")))
                            mode.c_cc[i] = static_cast<cc_t>(index_of(v));
                        else
                            py_raise("TypeError", "tcsetattr: elements of attributes must be characters or integers");
                    }
                    if (::cfsetispeed(&mode, ispeed) == -1 || ::cfsetospeed(&mode, ospeed) == -1 || ::tcsetattr(fd, when, &mode) == -1)
                        termios_error(errno);
                    return py::none();
                }));
                add("tcsendbreak", native("tcsendbreak", "termios", "Send a break on file descriptor fd.\n\nA zero duration sends a break for 0.25-0.5 seconds; a nonzero duration\nhas a system dependent meaning.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcsendbreak", { py_positional_only("fd"), py_positional_only("duration") }, aArgs, aKwargs);
                    if (::tcsendbreak(file_descriptor(a[0]), int_of(a[1])) == -1)
                        termios_error(errno);
                    return py::none();
                }));
                add("tcdrain", native("tcdrain", "termios", "Wait until all output written to file descriptor fd has been transmitted.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcdrain", { py_positional_only("fd") }, aArgs, aKwargs);
                    int const fd = file_descriptor(a[0]);
                    gil_release();
                    int const r = ::tcdrain(fd);
                    gil_acquire();
                    if (r == -1)
                        termios_error(errno);
                    return py::none();
                }));
                add("tcflush", native("tcflush", "termios", "Discard queued data on file descriptor fd.\n\nThe queue selector specifies which queue: termios.TCIFLUSH for the input\nqueue, termios.TCOFLUSH for the output queue, or termios.TCIOFLUSH for\nboth queues.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcflush", { py_positional_only("fd"), py_positional_only("queue") }, aArgs, aKwargs);
                    if (::tcflush(file_descriptor(a[0]), int_of(a[1])) == -1)
                        termios_error(errno);
                    return py::none();
                }));
                add("tcflow", native("tcflow", "termios", "Suspend or resume input or output on file descriptor fd.\n\nThe action argument can be termios.TCOOFF to suspend output,\ntermios.TCOON to restart output, termios.TCIOFF to suspend input,\nor termios.TCION to restart input.", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcflow", { py_positional_only("fd"), py_positional_only("action") }, aArgs, aKwargs);
                    if (::tcflow(file_descriptor(a[0]), int_of(a[1])) == -1)
                        termios_error(errno);
                    return py::none();
                }));
                add("tcgetwinsize", native("tcgetwinsize", "termios", "Get the tty winsize for file descriptor fd.\n\nReturns a tuple (ws_row, ws_col).", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcgetwinsize", { py_positional_only("fd") }, aArgs, aKwargs);
                    struct winsize w{};
                    if (::ioctl(file_descriptor(a[0]), TIOCGWINSZ, &w) == -1)
                        termios_error(errno);
                    return py_tuple({ py{ static_cast<long long>(w.ws_row) }, py{ static_cast<long long>(w.ws_col) } });
                }));
                add("tcsetwinsize", native("tcsetwinsize", "termios", "Set the tty winsize for file descriptor fd.\n\nThe winsize to be set is taken from the winsize argument, which\nis a two-item tuple (ws_row, ws_col) like the one returned by\ntcgetwinsize().", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("tcsetwinsize", { py_positional_only("fd"), py_positional_only("winsize") }, aArgs, aKwargs);
                    int const fd = file_descriptor(a[0]);
                    py const size = a[1];
                    if (!builtin("hasattr")(size, py{ "__len__" }).truthy() || index_of(builtin("len")(size)) != 2)
                        py_raise("TypeError", "tcsetwinsize, arg 2: must be a two-item sequence");
                    struct winsize w{};
                    if (::ioctl(fd, TIOCGWINSZ, &w) == -1)
                        termios_error(errno);
                    auto const rows = index_of(size[py{ 0 }]);
                    auto const columns = index_of(size[py{ 1 }]);
                    if (rows < 0 || rows > USHRT_MAX || columns < 0 || columns > USHRT_MAX)
                        py_raise("OverflowError", "winsize value(s) out of range.");
                    w.ws_row = static_cast<unsigned short>(rows);
                    w.ws_col = static_cast<unsigned short>(columns);
                    if (::ioctl(fd, TIOCSWINSZ, &w) == -1)
                        termios_error(errno);
                    return py::none();
                }));
                std::vector<std::pair<char const*, long long>> const constants = {
#if defined(B0)
                    { "B0", static_cast<long long>(B0) },
#endif
#if defined(B1000000)
                    { "B1000000", static_cast<long long>(B1000000) },
#endif
#if defined(B110)
                    { "B110", static_cast<long long>(B110) },
#endif
#if defined(B115200)
                    { "B115200", static_cast<long long>(B115200) },
#endif
#if defined(B1152000)
                    { "B1152000", static_cast<long long>(B1152000) },
#endif
#if defined(B1200)
                    { "B1200", static_cast<long long>(B1200) },
#endif
#if defined(B134)
                    { "B134", static_cast<long long>(B134) },
#endif
#if defined(B150)
                    { "B150", static_cast<long long>(B150) },
#endif
#if defined(B1500000)
                    { "B1500000", static_cast<long long>(B1500000) },
#endif
#if defined(B1800)
                    { "B1800", static_cast<long long>(B1800) },
#endif
#if defined(B19200)
                    { "B19200", static_cast<long long>(B19200) },
#endif
#if defined(B200)
                    { "B200", static_cast<long long>(B200) },
#endif
#if defined(B2000000)
                    { "B2000000", static_cast<long long>(B2000000) },
#endif
#if defined(B230400)
                    { "B230400", static_cast<long long>(B230400) },
#endif
#if defined(B2400)
                    { "B2400", static_cast<long long>(B2400) },
#endif
#if defined(B2500000)
                    { "B2500000", static_cast<long long>(B2500000) },
#endif
#if defined(B300)
                    { "B300", static_cast<long long>(B300) },
#endif
#if defined(B3000000)
                    { "B3000000", static_cast<long long>(B3000000) },
#endif
#if defined(B3500000)
                    { "B3500000", static_cast<long long>(B3500000) },
#endif
#if defined(B38400)
                    { "B38400", static_cast<long long>(B38400) },
#endif
#if defined(B4000000)
                    { "B4000000", static_cast<long long>(B4000000) },
#endif
#if defined(B460800)
                    { "B460800", static_cast<long long>(B460800) },
#endif
#if defined(B4800)
                    { "B4800", static_cast<long long>(B4800) },
#endif
#if defined(B50)
                    { "B50", static_cast<long long>(B50) },
#endif
#if defined(B500000)
                    { "B500000", static_cast<long long>(B500000) },
#endif
#if defined(B57600)
                    { "B57600", static_cast<long long>(B57600) },
#endif
#if defined(B576000)
                    { "B576000", static_cast<long long>(B576000) },
#endif
#if defined(B600)
                    { "B600", static_cast<long long>(B600) },
#endif
#if defined(B75)
                    { "B75", static_cast<long long>(B75) },
#endif
#if defined(B921600)
                    { "B921600", static_cast<long long>(B921600) },
#endif
#if defined(B9600)
                    { "B9600", static_cast<long long>(B9600) },
#endif
#if defined(BRKINT)
                    { "BRKINT", static_cast<long long>(BRKINT) },
#endif
#if defined(BS0)
                    { "BS0", static_cast<long long>(BS0) },
#endif
#if defined(BS1)
                    { "BS1", static_cast<long long>(BS1) },
#endif
#if defined(BSDLY)
                    { "BSDLY", static_cast<long long>(BSDLY) },
#endif
#if defined(CBAUD)
                    { "CBAUD", static_cast<long long>(CBAUD) },
#endif
#if defined(CBAUDEX)
                    { "CBAUDEX", static_cast<long long>(CBAUDEX) },
#endif
#if defined(CDSUSP)
                    { "CDSUSP", static_cast<long long>(CDSUSP) },
#endif
#if defined(CEOF)
                    { "CEOF", static_cast<long long>(CEOF) },
#endif
#if defined(CEOL)
                    { "CEOL", static_cast<long long>(CEOL) },
#endif
#if defined(CEOT)
                    { "CEOT", static_cast<long long>(CEOT) },
#endif
#if defined(CERASE)
                    { "CERASE", static_cast<long long>(CERASE) },
#endif
#if defined(CFLUSH)
                    { "CFLUSH", static_cast<long long>(CFLUSH) },
#endif
#if defined(CIBAUD)
                    { "CIBAUD", static_cast<long long>(CIBAUD) },
#endif
#if defined(CINTR)
                    { "CINTR", static_cast<long long>(CINTR) },
#endif
#if defined(CKILL)
                    { "CKILL", static_cast<long long>(CKILL) },
#endif
#if defined(CLNEXT)
                    { "CLNEXT", static_cast<long long>(CLNEXT) },
#endif
#if defined(CLOCAL)
                    { "CLOCAL", static_cast<long long>(CLOCAL) },
#endif
#if defined(CQUIT)
                    { "CQUIT", static_cast<long long>(CQUIT) },
#endif
#if defined(CR0)
                    { "CR0", static_cast<long long>(CR0) },
#endif
#if defined(CR1)
                    { "CR1", static_cast<long long>(CR1) },
#endif
#if defined(CR2)
                    { "CR2", static_cast<long long>(CR2) },
#endif
#if defined(CR3)
                    { "CR3", static_cast<long long>(CR3) },
#endif
#if defined(CRDLY)
                    { "CRDLY", static_cast<long long>(CRDLY) },
#endif
#if defined(CREAD)
                    { "CREAD", static_cast<long long>(CREAD) },
#endif
#if defined(CRPRNT)
                    { "CRPRNT", static_cast<long long>(CRPRNT) },
#endif
#if defined(CRTSCTS)
                    { "CRTSCTS", static_cast<long long>(CRTSCTS) },
#endif
#if defined(CS5)
                    { "CS5", static_cast<long long>(CS5) },
#endif
#if defined(CS6)
                    { "CS6", static_cast<long long>(CS6) },
#endif
#if defined(CS7)
                    { "CS7", static_cast<long long>(CS7) },
#endif
#if defined(CS8)
                    { "CS8", static_cast<long long>(CS8) },
#endif
#if defined(CSIZE)
                    { "CSIZE", static_cast<long long>(CSIZE) },
#endif
#if defined(CSTART)
                    { "CSTART", static_cast<long long>(CSTART) },
#endif
#if defined(CSTOP)
                    { "CSTOP", static_cast<long long>(CSTOP) },
#endif
#if defined(CSTOPB)
                    { "CSTOPB", static_cast<long long>(CSTOPB) },
#endif
#if defined(CSUSP)
                    { "CSUSP", static_cast<long long>(CSUSP) },
#endif
#if defined(CWERASE)
                    { "CWERASE", static_cast<long long>(CWERASE) },
#endif
#if defined(ECHO)
                    { "ECHO", static_cast<long long>(ECHO) },
#endif
#if defined(ECHOCTL)
                    { "ECHOCTL", static_cast<long long>(ECHOCTL) },
#endif
#if defined(ECHOE)
                    { "ECHOE", static_cast<long long>(ECHOE) },
#endif
#if defined(ECHOK)
                    { "ECHOK", static_cast<long long>(ECHOK) },
#endif
#if defined(ECHOKE)
                    { "ECHOKE", static_cast<long long>(ECHOKE) },
#endif
#if defined(ECHONL)
                    { "ECHONL", static_cast<long long>(ECHONL) },
#endif
#if defined(ECHOPRT)
                    { "ECHOPRT", static_cast<long long>(ECHOPRT) },
#endif
#if defined(EXTA)
                    { "EXTA", static_cast<long long>(EXTA) },
#endif
#if defined(EXTB)
                    { "EXTB", static_cast<long long>(EXTB) },
#endif
#if defined(EXTPROC)
                    { "EXTPROC", static_cast<long long>(EXTPROC) },
#endif
#if defined(FF0)
                    { "FF0", static_cast<long long>(FF0) },
#endif
#if defined(FF1)
                    { "FF1", static_cast<long long>(FF1) },
#endif
#if defined(FFDLY)
                    { "FFDLY", static_cast<long long>(FFDLY) },
#endif
#if defined(FIOASYNC)
                    { "FIOASYNC", static_cast<long long>(FIOASYNC) },
#endif
#if defined(FIOCLEX)
                    { "FIOCLEX", static_cast<long long>(FIOCLEX) },
#endif
#if defined(FIONBIO)
                    { "FIONBIO", static_cast<long long>(FIONBIO) },
#endif
#if defined(FIONCLEX)
                    { "FIONCLEX", static_cast<long long>(FIONCLEX) },
#endif
#if defined(FIONREAD)
                    { "FIONREAD", static_cast<long long>(FIONREAD) },
#endif
#if defined(FLUSHO)
                    { "FLUSHO", static_cast<long long>(FLUSHO) },
#endif
#if defined(HUPCL)
                    { "HUPCL", static_cast<long long>(HUPCL) },
#endif
#if defined(ICANON)
                    { "ICANON", static_cast<long long>(ICANON) },
#endif
#if defined(ICRNL)
                    { "ICRNL", static_cast<long long>(ICRNL) },
#endif
#if defined(IEXTEN)
                    { "IEXTEN", static_cast<long long>(IEXTEN) },
#endif
#if defined(IGNBRK)
                    { "IGNBRK", static_cast<long long>(IGNBRK) },
#endif
#if defined(IGNCR)
                    { "IGNCR", static_cast<long long>(IGNCR) },
#endif
#if defined(IGNPAR)
                    { "IGNPAR", static_cast<long long>(IGNPAR) },
#endif
#if defined(IMAXBEL)
                    { "IMAXBEL", static_cast<long long>(IMAXBEL) },
#endif
#if defined(INLCR)
                    { "INLCR", static_cast<long long>(INLCR) },
#endif
#if defined(INPCK)
                    { "INPCK", static_cast<long long>(INPCK) },
#endif
#if defined(IOCSIZE_MASK)
                    { "IOCSIZE_MASK", static_cast<long long>(IOCSIZE_MASK) },
#endif
#if defined(IOCSIZE_SHIFT)
                    { "IOCSIZE_SHIFT", static_cast<long long>(IOCSIZE_SHIFT) },
#endif
#if defined(ISIG)
                    { "ISIG", static_cast<long long>(ISIG) },
#endif
#if defined(ISTRIP)
                    { "ISTRIP", static_cast<long long>(ISTRIP) },
#endif
#if defined(IUCLC)
                    { "IUCLC", static_cast<long long>(IUCLC) },
#endif
#if defined(IUTF8)
                    { "IUTF8", static_cast<long long>(IUTF8) },
#endif
#if defined(IXANY)
                    { "IXANY", static_cast<long long>(IXANY) },
#endif
#if defined(IXOFF)
                    { "IXOFF", static_cast<long long>(IXOFF) },
#endif
#if defined(IXON)
                    { "IXON", static_cast<long long>(IXON) },
#endif
#if defined(NCC)
                    { "NCC", static_cast<long long>(NCC) },
#endif
#if defined(NCCS)
                    { "NCCS", static_cast<long long>(NCCS) },
#endif
#if defined(NL0)
                    { "NL0", static_cast<long long>(NL0) },
#endif
#if defined(NL1)
                    { "NL1", static_cast<long long>(NL1) },
#endif
#if defined(NLDLY)
                    { "NLDLY", static_cast<long long>(NLDLY) },
#endif
#if defined(NOFLSH)
                    { "NOFLSH", static_cast<long long>(NOFLSH) },
#endif
#if defined(N_MOUSE)
                    { "N_MOUSE", static_cast<long long>(N_MOUSE) },
#endif
#if defined(N_PPP)
                    { "N_PPP", static_cast<long long>(N_PPP) },
#endif
#if defined(N_SLIP)
                    { "N_SLIP", static_cast<long long>(N_SLIP) },
#endif
#if defined(N_STRIP)
                    { "N_STRIP", static_cast<long long>(N_STRIP) },
#endif
#if defined(N_TTY)
                    { "N_TTY", static_cast<long long>(N_TTY) },
#endif
#if defined(OCRNL)
                    { "OCRNL", static_cast<long long>(OCRNL) },
#endif
#if defined(OFDEL)
                    { "OFDEL", static_cast<long long>(OFDEL) },
#endif
#if defined(OFILL)
                    { "OFILL", static_cast<long long>(OFILL) },
#endif
#if defined(OLCUC)
                    { "OLCUC", static_cast<long long>(OLCUC) },
#endif
#if defined(ONLCR)
                    { "ONLCR", static_cast<long long>(ONLCR) },
#endif
#if defined(ONLRET)
                    { "ONLRET", static_cast<long long>(ONLRET) },
#endif
#if defined(ONOCR)
                    { "ONOCR", static_cast<long long>(ONOCR) },
#endif
#if defined(OPOST)
                    { "OPOST", static_cast<long long>(OPOST) },
#endif
#if defined(PARENB)
                    { "PARENB", static_cast<long long>(PARENB) },
#endif
#if defined(PARMRK)
                    { "PARMRK", static_cast<long long>(PARMRK) },
#endif
#if defined(PARODD)
                    { "PARODD", static_cast<long long>(PARODD) },
#endif
#if defined(PENDIN)
                    { "PENDIN", static_cast<long long>(PENDIN) },
#endif
#if defined(TAB0)
                    { "TAB0", static_cast<long long>(TAB0) },
#endif
#if defined(TAB1)
                    { "TAB1", static_cast<long long>(TAB1) },
#endif
#if defined(TAB2)
                    { "TAB2", static_cast<long long>(TAB2) },
#endif
#if defined(TAB3)
                    { "TAB3", static_cast<long long>(TAB3) },
#endif
#if defined(TABDLY)
                    { "TABDLY", static_cast<long long>(TABDLY) },
#endif
#if defined(TCFLSH)
                    { "TCFLSH", static_cast<long long>(TCFLSH) },
#endif
#if defined(TCGETA)
                    { "TCGETA", static_cast<long long>(TCGETA) },
#endif
#if defined(TCGETS)
                    { "TCGETS", static_cast<long long>(TCGETS) },
#endif
#if defined(TCIFLUSH)
                    { "TCIFLUSH", static_cast<long long>(TCIFLUSH) },
#endif
#if defined(TCIOFF)
                    { "TCIOFF", static_cast<long long>(TCIOFF) },
#endif
#if defined(TCIOFLUSH)
                    { "TCIOFLUSH", static_cast<long long>(TCIOFLUSH) },
#endif
#if defined(TCION)
                    { "TCION", static_cast<long long>(TCION) },
#endif
#if defined(TCOFLUSH)
                    { "TCOFLUSH", static_cast<long long>(TCOFLUSH) },
#endif
#if defined(TCOOFF)
                    { "TCOOFF", static_cast<long long>(TCOOFF) },
#endif
#if defined(TCOON)
                    { "TCOON", static_cast<long long>(TCOON) },
#endif
#if defined(TCSADRAIN)
                    { "TCSADRAIN", static_cast<long long>(TCSADRAIN) },
#endif
#if defined(TCSAFLUSH)
                    { "TCSAFLUSH", static_cast<long long>(TCSAFLUSH) },
#endif
#if defined(TCSANOW)
                    { "TCSANOW", static_cast<long long>(TCSANOW) },
#endif
#if defined(TCSBRK)
                    { "TCSBRK", static_cast<long long>(TCSBRK) },
#endif
#if defined(TCSBRKP)
                    { "TCSBRKP", static_cast<long long>(TCSBRKP) },
#endif
#if defined(TCSETA)
                    { "TCSETA", static_cast<long long>(TCSETA) },
#endif
#if defined(TCSETAF)
                    { "TCSETAF", static_cast<long long>(TCSETAF) },
#endif
#if defined(TCSETAW)
                    { "TCSETAW", static_cast<long long>(TCSETAW) },
#endif
#if defined(TCSETS)
                    { "TCSETS", static_cast<long long>(TCSETS) },
#endif
#if defined(TCSETSF)
                    { "TCSETSF", static_cast<long long>(TCSETSF) },
#endif
#if defined(TCSETSW)
                    { "TCSETSW", static_cast<long long>(TCSETSW) },
#endif
#if defined(TCXONC)
                    { "TCXONC", static_cast<long long>(TCXONC) },
#endif
#if defined(TIOCCONS)
                    { "TIOCCONS", static_cast<long long>(TIOCCONS) },
#endif
#if defined(TIOCEXCL)
                    { "TIOCEXCL", static_cast<long long>(TIOCEXCL) },
#endif
#if defined(TIOCGETD)
                    { "TIOCGETD", static_cast<long long>(TIOCGETD) },
#endif
#if defined(TIOCGICOUNT)
                    { "TIOCGICOUNT", static_cast<long long>(TIOCGICOUNT) },
#endif
#if defined(TIOCGLCKTRMIOS)
                    { "TIOCGLCKTRMIOS", static_cast<long long>(TIOCGLCKTRMIOS) },
#endif
#if defined(TIOCGPGRP)
                    { "TIOCGPGRP", static_cast<long long>(TIOCGPGRP) },
#endif
#if defined(TIOCGSERIAL)
                    { "TIOCGSERIAL", static_cast<long long>(TIOCGSERIAL) },
#endif
#if defined(TIOCGSOFTCAR)
                    { "TIOCGSOFTCAR", static_cast<long long>(TIOCGSOFTCAR) },
#endif
#if defined(TIOCGWINSZ)
                    { "TIOCGWINSZ", static_cast<long long>(TIOCGWINSZ) },
#endif
#if defined(TIOCINQ)
                    { "TIOCINQ", static_cast<long long>(TIOCINQ) },
#endif
#if defined(TIOCLINUX)
                    { "TIOCLINUX", static_cast<long long>(TIOCLINUX) },
#endif
#if defined(TIOCMBIC)
                    { "TIOCMBIC", static_cast<long long>(TIOCMBIC) },
#endif
#if defined(TIOCMBIS)
                    { "TIOCMBIS", static_cast<long long>(TIOCMBIS) },
#endif
#if defined(TIOCMGET)
                    { "TIOCMGET", static_cast<long long>(TIOCMGET) },
#endif
#if defined(TIOCMIWAIT)
                    { "TIOCMIWAIT", static_cast<long long>(TIOCMIWAIT) },
#endif
#if defined(TIOCMSET)
                    { "TIOCMSET", static_cast<long long>(TIOCMSET) },
#endif
#if defined(TIOCM_CAR)
                    { "TIOCM_CAR", static_cast<long long>(TIOCM_CAR) },
#endif
#if defined(TIOCM_CD)
                    { "TIOCM_CD", static_cast<long long>(TIOCM_CD) },
#endif
#if defined(TIOCM_CTS)
                    { "TIOCM_CTS", static_cast<long long>(TIOCM_CTS) },
#endif
#if defined(TIOCM_DSR)
                    { "TIOCM_DSR", static_cast<long long>(TIOCM_DSR) },
#endif
#if defined(TIOCM_DTR)
                    { "TIOCM_DTR", static_cast<long long>(TIOCM_DTR) },
#endif
#if defined(TIOCM_LE)
                    { "TIOCM_LE", static_cast<long long>(TIOCM_LE) },
#endif
#if defined(TIOCM_RI)
                    { "TIOCM_RI", static_cast<long long>(TIOCM_RI) },
#endif
#if defined(TIOCM_RNG)
                    { "TIOCM_RNG", static_cast<long long>(TIOCM_RNG) },
#endif
#if defined(TIOCM_RTS)
                    { "TIOCM_RTS", static_cast<long long>(TIOCM_RTS) },
#endif
#if defined(TIOCM_SR)
                    { "TIOCM_SR", static_cast<long long>(TIOCM_SR) },
#endif
#if defined(TIOCM_ST)
                    { "TIOCM_ST", static_cast<long long>(TIOCM_ST) },
#endif
#if defined(TIOCNOTTY)
                    { "TIOCNOTTY", static_cast<long long>(TIOCNOTTY) },
#endif
#if defined(TIOCNXCL)
                    { "TIOCNXCL", static_cast<long long>(TIOCNXCL) },
#endif
#if defined(TIOCOUTQ)
                    { "TIOCOUTQ", static_cast<long long>(TIOCOUTQ) },
#endif
#if defined(TIOCPKT)
                    { "TIOCPKT", static_cast<long long>(TIOCPKT) },
#endif
#if defined(TIOCPKT_DATA)
                    { "TIOCPKT_DATA", static_cast<long long>(TIOCPKT_DATA) },
#endif
#if defined(TIOCPKT_DOSTOP)
                    { "TIOCPKT_DOSTOP", static_cast<long long>(TIOCPKT_DOSTOP) },
#endif
#if defined(TIOCPKT_FLUSHREAD)
                    { "TIOCPKT_FLUSHREAD", static_cast<long long>(TIOCPKT_FLUSHREAD) },
#endif
#if defined(TIOCPKT_FLUSHWRITE)
                    { "TIOCPKT_FLUSHWRITE", static_cast<long long>(TIOCPKT_FLUSHWRITE) },
#endif
#if defined(TIOCPKT_NOSTOP)
                    { "TIOCPKT_NOSTOP", static_cast<long long>(TIOCPKT_NOSTOP) },
#endif
#if defined(TIOCPKT_START)
                    { "TIOCPKT_START", static_cast<long long>(TIOCPKT_START) },
#endif
#if defined(TIOCPKT_STOP)
                    { "TIOCPKT_STOP", static_cast<long long>(TIOCPKT_STOP) },
#endif
#if defined(TIOCSCTTY)
                    { "TIOCSCTTY", static_cast<long long>(TIOCSCTTY) },
#endif
#if defined(TIOCSERCONFIG)
                    { "TIOCSERCONFIG", static_cast<long long>(TIOCSERCONFIG) },
#endif
#if defined(TIOCSERGETLSR)
                    { "TIOCSERGETLSR", static_cast<long long>(TIOCSERGETLSR) },
#endif
#if defined(TIOCSERGETMULTI)
                    { "TIOCSERGETMULTI", static_cast<long long>(TIOCSERGETMULTI) },
#endif
#if defined(TIOCSERGSTRUCT)
                    { "TIOCSERGSTRUCT", static_cast<long long>(TIOCSERGSTRUCT) },
#endif
#if defined(TIOCSERGWILD)
                    { "TIOCSERGWILD", static_cast<long long>(TIOCSERGWILD) },
#endif
#if defined(TIOCSERSETMULTI)
                    { "TIOCSERSETMULTI", static_cast<long long>(TIOCSERSETMULTI) },
#endif
#if defined(TIOCSERSWILD)
                    { "TIOCSERSWILD", static_cast<long long>(TIOCSERSWILD) },
#endif
#if defined(TIOCSER_TEMT)
                    { "TIOCSER_TEMT", static_cast<long long>(TIOCSER_TEMT) },
#endif
#if defined(TIOCSETD)
                    { "TIOCSETD", static_cast<long long>(TIOCSETD) },
#endif
#if defined(TIOCSLCKTRMIOS)
                    { "TIOCSLCKTRMIOS", static_cast<long long>(TIOCSLCKTRMIOS) },
#endif
#if defined(TIOCSPGRP)
                    { "TIOCSPGRP", static_cast<long long>(TIOCSPGRP) },
#endif
#if defined(TIOCSSERIAL)
                    { "TIOCSSERIAL", static_cast<long long>(TIOCSSERIAL) },
#endif
#if defined(TIOCSSOFTCAR)
                    { "TIOCSSOFTCAR", static_cast<long long>(TIOCSSOFTCAR) },
#endif
#if defined(TIOCSTI)
                    { "TIOCSTI", static_cast<long long>(TIOCSTI) },
#endif
#if defined(TIOCSWINSZ)
                    { "TIOCSWINSZ", static_cast<long long>(TIOCSWINSZ) },
#endif
#if defined(TOSTOP)
                    { "TOSTOP", static_cast<long long>(TOSTOP) },
#endif
#if defined(VDISCARD)
                    { "VDISCARD", static_cast<long long>(VDISCARD) },
#endif
#if defined(VEOF)
                    { "VEOF", static_cast<long long>(VEOF) },
#endif
#if defined(VEOL)
                    { "VEOL", static_cast<long long>(VEOL) },
#endif
#if defined(VEOL2)
                    { "VEOL2", static_cast<long long>(VEOL2) },
#endif
#if defined(VERASE)
                    { "VERASE", static_cast<long long>(VERASE) },
#endif
#if defined(VINTR)
                    { "VINTR", static_cast<long long>(VINTR) },
#endif
#if defined(VKILL)
                    { "VKILL", static_cast<long long>(VKILL) },
#endif
#if defined(VLNEXT)
                    { "VLNEXT", static_cast<long long>(VLNEXT) },
#endif
#if defined(VMIN)
                    { "VMIN", static_cast<long long>(VMIN) },
#endif
#if defined(VQUIT)
                    { "VQUIT", static_cast<long long>(VQUIT) },
#endif
#if defined(VREPRINT)
                    { "VREPRINT", static_cast<long long>(VREPRINT) },
#endif
#if defined(VSTART)
                    { "VSTART", static_cast<long long>(VSTART) },
#endif
#if defined(VSTOP)
                    { "VSTOP", static_cast<long long>(VSTOP) },
#endif
#if defined(VSUSP)
                    { "VSUSP", static_cast<long long>(VSUSP) },
#endif
#if defined(VSWTC)
                    { "VSWTC", static_cast<long long>(VSWTC) },
#endif
#if defined(VSWTCH)
                    { "VSWTCH", static_cast<long long>(VSWTCH) },
#endif
#if defined(VT0)
                    { "VT0", static_cast<long long>(VT0) },
#endif
#if defined(VT1)
                    { "VT1", static_cast<long long>(VT1) },
#endif
#if defined(VTDLY)
                    { "VTDLY", static_cast<long long>(VTDLY) },
#endif
#if defined(VTIME)
                    { "VTIME", static_cast<long long>(VTIME) },
#endif
#if defined(VWERASE)
                    { "VWERASE", static_cast<long long>(VWERASE) },
#endif
#if defined(XCASE)
                    { "XCASE", static_cast<long long>(XCASE) },
#endif
#if defined(XTABS)
                    { "XTABS", static_cast<long long>(XTABS) },
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
        inline bool const sTermios = register_module("termios", termios_module);
    }
}

#endif
