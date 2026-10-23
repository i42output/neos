/*
  posixsubprocess.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_posixsubprocess.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its object
  model (fork only: no vfork) (see runtime/python/NOTICE).
*/

// The module _posixsubprocess (CPython 3.13's Modules/_posixsubprocess.c): fork_exec, which subprocess.Popen calls (POSIX) to make
// its child: the child's file descriptors are set up (its standard ones duplicated from the pipes, the others closed if close_fds),
// its directory, umask, signals, session, process group, groups and ids set, preexec_fn called, and its program executed; an error
// in the child (before its program runs) is written to errpipe_write ("OSError:<errno in hex>:<noexec...>" or "SubprocessError:0:
// <message>"), for the parent (subprocess.py) to raise.

#if !defined(_WIN32)

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <grp.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        // (the child's work (as CPython's child_exec): only async-signal-safe calls, apart from preexec_fn's (the parent made its
        // arguments, so that the child allocates nothing))
        struct child_arguments
        {
            std::vector<char*> executables;     ///< (null terminated)
            std::vector<char*> argv;            ///< (null terminated; empty: none)
            std::vector<char*> envp;            ///< (null terminated; empty: none (the parent's environment))
            char const* cwd = nullptr;
            int p2cread = -1, p2cwrite = -1, c2pread = -1, c2pwrite = -1, errread = -1, errwrite = -1, errpipeRead = -1, errpipeWrite = -1;
            bool closeFds = false;
            bool restoreSignals = false;
            bool callSetsid = false;
            pid_t pgidToSet = -1;
            gid_t gid = static_cast<gid_t>(-1);
            bool setGroups = false;
            std::vector<gid_t> extraGroups;
            uid_t uid = static_cast<uid_t>(-1);
            int childUmask = -1;
            std::vector<int> fdsToKeep;         ///< (sorted)
            object* preexecFn = nullptr;        ///< (borrowed; null: none)
        };

        void write_noraise(int aFd, char const* aData, std::size_t aSize)
        {
            while (aSize > 0u)
            {
                auto const n = ::write(aFd, aData, aSize);
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    return;
                }
                aData += n;
                aSize -= static_cast<std::size_t>(n);
            }
        }

        // (_Py_set_inheritable_async_safe)
        bool set_inheritable(int aFd, bool aInheritable)
        {
            auto const flags = ::fcntl(aFd, F_GETFD);
            if (flags < 0)
                return false;
            auto const newFlags = aInheritable ? (flags & ~FD_CLOEXEC) : (flags | FD_CLOEXEC);
            return newFlags == flags || ::fcntl(aFd, F_SETFD, newFlags) == 0;
        }

        bool kept(int aFd, std::vector<int> const& aFdsToKeep)
        {
            std::size_t low = 0u;
            std::size_t high = aFdsToKeep.size();
            while (low < high)
            {
                auto const middle = (low + high) / 2u;
                if (aFdsToKeep[middle] == aFd)
                    return true;
                if (aFdsToKeep[middle] < aFd)
                    low = middle + 1u;
                else
                    high = middle;
            }
            return false;
        }

        // (_close_open_fds: the file descriptors from aStart that are not kept, closed (close_range between those kept, if the system
        // has it; else each up to the limit))
        void close_open_fds(int aStart, std::vector<int> const& aFdsToKeep)
        {
#if defined(__linux__) && defined(SYS_close_range)
            {
                bool supported = true;
                int from = aStart;
                for (auto const k : aFdsToKeep)
                {
                    if (k < from)
                        continue;
                    if (k > from && ::syscall(SYS_close_range, static_cast<unsigned>(from), static_cast<unsigned>(k - 1), 0u) != 0)
                    {
                        supported = false;
                        break;
                    }
                    from = k + 1;
                }
                if (supported && ::syscall(SYS_close_range, static_cast<unsigned>(from), ~0u, 0u) == 0)
                    return;
            }
#endif
            long limit = ::sysconf(_SC_OPEN_MAX);
            if (limit < 0 || limit > INT_MAX)
                limit = 256;
            for (int fd = aStart; fd < static_cast<int>(limit); ++fd)
                if (!kept(fd, aFdsToKeep))
                    ::close(fd);
        }

        [[noreturn]] void child_exec(child_arguments const& c)
        {
            int savedErrno = 0;
            // (an error before exec)
            char const* errMsg = "noexec";
            int c2pwrite = c.c2pwrite;
            int errwrite = c.errwrite;
            auto const report = [&]()
            {
                // (the error, for the parent: written to errpipe_write (its total is less than PIPE_BUF; errors writing it are ignored))
                savedErrno = errno;
                if (savedErrno != 0)
                {
                    char hexErrno[sizeof(savedErrno) * 2 + 1];
                    char* cur = hexErrno + sizeof(hexErrno);
                    write_noraise(c.errpipeWrite, "OSError:", 8u);
                    while (savedErrno != 0 && cur != hexErrno)
                    {
                        *--cur = "0123456789abcdef"[savedErrno % 16];
                        savedErrno /= 16;
                    }
                    write_noraise(c.errpipeWrite, cur, static_cast<std::size_t>(hexErrno + sizeof(hexErrno) - cur));
                    write_noraise(c.errpipeWrite, ":", 1u);
                }
                else
                    write_noraise(c.errpipeWrite, "SubprocessError:0:", 18u);
                write_noraise(c.errpipeWrite, errMsg, std::strlen(errMsg));
                ::_exit(255);
            };
#define NEOS_POSIX_CALL(call) do { if ((call) == -1) report(); } while (0)
            // (the file descriptors kept: inheritable, but for errpipe_write)
            for (auto const fd : c.fdsToKeep)
                if (fd != c.errpipeWrite && !set_inheritable(fd, true))
                    report();
            // (the parent's ends of the pipes, closed)
            if (c.p2cwrite != -1)
                NEOS_POSIX_CALL(::close(c.p2cwrite));
            if (c.c2pread != -1)
                NEOS_POSIX_CALL(::close(c.c2pread));
            if (c.errread != -1)
                NEOS_POSIX_CALL(::close(c.errread));
            NEOS_POSIX_CALL(::close(c.errpipeRead));
            // (a file descriptor 0, 1 or 2 that is to be another (#12607): duplicated first, so that it is not overwritten)
            if (c2pwrite == 0)
            {
                NEOS_POSIX_CALL(c2pwrite = ::dup(c2pwrite));
                if (!set_inheritable(c2pwrite, false))
                    report();
            }
            while (errwrite == 0 || errwrite == 1)
            {
                NEOS_POSIX_CALL(errwrite = ::dup(errwrite));
                if (!set_inheritable(errwrite, false))
                    report();
            }
            // (the child's standard file descriptors (dup2 clears FD_CLOEXEC, but not if it would do nothing (#10806)))
            if (c.p2cread == 0)
            {
                if (!set_inheritable(c.p2cread, true))
                    report();
            }
            else if (c.p2cread != -1)
                NEOS_POSIX_CALL(::dup2(c.p2cread, 0));
            if (c2pwrite == 1)
            {
                if (!set_inheritable(c2pwrite, true))
                    report();
            }
            else if (c2pwrite != -1)
                NEOS_POSIX_CALL(::dup2(c2pwrite, 1));
            if (errwrite == 2)
            {
                if (!set_inheritable(errwrite, true))
                    report();
            }
            else if (errwrite != -1)
                NEOS_POSIX_CALL(::dup2(errwrite, 2));
            if (c.cwd != nullptr && ::chdir(c.cwd) == -1)
            {
                errMsg = "noexec:chdir";
                report();
            }
            if (c.childUmask >= 0)
                ::umask(static_cast<mode_t>(c.childUmask));
            // (_Py_RestoreSignals: the signals that Python ignores, as they were)
            if (c.restoreSignals)
            {
#if defined(SIGPIPE)
                ::signal(SIGPIPE, SIG_DFL);
#endif
#if defined(SIGXFZ)
                ::signal(SIGXFZ, SIG_DFL);
#endif
#if defined(SIGXFSZ)
                ::signal(SIGXFSZ, SIG_DFL);
#endif
            }
            if (c.callSetsid)
                NEOS_POSIX_CALL(::setsid());
            if (c.pgidToSet >= 0)
                NEOS_POSIX_CALL(::setpgid(0, c.pgidToSet));
            if (c.setGroups)
                NEOS_POSIX_CALL(::setgroups(c.extraGroups.size(), c.extraGroups.empty() ? nullptr : c.extraGroups.data()));
            if (c.gid != static_cast<gid_t>(-1))
                NEOS_POSIX_CALL(::setregid(c.gid, c.gid));
            if (c.uid != static_cast<uid_t>(-1))
                NEOS_POSIX_CALL(::setreuid(c.uid, c.uid));
#undef NEOS_POSIX_CALL
            errMsg = "";
            if (c.preexecFn != nullptr)
            {
                // (the program's function, called in the child (the user has asked for a deadlock))
                ref const args{ make_tuple() };
                ref const result{ call(c.preexecFn, args.get(), nullptr) };
                if (!result)
                {
                    errMsg = "Exception occurred in preexec_fn.";
                    errno = 0;
                    report();
                }
            }
            // (after preexec_fn, which might open file descriptors)
            if (c.closeFds)
                close_open_fds(3, c.fdsToKeep);
            // (Lib/os.py's _execvpe's search, of the list that subprocess.py made; the first error is reported, not the last)
            savedErrno = 0;
            for (auto const executable : c.executables)
            {
                if (executable == nullptr)
                    break;
                if (!c.envp.empty())
                    ::execve(executable, c.argv.empty() ? nullptr : c.argv.data(), c.envp.data());
                else
                    ::execv(executable, c.argv.empty() ? nullptr : c.argv.data());
                if (errno != ENOENT && errno != ENOTDIR && savedErrno == 0)
                    savedErrno = errno;
            }
            if (savedErrno != 0)
                errno = savedErrno;
            report();
            ::_exit(255);
        }

        // (a sequence's items as bytes (each: str, bytes or os.PathLike, as PyUnicode_FSConverter converts them))
        std::vector<std::string> fs_converted(py const& aSequence, char const* aWhat)
        {
            std::vector<std::string> result;
            py const fsencode = import_attribute("os", "fsencode");
            for (py const& item : aSequence.items())
            {
                py const b = fsencode(item);
                std::string_view data;
                if (!bytes_like(b.get(), data))
                    py_raise("TypeError", std::string{ aWhat } + " must be bytes");
                if (data.find('\0') != std::string_view::npos)
                    py_raise("ValueError", "embedded null byte");
                result.emplace_back(data);
            }
            return result;
        }

        std::vector<char*> pointers(std::vector<std::string>& aStrings)
        {
            std::vector<char*> result;
            for (auto& s : aStrings)
                result.push_back(s.data());
            result.push_back(nullptr);
            return result;
        }

        long long int_of(py const& aValue, char const* aWhat)
        {
            long long v = 0;
            if (!to_index(aValue.get(), v))
                throw raised{};
            (void)aWhat;
            return v;
        }

        // (fork_exec(args, executable_list, close_fds, pass_fds, cwd, env, p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite,
        // errpipe_read, errpipe_write, restore_signals, call_setsid, pgid_to_set, gid, extra_groups, uid, child_umask, preexec_fn,
        // allow_vfork): the child's pid)
        py fork_exec(std::vector<py> const& aArgs, object* aKwargs)
        {
            if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                py_raise("TypeError", "fork_exec() takes no keyword arguments");
            if (aArgs.size() != 23u)
                py_raise("TypeError", "fork_exec expected 23 arguments, got " + std::to_string(aArgs.size()));
            if (!host_unrestricted("_posixsubprocess.fork_exec"))
                throw raised{};
            child_arguments c;
            auto const& processArgs = aArgs[0];
            auto const& passFds = aArgs[3];
            c.closeFds = aArgs[2].truthy();
            c.p2cread = static_cast<int>(int_of(aArgs[6], "p2cread"));
            c.p2cwrite = static_cast<int>(int_of(aArgs[7], "p2cwrite"));
            c.c2pread = static_cast<int>(int_of(aArgs[8], "c2pread"));
            c.c2pwrite = static_cast<int>(int_of(aArgs[9], "c2pwrite"));
            c.errread = static_cast<int>(int_of(aArgs[10], "errread"));
            c.errwrite = static_cast<int>(int_of(aArgs[11], "errwrite"));
            c.errpipeRead = static_cast<int>(int_of(aArgs[12], "errpipe_read"));
            c.errpipeWrite = static_cast<int>(int_of(aArgs[13], "errpipe_write"));
            c.restoreSignals = aArgs[14].truthy();
            c.callSetsid = aArgs[15].truthy();
            c.pgidToSet = static_cast<pid_t>(int_of(aArgs[16], "pgid_to_set"));
            c.childUmask = static_cast<int>(int_of(aArgs[20], "child_umask"));
            auto const& preexecFn = aArgs[21];
            if (!isinstance(passFds, builtin("tuple")))
                py_raise("TypeError", std::string{ "fork_exec() argument 4 must be tuple, not " } + type_name(passFds.get()));
            if (c.closeFds && c.errpipeWrite < 3)
                py_raise("ValueError", "errpipe_write must be >= 3");
            // (_sanity_check_python_fd_sequence: ints, sorted, not negative)
            long long previous = -1;
            for (py const& fd : passFds.items())
            {
                long long v = 0;
                if (!isinstance(fd, builtin("int")) || !to_index(fd.get(), v) || v < 0 || v <= previous || v > INT_MAX)
                {
                    clear_error();
                    py_raise("ValueError", "bad value(s) in fds_to_keep");
                }
                previous = v;
                c.fdsToKeep.push_back(static_cast<int>(v));
            }
            // (the conversions: made in the parent, so that the child allocates nothing)
            auto executables = fs_converted(aArgs[1], "executable_list");
            c.executables = pointers(executables);
            std::vector<std::string> argv;
            if (!processArgs.is_none())
            {
                if (!isinstance(processArgs, builtin("list")) && !isinstance(processArgs, builtin("tuple")))
                    py_raise("TypeError", "argv must be a tuple");
                argv = fs_converted(processArgs, "argv");
                c.argv = pointers(argv);
            }
            std::vector<std::string> envp;
            if (!aArgs[5].is_none())
            {
                envp = fs_converted(aArgs[5], "env");
                c.envp = pointers(envp);
            }
            std::string cwd;
            if (!aArgs[4].is_none())
            {
                cwd = fs_converted(py_list({ aArgs[4] }), "cwd")[0];
                c.cwd = cwd.c_str();
            }
            if (!aArgs[18].is_none())
            {
                if (!isinstance(aArgs[18], builtin("list")))
                    py_raise("TypeError", "setgroups argument must be a list");
                c.setGroups = true;
                for (py const& g : aArgs[18].items())
                {
                    if (!isinstance(g, builtin("int")))
                        py_raise("TypeError", "extra_groups must be integers");
                    long long v = 0;
                    if (!to_index(g.get(), v) || v < 0)
                    {
                        clear_error();
                        py_raise("ValueError", "invalid group id");
                    }
                    c.extraGroups.push_back(static_cast<gid_t>(v));
                }
            }
            if (!aArgs[17].is_none())
                c.gid = static_cast<gid_t>(int_of(aArgs[17], "gid"));
            if (!aArgs[19].is_none())
                c.uid = static_cast<uid_t>(int_of(aArgs[19], "uid"));
            if (!preexecFn.is_none())
                c.preexecFn = preexecFn.get();
            std::fflush(nullptr);
            auto const pid = ::fork();
            if (pid == 0)
                child_exec(c);
            if (pid == -1)
            {
                raise_os_error(errno);
                throw raised{};
            }
            return py{ static_cast<long long>(pid) };
        }

        bool posixsubprocess_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                aAttributes.emplace_back("__doc__", py{ "A POSIX helper for the subprocess module." }.release());
                aAttributes.emplace_back("fork_exec", make_native_function("fork_exec", "fork_exec",
                    "Spawn a fresh new child process.\n\nFork a child process, close parent file descriptors as appropriate in\nthe child and duplicate the few that are needed before calling exec() in\nthe child process.\n\nIf close_fds is True, close file descriptors 3 and higher, except those\nlisted in the sorted tuple pass_fds.\n\nThe preexec_fn, if supplied, will be called immediately before closing\nfile descriptors and exec.\n\nWARNING: preexec_fn is NOT SAFE if your application uses threads.\n         It may trigger infrequent, difficult to debug deadlocks.\n\nIf an error occurs in the child process before the exec, it is\nserialized and written to the errpipe_write fd per subprocess.py.\n\nReturns: the child process's PID.\n\nRaises: Only on an error in the parent process.",
                    "_posixsubprocess", fork_exec).release());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sPosixsubprocess = register_module("_posixsubprocess", posixsubprocess_module);
    }
}

#endif
