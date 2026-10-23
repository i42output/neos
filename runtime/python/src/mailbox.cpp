/*
  mailbox.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/mailbox.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module mailbox (CPython 3.13's Lib/mailbox.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "mailbox_docs.hpp"

namespace neos::python_runtime::detail::mailbox_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
        using pyclass::BigI;
        using pyclass::F;
        using pyclass::B;
        using pyclass::Mod;
        using pyclass::none;
        using pyclass::boolean;
        using pyclass::M;
        using pyclass::has_attr;
        using pyclass::get_attr;
        using pyclass::callable;
        using pyclass::module_writer;
        using pyclass::namespace_type;
        using pyclass::try_except;
        using pyclass::try_finally;
        using pyclass::with_;
        using pyclass::unpack2;
        using pyclass::format;
        using pyclass::handler_scope;
        using pyclass::matches;
        using pyclass::iterate;
        using pyclass::unpack;
        using pyclass::call_ex;
        using pyclass::dict_of;
        using pyclass::set_of;
        using pyclass::inplace;
        using pyclass::del_attr;
        using pyclass::del_item;
        using pyclass::formatted;
        using pyclass::yield_from;
        using pyclass::truth_of;
        using pyclass::module_of;
        using pyclass::handled_exception;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "mailbox.name"))
            bool made = false;
        };
        state& the_state()
        {
            static state* sState = new state{};
            return *sState;
        }

        // (its entry: the object, as it is defined (or assigned) now)
        py const& object_entry(char const* aQualifiedName)
        {
            auto& s = the_state();
            auto const o = s.objects.find(aQualifiedName);
            if (o == s.objects.end())
                py_raise("SystemError", std::string{ "mailbox: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"mailbox.name">()))
        template <pyclass::fixed_string Name>
        py const& G() { static py const* const sEntry = &object_entry(Name.value); return *sEntry; }
        // (an object made: one of the module's attributes)
        void define(char const* aModule, char const* aName, py const& aValue)
        {
            auto& s = the_state();
            s.objects[std::string{ aModule } + "." + aName] = aValue;
            for (auto& a : s.attributes)
                if (a.first == aName)
                {
                    a.second = aValue;
                    return;
                }
            s.attributes.emplace_back(aName, aValue);
        }
        // (del name: no longer one of the module's attributes (its object remains, for the module's functions))
        [[maybe_unused]] void undefine(char const* aModule, char const* aName)
        {
            auto& s = the_state();
            (void)aModule;
            std::erase_if(s.attributes, [&](auto const& a) { return a.first == aName; });
        }
        // (a module's global assigned (global name; name = value): its object, and the module's attribute (if it has been imported))
        void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("mailbox"))
                return py{ fn_import("mailbox") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "mailbox" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "mailbox", mailbox_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- mailbox (Lib/mailbox.py)

        void make_mailbox()
        {
            static module_writer const& w = writer("mailbox");
            // Notes for authors of new mailbox subclasses:
            // 
            // Remember to fsync() changes to disk before closing a modified file
            // or returning from a flush() method.  See functions _sync_flush() and
            // _sync_close().
            define("mailbox", "__all__", py_list({ S<"Mailbox">(), S<"Maildir">(), S<"mbox">(), S<"MH">(), S<"Babyl">(), S<"MMDF">(), S<"Message">(), S<"MaildirMessage">(), S<"mboxMessage">(), S<"MHMessage">(), S<"BabylMessage">(), S<"MMDFMessage">(), S<"Error">(), S<"NoSuchMailboxError">(), S<"NotEmptyError">(), S<"ExternalClashError">(), S<"FormatError">() }));
            define("mailbox", "linesep", (Mod<"os">().attr("linesep")).method("encode", { S<"ascii">() }));
                // Whether each message must end in a newline
            define("mailbox", "Mailbox", w.make_class("Mailbox", {  }, {
                { "__init__", w.method("Mailbox", "__init__", { py_positional("path"), py_positional("factory", none()), py_positional("create", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py factory = a[2];
                        py create = a[3];
                        self.set_attr("_path", (Mod<"os">().attr("path")).method("abspath", { (Mod<"os">().attr("path")).method("expanduser", { path }) }));
                        self.set_attr("_factory", factory);
                        return none();
                    }) },
                { "add", w.method("Mailbox", "add", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "remove", w.method("Mailbox", "remove", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "__delitem__", w.method("Mailbox", "__delitem__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        self.method("remove", { key });
                        return none();
                    }) },
                { "discard", w.method("Mailbox", "discard", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        try
                        {
                            self.method("remove", { key });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"KeyError">()))
                            {
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        return none();
                    }) },
                { "__setitem__", w.method("Mailbox", "__setitem__", { py_positional("key"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py message = a[2];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "get", w.method("Mailbox", "get", { py_positional("key"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py default_ = a[2];
                        try
                        {
                            return self.method("__getitem__", { key });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, B<"KeyError">()))
                            {
                                return default_;
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                        return none();
                    }) },
                { "__getitem__", w.method("Mailbox", "__getitem__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py file;
                        if (!self.attr("_factory").truthy())
                        {
                            return self.method("get_message", { key });
                        }
                        else
                        {
                            {
                                py const manager3 = (Mod<"contextlib">().attr("closing"))(self.method("get_file", { key }));
                                py const exit3 = pyclass::special_method(manager3, "__exit__");
                                py const value3 = pyclass::special_method(manager3, "__enter__")(manager3);
                                file = value3;
                                [[maybe_unused]] int flow3 = 0;
                                [[maybe_unused]] py result3;
                                bool exited3 = false;
                                try
                                {
                                    result3 = self.method("_factory", { file });
                                    flow3 = 1;
                                    goto finally3;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler3;
                                    exited3 = true;
                                    if (!exit3(manager3, type_of(handler3.exception), handler3.exception, handler3.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler3.exception);
                                }
                                finally3:
                                if (!exited3)
                                    exit3(manager3, none(), none(), none());
                                if (flow3 == 1)
                                    return result3;
                            }
                        }
                        return none();
                    }) },
                { "get_message", w.method("Mailbox", "get_message", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "get_string", w.method("Mailbox", "get_string", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        return (Mod<"email">().attr("message_from_bytes"))(self.method("get_bytes", { key })).method("as_string");
                    }) },
                { "get_bytes", w.method("Mailbox", "get_bytes", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "get_file", w.method("Mailbox", "get_file", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "iterkeys", w.method("Mailbox", "iterkeys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "keys", w.method("Mailbox", "keys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.method("iterkeys"));
                    }) },
                { "itervalues", w.method("Mailbox", "itervalues", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("itervalues", "Mailbox.itervalues", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py key;
                            py value;
                            for (py const& item : iterate{ self.method("iterkeys") })
                            {
                                key = item;
                                try
                                {
                                    value = self[key];
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler5;
                                    if (matches(handler5.exception, B<"KeyError">()))
                                    {
                                        continue;
                                    }
                                    else
                                        py_reraise(handler5.exception);
                                }
                                py_yield(value);
                            }
                            return none();
                        });
                    }) },
                { "__iter__", w.method("Mailbox", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("itervalues");
                    }) },
                { "values", w.method("Mailbox", "values", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.method("itervalues"));
                    }) },
                { "iteritems", w.method("Mailbox", "iteritems", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("iteritems", "Mailbox.iteritems", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py key;
                            py value;
                            for (py const& item : iterate{ self.method("iterkeys") })
                            {
                                key = item;
                                try
                                {
                                    value = self[key];
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler7;
                                    if (matches(handler7.exception, B<"KeyError">()))
                                    {
                                        continue;
                                    }
                                    else
                                        py_reraise(handler7.exception);
                                }
                                py_yield(py_tuple({ key, value }));
                            }
                            return none();
                        });
                    }) },
                { "items", w.method("Mailbox", "items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.method("iteritems"));
                    }) },
                { "__contains__", w.method("Mailbox", "__contains__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "__len__", w.method("Mailbox", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "clear", w.method("Mailbox", "clear", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key;
                        for (py const& item : iterate{ self.method("keys") })
                        {
                            key = item;
                            self.method("discard", { key });
                        }
                        return none();
                    }) },
                { "pop", w.method("Mailbox", "pop", { py_positional("key"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py default_ = a[2];
                        py result;
                        try
                        {
                            result = self[key];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, B<"KeyError">()))
                            {
                                return default_;
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        self.method("discard", { key });
                        return result;
                    }) },
                { "popitem", w.method("Mailbox", "popitem", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key;
                        bool broke10 = false;
                        for (py const& item : iterate{ self.method("iterkeys") })
                        {
                            key = item;
                            return py_tuple({ key, self.method("pop", { key }) });
                        }
                        if (!broke10)
                        {
                            py_raise((B<"KeyError">())(S<"No messages in mailbox">()));
                        }
                        return none();
                    }) },
                { "update", w.method("Mailbox", "update", { py_positional("arg", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        py bad_key;
                        py key;
                        py message;
                        py source;
                        if (has_attr(arg, "iteritems"))
                        {
                            source = arg.method("iteritems");
                        }
                        else if (has_attr(arg, "items"))
                        {
                            source = arg.method("items");
                        }
                        else
                        {
                            source = arg;
                        }
                        bad_key = boolean(false);
                        for (py const& item : iterate{ source })
                        {
                            auto const items12 = unpack(item, 2);
                            key = items12[0];
                            message = items12[1];
                            try
                            {
                                self.set_item(key, message);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler13;
                                if (matches(handler13.exception, B<"KeyError">()))
                                {
                                    bad_key = boolean(true);
                                }
                                else
                                    py_reraise(handler13.exception);
                            }
                        }
                        if (bad_key.truthy())
                        {
                            py_raise((B<"KeyError">())(S<"No message with key(s)">()));
                        }
                        return none();
                    }) },
                { "flush", w.method("Mailbox", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "lock", w.method("Mailbox", "lock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "unlock", w.method("Mailbox", "unlock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "close", w.method("Mailbox", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"Method must be implemented by subclass">()));
                    }) },
                { "_string_to_bytes", w.method("Mailbox", "_string_to_bytes", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        try
                        {
                            return message.method("encode", { S<"ascii">() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler14;
                            if (matches(handler14.exception, B<"UnicodeError">()))
                            {
                                py_raise((B<"ValueError">())(S<"String input must be ASCII-only; use bytes or a Message instead">()));
                            }
                            else
                                py_reraise(handler14.exception);
                        }
                        return none();
                    }) },
                { "_append_newline", boolean(false) },
                { "_dump_message", w.method("Mailbox", "_dump_message", { py_positional("message"), py_positional("target"), py_positional("mangle_from_", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py target = a[2];
                        py mangle_from_ = a[3];
                        py buffer;
                        py data;
                        py gen;
                        py lastline;
                        py line;
                        if (isinstance(message, Mod<"email.message">().attr("Message")))
                        {
                            buffer = (Mod<"io">().attr("BytesIO"))();
                            gen = (Mod<"email.generator">().attr("BytesGenerator"))(buffer, mangle_from_, I<0>());
                            gen.method("flatten", { message });
                            buffer.method("seek", { I<0>() });
                            data = buffer.method("read");
                            data = data.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() });
                            target.method("write", { data });
                            if ((self.attr("_append_newline").truthy() && (!data.method("endswith", { G<"mailbox.linesep">() }).truthy())))
                            {
                                // Make sure the message ends with a newline
                                target.method("write", { G<"mailbox.linesep">() });
                            }
                        }
                        else if (isinstance(message, py_tuple({ B<"str">(), B<"bytes">(), Mod<"io">().attr("StringIO") })))
                        {
                            if (isinstance(message, Mod<"io">().attr("StringIO")))
                            {
                                (Mod<"warnings">().attr("warn"))(S<"Use of StringIO input is deprecated, use BytesIO instead">(), B<"DeprecationWarning">(), I<3>());
                                message = message.method("getvalue");
                            }
                            if (isinstance(message, B<"str">()))
                            {
                                message = self.method("_string_to_bytes", { message });
                            }
                            if (mangle_from_.truthy())
                            {
                                message = message.method("replace", { py{ make_bytes(std::string{ "\x0a\x46\x72\x6f\x6d\x20", 6 }) }, py{ make_bytes(std::string{ "\x0a\x3e\x46\x72\x6f\x6d\x20", 7 }) } });
                            }
                            message = message.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() });
                            target.method("write", { message });
                            if ((self.attr("_append_newline").truthy() && (!message.method("endswith", { G<"mailbox.linesep">() }).truthy())))
                            {
                                // Make sure the message ends with a newline
                                target.method("write", { G<"mailbox.linesep">() });
                            }
                        }
                        else if (has_attr(message, "read"))
                        {
                            if (has_attr(message, "buffer"))
                            {
                                (Mod<"warnings">().attr("warn"))(S<"Use of text mode files is deprecated, use a binary mode file instead">(), B<"DeprecationWarning">(), I<3>());
                                message = message.attr("buffer");
                            }
                            lastline = none();
                            while (boolean(true).truthy())
                            {
                                line = message.method("readline");
                                // Universal newline support.
                                if (line.method("endswith", { py{ make_bytes(std::string{ "\x0d\x0a", 2 }) } }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-2>(), none())] + py{ make_bytes(std::string{ "\x0a", 1 }) };
                                }
                                else if (line.method("endswith", { py{ make_bytes(std::string{ "\x0d", 1 }) } }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-1>(), none())] + py{ make_bytes(std::string{ "\x0a", 1 }) };
                                }
                                if (!line.truthy())
                                {
                                    break;
                                }
                                if ((mangle_from_.truthy() && line.method("startswith", { py{ make_bytes(std::string{ "\x46\x72\x6f\x6d\x20", 5 }) } }).truthy()))
                                {
                                    line = py{ make_bytes(std::string{ "\x3e\x46\x72\x6f\x6d\x20", 6 }) } + line[B<"slice">()(I<5>(), none(), none())];
                                }
                                line = line.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() });
                                target.method("write", { line });
                                lastline = line;
                            }
                            if ((self.attr("_append_newline").truthy() && lastline.truthy() && (!lastline.method("endswith", { G<"mailbox.linesep">() }).truthy())))
                            {
                                // Make sure the message ends with a newline
                                target.method("write", { G<"mailbox.linesep">() });
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"Invalid message type: %s">()) % (B<"type">())(message)));
                        }
                        return none();
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

                // This method is for backward compatibility only.
            define("mailbox", "Maildir", w.make_class("Maildir", { G<"mailbox.Mailbox">() }, {
                { "colon", S<":">() },
                { "__init__", w.method("Maildir", "__init__", { py_positional("dirname"), py_positional("factory", none()), py_positional("create", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dirname = a[1];
                        py factory = a[2];
                        py create = a[3];
                        py path;
                        (G<"mailbox.Mailbox">()).method("__init__", { self, dirname, factory, create });
                        self.set_attr("_paths", dict_of({ { S<"tmp">(), (Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<"tmp">() }) }, { S<"new">(), (Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<"new">() }) }, { S<"cur">(), (Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<"cur">() }) } }));
                        if (!(Mod<"os">().attr("path")).method("exists", { self.attr("_path") }).truthy())
                        {
                            if (create.truthy())
                            {
                                (Mod<"os">().attr("mkdir"))(self.attr("_path"), I<448>());
                                for (py const& item : iterate{ self.attr("_paths").method("values") })
                                {
                                    path = item;
                                    (Mod<"os">().attr("mkdir"))(path, I<448>());
                                }
                            }
                            else
                            {
                                py_raise((G<"mailbox.NoSuchMailboxError">())(self.attr("_path")));
                            }
                        }
                        self.set_attr("_toc", py_dict());
                        self.set_attr("_toc_mtimes", dict_of({ { S<"cur">(), I<0>() }, { S<"new">(), I<0>() } }));
                        self.set_attr("_last_read", I<0>());
                        self.set_attr("_skewfactor", F<0.1>());
                        return none();
                    }) },
                { "add", w.method("Maildir", "add", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py dest;
                        py e;
                        py subdir;
                        py suffix;
                        py tmp_file;
                        py uniq;
                        tmp_file = self.method("_create_tmp");
                        try
                        {
                            self.method("_dump_message", { message, tmp_file });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler17;
                            if (matches(handler17.exception, B<"BaseException">()))
                            {
                                tmp_file.method("close");
                                (Mod<"os">().attr("remove"))(tmp_file.attr("name"));
                                py_reraise(handler17.exception);
                            }
                            else
                                py_reraise(handler17.exception);
                        }
                        (G<"mailbox._sync_close">())(tmp_file);
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            subdir = message.method("get_subdir");
                            suffix = self.attr("colon") + message.method("get_info");
                            if (suffix == self.attr("colon"))
                            {
                                suffix = S<"">();
                            }
                        }
                        else
                        {
                            subdir = S<"new">();
                            suffix = S<"">();
                        }
                        uniq = (Mod<"os">().attr("path")).method("basename", { tmp_file.attr("name") }).method("split", { self.attr("colon") })[I<0>()];
                        dest = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), subdir, uniq + suffix });
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            (Mod<"os">().attr("utime"))(tmp_file.attr("name"), py_tuple({ (Mod<"os">().attr("path")).method("getatime", { tmp_file.attr("name") }), message.method("get_date") }));
                        }
                        // No file modification should be done after the file is moved to its
                        // final position in order to prevent race conditions with changes
                        // from other programs
                        try
                        {
                            {
                                bool otherwise19 = false;
                                try
                                {
                                    (Mod<"os">().attr("link"))(tmp_file.attr("name"), dest);
                                    otherwise19 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler19;
                                    if (matches(handler19.exception, py_tuple({ B<"AttributeError">(), B<"PermissionError">() })))
                                    {
                                        (Mod<"os">().attr("rename"))(tmp_file.attr("name"), dest);
                                    }
                                    else
                                        py_reraise(handler19.exception);
                                }
                                if (otherwise19)
                                {
                                    (Mod<"os">().attr("remove"))(tmp_file.attr("name"));
                                }
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler18;
                            if (matches(handler18.exception, B<"OSError">()))
                            {
                                e = handler18.exception;
                                (Mod<"os">().attr("remove"))(tmp_file.attr("name"));
                                if (e.attr("errno") == (Mod<"errno">().attr("EEXIST")))
                                {
                                    py_raise((G<"mailbox.ExternalClashError">())((S<"Name clash with existing message: %s">()) % dest));
                                }
                                else
                                {
                                    py_reraise(handler18.exception);
                                }
                            }
                            else
                                py_reraise(handler18.exception);
                        }
                        return uniq;
                    }) },
                { "remove", w.method("Maildir", "remove", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        (Mod<"os">().attr("remove"))((Mod<"os">().attr("path")).method("join", { self.attr("_path"), self.method("_lookup", { key }) }));
                        return none();
                    }) },
                { "discard", w.method("Maildir", "discard", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        // This overrides an inapplicable implementation in the superclass.
                        try
                        {
                            self.method("remove", { key });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler20;
                            if (matches(handler20.exception, py_tuple({ B<"KeyError">(), B<"FileNotFoundError">() })))
                            {
                            }
                            else
                                py_reraise(handler20.exception);
                        }
                        return none();
                    }) },
                { "__setitem__", w.method("Maildir", "__setitem__", { py_positional("key"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py message = a[2];
                        py dominant_subpath;
                        py new_path;
                        py old_subpath;
                        py subdir;
                        py suffix;
                        py temp_key;
                        py temp_subpath;
                        py tmp_path;
                        old_subpath = self.method("_lookup", { key });
                        temp_key = self.method("add", { message });
                        temp_subpath = self.method("_lookup", { temp_key });
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            // temp's subdir and suffix were specified by message.
                            dominant_subpath = temp_subpath;
                        }
                        else
                        {
                            // temp's subdir and suffix were defaults from add().
                            dominant_subpath = old_subpath;
                        }
                        subdir = (Mod<"os">().attr("path")).method("dirname", { dominant_subpath });
                        if (dominant_subpath.contains(self.attr("colon")))
                        {
                            suffix = self.attr("colon") + dominant_subpath.method("split", { self.attr("colon") })[I<-1>()];
                        }
                        else
                        {
                            suffix = S<"">();
                        }
                        self.method("discard", { key });
                        tmp_path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), temp_subpath });
                        new_path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), subdir, key + suffix });
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            (Mod<"os">().attr("utime"))(tmp_path, py_tuple({ (Mod<"os">().attr("path")).method("getatime", { tmp_path }), message.method("get_date") }));
                        }
                        // No file modification should be done after the file is moved to its
                        // final position in order to prevent race conditions with changes
                        // from other programs
                        (Mod<"os">().attr("rename"))(tmp_path, new_path);
                        return none();
                    }) },
                { "get_message", w.method("Maildir", "get_message", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py f;
                        py msg;
                        py name;
                        py subdir;
                        py subpath;
                        subpath = self.method("_lookup", { key });
                        {
                            py const manager21 = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), subpath }), S<"rb">());
                            py const exit21 = pyclass::special_method(manager21, "__exit__");
                            py const value21 = pyclass::special_method(manager21, "__enter__")(manager21);
                            f = value21;
                            [[maybe_unused]] int flow21 = 0;
                            [[maybe_unused]] py result21;
                            bool exited21 = false;
                            try
                            {
                                if (self.attr("_factory").truthy())
                                {
                                    msg = self.method("_factory", { f });
                                }
                                else
                                {
                                    msg = (G<"mailbox.MaildirMessage">())(f);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler21;
                                exited21 = true;
                                if (!exit21(manager21, type_of(handler21.exception), handler21.exception, handler21.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler21.exception);
                            }
                            finally21:
                            if (!exited21)
                                exit21(manager21, none(), none(), none());
                            if (flow21 == 1)
                                return result21;
                        }
                        auto const items22 = unpack((Mod<"os">().attr("path")).method("split", { subpath }), 2);
                        subdir = items22[0];
                        name = items22[1];
                        msg.method("set_subdir", { subdir });
                        if (name.contains(self.attr("colon")))
                        {
                            msg.method("set_info", { name.method("split", { self.attr("colon") })[I<-1>()] });
                        }
                        msg.method("set_date", { (Mod<"os">().attr("path")).method("getmtime", { (Mod<"os">().attr("path")).method("join", { self.attr("_path"), subpath }) }) });
                        return msg;
                    }) },
                { "get_bytes", w.method("Maildir", "get_bytes", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py f;
                        {
                            py const manager23 = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), self.method("_lookup", { key }) }), S<"rb">());
                            py const exit23 = pyclass::special_method(manager23, "__exit__");
                            py const value23 = pyclass::special_method(manager23, "__enter__")(manager23);
                            f = value23;
                            [[maybe_unused]] int flow23 = 0;
                            [[maybe_unused]] py result23;
                            bool exited23 = false;
                            try
                            {
                                result23 = f.method("read").method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } });
                                flow23 = 1;
                                goto finally23;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler23;
                                exited23 = true;
                                if (!exit23(manager23, type_of(handler23.exception), handler23.exception, handler23.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler23.exception);
                            }
                            finally23:
                            if (!exited23)
                                exit23(manager23, none(), none(), none());
                            if (flow23 == 1)
                                return result23;
                        }
                        return none();
                    }) },
                { "get_file", w.method("Maildir", "get_file", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py f;
                        f = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), self.method("_lookup", { key }) }), S<"rb">());
                        return (G<"mailbox._ProxyFile">())(f);
                    }) },
                { "get_info", w.method("Maildir", "get_info", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py subpath;
                        subpath = self.method("_lookup", { key });
                        if (subpath.contains(self.attr("colon")))
                        {
                            return subpath.method("split", { self.attr("colon") })[I<-1>()];
                        }
                        return S<"">();
                    }) },
                { "set_info", w.method("Maildir", "set_info", { py_positional("key"), py_positional("info") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py info = a[2];
                        py new_path;
                        py new_subpath;
                        py old_path;
                        py old_subpath;
                        if (!isinstance(info, B<"str">()))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "info must be a string: " } + (B<"type">())(info).str())));
                        }
                        old_subpath = self.method("_lookup", { key });
                        new_subpath = old_subpath.method("split", { self.attr("colon") })[I<0>()];
                        if (info.truthy())
                        {
                            new_subpath = inplace(NEOS_PY_ADD, new_subpath, self.attr("colon") + info);
                        }
                        if (new_subpath == old_subpath)
                        {
                            return none();
                        }
                        old_path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), old_subpath });
                        new_path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), new_subpath });
                        (Mod<"os">().attr("rename"))(old_path, new_path);
                        self.attr("_toc").set_item(key, new_subpath);
                        return none();
                    }) },
                { "get_flags", w.method("Maildir", "get_flags", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py info;
                        info = self.method("get_info", { key });
                        if (info.method("startswith", { S<"2,">() }).truthy())
                        {
                            return info[B<"slice">()(I<2>(), none(), none())];
                        }
                        return S<"">();
                    }) },
                { "set_flags", w.method("Maildir", "set_flags", { py_positional("key"), py_positional("flags") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py flags = a[2];
                        if (!isinstance(flags, B<"str">()))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "flags must be a string: " } + (B<"type">())(flags).str())));
                        }
                        // TODO: check if flags are valid standard flag characters?
                        self.method("set_info", { key, (S<"2,">()) + (S<"">()).method("join", { (B<"sorted">())((B<"set">())(flags)) }) });
                        return none();
                    }) },
                { "add_flag", w.method("Maildir", "add_flag", { py_positional("key"), py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py flag = a[2];
                        if (!isinstance(flag, B<"str">()))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "flag must be a string: " } + (B<"type">())(flag).str())));
                        }
                        // TODO: check that flag is a valid standard flag character?
                        self.method("set_flags", { key, (S<"">()).method("join", { (B<"set">())(self.method("get_flags", { key })) | (B<"set">())(flag) }) });
                        return none();
                    }) },
                { "remove_flag", w.method("Maildir", "remove_flag", { py_positional("key"), py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py flag = a[2];
                        if (!isinstance(flag, B<"str">()))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "flag must be a string: " } + (B<"type">())(flag).str())));
                        }
                        if (self.method("get_flags", { key }).truthy())
                        {
                            self.method("set_flags", { key, (S<"">()).method("join", { (B<"set">())(self.method("get_flags", { key })) - (B<"set">())(flag) }) });
                        }
                        return none();
                    }) },
                { "iterkeys", w.method("Maildir", "iterkeys", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("iterkeys", "Maildir.iterkeys", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py key;
                            self.method("_refresh");
                            for (py const& item : iterate{ self.attr("_toc") })
                            {
                                key = item;
                                try
                                {
                                    self.method("_lookup", { key });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler25;
                                    if (matches(handler25.exception, B<"KeyError">()))
                                    {
                                        continue;
                                    }
                                    else
                                        py_reraise(handler25.exception);
                                }
                                py_yield(key);
                            }
                            return none();
                        });
                    }) },
                { "__contains__", w.method("Maildir", "__contains__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        self.method("_refresh");
                        return boolean(self.attr("_toc").contains(key));
                    }) },
                { "__len__", w.method("Maildir", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_refresh");
                        return py{ self.attr("_toc").size() };
                    }) },
                { "flush", w.method("Maildir", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        // Maildir changes are always written immediately, so there's nothing
                        // to do.
                        return none();
                    }) },
                { "lock", w.method("Maildir", "lock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "unlock", w.method("Maildir", "unlock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "close", w.method("Maildir", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "list_folders", w.method("Maildir", "list_folders", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entry;
                        py result;
                        result = py_list();
                        for (py const& item : iterate{ (Mod<"os">().attr("listdir"))(self.attr("_path")) })
                        {
                            entry = item;
                            if (((py{ entry.size() } > (I<1>())) && (entry[I<0>()] == (S<".">())) && (Mod<"os">().attr("path")).method("isdir", { (Mod<"os">().attr("path")).method("join", { self.attr("_path"), entry }) }).truthy()))
                            {
                                result.method("append", { entry[B<"slice">()(I<1>(), none(), none())] });
                            }
                        }
                        return result;
                    }) },
                { "get_folder", w.method("Maildir", "get_folder", { py_positional("folder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py folder = a[1];
                        return (G<"mailbox.Maildir">()).call({ (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (S<".">()) + folder }) }, { { "factory", self.attr("_factory") }, { "create", boolean(false) } });
                    }) },
                { "add_folder", w.method("Maildir", "add_folder", { py_positional("folder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py folder = a[1];
                        py maildirfolder_path;
                        py path;
                        py result;
                        path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (S<".">()) + folder });
                        result = (G<"mailbox.Maildir">()).call({ path }, { { "factory", self.attr("_factory") } });
                        maildirfolder_path = (Mod<"os">().attr("path")).method("join", { path, S<"maildirfolder">() });
                        if (!(Mod<"os">().attr("path")).method("exists", { maildirfolder_path }).truthy())
                        {
                            (Mod<"os">().attr("close"))((Mod<"os">().attr("open"))(maildirfolder_path, (Mod<"os">().attr("O_CREAT")) | (Mod<"os">().attr("O_WRONLY")), I<438>()));
                        }
                        return result;
                    }) },
                { "remove_folder", w.method("Maildir", "remove_folder", { py_positional("folder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py folder = a[1];
                        py dirs;
                        py entry;
                        py files;
                        py path;
                        py root;
                        path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (S<".">()) + folder });
                        for (py const& item : iterate{ (Mod<"os">().attr("listdir"))((Mod<"os">().attr("path")).method("join", { path, S<"new">() })) + (Mod<"os">().attr("listdir"))((Mod<"os">().attr("path")).method("join", { path, S<"cur">() })) })
                        {
                            entry = item;
                            if (((py{ entry.size() } < (I<1>())) || (entry[I<0>()] != (S<".">()))))
                            {
                                py_raise((G<"mailbox.NotEmptyError">())((S<"Folder contains message(s): %s">()) % folder));
                            }
                        }
                        for (py const& item : iterate{ (Mod<"os">().attr("listdir"))(path) })
                        {
                            entry = item;
                            if (((entry != (S<"new">())) && (entry != (S<"cur">())) && (entry != (S<"tmp">())) && (Mod<"os">().attr("path")).method("isdir", { (Mod<"os">().attr("path")).method("join", { path, entry }) }).truthy()))
                            {
                                py_raise((G<"mailbox.NotEmptyError">())((S<"Folder contains subdirectory '%s': %s">()) % py_tuple({ folder, entry })));
                            }
                        }
                        for (py const& item : iterate{ (Mod<"os">().attr("walk")).call({ path }, { { "topdown", boolean(false) } }) })
                        {
                            auto const items30 = unpack(item, 3);
                            root = items30[0];
                            dirs = items30[1];
                            files = items30[2];
                            for (py const& item : iterate{ files })
                            {
                                entry = item;
                                (Mod<"os">().attr("remove"))((Mod<"os">().attr("path")).method("join", { root, entry }));
                            }
                            for (py const& item : iterate{ dirs })
                            {
                                entry = item;
                                (Mod<"os">().attr("rmdir"))((Mod<"os">().attr("path")).method("join", { root, entry }));
                            }
                        }
                        (Mod<"os">().attr("rmdir"))(path);
                        return none();
                    }) },
                { "clean", w.method("Maildir", "clean", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entry;
                        py now;
                        py path;
                        now = (Mod<"time">().attr("time"))();
                        for (py const& item : iterate{ (Mod<"os">().attr("listdir"))((Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<"tmp">() })) })
                        {
                            entry = item;
                            path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<"tmp">(), entry });
                            if ((now - (Mod<"os">().attr("path")).method("getatime", { path })) > (I<129600>()))
                            {
                                (Mod<"os">().attr("remove"))(path);
                            }
                        }
                        return none();
                    }) },
                { "_count", I<1>() },
                { "_create_tmp", w.method("Maildir", "_create_tmp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py hostname;
                        py now;
                        py path;
                        py uniq;
                        now = (Mod<"time">().attr("time"))();
                        hostname = (Mod<"socket">().attr("gethostname"))();
                        if (hostname.contains((S<"/">())))
                        {
                            hostname = hostname.method("replace", { S<"/">(), S<"\\057">() });
                        }
                        if (hostname.contains((S<":">())))
                        {
                            hostname = hostname.method("replace", { S<":">(), S<"\\072">() });
                        }
                        uniq = (S<"%s.M%sP%sQ%s.%s">()) % py_tuple({ (B<"int">())(now), (B<"int">())((now % (I<1>())) * (F<1000000.0>())), (Mod<"os">().attr("getpid"))(), (G<"mailbox.Maildir">()).attr("_count"), hostname });
                        path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<"tmp">(), uniq });
                        try
                        {
                            (Mod<"os">().attr("stat"))(path);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler34;
                            if (matches(handler34.exception, B<"FileNotFoundError">()))
                            {
                                {
                                    py const target35 = G<"mailbox.Maildir">();
                                    target35.set_attr("_count", inplace(NEOS_PY_ADD, target35.attr("_count"), I<1>()));
                                }
                                try
                                {
                                    return (G<"mailbox._create_carefully">())(path);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler36;
                                    if (matches(handler36.exception, B<"FileExistsError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler36.exception);
                                }
                            }
                            else
                                py_reraise(handler34.exception);
                        }
                        // Fall through to here if stat succeeded or open raised EEXIST.
                        py_raise((G<"mailbox.ExternalClashError">())((S<"Name clash prevented file creation: %s">()) % path));
                    }) },
                { "_refresh", w.method("Maildir", "_refresh", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entry;
                        py mtime;
                        py p;
                        py path;
                        py refresh;
                        py subdir;
                        py uniq;
                        // If it has been less than two seconds since the last _refresh() call,
                        // we have to unconditionally re-read the mailbox just in case it has
                        // been modified, because os.path.mtime() has a 2 sec resolution in the
                        // most common worst case (FAT) and a 1 sec resolution typically.  This
                        // results in a few unnecessary re-reads when _refresh() is called
                        // multiple times in that interval, but once the clock ticks over, we
                        // will only re-read as needed.  Because the filesystem might be being
                        // served by an independent system with its own clock, we record and
                        // compare with the mtimes from the filesystem.  Because the other
                        // system's clock might be skewing relative to our clock, we add an
                        // extra delta to our wait.  The default is one tenth second, but is an
                        // instance variable and so can be adjusted if dealing with a
                        // particularly skewed or irregular system.
                        if (((Mod<"time">().attr("time"))() - self.attr("_last_read")) > ((I<2>()) + self.attr("_skewfactor")))
                        {
                            refresh = boolean(false);
                            for (py const& item : iterate{ self.attr("_toc_mtimes") })
                            {
                                subdir = item;
                                mtime = (Mod<"os">().attr("path")).method("getmtime", { self.attr("_paths")[subdir] });
                                if (mtime > self.attr("_toc_mtimes")[subdir])
                                {
                                    refresh = boolean(true);
                                }
                                self.attr("_toc_mtimes").set_item(subdir, mtime);
                            }
                            if (!refresh.truthy())
                            {
                                return none();
                            }
                        }
                        // Refresh toc
                        self.set_attr("_toc", py_dict());
                        for (py const& item : iterate{ self.attr("_toc_mtimes") })
                        {
                            subdir = item;
                            path = self.attr("_paths")[subdir];
                            for (py const& item : iterate{ (Mod<"os">().attr("listdir"))(path) })
                            {
                                entry = item;
                                if (entry.method("startswith", { S<".">() }).truthy())
                                {
                                    continue;
                                }
                                p = (Mod<"os">().attr("path")).method("join", { path, entry });
                                if ((Mod<"os">().attr("path")).method("isdir", { p }).truthy())
                                {
                                    continue;
                                }
                                uniq = entry.method("split", { self.attr("colon") })[I<0>()];
                                self.attr("_toc").set_item(uniq, (Mod<"os">().attr("path")).method("join", { subdir, entry }));
                            }
                        }
                        self.set_attr("_last_read", (Mod<"time">().attr("time"))());
                        return none();
                    }) },
                { "_lookup", w.method("Maildir", "_lookup", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        try
                        {
                            if ((Mod<"os">().attr("path")).method("exists", { (Mod<"os">().attr("path")).method("join", { self.attr("_path"), self.attr("_toc")[key] }) }).truthy())
                            {
                                return self.attr("_toc")[key];
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler40;
                            if (matches(handler40.exception, B<"KeyError">()))
                            {
                            }
                            else
                                py_reraise(handler40.exception);
                        }
                        self.method("_refresh");
                        try
                        {
                            return self.attr("_toc")[key];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler41;
                            if (matches(handler41.exception, B<"KeyError">()))
                            {
                                fn_raise_from((B<"KeyError">())((S<"No message with key: %s">()) % key).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler41.exception);
                        }
                        return none();
                    }) },
                { "next", w.method("Maildir", "next", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!has_attr(self, "_onetime_keys"))
                        {
                            self.set_attr("_onetime_keys", self.method("iterkeys"));
                        }
                        while (boolean(true).truthy())
                        {
                            try
                            {
                                return self[(B<"next">())(self.attr("_onetime_keys"))];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler43;
                                if (matches(handler43.exception, B<"StopIteration">()))
                                {
                                    return none();
                                }
                                else if (matches(handler43.exception, B<"KeyError">()))
                                {
                                    continue;
                                }
                                else
                                    py_reraise(handler43.exception);
                            }
                        }
                        return none();
                    }) }, }));

            define("mailbox", "_singlefileMailbox", w.make_class("_singlefileMailbox", { G<"mailbox.Mailbox">() }, {
                { "__init__", w.method("_singlefileMailbox", "__init__", { py_positional("path"), py_positional("factory", none()), py_positional("create", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py factory = a[2];
                        py create = a[3];
                        py e;
                        py f;
                        (G<"mailbox.Mailbox">()).method("__init__", { self, path, factory, create });
                        try
                        {
                            f = (B<"open">())(self.attr("_path"), S<"rb+">());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler44;
                            if (matches(handler44.exception, B<"OSError">()))
                            {
                                e = handler44.exception;
                                if (e.attr("errno") == (Mod<"errno">().attr("ENOENT")))
                                {
                                    if (create.truthy())
                                    {
                                        f = (B<"open">())(self.attr("_path"), S<"wb+">());
                                    }
                                    else
                                    {
                                        py_raise((G<"mailbox.NoSuchMailboxError">())(self.attr("_path")));
                                    }
                                }
                                else if (py_tuple({ Mod<"errno">().attr("EACCES"), Mod<"errno">().attr("EROFS") }).contains(e.attr("errno")))
                                {
                                    f = (B<"open">())(self.attr("_path"), S<"rb">());
                                }
                                else
                                {
                                    py_reraise(handler44.exception);
                                }
                            }
                            else
                                py_reraise(handler44.exception);
                        }
                        self.set_attr("_file", f);
                        self.set_attr("_toc", none());
                        self.set_attr("_next_key", I<0>());
                        self.set_attr("_pending", boolean(false));
                        self.set_attr("_pending_sync", boolean(false));
                        self.set_attr("_locked", boolean(false));
                        self.set_attr("_file_length", none());
                        return none();
                    }) },
                { "add", w.method("_singlefileMailbox", "add", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        self.method("_lookup");
                        self.attr("_toc").set_item(self.attr("_next_key"), self.method("_append_message", { message }));
                        {
                            py const target45 = self;
                            target45.set_attr("_next_key", inplace(NEOS_PY_ADD, target45.attr("_next_key"), I<1>()));
                        }
                        // _append_message appends the message to the mailbox file. We
                        // don't need a full rewrite + rename, sync is enough.
                        self.set_attr("_pending_sync", boolean(true));
                        return self.attr("_next_key") - (I<1>());
                    }) },
                { "remove", w.method("_singlefileMailbox", "remove", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        self.method("_lookup", { key });
                        del_item(self.attr("_toc"), key);
                        self.set_attr("_pending", boolean(true));
                        return none();
                    }) },
                { "__setitem__", w.method("_singlefileMailbox", "__setitem__", { py_positional("key"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py message = a[2];
                        self.method("_lookup", { key });
                        self.attr("_toc").set_item(key, self.method("_append_message", { message }));
                        self.set_attr("_pending", boolean(true));
                        return none();
                    }) },
                { "iterkeys", w.method("_singlefileMailbox", "iterkeys", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("iterkeys", "_singlefileMailbox.iterkeys", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            self.method("_lookup");
                            yield_from(self.attr("_toc").method("keys"));
                            return none();
                        });
                    }) },
                { "__contains__", w.method("_singlefileMailbox", "__contains__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        self.method("_lookup");
                        return boolean(self.attr("_toc").contains(key));
                    }) },
                { "__len__", w.method("_singlefileMailbox", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_lookup");
                        return py{ self.attr("_toc").size() };
                    }) },
                { "lock", w.method("_singlefileMailbox", "lock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_locked").truthy())
                        {
                            (G<"mailbox._lock_file">())(self.attr("_file"));
                            self.set_attr("_locked", boolean(true));
                        }
                        return none();
                    }) },
                { "unlock", w.method("_singlefileMailbox", "unlock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_locked").truthy())
                        {
                            (G<"mailbox._unlock_file">())(self.attr("_file"));
                            self.set_attr("_locked", boolean(false));
                        }
                        return none();
                    }) },
                { "flush", w.method("_singlefileMailbox", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py buffer;
                        py cur_len;
                        py info;
                        py key;
                        py new_file;
                        py new_start;
                        py new_toc;
                        py start;
                        py stop;
                        if (!self.attr("_pending").truthy())
                        {
                            if (self.attr("_pending_sync").truthy())
                            {
                                // Messages have only been added, so syncing the file
                                // is enough.
                                (G<"mailbox._sync_flush">())(self.attr("_file"));
                                self.set_attr("_pending_sync", boolean(false));
                            }
                            return none();
                        }
                        // In order to be writing anything out at all, self._toc must
                        // already have been generated (and presumably has been modified
                        // by adding or deleting an item).
                        if (self.attr("_toc").is_none())
                            py_raise(B<"AssertionError">()());
                        // Check length of self._file; if it's changed, some other process
                        // has modified the mailbox since we scanned it.
                        self.attr("_file").method("seek", { I<0>(), I<2>() });
                        cur_len = self.attr("_file").method("tell");
                        if (cur_len != self.attr("_file_length"))
                        {
                            py_raise((G<"mailbox.ExternalClashError">())((S<"Size of mailbox file changed (expected %i, found %i)">()) % py_tuple({ self.attr("_file_length"), cur_len })));
                        }
                        new_file = (G<"mailbox._create_temporary">())(self.attr("_path"));
                        try
                        {
                            new_toc = py_dict();
                            self.method("_pre_mailbox_hook", { new_file });
                            for (py const& item : iterate{ (B<"sorted">())(self.attr("_toc").method("keys")) })
                            {
                                key = item;
                                auto const items48 = unpack(self.attr("_toc")[key], 2);
                                start = items48[0];
                                stop = items48[1];
                                self.attr("_file").method("seek", { start });
                                self.method("_pre_message_hook", { new_file });
                                new_start = new_file.method("tell");
                                while (boolean(true).truthy())
                                {
                                    buffer = self.attr("_file").method("read", { (B<"min">())(I<4096>(), stop - self.attr("_file").method("tell")) });
                                    if (!buffer.truthy())
                                    {
                                        break;
                                    }
                                    new_file.method("write", { buffer });
                                }
                                new_toc.set_item(key, py_tuple({ new_start, new_file.method("tell") }));
                                self.method("_post_message_hook", { new_file });
                            }
                            self.set_attr("_file_length", new_file.method("tell"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler46;
                            {
                                new_file.method("close");
                                (Mod<"os">().attr("remove"))(new_file.attr("name"));
                                py_reraise(handler46.exception);
                            }
                        }
                        (G<"mailbox._sync_close">())(new_file);
                        // self._file is about to get replaced, so no need to sync.
                        self.attr("_file").method("close");
                        // Make sure the new file's mode and owner are the same as the old file's
                        info = (Mod<"os">().attr("stat"))(self.attr("_path"));
                        (Mod<"os">().attr("chmod"))(new_file.attr("name"), info.attr("st_mode"));
                        try
                        {
                            (Mod<"os">().attr("chown"))(new_file.attr("name"), info.attr("st_uid"), info.attr("st_gid"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler50;
                            if (matches(handler50.exception, py_tuple({ B<"AttributeError">(), B<"OSError">() })))
                            {
                            }
                            else
                                py_reraise(handler50.exception);
                        }
                        try
                        {
                            (Mod<"os">().attr("rename"))(new_file.attr("name"), self.attr("_path"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler51;
                            if (matches(handler51.exception, B<"FileExistsError">()))
                            {
                                (Mod<"os">().attr("remove"))(self.attr("_path"));
                                (Mod<"os">().attr("rename"))(new_file.attr("name"), self.attr("_path"));
                            }
                            else
                                py_reraise(handler51.exception);
                        }
                        self.set_attr("_file", (B<"open">())(self.attr("_path"), S<"rb+">()));
                        self.set_attr("_toc", new_toc);
                        self.set_attr("_pending", boolean(false));
                        self.set_attr("_pending_sync", boolean(false));
                        if (self.attr("_locked").truthy())
                        {
                            (G<"mailbox._lock_file">()).call({ self.attr("_file") }, { { "dotlock", boolean(false) } });
                        }
                        return none();
                    }) },
                { "_pre_mailbox_hook", w.method("_singlefileMailbox", "_pre_mailbox_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        return none();
                    }) },
                { "_pre_message_hook", w.method("_singlefileMailbox", "_pre_message_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        return none();
                    }) },
                { "_post_message_hook", w.method("_singlefileMailbox", "_post_message_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        return none();
                    }) },
                { "close", w.method("_singlefileMailbox", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            std::optional<py> pending52;
                            [[maybe_unused]] int flow52 = 0;
                            [[maybe_unused]] py result52;
                            try
                            {
                                self.method("flush");
                            }
                            catch (raised const&)
                            {
                                pending52 = py_take_exception();
                            }
                            finally52:
                            {
                                std::optional<py> pending53;
                                [[maybe_unused]] int flow53 = 0;
                                [[maybe_unused]] py result53;
                                try
                                {
                                    if (self.attr("_locked").truthy())
                                    {
                                        self.method("unlock");
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending53 = py_take_exception();
                                }
                                finally53:
                                self.attr("_file").method("close");
                                if (pending53)
                                    py_reraise(*pending53);
                                if (flow53 == 1)
                                    return result53;
                            }
                            if (pending52)
                                py_reraise(*pending52);
                            if (flow52 == 1)
                                return result52;
                        }
                        return none();
                    }) },
                { "_lookup", w.method("_singlefileMailbox", "_lookup", { py_positional("key", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        if (self.attr("_toc").is_none())
                        {
                            self.method("_generate_toc");
                        }
                        if (!key.is_none())
                        {
                            try
                            {
                                return self.attr("_toc")[key];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler54;
                                if (matches(handler54.exception, B<"KeyError">()))
                                {
                                    fn_raise_from((B<"KeyError">())((S<"No message with key: %s">()) % key).get(), none().get());
                                    throw raised{};
                                }
                                else
                                    py_reraise(handler54.exception);
                            }
                        }
                        return none();
                    }) },
                { "_append_message", w.method("_singlefileMailbox", "_append_message", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py before;
                        py offsets;
                        self.attr("_file").method("seek", { I<0>(), I<2>() });
                        before = self.attr("_file").method("tell");
                        if (((py{ self.attr("_toc").size() } == (I<0>())) && (!self.attr("_pending").truthy())))
                        {
                            // This is the first message, and the _pre_mailbox_hook
                            // hasn't yet been called. If self._pending is True,
                            // messages have been removed, so _pre_mailbox_hook must
                            // have been called already.
                            self.method("_pre_mailbox_hook", { self.attr("_file") });
                        }
                        try
                        {
                            self.method("_pre_message_hook", { self.attr("_file") });
                            offsets = self.method("_install_message", { message });
                            self.method("_post_message_hook", { self.attr("_file") });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler55;
                            if (matches(handler55.exception, B<"BaseException">()))
                            {
                                self.attr("_file").method("truncate", { before });
                                py_reraise(handler55.exception);
                            }
                            else
                                py_reraise(handler55.exception);
                        }
                        self.attr("_file").method("flush");
                        self.set_attr("_file_length", self.attr("_file").method("tell"));
                        return offsets;
                    }) }, }));

            define("mailbox", "_mboxMMDF", w.make_class("_mboxMMDF", { G<"mailbox._singlefileMailbox">() }, {
                { "_mangle_from_", boolean(true) },
                { "get_message", w.method("_mboxMMDF", "get_message", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py from_line;
                        py msg;
                        py start;
                        py stop;
                        py string;
                        auto const items56 = unpack(self.method("_lookup", { key }), 2);
                        start = items56[0];
                        stop = items56[1];
                        self.attr("_file").method("seek", { start });
                        from_line = self.attr("_file").method("readline").method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "", 0 }) } }).method("decode", { S<"ascii">() });
                        string = self.attr("_file").method("read", { stop - self.attr("_file").method("tell") });
                        msg = self.method("_message_factory", { string.method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } }) });
                        msg.method("set_unixfrom", { from_line });
                        msg.method("set_from", { from_line[B<"slice">()(I<5>(), none(), none())] });
                        return msg;
                    }) },
                { "get_string", w.method("_mboxMMDF", "get_string", { py_positional("key"), py_positional("from_", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py from_ = a[2];
                        return (Mod<"email">().attr("message_from_bytes"))(self.method("get_bytes", { key, from_ })).method("as_string", {  }, { { "unixfrom", from_ } });
                    }) },
                { "get_bytes", w.method("_mboxMMDF", "get_bytes", { py_positional("key"), py_positional("from_", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py from_ = a[2];
                        py start;
                        py stop;
                        py string;
                        auto const items57 = unpack(self.method("_lookup", { key }), 2);
                        start = items57[0];
                        stop = items57[1];
                        self.attr("_file").method("seek", { start });
                        if (!from_.truthy())
                        {
                            self.attr("_file").method("readline");
                        }
                        string = self.attr("_file").method("read", { stop - self.attr("_file").method("tell") });
                        return string.method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } });
                    }) },
                { "get_file", w.method("_mboxMMDF", "get_file", { py_positional("key"), py_positional("from_", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py from_ = a[2];
                        py start;
                        py stop;
                        auto const items58 = unpack(self.method("_lookup", { key }), 2);
                        start = items58[0];
                        stop = items58[1];
                        self.attr("_file").method("seek", { start });
                        if (!from_.truthy())
                        {
                            self.attr("_file").method("readline");
                        }
                        return (G<"mailbox._PartialFile">())(self.attr("_file"), self.attr("_file").method("tell"), stop);
                    }) },
                { "_install_message", w.method("_mboxMMDF", "_install_message", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py author;
                        py from_line;
                        py newline;
                        py start;
                        py stop;
                        from_line = none();
                        if (isinstance(message, B<"str">()))
                        {
                            message = self.method("_string_to_bytes", { message });
                        }
                        if ((isinstance(message, B<"bytes">()) && message.method("startswith", { py{ make_bytes(std::string{ "\x46\x72\x6f\x6d\x20", 5 }) } }).truthy()))
                        {
                            newline = message.method("find", { py{ make_bytes(std::string{ "\x0a", 1 }) } });
                            if (newline != (I<-1>()))
                            {
                                from_line = message[B<"slice">()(none(), newline, none())];
                                message = message[B<"slice">()(newline + (I<1>()), none(), none())];
                            }
                            else
                            {
                                from_line = message;
                                message = py{ make_bytes(std::string{ "", 0 }) };
                            }
                        }
                        else if (isinstance(message, G<"mailbox._mboxMMDFMessage">()))
                        {
                            author = message.method("get_from").method("encode", { S<"ascii">() });
                            from_line = py{ make_bytes(std::string{ "\x46\x72\x6f\x6d\x20", 5 }) } + author;
                        }
                        else if (isinstance(message, Mod<"email.message">().attr("Message")))
                        {
                            from_line = message.method("get_unixfrom");
                            if (!from_line.is_none())
                            {
                                from_line = from_line.method("encode", { S<"ascii">() });
                            }
                        }
                        if (from_line.is_none())
                        {
                            from_line = py{ make_bytes(std::string{ "\x46\x72\x6f\x6d\x20\x4d\x41\x49\x4c\x45\x52\x2d\x44\x41\x45\x4d\x4f\x4e\x20", 19 }) } + (Mod<"time">().attr("asctime"))((Mod<"time">().attr("gmtime"))()).method("encode");
                        }
                        start = self.attr("_file").method("tell");
                        self.attr("_file").method("write", { from_line + (G<"mailbox.linesep">()) });
                        self.method("_dump_message", { message, self.attr("_file"), self.attr("_mangle_from_") });
                        stop = self.attr("_file").method("tell");
                        return py_tuple({ start, stop });
                    }) }, }));

                // All messages must end in a newline character, and
                // _post_message_hooks outputs an empty line between messages.
            define("mailbox", "mbox", w.make_class("mbox", { G<"mailbox._mboxMMDF">() }, {
                { "_mangle_from_", boolean(true) },
                { "_append_newline", boolean(true) },
                { "__init__", w.method("mbox", "__init__", { py_positional("path"), py_positional("factory", none()), py_positional("create", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py factory = a[2];
                        py create = a[3];
                        self.set_attr("_message_factory", G<"mailbox.mboxMessage">());
                        (G<"mailbox._mboxMMDF">()).method("__init__", { self, path, factory, create });
                        return none();
                    }) },
                { "_post_message_hook", w.method("mbox", "_post_message_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        f.method("write", { G<"mailbox.linesep">() });
                        return none();
                    }) },
                { "_generate_toc", w.method("mbox", "_generate_toc", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py last_was_empty;
                        py line;
                        py line_pos;
                        py starts;
                        py stops;
                        auto const items59 = unpack(py_tuple({ py_list(), py_list() }), 2);
                        starts = items59[0];
                        stops = items59[1];
                        last_was_empty = boolean(false);
                        self.attr("_file").method("seek", { I<0>() });
                        while (boolean(true).truthy())
                        {
                            line_pos = self.attr("_file").method("tell");
                            line = self.attr("_file").method("readline");
                            if (line.method("startswith", { py{ make_bytes(std::string{ "\x46\x72\x6f\x6d\x20", 5 }) } }).truthy())
                            {
                                if (py{ stops.size() } < py{ starts.size() })
                                {
                                    if (last_was_empty.truthy())
                                    {
                                        stops.method("append", { line_pos - py{ (G<"mailbox.linesep">()).size() } });
                                    }
                                    else
                                    {
                                        // The last line before the "From " line wasn't
                                        // blank, but we consider it a start of a
                                        // message anyway.
                                        stops.method("append", { line_pos });
                                    }
                                }
                                starts.method("append", { line_pos });
                                last_was_empty = boolean(false);
                            }
                            else if (!line.truthy())
                            {
                                if (last_was_empty.truthy())
                                {
                                    stops.method("append", { line_pos - py{ (G<"mailbox.linesep">()).size() } });
                                }
                                else
                                {
                                    stops.method("append", { line_pos });
                                }
                                break;
                            }
                            else if (line == (G<"mailbox.linesep">()))
                            {
                                last_was_empty = boolean(true);
                            }
                            else
                            {
                                last_was_empty = boolean(false);
                            }
                        }
                        self.set_attr("_toc", (B<"dict">())((B<"enumerate">())((B<"zip">())(starts, stops))));
                        self.set_attr("_next_key", py{ self.attr("_toc").size() });
                        self.set_attr("_file_length", self.attr("_file").method("tell"));
                        return none();
                    }) }, }));

            define("mailbox", "MMDF", w.make_class("MMDF", { G<"mailbox._mboxMMDF">() }, {
                { "__init__", w.method("MMDF", "__init__", { py_positional("path"), py_positional("factory", none()), py_positional("create", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py factory = a[2];
                        py create = a[3];
                        self.set_attr("_message_factory", G<"mailbox.MMDFMessage">());
                        (G<"mailbox._mboxMMDF">()).method("__init__", { self, path, factory, create });
                        return none();
                    }) },
                { "_pre_message_hook", w.method("MMDF", "_pre_message_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        f.method("write", { py{ make_bytes(std::string{ "\x01\x01\x01\x01", 4 }) } + (G<"mailbox.linesep">()) });
                        return none();
                    }) },
                { "_post_message_hook", w.method("MMDF", "_post_message_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        f.method("write", { ((G<"mailbox.linesep">()) + py{ make_bytes(std::string{ "\x01\x01\x01\x01", 4 }) }) + (G<"mailbox.linesep">()) });
                        return none();
                    }) },
                { "_generate_toc", w.method("MMDF", "_generate_toc", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        py line_pos;
                        py next_pos;
                        py starts;
                        py stops;
                        auto const items61 = unpack(py_tuple({ py_list(), py_list() }), 2);
                        starts = items61[0];
                        stops = items61[1];
                        self.attr("_file").method("seek", { I<0>() });
                        next_pos = I<0>();
                        while (boolean(true).truthy())
                        {
                            line_pos = next_pos;
                            line = self.attr("_file").method("readline");
                            next_pos = self.attr("_file").method("tell");
                            if (line.method("startswith", { py{ make_bytes(std::string{ "\x01\x01\x01\x01", 4 }) } + (G<"mailbox.linesep">()) }).truthy())
                            {
                                starts.method("append", { next_pos });
                                while (boolean(true).truthy())
                                {
                                    line_pos = next_pos;
                                    line = self.attr("_file").method("readline");
                                    next_pos = self.attr("_file").method("tell");
                                    if (line == (py{ make_bytes(std::string{ "\x01\x01\x01\x01", 4 }) } + (G<"mailbox.linesep">())))
                                    {
                                        stops.method("append", { line_pos - py{ (G<"mailbox.linesep">()).size() } });
                                        break;
                                    }
                                    else if (!line.truthy())
                                    {
                                        stops.method("append", { line_pos });
                                        break;
                                    }
                                }
                            }
                            else if (!line.truthy())
                            {
                                break;
                            }
                        }
                        self.set_attr("_toc", (B<"dict">())((B<"enumerate">())((B<"zip">())(starts, stops))));
                        self.set_attr("_next_key", py{ self.attr("_toc").size() });
                        self.attr("_file").method("seek", { I<0>(), I<2>() });
                        self.set_attr("_file_length", self.attr("_file").method("tell"));
                        return none();
                    }) }, }));

            define("mailbox", "MH", w.make_class("MH", { G<"mailbox.Mailbox">() }, {
                { "__init__", w.method("MH", "__init__", { py_positional("path"), py_positional("factory", none()), py_positional("create", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py factory = a[2];
                        py create = a[3];
                        (G<"mailbox.Mailbox">()).method("__init__", { self, path, factory, create });
                        if (!(Mod<"os">().attr("path")).method("exists", { self.attr("_path") }).truthy())
                        {
                            if (create.truthy())
                            {
                                (Mod<"os">().attr("mkdir"))(self.attr("_path"), I<448>());
                                (Mod<"os">().attr("close"))((Mod<"os">().attr("open"))((Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<".mh_sequences">() }), ((Mod<"os">().attr("O_CREAT")) | (Mod<"os">().attr("O_EXCL"))) | (Mod<"os">().attr("O_WRONLY")), I<384>()));
                            }
                            else
                            {
                                py_raise((G<"mailbox.NoSuchMailboxError">())(self.attr("_path")));
                            }
                        }
                        self.set_attr("_locked", boolean(false));
                        return none();
                    }) },
                { "add", w.method("MH", "add", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py closed;
                        py f;
                        py keys;
                        py new_key;
                        py new_path;
                        keys = self.method("keys");
                        if (py{ keys.size() } == (I<0>()))
                        {
                            new_key = I<1>();
                        }
                        else
                        {
                            new_key = (B<"max">())(keys) + (I<1>());
                        }
                        new_path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(new_key) });
                        f = (G<"mailbox._create_carefully">())(new_path);
                        closed = boolean(false);
                        {
                            std::optional<py> pending64;
                            [[maybe_unused]] int flow64 = 0;
                            [[maybe_unused]] py result64;
                            try
                            {
                                if (self.attr("_locked").truthy())
                                {
                                    (G<"mailbox._lock_file">())(f);
                                }
                                {
                                    std::optional<py> pending65;
                                    [[maybe_unused]] int flow65 = 0;
                                    [[maybe_unused]] py result65;
                                    try
                                    {
                                        try
                                        {
                                            self.method("_dump_message", { message, f });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler66;
                                            if (matches(handler66.exception, B<"BaseException">()))
                                            {
                                                // Unlock and close so it can be deleted on Windows
                                                if (self.attr("_locked").truthy())
                                                {
                                                    (G<"mailbox._unlock_file">())(f);
                                                }
                                                (G<"mailbox._sync_close">())(f);
                                                closed = boolean(true);
                                                (Mod<"os">().attr("remove"))(new_path);
                                                py_reraise(handler66.exception);
                                            }
                                            else
                                                py_reraise(handler66.exception);
                                        }
                                        if (isinstance(message, G<"mailbox.MHMessage">()))
                                        {
                                            self.method("_dump_sequences", { message, new_key });
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending65 = py_take_exception();
                                    }
                                    finally65:
                                    if (self.attr("_locked").truthy())
                                    {
                                        (G<"mailbox._unlock_file">())(f);
                                    }
                                    if (pending65)
                                        py_reraise(*pending65);
                                    if (flow65 == 1)
                                        { result64 = result65; flow64 = 1; goto finally64; }
                                }
                            }
                            catch (raised const&)
                            {
                                pending64 = py_take_exception();
                            }
                            finally64:
                            if (!closed.truthy())
                            {
                                (G<"mailbox._sync_close">())(f);
                            }
                            if (pending64)
                                py_reraise(*pending64);
                            if (flow64 == 1)
                                return result64;
                        }
                        return new_key;
                    }) },
                { "remove", w.method("MH", "remove", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py e;
                        py f;
                        py path;
                        path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) });
                        {
                            bool otherwise67 = false;
                            try
                            {
                                f = (B<"open">())(path, S<"rb+">());
                                otherwise67 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler67;
                                if (matches(handler67.exception, B<"OSError">()))
                                {
                                    e = handler67.exception;
                                    if (e.attr("errno") == (Mod<"errno">().attr("ENOENT")))
                                    {
                                        py_raise((B<"KeyError">())((S<"No message with key: %s">()) % key));
                                    }
                                    else
                                    {
                                        py_reraise(handler67.exception);
                                    }
                                }
                                else
                                    py_reraise(handler67.exception);
                            }
                            if (otherwise67)
                            {
                                f.method("close");
                                (Mod<"os">().attr("remove"))(path);
                            }
                        }
                        return none();
                    }) },
                { "__setitem__", w.method("MH", "__setitem__", { py_positional("key"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py message = a[2];
                        py e;
                        py f;
                        py path;
                        path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) });
                        try
                        {
                            f = (B<"open">())(path, S<"rb+">());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler68;
                            if (matches(handler68.exception, B<"OSError">()))
                            {
                                e = handler68.exception;
                                if (e.attr("errno") == (Mod<"errno">().attr("ENOENT")))
                                {
                                    py_raise((B<"KeyError">())((S<"No message with key: %s">()) % key));
                                }
                                else
                                {
                                    py_reraise(handler68.exception);
                                }
                            }
                            else
                                py_reraise(handler68.exception);
                        }
                        {
                            std::optional<py> pending69;
                            [[maybe_unused]] int flow69 = 0;
                            [[maybe_unused]] py result69;
                            try
                            {
                                if (self.attr("_locked").truthy())
                                {
                                    (G<"mailbox._lock_file">())(f);
                                }
                                {
                                    std::optional<py> pending70;
                                    [[maybe_unused]] int flow70 = 0;
                                    [[maybe_unused]] py result70;
                                    try
                                    {
                                        (Mod<"os">().attr("close"))((Mod<"os">().attr("open"))(path, (Mod<"os">().attr("O_WRONLY")) | (Mod<"os">().attr("O_TRUNC"))));
                                        self.method("_dump_message", { message, f });
                                        if (isinstance(message, G<"mailbox.MHMessage">()))
                                        {
                                            self.method("_dump_sequences", { message, key });
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending70 = py_take_exception();
                                    }
                                    finally70:
                                    if (self.attr("_locked").truthy())
                                    {
                                        (G<"mailbox._unlock_file">())(f);
                                    }
                                    if (pending70)
                                        py_reraise(*pending70);
                                    if (flow70 == 1)
                                        { result69 = result70; flow69 = 1; goto finally69; }
                                }
                            }
                            catch (raised const&)
                            {
                                pending69 = py_take_exception();
                            }
                            finally69:
                            (G<"mailbox._sync_close">())(f);
                            if (pending69)
                                py_reraise(*pending69);
                            if (flow69 == 1)
                                return result69;
                        }
                        return none();
                    }) },
                { "get_message", w.method("MH", "get_message", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py e;
                        py f;
                        py key_list;
                        py msg;
                        py name;
                        try
                        {
                            if (self.attr("_locked").truthy())
                            {
                                f = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }), S<"rb+">());
                            }
                            else
                            {
                                f = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }), S<"rb">());
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler71;
                            if (matches(handler71.exception, B<"OSError">()))
                            {
                                e = handler71.exception;
                                if (e.attr("errno") == (Mod<"errno">().attr("ENOENT")))
                                {
                                    py_raise((B<"KeyError">())((S<"No message with key: %s">()) % key));
                                }
                                else
                                {
                                    py_reraise(handler71.exception);
                                }
                            }
                            else
                                py_reraise(handler71.exception);
                        }
                        {
                            py const manager72 = f;
                            py const exit72 = pyclass::special_method(manager72, "__exit__");
                            py const value72 = pyclass::special_method(manager72, "__enter__")(manager72);
                            [[maybe_unused]] int flow72 = 0;
                            [[maybe_unused]] py result72;
                            bool exited72 = false;
                            try
                            {
                                if (self.attr("_locked").truthy())
                                {
                                    (G<"mailbox._lock_file">())(f);
                                }
                                {
                                    std::optional<py> pending73;
                                    [[maybe_unused]] int flow73 = 0;
                                    [[maybe_unused]] py result73;
                                    try
                                    {
                                        msg = (G<"mailbox.MHMessage">())(f);
                                    }
                                    catch (raised const&)
                                    {
                                        pending73 = py_take_exception();
                                    }
                                    finally73:
                                    if (self.attr("_locked").truthy())
                                    {
                                        (G<"mailbox._unlock_file">())(f);
                                    }
                                    if (pending73)
                                        py_reraise(*pending73);
                                    if (flow73 == 1)
                                        { result72 = result73; flow72 = 1; goto finally72; }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler72;
                                exited72 = true;
                                if (!exit72(manager72, type_of(handler72.exception), handler72.exception, handler72.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler72.exception);
                            }
                            finally72:
                            if (!exited72)
                                exit72(manager72, none(), none(), none());
                            if (flow72 == 1)
                                return result72;
                        }
                        for (py const& item : iterate{ self.method("get_sequences").method("items") })
                        {
                            auto const items75 = unpack(item, 2);
                            name = items75[0];
                            key_list = items75[1];
                            if (key_list.contains(key))
                            {
                                msg.method("add_sequence", { name });
                            }
                        }
                        return msg;
                    }) },
                { "get_bytes", w.method("MH", "get_bytes", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py e;
                        py f;
                        try
                        {
                            if (self.attr("_locked").truthy())
                            {
                                f = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }), S<"rb+">());
                            }
                            else
                            {
                                f = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }), S<"rb">());
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler76;
                            if (matches(handler76.exception, B<"OSError">()))
                            {
                                e = handler76.exception;
                                if (e.attr("errno") == (Mod<"errno">().attr("ENOENT")))
                                {
                                    py_raise((B<"KeyError">())((S<"No message with key: %s">()) % key));
                                }
                                else
                                {
                                    py_reraise(handler76.exception);
                                }
                            }
                            else
                                py_reraise(handler76.exception);
                        }
                        {
                            py const manager77 = f;
                            py const exit77 = pyclass::special_method(manager77, "__exit__");
                            py const value77 = pyclass::special_method(manager77, "__enter__")(manager77);
                            [[maybe_unused]] int flow77 = 0;
                            [[maybe_unused]] py result77;
                            bool exited77 = false;
                            try
                            {
                                if (self.attr("_locked").truthy())
                                {
                                    (G<"mailbox._lock_file">())(f);
                                }
                                {
                                    std::optional<py> pending78;
                                    [[maybe_unused]] int flow78 = 0;
                                    [[maybe_unused]] py result78;
                                    try
                                    {
                                        result78 = f.method("read").method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } });
                                        flow78 = 1;
                                        goto finally78;
                                    }
                                    catch (raised const&)
                                    {
                                        pending78 = py_take_exception();
                                    }
                                    finally78:
                                    if (self.attr("_locked").truthy())
                                    {
                                        (G<"mailbox._unlock_file">())(f);
                                    }
                                    if (pending78)
                                        py_reraise(*pending78);
                                    if (flow78 == 1)
                                        { result77 = result78; flow77 = 1; goto finally77; }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler77;
                                exited77 = true;
                                if (!exit77(manager77, type_of(handler77.exception), handler77.exception, handler77.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler77.exception);
                            }
                            finally77:
                            if (!exited77)
                                exit77(manager77, none(), none(), none());
                            if (flow77 == 1)
                                return result77;
                        }
                        return none();
                    }) },
                { "get_file", w.method("MH", "get_file", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py e;
                        py f;
                        try
                        {
                            f = (B<"open">())((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }), S<"rb">());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler79;
                            if (matches(handler79.exception, B<"OSError">()))
                            {
                                e = handler79.exception;
                                if (e.attr("errno") == (Mod<"errno">().attr("ENOENT")))
                                {
                                    py_raise((B<"KeyError">())((S<"No message with key: %s">()) % key));
                                }
                                else
                                {
                                    py_reraise(handler79.exception);
                                }
                            }
                            else
                                py_reraise(handler79.exception);
                        }
                        return (G<"mailbox._ProxyFile">())(f);
                    }) },
                { "iterkeys", w.method("MH", "iterkeys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"iter">())((B<"sorted">())([&]() -> py { py const comprehension80 = py_list(); py entry; for (py const& item : iterate{ (Mod<"os">().attr("listdir"))(self.attr("_path")) }) { entry = item; if (!entry.method("isdigit").truthy()) continue; comprehension80.method("append", { (B<"int">())(entry) }); } return comprehension80; }()));
                    }) },
                { "__contains__", w.method("MH", "__contains__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        return (Mod<"os">().attr("path")).method("exists", { (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }) });
                    }) },
                { "__len__", w.method("MH", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ (B<"list">())(self.method("iterkeys")).size() };
                    }) },
                { "_open_mh_sequences_file", w.method("MH", "_open_mh_sequences_file", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py kwargs;
                        py mode;
                        py path;
                        mode = (text.truthy() ? S<"">() : S<"b">());
                        kwargs = (text.truthy() ? dict_of({ { S<"encoding">(), S<"ASCII">() } }) : py_dict());
                        path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<".mh_sequences">() });
                        while (boolean(true).truthy())
                        {
                            try
                            {
                                return call_ex((B<"open">()), { path, (S<"r+">()) + mode }, py{}, kwargs);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler82;
                                if (matches(handler82.exception, B<"FileNotFoundError">()))
                                {
                                }
                                else
                                    py_reraise(handler82.exception);
                            }
                            try
                            {
                                return call_ex((B<"open">()), { path, (S<"x+">()) + mode }, py{}, kwargs);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler83;
                                if (matches(handler83.exception, B<"FileExistsError">()))
                                {
                                }
                                else
                                    py_reraise(handler83.exception);
                            }
                        }
                        return none();
                    }) },
                { "lock", w.method("MH", "lock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_locked").truthy())
                        {
                            self.set_attr("_file", self.method("_open_mh_sequences_file", {  }, { { "text", boolean(false) } }));
                            (G<"mailbox._lock_file">())(self.attr("_file"));
                            self.set_attr("_locked", boolean(true));
                        }
                        return none();
                    }) },
                { "unlock", w.method("MH", "unlock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_locked").truthy())
                        {
                            (G<"mailbox._unlock_file">())(self.attr("_file"));
                            (G<"mailbox._sync_close">())(self.attr("_file"));
                            del_attr(self, "_file");
                            self.set_attr("_locked", boolean(false));
                        }
                        return none();
                    }) },
                { "flush", w.method("MH", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "close", w.method("MH", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_locked").truthy())
                        {
                            self.method("unlock");
                        }
                        return none();
                    }) },
                { "list_folders", w.method("MH", "list_folders", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entry;
                        py result;
                        result = py_list();
                        for (py const& item : iterate{ (Mod<"os">().attr("listdir"))(self.attr("_path")) })
                        {
                            entry = item;
                            if ((Mod<"os">().attr("path")).method("isdir", { (Mod<"os">().attr("path")).method("join", { self.attr("_path"), entry }) }).truthy())
                            {
                                result.method("append", { entry });
                            }
                        }
                        return result;
                    }) },
                { "get_folder", w.method("MH", "get_folder", { py_positional("folder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py folder = a[1];
                        return (G<"mailbox.MH">()).call({ (Mod<"os">().attr("path")).method("join", { self.attr("_path"), folder }) }, { { "factory", self.attr("_factory") }, { "create", boolean(false) } });
                    }) },
                { "add_folder", w.method("MH", "add_folder", { py_positional("folder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py folder = a[1];
                        return (G<"mailbox.MH">()).call({ (Mod<"os">().attr("path")).method("join", { self.attr("_path"), folder }) }, { { "factory", self.attr("_factory") } });
                    }) },
                { "remove_folder", w.method("MH", "remove_folder", { py_positional("folder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py folder = a[1];
                        py entries;
                        py path;
                        path = (Mod<"os">().attr("path")).method("join", { self.attr("_path"), folder });
                        entries = (Mod<"os">().attr("listdir"))(path);
                        if (entries == py_list({ S<".mh_sequences">() }))
                        {
                            (Mod<"os">().attr("remove"))((Mod<"os">().attr("path")).method("join", { path, S<".mh_sequences">() }));
                        }
                        else if (entries == py_list())
                        {
                        }
                        else
                        {
                            py_raise((G<"mailbox.NotEmptyError">())((S<"Folder not empty: %s">()) % self.attr("_path")));
                        }
                        (Mod<"os">().attr("rmdir"))(path);
                        return none();
                    }) },
                { "get_sequences", w.method("MH", "get_sequences", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py all_keys;
                        py contents;
                        py f;
                        py keys;
                        py line;
                        py name;
                        py results;
                        py spec;
                        py start;
                        py stop;
                        results = py_dict();
                        try
                        {
                            f = (B<"open">()).call({ (Mod<"os">().attr("path")).method("join", { self.attr("_path"), S<".mh_sequences">() }), S<"r">() }, { { "encoding", S<"ASCII">() } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler85;
                            if (matches(handler85.exception, B<"FileNotFoundError">()))
                            {
                                return results;
                            }
                            else
                                py_reraise(handler85.exception);
                        }
                        {
                            py const manager86 = f;
                            py const exit86 = pyclass::special_method(manager86, "__exit__");
                            py const value86 = pyclass::special_method(manager86, "__enter__")(manager86);
                            [[maybe_unused]] int flow86 = 0;
                            [[maybe_unused]] py result86;
                            bool exited86 = false;
                            try
                            {
                                all_keys = (B<"set">())(self.method("keys"));
                                for (py const& item : iterate{ f })
                                {
                                    line = item;
                                    try
                                    {
                                        auto const items89 = unpack(line.method("split", { S<":">() }), 2);
                                        name = items89[0];
                                        contents = items89[1];
                                        keys = (B<"set">())();
                                        for (py const& item : iterate{ contents.method("split") })
                                        {
                                            spec = item;
                                            if (spec.method("isdigit").truthy())
                                            {
                                                keys.method("add", { (B<"int">())(spec) });
                                            }
                                            else
                                            {
                                                auto const items92 = unpack([&]() -> py { py const comprehension91 = py_list(); py x; for (py const& item : iterate{ spec.method("split", { S<"-">() }) }) { x = item; comprehension91.method("append", { (B<"int">())(x) }); } return comprehension91; }(), 2);
                                                start = items92[0];
                                                stop = items92[1];
                                                keys.method("update", { (B<"range">())(start, stop + (I<1>())) });
                                            }
                                        }
                                        results.set_item(name, [&]() -> py { py const comprehension93 = py_list(); py key; for (py const& item : iterate{ (B<"sorted">())(keys) }) { key = item; if (!all_keys.contains(key)) continue; comprehension93.method("append", { key }); } return comprehension93; }());
                                        if (py{ results[name].size() } == (I<0>()))
                                        {
                                            del_item(results, name);
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler88;
                                        if (matches(handler88.exception, B<"ValueError">()))
                                        {
                                            py_raise((G<"mailbox.FormatError">())((S<"Invalid sequence specification: %s">()) % line.method("rstrip")));
                                        }
                                        else
                                            py_reraise(handler88.exception);
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler86;
                                exited86 = true;
                                if (!exit86(manager86, type_of(handler86.exception), handler86.exception, handler86.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler86.exception);
                            }
                            finally86:
                            if (!exited86)
                                exit86(manager86, none(), none(), none());
                            if (flow86 == 1)
                                return result86;
                        }
                        return results;
                    }) },
                { "set_sequences", w.method("MH", "set_sequences", { py_positional("sequences") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sequences = a[1];
                        py completing;
                        py f;
                        py key;
                        py keys;
                        py name;
                        py prev;
                        f = self.method("_open_mh_sequences_file", {  }, { { "text", boolean(true) } });
                        {
                            std::optional<py> pending94;
                            [[maybe_unused]] int flow94 = 0;
                            [[maybe_unused]] py result94;
                            try
                            {
                                (Mod<"os">().attr("close"))((Mod<"os">().attr("open"))(f.attr("name"), (Mod<"os">().attr("O_WRONLY")) | (Mod<"os">().attr("O_TRUNC"))));
                                for (py const& item : iterate{ sequences.method("items") })
                                {
                                    auto const items96 = unpack(item, 2);
                                    name = items96[0];
                                    keys = items96[1];
                                    if (py{ keys.size() } == (I<0>()))
                                    {
                                        continue;
                                    }
                                    f.method("write", { name + (S<":">()) });
                                    prev = none();
                                    completing = boolean(false);
                                    for (py const& item : iterate{ (B<"sorted">())((B<"set">())(keys)) })
                                    {
                                        key = item;
                                        if ((key - (I<1>())) == prev)
                                        {
                                            if (!completing.truthy())
                                            {
                                                completing = boolean(true);
                                                f.method("write", { S<"-">() });
                                            }
                                        }
                                        else if (completing.truthy())
                                        {
                                            completing = boolean(false);
                                            f.method("write", { (S<"%s %s">()) % py_tuple({ prev, key }) });
                                        }
                                        else
                                        {
                                            f.method("write", { (S<" %s">()) % key });
                                        }
                                        prev = key;
                                    }
                                    if (completing.truthy())
                                    {
                                        f.method("write", { (B<"str">())(prev) + (S<"\n">()) });
                                    }
                                    else
                                    {
                                        f.method("write", { S<"\n">() });
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending94 = py_take_exception();
                            }
                            finally94:
                            (G<"mailbox._sync_close">())(f);
                            if (pending94)
                                py_reraise(*pending94);
                            if (flow94 == 1)
                                return result94;
                        }
                        return none();
                    }) },
                { "pack", w.method("MH", "pack", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py changes;
                        py key;
                        py key_list;
                        py name;
                        py new_;
                        py old;
                        py prev;
                        py sequences;
                        sequences = self.method("get_sequences");
                        prev = I<0>();
                        changes = py_list();
                        for (py const& item : iterate{ self.method("iterkeys") })
                        {
                            key = item;
                            if ((key - (I<1>())) != prev)
                            {
                                changes.method("append", { py_tuple({ key, prev + (I<1>()) }) });
                                {
                                    bool otherwise99 = false;
                                    try
                                    {
                                        (Mod<"os">().attr("link"))((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }), (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(prev + (I<1>())) }));
                                        otherwise99 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler99;
                                        if (matches(handler99.exception, py_tuple({ B<"AttributeError">(), B<"PermissionError">() })))
                                        {
                                            (Mod<"os">().attr("rename"))((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }), (Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(prev + (I<1>())) }));
                                        }
                                        else
                                            py_reraise(handler99.exception);
                                    }
                                    if (otherwise99)
                                    {
                                        (Mod<"os">().attr("unlink"))((Mod<"os">().attr("path")).method("join", { self.attr("_path"), (B<"str">())(key) }));
                                    }
                                }
                            }
                            prev = inplace(NEOS_PY_ADD, prev, I<1>());
                        }
                        self.set_attr("_next_key", prev + (I<1>()));
                        if (py{ changes.size() } == (I<0>()))
                        {
                            return none();
                        }
                        for (py const& item : iterate{ sequences.method("items") })
                        {
                            auto const items101 = unpack(item, 2);
                            name = items101[0];
                            key_list = items101[1];
                            for (py const& item : iterate{ changes })
                            {
                                auto const items103 = unpack(item, 2);
                                old = items103[0];
                                new_ = items103[1];
                                if (key_list.contains(old))
                                {
                                    key_list.set_item(key_list.method("index", { old }), new_);
                                }
                            }
                        }
                        self.method("set_sequences", { sequences });
                        return none();
                    }) },
                { "_dump_sequences", w.method("MH", "_dump_sequences", { py_positional("message"), py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py key = a[2];
                        py all_sequences;
                        py key_list;
                        py name;
                        py pending_sequences;
                        py sequence;
                        pending_sequences = message.method("get_sequences");
                        all_sequences = self.method("get_sequences");
                        for (py const& item : iterate{ all_sequences.method("items") })
                        {
                            auto const items105 = unpack(item, 2);
                            name = items105[0];
                            key_list = items105[1];
                            if (pending_sequences.contains(name))
                            {
                                key_list.method("append", { key });
                            }
                            else if (key_list.contains(key))
                            {
                                del_item(key_list, key_list.method("index", { key }));
                            }
                        }
                        for (py const& item : iterate{ pending_sequences })
                        {
                            sequence = item;
                            if (!all_sequences.contains(sequence))
                            {
                                all_sequences.set_item(sequence, py_list({ key }));
                            }
                        }
                        self.method("set_sequences", { all_sequences });
                        return none();
                    }) }, }));

            define("mailbox", "Babyl", w.make_class("Babyl", { G<"mailbox._singlefileMailbox">() }, {
                { "_special_labels", (B<"frozenset">())(set_of({ S<"unseen">(), S<"deleted">(), S<"filed">(), S<"answered">(), S<"forwarded">(), S<"edited">(), S<"resent">() })) },
                { "__init__", w.method("Babyl", "__init__", { py_positional("path"), py_positional("factory", none()), py_positional("create", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py factory = a[2];
                        py create = a[3];
                        (G<"mailbox._singlefileMailbox">()).method("__init__", { self, path, factory, create });
                        self.set_attr("_labels", py_dict());
                        return none();
                    }) },
                { "add", w.method("Babyl", "add", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py key;
                        key = (G<"mailbox._singlefileMailbox">()).method("add", { self, message });
                        if (isinstance(message, G<"mailbox.BabylMessage">()))
                        {
                            self.attr("_labels").set_item(key, message.method("get_labels"));
                        }
                        return key;
                    }) },
                { "remove", w.method("Babyl", "remove", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        (G<"mailbox._singlefileMailbox">()).method("remove", { self, key });
                        if (self.attr("_labels").contains(key))
                        {
                            del_item(self.attr("_labels"), key);
                        }
                        return none();
                    }) },
                { "__setitem__", w.method("Babyl", "__setitem__", { py_positional("key"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py message = a[2];
                        (G<"mailbox._singlefileMailbox">()).method("__setitem__", { self, key, message });
                        if (isinstance(message, G<"mailbox.BabylMessage">()))
                        {
                            self.attr("_labels").set_item(key, message.method("get_labels"));
                        }
                        return none();
                    }) },
                { "get_message", w.method("Babyl", "get_message", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py body;
                        py line;
                        py msg;
                        py n;
                        py original_headers;
                        py start;
                        py stop;
                        py visible_headers;
                        auto const items107 = unpack(self.method("_lookup", { key }), 2);
                        start = items107[0];
                        stop = items107[1];
                        self.attr("_file").method("seek", { start });
                        self.attr("_file").method("readline");
                        original_headers = (Mod<"io">().attr("BytesIO"))();
                        while (boolean(true).truthy())
                        {
                            line = self.attr("_file").method("readline");
                            if (((line == (py{ make_bytes(std::string{ "\x2a\x2a\x2a\x20\x45\x4f\x4f\x48\x20\x2a\x2a\x2a", 12 }) } + (G<"mailbox.linesep">()))) || (!line.truthy())))
                            {
                                break;
                            }
                            original_headers.method("write", { line.method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } }) });
                        }
                        visible_headers = (Mod<"io">().attr("BytesIO"))();
                        while (boolean(true).truthy())
                        {
                            line = self.attr("_file").method("readline");
                            if (((line == (G<"mailbox.linesep">())) || (!line.truthy())))
                            {
                                break;
                            }
                            visible_headers.method("write", { line.method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } }) });
                        }
                        // Read up to the stop, or to the end
                        n = stop - self.attr("_file").method("tell");
                        if (!(n >= (I<0>())))
                            py_raise(B<"AssertionError">()());
                        body = self.attr("_file").method("read", { n });
                        body = body.method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } });
                        msg = (G<"mailbox.BabylMessage">())(original_headers.method("getvalue") + body);
                        msg.method("set_visible", { visible_headers.method("getvalue") });
                        if (self.attr("_labels").contains(key))
                        {
                            msg.method("set_labels", { self.attr("_labels")[key] });
                        }
                        return msg;
                    }) },
                { "get_bytes", w.method("Babyl", "get_bytes", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py data;
                        py headers;
                        py line;
                        py n;
                        py original_headers;
                        py start;
                        py stop;
                        auto const items110 = unpack(self.method("_lookup", { key }), 2);
                        start = items110[0];
                        stop = items110[1];
                        self.attr("_file").method("seek", { start });
                        self.attr("_file").method("readline");
                        original_headers = (Mod<"io">().attr("BytesIO"))();
                        while (boolean(true).truthy())
                        {
                            line = self.attr("_file").method("readline");
                            if (((line == (py{ make_bytes(std::string{ "\x2a\x2a\x2a\x20\x45\x4f\x4f\x48\x20\x2a\x2a\x2a", 12 }) } + (G<"mailbox.linesep">()))) || (!line.truthy())))
                            {
                                break;
                            }
                            original_headers.method("write", { line.method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } }) });
                        }
                        while (boolean(true).truthy())
                        {
                            line = self.attr("_file").method("readline");
                            if (((line == (G<"mailbox.linesep">())) || (!line.truthy())))
                            {
                                break;
                            }
                        }
                        headers = original_headers.method("getvalue");
                        n = stop - self.attr("_file").method("tell");
                        if (!(n >= (I<0>())))
                            py_raise(B<"AssertionError">()());
                        data = self.attr("_file").method("read", { n });
                        data = data.method("replace", { G<"mailbox.linesep">(), py{ make_bytes(std::string{ "\x0a", 1 }) } });
                        return headers + data;
                    }) },
                { "get_file", w.method("Babyl", "get_file", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        return (Mod<"io">().attr("BytesIO"))(self.method("get_bytes", { key }).method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }));
                    }) },
                { "get_labels", w.method("Babyl", "get_labels", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py label_list;
                        py labels;
                        self.method("_lookup");
                        labels = (B<"set">())();
                        for (py const& item : iterate{ self.attr("_labels").method("values") })
                        {
                            label_list = item;
                            labels.method("update", { label_list });
                        }
                        labels.method("difference_update", { self.attr("_special_labels") });
                        return (B<"list">())(labels);
                    }) },
                { "_generate_toc", w.method("Babyl", "_generate_toc", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py label_lists;
                        py labels;
                        py line;
                        py line_pos;
                        py next_pos;
                        py starts;
                        py stops;
                        auto const items114 = unpack(py_tuple({ py_list(), py_list() }), 2);
                        starts = items114[0];
                        stops = items114[1];
                        self.attr("_file").method("seek", { I<0>() });
                        next_pos = I<0>();
                        label_lists = py_list();
                        while (boolean(true).truthy())
                        {
                            line_pos = next_pos;
                            line = self.attr("_file").method("readline");
                            next_pos = self.attr("_file").method("tell");
                            if (line == (py{ make_bytes(std::string{ "\x1f\x0c", 2 }) } + (G<"mailbox.linesep">())))
                            {
                                if (py{ stops.size() } < py{ starts.size() })
                                {
                                    stops.method("append", { line_pos - py{ (G<"mailbox.linesep">()).size() } });
                                }
                                starts.method("append", { next_pos });
                                labels = [&]() -> py { py const comprehension116 = py_list(); py label; for (py const& item : iterate{ self.attr("_file").method("readline")[B<"slice">()(I<1>(), none(), none())].method("split", { py{ make_bytes(std::string{ "\x2c", 1 }) } }) }) { label = item; if (!label.method("strip").truthy()) continue; comprehension116.method("append", { label.method("strip") }); } return comprehension116; }();
                                label_lists.method("append", { labels });
                            }
                            else if (((line == py{ make_bytes(std::string{ "\x1f", 1 }) }) || (line == (py{ make_bytes(std::string{ "\x1f", 1 }) } + (G<"mailbox.linesep">())))))
                            {
                                if (py{ stops.size() } < py{ starts.size() })
                                {
                                    stops.method("append", { line_pos - py{ (G<"mailbox.linesep">()).size() } });
                                }
                            }
                            else if (!line.truthy())
                            {
                                stops.method("append", { line_pos - py{ (G<"mailbox.linesep">()).size() } });
                                break;
                            }
                        }
                        self.set_attr("_toc", (B<"dict">())((B<"enumerate">())((B<"zip">())(starts, stops))));
                        self.set_attr("_labels", (B<"dict">())((B<"enumerate">())(label_lists)));
                        self.set_attr("_next_key", py{ self.attr("_toc").size() });
                        self.attr("_file").method("seek", { I<0>(), I<2>() });
                        self.set_attr("_file_length", self.attr("_file").method("tell"));
                        return none();
                    }) },
                { "_pre_mailbox_hook", w.method("Babyl", "_pre_mailbox_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        py babyl;
                        py labels;
                        babyl = py{ make_bytes(std::string{ "\x42\x41\x42\x59\x4c\x20\x4f\x50\x54\x49\x4f\x4e\x53\x3a", 14 }) } + (G<"mailbox.linesep">());
                        babyl = inplace(NEOS_PY_ADD, babyl, py{ make_bytes(std::string{ "\x56\x65\x72\x73\x69\x6f\x6e\x3a\x20\x35", 10 }) } + (G<"mailbox.linesep">()));
                        labels = self.method("get_labels");
                        labels = [&]() -> py { py const comprehension117 = py_list(); py label; for (py const& item : iterate{ labels }) { label = item; comprehension117.method("append", { label.method("encode") }); } return comprehension117; }();
                        babyl = inplace(NEOS_PY_ADD, babyl, (py{ make_bytes(std::string{ "\x4c\x61\x62\x65\x6c\x73\x3a", 7 }) } + py{ make_bytes(std::string{ "\x2c", 1 }) }.method("join", { labels })) + (G<"mailbox.linesep">()));
                        babyl = inplace(NEOS_PY_ADD, babyl, py{ make_bytes(std::string{ "\x1f", 1 }) });
                        f.method("write", { babyl });
                        return none();
                    }) },
                { "_pre_message_hook", w.method("Babyl", "_pre_message_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        f.method("write", { py{ make_bytes(std::string{ "\x0c", 1 }) } + (G<"mailbox.linesep">()) });
                        return none();
                    }) },
                { "_post_message_hook", w.method("Babyl", "_post_message_hook", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        f.method("write", { (G<"mailbox.linesep">()) + py{ make_bytes(std::string{ "\x1f", 1 }) } });
                        return none();
                    }) },
                { "_install_message", w.method("Babyl", "_install_message", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py body_start;
                        py buffer;
                        py first_pass;
                        py label;
                        py labels;
                        py line;
                        py orig_buffer;
                        py orig_generator;
                        py original_pos;
                        py special_labels;
                        py start;
                        py stop;
                        py vis_buffer;
                        py vis_generator;
                        start = self.attr("_file").method("tell");
                        if (isinstance(message, G<"mailbox.BabylMessage">()))
                        {
                            special_labels = py_list();
                            labels = py_list();
                            for (py const& item : iterate{ message.method("get_labels") })
                            {
                                label = item;
                                if (self.attr("_special_labels").contains(label))
                                {
                                    special_labels.method("append", { label });
                                }
                                else
                                {
                                    labels.method("append", { label });
                                }
                            }
                            self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x31", 1 }) } });
                            for (py const& item : iterate{ special_labels })
                            {
                                label = item;
                                self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x2c\x20", 2 }) } + label.method("encode") });
                            }
                            self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x2c\x2c", 2 }) } });
                            for (py const& item : iterate{ labels })
                            {
                                label = item;
                                self.attr("_file").method("write", { (py{ make_bytes(std::string{ "\x20", 1 }) } + label.method("encode")) + py{ make_bytes(std::string{ "\x2c", 1 }) } });
                            }
                            self.attr("_file").method("write", { G<"mailbox.linesep">() });
                        }
                        else
                        {
                            self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x31\x2c\x2c", 3 }) } + (G<"mailbox.linesep">()) });
                        }
                        if (isinstance(message, Mod<"email.message">().attr("Message")))
                        {
                            orig_buffer = (Mod<"io">().attr("BytesIO"))();
                            orig_generator = (Mod<"email.generator">().attr("BytesGenerator"))(orig_buffer, boolean(false), I<0>());
                            orig_generator.method("flatten", { message });
                            orig_buffer.method("seek", { I<0>() });
                            while (boolean(true).truthy())
                            {
                                line = orig_buffer.method("readline");
                                self.attr("_file").method("write", { line.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                                if (((line == py{ make_bytes(std::string{ "\x0a", 1 }) }) || (!line.truthy())))
                                {
                                    break;
                                }
                            }
                            self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x2a\x2a\x2a\x20\x45\x4f\x4f\x48\x20\x2a\x2a\x2a", 12 }) } + (G<"mailbox.linesep">()) });
                            if (isinstance(message, G<"mailbox.BabylMessage">()))
                            {
                                vis_buffer = (Mod<"io">().attr("BytesIO"))();
                                vis_generator = (Mod<"email.generator">().attr("BytesGenerator"))(vis_buffer, boolean(false), I<0>());
                                vis_generator.method("flatten", { message.method("get_visible") });
                                while (boolean(true).truthy())
                                {
                                    line = vis_buffer.method("readline");
                                    self.attr("_file").method("write", { line.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                                    if (((line == py{ make_bytes(std::string{ "\x0a", 1 }) }) || (!line.truthy())))
                                    {
                                        break;
                                    }
                                }
                            }
                            else
                            {
                                orig_buffer.method("seek", { I<0>() });
                                while (boolean(true).truthy())
                                {
                                    line = orig_buffer.method("readline");
                                    self.attr("_file").method("write", { line.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                                    if (((line == py{ make_bytes(std::string{ "\x0a", 1 }) }) || (!line.truthy())))
                                    {
                                        break;
                                    }
                                }
                            }
                            while (boolean(true).truthy())
                            {
                                buffer = orig_buffer.method("read", { I<4096>() });
                                if (!buffer.truthy())
                                {
                                    break;
                                }
                                self.attr("_file").method("write", { buffer.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                            }
                        }
                        else if (isinstance(message, py_tuple({ B<"bytes">(), B<"str">(), Mod<"io">().attr("StringIO") })))
                        {
                            if (isinstance(message, Mod<"io">().attr("StringIO")))
                            {
                                (Mod<"warnings">().attr("warn"))(S<"Use of StringIO input is deprecated, use BytesIO instead">(), B<"DeprecationWarning">(), I<3>());
                                message = message.method("getvalue");
                            }
                            if (isinstance(message, B<"str">()))
                            {
                                message = self.method("_string_to_bytes", { message });
                            }
                            body_start = message.method("find", { py{ make_bytes(std::string{ "\x0a\x0a", 2 }) } }) + (I<2>());
                            if ((body_start - (I<2>())) != (I<-1>()))
                            {
                                self.attr("_file").method("write", { message[B<"slice">()(none(), body_start, none())].method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                                self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x2a\x2a\x2a\x20\x45\x4f\x4f\x48\x20\x2a\x2a\x2a", 12 }) } + (G<"mailbox.linesep">()) });
                                self.attr("_file").method("write", { message[B<"slice">()(none(), body_start, none())].method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                                self.attr("_file").method("write", { message[B<"slice">()(body_start, none(), none())].method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                            }
                            else
                            {
                                self.attr("_file").method("write", { (py{ make_bytes(std::string{ "\x2a\x2a\x2a\x20\x45\x4f\x4f\x48\x20\x2a\x2a\x2a", 12 }) } + (G<"mailbox.linesep">())) + (G<"mailbox.linesep">()) });
                                self.attr("_file").method("write", { message.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                            }
                        }
                        else if (has_attr(message, "readline"))
                        {
                            if (has_attr(message, "buffer"))
                            {
                                (Mod<"warnings">().attr("warn"))(S<"Use of text mode files is deprecated, use a binary mode file instead">(), B<"DeprecationWarning">(), I<3>());
                                message = message.attr("buffer");
                            }
                            original_pos = message.method("tell");
                            first_pass = boolean(true);
                            while (boolean(true).truthy())
                            {
                                line = message.method("readline");
                                // Universal newline support.
                                if (line.method("endswith", { py{ make_bytes(std::string{ "\x0d\x0a", 2 }) } }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-2>(), none())] + py{ make_bytes(std::string{ "\x0a", 1 }) };
                                }
                                else if (line.method("endswith", { py{ make_bytes(std::string{ "\x0d", 1 }) } }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-1>(), none())] + py{ make_bytes(std::string{ "\x0a", 1 }) };
                                }
                                self.attr("_file").method("write", { line.method("replace", { py{ make_bytes(std::string{ "\x0a", 1 }) }, G<"mailbox.linesep">() }) });
                                if (((line == py{ make_bytes(std::string{ "\x0a", 1 }) }) || (!line.truthy())))
                                {
                                    if (first_pass.truthy())
                                    {
                                        first_pass = boolean(false);
                                        self.attr("_file").method("write", { py{ make_bytes(std::string{ "\x2a\x2a\x2a\x20\x45\x4f\x4f\x48\x20\x2a\x2a\x2a", 12 }) } + (G<"mailbox.linesep">()) });
                                        message.method("seek", { original_pos });
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }
                            }
                            while (boolean(true).truthy())
                            {
                                line = message.method("readline");
                                if (!line.truthy())
                                {
                                    break;
                                }
                                // Universal newline support.
                                if (line.method("endswith", { py{ make_bytes(std::string{ "\x0d\x0a", 2 }) } }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-2>(), none())] + (G<"mailbox.linesep">());
                                }
                                else if (line.method("endswith", { py{ make_bytes(std::string{ "\x0d", 1 }) } }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-1>(), none())] + (G<"mailbox.linesep">());
                                }
                                else if (line.method("endswith", { py{ make_bytes(std::string{ "\x0a", 1 }) } }).truthy())
                                {
                                    line = line[B<"slice">()(none(), I<-1>(), none())] + (G<"mailbox.linesep">());
                                }
                                self.attr("_file").method("write", { line });
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"Invalid message type: %s">()) % (B<"type">())(message)));
                        }
                        stop = self.attr("_file").method("tell");
                        return py_tuple({ start, stop });
                    }) }, }));

            define("mailbox", "Message", w.make_class("Message", { Mod<"email.message">().attr("Message") }, {
                { "__init__", w.method("Message", "__init__", { py_positional("message", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        if (isinstance(message, Mod<"email.message">().attr("Message")))
                        {
                            self.method("_become_message", { (Mod<"copy">().attr("deepcopy"))(message) });
                            if (isinstance(message, G<"mailbox.Message">()))
                            {
                                message.method("_explain_to", { self });
                            }
                        }
                        else if (isinstance(message, B<"bytes">()))
                        {
                            self.method("_become_message", { (Mod<"email">().attr("message_from_bytes"))(message) });
                        }
                        else if (isinstance(message, B<"str">()))
                        {
                            self.method("_become_message", { (Mod<"email">().attr("message_from_string"))(message) });
                        }
                        else if (isinstance(message, Mod<"io">().attr("TextIOWrapper")))
                        {
                            self.method("_become_message", { (Mod<"email">().attr("message_from_file"))(message) });
                        }
                        else if (has_attr(message, "read"))
                        {
                            self.method("_become_message", { (Mod<"email">().attr("message_from_binary_file"))(message) });
                        }
                        else if (message.is_none())
                        {
                            (Mod<"email.message">().attr("Message")).method("__init__", { self });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"Invalid message type: %s">()) % (B<"type">())(message)));
                        }
                        return none();
                    }) },
                { "_become_message", w.method("Message", "_become_message", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py name;
                        py type_specific;
                        type_specific = (B<"getattr">())(message, S<"_type_specific_attributes">(), py_list());
                        for (py const& item : iterate{ message.attr("__dict__") })
                        {
                            name = item;
                            if (!type_specific.contains(name))
                            {
                                self.attr("__dict__").set_item(name, message.attr("__dict__")[name]);
                            }
                        }
                        return none();
                    }) },
                { "_explain_to", w.method("Message", "_explain_to", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        if (isinstance(message, G<"mailbox.Message">()))
                        {
                            return none();
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(S<"Cannot convert to specified type">()));
                        }
                        return none();
                    }) }, }));

            define("mailbox", "MaildirMessage", w.make_class("MaildirMessage", { G<"mailbox.Message">() }, {
                { "_type_specific_attributes", py_list({ S<"_subdir">(), S<"_info">(), S<"_date">() }) },
                { "__init__", w.method("MaildirMessage", "__init__", { py_positional("message", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        self.set_attr("_subdir", S<"new">());
                        self.set_attr("_info", S<"">());
                        self.set_attr("_date", (Mod<"time">().attr("time"))());
                        (G<"mailbox.Message">()).method("__init__", { self, message });
                        return none();
                    }) },
                { "get_subdir", w.method("MaildirMessage", "get_subdir", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_subdir");
                    }) },
                { "set_subdir", w.method("MaildirMessage", "set_subdir", { py_positional("subdir") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py subdir = a[1];
                        if (((subdir == (S<"new">())) || (subdir == (S<"cur">()))))
                        {
                            self.set_attr("_subdir", subdir);
                        }
                        else
                        {
                            py_raise((B<"ValueError">())((S<"subdir must be 'new' or 'cur': %s">()) % subdir));
                        }
                        return none();
                    }) },
                { "get_flags", w.method("MaildirMessage", "get_flags", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_info").method("startswith", { S<"2,">() }).truthy())
                        {
                            return self.attr("_info")[B<"slice">()(I<2>(), none(), none())];
                        }
                        else
                        {
                            return S<"">();
                        }
                        return none();
                    }) },
                { "set_flags", w.method("MaildirMessage", "set_flags", { py_positional("flags") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flags = a[1];
                        self.set_attr("_info", (S<"2,">()) + (S<"">()).method("join", { (B<"sorted">())(flags) }));
                        return none();
                    }) },
                { "add_flag", w.method("MaildirMessage", "add_flag", { py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag = a[1];
                        self.method("set_flags", { (S<"">()).method("join", { (B<"set">())(self.method("get_flags")) | (B<"set">())(flag) }) });
                        return none();
                    }) },
                { "remove_flag", w.method("MaildirMessage", "remove_flag", { py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag = a[1];
                        if (self.method("get_flags").truthy())
                        {
                            self.method("set_flags", { (S<"">()).method("join", { (B<"set">())(self.method("get_flags")) - (B<"set">())(flag) }) });
                        }
                        return none();
                    }) },
                { "get_date", w.method("MaildirMessage", "get_date", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_date");
                    }) },
                { "set_date", w.method("MaildirMessage", "set_date", { py_positional("date") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py date = a[1];
                        try
                        {
                            self.set_attr("_date", (B<"float">())(date));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler128;
                            if (matches(handler128.exception, B<"ValueError">()))
                            {
                                fn_raise_from((B<"TypeError">())((S<"can't convert to float: %s">()) % date).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler128.exception);
                        }
                        return none();
                    }) },
                { "get_info", w.method("MaildirMessage", "get_info", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_info");
                    }) },
                { "set_info", w.method("MaildirMessage", "set_info", { py_positional("info") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info = a[1];
                        if (isinstance(info, B<"str">()))
                        {
                            self.set_attr("_info", info);
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"info must be a string: %s">()) % (B<"type">())(info)));
                        }
                        return none();
                    }) },
                { "_explain_to", w.method("MaildirMessage", "_explain_to", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py flags;
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            message.method("set_flags", { self.method("get_flags") });
                            message.method("set_subdir", { self.method("get_subdir") });
                            message.method("set_date", { self.method("get_date") });
                        }
                        else if (isinstance(message, G<"mailbox._mboxMMDFMessage">()))
                        {
                            flags = (B<"set">())(self.method("get_flags"));
                            if (flags.contains((S<"S">())))
                            {
                                message.method("add_flag", { S<"R">() });
                            }
                            if (self.method("get_subdir") == (S<"cur">()))
                            {
                                message.method("add_flag", { S<"O">() });
                            }
                            if (flags.contains((S<"T">())))
                            {
                                message.method("add_flag", { S<"D">() });
                            }
                            if (flags.contains((S<"F">())))
                            {
                                message.method("add_flag", { S<"F">() });
                            }
                            if (flags.contains((S<"R">())))
                            {
                                message.method("add_flag", { S<"A">() });
                            }
                            message.method("set_from", { S<"MAILER-DAEMON">(), (Mod<"time">().attr("gmtime"))(self.method("get_date")) });
                        }
                        else if (isinstance(message, G<"mailbox.MHMessage">()))
                        {
                            flags = (B<"set">())(self.method("get_flags"));
                            if (!flags.contains((S<"S">())))
                            {
                                message.method("add_sequence", { S<"unseen">() });
                            }
                            if (flags.contains((S<"R">())))
                            {
                                message.method("add_sequence", { S<"replied">() });
                            }
                            if (flags.contains((S<"F">())))
                            {
                                message.method("add_sequence", { S<"flagged">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.BabylMessage">()))
                        {
                            flags = (B<"set">())(self.method("get_flags"));
                            if (!flags.contains((S<"S">())))
                            {
                                message.method("add_label", { S<"unseen">() });
                            }
                            if (flags.contains((S<"T">())))
                            {
                                message.method("add_label", { S<"deleted">() });
                            }
                            if (flags.contains((S<"R">())))
                            {
                                message.method("add_label", { S<"answered">() });
                            }
                            if (flags.contains((S<"P">())))
                            {
                                message.method("add_label", { S<"forwarded">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.Message">()))
                        {
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"Cannot convert to specified type: %s">()) % (B<"type">())(message)));
                        }
                        return none();
                    }) }, }));

            define("mailbox", "_mboxMMDFMessage", w.make_class("_mboxMMDFMessage", { G<"mailbox.Message">() }, {
                { "_type_specific_attributes", py_list({ S<"_from">() }) },
                { "__init__", w.method("_mboxMMDFMessage", "__init__", { py_positional("message", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py unixfrom;
                        self.method("set_from", { S<"MAILER-DAEMON">(), boolean(true) });
                        if (isinstance(message, Mod<"email.message">().attr("Message")))
                        {
                            unixfrom = message.method("get_unixfrom");
                            if (((!unixfrom.is_none()) && unixfrom.method("startswith", { S<"From ">() }).truthy()))
                            {
                                self.method("set_from", { unixfrom[B<"slice">()(I<5>(), none(), none())] });
                            }
                        }
                        (G<"mailbox.Message">()).method("__init__", { self, message });
                        return none();
                    }) },
                { "get_from", w.method("_mboxMMDFMessage", "get_from", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_from");
                    }) },
                { "set_from", w.method("_mboxMMDFMessage", "set_from", { py_positional("from_"), py_positional("time_", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py from_ = a[1];
                        py time_ = a[2];
                        if (!time_.is_none())
                        {
                            if (time_.is(boolean(true)))
                            {
                                time_ = (Mod<"time">().attr("gmtime"))();
                            }
                            from_ = inplace(NEOS_PY_ADD, from_, (S<" ">()) + (Mod<"time">().attr("asctime"))(time_));
                        }
                        self.set_attr("_from", from_);
                        return none();
                    }) },
                { "get_flags", w.method("_mboxMMDFMessage", "get_flags", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("get", { S<"Status">(), S<"">() }) + self.method("get", { S<"X-Status">(), S<"">() });
                    }) },
                { "set_flags", w.method("_mboxMMDFMessage", "set_flags", { py_positional("flags") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flags = a[1];
                        py flag;
                        py status_flags;
                        py xstatus_flags;
                        flags = (B<"set">())(flags);
                        auto const items129 = unpack(py_tuple({ S<"">(), S<"">() }), 2);
                        status_flags = items129[0];
                        xstatus_flags = items129[1];
                        for (py const& item : iterate{ py_tuple({ S<"R">(), S<"O">() }) })
                        {
                            flag = item;
                            if (flags.contains(flag))
                            {
                                status_flags = inplace(NEOS_PY_ADD, status_flags, flag);
                                flags.method("remove", { flag });
                            }
                        }
                        for (py const& item : iterate{ py_tuple({ S<"D">(), S<"F">(), S<"A">() }) })
                        {
                            flag = item;
                            if (flags.contains(flag))
                            {
                                xstatus_flags = inplace(NEOS_PY_ADD, xstatus_flags, flag);
                                flags.method("remove", { flag });
                            }
                        }
                        xstatus_flags = inplace(NEOS_PY_ADD, xstatus_flags, (S<"">()).method("join", { (B<"sorted">())(flags) }));
                        try
                        {
                            self.method("replace_header", { S<"Status">(), status_flags });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler132;
                            if (matches(handler132.exception, B<"KeyError">()))
                            {
                                self.method("add_header", { S<"Status">(), status_flags });
                            }
                            else
                                py_reraise(handler132.exception);
                        }
                        try
                        {
                            self.method("replace_header", { S<"X-Status">(), xstatus_flags });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler133;
                            if (matches(handler133.exception, B<"KeyError">()))
                            {
                                self.method("add_header", { S<"X-Status">(), xstatus_flags });
                            }
                            else
                                py_reraise(handler133.exception);
                        }
                        return none();
                    }) },
                { "add_flag", w.method("_mboxMMDFMessage", "add_flag", { py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag = a[1];
                        self.method("set_flags", { (S<"">()).method("join", { (B<"set">())(self.method("get_flags")) | (B<"set">())(flag) }) });
                        return none();
                    }) },
                { "remove_flag", w.method("_mboxMMDFMessage", "remove_flag", { py_positional("flag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py flag = a[1];
                        if ((self.contains((S<"Status">())) || self.contains((S<"X-Status">()))))
                        {
                            self.method("set_flags", { (S<"">()).method("join", { (B<"set">())(self.method("get_flags")) - (B<"set">())(flag) }) });
                        }
                        return none();
                    }) },
                { "_explain_to", w.method("_mboxMMDFMessage", "_explain_to", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py flags;
                        py maybe_date;
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            flags = (B<"set">())(self.method("get_flags"));
                            if (flags.contains((S<"O">())))
                            {
                                message.method("set_subdir", { S<"cur">() });
                            }
                            if (flags.contains((S<"F">())))
                            {
                                message.method("add_flag", { S<"F">() });
                            }
                            if (flags.contains((S<"A">())))
                            {
                                message.method("add_flag", { S<"R">() });
                            }
                            if (flags.contains((S<"R">())))
                            {
                                message.method("add_flag", { S<"S">() });
                            }
                            if (flags.contains((S<"D">())))
                            {
                                message.method("add_flag", { S<"T">() });
                            }
                            del_item(message, S<"status">());
                            del_item(message, S<"x-status">());
                            maybe_date = (S<" ">()).method("join", { self.method("get_from").method("split")[B<"slice">()(I<-5>(), none(), none())] });
                            try
                            {
                                message.method("set_date", { (Mod<"calendar">().attr("timegm"))((Mod<"time">().attr("strptime"))(maybe_date, S<"%a %b %d %H:%M:%S %Y">())) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler134;
                                if (matches(handler134.exception, py_tuple({ B<"ValueError">(), B<"OverflowError">() })))
                                {
                                }
                                else
                                    py_reraise(handler134.exception);
                            }
                        }
                        else if (isinstance(message, G<"mailbox._mboxMMDFMessage">()))
                        {
                            message.method("set_flags", { self.method("get_flags") });
                            message.method("set_from", { self.method("get_from") });
                        }
                        else if (isinstance(message, G<"mailbox.MHMessage">()))
                        {
                            flags = (B<"set">())(self.method("get_flags"));
                            if (!flags.contains((S<"R">())))
                            {
                                message.method("add_sequence", { S<"unseen">() });
                            }
                            if (flags.contains((S<"A">())))
                            {
                                message.method("add_sequence", { S<"replied">() });
                            }
                            if (flags.contains((S<"F">())))
                            {
                                message.method("add_sequence", { S<"flagged">() });
                            }
                            del_item(message, S<"status">());
                            del_item(message, S<"x-status">());
                        }
                        else if (isinstance(message, G<"mailbox.BabylMessage">()))
                        {
                            flags = (B<"set">())(self.method("get_flags"));
                            if (!flags.contains((S<"R">())))
                            {
                                message.method("add_label", { S<"unseen">() });
                            }
                            if (flags.contains((S<"D">())))
                            {
                                message.method("add_label", { S<"deleted">() });
                            }
                            if (flags.contains((S<"A">())))
                            {
                                message.method("add_label", { S<"answered">() });
                            }
                            del_item(message, S<"status">());
                            del_item(message, S<"x-status">());
                        }
                        else if (isinstance(message, G<"mailbox.Message">()))
                        {
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"Cannot convert to specified type: %s">()) % (B<"type">())(message)));
                        }
                        return none();
                    }) }, }));

            define("mailbox", "mboxMessage", w.make_class("mboxMessage", { G<"mailbox._mboxMMDFMessage">() }, {
 }));

            define("mailbox", "MHMessage", w.make_class("MHMessage", { G<"mailbox.Message">() }, {
                { "_type_specific_attributes", py_list({ S<"_sequences">() }) },
                { "__init__", w.method("MHMessage", "__init__", { py_positional("message", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        self.set_attr("_sequences", py_list());
                        (G<"mailbox.Message">()).method("__init__", { self, message });
                        return none();
                    }) },
                { "get_sequences", w.method("MHMessage", "get_sequences", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_sequences")[B<"slice">()(none(), none(), none())];
                    }) },
                { "set_sequences", w.method("MHMessage", "set_sequences", { py_positional("sequences") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sequences = a[1];
                        self.set_attr("_sequences", (B<"list">())(sequences));
                        return none();
                    }) },
                { "add_sequence", w.method("MHMessage", "add_sequence", { py_positional("sequence") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sequence = a[1];
                        if (isinstance(sequence, B<"str">()))
                        {
                            if (!self.attr("_sequences").contains(sequence))
                            {
                                self.attr("_sequences").method("append", { sequence });
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"sequence type must be str: %s">()) % (B<"type">())(sequence)));
                        }
                        return none();
                    }) },
                { "remove_sequence", w.method("MHMessage", "remove_sequence", { py_positional("sequence") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sequence = a[1];
                        try
                        {
                            self.attr("_sequences").method("remove", { sequence });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler135;
                            if (matches(handler135.exception, B<"ValueError">()))
                            {
                            }
                            else
                                py_reraise(handler135.exception);
                        }
                        return none();
                    }) },
                { "_explain_to", w.method("MHMessage", "_explain_to", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py sequence;
                        py sequences;
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            sequences = (B<"set">())(self.method("get_sequences"));
                            if (sequences.contains((S<"unseen">())))
                            {
                                message.method("set_subdir", { S<"cur">() });
                            }
                            else
                            {
                                message.method("set_subdir", { S<"cur">() });
                                message.method("add_flag", { S<"S">() });
                            }
                            if (sequences.contains((S<"flagged">())))
                            {
                                message.method("add_flag", { S<"F">() });
                            }
                            if (sequences.contains((S<"replied">())))
                            {
                                message.method("add_flag", { S<"R">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox._mboxMMDFMessage">()))
                        {
                            sequences = (B<"set">())(self.method("get_sequences"));
                            if (!sequences.contains((S<"unseen">())))
                            {
                                message.method("add_flag", { S<"RO">() });
                            }
                            else
                            {
                                message.method("add_flag", { S<"O">() });
                            }
                            if (sequences.contains((S<"flagged">())))
                            {
                                message.method("add_flag", { S<"F">() });
                            }
                            if (sequences.contains((S<"replied">())))
                            {
                                message.method("add_flag", { S<"A">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.MHMessage">()))
                        {
                            for (py const& item : iterate{ self.method("get_sequences") })
                            {
                                sequence = item;
                                message.method("add_sequence", { sequence });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.BabylMessage">()))
                        {
                            sequences = (B<"set">())(self.method("get_sequences"));
                            if (sequences.contains((S<"unseen">())))
                            {
                                message.method("add_label", { S<"unseen">() });
                            }
                            if (sequences.contains((S<"replied">())))
                            {
                                message.method("add_label", { S<"answered">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.Message">()))
                        {
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"Cannot convert to specified type: %s">()) % (B<"type">())(message)));
                        }
                        return none();
                    }) }, }));

            define("mailbox", "BabylMessage", w.make_class("BabylMessage", { G<"mailbox.Message">() }, {
                { "_type_specific_attributes", py_list({ S<"_labels">(), S<"_visible">() }) },
                { "__init__", w.method("BabylMessage", "__init__", { py_positional("message", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        self.set_attr("_labels", py_list());
                        self.set_attr("_visible", (G<"mailbox.Message">())());
                        (G<"mailbox.Message">()).method("__init__", { self, message });
                        return none();
                    }) },
                { "get_labels", w.method("BabylMessage", "get_labels", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_labels")[B<"slice">()(none(), none(), none())];
                    }) },
                { "set_labels", w.method("BabylMessage", "set_labels", { py_positional("labels") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py labels = a[1];
                        self.set_attr("_labels", (B<"list">())(labels));
                        return none();
                    }) },
                { "add_label", w.method("BabylMessage", "add_label", { py_positional("label") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py label = a[1];
                        if (isinstance(label, B<"str">()))
                        {
                            if (!self.attr("_labels").contains(label))
                            {
                                self.attr("_labels").method("append", { label });
                            }
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"label must be a string: %s">()) % (B<"type">())(label)));
                        }
                        return none();
                    }) },
                { "remove_label", w.method("BabylMessage", "remove_label", { py_positional("label") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py label = a[1];
                        try
                        {
                            self.attr("_labels").method("remove", { label });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler137;
                            if (matches(handler137.exception, B<"ValueError">()))
                            {
                            }
                            else
                                py_reraise(handler137.exception);
                        }
                        return none();
                    }) },
                { "get_visible", w.method("BabylMessage", "get_visible", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"mailbox.Message">())(self.attr("_visible"));
                    }) },
                { "set_visible", w.method("BabylMessage", "set_visible", { py_positional("visible") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py visible = a[1];
                        self.set_attr("_visible", (G<"mailbox.Message">())(visible));
                        return none();
                    }) },
                { "update_visible", w.method("BabylMessage", "update_visible", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header;
                        for (py const& item : iterate{ self.attr("_visible").method("keys") })
                        {
                            header = item;
                            if (self.contains(header))
                            {
                                self.attr("_visible").method("replace_header", { header, self[header] });
                            }
                            else
                            {
                                del_item(self.attr("_visible"), header);
                            }
                        }
                        for (py const& item : iterate{ py_tuple({ S<"Date">(), S<"From">(), S<"Reply-To">(), S<"To">(), S<"CC">(), S<"Subject">() }) })
                        {
                            header = item;
                            if ((self.contains(header) && (!self.attr("_visible").contains(header))))
                            {
                                self.attr("_visible").set_item(header, self[header]);
                            }
                        }
                        return none();
                    }) },
                { "_explain_to", w.method("BabylMessage", "_explain_to", { py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py label;
                        py labels;
                        if (isinstance(message, G<"mailbox.MaildirMessage">()))
                        {
                            labels = (B<"set">())(self.method("get_labels"));
                            if (labels.contains((S<"unseen">())))
                            {
                                message.method("set_subdir", { S<"cur">() });
                            }
                            else
                            {
                                message.method("set_subdir", { S<"cur">() });
                                message.method("add_flag", { S<"S">() });
                            }
                            if ((labels.contains((S<"forwarded">())) || labels.contains((S<"resent">()))))
                            {
                                message.method("add_flag", { S<"P">() });
                            }
                            if (labels.contains((S<"answered">())))
                            {
                                message.method("add_flag", { S<"R">() });
                            }
                            if (labels.contains((S<"deleted">())))
                            {
                                message.method("add_flag", { S<"T">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox._mboxMMDFMessage">()))
                        {
                            labels = (B<"set">())(self.method("get_labels"));
                            if (!labels.contains((S<"unseen">())))
                            {
                                message.method("add_flag", { S<"RO">() });
                            }
                            else
                            {
                                message.method("add_flag", { S<"O">() });
                            }
                            if (labels.contains((S<"deleted">())))
                            {
                                message.method("add_flag", { S<"D">() });
                            }
                            if (labels.contains((S<"answered">())))
                            {
                                message.method("add_flag", { S<"A">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.MHMessage">()))
                        {
                            labels = (B<"set">())(self.method("get_labels"));
                            if (labels.contains((S<"unseen">())))
                            {
                                message.method("add_sequence", { S<"unseen">() });
                            }
                            if (labels.contains((S<"answered">())))
                            {
                                message.method("add_sequence", { S<"replied">() });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.BabylMessage">()))
                        {
                            message.method("set_visible", { self.method("get_visible") });
                            for (py const& item : iterate{ self.method("get_labels") })
                            {
                                label = item;
                                message.method("add_label", { label });
                            }
                        }
                        else if (isinstance(message, G<"mailbox.Message">()))
                        {
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"Cannot convert to specified type: %s">()) % (B<"type">())(message)));
                        }
                        return none();
                    }) }, }));

            define("mailbox", "MMDFMessage", w.make_class("MMDFMessage", { G<"mailbox._mboxMMDFMessage">() }, {
 }));

            define("mailbox", "_ProxyFile", w.make_class("_ProxyFile", {  }, {
                { "__init__", w.method("_ProxyFile", "__init__", { py_positional("f"), py_positional("pos", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        py pos = a[2];
                        self.set_attr("_file", f);
                        if (pos.is_none())
                        {
                            self.set_attr("_pos", f.method("tell"));
                        }
                        else
                        {
                            self.set_attr("_pos", pos);
                        }
                        return none();
                    }) },
                { "read", w.method("_ProxyFile", "read", { py_positional("size", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return self.method("_read", { size, self.attr("_file").attr("read") });
                    }) },
                { "read1", w.method("_ProxyFile", "read1", { py_positional("size", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return self.method("_read", { size, self.attr("_file").attr("read1") });
                    }) },
                { "readline", w.method("_ProxyFile", "readline", { py_positional("size", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return self.method("_read", { size, self.attr("_file").attr("readline") });
                    }) },
                { "readlines", w.method("_ProxyFile", "readlines", { py_positional("sizehint", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sizehint = a[1];
                        py line;
                        py result;
                        result = py_list();
                        for (py const& item : iterate{ self })
                        {
                            line = item;
                            result.method("append", { line });
                            if (!sizehint.is_none())
                            {
                                sizehint = inplace(NEOS_PY_SUBTRACT, sizehint, py{ line.size() });
                                if (sizehint <= (I<0>()))
                                {
                                    break;
                                }
                            }
                        }
                        return result;
                    }) },
                { "__iter__", w.method("_ProxyFile", "__iter__", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("__iter__", "_ProxyFile.__iter__", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py line;
                            while (([&]() -> py { line = self.method("readline"); return line; }()).truthy())
                            {
                                py_yield(line);
                            }
                            return none();
                        });
                    }) },
                { "tell", w.method("_ProxyFile", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_pos");
                    }) },
                { "seek", w.method("_ProxyFile", "seek", { py_positional("offset"), py_positional("whence", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py whence = a[2];
                        if (whence == (I<1>()))
                        {
                            self.attr("_file").method("seek", { self.attr("_pos") });
                        }
                        self.attr("_file").method("seek", { offset, whence });
                        self.set_attr("_pos", self.attr("_file").method("tell"));
                        return none();
                    }) },
                { "close", w.method("_ProxyFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (has_attr(self, "_file"))
                        {
                            {
                                std::optional<py> pending143;
                                [[maybe_unused]] int flow143 = 0;
                                [[maybe_unused]] py result143;
                                try
                                {
                                    if (has_attr(self.attr("_file"), "close"))
                                    {
                                        self.attr("_file").method("close");
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending143 = py_take_exception();
                                }
                                finally143:
                                del_attr(self, "_file");
                                if (pending143)
                                    py_reraise(*pending143);
                                if (flow143 == 1)
                                    return result143;
                            }
                        }
                        return none();
                    }) },
                { "_read", w.method("_ProxyFile", "_read", { py_positional("size"), py_positional("read_method") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py read_method = a[2];
                        py result;
                        if (size.is_none())
                        {
                            size = I<-1>();
                        }
                        self.attr("_file").method("seek", { self.attr("_pos") });
                        result = read_method(size);
                        self.set_attr("_pos", self.attr("_file").method("tell"));
                        return result;
                    }) },
                { "__enter__", w.method("_ProxyFile", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("_ProxyFile", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        self.method("close");
                        return none();
                    }, true) },
                { "readable", w.method("_ProxyFile", "readable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_file").method("readable");
                    }) },
                { "writable", w.method("_ProxyFile", "writable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_file").method("writable");
                    }) },
                { "seekable", w.method("_ProxyFile", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_file").method("seekable");
                    }) },
                { "flush", w.method("_ProxyFile", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_file").method("flush");
                    }) },
                { "closed", B<"property">()(w.method("_ProxyFile", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!has_attr(self, "_file"))
                        {
                            return boolean(true);
                        }
                        if (!has_attr(self.attr("_file"), "closed"))
                        {
                            return boolean(false);
                        }
                        return self.attr("_file").attr("closed");
                    })) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

            define("mailbox", "_PartialFile", w.make_class("_PartialFile", { G<"mailbox._ProxyFile">() }, {
                { "__init__", w.method("_PartialFile", "__init__", { py_positional("f"), py_positional("start", none()), py_positional("stop", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        py start = a[2];
                        py stop = a[3];
                        (G<"mailbox._ProxyFile">()).method("__init__", { self, f, start });
                        self.set_attr("_start", start);
                        self.set_attr("_stop", stop);
                        return none();
                    }) },
                { "tell", w.method("_PartialFile", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"mailbox._ProxyFile">()).method("tell", { self }) - self.attr("_start");
                    }) },
                { "seek", w.method("_PartialFile", "seek", { py_positional("offset"), py_positional("whence", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py whence = a[2];
                        if (whence == (I<0>()))
                        {
                            self.set_attr("_pos", self.attr("_start"));
                            whence = I<1>();
                        }
                        else if (whence == (I<2>()))
                        {
                            self.set_attr("_pos", self.attr("_stop"));
                            whence = I<1>();
                        }
                        (G<"mailbox._ProxyFile">()).method("seek", { self, offset, whence });
                        return none();
                    }) },
                { "_read", w.method("_PartialFile", "_read", { py_positional("size"), py_positional("read_method") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py read_method = a[2];
                        py remaining;
                        remaining = self.attr("_stop") - self.attr("_pos");
                        if (remaining <= (I<0>()))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if ((size.is_none() || (size < (I<0>())) || (size > remaining)))
                        {
                            size = remaining;
                        }
                        return (G<"mailbox._ProxyFile">()).method("_read", { self, size, read_method });
                    }) },
                { "close", w.method("_PartialFile", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (has_attr(self, "_file"))
                        {
                            del_attr(self, "_file");
                        }
                        return none();
                    }) }, }));

            define("mailbox", "_lock_file", w.function("_lock_file", "_lock_file", { py_positional("f"), py_positional("dotlock", boolean(true)) }, [](arguments a) -> py
                {
                    py f = a[0];
                    py dotlock = a[1];
                    py dotlock_done;
                    py e;
                    py pre_lock;
                    dotlock_done = boolean(false);
                    try
                    {
                        if ((Mod<"fcntl">()).truthy())
                        {
                            try
                            {
                                (Mod<"fcntl">().attr("lockf"))(f, (Mod<"fcntl">().attr("LOCK_EX")) | (Mod<"fcntl">().attr("LOCK_NB")));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler145;
                                if (matches(handler145.exception, B<"OSError">()))
                                {
                                    e = handler145.exception;
                                    if (py_tuple({ Mod<"errno">().attr("EAGAIN"), Mod<"errno">().attr("EACCES"), Mod<"errno">().attr("EROFS") }).contains(e.attr("errno")))
                                    {
                                        py_raise((G<"mailbox.ExternalClashError">())((S<"lockf: lock unavailable: %s">()) % f.attr("name")));
                                    }
                                    else
                                    {
                                        py_reraise(handler145.exception);
                                    }
                                }
                                else
                                    py_reraise(handler145.exception);
                            }
                        }
                        if (dotlock.truthy())
                        {
                            try
                            {
                                pre_lock = (G<"mailbox._create_temporary">())(f.attr("name") + (S<".lock">()));
                                pre_lock.method("close");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler146;
                                if (matches(handler146.exception, B<"OSError">()))
                                {
                                    e = handler146.exception;
                                    if (py_tuple({ Mod<"errno">().attr("EACCES"), Mod<"errno">().attr("EROFS") }).contains(e.attr("errno")))
                                    {
                                        return none();
                                    }
                                    else
                                    {
                                        py_reraise(handler146.exception);
                                    }
                                }
                                else
                                    py_reraise(handler146.exception);
                            }
                            try
                            {
                                {
                                    bool otherwise148 = false;
                                    try
                                    {
                                        (Mod<"os">().attr("link"))(pre_lock.attr("name"), f.attr("name") + (S<".lock">()));
                                        dotlock_done = boolean(true);
                                        otherwise148 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler148;
                                        if (matches(handler148.exception, py_tuple({ B<"AttributeError">(), B<"PermissionError">() })))
                                        {
                                            (Mod<"os">().attr("rename"))(pre_lock.attr("name"), f.attr("name") + (S<".lock">()));
                                            dotlock_done = boolean(true);
                                        }
                                        else
                                            py_reraise(handler148.exception);
                                    }
                                    if (otherwise148)
                                    {
                                        (Mod<"os">().attr("unlink"))(pre_lock.attr("name"));
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler147;
                                if (matches(handler147.exception, B<"FileExistsError">()))
                                {
                                    (Mod<"os">().attr("remove"))(pre_lock.attr("name"));
                                    py_raise((G<"mailbox.ExternalClashError">())((S<"dot lock unavailable: %s">()) % f.attr("name")));
                                }
                                else
                                    py_reraise(handler147.exception);
                            }
                        }
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler144;
                        {
                            if ((Mod<"fcntl">()).truthy())
                            {
                                (Mod<"fcntl">().attr("lockf"))(f, Mod<"fcntl">().attr("LOCK_UN"));
                            }
                            if (dotlock_done.truthy())
                            {
                                (Mod<"os">().attr("remove"))(f.attr("name") + (S<".lock">()));
                            }
                            py_reraise(handler144.exception);
                        }
                    }
                    return none();
                }));

            define("mailbox", "_unlock_file", w.function("_unlock_file", "_unlock_file", { py_positional("f") }, [](arguments a) -> py
                {
                    py f = a[0];
                    if ((Mod<"fcntl">()).truthy())
                    {
                        (Mod<"fcntl">().attr("lockf"))(f, Mod<"fcntl">().attr("LOCK_UN"));
                    }
                    if ((Mod<"os">().attr("path")).method("exists", { f.attr("name") + (S<".lock">()) }).truthy())
                    {
                        (Mod<"os">().attr("remove"))(f.attr("name") + (S<".lock">()));
                    }
                    return none();
                }));

            define("mailbox", "_create_carefully", w.function("_create_carefully", "_create_carefully", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    return (B<"open">())(path, S<"xb+">());
                }));

            define("mailbox", "_create_temporary", w.function("_create_temporary", "_create_temporary", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    return (G<"mailbox._create_carefully">())((S<"%s.%s.%s.%s">()) % py_tuple({ path, (B<"int">())((Mod<"time">().attr("time"))()), (Mod<"socket">().attr("gethostname"))(), (Mod<"os">().attr("getpid"))() }));
                }));

            define("mailbox", "_sync_flush", w.function("_sync_flush", "_sync_flush", { py_positional("f") }, [](arguments a) -> py
                {
                    py f = a[0];
                    f.method("flush");
                    if (has_attr(Mod<"os">(), "fsync"))
                    {
                        (Mod<"os">().attr("fsync"))(f.method("fileno"));
                    }
                    return none();
                }));

            define("mailbox", "_sync_close", w.function("_sync_close", "_sync_close", { py_positional("f") }, [](arguments a) -> py
                {
                    py f = a[0];
                    (G<"mailbox._sync_flush">())(f);
                    f.method("close");
                    return none();
                }));

            define("mailbox", "Error", w.make_class("Error", { B<"Exception">() }, {
 }));

            define("mailbox", "NoSuchMailboxError", w.make_class("NoSuchMailboxError", { G<"mailbox.Error">() }, {
 }));

            define("mailbox", "NotEmptyError", w.make_class("NotEmptyError", { G<"mailbox.Error">() }, {
 }));

            define("mailbox", "ExternalClashError", w.make_class("ExternalClashError", { G<"mailbox.Error">() }, {
 }));

            define("mailbox", "FormatError", w.make_class("FormatError", { G<"mailbox.Error">() }, {
 }));
        }

        bool mailbox_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_mailbox();
                }
                if (auto const doc = mailbox_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("calendar", py{ fn_import("calendar") }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("errno", py{ fn_import("errno") }.release());
                aAttributes.emplace_back("copy", py{ fn_import("copy") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("contextlib", py{ fn_import("contextlib") }.release());
                aAttributes.emplace_back("GenericAlias", import_attribute("types", "GenericAlias").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("mailbox", mailbox_module);
    }
}
