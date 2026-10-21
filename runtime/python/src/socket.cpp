/*
  socket.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/socketmodule.c, Python/fileutils.c and Lib/encodings/idna.py),
  Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation
  License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos Python runtime
  plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The module _socket (CPython 3.13's Modules/socketmodule.c): socket objects (of the address families AF_INET, AF_INET6 and, not on
// Windows, AF_UNIX), the module's functions (name resolution, conversions, socketpair (not on Windows), ...), its exceptions (herror and
// gaierror) and its constants (socket_constants.hpp); sendmsg(), recvmsg(), recvmsg_into(), CMSG_LEN() and CMSG_SPACE() are not Windows' (as
// CPython's are not). Not implemented: sendmsg_afalg(), Windows' ioctl() and share() (and a socket from share()'s data), and Windows'
// if_nameindex(), if_nametoindex() and if_indextoname(); the other address families (e.g. AF_PACKET, AF_BLUETOOTH, AF_CAN) have their constants but not their addresses.
// socket is a class made (by type) of closures, so socket.socket (socket.cpp's Lib/socket.py) is its subclass; a socket object's state (its
// descriptor, family, type, protocol and timeout) is the module's (keyed by the object), as CPython's is in the object. Its operations release
// the GIL while they wait (with a timeout, as CPython's do: the descriptor is non-blocking and poll() (Windows: select()) waits).

#if defined(_WIN32) && !defined(_WINSOCK_DEPRECATED_NO_WARNINGS)
#define _WINSOCK_DEPRECATED_NO_WARNINGS // (gethostbyname and gethostbyaddr: socket.gethostbyname's and socket.gethostbyaddr's, as CPython's)
#endif
#include "sockets.hpp"
#if defined(_WIN32)
#include <mstcpip.h>
#else
#include <climits>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#if defined(__linux__)
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <net/ethernet.h>
#include <netpacket/packet.h>
#endif
#endif
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "pytime.hpp"
#include "values.hpp"
#include "socket_docs.hpp"
#include "socket_constants.hpp"

#if defined(_WIN32)
// (CPython's socketmodule.h: AF_UNIX is not supported on Windows (it has no sys/un.h))
#undef AF_UNIX
#endif

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "_socket";

        py S(char const* aText) { return py{ aText }; }
        py S(std::string const& aText) { return py{ aText }; }
        py none() { return py::none(); }
        py B(char const* aData, std::size_t aSize) { return py{ make_bytes(std::string{ aData, aSize }) }; }

        using sockets::socket_type;
        using sockets::kInvalidSocket;
#if defined(_WIN32)
        using socklen_type = int;
#else
        using socklen_type = socklen_t;
#endif

        // (an address of any of the supported families (sock_addr_t))
        union sock_addr
        {
            sockaddr sa;
            sockaddr_in in;
            sockaddr_in6 in6;
#if defined(AF_UNIX)
            sockaddr_un un;
#endif
            sockaddr_storage storage;
        };

        // --- the errors of the sockets API

        // (GET_SOCK_ERROR and SET_SOCK_ERROR)
        int sock_errno()
        {
#if defined(_WIN32)
            return ::WSAGetLastError();
#else
            return errno;
#endif
        }
        void set_sock_errno(int aError)
        {
#if defined(_WIN32)
            ::WSASetLastError(aError);
#else
            errno = aError;
#endif
        }
#if defined(_WIN32)
        constexpr int kEINTR = WSAEINTR;
        constexpr int kEWOULDBLOCK = WSAEWOULDBLOCK;
        constexpr int kEAGAIN = WSAEWOULDBLOCK;
        constexpr int kSOCK_TIMEOUT_ERR = WSAEWOULDBLOCK;
        constexpr int kSOCK_INPROGRESS_ERR = WSAEWOULDBLOCK;
        constexpr int kEISCONN = WSAEISCONN;
        constexpr int kECONNRESET = WSAECONNRESET;
#else
        constexpr int kEINTR = EINTR;
        constexpr int kEWOULDBLOCK = EWOULDBLOCK;
        constexpr int kEAGAIN = EAGAIN;
        constexpr int kSOCK_TIMEOUT_ERR = EWOULDBLOCK;
        constexpr int kSOCK_INPROGRESS_ERR = EINPROGRESS;
        constexpr int kEISCONN = EISCONN;
        constexpr int kECONNRESET = ECONNRESET;
#endif

        // (the error of the last call of the sockets API: OSError (set_error))
        [[noreturn]] void set_error()
        {
            sockets::raise_socket_error();
            throw raised{};
        }

        // (aBody() with the GIL released (Py_BEGIN_ALLOW_THREADS ... Py_END_ALLOW_THREADS): the error it left is kept)
        template <typename Body>
        auto allow_threads(Body&& aBody)
        {
            gil_release();
            auto const result = aBody();
            int const error = sock_errno();
            int const cerror = errno;
            gil_acquire();
            errno = cerror;
            set_sock_errno(error);
            return result;
        }

        int socket_close(socket_type aFd)
        {
#if defined(_WIN32)
            return ::closesocket(aFd);
#else
            return ::close(aFd);
#endif
        }

        // --- the module's state

        struct sock_state
        {
            socket_type fd = kInvalidSocket;
            int family = 0;
            int type = 0;
            int proto = 0;
            pytime::time_type timeout = -pytime::kSecondToNs;   ///< (< 0: blocking; 0: non-blocking; > 0: a timeout)
        };

        struct module_state
        {
            std::map<object*, sock_state> sockets;  ///< (the socket objects' states (keyed by the objects))
            py sockClass;
            py herror;
            py gaierror;
            pytime::time_type defaulttimeout = -pytime::kSecondToNs;
            int sockCloexecWorks = -1;
            int accept4Works = -1;
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        // --- conversions of arguments (as PyArg_ParseTuple's formats convert them)

        std::string type_name_of(py const& aObject) { return type_name(aObject.get()); }

        // (i: PyLong_AsInt)
        int as_int(py const& aValue)
        {
            auto const n = aValue.as_index();
            if (n < INT_MIN || n > INT_MAX)
                py_raise("OverflowError", n < 0 ? "signed integer is less than minimum" : "signed integer is greater than maximum");
            return static_cast<int>(n);
        }
        // (PyLong_AsInt (an int argument of a clinic function))
        int as_c_int(py const& aValue)
        {
            auto const n = aValue.as_index();
            if (n < INT_MIN || n > INT_MAX)
                py_raise("OverflowError", n < 0 ? "Python int too small to convert to C int" : "Python int too large to convert to C int");
            return static_cast<int>(n);
        }
        // (n: Py_ssize_t)
        long long as_ssize(py const& aValue) { return aValue.as_index(); }
        // (I: an unsigned int (its low bits))
        unsigned int as_unsigned_int(py const& aValue)
        {
            py const index = import_attribute("operator", "index")(aValue);
            return static_cast<unsigned int>((index & py{ 0xFFFFFFFFLL }).as_index());
        }
        // (y*: a bytes-like object)
        std::string_view as_buffer(py const& aValue)
        {
            std::string_view data;
            if (!bytes_like(aValue.get(), data))
                py_raise("TypeError", "a bytes-like object is required, not '" + type_name_of(aValue) + "'");
            return data;
        }
        // (w*: a read-write bytes-like object)
        buffer_info as_writable_buffer(char const* aFunction, std::size_t aPosition, py const& aValue)
        {
            buffer_info buffer;
            if (!buffer_of(aValue.get(), buffer) || buffer.readonly)
                py_raise("TypeError", std::string{ aFunction } + "() argument " + std::to_string(aPosition) + " must be read-write bytes-like object, not " +
                    type_name_of(aValue));
            return buffer;
        }
        // (s: a str (UTF-8, no null character))
        std::string as_str(char const* aFunction, std::size_t aPosition, py const& aValue)
        {
            if (!isinstance(aValue, builtin("str")))
                py_raise("TypeError", std::string{ aFunction } + "() argument " + std::to_string(aPosition) + " must be str, not " + type_name_of(aValue));
            std::string text = aValue.str();
            if (text.find('\0') != std::string::npos)
                py_raise("ValueError", "embedded null character");
            return text;
        }
        // (a str encoded as IDNA (Lib/encodings/idna.py's Codec.encode: an ASCII name's fast path; otherwise the codec's))
        std::string idna_encode(py const& aText)
        {
            std::string const text = aText.str();
            bool const ascii = std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80u; });
            if (!ascii)
                return std::string{ as_buffer(aText.method("encode", { S("idna") })) };
            // (ASCII name: fast path)
            std::vector<std::string> labels;
            for (std::size_t start = 0u;;)
            {
                auto const dot = text.find('.', start);
                labels.push_back(text.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
                if (dot == std::string::npos)
                    break;
                start = dot + 1u;
            }
            auto const offsetOf = [&](std::size_t aLabel)
            {
                std::size_t offset = aLabel;
                for (std::size_t i = 0u; i < aLabel; ++i)
                    offset += labels[i].size();
                return offset;
            };
            for (std::size_t i = 0u; i + 1u < labels.size(); ++i)
                if (labels[i].empty())
                {
                    auto const offset = offsetOf(i);
                    py_raise(builtin("UnicodeEncodeError")(S("idna"), aText, py{ offset }, py{ offset + 1u }, S("label empty")));
                }
            for (std::size_t i = 0u; i < labels.size(); ++i)
                if (labels[i].size() >= 64u)
                {
                    auto const offset = offsetOf(i);
                    py_raise(builtin("UnicodeEncodeError")(S("idna"), aText, py{ offset }, py{ offset + labels[i].size() }, S("label too long")));
                }
            return text;
        }

        // (et with "idna": a str (IDNA-encoded), bytes or bytearray (no null byte))
        std::string as_idna(char const* aFunction, py const& aValue)
        {
            std::string result;
            if (isinstance(aValue, builtin("str")))
                result = idna_encode(aValue);
            else if (isinstance(aValue, builtin("bytes")) || isinstance(aValue, builtin("bytearray")))
                result = std::string{ as_buffer(aValue) };
            else
                py_raise("TypeError", std::string{ aFunction } + "() argument 1 must be str, bytes or bytearray, not " + type_name_of(aValue));
            if (result.find('\0') != std::string::npos)
                py_raise("TypeError", std::string{ aFunction } + "() argument 1 must be encoded string without null bytes, not " + type_name_of(aValue));
            return result;
        }

        // (bytes as a file system name (os.fsdecode: PyUnicode_DecodeFSDefault) and a str as one (os.fsencode: PyUnicode_EncodeFSDefault))
#if defined(_WIN32)
        char const* const sFsErrors = "surrogatepass";
#else
        char const* const sFsErrors = "surrogateescape";
#endif
        [[maybe_unused]] py fs_decode(py const& aBytes) { return aBytes.method("decode", { S("utf-8"), S(sFsErrors) }); }
        [[maybe_unused]] py fs_encode(py const& aText) { return aText.method("encode", { S("utf-8"), S(sFsErrors) }); }

        // (PyArg_ParseTuple's check of the number of arguments)
        void check_count(char const* aFunction, std::size_t aCount, std::size_t aMin, std::size_t aMax)
        {
            if (aCount >= aMin && aCount <= aMax)
                return;
            std::size_t const n = aCount < aMin ? aMin : aMax;
            py_raise("TypeError", std::string{ aFunction } + "() takes " + (aMin == aMax ? "exactly" : aCount < aMin ? "at least" : "at most") + " " +
                std::to_string(n) + " argument" + (n == 1u ? "" : "s") + " (" + std::to_string(aCount) + " given)");
        }

        // (Argument Clinic's parse of arguments (parse_arguments): an argument not given is empty)
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

        bool has_keywords(object* aKwargs) { return aKwargs != nullptr && length(aKwargs) > 0; }

        // --- socket objects

        sock_state& sock_of(py const& aSelf)
        {
            auto& sockets = state().sockets;
            auto existing = sockets.find(aSelf.get());
            if (existing == sockets.end())
                existing = sockets.emplace(aSelf.get(), sock_state{}).first;
            return existing->second;
        }

        // (Python/fileutils.c's set_inheritable (not inheritable): aAtomicFlagWorks: whether SOCK_CLOEXEC is known to work (-1: not known))
        void set_non_inheritable(socket_type aFd, int* aAtomicFlagWorks)
        {
#if defined(_WIN32)
            (void)aAtomicFlagWorks;
            if (!::SetHandleInformation(reinterpret_cast<HANDLE>(aFd), HANDLE_FLAG_INHERIT, 0))
            {
                sockets::raise_windows_error(static_cast<int>(::GetLastError()));
                throw raised{};
            }
#else
            if (aAtomicFlagWorks != nullptr && *aAtomicFlagWorks != 0)
            {
                if (*aAtomicFlagWorks == -1)
                {
                    int const flags = ::fcntl(aFd, F_GETFD);
                    if (flags == -1)
                    {
                        raise_os_error(errno);
                        throw raised{};
                    }
                    *aAtomicFlagWorks = (flags & FD_CLOEXEC) != 0 ? 1 : 0;
                }
                if (*aAtomicFlagWorks == 1)
                    return;
            }
            int const flags = ::fcntl(aFd, F_GETFD);
            if (flags < 0)
            {
                raise_os_error(errno);
                throw raised{};
            }
            if ((flags & FD_CLOEXEC) != 0)
                return;
            if (::fcntl(aFd, F_SETFD, flags | FD_CLOEXEC) < 0)
            {
                raise_os_error(errno);
                throw raised{};
            }
#endif
        }

        // (blocking or not (internal_setblocking))
        void internal_setblocking(sock_state& s, bool aBlock)
        {
            socket_type const fd = s.fd;
            int const result = allow_threads([&]()
            {
#if defined(_WIN32)
                u_long arg = aBlock ? 0u : 1u;
                return ::ioctlsocket(fd, FIONBIO, &arg) != 0 ? -1 : 0;
#else
                int block = aBlock ? 0 : 1;
                return ::ioctl(fd, FIONBIO, &block) == -1 ? -1 : 0;
#endif
            });
            if (result != 0)
            {
#if defined(_WIN32)
                sockets::raise_windows_error(::WSAGetLastError());
                throw raised{};
#else
                raise_os_error(errno);
                throw raised{};
#endif
            }
        }

        // (wait until the socket is ready to read (or to write; aConnect: or an error): -1: error; 1: timed out; 0: ready (internal_select))
        int internal_select(sock_state& s, bool aWriting, pytime::time_type aInterval, bool aConnect)
        {
            // (guard against closed socket)
            if (s.fd == kInvalidSocket)
                return 0;
            int n = 0;
#if !defined(_WIN32)
            pollfd p{};
            p.fd = s.fd;
            p.events = aWriting ? POLLOUT : POLLIN;
            // (on Windows, the socket becomes writable on connection success, but a connection failure is notified as an error; on POSIX, the
            // socket becomes writable on connection success or on connection failure)
            if (aConnect)
                p.events |= POLLERR;
            // (the timeout in ms)
            auto ms = pytime::as_milliseconds(aInterval, pytime::rounding::Ceiling);
            if (ms > INT_MAX)
                ms = INT_MAX;
            // (on some OSes, typically BSD-based ones, the timeout parameter of the poll() syscall, when negative, must be exactly INFTIM, where
            // defined, or -1 (issue 37811))
            if (ms < 0)
                ms = -1;
            n = allow_threads([&]() { return ::poll(&p, 1, static_cast<int>(ms)); });
#else
            timeval tv{};
            timeval* tvp = nullptr;
            if (aInterval >= 0)
            {
                pytime::time_type seconds = 0;
                long microseconds = 0;
                if (!pytime::as_timeval(aInterval, seconds, microseconds, pytime::rounding::Ceiling) || seconds > LONG_MAX)
                    seconds = LONG_MAX;
                tv.tv_sec = static_cast<long>(seconds);
                tv.tv_usec = microseconds;
                tvp = &tv;
            }
            fd_set fds, efds;
            FD_ZERO(&fds);
            FD_SET(s.fd, &fds);
            FD_ZERO(&efds);
            if (aConnect)
                FD_SET(s.fd, &efds);
            // (see if the socket is ready)
            n = allow_threads([&]() { return aWriting ? ::select(0, nullptr, &fds, &efds, tvp) : ::select(0, &fds, nullptr, &efds, tvp); });
#endif
            if (n < 0)
                return -1;
            if (n == 0)
                return 1;
            return 0;
        }

        // (call a socket function (CPython's sock_call_ex): it is called with the GIL released; with a timeout, after waiting until the socket is
        // ready (writable if aWriting, else readable); interrupted by a signal (EINTR), it is retried (unless a signal handler raised: PEP 475),
        // the timeout recomputed; true if it succeeded; otherwise false (*aErr: its error) if aErr is given, else its error is raised)
        template <typename Func>
        bool sock_call_ex(sock_state& s, bool aWriting, Func&& aFunc, bool aConnect, int* aErr, pytime::time_type aTimeout)
        {
            bool const hasTimeout = aTimeout > 0;
            pytime::time_type deadline = 0;
            bool deadlineInitialized = false;
            // (outer loop to retry select() when select() is interrupted by a signal or to retry select()+sock_func() on false positive)
            for (;;)
            {
                // (for connect(), poll even for blocking socket: the connection runs asynchronously)
                if (hasTimeout || aConnect)
                {
                    int res = 0;
                    if (hasTimeout)
                    {
                        pytime::time_type interval = 0;
                        if (deadlineInitialized)
                            interval = pytime::deadline_get(deadline);  // (recompute the timeout)
                        else
                        {
                            deadlineInitialized = true;
                            deadline = pytime::deadline_init(aTimeout);
                            interval = aTimeout;
                        }
                        res = interval >= 0 ? internal_select(s, aWriting, interval, aConnect) : 1;
                    }
                    else
                        res = internal_select(s, aWriting, aTimeout, aConnect);
                    if (res == -1)
                    {
                        if (aErr != nullptr)
                            *aErr = sock_errno();
                        if (sock_errno() == kEINTR)
                        {
                            // (select() was interrupted by a signal)
                            if (!signals_check())
                                throw raised{};
                            // (retry select())
                            continue;
                        }
                        // (select() failed)
                        set_error();
                    }
                    if (res == 1)
                    {
                        if (aErr != nullptr)
                        {
                            *aErr = kSOCK_TIMEOUT_ERR;
                            return false;
                        }
                        py_raise("TimeoutError", "timed out");
                    }
                    // (the socket is ready)
                }
                // (inner loop to retry sock_func() when sock_func() is interrupted by a signal)
                for (;;)
                {
                    bool const ok = allow_threads([&]() { return aFunc(s); });
                    if (ok)
                    {
                        // (sock_func() succeeded)
                        if (aErr != nullptr)
                            *aErr = 0;
                        return true;
                    }
                    if (aErr != nullptr)
                        *aErr = sock_errno();
                    if (sock_errno() != kEINTR)
                        break;
                    // (sock_func() was interrupted by a signal)
                    if (!signals_check())
                        throw raised{};
                    // (retry sock_func())
                }
                if (s.timeout > 0 && (sock_errno() == kEWOULDBLOCK || sock_errno() == kEAGAIN))
                {
                    // (false positive: sock_func() failed with EWOULDBLOCK or EAGAIN (e.g. select() could indicate a socket is ready for reading,
                    // but the data then discarded by the OS because of a wrong checksum): loop on select() to recheck for socket readiness)
                    continue;
                }
                // (sock_func() failed)
                if (aErr == nullptr)
                    set_error();
                // (else: aErr was already set before)
                return false;
            }
        }

        template <typename Func>
        bool sock_call(sock_state& s, bool aWriting, Func&& aFunc)
        {
            return sock_call_ex(s, aWriting, std::forward<Func>(aFunc), false, nullptr, s.timeout);
        }

        // (initialize a socket object's state (init_sockobject))
        void init_sockobject(sock_state& s, socket_type aFd, int aFamily, int aType, int aProto)
        {
            s.fd = aFd;
            s.family = aFamily;
            s.type = aType;
            // (it's possible to pass SOCK_NONBLOCK and SOCK_CLOEXEC bit flags on some OSes as part of socket.type: we want to reset them here,
            // to make socket.type be set to the same value on all platforms; otherwise, simple code like 'if sock.type == SOCK_STREAM' is not
            // portable)
#if defined(SOCK_NONBLOCK)
            s.type = s.type & ~SOCK_NONBLOCK;
#endif
#if defined(SOCK_CLOEXEC)
            s.type = s.type & ~SOCK_CLOEXEC;
#endif
            s.proto = aProto;
#if defined(SOCK_NONBLOCK)
            if ((aType & SOCK_NONBLOCK) != 0)
                s.timeout = 0;
            else
#endif
            {
                s.timeout = state().defaulttimeout;
                if (s.timeout >= 0)
                    internal_setblocking(s, false);
            }
        }

        // (a new socket object (new_sockobject))
        [[maybe_unused]] py new_sockobject(socket_type aFd, int aFamily, int aType, int aProto)
        {
            py const result = builtin("object").attr("__new__")(state().sockClass);
            init_sockobject(sock_of(result), aFd, aFamily, aType, aProto);
            return result;
        }

        // --- addresses

        // (a gaierror (or, for EAI_SYSTEM, the system's error) (set_gaierror))
        [[noreturn]] void set_gaierror(int aError)
        {
#if defined(EAI_SYSTEM)
            // (EAI_SYSTEM is not available on Windows XP)
            if (aError == EAI_SYSTEM)
                set_error();
#endif
#if defined(_WIN32)
            std::string const message = ::gai_strerrorA(aError);
#else
            std::string const message = ::gai_strerror(aError);
#endif
            py_raise(state().gaierror(py{ aError }, S(message)));
        }

        // (a host name, or one of a few symbolic names, as a numeric IP address (in aAddress); the names "" and "<broadcast>" are special:
        // the length of the address (IPv4: 4) (setipaddr))
        int setipaddr(std::string const& aName, sockaddr* aAddress, std::size_t aSize, int aFamily)
        {
            std::memset(aAddress, 0, sizeof(*aAddress));
            if (aName.empty())
            {
                addrinfo hints{};
                hints.ai_family = aFamily;
                hints.ai_socktype = SOCK_DGRAM;     // (dummy)
                hints.ai_flags = AI_PASSIVE;
                addrinfo* res = nullptr;
                int const error = allow_threads([&]() { return ::getaddrinfo(nullptr, "0", &hints, &res); });
                if (error != 0)
                    set_gaierror(error);
                int siz = 0;
                switch (res->ai_family)
                {
                case AF_INET:
                    siz = 4;
                    break;
                case AF_INET6:
                    siz = 16;
                    break;
                default:
                    ::freeaddrinfo(res);
                    py_raise("OSError", "unsupported address family");
                }
                if (res->ai_next != nullptr)
                {
                    ::freeaddrinfo(res);
                    py_raise("OSError", "wildcard resolved to multiple address");
                }
                if (static_cast<std::size_t>(res->ai_addrlen) < aSize)
                    aSize = static_cast<std::size_t>(res->ai_addrlen);
                std::memcpy(aAddress, res->ai_addr, aSize);
                ::freeaddrinfo(res);
                return siz;
            }
            // (special-case broadcast - inet_addr() below can return INADDR_NONE for this)
            if (aName == "255.255.255.255" || aName == "<broadcast>")
            {
                if (aFamily != AF_INET && aFamily != AF_UNSPEC)
                    py_raise("OSError", "address family mismatched");
                auto* const sin = reinterpret_cast<sockaddr_in*>(aAddress);
                std::memset(sin, 0, sizeof(*sin));
                sin->sin_family = AF_INET;
                sin->sin_addr.s_addr = INADDR_BROADCAST;
                return static_cast<int>(sizeof(sin->sin_addr));
            }
            // (avoid a name resolution in case of numeric address: check for an IPv4 address)
            if (aFamily == AF_UNSPEC || aFamily == AF_INET)
            {
                auto* const sin = reinterpret_cast<sockaddr_in*>(aAddress);
                std::memset(sin, 0, sizeof(*sin));
                if (::inet_pton(AF_INET, aName.c_str(), &sin->sin_addr) > 0)
                {
                    sin->sin_family = AF_INET;
                    return 4;
                }
            }
            // (check for an IPv6 address - if the address contains a scope ID, we fallback to getaddrinfo(), which can handle translation from
            // interface name to interface index)
            if ((aFamily == AF_UNSPEC || aFamily == AF_INET6) && aName.find('%') == std::string::npos)
            {
                auto* const sin = reinterpret_cast<sockaddr_in6*>(aAddress);
                std::memset(sin, 0, sizeof(*sin));
                if (::inet_pton(AF_INET6, aName.c_str(), &sin->sin6_addr) > 0)
                {
                    sin->sin6_family = AF_INET6;
                    return 16;
                }
            }
            // (perform a name resolution)
            addrinfo hints{};
            hints.ai_family = aFamily;
            addrinfo* res = nullptr;
            int const error = allow_threads([&]() { return ::getaddrinfo(aName.c_str(), nullptr, &hints, &res); });
            if (error != 0)
                set_gaierror(error);
            if (static_cast<std::size_t>(res->ai_addrlen) < aSize)
                aSize = static_cast<std::size_t>(res->ai_addrlen);
            std::memcpy(aAddress, res->ai_addr, aSize);
            ::freeaddrinfo(res);
            switch (aAddress->sa_family)
            {
            case AF_INET:
                return 4;
            case AF_INET6:
                return 16;
            default:
                py_raise("OSError", "unknown address family");
            }
        }

        // (an IPv4 address as a str (make_ipv4_addr))
        py make_ipv4_addr(sockaddr_in const* aAddress)
        {
            char buf[INET_ADDRSTRLEN];
            if (::inet_ntop(AF_INET, const_cast<in_addr*>(&aAddress->sin_addr), buf, sizeof(buf)) == nullptr)
            {
                raise_os_error(errno);
                throw raised{};
            }
            return S(buf);
        }
        // (an IPv6 address as a str (make_ipv6_addr))
        py make_ipv6_addr(sockaddr_in6 const* aAddress)
        {
            char buf[INET6_ADDRSTRLEN];
            if (::inet_ntop(AF_INET6, const_cast<in6_addr*>(&aAddress->sin6_addr), buf, sizeof(buf)) == nullptr)
            {
                raise_os_error(errno);
                throw raised{};
            }
            return S(buf);
        }

        // (a socket address as an object, suitable for passing it back to bind(), connect() etc. (makesockaddr))
        py makesockaddr(sockaddr const* aAddress, std::size_t aLength)
        {
            if (aLength == 0u)
            {
                // (no address -- may be recvfrom() from known socket)
                return none();
            }
            switch (aAddress->sa_family)
            {
            case AF_INET:
                {
                    auto const* const a = reinterpret_cast<sockaddr_in const*>(aAddress);
                    return py_tuple({ make_ipv4_addr(a), py{ static_cast<long long>(ntohs(a->sin_port)) } });
                }
#if defined(AF_UNIX)
            case AF_UNIX:
                {
                    auto const* const a = reinterpret_cast<sockaddr_un const*>(aAddress);
#if defined(__linux__)
                    std::size_t const linuxaddrlen = aLength - offsetof(sockaddr_un, sun_path);
                    if (linuxaddrlen > 0u && a->sun_path[0] == 0)
                    {
                        // (Linux abstract namespace)
                        return B(a->sun_path, linuxaddrlen);
                    }
#endif
                    // (regular NULL-terminated string)
                    return fs_decode(B(a->sun_path, ::strnlen(a->sun_path, sizeof(a->sun_path))));
                }
#endif
            case AF_INET6:
                {
                    auto const* const a = reinterpret_cast<sockaddr_in6 const*>(aAddress);
                    return py_tuple({ make_ipv6_addr(a), py{ static_cast<long long>(ntohs(a->sin6_port)) },
                        py{ static_cast<long long>(ntohl(a->sin6_flowinfo)) }, py{ static_cast<long long>(a->sin6_scope_id) } });
                }
            default:
                // (if we don't know the address family, don't raise an exception -- return it as an (int, bytes) tuple)
                return py_tuple({ py{ static_cast<long long>(aAddress->sa_family) }, B(aAddress->sa_data, sizeof(aAddress->sa_data)) });
            }
        }

        // (a host name (idna_converter: ASCII-only host names (in particular, numeric IP addresses) bypass IDNA))
        std::string idna_converter(py const& aObject)
        {
            std::string result;
            if (isinstance(aObject, builtin("bytes")) || isinstance(aObject, builtin("bytearray")))
                result = std::string{ as_buffer(aObject) };
            else if (isinstance(aObject, builtin("str")))
            {
                std::string const text = aObject.str();
                bool const ascii = std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80u; });
                if (ascii)
                    result = text;
                else
                {
                    try
                    {
                        result = std::string{ as_buffer(aObject.method("encode", { S("idna") })) };
                    }
                    catch (raised const&)
                    {
                        py_take_exception();
                        py_raise("TypeError", "encoding of hostname failed");
                    }
                }
            }
            else
                py_raise("TypeError", "str, bytes or bytearray expected, not " + type_name_of(aObject));
            if (result.find('\0') != std::string::npos)
                py_raise("TypeError", "host name must not contain null character");
            return result;
        }

        // (an address's port ("i": OverflowError: "<caller>(): port must be 0-65535."))
        int address_port(py const& aPort, char const* aCaller)
        {
            try
            {
                return as_int(aPort);
            }
            catch (raised const&)
            {
                if (!caught({ "OverflowError" }))
                    throw;
                py_raise("OverflowError", std::string{ aCaller } + "(): port must be 0-65535.");
            }
        }

        // (a socket address argument as the socket's family has it: its length (getsockaddrarg))
        int getsockaddrarg(sock_state const& s, py const& aArgs, sock_addr& aAddress, char const* aCaller)
        {
            switch (s.family)
            {
#if defined(AF_UNIX)
            case AF_UNIX:
                {
                    // (PEP 383: not using PyUnicode_FSConverter since we need to allow embedded nulls on Linux)
                    py const args = isinstance(aArgs, builtin("str")) ? fs_encode(aArgs) : aArgs;
                    std::string_view path;
                    if (!bytes_like(args.get(), path))
                        py_raise("TypeError", "a bytes-like object is required, not '" + type_name_of(args) + "'");
                    auto* const addr = &aAddress.un;
                    std::memset(addr, 0, sizeof(sockaddr_un));
                    int length = 0;
#if defined(__linux__)
                    if (path.empty() || path[0] == '\0')
                    {
                        // (Linux abstract namespace extension: empty address auto-binding to an abstract address; address that starts with null
                        // byte)
                        if (path.size() > sizeof(addr->sun_path))
                            py_raise("OSError", "AF_UNIX path too long");
                        length = static_cast<int>(path.size() + offsetof(sockaddr_un, sun_path));
                    }
                    else
#endif
                    {
                        // (regular NULL-terminated string)
                        if (path.size() >= sizeof(addr->sun_path))
                            py_raise("OSError", "AF_UNIX path too long");
                        addr->sun_path[path.size()] = 0;
                        // (including the tailing NUL)
                        length = static_cast<int>(path.size() + offsetof(sockaddr_un, sun_path) + 1u);
                    }
                    addr->sun_family = static_cast<sa_family_t>(s.family);
                    std::memcpy(addr->sun_path, path.data(), path.size());
                    return length;
                }
#endif
            case AF_INET:
                {
                    if (!isinstance(aArgs, builtin("tuple")))
                        py_raise("TypeError", std::string{ aCaller } + "(): AF_INET address must be tuple, not " + type_name_of(aArgs));
                    auto const items = aArgs.items();
                    if (items.size() != 2u)
                        py_raise("TypeError", "AF_INET address must be a pair (host, port)");
                    std::string const host = idna_converter(items[0]);
                    int const port = address_port(items[1], aCaller);
                    auto* const addr = &aAddress.in;
                    std::memset(addr, 0, sizeof(sockaddr_in));
                    setipaddr(host, reinterpret_cast<sockaddr*>(addr), sizeof(*addr), AF_INET);
                    if (port < 0 || port > 0xffff)
                        py_raise("OverflowError", std::string{ aCaller } + "(): port must be 0-65535.");
                    addr->sin_family = AF_INET;
                    addr->sin_port = htons(static_cast<unsigned short>(port));
                    return static_cast<int>(sizeof(*addr));
                }
            case AF_INET6:
                {
                    if (!isinstance(aArgs, builtin("tuple")))
                        py_raise("TypeError", std::string{ aCaller } + "(): AF_INET6 address must be tuple, not " + type_name_of(aArgs));
                    auto const items = aArgs.items();
                    if (items.size() < 2u || items.size() > 4u)
                        py_raise("TypeError", "AF_INET6 address must be a tuple (host, port[, flowinfo[, scopeid]])");
                    std::string const host = idna_converter(items[0]);
                    int const port = address_port(items[1], aCaller);
                    unsigned int const flowinfo = items.size() > 2u ? as_unsigned_int(items[2]) : 0u;
                    unsigned int const scopeId = items.size() > 3u ? as_unsigned_int(items[3]) : 0u;
                    auto* const addr = &aAddress.in6;
                    std::memset(addr, 0, sizeof(sockaddr_in6));
                    setipaddr(host, reinterpret_cast<sockaddr*>(addr), sizeof(*addr), AF_INET6);
                    if (port < 0 || port > 0xffff)
                        py_raise("OverflowError", std::string{ aCaller } + "(): port must be 0-65535.");
                    if (flowinfo > 0xfffffu)
                        py_raise("OverflowError", std::string{ aCaller } + "(): flowinfo must be 0-1048575.");
                    addr->sin6_family = static_cast<decltype(addr->sin6_family)>(s.family);
                    addr->sin6_port = htons(static_cast<unsigned short>(port));
                    addr->sin6_flowinfo = htonl(flowinfo);
                    addr->sin6_scope_id = scopeId;
                    return static_cast<int>(sizeof(*addr));
                }
            default:
                py_raise("OSError", std::string{ aCaller } + "(): bad family");
            }
        }

        // (the length of an address of the socket's family (getsockaddrlen))
        socklen_type getsockaddrlen(sock_state const& s)
        {
            switch (s.family)
            {
#if defined(AF_UNIX)
            case AF_UNIX:
                return sizeof(sockaddr_un);
#endif
            case AF_INET:
                return sizeof(sockaddr_in);
            case AF_INET6:
                return sizeof(sockaddr_in6);
            default:
                py_raise("OSError", "getsockaddrlen: bad family");
            }
        }

        // --- sendmsg() and recvmsg[_into]() (not Windows': they require a working CMSG_LEN() (RFC 2292/3542))

#if defined(CMSG_LEN) && !defined(_WIN32)
#define NEOS_PY_SOCKET_CMSG
        // (the largest value to try to store in a socklen_t (used when handling ancillary data): POSIX requires socklen_t to hold at least
        // (2**31)-1 and recommends against storing larger values, but socklen_t was originally int in the BSD interface, so to be on the safe
        // side we use the smaller of (2**31)-1 and INT_MAX)
        constexpr std::size_t SOCKLEN_T_LIMIT = INT_MAX > 0x7fffffff ? 0x7fffffff : INT_MAX;

        // (if length is in range, set *result to CMSG_LEN(length) and return true; otherwise, return false (get_CMSG_LEN))
        bool get_CMSG_LEN(std::size_t aLength, std::size_t& aResult)
        {
            if (aLength > (SOCKLEN_T_LIMIT - CMSG_LEN(0)))
                return false;
            std::size_t const tmp = CMSG_LEN(aLength);
            if (tmp > SOCKLEN_T_LIMIT || tmp < aLength)
                return false;
            aResult = tmp;
            return true;
        }
#if defined(CMSG_SPACE)
        // (if length is in range, set *result to CMSG_SPACE(length) and return true; otherwise, return false (get_CMSG_SPACE))
        bool get_CMSG_SPACE(std::size_t aLength, std::size_t& aResult)
        {
            // (use CMSG_SPACE(1) here in order to take account of the padding necessary before *and* after the data)
            if (aLength > (SOCKLEN_T_LIMIT - CMSG_SPACE(1)))
                return false;
            std::size_t const tmp = CMSG_SPACE(aLength);
            if (tmp > SOCKLEN_T_LIMIT || tmp < aLength)
                return false;
            aResult = tmp;
            return true;
        }
#endif
        // (true iff msg->msg_controllen is valid, cmsgh is a valid pointer in msg->msg_control with at least "space" bytes after it, and its
        // cmsg_len member inside the buffer (cmsg_min_space))
        bool cmsg_min_space(msghdr const* aMsg, cmsghdr const* aCmsgh, std::size_t aSpace)
        {
            static std::size_t const cmsgLenEnd = offsetof(cmsghdr, cmsg_len) + sizeof(aCmsgh->cmsg_len);
            if (aCmsgh == nullptr || aMsg->msg_control == nullptr)
                return false;
            if (aSpace < cmsgLenEnd)
                aSpace = cmsgLenEnd;
            std::size_t const cmsgOffset = static_cast<std::size_t>(reinterpret_cast<char const*>(aCmsgh) - static_cast<char const*>(aMsg->msg_control));
            return cmsgOffset <= static_cast<std::size_t>(-1) - aSpace && cmsgOffset + aSpace <= static_cast<std::size_t>(aMsg->msg_controllen);
        }
        // (if pointer CMSG_DATA(cmsgh) is in buffer msg->msg_control, set *space to number of bytes following it in the buffer and return true;
        // otherwise, return false (assumes cmsgh, msg->msg_control and msg->msg_controllen are valid) (get_cmsg_data_space))
        bool get_cmsg_data_space(msghdr const* aMsg, cmsghdr* aCmsgh, std::size_t& aSpace)
        {
            auto const* const dataPtr = reinterpret_cast<char const*>(CMSG_DATA(aCmsgh));
            if (dataPtr == nullptr)
                return false;
            std::size_t const dataOffset = static_cast<std::size_t>(dataPtr - static_cast<char const*>(aMsg->msg_control));
            if (dataOffset > static_cast<std::size_t>(aMsg->msg_controllen))
                return false;
            aSpace = static_cast<std::size_t>(aMsg->msg_controllen) - dataOffset;
            return true;
        }
        // (if cmsgh is invalid or not contained in the buffer pointed to by msg->msg_control, return -1; if cmsgh is valid and its associated
        // data is entirely contained in the buffer, set *data_len to the length of the associated data and return 0; if only part of the
        // associated data is contained in the buffer but cmsgh is otherwise valid, set *data_len to the length contained in the buffer and
        // return 1 (get_cmsg_data_len))
        int get_cmsg_data_len(msghdr const* aMsg, cmsghdr* aCmsgh, std::size_t& aDataLen)
        {
            if (!cmsg_min_space(aMsg, aCmsgh, CMSG_LEN(0)) || aCmsgh->cmsg_len < CMSG_LEN(0))
                return -1;
            std::size_t const cmsgDataLen = aCmsgh->cmsg_len - CMSG_LEN(0);
            std::size_t space = 0u;
            if (!get_cmsg_data_space(aMsg, aCmsgh, space))
                return -1;
            if (space >= cmsgDataLen)
            {
                aDataLen = cmsgDataLen;
                return 0;
            }
            aDataLen = space;
            return 1;
        }

        // (call recvmsg() with the supplied iovec structures, flags, and ancillary data buffer size (controllen): the tuple that recvmsg() or
        // recvmsg_into() returns, its first item aMakeval(the length read); on error, the file descriptors received via SCM_RIGHTS are closed
        // (sock_recvmsg_guts))
        py sock_recvmsg_guts(sock_state& s, std::vector<iovec>& aIov, int aFlags, long long aControllen, std::function<py(long long)> const& aMakeval)
        {
            // (XXX: POSIX says that msg_name and msg_namelen "shall be ignored" when the socket is connected (Linux fills them in anyway for
            // AF_UNIX sockets at least); normally msg_namelen seems to be set to 0 if there's no address, but try to initialize msg_name to
            // something that won't be mistaken for a real address if that doesn't happen)
            socklen_type const addrbuflen = getsockaddrlen(s);
            sock_addr addrbuf{};
            addrbuf.sa.sa_family = AF_UNSPEC;
            if (aControllen < 0 || static_cast<unsigned long long>(aControllen) > SOCKLEN_T_LIMIT)
                py_raise("ValueError", "invalid ancillary data buffer length");
            std::vector<char> controlbuf(static_cast<std::size_t>(aControllen));
            msghdr msg{};
            msg.msg_name = &addrbuf.sa;
            msg.msg_namelen = addrbuflen;
            msg.msg_iov = aIov.data();
            msg.msg_iovlen = aIov.size();
            msg.msg_control = aControllen > 0 ? controlbuf.data() : nullptr;
            msg.msg_controllen = static_cast<decltype(msg.msg_controllen)>(aControllen);
            long long result = 0;
            sock_call(s, false, [&](sock_state& aSock)
            {
                result = ::recvmsg(aSock.fd, &msg, aFlags);
                return result >= 0;
            });
            try
            {
                // (make list of (level, type, data) tuples from control messages)
                py const cmsgList = py_list();
                // (check for empty ancillary data as old CMSG_FIRSTHDR() implementations didn't do so)
                for (cmsghdr* cmsgh = msg.msg_controllen > 0 ? CMSG_FIRSTHDR(&msg) : nullptr; cmsgh != nullptr; cmsgh = CMSG_NXTHDR(&msg, cmsgh))
                {
                    std::size_t cmsgdatalen = 0u;
                    int const cmsgStatus = get_cmsg_data_len(&msg, cmsgh, cmsgdatalen);
                    if (cmsgStatus != 0)
                        import_attribute("warnings", "warn")(S("received malformed or improperly-truncated ancillary data"), builtin("RuntimeWarning"), py{ 1 });
                    if (cmsgStatus < 0)
                        break;
                    cmsgList.method("append", { py_tuple({ py{ static_cast<long long>(cmsgh->cmsg_level) }, py{ static_cast<long long>(cmsgh->cmsg_type) },
                        B(reinterpret_cast<char const*>(CMSG_DATA(cmsgh)), cmsgdatalen) }) });
                    if (cmsgStatus != 0)
                        break;
                }
                py const value = aMakeval(result);
                return py_tuple({ value, cmsgList, py{ static_cast<long long>(msg.msg_flags) },
                    makesockaddr(&addrbuf.sa, static_cast<std::size_t>(msg.msg_namelen > addrbuflen ? addrbuflen : msg.msg_namelen)) });
            }
            catch (raised const&)
            {
#if defined(SCM_RIGHTS)
                // (close all descriptors coming from SCM_RIGHTS, so they don't leak)
                for (cmsghdr* cmsgh = msg.msg_controllen > 0 ? CMSG_FIRSTHDR(&msg) : nullptr; cmsgh != nullptr; cmsgh = CMSG_NXTHDR(&msg, cmsgh))
                {
                    std::size_t cmsgdatalen = 0u;
                    int const cmsgStatus = get_cmsg_data_len(&msg, cmsgh, cmsgdatalen);
                    if (cmsgStatus < 0)
                        break;
                    if (cmsgh->cmsg_level == SOL_SOCKET && cmsgh->cmsg_type == SCM_RIGHTS)
                    {
                        std::size_t numfds = cmsgdatalen / sizeof(int);
                        int fd = 0;
                        for (auto const* fdp = reinterpret_cast<char const*>(CMSG_DATA(cmsgh)); numfds-- > 0u; fdp += sizeof(int))
                        {
                            std::memcpy(&fd, fdp, sizeof(int));
                            ::close(fd);
                        }
                    }
                    if (cmsgStatus != 0)
                        break;
                }
#endif
                throw;
            }
        }

        // (an iterable as a tuple's items (PySequence_Tuple): TypeError (aMessage) if it is not one)
        std::vector<py> sequence_items(py const& aObject, std::string const& aMessage)
        {
            try
            {
                return builtin("tuple")(aObject).items();
            }
            catch (raised const&)
            {
                py_take_exception();
                py_raise("TypeError", aMessage);
            }
        }
#endif

        // --- the methods of socket objects

        enum class arguments { None, One, Var, VarKeywords };

        using method_body = std::function<py(sock_state& s, py const& aSelf, std::vector<py> const& aArgs, object* aKwargs)>;

        // (a method of socket objects (as CPython's: METH_NOARGS, METH_O, METH_VARARGS (and METH_KEYWORDS)): aArgs: its arguments after self)
        py method(char const* aName, arguments aArguments, method_body aBody)
        {
            std::string const name = aName;
            std::string const qualname = std::string{ "socket." } + aName;
            return make_native_function(name, qualname, socket_docs::doc(("_socket.socket." + name).c_str()), sModule,
                [name, qualname, aArguments, aBody](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aArgs.empty())
                        py_raise("TypeError", "descriptor '" + name + "' of '_socket.socket' object needs an argument");
                    if (!isinstance(aArgs[0], state().sockClass))
                        py_raise("TypeError", "descriptor '" + name + "' for '_socket.socket' objects doesn't apply to a '" + type_name_of(aArgs[0]) + "' object");
                    std::vector<py> const args{ aArgs.begin() + 1, aArgs.end() };
                    if (aArguments != arguments::VarKeywords && has_keywords(aKwargs))
                        py_raise("TypeError", qualname + "() takes no keyword arguments");
                    if (aArguments == arguments::None && !args.empty())
                        py_raise("TypeError", qualname + "() takes no arguments (" + std::to_string(args.size()) + " given)");
                    if (aArguments == arguments::One && args.size() != 1u)
                        py_raise("TypeError", qualname + "() takes exactly one argument (" + std::to_string(args.size()) + " given)");
                    return aBody(sock_of(aArgs[0]), aArgs[0], args, aKwargs);
                });
        }

        // (the socket's timeout argument (socket_parse_timeout): None: -1 s)
        pytime::time_type socket_parse_timeout(py const& aTimeout)
        {
            if (aTimeout.is_none())
                return -pytime::kSecondToNs;
            pytime::time_type timeout = 0;
            py_checked(pytime::from_seconds_object(timeout, aTimeout.get(), pytime::rounding::Timeout));
            if (timeout < 0)
                py_raise("ValueError", "Timeout value out of range");
#if defined(_WIN32)
            pytime::time_type seconds = 0;
            long microseconds = 0;
            if (!pytime::as_timeval(timeout, seconds, microseconds, pytime::rounding::Timeout) || seconds > LONG_MAX)
                py_raise("OverflowError", "timeout doesn't fit into C timeval");
#endif
            return timeout;
        }

        py sock_gettimeout(sock_state const& s)
        {
            if (s.timeout < 0)
                return none();
            return py{ pytime::as_seconds_double(s.timeout) };
        }

        // (recv() and recv_into(): the number of bytes received (sock_recv_guts))
        long long sock_recv_guts(sock_state& s, char* aBuffer, long long aLength, int aFlags)
        {
            if (aLength == 0)
            {
                // (if 0 bytes were requested, do nothing)
                return 0;
            }
            long long result = 0;
            sock_call(s, false, [&](sock_state& aSock)
            {
#if defined(_WIN32)
                int const length = static_cast<int>(std::min<long long>(aLength, INT_MAX));
                result = ::recv(aSock.fd, aBuffer, length, aFlags);
#else
                result = ::recv(aSock.fd, aBuffer, static_cast<std::size_t>(aLength), aFlags);
#endif
                return result >= 0;
            });
            return result;
        }

        // (recvfrom() and recvfrom_into(): the number of bytes received and the sender's address (sock_recvfrom_guts))
        long long sock_recvfrom_guts(sock_state& s, char* aBuffer, long long aLength, int aFlags, py& aAddress)
        {
            socklen_type addrlen = getsockaddrlen(s);
            sock_addr addrbuf{};
            long long result = 0;
            sock_call(s, false, [&](sock_state& aSock)
            {
                std::memset(&addrbuf, 0, static_cast<std::size_t>(addrlen));
#if defined(_WIN32)
                int const length = static_cast<int>(std::min<long long>(aLength, INT_MAX));
                result = ::recvfrom(aSock.fd, aBuffer, length, aFlags, &addrbuf.sa, &addrlen);
#else
                result = ::recvfrom(aSock.fd, aBuffer, static_cast<std::size_t>(aLength), aFlags, &addrbuf.sa, &addrlen);
#endif
                return result >= 0;
            });
            aAddress = makesockaddr(&addrbuf.sa, static_cast<std::size_t>(addrlen));
            return result;
        }

        // (connect(): 0, or the error (aRaise: raised) (internal_connect))
        int internal_connect(sock_state& s, sockaddr const* aAddress, int aLength, bool aRaise)
        {
            socket_type const fd = s.fd;
            int const res = allow_threads([&]() { return ::connect(fd, aAddress, static_cast<socklen_type>(aLength)); });
            if (res == 0)
            {
                // (connect() succeeded, the socket is connected)
                return 0;
            }
            // (connect() failed: save error, PyErr_CheckSignals() can replace it)
            int err = sock_errno();
            bool waitConnect = false;
            if (err == kEINTR)
            {
                if (!signals_check())
                    throw raised{};
                // (issue #23618: when connect() fails with EINTR, the connection is running asynchronously; if the socket is blocking or has
                // a timeout, wait until the connection completes, fails or timed out using select(), and then get the connection status using
                // getsockopt(SO_ERROR); if the socket is non-blocking, raise InterruptedError: the caller is responsible to wait until the
                // connection completes, fails or timed out (it's the case in asyncio for example))
                waitConnect = s.timeout != 0;
            }
            else
                waitConnect = s.timeout > 0 && err == kSOCK_INPROGRESS_ERR;
            if (!waitConnect)
            {
                if (aRaise)
                {
                    // (restore error, maybe replaced by PyErr_CheckSignals())
                    set_sock_errno(err);
                    set_error();
                }
                return err;
            }
            auto const connected = [](sock_state& aSock)
            {
                int error = 0;
                socklen_type size = sizeof(error);
                if (::getsockopt(aSock.fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0)
                {
                    // (getsockopt() failed)
                    return false;
                }
                if (error == kEISCONN)
                    return true;
                if (error != 0)
                {
                    // (sock_call_ex() uses GET_SOCK_ERROR() to get the error code)
                    set_sock_errno(error);
                    return false;
                }
                return true;
            };
            if (aRaise)
            {
                // (socket.connect() raises an exception on error)
                sock_call_ex(s, true, connected, true, nullptr, s.timeout);
            }
            else
            {
                // (socket.connect_ex() returns the error code on error)
                if (!sock_call_ex(s, true, connected, true, &err, s.timeout))
                    return err;
            }
            return 0;
        }

        // (the socket's repr: "<socket object, fd=%ld, family=%d, type=%d, proto=%d>" (sock_repr))
        std::string sock_repr(sock_state const& s)
        {
            long long const fd = s.fd == kInvalidSocket ? -1 : static_cast<long long>(s.fd);
            return "<socket object, fd=" + std::to_string(fd) + ", family=" + std::to_string(s.family) + ", type=" + std::to_string(s.type) +
                ", proto=" + std::to_string(s.proto) + ">";
        }

        py socket_fd(socket_type aFd) { return py{ aFd == kInvalidSocket ? -1LL : static_cast<long long>(aFd) }; }

        // (PyLong_AsSocket_t)
        socket_type as_socket(py const& aValue)
        {
            auto const n = aValue.as_index();
#if defined(_WIN32)
            return static_cast<socket_type>(n);
#else
            if (n < INT_MIN || n > INT_MAX)
                py_raise("OverflowError", n < 0 ? "Python int too small to convert to C long" : "Python int too large to convert to C long");
            return static_cast<socket_type>(n);
#endif
        }

        // (socket.__init__(family=-1, type=-1, proto=-1, fileno=None) (sock_initobj))
        py sock_initobj(std::vector<py> const& aArgs, object* aKwargs)
        {
            if (aArgs.empty())
                py_raise("TypeError", "descriptor '__init__' of '_socket.socket' object needs an argument");
            py const self = aArgs[0];
            if (!isinstance(self, state().sockClass))
                py_raise("TypeError", "descriptor '__init__' requires a '_socket.socket' object but received a '" + type_name_of(self) + "'");
            auto const a = clinic("socket", aArgs, aKwargs, { "family", "type", "proto", "fileno" }, 0u, 0u, 1u);
            int family = a[0].empty() ? -1 : as_c_int(a[0]);
            int type = a[1].empty() ? -1 : as_c_int(a[1]);
            int proto = a[2].empty() ? -1 : as_c_int(a[2]);
            py const fdobj = a[3];
            auto& s = sock_of(self);
            socket_type fd = kInvalidSocket;
            // (a socket made, or of a descriptor, can access the host (as CPython's audit event socket.__new__))
            py_checked(host_unrestricted("socket.__new__"));
            if (!fdobj.empty() && !fdobj.is_none())
            {
                fd = as_socket(fdobj);
#if defined(_WIN32)
                if (fd == kInvalidSocket)
#else
                if (fd < 0)
#endif
                    py_raise("ValueError", "negative file descriptor");
                // (validate that passed file descriptor is valid and a socket)
                sock_addr addrbuf{};
                socklen_type addrlen = sizeof(sock_addr);
                if (::getsockname(fd, &addrbuf.sa, &addrlen) == 0)
                {
                    if (family == -1)
                        family = addrbuf.sa.sa_family;
                }
                else
                {
#if defined(_WIN32)
                    // (getsockname() on an unbound socket is an error on Windows; invalid descriptor and not a socket is same error code; error
                    // out if family must be resolved, or bad descriptor)
                    if (family == -1 || sock_errno() == WSAENOTSOCK)
#else
                    // (getsockname() is not supported for SOL_ALG on Linux)
                    if (family == -1 || errno == EBADF || errno == ENOTSOCK)
#endif
                        set_error();
                }
                if (type == -1)
                {
                    int tmp = 0;
                    socklen_type slen = sizeof(tmp);
                    if (::getsockopt(fd, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&tmp), &slen) == 0)
                        type = tmp;
                    else
                        set_error();
                }
#if defined(SO_PROTOCOL)
                if (proto == -1)
                {
                    int tmp = 0;
                    socklen_type slen = sizeof(tmp);
                    if (::getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, reinterpret_cast<char*>(&tmp), &slen) == 0)
                        proto = tmp;
                    else
                        set_error();
                }
#else
                proto = 0;
#endif
            }
            else
            {
                // (no fd, default to AF_INET and SOCK_STREAM)
                if (family == -1)
                    family = AF_INET;
                if (type == -1)
                    type = SOCK_STREAM;
                if (proto == -1)
                    proto = 0;
#if defined(_WIN32)
                fd = allow_threads([&]() { return ::WSASocketW(family, type, proto, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT); });
                if (fd == kInvalidSocket)
                    set_error();
#else
                auto& m = state();
                fd = allow_threads([&]()
                {
                    int result = -1;
#if defined(SOCK_CLOEXEC)
                    if (m.sockCloexecWorks != 0)
                    {
                        result = ::socket(family, type | SOCK_CLOEXEC, proto);
                        if (m.sockCloexecWorks == -1)
                        {
                            if (result >= 0)
                                m.sockCloexecWorks = 1;
                            else if (errno == EINVAL)
                            {
                                // (Linux older than 2.6.27 does not support SOCK_CLOEXEC)
                                m.sockCloexecWorks = 0;
                                result = ::socket(family, type, proto);
                            }
                        }
                    }
                    else
#endif
                        result = ::socket(family, type, proto);
                    return result;
                });
                if (fd == kInvalidSocket)
                    set_error();
                try
                {
#if defined(SOCK_CLOEXEC)
                    set_non_inheritable(fd, &m.sockCloexecWorks);
#else
                    set_non_inheritable(fd, nullptr);
#endif
                }
                catch (raised const&)
                {
                    socket_close(fd);
                    throw;
                }
#endif
            }
            try
            {
                init_sockobject(s, fd, family, type, proto);
            }
            catch (raised const&)
            {
                socket_close(fd);
                throw;
            }
            return none();
        }

        // (the socket's finalization: an unclosed socket is closed (ResourceWarning) (sock_finalize))
        py sock_finalize(std::vector<py> const& aArgs, object*)
        {
            if (aArgs.empty())
                return none();
            auto& sockets = state().sockets;
            auto const existing = sockets.find(aArgs[0].get());
            if (existing == sockets.end())
                return none();
            if (existing->second.fd != kInvalidSocket)
            {
                try
                {
                    import_attribute("warnings", "warn").call({ S("unclosed " + aArgs[0].repr()), builtin("ResourceWarning") },
                        { { "stacklevel", py{ 1 } }, { "source", aArgs[0] } });
                }
                catch (raised const&)
                {
                    // (spurious errors can appear at shutdown)
                    write_unraisable(aArgs[0].get());
                }
                // (only close the socket *after* logging the ResourceWarning warning to allow the logger to call socket methods like
                // socket.getsockname(); if the socket is closed before, socket methods fails with the EBADF error)
                auto const found = sockets.find(aArgs[0].get());
                if (found != sockets.end() && found->second.fd != kInvalidSocket)
                {
                    socket_type const fd = found->second.fd;
                    found->second.fd = kInvalidSocket;
                    // (we do not want to retry upon EINTR: see sock_close())
                    allow_threads([&]() { return socket_close(fd); });
                }
            }
            sockets.erase(aArgs[0].get());
            return none();
        }

        // (send(): the bytes sent)
        long long send_impl(sock_state& aSock, char const* aData, long long aLength, int aFlags)
        {
#if defined(_WIN32)
            int const length = static_cast<int>(std::min<long long>(aLength, INT_MAX));
            return ::send(aSock.fd, aData, length, aFlags);
#else
            return ::send(aSock.fd, aData, static_cast<std::size_t>(aLength), aFlags);
#endif
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        void make_socket_class()
        {
            auto const property = [](char const* aName, char const* aDoc, std::function<py(sock_state&)> aGetter)
            {
                py const getter = make_native_function(aName, std::string{ "socket." } + aName, "", sModule, [aName, aGetter](std::vector<py> const& aArgs, object*) -> py
                {
                    if (aArgs.empty() || !isinstance(aArgs[0], state().sockClass))
                        py_raise("TypeError", std::string{ "descriptor '" } + aName + "' for '_socket.socket' objects doesn't apply to a '" +
                            (aArgs.empty() ? std::string{ "NoneType" } : type_name_of(aArgs[0])) + "' object");
                    return aGetter(sock_of(aArgs[0]));
                });
                return builtin("property").call({ getter, none(), none(), S(aDoc) });
            };
            namespace_type ns = {
                { "__init__", make_native_function("__init__", "socket.__init__", "Initialize self.  See help(type(self)) for accurate signature.", sModule, sock_initobj) },
                { "__del__", make_native_function("__del__", "socket.__del__", "", sModule, sock_finalize) },
                { "__repr__", method("__repr__", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*) { return S(sock_repr(s)); }) },
                { "_accept", method("_accept", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*)
                    {
                        socklen_type addrlen = getsockaddrlen(s);
                        sock_addr addrbuf{};
                        socket_type newfd = kInvalidSocket;
                        auto& m = state();
                        sock_call(s, false, [&](sock_state& aSock)
                        {
                            addrlen = getsockaddrlen(aSock);
#if defined(__linux__) && defined(SOCK_CLOEXEC)
                            if (m.accept4Works != 0)
                            {
                                newfd = ::accept4(aSock.fd, &addrbuf.sa, &addrlen, SOCK_CLOEXEC);
                                if (newfd == kInvalidSocket && m.accept4Works == -1)
                                {
                                    // (on Linux older than 2.6.28, accept4() fails with ENOSYS)
                                    m.accept4Works = errno != ENOSYS ? 1 : 0;
                                }
                            }
                            if (m.accept4Works == 0)
                                newfd = ::accept(aSock.fd, &addrbuf.sa, &addrlen);
#else
                            (void)m;
                            newfd = ::accept(aSock.fd, &addrbuf.sa, &addrlen);
#endif
                            return newfd != kInvalidSocket;
                        });
                        try
                        {
#if defined(_WIN32)
                            set_non_inheritable(newfd, nullptr);
#elif defined(__linux__) && defined(SOCK_CLOEXEC)
                            if (m.accept4Works == 0)
                                set_non_inheritable(newfd, nullptr);
#else
                            set_non_inheritable(newfd, nullptr);
#endif
                        }
                        catch (raised const&)
                        {
                            socket_close(newfd);
                            throw;
                        }
                        py const sock = socket_fd(newfd);
                        py const addr = makesockaddr(&addrbuf.sa, static_cast<std::size_t>(addrlen));
                        return py_tuple({ sock, addr });
                    }) },
                { "bind", method("bind", arguments::One, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        sock_addr addrbuf{};
                        int const addrlen = getsockaddrarg(s, a[0], addrbuf, "bind");
                        socket_type const fd = s.fd;
                        int const res = allow_threads([&]() { return ::bind(fd, &addrbuf.sa, static_cast<socklen_type>(addrlen)); });
                        if (res < 0)
                            set_error();
                        return none();
                    }) },
                { "close", method("close", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*)
                    {
                        socket_type const fd = s.fd;
                        if (fd != kInvalidSocket)
                        {
                            s.fd = kInvalidSocket;
                            // (we do not want to retry upon EINTR: see http://lwn.net/Articles/576478/ and
                            // http://linux.derkeiler.com/Mailing-Lists/Kernel/2005-09/3000.html for more details)
                            int const res = allow_threads([&]() { return socket_close(fd); });
                            // (bpo-30319: the peer can already have closed the connection; Python ignores ECONNRESET on close())
                            if (res < 0 && sock_errno() != kECONNRESET)
                                set_error();
                        }
                        return none();
                    }) },
                { "connect", method("connect", arguments::One, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        sock_addr addrbuf{};
                        int const addrlen = getsockaddrarg(s, a[0], addrbuf, "connect");
                        internal_connect(s, &addrbuf.sa, addrlen, true);
                        return none();
                    }) },
                { "connect_ex", method("connect_ex", arguments::One, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        sock_addr addrbuf{};
                        int const addrlen = getsockaddrarg(s, a[0], addrbuf, "connect_ex");
                        return py{ static_cast<long long>(internal_connect(s, &addrbuf.sa, addrlen, false)) };
                    }) },
                { "detach", method("detach", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*)
                    {
                        socket_type const fd = s.fd;
                        s.fd = kInvalidSocket;
                        return socket_fd(fd);
                    }) },
                { "fileno", method("fileno", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*) { return socket_fd(s.fd); }) },
                { "getpeername", method("getpeername", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*)
                    {
                        socklen_type addrlen = getsockaddrlen(s);
                        sock_addr addrbuf{};
                        socket_type const fd = s.fd;
                        int const res = allow_threads([&]() { return ::getpeername(fd, &addrbuf.sa, &addrlen); });
                        if (res < 0)
                            set_error();
                        return makesockaddr(&addrbuf.sa, static_cast<std::size_t>(addrlen));
                    }) },
                { "getsockname", method("getsockname", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*)
                    {
                        socklen_type addrlen = getsockaddrlen(s);
                        sock_addr addrbuf{};
                        socket_type const fd = s.fd;
                        int const res = allow_threads([&]() { return ::getsockname(fd, &addrbuf.sa, &addrlen); });
                        if (res < 0)
                            set_error();
                        return makesockaddr(&addrbuf.sa, static_cast<std::size_t>(addrlen));
                    }) },
                { "getsockopt", method("getsockopt", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        check_count("getsockopt", a.size(), 2u, 3u);
                        int const level = as_int(a[0]);
                        int const optname = as_int(a[1]);
                        int const buflen = a.size() > 2u ? as_int(a[2]) : 0;
                        if (buflen == 0)
                        {
                            int flag = 0;
                            socklen_type flagsize = sizeof(flag);
                            if (::getsockopt(s.fd, level, optname, reinterpret_cast<char*>(&flag), &flagsize) < 0)
                                set_error();
                            return py{ static_cast<long long>(flag) };
                        }
                        if (buflen <= 0 || buflen > 1024)
                            py_raise("OSError", "getsockopt buflen out of range");
                        std::string buf(static_cast<std::size_t>(buflen), '\0');
                        socklen_type size = static_cast<socklen_type>(buflen);
                        if (::getsockopt(s.fd, level, optname, buf.data(), &size) < 0)
                            set_error();
                        buf.resize(static_cast<std::size_t>(size));
                        return py{ make_bytes(std::move(buf)) };
                    }) },
                { "listen", method("listen", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        // (we try to choose a default backlog high enough to avoid connection drops for common workloads, yet not too high to limit
                        // resource usage)
                        check_count("listen", a.size(), 0u, 1u);
                        int backlog = std::min(SOMAXCONN, 128);
                        if (!a.empty())
                            backlog = as_int(a[0]);
                        // (to avoid problems on systems that don't allow a negative backlog (which doesn't make sense anyway) we force a minimum value of 0)
                        if (backlog < 0)
                            backlog = 0;
                        socket_type const fd = s.fd;
                        int const res = allow_threads([&]() { return ::listen(fd, backlog); });
                        if (res < 0)
                            set_error();
                        return none();
                    }) },
                { "recv", method("recv", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        check_count("recv", a.size(), 1u, 2u);
                        long long const recvlen = as_ssize(a[0]);
                        int const flags = a.size() > 1u ? as_int(a[1]) : 0;
                        if (recvlen < 0)
                            py_raise("ValueError", "negative buffersize in recv");
                        std::string buf(static_cast<std::size_t>(recvlen), '\0');
                        auto const outlen = sock_recv_guts(s, buf.data(), recvlen, flags);
                        buf.resize(static_cast<std::size_t>(outlen));
                        return py{ make_bytes(std::move(buf)) };
                    }) },
                { "recv_into", method("recv_into", arguments::VarKeywords, [](sock_state& s, py const&, std::vector<py> const& aArgs, object* aKwargs)
                    {
                        auto const a = clinic("recv_into", aArgs, aKwargs, { "buffer", "nbytes", "flags" }, 1u, 0u);
                        auto const buffer = as_writable_buffer("recv_into", 1u, a[0]);
                        long long recvlen = a[1].empty() ? 0 : as_ssize(a[1]);
                        int const flags = a[2].empty() ? 0 : as_int(a[2]);
                        long long const buflen = static_cast<long long>(buffer.size);
                        if (recvlen < 0)
                            py_raise("ValueError", "negative buffersize in recv_into");
                        if (recvlen == 0)
                        {
                            // (if nbytes was not specified, use the buffer's length)
                            recvlen = buflen;
                        }
                        // (check if the buffer is large enough)
                        if (buflen < recvlen)
                            py_raise("ValueError", "buffer too small for requested bytes");
                        // (return the number of bytes read; note that we do not do anything special here in the case that readlen < recvlen)
                        return py{ sock_recv_guts(s, buffer.data, recvlen, flags) };
                    }) },
                { "recvfrom", method("recvfrom", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        check_count("recvfrom", a.size(), 1u, 2u);
                        long long const recvlen = as_ssize(a[0]);
                        int const flags = a.size() > 1u ? as_int(a[1]) : 0;
                        if (recvlen < 0)
                            py_raise("ValueError", "negative buffersize in recvfrom");
                        std::string buf(static_cast<std::size_t>(recvlen), '\0');
                        py addr;
                        auto const outlen = sock_recvfrom_guts(s, buf.data(), recvlen, flags, addr);
                        buf.resize(static_cast<std::size_t>(outlen));
                        return py_tuple({ py{ make_bytes(std::move(buf)) }, addr });
                    }) },
                { "recvfrom_into", method("recvfrom_into", arguments::VarKeywords, [](sock_state& s, py const&, std::vector<py> const& aArgs, object* aKwargs)
                    {
                        auto const a = clinic("recvfrom_into", aArgs, aKwargs, { "buffer", "nbytes", "flags" }, 1u, 0u);
                        auto const buffer = as_writable_buffer("recvfrom_into", 1u, a[0]);
                        long long recvlen = a[1].empty() ? 0 : as_ssize(a[1]);
                        int const flags = a[2].empty() ? 0 : as_int(a[2]);
                        long long const buflen = static_cast<long long>(buffer.size);
                        if (recvlen < 0)
                            py_raise("ValueError", "negative buffersize in recvfrom_into");
                        if (recvlen == 0)
                        {
                            // (if nbytes was not specified, use the buffer's length)
                            recvlen = buflen;
                        }
                        else if (recvlen > buflen)
                            py_raise("ValueError", "nbytes is greater than the length of the buffer");
                        py addr;
                        auto const readlen = sock_recvfrom_guts(s, buffer.data, recvlen, flags, addr);
                        return py_tuple({ py{ readlen }, addr });
                    }) },
                { "send", method("send", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        check_count("send", a.size(), 1u, 2u);
                        auto const data = as_buffer(a[0]);
                        int const flags = a.size() > 1u ? as_int(a[1]) : 0;
                        long long result = 0;
                        sock_call(s, true, [&](sock_state& aSock)
                        {
                            result = send_impl(aSock, data.data(), static_cast<long long>(data.size()), flags);
                            return result >= 0;
                        });
                        return py{ result };
                    }) },
                { "sendall", method("sendall", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        check_count("sendall", a.size(), 1u, 2u);
                        auto const data = as_buffer(a[0]);
                        int const flags = a.size() > 1u ? as_int(a[1]) : 0;
                        char const* buf = data.data();
                        long long len = static_cast<long long>(data.size());
                        bool const hasTimeout = s.timeout > 0;
                        pytime::time_type timeout = s.timeout;
                        pytime::time_type deadline = 0;
                        bool deadlineInitialized = false;
                        do
                        {
                            if (hasTimeout)
                            {
                                if (deadlineInitialized)
                                    timeout = pytime::deadline_get(deadline);   // (recompute the timeout)
                                else
                                {
                                    deadlineInitialized = true;
                                    deadline = pytime::deadline_init(timeout);
                                }
                                if (timeout <= 0)
                                    py_raise("TimeoutError", "timed out");
                            }
                            long long n = 0;
                            sock_call_ex(s, true, [&](sock_state& aSock)
                            {
                                n = send_impl(aSock, buf, len, flags);
                                return n >= 0;
                            }, false, nullptr, timeout);
                            buf += n;
                            len -= n;
                            // (we must run our signal handlers before looping again: send() can return a successful partial write when it is
                            // interrupted, so we can't restrict ourselves to EINTR)
                            if (!signals_check())
                                throw raised{};
                        } while (len > 0);
                        return none();
                    }) },
                { "sendto", method("sendto", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        if (a.size() != 2u && a.size() != 3u)
                            py_raise("TypeError", "sendto() takes 2 or 3 arguments (" + std::to_string(a.size()) + " given)");
                        auto const data = as_buffer(a[0]);
                        int const flags = a.size() == 3u ? as_int(a[1]) : 0;
                        py const addro = a.back();
                        sock_addr addrbuf{};
                        int const addrlen = getsockaddrarg(s, addro, addrbuf, "sendto");
                        long long result = 0;
                        sock_call(s, true, [&](sock_state& aSock)
                        {
#if defined(_WIN32)
                            int const length = static_cast<int>(std::min<std::size_t>(data.size(), INT_MAX));
                            result = ::sendto(aSock.fd, data.data(), length, flags, &addrbuf.sa, addrlen);
#else
                            result = ::sendto(aSock.fd, data.data(), data.size(), flags, &addrbuf.sa, static_cast<socklen_type>(addrlen));
#endif
                            return result >= 0;
                        });
                        return py{ result };
                    }) },
                { "setblocking", method("setblocking", arguments::One, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        bool const block = a[0].truthy();
                        s.timeout = block ? -pytime::kSecondToNs : 0;
                        internal_setblocking(s, block);
                        return none();
                    }) },
                { "getblocking", method("getblocking", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*)
                    {
                        return py::boolean(s.timeout != 0);
                    }) },
                { "settimeout", method("settimeout", arguments::One, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        auto const timeout = socket_parse_timeout(a[0]);
                        s.timeout = timeout;
                        // (blocking mode for a Python socket object means that operations like recv() or sendall() will block the execution of the
                        // current thread until they are complete or aborted with a TimeoutError or socket.error errors; when timeout is None, the
                        // underlying FD is in a blocking mode; when timeout is a positive number, the FD is in a non-blocking mode, and socket ops
                        // are implemented with a select() call; when timeout is 0.0, the FD is in a non-blocking mode)
                        internal_setblocking(s, timeout < 0);
                        return none();
                    }) },
                { "gettimeout", method("gettimeout", arguments::None, [](sock_state& s, py const&, std::vector<py> const&, object*) { return sock_gettimeout(s); }) },
                { "setsockopt", method("setsockopt", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        int res = 0;
                        // (setsockopt(level, opt, flag))
                        if (a.size() == 3u && is_int_like(a[2].get()))
                        {
                            int const level = as_int(a[0]);
                            int const optname = as_int(a[1]);
                            int const flag = as_int(a[2]);
                            res = ::setsockopt(s.fd, level, optname, reinterpret_cast<char const*>(&flag), sizeof(flag));
                        }
                        // (setsockopt(level, opt, None, flag))
                        else if (a.size() == 4u && a[2].is_none())
                        {
                            int const level = as_int(a[0]);
                            int const optname = as_int(a[1]);
                            unsigned int const optlen = as_unsigned_int(a[3]);
                            res = ::setsockopt(s.fd, level, optname, nullptr, static_cast<socklen_type>(optlen));
                        }
                        // (setsockopt(level, opt, buffer))
                        else
                        {
                            check_count("setsockopt", a.size(), 3u, 3u);
                            int const level = as_int(a[0]);
                            int const optname = as_int(a[1]);
                            auto const optval = as_buffer(a[2]);
#if defined(_WIN32)
                            if (optval.size() > static_cast<std::size_t>(INT_MAX))
                                py_raise("OverflowError", "socket option is larger than " + std::to_string(INT_MAX) + " bytes");
                            res = ::setsockopt(s.fd, level, optname, optval.data(), static_cast<int>(optval.size()));
#else
                            res = ::setsockopt(s.fd, level, optname, optval.data(), static_cast<socklen_type>(optval.size()));
#endif
                        }
                        if (res < 0)
                            set_error();
                        return none();
                    }) },
                { "shutdown", method("shutdown", arguments::One, [](sock_state& s, py const&, std::vector<py> const& a, object*)
                    {
                        int const how = as_c_int(a[0]);
                        socket_type const fd = s.fd;
                        int const res = allow_threads([&]() { return ::shutdown(fd, how); });
                        if (res < 0)
                            set_error();
                        return none();
                    }) },
                { "family", property("family", "the socket family", [](sock_state& s) { return py{ static_cast<long long>(s.family) }; }) },
                { "type", property("type", "the socket type", [](sock_state& s) { return py{ static_cast<long long>(s.type) }; }) },
                { "proto", property("proto", "the socket protocol", [](sock_state& s) { return py{ static_cast<long long>(s.proto) }; }) },
                { "timeout", property("timeout", "the socket timeout", [](sock_state& s) { return sock_gettimeout(s); }) }
            };
#if defined(NEOS_PY_SOCKET_CMSG)
            ns.emplace_back("recvmsg", method("recvmsg", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
            {
                check_count("recvmsg", a.size(), 1u, 3u);
                long long const bufsize = as_ssize(a[0]);
                long long const ancbufsize = a.size() > 1u ? as_ssize(a[1]) : 0;
                int const flags = a.size() > 2u ? as_int(a[2]) : 0;
                if (bufsize < 0)
                    py_raise("ValueError", "negative buffer size in recvmsg()");
                std::string buf(static_cast<std::size_t>(bufsize), '\0');
                std::vector<iovec> iov(1u);
                iov[0].iov_base = buf.data();
                iov[0].iov_len = buf.size();
                return sock_recvmsg_guts(s, iov, flags, ancbufsize, [&](long long aReceived)
                {
                    if (aReceived < bufsize)
                        buf.resize(static_cast<std::size_t>(aReceived));
                    return py{ make_bytes(buf) };
                });
            }));
            ns.emplace_back("recvmsg_into", method("recvmsg_into", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
            {
                check_count("recvmsg_into", a.size(), 1u, 3u);
                long long const ancbufsize = a.size() > 1u ? as_ssize(a[1]) : 0;
                int const flags = a.size() > 2u ? as_int(a[2]) : 0;
                auto const buffers = sequence_items(a[0], "recvmsg_into() argument 1 must be an iterable");
                if (buffers.size() > static_cast<std::size_t>(INT_MAX))
                    py_raise("OSError", "recvmsg_into() argument 1 is too long");
                // (fill in an iovec for each item)
                std::vector<iovec> iovs(buffers.size());
                for (std::size_t i = 0u; i < buffers.size(); ++i)
                {
                    buffer_info buffer;
                    if (!buffer_of(buffers[i].get(), buffer) || buffer.readonly)
                        py_raise("TypeError", "recvmsg_into() argument 1 must be an iterable of single-segment read-write buffers");
                    iovs[i].iov_base = buffer.data;
                    iovs[i].iov_len = buffer.size;
                }
                return sock_recvmsg_guts(s, iovs, flags, ancbufsize, [](long long aReceived) { return py{ aReceived }; });
            }));
            ns.emplace_back("sendmsg", method("sendmsg", arguments::Var, [](sock_state& s, py const&, std::vector<py> const& a, object*)
            {
                check_count("sendmsg", a.size(), 1u, 4u);
                int const flags = a.size() > 2u ? as_int(a[2]) : 0;
                msghdr msg{};
                // (parse destination address)
                sock_addr addrbuf{};
                if (a.size() > 3u && !a[3].is_none())
                {
                    int const addrlen = getsockaddrarg(s, a[3], addrbuf, "sendmsg");
                    msg.msg_name = &addrbuf;
                    msg.msg_namelen = static_cast<socklen_t>(addrlen);
                }
                // (fill in an iovec for each message part (sock_sendmsg_iovec))
                auto const dataParts = sequence_items(a[0], "sendmsg() argument 1 must be an iterable");
                if (dataParts.size() > static_cast<std::size_t>(INT_MAX))
                    py_raise("OSError", "sendmsg() argument 1 is too long");
                std::vector<iovec> iovs(dataParts.size());
                for (std::size_t i = 0u; i < dataParts.size(); ++i)
                {
                    auto const data = as_buffer(dataParts[i]);
                    iovs[i].iov_base = const_cast<char*>(data.data());
                    iovs[i].iov_len = data.size();
                }
                msg.msg_iov = iovs.empty() ? nullptr : iovs.data();
                msg.msg_iovlen = iovs.size();
                std::vector<py> cmsgItems;
                if (a.size() > 1u)
                    cmsgItems = sequence_items(a[1], "sendmsg() argument 2 must be an iterable");
#if !defined(CMSG_SPACE)
                if (cmsgItems.size() > 1u)
                    py_raise("OSError", "sending multiple control messages is not supported on this system");
#endif
                // (save level, type and data for each control message, and calculate total size)
                struct cmsginfo
                {
                    int level;
                    int type;
                    std::string_view data;
                };
                std::vector<cmsginfo> cmsgs;
                std::size_t controllen = 0u;
                std::size_t controllenLast = 0u;
                for (auto const& item : cmsgItems)
                {
                    char const* const where = "[sendmsg() ancillary data items]()";
                    // (PyArg_Parse's "(iiy*)": a sequence (not bytes) of 3 items)
                    if (!builtin("hasattr")(item, S("__len__")).truthy() || !builtin("hasattr")(item, S("__getitem__")).truthy() ||
                        isinstance(item, builtin("bytes")) || isinstance(item, builtin("dict")))
                        py_raise("TypeError", std::string{ where } + " argument must be 3-item sequence, not " + type_name_of(item));
                    auto const parts = item.items();
                    if (parts.size() != 3u)
                        py_raise("TypeError", std::string{ where } + " argument must be sequence of length 3, not " + std::to_string(parts.size()));
                    cmsgs.push_back(cmsginfo{ as_int(parts[0]), as_int(parts[1]), as_buffer(parts[2]) });
                    std::size_t const bufsize = cmsgs.back().data.size();
                    std::size_t space = 0u;
#if defined(CMSG_SPACE)
                    if (!get_CMSG_SPACE(bufsize, space))
#else
                    if (!get_CMSG_LEN(bufsize, space))
#endif
                        py_raise("OSError", "ancillary data item too large");
                    controllen += space;
                    if (controllen > SOCKLEN_T_LIMIT || controllen < controllenLast)
                        py_raise("OSError", "too much ancillary data");
                    controllenLast = controllen;
                }
                // (construct ancillary data block from control message info; need to zero out the buffer as a workaround for glibc's CMSG_NXTHDR()
                // implementation: after getting the pointer to the next header, it checks its (uninitialized) cmsg_len member to see if the
                // "message" fits in the buffer, and returns NULL if it doesn't; zero-filling the buffer ensures that this doesn't happen)
                std::vector<char> controlbuf(controllen, '\0');
                if (!cmsgs.empty())
                {
                    msg.msg_control = controlbuf.data();
                    msg.msg_controllen = static_cast<decltype(msg.msg_controllen)>(controllen);
                    cmsghdr* cmsgh = nullptr;
                    for (std::size_t i = 0u; i < cmsgs.size(); ++i)
                    {
                        std::size_t const dataLen = cmsgs[i].data.size();
                        cmsgh = i == 0u ? CMSG_FIRSTHDR(&msg) : CMSG_NXTHDR(&msg, cmsgh);
                        if (cmsgh == nullptr)
                            py_raise("RuntimeError", std::string{ "unexpected NULL result from " } + (i == 0u ? "CMSG_FIRSTHDR" : "CMSG_NXTHDR") + "()");
                        std::size_t msgLen = 0u;
                        if (!get_CMSG_LEN(dataLen, msgLen))
                            py_raise("RuntimeError", "item size out of range for CMSG_LEN()");
                        bool enoughSpace = false;
                        if (cmsg_min_space(&msg, cmsgh, msgLen))
                        {
                            cmsgh->cmsg_len = static_cast<decltype(cmsgh->cmsg_len)>(msgLen);
                            std::size_t space = 0u;
                            if (get_cmsg_data_space(&msg, cmsgh, space))
                                enoughSpace = space >= dataLen;
                        }
                        if (!enoughSpace)
                            py_raise("RuntimeError", "ancillary data does not fit in calculated space");
                        cmsgh->cmsg_level = cmsgs[i].level;
                        cmsgh->cmsg_type = cmsgs[i].type;
                        std::memcpy(CMSG_DATA(cmsgh), cmsgs[i].data.data(), dataLen);
                    }
                }
                // (make the system call)
                long long result = 0;
                sock_call(s, true, [&](sock_state& aSock)
                {
                    result = ::sendmsg(aSock.fd, &msg, flags);
                    return result >= 0;
                });
                return py{ result };
            }));
#endif
            py const nsDict = py_dict();
            nsDict.set_item(S("__module__"), S(sModule));
            nsDict.set_item(S("__qualname__"), S("socket"));
            nsDict.set_item(S("__doc__"), S(socket_docs::doc("_socket.socket")));
            for (auto const& [name, value] : ns)
                nsDict.set_item(S(name), value);
            state().sockClass = builtin("type")(S("socket"), py_tuple({ builtin("object") }), nsDict);
        }

        // --- the module's functions

        using function_body = std::function<py(std::vector<py> const& aArgs, object* aKwargs)>;

        // (a function of the module (aArguments: as CPython's (METH_NOARGS, METH_O, METH_VARARGS) or a clinic function's (VarKeywords: it parses
        // its arguments)))
        py function(char const* aName, arguments aArguments, function_body aBody)
        {
            std::string const name = aName;
            return make_native_function(name, name, socket_docs::doc(("_socket." + name).c_str()), sModule,
                [name, aArguments, aBody](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    if (aArguments != arguments::VarKeywords && has_keywords(aKwargs))
                        py_raise("TypeError", name + "() takes no keyword arguments");
                    if (aArguments == arguments::None && !aArgs.empty())
                        py_raise("TypeError", name + "() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                    if (aArguments == arguments::One && aArgs.size() != 1u)
                        py_raise("TypeError", name + "() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                    return aBody(aArgs, aKwargs);
                });
        }

#if !defined(_WIN32)
        // (an herror (set_herror))
        [[noreturn]] void set_herror(int aError)
        {
            py_raise(state().herror(py{ aError }, S(::hstrerror(aError))));
        }
#else
        [[noreturn]] void set_herror(int aError)
        {
            py_raise(state().herror(py{ aError }, S("host not found")));
        }
#endif

        // (a host's name, aliases and addresses (common to gethostbyname_ex and gethostbyaddr) (gethost_common))
        py gethost_common(hostent const* aHost, int aFamily, int aError)
        {
            if (aHost == nullptr)
            {
                // (let's get real error message to return)
                set_herror(aError);
            }
            if (aHost->h_addrtype != aFamily)
            {
                // (let's get real error message to return)
                raise_os_error(EAFNOSUPPORT);
                throw raised{};
            }
            py const nameList = py_list();
            py const addrList = py_list();
            // (SF #1511317: h_aliases can be NULL)
            if (aHost->h_aliases != nullptr)
                for (char** pch = aHost->h_aliases; *pch != nullptr; ++pch)
                    nameList.method("append", { S(*pch) });
            for (char** pch = aHost->h_addr_list; *pch != nullptr; ++pch)
            {
                switch (aFamily)
                {
                case AF_INET:
                    {
                        sockaddr_in sin{};
                        sin.sin_family = static_cast<decltype(sin.sin_family)>(aFamily);
                        std::memcpy(&sin.sin_addr, *pch, sizeof(sin.sin_addr));
                        addrList.method("append", { make_ipv4_addr(&sin) });
                    }
                    break;
                case AF_INET6:
                    {
                        sockaddr_in6 sin6{};
                        sin6.sin6_family = static_cast<decltype(sin6.sin6_family)>(aFamily);
                        std::memcpy(&sin6.sin6_addr, *pch, sizeof(sin6.sin6_addr));
                        addrList.method("append", { make_ipv6_addr(&sin6) });
                    }
                    break;
                default:    // (can't happen)
                    py_raise("OSError", "unsupported address family");
                }
            }
            return py_tuple({ S(aHost->h_name), nameList, addrList });
        }

        // (gethostbyname_ex()'s and gethostbyaddr()'s calls: Linux's are reentrant (gethostbyname_r); Windows': per thread)
        hostent const* host_by_name(std::string const& aName, hostent& aBuffer, std::vector<char>& aData, int& aError)
        {
#if defined(__linux__)
            hostent* h = nullptr;
            ::gethostbyname_r(aName.c_str(), &aBuffer, aData.data(), aData.size() - 1u, &h, &aError);
            return h;
#elif defined(_WIN32)
            (void)aBuffer;
            (void)aData;
            hostent const* const h = ::gethostbyname(aName.c_str());
            aError = ::WSAGetLastError();
            return h;
#else
            (void)aBuffer;
            (void)aData;
            hostent const* const h = ::gethostbyname(aName.c_str());
            aError = h_errno;
            return h;
#endif
        }
        hostent const* host_by_address(char const* aAddress, int aLength, int aFamily, hostent& aBuffer, std::vector<char>& aData, int& aError)
        {
#if defined(__linux__)
            hostent* h = nullptr;
            ::gethostbyaddr_r(aAddress, static_cast<socklen_t>(aLength), aFamily, &aBuffer, aData.data(), aData.size() - 1u, &h, &aError);
            return h;
#elif defined(_WIN32)
            (void)aBuffer;
            (void)aData;
            hostent const* const h = ::gethostbyaddr(aAddress, aLength, aFamily);
            aError = ::WSAGetLastError();
            return h;
#else
            (void)aBuffer;
            (void)aData;
            hostent const* const h = ::gethostbyaddr(aAddress, static_cast<socklen_t>(aLength), aFamily);
            aError = h_errno;
            return h;
#endif
        }

        // (ntohl() and htonl())
        py long_32(std::vector<py> const& aArgs, bool aToHost)
        {
            py const& arg = aArgs[0];
            if (!is_int_like(arg.get()))
                py_raise("TypeError", "expected int, " + type_name_of(arg) + " found");
            if (arg < py{ 0 })
                py_raise("OverflowError", "can't convert negative int to unsigned");
            // (only want the trailing 32 bits)
            if (arg > py{ 0xFFFFFFFFLL })
                py_raise("OverflowError", "int larger than 32 bits");
            auto const x = static_cast<std::uint32_t>(arg.as_index());
            return py{ static_cast<long long>(aToHost ? ntohl(x) : htonl(x)) };
        }

        // (ntohs() and htons())
        py short_16(char const* aName, std::vector<py> const& aArgs, object* aKwargs, bool aToHost)
        {
            auto const a = clinic(aName, aArgs, aKwargs, { "" }, 1u, 1u);
            int const x = as_c_int(a[0]);
            if (x < 0)
                py_raise("OverflowError", std::string{ aName } + ": can't convert negative Python int to C 16-bit unsigned integer");
            if (x > 0xffff)
                py_raise("OverflowError", std::string{ aName } + ": Python int too large to convert to C 16-bit unsigned integer");
            auto const v = static_cast<unsigned short>(x);
            return py{ static_cast<long long>(aToHost ? ntohs(v) : htons(v)) };
        }

        void add_functions(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
            add("gethostbyname", function("gethostbyname", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("gethostbyname", a.size(), 1u, 1u);
                std::string const name = as_idna("gethostbyname", a[0]);
                py_checked(host_unrestricted("socket.gethostbyname"));
                sockaddr_in addrbuf{};
                setipaddr(name, reinterpret_cast<sockaddr*>(&addrbuf), sizeof(addrbuf), AF_INET);
                return make_ipv4_addr(&addrbuf);
            }));
            add("gethostbyname_ex", function("gethostbyname_ex", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("gethostbyname_ex", a.size(), 1u, 1u);
                std::string const name = as_idna("gethostbyname_ex", a[0]);
                py_checked(host_unrestricted("socket.gethostbyname"));
                sock_addr addr{};
                setipaddr(name, &addr.sa, sizeof(addr), AF_INET);
                hostent buffer{};
                std::vector<char> data(16384u);
                int error = 0;
                hostent const* const h = allow_threads([&]() { return host_by_name(name, buffer, data, error); });
                // (some C libraries would require addr.__ss_family instead of addr.ss_family: therefore, we cast the sockaddr_storage into
                // sockaddr to access sa_family)
                return gethost_common(h, addr.sa.sa_family, error);
            }));
            add("gethostbyaddr", function("gethostbyaddr", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("gethostbyaddr", a.size(), 1u, 1u);
                std::string const ipNum = as_idna("gethostbyaddr", a[0]);
                py_checked(host_unrestricted("socket.gethostbyaddr"));
                sock_addr addr{};
                setipaddr(ipNum, &addr.sa, sizeof(addr), AF_UNSPEC);
                int const af = addr.sa.sa_family;
                char const* ap = nullptr;
                int al = 0;
                switch (af)
                {
                case AF_INET:
                    ap = reinterpret_cast<char const*>(&addr.in.sin_addr);
                    al = sizeof(addr.in.sin_addr);
                    break;
                case AF_INET6:
                    ap = reinterpret_cast<char const*>(&addr.in6.sin6_addr);
                    al = sizeof(addr.in6.sin6_addr);
                    break;
                default:
                    py_raise("OSError", "unsupported address family");
                }
                hostent buffer{};
                std::vector<char> data(16384u);
                int error = 0;
                hostent const* const h = allow_threads([&]() { return host_by_address(ap, al, af, buffer, data, error); });
                return gethost_common(h, af, error);
            }));
            add("gethostname", function("gethostname", arguments::None, [](std::vector<py> const&, object*)
            {
#if defined(_WIN32)
                // (don't use winsock's gethostname, as this returns the ANSI version of the hostname, whereas we need a Unicode string;
                // otherwise, gethostname apparently also returns the DNS name)
                std::vector<wchar_t> buf(MAX_COMPUTERNAME_LENGTH + 1);
                DWORD size = static_cast<DWORD>(buf.size());
                if (!::GetComputerNameExW(ComputerNamePhysicalDnsHostname, buf.data(), &size))
                {
                    if (::GetLastError() != ERROR_MORE_DATA)
                    {
                        sockets::raise_windows_error(static_cast<int>(::GetLastError()));
                        throw raised{};
                    }
                    if (size == 0)
                        return S("");
                    // (MSDN says ERROR_MORE_DATA may occur because DNS allows longer names)
                    buf.resize(size);
                    if (!::GetComputerNameExW(ComputerNamePhysicalDnsHostname, buf.data(), &size))
                    {
                        sockets::raise_windows_error(static_cast<int>(::GetLastError()));
                        throw raised{};
                    }
                }
                auto const length = ::WideCharToMultiByte(CP_UTF8, 0, buf.data(), static_cast<int>(size), nullptr, 0, nullptr, nullptr);
                std::string name(static_cast<std::size_t>(length), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, buf.data(), static_cast<int>(size), name.data(), length, nullptr, nullptr);
                return S(name);
#else
                char buf[1024];
                int const res = allow_threads([&]() { return ::gethostname(buf, static_cast<int>(sizeof(buf)) - 1); });
                if (res < 0)
                    set_error();
                buf[sizeof(buf) - 1u] = '\0';
                return fs_decode(B(buf, std::strlen(buf)));
#endif
            }));
#if !defined(_WIN32)
            add("sethostname", function("sethostname", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("sethostname", a.size(), 1u, 1u);
                py const hnobj = isinstance(a[0], builtin("str")) ? fs_encode(a[0]) : a[0];
                py_checked(host_unrestricted("socket.sethostname"));
                auto const name = as_buffer(hnobj);
                if (::sethostname(name.data(), name.size()) != 0)
                    set_error();
                return none();
            }));
#endif
            add("getservbyname", function("getservbyname", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("getservbyname", a.size(), 1u, 2u);
                std::string const name = as_str("getservbyname", 1u, a[0]);
                std::string const proto = a.size() > 1u ? as_str("getservbyname", 2u, a[1]) : std::string{};
                bool const hasProto = a.size() > 1u;
                servent const* const sp = allow_threads([&]() { return ::getservbyname(name.c_str(), hasProto ? proto.c_str() : nullptr); });
                if (sp == nullptr)
                    py_raise("OSError", "service/proto not found");
                return py{ static_cast<long long>(ntohs(static_cast<unsigned short>(sp->s_port))) };
            }));
            add("getservbyport", function("getservbyport", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("getservbyport", a.size(), 1u, 2u);
                int const port = as_int(a[0]);
                std::string const proto = a.size() > 1u ? as_str("getservbyport", 2u, a[1]) : std::string{};
                bool const hasProto = a.size() > 1u;
                if (port < 0 || port > 0xffff)
                    py_raise("OverflowError", "getservbyport: port must be 0-65535.");
                servent const* const sp = allow_threads([&]() { return ::getservbyport(htons(static_cast<unsigned short>(port)), hasProto ? proto.c_str() : nullptr); });
                if (sp == nullptr)
                    py_raise("OSError", "port/proto not found");
                return S(sp->s_name);
            }));
            add("getprotobyname", function("getprotobyname", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("getprotobyname", a.size(), 1u, 1u);
                std::string const name = as_str("getprotobyname", 1u, a[0]);
                protoent const* const sp = allow_threads([&]() { return ::getprotobyname(name.c_str()); });
                if (sp == nullptr)
                    py_raise("OSError", "protocol not found");
                return py{ static_cast<long long>(sp->p_proto) };
            }));
            add("close", function("close", arguments::One, [](std::vector<py> const& a, object*)
            {
                socket_type const fd = as_socket(a[0]);
                int const res = allow_threads([&]() { return socket_close(fd); });
                // (bpo-30319: the peer can already have closed the connection; Python ignores ECONNRESET on close())
                if (res < 0 && sock_errno() != kECONNRESET)
                    set_error();
                return none();
            }));
            add("dup", function("dup", arguments::One, [](std::vector<py> const& a, object*)
            {
                socket_type const fd = as_socket(a[0]);
#if defined(_WIN32)
                WSAPROTOCOL_INFOW info{};
                if (::WSADuplicateSocketW(fd, ::GetCurrentProcessId(), &info))
                    set_error();
                socket_type const newfd = ::WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &info, 0, WSA_FLAG_OVERLAPPED);
                if (newfd == kInvalidSocket)
                    set_error();
                try
                {
                    set_non_inheritable(newfd, nullptr);
                }
                catch (raised const&)
                {
                    ::closesocket(newfd);
                    throw;
                }
#else
                // (on UNIX, dup can be used to duplicate the file descriptor of a socket (_Py_dup: non-inheritable))
                socket_type const newfd = allow_threads([&]() { return ::fcntl(fd, F_DUPFD_CLOEXEC, 0); });
                if (newfd < 0)
                {
                    raise_os_error(errno);
                    throw raised{};
                }
#endif
                return socket_fd(newfd);
            }));
#if !defined(_WIN32)
            add("socketpair", function("socketpair", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("socketpair", a.size(), 0u, 3u);
                int family = AF_UNIX;
                int type = SOCK_STREAM;
                int proto = 0;
                if (a.size() > 0u)
                    family = as_int(a[0]);
                if (a.size() > 1u)
                    type = as_int(a[1]);
                if (a.size() > 2u)
                    proto = as_int(a[2]);
                py_checked(host_unrestricted("socket.socketpair"));
                // (create a pair of socket fds)
                auto& m = state();
                int sv[2] = { -1, -1 };
                int const ret = allow_threads([&]()
                {
                    int result = -1;
#if defined(SOCK_CLOEXEC)
                    if (m.sockCloexecWorks != 0)
                    {
                        result = ::socketpair(family, type | SOCK_CLOEXEC, proto, sv);
                        if (m.sockCloexecWorks == -1)
                        {
                            if (result >= 0)
                                m.sockCloexecWorks = 1;
                            else if (errno == EINVAL)
                            {
                                // (Linux older than 2.6.27 does not support SOCK_CLOEXEC)
                                m.sockCloexecWorks = 0;
                                result = ::socketpair(family, type, proto, sv);
                            }
                        }
                    }
                    else
#endif
                        result = ::socketpair(family, type, proto, sv);
                    return result;
                });
                if (ret < 0)
                    set_error();
                py s0, s1;
                try
                {
#if defined(SOCK_CLOEXEC)
                    set_non_inheritable(sv[0], &m.sockCloexecWorks);
                    set_non_inheritable(sv[1], &m.sockCloexecWorks);
#else
                    set_non_inheritable(sv[0], nullptr);
                    set_non_inheritable(sv[1], nullptr);
#endif
                    s0 = new_sockobject(sv[0], family, type, proto);
                    s1 = new_sockobject(sv[1], family, type, proto);
                }
                catch (raised const&)
                {
                    if (s0.empty())
                        socket_close(sv[0]);
                    if (s1.empty())
                        socket_close(sv[1]);
                    throw;
                }
                return py_tuple({ s0, s1 });
            }));
#endif
            add("ntohs", function("ntohs", arguments::VarKeywords, [](std::vector<py> const& a, object* aKwargs) { return short_16("ntohs", a, aKwargs, true); }));
            add("ntohl", function("ntohl", arguments::One, [](std::vector<py> const& a, object*) { return long_32(a, true); }));
            add("htons", function("htons", arguments::VarKeywords, [](std::vector<py> const& a, object* aKwargs) { return short_16("htons", a, aKwargs, false); }));
            add("htonl", function("htonl", arguments::One, [](std::vector<py> const& a, object*) { return long_32(a, false); }));
            add("inet_aton", function("inet_aton", arguments::VarKeywords, [](std::vector<py> const& aArgs, object* aKwargs)
            {
                auto const a = clinic("inet_aton", aArgs, aKwargs, { "" }, 1u, 1u);
                if (!isinstance(a[0], builtin("str")))
                    py_raise("TypeError", "inet_aton() argument must be str, not " + type_name_of(a[0]));
                std::string const ipAddr = a[0].str();
                if (ipAddr.find('\0') != std::string::npos)
                    py_raise("ValueError", "embedded null character");
#if !defined(_WIN32)
                in_addr buf{};
                if (::inet_aton(ipAddr.c_str(), &buf))
                    return B(reinterpret_cast<char const*>(&buf), sizeof(buf));
                py_raise("OSError", "illegal IP address string passed to inet_aton");
#else
                // (have to use inet_addr() instead; special-case this address as inet_addr might return INADDR_NONE for this)
                unsigned long packedAddr = 0;
                if (ipAddr == "255.255.255.255")
                    packedAddr = INADDR_BROADCAST;
                else
                {
                    in_addr buf{};
                    if (::inet_pton(AF_INET, ipAddr.c_str(), &buf) != 1)
                        py_raise("OSError", "illegal IP address string passed to inet_aton");
                    packedAddr = buf.s_addr;
                }
                auto const packed = static_cast<std::uint32_t>(packedAddr);
                return B(reinterpret_cast<char const*>(&packed), sizeof(packed));
#endif
            }));
            add("inet_ntoa", function("inet_ntoa", arguments::VarKeywords, [](std::vector<py> const& aArgs, object* aKwargs)
            {
                auto const a = clinic("inet_ntoa", aArgs, aKwargs, { "" }, 1u, 1u);
                auto const packedIp = as_buffer(a[0]);
                sockaddr_in sin{};
                if (packedIp.size() != sizeof(sin.sin_addr))
                    py_raise("OSError", "packed IP wrong length for inet_ntoa");
                std::memcpy(&sin.sin_addr, packedIp.data(), packedIp.size());
                return make_ipv4_addr(&sin);
            }));
            add("inet_pton", function("inet_pton", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("inet_pton", a.size(), 2u, 2u);
                int const af = as_int(a[0]);
                std::string const ip = as_str("inet_pton", 2u, a[1]);
                char packed[sizeof(in6_addr) > sizeof(in_addr) ? sizeof(in6_addr) : sizeof(in_addr)] = {};
                int const retval = ::inet_pton(af, ip.c_str(), packed);
                if (retval < 0)
                {
                    raise_os_error(errno);
                    throw raised{};
                }
                if (retval == 0)
                    py_raise("OSError", "illegal IP address string passed to inet_pton");
                if (af == AF_INET)
                    return B(packed, sizeof(in_addr));
                if (af == AF_INET6)
                    return B(packed, sizeof(in6_addr));
                py_raise("OSError", "unknown address family");
            }));
            add("inet_ntop", function("inet_ntop", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("inet_ntop", a.size(), 2u, 2u);
                int const af = as_int(a[0]);
                auto const packedIp = as_buffer(a[1]);
                if (af == AF_INET)
                {
                    if (packedIp.size() != sizeof(in_addr))
                        py_raise("ValueError", "invalid length of packed IP address string");
                }
                else if (af == AF_INET6)
                {
                    if (packedIp.size() != sizeof(in6_addr))
                        py_raise("ValueError", "invalid length of packed IP address string");
                }
                else
                    py_raise("ValueError", "unknown address family " + std::to_string(af));
                // (inet_ntop guarantee NUL-termination of resulting string)
                char ip[INET6_ADDRSTRLEN > INET_ADDRSTRLEN ? INET6_ADDRSTRLEN : INET_ADDRSTRLEN];
                std::string const data{ packedIp };
                if (::inet_ntop(af, const_cast<char*>(data.data()), ip, sizeof(ip)) == nullptr)
                {
                    raise_os_error(errno);
                    throw raised{};
                }
                return S(ip);
            }));
            add("getaddrinfo", function("getaddrinfo", arguments::VarKeywords, [](std::vector<py> const& aArgs, object* aKwargs)
            {
                auto const a = clinic("getaddrinfo", aArgs, aKwargs, { "host", "port", "family", "type", "proto", "flags" }, 2u, 0u);
                int const family = a[2].empty() ? AF_UNSPEC : as_int(a[2]);
                int const socktype = a[3].empty() ? 0 : as_int(a[3]);
                int const protocol = a[4].empty() ? 0 : as_int(a[4]);
                int const flags = a[5].empty() ? 0 : as_int(a[5]);
                py const& hobj = a[0];
                py const& pobj = a[1];
                std::string host, port;
                bool hasHost = true, hasPort = true;
                if (hobj.is_none())
                    hasHost = false;
                else if (isinstance(hobj, builtin("str")))
                    host = idna_encode(hobj);
                else if (isinstance(hobj, builtin("bytes")))
                    host = std::string{ as_buffer(hobj) };
                else
                    py_raise("TypeError", "getaddrinfo() argument 1 must be string or None");
                if (type_of(pobj).is(builtin("int")))
                    port = pobj.str();
                else if (isinstance(pobj, builtin("str")))
                    port = pobj.str();
                else if (isinstance(pobj, builtin("bytes")))
                    port = std::string{ as_buffer(pobj) };
                else if (pobj.is_none())
                    hasPort = false;
                else
                    py_raise("OSError", "Int or String expected");
                py_checked(host_unrestricted("socket.getaddrinfo"));
                addrinfo hints{};
                hints.ai_family = family;
                hints.ai_socktype = socktype;
                hints.ai_protocol = protocol;
                hints.ai_flags = flags;
                addrinfo* res0 = nullptr;
                int const error = allow_threads([&]() { return ::getaddrinfo(hasHost ? host.c_str() : nullptr, hasPort ? port.c_str() : nullptr, &hints, &res0); });
                if (error != 0)
                    set_gaierror(error);
                py const all = py_list();
                try
                {
                    for (addrinfo* res = res0; res != nullptr; res = res->ai_next)
                    {
                        py const addr = makesockaddr(res->ai_addr, static_cast<std::size_t>(res->ai_addrlen));
                        all.method("append", { py_tuple({ py{ static_cast<long long>(res->ai_family) }, py{ static_cast<long long>(res->ai_socktype) },
                            py{ static_cast<long long>(res->ai_protocol) }, S(res->ai_canonname != nullptr ? res->ai_canonname : ""), addr }) });
                    }
                }
                catch (raised const&)
                {
                    ::freeaddrinfo(res0);
                    throw;
                }
                if (res0 != nullptr)
                    ::freeaddrinfo(res0);
                return all;
            }));
            add("getnameinfo", function("getnameinfo", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("getnameinfo", a.size(), 2u, 2u);
                py const& sa = a[0];
                int const flags = as_int(a[1]);
                if (!isinstance(sa, builtin("tuple")))
                    py_raise("TypeError", "getnameinfo() argument 1 must be a tuple");
                auto const items = sa.items();
                if (items.size() < 2u || items.size() > 4u)
                    py_raise("TypeError", "getnameinfo(): illegal sockaddr argument");
                std::string const hostp = as_str("getnameinfo", 1u, items[0]);
                int const port = as_int(items[1]);
                unsigned int const flowinfo = items.size() > 2u ? as_unsigned_int(items[2]) : 0u;
                unsigned int const scopeId = items.size() > 3u ? as_unsigned_int(items[3]) : 0u;
                if (flowinfo > 0xfffffu)
                    py_raise("OverflowError", "getnameinfo(): flowinfo must be 0-1048575.");
                py_checked(host_unrestricted("socket.getnameinfo"));
                std::string const pbufText = std::to_string(port);
                addrinfo hints{};
                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_DGRAM;     // (make numeric port happy)
                hints.ai_flags = AI_NUMERICHOST;    // (don't do any name resolution)
                addrinfo* res = nullptr;
                int error = allow_threads([&]() { return ::getaddrinfo(hostp.c_str(), pbufText.c_str(), &hints, &res); });
                if (error != 0)
                    set_gaierror(error);
                struct free_res { addrinfo* res; ~free_res() { if (res != nullptr) ::freeaddrinfo(res); } } const freeRes{ res };
                if (res->ai_next != nullptr)
                    py_raise("OSError", "sockaddr resolved to multiple addresses");
                switch (res->ai_family)
                {
                case AF_INET:
                    if (items.size() != 2u)
                        py_raise("OSError", "IPv4 sockaddr must be 2 tuple");
                    break;
                case AF_INET6:
                    {
                        auto* const sin6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
                        sin6->sin6_flowinfo = htonl(flowinfo);
                        sin6->sin6_scope_id = scopeId;
                    }
                    break;
                }
                char hbuf[NI_MAXHOST];
                char pbuf[NI_MAXSERV];
                error = allow_threads([&]() { return ::getnameinfo(res->ai_addr, static_cast<socklen_type>(res->ai_addrlen), hbuf, sizeof(hbuf), pbuf, sizeof(pbuf), flags); });
                if (error != 0)
                    set_gaierror(error);
                return py_tuple({ S(hbuf), S(pbuf) });
            }));
#if defined(NEOS_PY_SOCKET_CMSG)
            add("CMSG_LEN", function("CMSG_LEN", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("CMSG_LEN", a.size(), 1u, 1u);
                long long const length = as_ssize(a[0]);
                std::size_t result = 0u;
                if (length < 0 || !get_CMSG_LEN(static_cast<std::size_t>(length), result))
                    py_raise("OverflowError", "CMSG_LEN() argument out of range");
                return py{ static_cast<long long>(result) };
            }));
#if defined(CMSG_SPACE)
            add("CMSG_SPACE", function("CMSG_SPACE", arguments::Var, [](std::vector<py> const& a, object*)
            {
                check_count("CMSG_SPACE", a.size(), 1u, 1u);
                long long const length = as_ssize(a[0]);
                std::size_t result = 0u;
                if (length < 0 || !get_CMSG_SPACE(static_cast<std::size_t>(length), result))
                    py_raise("OverflowError", "CMSG_SPACE() argument out of range");
                return py{ static_cast<long long>(result) };
            }));
#endif
#endif
            add("getdefaulttimeout", function("getdefaulttimeout", arguments::None, [](std::vector<py> const&, object*)
            {
                auto const timeout = state().defaulttimeout;
                if (timeout < 0)
                    return none();
                return py{ pytime::as_seconds_double(timeout) };
            }));
            add("setdefaulttimeout", function("setdefaulttimeout", arguments::One, [](std::vector<py> const& a, object*)
            {
                state().defaulttimeout = socket_parse_timeout(a[0]);
                return none();
            }));
#if !defined(_WIN32)
            add("if_nameindex", function("if_nameindex", arguments::None, [](std::vector<py> const&, object*)
            {
                struct if_nameindex* const ni = ::if_nameindex();
                if (ni == nullptr)
                {
                    raise_os_error(errno);
                    throw raised{};
                }
                py const list = py_list();
                try
                {
                    for (int i = 0; ni[i].if_index != 0 && i < INT_MAX; ++i)
                        list.method("append", { py_tuple({ py{ static_cast<long long>(ni[i].if_index) },
                            fs_decode(B(ni[i].if_name, std::strlen(ni[i].if_name))) }) });
                }
                catch (raised const&)
                {
                    ::if_freenameindex(ni);
                    throw;
                }
                ::if_freenameindex(ni);
                return list;
            }));
            add("if_nametoindex", function("if_nametoindex", arguments::VarKeywords, [](std::vector<py> const& aArgs, object* aKwargs)
            {
                auto const a = clinic("if_nametoindex", aArgs, aKwargs, { "" }, 1u, 1u);
                py const oname = fs_encode(a[0]);
                std::string const name{ as_buffer(oname) };
                errno = ENODEV;  // (in case 'if_nametoindex' does not set errno)
                unsigned long const index = ::if_nametoindex(name.c_str());
                if (index == 0)
                {
                    raise_os_error(errno);
                    throw raised{};
                }
                return py{ static_cast<long long>(index) };
            }));
            add("if_indextoname", function("if_indextoname", arguments::One, [](std::vector<py> const& a, object*)
            {
                if (!is_int_like(a[0].get()))
                    py_raise("TypeError", "an integer is required");
                if (a[0] < py{ 0 })
                    py_raise("OverflowError", "can't convert negative int to unsigned");
                if (a[0] > py{ 0xFFFFFFFFLL })
                    py_raise("OverflowError", "index is too large");
                auto const index = static_cast<unsigned int>(a[0].as_index());
                errno = ENXIO;  // (in case 'if_indextoname' does not set errno)
                char name[IF_NAMESIZE + 1];
                if (::if_indextoname(index, name) == nullptr)
                {
                    raise_os_error(errno);
                    throw raised{};
                }
                return fs_decode(B(name, std::strlen(name)));
            }));
#endif
        }

        // --- the module

#if defined(_WIN32)
        // (Winsock: started once (os_init))
        void os_init()
        {
            static int const sResult = []()
            {
                WSADATA data{};
                int const ret = ::WSAStartup(0x0101, &data);
                if (ret == 0)
                    std::atexit([]() { ::WSACleanup(); });
                return ret;
            }();
            switch (sResult)
            {
            case 0:     // (no error)
                return;
            case WSASYSNOTREADY:
                py_raise("ImportError", "WSAStartup failed: network not ready");
            case WSAVERNOTSUPPORTED:
            case WSAEINVAL:
                py_raise("ImportError", "WSAStartup failed: requested version not supported");
            default:
                py_raise("ImportError", "WSAStartup failed: error code " + std::to_string(sResult));
            }
        }
#else
        void os_init() {}
#endif

        bool socket_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            ref const result{ py_guarded([&]() -> py
            {
                os_init();
                auto& s = state();
                if (!s.made)
                {
                    for (char const* name : { "socket.herror", "socket.gaierror" })
                        if (!is_exception_class(name))
                            user_exception_bases()[name] = { "OSError" };
                    s.herror = py{ exception_class("socket.herror") };
                    s.gaierror = py{ exception_class("socket.gaierror") };
                    make_socket_class();
                    s.made = true;
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S(socket_docs::doc("_socket")));
                add("herror", s.herror);
                add("gaierror", s.gaierror);
                add("error", builtin("OSError"));
                add("timeout", builtin("TimeoutError"));
                add("SocketType", s.sockClass);
                add("socket", s.sockClass);
                add("has_ipv6", py::boolean(true));
                add_functions(aAttributes);
                socket_constants::define(
                    [&](char const* aName, long aValue) { add(aName, py{ static_cast<long long>(aValue) }); },
                    [&](char const* aName, char const* aValue) { add(aName, S(aValue)); },
                    [&](char const* aName, unsigned long aValue) { add(aName, py{ static_cast<long long>(aValue) }); });
                return py::none();
            }) };
            return result.get() != nullptr;
        }

        inline bool const sSocket = register_module("_socket", socket_module);
    }
}
