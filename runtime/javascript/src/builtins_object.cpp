/*
  builtins_object.cpp

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

// Object: the Object constructor and Object.prototype

#include <algorithm>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        value object_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const v = argument(aArguments, 0u);
            if (aNewTarget != Undefined && aNewTarget != value_of(aCallee))
                return ordinary_create_from_constructor(aNewTarget, &realm::objectPrototype);
            if (is_nullish(v))
                return make_object();
            return to_object(v);
        }

        value object_assign(value, arguments aArguments, value, function_object&)
        {
            auto const to = to_object(argument(aArguments, 0u));
            for (std::size_t i = 1u; i < aArguments.size(); ++i)
            {
                auto const source = aArguments[i];
                if (is_nullish(source))
                    continue;
                auto const from = to_object(source);
                for (auto const key : obj(from).own_property_keys())
                {
                    auto const desc = obj(from).get_own_property(key);
                    if (desc && desc->enumerable.value_or(false))
                        set(to, key, get(from, key), true);
                }
            }
            return to;
        }

        value object_create(value, arguments aArguments, value, function_object&)
        {
            auto const proto = argument(aArguments, 0u);
            if (!is_object(proto) && proto != Null)
                throw_type_error("Object prototype may only be an Object or null: " + describe(proto));
            auto const o = make_object(proto);
            auto const properties = argument(aArguments, 1u);
            if (properties != Undefined)
                object_define_properties(o, properties);
            return o;
        }

        value object_define_properties_function(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            if (!is_object(o))
                throw_type_error("Object.defineProperties called on non-object");
            object_define_properties(o, argument(aArguments, 1u));
            return o;
        }

        value object_define_property(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            if (!is_object(o))
                throw_type_error("Object.defineProperty called on non-object");
            auto const key = to_property_key(argument(aArguments, 1u));
            auto const desc = to_property_descriptor(argument(aArguments, 2u));
            define_property_or_throw(o, key, desc);
            return o;
        }

        value object_entries(value, arguments aArguments, value, function_object&)
        {
            return create_array_from_list(enumerable_own_properties(to_object(argument(aArguments, 0u)), 2));
        }

        value object_keys(value, arguments aArguments, value, function_object&)
        {
            return create_array_from_list(enumerable_own_properties(to_object(argument(aArguments, 0u)), 0));
        }

        value object_values(value, arguments aArguments, value, function_object&)
        {
            return create_array_from_list(enumerable_own_properties(to_object(argument(aArguments, 0u)), 1));
        }

        value object_freeze(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            if (!is_object(o))
                return o;
            if (!set_integrity_level(o, true))
                throw_type_error("Cannot freeze");
            return o;
        }

        value object_seal(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            if (!is_object(o))
                return o;
            if (!set_integrity_level(o, false))
                throw_type_error("Cannot seal");
            return o;
        }

        value object_prevent_extensions(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            if (!is_object(o))
                return o;
            if (!obj(o).prevent_extensions())
                throw_type_error("Cannot prevent extensions");
            return o;
        }

        value object_is_frozen(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            return boolean(!is_object(o) || test_integrity_level(o, true));
        }

        value object_is_sealed(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            return boolean(!is_object(o) || test_integrity_level(o, false));
        }

        value object_is_extensible(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            return boolean(is_object(o) && obj(o).is_extensible());
        }

        value object_from_entries(value, arguments aArguments, value, function_object&)
        {
            auto const iterable = argument(aArguments, 0u);
            if (is_nullish(iterable))
                throw_type_error(describe(iterable) + " is not iterable");
            auto const o = make_object();
            auto record = get_iterator(iterable);
            while (auto const next = iterator_step_value(record))
            {
                try
                {
                    if (!is_object(*next))
                        throw_type_error("Iterator value " + describe(*next) + " is not an entry object");
                    auto const k = get(*next, index_key(0u));
                    auto const v = get(*next, index_key(1u));
                    create_data_property_or_throw(o, to_property_key(k), v);
                }
                catch (...)
                {
                    iterator_close_on_throw(record);
                    throw;
                }
            }
            return o;
        }

        value object_get_own_property_descriptor(value, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(argument(aArguments, 0u));
            auto const key = to_property_key(argument(aArguments, 1u));
            return from_property_descriptor(obj(o).get_own_property(key));
        }

        value object_get_own_property_descriptors(value, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(argument(aArguments, 0u));
            auto const result = make_object();
            for (auto const key : obj(o).own_property_keys())
            {
                auto const desc = from_property_descriptor(obj(o).get_own_property(key));
                if (desc != Undefined)
                    create_data_property_or_throw(result, key, desc);
            }
            return result;
        }

        value own_keys(value aValue, bool aSymbols)
        {
            auto const o = to_object(aValue);
            value_list result;
            for (auto const key : obj(o).own_property_keys())
                if (is_symbol(key) == aSymbols)
                    result.push_back(key);
            return create_array_from_list(result);
        }

        value object_get_own_property_names(value, arguments aArguments, value, function_object&)
        {
            return own_keys(argument(aArguments, 0u), false);
        }

        value object_get_own_property_symbols(value, arguments aArguments, value, function_object&)
        {
            return own_keys(argument(aArguments, 0u), true);
        }

        value object_get_prototype_of(value, arguments aArguments, value, function_object&)
        {
            return obj(to_object(argument(aArguments, 0u))).get_prototype_of();
        }

        value object_set_prototype_of(value, arguments aArguments, value, function_object&)
        {
            auto const o = argument(aArguments, 0u);
            auto const proto = argument(aArguments, 1u);
            if (is_nullish(o))
                throw_type_error("Object.setPrototypeOf called on null or undefined");
            if (!is_object(proto) && proto != Null)
                throw_type_error("Object prototype may only be an Object or null: " + describe(proto));
            if (!is_object(o))
                return o;
            if (!obj(o).set_prototype_of(proto))
                throw_type_error("Cannot set prototype");
            return o;
        }

        value object_has_own(value, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(argument(aArguments, 0u));
            auto const key = to_property_key(argument(aArguments, 1u));
            return boolean(has_own_property(o, key));
        }

        value object_is(value, arguments aArguments, value, function_object&)
        {
            return boolean(same_value(argument(aArguments, 0u), argument(aArguments, 1u)));
        }

        value object_group_by(value, arguments aArguments, value, function_object&)
        {
            auto const items = argument(aArguments, 0u);
            auto const callback = argument(aArguments, 1u);
            if (is_nullish(items))
                throw_type_error("Object.groupBy called on null or undefined");
            if (!is_callable(callback))
                throw_type_error(describe(callback) + " is not a function");
            rooted_vector<std::pair<value, value_list>> groups;
            auto record = get_iterator(items);
            double k = 0.0;
            while (auto const next = iterator_step_value(record))
            {
                value key;
                try
                {
                    value const args[] = { *next, number(k) };
                    key = to_property_key(call(callback, Undefined, args));
                }
                catch (...)
                {
                    iterator_close_on_throw(record);
                    throw;
                }
                auto existing = std::find_if(groups.begin(), groups.end(), [&](auto const& g) { return same_value(g.first, key); });
                if (existing == groups.end())
                    groups.emplace_back(key, value_list{ *next });
                else
                    existing->second.push_back(*next);
                ++k;
            }
            auto const result = make_object(Null);
            for (auto const& g : groups)
                create_data_property_or_throw(result, g.first, create_array_from_list(g.second));
            return result;
        }

        // --- Object.prototype

        value object_prototype_has_own_property(value aThis, arguments aArguments, value, function_object&)
        {
            auto const key = to_property_key(argument(aArguments, 0u));
            auto const o = to_object(aThis);
            return boolean(has_own_property(o, key));
        }

        value object_prototype_is_prototype_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto v = argument(aArguments, 0u);
            if (!is_object(v))
                return False;
            auto const o = to_object(aThis);
            for (;;)
            {
                v = obj(v).get_prototype_of();
                if (v == Null)
                    return False;
                if (v == o)
                    return True;
            }
        }

        value object_prototype_property_is_enumerable(value aThis, arguments aArguments, value, function_object&)
        {
            auto const key = to_property_key(argument(aArguments, 0u));
            auto const o = to_object(aThis);
            auto const desc = obj(o).get_own_property(key);
            return boolean(desc && desc->enumerable.value_or(false));
        }

        value object_prototype_to_locale_string(value aThis, arguments, value, function_object&)
        {
            return invoke(aThis, the_names().toString);
        }

        value object_prototype_value_of(value aThis, arguments, value, function_object&)
        {
            return to_object(aThis);
        }

        value object_prototype_to_string_of(value aThis)
        {
            if (aThis == Undefined)
                return ascii("[object Undefined]");
            if (aThis == Null)
                return ascii("[object Null]");
            auto const o = to_object(aThis);
            char const* builtinTag = "Object";
            auto& object = obj(o);
            if (is_array(o))
                builtinTag = "Array";
            else
                switch (object.cls)
                {
                case object_class::Arguments: builtinTag = "Arguments"; break;
                case object_class::Function: builtinTag = "Function"; break;
                case object_class::Error: builtinTag = "Error"; break;
                case object_class::Boolean: builtinTag = "Boolean"; break;
                case object_class::Number: builtinTag = "Number"; break;
                case object_class::String: builtinTag = "String"; break;
                case object_class::Date: builtinTag = "Date"; break;
                case object_class::RegExp: builtinTag = "RegExp"; break;
                default:
                    if (object.callable())
                        builtinTag = "Function";
                    break;
                }
            auto const tag = get(o, the_realm().symToStringTag);
            std::u16string result = u"[object ";
            if (is_string(tag))
                result += str(tag);
            else
                result += utf16_ascii(builtinTag);
            result += u"]";
            return make_string(result);
        }

        value object_prototype_proto_get(value aThis, arguments, value, function_object&)
        {
            return obj(to_object(aThis)).get_prototype_of();
        }

        value object_prototype_proto_set(value aThis, arguments aArguments, value, function_object&)
        {
            if (is_nullish(aThis))
                throw_type_error("Object.prototype.__proto__ called on null or undefined");
            auto const proto = argument(aArguments, 0u);
            if (!is_object(proto) && proto != Null)
                return Undefined;
            if (!is_object(aThis))
                return Undefined;
            if (!obj(aThis).set_prototype_of(proto))
                throw_type_error("Object.prototype.__proto__ setter failed");
            return Undefined;
        }

        value object_prototype_define_getter(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const getter = argument(aArguments, 1u);
            if (!is_callable(getter))
                throw_type_error("Object.prototype.__defineGetter__: Expecting function");
            property_descriptor d;
            d.getter = getter;
            d.enumerable = true;
            d.configurable = true;
            define_property_or_throw(o, to_property_key(argument(aArguments, 0u)), d);
            return Undefined;
        }

        value object_prototype_define_setter(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const setter = argument(aArguments, 1u);
            if (!is_callable(setter))
                throw_type_error("Object.prototype.__defineSetter__: Expecting function");
            property_descriptor d;
            d.setter = setter;
            d.enumerable = true;
            d.configurable = true;
            define_property_or_throw(o, to_property_key(argument(aArguments, 0u)), d);
            return Undefined;
        }

        value lookup_accessor(value aThis, value aKey, bool aGetter)
        {
            auto o = to_object(aThis);
            auto const key = to_property_key(aKey);
            for (;;)
            {
                auto const desc = obj(o).get_own_property(key);
                if (desc)
                {
                    if (desc->is_accessor())
                        return aGetter ? desc->getter.value_or(Undefined) : desc->setter.value_or(Undefined);
                    return Undefined;
                }
                o = obj(o).get_prototype_of();
                if (o == Null)
                    return Undefined;
            }
        }

        value object_prototype_lookup_getter(value aThis, arguments aArguments, value, function_object&)
        {
            return lookup_accessor(aThis, argument(aArguments, 0u), true);
        }

        value object_prototype_lookup_setter(value aThis, arguments aArguments, value, function_object&)
        {
            return lookup_accessor(aThis, argument(aArguments, 0u), false);
        }

        value object_prototype_to_string(value aThis, arguments, value, function_object&)
        {
            return object_prototype_to_string_of(aThis);
        }
    }

    value object_prototype_to_string_value(value aObject)
    {
        // (%Object.prototype.toString% itself (Array.prototype.toString's fallback))
        return object_prototype_to_string_of(aObject);
    }

    void object_define_properties(value aObject, value aProperties)
    {
        auto const props = to_object(aProperties);
        rooted_vector<std::pair<value, property_descriptor>> descriptors;
        for (auto const key : obj(props).own_property_keys())
        {
            auto const desc = obj(props).get_own_property(key);
            if (desc && desc->enumerable.value_or(false))
                descriptors.emplace_back(key, to_property_descriptor(get(props, key)));
        }
        for (auto const& d : descriptors)
            define_property_or_throw(aObject, d.first, d.second);
    }

    void init_object(realm& aRealm)
    {
        auto const c = make_native("Object", 1u, object_constructor, true);
        aRealm.object = c;
        obj(c).put(the_names().prototype, aRealm.objectPrototype, 0u);
        method(c, "assign", 2u, object_assign);
        method(c, "create", 2u, object_create);
        method(c, "defineProperties", 2u, object_define_properties_function);
        method(c, "defineProperty", 3u, object_define_property);
        method(c, "entries", 1u, object_entries);
        method(c, "freeze", 1u, object_freeze);
        method(c, "fromEntries", 1u, object_from_entries);
        method(c, "getOwnPropertyDescriptor", 2u, object_get_own_property_descriptor);
        method(c, "getOwnPropertyDescriptors", 1u, object_get_own_property_descriptors);
        method(c, "getOwnPropertyNames", 1u, object_get_own_property_names);
        method(c, "getOwnPropertySymbols", 1u, object_get_own_property_symbols);
        method(c, "getPrototypeOf", 1u, object_get_prototype_of);
        method(c, "groupBy", 2u, object_group_by);
        method(c, "hasOwn", 2u, object_has_own);
        method(c, "is", 2u, object_is);
        method(c, "isExtensible", 1u, object_is_extensible);
        method(c, "isFrozen", 1u, object_is_frozen);
        method(c, "isSealed", 1u, object_is_sealed);
        method(c, "keys", 1u, object_keys);
        method(c, "preventExtensions", 1u, object_prevent_extensions);
        method(c, "seal", 1u, object_seal);
        method(c, "setPrototypeOf", 2u, object_set_prototype_of);
        method(c, "values", 1u, object_values);
        auto const p = aRealm.objectPrototype;
        obj(p).put(the_names().constructor, c);
        method(p, "__defineGetter__", 2u, object_prototype_define_getter);
        method(p, "__defineSetter__", 2u, object_prototype_define_setter);
        method(p, "hasOwnProperty", 1u, object_prototype_has_own_property);
        method(p, "isPrototypeOf", 1u, object_prototype_is_prototype_of);
        method(p, "__lookupGetter__", 1u, object_prototype_lookup_getter);
        method(p, "__lookupSetter__", 1u, object_prototype_lookup_setter);
        method(p, "propertyIsEnumerable", 1u, object_prototype_property_is_enumerable);
        method(p, "toLocaleString", 0u, object_prototype_to_locale_string);
        method(p, "toString", 0u, object_prototype_to_string);
        method(p, "valueOf", 0u, object_prototype_value_of);
        getter(p, ascii("__proto__"), object_prototype_proto_get, object_prototype_proto_set);
        obj(aRealm.global).put(ascii("Object"), c);
    }
}
