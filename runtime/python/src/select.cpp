/*
  select.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/selectmodule.c and Objects/fileobject.c), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted
  to its object model (see runtime/python/NOTICE).
*/

// The module select (CPython 3.13's Modules/selectmodule.c): select(), poll objects (not on Windows) and epoll objects (Linux), and their
// constants; devpoll (Solaris) and kqueue (BSD and macOS) are not implemented. poll and epoll are classes made (by type) of closures; an
// object's state (its registrations, its descriptor) is the module's (keyed by the object), as CPython's is in the object.

#include "sockets.hpp"
#if !defined(_WIN32)
#include <climits>
#include <poll.h>
#if defined(__linux__)
#include <sys/epoll.h>
#endif
#endif
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "pytime.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "select";

        py S(char const* aText) { return py{ aText }; }

        // (a closure as a function of the module)
        py function(std::string const& aName, std::string const& aQualname, native_body aBody, char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, std::move(aBody));
        }

        // (the arguments as Argument Clinic parses them (parse_arguments); aSkip leading arguments (self) are not parsed: an argument not
        // given is empty)
        std::vector<py> clinic(char const* aFunction, std::vector<py> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
            std::size_t aRequired, std::size_t aPositionalOnly, std::size_t aSkip = 0u)
        {
            std::vector<object*> raw;
            for (std::size_t i = aSkip; i < aArgs.size(); ++i)
                raw.push_back(aArgs[i].get());
            std::vector<object*> parsed;
            py_checked(parse_arguments(aFunction, raw, aKwargs, aNames, aRequired, aPositionalOnly, parsed));
            std::vector<py> result;
            for (auto p : parsed)
                result.push_back(p != nullptr ? py::borrowed(p) : py{});
            return result;
        }

        // (an int as a C int (PyLong_AsInt))
        int as_int(py const& aValue)
        {
            auto const n = aValue.as_index();
            if (n < INT_MIN || n > INT_MAX)
                py_raise("OverflowError", n < 0 ? "Python int too small to convert to C int" : "Python int too large to convert to C int");
            return static_cast<int>(n);
        }

        // (an int, or an object's fileno(), as a file descriptor (Objects/fileobject.c's PyObject_AsFileDescriptor))
        int as_file_descriptor(py const& aObject)
        {
            int fd = 0;
            if (kind_of(aObject.get()) == NEOS_PY_INT || kind_of(aObject.get()) == NEOS_PY_BOOL)
            {
                if (kind_of(aObject.get()) == NEOS_PY_BOOL)
                    import_attribute("warnings", "warn").call({ S("bool is used as a file descriptor"), builtin("RuntimeWarning") }, { { "stacklevel", py{ 1 } } });
                fd = as_int(aObject);
            }
            else if (builtin("hasattr")(aObject, S("fileno")).truthy())
            {
                py const fno = aObject.method("fileno");
                if (kind_of(fno.get()) != NEOS_PY_INT && kind_of(fno.get()) != NEOS_PY_BOOL)
                    py_raise("TypeError", "fileno() returned a non-integer");
                fd = as_int(fno);
            }
            else
                py_raise("TypeError", "argument must be an int, or have a fileno() method.");
            if (fd < 0)
                py_raise("ValueError", "file descriptor cannot be a negative integer (" + std::to_string(fd) + ")");
            return fd;
        }

        // (an int as a C unsigned short (_PyLong_UnsignedShort_Converter))
        unsigned short as_unsigned_short(py const& aValue)
        {
            if (kind_of(aValue.get()) == NEOS_PY_INT && aValue < py{ 0 })
                py_raise("ValueError", "value must be positive");
            if (kind_of(aValue.get()) != NEOS_PY_INT && kind_of(aValue.get()) != NEOS_PY_BOOL)
                py_raise("TypeError", "'" + std::string{ type_name(aValue.get()) } + "' object cannot be interpreted as an integer");
            if (aValue > py{ 0xFFFF })
                py_raise("OverflowError", "Python int too large for C unsigned short");
            return static_cast<unsigned short>(aValue.as_index());
        }

        // (an int's low bits as a C unsigned int (unsigned_int(bitwise=True): PyLong_AsUnsignedLongMask))
        unsigned int as_unsigned_int_mask(py const& aValue)
        {
            py const index = import_attribute("operator", "index")(aValue);
            return static_cast<unsigned int>((index & py{ 0xFFFFFFFFLL }).as_index());
        }

        // (the module's state: the objects' states (keyed by the objects), the classes)
        struct module_state
        {
#if !defined(_WIN32)
            struct poll_state
            {
                py dict;                            ///< (the registrations: fd -> eventmask)
                bool ufdUptodate = false;
                std::vector<pollfd> ufds;
                bool pollRunning = false;
            };
            std::map<object*, poll_state> polls;
            py pollClass;
#endif
#if defined(__linux__)
            std::map<object*, int> epolls;          ///< (the epoll objects' descriptors (-1: closed))
            py epollClass;
#endif
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: type(aName, (object,), namespace))
        py make_select_class(char const* aName, namespace_type const& aNamespace, char const* aDoc)
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S(sModule));
            ns.set_item(S("__qualname__"), S(aName));
            ns.set_item(S("__doc__"), S(aDoc));
            for (auto const& [name, value] : aNamespace)
                ns.set_item(S(name), value);
            return builtin("type")(S(aName), py_tuple({ builtin("object") }), ns);
        }

        // --- select()

        struct selected
        {
            py object;
            sockets::socket_type fd;
        };

        // (a sequence of file descriptors (or objects with fileno()) as an fd_set: the highest descriptor + 1 (seq2set))
        int seq2set(py const& aSequence, fd_set& aSet, std::vector<selected>& aObjects)
        {
            int max = -1;
            aObjects.clear();
            FD_ZERO(&aSet);
            py fast;
            try
            {
                fast = builtin("list")(aSequence);
            }
            catch (raised const&)
            {
                if (caught({ "TypeError" }))
                    py_raise("TypeError", "arguments 1-3 must be sequences");
                throw;
            }
            for (auto const& o : fast.items())
            {
                auto const v = static_cast<sockets::socket_type>(as_file_descriptor(o));
#if defined(_WIN32)
                max = 0;    // (not used for Win32)
#else
                if (!(static_cast<unsigned int>(v) < static_cast<unsigned int>(FD_SETSIZE)))
                    py_raise("ValueError", "filedescriptor out of range in select()");
                if (static_cast<int>(v) > max)
                    max = static_cast<int>(v);
#endif
                FD_SET(v, &aSet);
                if (aObjects.size() >= static_cast<std::size_t>(FD_SETSIZE))
                    py_raise("ValueError", "too many file descriptors in select()");
                aObjects.push_back(selected{ o, v });
            }
            return max + 1;
        }

        // (the objects whose descriptors are in an fd_set (set2list))
        py set2list(fd_set& aSet, std::vector<selected> const& aObjects)
        {
            std::vector<py> items;
            for (auto const& o : aObjects)
                if (FD_ISSET(o.fd, &aSet))
                    items.push_back(o.object);
            return py_list(items);
        }

        // (a timeout as a timeval: false if its seconds overflow (pytime_as_timeval_struct))
        bool to_timeval(pytime::time_type aTimeout, timeval& aResult, pytime::rounding aRounding)
        {
            pytime::time_type seconds = 0;
            long microseconds = 0;
            bool ok = pytime::as_timeval(aTimeout, seconds, microseconds, aRounding);
#if defined(_WIN32)
            using seconds_type = long;
#else
            using seconds_type = decltype(aResult.tv_sec);
#endif
            if (seconds > static_cast<pytime::time_type>(std::numeric_limits<seconds_type>::max()))
            {
                seconds = std::numeric_limits<seconds_type>::max();
                ok = false;
                microseconds = 0;
            }
            else if (seconds < static_cast<pytime::time_type>(std::numeric_limits<seconds_type>::min()))
            {
                seconds = std::numeric_limits<seconds_type>::min();
                ok = false;
                microseconds = 0;
            }
            aResult.tv_sec = static_cast<seconds_type>(seconds);
            aResult.tv_usec = microseconds;
            return ok;
        }

        py select_select(std::vector<py> const& aArgs, object* aKwargs)
        {
            auto const a = clinic("select.select", aArgs, aKwargs, { "rlist", "wlist", "xlist", "timeout" }, 3u, 4u);
            timeval tv{};
            timeval* tvp = nullptr;
            pytime::time_type timeout = 0;
            pytime::time_type deadline = 0;
            if (!a[3].empty() && !a[3].is_none())
            {
                if (!pytime::from_seconds_object(timeout, a[3].get(), pytime::rounding::Timeout))
                {
                    if (caught({ "TypeError" }))
                        py_raise("TypeError", "timeout must be a float or None");
                    throw raised{};
                }
                if (!to_timeval(timeout, tv, pytime::rounding::Timeout))
                    py_raise("OverflowError", "timestamp out of range for platform time_t");
                if (tv.tv_sec < 0)
                    py_raise("ValueError", "timeout must be non-negative");
                tvp = &tv;
            }
            fd_set ifdset, ofdset, efdset;
            std::vector<selected> rfd2obj, wfd2obj, efd2obj;
            int const imax = seq2set(a[0], ifdset, rfd2obj);
            int const omax = seq2set(a[1], ofdset, wfd2obj);
            int const emax = seq2set(a[2], efdset, efd2obj);
            int max = imax;
            if (omax > max)
                max = omax;
            if (emax > max)
                max = emax;
            if (tvp != nullptr)
                deadline = pytime::deadline_init(timeout);
            int n = 0;
            int error = 0;
            for (;;)
            {
                gil_release();
                errno = 0;
                n = ::select(max, imax ? &ifdset : nullptr, omax ? &ofdset : nullptr, emax ? &efdset : nullptr, tvp);
                error = errno;
#if defined(_WIN32)
                int const socketError = n == SOCKET_ERROR ? ::WSAGetLastError() : 0;
#endif
                gil_acquire();
#if defined(_WIN32)
                if (n == SOCKET_ERROR)
                    ::WSASetLastError(socketError);
#endif
                if (error != EINTR)
                    break;
                // (select() was interrupted by a signal)
                if (!signals_check())
                    throw raised{};
                if (tvp != nullptr)
                {
                    timeout = pytime::deadline_get(deadline);
                    if (timeout < 0)
                    {
                        // (bpo-35310: lists were unmodified -- clear them explicitly)
                        FD_ZERO(&ifdset);
                        FD_ZERO(&ofdset);
                        FD_ZERO(&efdset);
                        n = 0;
                        break;
                    }
                    to_timeval(timeout, tv, pytime::rounding::Ceiling);
                    // (retry select() with the recomputed timeout)
                }
            }
#if defined(_WIN32)
            if (n == SOCKET_ERROR)
            {
                sockets::raise_windows_error(::WSAGetLastError());
                throw raised{};
            }
#else
            if (n < 0)
            {
                raise_os_error(error);
                throw raised{};
            }
#endif
            return py_tuple({ set2list(ifdset, rfd2obj), set2list(ofdset, wfd2obj), set2list(efdset, efd2obj) });
        }

