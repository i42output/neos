/*
  proxy.cpp

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
*/

// Proxy exotic objects: their internal methods (the handler's traps and the invariants that the traps' results are
// checked against), the Proxy constructor and Proxy.revocable

#include <algorithm>
#include <unordered_set>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        class proxy_object : public object
        {
        public:
            proxy_object(value aTarget, value aHandler) :
                object{ Null, object_class::Proxy }, target{ aTarget }, handler{ aHandler }, isCallable{ is_callable(aTarget) }, isConstructor{ is_constructor(aTarget) }
            {
            }
        public:
            value get_prototype_of() override;
            bool set_prototype_of(value aPrototype) override;
            bool is_extensible() override;
            bool prevent_extensions() override;
            std::optional<property_descriptor> get_own_property(value aKey) override;
            bool define_own_property(value aKey, property_descriptor const& aDescriptor) override;
            bool has_property(value aKey) override;
            value get(value aKey, value aReceiver) override;
            bool set(value aKey, value aValue, value aReceiver) override;
            bool delete_property(value aKey) override;
            std::vector<value> own_property_keys() override;
            bool callable() const override { return isCallable; }
            bool constructor() const override { return isConstructor; }
            value call(value aThis, arguments aArguments) override;
            value construct(arguments aArguments, value aNewTarget) override;
        private:
            // (ValidateNonRevokedProxy, the target and handler (as they are now: a trap can revoke the proxy), then GetMethod(handler,
            // name): undefined if there is no trap)
            struct trap_record
            {
                value trap;
                value target;
                value handler;
            };
            trap_record trap(char const* aName)
            {
                if (handler == Null)
                    throw_type_error(std::string{ "Cannot perform '" } + aName + "' on a proxy that has been revoked");
                trap_record result{ Undefined, target, handler };
                result.trap = get_method(handler, ascii(aName));
                return result;
            }
            template <typename... Args>
            static value call_trap(value aHandler, value aTrap, Args... aArguments)
            {
                value const args[] = { aArguments... };
                return javascript_runtime::call(aTrap, aHandler, args);
            }
        public:
            value target;       ///< (null: revoked)
            value handler;      ///< (null: revoked)
            bool const isCallable;
            bool const isConstructor;
        };

        proxy_object& as_proxy(value aProxy)
        {
            return static_cast<proxy_object&>(obj(aProxy));
        }

        std::string key_name(value aKey)
        {
            return is_string(aKey) ? utf8(aKey) : describe(aKey);
        }

        bool is_configurable(std::optional<property_descriptor> const& aDescriptor)
        {
            return aDescriptor->configurable.value_or(false);
        }

        value proxy_object::get_prototype_of()
        {
            auto const [t, target, handler] = trap("getPrototypeOf");
            if (t == Undefined)
                return obj(target).get_prototype_of();
            auto const handlerProto = call_trap(handler, t, target);
            if (!is_object(handlerProto) && handlerProto != Null)
                throw_type_error("'getPrototypeOf' on proxy: trap returned neither object nor null");
            if (obj(target).is_extensible())
                return handlerProto;
            auto const targetProto = obj(target).get_prototype_of();
            if (!same_value(handlerProto, targetProto))
                throw_type_error("'getPrototypeOf' on proxy: proxy target is non-extensible but the trap did not return its actual prototype");
            return handlerProto;
        }

        bool proxy_object::set_prototype_of(value aPrototype)
        {
            auto const [t, target, handler] = trap("setPrototypeOf");
            if (t == Undefined)
                return obj(target).set_prototype_of(aPrototype);
            if (!to_boolean(call_trap(handler, t, target, aPrototype)))
                return false;
            if (obj(target).is_extensible())
                return true;
            auto const targetProto = obj(target).get_prototype_of();
            if (!same_value(aPrototype, targetProto))
                throw_type_error("'setPrototypeOf' on proxy: trap returned truish for setting a new prototype on the non-extensible proxy target");
            return true;
        }

        bool proxy_object::is_extensible()
        {
            auto const [t, target, handler] = trap("isExtensible");
            if (t == Undefined)
                return obj(target).is_extensible();
            auto const result = to_boolean(call_trap(handler, t, target));
            if (result != obj(target).is_extensible())
                throw_type_error("'isExtensible' on proxy: trap result does not reflect extensibility of proxy target");
            return result;
        }

        bool proxy_object::prevent_extensions()
        {
            auto const [t, target, handler] = trap("preventExtensions");
            if (t == Undefined)
                return obj(target).prevent_extensions();
            auto const result = to_boolean(call_trap(handler, t, target));
            if (result && obj(target).is_extensible())
                throw_type_error("'preventExtensions' on proxy: trap returned truish but the proxy target is extensible");
            return result;
        }

        std::optional<property_descriptor> proxy_object::get_own_property(value aKey)
        {
            auto const [t, target, handler] = trap("getOwnPropertyDescriptor");
            if (t == Undefined)
                return obj(target).get_own_property(aKey);
            auto const trapResult = call_trap(handler, t, target, aKey);
            if (!is_object(trapResult) && trapResult != Undefined)
                throw_type_error("'getOwnPropertyDescriptor' on proxy: trap returned neither object nor undefined for property '" + key_name(aKey) + "'");
            auto const targetDesc = obj(target).get_own_property(aKey);
            if (trapResult == Undefined)
            {
                if (!targetDesc)
                    return std::nullopt;
                if (!is_configurable(targetDesc))
                    throw_type_error("'getOwnPropertyDescriptor' on proxy: trap returned undefined for property '" + key_name(aKey) +
                        "' which is non-configurable in the proxy target");
                if (!obj(target).is_extensible())
                    throw_type_error("'getOwnPropertyDescriptor' on proxy: trap returned undefined for property '" + key_name(aKey) +
                        "' which exists in the non-extensible proxy target");
                return std::nullopt;
            }
            auto const extensibleTarget = obj(target).is_extensible();
            auto resultDesc = to_property_descriptor(trapResult);
            complete_property_descriptor(resultDesc);
            if (!is_compatible_property_descriptor(extensibleTarget, resultDesc, targetDesc))
                throw_type_error("'getOwnPropertyDescriptor' on proxy: trap returned descriptor for property '" + key_name(aKey) +
                    "' that is incompatible with the existing property in the proxy target");
            if (!resultDesc.configurable.value_or(false))
            {
                if (!targetDesc || is_configurable(targetDesc))
                    throw_type_error("'getOwnPropertyDescriptor' on proxy: trap reported non-configurability for property '" + key_name(aKey) +
                        "' which is either non-existent or configurable in the proxy target");
                if (resultDesc.writable && !*resultDesc.writable && targetDesc->writable.value_or(false))
                    throw_type_error("'getOwnPropertyDescriptor' on proxy: trap reported non-configurable and writable for property '" + key_name(aKey) +
                        "' which is non-configurable, non-writable in the proxy target");
            }
            return resultDesc;
        }

        bool proxy_object::define_own_property(value aKey, property_descriptor const& aDescriptor)
        {
            auto const [t, target, handler] = trap("defineProperty");
            if (t == Undefined)
                return obj(target).define_own_property(aKey, aDescriptor);
            auto const descObj = from_property_descriptor(aDescriptor);
            if (!to_boolean(call_trap(handler, t, target, aKey, descObj)))
                return false;
            auto const targetDesc = obj(target).get_own_property(aKey);
            auto const extensibleTarget = obj(target).is_extensible();
            bool const settingConfigFalse = aDescriptor.configurable && !*aDescriptor.configurable;
            if (!targetDesc)
            {
                if (!extensibleTarget)
                    throw_type_error("'defineProperty' on proxy: trap returned truish for adding property '" + key_name(aKey) +
                        "'  to the non-extensible proxy target");
                if (settingConfigFalse)
                    throw_type_error("'defineProperty' on proxy: trap returned truish for defining non-configurable property '" + key_name(aKey) +
                        "' which is either non-existent or configurable in the proxy target");
            }
            else
            {
                if (!is_compatible_property_descriptor(extensibleTarget, aDescriptor, targetDesc))
                    throw_type_error("'defineProperty' on proxy: trap returned truish for adding property '" + key_name(aKey) +
                        "'  that is incompatible with the existing property in the proxy target");
                if (settingConfigFalse && is_configurable(targetDesc))
                    throw_type_error("'defineProperty' on proxy: trap returned truish for defining non-configurable property '" + key_name(aKey) +
                        "' which is either non-existent or configurable in the proxy target");
                if (targetDesc->is_data() && !is_configurable(targetDesc) && targetDesc->writable.value_or(false) &&
                    aDescriptor.writable && !*aDescriptor.writable)
                    throw_type_error("'defineProperty' on proxy: trap returned truish for defining non-configurable property '" + key_name(aKey) +
                        "' which cannot be non-writable, unless there exists a corresponding non-configurable, non-writable own property of the target object.");
            }
            return true;
        }

        bool proxy_object::has_property(value aKey)
        {
            auto const [t, target, handler] = trap("has");
            if (t == Undefined)
                return obj(target).has_property(aKey);
            auto const result = to_boolean(call_trap(handler, t, target, aKey));
            if (!result)
            {
                auto const targetDesc = obj(target).get_own_property(aKey);
                if (targetDesc)
                {
                    if (!is_configurable(targetDesc))
                        throw_type_error("'has' on proxy: trap returned falsish for property '" + key_name(aKey) + "' which exists in the proxy target as non-configurable");
                    if (!obj(target).is_extensible())
                        throw_type_error("'has' on proxy: trap returned falsish for property '" + key_name(aKey) + "' but the proxy target is not extensible");
                }
            }
            return result;
        }

        value proxy_object::get(value aKey, value aReceiver)
        {
            auto const [t, target, handler] = trap("get");
            if (t == Undefined)
                return obj(target).get(aKey, aReceiver);
            auto const trapResult = call_trap(handler, t, target, aKey, aReceiver);
            auto const targetDesc = obj(target).get_own_property(aKey);
            if (targetDesc && !is_configurable(targetDesc))
            {
                if (targetDesc->is_data() && !targetDesc->writable.value_or(false) && !same_value(trapResult, targetDesc->valueField.value_or(Undefined)))
                    throw_type_error("'get' on proxy: property '" + key_name(aKey) +
                        "' is a read-only and non-configurable data property on the proxy target but the proxy did not return its actual value");
                if (targetDesc->is_accessor() && targetDesc->getter.value_or(Undefined) == Undefined && trapResult != Undefined)
                    throw_type_error("'get' on proxy: property '" + key_name(aKey) +
                        "' is a non-configurable accessor property on the proxy target and does not have a getter function, but the trap did not return 'undefined'");
            }
            return trapResult;
        }

        bool proxy_object::set(value aKey, value aValue, value aReceiver)
        {
            auto const [t, target, handler] = trap("set");
            if (t == Undefined)
                return obj(target).set(aKey, aValue, aReceiver);
            if (!to_boolean(call_trap(handler, t, target, aKey, aValue, aReceiver)))
                return false;
            auto const targetDesc = obj(target).get_own_property(aKey);
            if (targetDesc && !is_configurable(targetDesc))
            {
                if (targetDesc->is_data() && !targetDesc->writable.value_or(false) && !same_value(aValue, targetDesc->valueField.value_or(Undefined)))
                    throw_type_error("'set' on proxy: trap returned truish for property '" + key_name(aKey) +
                        "' which exists in the proxy target as a non-configurable and non-writable data property with a different value");
                if (targetDesc->is_accessor() && targetDesc->setter.value_or(Undefined) == Undefined)
                    throw_type_error("'set' on proxy: trap returned truish for property '" + key_name(aKey) +
                        "' which exists in the proxy target as a non-configurable and non-writable accessor property without a setter");
            }
            return true;
        }

        bool proxy_object::delete_property(value aKey)
        {
            auto const [t, target, handler] = trap("deleteProperty");
            if (t == Undefined)
                return obj(target).delete_property(aKey);
            if (!to_boolean(call_trap(handler, t, target, aKey)))
                return false;
            auto const targetDesc = obj(target).get_own_property(aKey);
            if (!targetDesc)
                return true;
            if (!is_configurable(targetDesc))
                throw_type_error("'deleteProperty' on proxy: trap returned truish for property '" + key_name(aKey) + "' which is non-configurable in the proxy target");
            if (!obj(target).is_extensible())
                throw_type_error("'deleteProperty' on proxy: trap returned truish for property '" + key_name(aKey) + "' but the proxy target is non-extensible");
            return true;
        }

        std::vector<value> proxy_object::own_property_keys()
        {
            auto const [t, target, handler] = trap("ownKeys");
            if (t == Undefined)
                return obj(target).own_property_keys();
            auto const trapResultArray = call_trap(handler, t, target);
            auto trapResult = create_list_from_array_like(trapResultArray, true);
            for (auto& key : trapResult)
                key = to_property_key(key);     // (strings interned: the keys of property tables)
            std::unordered_set<value> distinct;
            for (auto const key : trapResult)
                if (!distinct.insert(key).second)
                    throw_type_error("'ownKeys' on proxy: trap returned duplicate entries");
            auto const extensibleTarget = obj(target).is_extensible();
            auto const targetKeys = obj(target).own_property_keys();
            std::vector<value> targetConfigurableKeys;
            std::vector<value> targetNonconfigurableKeys;
            for (auto const key : targetKeys)
            {
                auto const desc = obj(target).get_own_property(key);
                if (desc && !is_configurable(desc))
                    targetNonconfigurableKeys.push_back(key);
                else
                    targetConfigurableKeys.push_back(key);
            }
            if (extensibleTarget && targetNonconfigurableKeys.empty())
                return trapResult;
            auto unchecked = trapResult;
            auto const check = [&](value aKey)
            {
                auto const existing = std::find(unchecked.begin(), unchecked.end(), aKey);
                if (existing == unchecked.end())
                    return false;
                unchecked.erase(existing);
                return true;
            };
            for (auto const key : targetNonconfigurableKeys)
                if (!check(key))
                    throw_type_error("'ownKeys' on proxy: trap result did not include '" + key_name(key) + "'");
            if (extensibleTarget)
                return trapResult;
            for (auto const key : targetConfigurableKeys)
                if (!check(key))
                    throw_type_error("'ownKeys' on proxy: trap result did not include '" + key_name(key) + "'");
            if (!unchecked.empty())
                throw_type_error("'ownKeys' on proxy: trap returned extra keys but proxy target is non-extensible");
            return trapResult;
        }

        value proxy_object::call(value aThis, arguments aArguments)
        {
            auto const [t, target, handler] = trap("apply");
            if (t == Undefined)
                return javascript_runtime::call(target, aThis, aArguments);
            auto const argArray = create_array_from_list(aArguments);
            return call_trap(handler, t, target, aThis, argArray);
        }

        value proxy_object::construct(arguments aArguments, value aNewTarget)
        {
            auto const [t, target, handler] = trap("construct");
            if (t == Undefined)
                return javascript_runtime::construct(target, aArguments, aNewTarget);
            auto const argArray = create_array_from_list(aArguments);
            auto const newObj = call_trap(handler, t, target, argArray, aNewTarget);
            if (!is_object(newObj))
                throw_type_error("'construct' on proxy: trap returned non-object ('" + describe(newObj) + "')");
            return newObj;
        }

        value proxy_create(value aTarget, value aHandler)
        {
            if (!is_object(aTarget) || !is_object(aHandler))
                throw_type_error("Cannot create proxy with a non-object as target or handler");
            return value_of(the_heap().make<proxy_object>(aTarget, aHandler));
        }

        value proxy_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor Proxy requires 'new'");
            return proxy_create(argument(aArguments, 0u), argument(aArguments, 1u));
        }

        value proxy_revoker(value, arguments, value, function_object& aCallee)
        {
            auto const p = aCallee.slots[0];
            if (p == Null)
                return Undefined;
            aCallee.slots[0] = Null;
            auto& proxy = as_proxy(p);
            proxy.target = Null;
            proxy.handler = Null;
            return Undefined;
        }

        value proxy_revocable(value, arguments aArguments, value, function_object&)
        {
            auto const p = proxy_create(argument(aArguments, 0u), argument(aArguments, 1u));
            auto const revoker = make_native(the_names().empty, 0u, proxy_revoker);
            as_function(revoker)->slots[0] = p;
            auto const result = make_object();
            create_data_property_or_throw(result, ascii("proxy"), p);
            create_data_property_or_throw(result, ascii("revoke"), revoker);
            return result;
        }
    }

    bool proxy_is_array(value aProxy)
    {
        auto const target = as_proxy(aProxy).target;
        if (target == Null)
            throw_type_error("Cannot perform 'IsArray' on a proxy that has been revoked");
        return is_array(target);
    }

    void proxy_function_realm(value aProxy)
    {
        // (GetFunctionRealm: a revoked proxy throws; else its target's realm (there is one realm))
        for (auto p = aProxy; is_object(p);)
        {
            auto& o = obj(p);
            if (o.cls == object_class::Proxy)
            {
                if (as_proxy(p).target == Null)
                    throw_type_error("Cannot get the realm of a proxy that has been revoked");
                p = as_proxy(p).target;
            }
            else if (auto* f = as_function(p); f != nullptr && f->kind == function_kind::Bound)
                p = f->boundTarget;
            else
                break;
        }
    }

    value proxy_target(value aProxy)
    {
        return as_proxy(aProxy).target;
    }

    void init_proxy(realm& aRealm)
    {
        auto const c = make_native("Proxy", 2u, proxy_constructor, true);
        aRealm.proxy = c;
        method(c, "revocable", 2u, proxy_revocable);
        obj(aRealm.global).put(ascii("Proxy"), c);
    }
}
