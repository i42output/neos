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

// Linking (see neos_rt_link_type in neos/runtime/plugin.h): a program's Lua linked with code of other languages. Its globals (those of
// the globals table) are the other languages' to use by their names (neos_runtime_plugin_link); a global that it reads that is nil is
// one of theirs, if one of them has it (lua::link, which the code of a linked program calls when it starts, gives the globals table a
// metatable whose __index finds them with neos_rt_host::link). Values cross as linked values: nil is no value; booleans, integers,
// floats and strings are themselves; other values are linked objects: a Lua value's own (lua.value), or, given to Lua, another
// language's, which Lua sees as a function (if it can be called; a native function) or a userdata (its __name "linked"), that is given
// back as the linked object it is.

#include <atomic>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        // (a Lua value's linked object: its value is its slot's (a root) in the linked values)
        struct linked_value
        {
            neos_rt_object base;
            std::atomic<long> references{ 1 };
            std::size_t slot;
        };

        static_values sLinked;              ///< (the values of Lua's linked objects, by slot)
        std::vector<std::size_t> sFreeSlots;
        static_values sImported;            ///< (the proxies of the other languages' globals that Lua has found, by their names' numbers)
        std::map<std::string, std::size_t, std::less<>> sImportedNames;
        static_values sProxyMetatable;      ///< (0: the metatable of proxies that are userdata)

        void retain_linked(neos_rt_object* aObject);
        void release_linked(neos_rt_object* aObject);
        int callable_linked(neos_rt_object* aObject);
        char const* call_linked(neos_rt_object* aObject, unsigned aCount, neos_rt_type const* aTypes, neos_rt_value const* aArguments,
            neos_rt_type* aResultType, neos_rt_value* aResult);

        neos_rt_link_type const sLinkType = { { "lua.value", retain_linked, release_linked }, "lua", callable_linked, call_linked };

        value& value_of_linked(neos_rt_object* aObject)
        {
            return sLinked[reinterpret_cast<linked_value*>(aObject)->slot];
        }

        neos_rt_object* make_linked(value aValue)
        {
            std::size_t slot;
            if (!sFreeSlots.empty())
            {
                slot = sFreeSlots.back();
                sFreeSlots.pop_back();
                sLinked[slot] = aValue;
            }
            else
            {
                slot = sLinked.size();
                sLinked.push_back(aValue);
            }
            return &(new linked_value{ { &sLinkType.base }, 1, slot })->base;
        }

        void retain_linked(neos_rt_object* aObject)
        {
            reinterpret_cast<linked_value*>(aObject)->references.fetch_add(1, std::memory_order_relaxed);
        }

        void release_linked(neos_rt_object* aObject)
        {
            auto* const linked = reinterpret_cast<linked_value*>(aObject);
            if (linked->references.fetch_sub(1, std::memory_order_acq_rel) != 1)
                return;
            if (linked->slot < sLinked.size())
            {
                sLinked[linked->slot] = Nil;
                sFreeSlots.push_back(linked->slot);
            }
            delete linked;
        }

        neos_rt_link_type const& link_type_of(neos_rt_object* aObject)
        {
            return *reinterpret_cast<neos_rt_link_type const*>(aObject->type);
        }

        // (a proxy's userdata: it holds a reference to another language's linked object)
        void destroy_proxy(void* aObject)
        {
            auto* const object = static_cast<neos_rt_object*>(aObject);
            object->type->release(object);
        }

        void proxy_call(native_call& aCall);

        // (the linked object that a Lua value is a proxy of (null: none))
        neos_rt_object* proxied(value aValue)
        {
            if (is_function(aValue) && fun(aValue).native && fun(aValue).function == &proxy_call && !fun(aValue).upvalues.empty())
                aValue = fun(aValue).upvalues[0];
            if (is_type(aValue, cell_type::Userdata) && cell_of<userdata_cell>(aValue).destroy == &destroy_proxy)
                return static_cast<neos_rt_object*>(cell_of<userdata_cell>(aValue).data);
            return nullptr;
        }

        // (a linked value as a Lua value (an object: aOwned, its reference is taken over))
        value from_linked(neos_rt_type aType, neos_rt_value const& aValue, bool aOwned)
        {
            switch (aType)
            {
            case NEOS_RT_BOOL:
                return boolean(aValue.b != 0);
            case NEOS_RT_I32:
                return make_integer(aValue.i32);
            case NEOS_RT_U32:
                return make_integer(static_cast<std::int64_t>(aValue.u32));
            case NEOS_RT_I64:
                return make_integer(aValue.i64);
            case NEOS_RT_U64:
                return make_integer(static_cast<std::int64_t>(aValue.u64));
            case NEOS_RT_F32:
                return make_float(static_cast<double>(aValue.f32));
            case NEOS_RT_F64:
                return make_float(aValue.f64);
            case NEOS_RT_STRING:
                return make_string(std::string_view{ aValue.string != nullptr ? aValue.string : "" });
            case NEOS_RT_OBJECT:
                {
                    auto* const object = aValue.object;
                    if (object == nullptr)
                        return Nil;
                    if (object->type == &sLinkType.base)
                    {
                        // (a Lua value, given back)
                        auto const result = value_of_linked(object);
                        if (aOwned)
                            release_linked(object);
                        return result;
                    }
                    if (!aOwned)
                        object->type->retain(object);
                    auto& u = the_heap().make<userdata_cell>();
                    u.data = object;
                    u.destroy = &destroy_proxy;
                    if (sProxyMetatable.empty())
                    {
                        value_list keep{ value_of(u) };
                        auto const metatable = make_table();
                        sProxyMetatable.push_back(metatable);
                        raw_set(metatable, name(NameName), make_string(std::string_view{ "linked" }));
                    }
                    u.metatable = sProxyMetatable[0];
                    auto const userdata = value_of(u);
                    if (link_type_of(object).callable(object) == 0)
                        return userdata;
                    return make_native(&proxy_call, "linked", { userdata });
                }
            default:
                return Nil;
            }
        }

        // (a Lua value as a linked value (aStrings: the storage of strings; an object: a new reference if aOwned is null, else borrowed (a
        // new linked object of a Lua value is added to aOwned, for its reference to be released when it is no longer needed)))
        void to_linked(value aValue, neos_rt_type& aType, neos_rt_value& aResult, std::deque<std::string>& aStrings, std::vector<neos_rt_object*>* aOwned)
        {
            aResult = neos_rt_value{};
            if (is_nil(aValue))
                aType = NEOS_RT_VOID;
            else if (is_boolean(aValue))
            {
                aType = NEOS_RT_BOOL;
                aResult.b = aValue == True ? 1 : 0;
            }
            else if (is_integer(aValue))
            {
                aType = NEOS_RT_I64;
                aResult.i64 = integer_of(aValue);
            }
            else if (is_float(aValue))
            {
                aType = NEOS_RT_F64;
                aResult.f64 = float_of(aValue);
            }
            else if (is_string(aValue))
            {
                aStrings.push_back(str(aValue));
                aType = NEOS_RT_STRING;
                aResult.string = aStrings.back().c_str();
            }
            else
            {
                aType = NEOS_RT_OBJECT;
                if (auto* const object = proxied(aValue))
                {
                    // (another language's value, given back)
                    aResult.object = object;
                    if (aOwned == nullptr)
                        object->type->retain(object);
                }
                else
                {
                    aResult.object = make_linked(aValue);
                    if (aOwned != nullptr)
                        aOwned->push_back(aResult.object);
                }
            }
        }

        // (an error's message: "LuaError: message")
        std::string error_text(value aError)
        {
            try
            {
                return "LuaError: " + (is_string(aError) || is_number(aError) ? tostring_text(aError) : "(error object is a " + type_name(aError) + " value)");
            }
            catch (...)
            {
                return "LuaError: (error object)";
            }
        }

        int callable_linked(neos_rt_object* aObject)
        {
            auto const v = value_of_linked(aObject);
            return is_function(v) || !is_nil(metamethod(v, NameCall)) ? 1 : 0;
        }

        char const* call_linked(neos_rt_object* aObject, unsigned aCount, neos_rt_type const* aTypes, neos_rt_value const* aArguments,
            neos_rt_type* aResultType, neos_rt_value* aResult)
        {
            thread_local std::string tError;
            thread_local std::deque<std::string> tStrings;
            *aResultType = NEOS_RT_VOID;
            *aResult = neos_rt_value{};
            try
            {
                value_list arguments;
                for (unsigned i = 0u; i < aCount; ++i)
                    arguments.push_back(from_linked(aTypes[i], aArguments[i], false));
                value_list results;
                call(value_of_linked(aObject), arguments.span(), results);
                tStrings.clear();
                to_linked(results.empty() ? Nil : results[0], *aResultType, *aResult, tStrings, nullptr);
                return nullptr;
            }
            catch (lua_error const& e)
            {
                tError = error_text(e.v);
            }
            catch (std::exception const& e)
            {
                tError = std::string{ "LuaError: " } + e.what();
            }
            return tError.c_str();
        }

        // (a call of a proxy of another language's callable linked object)
        void proxy_call(native_call& aCall)
        {
            auto* const target = proxied(aCall.self.upvalues[0]);
            std::deque<std::string> strings;
            std::vector<neos_rt_object*> owned;
            std::vector<neos_rt_type> types(aCall.count());
            std::vector<neos_rt_value> values(aCall.count());
            neos_rt_type resultType = NEOS_RT_VOID;
            neos_rt_value result{};
            char const* error = nullptr;
            {
                struct release_owned { std::vector<neos_rt_object*>& owned; ~release_owned() { for (auto* o : owned) o->type->release(o); } } const releasing{ owned };
                for (std::size_t i = 0u; i < aCall.count(); ++i)
                    to_linked(aCall.args[i], types[i], values[i], strings, &owned);
                error = link_type_of(target).call(target, static_cast<unsigned>(types.size()), types.data(), values.data(), &resultType, &result);
            }
            if (error != nullptr)
                runtime_error(error);
            aCall.ret(from_linked(resultType, result, true));
        }

        // (the globals table's __index (a linked program's): another language's global, if one has it)
        void index_global(native_call& aCall)
        {
            auto const key = aCall.arg(1u);
            auto const* const h = host();
            if (!is_string(key) || h == nullptr || h->version < 16u || h->link == nullptr)
            {
                aCall.ret(Nil);
                return;
            }
            auto const& text = str(key);
            if (auto const existing = sImportedNames.find(text); existing != sImportedNames.end())
            {
                aCall.ret(sImported[existing->second]);
                return;
            }
            neos_rt_type type = NEOS_RT_VOID;
            neos_rt_value result{};
            if (h->link(text.c_str(), "lua", &type, &result) == 0)
            {
                aCall.ret(Nil);
                return;
            }
            bool const proxy = type == NEOS_RT_OBJECT && result.object != nullptr && result.object->type != &sLinkType.base;
            value_list keep{ from_linked(type, result, true) };
            // (objects are found once (the name's proxy); other values each time (they can change))
            if (proxy)
            {
                sImportedNames.emplace(text, sImported.size());
                sImported.push_back(keep[0]);
            }
            aCall.ret(keep[0]);
        }
    }

    // (lua::link: the program is linked with code of other languages (the globals table finds theirs))
    value link_globals()
    {
        try
        {
            auto& globals = tab(the_state().globals);
            if (is_nil(globals.metatable))
            {
                value_list keep{ make_table() };
                raw_set(keep[0], name(NameIndex), make_native(&index_global, "linked"));
                globals.metatable = keep[0];
            }
        }
        catch (lua_error const& e)
        {
            the_state().pending = e.v;
            return Exception;
        }
        return Nil;
    }

    // (neos_runtime_plugin_link: a global of the program's Lua)
    int link_global(char const* aName, neos_rt_type* aType, neos_rt_value* aValue)
    {
        if (aName == nullptr || gState == nullptr || !gState->initialized)
            return 0;
        try
        {
            auto const v = raw_get(the_state().globals, intern(std::string_view{ aName }));
            if (is_nil(v))
                return 0;
            thread_local std::deque<std::string> tStrings;
            tStrings.clear();
            to_linked(v, *aType, *aValue, tStrings, nullptr);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }
}