#if !defined(_WIN32)
        // --- poll objects

        module_state::poll_state& poll_of(py const& aSelf)
        {
            auto& polls = state().polls;
            auto existing = polls.find(aSelf.get());
            if (existing == polls.end())
                py_raise("TypeError", "descriptor requires a 'select.poll' object");
            return existing->second;
        }

        // (the pollfd array of the registrations (update_ufd_array))
        void update_ufd_array(module_state::poll_state& aPoll)
        {
            aPoll.ufds.clear();
            for (auto const& item : py{ call_method_named(aPoll.dict.get(), "items", {}) }.items())
            {
                pollfd p{};
                p.fd = static_cast<int>(item[py{ 0 }].as_index());
                p.events = static_cast<short>(static_cast<unsigned short>(item[py{ 1 }].as_index()));
                aPoll.ufds.push_back(p);
            }
            aPoll.ufdUptodate = true;
        }

        void make_poll_class()
        {
            auto& s = state();
            auto const self_method = [](char const* aName, std::function<py(py const&, std::vector<py> const&, object*)> aBody)
            {
                return function(aName, std::string{ "poll." } + aName, [aName, aBody](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aArgs.empty())
                        py_raise("TypeError", std::string{ "descriptor '" } + aName + "' of 'select.poll' object needs an argument");
                    return aBody(aArgs[0], aArgs, aKwargs);
                });
            };
            namespace_type const ns =
            {
                { "register", self_method("register", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("poll.register", aArgs, aKwargs, { "fd", "eventmask" }, 1u, 2u, 1u);
                        int const fd = as_file_descriptor(a[0]);
                        unsigned short const eventmask = !a[1].empty() ? as_unsigned_short(a[1]) : static_cast<unsigned short>(POLLIN | POLLPRI | POLLOUT);
                        auto& p = poll_of(aSelf);
                        // (add entry to the internal dictionary: the key is the file descriptor, and the value is the event mask)
                        p.dict.set_item(py{ fd }, py{ static_cast<long long>(eventmask) });
                        p.ufdUptodate = false;
                        return py::none();
                    }) },
                { "modify", self_method("modify", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("poll.modify", aArgs, aKwargs, { "fd", "eventmask" }, 2u, 2u, 1u);
                        int const fd = as_file_descriptor(a[0]);
                        unsigned short const eventmask = as_unsigned_short(a[1]);
                        auto& p = poll_of(aSelf);
                        // (modify registered fd)
                        if (!p.dict.contains(py{ fd }))
                        {
                            raise_os_error(ENOENT);
                            throw raised{};
                        }
                        p.dict.set_item(py{ fd }, py{ static_cast<long long>(eventmask) });
                        p.ufdUptodate = false;
                        return py::none();
                    }) },
                { "unregister", self_method("unregister", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("poll.unregister", aArgs, aKwargs, { "fd" }, 1u, 1u, 1u);
                        int const fd = as_file_descriptor(a[0]);
                        auto& p = poll_of(aSelf);
                        // (this will simply raise the KeyError if the file descriptor isn't registered)
                        p.dict.method("__delitem__", { py{ fd } });
                        p.ufdUptodate = false;
                        return py::none();
                    }) },
                { "poll", self_method("poll", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("poll.poll", aArgs, aKwargs, { "timeout" }, 0u, 1u, 1u);
                        pytime::time_type timeout = -1;
                        pytime::time_type ms = -1;
                        pytime::time_type deadline = 0;
                        if (!a[0].empty() && !a[0].is_none())
                        {
                            if (!pytime::from_milliseconds_object(timeout, a[0].get(), pytime::rounding::Timeout))
                            {
                                if (caught({ "TypeError" }))
                                    py_raise("TypeError", "timeout must be an integer or None");
                                throw raised{};
                            }
                            ms = pytime::as_milliseconds(timeout, pytime::rounding::Timeout);
                            if (ms < INT_MIN || ms > INT_MAX)
                                py_raise("OverflowError", "timeout is too large");
                            if (timeout >= 0)
                                deadline = pytime::deadline_init(timeout);
                        }
                        // (on some OSes, typically BSD-based ones, the timeout parameter of the poll() syscall, when negative, must be exactly
                        // INFTIM, where defined, or -1 (issue 31334))
                        if (ms < 0)
                        {
#ifdef INFTIM
                            ms = INFTIM;
#else
                            ms = -1;
#endif
                        }
                        auto& p = poll_of(aSelf);
                        // (avoid concurrent poll() invocation, issue 8865)
                        if (p.pollRunning)
                            py_raise("RuntimeError", "concurrent poll() invocation");
                        // (ensure the ufd array is up to date)
                        if (!p.ufdUptodate)
                            update_ufd_array(p);
                        p.pollRunning = true;
                        int pollResult = 0;
                        int error = 0;
                        bool asyncError = false;
                        // (the array (its storage) is the object's: it is not changed while poll() runs (pollRunning))
                        auto* const ufds = p.ufds.data();
                        auto const ufdLen = static_cast<nfds_t>(p.ufds.size());
                        for (;;)
                        {
                            gil_release();
                            errno = 0;
                            pollResult = ::poll(ufds, ufdLen, static_cast<int>(ms));
                            error = errno;
                            gil_acquire();
                            if (error != EINTR)
                                break;
                            // (poll() was interrupted by a signal)
                            if (!signals_check())
                            {
                                asyncError = true;
                                break;
                            }
                            if (timeout >= 0)
                            {
                                timeout = pytime::deadline_get(deadline);
                                if (timeout < 0)
                                {
                                    pollResult = 0;
                                    break;
                                }
                                ms = pytime::as_milliseconds(timeout, pytime::rounding::Ceiling);
                                // (retry poll() with the recomputed timeout)
                            }
                        }
                        auto& q = poll_of(aSelf);
                        q.pollRunning = false;
                        if (asyncError)
                            throw raised{};
                        if (pollResult < 0)
                        {
                            raise_os_error(error);
                            throw raised{};
                        }
                        // (build the result list)
                        std::vector<py> result;
                        std::size_t i = 0u;
                        for (int j = 0; j < pollResult; ++j)
                        {
                            // (skip to the next fired descriptor)
                            while (!q.ufds[i].revents)
                                ++i;
                            // (the & 0xffff is a workaround for AIX: 'revents' is a 16-bit short, and IBM assigned POLLNVAL to be 0x8000)
                            result.push_back(py_tuple({ py{ q.ufds[i].fd }, py{ static_cast<long long>(q.ufds[i].revents & 0xffff) } }));
                            ++i;
                        }
                        return py_list(result);
                    }) },
                { "__del__", function("__del__", "poll.__del__", [](std::vector<py> const& aArgs, object*) -> py
                    {
                        if (!aArgs.empty())
                            state().polls.erase(aArgs[0].get());
                        return py::none();
                    }) }
            };
            s.pollClass = make_select_class("poll", ns, "");
        }

        py select_poll(std::vector<py> const& aArgs, object* aKwargs)
        {
            clinic("select.poll", aArgs, aKwargs, {}, 0u, 0u);
            auto& s = state();
            py const self = builtin("object").attr("__new__")(s.pollClass);
            auto& p = s.polls[self.get()];
            p.dict = py_dict();
            p.ufdUptodate = false;
            p.pollRunning = false;
            return self;
        }
