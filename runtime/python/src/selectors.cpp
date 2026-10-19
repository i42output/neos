/*
  selectors.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/selectors.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF).
  Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to its object model (see
  runtime/python/NOTICE).
*/

// The module selectors (CPython 3.13's Lib/selectors.py): SelectorKey, BaseSelector and its implementations over the module select
// (SelectSelector, PollSelector, EpollSelector) and DefaultSelector; select's devpoll and kqueue are not implemented, so neither are
// DevpollSelector and KqueueSelector. Its classes are made (by type, or abc.ABCMeta) of closures, so a program's subclasses override their
// methods.

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "selectors";

        constexpr long long EVENT_READ = 1 << 0;
        constexpr long long EVENT_WRITE = 1 << 1;

        py S(char const* aText) { return py{ aText }; }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }

        // (the module's objects: made once)
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;
            py selectorKey;
            py fileobjToFd;
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py cls(char const* aName) { return state().classes.at(aName); }
        py select_attr(char const* aName) { return import_attribute("select", aName); }

        // (a closure as a function of the module)
        py function(std::string const& aName, std::string const& aQualname, native_body aBody, char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, std::move(aBody));
        }

        using body_type = std::function<py(std::vector<py> const&)>;

        // (a function of its parameters (aVarArgs: *args, appended))
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            char const* aDoc = "")
        {
            return function(aName, aQualname, [aQualname, aParameters, aBody, aVarArgs](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                return aBody(a);
            }, aDoc);
        }
        // (a method: its parameters after self (aClass: its class's qualified name))
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false, char const* aDoc = "")
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            return bound_function(aName, std::string{ aClass } + "." + aName, std::move(aParameters), std::move(aBody), aVarArgs, aDoc);
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: aMetaclass(aName, aBases, namespace))
        py make_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, char const* aDoc, py aMetaclass = {})
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S(sModule));
            ns.set_item(S("__qualname__"), S(aName));
            ns.set_item(S("__doc__"), aDoc != nullptr ? S(aDoc) : none());
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

        // (super(aClass, self))
        py super_of(char const* aClass, py const& aSelf) { return builtin("super")(cls(aClass), aSelf); }

        // (an exception with no context shown (raise ... from None))
        [[noreturn]] void raise_from_none(char const* aClass, std::string const& aMessage)
        {
            py const e = builtin(aClass)(py{ aMessage });
            e.set_attr("__suppress_context__", boolean(true));
            py_raise(e);
        }

        // (the pending exception, if it is an instance of aClass: handled (true); otherwise it remains raised (false))
        bool caught_class(char const* aClass) { return caught({ aClass }); }

        // (a file object (or descriptor) as a file descriptor (_fileobj_to_fd))
        py fileobj_to_fd(py const& aFileobj)
        {
            py fd;
            if (isinstance(aFileobj, builtin("int")))
                fd = aFileobj;
            else
            {
                try
                {
                    fd = builtin("int")(aFileobj.method("fileno"));
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError", "TypeError", "ValueError" }))
                        throw;
                    raise_from_none("ValueError", "Invalid file object: " + aFileobj.repr());
                }
            }
            if (fd < py{ 0 })
                py_raise("ValueError", "Invalid file descriptor: " + fd.str());
            return fd;
        }

        // (events & EVENT_READ and aRead) | (events & EVENT_WRITE and aWrite): a poller's event mask)
        py poller_events(py const& aEvents, py const& aRead, py const& aWrite)
        {
            py const r = (aEvents & py{ EVENT_READ }).truthy() ? aRead : (aEvents & py{ EVENT_READ });
            py const w = (aEvents & py{ EVENT_WRITE }).truthy() ? aWrite : (aEvents & py{ EVENT_WRITE });
            return r | w;
        }

        // --- _SelectorMapping

        void make_selector_mapping()
        {
            char const* const c = "_SelectorMapping";
            make_class(c, { import_attribute("collections.abc", "Mapping") }, {
                { "__init__", method(c, "__init__", { py_positional("selector") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_selector", a[1]);
                        return none();
                    }) },
                { "__len__", method(c, "__len__", {}, [](std::vector<py> const& a)
                    {
                        return py{ static_cast<long long>(a[0].attr("_selector").attr("_fd_to_key").size()) };
                    }) },
                { "get", method(c, "get", { py_positional("fileobj"), py_positional("default", none()) }, [](std::vector<py> const& a)
                    {
                        py const fd = a[0].attr("_selector").method("_fileobj_lookup", { a[1] });
                        return a[0].attr("_selector").attr("_fd_to_key").method("get", { fd, a[2] });
                    }) },
                { "__getitem__", method(c, "__getitem__", { py_positional("fileobj") }, [](std::vector<py> const& a)
                    {
                        py const fd = a[0].attr("_selector").method("_fileobj_lookup", { a[1] });
                        py const key = a[0].attr("_selector").attr("_fd_to_key").method("get", { fd });
                        if (key.is_none())
                            py_raise(builtin("KeyError")(py{ a[1].repr() + " is not registered" }));
                        return key;
                    }) },
                { "__iter__", method(c, "__iter__", {}, [](std::vector<py> const& a)
                    {
                        return builtin("iter")(a[0].attr("_selector").attr("_fd_to_key"));
                    }) }
            }, "Mapping of file objects to selector keys.");
        }

        // --- BaseSelector

        void make_base_selector()
        {
            char const* const c = "BaseSelector";
            py const abstractmethod = import_attribute("abc", "abstractmethod");
            auto const notImplemented = [](std::vector<py> const&) -> py { py_raise(builtin("NotImplementedError")); };
            make_class(c, {}, {
                { "register", abstractmethod(method(c, "register", { py_positional("fileobj"), py_positional("events"), py_positional("data", none()) }, notImplemented,
                    false, "Register a file object.\n\n        Parameters:\n        fileobj -- file object or file descriptor\n"
                    "        events  -- events to monitor (bitwise mask of EVENT_READ|EVENT_WRITE)\n        data    -- attached data\n\n        Returns:\n"
                    "        SelectorKey instance\n\n        Raises:\n        ValueError if events is invalid\n        KeyError if fileobj is already registered\n"
                    "        OSError if fileobj is closed or otherwise is unacceptable to\n                the underlying system call (if a system call is made)\n\n"
                    "        Note:\n        OSError may or may not be raised\n        ")) },
                { "unregister", abstractmethod(method(c, "unregister", { py_positional("fileobj") }, notImplemented, false,
                    "Unregister a file object.\n\n        Parameters:\n        fileobj -- file object or file descriptor\n\n        Returns:\n"
                    "        SelectorKey instance\n\n        Raises:\n        KeyError if fileobj is not registered\n\n        Note:\n"
                    "        If fileobj is registered but has since been closed this does\n        *not* raise OSError (even if the wrapped syscall does)\n        ")) },
                { "modify", method(c, "modify", { py_positional("fileobj"), py_positional("events"), py_positional("data", none()) }, [](std::vector<py> const& a)
                    {
                        a[0].method("unregister", { a[1] });
                        return a[0].method("register", { a[1], a[2], a[3] });
                    }, false, "Change a registered file object monitored events or attached data.\n\n        Parameters:\n"
                    "        fileobj -- file object or file descriptor\n        events  -- events to monitor (bitwise mask of EVENT_READ|EVENT_WRITE)\n"
                    "        data    -- attached data\n\n        Returns:\n        SelectorKey instance\n\n        Raises:\n"
                    "        Anything that unregister() or register() raises\n        ") },
                { "select", abstractmethod(method(c, "select", { py_positional("timeout", none()) }, notImplemented, false,
                    "Perform the actual selection, until some monitored file objects are\n        ready or a timeout expires.\n\n        Parameters:\n"
                    "        timeout -- if timeout > 0, this specifies the maximum wait time, in\n                   seconds\n"
                    "                   if timeout <= 0, the select() call won't block, and will\n                   report the currently ready file objects\n"
                    "                   if timeout is None, select() will block until a monitored\n                   file object becomes ready\n\n"
                    "        Returns:\n        list of (key, events) for ready file objects\n        `events` is a bitwise mask of EVENT_READ|EVENT_WRITE\n        ")) },
                { "close", method(c, "close", {}, [](std::vector<py> const&) { return none(); }, false,
                    "Close the selector.\n\n        This must be called to make sure that any underlying resource is freed.\n        ") },
                { "get_key", method(c, "get_key", { py_positional("fileobj") }, [](std::vector<py> const& a)
                    {
                        py const mapping = a[0].method("get_map");
                        if (mapping.is_none())
                            py_raise("RuntimeError", "Selector is closed");
                        try
                        {
                            return mapping[a[1]];
                        }
                        catch (raised const&)
                        {
                            if (!caught_class("KeyError"))
                                throw;
                            raise_from_none("KeyError", a[1].repr() + " is not registered");
                        }
                    }, false, "Return the key associated to a registered file object.\n\n        Returns:\n        SelectorKey for this file object\n        ") },
                { "get_map", abstractmethod(method(c, "get_map", {}, notImplemented, false, "Return a mapping of file objects to selector keys.")) },
                { "__enter__", method(c, "__enter__", {}, [](std::vector<py> const& a) { return a[0]; }) },
                { "__exit__", method(c, "__exit__", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("close");
                        return none();
                    }, true) }
            }, "Selector abstract base class.\n\n    A selector supports registering file objects to be monitored for specific\n    I/O events.\n\n"
               "    A file object is a file descriptor or any object with a `fileno()` method.\n    An arbitrary object can be attached to the file object, which can be used\n"
               "    for example to store context information, a callback, etc.\n\n    A selector can use various implementations (select(), poll(), epoll()...)\n"
               "    depending on the platform. The default `Selector` class uses the most\n    efficient implementation on the current platform.\n    ",
               import_attribute("abc", "ABCMeta"));
        }

        // --- _BaseSelectorImpl

        void make_base_selector_impl()
        {
            char const* const c = "_BaseSelectorImpl";
            make_class(c, { cls("BaseSelector") }, {
                { "__init__", method(c, "__init__", {}, [](std::vector<py> const& a)
                    {
                        // (this maps file descriptors to keys)
                        a[0].set_attr("_fd_to_key", py_dict());
                        // (read-only mapping returned by get_map())
                        a[0].set_attr("_map", cls("_SelectorMapping")(a[0]));
                        return none();
                    }) },
                { "_fileobj_lookup", method(c, "_fileobj_lookup", { py_positional("fileobj") }, [](std::vector<py> const& a)
                    {
                        try
                        {
                            return fileobj_to_fd(a[1]);
                        }
                        catch (raised const&)
                        {
                            py const e = py_take_exception();
                            if (!isinstance(e, builtin("ValueError")))
                                py_reraise(e);
                            // (do an exhaustive search)
                            for (auto const& key : a[0].attr("_fd_to_key").method("values").items())
                                if (key.attr("fileobj").is(a[1]))
                                    return key.attr("fd");
                            // (raise ValueError after all)
                            py_reraise(e);
                        }
                    }, false, "Return a file descriptor from a file object.\n\n        This wraps _fileobj_to_fd() to do an exhaustive search in case\n"
                    "        the object is invalid but we still have it in our map.  This\n        is used by unregister() so we can unregister an object that\n"
                    "        was previously registered even if it is closed.  It is also\n        used by _SelectorMapping.\n        ") },
                { "register", method(c, "register", { py_positional("fileobj"), py_positional("events"), py_positional("data", none()) }, [](std::vector<py> const& a)
                    {
                        py const& events = a[2];
                        if (!events.truthy() || (events & py{ ~(EVENT_READ | EVENT_WRITE) }).truthy())
                            py_raise("ValueError", "Invalid events: " + events.repr());
                        py const key = state().selectorKey(a[1], a[0].method("_fileobj_lookup", { a[1] }), events, a[3]);
                        py const fdToKey = a[0].attr("_fd_to_key");
                        if (fdToKey.contains(key.attr("fd")))
                            py_raise(builtin("KeyError")(py{ a[1].repr() + " (FD " + key.attr("fd").str() + ") is already registered" }));
                        fdToKey.set_item(key.attr("fd"), key);
                        return key;
                    }) },
                { "unregister", method(c, "unregister", { py_positional("fileobj") }, [](std::vector<py> const& a)
                    {
                        try
                        {
                            return a[0].attr("_fd_to_key").method("pop", { a[0].method("_fileobj_lookup", { a[1] }) });
                        }
                        catch (raised const&)
                        {
                            if (!caught_class("KeyError"))
                                throw;
                            raise_from_none("KeyError", a[1].repr() + " is not registered");
                        }
                    }) },
                { "modify", method(c, "modify", { py_positional("fileobj"), py_positional("events"), py_positional("data", none()) }, [](std::vector<py> const& a)
                    {
                        py key;
                        try
                        {
                            key = a[0].attr("_fd_to_key")[a[0].method("_fileobj_lookup", { a[1] })];
                        }
                        catch (raised const&)
                        {
                            if (!caught_class("KeyError"))
                                throw;
                            raise_from_none("KeyError", a[1].repr() + " is not registered");
                        }
                        if (a[2] != key.attr("events"))
                        {
                            a[0].method("unregister", { a[1] });
                            key = a[0].method("register", { a[1], a[2], a[3] });
                        }
                        else if (a[3] != key.attr("data"))
                        {
                            // (use a shortcut to update the data)
                            key = key.method("_replace", {}, { { "data", a[3] } });
                            a[0].attr("_fd_to_key").set_item(key.attr("fd"), key);
                        }
                        return key;
                    }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        a[0].attr("_fd_to_key").method("clear");
                        a[0].set_attr("_map", none());
                        return none();
                    }) },
                { "get_map", method(c, "get_map", {}, [](std::vector<py> const& a) { return a[0].attr("_map"); }) }
            }, "Base selector implementation.");
        }

        // --- SelectSelector

        void make_select_selector()
        {
            char const* const c = "SelectSelector";
            namespace_type ns = {
                { "__init__", method(c, "__init__", {}, [](std::vector<py> const& a)
                    {
                        super_of("SelectSelector", a[0]).method("__init__");
                        a[0].set_attr("_readers", builtin("set")());
                        a[0].set_attr("_writers", builtin("set")());
                        return none();
                    }) },
                { "register", method(c, "register", { py_positional("fileobj"), py_positional("events"), py_positional("data", none()) }, [](std::vector<py> const& a)
                    {
                        py const key = super_of("SelectSelector", a[0]).method("register", { a[1], a[2], a[3] });
                        if ((a[2] & py{ EVENT_READ }).truthy())
                            a[0].attr("_readers").method("add", { key.attr("fd") });
                        if ((a[2] & py{ EVENT_WRITE }).truthy())
                            a[0].attr("_writers").method("add", { key.attr("fd") });
                        return key;
                    }) },
                { "unregister", method(c, "unregister", { py_positional("fileobj") }, [](std::vector<py> const& a)
                    {
                        py const key = super_of("SelectSelector", a[0]).method("unregister", { a[1] });
                        a[0].attr("_readers").method("discard", { key.attr("fd") });
                        a[0].attr("_writers").method("discard", { key.attr("fd") });
                        return key;
                    }) },
#if defined(_WIN32)
                { "_select", method(c, "_select", { py_positional("r"), py_positional("w"), py_positional("_"), py_positional("timeout", none()) }, [](std::vector<py> const& a)
                    {
                        py const result = select_attr("select")(a[1], a[2], a[2], a[4]);
                        return py_tuple({ result[py{ 0 }], result[py{ 1 }] + result[py{ 2 }], py_list() });
                    }) },
#else
                { "_select", builtin("staticmethod")(select_attr("select")) },
#endif
                { "select", method(c, "select", { py_positional("timeout", none()) }, [](std::vector<py> const& a)
                    {
                        py const timeout = a[1].is_none() ? none() : builtin("max")(a[1], py{ 0 });
                        py const ready = py_list();
                        py result;
                        try
                        {
                            result = a[0].method("_select", { a[0].attr("_readers"), a[0].attr("_writers"), py_list(), timeout });
                        }
                        catch (raised const&)
                        {
                            if (!caught_class("InterruptedError"))
                                throw;
                            return ready;
                        }
                        py const r = builtin("frozenset")(result[py{ 0 }]);
                        py const w = builtin("frozenset")(result[py{ 1 }]);
                        py const rw = r | w;
                        py const fdToKey = a[0].attr("_fd_to_key");
                        for (auto const& fd : rw.items())
                        {
                            py const key = fdToKey.method("get", { fd });
                            if (key.truthy())
                            {
                                py const events = (r.contains(fd) ? py{ EVENT_READ } : py::boolean(false)) | (w.contains(fd) ? py{ EVENT_WRITE } : py::boolean(false));
                                ready.method("append", { py_tuple({ key, events & key.attr("events") }) });
                            }
                        }
                        return ready;
                    }) }
            };
            make_class(c, { cls("_BaseSelectorImpl") }, ns, "Select-based selector.");
        }

        // --- _PollLikeSelector, PollSelector and EpollSelector

        void make_poll_like_selector()
        {
            char const* const c = "_PollLikeSelector";
            make_class(c, { cls("_BaseSelectorImpl") }, {
                { "_selector_cls", none() },
                { "_EVENT_READ", none() },
                { "_EVENT_WRITE", none() },
                { "__init__", method(c, "__init__", {}, [](std::vector<py> const& a)
                    {
                        super_of("_PollLikeSelector", a[0]).method("__init__");
                        a[0].set_attr("_selector", a[0].attr("_selector_cls")());
                        return none();
                    }) },
                { "register", method(c, "register", { py_positional("fileobj"), py_positional("events"), py_positional("data", none()) }, [](std::vector<py> const& a)
                    {
                        py const key = super_of("_PollLikeSelector", a[0]).method("register", { a[1], a[2], a[3] });
                        py const pollerEvents = poller_events(a[2], a[0].attr("_EVENT_READ"), a[0].attr("_EVENT_WRITE"));
                        try
                        {
                            a[0].attr("_selector").method("register", { key.attr("fd"), pollerEvents });
                        }
                        catch (raised const&)
                        {
                            py const e = py_take_exception();
                            super_of("_PollLikeSelector", a[0]).method("unregister", { a[1] });
                            py_reraise(e);
                        }
                        return key;
                    }) },
                { "unregister", method(c, "unregister", { py_positional("fileobj") }, [](std::vector<py> const& a)
                    {
                        py const key = super_of("_PollLikeSelector", a[0]).method("unregister", { a[1] });
                        try
                        {
                            a[0].attr("_selector").method("unregister", { key.attr("fd") });
                        }
                        catch (raised const&)
                        {
                            // (this can happen if the FD was closed since it was registered)
                            if (!caught_class("OSError"))
                                throw;
                        }
                        return key;
                    }) },
                { "modify", method(c, "modify", { py_positional("fileobj"), py_positional("events"), py_positional("data", none()) }, [](std::vector<py> const& a)
                    {
                        py key;
                        try
                        {
                            key = a[0].attr("_fd_to_key")[a[0].method("_fileobj_lookup", { a[1] })];
                        }
                        catch (raised const&)
                        {
                            if (!caught_class("KeyError"))
                                throw;
                            raise_from_none("KeyError", a[1].repr() + " is not registered");
                        }
                        bool changed = false;
                        if (a[2] != key.attr("events"))
                        {
                            py const selectorEvents = poller_events(a[2], a[0].attr("_EVENT_READ"), a[0].attr("_EVENT_WRITE"));
                            try
                            {
                                a[0].attr("_selector").method("modify", { key.attr("fd"), selectorEvents });
                            }
                            catch (raised const&)
                            {
                                py const e = py_take_exception();
                                super_of("_PollLikeSelector", a[0]).method("unregister", { a[1] });
                                py_reraise(e);
                            }
                            changed = true;
                        }
                        if (a[3] != key.attr("data"))
                            changed = true;
                        if (changed)
                        {
                            key = key.method("_replace", {}, { { "events", a[2] }, { "data", a[3] } });
                            a[0].attr("_fd_to_key").set_item(key.attr("fd"), key);
                        }
                        return key;
                    }) },
                { "select", method(c, "select", { py_positional("timeout", none()) }, [](std::vector<py> const& a)
                    {
                        // (this is shared between poll() and epoll(); epoll() has a different signature and handling of timeout parameter)
                        py timeout = a[1];
                        if (timeout.is_none())
                            timeout = none();
                        else if (timeout <= py{ 0 })
                            timeout = py{ 0 };
                        else
                            // (poll() has a resolution of 1 millisecond, round away from zero to wait *at least* timeout seconds)
                            timeout = import_attribute("math", "ceil")(timeout * py{ 1e3 });
                        py const ready = py_list();
                        py fdEventList;
                        try
                        {
                            fdEventList = a[0].attr("_selector").method("poll", { timeout });
                        }
                        catch (raised const&)
                        {
                            if (!caught_class("InterruptedError"))
                                throw;
                            return ready;
                        }
                        py const fdToKey = a[0].attr("_fd_to_key");
                        py const notRead = py{ unary(NEOS_PY_INVERT, a[0].attr("_EVENT_READ").get()) };
                        py const notWrite = py{ unary(NEOS_PY_INVERT, a[0].attr("_EVENT_WRITE").get()) };
                        for (auto const& item : fdEventList.items())
                        {
                            py const fd = item[py{ 0 }];
                            py const event = item[py{ 1 }];
                            py const key = fdToKey.method("get", { fd });
                            if (key.truthy())
                            {
                                py const w = (event & notRead).truthy() ? py{ EVENT_WRITE } : (event & notRead);
                                py const r = (event & notWrite).truthy() ? py{ EVENT_READ } : (event & notWrite);
                                ready.method("append", { py_tuple({ key, (w | r) & key.attr("events") }) });
                            }
                        }
                        return ready;
                    }) }
            }, "Base class shared between poll, epoll and devpoll selectors.");
            if (builtin("hasattr")(py{ fn_import("select") }, S("poll")).truthy())
                make_class("PollSelector", { cls("_PollLikeSelector") }, {
                    // (a builtin function, which a class does not bind)
                    { "_selector_cls", builtin("staticmethod")(select_attr("poll")) },
                    { "_EVENT_READ", select_attr("POLLIN") },
                    { "_EVENT_WRITE", select_attr("POLLOUT") }
                }, "Poll-based selector.");
            if (builtin("hasattr")(py{ fn_import("select") }, S("epoll")).truthy())
            {
                char const* const e = "EpollSelector";
                make_class(e, { cls("_PollLikeSelector") }, {
                    { "_selector_cls", select_attr("epoll") },
                    { "_EVENT_READ", select_attr("EPOLLIN") },
                    { "_EVENT_WRITE", select_attr("EPOLLOUT") },
                    { "fileno", method(e, "fileno", {}, [](std::vector<py> const& a) { return a[0].attr("_selector").method("fileno"); }) },
                    { "select", method(e, "select", { py_positional("timeout", none()) }, [](std::vector<py> const& a)
                        {
                            py timeout = a[1];
                            if (timeout.is_none())
                                timeout = py{ -1 };
                            else if (timeout <= py{ 0 })
                                timeout = py{ 0 };
                            else
                                // (epoll_wait() has a resolution of 1 millisecond, round away from zero to wait *at least* timeout seconds)
                                timeout = import_attribute("math", "ceil")(timeout * py{ 1e3 }) * py{ 1e-3 };
                            // (epoll_wait() expects `maxevents` to be greater than zero; we want to make sure that `select()` can be called when no
                            // FD is registered)
                            auto const n = a[0].attr("_fd_to_key").size();
                            py const maxEv{ n != 0 ? n : 1LL };
                            py const ready = py_list();
                            py fdEventList;
                            try
                            {
                                fdEventList = a[0].attr("_selector").method("poll", { timeout, maxEv });
                            }
                            catch (raised const&)
                            {
                                if (!caught_class("InterruptedError"))
                                    throw;
                                return ready;
                            }
                            py const fdToKey = a[0].attr("_fd_to_key");
                            py const notEpollIn = import_attribute("selectors", "_NOT_EPOLLIN");
                            py const notEpollOut = import_attribute("selectors", "_NOT_EPOLLOUT");
                            for (auto const& item : fdEventList.items())
                            {
                                py const fd = item[py{ 0 }];
                                py const event = item[py{ 1 }];
                                py const key = fdToKey.method("get", { fd });
                                if (key.truthy())
                                {
                                    py const w = (event & notEpollIn).truthy() ? py{ EVENT_WRITE } : (event & notEpollIn);
                                    py const r = (event & notEpollOut).truthy() ? py{ EVENT_READ } : (event & notEpollOut);
                                    ready.method("append", { py_tuple({ key, (w | r) & key.attr("events") }) });
                                }
                            }
                            return ready;
                        }) },
                    { "close", method(e, "close", {}, [](std::vector<py> const& a)
                        {
                            a[0].attr("_selector").method("close");
                            super_of("EpollSelector", a[0]).method("close");
                            return none();
                        }) }
                }, "Epoll-based selector.");
            }
        }

        // (whether the select module's method can be used (_can_use): it exists, and the OS and kernel support it)
        bool can_use(char const* aMethod)
        {
            py const selector = builtin("getattr")(py{ fn_import("select") }, S(aMethod), none());
            if (selector.is_none())
                // (select module does not implement method)
                return false;
            // (check if the OS and kernel actually support the method; call may fail with OSError: [Errno 38] Function not implemented)
            try
            {
                py const selectorObj = selector();
                if (std::string_view{ aMethod } == "poll")
                    // (check that poll actually works)
                    selectorObj.method("poll", { py{ 0 } });
                else
                    // (close epoll, kqueue, and devpoll fd)
                    selectorObj.method("close");
                return true;
            }
            catch (raised const&)
            {
                if (!caught_class("OSError"))
                    throw;
                return false;
            }
        }

        bool selectors_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (!s.made)
                {
                    s.made = true;
                    py const namedtuple = import_attribute("collections", "namedtuple");
                    s.selectorKey = namedtuple(S("SelectorKey"), py_list({ S("fileobj"), S("fd"), S("events"), S("data") }));
                    s.selectorKey.set_attr("__module__", S(sModule));
                    s.selectorKey.set_attr("__doc__", S("SelectorKey(fileobj, fd, events, data)\n\n    Object used to associate a file object to its backing\n"
                        "    file descriptor, selected event mask, and attached data.\n"));
                    s.selectorKey.attr("fileobj").set_attr("__doc__", S("File object registered."));
                    s.selectorKey.attr("fd").set_attr("__doc__", S("Underlying file descriptor."));
                    s.selectorKey.attr("events").set_attr("__doc__", S("Events that must be waited for on this file object."));
                    s.selectorKey.attr("data").set_attr("__doc__", S("Optional opaque data associated to this file object.\n"
                        "For example, this could be used to store a per-client session ID."));
                    s.fileobjToFd = bound_function("_fileobj_to_fd", "_fileobj_to_fd", { py_positional("fileobj") }, [](std::vector<py> const& a)
                    {
                        return fileobj_to_fd(a[0]);
                    }, false, "Return a file descriptor from a file object.\n\n    Parameters:\n    fileobj -- file object or file descriptor\n\n    Returns:\n"
                        "    corresponding file descriptor\n\n    Raises:\n    ValueError if the object is invalid\n    ");
                    make_selector_mapping();
                    make_base_selector();
                    make_base_selector_impl();
                    make_select_selector();
                    make_poll_like_selector();
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S("Selectors module.\n\nThis module allows high-level and efficient I/O multiplexing, built upon the\n`select` module primitives.\n"));
                add("ABCMeta", import_attribute("abc", "ABCMeta"));
                add("abstractmethod", import_attribute("abc", "abstractmethod"));
                add("namedtuple", import_attribute("collections", "namedtuple"));
                add("Mapping", import_attribute("collections.abc", "Mapping"));
                add("math", py{ fn_import("math") });
                add("select", py{ fn_import("select") });
                add("sys", py{ fn_import("sys") });
                add("EVENT_READ", py{ EVENT_READ });
                add("EVENT_WRITE", py{ EVENT_WRITE });
                add("_fileobj_to_fd", s.fileobjToFd);
                add("SelectorKey", s.selectorKey);
                for (auto const& [name, c] : s.classes)
                    add(name.c_str(), c);
                py const select = py{ fn_import("select") };
                if (builtin("hasattr")(select, S("epoll")).truthy())
                {
                    add("_NOT_EPOLLIN", py{ unary(NEOS_PY_INVERT, select_attr("EPOLLIN").get()) });
                    add("_NOT_EPOLLOUT", py{ unary(NEOS_PY_INVERT, select_attr("EPOLLOUT").get()) });
                }
                add("_can_use", bound_function("_can_use", "_can_use", { py_positional("method") }, [](std::vector<py> const& a)
                {
                    return boolean(can_use(a[0].str().c_str()));
                }, false, "Check if we can use the selector depending upon the\n    operating system. "));
                // (choose the best implementation, roughly: epoll|kqueue|devpoll > poll > select; select() also can't accept a FD > FD_SETSIZE
                // (usually around 1024))
                if (can_use("epoll"))
                    add("DefaultSelector", cls("EpollSelector"));
                else if (can_use("poll"))
                    add("DefaultSelector", cls("PollSelector"));
                else
                    add("DefaultSelector", cls("SelectSelector"));
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sSelectors = register_module("selectors", selectors_module);
    }
}
