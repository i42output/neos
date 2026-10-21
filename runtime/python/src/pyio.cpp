/*
  pyio.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/_pyio.py, and Modules/_io/iobase.c, Modules/_io/bufferedio.c,
  Modules/_io/textio.c, Modules/_io/_iomodule.c, Modules/_io/fileio.c and Python/fileutils.c (_Py_read, _Py_write and
  set_inheritable)), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF).
  Changes: translated from Python and C to C++ for the neos Python runtime plugin and adapted to its object model (see
  runtime/python/NOTICE).
*/

// The io module's classes (CPython 3.13's Lib/_pyio.py): the abstract base classes IOBase, RawIOBase, BufferedIOBase and TextIOBase,
// FileIO (Modules/_io/fileio.c: a raw stream of a file descriptor), and the buffered and text layers over a stream (BufferedReader, BufferedWriter, BufferedRWPair, BufferedRandom, TextIOWrapper and
// IncrementalNewlineDecoder). Their classes are made (by abc.ABCMeta) of closures, so a program's subclasses (e.g. socket.SocketIO, a
// RawIOBase) override their methods. The files that open() returns, and StringIO and BytesIO, are the runtime's own (io.cpp): they are
// registered as virtual subclasses of these classes (as CPython's io.py registers _io's classes with its ABCs). Where _pyio's behaviour differs
// from _io's (CPython's C implementation, which programs see), _io's is ported: its messages, BufferedWriter's buffering, IOBase's
// finalization, the reprs and the checks of the streams that buffered streams are made of; BufferedReader's reads of its raw stream are
// _pyio's (their sizes can differ from _io's).

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include "objects.hpp"
#include "values.hpp"
#include "pyio_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        constexpr long long DEFAULT_BUFFER_SIZE = 8 * 1024;    // (bytes)

        py S(char const* aText) { return py{ aText }; }
        py S(std::string const& aText) { return py{ aText }; }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py B(std::string_view aBytes) { return py{ make_bytes(std::string{ aBytes }) }; }

        // (the module's objects: made once)
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;
            py unsupportedOperation;
            py validSeekFlags;
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py const& cls(char const* aName) { return state().classes.at(aName); }

        // (the abstract base classes are io's; the other classes are _io's (as CPython's are))
        char const* module_of(std::string_view aClass)
        {
            return aClass == "IOBase" || aClass == "RawIOBase" || aClass == "BufferedIOBase" || aClass == "TextIOBase" ? "io" : "_io";
        }

        using body_type = std::function<py(std::vector<py> const&)>;

        // (a method (its parameters after self; aVarArgs: *args, appended) of a class: its docstring is _pyio's)
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false)
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            std::string const qualname = std::string{ aClass } + "." + aName;
            char const* const doc = pyio_docs::doc(qualname.c_str());
            return make_native_function(aName, qualname, doc != nullptr ? doc : "", module_of(aClass),
                [qualname, aParameters, aBody, aVarArgs](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    py rest;
                    auto a = py_bind(qualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, nullptr);
                    if (aVarArgs)
                        a.push_back(rest);
                    return aBody(a);
                });
        }

        // (a property (its getter's docstring is _pyio's))
        py property(char const* aClass, char const* aName, body_type aGetter)
        {
            py const getter = method(aClass, aName, {}, std::move(aGetter));
            return builtin("property").call({ getter, none(), none(), getter.attr("__doc__") });
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class: aMetaclass (or its bases' metaclass)(aName, aBases, namespace))
        py make_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, py aMetaclass = {})
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S(module_of(aName)));
            ns.set_item(S("__qualname__"), S(aName));
            char const* const doc = pyio_docs::doc(aName);
            ns.set_item(S("__doc__"), doc != nullptr ? S(doc) : none());
            for (auto const& [name, value] : aNamespace)
                ns.set_item(S(name), value);
            std::vector<py> const bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            py meta = aMetaclass.empty() ? builtin("type") : aMetaclass;
            for (auto const& b : bases)
            {
                py const t = type_of(b);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            py const result = meta(S(aName), py_tuple(bases), ns);
            state().classes[aName] = result;
            return result;
        }

        // --- Python's statements

        // (aException's context (as raise in an except clause sets it): aContext)
        [[noreturn]] void raise_in_except(py const& aException, py const& aContext)
        {
            aException.set_attr("__context__", aContext);
            py_raise(aException);
        }

        // (try: aBody() finally: aFinally())
        template <typename Body, typename Final>
        py try_finally(Body&& aBody, Final&& aFinally)
        {
            py result;
            try
            {
                result = aBody();
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                try
                {
                    aFinally();
                }
                catch (raised const&)
                {
                    py const other = py_take_exception();
                    if (other.attr("__context__").is_none() && !other.is(e))
                        other.set_attr("__context__", e);
                    py_reraise(other);
                }
                py_reraise(e);
            }
            aFinally();
            return result;
        }

        // (with aLock: aBody())
        template <typename Body>
        py with_lock(py const& aLock, Body&& aBody)
        {
            aLock.method("acquire");
            return try_finally(std::forward<Body>(aBody), [&]() { aLock.method("release"); });
        }

        // (for item in aIterable: aBody(item) (false: break))
        template <typename Body>
        void for_each(py const& aIterable, Body&& aBody)
        {
            py const it{ iter(aIterable.get()) };
            for (;;)
            {
                object* const item = next(it.get());
                if (item == nullptr)
                {
                    if (failed())
                        throw raised{};
                    return;
                }
                if (!aBody(py{ item }))
                    return;
            }
        }

        // --- Python's expressions

        bool hasattr(py const& aObject, char const* aName) { return builtin("hasattr")(aObject, S(aName)).truthy(); }
        py getattr_or_none(py const& aObject, char const* aName) { return builtin("getattr")(aObject, S(aName), none()); }
        py slice_of(py const& aObject, py const& aStart, py const& aStop) { return aObject[builtin("slice")(aStart, aStop)]; }
        py from(py const& aObject, py const& aStart) { return slice_of(aObject, aStart, none()); }   // (aObject[aStart:])
        py upto(py const& aObject, py const& aStop) { return slice_of(aObject, none(), aStop); }     // (aObject[:aStop])
        void delete_from(py const& aObject, py const& aStart, py const& aStop)                      // (del aObject[aStart:aStop])
        {
            py const key = builtin("slice")(aStart, aStop);
            py_checked(setitem(aObject.get(), key.get(), nullptr) == 0);
        }
        py iadd(py const& aLhs, py const& aRhs) { return py{ binary(NEOS_PY_ADD, aLhs.get(), aRhs.get(), true) }; }
        py len(py const& aObject) { return py{ aObject.size() }; }
        py class_name(py const& aSelf) { return aSelf.attr("__class__").attr("__name__"); }

        // (CPython's tp_name of an object's type: _io's classes' qualified names; a subclass's is its name)
        std::string type_name_of(py const& aObject)
        {
            py const type = type_of(aObject);
            std::string const name = type.attr("__name__").str();
            for (auto const& [className, c] : state().classes)
                if (c.is(type))
                    return std::string{ module_of(className) } + "." + name;
            return name;
        }

        // (an attribute that an object may not have (AttributeError), or that it cannot give (ValueError, e.g. a detached stream): empty if it
        // has none)
        py optional_attribute(py const& aObject, char const* aName)
        {
            try
            {
                return aObject.attr(aName);
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError", "ValueError" }))
                    throw;
                return {};
            }
        }

        // (the raw stream of a buffered stream and the buffer of a TextIOWrapper: _io's ValueError once it has been detached
        // (Modules/_io/bufferedio.c's CHECK_INITIALIZED and Modules/_io/textio.c's CHECK_ATTACHED))
        py attached_raw(py const& aSelf)
        {
            py raw = aSelf.attr("_raw");
            if (raw.is_none())
                py_raise("ValueError", "raw stream has been detached");
            return raw;
        }

        py attached_buffer(py const& aSelf)
        {
            py buffer = aSelf.attr("_buffer");
            if (buffer.is_none())
                py_raise("ValueError", "underlying buffer has been detached");
            return buffer;
        }

        [[noreturn]] void raise_unsupported(py const& aMessage) { py_raise(state().unsupportedOperation(aMessage)); }

        // (a size argument (an integer or None (-1)): _io's (Modules/_io/_iomodule.c's _Py_convert_optional_to_ssize_t))
        long long size_argument(py const& aSize)
        {
            if (aSize.is_none())
                return -1;
            if (!is_int_like(aSize.get()) && getattr_or_none(aSize, "__index__").is_none())
                py_raise("TypeError", "argument should be integer or None, not '" + class_name(aSize).str() + "'");
            return aSize.as_index();
        }

        // (a buffered stream's whence: _io's message (Modules/_io/bufferedio.c))
        void check_whence(py const& aWhence)
        {
            if (!state().validSeekFlags.contains(aWhence))
                py_raise("ValueError", "whence value " + aWhence.str() + " unsupported");
        }

        // (_io's checks of a stream that a buffered stream or BufferedRWPair is made of (Modules/_io/iobase.c's _PyIOBase_check_readable and
        // _PyIOBase_check_writable): UnsupportedOperation)
        void check_readable(py const& aRaw)
        {
            if (!aRaw.method("readable").truthy())
                raise_unsupported(S("File or stream is not readable."));
        }
        void check_writable(py const& aRaw)
        {
            if (!aRaw.method("writable").truthy())
                raise_unsupported(S("File or stream is not writable."));
        }
        void check_seekable(py const& aRaw)
        {
            if (!aRaw.method("seekable").truthy())
                raise_unsupported(S("File or stream is not seekable."));
        }
        void check_buffer_size(py const& aBufferSize)
        {
            if (aBufferSize <= py{ 0 })
                py_raise("ValueError", "buffer size must be strictly positive");
        }
        // (a closed buffered stream: ValueError (aMessage))
        void check_closed(py const& aSelf, char const* aMessage)
        {
            if (aSelf.attr("closed").truthy())
                py_raise("ValueError", aMessage);
        }
        // (a raw stream's position (Modules/_io/bufferedio.c's _buffered_raw_tell and _buffered_raw_seek): OSError if it is negative)
        py checked_position(py const& aPosition)
        {
            if (aPosition < py{ 0 })
                py_raise("OSError", "Raw stream returned invalid position " + aPosition.str());
            return aPosition;
        }

        // --- IOBase

        void make_iobase()
        {
            char const* const c = "IOBase";
            make_class(c, {}, {
                // ### Internal ###
                { "_unsupported", method(c, "_unsupported", { py_positional("name") }, [](std::vector<py> const& a) -> py
                    {
                        // (_io's message: the operation's name (Modules/_io/iobase.c); _pyio's is "<class>.<name>() not supported")
                        raise_unsupported(a[1]);
                    }) },
                // ### Positioning ###
                { "seek", method(c, "seek", { py_positional("pos"), py_positional("whence", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("seek") });
                        return none();
                    }) },
                { "tell", method(c, "tell", {}, [](std::vector<py> const& a) { return a[0].method("seek", { py{ 0 }, py{ 1 } }); }) },
                { "truncate", method(c, "truncate", { py_positional("pos", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("truncate") });
                        return none();
                    }) },
                // ### Flush and close ###
                { "flush", method(c, "flush", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_checkClosed");
                        return none();
                    }) },
                { "_IOBase__closed", boolean(false) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("_IOBase__closed").truthy())
                            try_finally([&]() { return a[0].method("flush"); }, [&]() { a[0].set_attr("_IOBase__closed", boolean(true)); });
                        return none();
                    }) },
                // (__del__ is _io's finalization (Modules/_io/iobase.c's iobase_finalize): if close() fails, the exception is reported as
                // unraisable in the object)
                { "__del__", method(c, "__del__", {}, [](std::vector<py> const& a)
                    {
                        bool closed = true;
                        try
                        {
                            closed = a[0].attr("closed").truthy();
                        }
                        catch (raised const&)
                        {
                            // (if `closed` doesn't exist or can't be evaluated as bool, then the object is probably in an unusable state, so ignore)
                            py_take_exception();
                            return none();
                        }
                        if (closed)
                            return none();
                        try
                        {
                            py const deallocWarn = getattr_or_none(a[0], "_dealloc_warn");
                            if (deallocWarn.truthy())
                                deallocWarn(a[0]);
                            a[0].method("close");
                        }
                        catch (raised const&)
                        {
                            write_unraisable(a[0].get());
                        }
                        return none();
                    }) },
                // ### Inquiries ###
                { "seekable", method(c, "seekable", {}, [](std::vector<py> const&) { return boolean(false); }) },
                { "_checkSeekable", method(c, "_checkSeekable", { py_positional("msg", none()) }, [](std::vector<py> const& a)
                    {
                        if (!a[0].method("seekable").truthy())
                            raise_unsupported(a[1].is_none() ? S("File or stream is not seekable.") : a[1]);
                        return none();
                    }) },
                { "readable", method(c, "readable", {}, [](std::vector<py> const&) { return boolean(false); }) },
                { "_checkReadable", method(c, "_checkReadable", { py_positional("msg", none()) }, [](std::vector<py> const& a)
                    {
                        if (!a[0].method("readable").truthy())
                            raise_unsupported(a[1].is_none() ? S("File or stream is not readable.") : a[1]);
                        return none();
                    }) },
                { "writable", method(c, "writable", {}, [](std::vector<py> const&) { return boolean(false); }) },
                { "_checkWritable", method(c, "_checkWritable", { py_positional("msg", none()) }, [](std::vector<py> const& a)
                    {
                        if (!a[0].method("writable").truthy())
                            raise_unsupported(a[1].is_none() ? S("File or stream is not writable.") : a[1]);
                        return none();
                    }) },
                { "closed", property(c, "closed", [](std::vector<py> const& a) { return a[0].attr("_IOBase__closed"); }) },
                { "_checkClosed", method(c, "_checkClosed", { py_positional("msg", none()) }, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("closed").truthy())
                            py_raise(builtin("ValueError")(a[1].is_none() ? S("I/O operation on closed file.") : a[1]));
                        return none();
                    }) },
                // ### Context manager ###
                { "__enter__", method(c, "__enter__", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_checkClosed");
                        return a[0];
                    }) },
                { "__exit__", method(c, "__exit__", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("close");
                        return none();
                    }, true) },
                // ### Lower-level APIs ###
                { "fileno", method(c, "fileno", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("fileno") });
                        return none();
                    }) },
                { "isatty", method(c, "isatty", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_checkClosed");
                        return boolean(false);
                    }) },
                // ### Readline[s] and writelines ###
                { "readline", method(c, "readline", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        // (for backwards compatibility, a (slowish) readline())
                        py const& self = a[0];
                        bool const peek = hasattr(self, "peek");
                        long long const size = size_argument(a[1]);
                        auto const nreadahead = [&]() -> long long
                        {
                            if (!peek)
                                return 1;
                            py const readahead = self.method("peek", { py{ 1 } });
                            if (!readahead.truthy())
                                return 1;
                            long long n = readahead.method("find", { B("\n") }).as_index() + 1;
                            if (n == 0)
                                n = readahead.size();
                            if (size >= 0)
                                n = std::min(n, size);
                            return n;
                        };
                        py res = builtin("bytearray")();
                        while (size < 0 || res.size() < size)
                        {
                            py const b = self.method("read", { py{ nreadahead() } });
                            if (!b.truthy())
                                break;
                            res = iadd(res, b);
                            if (res.method("endswith", { B("\n") }).truthy())
                                break;
                        }
                        return builtin("bytes")(res);
                    }) },
                { "__iter__", method(c, "__iter__", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_checkClosed");
                        return a[0];
                    }) },
                { "__next__", method(c, "__next__", {}, [](std::vector<py> const& a)
                    {
                        py const line = a[0].method("readline");
                        if (!line.truthy())
                            py_raise(builtin("StopIteration"));
                        return line;
                    }) },
                { "readlines", method(c, "readlines", { py_positional("hint", none()) }, [](std::vector<py> const& a)
                    {
                        if (a[1].is_none() || a[1] <= py{ 0 })
                            return builtin("list")(a[0]);
                        py n{ 0 };
                        py const lines = py_list();
                        for_each(a[0], [&](py const& aLine)
                        {
                            lines.method("append", { aLine });
                            n = n + len(aLine);
                            return !(n >= a[1]);
                        });
                        return lines;
                    }) },
                { "writelines", method(c, "writelines", { py_positional("lines") }, [](std::vector<py> const& a)
                    {
                        a[0].method("_checkClosed");
                        for_each(a[1], [&](py const& aLine)
                        {
                            a[0].method("write", { aLine });
                            return true;
                        });
                        return none();
                    }) }
            }, import_attribute("abc", "ABCMeta"));
        }

        // --- RawIOBase

        void make_raw_iobase()
        {
            char const* const c = "RawIOBase";
            // (the read() method is implemented by calling readinto(); derived classes that want to support read() only need to implement
            // readinto() as a primitive operation)
            make_class(c, { cls("IOBase") }, {
                { "read", method(c, "read", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        py size = a[1].is_none() ? py{ -1 } : a[1];
                        if (size < py{ 0 })
                            return a[0].method("readall");
                        py const b = builtin("bytearray")(size.method("__index__"));
                        py const n = a[0].method("readinto", { b });
                        if (n.is_none())
                            return none();
                        if (n < py{ 0 } || n > len(b))
                            py_raise("ValueError", "readinto returned " + n.str() + " outside buffer size " + std::to_string(b.size()));
                        delete_from(b, n, none());
                        return builtin("bytes")(b);
                    }) },
                { "readall", method(c, "readall", {}, [](std::vector<py> const& a)
                    {
                        // (_io's (Modules/_io/iobase.c): None before any data is None; read() must return bytes)
                        py const chunks = py_list();
                        for (;;)
                        {
                            py const data = a[0].method("read", { py{ DEFAULT_BUFFER_SIZE } });
                            if (data.is_none())
                            {
                                if (chunks.size() == 0)
                                    return data;
                                break;
                            }
                            if (!isinstance(data, builtin("bytes")))
                                py_raise("TypeError", "read() should return bytes");
                            if (data.size() == 0)
                                break;  // (EOF)
                            chunks.method("append", { data });
                        }
                        return B("").method("join", { chunks });
                    }) },
                // (_io's readinto() and write() raise NotImplementedError (Modules/_io/iobase.c); _pyio's raise UnsupportedOperation)
                { "readinto", method(c, "readinto", { py_positional("b") }, [](std::vector<py> const&) -> py
                    {
                        py_raise(builtin("NotImplementedError"));
                    }) },
                { "write", method(c, "write", { py_positional("b") }, [](std::vector<py> const&) -> py
                    {
                        py_raise(builtin("NotImplementedError"));
                    }) }
            });
        }

        // --- FileIO (Modules/_io/fileio.c: a raw stream of a file descriptor; its state is native (fileio_new's), its file's reads and
        // writes are Python/fileutils.c's _Py_read and _Py_write)

        struct fileio_state
        {
            int fd = -1;
            bool created = false;
            bool readable = false;
            bool writable = false;
            bool appending = false;
            int seekable = -1;      ///< (-1: not known)
            bool closefd = true;
        };

        std::map<object*, fileio_state>& fileio_states()
        {
            static auto* sStates = new std::map<object*, fileio_state>{};
            return *sStates;
        }

        fileio_state& fileio_of(py const& aSelf) { return fileio_states()[aSelf.get()]; }

        [[noreturn]] void err_closed() { py_raise("ValueError", "I/O operation on closed file"); }
        [[noreturn]] void err_mode(char const* aAction) { raise_unsupported(S(std::string{ "File not open for " } + aAction)); }
        [[noreturn]] void raise_errno(int aErrno, py const& aFilename = {})
        {
            raise_os_error(aErrno, aFilename.empty() ? nullptr : aFilename.get());
            throw raised{};
        }

