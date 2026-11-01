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

/*
  Linking (see neos_rt_link_type in neos/runtime/plugin.h): a program's JavaScript linked with code of other languages. Its globals
  (the global object's own data properties and the global lexical declarations) are the other languages' to use by their names
  (neos_runtime_plugin_link); a name that it uses but that is not a global (where it would be a ReferenceError) is one of theirs, if
  one of them has it (neos_rt_host::link). Values cross as linked values: undefined and null are no value; booleans, numbers and
  strings are themselves (a number that is an integer is an integer); other values are linked objects: a JavaScript value's own
  (javascript.value), or, given to JavaScript, another language's, which JavaScript sees as a proxy (a function, if it can be called,
  else an object) that is given back as the linked object it is.
*/

#include "js.hpp"
#include "api.hpp"
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace neos::javascript_runtime
{
    value call_linked_proxy(value aThis, arguments aArguments, value aNewTarget, function_object& aCallee);

    namespace
    {
        // (a JavaScript value's linked object: its value is its slot's (a root) in the linked values of its isolate)
        struct linked_value
        {
            neos_rt_object base;
            std::atomic<long> references{ 1 };
            heap* isolate;
            std::size_t slot;
        };

        // (an isolate's state of linking: its values that linked objects refer to (roots); the proxies of other languages' linked objects 
        // (by number: each object retained once, while its proxy lives; not roots (but those of the names that it has found): a proxy that 
        // the collector finds dead is forgotten, and its object released when linking next runs (see sweep_links)))
        struct link_state
        {
            value_list values;
            std::vector<std::size_t> freeSlots;
            std::vector<value> proxies;                 ///< (Undefined: a free number)
            std::vector<neos_rt_object*> foreign;
            std::vector<std::size_t> freeNumbers;
            std::vector<neos_rt_object*> dead;          ///< (the objects of proxies that have died: to be released)
            std::unordered_map<neos_rt_object*, std::size_t> foreignNumbers;
            std::unordered_map<value, std::size_t> proxyNumbers;
            std::map<std::string, std::size_t, std::less<>> imported;   ///< (names -> proxies' numbers)
            ~link_state()
            {
                for (auto* object : foreign)
                    if (object != nullptr)
                        object->type->release(object);
                for (auto* object : dead)
                    object->type->release(object);
            }
        };

        isolate_local<link_state> const sLinks;
        thread_local heap* tLinkIsolate = nullptr;     ///< (the calling thread's isolate, once it has used linking)

        link_state& links()
        {
            auto& s = *sLinks;
            tLinkIsolate = &the_heap();
            // (the objects of the proxies that have died)
            while (!s.dead.empty())
            {
                auto* const object = s.dead.back();
                s.dead.pop_back();
                object->type->release(object);
            }
            return s;
        }

        // (the collector: the proxies of the names found are roots; the others are forgotten when they die)
        void mark_links(tracer& aTracer)
        {
            if (tLinkIsolate == nullptr || current_isolate() != tLinkIsolate)
                return;
            auto& s = *sLinks;
            for (auto const& name : s.imported)
                aTracer.mark(s.proxies[name.second]);
        }

        void sweep_links(tracer& aTracer)
        {
            if (tLinkIsolate == nullptr || current_isolate() != tLinkIsolate)
                return;
            auto& s = *sLinks;
            for (std::size_t number = 0u; number < s.proxies.size(); ++number)
                if (s.proxies[number] != Undefined && !aTracer.alive(s.proxies[number]))
                {
                    s.proxyNumbers.erase(s.proxies[number]);
                    s.foreignNumbers.erase(s.foreign[number]);
                    s.dead.push_back(s.foreign[number]);
                    s.foreign[number] = nullptr;
                    s.proxies[number] = Undefined;
                    s.freeNumbers.push_back(number);
                }
        }

        root_tracer const sLinkTracer{ mark_links, sweep_links };

        void retain_linked(neos_rt_object* aObject);
        void release_linked(neos_rt_object* aObject);
        int callable_linked(neos_rt_object* aObject);
        char const* call_linked(neos_rt_object* aObject, unsigned aCount, neos_rt_type const* aTypes, neos_rt_value const* aArguments,
            neos_rt_type* aResultType, neos_rt_value* aResult);

        neos_rt_link_type const sLinkType = { { "javascript.value", retain_linked, release_linked }, "javascript", callable_linked, call_linked };

        value& value_of_linked(neos_rt_object* aObject)
        {
            return sLinks->values[reinterpret_cast<linked_value*>(aObject)->slot];
        }

        // (a new linked object of a JavaScript value)
        neos_rt_object* make_linked(value aValue)
        {
            auto& s = links();
            std::size_t slot;
            if (!s.freeSlots.empty())
            {
                slot = s.freeSlots.back();
                s.freeSlots.pop_back();
                s.values[slot] = aValue;
            }
            else
            {
                slot = s.values.size();
                s.values.push_back(aValue);
            }
            return &(new linked_value{ { &sLinkType.base }, 1, &the_heap(), slot })->base;
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
            // (its slot is free, unless its isolate has ended (e.g. released by another language's runtime when the program ends))
            if (current_isolate() == linked->isolate)
            {
                auto& s = *sLinks;
                s.values[linked->slot] = Undefined;
                s.freeSlots.push_back(linked->slot);
            }
            delete linked;
        }

        // (the proxy of another language's linked object (aOwned: the reference is the proxy's to take))
        value proxy_of(neos_rt_object* aObject, bool aOwned, std::string_view aName = {})
        {
            auto& s = links();
            if (auto const existing = s.foreignNumbers.find(aObject); existing != s.foreignNumbers.end())
            {
                if (aOwned)
                    aObject->type->release(aObject);
                return s.proxies[existing->second];
            }
            if (!aOwned)
                aObject->type->retain(aObject);
            std::size_t number;
            if (!s.freeNumbers.empty())
            {
                number = s.freeNumbers.back();
                s.freeNumbers.pop_back();
            }
            else
            {
                number = s.foreign.size();
                s.foreign.push_back(nullptr);
                s.proxies.push_back(Undefined);
            }
            s.foreign[number] = aObject;
            s.foreignNumbers.emplace(aObject, number);
            auto const& type = *reinterpret_cast<neos_rt_link_type const*>(aObject->type);
            value proxy;
            if (type.callable(aObject) != 0)
            {
                proxy = make_native(make_string_utf8(aName.empty() ? std::string_view{ "linked" } : aName), 0u, call_linked_proxy);
                as_function(proxy)->nativeId = static_cast<std::uint32_t>(number);
            }
            else
                proxy = make_object();
            s.proxies[number] = proxy;
            s.proxyNumbers.emplace(proxy, number);
            return proxy;
        }

        // (a linked value as a JavaScript value (an object: aOwned, the reference is taken over))
        value from_linked(neos_rt_type aType, neos_rt_value const& aValue, bool aOwned, std::string_view aName = {})
        {
            switch (aType)
            {
            case NEOS_RT_BOOL:
                return boolean(aValue.b != 0);
            case NEOS_RT_I32:
                return number(static_cast<double>(aValue.i32));
            case NEOS_RT_U32:
                return number(static_cast<double>(aValue.u32));
            case NEOS_RT_I64:
                return number(static_cast<double>(aValue.i64));
            case NEOS_RT_U64:
                return number(static_cast<double>(aValue.u64));
            case NEOS_RT_F32:
                return number(static_cast<double>(aValue.f32));
            case NEOS_RT_F64:
                return number(aValue.f64);
            case NEOS_RT_STRING:
                return make_string_utf8(aValue.string != nullptr ? aValue.string : "");
            case NEOS_RT_OBJECT:
                if (aValue.object == nullptr)
                    return Null;
                if (aValue.object->type == &sLinkType.base)
                {
                    // (a JavaScript value, given back)
                    auto const result = value_of_linked(aValue.object);
                    if (aOwned)
                        release_linked(aValue.object);
                    return result;
                }
                return proxy_of(aValue.object, aOwned, aName);
            default:
                return Undefined;
            }
        }

        // (a JavaScript value as a linked value (aStrings: the storage of strings; an object: a new reference if aNew, else borrowed (a
        // new linked object of a JavaScript value is added to aOwned, for its reference to be released when it is no longer needed)))
        void to_linked(value aValue, neos_rt_type& aType, neos_rt_value& aResult, std::deque<std::string>& aStrings, std::vector<neos_rt_object*>* aOwned)
        {
            aResult = neos_rt_value{};
            if (is_nullish(aValue))
                aType = NEOS_RT_VOID;
            else if (is_boolean(aValue))
            {
                aType = NEOS_RT_BOOL;
                aResult.b = aValue == True ? 1 : 0;
            }
            else if (is_number(aValue))
            {
                auto const n = as_number(aValue);
                if (std::isfinite(n) && std::trunc(n) == n && std::fabs(n) < 9.2e18 && !(n == 0.0 && std::signbit(n)))
                {
                    aType = NEOS_RT_I64;
                    aResult.i64 = static_cast<long long>(n);
                }
                else
                {
                    aType = NEOS_RT_F64;
                    aResult.f64 = n;
                }
            }
            else if (is_string(aValue))
            {
                aStrings.push_back(utf8(aValue));
                aType = NEOS_RT_STRING;
                aResult.string = aStrings.back().c_str();
            }
            else if (is_bigint(aValue))
            {
                aType = NEOS_RT_I64;
                aResult.i64 = static_cast<long long>(bigint_to_int64(aValue));
            }
            else
            {
                aType = NEOS_RT_OBJECT;
                auto& s = *sLinks;
                if (auto const proxy = s.proxyNumbers.find(aValue); proxy != s.proxyNumbers.end())
                {
                    // (another language's value, given back)
                    aResult.object = s.foreign[proxy->second];
                    if (aOwned == nullptr)
                        aResult.object->type->retain(aResult.object);
                }
                else
                {
                    aResult.object = make_linked(aValue);
                    if (aOwned != nullptr)
                        aOwned->push_back(aResult.object);
                }
            }
        }

        // (the error message of a JavaScript exception: "type: message", as its string)
        std::string error_text(value aThrown)
        {
            try
            {
                return utf8(to_string(aThrown));
            }
            catch (...)
            {
                return "Error: uncaught exception";
            }
        }

        int callable_linked(neos_rt_object* aObject)
        {
            return is_callable(value_of_linked(aObject)) ? 1 : 0;
        }

        char const* call_linked(neos_rt_object* aObject, unsigned aCount, neos_rt_type const* aTypes, neos_rt_value const* aArguments,
            neos_rt_type* aResultType, neos_rt_value* aResult)
        {
            thread_local std::string tError;
            thread_local std::deque<std::string> tStrings;
            try
            {
                value_list arguments;
                for (unsigned i = 0u; i < aCount; ++i)
                    arguments.push_back(from_linked(aTypes[i], aArguments[i], false));
                auto const callee = value_of_linked(aObject);
                if (!is_callable(callee))
                    throw_type_error("linked value is not a function");
                auto const result = call(callee, Undefined, arguments);
                tStrings.clear();
                to_linked(result, *aResultType, *aResult, tStrings, nullptr);
                return nullptr;
            }
            catch (js_exception const& e)
            {
                tError = error_text(e.thrown);
            }
            catch (std::exception const& e)
            {
                tError = std::string{ "Error: " } + e.what();
            }
            *aResultType = NEOS_RT_VOID;
            *aResult = neos_rt_value{};
            return tError.c_str();
        }
    }

    // (a call of a proxy of another language's callable linked object)
    value call_linked_proxy(value, arguments aArguments, value, function_object& aCallee)
    {
        auto& s = *sLinks;
        auto* const target = s.foreign.at(aCallee.nativeId);
        std::deque<std::string> strings;
        std::vector<neos_rt_object*> owned;
        std::vector<neos_rt_type> types(aArguments.size());
        std::vector<neos_rt_value> values(aArguments.size());
        struct release { std::vector<neos_rt_object*>& owned; ~release() { for (auto* o : owned) o->type->release(o); } } const releasing{ owned };
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
            to_linked(aArguments[i], types[i], values[i], strings, &owned);
        neos_rt_type resultType = NEOS_RT_VOID;
        neos_rt_value result{};
        auto const& type = *reinterpret_cast<neos_rt_link_type const*>(target->type);
        if (auto const error = type.call(target, static_cast<unsigned>(types.size()), types.data(), values.data(), &resultType, &result))
            throw_error(the_realm().errorPrototype, error);
        return from_linked(resultType, result, true);
    }

    // (a name that the program's JavaScript uses but that is not a global: another language's global (its proxy), if one has it)
    std::optional<value> linked_global(value aName)
    {
        auto const* const h = host();
        if (h == nullptr || h->version < 16u || h->link == nullptr)
            return {};
        auto& s = links();
        auto const name = utf8(aName);
        if (auto const existing = s.imported.find(name); existing != s.imported.end())
            return s.proxies[existing->second];
        neos_rt_type type = NEOS_RT_VOID;
        neos_rt_value result{};
        if (h->link(name.c_str(), "javascript", &type, &result) == 0)
            return {};
        auto const v = from_linked(type, result, true, name);
        // (objects are found once: the name's proxy; other values each time (they can change))
        if (type == NEOS_RT_OBJECT && result.object != nullptr && result.object->type != &sLinkType.base)
            s.imported.emplace(name, s.foreignNumbers.at(result.object));
        return v;
    }

    // (neos_runtime_plugin_link: a global of the program's JavaScript)
    int link_global(char const* aName, neos_rt_type* aType, neos_rt_value* aValue)
    {
        if (aName == nullptr || current_isolate() == nullptr || !realm_initialized())
            return 0;
        try
        {
            auto const name = intern(make_string_utf8(aName));
            std::optional<value> found;
            auto& lexicals = global_lexicals();
            if (auto const b = lexicals.find(name); b != lexicals.end() && b->second.get() != Empty)
                found = b->second.get();
            else if (auto& g = obj(the_realm().global); g.cls == object_class::Ordinary)
            {
                if (auto const p = g.own(name); p != nullptr && !p->accessor())
                    found = p->data();
            }
            if (!found)
                return 0;
            thread_local std::deque<std::string> tStrings;
            tStrings.clear();
            to_linked(*found, *aType, *aValue, tStrings, nullptr);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }
}
