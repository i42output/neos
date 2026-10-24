/*
  hmac.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/hmac.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module hmac (CPython 3.13's Lib/hmac.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "hmac_docs.hpp"

namespace neos::python_runtime::detail::hmac_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "hmac.name"))
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
                py_raise("SystemError", std::string{ "hmac: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"hmac.name">()))
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
        [[maybe_unused]] void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "hmac", hmac_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- hmac (Lib/hmac.py)

        void make_hmac()
        {
            static module_writer const& w = writer("hmac");
            define("hmac", "_hashopenssl", none());
            define("hmac", "_functype", none());
            define("hmac", "trans_5C", (B<"bytes">())([&]() -> py { py const comprehension1 = py_list(); py x; for (py const& item : iterate{ (B<"range">())(I<256>()) }) { x = item; comprehension1.method("append", { x ^ (I<92>()) }); } return comprehension1; }()));
            define("hmac", "trans_36", (B<"bytes">())([&]() -> py { py const comprehension2 = py_list(); py x; for (py const& item : iterate{ (B<"range">())(I<256>()) }) { x = item; comprehension2.method("append", { x ^ (I<54>()) }); } return comprehension2; }()));
            // The size of the digests returned by HMAC depends on the underlying
            // hashing module used.  Use digest_size from the instance of HMAC instead.
            define("hmac", "digest_size", none());
            define("hmac", "HMAC", w.make_class("HMAC", {  }, {
                { "blocksize", I<64>() },
                { "__slots__", py_tuple({ S<"_hmac">(), S<"_inner">(), S<"_outer">(), S<"block_size">(), S<"digest_size">() }) },
                { "__init__", w.method("HMAC", "__init__", { py_positional("key"), py_positional("msg", none()), py_positional("digestmod", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py msg = a[2];
                        py digestmod = a[3];
                        if (!isinstance(key, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                        {
                            py_raise((B<"TypeError">())((S<"key: expected bytes or bytearray, but got %r">()) % (B<"type">())(key).attr("__name__")));
                        }
                        if (!digestmod.truthy())
                        {
                            py_raise((B<"TypeError">())(S<"Missing required argument 'digestmod'.">()));
                        }
                        if (((G<"hmac._hashopenssl">()).truthy() && isinstance(digestmod, py_tuple({ B<"str">(), G<"hmac._functype">() }))))
                        {
                            try
                            {
                                self.method("_init_hmac", { key, msg, digestmod });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, (G<"hmac._hashopenssl">()).attr("UnsupportedDigestmodError")))
                                {
                                    self.method("_init_old", { key, msg, digestmod });
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                        }
                        else
                        {
                            self.method("_init_old", { key, msg, digestmod });
                        }
                        return none();
                    }) },
                { "_init_hmac", w.method("HMAC", "_init_hmac", { py_positional("key"), py_positional("msg"), py_positional("digestmod") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py msg = a[2];
                        py digestmod = a[3];
                        self.set_attr("_hmac", (G<"hmac._hashopenssl">()).method("hmac_new", { key, msg }, { { "digestmod", digestmod } }));
                        self.set_attr("digest_size", self.attr("_hmac").attr("digest_size"));
                        self.set_attr("block_size", self.attr("_hmac").attr("block_size"));
                        return none();
                    }) },
                { "_init_old", w.method("HMAC", "_init_old", { py_positional("key"), py_positional("msg"), py_positional("digestmod") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py msg = a[2];
                        auto const digestmod = std::make_shared<py>(a[3]);
                        py blocksize;
                        py digest_cons;
                        if (callable((*digestmod)))
                        {
                            digest_cons = (*digestmod);
                        }
                        else if (isinstance((*digestmod), B<"str">()))
                        {
                            digest_cons = w.function("<lambda>", "HMAC._init_old.<locals>.<lambda>", { py_positional("d", py{ make_bytes(std::string{ "", 0 }) }) }, [digestmod](arguments a) -> py
        {
            py d = a[0];
            return (Mod<"hashlib">().attr("new"))((*digestmod), d);
        });
                        }
                        else
                        {
                            digest_cons = w.function("<lambda>", "HMAC._init_old.<locals>.<lambda>", { py_positional("d", py{ make_bytes(std::string{ "", 0 }) }) }, [digestmod](arguments a) -> py
        {
            py d = a[0];
            return (*digestmod).method("new", { d });
        });
                        }
                        self.set_attr("_hmac", none());
                        self.set_attr("_outer", digest_cons());
                        self.set_attr("_inner", digest_cons());
                        self.set_attr("digest_size", self.attr("_inner").attr("digest_size"));
                        if (has_attr(self.attr("_inner"), "block_size"))
                        {
                            blocksize = self.attr("_inner").attr("block_size");
                            if (blocksize < (I<16>()))
                            {
                                (Mod<"warnings">().attr("warn"))((S<"block_size of %d seems too small; using our default of %d.">()) % py_tuple({ blocksize, self.attr("blocksize") }), B<"RuntimeWarning">(), I<2>());
                                blocksize = self.attr("blocksize");
                            }
                        }
                        else
                        {
                            (Mod<"warnings">().attr("warn"))((S<"No block_size attribute on given digest object; Assuming %d.">()) % self.attr("blocksize"), B<"RuntimeWarning">(), I<2>());
                            blocksize = self.attr("blocksize");
                        }
                        if (py{ key.size() } > blocksize)
                        {
                            key = digest_cons(key).method("digest");
                        }
                        // self.blocksize is the default blocksize. self.block_size is
                        // effective block size as well as the public API attribute.
                        self.set_attr("block_size", blocksize);
                        key = key.method("ljust", { blocksize, py{ make_bytes(std::string{ "\x00", 1 }) } });
                        self.attr("_outer").method("update", { key.method("translate", { G<"hmac.trans_5C">() }) });
                        self.attr("_inner").method("update", { key.method("translate", { G<"hmac.trans_36">() }) });
                        if (!msg.is_none())
                        {
                            self.method("update", { msg });
                        }
                        return none();
                    }) },
                { "name", B<"property">()(w.method("HMAC", "name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_hmac").truthy())
                        {
                            return self.attr("_hmac").attr("name");
                        }
                        else
                        {
                            return S(std::string{ "hmac-" } + self.attr("_inner").attr("name").str());
                        }
                        return none();
                    })) },
                { "update", w.method("HMAC", "update", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py inst;
                        inst = [&]() -> py { py const t = self.attr("_hmac"); return t.truthy() ? t : self.attr("_inner"); }();
                        inst.method("update", { msg });
                        return none();
                    }) },
                { "copy", w.method("HMAC", "copy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other;
                        // Call __new__ directly to avoid the expensive __init__.
                        other = self.attr("__class__").method("__new__", { self.attr("__class__") });
                        other.set_attr("digest_size", self.attr("digest_size"));
                        other.set_attr("block_size", self.attr("block_size"));
                        if (self.attr("_hmac").truthy())
                        {
                            other.set_attr("_hmac", self.attr("_hmac").method("copy"));
                            py const value4 = none();
                            other.set_attr("_inner", value4);
                            other.set_attr("_outer", value4);
                        }
                        else
                        {
                            other.set_attr("_hmac", none());
                            other.set_attr("_inner", self.attr("_inner").method("copy"));
                            other.set_attr("_outer", self.attr("_outer").method("copy"));
                        }
                        return other;
                    }) },
                { "_current", w.method("HMAC", "_current", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py h;
                        if (self.attr("_hmac").truthy())
                        {
                            return self.attr("_hmac");
                        }
                        else
                        {
                            h = self.attr("_outer").method("copy");
                            h.method("update", { self.attr("_inner").method("digest") });
                            return h;
                        }
                        return none();
                    }) },
                { "digest", w.method("HMAC", "digest", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py h;
                        h = self.method("_current");
                        return h.method("digest");
                    }) },
                { "hexdigest", w.method("HMAC", "hexdigest", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py h;
                        h = self.method("_current");
                        return h.method("hexdigest");
                    }) }, }));

            define("hmac", "new", w.function("new", "new", { py_positional("key"), py_positional("msg", none()), py_positional("digestmod", S<"">()) }, [](arguments a) -> py
                {
                    py key = a[0];
                    py msg = a[1];
                    py digestmod = a[2];
                    return (G<"hmac.HMAC">())(key, msg, digestmod);
                }));

            define("hmac", "digest", w.function("digest", "digest", { py_positional("key"), py_positional("msg"), py_positional("digest") }, [](arguments a) -> py
                {
                    py key = a[0];
                    py msg = a[1];
                    auto const digest = std::make_shared<py>(a[2]);
                    py blocksize;
                    py digest_cons;
                    py inner;
                    py outer;
                    if (((!(G<"hmac._hashopenssl">()).is_none()) && isinstance((*digest), py_tuple({ B<"str">(), G<"hmac._functype">() }))))
                    {
                        try
                        {
                            return (G<"hmac._hashopenssl">()).method("hmac_digest", { key, msg, (*digest) });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, (G<"hmac._hashopenssl">()).attr("UnsupportedDigestmodError")))
                            {
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                    }
                    if (callable((*digest)))
                    {
                        digest_cons = (*digest);
                    }
                    else if (isinstance((*digest), B<"str">()))
                    {
                        digest_cons = w.function("<lambda>", "digest.<locals>.<lambda>", { py_positional("d", py{ make_bytes(std::string{ "", 0 }) }) }, [digest](arguments a) -> py
        {
            py d = a[0];
            return (Mod<"hashlib">().attr("new"))((*digest), d);
        });
                    }
                    else
                    {
                        digest_cons = w.function("<lambda>", "digest.<locals>.<lambda>", { py_positional("d", py{ make_bytes(std::string{ "", 0 }) }) }, [digest](arguments a) -> py
        {
            py d = a[0];
            return (*digest).method("new", { d });
        });
                    }
                    inner = digest_cons();
                    outer = digest_cons();
                    blocksize = (B<"getattr">())(inner, S<"block_size">(), I<64>());
                    if (py{ key.size() } > blocksize)
                    {
                        key = digest_cons(key).method("digest");
                    }
                    key = key + (py{ make_bytes(std::string{ "\x00", 1 }) } * (blocksize - py{ key.size() }));
                    inner.method("update", { key.method("translate", { G<"hmac.trans_36">() }) });
                    outer.method("update", { key.method("translate", { G<"hmac.trans_5C">() }) });
                    inner.method("update", { msg });
                    outer.method("update", { inner.method("digest") });
                    return outer.method("digest");
                }));
        }

        bool hmac_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_hmac();
                }
                if (auto const doc = hmac_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("_warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("compare_digest", import_attribute("_operator", "_compare_digest").release());
                aAttributes.emplace_back("_hashlib", py{ fn_import("hashlib") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("hmac", hmac_module);
    }
}