#if defined(_WIN32)
        constexpr std::size_t kReadMax = INT_MAX;
        constexpr std::size_t kWriteMax = INT_MAX;
        constexpr int kErrorNoData = 232;   // (ERROR_NO_DATA)
#else
        constexpr std::size_t kReadMax = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
        constexpr std::size_t kWriteMax = kReadMax;
#endif

        // (_Py_read: at most aCount bytes; interrupted by a signal: retried, unless its handler raises; -1: an exception was raised (errno:
        // its error))
        long long fd_read(int aFd, void* aBuffer, std::size_t aCount)
        {
            aCount = std::min(aCount, kReadMax);
            // (the standard input: the running program's (see read_standard_some))
            if (aFd == 0)
            {
                gil_release();
                auto const n = read_standard_some(static_cast<char*>(aBuffer), aCount);
                gil_acquire();
                return static_cast<long long>(n);
            }
            long long n = 0;
            int error = 0;
            for (;;)
            {
                gil_release();
                errno = 0;
#if defined(_WIN32)
                _doserrno = 0;
                n = ::_read(aFd, aBuffer, static_cast<unsigned>(aCount));
                // (read() on a non-blocking empty pipe fails with EINVAL, which is mapped from the Windows error code ERROR_NO_DATA)
                if (n < 0 && errno == EINVAL && _doserrno == kErrorNoData)
                    errno = EAGAIN;
#else
                n = ::read(aFd, aBuffer, aCount);
#endif
                error = errno;
                gil_acquire();
                if (n < 0 && error == EINTR)
                {
                    if (!signals_check())
                    {
                        errno = error;
                        return -1;
                    }
                    continue;
                }
                break;
            }
            if (n < 0)
            {
                raise_os_error(error);
                errno = error;
                return -1;
            }
            return n;
        }

        // (_Py_write: as fd_read)
        long long fd_write(int aFd, void const* aBuffer, std::size_t aCount)
        {
            // (the standard output and error: the running program's (see write_standard))
            if (aFd == 1 || aFd == 2)
            {
                gil_release();
                bool const written = write_standard(aFd, std::string_view{ static_cast<char const*>(aBuffer), aCount });
                gil_acquire();
                if (!written)
                {
                    raise_os_error(EIO);
                    errno = EIO;
                    return -1;
                }
                return static_cast<long long>(aCount);
            }
#if defined(_WIN32)
            // (the Windows console returns an error (12: not enough space) on writing into stdout if stdout mode is binary and the length is
            // greater than 66,000 bytes (or less, depending on heap usage))
            if (aCount > 32767u)
            {
                gil_release();
                bool const tty = ::_isatty(aFd) != 0;
                gil_acquire();
                if (tty)
                    aCount = 32767u;
            }
#endif
            aCount = std::min(aCount, kWriteMax);
            long long n = 0;
            int error = 0;
            for (;;)
            {
                gil_release();
                errno = 0;
#if defined(_WIN32)
                // (write() on a non-blocking pipe fails with ENOSPC on Windows if the pipe lacks available space for the entire buffer)
                int c = static_cast<int>(aCount);
                do
                {
                    _doserrno = 0;
                    n = ::_write(aFd, aBuffer, static_cast<unsigned>(c));
                    if (n >= 0 || errno != ENOSPC || _doserrno != 0)
                        break;
                    errno = EAGAIN;
                    c /= 2;
                } while (c > 0);
#else
                n = ::write(aFd, aBuffer, aCount);
#endif
                error = errno;
                gil_acquire();
                if (n < 0 && error == EINTR)
                {
                    if (!signals_check())
                    {
                        errno = error;
                        return -1;
                    }
                    continue;
                }
                break;
            }
            if (n < 0)
            {
                raise_os_error(error);
                errno = error;
                return -1;
            }
            return n;
        }

        // (internal_close: the descriptor is closed (OSError if that fails))
        void internal_close(fileio_state& f)
        {
            if (f.fd < 0)
                return;
            int const fd = f.fd;
            f.fd = -1;
            gil_release();
#if defined(_WIN32)
            int const result = ::_close(fd);
#else
            int const result = ::close(fd);
#endif
            int const error = errno;
            gil_acquire();
            if (result < 0)
                raise_errno(error);
        }

        // (portable_lseek: the position (whence 0, 1 or 2); the stream's seekable is known once it has been tried)
        py portable_lseek(fileio_state& f, py const& aPosition, int aWhence, bool aSuppressPipeError)
        {
            long long const position = aPosition.empty() ? 0 : aPosition.as_index();
            int const whence = aWhence == 0 ? SEEK_SET : aWhence == 1 ? SEEK_CUR : aWhence == 2 ? SEEK_END : aWhence;
            gil_release();
#if defined(_WIN32)
            long long result = ::_lseeki64(f.fd, position, whence);
#else
            long long result = static_cast<long long>(::lseek(f.fd, static_cast<off_t>(position), whence));
#endif
            int const error = errno;
            gil_acquire();
            if (f.seekable < 0)
                f.seekable = result >= 0 ? 1 : 0;
            if (result < 0)
            {
                if (aSuppressPipeError && error == ESPIPE)
                    result = 0;
                else
                    raise_errno(error);
            }
            return py{ result };
        }

        // (mode_string)
        char const* mode_string(fileio_state const& f)
        {
            if (f.created)
                return f.readable ? "xb+" : "xb";
            if (f.appending)
                return f.readable ? "ab+" : "ab";
            if (f.readable)
                return f.writable ? "rb+" : "rb";
            return "wb";
        }

        // (Python/fileutils.c's set_inheritable (not inheritable): aAtomicFlagWorks: whether O_CLOEXEC is known to work (-1: not known))
#if !defined(_WIN32)
        void set_non_inheritable(int aFd, int* aAtomicFlagWorks)
        {
            if (aAtomicFlagWorks != nullptr && *aAtomicFlagWorks != 0)
            {
                if (*aAtomicFlagWorks == -1)
                {
                    int const flags = ::fcntl(aFd, F_GETFD);
                    if (flags == -1)
                        raise_errno(errno);
                    *aAtomicFlagWorks = (flags & FD_CLOEXEC) != 0 ? 1 : 0;
                }
                if (*aAtomicFlagWorks == 1)
                    return;
            }
            int const flags = ::fcntl(aFd, F_GETFD);
            if (flags < 0)
                raise_errno(errno);
            if ((flags & FD_CLOEXEC) != 0)
                return;
            if (::fcntl(aFd, F_SETFD, flags | FD_CLOEXEC) < 0)
                raise_errno(errno);
        }
