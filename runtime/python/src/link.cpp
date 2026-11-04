/*
  link.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
*/

// Linking (see neos_rt_link_type in neos/runtime/plugin.h): a program's Python linked with code of other languages. The names of
// its module __main__ (its namespace: the compiler gives a linked program's module a getter of its names, as it does a module that
// uses globals()) are the other languages' to use (neos_runtime_plugin_link); a name that it uses but neither binds nor is a builtin
// (where it would be a NameError) is one of theirs, if one of them has it (neos_rt_host::link: python::global_lookup). Values cross
// as linked values: None is no value; bools, ints, floats and strs are themselves (an int that does not fit in 64 bits is a float);
// other objects are linked objects: a Python object's own (python.object), or, given to Python, another language's, which Python
// sees as an object of type "linked" (callable if the value is) that is given back as the linked object it is.

#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // (a Python object's linked object)
            struct linked_object
            {
                neos_rt_object base;
                std::atomic<long> references{ 1 };
                object* target;
            };

            void retain_linked(neos_rt_object* aObject)
            {
                reinterpret_cast<linked_object*>(aObject)->references.fetch_add(1, std::memory_order_relaxed);
            }

            void release_linked(neos_rt_object* aObject)
            {
                auto* const linked = reinterpret_cast<linked_object*>(aObject);
                if (linked->references.fetch_sub(1, std::memory_order_acq_rel) != 1)
                    return;
                release(linked->target);
                delete linked;
            }

            int callable_linked(neos_rt_object* aObject);
            char const* call_linked(neos_rt_object* aObject, unsigned aCount, neos_rt_type const* aTypes, neos_rt_value const* aArguments,
                neos_rt_type* aResultType, neos_rt_value* aResult);

            neos_rt_link_type const sLinkType = { { "python.object", retain_linked, release_linked }, "python", callable_linked, call_linked };

            // (another language's linked object, as Python sees it: an object of type "linked")
            struct proxy_object
            {
                header h;
                neos_rt_object* target;     ///< (a reference)
                std::string name;           ///< (the global's name that it was found by, if any)
                ~proxy_object() { target->type->release(target); }
            };

            neos_rt_link_type const& link_type_of(neos_rt_object* aObject)
            {
                return *reinterpret_cast<neos_rt_link_type const*>(aObject->type);
            }

            object* proxy_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs);

            object* proxy_repr(neos_py_context*, object* aSelf)
            {
                auto const& proxy = *as<proxy_object>(aSelf);
                auto const& type = link_type_of(proxy.target);
                std::string text = std::string{ "<" } + (type.language != nullptr ? type.language : "linked") +
                    (type.callable(proxy.target) != 0 ? " function" : " object");
                if (!proxy.name.empty())
                    text += " " + proxy.name;
                return make_str(text + ">");
            }

            neos_py_type make_proxy_type()
            {
                auto type = make_type("python.linked", "linked", retain_object, release_object<proxy_object>);
                type.repr = proxy_repr;
                type.call = proxy_call;
                return type;
            }

            neos_py_type sProxyType = make_proxy_type();

            // (a linked value as a Python object (a new reference; an object: aOwned, its reference is taken over))
            object* from_linked(neos_rt_type aType, neos_rt_value const& aValue, bool aOwned, std::string_view aName = {})
            {
                switch (aType)
                {
                case NEOS_RT_BOOL:
                    return make_bool(aValue.b != 0);
                case NEOS_RT_I32:
                    return make_int(aValue.i32);
                case NEOS_RT_U32:
                    return make_int(static_cast<long long>(aValue.u32));
                case NEOS_RT_I64:
                    return make_int(aValue.i64);
                case NEOS_RT_U64:
                    return make_int(static_cast<long long>(aValue.u64));
                case NEOS_RT_F32:
                    return make_float(aValue.f32);
                case NEOS_RT_F64:
                    return make_float(aValue.f64);
                case NEOS_RT_BIG_INTEGER:
                    {
                        // (its hexadecimal digits: see neos_rt_link_type)
                        std::string_view digits{ aValue.string != nullptr ? aValue.string : "" };
                        bool const negative = !digits.empty() && digits[0] == '-';
                        if (negative)
                            digits.remove_prefix(1u);
                        if (digits.empty())
                            break;
                        return digits_int(digits, 16u, negative);
                    }
                case NEOS_RT_STRING:
                    return make_str(aValue.string != nullptr ? aValue.string : "");
                case NEOS_RT_OBJECT:
                    if (aValue.object == nullptr)
                        break;
                    if (aValue.object->type == &sLinkType.base)
                    {
                        // (a Python object, given back)
                        auto* const target = reinterpret_cast<linked_object*>(aValue.object)->target;
                        retain(target);
                        if (aOwned)
                            release_linked(aValue.object);
                        return target;
                    }
                    if (!aOwned)
                        aValue.object->type->retain(aValue.object);
                    return &(new proxy_object{ { { &sProxyType.base } }, aValue.object, std::string{ aName } })->h.base;
                default:
                    break;
                }
                retain(none());
                return none();
            }

            // (a Python object as a linked value (aStrings: the storage of strings; an object: a new reference if aOwned is null, else
            // borrowed (a new linked object of a Python object is added to aOwned, for its reference to be released when it is no longer
            // needed))
            void to_linked(object* aObject, neos_rt_type& aType, neos_rt_value& aResult, std::deque<std::string>& aStrings, std::vector<neos_rt_object*>* aOwned)
            {
                aResult = neos_rt_value{};
                switch (kind_of(aObject))
                {
                case NEOS_PY_NONE:
                    aType = NEOS_RT_VOID;
                    return;
                case NEOS_PY_BOOL:
                    aType = NEOS_RT_BOOL;
                    aResult.b = aObject == &sTrue.base ? 1 : 0;
                    return;
                case NEOS_PY_INT:
                    {
                        long long i = 0;
                        if (to_index(aObject, i))
                        {
                            aType = NEOS_RT_I64;
                            aResult.i64 = i;
                            return;
                        }
                        clear_error();
                        double d = 0.0;
                        if (to_double(aObject, d))
                        {
                            aType = NEOS_RT_F64;
                            aResult.f64 = d;
                            return;
                        }
                        clear_error();
                    }
                    break;
                case NEOS_PY_FLOAT:
                    aType = NEOS_RT_F64;
                    aResult.f64 = as<float_object>(aObject)->value;
                    return;
                case NEOS_PY_STR:
                    aStrings.push_back(as<str_object>(aObject)->value);
                    aType = NEOS_RT_STRING;
                    aResult.string = aStrings.back().c_str();
                    return;
                default:
                    break;
                }
                aType = NEOS_RT_OBJECT;
                if (aObject->type == &sProxyType.base)
                {
                    // (another language's value, given back)
                    aResult.object = as<proxy_object>(aObject)->target;
                    if (aOwned == nullptr)
                        aResult.object->type->retain(aResult.object);
                    return;
                }
                retain(aObject);
                aResult.object = &(new linked_object{ { &sLinkType.base }, 1, aObject })->base;
                if (aOwned != nullptr)
                    aOwned->push_back(aResult.object);
            }

            int callable_linked(neos_rt_object* aObject)
            {
                return is_callable(reinterpret_cast<linked_object*>(aObject)->target) ? 1 : 0;
            }

            char const* call_linked(neos_rt_object* aObject, unsigned aCount, neos_rt_type const* aTypes, neos_rt_value const* aArguments,
                neos_rt_type* aResultType, neos_rt_value* aResult)
            {
                thread_local std::string tError;
                thread_local std::deque<std::string> tStrings;
                // (called on the program that the host runs (the function's: see neos_rt_link_type))
                struct entered { void* previous = enter_program(); ~entered() { leave_program(previous); } } const entering;
                *aResultType = NEOS_RT_VOID;
                *aResult = neos_rt_value{};
                std::vector<object*> arguments;
                for (unsigned i = 0u; i < aCount; ++i)
                    arguments.push_back(from_linked(aTypes[i], aArguments[i], false));
                ref const result{ invoke(reinterpret_cast<linked_object*>(aObject)->target, std::move(arguments)) };
                if (!result)
                {
                    tError = failed() ? std::string{ error_text() } : std::string{ "SystemError: call failed" };
                    clear_error();
                    return tError.c_str();
                }
                tStrings.clear();
                to_linked(result.get(), *aResultType, *aResult, tStrings, nullptr);
                return nullptr;
            }

            // (a call of another language's callable linked object)
            object* proxy_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
            {
                if (aKwargs != nullptr && kind_of(aKwargs) == NEOS_PY_DICT && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", "a linked function takes no keyword arguments");
                auto* const target = as<proxy_object>(aSelf)->target;
                auto const& type = link_type_of(target);
                if (type.callable(target) == 0)
                    return error("TypeError", "'linked' object is not callable");
                auto const& items = as<tuple_object>(aArgs)->items;
                std::deque<std::string> strings;
                std::vector<neos_rt_object*> owned;
                std::vector<neos_rt_type> types(items.size());
                std::vector<neos_rt_value> values(items.size());
                struct release_owned { std::vector<neos_rt_object*>& owned; ~release_owned() { for (auto* o : owned) o->type->release(o); } } const releasing{ owned };
                for (std::size_t i = 0u; i < items.size(); ++i)
                    to_linked(items[i], types[i], values[i], strings, &owned);
                neos_rt_type resultType = NEOS_RT_VOID;
                neos_rt_value result{};
                if (auto const message = type.call(target, static_cast<unsigned>(types.size()), types.data(), values.data(), &resultType, &result))
                {
                    // (its error: as the Python exception of its type, if Python has one of that name (e.g. TypeError), else a RuntimeError)
                    std::string const text = message;
                    auto const colon = text.find(": ");
                    if (colon != std::string::npos && is_exception_class(text.substr(0u, colon)))
                        return error(text.substr(0u, colon), text.substr(colon + 2u));
                    return error("RuntimeError", text);
                }
                return from_linked(resultType, result, true);
            }
        }

        // (a name that the program's Python neither binds nor is a builtin: another language's global, if one has it (a new reference;
        // null if none has (the exception pending is kept)))
        object* linked_global(char const* aName)
        {
            auto const* const h = host();
            if (h == nullptr || h->version < 16u || h->link == nullptr)
                return nullptr;
            // (objects are found once (the name's proxy); other values each time (they can change))
            // (by the program's module (a linked program's translation units of the language each have their own) and name)
            static std::map<std::string, object*, std::less<>> sImported;
            auto const importKey = main_module_key() + "/" + aName;
            if (auto const existing = sImported.find(importKey); existing != sImported.end())
                return new_reference(existing->second);
            neos_rt_type type = NEOS_RT_VOID;
            neos_rt_value value{};
            if (h->link(aName, "python", &type, &value) == 0)
                return nullptr;
            bool const proxy = type == NEOS_RT_OBJECT && value.object != nullptr && value.object->type != &sLinkType.base;
            auto* const result = from_linked(type, value, true, aName);
            if (proxy)
                sImported.emplace(importKey, new_reference(result));
            return result;
        }

        // (neos_runtime_plugin_link: a name of the program's module __main__ (its namespace's))
        int link_global(char const* aName, neos_rt_type* aType, neos_rt_value* aValue)
        {
            // (the running program's (the host's: a linked program's translation units of the language are each a program of their own))
            struct entered { void* previous = enter_program(); ~entered() { leave_program(previous); } } const entering;
            // (the module's names, once its getter is registered (its code has begun to run: see python::register_namespace))
            if (aName == nullptr || std::string_view{ aName }.starts_with("__") || !namespace_registered("__main__"))
                return 0;
            ref const ns{ fn_module_namespace("__main__", nullptr) };
            if (!ns || kind_of(builtin_value(ns.get())) != NEOS_PY_DICT)
            {
                clear_error();
                return 0;
            }
            ref const key{ make_str(aName) };
            auto const position = dict_find(builtin_value(ns.get()), key.get());
            if (position < 0)
            {
                clear_error();
                return 0;
            }
            auto* const item = as<dict_object>(builtin_value(ns.get()))->items[static_cast<std::size_t>(position)].second;
            thread_local std::deque<std::string> tStrings;
            tStrings.clear();
            to_linked(item, *aType, *aValue, tStrings, nullptr);
            return 1;
        }
    }
}

NEOS_RUNTIME_PLUGIN_LINK
{
    return neos::python_runtime::detail::link_global(aName, aType, aValue);
}
