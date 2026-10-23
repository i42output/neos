/*
  unittest_mock.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/unittest/mock.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module unittest.mock (CPython 3.13's Lib/unittest/mock.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "asyncio.hpp"
#include "unittest_mock_docs.hpp"

namespace neos::python_runtime::detail::unittest_mock_module
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
        using asyncio::async_method;
        using asyncio::async_function;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "unittest.mock.name"))
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
                py_raise("SystemError", std::string{ "unittest.mock: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"unittest.mock.name">()))
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
            static module_writer const sWriter{ "unittest.mock", unittest_mock_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- unittest.mock (Lib/unittest/mock.py)

        void make_unittest_mock()
        {
            static module_writer const& w = writer("unittest.mock");
            // mock.py
            // Test tools for mocking and patching.
            // Maintained by Michael Foord
            // Backport for other versions of Python available from
            // https://pypi.org/project/mock
            define("unittest.mock", "__all__", py_tuple({ S<"Mock">(), S<"MagicMock">(), S<"patch">(), S<"sentinel">(), S<"DEFAULT">(), S<"ANY">(), S<"call">(), S<"create_autospec">(), S<"AsyncMock">(), S<"ThreadingMock">(), S<"FILTER_DIR">(), S<"NonCallableMock">(), S<"NonCallableMagicMock">(), S<"mock_open">(), S<"PropertyMock">(), S<"seal">() }));
            define("unittest.mock", "InvalidSpecError", w.make_class("InvalidSpecError", { B<"Exception">() }, {
 }));

            define("unittest.mock", "_builtins", [&]() -> py { py const comprehension1 = B<"set">()(); py name; for (py const& item : iterate{ (B<"dir">())(Mod<"builtins">()) }) { name = item; if (name.method("startswith", { S<"_">() }).truthy()) continue; comprehension1.method("add", { name }); } return comprehension1; }());
            define("unittest.mock", "FILTER_DIR", boolean(true));
            // Workaround for issue #12370
            // Without this, the __class__ properties wouldn't be set correctly
            define("unittest.mock", "_safe_super", B<"super">());
            define("unittest.mock", "_is_async_obj", w.function("_is_async_obj", "_is_async_obj", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    if (((G<"unittest.mock._is_instance_mock">())(obj).truthy() && (!isinstance(obj, G<"unittest.mock.AsyncMock">()))))
                    {
                        return boolean(false);
                    }
                    if (has_attr(obj, "__func__"))
                    {
                        obj = (B<"getattr">())(obj, S<"__func__">());
                    }
                    return [&]() -> py { py const t = (Mod<"asyncio">().attr("iscoroutinefunction"))(obj); return t.truthy() ? t : (Mod<"inspect">().attr("isawaitable"))(obj); }();
                }));

            define("unittest.mock", "_is_async_func", w.function("_is_async_func", "_is_async_func", { py_positional("func") }, [](arguments a) -> py
                {
                    py func = a[0];
                    if ((B<"getattr">())(func, S<"__code__">(), none()).truthy())
                    {
                        return (Mod<"asyncio">().attr("iscoroutinefunction"))(func);
                    }
                    else
                    {
                        return boolean(false);
                    }
                    return none();
                }));

            define("unittest.mock", "_is_instance_mock", w.function("_is_instance_mock", "_is_instance_mock", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    return boolean(issubclass((B<"type">())(obj), G<"unittest.mock.NonCallableMock">()));
                }));

            define("unittest.mock", "_is_exception", w.function("_is_exception", "_is_exception", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    return [&]() -> py { py const t = boolean(isinstance(obj, B<"BaseException">())); return t.truthy() ? t : [&]() -> py { py const t = boolean(isinstance(obj, B<"type">())); return !t.truthy() ? t : boolean(issubclass(obj, B<"BaseException">())); }(); }();
                }));

            define("unittest.mock", "_extract_mock", w.function("_extract_mock", "_extract_mock", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    if ((isinstance(obj, G<"unittest.mock.FunctionTypes">()) && has_attr(obj, "mock")))
                    {
                        return obj.attr("mock");
                    }
                    else
                    {
                        return obj;
                    }
                    return none();
                }));

            define("unittest.mock", "_get_signature_object", w.function("_get_signature_object", "_get_signature_object", { py_positional("func"), py_positional("as_instance"), py_positional("eat_self") }, [](arguments a) -> py
                {
                    py func = a[0];
                    py as_instance = a[1];
                    py eat_self = a[2];
                    py sig_func;
                    if ((isinstance(func, B<"type">()) && (!as_instance.truthy())))
                    {
                        // If it's a type and should be modelled as a type, use __init__.
                        func = func.attr("__init__");
                        // Skip the `self` argument in __init__
                        eat_self = boolean(true);
                    }
                    else if (isinstance(func, py_tuple({ B<"classmethod">(), B<"staticmethod">() })))
                    {
                        if (isinstance(func, B<"classmethod">()))
                        {
                            // Skip the `cls` argument of a class method
                            eat_self = boolean(true);
                        }
                        // Use the original decorated method to extract the correct function signature
                        func = func.attr("__func__");
                    }
                    else if (!isinstance(func, G<"unittest.mock.FunctionTypes">()))
                    {
                        // If we really want to model an instance of the passed type,
                        // __call__ should be looked up, not __init__.
                        try
                        {
                            func = func.attr("__call__");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, B<"AttributeError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                    }
                    if (eat_self.truthy())
                    {
                        sig_func = (Mod<"functools">().attr("partial"))(func, none());
                    }
                    else
                    {
                        sig_func = func;
                    }
                    try
                    {
                        return py_tuple({ func, (Mod<"inspect">().attr("signature"))(sig_func) });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler3;
                        if (matches(handler3.exception, B<"ValueError">()))
                        {
                            // Certain callable types are not supported by inspect.signature()
                            return none();
                        }
                        else
                            py_reraise(handler3.exception);
                    }
                    return none();
                }));

            define("unittest.mock", "_check_signature", w.function("_check_signature", "_check_signature", { py_positional("func"), py_positional("mock"), py_positional("skipfirst"), py_positional("instance", boolean(false)) }, [](arguments a) -> py
                {
                    py func = a[0];
                    py mock = a[1];
                    py skipfirst = a[2];
                    py instance = a[3];
                    py checksig;
                    auto const sig = std::make_shared<py>();
                    *sig = (G<"unittest.mock._get_signature_object">())(func, instance, skipfirst);
                    if ((*sig).is_none())
                    {
                        return none();
                    }
                    auto const items4 = unpack((*sig), 2);
                    func = items4[0];
                    *sig = items4[1];
                    checksig = w.function("checksig", "_check_signature.<locals>.checksig", { py_positional_only("self") }, [sig](arguments a) -> py
    {
        py self = a[0];
        py args = a[1];
        py kwargs = a[2];
        call_ex((*sig).attr("bind"), {  }, args, kwargs);
        return none();
    }, true, true);
                    (G<"unittest.mock._copy_func_details">())(func, checksig);
                    (B<"type">())(mock).set_attr("_mock_check_sig", checksig);
                    (B<"type">())(mock).set_attr("__signature__", (*sig));
                    return none();
                }));

            define("unittest.mock", "_copy_func_details", w.function("_copy_func_details", "_copy_func_details", { py_positional("func"), py_positional("funcopy") }, [](arguments a) -> py
                {
                    py func = a[0];
                    py funcopy = a[1];
                    py attribute;
                    for (py const& item : iterate{ py_tuple({ S<"__name__">(), S<"__doc__">(), S<"__text_signature__">(), S<"__module__">(), S<"__defaults__">(), S<"__kwdefaults__">() }) })
                    {
                        attribute = item;
                        try
                        {
                            (B<"setattr">())(funcopy, attribute, (B<"getattr">())(func, attribute));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"AttributeError">()))
                            {
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                    }
                    return none();
                }));

            define("unittest.mock", "_callable", w.function("_callable", "_callable", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    if (isinstance(obj, B<"type">()))
                    {
                        return boolean(true);
                    }
                    if (isinstance(obj, py_tuple({ B<"staticmethod">(), B<"classmethod">(), Mod<"types">().attr("MethodType") })))
                    {
                        return (G<"unittest.mock._callable">())(obj.attr("__func__"));
                    }
                    if (!(B<"getattr">())(obj, S<"__call__">(), none()).is_none())
                    {
                        return boolean(true);
                    }
                    return boolean(false);
                }));

            define("unittest.mock", "_is_list", w.function("_is_list", "_is_list", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    return boolean(py_tuple({ B<"list">(), B<"tuple">() }).contains((B<"type">())(obj)));
                }));

            define("unittest.mock", "_instance_callable", w.function("_instance_callable", "_instance_callable", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    py base;
                    if (!isinstance(obj, B<"type">()))
                    {
                        // already an instance
                        return boolean(!(B<"getattr">())(obj, S<"__call__">(), none()).is_none());
                    }
                    // *could* be broken by a class overriding __mro__ or __dict__ via
                    // a metaclass
                    for (py const& item : iterate{ py_tuple({ obj }) + obj.attr("__mro__") })
                    {
                        base = item;
                        if (!base.attr("__dict__").method("get", { S<"__call__">() }).is_none())
                        {
                            return boolean(true);
                        }
                    }
                    return boolean(false);
                }));

            define("unittest.mock", "_set_signature", w.function("_set_signature", "_set_signature", { py_positional("mock"), py_positional("original"), py_positional("instance", boolean(false)) }, [](arguments a) -> py
                {
                    py mock = a[0];
                    py original = a[1];
                    py instance = a[2];
                    py checksig;
                    py context;
                    py func;
                    py funcopy;
                    py name;
                    py result;
                    auto const sig = std::make_shared<py>();
                    py skipfirst;
                    py src;
                    skipfirst = boolean(isinstance(original, B<"type">()));
                    result = (G<"unittest.mock._get_signature_object">())(original, instance, skipfirst);
                    if (result.is_none())
                    {
                        return mock;
                    }
                    auto const items8 = unpack(result, 2);
                    func = items8[0];
                    *sig = items8[1];
                    checksig = w.function("checksig", "_set_signature.<locals>.checksig", {  }, [sig](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        call_ex((*sig).attr("bind"), {  }, args, kwargs);
        return none();
    }, true, true);
                    (G<"unittest.mock._copy_func_details">())(func, checksig);
                    name = original.attr("__name__");
                    if (!name.method("isidentifier").truthy())
                    {
                        name = S<"funcopy">();
                    }
                    context = dict_of({ { S<"_checksig_">(), checksig }, { S<"mock">(), mock } });
                    src = (S<"def %s(*args, **kwargs):\n    _checksig_(*args, **kwargs)\n    return mock(*args, **kwargs)">()) % name;
                    (B<"exec">())(src, context);
                    funcopy = context[name];
                    (G<"unittest.mock._setup_func">())(funcopy, mock, (*sig));
                    return funcopy;
                }));

            define("unittest.mock", "_set_async_signature", w.function("_set_async_signature", "_set_async_signature", { py_positional("mock"), py_positional("original"), py_positional("instance", boolean(false)), py_positional("is_async_mock", boolean(false)) }, [](arguments a) -> py
                {
                    py mock = a[0];
                    py original = a[1];
                    py instance = a[2];
                    py is_async_mock = a[3];
                    py checksig;
                    py context;
                    py func;
                    py funcopy;
                    py name;
                    auto const sig = std::make_shared<py>();
                    py skipfirst;
                    py src;
                    skipfirst = boolean(isinstance(original, B<"type">()));
                    auto const items9 = unpack((G<"unittest.mock._get_signature_object">())(original, instance, skipfirst), 2);
                    func = items9[0];
                    *sig = items9[1];
                    checksig = w.function("checksig", "_set_async_signature.<locals>.checksig", {  }, [sig](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        call_ex((*sig).attr("bind"), {  }, args, kwargs);
        return none();
    }, true, true);
                    (G<"unittest.mock._copy_func_details">())(func, checksig);
                    name = original.attr("__name__");
                    context = dict_of({ { S<"_checksig_">(), checksig }, { S<"mock">(), mock } });
                    src = (S<"async def %s(*args, **kwargs):\n    _checksig_(*args, **kwargs)\n    return await mock(*args, **kwargs)">()) % name;
                    (B<"exec">())(src, context);
                    funcopy = context[name];
                    (G<"unittest.mock._setup_func">())(funcopy, mock, (*sig));
                    (G<"unittest.mock._setup_async_mock">())(funcopy);
                    return funcopy;
                }));

            define("unittest.mock", "_setup_func", w.function("_setup_func", "_setup_func", { py_positional("funcopy"), py_positional("mock"), py_positional("sig") }, [](arguments a) -> py
                {
                    auto const funcopy = std::make_shared<py>(a[0]);
                    auto const mock = std::make_shared<py>(a[1]);
                    py sig = a[2];
                    py assert_any_call;
                    py assert_called;
                    py assert_called_once;
                    py assert_called_once_with;
                    py assert_called_with;
                    py assert_has_calls;
                    py assert_not_called;
                    py reset_mock;
                    (*funcopy).set_attr("mock", (*mock));
                    assert_called_with = w.function("assert_called_with", "_setup_func.<locals>.assert_called_with", {  }, [mock](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        return call_ex((*mock).attr("assert_called_with"), {  }, args, kwargs);
    }, true, true);
                    assert_called = w.function("assert_called", "_setup_func.<locals>.assert_called", {  }, [mock](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        return call_ex((*mock).attr("assert_called"), {  }, args, kwargs);
    }, true, true);
                    assert_not_called = w.function("assert_not_called", "_setup_func.<locals>.assert_not_called", {  }, [mock](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        return call_ex((*mock).attr("assert_not_called"), {  }, args, kwargs);
    }, true, true);
                    assert_called_once = w.function("assert_called_once", "_setup_func.<locals>.assert_called_once", {  }, [mock](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        return call_ex((*mock).attr("assert_called_once"), {  }, args, kwargs);
    }, true, true);
                    assert_called_once_with = w.function("assert_called_once_with", "_setup_func.<locals>.assert_called_once_with", {  }, [mock](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        return call_ex((*mock).attr("assert_called_once_with"), {  }, args, kwargs);
    }, true, true);
                    assert_has_calls = w.function("assert_has_calls", "_setup_func.<locals>.assert_has_calls", {  }, [mock](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        return call_ex((*mock).attr("assert_has_calls"), {  }, args, kwargs);
    }, true, true);
                    assert_any_call = w.function("assert_any_call", "_setup_func.<locals>.assert_any_call", {  }, [mock](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        return call_ex((*mock).attr("assert_any_call"), {  }, args, kwargs);
    }, true, true);
                    reset_mock = w.function("reset_mock", "_setup_func.<locals>.reset_mock", {  }, [funcopy, mock](arguments a) -> py
    {
        py ret;
        (*funcopy).set_attr("method_calls", (G<"unittest.mock._CallList">())());
        (*funcopy).set_attr("mock_calls", (G<"unittest.mock._CallList">())());
        (*mock).method("reset_mock");
        ret = (*funcopy).attr("return_value");
        if (((G<"unittest.mock._is_instance_mock">())(ret).truthy() && (!ret.is((*mock)))))
        {
            ret.method("reset_mock");
        }
        return none();
    });
                    (*funcopy).set_attr("called", boolean(false));
                    (*funcopy).set_attr("call_count", I<0>());
                    (*funcopy).set_attr("call_args", none());
                    (*funcopy).set_attr("call_args_list", (G<"unittest.mock._CallList">())());
                    (*funcopy).set_attr("method_calls", (G<"unittest.mock._CallList">())());
                    (*funcopy).set_attr("mock_calls", (G<"unittest.mock._CallList">())());
                    (*funcopy).set_attr("return_value", (*mock).attr("return_value"));
                    (*funcopy).set_attr("side_effect", (*mock).attr("side_effect"));
                    (*funcopy).set_attr("_mock_children", (*mock).attr("_mock_children"));
                    (*funcopy).set_attr("assert_called_with", assert_called_with);
                    (*funcopy).set_attr("assert_called_once_with", assert_called_once_with);
                    (*funcopy).set_attr("assert_has_calls", assert_has_calls);
                    (*funcopy).set_attr("assert_any_call", assert_any_call);
                    (*funcopy).set_attr("reset_mock", reset_mock);
                    (*funcopy).set_attr("assert_called", assert_called);
                    (*funcopy).set_attr("assert_not_called", assert_not_called);
                    (*funcopy).set_attr("assert_called_once", assert_called_once);
                    (*funcopy).set_attr("__signature__", sig);
                    (*mock).set_attr("_mock_delegate", (*funcopy));
                    return none();
                }));

            define("unittest.mock", "_setup_async_mock", w.function("_setup_async_mock", "_setup_async_mock", { py_positional("mock") }, [](arguments a) -> py
                {
                    auto const mock = std::make_shared<py>(a[0]);
                    py attribute;
                    py wrapper;
                    (*mock).set_attr("_is_coroutine", (Mod<"asyncio">().attr("coroutines")).attr("_is_coroutine"));
                    (*mock).set_attr("await_count", I<0>());
                    (*mock).set_attr("await_args", none());
                    (*mock).set_attr("await_args_list", (G<"unittest.mock._CallList">())());
                    // Mock is not configured yet so the attributes are set
                    // to a function and then the corresponding mock helper function
                    // is called when the helper is accessed similar to _setup_func.
                    wrapper = w.function("wrapper", "_setup_async_mock.<locals>.wrapper", { py_positional_only("attr") }, [mock](arguments a) -> py
    {
        py attr = a[0];
        py args = a[1];
        py kwargs = a[2];
        return call_ex((B<"getattr">())((*mock).attr("mock"), attr), {  }, args, kwargs);
    }, true, true);
                    for (py const& item : iterate{ py_tuple({ S<"assert_awaited">(), S<"assert_awaited_once">(), S<"assert_awaited_with">(), S<"assert_awaited_once_with">(), S<"assert_any_await">(), S<"assert_has_awaits">(), S<"assert_not_awaited">() }) })
                    {
                        attribute = item;
                        // setattr(mock, attribute, wrapper) causes late binding
                        // hence attribute will always be the last value in the loop
                        // Use partial(wrapper, attribute) to ensure the attribute is bound
                        // correctly.
                        (B<"setattr">())((*mock), attribute, (Mod<"functools">().attr("partial"))(wrapper, attribute));
                    }
                    return none();
                }));

            define("unittest.mock", "_is_magic", w.function("_is_magic", "_is_magic", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    return boolean(((S<"__%s__">()) % name[B<"slice">()(I<2>(), I<-2>(), none())]) == name);
                }));

            define("unittest.mock", "_SentinelObject", w.make_class("_SentinelObject", { B<"object">() }, {
                { "__init__", w.method("_SentinelObject", "__init__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.set_attr("name", name);
                        return none();
                    }) },
                { "__repr__", w.method("_SentinelObject", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"sentinel.%s">()) % self.attr("name");
                    }) },
                { "__reduce__", w.method("_SentinelObject", "__reduce__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"sentinel.%s">()) % self.attr("name");
                    }) }, }));

            define("unittest.mock", "_Sentinel", w.make_class("_Sentinel", { B<"object">() }, {
                { "__init__", w.method("_Sentinel", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_sentinels", py_dict());
                        return none();
                    }) },
                { "__getattr__", w.method("_Sentinel", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (name == (S<"__bases__">()))
                        {
                            // Without this help(unittest.mock) raises an exception
                            py_raise(B<"AttributeError">());
                        }
                        return self.attr("_sentinels").method("setdefault", { name, (G<"unittest.mock._SentinelObject">())(name) });
                    }) },
                { "__reduce__", w.method("_Sentinel", "__reduce__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"sentinel">();
                    }) }, }));

            define("unittest.mock", "sentinel", (G<"unittest.mock._Sentinel">())());
            define("unittest.mock", "DEFAULT", (G<"unittest.mock.sentinel">()).attr("DEFAULT"));
            define("unittest.mock", "_missing", (G<"unittest.mock.sentinel">()).attr("MISSING"));
            define("unittest.mock", "_deleted", (G<"unittest.mock.sentinel">()).attr("DELETED"));
            define("unittest.mock", "_allowed_names", set_of({ S<"return_value">(), S<"_mock_return_value">(), S<"side_effect">(), S<"_mock_side_effect">(), S<"_mock_parent">(), S<"_mock_new_parent">(), S<"_mock_name">(), S<"_mock_new_name">() }));
            define("unittest.mock", "_delegating_property", w.function("_delegating_property", "_delegating_property", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py _get;
                    py _set;
                    py _the_name;
                    (G<"unittest.mock._allowed_names">()).method("add", { name });
                    _the_name = (S<"_mock_">()) + name;
                    _get = w.function("_get", "_delegating_property.<locals>._get", { py_positional("self"), py_positional("name", name), py_positional("_the_name", _the_name) }, [](arguments a) -> py
    {
        py self = a[0];
        py name = a[1];
        py _the_name = a[2];
        py sig;
        sig = self.attr("_mock_delegate");
        if (sig.is_none())
        {
            return (B<"getattr">())(self, _the_name);
        }
        return (B<"getattr">())(sig, name);
    });
                    _set = w.function("_set", "_delegating_property.<locals>._set", { py_positional("self"), py_positional("value"), py_positional("name", name), py_positional("_the_name", _the_name) }, [](arguments a) -> py
    {
        py self = a[0];
        py value = a[1];
        py name = a[2];
        py _the_name = a[3];
        py sig;
        sig = self.attr("_mock_delegate");
        if (sig.is_none())
        {
            self.attr("__dict__").set_item(_the_name, value);
        }
        else
        {
            (B<"setattr">())(sig, name, value);
        }
        return none();
    });
                    return (B<"property">())(_get, _set);
                }));

            define("unittest.mock", "_CallList", w.make_class("_CallList", { B<"list">() }, {
                { "__contains__", w.method("_CallList", "__contains__", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py i;
                        py len_self;
                        py len_value;
                        py sub_list;
                        if (!isinstance(value, B<"list">()))
                        {
                            return (B<"list">()).method("__contains__", { self, value });
                        }
                        len_value = py{ value.size() };
                        len_self = py{ self.size() };
                        if (len_value > len_self)
                        {
                            return boolean(false);
                        }
                        for (py const& item : iterate{ (B<"range">())(I<0>(), (len_self - len_value) + (I<1>())) })
                        {
                            i = item;
                            sub_list = self[B<"slice">()(i, i + len_value, none())];
                            if (sub_list == value)
                            {
                                return boolean(true);
                            }
                        }
                        return boolean(false);
                    }) },
                { "__repr__", w.method("_CallList", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"pprint">().attr("pformat"))((B<"list">())(self));
                    }) }, }));

            define("unittest.mock", "_check_and_set_parent", w.function("_check_and_set_parent", "_check_and_set_parent", { py_positional("parent"), py_positional("value"), py_positional("name"), py_positional("new_name") }, [](arguments a) -> py
                {
                    py parent = a[0];
                    py value = a[1];
                    py name = a[2];
                    py new_name = a[3];
                    py _parent;
                    value = (G<"unittest.mock._extract_mock">())(value);
                    if (!(G<"unittest.mock._is_instance_mock">())(value).truthy())
                    {
                        return boolean(false);
                    }
                    if ((([&]() -> py { py const t = value.attr("_mock_name"); return t.truthy() ? t : value.attr("_mock_new_name"); }()).truthy() || (!value.attr("_mock_parent").is_none()) || (!value.attr("_mock_new_parent").is_none())))
                    {
                        return boolean(false);
                    }
                    _parent = parent;
                    while (!_parent.is_none())
                    {
                        // setting a mock (value) as a child or return value of itself
                        // should not modify the mock
                        if (_parent.is(value))
                        {
                            return boolean(false);
                        }
                        _parent = _parent.attr("_mock_new_parent");
                    }
                    if (new_name.truthy())
                    {
                        value.set_attr("_mock_new_parent", parent);
                        value.set_attr("_mock_new_name", new_name);
                    }
                    if (name.truthy())
                    {
                        value.set_attr("_mock_parent", parent);
                        value.set_attr("_mock_name", name);
                    }
                    return boolean(true);
                }));

            // Internal class to identify if we wrapped an iterator object or not.
            define("unittest.mock", "_MockIter", w.make_class("_MockIter", { B<"object">() }, {
                { "__init__", w.method("_MockIter", "__init__", { py_positional("obj") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        self.set_attr("obj", (B<"iter">())(obj));
                        return none();
                    }) },
                { "__next__", w.method("_MockIter", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"next">())(self.attr("obj"));
                    }) }, }));

            define("unittest.mock", "Base", w.make_class("Base", { B<"object">() }, {
                { "_mock_return_value", G<"unittest.mock.DEFAULT">() },
                { "_mock_side_effect", none() },
                { "__init__", w.method("Base", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        return none();
                    }, true, true) }, }));

                // Store a mutex as a class attribute in order to protect concurrent access
                // to mock attributes. Using a class attribute allows all NonCallableMock
                // instances to share the mutex for simplicity.
                // 
                // See https://github.com/python/cpython/issues/98624 for why this is
                // necessary.
            define("unittest.mock", "NonCallableMock", w.make_class("NonCallableMock", { G<"unittest.mock.Base">() }, {
                { "_lock", (Mod<"threading">().attr("RLock"))() },
                { "__new__", w.method("NonCallableMock", "__new__", { py_positional("spec", none()), py_positional("wraps", none()), py_positional("name", none()), py_positional("spec_set", none()), py_positional("parent", none()), py_positional("_spec_state", none()), py_positional("_new_name", S<"">()), py_positional("_new_parent", none()), py_positional("_spec_as_instance", boolean(false)), py_positional("_eat_self", none()), py_positional("unsafe", boolean(false)) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py spec = a[1];
                        py wraps = a[2];
                        py name = a[3];
                        py spec_set = a[4];
                        py parent = a[5];
                        py _spec_state = a[6];
                        py _new_name = a[7];
                        py _new_parent = a[8];
                        py _spec_as_instance = a[9];
                        py _eat_self = a[10];
                        py unsafe = a[11];
                        py kwargs = a[12];
                        py bases;
                        py instance;
                        py new_;
                        py spec_arg;
                        bases = py_tuple({ cls });
                        if (!issubclass(cls, G<"unittest.mock.AsyncMockMixin">()))
                        {
                            // Check if spec is an async object or function
                            spec_arg = [&]() -> py { py const t = spec_set; return t.truthy() ? t : spec; }();
                            if (((!spec_arg.is_none()) && (G<"unittest.mock._is_async_obj">())(spec_arg).truthy()))
                            {
                                bases = py_tuple({ G<"unittest.mock.AsyncMockMixin">(), cls });
                            }
                        }
                        new_ = (B<"type">())(cls.attr("__name__"), bases, dict_of({ { S<"__doc__">(), cls.attr("__doc__") } }));
                        instance = (G<"unittest.mock._safe_super">())(G<"unittest.mock.NonCallableMock">(), cls).method("__new__", { new_ });
                        return instance;
                    }, false, true) },
                { "__init__", w.method("NonCallableMock", "__init__", { py_positional("spec", none()), py_positional("wraps", none()), py_positional("name", none()), py_positional("spec_set", none()), py_positional("parent", none()), py_positional("_spec_state", none()), py_positional("_new_name", S<"">()), py_positional("_new_parent", none()), py_positional("_spec_as_instance", boolean(false)), py_positional("_eat_self", none()), py_positional("unsafe", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py spec = a[1];
                        py wraps = a[2];
                        py name = a[3];
                        py spec_set = a[4];
                        py parent = a[5];
                        py _spec_state = a[6];
                        py _new_name = a[7];
                        py _new_parent = a[8];
                        py _spec_as_instance = a[9];
                        py _eat_self = a[10];
                        py unsafe = a[11];
                        py kwargs = a[12];
                        py __dict__;
                        if (_new_parent.is_none())
                        {
                            _new_parent = parent;
                        }
                        __dict__ = self.attr("__dict__");
                        __dict__.set_item(S<"_mock_parent">(), parent);
                        __dict__.set_item(S<"_mock_name">(), name);
                        __dict__.set_item(S<"_mock_new_name">(), _new_name);
                        __dict__.set_item(S<"_mock_new_parent">(), _new_parent);
                        __dict__.set_item(S<"_mock_sealed">(), boolean(false));
                        if (!spec_set.is_none())
                        {
                            spec = spec_set;
                            spec_set = boolean(true);
                        }
                        if (_eat_self.is_none())
                        {
                            _eat_self = boolean(!parent.is_none());
                        }
                        self.method("_mock_add_spec", { spec, spec_set, _spec_as_instance, _eat_self });
                        __dict__.set_item(S<"_mock_children">(), py_dict());
                        __dict__.set_item(S<"_mock_wraps">(), wraps);
                        __dict__.set_item(S<"_mock_delegate">(), none());
                        __dict__.set_item(S<"_mock_called">(), boolean(false));
                        __dict__.set_item(S<"_mock_call_args">(), none());
                        __dict__.set_item(S<"_mock_call_count">(), I<0>());
                        __dict__.set_item(S<"_mock_call_args_list">(), (G<"unittest.mock._CallList">())());
                        __dict__.set_item(S<"_mock_mock_calls">(), (G<"unittest.mock._CallList">())());
                        __dict__.set_item(S<"method_calls">(), (G<"unittest.mock._CallList">())());
                        __dict__.set_item(S<"_mock_unsafe">(), unsafe);
                        if (kwargs.truthy())
                        {
                            call_ex(self.attr("configure_mock"), {  }, py{}, kwargs);
                        }
                        (G<"unittest.mock._safe_super">())(G<"unittest.mock.NonCallableMock">(), self).method("__init__", { spec, wraps, name, spec_set, parent, _spec_state });
                        return none();
                    }, false, true) },
                { "attach_mock", w.method("NonCallableMock", "attach_mock", { py_positional("mock"), py_positional("attribute") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mock = a[1];
                        py attribute = a[2];
                        py inner_mock;
                        inner_mock = (G<"unittest.mock._extract_mock">())(mock);
                        inner_mock.set_attr("_mock_parent", none());
                        inner_mock.set_attr("_mock_new_parent", none());
                        inner_mock.set_attr("_mock_name", S<"">());
                        inner_mock.set_attr("_mock_new_name", none());
                        (B<"setattr">())(self, attribute, mock);
                        return none();
                    }) },
                { "mock_add_spec", w.method("NonCallableMock", "mock_add_spec", { py_positional("spec"), py_positional("spec_set", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py spec = a[1];
                        py spec_set = a[2];
                        self.method("_mock_add_spec", { spec, spec_set });
                        return none();
                    }) },
                { "_mock_add_spec", w.method("NonCallableMock", "_mock_add_spec", { py_positional("spec"), py_positional("spec_set"), py_positional("_spec_as_instance", boolean(false)), py_positional("_eat_self", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py spec = a[1];
                        py spec_set = a[2];
                        py _spec_as_instance = a[3];
                        py _eat_self = a[4];
                        py __dict__;
                        py _spec_asyncs;
                        py _spec_class;
                        py _spec_signature;
                        py attr;
                        py res;
                        py spec_list;
                        py static_attr;
                        py unwrapped_attr;
                        if ((G<"unittest.mock._is_instance_mock">())(spec).truthy())
                        {
                            py_raise((G<"unittest.mock.InvalidSpecError">())(S(std::string{ "Cannot spec a Mock object. [object=" } + spec.repr() + "]")));
                        }
                        _spec_class = none();
                        _spec_signature = none();
                        _spec_asyncs = py_list();
                        if (((!spec.is_none()) && (!(G<"unittest.mock._is_list">())(spec).truthy())))
                        {
                            if (isinstance(spec, B<"type">()))
                            {
                                _spec_class = spec;
                            }
                            else
                            {
                                _spec_class = (B<"type">())(spec);
                            }
                            res = (G<"unittest.mock._get_signature_object">())(spec, _spec_as_instance, _eat_self);
                            _spec_signature = [&]() -> py { py const t = res; return !t.truthy() ? t : res[I<1>()]; }();
                            spec_list = (B<"dir">())(spec);
                            for (py const& item : iterate{ spec_list })
                            {
                                attr = item;
                                static_attr = (Mod<"inspect">().attr("getattr_static"))(spec, attr, none());
                                unwrapped_attr = static_attr;
                                try
                                {
                                    unwrapped_attr = (Mod<"inspect">().attr("unwrap"))(unwrapped_attr);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler14;
                                    if (matches(handler14.exception, B<"ValueError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler14.exception);
                                }
                                if ((Mod<"asyncio">().attr("iscoroutinefunction"))(unwrapped_attr).truthy())
                                {
                                    _spec_asyncs.method("append", { attr });
                                }
                            }
                            spec = spec_list;
                        }
                        __dict__ = self.attr("__dict__");
                        __dict__.set_item(S<"_spec_class">(), _spec_class);
                        __dict__.set_item(S<"_spec_set">(), spec_set);
                        __dict__.set_item(S<"_spec_signature">(), _spec_signature);
                        __dict__.set_item(S<"_mock_methods">(), spec);
                        __dict__.set_item(S<"_spec_asyncs">(), _spec_asyncs);
                        return none();
                    }) },
                { "_NonCallableMock__get_return_value", w.method("NonCallableMock", "__get_return_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ret;
                        ret = self.attr("_mock_return_value");
                        if (!self.attr("_mock_delegate").is_none())
                        {
                            ret = self.attr("_mock_delegate").attr("return_value");
                        }
                        if ((ret.is((G<"unittest.mock.DEFAULT">())) && self.attr("_mock_wraps").is_none()))
                        {
                            ret = self.method("_get_child_mock", {  }, { { "_new_parent", self }, { "_new_name", S<"()">() } });
                            self.set_attr("return_value", ret);
                        }
                        return ret;
                    }) },
                { "_NonCallableMock__set_return_value", w.method("NonCallableMock", "__set_return_value", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (!self.attr("_mock_delegate").is_none())
                        {
                            self.attr("_mock_delegate").set_attr("return_value", value);
                        }
                        else
                        {
                            self.set_attr("_mock_return_value", value);
                            (G<"unittest.mock._check_and_set_parent">())(self, value, none(), S<"()">());
                        }
                        return none();
                    }) },
                { "_NonCallableMock__return_value_doc", S<"The value to be returned when the mock is called.">() },
                { "return_value", (B<"property">()).call({ w.method("NonCallableMock", "__get_return_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ret;
                        ret = self.attr("_mock_return_value");
                        if (!self.attr("_mock_delegate").is_none())
                        {
                            ret = self.attr("_mock_delegate").attr("return_value");
                        }
                        if ((ret.is((G<"unittest.mock.DEFAULT">())) && self.attr("_mock_wraps").is_none()))
                        {
                            ret = self.method("_get_child_mock", {  }, { { "_new_parent", self }, { "_new_name", S<"()">() } });
                            self.set_attr("return_value", ret);
                        }
                        return ret;
                    }), w.method("NonCallableMock", "__set_return_value", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (!self.attr("_mock_delegate").is_none())
                        {
                            self.attr("_mock_delegate").set_attr("return_value", value);
                        }
                        else
                        {
                            self.set_attr("_mock_return_value", value);
                            (G<"unittest.mock._check_and_set_parent">())(self, value, none(), S<"()">());
                        }
                        return none();
                    }) }, { { "doc", (S<"The value to be returned when the mock is called.">()) } }) },
                { "__class__", B<"property">()(w.method("NonCallableMock", "__class__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_spec_class").is_none())
                        {
                            return (B<"type">())(self);
                        }
                        return self.attr("_spec_class");
                    })) },
                { "called", (G<"unittest.mock._delegating_property">())(S<"called">()) },
                { "call_count", (G<"unittest.mock._delegating_property">())(S<"call_count">()) },
                { "call_args", (G<"unittest.mock._delegating_property">())(S<"call_args">()) },
                { "call_args_list", (G<"unittest.mock._delegating_property">())(S<"call_args_list">()) },
                { "mock_calls", (G<"unittest.mock._delegating_property">())(S<"mock_calls">()) },
                { "_NonCallableMock__get_side_effect", w.method("NonCallableMock", "__get_side_effect", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py delegated;
                        py sf;
                        delegated = self.attr("_mock_delegate");
                        if (delegated.is_none())
                        {
                            return self.attr("_mock_side_effect");
                        }
                        sf = delegated.attr("side_effect");
                        if (((!sf.is_none()) && (!callable(sf)) && (!isinstance(sf, G<"unittest.mock._MockIter">())) && (!(G<"unittest.mock._is_exception">())(sf).truthy())))
                        {
                            sf = (G<"unittest.mock._MockIter">())(sf);
                            delegated.set_attr("side_effect", sf);
                        }
                        return sf;
                    }) },
                { "_NonCallableMock__set_side_effect", w.method("NonCallableMock", "__set_side_effect", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py delegated;
                        value = (G<"unittest.mock._try_iter">())(value);
                        delegated = self.attr("_mock_delegate");
                        if (delegated.is_none())
                        {
                            self.set_attr("_mock_side_effect", value);
                        }
                        else
                        {
                            delegated.set_attr("side_effect", value);
                        }
                        return none();
                    }) },
                { "side_effect", (B<"property">())(w.method("NonCallableMock", "__get_side_effect", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py delegated;
                        py sf;
                        delegated = self.attr("_mock_delegate");
                        if (delegated.is_none())
                        {
                            return self.attr("_mock_side_effect");
                        }
                        sf = delegated.attr("side_effect");
                        if (((!sf.is_none()) && (!callable(sf)) && (!isinstance(sf, G<"unittest.mock._MockIter">())) && (!(G<"unittest.mock._is_exception">())(sf).truthy())))
                        {
                            sf = (G<"unittest.mock._MockIter">())(sf);
                            delegated.set_attr("side_effect", sf);
                        }
                        return sf;
                    }), w.method("NonCallableMock", "__set_side_effect", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py delegated;
                        value = (G<"unittest.mock._try_iter">())(value);
                        delegated = self.attr("_mock_delegate");
                        if (delegated.is_none())
                        {
                            self.set_attr("_mock_side_effect", value);
                        }
                        else
                        {
                            delegated.set_attr("side_effect", value);
                        }
                        return none();
                    })) },
                { "reset_mock", w.method("NonCallableMock", "reset_mock", { py_positional("visited", none()), py_keyword("return_value", boolean(false)), py_keyword("side_effect", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py visited = a[1];
                        py return_value = a[2];
                        py side_effect = a[3];
                        py child;
                        py ret;
                        if (visited.is_none())
                        {
                            visited = py_list();
                        }
                        if (visited.contains((B<"id">())(self)))
                        {
                            return none();
                        }
                        visited.method("append", { (B<"id">())(self) });
                        self.set_attr("called", boolean(false));
                        self.set_attr("call_args", none());
                        self.set_attr("call_count", I<0>());
                        self.set_attr("mock_calls", (G<"unittest.mock._CallList">())());
                        self.set_attr("call_args_list", (G<"unittest.mock._CallList">())());
                        self.set_attr("method_calls", (G<"unittest.mock._CallList">())());
                        if (return_value.truthy())
                        {
                            self.set_attr("_mock_return_value", G<"unittest.mock.DEFAULT">());
                        }
                        if (side_effect.truthy())
                        {
                            self.set_attr("_mock_side_effect", none());
                        }
                        for (py const& item : iterate{ self.attr("_mock_children").method("values") })
                        {
                            child = item;
                            if ((isinstance(child, G<"unittest.mock._SpecState">()) || child.is((G<"unittest.mock._deleted">()))))
                            {
                                continue;
                            }
                            child.method("reset_mock", { visited }, { { "return_value", return_value }, { "side_effect", side_effect } });
                        }
                        ret = self.attr("_mock_return_value");
                        if (((G<"unittest.mock._is_instance_mock">())(ret).truthy() && (!ret.is(self))))
                        {
                            ret.method("reset_mock", { visited });
                        }
                        return none();
                    }) },
                { "configure_mock", w.method("NonCallableMock", "configure_mock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kwargs = a[1];
                        py arg;
                        py args;
                        py entry;
                        py final;
                        py obj;
                        py val;
                        for (py const& item : iterate{ (B<"sorted">()).call({ kwargs.method("items") }, { { "key", w.function("<lambda>", "NonCallableMock.configure_mock.<locals>.<lambda>", { py_positional("entry") }, [](arguments a) -> py
    {
        py entry = a[0];
        return entry[I<0>()].method("count", { S<".">() });
    }) } }) })
                        {
                            auto const items17 = unpack(item, 2);
                            arg = items17[0];
                            val = items17[1];
                            // we sort on the number of dots so that
                            // attributes are set before we set attributes on
                            // attributes
                            args = arg.method("split", { S<".">() });
                            final = args.method("pop");
                            obj = self;
                            for (py const& item : iterate{ args })
                            {
                                entry = item;
                                obj = (B<"getattr">())(obj, entry);
                            }
                            (B<"setattr">())(obj, final, val);
                        }
                        return none();
                    }, false, true) },
                { "__getattr__", w.method("NonCallableMock", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py result;
                        py target_name;
                        py wraps;
                        if (set_of({ S<"_mock_methods">(), S<"_mock_unsafe">() }).contains(name))
                        {
                            py_raise((B<"AttributeError">())(name));
                        }
                        else if (!self.attr("_mock_methods").is_none())
                        {
                            if (((!self.attr("_mock_methods").contains(name)) || (G<"unittest.mock._all_magics">()).contains(name)))
                            {
                                py_raise((B<"AttributeError">())((S<"Mock object has no attribute %r">()) % name));
                            }
                        }
                        else if ((G<"unittest.mock._is_magic">())(name).truthy())
                        {
                            py_raise((B<"AttributeError">())(name));
                        }
                        if (((!self.attr("_mock_unsafe").truthy()) && ([&]() -> py { py const t = boolean(!self.attr("_mock_methods").truthy()); return t.truthy() ? t : boolean(!self.attr("_mock_methods").contains(name)); }()).truthy()))
                        {
                            if ((name.method("startswith", { py_tuple({ S<"assert">(), S<"assret">(), S<"asert">(), S<"aseert">(), S<"assrt">() }) }).truthy() || (G<"unittest.mock._ATTRIB_DENY_LIST">()).contains(name)))
                            {
                                py_raise((B<"AttributeError">())(S(name.repr() + " is not a valid assertion. Use a spec for the mock if " + name.repr() + " is meant to be an attribute.")));
                            }
                        }
                        {
                            py const manager19 = (G<"unittest.mock.NonCallableMock">()).attr("_lock");
                            py const exit19 = pyclass::special_method(manager19, "__exit__");
                            py const value19 = pyclass::special_method(manager19, "__enter__")(manager19);
                            [[maybe_unused]] int flow19 = 0;
                            [[maybe_unused]] py result19;
                            bool exited19 = false;
                            try
                            {
                                result = self.attr("_mock_children").method("get", { name });
                                if (result.is((G<"unittest.mock._deleted">())))
                                {
                                    py_raise((B<"AttributeError">())(name));
                                }
                                else if (result.is_none())
                                {
                                    wraps = none();
                                    if (!self.attr("_mock_wraps").is_none())
                                    {
                                        // XXXX should we get the attribute without triggering code
                                        // execution?
                                        wraps = (B<"getattr">())(self.attr("_mock_wraps"), name);
                                    }
                                    result = self.method("_get_child_mock", {  }, { { "parent", self }, { "name", name }, { "wraps", wraps }, { "_new_name", name }, { "_new_parent", self } });
                                    self.attr("_mock_children").set_item(name, result);
                                }
                                else if (isinstance(result, G<"unittest.mock._SpecState">()))
                                {
                                    try
                                    {
                                        result = (G<"unittest.mock.create_autospec">())(result.attr("spec"), result.attr("spec_set"), result.attr("instance"), result.attr("parent"), result.attr("name"));
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler20;
                                        if (matches(handler20.exception, G<"unittest.mock.InvalidSpecError">()))
                                        {
                                            target_name = [&]() -> py { py const t = self.attr("__dict__")[S<"_mock_name">()]; return t.truthy() ? t : self; }();
                                            py_raise((G<"unittest.mock.InvalidSpecError">())(S(std::string{ "Cannot autospec attr " } + name.repr() + " from target " + target_name.repr() + " as it has already been mocked out. [target=" + self.repr() + ", attr=" + result.attr("spec").repr() + "]")));
                                        }
                                        else
                                            py_reraise(handler20.exception);
                                    }
                                    self.attr("_mock_children").set_item(name, result);
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler19;
                                exited19 = true;
                                if (!exit19(manager19, type_of(handler19.exception), handler19.exception, handler19.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler19.exception);
                            }
                            finally19:
                            if (!exited19)
                                exit19(manager19, none(), none(), none());
                            if (flow19 == 1)
                                return result19;
                        }
                        return result;
                    }) },
                { "_extract_mock_name", w.method("NonCallableMock", "_extract_mock_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _first;
                        py _name_list;
                        py _parent;
                        py dot;
                        py last;
                        _name_list = py_list({ self.attr("_mock_new_name") });
                        _parent = self.attr("_mock_new_parent");
                        last = self;
                        dot = S<".">();
                        if (_name_list == py_list({ S<"()">() }))
                        {
                            dot = S<"">();
                        }
                        while (!_parent.is_none())
                        {
                            last = _parent;
                            _name_list.method("append", { _parent.attr("_mock_new_name") + dot });
                            dot = S<".">();
                            if (_parent.attr("_mock_new_name") == (S<"()">()))
                            {
                                dot = S<"">();
                            }
                            _parent = _parent.attr("_mock_new_parent");
                        }
                        _name_list = (B<"list">())((B<"reversed">())(_name_list));
                        _first = [&]() -> py { py const t = last.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }();
                        if (py{ _name_list.size() } > (I<1>()))
                        {
                            if (!py_tuple({ S<"()">(), S<"().">() }).contains(_name_list[I<1>()]))
                            {
                                _first = inplace(NEOS_PY_ADD, _first, S<".">());
                            }
                        }
                        _name_list.set_item(I<0>(), _first);
                        return (S<"">()).method("join", { _name_list });
                    }) },
                { "__repr__", w.method("NonCallableMock", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name;
                        py name_string;
                        py spec_string;
                        name = self.method("_extract_mock_name");
                        name_string = S<"">();
                        if (!py_tuple({ S<"mock">(), S<"mock.">() }).contains(name))
                        {
                            name_string = (S<" name=%r">()) % name;
                        }
                        spec_string = S<"">();
                        if (!self.attr("_spec_class").is_none())
                        {
                            spec_string = S<" spec=%r">();
                            if (self.attr("_spec_set").truthy())
                            {
                                spec_string = S<" spec_set=%r">();
                            }
                            spec_string = spec_string % self.attr("_spec_class").attr("__name__");
                        }
                        return (S<"<%s%s%s id='%s'>">()) % py_tuple({ (B<"type">())(self).attr("__name__"), name_string, spec_string, (B<"id">())(self) });
                    }) },
                { "__dir__", w.method("NonCallableMock", "__dir__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extras;
                        py from_child_mocks;
                        py from_dict;
                        py from_type;
                        if (!(G<"unittest.mock.FILTER_DIR">()).truthy())
                        {
                            return (B<"object">()).method("__dir__", { self });
                        }
                        extras = [&]() -> py { py const t = self.attr("_mock_methods"); return t.truthy() ? t : py_list(); }();
                        from_type = (B<"dir">())((B<"type">())(self));
                        from_dict = (B<"list">())(self.attr("__dict__"));
                        from_child_mocks = [&]() -> py { py const comprehension22 = py_list(); py m_name; py m_value; for (py const& item : iterate{ self.attr("_mock_children").method("items") }) { auto const items23 = unpack(item, 2); m_name = items23[0]; m_value = items23[1]; if (m_value.is((G<"unittest.mock._deleted">()))) continue; comprehension22.method("append", { m_name }); } return comprehension22; }();
                        from_type = [&]() -> py { py const comprehension24 = py_list(); py e; for (py const& item : iterate{ from_type }) { e = item; if (e.method("startswith", { S<"_">() }).truthy()) continue; comprehension24.method("append", { e }); } return comprehension24; }();
                        from_dict = [&]() -> py { py const comprehension25 = py_list(); py e; for (py const& item : iterate{ from_dict }) { e = item; if (!([&]() -> py { py const t = boolean(!e.method("startswith", { S<"_">() }).truthy()); return t.truthy() ? t : (G<"unittest.mock._is_magic">())(e); }()).truthy()) continue; comprehension25.method("append", { e }); } return comprehension25; }();
                        return (B<"sorted">())((B<"set">())(((extras + from_type) + from_dict) + from_child_mocks));
                    }) },
                { "__setattr__", w.method("NonCallableMock", "__setattr__", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py name = a[1];
                        py value = a[2];
                        py mock_name;
                        py msg;
                        auto const original = std::make_shared<py>();
                        if ((G<"unittest.mock._allowed_names">()).contains(name))
                        {
                            // property setters go through here
                            return (B<"object">()).method("__setattr__", { (*self), name, value });
                        }
                        else if (((*self).attr("_spec_set").truthy() && (!(*self).attr("_mock_methods").is_none()) && (!(*self).attr("_mock_methods").contains(name)) && (!(*self).attr("__dict__").contains(name))))
                        {
                            py_raise((B<"AttributeError">())((S<"Mock object has no attribute '%s'">()) % name));
                        }
                        else if ((G<"unittest.mock._unsupported_magics">()).contains(name))
                        {
                            msg = (S<"Attempting to set unsupported magic method %r.">()) % name;
                            py_raise((B<"AttributeError">())(msg));
                        }
                        else if ((G<"unittest.mock._all_magics">()).contains(name))
                        {
                            if (((!(*self).attr("_mock_methods").is_none()) && (!(*self).attr("_mock_methods").contains(name))))
                            {
                                py_raise((B<"AttributeError">())((S<"Mock object has no attribute '%s'">()) % name));
                            }
                            if (!(G<"unittest.mock._is_instance_mock">())(value).truthy())
                            {
                                (B<"setattr">())((B<"type">())((*self)), name, (G<"unittest.mock._get_method">())(name, value));
                                *original = value;
                                value = w.function("<lambda>", "NonCallableMock.__setattr__.<locals>.<lambda>", {  }, [original, self](arguments a) -> py
            {
                py args = a[0];
                py kw = a[1];
                return call_ex((*original), { (*self) }, args, kw);
            }, true, true);
                            }
                            else
                            {
                                // only set _new_name and not name so that mock_calls is tracked
                                // but not method calls
                                (G<"unittest.mock._check_and_set_parent">())((*self), value, none(), name);
                                (B<"setattr">())((B<"type">())((*self)), name, value);
                                (*self).attr("_mock_children").set_item(name, value);
                            }
                        }
                        else if (name == (S<"__class__">()))
                        {
                            (*self).set_attr("_spec_class", value);
                            return none();
                        }
                        else if ((G<"unittest.mock._check_and_set_parent">())((*self), value, name, name).truthy())
                        {
                            (*self).attr("_mock_children").set_item(name, value);
                        }
                        if (((*self).attr("_mock_sealed").truthy() && (!(B<"hasattr">())((*self), name).truthy())))
                        {
                            mock_name = S((*self).method("_extract_mock_name").str() + "." + name.str());
                            py_raise((B<"AttributeError">())(S(std::string{ "Cannot set " } + mock_name.str())));
                        }
                        if (isinstance(value, G<"unittest.mock.PropertyMock">()))
                        {
                            (*self).attr("__dict__").set_item(name, value);
                            return none();
                        }
                        return (B<"object">()).method("__setattr__", { (*self), name, value });
                    }) },
                { "__delattr__", w.method("NonCallableMock", "__delattr__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py obj;
                        if (((G<"unittest.mock._all_magics">()).contains(name) && (B<"type">())(self).attr("__dict__").contains(name)))
                        {
                            (B<"delattr">())((B<"type">())(self), name);
                            if (!self.attr("__dict__").contains(name))
                            {
                                // for magic methods that are still MagicProxy objects and
                                // not set on the instance itself
                                return none();
                            }
                        }
                        obj = self.attr("_mock_children").method("get", { name, G<"unittest.mock._missing">() });
                        if (self.attr("__dict__").contains(name))
                        {
                            (G<"unittest.mock._safe_super">())(G<"unittest.mock.NonCallableMock">(), self).method("__delattr__", { name });
                        }
                        else if (obj.is((G<"unittest.mock._deleted">())))
                        {
                            py_raise((B<"AttributeError">())(name));
                        }
                        if (!obj.is((G<"unittest.mock._missing">())))
                        {
                            del_item(self.attr("_mock_children"), name);
                        }
                        self.attr("_mock_children").set_item(name, G<"unittest.mock._deleted">());
                        return none();
                    }) },
                { "_format_mock_call_signature", w.method("NonCallableMock", "_format_mock_call_signature", { py_positional("args"), py_positional("kwargs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py name;
                        name = [&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }();
                        return (G<"unittest.mock._format_call_signature">())(name, args, kwargs);
                    }) },
                { "_format_mock_failure_message", w.method("NonCallableMock", "_format_mock_failure_message", { py_positional("args"), py_positional("kwargs"), py_positional("action", S<"call">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py action = a[3];
                        py actual_string;
                        py call_args;
                        py expected_string;
                        py message;
                        message = S<"expected %s not found.\nExpected: %s\n  Actual: %s">();
                        expected_string = self.method("_format_mock_call_signature", { args, kwargs });
                        call_args = self.attr("call_args");
                        actual_string = call_ex(self.attr("_format_mock_call_signature"), {  }, call_args, py{});
                        return message % py_tuple({ action, expected_string, actual_string });
                    }) },
                { "_get_call_signature_from_name", w.method("NonCallableMock", "_get_call_signature_from_name", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py child;
                        py children;
                        py names;
                        py sig;
                        if (!name.truthy())
                        {
                            return self.attr("_spec_signature");
                        }
                        sig = none();
                        names = name.method("replace", { S<"()">(), S<"">() }).method("split", { S<".">() });
                        children = self.attr("_mock_children");
                        for (py const& item : iterate{ names })
                        {
                            name = item;
                            child = children.method("get", { name });
                            if ((child.is_none() || isinstance(child, G<"unittest.mock._SpecState">())))
                            {
                                break;
                            }
                            else
                            {
                                // If an autospecced object is attached using attach_mock the
                                // child would be a function with mock object as attribute from
                                // which signature has to be derived.
                                child = (G<"unittest.mock._extract_mock">())(child);
                                children = child.attr("_mock_children");
                                sig = child.attr("_spec_signature");
                            }
                        }
                        return sig;
                    }) },
                { "_call_matcher", w.method("NonCallableMock", "_call_matcher", { py_positional("_call") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _call = a[1];
                        py args;
                        py bound_call;
                        py e;
                        py kwargs;
                        py name;
                        py sig;
                        if ((isinstance(_call, B<"tuple">()) && (py{ _call.size() } > (I<2>()))))
                        {
                            sig = self.method("_get_call_signature_from_name", { _call[I<0>()] });
                        }
                        else
                        {
                            sig = self.attr("_spec_signature");
                        }
                        if (!sig.is_none())
                        {
                            if (py{ _call.size() } == (I<2>()))
                            {
                                name = S<"">();
                                auto const items27 = unpack(_call, 2);
                                args = items27[0];
                                kwargs = items27[1];
                            }
                            else
                            {
                                auto const items28 = unpack(_call, 3);
                                name = items28[0];
                                args = items28[1];
                                kwargs = items28[2];
                            }
                            try
                            {
                                bound_call = call_ex(sig.attr("bind"), {  }, args, kwargs);
                                return (G<"unittest.mock.call">())(name, bound_call.attr("args"), bound_call.attr("kwargs"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler29;
                                if (matches(handler29.exception, B<"TypeError">()))
                                {
                                    e = handler29.exception;
                                    return e.method("with_traceback", { none() });
                                }
                                else
                                    py_reraise(handler29.exception);
                            }
                        }
                        else
                        {
                            return _call;
                        }
                        return none();
                    }) },
                { "assert_not_called", w.method("NonCallableMock", "assert_not_called", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        if (self.attr("call_count") != (I<0>()))
                        {
                            msg = (S<"Expected '%s' to not have been called. Called %s times.%s">()) % py_tuple({ [&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }(), self.attr("call_count"), self.method("_calls_repr") });
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return none();
                    }) },
                { "assert_called", w.method("NonCallableMock", "assert_called", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        if (self.attr("call_count") == (I<0>()))
                        {
                            msg = (S<"Expected '%s' to have been called.">()) % ([&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }());
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return none();
                    }) },
                { "assert_called_once", w.method("NonCallableMock", "assert_called_once", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        if (!(self.attr("call_count") == (I<1>())))
                        {
                            msg = (S<"Expected '%s' to have been called once. Called %s times.%s">()) % py_tuple({ [&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }(), self.attr("call_count"), self.method("_calls_repr") });
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return none();
                    }) },
                { "assert_called_with", w.method("NonCallableMock", "assert_called_with", {  }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const args = std::make_shared<py>(a[1]);
                        auto const kwargs = std::make_shared<py>(a[2]);
                        py _error_message;
                        py actual;
                        py cause;
                        py error_message;
                        py expected;
                        if ((*self).attr("call_args").is_none())
                        {
                            expected = (*self).method("_format_mock_call_signature", { (*args), (*kwargs) });
                            actual = S<"not called.">();
                            error_message = (S<"expected call not found.\nExpected: %s\n  Actual: %s">()) % py_tuple({ expected, actual });
                            py_raise((B<"AssertionError">())(error_message));
                        }
                        _error_message = w.function("_error_message", "NonCallableMock.assert_called_with.<locals>._error_message", {  }, [args, kwargs, self](arguments a) -> py
    {
        py msg;
        msg = (*self).method("_format_mock_failure_message", { (*args), (*kwargs) });
        return msg;
    });
                        expected = (*self).method("_call_matcher", { (G<"unittest.mock._Call">()).call({ py_tuple({ (*args), (*kwargs) }) }, { { "two", boolean(true) } }) });
                        actual = (*self).method("_call_matcher", { (*self).attr("call_args") });
                        if (actual != expected)
                        {
                            cause = (isinstance(expected, B<"Exception">()) ? expected : none());
                            fn_raise_from((B<"AssertionError">())(_error_message()).get(), cause.get());
                            throw raised{};
                        }
                        return none();
                    }, true, true) },
                { "assert_called_once_with", w.method("NonCallableMock", "assert_called_once_with", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py msg;
                        if (!(self.attr("call_count") == (I<1>())))
                        {
                            msg = (S<"Expected '%s' to be called once. Called %s times.%s">()) % py_tuple({ [&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }(), self.attr("call_count"), self.method("_calls_repr") });
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return call_ex(self.attr("assert_called_with"), {  }, args, kwargs);
                    }, true, true) },
                { "assert_has_calls", w.method("NonCallableMock", "assert_has_calls", { py_positional("calls"), py_positional("any_order", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py calls = a[1];
                        py any_order = a[2];
                        py all_calls;
                        py cause;
                        py expected;
                        py kall;
                        py not_found;
                        py problem;
                        expected = [&]() -> py { py const comprehension30 = py_list(); py c; for (py const& item : iterate{ calls }) { c = item; comprehension30.method("append", { self.method("_call_matcher", { c }) }); } return comprehension30; }();
                        cause = (B<"next">())([&]() -> py { py const comprehension31 = py_list(); py e; for (py const& item : iterate{ expected }) { e = item; if (!isinstance(e, B<"Exception">())) continue; comprehension31.method("append", { e }); } return comprehension31; }(), none());
                        all_calls = (G<"unittest.mock._CallList">())([&]() -> py { py const comprehension32 = py_list(); py c; for (py const& item : iterate{ self.attr("mock_calls") }) { c = item; comprehension32.method("append", { self.method("_call_matcher", { c }) }); } return comprehension32; }());
                        if (!any_order.truthy())
                        {
                            if (!all_calls.contains(expected))
                            {
                                if (cause.is_none())
                                {
                                    problem = S<"Calls not found.">();
                                }
                                else
                                {
                                    problem = (S<"Error processing expected calls.\nErrors: {}">()).method("format", { [&]() -> py { py const comprehension33 = py_list(); py e; for (py const& item : iterate{ expected }) { e = item; comprehension33.method("append", { (isinstance(e, B<"Exception">()) ? e : none()) }); } return comprehension33; }() });
                                }
                                fn_raise_from((B<"AssertionError">())(S(problem.str() + "\nExpected: " + (G<"unittest.mock._CallList">())(calls).str() + "\n  Actual: " + (Mod<"unittest.util">().attr("safe_repr"))(self.attr("mock_calls")).str())).get(), cause.get());
                                throw raised{};
                            }
                            return none();
                        }
                        all_calls = (B<"list">())(all_calls);
                        not_found = py_list();
                        for (py const& item : iterate{ expected })
                        {
                            kall = item;
                            try
                            {
                                all_calls.method("remove", { kall });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler35;
                                if (matches(handler35.exception, B<"ValueError">()))
                                {
                                    not_found.method("append", { kall });
                                }
                                else
                                    py_reraise(handler35.exception);
                            }
                        }
                        if (not_found.truthy())
                        {
                            fn_raise_from((B<"AssertionError">())((S<"%r does not contain all of %r in its call list, found %r instead">()) % py_tuple({ [&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }(), (B<"tuple">())(not_found), all_calls })).get(), cause.get());
                            throw raised{};
                        }
                        return none();
                    }) },
                { "assert_any_call", w.method("NonCallableMock", "assert_any_call", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py actual;
                        py cause;
                        py expected;
                        py expected_string;
                        expected = self.method("_call_matcher", { (G<"unittest.mock._Call">()).call({ py_tuple({ args, kwargs }) }, { { "two", boolean(true) } }) });
                        cause = (isinstance(expected, B<"Exception">()) ? expected : none());
                        actual = [&]() -> py { py const comprehension36 = py_list(); py c; for (py const& item : iterate{ self.attr("call_args_list") }) { c = item; comprehension36.method("append", { self.method("_call_matcher", { c }) }); } return comprehension36; }();
                        if ((cause.truthy() || (!(G<"unittest.mock._AnyComparer">())(actual).contains(expected))))
                        {
                            expected_string = self.method("_format_mock_call_signature", { args, kwargs });
                            fn_raise_from((B<"AssertionError">())((S<"%s call not found">()) % expected_string).get(), cause.get());
                            throw raised{};
                        }
                        return none();
                    }, true, true) },
                { "_get_child_mock", w.method("NonCallableMock", "_get_child_mock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kw = a[1];
                        py _new_name;
                        py _type;
                        py attribute;
                        py klass;
                        py mock_name;
                        if (self.attr("_mock_sealed").truthy())
                        {
                            attribute = (kw.contains((S<"name">())) ? S(std::string{ "." } + kw[S<"name">()].str()) : S<"()">());
                            mock_name = self.method("_extract_mock_name") + attribute;
                            py_raise((B<"AttributeError">())(mock_name));
                        }
                        _new_name = kw.method("get", { S<"_new_name">() });
                        if (self.attr("__dict__")[S<"_spec_asyncs">()].contains(_new_name))
                        {
                            return call_ex((G<"unittest.mock.AsyncMock">()), {  }, py{}, kw);
                        }
                        _type = (B<"type">())(self);
                        if ((issubclass(_type, G<"unittest.mock.MagicMock">()) && (G<"unittest.mock._async_method_magics">()).contains(_new_name)))
                        {
                            // Any asynchronous magic becomes an AsyncMock
                            klass = G<"unittest.mock.AsyncMock">();
                        }
                        else if (issubclass(_type, G<"unittest.mock.AsyncMockMixin">()))
                        {
                            if (((G<"unittest.mock._all_sync_magics">()).contains(_new_name) || ([&]() -> py { py const t = self.attr("_mock_methods"); return !t.truthy() ? t : boolean(self.attr("_mock_methods").contains(_new_name)); }()).truthy()))
                            {
                                // Any synchronous method on AsyncMock becomes a MagicMock
                                klass = G<"unittest.mock.MagicMock">();
                            }
                            else
                            {
                                klass = G<"unittest.mock.AsyncMock">();
                            }
                        }
                        else if (!issubclass(_type, G<"unittest.mock.CallableMixin">()))
                        {
                            if (issubclass(_type, G<"unittest.mock.NonCallableMagicMock">()))
                            {
                                klass = G<"unittest.mock.MagicMock">();
                            }
                            else if (issubclass(_type, G<"unittest.mock.NonCallableMock">()))
                            {
                                klass = G<"unittest.mock.Mock">();
                            }
                        }
                        else
                        {
                            klass = _type.attr("__mro__")[I<1>()];
                        }
                        return call_ex(klass, {  }, py{}, kw);
                    }, false, true) },
                { "_calls_repr", w.method("NonCallableMock", "_calls_repr", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("mock_calls").truthy())
                        {
                            return S<"">();
                        }
                        return S(std::string{ "\nCalls: " } + (Mod<"unittest.util">().attr("safe_repr"))(self.attr("mock_calls")).str() + ".");
                    }) }, }));

            // Denylist for forbidden attribute names in safe mode
            define("unittest.mock", "_ATTRIB_DENY_LIST", (B<"frozenset">())([&]() -> py { py const comprehension37 = B<"set">()(); py name; for (py const& item : iterate{ (B<"dir">())(G<"unittest.mock.NonCallableMock">()) }) { name = item; if (!name.method("startswith", { S<"assert_">() }).truthy()) continue; comprehension37.method("add", { name.method("removeprefix", { S<"assert_">() }) }); } return comprehension37; }()));
            define("unittest.mock", "_AnyComparer", w.make_class("_AnyComparer", { B<"list">() }, {
                { "__contains__", w.method("_AnyComparer", "__contains__", { py_positional("item") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py item_ = a[1];
                        py _call;
                        for (py const& item : iterate{ self })
                        {
                            _call = item;
                            if (!(py{ item_.size() } == py{ _call.size() }))
                                py_raise(B<"AssertionError">()());
                            if ((B<"all">())([&]() -> py { py const comprehension39 = py_list(); py actual; py expected; for (py const& item : iterate{ (B<"zip">())(item_, _call) }) { auto const items40 = unpack(item, 2); expected = items40[0]; actual = items40[1]; comprehension39.method("append", { boolean(expected == actual) }); } return comprehension39; }()).truthy())
                            {
                                return boolean(true);
                            }
                        }
                        return boolean(false);
                    }) }, }));

            define("unittest.mock", "_try_iter", w.function("_try_iter", "_try_iter", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    if (obj.is_none())
                    {
                        return obj;
                    }
                    if ((G<"unittest.mock._is_exception">())(obj).truthy())
                    {
                        return obj;
                    }
                    if ((G<"unittest.mock._callable">())(obj).truthy())
                    {
                        return obj;
                    }
                    try
                    {
                        return (B<"iter">())(obj);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler41;
                        if (matches(handler41.exception, B<"TypeError">()))
                        {
                            // XXXX backwards compatibility
                            // but this will blow up on first call - so maybe we should fail early?
                            return obj;
                        }
                        else
                            py_reraise(handler41.exception);
                    }
                    return none();
                }));

            define("unittest.mock", "CallableMixin", w.make_class("CallableMixin", { G<"unittest.mock.Base">() }, {
                { "__init__", w.method("CallableMixin", "__init__", { py_positional("spec", none()), py_positional("side_effect", none()), py_positional("return_value", G<"unittest.mock.DEFAULT">()), py_positional("wraps", none()), py_positional("name", none()), py_positional("spec_set", none()), py_positional("parent", none()), py_positional("_spec_state", none()), py_positional("_new_name", S<"">()), py_positional("_new_parent", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py spec = a[1];
                        py side_effect = a[2];
                        py return_value = a[3];
                        py wraps = a[4];
                        py name = a[5];
                        py spec_set = a[6];
                        py parent = a[7];
                        py _spec_state = a[8];
                        py _new_name = a[9];
                        py _new_parent = a[10];
                        py kwargs = a[11];
                        self.attr("__dict__").set_item(S<"_mock_return_value">(), return_value);
                        call_ex((G<"unittest.mock._safe_super">())(G<"unittest.mock.CallableMixin">(), self).attr("__init__"), { spec, wraps, name, spec_set, parent, _spec_state, _new_name, _new_parent }, py{}, kwargs);
                        self.set_attr("side_effect", side_effect);
                        return none();
                    }, false, true) },
                { "_mock_check_sig", w.method("CallableMixin", "_mock_check_sig", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        return none();
                    }, true, true) },
                { "__call__", w.method("CallableMixin", "__call__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        call_ex(self.attr("_mock_check_sig"), {  }, args, kwargs);
                        call_ex(self.attr("_increment_mock_call"), {  }, args, kwargs);
                        return call_ex(self.attr("_mock_call"), {  }, args, kwargs);
                    }, true, true) },
                { "_mock_call", w.method("CallableMixin", "_mock_call", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        return call_ex(self.attr("_execute_mock_call"), {  }, args, kwargs);
                    }, true, true) },
                { "_increment_mock_call", w.method("CallableMixin", "_increment_mock_call", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py _call;
                        py _new_parent;
                        py do_method_calls;
                        py dot;
                        py is_a_call;
                        py method_call_name;
                        py mock_call_name;
                        py this_mock_call;
                        self.set_attr("called", boolean(true));
                        // handle call_args
                        // needs to be set here so assertions on call arguments pass before
                        // execution in the case of awaited calls
                        {
                            py const manager42 = (G<"unittest.mock.NonCallableMock">()).attr("_lock");
                            py const exit42 = pyclass::special_method(manager42, "__exit__");
                            py const value42 = pyclass::special_method(manager42, "__enter__")(manager42);
                            [[maybe_unused]] int flow42 = 0;
                            [[maybe_unused]] py result42;
                            bool exited42 = false;
                            try
                            {
                                // Lock is used here so that call_args_list and call_count are
                                // set atomically otherwise it is possible that by the time call_count
                                // is set another thread may have appended to call_args_list.
                                // The rest of this function relies on list.append being atomic and
                                // skips locking.
                                _call = (G<"unittest.mock._Call">()).call({ py_tuple({ args, kwargs }) }, { { "two", boolean(true) } });
                                self.set_attr("call_args", _call);
                                self.attr("call_args_list").method("append", { _call });
                                self.set_attr("call_count", py{ self.attr("call_args_list").size() });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler42;
                                exited42 = true;
                                if (!exit42(manager42, type_of(handler42.exception), handler42.exception, handler42.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler42.exception);
                            }
                            finally42:
                            if (!exited42)
                                exit42(manager42, none(), none(), none());
                            if (flow42 == 1)
                                return result42;
                        }
                        // initial stuff for method_calls:
                        do_method_calls = boolean(!self.attr("_mock_parent").is_none());
                        method_call_name = self.attr("_mock_name");
                        // initial stuff for mock_calls:
                        mock_call_name = self.attr("_mock_new_name");
                        is_a_call = boolean(mock_call_name == (S<"()">()));
                        self.attr("mock_calls").method("append", { (G<"unittest.mock._Call">())(py_tuple({ S<"">(), args, kwargs })) });
                        // follow up the chain of mocks:
                        _new_parent = self.attr("_mock_new_parent");
                        while (!_new_parent.is_none())
                        {
                            // handle method_calls:
                            if (do_method_calls.truthy())
                            {
                                _new_parent.attr("method_calls").method("append", { (G<"unittest.mock._Call">())(py_tuple({ method_call_name, args, kwargs })) });
                                do_method_calls = boolean(!_new_parent.attr("_mock_parent").is_none());
                                if (do_method_calls.truthy())
                                {
                                    method_call_name = (_new_parent.attr("_mock_name") + (S<".">())) + method_call_name;
                                }
                            }
                            // handle mock_calls:
                            this_mock_call = (G<"unittest.mock._Call">())(py_tuple({ mock_call_name, args, kwargs }));
                            _new_parent.attr("mock_calls").method("append", { this_mock_call });
                            if (_new_parent.attr("_mock_new_name").truthy())
                            {
                                if (is_a_call.truthy())
                                {
                                    dot = S<"">();
                                }
                                else
                                {
                                    dot = S<".">();
                                }
                                is_a_call = boolean(_new_parent.attr("_mock_new_name") == (S<"()">()));
                                mock_call_name = (_new_parent.attr("_mock_new_name") + dot) + mock_call_name;
                            }
                            // follow the parental chain:
                            _new_parent = _new_parent.attr("_mock_new_parent");
                        }
                        return none();
                    }, true, true) },
                { "_execute_mock_call", w.method("CallableMixin", "_execute_mock_call", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py effect;
                        py result;
                        effect = self.attr("side_effect");
                        if (!effect.is_none())
                        {
                            if ((G<"unittest.mock._is_exception">())(effect).truthy())
                            {
                                py_raise(effect);
                            }
                            else if (!(G<"unittest.mock._callable">())(effect).truthy())
                            {
                                result = (B<"next">())(effect);
                                if ((G<"unittest.mock._is_exception">())(result).truthy())
                                {
                                    py_raise(result);
                                }
                            }
                            else
                            {
                                result = call_ex(effect, {  }, args, kwargs);
                            }
                            if (!result.is((G<"unittest.mock.DEFAULT">())))
                            {
                                return result;
                            }
                        }
                        if (!self.attr("_mock_return_value").is((G<"unittest.mock.DEFAULT">())))
                        {
                            return self.attr("return_value");
                        }
                        if ((self.attr("_mock_delegate").truthy() && (!self.attr("_mock_delegate").attr("return_value").is((G<"unittest.mock.DEFAULT">())))))
                        {
                            return self.attr("return_value");
                        }
                        if (!self.attr("_mock_wraps").is_none())
                        {
                            return call_ex(self.attr("_mock_wraps"), {  }, args, kwargs);
                        }
                        return self.attr("return_value");
                    }, true, true) }, }));

            define("unittest.mock", "Mock", w.make_class("Mock", { G<"unittest.mock.CallableMixin">(), G<"unittest.mock.NonCallableMock">() }, {
 }));

            // _check_spec_arg_typos takes kwargs from commands like patch and checks that
            // they don't contain common misspellings of arguments related to autospeccing.
            define("unittest.mock", "_check_spec_arg_typos", w.function("_check_spec_arg_typos", "_check_spec_arg_typos", { py_positional("kwargs_to_check") }, [](arguments a) -> py
                {
                    py kwargs_to_check = a[0];
                    py typo;
                    py typos;
                    typos = py_tuple({ S<"autospect">(), S<"auto_spec">(), S<"set_spec">() });
                    for (py const& item : iterate{ typos })
                    {
                        typo = item;
                        if (kwargs_to_check.contains(typo))
                        {
                            py_raise((B<"RuntimeError">())(S(typo.repr() + " might be a typo; use unsafe=True if this is intended")));
                        }
                    }
                    return none();
                }));

            define("unittest.mock", "_patch", w.make_class("_patch", { B<"object">() }, {
                { "attribute_name", none() },
                { "_active_patches", py_list() },
                { "__init__", w.method("_patch", "__init__", { py_positional("getter"), py_positional("attribute"), py_positional("new"), py_positional("spec"), py_positional("create"), py_positional("spec_set"), py_positional("autospec"), py_positional("new_callable"), py_positional("kwargs"), py_keyword("unsafe", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py getter = a[1];
                        py attribute = a[2];
                        py new_ = a[3];
                        py spec = a[4];
                        py create = a[5];
                        py spec_set = a[6];
                        py autospec = a[7];
                        py new_callable = a[8];
                        py kwargs = a[9];
                        py unsafe = a[10];
                        if (!new_callable.is_none())
                        {
                            if (!new_.is((G<"unittest.mock.DEFAULT">())))
                            {
                                py_raise((B<"ValueError">())(S<"Cannot use 'new' and 'new_callable' together">()));
                            }
                            if (!autospec.is_none())
                            {
                                py_raise((B<"ValueError">())(S<"Cannot use 'autospec' and 'new_callable' together">()));
                            }
                        }
                        if (!unsafe.truthy())
                        {
                            (G<"unittest.mock._check_spec_arg_typos">())(kwargs);
                        }
                        if ((G<"unittest.mock._is_instance_mock">())(spec).truthy())
                        {
                            py_raise((G<"unittest.mock.InvalidSpecError">())(S(std::string{ "Cannot spec attr " } + attribute.repr() + " as the spec has already been mocked out. [spec=" + spec.repr() + "]")));
                        }
                        if ((G<"unittest.mock._is_instance_mock">())(spec_set).truthy())
                        {
                            py_raise((G<"unittest.mock.InvalidSpecError">())(S(std::string{ "Cannot spec attr " } + attribute.repr() + " as the spec_set target has already been mocked out. [spec_set=" + spec_set.repr() + "]")));
                        }
                        self.set_attr("getter", getter);
                        self.set_attr("attribute", attribute);
                        self.set_attr("new", new_);
                        self.set_attr("new_callable", new_callable);
                        self.set_attr("spec", spec);
                        self.set_attr("create", create);
                        self.set_attr("has_local", boolean(false));
                        self.set_attr("spec_set", spec_set);
                        self.set_attr("autospec", autospec);
                        self.set_attr("kwargs", kwargs);
                        self.set_attr("additional_patchers", py_list());
                        self.set_attr("is_started", boolean(false));
                        return none();
                    }) },
                { "copy", w.method("_patch", "copy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py patcher;
                        patcher = (G<"unittest.mock._patch">())(self.attr("getter"), self.attr("attribute"), self.attr("new"), self.attr("spec"), self.attr("create"), self.attr("spec_set"), self.attr("autospec"), self.attr("new_callable"), self.attr("kwargs"));
                        patcher.set_attr("attribute_name", self.attr("attribute_name"));
                        patcher.set_attr("additional_patchers", [&]() -> py { py const comprehension45 = py_list(); py p; for (py const& item : iterate{ self.attr("additional_patchers") }) { p = item; comprehension45.method("append", { p.method("copy") }); } return comprehension45; }());
                        return patcher;
                    }) },
                { "__call__", w.method("_patch", "__call__", { py_positional("func") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py func = a[1];
                        if (isinstance(func, B<"type">()))
                        {
                            return self.method("decorate_class", { func });
                        }
                        if ((Mod<"inspect">().attr("iscoroutinefunction"))(func).truthy())
                        {
                            return self.method("decorate_async_callable", { func });
                        }
                        return self.method("decorate_callable", { func });
                    }) },
                { "decorate_class", w.method("_patch", "decorate_class", { py_positional("klass") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py klass = a[1];
                        py attr;
                        py attr_value;
                        py patcher;
                        for (py const& item : iterate{ (B<"dir">())(klass) })
                        {
                            attr = item;
                            if (!attr.method("startswith", { (G<"unittest.mock.patch">()).attr("TEST_PREFIX") }).truthy())
                            {
                                continue;
                            }
                            attr_value = (B<"getattr">())(klass, attr);
                            if (!has_attr(attr_value, "__call__"))
                            {
                                continue;
                            }
                            patcher = self.method("copy");
                            (B<"setattr">())(klass, attr, patcher(attr_value));
                        }
                        return klass;
                    }) },
                { "decoration_helper", (Mod<"contextlib">().attr("contextmanager"))(w.method("_patch", "decoration_helper", { py_positional("patched"), py_positional("args"), py_positional("keywargs") }, [](arguments a) -> py
                    {
                        return make_native_generator("decoration_helper", "_patch.decoration_helper", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py patched = a[1];
                            py args = a[2];
                            py keywargs = a[3];
                            py arg;
                            py exit_stack;
                            py extra_args;
                            py patching;
                            extra_args = py_list();
                            {
                                py const manager47 = (Mod<"contextlib">().attr("ExitStack"))();
                                py const exit47 = pyclass::special_method(manager47, "__exit__");
                                py const value47 = pyclass::special_method(manager47, "__enter__")(manager47);
                                exit_stack = value47;
                                [[maybe_unused]] int flow47 = 0;
                                [[maybe_unused]] py result47;
                                bool exited47 = false;
                                try
                                {
                                    for (py const& item : iterate{ patched.attr("patchings") })
                                    {
                                        patching = item;
                                        arg = exit_stack.method("enter_context", { patching });
                                        if (!patching.attr("attribute_name").is_none())
                                        {
                                            keywargs.method("update", { arg });
                                        }
                                        else if (patching.attr("new").is((G<"unittest.mock.DEFAULT">())))
                                        {
                                            extra_args.method("append", { arg });
                                        }
                                    }
                                    args = inplace(NEOS_PY_ADD, args, (B<"tuple">())(extra_args));
                                    py_yield(py_tuple({ args, keywargs }));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler47;
                                    exited47 = true;
                                    if (!exit47(manager47, type_of(handler47.exception), handler47.exception, handler47.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler47.exception);
                                }
                                finally47:
                                if (!exited47)
                                    exit47(manager47, none(), none(), none());
                                if (flow47 == 1)
                                    return result47;
                            }
                            return none();
                        });
                    })) },
                { "decorate_callable", w.method("_patch", "decorate_callable", { py_positional("func") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const func = std::make_shared<py>(a[1]);
                        auto const patched = std::make_shared<py>();
                        if (has_attr((*func), "patchings"))
                        {
                            (*func).attr("patchings").method("append", { (*self) });
                            return (*func);
                        }
                        *patched = (Mod<"functools">().attr("wraps"))((*func))(w.function("patched", "_patch.decorate_callable.<locals>.patched", {  }, [func, patched, self](arguments a) -> py
    {
        py args = a[0];
        py keywargs = a[1];
        py newargs;
        py newkeywargs;
        {
            py const manager49 = (*self).method("decoration_helper", { (*patched), args, keywargs });
            py const exit49 = pyclass::special_method(manager49, "__exit__");
            py const value49 = pyclass::special_method(manager49, "__enter__")(manager49);
            auto const items50 = unpack(value49, 2);
            newargs = items50[0];
            newkeywargs = items50[1];
            [[maybe_unused]] int flow49 = 0;
            [[maybe_unused]] py result49;
            bool exited49 = false;
            try
            {
                result49 = call_ex((*func), {  }, newargs, newkeywargs);
                flow49 = 1;
                goto finally49;
            }
            catch (raised const&)
            {
                pyclass::handler_scope handler49;
                exited49 = true;
                if (!exit49(manager49, type_of(handler49.exception), handler49.exception, handler49.exception.attr("__traceback__")).truthy())
                    py_reraise(handler49.exception);
            }
            finally49:
            if (!exited49)
                exit49(manager49, none(), none(), none());
            if (flow49 == 1)
                return result49;
        }
        return none();
    }, true, true));
                        (*patched).set_attr("patchings", py_list({ (*self) }));
                        return (*patched);
                    }) },
                { "decorate_async_callable", w.method("_patch", "decorate_async_callable", { py_positional("func") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const func = std::make_shared<py>(a[1]);
                        auto const patched = std::make_shared<py>();
                        if (has_attr((*func), "patchings"))
                        {
                            (*func).attr("patchings").method("append", { (*self) });
                            return (*func);
                        }
                        *patched = (Mod<"functools">().attr("wraps"))((*func))(async_function(w, "_patch.decorate_async_callable.<locals>.patched", {  }, [func, patched, self](arguments a) -> py
    {
        py args = a[0];
        py keywargs = a[1];
        py newargs;
        py newkeywargs;
        {
            py const manager51 = (*self).method("decoration_helper", { (*patched), args, keywargs });
            py const exit51 = pyclass::special_method(manager51, "__exit__");
            py const value51 = pyclass::special_method(manager51, "__enter__")(manager51);
            auto const items52 = unpack(value51, 2);
            newargs = items52[0];
            newkeywargs = items52[1];
            [[maybe_unused]] int flow51 = 0;
            [[maybe_unused]] py result51;
            bool exited51 = false;
            try
            {
                result51 = py_await(call_ex((*func), {  }, newargs, newkeywargs));
                flow51 = 1;
                goto finally51;
            }
            catch (raised const&)
            {
                pyclass::handler_scope handler51;
                exited51 = true;
                if (!exit51(manager51, type_of(handler51.exception), handler51.exception, handler51.exception.attr("__traceback__")).truthy())
                    py_reraise(handler51.exception);
            }
            finally51:
            if (!exited51)
                exit51(manager51, none(), none(), none());
            if (flow51 == 1)
                return result51;
        }
        return none();
    }, true, true));
                        (*patched).set_attr("patchings", py_list({ (*self) }));
                        return (*patched);
                    }) },
                { "get_original", w.method("_patch", "get_original", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py local;
                        py name;
                        py original;
                        py target;
                        target = self.method("getter");
                        name = self.attr("attribute");
                        original = G<"unittest.mock.DEFAULT">();
                        local = boolean(false);
                        {
                            bool otherwise53 = false;
                            try
                            {
                                original = target.attr("__dict__")[name];
                                otherwise53 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler53;
                                if (matches(handler53.exception, py_tuple({ B<"AttributeError">(), B<"KeyError">() })))
                                {
                                    original = (B<"getattr">())(target, name, G<"unittest.mock.DEFAULT">());
                                }
                                else
                                    py_reraise(handler53.exception);
                            }
                            if (otherwise53)
                            {
                                local = boolean(true);
                            }
                        }
                        if (((G<"unittest.mock._builtins">()).contains(name) && isinstance(target, Mod<"types">().attr("ModuleType"))))
                        {
                            self.set_attr("create", boolean(true));
                        }
                        if (((!self.attr("create").truthy()) && original.is((G<"unittest.mock.DEFAULT">()))))
                        {
                            py_raise((B<"AttributeError">())((S<"%s does not have the attribute %r">()) % py_tuple({ target, name })));
                        }
                        return py_tuple({ original, local });
                    }) },
                { "__enter__", w.method("_patch", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py Klass;
                        py _kwargs;
                        py arg;
                        py autospec;
                        py extra_args;
                        py inherit;
                        py kwargs;
                        py local;
                        py new_;
                        py new_attr;
                        py new_callable;
                        py not_callable;
                        py original;
                        py patching;
                        py spec;
                        py spec_set;
                        py target_name;
                        py this_spec;
                        if (self.attr("is_started").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Patch is already started">()));
                        }
                        auto const items54 = unpack(py_tuple({ self.attr("new"), self.attr("spec"), self.attr("spec_set") }), 3);
                        new_ = items54[0];
                        spec = items54[1];
                        spec_set = items54[2];
                        auto const items55 = unpack(py_tuple({ self.attr("autospec"), self.attr("kwargs") }), 2);
                        autospec = items55[0];
                        kwargs = items55[1];
                        new_callable = self.attr("new_callable");
                        self.set_attr("target", self.method("getter"));
                        // normalise False to None
                        if (spec.is(boolean(false)))
                        {
                            spec = none();
                        }
                        if (spec_set.is(boolean(false)))
                        {
                            spec_set = none();
                        }
                        if (autospec.is(boolean(false)))
                        {
                            autospec = none();
                        }
                        if (((!spec.is_none()) && (!autospec.is_none())))
                        {
                            py_raise((B<"TypeError">())(S<"Can't specify spec and autospec">()));
                        }
                        if ((([&]() -> py { py const t = boolean(!spec.is_none()); return t.truthy() ? t : boolean(!autospec.is_none()); }()).truthy() && (!py_tuple({ boolean(true), none() }).contains(spec_set))))
                        {
                            py_raise((B<"TypeError">())(S<"Can't provide explicit spec_set *and* spec or autospec">()));
                        }
                        auto const items56 = unpack(self.method("get_original"), 2);
                        original = items56[0];
                        local = items56[1];
                        if ((new_.is((G<"unittest.mock.DEFAULT">())) && autospec.is_none()))
                        {
                            inherit = boolean(false);
                            if (spec.is(boolean(true)))
                            {
                                // set spec to the object we are replacing
                                spec = original;
                                if (spec_set.is(boolean(true)))
                                {
                                    spec_set = original;
                                    spec = none();
                                }
                            }
                            else if (!spec.is_none())
                            {
                                if (spec_set.is(boolean(true)))
                                {
                                    spec_set = spec;
                                    spec = none();
                                }
                            }
                            else if (spec_set.is(boolean(true)))
                            {
                                spec_set = original;
                            }
                            if (((!spec.is_none()) || (!spec_set.is_none())))
                            {
                                if (original.is((G<"unittest.mock.DEFAULT">())))
                                {
                                    py_raise((B<"TypeError">())(S<"Can't use 'spec' with create=True">()));
                                }
                                if (isinstance(original, B<"type">()))
                                {
                                    // If we're patching out a class and there is a spec
                                    inherit = boolean(true);
                                }
                            }
                            // Determine the Klass to use
                            if (!new_callable.is_none())
                            {
                                Klass = new_callable;
                            }
                            else if ((spec.is_none() && (G<"unittest.mock._is_async_obj">())(original).truthy()))
                            {
                                Klass = G<"unittest.mock.AsyncMock">();
                            }
                            else if (((!spec.is_none()) || (!spec_set.is_none())))
                            {
                                this_spec = spec;
                                if (!spec_set.is_none())
                                {
                                    this_spec = spec_set;
                                }
                                if ((G<"unittest.mock._is_list">())(this_spec).truthy())
                                {
                                    not_callable = boolean(!this_spec.contains((S<"__call__">())));
                                }
                                else
                                {
                                    not_callable = boolean(!callable(this_spec));
                                }
                                if ((G<"unittest.mock._is_async_obj">())(this_spec).truthy())
                                {
                                    Klass = G<"unittest.mock.AsyncMock">();
                                }
                                else if (not_callable.truthy())
                                {
                                    Klass = G<"unittest.mock.NonCallableMagicMock">();
                                }
                                else
                                {
                                    Klass = G<"unittest.mock.MagicMock">();
                                }
                            }
                            else
                            {
                                Klass = G<"unittest.mock.MagicMock">();
                            }
                            _kwargs = py_dict();
                            if (!spec.is_none())
                            {
                                _kwargs.set_item(S<"spec">(), spec);
                            }
                            if (!spec_set.is_none())
                            {
                                _kwargs.set_item(S<"spec_set">(), spec_set);
                            }
                            // add a name to mocks
                            if ((isinstance(Klass, B<"type">()) && issubclass(Klass, G<"unittest.mock.NonCallableMock">()) && self.attr("attribute").truthy()))
                            {
                                _kwargs.set_item(S<"name">(), self.attr("attribute"));
                            }
                            _kwargs.method("update", { kwargs });
                            new_ = call_ex(Klass, {  }, py{}, _kwargs);
                            if ((inherit.truthy() && (G<"unittest.mock._is_instance_mock">())(new_).truthy()))
                            {
                                // we can only tell if the instance should be callable if the
                                // spec is not a list
                                this_spec = spec;
                                if (!spec_set.is_none())
                                {
                                    this_spec = spec_set;
                                }
                                if (((!(G<"unittest.mock._is_list">())(this_spec).truthy()) && (!(G<"unittest.mock._instance_callable">())(this_spec).truthy())))
                                {
                                    Klass = G<"unittest.mock.NonCallableMagicMock">();
                                }
                                _kwargs.method("pop", { S<"name">() });
                                new_.set_attr("return_value", call_ex(Klass, {  }, py{}, _kwargs, { { "_new_parent", new_ }, { "_new_name", S<"()">() } }));
                            }
                        }
                        else if (!autospec.is_none())
                        {
                            // spec is ignored, new *must* be default, spec_set is treated
                            // as a boolean. Should we check spec is not None and that spec_set
                            // is a bool?
                            if (!new_.is((G<"unittest.mock.DEFAULT">())))
                            {
                                py_raise((B<"TypeError">())(S<"autospec creates the mock for you. Can't specify autospec and new.">()));
                            }
                            if (original.is((G<"unittest.mock.DEFAULT">())))
                            {
                                py_raise((B<"TypeError">())(S<"Can't use 'autospec' with create=True">()));
                            }
                            spec_set = (B<"bool">())(spec_set);
                            if (autospec.is(boolean(true)))
                            {
                                autospec = original;
                            }
                            if ((G<"unittest.mock._is_instance_mock">())(self.attr("target")).truthy())
                            {
                                py_raise((G<"unittest.mock.InvalidSpecError">())(S(std::string{ "Cannot autospec attr " } + self.attr("attribute").repr() + " as the patch target has already been mocked out. [target=" + self.attr("target").repr() + ", attr=" + autospec.repr() + "]")));
                            }
                            if ((G<"unittest.mock._is_instance_mock">())(autospec).truthy())
                            {
                                target_name = (B<"getattr">())(self.attr("target"), S<"__name__">(), self.attr("target"));
                                py_raise((G<"unittest.mock.InvalidSpecError">())(S(std::string{ "Cannot autospec attr " } + self.attr("attribute").repr() + " from target " + target_name.repr() + " as it has already been mocked out. [target=" + self.attr("target").repr() + ", attr=" + autospec.repr() + "]")));
                            }
                            new_ = call_ex((G<"unittest.mock.create_autospec">()), { autospec }, py{}, kwargs, { { "spec_set", spec_set }, { "_name", self.attr("attribute") } });
                        }
                        else if (kwargs.truthy())
                        {
                            // can't set keyword args when we aren't creating the mock
                            // XXXX If new is a Mock we could call new.configure_mock(**kwargs)
                            py_raise((B<"TypeError">())(S<"Can't pass kwargs to a mock we aren't creating">()));
                        }
                        new_attr = new_;
                        self.set_attr("temp_original", original);
                        self.set_attr("is_local", local);
                        self.set_attr("_exit_stack", (Mod<"contextlib">().attr("ExitStack"))());
                        self.set_attr("is_started", boolean(true));
                        try
                        {
                            (B<"setattr">())(self.attr("target"), self.attr("attribute"), new_attr);
                            if (!self.attr("attribute_name").is_none())
                            {
                                extra_args = py_dict();
                                if (self.attr("new").is((G<"unittest.mock.DEFAULT">())))
                                {
                                    extra_args.set_item(self.attr("attribute_name"), new_);
                                }
                                for (py const& item : iterate{ self.attr("additional_patchers") })
                                {
                                    patching = item;
                                    arg = self.attr("_exit_stack").method("enter_context", { patching });
                                    if (patching.attr("new").is((G<"unittest.mock.DEFAULT">())))
                                    {
                                        extra_args.method("update", { arg });
                                    }
                                }
                                return extra_args;
                            }
                            return new_;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler57;
                            {
                                if (!call_ex(self.attr("__exit__"), {  }, (Mod<"sys">().attr("exc_info"))(), py{}).truthy())
                                {
                                    py_reraise(handler57.exception);
                                }
                            }
                        }
                        return none();
                    }) },
                { "__exit__", w.method("_patch", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_info = a[1];
                        py exit_stack;
                        if (!self.attr("is_started").truthy())
                        {
                            return none();
                        }
                        if ((self.attr("is_local").truthy() && (!self.attr("temp_original").is((G<"unittest.mock.DEFAULT">())))))
                        {
                            (B<"setattr">())(self.attr("target"), self.attr("attribute"), self.attr("temp_original"));
                        }
                        else
                        {
                            (B<"delattr">())(self.attr("target"), self.attr("attribute"));
                            if (((!self.attr("create").truthy()) && ([&]() -> py { py const t = boolean(!(B<"hasattr">())(self.attr("target"), self.attr("attribute")).truthy()); return t.truthy() ? t : boolean(py_tuple({ S<"__doc__">(), S<"__module__">(), S<"__defaults__">(), S<"__annotations__">(), S<"__kwdefaults__">() }).contains(self.attr("attribute"))); }()).truthy()))
                            {
                                // needed for proxy objects like django settings
                                (B<"setattr">())(self.attr("target"), self.attr("attribute"), self.attr("temp_original"));
                            }
                        }
                        del_attr(self, "temp_original");
                        del_attr(self, "is_local");
                        del_attr(self, "target");
                        exit_stack = self.attr("_exit_stack");
                        del_attr(self, "_exit_stack");
                        self.set_attr("is_started", boolean(false));
                        return call_ex(exit_stack.attr("__exit__"), {  }, exc_info, py{});
                    }, true) },
                { "start", w.method("_patch", "start", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result;
                        result = self.method("__enter__");
                        self.attr("_active_patches").method("append", { self });
                        return result;
                    }) },
                { "stop", w.method("_patch", "stop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            self.attr("_active_patches").method("remove", { self });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler59;
                            if (matches(handler59.exception, B<"ValueError">()))
                            {
                                // If the patch hasn't been started this will fail
                                return none();
                            }
                            else
                                py_reraise(handler59.exception);
                        }
                        return self.method("__exit__", { none(), none(), none() });
                    }) }, }));

            define("unittest.mock", "_get_target", w.function("_get_target", "_get_target", { py_positional("target") }, [](arguments a) -> py
                {
                    py target = a[0];
                    py attribute;
                    try
                    {
                        auto const items61 = unpack(target.method("rsplit", { S<".">(), I<1>() }), 2);
                        target = items61[0];
                        attribute = items61[1];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler60;
                        if (matches(handler60.exception, py_tuple({ B<"TypeError">(), B<"ValueError">(), B<"AttributeError">() })))
                        {
                            py_raise((B<"TypeError">())(S(std::string{ "Need a valid target to patch. You supplied: " } + target.repr())));
                        }
                        else
                            py_reraise(handler60.exception);
                    }
                    return py_tuple({ (Mod<"functools">().attr("partial"))(Mod<"pkgutil">().attr("resolve_name"), target), attribute });
                }));

            define("unittest.mock", "_patch_object", w.function("_patch_object", "_patch_object", { py_positional("target"), py_positional("attribute"), py_positional("new", G<"unittest.mock.DEFAULT">()), py_positional("spec", none()), py_positional("create", boolean(false)), py_positional("spec_set", none()), py_positional("autospec", none()), py_positional("new_callable", none()), py_keyword("unsafe", boolean(false)) }, [](arguments a) -> py
                {
                    auto const target = std::make_shared<py>(a[0]);
                    py attribute = a[1];
                    py new_ = a[2];
                    py spec = a[3];
                    py create = a[4];
                    py spec_set = a[5];
                    py autospec = a[6];
                    py new_callable = a[7];
                    py unsafe = a[8];
                    py kwargs = a[9];
                    py getter;
                    if ((B<"type">())((*target)).is((B<"str">())))
                    {
                        py_raise((B<"TypeError">())(S((*target).repr() + " must be the actual object to be patched, not a str")));
                    }
                    getter = w.function("<lambda>", "_patch_object.<locals>.<lambda>", {  }, [target](arguments a) -> py
    {
        return (*target);
    });
                    return (G<"unittest.mock._patch">()).call({ getter, attribute, new_, spec, create, spec_set, autospec, new_callable, kwargs }, { { "unsafe", unsafe } });
                }, false, true));

            define("unittest.mock", "_patch_multiple", w.function("_patch_multiple", "_patch_multiple", { py_positional("target"), py_positional("spec", none()), py_positional("create", boolean(false)), py_positional("spec_set", none()), py_positional("autospec", none()), py_positional("new_callable", none()) }, [](arguments a) -> py
                {
                    auto const target = std::make_shared<py>(a[0]);
                    py spec = a[1];
                    py create = a[2];
                    py spec_set = a[3];
                    py autospec = a[4];
                    py new_callable = a[5];
                    py kwargs = a[6];
                    py attribute;
                    py getter;
                    py items;
                    py new_;
                    py patcher;
                    py this_patcher;
                    if ((B<"type">())((*target)).is((B<"str">())))
                    {
                        getter = (Mod<"functools">().attr("partial"))(Mod<"pkgutil">().attr("resolve_name"), (*target));
                    }
                    else
                    {
                        getter = w.function("<lambda>", "_patch_multiple.<locals>.<lambda>", {  }, [target](arguments a) -> py
        {
            return (*target);
        });
                    }
                    if (!kwargs.truthy())
                    {
                        py_raise((B<"ValueError">())(S<"Must supply at least one keyword argument with patch.multiple">()));
                    }
                    // need to wrap in a list for python 3, where items is a view
                    items = (B<"list">())(kwargs.method("items"));
                    auto const items62 = unpack(items[I<0>()], 2);
                    attribute = items62[0];
                    new_ = items62[1];
                    patcher = (G<"unittest.mock._patch">())(getter, attribute, new_, spec, create, spec_set, autospec, new_callable, py_dict());
                    patcher.set_attr("attribute_name", attribute);
                    for (py const& item : iterate{ items[B<"slice">()(I<1>(), none(), none())] })
                    {
                        auto const items64 = unpack(item, 2);
                        attribute = items64[0];
                        new_ = items64[1];
                        this_patcher = (G<"unittest.mock._patch">())(getter, attribute, new_, spec, create, spec_set, autospec, new_callable, py_dict());
                        this_patcher.set_attr("attribute_name", attribute);
                        patcher.attr("additional_patchers").method("append", { this_patcher });
                    }
                    return patcher;
                }, false, true));

            define("unittest.mock", "patch", w.function("patch", "patch", { py_positional("target"), py_positional("new", G<"unittest.mock.DEFAULT">()), py_positional("spec", none()), py_positional("create", boolean(false)), py_positional("spec_set", none()), py_positional("autospec", none()), py_positional("new_callable", none()), py_keyword("unsafe", boolean(false)) }, [](arguments a) -> py
                {
                    py target = a[0];
                    py new_ = a[1];
                    py spec = a[2];
                    py create = a[3];
                    py spec_set = a[4];
                    py autospec = a[5];
                    py new_callable = a[6];
                    py unsafe = a[7];
                    py kwargs = a[8];
                    py attribute;
                    py getter;
                    auto const items65 = unpack((G<"unittest.mock._get_target">())(target), 2);
                    getter = items65[0];
                    attribute = items65[1];
                    return (G<"unittest.mock._patch">()).call({ getter, attribute, new_, spec, create, spec_set, autospec, new_callable, kwargs }, { { "unsafe", unsafe } });
                }, false, true));

            define("unittest.mock", "_patch_dict", w.make_class("_patch_dict", { B<"object">() }, {
                { "__init__", w.method("_patch_dict", "__init__", { py_positional("in_dict"), py_positional("values", py_tuple()), py_positional("clear", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py in_dict = a[1];
                        py values = a[2];
                        py clear = a[3];
                        py kwargs = a[4];
                        self.set_attr("in_dict", in_dict);
                        // support any argument supported by dict(...) constructor
                        self.set_attr("values", (B<"dict">())(values));
                        self.attr("values").method("update", { kwargs });
                        self.set_attr("clear", clear);
                        self.set_attr("_original", none());
                        return none();
                    }, false, true) },
                { "__call__", w.method("_patch_dict", "__call__", { py_positional("f") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f = a[1];
                        if (isinstance(f, B<"type">()))
                        {
                            return self.method("decorate_class", { f });
                        }
                        if ((Mod<"inspect">().attr("iscoroutinefunction"))(f).truthy())
                        {
                            return self.method("decorate_async_callable", { f });
                        }
                        return self.method("decorate_callable", { f });
                    }) },
                { "decorate_callable", w.method("_patch_dict", "decorate_callable", { py_positional("f") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const f = std::make_shared<py>(a[1]);
                        py _inner;
                        _inner = (Mod<"functools">().attr("wraps"))((*f))(w.function("_inner", "_patch_dict.decorate_callable.<locals>._inner", {  }, [f, self](arguments a) -> py
    {
        py args = a[0];
        py kw = a[1];
        (*self).method("_patch_dict");
        {
            std::optional<py> pending66;
            [[maybe_unused]] int flow66 = 0;
            [[maybe_unused]] py result66;
            try
            {
                result66 = call_ex((*f), {  }, args, kw);
                flow66 = 1;
                goto finally66;
            }
            catch (raised const&)
            {
                pending66 = py_take_exception();
            }
            finally66:
            (*self).method("_unpatch_dict");
            if (pending66)
                py_reraise(*pending66);
            if (flow66 == 1)
                return result66;
        }
        return none();
    }, true, true));
                        return _inner;
                    }) },
                { "decorate_async_callable", w.method("_patch_dict", "decorate_async_callable", { py_positional("f") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const f = std::make_shared<py>(a[1]);
                        py _inner;
                        _inner = (Mod<"functools">().attr("wraps"))((*f))(async_function(w, "_patch_dict.decorate_async_callable.<locals>._inner", {  }, [f, self](arguments a) -> py
    {
        py args = a[0];
        py kw = a[1];
        (*self).method("_patch_dict");
        {
            std::optional<py> pending67;
            [[maybe_unused]] int flow67 = 0;
            [[maybe_unused]] py result67;
            try
            {
                result67 = py_await(call_ex((*f), {  }, args, kw));
                flow67 = 1;
                goto finally67;
            }
            catch (raised const&)
            {
                pending67 = py_take_exception();
            }
            finally67:
            (*self).method("_unpatch_dict");
            if (pending67)
                py_reraise(*pending67);
            if (flow67 == 1)
                return result67;
        }
        return none();
    }, true, true));
                        return _inner;
                    }) },
                { "decorate_class", w.method("_patch_dict", "decorate_class", { py_positional("klass") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py klass = a[1];
                        py attr;
                        py attr_value;
                        py decorated;
                        py decorator;
                        for (py const& item : iterate{ (B<"dir">())(klass) })
                        {
                            attr = item;
                            attr_value = (B<"getattr">())(klass, attr);
                            if ((attr.method("startswith", { (G<"unittest.mock.patch">()).attr("TEST_PREFIX") }).truthy() && has_attr(attr_value, "__call__")))
                            {
                                decorator = (G<"unittest.mock._patch_dict">())(self.attr("in_dict"), self.attr("values"), self.attr("clear"));
                                decorated = decorator(attr_value);
                                (B<"setattr">())(klass, attr, decorated);
                            }
                        }
                        return klass;
                    }) },
                { "__enter__", w.method("_patch_dict", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_patch_dict");
                        return self.attr("in_dict");
                    }) },
                { "_patch_dict", w.method("_patch_dict", "_patch_dict", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py clear;
                        py in_dict;
                        py key;
                        py original;
                        py values;
                        values = self.attr("values");
                        if (isinstance(self.attr("in_dict"), B<"str">()))
                        {
                            self.set_attr("in_dict", (Mod<"pkgutil">().attr("resolve_name"))(self.attr("in_dict")));
                        }
                        in_dict = self.attr("in_dict");
                        clear = self.attr("clear");
                        try
                        {
                            original = in_dict.method("copy");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler69;
                            if (matches(handler69.exception, B<"AttributeError">()))
                            {
                                // dict like object with no copy method
                                // must support iteration over keys
                                original = py_dict();
                                for (py const& item : iterate{ in_dict })
                                {
                                    key = item;
                                    original.set_item(key, in_dict[key]);
                                }
                            }
                            else
                                py_reraise(handler69.exception);
                        }
                        self.set_attr("_original", original);
                        if (clear.truthy())
                        {
                            (G<"unittest.mock._clear_dict">())(in_dict);
                        }
                        try
                        {
                            in_dict.method("update", { values });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler71;
                            if (matches(handler71.exception, B<"AttributeError">()))
                            {
                                // dict like object with no update method
                                for (py const& item : iterate{ values })
                                {
                                    key = item;
                                    in_dict.set_item(key, values[key]);
                                }
                            }
                            else
                                py_reraise(handler71.exception);
                        }
                        return none();
                    }) },
                { "_unpatch_dict", w.method("_patch_dict", "_unpatch_dict", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py in_dict;
                        py key;
                        py original;
                        in_dict = self.attr("in_dict");
                        original = self.attr("_original");
                        (G<"unittest.mock._clear_dict">())(in_dict);
                        try
                        {
                            in_dict.method("update", { original });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler73;
                            if (matches(handler73.exception, B<"AttributeError">()))
                            {
                                for (py const& item : iterate{ original })
                                {
                                    key = item;
                                    in_dict.set_item(key, original[key]);
                                }
                            }
                            else
                                py_reraise(handler73.exception);
                        }
                        return none();
                    }) },
                { "__exit__", w.method("_patch_dict", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        if (!self.attr("_original").is_none())
                        {
                            self.method("_unpatch_dict");
                        }
                        return boolean(false);
                    }, true) },
                { "start", w.method("_patch_dict", "start", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result;
                        result = self.method("__enter__");
                        (G<"unittest.mock._patch">()).attr("_active_patches").method("append", { self });
                        return result;
                    }) },
                { "stop", w.method("_patch_dict", "stop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            (G<"unittest.mock._patch">()).attr("_active_patches").method("remove", { self });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler75;
                            if (matches(handler75.exception, B<"ValueError">()))
                            {
                                // If the patch hasn't been started this will fail
                                return none();
                            }
                            else
                                py_reraise(handler75.exception);
                        }
                        return self.method("__exit__", { none(), none(), none() });
                    }) }, }));

            define("unittest.mock", "_clear_dict", w.function("_clear_dict", "_clear_dict", { py_positional("in_dict") }, [](arguments a) -> py
                {
                    py in_dict = a[0];
                    py key;
                    py keys;
                    try
                    {
                        in_dict.method("clear");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler76;
                        if (matches(handler76.exception, B<"AttributeError">()))
                        {
                            keys = (B<"list">())(in_dict);
                            for (py const& item : iterate{ keys })
                            {
                                key = item;
                                del_item(in_dict, key);
                            }
                        }
                        else
                            py_reraise(handler76.exception);
                    }
                    return none();
                }));

            define("unittest.mock", "_patch_stopall", w.function("_patch_stopall", "_patch_stopall", {  }, [](arguments a) -> py
                {
                    py patch;
                    for (py const& item : iterate{ (B<"reversed">())((G<"unittest.mock._patch">()).attr("_active_patches")) })
                    {
                        patch = item;
                        patch.method("stop");
                    }
                    return none();
                }));

            (G<"unittest.mock.patch">()).set_attr("object", G<"unittest.mock._patch_object">());
            (G<"unittest.mock.patch">()).set_attr("dict", G<"unittest.mock._patch_dict">());
            (G<"unittest.mock.patch">()).set_attr("multiple", G<"unittest.mock._patch_multiple">());
            (G<"unittest.mock.patch">()).set_attr("stopall", G<"unittest.mock._patch_stopall">());
            (G<"unittest.mock.patch">()).set_attr("TEST_PREFIX", S<"test">());
            define("unittest.mock", "magic_methods", S<"lt le gt ge eq ne getitem setitem delitem len contains iter hash str sizeof enter exit divmod rdivmod neg pos abs invert complex int float index round trunc floor ceil bool next fspath aiter ">());
            // we added divmod and rdivmod here instead of numerics
            // because there is no idivmod
            define("unittest.mock", "numerics", S<"add sub mul matmul truediv floordiv mod lshift rshift and xor or pow">());
            define("unittest.mock", "inplace", (S<" ">()).method("join", { [&]() -> py { py const comprehension79 = py_list(); py n; for (py const& item : iterate{ (G<"unittest.mock.numerics">()).method("split") }) { n = item; comprehension79.method("append", { (S<"i%s">()) % n }); } return comprehension79; }() }));
            define("unittest.mock", "right", (S<" ">()).method("join", { [&]() -> py { py const comprehension80 = py_list(); py n; for (py const& item : iterate{ (G<"unittest.mock.numerics">()).method("split") }) { n = item; comprehension80.method("append", { (S<"r%s">()) % n }); } return comprehension80; }() }));
            // not including __prepare__, __instancecheck__, __subclasscheck__
            // (as they are metaclass methods)
            // __del__ is not supported at all as it causes problems if it exists
            define("unittest.mock", "_non_defaults", set_of({ S<"__get__">(), S<"__set__">(), S<"__delete__">(), S<"__reversed__">(), S<"__missing__">(), S<"__reduce__">(), S<"__reduce_ex__">(), S<"__getinitargs__">(), S<"__getnewargs__">(), S<"__getstate__">(), S<"__setstate__">(), S<"__getformat__">(), S<"__repr__">(), S<"__dir__">(), S<"__subclasses__">(), S<"__format__">(), S<"__getnewargs_ex__">() }));
            define("unittest.mock", "_get_method", w.function("_get_method", "_get_method", { py_positional("name"), py_positional("func") }, [](arguments a) -> py
                {
                    py name = a[0];
                    auto const func = std::make_shared<py>(a[1]);
                    py method;
                    method = w.function("method", "_get_method.<locals>.method", { py_positional_only("self") }, [func](arguments a) -> py
    {
        py self = a[0];
        py args = a[1];
        py kw = a[2];
        return call_ex((*func), { self }, args, kw);
    }, true, true);
                    method.set_attr("__name__", name);
                    return method;
                }));

            define("unittest.mock", "_magics", [&]() -> py { py const comprehension81 = B<"set">()(); py method; for (py const& item : iterate{ (S<" ">()).method("join", { py_list({ G<"unittest.mock.magic_methods">(), G<"unittest.mock.numerics">(), G<"unittest.mock.inplace">(), G<"unittest.mock.right">() }) }).method("split") }) { method = item; comprehension81.method("add", { (S<"__%s__">()) % method }); } return comprehension81; }());
            // Magic methods used for async `with` statements
            define("unittest.mock", "_async_method_magics", set_of({ S<"__aenter__">(), S<"__aexit__">(), S<"__anext__">() }));
            // Magic methods that are only used with async calls but are synchronous functions themselves
            define("unittest.mock", "_sync_async_magics", set_of({ S<"__aiter__">() }));
            define("unittest.mock", "_async_magics", (G<"unittest.mock._async_method_magics">()) | (G<"unittest.mock._sync_async_magics">()));
            define("unittest.mock", "_all_sync_magics", (G<"unittest.mock._magics">()) | (G<"unittest.mock._non_defaults">()));
            define("unittest.mock", "_all_magics", (G<"unittest.mock._all_sync_magics">()) | (G<"unittest.mock._async_magics">()));
            define("unittest.mock", "_unsupported_magics", set_of({ S<"__getattr__">(), S<"__setattr__">(), S<"__init__">(), S<"__new__">(), S<"__prepare__">(), S<"__instancecheck__">(), S<"__subclasscheck__">(), S<"__del__">() }));
            define("unittest.mock", "_calculate_return_value", dict_of({ { S<"__hash__">(), w.function("<lambda>", "<lambda>", { py_positional("self") }, [](arguments a) -> py
                {
                    py self = a[0];
                    return (B<"object">()).method("__hash__", { self });
                }) }, { S<"__str__">(), w.function("<lambda>", "<lambda>", { py_positional("self") }, [](arguments a) -> py
                {
                    py self = a[0];
                    return (B<"object">()).method("__str__", { self });
                }) }, { S<"__sizeof__">(), w.function("<lambda>", "<lambda>", { py_positional("self") }, [](arguments a) -> py
                {
                    py self = a[0];
                    return (B<"object">()).method("__sizeof__", { self });
                }) }, { S<"__fspath__">(), w.function("<lambda>", "<lambda>", { py_positional("self") }, [](arguments a) -> py
                {
                    py self = a[0];
                    return S((B<"type">())(self).attr("__name__").str() + "/" + self.method("_extract_mock_name").str() + "/" + (B<"id">())(self).str());
                }) } }));
            define("unittest.mock", "_return_values", dict_of({ { S<"__lt__">(), py::borrowed(not_implemented()) }, { S<"__gt__">(), py::borrowed(not_implemented()) }, { S<"__le__">(), py::borrowed(not_implemented()) }, { S<"__ge__">(), py::borrowed(not_implemented()) }, { S<"__int__">(), I<1>() }, { S<"__contains__">(), boolean(false) }, { S<"__len__">(), I<0>() }, { S<"__exit__">(), boolean(false) }, { S<"__complex__">(), py{ make_complex(0.0, 1.0) } }, { S<"__float__">(), F<1.0>() }, { S<"__bool__">(), boolean(true) }, { S<"__index__">(), I<1>() }, { S<"__aexit__">(), boolean(false) } }));
            define("unittest.mock", "_get_eq", w.function("_get_eq", "_get_eq", { py_positional("self") }, [](arguments a) -> py
                {
                    auto const self = std::make_shared<py>(a[0]);
                    py __eq__;
                    __eq__ = w.function("__eq__", "_get_eq.<locals>.__eq__", { py_positional("other") }, [self](arguments a) -> py
    {
        py other = a[0];
        py ret_val;
        ret_val = (*self).attr("__eq__").attr("_mock_return_value");
        if (!ret_val.is((G<"unittest.mock.DEFAULT">())))
        {
            return ret_val;
        }
        if ((*self).is(other))
        {
            return boolean(true);
        }
        return py::borrowed(not_implemented());
    });
                    return __eq__;
                }));

            define("unittest.mock", "_get_ne", w.function("_get_ne", "_get_ne", { py_positional("self") }, [](arguments a) -> py
                {
                    auto const self = std::make_shared<py>(a[0]);
                    py __ne__;
                    __ne__ = w.function("__ne__", "_get_ne.<locals>.__ne__", { py_positional("other") }, [self](arguments a) -> py
    {
        py other = a[0];
        if (!(*self).attr("__ne__").attr("_mock_return_value").is((G<"unittest.mock.DEFAULT">())))
        {
            return G<"unittest.mock.DEFAULT">();
        }
        if ((*self).is(other))
        {
            return boolean(false);
        }
        return py::borrowed(not_implemented());
    });
                    return __ne__;
                }));

            define("unittest.mock", "_get_iter", w.function("_get_iter", "_get_iter", { py_positional("self") }, [](arguments a) -> py
                {
                    auto const self = std::make_shared<py>(a[0]);
                    py __iter__;
                    __iter__ = w.function("__iter__", "_get_iter.<locals>.__iter__", {  }, [self](arguments a) -> py
    {
        py ret_val;
        ret_val = (*self).attr("__iter__").attr("_mock_return_value");
        if (ret_val.is((G<"unittest.mock.DEFAULT">())))
        {
            return (B<"iter">())(py_list());
        }
        // if ret_val was already an iterator, then calling iter on it should
        // return the iterator unchanged
        return (B<"iter">())(ret_val);
    });
                    return __iter__;
                }));

            define("unittest.mock", "_get_async_iter", w.function("_get_async_iter", "_get_async_iter", { py_positional("self") }, [](arguments a) -> py
                {
                    auto const self = std::make_shared<py>(a[0]);
                    py __aiter__;
                    __aiter__ = w.function("__aiter__", "_get_async_iter.<locals>.__aiter__", {  }, [self](arguments a) -> py
    {
        py ret_val;
        ret_val = (*self).attr("__aiter__").attr("_mock_return_value");
        if (ret_val.is((G<"unittest.mock.DEFAULT">())))
        {
            return (G<"unittest.mock._AsyncIterator">())((B<"iter">())(py_list()));
        }
        return (G<"unittest.mock._AsyncIterator">())((B<"iter">())(ret_val));
    });
                    return __aiter__;
                }));

            define("unittest.mock", "_side_effect_methods", dict_of({ { S<"__eq__">(), G<"unittest.mock._get_eq">() }, { S<"__ne__">(), G<"unittest.mock._get_ne">() }, { S<"__iter__">(), G<"unittest.mock._get_iter">() }, { S<"__aiter__">(), G<"unittest.mock._get_async_iter">() } }));
            define("unittest.mock", "_set_return_value", w.function("_set_return_value", "_set_return_value", { py_positional("mock"), py_positional("method"), py_positional("name") }, [](arguments a) -> py
                {
                    py mock = a[0];
                    py method = a[1];
                    py name = a[2];
                    py fixed;
                    py return_calculator;
                    py return_value;
                    py side_effector;
                    fixed = (G<"unittest.mock._return_values">()).method("get", { name, G<"unittest.mock.DEFAULT">() });
                    if (!fixed.is((G<"unittest.mock.DEFAULT">())))
                    {
                        method.set_attr("return_value", fixed);
                        return none();
                    }
                    return_calculator = (G<"unittest.mock._calculate_return_value">()).method("get", { name });
                    if (!return_calculator.is_none())
                    {
                        return_value = return_calculator(mock);
                        method.set_attr("return_value", return_value);
                        return none();
                    }
                    side_effector = (G<"unittest.mock._side_effect_methods">()).method("get", { name });
                    if (!side_effector.is_none())
                    {
                        method.set_attr("side_effect", side_effector(mock));
                    }
                    return none();
                }));

            define("unittest.mock", "MagicMixin", w.make_class("MagicMixin", { G<"unittest.mock.Base">() }, {
                { "__init__", w.method("MagicMixin", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        self.method("_mock_set_magics");
                        call_ex((G<"unittest.mock._safe_super">())(G<"unittest.mock.MagicMixin">(), self).attr("__init__"), {  }, args, kw);
                        self.method("_mock_set_magics");
                        return none();
                    }, true, true) },
                { "_mock_set_magics", w.method("MagicMixin", "_mock_set_magics", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _type;
                        py entry;
                        py orig_magics;
                        py remove_magics;
                        py these_magics;
                        orig_magics = (G<"unittest.mock._magics">()) | (G<"unittest.mock._async_method_magics">());
                        these_magics = orig_magics;
                        if (!(B<"getattr">())(self, S<"_mock_methods">(), none()).is_none())
                        {
                            these_magics = orig_magics.method("intersection", { self.attr("_mock_methods") });
                            remove_magics = (B<"set">())();
                            remove_magics = orig_magics - these_magics;
                            for (py const& item : iterate{ remove_magics })
                            {
                                entry = item;
                                if ((B<"type">())(self).attr("__dict__").contains(entry))
                                {
                                    // remove unneeded magic methods
                                    (B<"delattr">())(self, entry);
                                }
                            }
                        }
                        // don't overwrite existing attributes if called a second time
                        these_magics = these_magics - (B<"set">())((B<"type">())(self).attr("__dict__"));
                        _type = (B<"type">())(self);
                        for (py const& item : iterate{ these_magics })
                        {
                            entry = item;
                            (B<"setattr">())(_type, entry, (G<"unittest.mock.MagicProxy">())(entry, self));
                        }
                        return none();
                    }) }, }));

            define("unittest.mock", "NonCallableMagicMock", w.make_class("NonCallableMagicMock", { G<"unittest.mock.MagicMixin">(), G<"unittest.mock.NonCallableMock">() }, {
                { "mock_add_spec", w.method("NonCallableMagicMock", "mock_add_spec", { py_positional("spec"), py_positional("spec_set", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py spec = a[1];
                        py spec_set = a[2];
                        self.method("_mock_add_spec", { spec, spec_set });
                        self.method("_mock_set_magics");
                        return none();
                    }) }, }));

            define("unittest.mock", "AsyncMagicMixin", w.make_class("AsyncMagicMixin", { G<"unittest.mock.MagicMixin">() }, {
 }));

            define("unittest.mock", "MagicMock", w.make_class("MagicMock", { G<"unittest.mock.MagicMixin">(), G<"unittest.mock.Mock">() }, {
                { "mock_add_spec", w.method("MagicMock", "mock_add_spec", { py_positional("spec"), py_positional("spec_set", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py spec = a[1];
                        py spec_set = a[2];
                        self.method("_mock_add_spec", { spec, spec_set });
                        self.method("_mock_set_magics");
                        return none();
                    }) },
                { "reset_mock", w.method("MagicMock", "reset_mock", { py_keyword("return_value", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py return_value = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        if ((return_value.truthy() && self.attr("_mock_name").truthy() && (G<"unittest.mock._is_magic">())(self.attr("_mock_name")).truthy()))
                        {
                            // Don't reset return values for magic methods,
                            // otherwise `m.__str__` will start
                            // to return `MagicMock` instances, instead of `str` instances.
                            return_value = boolean(false);
                        }
                        call_ex((B<"super">()(G<"unittest.mock.MagicMock">(), self)).attr("reset_mock"), {  }, args, kwargs, { { "return_value", return_value } });
                        return none();
                    }, true, true) }, }));

            define("unittest.mock", "MagicProxy", w.make_class("MagicProxy", { G<"unittest.mock.Base">() }, {
                { "__init__", w.method("MagicProxy", "__init__", { py_positional("name"), py_positional("parent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py parent = a[2];
                        self.set_attr("name", name);
                        self.set_attr("parent", parent);
                        return none();
                    }) },
                { "create_mock", w.method("MagicProxy", "create_mock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entry;
                        py m;
                        py parent;
                        entry = self.attr("name");
                        parent = self.attr("parent");
                        m = parent.method("_get_child_mock", {  }, { { "name", entry }, { "_new_name", entry }, { "_new_parent", parent } });
                        (B<"setattr">())(parent, entry, m);
                        (G<"unittest.mock._set_return_value">())(parent, m, entry);
                        return m;
                    }) },
                { "__get__", w.method("MagicProxy", "__get__", { py_positional("obj"), py_positional("_type", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py _type = a[2];
                        return self.method("create_mock");
                    }) }, }));

            define("unittest.mock", "_CODE_SIG", none());
            define("unittest.mock", "_CODE_ATTRS", none());
            define("unittest.mock", "AsyncMockMixin", w.make_class("AsyncMockMixin", { G<"unittest.mock.Base">() }, {
                { "await_count", (G<"unittest.mock._delegating_property">())(S<"await_count">()) },
                { "await_args", (G<"unittest.mock._delegating_property">())(S<"await_args">()) },
                { "await_args_list", (G<"unittest.mock._delegating_property">())(S<"await_args_list">()) },
                { "__init__", w.method("AsyncMockMixin", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py code_mock;
                        call_ex((B<"super">()(G<"unittest.mock.AsyncMockMixin">(), self)).attr("__init__"), {  }, args, kwargs);
                        // iscoroutinefunction() checks _is_coroutine property to say if an
                        // object is a coroutine. Without this check it looks to see if it is a
                        // function/method, which in this case it is not (since it is an
                        // AsyncMock).
                        // It is set through __dict__ because when spec_set is True, this
                        // attribute is likely undefined.
                        self.attr("__dict__").set_item(S<"_is_coroutine">(), (Mod<"asyncio">().attr("coroutines")).attr("_is_coroutine"));
                        self.attr("__dict__").set_item(S<"_mock_await_count">(), I<0>());
                        self.attr("__dict__").set_item(S<"_mock_await_args">(), none());
                        self.attr("__dict__").set_item(S<"_mock_await_args_list">(), (G<"unittest.mock._CallList">())());
                        if ((G<"unittest.mock._CODE_SIG">()).truthy())
                        {
                            code_mock = (G<"unittest.mock.NonCallableMock">()).call({  }, { { "spec_set", G<"unittest.mock._CODE_ATTRS">() } });
                            code_mock.attr("__dict__").set_item(S<"_spec_class">(), Mod<"types">().attr("CodeType"));
                            code_mock.attr("__dict__").set_item(S<"_spec_signature">(), G<"unittest.mock._CODE_SIG">());
                        }
                        else
                        {
                            code_mock = (G<"unittest.mock.NonCallableMock">()).call({  }, { { "spec_set", Mod<"types">().attr("CodeType") } });
                        }
                        code_mock.set_attr("co_flags", ((Mod<"inspect">().attr("CO_COROUTINE")) + (Mod<"inspect">().attr("CO_VARARGS"))) + (Mod<"inspect">().attr("CO_VARKEYWORDS")));
                        code_mock.set_attr("co_argcount", I<0>());
                        code_mock.set_attr("co_varnames", py_tuple({ S<"args">(), S<"kwargs">() }));
                        code_mock.set_attr("co_posonlyargcount", I<0>());
                        code_mock.set_attr("co_kwonlyargcount", I<0>());
                        self.attr("__dict__").set_item(S<"__code__">(), code_mock);
                        self.attr("__dict__").set_item(S<"__name__">(), S<"AsyncMock">());
                        self.attr("__dict__").set_item(S<"__defaults__">(), (B<"tuple">())());
                        self.attr("__dict__").set_item(S<"__kwdefaults__">(), py_dict());
                        self.attr("__dict__").set_item(S<"__annotations__">(), none());
                        return none();
                    }, true, true) },
                { "_execute_mock_call", async_method(w, "AsyncMockMixin", "_execute_mock_call", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py _call;
                        py effect;
                        py result;
                        _call = (G<"unittest.mock._Call">()).call({ py_tuple({ args, kwargs }) }, { { "two", boolean(true) } });
                        {
                            py const target84 = self;
                            target84.set_attr("await_count", inplace(NEOS_PY_ADD, target84.attr("await_count"), I<1>()));
                        }
                        self.set_attr("await_args", _call);
                        self.attr("await_args_list").method("append", { _call });
                        effect = self.attr("side_effect");
                        if (!effect.is_none())
                        {
                            if ((G<"unittest.mock._is_exception">())(effect).truthy())
                            {
                                py_raise(effect);
                            }
                            else if (!(G<"unittest.mock._callable">())(effect).truthy())
                            {
                                try
                                {
                                    result = (B<"next">())(effect);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler85;
                                    if (matches(handler85.exception, B<"StopIteration">()))
                                    {
                                        // It is impossible to propagate a StopIteration
                                        // through coroutines because of PEP 479
                                        py_raise(B<"StopAsyncIteration">());
                                    }
                                    else
                                        py_reraise(handler85.exception);
                                }
                                if ((G<"unittest.mock._is_exception">())(result).truthy())
                                {
                                    py_raise(result);
                                }
                            }
                            else if ((Mod<"asyncio">().attr("iscoroutinefunction"))(effect).truthy())
                            {
                                result = py_await(call_ex(effect, {  }, args, kwargs));
                            }
                            else
                            {
                                result = call_ex(effect, {  }, args, kwargs);
                            }
                            if (!result.is((G<"unittest.mock.DEFAULT">())))
                            {
                                return result;
                            }
                        }
                        if (!self.attr("_mock_return_value").is((G<"unittest.mock.DEFAULT">())))
                        {
                            return self.attr("return_value");
                        }
                        if (!self.attr("_mock_wraps").is_none())
                        {
                            if ((Mod<"asyncio">().attr("iscoroutinefunction"))(self.attr("_mock_wraps")).truthy())
                            {
                                return py_await(call_ex(self.attr("_mock_wraps"), {  }, args, kwargs));
                            }
                            return call_ex(self.attr("_mock_wraps"), {  }, args, kwargs);
                        }
                        return self.attr("return_value");
                    }, true, true) },
                { "assert_awaited", w.method("AsyncMockMixin", "assert_awaited", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        if (self.attr("await_count") == (I<0>()))
                        {
                            msg = S(std::string{ "Expected " } + ([&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }()).str() + " to have been awaited.");
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return none();
                    }) },
                { "assert_awaited_once", w.method("AsyncMockMixin", "assert_awaited_once", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        if (!(self.attr("await_count") == (I<1>())))
                        {
                            msg = S(std::string{ "Expected " } + ([&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }()).str() + " to have been awaited once. Awaited " + self.attr("await_count").str() + " times.");
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return none();
                    }) },
                { "assert_awaited_with", w.method("AsyncMockMixin", "assert_awaited_with", {  }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const args = std::make_shared<py>(a[1]);
                        auto const kwargs = std::make_shared<py>(a[2]);
                        py _error_message;
                        py actual;
                        py cause;
                        py expected;
                        if ((*self).attr("await_args").is_none())
                        {
                            expected = (*self).method("_format_mock_call_signature", { (*args), (*kwargs) });
                            py_raise((B<"AssertionError">())(S(std::string{ "Expected await: " } + expected.str() + "\nNot awaited")));
                        }
                        _error_message = w.function("_error_message", "AsyncMockMixin.assert_awaited_with.<locals>._error_message", {  }, [args, kwargs, self](arguments a) -> py
    {
        py msg;
        msg = (*self).method("_format_mock_failure_message", { (*args), (*kwargs) }, { { "action", S<"await">() } });
        return msg;
    });
                        expected = (*self).method("_call_matcher", { (G<"unittest.mock._Call">()).call({ py_tuple({ (*args), (*kwargs) }) }, { { "two", boolean(true) } }) });
                        actual = (*self).method("_call_matcher", { (*self).attr("await_args") });
                        if (actual != expected)
                        {
                            cause = (isinstance(expected, B<"Exception">()) ? expected : none());
                            fn_raise_from((B<"AssertionError">())(_error_message()).get(), cause.get());
                            throw raised{};
                        }
                        return none();
                    }, true, true) },
                { "assert_awaited_once_with", w.method("AsyncMockMixin", "assert_awaited_once_with", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py msg;
                        if (!(self.attr("await_count") == (I<1>())))
                        {
                            msg = S(std::string{ "Expected " } + ([&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }()).str() + " to have been awaited once. Awaited " + self.attr("await_count").str() + " times.");
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return call_ex(self.attr("assert_awaited_with"), {  }, args, kwargs);
                    }, true, true) },
                { "assert_any_await", w.method("AsyncMockMixin", "assert_any_await", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py actual;
                        py cause;
                        py expected;
                        py expected_string;
                        expected = self.method("_call_matcher", { (G<"unittest.mock._Call">()).call({ py_tuple({ args, kwargs }) }, { { "two", boolean(true) } }) });
                        cause = (isinstance(expected, B<"Exception">()) ? expected : none());
                        actual = [&]() -> py { py const comprehension86 = py_list(); py c; for (py const& item : iterate{ self.attr("await_args_list") }) { c = item; comprehension86.method("append", { self.method("_call_matcher", { c }) }); } return comprehension86; }();
                        if ((cause.truthy() || (!(G<"unittest.mock._AnyComparer">())(actual).contains(expected))))
                        {
                            expected_string = self.method("_format_mock_call_signature", { args, kwargs });
                            fn_raise_from((B<"AssertionError">())((S<"%s await not found">()) % expected_string).get(), cause.get());
                            throw raised{};
                        }
                        return none();
                    }, true, true) },
                { "assert_has_awaits", w.method("AsyncMockMixin", "assert_has_awaits", { py_positional("calls"), py_positional("any_order", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py calls = a[1];
                        py any_order = a[2];
                        py all_awaits;
                        py cause;
                        py expected;
                        py kall;
                        py not_found;
                        py problem;
                        expected = [&]() -> py { py const comprehension87 = py_list(); py c; for (py const& item : iterate{ calls }) { c = item; comprehension87.method("append", { self.method("_call_matcher", { c }) }); } return comprehension87; }();
                        cause = (B<"next">())([&]() -> py { py const comprehension88 = py_list(); py e; for (py const& item : iterate{ expected }) { e = item; if (!isinstance(e, B<"Exception">())) continue; comprehension88.method("append", { e }); } return comprehension88; }(), none());
                        all_awaits = (G<"unittest.mock._CallList">())([&]() -> py { py const comprehension89 = py_list(); py c; for (py const& item : iterate{ self.attr("await_args_list") }) { c = item; comprehension89.method("append", { self.method("_call_matcher", { c }) }); } return comprehension89; }());
                        if (!any_order.truthy())
                        {
                            if (!all_awaits.contains(expected))
                            {
                                if (cause.is_none())
                                {
                                    problem = S<"Awaits not found.">();
                                }
                                else
                                {
                                    problem = (S<"Error processing expected awaits.\nErrors: {}">()).method("format", { [&]() -> py { py const comprehension90 = py_list(); py e; for (py const& item : iterate{ expected }) { e = item; comprehension90.method("append", { (isinstance(e, B<"Exception">()) ? e : none()) }); } return comprehension90; }() });
                                }
                                fn_raise_from((B<"AssertionError">())(S(problem.str() + "\nExpected: " + (G<"unittest.mock._CallList">())(calls).str() + "\nActual: " + self.attr("await_args_list").str())).get(), cause.get());
                                throw raised{};
                            }
                            return none();
                        }
                        all_awaits = (B<"list">())(all_awaits);
                        not_found = py_list();
                        for (py const& item : iterate{ expected })
                        {
                            kall = item;
                            try
                            {
                                all_awaits.method("remove", { kall });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler92;
                                if (matches(handler92.exception, B<"ValueError">()))
                                {
                                    not_found.method("append", { kall });
                                }
                                else
                                    py_reraise(handler92.exception);
                            }
                        }
                        if (not_found.truthy())
                        {
                            fn_raise_from((B<"AssertionError">())((S<"%r not all found in await list">()) % py_tuple({ (B<"tuple">())(not_found) })).get(), cause.get());
                            throw raised{};
                        }
                        return none();
                    }) },
                { "assert_not_awaited", w.method("AsyncMockMixin", "assert_not_awaited", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg;
                        if (self.attr("await_count") != (I<0>()))
                        {
                            msg = S(std::string{ "Expected " } + ([&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }()).str() + " to not have been awaited. Awaited " + self.attr("await_count").str() + " times.");
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return none();
                    }) },
                { "reset_mock", w.method("AsyncMockMixin", "reset_mock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        call_ex((B<"super">()(G<"unittest.mock.AsyncMockMixin">(), self)).attr("reset_mock"), {  }, args, kwargs);
                        self.set_attr("await_count", I<0>());
                        self.set_attr("await_args", none());
                        self.set_attr("await_args_list", (G<"unittest.mock._CallList">())());
                        return none();
                    }, true, true) }, }));

            define("unittest.mock", "AsyncMock", w.make_class("AsyncMock", { G<"unittest.mock.AsyncMockMixin">(), G<"unittest.mock.AsyncMagicMixin">(), G<"unittest.mock.Mock">() }, {
 }));

            define("unittest.mock", "_ANY", w.make_class("_ANY", { B<"object">() }, {
                { "__eq__", w.method("_ANY", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(true);
                    }) },
                { "__ne__", w.method("_ANY", "__ne__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(false);
                    }) },
                { "__repr__", w.method("_ANY", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"<ANY>">();
                    }) }, }));

            define("unittest.mock", "ANY", (G<"unittest.mock._ANY">())());
            define("unittest.mock", "_format_call_signature", w.function("_format_call_signature", "_format_call_signature", { py_positional("name"), py_positional("args"), py_positional("kwargs") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py args = a[1];
                    py kwargs = a[2];
                    py args_string;
                    py formatted_args;
                    py kwargs_string;
                    py message;
                    message = (S<"%s(%%s)">()) % name;
                    formatted_args = S<"">();
                    args_string = (S<", ">()).method("join", { [&]() -> py { py const comprehension93 = py_list(); py arg; for (py const& item : iterate{ args }) { arg = item; comprehension93.method("append", { S(arg.repr()) }); } return comprehension93; }() });
                    kwargs_string = (S<", ">()).method("join", { [&]() -> py { py const comprehension94 = py_list(); py key; py value; for (py const& item : iterate{ kwargs.method("items") }) { auto const items95 = unpack(item, 2); key = items95[0]; value = items95[1]; comprehension94.method("append", { (S<"%s=%r">()) % py_tuple({ key, value }) }); } return comprehension94; }() });
                    if (args_string.truthy())
                    {
                        formatted_args = args_string;
                    }
                    if (kwargs_string.truthy())
                    {
                        if (formatted_args.truthy())
                        {
                            formatted_args = inplace(NEOS_PY_ADD, formatted_args, S<", ">());
                        }
                        formatted_args = inplace(NEOS_PY_ADD, formatted_args, kwargs_string);
                    }
                    return message % formatted_args;
                }));

            define("unittest.mock", "_Call", w.make_class("_Call", { B<"tuple">() }, {
                { "__new__", w.method("_Call", "__new__", { py_positional("value", py_tuple()), py_positional("name", S<"">()), py_positional("parent", none()), py_positional("two", boolean(false)), py_positional("from_kall", boolean(true)) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py name = a[2];
                        py parent = a[3];
                        py two = a[4];
                        py from_kall = a[5];
                        py _len;
                        py args;
                        py first;
                        py kwargs;
                        py second;
                        args = py_tuple();
                        kwargs = py_dict();
                        _len = py{ value.size() };
                        if (_len == (I<3>()))
                        {
                            auto const items96 = unpack(value, 3);
                            name = items96[0];
                            args = items96[1];
                            kwargs = items96[2];
                        }
                        else if (_len == (I<2>()))
                        {
                            auto const items97 = unpack(value, 2);
                            first = items97[0];
                            second = items97[1];
                            if (isinstance(first, B<"str">()))
                            {
                                name = first;
                                if (isinstance(second, B<"tuple">()))
                                {
                                    args = second;
                                }
                                else
                                {
                                    kwargs = second;
                                }
                            }
                            else
                            {
                                auto const items98 = unpack(py_tuple({ first, second }), 2);
                                args = items98[0];
                                kwargs = items98[1];
                            }
                        }
                        else if (_len == (I<1>()))
                        {
                            auto const items99 = unpack(value, 1);
                            value = items99[0];
                            if (isinstance(value, B<"str">()))
                            {
                                name = value;
                            }
                            else if (isinstance(value, B<"tuple">()))
                            {
                                args = value;
                            }
                            else
                            {
                                kwargs = value;
                            }
                        }
                        if (two.truthy())
                        {
                            return (B<"tuple">()).method("__new__", { cls, py_tuple({ args, kwargs }) });
                        }
                        return (B<"tuple">()).method("__new__", { cls, py_tuple({ name, args, kwargs }) });
                    }) },
                { "__init__", w.method("_Call", "__init__", { py_positional("value", py_tuple()), py_positional("name", none()), py_positional("parent", none()), py_positional("two", boolean(false)), py_positional("from_kall", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py name = a[2];
                        py parent = a[3];
                        py two = a[4];
                        py from_kall = a[5];
                        self.set_attr("_mock_name", name);
                        self.set_attr("_mock_parent", parent);
                        self.set_attr("_mock_from_kall", from_kall);
                        return none();
                    }) },
                { "__eq__", w.method("_Call", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py first;
                        py len_other;
                        py other_args;
                        py other_kwargs;
                        py other_name;
                        py second;
                        py self_args;
                        py self_kwargs;
                        py self_name;
                        py value;
                        try
                        {
                            len_other = py{ other.size() };
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler100;
                            if (matches(handler100.exception, B<"TypeError">()))
                            {
                                return py::borrowed(not_implemented());
                            }
                            else
                                py_reraise(handler100.exception);
                        }
                        self_name = S<"">();
                        if (py{ self.size() } == (I<2>()))
                        {
                            auto const items101 = unpack(self, 2);
                            self_args = items101[0];
                            self_kwargs = items101[1];
                        }
                        else
                        {
                            auto const items102 = unpack(self, 3);
                            self_name = items102[0];
                            self_args = items102[1];
                            self_kwargs = items102[2];
                        }
                        if (((B<"getattr">())(self, S<"_mock_parent">(), none()).truthy() && (B<"getattr">())(other, S<"_mock_parent">(), none()).truthy() && (self.attr("_mock_parent") != other.attr("_mock_parent"))))
                        {
                            return boolean(false);
                        }
                        other_name = S<"">();
                        if (len_other == (I<0>()))
                        {
                            auto const items103 = unpack(py_tuple({ py_tuple(), py_dict() }), 2);
                            other_args = items103[0];
                            other_kwargs = items103[1];
                        }
                        else if (len_other == (I<3>()))
                        {
                            auto const items104 = unpack(other, 3);
                            other_name = items104[0];
                            other_args = items104[1];
                            other_kwargs = items104[2];
                        }
                        else if (len_other == (I<1>()))
                        {
                            auto const items105 = unpack(other, 1);
                            value = items105[0];
                            if (isinstance(value, B<"tuple">()))
                            {
                                other_args = value;
                                other_kwargs = py_dict();
                            }
                            else if (isinstance(value, B<"str">()))
                            {
                                other_name = value;
                                auto const items106 = unpack(py_tuple({ py_tuple(), py_dict() }), 2);
                                other_args = items106[0];
                                other_kwargs = items106[1];
                            }
                            else
                            {
                                other_args = py_tuple();
                                other_kwargs = value;
                            }
                        }
                        else if (len_other == (I<2>()))
                        {
                            // could be (name, args) or (name, kwargs) or (args, kwargs)
                            auto const items107 = unpack(other, 2);
                            first = items107[0];
                            second = items107[1];
                            if (isinstance(first, B<"str">()))
                            {
                                other_name = first;
                                if (isinstance(second, B<"tuple">()))
                                {
                                    auto const items108 = unpack(py_tuple({ second, py_dict() }), 2);
                                    other_args = items108[0];
                                    other_kwargs = items108[1];
                                }
                                else
                                {
                                    auto const items109 = unpack(py_tuple({ py_tuple(), second }), 2);
                                    other_args = items109[0];
                                    other_kwargs = items109[1];
                                }
                            }
                            else
                            {
                                auto const items110 = unpack(py_tuple({ first, second }), 2);
                                other_args = items110[0];
                                other_kwargs = items110[1];
                            }
                        }
                        else
                        {
                            return boolean(false);
                        }
                        if ((self_name.truthy() && (other_name != self_name)))
                        {
                            return boolean(false);
                        }
                        // this order is important for ANY to work!
                        return boolean(py_tuple({ other_args, other_kwargs }) == py_tuple({ self_args, self_kwargs }));
                    }) },
                { "__ne__", (B<"object">()).attr("__ne__") },
                { "__call__", w.method("_Call", "__call__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py name;
                        if (self.attr("_mock_name").is_none())
                        {
                            return (G<"unittest.mock._Call">()).call({ py_tuple({ S<"">(), args, kwargs }) }, { { "name", S<"()">() } });
                        }
                        name = self.attr("_mock_name") + (S<"()">());
                        return (G<"unittest.mock._Call">()).call({ py_tuple({ self.attr("_mock_name"), args, kwargs }) }, { { "name", name }, { "parent", self } });
                    }, true, true) },
                { "__getattr__", w.method("_Call", "__getattr__", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        py name;
                        if (self.attr("_mock_name").is_none())
                        {
                            return (G<"unittest.mock._Call">()).call({  }, { { "name", attr }, { "from_kall", boolean(false) } });
                        }
                        name = (S<"%s.%s">()) % py_tuple({ self.attr("_mock_name"), attr });
                        return (G<"unittest.mock._Call">()).call({  }, { { "name", name }, { "parent", self }, { "from_kall", boolean(false) } });
                    }) },
                { "__getattribute__", w.method("_Call", "__getattribute__", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        if ((B<"tuple">()).attr("__dict__").contains(attr))
                        {
                            py_raise(B<"AttributeError">());
                        }
                        return (B<"tuple">()).method("__getattribute__", { self, attr });
                    }) },
                { "_get_call_arguments", w.method("_Call", "_get_call_arguments", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        py kwargs;
                        py name;
                        if (py{ self.size() } == (I<2>()))
                        {
                            auto const items111 = unpack(self, 2);
                            args = items111[0];
                            kwargs = items111[1];
                        }
                        else
                        {
                            auto const items112 = unpack(self, 3);
                            name = items112[0];
                            args = items112[1];
                            kwargs = items112[2];
                        }
                        return py_tuple({ args, kwargs });
                    }) },
                { "args", B<"property">()(w.method("_Call", "args", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_get_call_arguments")[I<0>()];
                    })) },
                { "kwargs", B<"property">()(w.method("_Call", "kwargs", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_get_call_arguments")[I<1>()];
                    })) },
                { "__repr__", w.method("_Call", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        py kwargs;
                        py name;
                        if (!self.attr("_mock_from_kall").truthy())
                        {
                            name = [&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"call">(); }();
                            if (name.method("startswith", { S<"()">() }).truthy())
                            {
                                name = (S<"call%s">()) % name;
                            }
                            return name;
                        }
                        if (py{ self.size() } == (I<2>()))
                        {
                            name = S<"call">();
                            auto const items113 = unpack(self, 2);
                            args = items113[0];
                            kwargs = items113[1];
                        }
                        else
                        {
                            auto const items114 = unpack(self, 3);
                            name = items114[0];
                            args = items114[1];
                            kwargs = items114[2];
                            if (!name.truthy())
                            {
                                name = S<"call">();
                            }
                            else if (!name.method("startswith", { S<"()">() }).truthy())
                            {
                                name = (S<"call.%s">()) % name;
                            }
                            else
                            {
                                name = (S<"call%s">()) % name;
                            }
                        }
                        return (G<"unittest.mock._format_call_signature">())(name, args, kwargs);
                    }) },
                { "call_list", w.method("_Call", "call_list", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py thing;
                        py vals;
                        vals = py_list();
                        thing = self;
                        while (!thing.is_none())
                        {
                            if (thing.attr("_mock_from_kall").truthy())
                            {
                                vals.method("append", { thing });
                            }
                            thing = thing.attr("_mock_parent");
                        }
                        return (G<"unittest.mock._CallList">())((B<"reversed">())(vals));
                    }) }, }));

            define("unittest.mock", "call", (G<"unittest.mock._Call">()).call({  }, { { "from_kall", boolean(false) } }));
            define("unittest.mock", "create_autospec", w.function("create_autospec", "create_autospec", { py_positional("spec"), py_positional("spec_set", boolean(false)), py_positional("instance", boolean(false)), py_positional("_parent", none()), py_positional("_name", none()), py_keyword("unsafe", boolean(false)) }, [](arguments a) -> py
                {
                    py spec = a[0];
                    py spec_set = a[1];
                    py instance = a[2];
                    py _parent = a[3];
                    py _name = a[4];
                    py unsafe = a[5];
                    py kwargs = a[6];
                    py Klass;
                    py _kwargs;
                    py _new_name;
                    py child_klass;
                    py child_kwargs;
                    py entry;
                    py is_async_func;
                    py is_type;
                    py mock;
                    py new_;
                    py original;
                    py parent;
                    py skipfirst;
                    py wrapped;
                    if ((G<"unittest.mock._is_list">())(spec).truthy())
                    {
                        // can't pass a list instance to the mock constructor as it will be
                        // interpreted as a list of strings
                        spec = (B<"type">())(spec);
                    }
                    is_type = boolean(isinstance(spec, B<"type">()));
                    if ((G<"unittest.mock._is_instance_mock">())(spec).truthy())
                    {
                        py_raise((G<"unittest.mock.InvalidSpecError">())(S(std::string{ "Cannot autospec a Mock object. [object=" } + spec.repr() + "]")));
                    }
                    is_async_func = (G<"unittest.mock._is_async_func">())(spec);
                    _kwargs = dict_of({ { S<"spec">(), spec } });
                    if (spec_set.truthy())
                    {
                        _kwargs = dict_of({ { S<"spec_set">(), spec } });
                    }
                    else if (spec.is_none())
                    {
                        // None we mock with a normal mock without a spec
                        _kwargs = py_dict();
                    }
                    if ((_kwargs.truthy() && instance.truthy()))
                    {
                        _kwargs.set_item(S<"_spec_as_instance">(), boolean(true));
                    }
                    if (!unsafe.truthy())
                    {
                        (G<"unittest.mock._check_spec_arg_typos">())(kwargs);
                    }
                    _name = kwargs.method("pop", { S<"name">(), _name });
                    _new_name = _name;
                    if (_parent.is_none())
                    {
                        // for a top level object no _new_name should be set
                        _new_name = S<"">();
                    }
                    _kwargs.method("update", { kwargs });
                    Klass = G<"unittest.mock.MagicMock">();
                    if ((Mod<"inspect">().attr("isdatadescriptor"))(spec).truthy())
                    {
                        // descriptors don't have a spec
                        // because we don't know what type they return
                        _kwargs = py_dict();
                    }
                    else if (is_async_func.truthy())
                    {
                        if (instance.truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Instance can not be True when create_autospec is mocking an async function">()));
                        }
                        Klass = G<"unittest.mock.AsyncMock">();
                    }
                    else if (!(G<"unittest.mock._callable">())(spec).truthy())
                    {
                        Klass = G<"unittest.mock.NonCallableMagicMock">();
                    }
                    else if ((is_type.truthy() && instance.truthy() && (!(G<"unittest.mock._instance_callable">())(spec).truthy())))
                    {
                        Klass = G<"unittest.mock.NonCallableMagicMock">();
                    }
                    mock = call_ex(Klass, {  }, py{}, _kwargs, { { "parent", _parent }, { "_new_parent", _parent }, { "_new_name", _new_name }, { "name", _name } });
                    if (isinstance(spec, G<"unittest.mock.FunctionTypes">()))
                    {
                        // should only happen at the top level because we don't
                        // recurse for functions
                        if (is_async_func.truthy())
                        {
                            mock = (G<"unittest.mock._set_async_signature">())(mock, spec);
                        }
                        else
                        {
                            mock = (G<"unittest.mock._set_signature">())(mock, spec);
                        }
                    }
                    else
                    {
                        (G<"unittest.mock._check_signature">())(spec, mock, is_type, instance);
                    }
                    if (((!_parent.is_none()) && (!instance.truthy())))
                    {
                        _parent.attr("_mock_children").set_item(_name, mock);
                    }
                    // Pop wraps from kwargs because it must not be passed to configure_mock.
                    wrapped = kwargs.method("pop", { S<"wraps">(), none() });
                    if ((is_type.truthy() && (!instance.truthy()) && (!kwargs.contains((S<"return_value">())))))
                    {
                        mock.set_attr("return_value", (G<"unittest.mock.create_autospec">()).call({ spec, spec_set }, { { "instance", boolean(true) }, { "_name", S<"()">() }, { "_parent", mock }, { "wraps", wrapped } }));
                    }
                    for (py const& item : iterate{ (B<"dir">())(spec) })
                    {
                        entry = item;
                        if ((G<"unittest.mock._is_magic">())(entry).truthy())
                        {
                            // MagicMock already does the useful magic methods for us
                            continue;
                        }
                        // XXXX do we need a better way of getting attributes without
                        // triggering code execution (?) Probably not - we need the actual
                        // object to mock it so we would rather trigger a property than mock
                        // the property descriptor. Likewise we want to mock out dynamically
                        // provided attributes.
                        // XXXX what about attributes that raise exceptions other than
                        // AttributeError on being fetched?
                        // we could be resilient against it, or catch and propagate the
                        // exception when the attribute is fetched from the mock
                        try
                        {
                            original = (B<"getattr">())(spec, entry);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler117;
                            if (matches(handler117.exception, B<"AttributeError">()))
                            {
                                continue;
                            }
                            else
                                py_reraise(handler117.exception);
                        }
                        child_kwargs = dict_of({ { S<"spec">(), original } });
                        // Wrap child attributes also.
                        if ((wrapped.truthy() && (B<"hasattr">())(wrapped, entry).truthy()))
                        {
                            child_kwargs.method("update", {  }, { { "wraps", original } });
                        }
                        if (spec_set.truthy())
                        {
                            child_kwargs = dict_of({ { S<"spec_set">(), original } });
                        }
                        if (!isinstance(original, G<"unittest.mock.FunctionTypes">()))
                        {
                            new_ = (G<"unittest.mock._SpecState">())(original, spec_set, mock, entry, instance);
                            mock.attr("_mock_children").set_item(entry, new_);
                        }
                        else
                        {
                            parent = mock;
                            if (isinstance(spec, G<"unittest.mock.FunctionTypes">()))
                            {
                                parent = mock.attr("mock");
                            }
                            skipfirst = (G<"unittest.mock._must_skip">())(spec, entry, is_type);
                            child_kwargs.set_item(S<"_eat_self">(), skipfirst);
                            if ((Mod<"asyncio">().attr("iscoroutinefunction"))(original).truthy())
                            {
                                child_klass = G<"unittest.mock.AsyncMock">();
                            }
                            else
                            {
                                child_klass = G<"unittest.mock.MagicMock">();
                            }
                            new_ = call_ex(child_klass, {  }, py{}, child_kwargs, { { "parent", parent }, { "name", entry }, { "_new_name", entry }, { "_new_parent", parent } });
                            mock.attr("_mock_children").set_item(entry, new_);
                            new_.set_attr("return_value", child_klass());
                            (G<"unittest.mock._check_signature">()).call({ original, new_ }, { { "skipfirst", skipfirst } });
                        }
                        // so functions created with _set_signature become instance attributes,
                        // *plus* their underlying mock exists in _mock_children of the parent
                        // mock. Adding to _mock_children may be unnecessary where we are also
                        // setting as an instance attribute?
                        if (isinstance(new_, G<"unittest.mock.FunctionTypes">()))
                        {
                            (B<"setattr">())(mock, entry, new_);
                        }
                    }
                    // kwargs are passed with respect to the parent mock so, they are not used
                    // for creating return_value of the parent mock. So, this condition
                    // should be true only for the parent mock if kwargs are given.
                    if (((G<"unittest.mock._is_instance_mock">())(mock).truthy() && kwargs.truthy()))
                    {
                        call_ex(mock.attr("configure_mock"), {  }, py{}, kwargs);
                    }
                    return mock;
                }, false, true));

            define("unittest.mock", "_must_skip", w.function("_must_skip", "_must_skip", { py_positional("spec"), py_positional("entry"), py_positional("is_type") }, [](arguments a) -> py
                {
                    py spec = a[0];
                    py entry = a[1];
                    py is_type = a[2];
                    py klass;
                    py result;
                    if (!isinstance(spec, B<"type">()))
                    {
                        if ((B<"getattr">())(spec, S<"__dict__">(), py_dict()).contains(entry))
                        {
                            // instance attribute - shouldn't skip
                            return boolean(false);
                        }
                        spec = spec.attr("__class__");
                    }
                    for (py const& item : iterate{ spec.attr("__mro__") })
                    {
                        klass = item;
                        result = klass.attr("__dict__").method("get", { entry, G<"unittest.mock.DEFAULT">() });
                        if (result.is((G<"unittest.mock.DEFAULT">())))
                        {
                            continue;
                        }
                        if (isinstance(result, py_tuple({ B<"staticmethod">(), B<"classmethod">() })))
                        {
                            return boolean(false);
                        }
                        else if (isinstance(result, G<"unittest.mock.FunctionTypes">()))
                        {
                            // Normal method => skip if looked up on type
                            // (if looked up on instance, self is already skipped)
                            return is_type;
                        }
                        else
                        {
                            return boolean(false);
                        }
                    }
                    // function is a dynamically provided attribute
                    return is_type;
                }));

            define("unittest.mock", "_SpecState", w.make_class("_SpecState", { B<"object">() }, {
                { "__init__", w.method("_SpecState", "__init__", { py_positional("spec"), py_positional("spec_set", boolean(false)), py_positional("parent", none()), py_positional("name", none()), py_positional("ids", none()), py_positional("instance", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py spec = a[1];
                        py spec_set = a[2];
                        py parent = a[3];
                        py name = a[4];
                        py ids = a[5];
                        py instance = a[6];
                        self.set_attr("spec", spec);
                        self.set_attr("ids", ids);
                        self.set_attr("spec_set", spec_set);
                        self.set_attr("parent", parent);
                        self.set_attr("instance", instance);
                        self.set_attr("name", name);
                        return none();
                    }) }, }));

            define("unittest.mock", "FunctionTypes", py_tuple({ (B<"type">())(G<"unittest.mock.create_autospec">()), (B<"type">())((G<"unittest.mock.ANY">()).attr("__eq__")) }));
            // python function
            // instance method
            define("unittest.mock", "file_spec", none());
            define("unittest.mock", "open_spec", none());
            define("unittest.mock", "_to_stream", w.function("_to_stream", "_to_stream", { py_positional("read_data") }, [](arguments a) -> py
                {
                    py read_data = a[0];
                    if (isinstance(read_data, B<"bytes">()))
                    {
                        return (Mod<"io">().attr("BytesIO"))(read_data);
                    }
                    else
                    {
                        return (Mod<"io">().attr("StringIO"))(read_data);
                    }
                    return none();
                }));

            define("unittest.mock", "mock_open", w.function("mock_open", "mock_open", { py_positional("mock", none()), py_positional("read_data", S<"">()) }, [](arguments a) -> py
                {
                    py mock = a[0];
                    auto const read_data = std::make_shared<py>(a[1]);
                    py _exit_side_effect;
                    py _io;
                    auto const _iter_side_effect = std::make_shared<py>();
                    py _next_side_effect;
                    py _read_data;
                    py _read_side_effect;
                    auto const _readline_side_effect = std::make_shared<py>();
                    py _readlines_side_effect;
                    auto const _state = std::make_shared<py>();
                    auto const handle = std::make_shared<py>();
                    py reset_data;
                    _read_data = (G<"unittest.mock._to_stream">())((*read_data));
                    *_state = py_list({ _read_data, none() });
                    _readlines_side_effect = w.function("_readlines_side_effect", "mock_open.<locals>._readlines_side_effect", {  }, [_state, handle](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        if (!(*handle).attr("readlines").attr("return_value").is_none())
        {
            return (*handle).attr("readlines").attr("return_value");
        }
        return call_ex((*_state)[I<0>()].attr("readlines"), {  }, args, kwargs);
    }, true, true);
                    _read_side_effect = w.function("_read_side_effect", "mock_open.<locals>._read_side_effect", {  }, [_state, handle](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        if (!(*handle).attr("read").attr("return_value").is_none())
        {
            return (*handle).attr("read").attr("return_value");
        }
        return call_ex((*_state)[I<0>()].attr("read"), {  }, args, kwargs);
    }, true, true);
                    *_readline_side_effect = w.function("_readline_side_effect", "mock_open.<locals>._readline_side_effect", {  }, [_iter_side_effect, _state](arguments a) -> py
    {
        return make_native_generator("_readline_side_effect", "mock_open.<locals>._readline_side_effect", w.module, [a = std::vector<py>(a.begin(), a.end()), _iter_side_effect, _state]() -> py
        {
            py args = a[0];
            py kwargs = a[1];
            yield_from((*_iter_side_effect)());
            while (boolean(true).truthy())
            {
                py_yield(call_ex((*_state)[I<0>()].attr("readline"), {  }, args, kwargs));
            }
            return none();
        });
    }, true, true);
                    *_iter_side_effect = w.function("_iter_side_effect", "mock_open.<locals>._iter_side_effect", {  }, [_state, handle](arguments a) -> py
    {
        return make_native_generator("_iter_side_effect", "mock_open.<locals>._iter_side_effect", w.module, [a = std::vector<py>(a.begin(), a.end()), _state, handle]() -> py
        {
            py line;
            if (!(*handle).attr("readline").attr("return_value").is_none())
            {
                while (boolean(true).truthy())
                {
                    py_yield((*handle).attr("readline").attr("return_value"));
                }
            }
            for (py const& item : iterate{ (*_state)[I<0>()] })
            {
                line = item;
                py_yield(line);
            }
            return none();
        });
    });
                    _next_side_effect = w.function("_next_side_effect", "mock_open.<locals>._next_side_effect", {  }, [_state, handle](arguments a) -> py
    {
        if (!(*handle).attr("readline").attr("return_value").is_none())
        {
            return (*handle).attr("readline").attr("return_value");
        }
        return (B<"next">())((*_state)[I<0>()]);
    });
                    _exit_side_effect = w.function("_exit_side_effect", "mock_open.<locals>._exit_side_effect", {  }, [handle](arguments a) -> py
    {
        py args = a[0];
        (*handle).method("close");
        return none();
    }, true);
                    if ((G<"unittest.mock.file_spec">()).is_none())
                    {
                        _io = module_of("_io");
                        set_global("unittest.mock", "file_spec", (B<"list">())((B<"set">())((B<"dir">())(_io.attr("TextIOWrapper"))).method("union", { (B<"set">())((B<"dir">())(_io.attr("BytesIO"))) })));
                    }
                    if ((G<"unittest.mock.open_spec">()).is_none())
                    {
                        _io = module_of("_io");
                        set_global("unittest.mock", "open_spec", (B<"list">())((B<"set">())((B<"dir">())(_io.attr("open")))));
                    }
                    if (mock.is_none())
                    {
                        mock = (G<"unittest.mock.MagicMock">()).call({  }, { { "name", S<"open">() }, { "spec", G<"unittest.mock.open_spec">() } });
                    }
                    *handle = (G<"unittest.mock.MagicMock">()).call({  }, { { "spec", G<"unittest.mock.file_spec">() } });
                    (*handle).attr("__enter__").set_attr("return_value", (*handle));
                    (*handle).attr("write").set_attr("return_value", none());
                    (*handle).attr("read").set_attr("return_value", none());
                    (*handle).attr("readline").set_attr("return_value", none());
                    (*handle).attr("readlines").set_attr("return_value", none());
                    (*handle).attr("read").set_attr("side_effect", _read_side_effect);
                    (*_state).set_item(I<1>(), (*_readline_side_effect)());
                    (*handle).attr("readline").set_attr("side_effect", (*_state)[I<1>()]);
                    (*handle).attr("readlines").set_attr("side_effect", _readlines_side_effect);
                    (*handle).attr("__iter__").set_attr("side_effect", (*_iter_side_effect));
                    (*handle).attr("__next__").set_attr("side_effect", _next_side_effect);
                    (*handle).attr("__exit__").set_attr("side_effect", _exit_side_effect);
                    reset_data = w.function("reset_data", "mock_open.<locals>.reset_data", {  }, [_readline_side_effect, _state, handle, read_data](arguments a) -> py
    {
        py args = a[0];
        py kwargs = a[1];
        (*_state).set_item(I<0>(), (G<"unittest.mock._to_stream">())((*read_data)));
        if ((*handle).attr("readline").attr("side_effect") == (*_state)[I<1>()])
        {
            // Only reset the side effect if the user hasn't overridden it.
            (*_state).set_item(I<1>(), (*_readline_side_effect)());
            (*handle).attr("readline").set_attr("side_effect", (*_state)[I<1>()]);
        }
        return G<"unittest.mock.DEFAULT">();
    }, true, true);
                    mock.set_attr("side_effect", reset_data);
                    mock.set_attr("return_value", (*handle));
                    return mock;
                }));

            define("unittest.mock", "PropertyMock", w.make_class("PropertyMock", { G<"unittest.mock.Mock">() }, {
                { "_get_child_mock", w.method("PropertyMock", "_get_child_mock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kwargs = a[1];
                        return call_ex((G<"unittest.mock.MagicMock">()), {  }, py{}, kwargs);
                    }, false, true) },
                { "__get__", w.method("PropertyMock", "__get__", { py_positional("obj"), py_positional("obj_type", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py obj_type = a[2];
                        return self();
                    }) },
                { "__set__", w.method("PropertyMock", "__set__", { py_positional("obj"), py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py val = a[2];
                        self(val);
                        return none();
                    }) }, }));

            define("unittest.mock", "_timeout_unset", (G<"unittest.mock.sentinel">()).attr("TIMEOUT_UNSET"));
            define("unittest.mock", "ThreadingMixin", w.make_class("ThreadingMixin", { G<"unittest.mock.Base">() }, {
                { "DEFAULT_TIMEOUT", none() },
                { "_get_child_mock", w.method("ThreadingMixin", "_get_child_mock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kw = a[1];
                        if (isinstance(kw.method("get", { S<"parent">() }), G<"unittest.mock.ThreadingMixin">()))
                        {
                            kw.set_item(S<"timeout">(), kw[S<"parent">()].attr("_mock_wait_timeout"));
                        }
                        else if (isinstance(kw.method("get", { S<"_new_parent">() }), G<"unittest.mock.ThreadingMixin">()))
                        {
                            kw.set_item(S<"timeout">(), kw[S<"_new_parent">()].attr("_mock_wait_timeout"));
                        }
                        return call_ex((B<"super">()(G<"unittest.mock.ThreadingMixin">(), self)).attr("_get_child_mock"), {  }, py{}, kw);
                    }, false, true) },
                { "__init__", w.method("ThreadingMixin", "__init__", { py_keyword("timeout", G<"unittest.mock._timeout_unset">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        call_ex((B<"super">()(G<"unittest.mock.ThreadingMixin">(), self)).attr("__init__"), {  }, args, kwargs);
                        if (timeout.is((G<"unittest.mock._timeout_unset">())))
                        {
                            timeout = self.attr("DEFAULT_TIMEOUT");
                        }
                        self.attr("__dict__").set_item(S<"_mock_event">(), (Mod<"threading">().attr("Event"))());
                        self.attr("__dict__").set_item(S<"_mock_calls_events">(), py_list());
                        self.attr("__dict__").set_item(S<"_mock_calls_events_lock">(), (Mod<"threading">().attr("Lock"))());
                        self.attr("__dict__").set_item(S<"_mock_wait_timeout">(), timeout);
                        return none();
                    }, true, true) },
                { "reset_mock", w.method("ThreadingMixin", "reset_mock", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        call_ex((B<"super">()(G<"unittest.mock.ThreadingMixin">(), self)).attr("reset_mock"), {  }, args, kwargs);
                        self.attr("__dict__").set_item(S<"_mock_event">(), (Mod<"threading">().attr("Event"))());
                        self.attr("__dict__").set_item(S<"_mock_calls_events">(), py_list());
                        return none();
                    }, true, true) },
                { "_ThreadingMixin__get_event", w.method("ThreadingMixin", "__get_event", { py_positional("expected_args"), py_positional("expected_kwargs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py expected_args = a[1];
                        py expected_kwargs = a[2];
                        py args;
                        py event;
                        py kwargs;
                        py new_event;
                        {
                            py const manager122 = self.attr("_mock_calls_events_lock");
                            py const exit122 = pyclass::special_method(manager122, "__exit__");
                            py const value122 = pyclass::special_method(manager122, "__enter__")(manager122);
                            [[maybe_unused]] int flow122 = 0;
                            [[maybe_unused]] py result122;
                            bool exited122 = false;
                            try
                            {
                                for (py const& item : iterate{ self.attr("_mock_calls_events") })
                                {
                                    auto const items124 = unpack(item, 3);
                                    args = items124[0];
                                    kwargs = items124[1];
                                    event = items124[2];
                                    if (py_tuple({ args, kwargs }) == py_tuple({ expected_args, expected_kwargs }))
                                    {
                                        result122 = event;
                                        flow122 = 1;
                                        goto finally122;
                                    }
                                }
                                new_event = (Mod<"threading">().attr("Event"))();
                                self.attr("_mock_calls_events").method("append", { py_tuple({ expected_args, expected_kwargs, new_event }) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler122;
                                exited122 = true;
                                if (!exit122(manager122, type_of(handler122.exception), handler122.exception, handler122.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler122.exception);
                            }
                            finally122:
                            if (!exited122)
                                exit122(manager122, none(), none(), none());
                            if (flow122 == 1)
                                return result122;
                        }
                        return new_event;
                    }) },
                { "_mock_call", w.method("ThreadingMixin", "_mock_call", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py call_event;
                        py ret_value;
                        ret_value = call_ex((B<"super">()(G<"unittest.mock.ThreadingMixin">(), self)).attr("_mock_call"), {  }, args, kwargs);
                        call_event = self.method("_ThreadingMixin__get_event", { args, kwargs });
                        call_event.method("set");
                        self.attr("_mock_event").method("set");
                        return ret_value;
                    }, true, true) },
                { "_increment_mock_call", w.method("ThreadingMixin", "_increment_mock_call", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        {
                            py const manager125 = self.attr("_mock_calls_events_lock");
                            py const exit125 = pyclass::special_method(manager125, "__exit__");
                            py const value125 = pyclass::special_method(manager125, "__enter__")(manager125);
                            [[maybe_unused]] int flow125 = 0;
                            [[maybe_unused]] py result125;
                            bool exited125 = false;
                            try
                            {
                                call_ex((B<"super">()(G<"unittest.mock.ThreadingMixin">(), self)).attr("_increment_mock_call"), {  }, args, kwargs);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler125;
                                exited125 = true;
                                if (!exit125(manager125, type_of(handler125.exception), handler125.exception, handler125.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler125.exception);
                            }
                            finally125:
                            if (!exited125)
                                exit125(manager125, none(), none(), none());
                            if (flow125 == 1)
                                return result125;
                        }
                        return none();
                    }, true, true) },
                { "wait_until_called", w.method("ThreadingMixin", "wait_until_called", { py_keyword("timeout", G<"unittest.mock._timeout_unset">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        py msg;
                        if (timeout.is((G<"unittest.mock._timeout_unset">())))
                        {
                            timeout = self.attr("_mock_wait_timeout");
                        }
                        if (!self.attr("_mock_event").method("wait", {  }, { { "timeout", timeout } }).truthy())
                        {
                            msg = S(([&]() -> py { py const t = self.attr("_mock_name"); return t.truthy() ? t : S<"mock">(); }()).str() + " was not called before timeout(" + timeout.str() + ").");
                            py_raise((B<"AssertionError">())(msg));
                        }
                        return none();
                    }) },
                { "wait_until_any_call_with", w.method("ThreadingMixin", "wait_until_any_call_with", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py event;
                        py expected_string;
                        event = self.method("_ThreadingMixin__get_event", { args, kwargs });
                        if (!event.method("wait", {  }, { { "timeout", self.attr("_mock_wait_timeout") } }).truthy())
                        {
                            expected_string = self.method("_format_mock_call_signature", { args, kwargs });
                            py_raise((B<"AssertionError">())(S(expected_string.str() + " call not found")));
                        }
                        return none();
                    }, true, true) }, }));

            define("unittest.mock", "ThreadingMock", w.make_class("ThreadingMock", { G<"unittest.mock.ThreadingMixin">(), G<"unittest.mock.MagicMixin">(), G<"unittest.mock.Mock">() }, {
 }));

            define("unittest.mock", "seal", w.function("seal", "seal", { py_positional("mock") }, [](arguments a) -> py
                {
                    py mock = a[0];
                    py attr;
                    py m;
                    mock.set_attr("_mock_sealed", boolean(true));
                    for (py const& item : iterate{ (B<"dir">())(mock) })
                    {
                        attr = item;
                        try
                        {
                            m = (B<"getattr">())(mock, attr);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler127;
                            if (matches(handler127.exception, B<"AttributeError">()))
                            {
                                continue;
                            }
                            else
                                py_reraise(handler127.exception);
                        }
                        if (!isinstance(m, G<"unittest.mock.NonCallableMock">()))
                        {
                            continue;
                        }
                        if (isinstance(m.attr("_mock_children").method("get", { attr }), G<"unittest.mock._SpecState">()))
                        {
                            continue;
                        }
                        if (m.attr("_mock_new_parent").is(mock))
                        {
                            (G<"unittest.mock.seal">())(m);
                        }
                    }
                    return none();
                }));

            define("unittest.mock", "_AsyncIterator", w.make_class("_AsyncIterator", {  }, {
                { "__init__", w.method("_AsyncIterator", "__init__", { py_positional("iterator") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py iterator = a[1];
                        py code_mock;
                        self.set_attr("iterator", iterator);
                        code_mock = (G<"unittest.mock.NonCallableMock">()).call({  }, { { "spec_set", Mod<"types">().attr("CodeType") } });
                        code_mock.set_attr("co_flags", Mod<"inspect">().attr("CO_ITERABLE_COROUTINE"));
                        self.attr("__dict__").set_item(S<"__code__">(), code_mock);
                        return none();
                    }) },
                { "__anext__", async_method(w, "_AsyncIterator", "__anext__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            return (B<"next">())(self.attr("iterator"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler128;
                            if (matches(handler128.exception, B<"StopIteration">()))
                            {
                            }
                            else
                                py_reraise(handler128.exception);
                        }
                        py_raise(B<"StopAsyncIteration">());
                    }) }, }));
        }

        bool unittest_mock_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_unittest_mock();
                }
                if (auto const doc = unittest_mock_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("asyncio", py{ fn_import("asyncio") }.release());
                aAttributes.emplace_back("contextlib", py{ fn_import("contextlib") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("inspect", py{ fn_import("inspect") }.release());
                aAttributes.emplace_back("pprint", py{ fn_import("pprint") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("builtins", py{ fn_import("builtins") }.release());
                aAttributes.emplace_back("pkgutil", py{ fn_import("pkgutil") }.release());
                aAttributes.emplace_back("iscoroutinefunction", import_attribute("asyncio", "iscoroutinefunction").release());
                aAttributes.emplace_back("threading", py{ fn_import("threading") }.release());
                aAttributes.emplace_back("CodeType", import_attribute("types", "CodeType").release());
                aAttributes.emplace_back("ModuleType", import_attribute("types", "ModuleType").release());
                aAttributes.emplace_back("MethodType", import_attribute("types", "MethodType").release());
                aAttributes.emplace_back("safe_repr", import_attribute("unittest.util", "safe_repr").release());
                aAttributes.emplace_back("wraps", import_attribute("functools", "wraps").release());
                aAttributes.emplace_back("partial", import_attribute("functools", "partial").release());
                aAttributes.emplace_back("RLock", import_attribute("threading", "RLock").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("unittest.mock", unittest_mock_module);
    }
}