#endif

        // (PyUnicode_FSConverter: a path's bytes (str: UTF-8 (the file system's encoding)); a null byte: ValueError)
        std::string fs_path(py const& aName)
        {
            py const path = import_attribute("os", "fspath")(aName);
            std::string result;
            if (isinstance(path, builtin("str")))
                result = path.str();
            else
            {
                std::string_view bytes;
                if (!bytes_like(path.get(), bytes))
                    py_raise("TypeError", "expected str, bytes or os.PathLike object, not " + class_name(path).str());
                result = std::string{ bytes };
            }
            if (result.find('\0') != std::string::npos)
                py_raise("ValueError", "embedded null byte");
            return result;
        }

        void make_fileio()
        {
            char const* const c = "FileIO";
            // (__init__(file, mode='r', closefd=True, opener=None): its arguments as the argument clinic checks them)
            py const init = make_native_function("__init__", "FileIO.__init__", "Initialize self.  See help(type(self)) for accurate signature.", "_io",
                [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    std::vector<object*> rest;
                    for (std::size_t i = 1u; i < aArgs.size(); ++i)
                        rest.push_back(aArgs[i].get());
                    std::vector<object*> a;
                    py_checked(parse_arguments("FileIO", rest, aKwargs, { "file", "mode", "closefd", "opener" }, 1u, 0u, a));
                    py const& self = aArgs.at(0);
                    py const nameobj = py::borrowed(a[0]);
                    std::string mode = "r";
                    if (a[1] != nullptr)
                    {
                        py const m = py::borrowed(a[1]);
                        if (!isinstance(m, builtin("str")))
                            py_raise("TypeError", "FileIO() argument 'mode' must be str, not " + class_name(m).str());
                        mode = m.str();
                        if (mode.find('\0') != std::string::npos)
                            py_raise("ValueError", "embedded null character");
                    }
                    bool const closefd = a[2] == nullptr || py::borrowed(a[2]).truthy();
                    py const opener = a[3] != nullptr ? py::borrowed(a[3]) : none();
                    auto& f = fileio_of(self);
                    if (f.fd >= 0)
                    {
                        // (have to close the existing file first)
                        if (f.closefd)
                            internal_close(f);
                        else
                            f.fd = -1;
                    }
                    if (isinstance(nameobj, builtin("bool")))
                        import_attribute("warnings", "warn")(S("bool is used as a file descriptor"), builtin("RuntimeWarning"));
                    // (a file descriptor (an int that is one), else a path)
                    long long fd = -1;
                    bool isDescriptor = false;
                    if (is_int_like(nameobj.get()) || hasattr(nameobj, "__index__"))
                    {
                        long long n = 0;
                        if (to_index(nameobj.get(), n) && n >= INT_MIN && n <= INT_MAX)
                        {
                            if (n < 0)
                                py_raise("ValueError", "negative file descriptor");
                            fd = n;
                            isDescriptor = true;
                        }
                        else
                            clear_error();
                    }
                    std::string name;
                    if (!isDescriptor)
                        name = fs_path(nameobj);
                    int flags = 0;
                    bool rwa = false;
                    bool plus = false;
                    fileio_state opened;
                    auto const badMode = []()
                    {
                        py_raise("ValueError", "Must have exactly one of create/read/write/append mode and at most one plus");
                    };
                    for (char const ch : mode)
                        switch (ch)
                        {
                        case 'x':
                            if (rwa)
                                badMode();
                            rwa = true;
                            opened.created = opened.writable = true;
                            flags |= O_EXCL | O_CREAT;
                            break;
                        case 'r':
                            if (rwa)
                                badMode();
                            rwa = true;
                            opened.readable = true;
                            break;
                        case 'w':
                            if (rwa)
                                badMode();
                            rwa = true;
                            opened.writable = true;
                            flags |= O_CREAT | O_TRUNC;
                            break;
                        case 'a':
                            if (rwa)
                                badMode();
                            rwa = true;
                            opened.writable = opened.appending = true;
                            flags |= O_APPEND | O_CREAT;
                            break;
                        case 'b':
                            break;
                        case '+':
                            if (plus)
                                badMode();
                            opened.readable = opened.writable = true;
                            plus = true;
                            break;
                        default:
                            py_raise("ValueError", "invalid mode: " + mode.substr(0u, 200u));
                        }
                    if (!rwa)
                        badMode();
                    flags |= opened.readable && opened.writable ? O_RDWR : opened.readable ? O_RDONLY : O_WRONLY;
#if defined(O_BINARY)
                    flags |= O_BINARY;
#endif
#if defined(_WIN32)
                    flags |= O_NOINHERIT;
#elif defined(O_CLOEXEC)
                    flags |= O_CLOEXEC;
#endif
                    // (a file opened to write (or created): restricted (a descriptor's is its opener's), as open())
                    if (!isDescriptor && opened.writable)
                        py_checked(host_unrestricted("open"));
                    bool fdIsOwn = false;
                    auto const fail = [&]()
                    {
                        // (the error: the descriptor that it opened is closed)
                        if (!fdIsOwn)
                            f.fd = -1;
                        if (f.fd >= 0)
                        {
                            py const e = py_take_exception();
                            try
                            {
                                internal_close(f);
                            }
                            catch (raised const&)
                            {
                                py const other = py_take_exception();
                                other.set_attr("__context__", e);
                                py_reraise(other);
                            }
                            py_reraise(e);
                        }
                        throw raised{};
                    };
                    try
                    {
                        if (isDescriptor)
                        {
                            opened.fd = static_cast<int>(fd);
                            opened.closefd = closefd;
                            f = opened;
                        }
                        else
                        {
                            opened.closefd = true;
                            if (!closefd)
                                py_raise("ValueError", "Cannot use closefd=False with file name");
#if !defined(_WIN32) && defined(O_CLOEXEC)
                            static int sOpenCloexecWorks = -1;
                            int* atomicFlagWorks = &sOpenCloexecWorks;
#elif !defined(_WIN32)
                            int* atomicFlagWorks = nullptr;
#endif
                            if (opener.is_none())
                            {
#if defined(_WIN32)
                                std::wstring wide;
                                if (!name.empty())
                                {
                                    int const size = ::MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
                                    wide.resize(static_cast<std::size_t>(size));
                                    ::MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), size);
                                }
#endif
                                int result = -1;
                                int error = 0;
                                for (;;)
                                {
                                    gil_release();
                                    errno = 0;
#if defined(_WIN32)
                                    result = ::_wopen(wide.c_str(), flags, 0666);
#else
                                    result = ::open(name.c_str(), flags, 0666);
#endif
                                    error = errno;
                                    gil_acquire();
                                    if (result < 0 && error == EINTR)
                                    {
                                        py_checked(signals_check());
                                        continue;
                                    }
                                    break;
                                }
                                if (result < 0)
                                    raise_errno(error, nameobj);
                                opened.fd = result;
                            }
                            else
                            {
#if !defined(_WIN32)
                                // (the opener may clear the atomic flag)
                                atomicFlagWorks = nullptr;
#endif
                                py const fdobj = opener(nameobj, py{ flags });
                                if (!isinstance(fdobj, builtin("int")))
                                    py_raise("TypeError", "expected integer from opener");
                                long long const n = fdobj.as_index();
                                if (n < 0)
                                    py_raise("ValueError", "opener returned " + std::to_string(n));
                                opened.fd = static_cast<int>(n);
                            }
                            f = opened;
                            fdIsOwn = true;
#if !defined(_WIN32)
                            set_non_inheritable(f.fd, atomicFlagWorks);
#endif
                        }
                        long long blksize = DEFAULT_BUFFER_SIZE;
                        gil_release();
#if defined(_WIN32)
                        struct _stat64 status{};
                        int const statResult = ::_fstat64(f.fd, &status);
                        int const statError = errno;
                        bool const invalidHandle = statResult < 0 && ::GetLastError() == ERROR_INVALID_HANDLE;
#else
                        struct stat status{};
                        int const statResult = ::fstat(f.fd, &status);
                        int const statError = errno;
#endif
                        gil_acquire();
                        if (statResult < 0)
                        {
                            // (fstat() errors other than EBADF are tolerated (e.g. an anonymous file on a Virtual Box shared folder
                            // filesystem raises ENOENT))
#if defined(_WIN32)
                            if (invalidHandle)
                                raise_errno(EBADF);
#else
                            if (statError == EBADF)
                                raise_errno(statError);
#endif
                        }
                        else
                        {
#if !defined(_WIN32)
                            // (on Unix, open succeeds for directories: there should be no file objects referring to directories)
                            if (S_ISDIR(status.st_mode))
                                raise_errno(EISDIR, nameobj);
                            if (status.st_blksize > 1)
                                blksize = static_cast<long long>(status.st_blksize);
#endif
                        }
#if defined(_WIN32)
                        // (newlines are not translated (\r\n <=> \n))
                        ::_setmode(f.fd, O_BINARY);
#endif
                        self.set_attr("_blksize", py{ blksize });
                        self.set_attr("name", nameobj);
                        // (for consistent behaviour, explicitly seek to the end of the file (otherwise, it might be done only on the first write()))
                        if (f.appending)
                            portable_lseek(f, {}, 2, true);
                    }
                    catch (raised const&)
                    {
                        fail();
                    }
                    return none();
                });
            auto const opened = [](py const& aSelf) -> fileio_state&
            {
                auto& f = fileio_of(aSelf);
                if (f.fd < 0)
                    err_closed();
                return f;
            };
            make_class(c, { cls("RawIOBase") }, {
                { "__init__", init },
                { "_blksize", py{ 0 } },
                { "_finalizing", boolean(false) },
                { "read", method(c, "read", { py_positional("size", py{ -1 }) }, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        if (!f.readable)
                            err_mode("reading");
                        long long size = size_argument(a[1]);
                        if (size < 0)
                            return a[0].method("readall");
                        std::string data(static_cast<std::size_t>(std::min<unsigned long long>(static_cast<unsigned long long>(size), kReadMax)), '\0');
                        auto const n = fd_read(f.fd, data.data(), data.size());
                        if (n == -1)
                        {
                            if (errno == EAGAIN)
                            {
                                py_take_exception();
                                return none();
                            }
                            throw raised{};
                        }
                        data.resize(static_cast<std::size_t>(n));
                        return B(data);
                    }) },
                { "readall", method(c, "readall", {}, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        // (a real file: a buffer one byte larger than the rest of the file (then EOF without enlarging it))
                        gil_release();
#if defined(_WIN32)
                        long long pos = ::_lseeki64(f.fd, 0, SEEK_CUR);
                        struct _stat64 status{};
                        bool const stat = ::_fstat64(f.fd, &status) == 0;
#else
                        long long pos = static_cast<long long>(::lseek(f.fd, 0, SEEK_CUR));
                        struct stat status{};
                        bool const stat = ::fstat(f.fd, &status) == 0;
#endif
                        gil_acquire();
                        long long const end = stat ? static_cast<long long>(status.st_size) : -1;
                        constexpr std::size_t kSmallChunk = 8u * 1024u;
                        std::size_t bufsize = end > 0 && end >= pos && pos >= 0 ? static_cast<std::size_t>(end - pos + 1) : kSmallChunk;
                        std::string result(bufsize, '\0');
                        std::size_t bytesRead = 0u;
                        for (;;)
                        {
                            if (bytesRead >= bufsize)
                            {
                                // (new_buffersize: expanded in proportion to its size (amortized linear time); for bigger sizes, by less than double)
                                std::size_t addend = bytesRead > 65536u ? bytesRead >> 3 : 256u + bytesRead;
                                if (addend < kSmallChunk)
                                    addend = kSmallChunk;   // (avoid tiny read() calls)
                                bufsize = addend + bytesRead;
                                if (result.size() < bufsize)
                                    result.resize(bufsize);
                            }
                            auto const n = fd_read(f.fd, result.data() + bytesRead, bufsize - bytesRead);
                            if (n == 0)
                                break;
                            if (n == -1)
                            {
                                if (errno == EAGAIN)
                                {
                                    py_take_exception();
                                    if (bytesRead > 0u)
                                        break;
                                    return none();
                                }
                                throw raised{};
                            }
                            bytesRead += static_cast<std::size_t>(n);
                            pos += n;
                        }
                        result.resize(bytesRead);
                        return B(result);
                    }) },
                { "readinto", method(c, "readinto", { py_positional_only("buffer") }, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        if (!f.readable)
                            err_mode("reading");
                        buffer_info buffer;
                        if (!buffer_of(a[1].get(), buffer) || buffer.readonly)
                            py_raise("TypeError", "readinto() argument 1 must be read-write bytes-like object, not " + class_name(a[1]).str());
                        auto const n = fd_read(f.fd, buffer.data, buffer.size);
                        if (n == -1)
                        {
                            if (errno == EAGAIN)
                            {
                                py_take_exception();
                                return none();
                            }
                            throw raised{};
                        }
                        return py{ n };
                    }) },
                { "write", method(c, "write", { py_positional_only("b") }, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        if (!f.writable)
                            err_mode("writing");
                        std::string_view data;
                        if (!bytes_like(a[1].get(), data))
                            py_raise("TypeError", "a bytes-like object is required, not '" + class_name(a[1]).str() + "'");
                        auto const n = fd_write(f.fd, data.data(), data.size());
                        if (n < 0)
                        {
                            if (errno == EAGAIN)
                            {
                                py_take_exception();
                                return none();
                            }
                            throw raised{};
                        }
                        return py{ n };
                    }) },
                { "seek", method(c, "seek", { py_positional_only("pos"), py_positional_only("whence", py{ 0 }) }, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        return portable_lseek(f, a[1], static_cast<int>(a[2].as_index()), false);
                    }) },
                { "tell", method(c, "tell", {}, [opened](std::vector<py> const& a) { return portable_lseek(opened(a[0]), {}, 1, false); }) },
                { "truncate", method(c, "truncate", { py_positional_only("size", none()) }, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        if (!f.writable)
                            err_mode("writing");
                        // (the current position, by default)
                        py const posobj = a[1].is_none() ? portable_lseek(f, {}, 1, false) : a[1];
                        long long const pos = posobj.as_index();
                        gil_release();
                        errno = 0;
#if defined(_WIN32)
                        int const result = ::_chsize_s(f.fd, pos) == 0 ? 0 : -1;
#else
                        int const result = ::ftruncate(f.fd, static_cast<off_t>(pos));
#endif
                        int const error = errno;
                        gil_acquire();
                        if (result != 0)
                            raise_errno(error);
                        return posobj;
                    }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        auto& f = fileio_of(a[0]);
                        py result;
                        py e;
                        try
                        {
                            result = cls("RawIOBase").attr("close")(a[0]);
                        }
                        catch (raised const&)
                        {
                            e = py_take_exception();
                        }
                        if (!f.closefd)
                        {
                            f.fd = -1;
                            if (!e.empty())
                                py_reraise(e);
                            return result;
                        }
                        try
                        {
                            internal_close(f);
                        }
                        catch (raised const&)
                        {
                            py const other = py_take_exception();
                            if (!e.empty())
                                other.set_attr("__context__", e);
                            py_reraise(other);
                        }
                        if (!e.empty())
                            py_reraise(e);
                        return result;
                    }) },
                { "seekable", method(c, "seekable", {}, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        if (f.seekable < 0)
                        {
                            // (portable_lseek() sets seekable)
                            try
                            {
                                portable_lseek(f, {}, 1, false);
                            }
                            catch (raised const&)
                            {
                                py_take_exception();
                            }
                        }
                        return boolean(f.seekable == 1);
                    }) },
                { "readable", method(c, "readable", {}, [opened](std::vector<py> const& a) { return boolean(opened(a[0]).readable); }) },
                { "writable", method(c, "writable", {}, [opened](std::vector<py> const& a) { return boolean(opened(a[0]).writable); }) },
                { "fileno", method(c, "fileno", {}, [opened](std::vector<py> const& a) { return py{ opened(a[0]).fd }; }) },
                { "isatty", method(c, "isatty", {}, [opened](std::vector<py> const& a)
                    {
                        auto& f = opened(a[0]);
                        gil_release();
#if defined(_WIN32)
                        bool const result = ::_isatty(f.fd) != 0;
#else
                        bool const result = ::isatty(f.fd) != 0;
#endif
                        gil_acquire();
                        return boolean(result);
                    }) },
                // (fileio_dealloc_warn: an unclosed file (that it would close) is a ResourceWarning)
                { "_dealloc_warn", method(c, "_dealloc_warn", { py_positional_only("source") }, [](std::vector<py> const& a)
                    {
                        auto const& f = fileio_of(a[0]);
                        if (f.fd >= 0 && f.closefd)
                        {
                            try
                            {
                                import_attribute("warnings", "warn").call({ S("unclosed file " + a[1].repr()), builtin("ResourceWarning") },
                                    { { "stacklevel", py{ 1 } }, { "source", a[1] } });
                            }
                            catch (raised const&)
                            {
                                // (spurious errors can appear at shutdown: a warning that is an error is reported, others are dropped)
                                py const e = py_take_exception();
                                if (exception_is(e.get(), "Warning"))
                                {
                                    reraise_exception(e.get());
                                    write_unraisable(a[0].get());
                                }
                            }
                        }
                        return none();
                    }) },
                { "__getstate__", method(c, "__getstate__", {}, [](std::vector<py> const& a) -> py
                    {
                        py_raise("TypeError", "cannot pickle " + class_name(a[0]).repr() + " instances");
                    }) },
                // ('closed' and 'mode' are attributes for backwards compatibility reasons)
                { "closed", property(c, "closed", [](std::vector<py> const& a) { return boolean(fileio_of(a[0]).fd < 0); }) },
                { "closefd", property(c, "closefd", [](std::vector<py> const& a) { return boolean(fileio_of(a[0]).closefd); }) },
                { "mode", property(c, "mode", [](std::vector<py> const& a) { return S(mode_string(fileio_of(a[0]))); }) },
                // (fileio_repr)
                { "__repr__", method(c, "__repr__", {}, [](std::vector<py> const& a)
                    {
                        auto const& f = fileio_of(a[0]);
                        std::string const typeName = type_name_of(a[0]);
                        if (f.fd < 0)
                            return S("<" + typeName + " [closed]>");
                        std::string const closefd = f.closefd ? "True" : "False";
                        py const nameobj = getattr_or_none(a[0], "name");
                        if (!hasattr(a[0], "name"))
                            return S("<" + typeName + " fd=" + std::to_string(f.fd) + " mode='" + mode_string(f) + "' closefd=" + closefd + ">");
                        static std::set<object*> sReprs;
                        if (sReprs.contains(a[0].get()))
                            py_raise("RuntimeError", "reentrant call inside " + typeName + ".__repr__");
                        sReprs.insert(a[0].get());
                        py result;
                        try
                        {
                            result = S("<" + typeName + " name=" + nameobj.repr() + " mode='" + mode_string(f) + "' closefd=" + closefd + ">");
                        }
                        catch (raised const&)
                        {
                            sReprs.erase(a[0].get());
                            throw;
                        }
                        sReprs.erase(a[0].get());
                        return result;
                    }) },
                // (fileio_dealloc: finalized as IOBase is (_PyIOBase_finalize), then its state is freed)
                { "__del__", method(c, "__del__", {}, [](std::vector<py> const& a)
                    {
                        try_finally([&]() { return cls("IOBase").attr("__del__")(a[0]); }, [&]() { fileio_states().erase(a[0].get()); });
                        return none();
                    }) }
            });
        }

        // --- BufferedIOBase

        void make_buffered_iobase()
        {
            char const* const c = "BufferedIOBase";
            make_class(c, { cls("IOBase") }, {
                { "read", method(c, "read", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("read") });
                        return none();
                    }) },
                { "read1", method(c, "read1", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("read1") });
                        return none();
                    }) },
                { "readinto", method(c, "readinto", { py_positional("b") }, [](std::vector<py> const& a)
                    {
                        return a[0].method("_readinto", { a[1] }, { { "read1", boolean(false) } });
                    }) },
                { "readinto1", method(c, "readinto1", { py_positional("b") }, [](std::vector<py> const& a)
                    {
                        return a[0].method("_readinto", { a[1] }, { { "read1", boolean(true) } });
                    }) },
                { "_readinto", method(c, "_readinto", { py_positional("b"), py_positional("read1") }, [](std::vector<py> const& a)
                    {
                        py b = a[1];
                        if (!isinstance(b, builtin("memoryview")))
                            b = builtin("memoryview")(b);
                        b = b.method("cast", { S("B") });
                        py const data = a[0].method(a[2].truthy() ? "read1" : "read", { len(b) });
                        // (_io's checks (Modules/_io/bufferedio.c's _bufferediobase_readinto_generic))
                        if (!isinstance(data, builtin("bytes")))
                            py_raise("TypeError", "read() should return bytes");
                        py const n = len(data);
                        if (n > len(b))
                            py_raise("ValueError", "read() returned too much data: " + std::to_string(b.size()) + " bytes requested, " + n.str() + " returned");
                        b.set_item(builtin("slice")(none(), n), data);
                        return n;
                    }) },
                { "write", method(c, "write", { py_positional("b") }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("write") });
                        return none();
                    }) },
                { "detach", method(c, "detach", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("detach") });
                        return none();
                    }) }
            });
        }

        // --- _BufferedIOMixin

        void make_buffered_io_mixin()
        {
            char const* const c = "_BufferedIOMixin";
            // (a mixin implementation of BufferedIOBase with an underlying raw stream: this passes most requests on to the underlying raw stream;
            // it does *not* provide implementations of read(), readinto() or write())
            make_class(c, { cls("BufferedIOBase") }, {
                { "__init__", method(c, "__init__", { py_positional("raw") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_raw", a[1]);
                        return none();
                    }) },
                // ### Positioning ###
                { "seek", method(c, "seek", { py_positional("pos"), py_positional("whence", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        return checked_position(attached_raw(a[0]).method("seek", { a[1], a[2] }));
                    }) },
                { "tell", method(c, "tell", {}, [](std::vector<py> const& a)
                    {
                        return checked_position(attached_raw(a[0]).method("tell"));
                    }) },
                { "truncate", method(c, "truncate", { py_positional("pos", none()) }, [](std::vector<py> const& a)
                    {
                        // (_io's checks (Modules/_io/bufferedio.c))
                        check_closed(a[0], "truncate of closed file");
                        if (!a[0].method("writable").truthy())
                            raise_unsupported(S("truncate"));
                        // (flush the stream: we're mixing buffered I/O with lower-level I/O, and a flush may be necessary to synch both views of the
                        // current file state)
                        a[0].method("flush");
                        py const pos = a[1].is_none() ? a[0].method("tell") : a[1];
                        return attached_raw(a[0]).method("truncate", { pos });
                    }) },
                // ### Flush and close ###
                { "flush", method(c, "flush", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("closed").truthy())
                            py_raise("ValueError", "flush of closed file");
                        attached_raw(a[0]).method("flush");
                        return none();
                    }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("closed").truthy())
                        {
                            // (may raise BlockingIOError or BrokenPipeError etc)
                            try_finally([&]() { return a[0].method("flush"); }, [&]() { attached_raw(a[0]).method("close"); });
                        }
                        return none();
                    }) },
                { "detach", method(c, "detach", {}, [](std::vector<py> const& a)
                    {
                        attached_raw(a[0]); // (_io's check (Modules/_io/bufferedio.c's CHECK_INITIALIZED))
                        a[0].method("flush");
                        py const raw = a[0].attr("_raw");
                        a[0].set_attr("_raw", none());
                        return raw;
                    }) },
                // ### Inquiries ###
                { "seekable", method(c, "seekable", {}, [](std::vector<py> const& a) { return attached_raw(a[0]).method("seekable"); }) },
                { "raw", property(c, "raw", [](std::vector<py> const& a) { return a[0].attr("_raw"); }) },
                { "closed", property(c, "closed", [](std::vector<py> const& a) { return attached_raw(a[0]).attr("closed"); }) },
                { "name", property(c, "name", [](std::vector<py> const& a) { return attached_raw(a[0]).attr("name"); }) },
                { "mode", property(c, "mode", [](std::vector<py> const& a) { return attached_raw(a[0]).attr("mode"); }) },
                { "__getstate__", method(c, "__getstate__", {}, [](std::vector<py> const& a) -> py
                    {
                        py_raise("TypeError", "cannot pickle " + class_name(a[0]).repr() + " object");
                    }) },
                // (_io's repr (Modules/_io/bufferedio.c's buffered_repr): its type's name, and its name if it has one)
                { "__repr__", method(c, "__repr__", {}, [](std::vector<py> const& a)
                    {
                        std::string const typeName = type_name_of(a[0]);
                        py const name = optional_attribute(a[0], "name");
                        if (name.empty())
                            return S("<" + typeName + ">");
                        return S("<" + typeName + " name=" + name.repr() + ">");
                    }) },
                { "_dealloc_warn", method(c, "_dealloc_warn", { py_positional("source") }, [](std::vector<py> const& a)
                    {
                        py const deallocWarn = getattr_or_none(a[0].attr("raw"), "_dealloc_warn");
                        if (deallocWarn.truthy())
                            deallocWarn(a[1]);
                        return none();
                    }) },
                // ### Lower-level APIs ###
                { "fileno", method(c, "fileno", {}, [](std::vector<py> const& a) { return attached_raw(a[0]).method("fileno"); }) },
                { "isatty", method(c, "isatty", {}, [](std::vector<py> const& a) { return attached_raw(a[0]).method("isatty"); }) }
            });
        }

        // --- BufferedReader

        void make_buffered_reader()
        {
            char const* const c = "BufferedReader";
            make_class(c, { cls("_BufferedIOMixin") }, {
                { "__init__", method(c, "__init__", { py_positional("raw"), py_positional("buffer_size", py{ DEFAULT_BUFFER_SIZE }) }, [](std::vector<py> const& a)
                    {
                        check_readable(a[1]);
                        cls("_BufferedIOMixin").attr("__init__")(a[0], a[1]);
                        check_buffer_size(a[2]);
                        a[0].set_attr("buffer_size", a[2]);
                        a[0].method("_reset_read_buf");
                        a[0].set_attr("_read_lock", import_attribute("_thread", "allocate_lock")());
                        return none();
                    }) },
                { "readable", method(c, "readable", {}, [](std::vector<py> const& a) { return attached_raw(a[0]).method("readable"); }) },
                { "_reset_read_buf", method(c, "_reset_read_buf", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_read_buf", B(""));
                        a[0].set_attr("_read_pos", py{ 0 });
                        return none();
                    }) },
                { "read", method(c, "read", { py_positional("size", none()) }, [](std::vector<py> const& a)
                    {
                        if (size_argument(a[1]) < -1)
                            py_raise("ValueError", "read length must be non-negative or -1");
                        check_closed(a[0], "read of closed file");
                        return with_lock(a[0].attr("_read_lock"), [&]() { return a[0].method("_read_unlocked", { a[1] }); });
                    }) },
                { "_read_unlocked", method(c, "_read_unlocked", { py_positional("n", none()) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py nodataVal = B("");
                        py const emptyValues = py_tuple({ B(""), none() });
                        py const buf = self.attr("_read_buf");
                        py const pos = self.attr("_read_pos");
                        // (special case for when the number of bytes to read is unspecified)
                        if (a[1].is_none() || a[1] == py{ -1 })
                        {
                            self.method("_reset_read_buf");
                            if (hasattr(attached_raw(self), "readall"))
                            {
                                py const chunk = attached_raw(self).method("readall");
                                if (chunk.is_none())
                                {
                                    py const rest = from(buf, pos);
                                    return rest.truthy() ? rest : none();
                                }
                                return from(buf, pos) + chunk;
                            }
                            // (strip the consumed bytes)
                            py const chunks = py_list({ from(buf, pos) });
                            for (;;)
                            {
                                // (read until EOF or until read() would block)
                                py const chunk = attached_raw(self).method("read");
                                if (emptyValues.contains(chunk))
                                {
                                    nodataVal = chunk;
                                    break;
                                }
                                chunks.method("append", { chunk });
                            }
                            py const result = B("").method("join", { chunks });
                            return result.truthy() ? result : nodataVal;
                        }
                        // (the number of bytes to read is specified, return at most n bytes)
                        py n = a[1];
                        py avail = len(buf) - pos;   // (length of the available buffered data)
                        if (n <= avail)
                        {
                            // (fast path: the data to read is fully buffered)
                            self.set_attr("_read_pos", self.attr("_read_pos") + n);
                            return slice_of(buf, pos, pos + n);
                        }
                        // (slow path: read from the stream until enough bytes are read, or until an EOF occurs or until read() would block)
                        py const chunks = py_list({ from(buf, pos) });
                        py const wanted = builtin("max")(self.attr("buffer_size"), n);
                        while (avail < n)
                        {
                            py const chunk = attached_raw(self).method("read", { wanted });
                            if (emptyValues.contains(chunk))
                            {
                                nodataVal = chunk;
                                break;
                            }
                            avail = avail + len(chunk);
                            chunks.method("append", { chunk });
                        }
                        // (n is more than avail only when an EOF occurred or when read() would have blocked)
                        n = builtin("min")(n, avail);
                        py const out = B("").method("join", { chunks });
                        self.set_attr("_read_buf", from(out, n));     // (save the extra data in the buffer)
                        self.set_attr("_read_pos", py{ 0 });
                        return out.truthy() ? upto(out, n) : nodataVal;
                    }) },
                { "peek", method(c, "peek", { py_positional("size", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_checkClosed", { S("peek of closed file") });
                        return with_lock(a[0].attr("_read_lock"), [&]() { return a[0].method("_peek_unlocked", { a[1] }); });
                    }) },
                { "_peek_unlocked", method(c, "_peek_unlocked", { py_positional("n", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py const want = builtin("min")(a[1], self.attr("buffer_size"));
                        py const have = len(self.attr("_read_buf")) - self.attr("_read_pos");
                        if (have < want || have <= py{ 0 })
                        {
                            py const toRead = self.attr("buffer_size") - have;
                            py const current = attached_raw(self).method("read", { toRead });
                            if (current.truthy())
                            {
                                self.set_attr("_read_buf", from(self.attr("_read_buf"), self.attr("_read_pos")) + current);
                                self.set_attr("_read_pos", py{ 0 });
                            }
                        }
                        return from(self.attr("_read_buf"), self.attr("_read_pos"));
                    }) },
                { "read1", method(c, "read1", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        // (returns up to size bytes: if at least one byte is buffered, we only return buffered bytes; otherwise, we do one raw read)
                        py const& self = a[0];
                        self.method("_checkClosed", { S("read of closed file") });
                        py size = a[1];
                        if (size < py{ 0 })
                            size = self.attr("buffer_size");
                        if (size == py{ 0 })
                            return B("");
                        return with_lock(self.attr("_read_lock"), [&]()
                        {
                            self.method("_peek_unlocked", { py{ 1 } });
                            return self.method("_read_unlocked", { builtin("min")(size, len(self.attr("_read_buf")) - self.attr("_read_pos")) });
                        });
                    }) },
                // (implementing readinto() and readinto1() is not strictly necessary (we could rely on the base class that provides an
                // implementation in terms of read() and read1()); we do it anyway to keep the _pyio implementation similar to the io implementation
                // (which implements the methods for performance reasons))
                { "_readinto", method(c, "_readinto", { py_positional("buf"), py_positional("read1") }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        bool const read1 = a[2].truthy();
                        self.method("_checkClosed", { S("readinto of closed file") });
                        // (need to create a memoryview object of type 'b', otherwise we may not be able to assign bytes to it, and slicing it would
                        // create a new object)
                        py buf = a[1];
                        if (!isinstance(buf, builtin("memoryview")))
                            buf = builtin("memoryview")(buf);
                        if (buf.attr("nbytes") == py{ 0 })
                            return py{ 0 };
                        buf = buf.method("cast", { S("B") });
                        long long const size = buf.size();
                        long long written = 0;
                        with_lock(self.attr("_read_lock"), [&]()
                        {
                            while (written < size)
                            {
                                // (first try to read from internal buffer (at most the rest of buf: _pyio's min(..., len(buf)) can overrun it after a
                                // refill (ValueError); _io's BufferedReader does not))
                                py const readBuf = self.attr("_read_buf");
                                long long const readPos = self.attr("_read_pos").as_index();
                                long long const avail = std::min(readBuf.size() - readPos, size - written);
                                if (avail != 0)
                                {
                                    buf.set_item(builtin("slice")(py{ written }, py{ written + avail }), slice_of(readBuf, py{ readPos }, py{ readPos + avail }));
                                    self.set_attr("_read_pos", py{ readPos + avail });
                                    written += avail;
                                    if (written == size)
                                        break;
                                }
                                // (if remaining space in callers buffer is larger than internal buffer, read directly into callers buffer)
                                if (py{ size - written } > self.attr("buffer_size"))
                                {
                                    py const n = attached_raw(self).method("readinto", { from(buf, py{ written }) });
                                    if (!n.truthy())
                                        break; // (eof)
                                    written += n.as_index();
                                }
                                // (otherwise refill internal buffer - unless we're in read1 mode and already got some data)
                                else if (!(read1 && written != 0))
                                {
                                    if (!self.method("_peek_unlocked", { py{ 1 } }).truthy())
                                        break; // (eof)
                                }
                                // (in readinto1 mode, return as soon as we have some data)
                                if (read1 && written != 0)
                                    break;
                            }
                            return none();
                        });
                        return py{ written };
                    }) },
                { "tell", method(c, "tell", {}, [](std::vector<py> const& a)
                    {
                        // (GH-95782: keep return value non-negative)
                        py const pos = cls("_BufferedIOMixin").attr("tell")(a[0]) - len(a[0].attr("_read_buf")) + a[0].attr("_read_pos");
                        return builtin("max")(pos, py{ 0 });
                    }) },
                { "seek", method(c, "seek", { py_positional("pos"), py_positional("whence", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        check_whence(a[2]);
                        self.method("_checkClosed", { S("seek of closed file") });
                        check_seekable(attached_raw(self));
                        return with_lock(self.attr("_read_lock"), [&]()
                        {
                            py pos = a[1];
                            if (a[2] == py{ 1 })
                                pos = pos - (len(self.attr("_read_buf")) - self.attr("_read_pos"));
                            pos = cls("_BufferedIOMixin").attr("seek")(self, pos, a[2]);
                            self.method("_reset_read_buf");
                            return pos;
                        });
                    }) }
            });
        }

        // --- BufferedWriter

        // (Modules/_io/bufferedio.c's _set_BlockingIOError: its errno is the raw write's)
        thread_local int tRawErrno = 0;    // (errno after the last raw write)
        [[noreturn]] void raise_blocking_io_error(py const& aWritten)
        {
            py_raise(builtin("BlockingIOError")(py{ tRawErrno }, S("write could not complete without blocking"), aWritten));
        }

        // (raw.write(aData): the number of bytes written; -2 if the raw stream is non-blocking and would have blocked (it returned None) (Modules/
        // _io/bufferedio.c's _bufferedwriter_raw_write: an InterruptedError is retried))

        long long raw_write(py const& aSelf, py const& aData)
        {
            py result;
            for (;;)
            {
                try
                {
                    errno = 0;
                    result = attached_raw(aSelf).method("write", { aData });
                    tRawErrno = errno;
                    break;
                }
                catch (raised const&)
                {
                    if (!caught({ "InterruptedError" }))
                        throw;
                }
            }
            if (result.is_none())
                return -2;
            long long const n = result.as_index();
            long long const size = aData.size();
            if (n < 0 || n > size)
                py_raise("OSError", "raw write() returned invalid length " + std::to_string(n) + " (should have been between 0 and " + std::to_string(size) + ")");
            return n;
        }

        void make_buffered_writer()
        {
            char const* const c = "BufferedWriter";
            make_class(c, { cls("_BufferedIOMixin") }, {
                { "__init__", method(c, "__init__", { py_positional("raw"), py_positional("buffer_size", py{ DEFAULT_BUFFER_SIZE }) }, [](std::vector<py> const& a)
                    {
                        check_writable(a[1]);
                        cls("_BufferedIOMixin").attr("__init__")(a[0], a[1]);
                        check_buffer_size(a[2]);
                        a[0].set_attr("buffer_size", a[2]);
                        a[0].set_attr("_write_buf", builtin("bytearray")());
                        a[0].set_attr("_write_lock", import_attribute("_thread", "allocate_lock")());
                        return none();
                    }) },
                { "writable", method(c, "writable", {}, [](std::vector<py> const& a) { return attached_raw(a[0]).method("writable"); }) },
                // (write(): _io's policy (Modules/_io/bufferedio.c's _io_BufferedWriter_write_impl): data that fits is buffered; otherwise the
                // buffer is flushed and the data is written to the raw stream until less than a buffer's worth remains, which is buffered (_pyio's
                // buffers everything and then flushes all of it))
                { "write", method(c, "write", { py_positional("b") }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        std::string_view bytes;
                        if (!bytes_like(a[1].get(), bytes))
                            py_raise("TypeError", "a bytes-like object is required, not '" + class_name(a[1]).str() + "'");
                        py const data = builtin("memoryview")(a[1]).method("cast", { S("B") });
                        return with_lock(self.attr("_write_lock"), [&]()
                        {
                            if (self.attr("closed").truthy())
                                py_raise("ValueError", "write to closed file");
                            long long const bufferSize = self.attr("buffer_size").as_index();
                            long long const length = data.size();
                            // (fast path: the data to write can be fully buffered)
                            if (length <= bufferSize - self.attr("_write_buf").size() && length < bufferSize)
                            {
                                self.attr("_write_buf").method("extend", { data });
                                return py{ length };
                            }
                            // (first write the current buffer)
                            try
                            {
                                self.method("_flush_unlocked");
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                if (!isinstance(e, builtin("BlockingIOError")))
                                    py_reraise(e);
                                // (the bytes that were not written remain in the buffer)
                                long long const avail = bufferSize - self.attr("_write_buf").size();
                                if (length <= avail)
                                {
                                    // (everything can be buffered)
                                    self.attr("_write_buf").method("extend", { data });
                                    return py{ length };
                                }
                                // (buffer as much as possible)
                                self.attr("_write_buf").method("extend", { upto(data, py{ avail }) });
                                raise_blocking_io_error(py{ avail });
                            }
                            // (then write buf itself: at this point the buffer has been emptied)
                            long long remaining = length;
                            long long written = 0;
                            while (remaining >= bufferSize)
                            {
                                long long const n = raw_write(self, from(data, py{ written }));
                                if (n == -2)
                                {
                                    // (write failed because raw file is non-blocking)
                                    if (remaining > bufferSize)
                                    {
                                        // (can't buffer everything, still buffer as much as possible)
                                        self.attr("_write_buf").method("extend", { slice_of(data, py{ written }, py{ written + bufferSize }) });
                                        written += bufferSize;
                                        raise_blocking_io_error(py{ written });
                                    }
                                    break;
                                }
                                written += n;
                                remaining -= n;
                            }
                            if (remaining > 0)
                            {
                                self.attr("_write_buf").method("extend", { from(data, py{ written }) });
                                written += remaining;
                            }
                            return py{ written };
                        });
                    }) },
                { "truncate", method(c, "truncate", { py_positional("pos", none()) }, [](std::vector<py> const& a)
                    {
                        return with_lock(a[0].attr("_write_lock"), [&]()
                        {
                            a[0].method("_flush_unlocked");
                            py const pos = a[1].is_none() ? attached_raw(a[0]).method("tell") : a[1];
                            return attached_raw(a[0]).method("truncate", { pos });
                        });
                    }) },
                { "flush", method(c, "flush", {}, [](std::vector<py> const& a)
                    {
                        with_lock(a[0].attr("_write_lock"), [&]() { return a[0].method("_flush_unlocked"); });
                        return none();
                    }) },
                { "_flush_unlocked", method(c, "_flush_unlocked", {}, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        if (self.attr("closed").truthy())
                            py_raise("ValueError", "flush of closed file");
                        // (_io's (Modules/_io/bufferedio.c's _bufferedwriter_flush_unlocked): a BlockingIOError of the raw stream's is raised as it is
                        // (_pyio raises RuntimeError))
                        while (self.attr("_write_buf").truthy())
                        {
                            long long const n = raw_write(self, self.attr("_write_buf"));
                            if (n == -2)
                                raise_blocking_io_error(py{ 0 });
                            delete_from(self.attr("_write_buf"), none(), py{ n });
                        }
                        return none();
                    }) },
                { "tell", method(c, "tell", {}, [](std::vector<py> const& a)
                    {
                        return cls("_BufferedIOMixin").attr("tell")(a[0]) + len(a[0].attr("_write_buf"));
                    }) },
                { "seek", method(c, "seek", { py_positional("pos"), py_positional("whence", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        check_whence(a[2]);
                        check_closed(a[0], "seek of closed file");
                        check_seekable(attached_raw(a[0]));
                        return with_lock(a[0].attr("_write_lock"), [&]()
                        {
                            a[0].method("_flush_unlocked");
                            return cls("_BufferedIOMixin").attr("seek")(a[0], a[1], a[2]);
                        });
                    }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        bool const done = with_lock(self.attr("_write_lock"), [&]()
                        {
                            return boolean(self.attr("closed").truthy());
                        }).truthy();
                        if (done)
                            return none();
                        // (we have to release the lock and call self.flush() (which will probably just re-take the lock) in case flush has been
                        // overridden in a subclass or the user set self.flush to something; this is the same behavior as the C implementation)
                        try_finally([&]()
                        {
                            // (may raise BlockingIOError or BrokenPipeError etc)
                            return self.method("flush");
                        }, [&]()
                        {
                            with_lock(self.attr("_write_lock"), [&]() { return attached_raw(self).method("close"); });
                        });
                        return none();
                    }) }
            });
        }

        // --- BufferedRWPair

        void make_buffered_rw_pair()
        {
            char const* const c = "BufferedRWPair";
            // (XXX the usefulness of this (compared to having two separate IO objects) is questionable)
            make_class(c, { cls("BufferedIOBase") }, {
                { "__init__", method(c, "__init__", { py_positional("reader"), py_positional("writer"), py_positional("buffer_size", py{ DEFAULT_BUFFER_SIZE }) },
                    [](std::vector<py> const& a)
                    {
                        check_readable(a[1]);
                        check_writable(a[2]);
                        a[0].set_attr("reader", cls("BufferedReader")(a[1], a[3]));
                        a[0].set_attr("writer", cls("BufferedWriter")(a[2], a[3]));
                        return none();
                    }) },
                { "read", method(c, "read", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("reader").method("read", { a[1].is_none() ? py{ -1 } : a[1] });
                    }) },
                { "readinto", method(c, "readinto", { py_positional("b") }, [](std::vector<py> const& a) { return a[0].attr("reader").method("readinto", { a[1] }); }) },
                { "write", method(c, "write", { py_positional("b") }, [](std::vector<py> const& a) { return a[0].attr("writer").method("write", { a[1] }); }) },
                { "peek", method(c, "peek", { py_positional("size", py{ 0 }) }, [](std::vector<py> const& a) { return a[0].attr("reader").method("peek", { a[1] }); }) },
                { "read1", method(c, "read1", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a) { return a[0].attr("reader").method("read1", { a[1] }); }) },
                { "readinto1", method(c, "readinto1", { py_positional("b") }, [](std::vector<py> const& a) { return a[0].attr("reader").method("readinto1", { a[1] }); }) },
                { "readable", method(c, "readable", {}, [](std::vector<py> const& a) { return a[0].attr("reader").method("readable"); }) },
                { "writable", method(c, "writable", {}, [](std::vector<py> const& a) { return a[0].attr("writer").method("writable"); }) },
                { "flush", method(c, "flush", {}, [](std::vector<py> const& a) { return a[0].attr("writer").method("flush"); }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        try_finally([&]() { return a[0].attr("writer").method("close"); }, [&]() { a[0].attr("reader").method("close"); });
                        return none();
                    }) },
                { "isatty", method(c, "isatty", {}, [](std::vector<py> const& a)
                    {
                        py const r = a[0].attr("reader").method("isatty");
                        return r.truthy() ? r : a[0].attr("writer").method("isatty");
                    }) },
                { "closed", property(c, "closed", [](std::vector<py> const& a) { return a[0].attr("writer").attr("closed"); }) }
            });
        }

        // --- BufferedRandom

        void make_buffered_random()
        {
            char const* const c = "BufferedRandom";
            auto const reader = [](char const* aName) { return cls("BufferedReader").attr(aName); };
            auto const writer = [](char const* aName) { return cls("BufferedWriter").attr(aName); };
            // (undo read ahead)
            auto const undoReadAhead = [](py const& aSelf)
            {
                attached_raw(aSelf).method("seek", { aSelf.attr("_read_pos") - len(aSelf.attr("_read_buf")), py{ 1 } });
            };
            make_class(c, { cls("BufferedWriter"), cls("BufferedReader") }, {
                { "__init__", method(c, "__init__", { py_positional("raw"), py_positional("buffer_size", py{ DEFAULT_BUFFER_SIZE }) }, [reader, writer](std::vector<py> const& a)
                    {
                        // (_io's checks (Modules/_io/iobase.c's _PyIOBase_check_seekable, ...))
                        check_seekable(a[1]);
                        reader("__init__")(a[0], a[1], a[2]);
                        writer("__init__")(a[0], a[1], a[2]);
                        return none();
                    }) },
                { "seek", method(c, "seek", { py_positional("pos"), py_positional("whence", py{ 0 }) }, [undoReadAhead](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        check_whence(a[2]);
                        check_closed(self, "seek of closed file");
                        check_seekable(attached_raw(self));
                        self.method("flush");
                        if (self.attr("_read_buf").truthy())
                            with_lock(self.attr("_read_lock"), [&]() { undoReadAhead(self); return none(); });
                        // (first do the raw seek, then empty the read buffer, so that if the raw seek fails, we don't lose buffered data forever)
                        py const pos = attached_raw(self).method("seek", { a[1], a[2] });
                        with_lock(self.attr("_read_lock"), [&]() { return self.method("_reset_read_buf"); });
                        if (pos < py{ 0 })
                            py_raise("OSError", "seek() returned invalid position");
                        return pos;
                    }) },
                { "tell", method(c, "tell", {}, [reader, writer](std::vector<py> const& a)
                    {
                        return a[0].attr("_write_buf").truthy() ? writer("tell")(a[0]) : reader("tell")(a[0]);
                    }) },
                { "truncate", method(c, "truncate", { py_positional("pos", none()) }, [writer](std::vector<py> const& a)
                    {
                        py const pos = a[1].is_none() ? a[0].method("tell") : a[1];
                        // (use seek to flush the read buffer)
                        return writer("truncate")(a[0], pos);
                    }) },
                { "read", method(c, "read", { py_positional("size", none()) }, [reader](std::vector<py> const& a)
                    {
                        py const size = a[1].is_none() ? py{ -1 } : a[1];
                        a[0].method("flush");
                        return reader("read")(a[0], size);
                    }) },
                { "readinto", method(c, "readinto", { py_positional("b") }, [reader](std::vector<py> const& a)
                    {
                        a[0].method("flush");
                        return reader("readinto")(a[0], a[1]);
                    }) },
                { "peek", method(c, "peek", { py_positional("size", py{ 0 }) }, [reader](std::vector<py> const& a)
                    {
                        a[0].method("flush");
                        return reader("peek")(a[0], a[1]);
                    }) },
                { "read1", method(c, "read1", { py_positional("size", py{ -1 }) }, [reader](std::vector<py> const& a)
                    {
                        a[0].method("flush");
                        return reader("read1")(a[0], a[1]);
                    }) },
                { "readinto1", method(c, "readinto1", { py_positional("b") }, [reader](std::vector<py> const& a)
                    {
                        a[0].method("flush");
                        return reader("readinto1")(a[0], a[1]);
                    }) },
                { "write", method(c, "write", { py_positional("b") }, [writer, undoReadAhead](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        if (self.attr("_read_buf").truthy())
                        {
                            with_lock(self.attr("_read_lock"), [&]()
                            {
                                undoReadAhead(self);
                                return self.method("_reset_read_buf");
                            });
                        }
                        return writer("write")(self, a[1]);
                    }) }
            });
        }

        // --- TextIOBase

        void make_text_iobase()
        {
            char const* const c = "TextIOBase";
            make_class(c, { cls("IOBase") }, {
                { "read", method(c, "read", { py_positional("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("read") });
                        return none();
                    }) },
                { "write", method(c, "write", { py_positional("s") }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("write") });
                        return none();
                    }) },
                { "truncate", method(c, "truncate", { py_positional("pos", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("truncate") });
                        return none();
                    }) },
                { "readline", method(c, "readline", { py_positional_only("size", py{ -1 }) }, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("readline") });
                        return none();
                    }) },
                { "detach", method(c, "detach", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_unsupported", { S("detach") });
                        return none();
                    }) },
                { "encoding", property(c, "encoding", [](std::vector<py> const&) { return none(); }) },
                { "newlines", property(c, "newlines", [](std::vector<py> const&) { return none(); }) },
                { "errors", property(c, "errors", [](std::vector<py> const&) { return none(); }) }
            });
        }

        // --- IncrementalNewlineDecoder

        void make_incremental_newline_decoder()
        {
            char const* const c = "IncrementalNewlineDecoder";
            make_class(c, { import_attribute("codecs", "IncrementalDecoder") }, {
                { "__init__", method(c, "__init__", { py_positional("decoder"), py_positional("translate"), py_positional("errors", S("strict")) }, [](std::vector<py> const& a)
                    {
                        import_attribute("codecs", "IncrementalDecoder").attr("__init__").call({ a[0] }, { { "errors", a[3] } });
                        a[0].set_attr("translate", a[2]);
                        a[0].set_attr("decoder", a[1]);
                        a[0].set_attr("seennl", py{ 0 });
                        a[0].set_attr("pendingcr", boolean(false));
                        return none();
                    }) },
                { "decode", method(c, "decode", { py_positional("input"), py_positional("final", boolean(false)) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py const& final = a[2];
                        // (decode input (with the eventual \r from a previous pass))
                        py output = self.attr("decoder").is_none() ? a[1] : self.attr("decoder").method("decode", { a[1] }, { { "final", final } });
                        if (self.attr("pendingcr").truthy() && (output.truthy() || final.truthy()))
                        {
                            output = S("\r") + output;
                            self.set_attr("pendingcr", boolean(false));
                        }
                        // (retain last \r even when not translating data: then readline() is sure to get \r\n in one pass)
                        if (output.method("endswith", { S("\r") }).truthy() && !final.truthy())
                        {
                            output = upto(output, py{ -1 });
                            self.set_attr("pendingcr", boolean(true));
                        }
                        // (record which newlines are read)
                        py const crlf = output.method("count", { S("\r\n") });
                        py const cr = output.method("count", { S("\r") }) - crlf;
                        py const lf = output.method("count", { S("\n") }) - crlf;
                        auto const flag = [&](py const& aCount, char const* aFlag) { return aCount.truthy() ? self.attr(aFlag) : aCount; };
                        self.set_attr("seennl", self.attr("seennl") | (flag(lf, "_LF") | flag(cr, "_CR") | flag(crlf, "_CRLF")));
                        if (self.attr("translate").truthy())
                        {
                            if (crlf.truthy())
                                output = output.method("replace", { S("\r\n"), S("\n") });
                            if (cr.truthy())
                                output = output.method("replace", { S("\r"), S("\n") });
                        }
                        return output;
                    }) },
                { "getstate", method(c, "getstate", {}, [](std::vector<py> const& a)
                    {
                        py buf, flag;
                        if (a[0].attr("decoder").is_none())
                        {
                            buf = B("");
                            flag = py{ 0 };
                        }
                        else
                        {
                            auto const state = a[0].attr("decoder").method("getstate").items();
                            if (state.size() != 2u)
                                py_raise("ValueError", state.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(state.size()) + ")" :
                                    "too many values to unpack (expected 2)");
                            buf = state[0];
                            flag = state[1];
                        }
                        flag = flag << py{ 1 };
                        if (a[0].attr("pendingcr").truthy())
                            flag = flag | py{ 1 };
                        return py_tuple({ buf, flag });
                    }) },
                { "setstate", method(c, "setstate", { py_positional("state") }, [](std::vector<py> const& a)
                    {
                        auto const state = a[1].items();
                        if (state.size() != 2u)
                            py_raise("ValueError", state.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(state.size()) + ")" :
                                "too many values to unpack (expected 2)");
                        a[0].set_attr("pendingcr", builtin("bool")(state[1] & py{ 1 }));
                        if (!a[0].attr("decoder").is_none())
                            a[0].attr("decoder").method("setstate", { py_tuple({ state[0], state[1] >> py{ 1 } }) });
                        return none();
                    }) },
                { "reset", method(c, "reset", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("seennl", py{ 0 });
                        a[0].set_attr("pendingcr", boolean(false));
                        if (!a[0].attr("decoder").is_none())
                            a[0].attr("decoder").method("reset");
                        return none();
                    }) },
                { "_LF", py{ 1 } },
                { "_CR", py{ 2 } },
                { "_CRLF", py{ 4 } },
                { "newlines", property(c, "newlines", [](std::vector<py> const& a)
                    {
                        py const newlines = py_tuple({ none(), S("\n"), S("\r"), py_tuple({ S("\r"), S("\n") }), S("\r\n"), py_tuple({ S("\n"), S("\r\n") }),
                            py_tuple({ S("\r"), S("\r\n") }), py_tuple({ S("\r"), S("\n"), S("\r\n") }) });
                        return newlines[a[0].attr("seennl")];
                    }) }
            });
        }

        // --- TextIOWrapper

        // (a pair (unpacked))
        std::pair<py, py> unpack2(py const& aPair)
        {
            auto const items = aPair.items();
            if (items.size() != 2u)
                py_raise("ValueError", items.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(items.size()) + ")" :
                    "too many values to unpack (expected 2)");
            return { items[0], items[1] };
        }

        // (the meaning of a tell() cookie is: seek to position, set the decoder flags to dec_flags, read bytes_to_feed bytes, feed them into
        // the decoder with need_eof as the EOF flag, then skip chars_to_skip characters of the decoded result; for most simple decoders, tell()
        // will often just give a byte offset in the file)
        py pack_cookie(py const& aPosition, py const& aDecFlags = py{ 0 }, py const& aBytesToFeed = py{ 0 }, bool aNeedEof = false,
            py const& aCharsToSkip = py{ 0 })
        {
            return aPosition | (aDecFlags << py{ 64 }) | (aBytesToFeed << py{ 128 }) | (aCharsToSkip << py{ 192 }) | (py{ aNeedEof ? 1 : 0 } << py{ 256 });
        }

        // --- TextIOWrapper's readline (Modules/_io/textio.c's _textiowrapper_readline and _PyIO_find_line_ending): its line ending is found in
        // its decoded chars where they are (their UTF-8: a line ending is ASCII), not in a copy of them

        // (the number of characters of UTF-8 text [aBegin, aEnd))
        long long char_count(std::string_view aText, std::size_t aBegin, std::size_t aEnd)
        {
            long long n = 0;
            for (auto i = aBegin; i < aEnd; ++i)
                if ((static_cast<unsigned char>(aText[i]) & 0xC0u) != 0x80u)
                    ++n;
            return n;
        }

        // (the offset of the character aChars characters after aBegin)
        std::size_t advance_chars(std::string_view aText, std::size_t aBegin, long long aChars)
        {
            auto i = aBegin;
            for (; aChars > 0 && i < aText.size(); --aChars)
            {
                ++i;
                while (i < aText.size() && (static_cast<unsigned char>(aText[i]) & 0xC0u) == 0x80u)
                    ++i;
            }
            return i;
        }

        // (the byte offset of a str's character: the offset that was found last is remembered (a TextIOWrapper's reads advance through its decoded
        // chunk); the str is kept (so it is the same str))
        struct char_offset
        {
            ref text;
            long long chars = 0;
            std::size_t bytes = 0u;
        };
        char_offset& last_char_offset()
        {
            static auto* sLast = new char_offset{};
            return *sLast;
        }
        std::size_t byte_offset(py const& aText, long long aChars)
        {
            // (ASCII: its characters are its bytes)
            auto const& s = *as<str_object>(aText.get());
            if (s.is_ascii())
                return static_cast<std::size_t>(aChars);
            auto& last = last_char_offset();
            std::string_view const value = s.value;
            long long chars = 0;
            std::size_t bytes = 0u;
            if (last.text.get() == aText.get() && last.chars <= aChars)
            {
                chars = last.chars;
                bytes = last.bytes;
            }
            bytes = advance_chars(value, bytes, aChars - chars);
            last.text = ref::borrowed(aText.get());
            last.chars = aChars;
            last.bytes = bytes;
            return bytes;
        }

        // (_PyIO_find_line_ending: the end of the line in aText from aStart (its offset after its line ending), else aConsumed: how much of it can
        // be put aside (it has no line ending))
        std::optional<std::size_t> find_line_ending(bool aTranslated, bool aUniversal, std::string_view aReadnl, std::string_view aText, std::size_t aStart,
            std::size_t& aConsumed)
        {
            if (aTranslated)
            {
                // (newlines are already translated, only search for \n)
                auto const pos = aText.find('\n', aStart);
                if (pos != std::string_view::npos)
                    return pos + 1u;
                aConsumed = aText.size();
                return {};
            }
            if (aUniversal)
            {
                // (universal newline search: find any of \r, \r\n, \n; the decoder ensures that \r\n are not split in two pieces)
                auto const pos = aText.find_first_of("\r\n", aStart);
                if (pos == std::string_view::npos)
                {
                    aConsumed = aText.size();
                    return {};
                }
                if (aText[pos] == '\r' && pos + 1u < aText.size() && aText[pos + 1u] == '\n')
                    return pos + 2u;
                return pos + 1u;
            }
            // (non-universal mode)
            if (aReadnl.size() == 1u)
            {
                auto const pos = aText.find(aReadnl[0], aStart);
                if (pos != std::string_view::npos)
                    return pos + 1u;
                aConsumed = aText.size();
                return {};
            }
            auto s = aStart;
            auto e = aText.size() >= aReadnl.size() - 1u ? aText.size() - (aReadnl.size() - 1u) : 0u;
            if (e < s)
                e = s;
            while (s < e)
            {
                auto const pos = aText.find(aReadnl[0], s);
                if (pos == std::string_view::npos || pos >= e)
                    break;
                if (aText.compare(pos, aReadnl.size(), aReadnl) == 0)
                    return pos + aReadnl.size();
                s = pos + 1u;
            }
            auto const pos = aText.find(aReadnl[0], e);
            aConsumed = pos == std::string_view::npos ? aText.size() : pos;
            return {};
        }

        // (a TextIOWrapper's closed: its buffer's; a BufferedReader's, BufferedWriter's or BufferedRandom's of a FileIO: whether its file is
        // closed (Modules/_io/textio.c's CHECK_CLOSED))
        bool text_closed(py const& aSelf)
        {
            // (the classes: found once (they are made once))
            static py const& textIOWrapper = cls("TextIOWrapper");
            static py const& bufferedReader = cls("BufferedReader");
            static py const& bufferedRandom = cls("BufferedRandom");
            static py const& bufferedWriter = cls("BufferedWriter");
            static py const& fileIO = cls("FileIO");
            if (type_of(aSelf).is(textIOWrapper))
            {
                py const buffer = aSelf.attr("_buffer");
                py const type = type_of(buffer);
                if (type.is(bufferedReader) || type.is(bufferedRandom) || type.is(bufferedWriter))
                {
                    py const raw = buffer.attr("_raw");
                    if (type_of(raw).is(fileIO))
                        return fileio_of(raw).fd < 0;
                }
            }
            return aSelf.attr("closed").truthy();
        }

        // (_textiowrapper_readline: a line (at most aLimit characters (-1: no limit)); empty if its decoded chars are not strs (the port's
        // readline then reads it))
        std::optional<py> fast_readline(py const& aSelf, long long aLimit, std::function<void()> const& aBeforeReading)
        {
            auto const decodedChars = [&]() -> std::optional<py>
            {
                py chars = aSelf.attr("_decoded_chars");
                if (kind_of(chars.get()) != NEOS_PY_STR)
                    return {};
                return chars;
            };
            auto const setDecodedChars = [&](py const& aChars)
            {
                aSelf.set_attr("_decoded_chars", aChars);
                aSelf.set_attr("_decoded_chars_used", py{ 0 });
            };
            {
                auto const first = decodedChars();
                if (!first)
                    return {};
            }
            bool checked = false;
            bool const translated = aSelf.attr("_readtranslate").truthy();
            bool const universal = aSelf.attr("_readuniversal").truthy();
            py const readnlObject = aSelf.attr("_readnl");
            std::string const readnl = readnlObject.is_none() ? std::string{} : readnlObject.str();
            std::string chunks;             // (the text put aside)
            long long chunked = 0;          // (its characters)
            std::string remaining;
            bool haveRemaining = false;
            std::string concatenated;
            py decoded;
            std::string_view text;          // (the line's text: the decoded chars (or what remained of the previous ones and them))
            bool ascii = false;             // (its characters are its bytes)
            bool haveLine = false;
            auto const count_of = [&](std::string_view aText, std::size_t aBegin, std::size_t aEnd)
            {
                return ascii ? static_cast<long long>(aEnd - aBegin) : char_count(aText, aBegin, aEnd);
            };
            auto const advance_of = [&](std::string_view aText, std::size_t aBegin, long long aChars)
            {
                return ascii ? std::min(aBegin + static_cast<std::size_t>(aChars), aText.size()) : advance_chars(aText, aBegin, aChars);
            };
            long long start = 0;
            long long endpos = 0;
            long long offsetToBuffer = 0;
            std::size_t startByte = 0u;
            std::size_t endByte = 0u;
            std::size_t offsetToBufferBytes = 0u;
            for (;;)
            {
                // (first, get some data if necessary)
                bool more = true;
                for (;;)
                {
                    auto const chars = decodedChars();
                    if (!chars)
                        return {};
                    decoded = *chars;
                    if (!as<str_object>(decoded.get())->value.empty())
                        break;
                    if (!checked)
                    {
                        aBeforeReading();
                        checked = true;
                    }
                    more = aSelf.method("_read_chunk").truthy();
                    if (!more)
                        break;
                }
                if (!more)
                {
                    // (end of file)
                    setDecodedChars(S(""));
                    aSelf.set_attr("_snapshot", none());
                    haveLine = false;
                    break;
                }
                if (!haveRemaining)
                {
                    auto const& chars = *as<str_object>(decoded.get());
                    ascii = chars.is_ascii();
                    text = chars.value;
                    start = aSelf.attr("_decoded_chars_used").as_index();
                    startByte = byte_offset(decoded, start);
                    offsetToBuffer = 0;
                    offsetToBufferBytes = 0u;
                }
                else
                {
                    concatenated = remaining + as<str_object>(decoded.get())->value;
                    text = concatenated;
                    ascii = false;
                    start = 0;
                    startByte = 0u;
                    offsetToBuffer = char_count(remaining, 0u, remaining.size());
                    offsetToBufferBytes = remaining.size();
                    remaining.clear();
                    haveRemaining = false;
                }
                haveLine = true;
                std::size_t consumed = 0u;
                if (auto const found = find_line_ending(translated, universal, readnl, text, startByte, consumed))
                {
                    endByte = *found;
                    endpos = start + count_of(text, startByte, endByte);
                    if (aLimit >= 0 && (endpos - start) + chunked >= aLimit)
                    {
                        endpos = start + aLimit - chunked;
                        endByte = advance_of(text, startByte, aLimit - chunked);
                    }
                    break;
                }
                // (we can put aside up to endpos)
                endByte = consumed;
                endpos = start + count_of(text, startByte, endByte);
                if (aLimit >= 0 && (endpos - start) + chunked >= aLimit)
                {
                    // (didn't find line ending, but reached length limit)
                    endpos = start + aLimit - chunked;
                    endByte = advance_of(text, startByte, aLimit - chunked);
                    break;
                }
                if (endByte > startByte)
                {
                    // (no line ending seen yet - put aside current data)
                    chunks.append(text.substr(startByte, endByte - startByte));
                    chunked += endpos - start;
                }
                // (there may be some remaining bytes we'll have to prepend to the next chunk of data)
                if (endByte < text.size())
                {
                    remaining = std::string{ text.substr(endByte) };
                    haveRemaining = true;
                }
                haveLine = false;
                // (we have consumed the buffer)
                setDecodedChars(S(""));
            }
            std::string result = std::move(chunks);
            if (haveRemaining)
                result += remaining;
            if (haveLine)
            {
                // (our line ends in the current buffer)
                aSelf.set_attr("_decoded_chars_used", py{ endpos - offsetToBuffer });
                auto& last = last_char_offset();
                last.text = ref::borrowed(decoded.get());
                last.chars = endpos - offsetToBuffer;
                last.bytes = endByte - offsetToBufferBytes;
                result.append(text.substr(startByte, endByte - startByte));
            }
            return S(result);
        }

        void make_text_io_wrapper()
        {
            char const* const c = "TextIOWrapper";
            // (_get_decoded_chars(n=None): advance into the _decoded_chars buffer)
            auto const getDecodedChars = [](py const& aSelf, py const& aCount = py::none())
            {
                py const offset = aSelf.attr("_decoded_chars_used");
                py const chars = aCount.is_none() ? from(aSelf.attr("_decoded_chars"), offset) : slice_of(aSelf.attr("_decoded_chars"), offset, offset + aCount);
                aSelf.set_attr("_decoded_chars_used", aSelf.attr("_decoded_chars_used") + len(chars));
                return chars;
            };
            auto const setDecodedChars = [](py const& aSelf, py const& aChars)
            {
                aSelf.set_attr("_decoded_chars", aChars);
                aSelf.set_attr("_decoded_chars_used", py{ 0 });
            };
            auto const getDecoder = [](py const& aSelf) { py const d = aSelf.attr("_decoder"); return d.truthy() ? d : aSelf.method("_get_decoder"); };
            auto const getEncoder = [](py const& aSelf) { py const e = aSelf.attr("_encoder"); return e.truthy() ? e : aSelf.method("_get_encoder"); };
            // (readline(size=None))
            auto const readline = [getDecodedChars, setDecodedChars](py const& aSelf, py const& aSize) -> py
                {
                    py const& self = aSelf;
                    // (_io's checks and messages (Modules/_io/textio.c): its size is an index (not None))
                    long long const size = aSize.as_index();
                    if (text_closed(self))
                        py_raise("ValueError", "I/O operation on closed file.");
                    auto const readable = [&]()
                    {
                        if (!self.method("readable").truthy())
                            raise_unsupported(S("not readable"));
                        // (make the decoder if it doesn't already exist)
                        if (!self.attr("_decoder").truthy())
                            self.method("_get_decoder");
                    };
                    // (its decoded chars are read without the checks of reading more (as Modules/_io/textio.c's are))
                    if (auto const line = fast_readline(self, size, readable))
                        return *line;
                    readable();
                    // (grab all the decoded text (we will rewind any extra bits later))
                    py line = getDecodedChars(self);
                    long long start = 0;
                    // (make the decoder if it doesn't already exist)
                    if (!self.attr("_decoder").truthy())
                        self.method("_get_decoder");
                    long long endpos = 0;
                    auto const find = [&](char const* aText, long long aStart) { return line.method("find", { S(aText), py{ aStart } }).as_index(); };
                    for (;;)
                    {
                        if (self.attr("_readtranslate").truthy())
                        {
                            // (newlines are already translated, only search for \n)
                            long long const pos = find("\n", start);
                            if (pos >= 0)
                            {
                                endpos = pos + 1;
                                break;
                            }
                            start = line.size();
                        }
                        else if (self.attr("_readuniversal").truthy())
                        {
                            // (universal newline search: find any of \r, \r\n, \n; the decoder ensures that \r\n are not split in two pieces)
                            long long const nlpos = find("\n", start);
                            long long const crpos = find("\r", start);
                            if (crpos == -1)
                            {
                                if (nlpos == -1)
                                {
                                    // (nothing found)
                                    start = line.size();
                                }
                                else
                                {
                                    // (found \n)
                                    endpos = nlpos + 1;
                                    break;
                                }
                            }
                            else if (nlpos == -1)
                            {
                                // (found lone \r)
                                endpos = crpos + 1;
                                break;
                            }
                            else if (nlpos < crpos)
                            {
                                // (found \n)
                                endpos = nlpos + 1;
                                break;
                            }
                            else if (nlpos == crpos + 1)
                            {
                                // (found \r\n)
                                endpos = crpos + 2;
                                break;
                            }
                            else
                            {
                                // (found \r)
                                endpos = crpos + 1;
                                break;
                            }
                        }
                        else
                        {
                            // (non-universal)
                            long long const pos = line.method("find", { self.attr("_readnl") }).as_index();
                            if (pos >= 0)
                            {
                                endpos = pos + self.attr("_readnl").size();
                                break;
                            }
                        }
                        if (size >= 0 && line.size() >= size)
                        {
                            endpos = size; // (reached length size)
                            break;
                        }
                        // (no line ending seen yet - get more data)
                        while (self.method("_read_chunk").truthy())
                            if (self.attr("_decoded_chars").truthy())
                                break;
                        if (self.attr("_decoded_chars").truthy())
                            line = line + getDecodedChars(self);
                        else
                        {
                            // (end of file)
                            setDecodedChars(self, S(""));
                            self.set_attr("_snapshot", none());
                            return line;
                        }
                    }
                    if (size >= 0 && endpos > size)
                        endpos = size;  // (don't exceed size)
                    // (rewind _decoded_chars to just after the line ending we found)
                    self.method("_rewind_decoded_chars", { py{ line.size() - endpos } });
                    return upto(line, py{ endpos });
                };
            make_class(c, { cls("TextIOBase") }, {
                { "_CHUNK_SIZE", py{ 2048 } },
                // (initialize _buffer as soon as possible since it's used by __del__() which calls close())
                { "_buffer", none() },
                // (the write_through argument has no effect here since this implementation always writes through; the argument is present only so
                // that the signature can match the signature of the C version)
                { "__init__", method(c, "__init__", { py_positional("buffer"), py_positional("encoding", none()), py_positional("errors", none()),
                    py_positional("newline", none()), py_positional("line_buffering", boolean(false)), py_positional("write_through", boolean(false)) },
                    [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        // (_io's checks of the arguments (Modules/_io/textio.c))
                        for (std::size_t i : { 2u, 3u, 4u })
                            if (!a[i].is_none() && !isinstance(a[i], builtin("str")))
                                py_raise("TypeError", std::string{ "TextIOWrapper() argument '" } + (i == 2u ? "encoding" : i == 3u ? "errors" : "newline") +
                                    "' must be str or None, not " + class_name(a[i]).str());
                        self.method("_check_newline", { a[4] });
                        py encoding = import_attribute("io", "text_encoding")(a[2]);
                        if (encoding == S("locale"))
                            encoding = self.method("_get_locale_encoding");
                        if (!isinstance(encoding, builtin("str")))
                            py_raise("ValueError", "invalid encoding: " + encoding.repr());
                        if (!import_attribute("codecs", "lookup")(encoding).attr("_is_text_encoding").truthy())
                            py_raise("LookupError", encoding.repr() + " is not a text encoding; use codecs.open() to handle arbitrary codecs");
                        py errors = a[3];
                        if (errors.is_none())
                            errors = S("strict");
                        else
                        {
                            if (!isinstance(errors, builtin("str")))
                                py_raise("ValueError", "invalid errors: " + errors.repr());
                            // (does open() check its 'errors' argument?)
                            py const sys{ fn_import("sys") };
                            if (hasattr(sys, "gettotalrefcount") || sys.attr("flags").attr("dev_mode").truthy())
                                import_attribute("codecs", "lookup_error")(errors);
                        }
                        self.set_attr("_buffer", a[1]);
                        self.set_attr("_decoded_chars", S(""));        // (buffer for text returned from decoder)
                        self.set_attr("_decoded_chars_used", py{ 0 }); // (offset into _decoded_chars for read())
                        self.set_attr("_snapshot", none());            // (info for reconstructing decoder state)
                        py const seekable = attached_buffer(self).method("seekable");
                        self.set_attr("_seekable", seekable);
                        self.set_attr("_telling", seekable);
                        self.set_attr("_has_read1", boolean(hasattr(attached_buffer(self), "read1")));
                        self.method("_configure", { encoding, errors, a[4], a[5], a[6] });
                        return none();
                    }) },
                { "_check_newline", method(c, "_check_newline", { py_positional("newline") }, [](std::vector<py> const& a)
                    {
                        if (!a[1].is_none() && !isinstance(a[1], builtin("str")))
                            py_raise("TypeError", "illegal newline type: " + type_of(a[1]).repr());
                        if (!py_tuple({ none(), S(""), S("\n"), S("\r"), S("\r\n") }).contains(a[1]))
                            py_raise("ValueError", "illegal newline value: " + a[1].str());    // (_io's message (_pyio's has its repr))
                        return none();
                    }) },
                { "_configure", method(c, "_configure", { py_positional("encoding", none()), py_positional("errors", none()), py_positional("newline", none()),
                    py_positional("line_buffering", boolean(false)), py_positional("write_through", boolean(false)) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py const& newline = a[3];
                        self.set_attr("_encoding", a[1]);
                        self.set_attr("_errors", a[2]);
                        self.set_attr("_encoder", none());
                        self.set_attr("_decoder", none());
                        self.set_attr("_b2cratio", py{ 0.0 });
                        self.set_attr("_readuniversal", boolean(!newline.truthy()));
                        self.set_attr("_readtranslate", boolean(newline.is_none()));
                        self.set_attr("_readnl", newline);
                        self.set_attr("_writetranslate", boolean(newline != S("")));
                        self.set_attr("_writenl", newline.truthy() ? newline : import_attribute("os", "linesep"));
                        self.set_attr("_line_buffering", a[4]);
                        self.set_attr("_write_through", a[5]);
                        // (don't write a BOM in the middle of a file)
                        if (self.attr("_seekable").truthy() && self.method("writable").truthy())
                        {
                            py const position = attached_buffer(self).method("tell");
                            if (position != py{ 0 })
                            {
                                try
                                {
                                    self.method("_get_encoder").method("setstate", { py{ 0 } });
                                }
                                catch (raised const&)
                                {
                                    // (sometimes the encoder doesn't exist)
                                    if (!caught({ "LookupError" }))
                                        throw;
                                }
                            }
                        }
                        return none();
                    }) },
                // (self._snapshot is either None, or a tuple (dec_flags, next_input) where dec_flags is the second (integer) item of the decoder state
                // and next_input is the chunk of input bytes that comes next after the snapshot point; we use this to reconstruct decoder states in
                // tell())
                // (_io's repr (Modules/_io/textio.c's textiowrapper_repr): its type's name, and its name and mode if it has them)
                { "__repr__", method(c, "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        std::string result = "<" + type_name_of(self);
                        for (char const* attribute : { "name", "mode" })
                            if (py const value = optional_attribute(self, attribute); !value.empty())
                                result += std::string{ " " } + attribute + "=" + value.repr();
                        return S(result + " encoding=" + self.attr("encoding").repr() + ">");
                    }) },
                { "encoding", property(c, "encoding", [](std::vector<py> const& a) { return a[0].attr("_encoding"); }) },
                { "errors", property(c, "errors", [](std::vector<py> const& a) { return a[0].attr("_errors"); }) },
                { "line_buffering", property(c, "line_buffering", [](std::vector<py> const& a) { return a[0].attr("_line_buffering"); }) },
                { "write_through", property(c, "write_through", [](std::vector<py> const& a) { return a[0].attr("_write_through"); }) },
                { "buffer", property(c, "buffer", [](std::vector<py> const& a) { return a[0].attr("_buffer"); }) },
                { "reconfigure", method(c, "reconfigure", { py_keyword("encoding", none()), py_keyword("errors", none()), py_keyword("newline", builtin("Ellipsis")),
                    py_keyword("line_buffering", none()), py_keyword("write_through", none()) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py encoding = a[1], errors = a[2], newline = a[3], lineBuffering = a[4], writeThrough = a[5];
                        py const ellipsis = builtin("Ellipsis");
                        // (_io's checks and messages (Modules/_io/textio.c): the encoding or newline can be set unless decoded text is pending (_pyio:
                        // after the first read))
                        for (auto const& [name, value] : { std::pair<char const*, py const*>{ "encoding", &encoding }, { "errors", &errors }, { "newline", &newline } })
                            if (!value->is_none() && !value->is(ellipsis) && !isinstance(*value, builtin("str")))
                                py_raise("TypeError", std::string{ "reconfigure() argument '" } + name + "' must be str or None, not " + class_name(*value).str());
                        if (self.attr("_decoded_chars").truthy() && (!encoding.is_none() || !errors.is_none() || !newline.is(ellipsis)))
                            raise_unsupported(S("It is not possible to set the encoding or newline of stream after the first read"));
                        if (errors.is_none())
                            errors = encoding.is_none() ? self.attr("_errors") : S("strict");
                        if (encoding.is_none())
                            encoding = self.attr("_encoding");
                        else
                        {
                            if (encoding == S("locale"))
                                encoding = self.method("_get_locale_encoding");
                        }
                        if (newline.is(ellipsis))
                            newline = self.attr("_readnl");
                        self.method("_check_newline", { newline });
                        if (lineBuffering.is_none())
                            lineBuffering = self.attr("line_buffering");
                        if (writeThrough.is_none())
                            writeThrough = self.attr("write_through");
                        self.method("flush");
                        self.method("_configure", { encoding, errors, newline, lineBuffering, writeThrough });
                        return none();
                    }) },
                { "seekable", method(c, "seekable", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed file.");
                        return a[0].attr("_seekable");
                    }) },
                { "readable", method(c, "readable", {}, [](std::vector<py> const& a) { return attached_buffer(a[0]).method("readable"); }) },
                { "writable", method(c, "writable", {}, [](std::vector<py> const& a) { return attached_buffer(a[0]).method("writable"); }) },
                { "flush", method(c, "flush", {}, [](std::vector<py> const& a)
                    {
                        attached_buffer(a[0]).method("flush");
                        a[0].set_attr("_telling", a[0].attr("_seekable"));
                        return none();
                    }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("closed").truthy())
                            try_finally([&]() { return a[0].method("flush"); }, [&]() { attached_buffer(a[0]).method("close"); });
                        return none();
                    }) },
                { "closed", property(c, "closed", [](std::vector<py> const& a) { return attached_buffer(a[0]).attr("closed"); }) },
                { "name", property(c, "name", [](std::vector<py> const& a) { return attached_buffer(a[0]).attr("name"); }) },
                { "fileno", method(c, "fileno", {}, [](std::vector<py> const& a) { return attached_buffer(a[0]).method("fileno"); }) },
                { "isatty", method(c, "isatty", {}, [](std::vector<py> const& a) { return attached_buffer(a[0]).method("isatty"); }) },
                { "write", method(c, "write", { py_positional("s") }, [setDecodedChars, getEncoder](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py s = a[1];
                        // (_io's checks and messages (Modules/_io/textio.c))
                        if (!isinstance(s, builtin("str")))
                            py_raise("TypeError", "write() argument must be str, not " + class_name(s).str());
                        if (self.attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed file.");
                        if (!self.method("writable").truthy())
                            raise_unsupported(S("not writable"));
                        py const length = len(s);
                        bool const haslf = (self.attr("_writetranslate").truthy() || self.attr("_line_buffering").truthy()) && s.contains(S("\n"));
                        if (haslf && self.attr("_writetranslate").truthy() && self.attr("_writenl") != S("\n"))
                            s = s.method("replace", { S("\n"), self.attr("_writenl") });
                        py const encoder = getEncoder(self);
                        // (XXX what if we were just reading?)
                        py const b = encoder.method("encode", { s });
                        attached_buffer(self).method("write", { b });
                        if (self.attr("_line_buffering").truthy() && (haslf || s.contains(S("\r"))))
                            self.method("flush");
                        if (!self.attr("_snapshot").is_none())
                        {
                            setDecodedChars(self, S(""));
                            self.set_attr("_snapshot", none());
                        }
                        if (self.attr("_decoder").truthy())
                            self.attr("_decoder").method("reset");
                        return length;
                    }) },
                { "_get_encoder", method(c, "_get_encoder", {}, [](std::vector<py> const& a)
                    {
                        py const makeEncoder = import_attribute("codecs", "getincrementalencoder")(a[0].attr("_encoding"));
                        a[0].set_attr("_encoder", makeEncoder(a[0].attr("_errors")));
                        return a[0].attr("_encoder");
                    }) },
                { "_get_decoder", method(c, "_get_decoder", {}, [](std::vector<py> const& a)
                    {
                        py const makeDecoder = import_attribute("codecs", "getincrementaldecoder")(a[0].attr("_encoding"));
                        py decoder = makeDecoder(a[0].attr("_errors"));
                        if (a[0].attr("_readuniversal").truthy())
                            decoder = cls("IncrementalNewlineDecoder")(decoder, a[0].attr("_readtranslate"));
                        a[0].set_attr("_decoder", decoder);
                        return decoder;
                    }) },
                // (the following three methods implement an ADT for _decoded_chars: text returned from the decoder is buffered here until the
                // client requests it by calling our read() or readline() method)
                { "_set_decoded_chars", method(c, "_set_decoded_chars", { py_positional("chars") }, [setDecodedChars](std::vector<py> const& a)
                    {
                        setDecodedChars(a[0], a[1]);
                        return none();
                    }) },
                { "_get_decoded_chars", method(c, "_get_decoded_chars", { py_positional("n", none()) }, [getDecodedChars](std::vector<py> const& a)
                    {
                        return getDecodedChars(a[0], a[1]);
                    }) },
                { "_get_locale_encoding", method(c, "_get_locale_encoding", {}, [](std::vector<py> const&)
                    {
                        try
                        {
                            return import_attribute("locale", "getencoding")();
                        }
                        catch (raised const&)
                        {
                            // (importing locale may fail if Python is being built)
                            if (!caught({ "ImportError" }))
                                throw;
                            return S("utf-8");
                        }
                    }) },
                { "_rewind_decoded_chars", method(c, "_rewind_decoded_chars", { py_positional("n") }, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("_decoded_chars_used") < a[1])
                            py_raise("AssertionError", "rewind decoded_chars out of bounds");
                        a[0].set_attr("_decoded_chars_used", a[0].attr("_decoded_chars_used") - a[1]);
                        return none();
                    }) },
                { "_read_chunk", method(c, "_read_chunk", {}, [setDecodedChars](std::vector<py> const& a)
                    {
                        // (the return value is True unless EOF was reached; the decoded string is placed in self._decoded_chars (replacing its
                        // previous value); the entire input chunk is sent to the decoder, though some of it may remain buffered in the decoder, yet to be
                        // converted)
                        py const& self = a[0];
                        if (self.attr("_decoder").is_none())
                            py_raise("ValueError", "no decoder");
                        py decBuffer, decFlags;
                        bool const telling = self.attr("_telling").truthy();
                        if (telling)
                        {
                            // (to prepare for tell(), we need to snapshot a point in the file where the decoder's input buffer is empty)
                            std::tie(decBuffer, decFlags) = unpack2(self.attr("_decoder").method("getstate"));
                            // (given this, we know there was a valid snapshot point len(dec_buffer) bytes ago with decoder state (b'', dec_flags))
                        }
                        // (read a chunk, decode it, and put the result in self._decoded_chars)
                        py const inputChunk = attached_buffer(self).method(self.attr("_has_read1").truthy() ? "read1" : "read", { self.attr("_CHUNK_SIZE") });
                        bool const eof = !inputChunk.truthy();
                        py const decodedChars = self.attr("_decoder").method("decode", { inputChunk, boolean(eof) });
                        setDecodedChars(self, decodedChars);
                        if (decodedChars.truthy())
                            self.set_attr("_b2cratio", len(inputChunk) / len(self.attr("_decoded_chars")));
                        else
                            self.set_attr("_b2cratio", py{ 0.0 });
                        if (telling)
                        {
                            // (at the snapshot point, len(dec_buffer) bytes before the read, the next input to be decoded is dec_buffer + input_chunk)
                            self.set_attr("_snapshot", py_tuple({ decFlags, decBuffer + inputChunk }));
                        }
                        return boolean(!eof);
                    }) },
                { "_pack_cookie", method(c, "_pack_cookie", { py_positional("position"), py_positional("dec_flags", py{ 0 }), py_positional("bytes_to_feed", py{ 0 }),
                    py_positional("need_eof", boolean(false)), py_positional("chars_to_skip", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        return pack_cookie(a[1], a[2], a[3], a[4].truthy(), a[5]);
                    }) },
                { "_unpack_cookie", method(c, "_unpack_cookie", { py_positional("bigint") }, [](std::vector<py> const& a)
                    {
                        py const unit = py{ 1 } << py{ 64 };
                        auto [rest, position] = unpack2(builtin("divmod")(a[1], unit));
                        py decFlags, bytesToFeed, needEof, charsToSkip;
                        std::tie(rest, decFlags) = unpack2(builtin("divmod")(rest, unit));
                        std::tie(rest, bytesToFeed) = unpack2(builtin("divmod")(rest, unit));
                        std::tie(needEof, charsToSkip) = unpack2(builtin("divmod")(rest, unit));
                        return py_tuple({ position, decFlags, bytesToFeed, builtin("bool")(needEof), charsToSkip });
                    }) },
                { "tell", method(c, "tell", {}, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        if (self.attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed file.");     // (_io's check)
                        if (!self.attr("_seekable").truthy())
                            raise_unsupported(S("underlying stream is not seekable"));
                        if (!self.attr("_telling").truthy())
                            py_raise("OSError", "telling position disabled by next() call");
                        self.method("flush");
                        py position = attached_buffer(self).method("tell");
                        py const decoder = self.attr("_decoder");
                        if (decoder.is_none() || self.attr("_snapshot").is_none())
                        {
                            if (self.attr("_decoded_chars").truthy())
                            {
                                // (this should never happen)
                                py_raise("AssertionError", "pending decoded text");
                            }
                            return position;
                        }
                        // (skip backward to the snapshot point (see _read_chunk))
                        auto [decFlags, nextInput] = unpack2(self.attr("_snapshot"));
                        position = position - len(nextInput);
                        // (how many decoded characters have been used up since the snapshot?)
                        py charsToSkip = self.attr("_decoded_chars_used");
                        if (charsToSkip == py{ 0 })
                        {
                            // (we haven't moved from the snapshot point)
                            return pack_cookie(position, decFlags);
                        }
                        // (starting from the snapshot position, we will walk the decoder forward until it gives us enough decoded characters)
                        py const savedState = decoder.method("getstate");
                        return try_finally([&]() -> py
                        {
                            // (fast search for an acceptable start point, close to our current pos; rationale: calling decoder.decode() has a large
                            // overhead regardless of chunk size; we want the number of such calls to be O(1) in most situations (common decoders,
                            // sensible input); actually, it will be exactly 1 for fixed-size codecs (all 8-bit codecs, also UTF-16 and UTF-32))
                            py skipBytes = builtin("int")(self.attr("_b2cratio") * charsToSkip);
                            py skipBack{ 1 };
                            if (!(skipBytes <= len(nextInput)))
                                py_raise(builtin("AssertionError")());
                            bool broke = false;
                            while (skipBytes > py{ 0 })
                            {
                                decoder.method("setstate", { py_tuple({ B(""), decFlags }) });
                                // (decode up to temptative start point)
                                py const n = len(decoder.method("decode", { upto(nextInput, skipBytes) }));
                                if (n <= charsToSkip)
                                {
                                    auto const [b, d] = unpack2(decoder.method("getstate"));
                                    if (!b.truthy())
                                    {
                                        // (before pos and no bytes buffered in decoder => OK)
                                        decFlags = d;
                                        charsToSkip = charsToSkip - n;
                                        broke = true;
                                        break;
                                    }
                                    // (skip back by buffered amount and reset heuristic)
                                    skipBytes = skipBytes - len(b);
                                    skipBack = py{ 1 };
                                }
                                else
                                {
                                    // (we're too far ahead, skip back a bit)
                                    skipBytes = skipBytes - skipBack;
                                    skipBack = skipBack * py{ 2 };
                                }
                            }
                            if (!broke)
                            {
                                skipBytes = py{ 0 };
                                decoder.method("setstate", { py_tuple({ B(""), decFlags }) });
                            }
                            // (note our initial start point)
                            py startPos = position + skipBytes;
                            py startFlags = decFlags;
                            if (charsToSkip == py{ 0 })
                            {
                                // (we haven't moved from the start point)
                                return pack_cookie(startPos, startFlags);
                            }
                            // (feed the decoder one byte at a time; as we go, note the nearest "safe start point" before the current location (a
                            // point where the decoder has nothing buffered, so seek() can safely start from there and advance to this location))
                            py bytesFed{ 0 };
                            bool needEof = false;
                            // (chars decoded since `start_pos`)
                            py charsDecoded{ 0 };
                            long long const end = nextInput.size();
                            bool ended = false;
                            for (long long i = skipBytes.as_index(); i < end; ++i)
                            {
                                bytesFed = bytesFed + py{ 1 };
                                charsDecoded = charsDecoded + len(decoder.method("decode", { slice_of(nextInput, py{ i }, py{ i + 1 }) }));
                                py decBuffer;
                                std::tie(decBuffer, decFlags) = unpack2(decoder.method("getstate"));
                                if (!decBuffer.truthy() && charsDecoded <= charsToSkip)
                                {
                                    // (decoder buffer is empty, so this is a safe start point)
                                    startPos = startPos + bytesFed;
                                    charsToSkip = charsToSkip - charsDecoded;
                                    startFlags = decFlags;
                                    bytesFed = py{ 0 };
                                    charsDecoded = py{ 0 };
                                }
                                if (charsDecoded >= charsToSkip)
                                {
                                    ended = true;
                                    break;
                                }
                            }
                            if (!ended)
                            {
                                // (we didn't get enough decoded data; signal EOF to get more)
                                charsDecoded = charsDecoded + len(decoder.method("decode", { B("") }, { { "final", boolean(true) } }));
                                needEof = true;
                                if (charsDecoded < charsToSkip)
                                    py_raise("OSError", "can't reconstruct logical file position");
                            }
                            // (the returned cookie corresponds to the last safe start point)
                            return pack_cookie(startPos, startFlags, bytesFed, needEof, charsToSkip);
                        }, [&]() { decoder.method("setstate", { savedState }); });
                    }) },
                { "truncate", method(c, "truncate", { py_positional("pos", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].method("flush");
                        py const pos = a[1].is_none() ? a[0].method("tell") : a[1];
                        return attached_buffer(a[0]).method("truncate", { pos });
                    }) },
                { "detach", method(c, "detach", {}, [](std::vector<py> const& a)
                    {
                        attached_buffer(a[0]); // (_io's check (Modules/_io/textio.c's CHECK_ATTACHED))
                        a[0].method("flush");
                        py const buffer = a[0].attr("_buffer");
                        a[0].set_attr("_buffer", none());
                        return buffer;
                    }) },
                { "seek", method(c, "seek", { py_positional("cookie"), py_positional("whence", py{ 0 }) }, [setDecodedChars, getDecoder, getEncoder](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py cookie = a[1];
                        py whence = a[2];
                        // (reset the encoder (merely useful for proper BOM handling))
                        auto const resetEncoder = [&](py const& aPosition)
                        {
                            py encoder;
                            try
                            {
                                encoder = getEncoder(self);
                            }
                            catch (raised const&)
                            {
                                // (sometimes the encoder doesn't exist)
                                if (!caught({ "LookupError" }))
                                    throw;
                                return;
                            }
                            if (aPosition != py{ 0 })
                                encoder.method("setstate", { py{ 0 } });
                            else
                                encoder.method("reset");
                        };
                        if (self.attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed file.");     // (_io's message (_pyio's is "tell on closed file"))
                        if (!self.attr("_seekable").truthy())
                            raise_unsupported(S("underlying stream is not seekable"));
                        if (whence == py{ 1 })
                        {
                            if (cookie != py{ 0 })
                                raise_unsupported(S("can't do nonzero cur-relative seeks"));
                            // (seeking to the current position should attempt to sync the underlying buffer with the current position)
                            whence = py{ 0 };
                            cookie = self.method("tell");
                        }
                        else if (whence == py{ 2 })
                        {
                            if (cookie != py{ 0 })
                                raise_unsupported(S("can't do nonzero end-relative seeks"));
                            self.method("flush");
                            py const position = attached_buffer(self).method("seek", { py{ 0 }, whence });
                            setDecodedChars(self, S(""));
                            self.set_attr("_snapshot", none());
                            if (self.attr("_decoder").truthy())
                                self.attr("_decoder").method("reset");
                            resetEncoder(position);
                            return position;
                        }
                        if (whence != py{ 0 })
                            py_raise("ValueError", "invalid whence (" + whence.repr() + ", should be 0, 1 or 2)");   // (_io's message)
                        if (cookie < py{ 0 })
                            py_raise("ValueError", "negative seek position " + cookie.repr());
                        self.method("flush");
                        // (the strategy of seek() is to go back to the safe start point and replay the effect of read(chars_to_skip) from there)
                        auto const cookieParts = self.method("_unpack_cookie", { cookie }).items();
                        py const& startPos = cookieParts[0];
                        py const& decFlags = cookieParts[1];
                        py const& bytesToFeed = cookieParts[2];
                        py const& needEof = cookieParts[3];
                        py const& charsToSkip = cookieParts[4];
                        // (seek back to the safe start point)
                        attached_buffer(self).method("seek", { startPos });
                        setDecodedChars(self, S(""));
                        self.set_attr("_snapshot", none());
                        // (restore the decoder to its state from the safe start point)
                        if (cookie == py{ 0 } && self.attr("_decoder").truthy())
                            self.attr("_decoder").method("reset");
                        else if (self.attr("_decoder").truthy() || decFlags.truthy() || charsToSkip.truthy())
                        {
                            self.set_attr("_decoder", getDecoder(self));
                            self.attr("_decoder").method("setstate", { py_tuple({ B(""), decFlags }) });
                            self.set_attr("_snapshot", py_tuple({ decFlags, B("") }));
                        }
                        if (charsToSkip.truthy())
                        {
                            // (just like _read_chunk, feed the decoder and save a snapshot)
                            py const inputChunk = attached_buffer(self).method("read", { bytesToFeed });
                            setDecodedChars(self, self.attr("_decoder").method("decode", { inputChunk, needEof }));
                            self.set_attr("_snapshot", py_tuple({ decFlags, inputChunk }));
                            // (skip chars_to_skip of the decoded characters)
                            if (len(self.attr("_decoded_chars")) < charsToSkip)
                                py_raise("OSError", "can't restore logical file position");
                            self.set_attr("_decoded_chars_used", charsToSkip);
                        }
                        resetEncoder(cookie);
                        return cookie;
                    }) },
                { "read", method(c, "read", { py_positional("size", none()) }, [getDecodedChars, setDecodedChars, getDecoder](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        // (_io's checks and messages (Modules/_io/textio.c))
                        py const size{ size_argument(a[1]) };
                        if (self.attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed file.");
                        if (!self.method("readable").truthy())
                            raise_unsupported(S("not readable"));
                        py const decoder = getDecoder(self);
                        if (size < py{ 0 })
                        {
                            // (read everything)
                            py const result = getDecodedChars(self) + decoder.method("decode", { attached_buffer(self).method("read") }, { { "final", boolean(true) } });
                            if (!self.attr("_snapshot").is_none())
                            {
                                setDecodedChars(self, S(""));
                                self.set_attr("_snapshot", none());
                            }
                            return result;
                        }
                        // (keep reading chunks until we have size characters to return)
                        bool eof = false;
                        py result = getDecodedChars(self, size);
                        while (len(result) < size && !eof)
                        {
                            eof = !self.method("_read_chunk").truthy();
                            result = result + getDecodedChars(self, size - len(result));
                        }
                        return result;
                    }) },
                { "__next__", method(c, "__next__", {}, [readline](std::vector<py> const& a)
                    {
                        a[0].set_attr("_telling", boolean(false));
                        // (TextIOWrapper's own readline, unless a subclass's (Modules/_io/textio.c's textiowrapper_iternext))
                        py const line = type_of(a[0]).is(cls("TextIOWrapper")) ? readline(a[0], py{ -1 }) : a[0].method("readline");
                        if (!line.truthy())
                        {
                            a[0].set_attr("_snapshot", none());
                            a[0].set_attr("_telling", a[0].attr("_seekable"));
                            py_raise(builtin("StopIteration"));
                        }
                        return line;
                    }) },
                { "readline", method(c, "readline", { py_positional("size", py{ -1 }) }, [readline](std::vector<py> const& a) { return readline(a[0], a[1]); }) },
                { "newlines", property(c, "newlines", [](std::vector<py> const& a)
                    {
                        attached_buffer(a[0]);
                        // (_io's: None if its decoder has none (Modules/_io/textio.c's textiowrapper_newlines_get))
                        py const decoder = a[0].attr("_decoder");
                        return decoder.truthy() ? getattr_or_none(decoder, "newlines") : none();
                    }) },
                { "_dealloc_warn", method(c, "_dealloc_warn", { py_positional("source") }, [](std::vector<py> const& a)
                    {
                        py const deallocWarn = getattr_or_none(a[0].attr("buffer"), "_dealloc_warn");
                        if (deallocWarn.truthy())
                            deallocWarn(a[1]);
                        return none();
                    }) }
            });
        }
    }

    // (the io module's classes (io_module (io.cpp) adds them to the module's attributes): false if they could not be made (an exception is
    // pending))
    bool pyio_attributes(std::vector<std::pair<std::string, object*>>& aAttributes)
    {
        ref const result{ py_guarded([&]()
        {
            auto& s = state();
            if (!s.made)
            {
                s.unsupportedOperation = py{ exception_class("io.UnsupportedOperation") };
                // (hardwired values)
                s.validSeekFlags = builtin("set")(py_list({ py{ 0 }, py{ 1 }, py{ 2 } }));
                py const os{ fn_import("os") };
                if (hasattr(os, "SEEK_HOLE"))
                {
                    s.validSeekFlags.method("add", { os.attr("SEEK_HOLE") });
                    s.validSeekFlags.method("add", { os.attr("SEEK_DATA") });
                }
                make_iobase();
                make_raw_iobase();
                make_fileio();
                make_buffered_iobase();
                make_buffered_io_mixin();
                make_buffered_reader();
                make_buffered_writer();
                make_buffered_rw_pair();
                make_buffered_random();
                make_text_iobase();
                make_incremental_newline_decoder();
                make_text_io_wrapper();
                // (the runtime's files are registered as these classes' virtual subclasses (as io.py registers _io's classes))
                cls("BufferedReader").method("register", { py{ type_object_for("BufferedReader", "_io.BufferedReader") } });
                cls("BufferedWriter").method("register", { py{ type_object_for("BufferedWriter", "_io.BufferedWriter") } });
                cls("BufferedRandom").method("register", { py{ type_object_for("BufferedRandom", "_io.BufferedRandom") } });
                cls("TextIOWrapper").method("register", { py{ type_object_for("TextIOWrapper", "_io.TextIOWrapper") } });
                cls("BufferedIOBase").method("register", { py{ type_object_for("BytesIO", "_io.BytesIO") } });
                cls("TextIOBase").method("register", { py{ type_object_for("StringIO", "_io.StringIO") } });
                s.made = true;
            }
            for (char const* name : { "IOBase", "RawIOBase", "FileIO", "BufferedIOBase", "TextIOBase", "BufferedReader", "BufferedWriter", "BufferedRWPair",
                "BufferedRandom", "TextIOWrapper", "IncrementalNewlineDecoder" })
                aAttributes.emplace_back(name, cls(name).reference());
            aAttributes.emplace_back("BlockingIOError", builtin("BlockingIOError").release());
            aAttributes.emplace_back("abc", py{ fn_import("abc") }.release());
            // (open_code(path): the file opened to read its bytes (Modules/_io/_iomodule.c's _io_open_code_impl: without hooks))
            aAttributes.emplace_back("open_code", make_native_function("open_code", "open_code", "Opens the provided file with the intent to import the contents.\n\n"
                "This may perform extra validation beyond open(), but is otherwise\ninterchangeable with calling open(path, 'rb').", "_io",
                [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("open_code", { py_positional("path") }, aArgs, aKwargs);
                    if (!isinstance(a[0], builtin("str")))
                        py_raise("TypeError", "open_code() argument 'path' must be str, not " + class_name(a[0]).str());
                    return builtin("open")(a[0], S("rb"));
                }).release());
            return py::none();
        }) };
        return result.get() != nullptr;
    }
}
