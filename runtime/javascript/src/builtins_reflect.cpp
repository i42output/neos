/*
  builtins_reflect.cpp

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

// Reflect: the object's internal methods as functions

#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        value target_object(arguments aArguments, char const* aMethod)
        {
            auto const target = argument(aArguments, 0u);
            if (!is_object(target))
                throw_type_error(std::string{ "Reflect." } + aMethod + " called on non-object");
            return target;
        }

        value reflect_apply(value, arguments aArguments, value, function_object&)
        {
            auto const target = argument(aArguments, 0u);
            if (!is_callable(target))
                throw_type_error(describe(target) + " is not a function");
            auto const args = create_list_from_array_like(argument(aArguments, 2u));
            return call(target, argument(aArguments, 1u), args);
        }

        value reflect_construct(value, arguments aArguments, value, function_object&)
        {
            auto const target = argument(aArguments, 0u);
            if (!is_constructor(target))
                throw_type_error(describe(target) + " is not a constructor");
            auto newTarget = target;
            if (aArguments.size() > 2u)
            {
                newTarget = aArguments[2];
                if (!is_constructor(newTarget))
                    throw_type_error(describe(newTarget) + " is not a constructor");
            }
            auto const args = create_list_from_array_like(argument(aArguments, 1u));
            return construct(target, args, newTarget);
        }

        value reflect_define_property(value, arguments aArguments, value, function_object&)
        {
            auto const target = target_object(aArguments, "defineProperty");
            auto const key = to_property_key(argument(aArguments, 1u));
            auto const desc = to_property_descriptor(argument(aArguments, 2u));
            return boolean(obj(target).define_own_property(key, desc));
        }

        value reflect_delete_property(value, arguments aArguments, value, function_object&)
        {
            auto const target = target_object(aArguments, "deleteProperty");
            auto const key = to_property_key(argument(aArguments, 1u));
            return boolean(obj(target).delete_property(key));
        }

        value reflect_get(value, arguments aArguments, value, function_object&)
        {
            auto const target = target_object(aArguments, "get");
            auto const key = to_property_key(argument(aArguments, 1u));
            auto const receiver = aArguments.size() > 2u ? aArguments[2] : target;
            return obj(target).get(key, receiver);
        }

        value reflect_get_own_property_descriptor(value, arguments aArguments, value, function_object&)
        {
            auto const target = target_object(aArguments, "getOwnPropertyDescriptor");
            auto const key = to_property_key(argument(aArguments, 1u));
            return from_property_descriptor(obj(target).get_own_property(key));
        }

        value reflect_get_prototype_of(value, arguments aArguments, value, function_object&)
        {
            return obj(target_object(aArguments, "getPrototypeOf")).get_prototype_of();
        }

        value reflect_has(value, arguments aArguments, value, function_object&)
        {
            auto const target = target_object(aArguments, "has");
            auto const key = to_property_key(argument(aArguments, 1u));
            return boolean(obj(target).has_property(key));
        }

        value reflect_is_extensible(value, arguments aArguments, value, function_object&)
        {
            return boolean(obj(target_object(aArguments, "isExtensible")).is_extensible());
        }

        value reflect_own_keys(value, arguments aArguments, value, function_object&)
        {
            return create_array_from_list(obj(target_object(aArguments, "ownKeys")).own_property_keys());
        }

        value reflect_prevent_extensions(value, arguments aArguments, value, function_object&)
        {
            return boolean(obj(target_object(aArguments, "preventExtensions")).prevent_extensions());
        }

        value reflect_set(value, arguments aArguments, value, function_object&)
        {
            auto const target = target_object(aArguments, "set");
            auto const key = to_property_key(argument(aArguments, 1u));
            auto const v = argument(aArguments, 2u);
            auto const receiver = aArguments.size() > 3u ? aArguments[3] : target;
            return boolean(obj(target).set(key, v, receiver));
        }

        value reflect_set_prototype_of(value, arguments aArguments, value, function_object&)
        {
            auto const target = target_object(aArguments, "setPrototypeOf");
            auto const proto = argument(aArguments, 1u);
            if (!is_object(proto) && proto != Null)
                throw_type_error("Object prototype may only be an Object or null: " + describe(proto));
            return boolean(obj(target).set_prototype_of(proto));
        }
    }

    void init_reflect(realm& aRealm)
    {
        auto const r = make_object();
        method(r, "apply", 3u, reflect_apply);
        method(r, "construct", 2u, reflect_construct);
        method(r, "defineProperty", 3u, reflect_define_property);
        method(r, "deleteProperty", 2u, reflect_delete_property);
        method(r, "get", 2u, reflect_get);
        method(r, "getOwnPropertyDescriptor", 2u, reflect_get_own_property_descriptor);
        method(r, "getPrototypeOf", 1u, reflect_get_prototype_of);
        method(r, "has", 2u, reflect_has);
        method(r, "isExtensible", 1u, reflect_is_extensible);
        method(r, "ownKeys", 1u, reflect_own_keys);
        method(r, "preventExtensions", 1u, reflect_prevent_extensions);
        method(r, "set", 3u, reflect_set);
        method(r, "setPrototypeOf", 2u, reflect_set_prototype_of);
        obj(r).put(aRealm.symToStringTag, ascii("Reflect"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("Reflect"), r);
    }
}
