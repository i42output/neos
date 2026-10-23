/*
  unittest_util.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/util.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/unittest/util.py's copyright: Copyright (c) 1999-2003 Steve Purcell;
  Copyright (c) 2003-2010 Python Software Foundation (see runtime/python/NOTICE).
*/

// The module unittest.util (CPython 3.13's Lib/unittest/util.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "unittest_util_docs.hpp"

namespace neos::python_runtime::detail::unittest_util_module
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.util.name"))
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
                py_raise("SystemError", std::string{ "unittest.util: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.util.name">()))
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
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "unittest.util", unittest_util_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.util (Lib/unittest/util.py)

        void make_unittest_util()
        {
            static module_writer const& w = writer("unittest.util");
            define("unittest.util", "__unittest", boolean(true));
            define("unittest.util", "_MAX_LENGTH", I<80>());
            define("unittest.util", "_PLACEHOLDER_LEN", I<12>());
            define("unittest.util", "_MIN_BEGIN_LEN", I<5>());
            define("unittest.util", "_MIN_END_LEN", I<5>());
            define("unittest.util", "_MIN_COMMON_LEN", I<5>());
            define("unittest.util", "_MIN_DIFF_LEN", (G<"unittest.util._MAX_LENGTH">()) - (((((G<"unittest.util._MIN_BEGIN_LEN">()) + (G<"unittest.util._PLACEHOLDER_LEN">())) + (G<"unittest.util._MIN_COMMON_LEN">())) + (G<"unittest.util._PLACEHOLDER_LEN">())) + (G<"unittest.util._MIN_END_LEN">())));
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    if (!((G<"unittest.util._MIN_DIFF_LEN">()) >= (I<0>())))
                        py_raise(B<"AssertionError">()());
                    return none();
                }))();
            define("unittest.util", "_shorten", w.function("_shorten", "_shorten", { py_positional("s"), py_positional("prefixlen"), py_positional("suffixlen") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py prefixlen = a[1];
                    py suffixlen = a[2];
                    py skip;
                    skip = (py{ s.size() } - prefixlen) - suffixlen;
                    if (skip > (G<"unittest.util._PLACEHOLDER_LEN">()))
                    {
                        s = (S<"%s[%d chars]%s">()) % py_tuple({ s[B<"slice">()(none(), prefixlen, none())], skip, s[B<"slice">()(py{ s.size() } - suffixlen, none(), none())] });
                    }
                    return s;
                }));

            define("unittest.util", "_common_shorten_repr", w.function("_common_shorten_repr", "_common_shorten_repr", {  }, [](arguments a) -> py
                {
                    py args = a[0];
                    py common_len;
                    py maxlen;
                    py prefix;
                    py prefixlen;
                    args = (B<"tuple">())((B<"map">())(G<"unittest.util.safe_repr">(), args));
                    maxlen = (B<"max">())((B<"map">())(B<"len">(), args));
                    if (maxlen <= (G<"unittest.util._MAX_LENGTH">()))
                    {
                        return args;
                    }
                    prefix = (Mod<"os.path">().attr("commonprefix"))(args);
                    prefixlen = py{ prefix.size() };
                    common_len = (G<"unittest.util._MAX_LENGTH">()) - (((maxlen - prefixlen) + (G<"unittest.util._MIN_BEGIN_LEN">())) + (G<"unittest.util._PLACEHOLDER_LEN">()));
                    if (common_len > (G<"unittest.util._MIN_COMMON_LEN">()))
                    {
                        if (!(((((G<"unittest.util._MIN_BEGIN_LEN">()) + (G<"unittest.util._PLACEHOLDER_LEN">())) + (G<"unittest.util._MIN_COMMON_LEN">())) + (maxlen - prefixlen)) < (G<"unittest.util._MAX_LENGTH">())))
                            py_raise(B<"AssertionError">()());
                        prefix = (G<"unittest.util._shorten">())(prefix, G<"unittest.util._MIN_BEGIN_LEN">(), common_len);
                        return (B<"tuple">())([&]() -> py { py const comprehension1 = py_list(); py s; for (py const& item : iterate{ args }) { s = item; comprehension1.method("append", { prefix + s[B<"slice">()(prefixlen, none(), none())] }); } return comprehension1; }());
                    }
                    prefix = (G<"unittest.util._shorten">())(prefix, G<"unittest.util._MIN_BEGIN_LEN">(), G<"unittest.util._MIN_COMMON_LEN">());
                    return (B<"tuple">())([&]() -> py { py const comprehension2 = py_list(); py s; for (py const& item : iterate{ args }) { s = item; comprehension2.method("append", { prefix + (G<"unittest.util._shorten">())(s[B<"slice">()(prefixlen, none(), none())], G<"unittest.util._MIN_DIFF_LEN">(), G<"unittest.util._MIN_END_LEN">()) }); } return comprehension2; }());
                }, true));

            define("unittest.util", "safe_repr", w.function("safe_repr", "safe_repr", { py_positional("obj"), py_positional("short", boolean(false)) }, [](arguments a) -> py
                {
                    py obj = a[0];
                    py short_ = a[1];
                    py result;
                    try
                    {
                        result = S(obj.repr());
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler3;
                        if (matches(handler3.exception, B<"Exception">()))
                        {
                            result = (B<"object">()).method("__repr__", { obj });
                        }
                        else
                            py_reraise(handler3.exception);
                    }
                    if (((!short_.truthy()) || (py{ result.size() } < (G<"unittest.util._MAX_LENGTH">()))))
                    {
                        return result;
                    }
                    return result[B<"slice">()(none(), G<"unittest.util._MAX_LENGTH">(), none())] + (S<" [truncated]...">());
                }));

            define("unittest.util", "strclass", w.function("strclass", "strclass", { py_positional("cls") }, [](arguments a) -> py
                {
                    py cls = a[0];
                    return (S<"%s.%s">()) % py_tuple({ cls.attr("__module__"), cls.attr("__qualname__") });
                }));

            define("unittest.util", "sorted_list_difference", w.function("sorted_list_difference", "sorted_list_difference", { py_positional("expected"), py_positional("actual") }, [](arguments a) -> py
                {
                    py expected = a[0];
                    py actual = a[1];
                    py a_;
                    py e;
                    py i;
                    py j;
                    py missing;
                    py unexpected;
                    py const value4 = I<0>();
                    i = value4;
                    j = value4;
                    missing = py_list();
                    unexpected = py_list();
                    while (boolean(true).truthy())
                    {
                        try
                        {
                            e = expected[i];
                            a_ = actual[j];
                            if (e < a_)
                            {
                                missing.method("append", { e });
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                                while (expected[i] == e)
                                {
                                    i = inplace(NEOS_PY_ADD, i, I<1>());
                                }
                            }
                            else if (e > a_)
                            {
                                unexpected.method("append", { a_ });
                                j = inplace(NEOS_PY_ADD, j, I<1>());
                                while (actual[j] == a_)
                                {
                                    j = inplace(NEOS_PY_ADD, j, I<1>());
                                }
                            }
                            else
                            {
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                                {
                                    std::optional<py> pending9;
                                    [[maybe_unused]] int flow9 = 0;
                                    [[maybe_unused]] py result9;
                                    try
                                    {
                                        while (expected[i] == e)
                                        {
                                            i = inplace(NEOS_PY_ADD, i, I<1>());
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending9 = py_take_exception();
                                    }
                                    finally9:
                                    j = inplace(NEOS_PY_ADD, j, I<1>());
                                    while (actual[j] == a_)
                                    {
                                        j = inplace(NEOS_PY_ADD, j, I<1>());
                                    }
                                    if (pending9)
                                        py_reraise(*pending9);
                                    if (flow9 == 1)
                                        return result9;
                                    if (flow9 == 2)
                                        break;
                                    if (flow9 == 3)
                                        continue;
                                }
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"IndexError">()))
                            {
                                missing.method("extend", { expected[B<"slice">()(i, none(), none())] });
                                unexpected.method("extend", { actual[B<"slice">()(j, none(), none())] });
                                break;
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                    }
                    return py_tuple({ missing, unexpected });
                }));

            define("unittest.util", "unorderable_list_difference", w.function("unorderable_list_difference", "unorderable_list_difference", { py_positional("expected"), py_positional("actual") }, [](arguments a) -> py
                {
                    py expected = a[0];
                    py actual = a[1];
                    py item_;
                    py missing;
                    missing = py_list();
                    while (expected.truthy())
                    {
                        item_ = expected.method("pop");
                        try
                        {
                            actual.method("remove", { item_ });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler13;
                            if (matches(handler13.exception, B<"ValueError">()))
                            {
                                missing.method("append", { item_ });
                            }
                            else
                                py_reraise(handler13.exception);
                        }
                    }
                    // anything left in actual is unexpected
                    return py_tuple({ missing, actual });
                }));

            define("unittest.util", "three_way_cmp", w.function("three_way_cmp", "three_way_cmp", { py_positional("x"), py_positional("y") }, [](arguments a) -> py
                {
                    py x = a[0];
                    py y = a[1];
                    return boolean(x > y) - boolean(x < y);
                }));

            define("unittest.util", "_Mismatch", (Mod<"collections">().attr("namedtuple"))(S<"Mismatch">(), S<"actual expected value">()));
            define("unittest.util", "_count_diff_all_purpose", w.function("_count_diff_all_purpose", "_count_diff_all_purpose", { py_positional("actual"), py_positional("expected") }, [](arguments a) -> py
                {
                    py actual = a[0];
                    py expected = a[1];
                    py NULL_;
                    py cnt_s;
                    py cnt_t;
                    py diff;
                    py elem;
                    py i;
                    py j;
                    py m;
                    py n;
                    py other_elem;
                    py result;
                    py s;
                    py t;
                    // elements need not be hashable
                    auto const items14 = unpack(py_tuple({ (B<"list">())(actual), (B<"list">())(expected) }), 2);
                    s = items14[0];
                    t = items14[1];
                    auto const items15 = unpack(py_tuple({ py{ s.size() }, py{ t.size() } }), 2);
                    m = items15[0];
                    n = items15[1];
                    NULL_ = (B<"object">())();
                    result = py_list();
                    for (py const& item : iterate{ (B<"enumerate">())(s) })
                    {
                        auto const items17 = unpack(item, 2);
                        i = items17[0];
                        elem = items17[1];
                        if (elem.is(NULL_))
                        {
                            continue;
                        }
                        py const value18 = I<0>();
                        cnt_s = value18;
                        cnt_t = value18;
                        for (py const& item : iterate{ (B<"range">())(i, m) })
                        {
                            j = item;
                            if (s[j] == elem)
                            {
                                cnt_s = inplace(NEOS_PY_ADD, cnt_s, I<1>());
                                s.set_item(j, NULL_);
                            }
                        }
                        for (py const& item : iterate{ (B<"enumerate">())(t) })
                        {
                            auto const items21 = unpack(item, 2);
                            j = items21[0];
                            other_elem = items21[1];
                            if (other_elem == elem)
                            {
                                cnt_t = inplace(NEOS_PY_ADD, cnt_t, I<1>());
                                t.set_item(j, NULL_);
                            }
                        }
                        if (cnt_s != cnt_t)
                        {
                            diff = (G<"unittest.util._Mismatch">())(cnt_s, cnt_t, elem);
                            result.method("append", { diff });
                        }
                    }
                    for (py const& item : iterate{ (B<"enumerate">())(t) })
                    {
                        auto const items23 = unpack(item, 2);
                        i = items23[0];
                        elem = items23[1];
                        if (elem.is(NULL_))
                        {
                            continue;
                        }
                        cnt_t = I<0>();
                        for (py const& item : iterate{ (B<"range">())(i, n) })
                        {
                            j = item;
                            if (t[j] == elem)
                            {
                                cnt_t = inplace(NEOS_PY_ADD, cnt_t, I<1>());
                                t.set_item(j, NULL_);
                            }
                        }
                        diff = (G<"unittest.util._Mismatch">())(I<0>(), cnt_t, elem);
                        result.method("append", { diff });
                    }
                    return result;
                }));

            define("unittest.util", "_count_diff_hashable", w.function("_count_diff_hashable", "_count_diff_hashable", { py_positional("actual"), py_positional("expected") }, [](arguments a) -> py
                {
                    py actual = a[0];
                    py expected = a[1];
                    py cnt_s;
                    py cnt_t;
                    py diff;
                    py elem;
                    py result;
                    py s;
                    py t;
                    // elements must be hashable
                    auto const items25 = unpack(py_tuple({ (Mod<"collections">().attr("Counter"))(actual), (Mod<"collections">().attr("Counter"))(expected) }), 2);
                    s = items25[0];
                    t = items25[1];
                    result = py_list();
                    for (py const& item : iterate{ s.method("items") })
                    {
                        auto const items27 = unpack(item, 2);
                        elem = items27[0];
                        cnt_s = items27[1];
                        cnt_t = t.method("get", { elem, I<0>() });
                        if (cnt_s != cnt_t)
                        {
                            diff = (G<"unittest.util._Mismatch">())(cnt_s, cnt_t, elem);
                            result.method("append", { diff });
                        }
                    }
                    for (py const& item : iterate{ t.method("items") })
                    {
                        auto const items29 = unpack(item, 2);
                        elem = items29[0];
                        cnt_t = items29[1];
                        if (!s.contains(elem))
                        {
                            diff = (G<"unittest.util._Mismatch">())(I<0>(), cnt_t, elem);
                            result.method("append", { diff });
                        }
                    }
                    return result;
                }));
        }

        bool unittest_util_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_util();
                }
                if (auto const doc = unittest_util_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("namedtuple", import_attribute("collections", "namedtuple").release());
                aAttributes.emplace_back("Counter", import_attribute("collections", "Counter").release());
                aAttributes.emplace_back("commonprefix", import_attribute("os.path", "commonprefix").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.util", unittest_util_module);
    }
}
