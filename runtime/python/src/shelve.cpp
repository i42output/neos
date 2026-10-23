/*
  shelve.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/shelve.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module shelve (CPython 3.13's Lib/shelve.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "shelve_docs.hpp"

namespace neos::python_runtime::detail::shelve_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "shelve.name"))
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
                py_raise("SystemError", std::string{ "shelve: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"shelve.name">()))
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
            if (modules_imported("shelve"))
                return py{ fn_import("shelve") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "shelve" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "shelve", shelve_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- shelve (Lib/shelve.py)

        void make_shelve()
        {
            static module_writer const& w = writer("shelve");
            define("shelve", "__all__", py_list({ S<"Shelf">(), S<"BsdDbShelf">(), S<"DbfilenameShelf">(), S<"open">() }));
            define("shelve", "_ClosedDict", w.make_class("_ClosedDict", { Mod<"collections.abc">().attr("MutableMapping") }, {
                { "closed", w.method("_ClosedDict", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py_raise((B<"ValueError">())(S<"invalid operation on closed shelf">()));
                    }, true) },
                { "__iter__", w.method("_ClosedDict", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py_raise((B<"ValueError">())(S<"invalid operation on closed shelf">()));
                    }, true) },
                { "__len__", w.method("_ClosedDict", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py_raise((B<"ValueError">())(S<"invalid operation on closed shelf">()));
                    }, true) },
                { "__getitem__", w.method("_ClosedDict", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py_raise((B<"ValueError">())(S<"invalid operation on closed shelf">()));
                    }, true) },
                { "__setitem__", w.method("_ClosedDict", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py_raise((B<"ValueError">())(S<"invalid operation on closed shelf">()));
                    }, true) },
                { "__delitem__", w.method("_ClosedDict", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py_raise((B<"ValueError">())(S<"invalid operation on closed shelf">()));
                    }, true) },
                { "keys", w.method("_ClosedDict", "closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py_raise((B<"ValueError">())(S<"invalid operation on closed shelf">()));
                    }, true) },
                { "__repr__", w.method("_ClosedDict", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"<Closed Dictionary>">();
                    }) }, }));

            define("shelve", "Shelf", w.make_class("Shelf", { Mod<"collections.abc">().attr("MutableMapping") }, {
                { "__init__", w.method("Shelf", "__init__", { py_positional("dict"), py_positional("protocol", none()), py_positional("writeback", boolean(false)), py_positional("keyencoding", S<"utf-8">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dict = a[1];
                        py protocol = a[2];
                        py writeback = a[3];
                        py keyencoding = a[4];
                        self.set_attr("dict", dict);
                        if (protocol.is_none())
                        {
                            protocol = Mod<"pickle">().attr("DEFAULT_PROTOCOL");
                        }
                        self.set_attr("_protocol", protocol);
                        self.set_attr("writeback", writeback);
                        self.set_attr("cache", py_dict());
                        self.set_attr("keyencoding", keyencoding);
                        return none();
                    }) },
                { "__iter__", w.method("Shelf", "__iter__", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("__iter__", "Shelf.__iter__", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py k;
                            for (py const& item : iterate{ self.attr("dict").method("keys") })
                            {
                                k = item;
                                py_yield(k.method("decode", { self.attr("keyencoding") }));
                            }
                            return none();
                        });
                    }) },
                { "__len__", w.method("Shelf", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("dict").size() };
                    }) },
                { "__contains__", w.method("Shelf", "__contains__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        return boolean(self.attr("dict").contains(key.method("encode", { self.attr("keyencoding") })));
                    }) },
                { "get", w.method("Shelf", "get", { py_positional("key"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py default_ = a[2];
                        if (self.attr("dict").contains(key.method("encode", { self.attr("keyencoding") })))
                        {
                            return self[key];
                        }
                        return default_;
                    }) },
                { "__getitem__", w.method("Shelf", "__getitem__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py f;
                        py value;
                        try
                        {
                            value = self.attr("cache")[key];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, B<"KeyError">()))
                            {
                                f = (Mod<"io">().attr("BytesIO"))(self.attr("dict")[key.method("encode", { self.attr("keyencoding") })]);
                                value = (Mod<"pickle">().attr("Unpickler"))(f).method("load");
                                if (self.attr("writeback").truthy())
                                {
                                    self.attr("cache").set_item(key, value);
                                }
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                        return value;
                    }) },
                { "__setitem__", w.method("Shelf", "__setitem__", { py_positional("key"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py value = a[2];
                        py f;
                        py p;
                        if (self.attr("writeback").truthy())
                        {
                            self.attr("cache").set_item(key, value);
                        }
                        f = (Mod<"io">().attr("BytesIO"))();
                        p = (Mod<"pickle">().attr("Pickler"))(f, self.attr("_protocol"));
                        p.method("dump", { value });
                        self.attr("dict").set_item(key.method("encode", { self.attr("keyencoding") }), f.method("getvalue"));
                        return none();
                    }) },
                { "__delitem__", w.method("Shelf", "__delitem__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        del_item(self.attr("dict"), key.method("encode", { self.attr("keyencoding") }));
                        try
                        {
                            del_item(self.attr("cache"), key);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            if (matches(handler3.exception, B<"KeyError">()))
                            {
                            }
                            else
                                py_reraise(handler3.exception);
                        }
                        return none();
                    }) },
                { "__enter__", w.method("Shelf", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("Shelf", "__exit__", { py_positional("type"), py_positional("value"), py_positional("traceback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py value = a[2];
                        py traceback = a[3];
                        self.method("close");
                        return none();
                    }) },
                { "close", w.method("Shelf", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("dict").is_none())
                        {
                            return none();
                        }
                        {
                            std::optional<py> pending4;
                            [[maybe_unused]] int flow4 = 0;
                            [[maybe_unused]] py result4;
                            try
                            {
                                self.method("sync");
                                try
                                {
                                    self.attr("dict").method("close");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler5;
                                    if (matches(handler5.exception, B<"AttributeError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler5.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending4 = py_take_exception();
                            }
                            finally4:
                            // Catch errors that may happen when close is called from __del__
                            // because CPython is in interpreter shutdown.
                            try
                            {
                                self.set_attr("dict", (G<"shelve._ClosedDict">())());
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                {
                                    self.set_attr("dict", none());
                                }
                            }
                            if (pending4)
                                py_reraise(*pending4);
                            if (flow4 == 1)
                                return result4;
                        }
                        return none();
                    }) },
                { "__del__", w.method("Shelf", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!has_attr(self, "writeback"))
                        {
                            // __init__ didn't succeed, so don't bother closing
                            // see http://bugs.python.org/issue1339007 for details
                            return none();
                        }
                        self.method("close");
                        return none();
                    }) },
                { "sync", w.method("Shelf", "sync", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entry;
                        py key;
                        if ((self.attr("writeback").truthy() && self.attr("cache").truthy()))
                        {
                            self.set_attr("writeback", boolean(false));
                            for (py const& item : iterate{ self.attr("cache").method("items") })
                            {
                                auto const items8 = unpack(item, 2);
                                key = items8[0];
                                entry = items8[1];
                                self.set_item(key, entry);
                            }
                            self.set_attr("writeback", boolean(true));
                            self.set_attr("cache", py_dict());
                        }
                        if (has_attr(self.attr("dict"), "sync"))
                        {
                            self.attr("dict").method("sync");
                        }
                        return none();
                    }) }, }));

            define("shelve", "BsdDbShelf", w.make_class("BsdDbShelf", { G<"shelve.Shelf">() }, {
                { "__init__", w.method("BsdDbShelf", "__init__", { py_positional("dict"), py_positional("protocol", none()), py_positional("writeback", boolean(false)), py_positional("keyencoding", S<"utf-8">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dict = a[1];
                        py protocol = a[2];
                        py writeback = a[3];
                        py keyencoding = a[4];
                        (G<"shelve.Shelf">()).method("__init__", { self, dict, protocol, writeback, keyencoding });
                        return none();
                    }) },
                { "set_location", w.method("BsdDbShelf", "set_location", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py f;
                        py value;
                        auto const items9 = unpack(self.attr("dict").method("set_location", { key }), 2);
                        key = items9[0];
                        value = items9[1];
                        f = (Mod<"io">().attr("BytesIO"))(value);
                        return py_tuple({ key.method("decode", { self.attr("keyencoding") }), (Mod<"pickle">().attr("Unpickler"))(f).method("load") });
                    }) },
                { "next", w.method("BsdDbShelf", "next", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f;
                        py key;
                        py value;
                        auto const items10 = unpack((B<"next">())(self.attr("dict")), 2);
                        key = items10[0];
                        value = items10[1];
                        f = (Mod<"io">().attr("BytesIO"))(value);
                        return py_tuple({ key.method("decode", { self.attr("keyencoding") }), (Mod<"pickle">().attr("Unpickler"))(f).method("load") });
                    }) },
                { "previous", w.method("BsdDbShelf", "previous", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f;
                        py key;
                        py value;
                        auto const items11 = unpack(self.attr("dict").method("previous"), 2);
                        key = items11[0];
                        value = items11[1];
                        f = (Mod<"io">().attr("BytesIO"))(value);
                        return py_tuple({ key.method("decode", { self.attr("keyencoding") }), (Mod<"pickle">().attr("Unpickler"))(f).method("load") });
                    }) },
                { "first", w.method("BsdDbShelf", "first", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f;
                        py key;
                        py value;
                        auto const items12 = unpack(self.attr("dict").method("first"), 2);
                        key = items12[0];
                        value = items12[1];
                        f = (Mod<"io">().attr("BytesIO"))(value);
                        return py_tuple({ key.method("decode", { self.attr("keyencoding") }), (Mod<"pickle">().attr("Unpickler"))(f).method("load") });
                    }) },
                { "last", w.method("BsdDbShelf", "last", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f;
                        py key;
                        py value;
                        auto const items13 = unpack(self.attr("dict").method("last"), 2);
                        key = items13[0];
                        value = items13[1];
                        f = (Mod<"io">().attr("BytesIO"))(value);
                        return py_tuple({ key.method("decode", { self.attr("keyencoding") }), (Mod<"pickle">().attr("Unpickler"))(f).method("load") });
                    }) }, }));

            define("shelve", "DbfilenameShelf", w.make_class("DbfilenameShelf", { G<"shelve.Shelf">() }, {
                { "__init__", w.method("DbfilenameShelf", "__init__", { py_positional("filename"), py_positional("flag", S<"c">()), py_positional("protocol", none()), py_positional("writeback", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py flag = a[2];
                        py protocol = a[3];
                        py writeback = a[4];
                        py dbm;
                        dbm = module_of("dbm");
                        (G<"shelve.Shelf">()).method("__init__", { self, dbm.method("open", { filename, flag }), protocol, writeback });
                        return none();
                    }) },
                { "clear", w.method("DbfilenameShelf", "clear", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        // Call through to the clear method on dbm-backed shelves.
                        // see https://github.com/python/cpython/issues/107089
                        self.attr("cache").method("clear");
                        self.attr("dict").method("clear");
                        return none();
                    }) }, }));

            define("shelve", "open", w.function("open", "open", { py_positional("filename"), py_positional("flag", S<"c">()), py_positional("protocol", none()), py_positional("writeback", boolean(false)) }, [](arguments a) -> py
                {
                    py filename = a[0];
                    py flag = a[1];
                    py protocol = a[2];
                    py writeback = a[3];
                    return (G<"shelve.DbfilenameShelf">())(filename, flag, protocol, writeback);
                }));
        }

        bool shelve_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_shelve();
                }
                if (auto const doc = shelve_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("DEFAULT_PROTOCOL", import_attribute("pickle", "DEFAULT_PROTOCOL").release());
                aAttributes.emplace_back("Pickler", import_attribute("pickle", "Pickler").release());
                aAttributes.emplace_back("Unpickler", import_attribute("pickle", "Unpickler").release());
                aAttributes.emplace_back("BytesIO", import_attribute("io", "BytesIO").release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("shelve", shelve_module);
    }
}