#endif

#if defined(__linux__)
        // --- epoll objects

        [[noreturn]] void epoll_err_closed()
        {
            py_raise("ValueError", "I/O operation on closed epoll object");
        }

        int& epoll_fd(py const& aSelf)
        {
            auto& epolls = state().epolls;
            auto existing = epolls.find(aSelf.get());
            if (existing == epolls.end())
                existing = epolls.emplace(aSelf.get(), -1).first;
            return existing->second;
        }

        // (close its descriptor: the errno of close(), if it failed (pyepoll_internal_close))
        int epoll_internal_close(int& aEpfd)
        {
            int saveErrno = 0;
            if (aEpfd >= 0)
            {
                int const epfd = aEpfd;
                aEpfd = -1;
                gil_release();
                if (::close(epfd) < 0)
                    saveErrno = errno;
                gil_acquire();
            }
            return saveErrno;
        }

        py epoll_internal_ctl(int aEpfd, int aOp, int aFd, unsigned int aEvents)
        {
            epoll_event ev{};
            int result = 0;
            int error = 0;
            if (aEpfd < 0)
                epoll_err_closed();
            switch (aOp)
            {
            case EPOLL_CTL_ADD:
            case EPOLL_CTL_MOD:
                ev.events = aEvents;
                ev.data.fd = aFd;
                gil_release();
                result = ::epoll_ctl(aEpfd, aOp, aFd, &ev);
                error = errno;
                gil_acquire();
                break;
            case EPOLL_CTL_DEL:
                // (in kernel versions before 2.6.9, the EPOLL_CTL_DEL operation required a non-NULL pointer in event, even though this argument
                // is ignored)
                gil_release();
                result = ::epoll_ctl(aEpfd, aOp, aFd, &ev);
                error = errno;
                gil_acquire();
                break;
            default:
                result = -1;
                error = EINVAL;
            }
            if (result < 0)
            {
                raise_os_error(error);
                throw raised{};
            }
            return py::none();
        }

        void make_epoll_class()
        {
            auto& s = state();
            auto const self_method = [](char const* aName, std::function<py(py const&, std::vector<py> const&, object*)> aBody, char const* aDoc = "")
            {
                return function(aName, std::string{ "epoll." } + aName, [aName, aBody](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aArgs.empty())
                        py_raise("TypeError", std::string{ "unbound method epoll." } + aName + "() needs an argument");
                    return aBody(aArgs[0], aArgs, aKwargs);
                }, aDoc);
            };
            namespace_type const ns =
            {
                { "__init__", self_method("__init__", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("epoll", aArgs, aKwargs, { "sizehint", "flags" }, 0u, 0u, 1u);
                        int sizehint = a[0].empty() ? -1 : as_int(a[0]);
                        int const flags = a[1].empty() ? 0 : as_int(a[1]);
                        if (sizehint == -1)
                            sizehint = FD_SETSIZE - 1;
                        else if (sizehint <= 0)
                            py_raise("ValueError", "negative sizehint");
                        if (flags && flags != EPOLL_CLOEXEC)
                            py_raise("OSError", "invalid flags");
                        auto& epfd = epoll_fd(aSelf);
                        (void)epoll_internal_close(epfd);
                        gil_release();
                        int const fd = ::epoll_create1(EPOLL_CLOEXEC);
                        int const error = errno;
                        gil_acquire();
                        if (fd < 0)
                        {
                            raise_os_error(error);
                            throw raised{};
                        }
                        epoll_fd(aSelf) = fd;
                        return py::none();
                    }) },
                { "close", self_method("close", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        clinic("epoll.close", aArgs, aKwargs, {}, 0u, 0u, 1u);
                        int const error = epoll_internal_close(epoll_fd(aSelf));
                        if (error != 0)
                        {
                            raise_os_error(error);
                            throw raised{};
                        }
                        return py::none();
                    }, "Close the epoll control file descriptor.\n\nFurther operations on the epoll object will raise an exception.") },
                { "closed", builtin("property")(self_method("closed", [](py const& aSelf, std::vector<py> const&, object*) -> py
                    {
                        return py::boolean(epoll_fd(aSelf) < 0);
                    }, "True if the epoll handler is closed")) },
                { "fileno", self_method("fileno", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        clinic("epoll.fileno", aArgs, aKwargs, {}, 0u, 0u, 1u);
                        int const epfd = epoll_fd(aSelf);
                        if (epfd < 0)
                            epoll_err_closed();
                        return py{ epfd };
                    }, "Return the epoll control file descriptor.") },
                { "fromfd", builtin("classmethod")(function("fromfd", "epoll.fromfd", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("epoll.fromfd", aArgs, aKwargs, { "fd" }, 1u, 1u, 1u);
                        int const fd = as_int(a[0]);
                        py const self = builtin("object").attr("__new__")(aArgs[0]);
                        epoll_fd(self) = fd;
                        return self;
                    }, "Create an epoll object from a given control fd.")) },
                { "register", self_method("register", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("epoll.register", aArgs, aKwargs, { "fd", "eventmask" }, 1u, 0u, 1u);
                        int const fd = as_file_descriptor(a[0]);
                        unsigned int const eventmask = a[1].empty() ? static_cast<unsigned int>(EPOLLIN | EPOLLPRI | EPOLLOUT) : as_unsigned_int_mask(a[1]);
                        return epoll_internal_ctl(epoll_fd(aSelf), EPOLL_CTL_ADD, fd, eventmask);
                    }, "Registers a new fd or raises an OSError if the fd is already registered.\n\n  fd\n    the target file descriptor of the operation\n"
                       "  eventmask\n    a bit set composed of the various EPOLL constants\n\nThe epoll interface supports all file descriptors that support poll.") },
                { "modify", self_method("modify", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("epoll.modify", aArgs, aKwargs, { "fd", "eventmask" }, 2u, 0u, 1u);
                        int const fd = as_file_descriptor(a[0]);
                        unsigned int const eventmask = as_unsigned_int_mask(a[1]);
                        return epoll_internal_ctl(epoll_fd(aSelf), EPOLL_CTL_MOD, fd, eventmask);
                    }, "Modify event mask for a registered file descriptor.\n\n  fd\n    the target file descriptor of the operation\n"
                       "  eventmask\n    a bit set composed of the various EPOLL constants") },
                { "unregister", self_method("unregister", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("epoll.unregister", aArgs, aKwargs, { "fd" }, 1u, 0u, 1u);
                        int const fd = as_file_descriptor(a[0]);
                        return epoll_internal_ctl(epoll_fd(aSelf), EPOLL_CTL_DEL, fd, 0u);
                    }, "Remove a registered file descriptor from the epoll object.\n\n  fd\n    the target file descriptor of the operation") },
                { "poll", self_method("poll", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = clinic("epoll.poll", aArgs, aKwargs, { "timeout", "maxevents" }, 0u, 0u, 1u);
                        pytime::time_type timeout = -1;
                        pytime::time_type ms = -1;
                        pytime::time_type deadline = 0;
                        if (epoll_fd(aSelf) < 0)
                            epoll_err_closed();
                        if (!a[0].empty() && !a[0].is_none())
                        {
                            // (epoll_wait() has a resolution of 1 millisecond, round towards infinity to wait at least timeout seconds)
                            if (!pytime::from_seconds_object(timeout, a[0].get(), pytime::rounding::Timeout))
                            {
                                if (caught({ "TypeError" }))
                                    py_raise("TypeError", "timeout must be an integer or None");
                                throw raised{};
                            }
                            ms = pytime::as_milliseconds(timeout, pytime::rounding::Ceiling);
                            if (ms < INT_MIN || ms > INT_MAX)
                                py_raise("OverflowError", "timeout is too large");
                            // (epoll_wait(2) treats all arbitrary negative numbers the same for the timeout argument, but -1 is the documented way
                            // to block indefinitely)
                            if (ms < 0)
                                ms = -1;
                            if (timeout >= 0)
                                deadline = pytime::deadline_init(timeout);
                        }
                        int maxevents = a[1].empty() ? -1 : as_int(a[1]);
                        if (maxevents == -1)
                            maxevents = FD_SETSIZE - 1;
                        else if (maxevents < 1)
                            py_raise("ValueError", "maxevents must be greater than 0, got " + std::to_string(maxevents));
                        std::vector<epoll_event> evs(static_cast<std::size_t>(maxevents));
                        int nfds = 0;
                        int error = 0;
                        int const epfd = epoll_fd(aSelf);
                        for (;;)
                        {
                            gil_release();
                            errno = 0;
                            nfds = ::epoll_wait(epfd, evs.data(), maxevents, static_cast<int>(ms));
                            error = errno;
                            gil_acquire();
                            if (error != EINTR)
                                break;
                            // (poll() was interrupted by a signal)
                            if (!signals_check())
                                throw raised{};
                            if (timeout >= 0)
                            {
                                timeout = pytime::deadline_get(deadline);
                                if (timeout < 0)
                                {
                                    nfds = 0;
                                    break;
                                }
                                ms = pytime::as_milliseconds(timeout, pytime::rounding::Ceiling);
                                // (retry epoll_wait() with the recomputed timeout)
                            }
                        }
                        if (nfds < 0)
                        {
                            raise_os_error(error);
                            throw raised{};
                        }
                        std::vector<py> elist;
                        for (int i = 0; i < nfds; ++i)
                            elist.push_back(py_tuple({ py{ evs[static_cast<std::size_t>(i)].data.fd }, py{ static_cast<long long>(evs[static_cast<std::size_t>(i)].events) } }));
                        return py_list(elist);
                    }, "Wait for events on the epoll file descriptor.\n\n  timeout\n    the maximum time to wait in seconds (as float);\n"
                       "    a timeout of None or -1 makes poll wait indefinitely\n  maxevents\n    the maximum number of events returned; -1 means no limit\n\n"
                       "Returns a list containing any descriptors that have events to report,\nas a list of (fd, events) 2-tuples.") },
                { "__enter__", self_method("__enter__", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        clinic("epoll.__enter__", aArgs, aKwargs, {}, 0u, 0u, 1u);
                        if (epoll_fd(aSelf) < 0)
                            epoll_err_closed();
                        return aSelf;
                    }) },
                { "__exit__", self_method("__exit__", [](py const& aSelf, std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        clinic("epoll.__exit__", aArgs, aKwargs, { "exc_type", "exc_value", "exc_tb" }, 0u, 3u, 1u);
                        return aSelf.method("close");
                    }) },
                { "__del__", function("__del__", "epoll.__del__", [](std::vector<py> const& aArgs, object*) -> py
                    {
                        if (!aArgs.empty())
                        {
                            auto& epolls = state().epolls;
                            auto existing = epolls.find(aArgs[0].get());
                            if (existing != epolls.end())
                            {
                                (void)epoll_internal_close(existing->second);
                                epolls.erase(existing);
                            }
                        }
                        return py::none();
                    }) }
            };
            s.epollClass = make_select_class("epoll", ns, "select.epoll(sizehint=-1, flags=0)\n--\n\nReturns an epolling object.\n\n  sizehint\n"
                "    The expected number of events to be registered.  It must be positive,\n    or -1 to use the default.  It is only used on older systems where\n"
                "    epoll_create1() is not available; otherwise it has no effect (though its\n    value is still checked).\n  flags\n"
                "    Deprecated and completely ignored.  However, when supplied, its value\n    must be 0 or select.EPOLL_CLOEXEC, otherwise OSError is raised.");
        }
