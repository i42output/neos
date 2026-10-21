/*
  object.cpp

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

// Objects: property storage, the ordinary internal methods, arrays (exotic), functions ([[Call]] and [[Construct]])
// and errors

#include <algorithm>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    // --- property tables

    property* property_table::find(value aKey)
    {
        if (iIndex)
        {
            auto const existing = iIndex->find(aKey);
            return existing != iIndex->end() ? &iEntries[existing->second] : nullptr;
        }
        for (auto& e : iEntries)
            if (e.key == aKey)
                return &e;
        return nullptr;
    }

    property& property_table::add(value aKey, value aData, value aSetter, std::uint8_t aAttributes)
    {
        iEntries.push_back(property{ aKey, aData, aSetter, aAttributes });
        if (iIndex)
            iIndex->emplace(aKey, static_cast<std::uint32_t>(iEntries.size() - 1u));
        else if (iEntries.size() > 8u)
            reindex();
        return iEntries.back();
    }

    void property_table::remove(value aKey)
    {
        auto* const p = find(aKey);
        if (p == nullptr)
            return;
        if (iIndex)
            iIndex->erase(aKey);
        if (p == &iEntries.back())
            iEntries.pop_back();
        else
        {
            p->key = 0u;
            ++iDeleted;
        }
        if (iDeleted > 8u && iDeleted > iEntries.size() / 2u)
        {
            std::erase_if(iEntries, [](property const& e) { return e.key == 0u; });
            iDeleted = 0u;
            if (iIndex)
                reindex();
        }
    }

    void property_table::reindex()
    {
        if (!iIndex)
            iIndex = std::make_unique<std::unordered_map<value, std::uint32_t>>();
        iIndex->clear();
        for (std::uint32_t i = 0u; i < iEntries.size(); ++i)
            if (iEntries[i].key != 0u)
                iIndex->emplace(iEntries[i].key, i);
    }

    // --- ordinary objects

    value make_object(value aPrototype)
    {
        return value_of(the_heap().make<object>(aPrototype));
    }

    value make_object()
    {
        return make_object(the_realm().objectPrototype);
    }

    void object::put(value aKey, value aValue, std::uint8_t aAttributes)
    {
        if (auto* existing = properties.find(aKey))
        {
            existing->data = aValue;
            existing->setter = Undefined;
            existing->attributes = aAttributes;
            return;
        }
        properties.add(aKey, aValue, Undefined, aAttributes);
    }

    void object::put_accessor(value aKey, value aGetter, value aSetter, std::uint8_t aAttributes)
    {
        aAttributes = static_cast<std::uint8_t>((aAttributes & ~AttributeWritable) | AttributeAccessor);
        if (auto* existing = properties.find(aKey))
        {
            existing->data = aGetter;
            existing->setter = aSetter;
            existing->attributes = aAttributes;
            return;
        }
        properties.add(aKey, aGetter, aSetter, aAttributes);
    }

    value object::get_prototype_of()
    {
        return proto;
    }

    bool object::ordinary_set_prototype_of(value aPrototype)
    {
        if (aPrototype == proto)
            return true;
        if (!extensible)
            return false;
        auto p = aPrototype;
        while (p != Null)
        {
            auto& o = obj(p);
            if (&o == this)
                return false;
            if (o.cls == object_class::Proxy)
                break;
            p = o.proto;
        }
        proto = aPrototype;
        return true;
    }

    bool object::set_prototype_of(value aPrototype)
    {
        return ordinary_set_prototype_of(aPrototype);
    }

    bool object::is_extensible()
    {
        return extensible;
    }

    bool object::prevent_extensions()
    {
        extensible = false;
        return true;
    }

    std::optional<property_descriptor> object::ordinary_get_own_property(value aKey)
    {
        auto const* p = properties.find(aKey);
        if (p == nullptr)
            return {};
        return p->descriptor();
    }

    std::optional<property_descriptor> object::get_own_property(value aKey)
    {
        return ordinary_get_own_property(aKey);
    }

    bool is_compatible_property_descriptor(bool aExtensible, property_descriptor const& aDescriptor, std::optional<property_descriptor> const& aCurrent)
    {
        return validate_and_apply_property_descriptor(nullptr, Undefined, aExtensible, aDescriptor, aCurrent);
    }

    bool validate_and_apply_property_descriptor(object* aObject, value aKey, bool aExtensible, property_descriptor const& aDescriptor,
        std::optional<property_descriptor> const& aCurrent)
    {
        if (!aCurrent)
        {
            if (!aExtensible)
                return false;
            if (aObject != nullptr)
            {
                std::uint8_t attributes = 0u;
                if (aDescriptor.enumerable.value_or(false))
                    attributes |= AttributeEnumerable;
                if (aDescriptor.configurable.value_or(false))
                    attributes |= AttributeConfigurable;
                if (aDescriptor.is_accessor())
                    aObject->properties.add(aKey, aDescriptor.getter.value_or(Undefined), aDescriptor.setter.value_or(Undefined),
                        static_cast<std::uint8_t>(attributes | AttributeAccessor));
                else
                {
                    if (aDescriptor.writable.value_or(false))
                        attributes |= AttributeWritable;
                    aObject->properties.add(aKey, aDescriptor.valueField.value_or(Undefined), Undefined, attributes);
                }
            }
            return true;
        }
        auto const& current = *aCurrent;
        if (aDescriptor.is_generic() && !aDescriptor.enumerable && !aDescriptor.configurable)
            return true;
        if (!current.configurable.value_or(false))
        {
            if (aDescriptor.configurable.value_or(false))
                return false;
            if (aDescriptor.enumerable && *aDescriptor.enumerable != current.enumerable.value_or(false))
                return false;
            if (!aDescriptor.is_generic() && aDescriptor.is_accessor() != current.is_accessor())
                return false;
            if (current.is_accessor())
            {
                if (aDescriptor.getter && !same_value(*aDescriptor.getter, current.getter.value_or(Undefined)))
                    return false;
                if (aDescriptor.setter && !same_value(*aDescriptor.setter, current.setter.value_or(Undefined)))
                    return false;
            }
            else if (!current.writable.value_or(false))
            {
                if (aDescriptor.writable.value_or(false))
                    return false;
                if (aDescriptor.valueField && !same_value(*aDescriptor.valueField, current.valueField.value_or(Undefined)))
                    return false;
            }
        }
        if (aObject != nullptr)
        {
            auto* p = aObject->properties.find(aKey);
            if (p == nullptr)
                return true;    // (an exotic object's own property that is not in its table: it applies the descriptor itself)
            if (current.is_data() && aDescriptor.is_accessor())
            {
                std::uint8_t attributes = AttributeAccessor;
                if (aDescriptor.configurable ? *aDescriptor.configurable : p->configurable())
                    attributes |= AttributeConfigurable;
                if (aDescriptor.enumerable ? *aDescriptor.enumerable : p->enumerable())
                    attributes |= AttributeEnumerable;
                p->data = aDescriptor.getter.value_or(Undefined);
                p->setter = aDescriptor.setter.value_or(Undefined);
                p->attributes = attributes;
            }
            else if (current.is_accessor() && aDescriptor.is_data())
            {
                std::uint8_t attributes = 0u;
                if (aDescriptor.configurable ? *aDescriptor.configurable : p->configurable())
                    attributes |= AttributeConfigurable;
                if (aDescriptor.enumerable ? *aDescriptor.enumerable : p->enumerable())
                    attributes |= AttributeEnumerable;
                if (aDescriptor.writable.value_or(false))
                    attributes |= AttributeWritable;
                p->data = aDescriptor.valueField.value_or(Undefined);
                p->setter = Undefined;
                p->attributes = attributes;
            }
            else
            {
                if (aDescriptor.valueField)
                    p->data = *aDescriptor.valueField;
                if (aDescriptor.getter)
                    p->data = *aDescriptor.getter;
                if (aDescriptor.setter)
                    p->setter = *aDescriptor.setter;
                auto const flag = [&](std::optional<bool> const& aField, std::uint8_t aAttribute)
                {
                    if (aField)
                        p->attributes = static_cast<std::uint8_t>(*aField ? (p->attributes | aAttribute) : (p->attributes & ~aAttribute));
                };
                if (!p->accessor())
                    flag(aDescriptor.writable, AttributeWritable);
                flag(aDescriptor.enumerable, AttributeEnumerable);
                flag(aDescriptor.configurable, AttributeConfigurable);
            }
        }
        return true;
    }

    bool object::ordinary_define_own_property(value aKey, property_descriptor const& aDescriptor)
    {
        auto const current = get_own_property(aKey);
        return validate_and_apply_property_descriptor(this, aKey, is_extensible(), aDescriptor, current);
    }

    bool object::define_own_property(value aKey, property_descriptor const& aDescriptor)
    {
        // (ordinary: the table is the object's own properties)
        auto const* p = properties.find(aKey);
        std::optional<property_descriptor> current;
        if (p != nullptr)
            current = p->descriptor();
        return validate_and_apply_property_descriptor(this, aKey, extensible, aDescriptor, current);
    }

    bool object::has_property(value aKey)
    {
        for (object* o = this;;)
        {
            if (o->properties.find(aKey) != nullptr)
                return true;
            auto const parent = o->get_prototype_of();
            if (parent == Null)
                return false;
            auto& p = obj(parent);
            if (typeid(p) != typeid(object) && typeid(p) != typeid(function_object))
                return p.has_property(aKey);
            o = &p;
        }
    }

    value object::ordinary_get(value aKey, value aReceiver)
    {
        auto const desc = get_own_property(aKey);
        if (!desc)
        {
            auto const parent = get_prototype_of();
            if (parent == Null)
                return Undefined;
            return obj(parent).get(aKey, aReceiver);
        }
        if (desc->is_data())
            return desc->valueField.value_or(Undefined);
        auto const getter = desc->getter.value_or(Undefined);
        if (getter == Undefined)
            return Undefined;
        return javascript_runtime::call(getter, aReceiver);
    }

    value object::get(value aKey, value aReceiver)
    {
        // (ordinary: own properties are the table's; the prototype chain is followed while it is ordinary)
        for (object* o = this;;)
        {
            if (auto const* p = o->properties.find(aKey))
            {
                if (!p->accessor())
                    return p->data;
                if (p->data == Undefined)
                    return Undefined;
                return javascript_runtime::call(p->data, aReceiver);
            }
            auto const parent = o->get_prototype_of();
            if (parent == Null)
                return Undefined;
            auto& next = obj(parent);
            if (typeid(next) != typeid(object) && typeid(next) != typeid(function_object))
                return next.get(aKey, aReceiver);
            o = &next;
        }
    }

    namespace
    {
        bool set_with_own_descriptor(object& aObject, value aKey, value aValue, value aReceiver, std::optional<property_descriptor> aOwn)
        {
            if (!aOwn)
            {
                auto const parent = aObject.get_prototype_of();
                if (parent != Null)
                    return obj(parent).set(aKey, aValue, aReceiver);
                aOwn = property_descriptor::data(Undefined, AttributesDefault);
            }
            if (aOwn->is_data())
            {
                if (!aOwn->writable.value_or(false))
                    return false;
                if (!is_object(aReceiver))
                    return false;
                auto& receiver = obj(aReceiver);
                auto const existing = receiver.get_own_property(aKey);
                if (existing)
                {
                    if (existing->is_accessor())
                        return false;
                    if (!existing->writable.value_or(false))
                        return false;
                    property_descriptor valueDescriptor;
                    valueDescriptor.valueField = aValue;
                    return receiver.define_own_property(aKey, valueDescriptor);
                }
                return create_data_property(aReceiver, aKey, aValue);
            }
            auto const setter = aOwn->setter.value_or(Undefined);
            if (setter == Undefined)
                return false;
            value const argument[] = { aValue };
            javascript_runtime::call(setter, aReceiver, argument);
            return true;
        }
    }

    bool object::ordinary_set(value aKey, value aValue, value aReceiver)
    {
        return set_with_own_descriptor(*this, aKey, aValue, aReceiver, get_own_property(aKey));
    }

    bool object::set(value aKey, value aValue, value aReceiver)
    {
        // (fast path: an ordinary receiver that is this object and has the property as a writable data property)
        if (auto* p = properties.find(aKey))
        {
            if (!p->accessor() && aReceiver == value_of(*this))
            {
                if (!p->writable())
                    return false;
                p->data = aValue;
                return true;
            }
            return set_with_own_descriptor(*this, aKey, aValue, aReceiver, p->descriptor());
        }
        return set_with_own_descriptor(*this, aKey, aValue, aReceiver, std::nullopt);
    }

    bool object::ordinary_delete(value aKey)
    {
        auto const desc = get_own_property(aKey);
        if (!desc)
            return true;
        if (desc->configurable.value_or(false))
        {
            properties.remove(aKey);
            return true;
        }
        return false;
    }

    bool object::delete_property(value aKey)
    {
        auto const* p = properties.find(aKey);
        if (p == nullptr)
            return true;
        if (p->configurable())
        {
            properties.remove(aKey);
            return true;
        }
        return false;
    }

    std::vector<value> object::ordinary_own_property_keys()
    {
        std::vector<std::pair<std::uint32_t, value>> indices;
        std::vector<value> strings;
        std::vector<value> symbols;
        properties.for_each([&](property const& p)
        {
            if (is_symbol(p.key))
                symbols.push_back(p.key);
            else if (auto const index = array_index(p.key))
                indices.emplace_back(*index, p.key);
            else
                strings.push_back(p.key);
        });
        std::sort(indices.begin(), indices.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::vector<value> result;
        result.reserve(indices.size() + strings.size() + symbols.size());
        for (auto const& i : indices)
            result.push_back(i.second);
        result.insert(result.end(), strings.begin(), strings.end());
        result.insert(result.end(), symbols.begin(), symbols.end());
        return result;
    }

    std::vector<value> object::own_property_keys()
    {
        return ordinary_own_property_keys();
    }

    value object::call(value, arguments)
    {
        throw_type_error("not a function");
    }

    value object::construct(arguments, value)
    {
        throw_type_error("not a constructor");
    }

    // --- arrays

    value make_array(std::size_t aLength)
    {
        if (aLength > 0xFFFFFFFFull)
            throw_range_error("invalid array length");
        auto& a = the_heap().make<array_object>(the_realm().arrayPrototype);
        a.length = static_cast<std::uint32_t>(aLength);
        return value_of(a);
    }

    value make_array(std::vector<value> aElements)
    {
        auto& a = the_heap().make<array_object>(the_realm().arrayPrototype);
        a.length = static_cast<std::uint32_t>(aElements.size());
        a.elements = std::move(aElements);
        return value_of(a);
    }

    void array_object::push(value aValue)
    {
        if (!sparse && elements.size() == length)
        {
            elements.push_back(aValue);
            ++length;
            return;
        }
        auto const key = index_key(length);
        create_data_property(value_of(*this), key, aValue);
    }

    void array_object::make_sparse()
    {
        if (sparse)
            return;
        sparse = true;
        // (the elements become ordinary properties: indices first (they are ordered by index anyway))
        property_table table;
        for (std::size_t i = 0u; i < elements.size(); ++i)
            if (elements[i] != Empty)
                table.add(index_key(i), elements[i], Undefined, AttributesDefault);
        properties.for_each([&](property const& p) { table.add(p.key, p.data, p.setter, p.attributes); });
        properties = std::move(table);
        elements.clear();
        elements.shrink_to_fit();
    }

    std::optional<property_descriptor> array_object::get_own_property(value aKey)
    {
        if (aKey == the_names().length)
            return property_descriptor::data(number(length), static_cast<std::uint8_t>(lengthWritable ? AttributeWritable : 0u));
        if (!sparse)
            if (auto const index = array_index(aKey))
            {
                if (*index < elements.size() && elements[*index] != Empty)
                    return property_descriptor::data(elements[*index], AttributesDefault);
                return {};
            }
        return ordinary_get_own_property(aKey);
    }

    bool array_object::set_length(property_descriptor const& aDescriptor)
    {
        // (ArraySetLength; "length" is a data property that is neither enumerable nor configurable)
        auto const define_length = [&](property_descriptor const& aLength)
        {
            auto const current = property_descriptor::data(number(length), static_cast<std::uint8_t>(lengthWritable ? AttributeWritable : 0u));
            if (!validate_and_apply_property_descriptor(nullptr, Undefined, true, aLength, current))
                return false;
            if (aLength.valueField)
                length = to_uint32(as_number(*aLength.valueField));
            if (aLength.writable)
                lengthWritable = *aLength.writable;
            return true;
        };
        if (!aDescriptor.valueField)
            return define_length(aDescriptor);
        auto const newLength = to_uint32(*aDescriptor.valueField);
        auto const numberLength = to_number(*aDescriptor.valueField);
        if (static_cast<double>(newLength) != numberLength)
            throw_range_error("invalid array length");
        property_descriptor newLengthDescriptor = aDescriptor;
        newLengthDescriptor.valueField = number(newLength);
        if (newLength >= length)
            return define_length(newLengthDescriptor);
        if (!lengthWritable)
            return false;
        bool const newWritable = !(newLengthDescriptor.writable && !*newLengthDescriptor.writable);
        if (!newWritable)
            newLengthDescriptor.writable = true;
        if (!define_length(newLengthDescriptor))
            return false;
        if (!sparse)
        {
            if (elements.size() > newLength)
                elements.resize(newLength);
            length = newLength;
            if (!newWritable)
                lengthWritable = false;
            return true;
        }
        // (delete the index properties from the greatest down; a non-configurable one stops it)
        std::vector<std::uint32_t> indices;
        properties.for_each([&](property const& p)
        {
            if (auto const index = array_index(p.key); index && *index >= newLength)
                indices.push_back(*index);
        });
        std::sort(indices.begin(), indices.end(), std::greater<>{});
        for (auto const index : indices)
        {
            auto const key = index_key(index);
            auto const* p = properties.find(key);
            if (p != nullptr && !p->configurable())
            {
                length = index + 1u;
                if (!newWritable)
                    lengthWritable = false;
                return false;
            }
            properties.remove(key);
        }
        length = newLength;
        if (!newWritable)
            lengthWritable = false;
        return true;
    }

    bool array_object::define_own_property(value aKey, property_descriptor const& aDescriptor)
    {
        if (aKey == the_names().length)
            return set_length(aDescriptor);
        auto const index = array_index(aKey);
        if (!index)
            return object::define_own_property(aKey, aDescriptor);
        if (*index >= length && !lengthWritable)
            return false;
        if (!sparse)
        {
            bool const plain = !aDescriptor.is_accessor() &&
                aDescriptor.writable.value_or(false) && aDescriptor.enumerable.value_or(false) && aDescriptor.configurable.value_or(false) &&
                aDescriptor.valueField.has_value();
            bool const exists = *index < elements.size() && elements[*index] != Empty;
            if (exists && !aDescriptor.is_accessor() && (!aDescriptor.writable || *aDescriptor.writable) &&
                (!aDescriptor.enumerable || *aDescriptor.enumerable) && (!aDescriptor.configurable || *aDescriptor.configurable))
            {
                // (an existing element stays a plain element)
                if (aDescriptor.valueField)
                    elements[*index] = *aDescriptor.valueField;
                return true;
            }
            if (!exists && plain && extensible && *index <= elements.size() + 64u)
            {
                if (*index >= elements.size())
                    elements.resize(*index + 1u, Empty);
                elements[*index] = *aDescriptor.valueField;
                if (*index >= length)
                    length = *index + 1u;
                return true;
            }
            if (!exists && !extensible)
                return false;
            make_sparse();
        }
        if (!object::define_own_property(aKey, aDescriptor))
            return false;
        if (*index >= length)
            length = *index + 1u;
        return true;
    }

    bool array_object::has_property(value aKey)
    {
        if (!sparse)
            if (auto const index = array_index(aKey))
            {
                if (*index < elements.size() && elements[*index] != Empty)
                    return true;
                auto const parent = get_prototype_of();
                return parent != Null && obj(parent).has_property(aKey);
            }
        if (aKey == the_names().length)
            return true;
        if (properties.find(aKey) != nullptr)
            return true;
        auto const parent = get_prototype_of();
        return parent != Null && obj(parent).has_property(aKey);
    }

    value array_object::get(value aKey, value aReceiver)
    {
        if (aKey == the_names().length)
            return number(length);
        if (!sparse)
            if (auto const index = array_index(aKey))
            {
                if (*index < elements.size() && elements[*index] != Empty)
                    return elements[*index];
                auto const parent = get_prototype_of();
                return parent == Null ? Undefined : obj(parent).get(aKey, aReceiver);
            }
        return ordinary_get(aKey, aReceiver);
    }

    bool array_object::set(value aKey, value aValue, value aReceiver)
    {
        if (!sparse && aReceiver == value_of(*this))
            if (auto const index = array_index(aKey); index && *index < elements.size() && elements[*index] != Empty)
            {
                elements[*index] = aValue;
                return true;
            }
        return ordinary_set(aKey, aValue, aReceiver);
    }

    bool array_object::delete_property(value aKey)
    {
        if (aKey == the_names().length)
            return false;
        if (!sparse)
            if (auto const index = array_index(aKey))
            {
                if (*index < elements.size())
                {
                    elements[*index] = Empty;
                    if (*index + 1u == elements.size())
                        while (!elements.empty() && elements.back() == Empty)
                            elements.pop_back();
                }
                return true;
            }
        return ordinary_delete(aKey);
    }

    std::vector<value> array_object::own_property_keys()
    {
        std::vector<value> result;
        if (!sparse)
        {
            for (std::size_t i = 0u; i < elements.size(); ++i)
                if (elements[i] != Empty)
                    result.push_back(index_key(i));
            auto rest = ordinary_own_property_keys();
            // ("length" is the first string key)
            std::size_t firstString = 0u;
            while (firstString < rest.size() && array_index(rest[firstString]))
                ++firstString;
            result.insert(result.end(), rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(firstString));
            result.push_back(the_names().length);
            result.insert(result.end(), rest.begin() + static_cast<std::ptrdiff_t>(firstString), rest.end());
            return result;
        }
        auto keys = ordinary_own_property_keys();
        std::size_t firstString = 0u;
        while (firstString < keys.size() && array_index(keys[firstString]))
            ++firstString;
        keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(firstString), the_names().length);
        return keys;
    }

    // --- functions

    value function_object::call(value aThis, arguments aArguments)
    {
        realm_scope scope{ functionRealm };
        switch (kind)
        {
        case function_kind::Native:
            return native(aThis, aArguments, Undefined, *this);
        case function_kind::Compiled:
            if (flavour == function_flavour::ClassConstructor)
                throw_type_error("class constructor cannot be invoked without 'new'");
            return call_compiled(*this, aThis, aArguments, Undefined);
        case function_kind::Bound:
            {
                std::vector<value> all = boundArguments;
                all.insert(all.end(), aArguments.begin(), aArguments.end());
                return javascript_runtime::call(boundTarget, boundThis, all);
            }
        }
        return Undefined;
    }

    value function_object::construct(arguments aArguments, value aNewTarget)
    {
        realm_scope scope{ functionRealm };
        switch (kind)
        {
        case function_kind::Native:
            return native(Undefined, aArguments, aNewTarget, *this);
        case function_kind::Compiled:
            {
                if (derived)
                    return call_compiled(*this, Empty, aArguments, aNewTarget);
                auto const thisValue = ordinary_create_from_constructor(aNewTarget, &realm::objectPrototype);
                if (flavour == function_flavour::ClassConstructor)
                    initialize_instance_elements(thisValue, value_of(*this));
                auto const result = call_compiled(*this, thisValue, aArguments, aNewTarget);
                return is_object(result) ? result : thisValue;
            }
        case function_kind::Bound:
            {
                std::vector<value> all = boundArguments;
                all.insert(all.end(), aArguments.begin(), aArguments.end());
                if (aNewTarget == value_of(*this))
                    aNewTarget = boundTarget;
                return javascript_runtime::construct(boundTarget, all, aNewTarget);
            }
        }
        return Undefined;
    }

    value make_native(value aName, std::uint32_t aLength, native_function aFunction, bool aConstructor, value aPrototype)
    {
        auto& f = the_heap().make<function_object>(aPrototype != 0u ? aPrototype : the_realm().functionPrototype, function_kind::Native);
        f.functionRealm = &the_realm();
        f.native = aFunction;
        f.isConstructor = aConstructor;
        f.strict = true;
        f.put(the_names().length, number(aLength), AttributeConfigurable);
        f.put(the_names().name, is_symbol(aName) ? Undefined : aName, AttributeConfigurable);
        auto const result = value_of(f);
        if (is_symbol(aName))
            set_function_name(result, aName);
        return result;
    }

    value make_native(char const* aName, std::uint32_t aLength, native_function aFunction, bool aConstructor)
    {
        return make_native(ascii(aName), aLength, aFunction, aConstructor);
    }

    void set_function_name(value aFunction, value aName, char const* aPrefix)
    {
        value name = aName;
        if (is_symbol(aName) && cell_of<symbol>(aName).privateName)
            name = cell_of<symbol>(aName).description;
        else if (is_symbol(aName))
        {
            auto const description = cell_of<symbol>(aName).description;
            if (description == Undefined)
                name = the_names().empty;
            else
                name = concat(concat(ascii("["), description), ascii("]"));
        }
        if (aPrefix != nullptr)
            name = concat(concat(ascii(aPrefix), ascii(" ")), name);
        auto& f = obj(aFunction);
        if (auto* existing = f.own(the_names().name))
            existing->data = name;
        else
            f.properties.add(the_names().name, name, Undefined, AttributeConfigurable);
    }

    void set_function_length(value aFunction, double aLength)
    {
        auto& f = obj(aFunction);
        if (auto* existing = f.own(the_names().length))
            existing->data = number(aLength);
        else
            f.properties.add(the_names().length, number(aLength), Undefined, AttributeConfigurable);
    }

    value method(value aObject, value aKey, std::uint32_t aLength, native_function aFunction, std::uint8_t aAttributes)
    {
        auto const f = make_native(aKey, aLength, aFunction);
        obj(aObject).put(aKey, f, aAttributes);
        return f;
    }

    value method(value aObject, char const* aName, std::uint32_t aLength, native_function aFunction, std::uint8_t aAttributes)
    {
        return method(aObject, ascii(aName), aLength, aFunction, aAttributes);
    }

    value getter(value aObject, value aKey, native_function aGetter, native_function aSetter, std::uint8_t aAttributes)
    {
        auto const g = make_native(aKey, 0u, aGetter);
        set_function_name(g, aKey, "get");
        value s = Undefined;
        if (aSetter != nullptr)
        {
            s = make_native(aKey, 1u, aSetter);
            set_function_name(s, aKey, "set");
        }
        obj(aObject).put_accessor(aKey, g, s, aAttributes);
        return g;
    }

    void constant(value aObject, char const* aName, value aValue)
    {
        obj(aObject).put(ascii(aName), aValue, 0u);
    }

    // --- errors

    value make_error(value aPrototype, value aMessage)
    {
        auto& e = the_heap().make<object>(aPrototype, object_class::Error);
        if (aMessage != Undefined)
            e.put(the_names().message, aMessage, AttributesHidden);
        return value_of(e);
    }

    void throw_value(value aValue)
    {
        throw js_exception{ aValue };
    }

    void throw_error(value aPrototype, std::string const& aMessage)
    {
        throw js_exception{ make_error(aPrototype, make_string_utf8(aMessage)) };
    }

    void throw_type_error(std::string const& aMessage)
    {
        throw_error(the_realm().typeErrorPrototype, aMessage);
    }

    void throw_range_error(std::string const& aMessage)
    {
        throw_error(the_realm().rangeErrorPrototype, aMessage);
    }

    void throw_reference_error(std::string const& aMessage)
    {
        throw_error(the_realm().referenceErrorPrototype, aMessage);
    }

    void throw_syntax_error(std::string const& aMessage)
    {
        throw_error(the_realm().syntaxErrorPrototype, aMessage);
    }
}