#endif

        bool select_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (!s.made)
                {
                    s.made = true;
#if !defined(_WIN32)
                    make_poll_class();
#endif
#if defined(__linux__)
                    make_epoll_class();
#endif
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                auto const add_int = [&](char const* aName, long long aValue) { add(aName, py{ aValue }); };
                add("__doc__", S("This module supports asynchronous I/O on multiple file descriptors.\n\n*** IMPORTANT NOTICE ***\n"
                    "On Windows, only sockets are supported; on Unix, all file descriptors."));
                add("error", builtin("OSError"));
                add("select", function("select", "select", select_select, "select($module, rlist, wlist, xlist, timeout=None, /)\n--\n\n"
                    "Wait until one or more file descriptors are ready for some kind of I/O.\n\nThe first three arguments are iterables of file descriptors to be waited for:\n"
                    "rlist -- wait until ready for reading\nwlist -- wait until ready for writing\nxlist -- wait for an \"exceptional condition\"\n"
                    "If only one kind of condition is required, pass [] for the other lists.\n\nA file descriptor is either a socket or file object, or a small integer\n"
                    "gotten from a fileno() method call on one of those.\n\nThe optional 4th argument specifies a timeout in seconds; it may be\n"
                    "a floating-point number to specify fractions of seconds.  If it is absent\nor None, the call will never time out.\n\n"
                    "The return value is a tuple of three lists corresponding to the first three\narguments; each contains the subset of the corresponding file\n"
                    "descriptors that are ready.\n\n*** IMPORTANT NOTICE ***\nOn Windows, only sockets are supported; on Unix, all file\ndescriptors can be used."));
#ifdef PIPE_BUF
                add_int("PIPE_BUF", PIPE_BUF);
#endif
#if !defined(_WIN32)
                add("poll", function("poll", "poll", select_poll, "poll($module, /)\n--\n\nReturns a polling object.\n\n"
                    "This object supports registering and unregistering file descriptors, and then\npolling them for I/O events."));
                add_int("POLLIN", POLLIN);
                add_int("POLLPRI", POLLPRI);
                add_int("POLLOUT", POLLOUT);
                add_int("POLLERR", POLLERR);
                add_int("POLLHUP", POLLHUP);
                add_int("POLLNVAL", POLLNVAL);
#ifdef POLLRDNORM
                add_int("POLLRDNORM", POLLRDNORM);
#endif
#ifdef POLLRDBAND
                add_int("POLLRDBAND", POLLRDBAND);
#endif
#ifdef POLLWRNORM
                add_int("POLLWRNORM", POLLWRNORM);
#endif
#ifdef POLLWRBAND
                add_int("POLLWRBAND", POLLWRBAND);
#endif
#ifdef POLLMSG
                add_int("POLLMSG", POLLMSG);
#endif
#ifdef POLLRDHUP
                // (kernel 2.6.17+)
                add_int("POLLRDHUP", POLLRDHUP);
#endif
#endif
#if defined(__linux__)
                add("epoll", s.epollClass);
                add_int("EPOLLIN", EPOLLIN);
                add_int("EPOLLOUT", EPOLLOUT);
                add_int("EPOLLPRI", EPOLLPRI);
                add_int("EPOLLERR", EPOLLERR);
                add_int("EPOLLHUP", EPOLLHUP);
#ifdef EPOLLRDHUP
                // (kernel 2.6.17)
                add_int("EPOLLRDHUP", EPOLLRDHUP);
#endif
                add_int("EPOLLET", static_cast<unsigned int>(EPOLLET));
#ifdef EPOLLONESHOT
                // (kernel 2.6.2+)
                add_int("EPOLLONESHOT", EPOLLONESHOT);
#endif
#ifdef EPOLLEXCLUSIVE
                add_int("EPOLLEXCLUSIVE", EPOLLEXCLUSIVE);
#endif
#ifdef EPOLLRDNORM
                add_int("EPOLLRDNORM", EPOLLRDNORM);
#endif
#ifdef EPOLLRDBAND
                add_int("EPOLLRDBAND", EPOLLRDBAND);
#endif
#ifdef EPOLLWRNORM
                add_int("EPOLLWRNORM", EPOLLWRNORM);
#endif
#ifdef EPOLLWRBAND
                add_int("EPOLLWRBAND", EPOLLWRBAND);
#endif
#ifdef EPOLLMSG
                add_int("EPOLLMSG", EPOLLMSG);
#endif
#ifdef EPOLL_CLOEXEC
                add_int("EPOLL_CLOEXEC", EPOLL_CLOEXEC);
#endif
#endif
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sSelect = register_module("select", select_module);
    }
}
