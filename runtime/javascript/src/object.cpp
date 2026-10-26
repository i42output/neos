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
    // --- shapes

    namespace
    {
        constexpr std::uint32_t MaxShapeKeys = 256u;        // (a table with more properties has no shape (e.g. an object used as a map))
        constexpr std::size_t MaxShapes = 1u << 18;         // (then tables that add properties have none)
        constexpr std::size_t FewKeys = 8u;                 // (keys that are found by searching them; more are indexed)

        // (a transition's key: the key and the attributes (a key's payload is its low 32 bits))
        value transition_key(value aKey, std::uint8_t aAttributes)
        {
            return aKey ^ (static_cast<value>(aAttributes) << 40u);
        }
    }

    shape::~shape()
    {
        // (the shapes that it transitions to: its (a shape's parent is the one that transitions to it))
        if (iManyTransitions)
            for (auto const& t : *iManyTransitions)
                delete t.second;
        for (auto const& t : iTransitions)
            delete t.second;
    }

    shape* shape::transition(value aKey, std::uint8_t aAttributes)
    {
        auto const k = transition_key(aKey, aAttributes);
        if (iManyTransitions)
        {
            auto const existing = iManyTransitions->find(k);
            if (existing != iManyTransitions->end())
                return existing->second;
        }
        else
            for (auto const& t : iTransitions)
                if (t.first == k)
                    return t.second;
        auto& shapes = the_heap().shape_count();
        if (count >= MaxShapeKeys || shapes >= MaxShapes)
            return nullptr;
        auto* const next = new shape{};
        ++shapes;
        pin_interned(aKey);     // (an equal key later (e.g. of another run of the same code) is this one: its transition is found)
        next->parent = this;
        next->key = aKey;
        next->attributes = aAttributes;
        next->count = count + 1u;
        if (iManyTransitions)
            iManyTransitions->emplace(k, next);
        else
        {
            iTransitions.emplace_back(k, next);
            if (iTransitions.size() > FewKeys)
            {
                iManyTransitions = std::make_unique<std::unordered_map<value, shape*>>(iTransitions.begin(), iTransitions.end());
                iTransitions.clear();
                iTransitions.shrink_to_fit();
            }
        }
        return next;
    }

    std::optional<std::uint32_t> shape::position(value aKey)
    {
        if (count <= FewKeys)
        {
            // (its slots (contiguous): searched)
            auto const all = slots();
            for (std::uint32_t i = 0u; i < all.size(); ++i)
                if (all[i].key == aKey)
                    return i;
            return {};
        }
        if (!iIndex)
        {
            iIndex = std::make_unique<std::unordered_map<value, std::uint32_t>>();
            iIndex->reserve(count);
            for (auto const* s = this; s->parent != nullptr; s = s->parent)
                iIndex->emplace(s->key, s->count - 1u);
        }
        auto const existing = iIndex->find(aKey);
        if (existing == iIndex->end())
            return {};
        return existing->second;
    }

    std::span<shape::slot const> shape::slots()
    {
        if (iSlots.size() != count)
        {
            iSlots.resize(count);
            for (auto const* s = this; s->parent != nullptr; s = s->parent)
                iSlots[s->count - 1u] = slot{ s->key, s->attributes };
        }
        return iSlots;
    }

    void accessor_pair::trace(tracer& aTracer)
    {
        aTracer.mark(getter);
        aTracer.mark(setter);
    }

    // --- property tables

    value property_ref::key() const
    {
        auto const& t = *iTable;
        return t.iShape != nullptr ? t.iShape->slots()[iPosition].key : t.entries()[iPosition].key;
    }

    std::uint8_t property_ref::attributes() const
    {
        auto const& t = *iTable;
        return t.iShape != nullptr ? t.iShape->slots()[iPosition].attributes : t.entries()[iPosition].attributes;
    }

    value property_ref::data() const
    {
        auto const& t = *iTable;
        if (t.iShape == nullptr)
            return t.entries()[iPosition].data;
        auto const v = t.iValues[iPosition];
        return (t.iShape->slots()[iPosition].attributes & AttributeAccessor) != 0u ? cell_of<accessor_pair>(v).getter : v;
    }

    value property_ref::setter() const
    {
        auto const& t = *iTable;
        if (t.iShape == nullptr)
            return t.entries()[iPosition].setter;
        return (t.iShape->slots()[iPosition].attributes & AttributeAccessor) != 0u ? cell_of<accessor_pair>(t.iValues[iPosition]).setter : Undefined;
    }

    void property_ref::set_data(value aData)
    {
        auto& t = *iTable;
        if (t.iShape == nullptr)
            t.entries()[iPosition].data = aData;
        else if ((t.iShape->slots()[iPosition].attributes & AttributeAccessor) != 0u)
            cell_of<accessor_pair>(t.iValues[iPosition]).getter = aData;
        else
            t.iValues[iPosition] = aData;
    }

    void property_ref::set(value aData, value aSetter, std::uint8_t aAttributes)
    {
        // (attributes that change: its layout is its own from now on (no shape))
        auto& t = *iTable;
        if (t.iShape != nullptr && t.iShape->slots()[iPosition].attributes != aAttributes)
            t.make_dictionary();
        if (t.iShape == nullptr)
        {
            auto& e = t.entries()[iPosition];
            e.data = aData;
            e.setter = aSetter;
            e.attributes = aAttributes;
            return;
        }
        if ((aAttributes & AttributeAccessor) != 0u)
        {
            auto& pair = cell_of<accessor_pair>(t.iValues[iPosition]);
            pair.getter = aData;
            pair.setter = aSetter;
        }
        else
            t.iValues[iPosition] = aData;
    }

    property_table::dictionary& property_table::dict()
    {
        if (iDictionary == 0u)
        {
            auto* const memory = region_allocate(sizeof(dictionary));
            iDictionary = region_offset(::new (memory) dictionary{});
        }
        return *dictionary_of();
    }

    void property_table::release_dictionary() noexcept
    {
        if (iDictionary == 0u)
            return;
        auto* const d = dictionary_of();
        d->~dictionary();
        region_release(d);
        iDictionary = 0u;
    }

    property_ref property_table::find(value aKey)
    {
        if (iShape != nullptr)
        {
            if (iValues.size() == 0u)
                return {};
            auto const position = iShape->position(aKey);
            return position ? property_ref{ *this, *position } : property_ref{};
        }
        if (entries().size() > FewKeys && index())
        {
            auto const existing = index()->find(aKey);
            return existing != index()->end() ? property_ref{ *this, existing->second } : property_ref{};
        }
        for (std::uint32_t i = 0u; i < entries().size(); ++i)
            if (entries()[i].key == aKey)
                return property_ref{ *this, i };
        return {};
    }

    void property_table::reserve(std::size_t aCapacity)
    {
        if (iShape != nullptr)
            iValues.reserve(aCapacity);
        else
            entries().reserve(aCapacity);
    }

    void property_table::make_dictionary()
    {
        if (iShape == nullptr)
            return;
        // (its entries: its shape's keys and attributes, and its values (an accessor's getter and setter))
        auto const slots = iShape->slots();
        entries().reserve(iValues.size());
        for (std::uint32_t i = 0u; i < iValues.size(); ++i)
        {
            property e{ slots[i].key, iValues[i], Undefined, slots[i].attributes };
            if (e.accessor())
            {
                auto const& pair = cell_of<accessor_pair>(iValues[i]);
                e.data = pair.getter;
                e.setter = pair.setter;
            }
            entries().push_back(e);
        }
        iValues = region_vector<value>{};
        iShape = nullptr;
        if (entries().size() > FewKeys)
            reindex();
    }

    property_ref property_table::add(value aKey, value aData, value aSetter, std::uint8_t aAttributes)
    {
        if (iShape != nullptr)
        {
            if (auto* const next = iShape->transition(aKey, aAttributes))
            {
                // (an accessor's value: its getter and setter (a pair))
                iValues.push_back((aAttributes & AttributeAccessor) != 0u ? value_of(the_heap().make<accessor_pair>(aData, aSetter)) : aData);
                iShape = next;
                return property_ref{ *this, static_cast<std::uint32_t>(iValues.size() - 1u) };
            }
            make_dictionary();
        }
        entries().push_back(property{ aKey, aData, aSetter, aAttributes });
        if (index())
            index()->emplace(aKey, static_cast<std::uint32_t>(entries().size() - 1u));
        else if (entries().size() > FewKeys)
            reindex();
        return property_ref{ *this, static_cast<std::uint32_t>(entries().size() - 1u) };
    }

    void property_table::add(value aData, shape* aShape)
    {
        iValues.push_back(aData);
        iShape = aShape;
    }

    void property_table::remove(value aKey)
    {
        auto const p = find(aKey);
        if (!p)
            return;
        make_dictionary();      // (its layout is its own from now on)
        auto const position = p.position();
        if (index())
            index()->erase(aKey);
        if (position + 1u == entries().size())
            entries().pop_back();
        else
        {
            entries()[position].key = 0u;
            ++deleted();
        }
        if (deleted() > 8u && deleted() > entries().size() / 2u)
        {
            entries().erase_if([](property const& e) { return e.key == 0u; });
            deleted() = 0u;
            if (index())
                reindex();
        }
    }

    bool property_table::cached_value(std::uint64_t aShape, std::uint32_t aPosition, value aKey, value& aResult) const
    {
        if (aShape != 0u)
        {
            if (reinterpret_cast<std::uintptr_t>(iShape) != aShape || aPosition >= iValues.size())
                return false;
            aResult = iValues[aPosition];
            return true;
        }
        if (iShape != nullptr || aPosition >= entries().size())
            return false;
        auto const& e = entries()[aPosition];
        if (e.key != aKey || e.accessor())
            return false;
        aResult = e.data;
        return true;
    }

    bool property_table::cached_store(std::uint64_t aShape, std::uint32_t aPosition, value aValue)
    {
        if (aShape == 0u || reinterpret_cast<std::uintptr_t>(iShape) != aShape || aPosition >= iValues.size())
            return false;
        iValues[aPosition] = aValue;
        return true;
    }

    void property_table::trace(tracer& aTracer) const
    {
        if (iShape != nullptr)
        {
            // (its shape's keys (once a collection: the shapes that it shares with others were marked by them))
            auto const epoch = aTracer.epoch();
            for (auto* s = iShape; s != nullptr && s->epoch != epoch; s = s->parent)
            {
                s->epoch = epoch;
                aTracer.mark(s->key);
            }
            aTracer.mark(std::span<value const>{ iValues.data(), iValues.size() });
        }
        else
            for (auto const& e : entries())
                aTracer.mark(e);
    }

    void property_table::reindex()
    {
        if (!index())
            index() = std::make_unique<std::unordered_map<value, std::uint32_t>>();
        index()->clear();
        for (std::uint32_t i = 0u; i < entries().size(); ++i)
            if (entries()[i].key != 0u)
                index()->emplace(entries()[i].key, i);
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
        if (auto existing = properties.find(aKey))
            existing->set(aValue, Undefined, aAttributes);
        else
            properties.add(aKey, aValue, Undefined, aAttributes);
        if (mirrored) [[unlikely]]
            global_object_changed(*this, aKey);
    }

    void object::put_accessor(value aKey, value aGetter, value aSetter, std::uint8_t aAttributes)
    {
        aAttributes = static_cast<std::uint8_t>((aAttributes & ~AttributeWritable) | AttributeAccessor);
        if (auto existing = properties.find(aKey))
            existing->set(aGetter, aSetter, aAttributes);
        else
            properties.add(aKey, aGetter, aSetter, aAttributes);
        if (mirrored) [[unlikely]]
            global_object_changed(*this, aKey);
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
        if (proto == Null && is_object_prototype(*this))
            return false;   // (an immutable prototype exotic object (SetImmutablePrototype))
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
        auto const p = properties.find(aKey);
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

    namespace
    {
        bool apply_property_descriptor(object* aObject, value aKey, bool aExtensible, property_descriptor const& aDescriptor,
            std::optional<property_descriptor> const& aCurrent);
    }

    bool validate_and_apply_property_descriptor(object* aObject, value aKey, bool aExtensible, property_descriptor const& aDescriptor,
        std::optional<property_descriptor> const& aCurrent)
    {
        auto const result = apply_property_descriptor(aObject, aKey, aExtensible, aDescriptor, aCurrent);
        if (aObject != nullptr && aObject->mirrored) [[unlikely]]
            global_object_changed(*aObject, aKey);
        return result;
    }

    namespace
    {
    bool apply_property_descriptor(object* aObject, value aKey, bool aExtensible, property_descriptor const& aDescriptor,
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
            auto p = aObject->properties.find(aKey);
            if (p == nullptr)
                return true;    // (an exotic object's own property that is not in its table: it applies the descriptor itself)
            if (current.is_data() && aDescriptor.is_accessor())
            {
                std::uint8_t attributes = AttributeAccessor;
                if (aDescriptor.configurable ? *aDescriptor.configurable : p->configurable())
                    attributes |= AttributeConfigurable;
                if (aDescriptor.enumerable ? *aDescriptor.enumerable : p->enumerable())
                    attributes |= AttributeEnumerable;
                p->set(aDescriptor.getter.value_or(Undefined), aDescriptor.setter.value_or(Undefined), attributes);
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
                p->set(aDescriptor.valueField.value_or(Undefined), Undefined, attributes);
            }
            else
            {
                auto data = p->data();
                auto setter = p->setter();
                auto attributes = p->attributes();
                if (aDescriptor.valueField)
                    data = *aDescriptor.valueField;
                if (aDescriptor.getter)
                    data = *aDescriptor.getter;
                if (aDescriptor.setter)
                    setter = *aDescriptor.setter;
                auto const flag = [&](std::optional<bool> const& aField, std::uint8_t aAttribute)
                {
                    if (aField)
                        attributes = static_cast<std::uint8_t>(*aField ? (attributes | aAttribute) : (attributes & ~aAttribute));
                };
                if ((attributes & AttributeAccessor) == 0u)
                    flag(aDescriptor.writable, AttributeWritable);
                flag(aDescriptor.enumerable, AttributeEnumerable);
                flag(aDescriptor.configurable, AttributeConfigurable);
                p->set(data, setter, attributes);
            }
        }
        return true;
    }
    }

    bool object::ordinary_define_own_property(value aKey, property_descriptor const& aDescriptor)
    {
        auto const current = get_own_property(aKey);
        return validate_and_apply_property_descriptor(this, aKey, is_extensible(), aDescriptor, current);
    }

    bool object::define_own_property(value aKey, property_descriptor const& aDescriptor)
    {
        // (ordinary: the table is the object's own properties)
        auto const p = properties.find(aKey);
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
            if (&typeid(p) != &typeid(object) && &typeid(p) != &typeid(function_object))
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
            if (auto const p = o->properties.find(aKey))
            {
                if (!p->accessor())
                    return p->data();
                auto const getter = p->data();
                if (getter == Undefined)
                    return Undefined;
                return javascript_runtime::call(getter, aReceiver);
            }
            auto const parent = o->get_prototype_of();
            if (parent == Null)
                return Undefined;
            auto& next = obj(parent);
            if (&typeid(next) != &typeid(object) && &typeid(next) != &typeid(function_object))
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
        if (auto p = properties.find(aKey))
        {
            if (!p->accessor() && aReceiver == value_of(*this))
            {
                if (!p->writable())
                    return false;
                p->set_data(aValue);
                if (mirrored) [[unlikely]]
                    global_object_changed(*this, aKey);
                return true;
            }
            return set_with_own_descriptor(*this, aKey, aValue, aReceiver, p->descriptor());
        }
        // (fast path: an ordinary, extensible receiver that is this object and none of whose prototypes (all ordinary) has the property:
        // the property is added (as [[Set]] adds it, by CreateDataProperty))
        if (aReceiver == value_of(*this) && extensible && !mirrored && ordinary_type(*this))
        {
            bool absent = true;
            for (auto parent = proto; parent != Null;)
            {
                auto& p = obj(parent);
                if (!ordinary_type(p) || p.properties.find(aKey) != nullptr)
                {
                    absent = false;
                    break;
                }
                parent = p.proto;
            }
            if (absent)
            {
                properties.add(aKey, aValue, Undefined, AttributesDefault);
                return true;
            }
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
            if (mirrored) [[unlikely]]
                global_object_changed(*this, aKey);
            return true;
        }
        return false;
    }

    bool object::delete_property(value aKey)
    {
        auto const p = properties.find(aKey);
        if (p == nullptr)
            return true;
        if (p->configurable())
        {
            properties.remove(aKey);
            if (mirrored) [[unlikely]]
                global_object_changed(*this, aKey);
            return true;
        }
        return false;
    }

    value_list object::ordinary_own_property_keys()
    {
        thread_local std::vector<std::pair<std::uint32_t, value>> tIndices;
        thread_local std::vector<value> tStrings;
        thread_local std::vector<value> tSymbols;
        tIndices.clear();
        tStrings.clear();
        tSymbols.clear();
        properties.for_each([&](property const& p)
        {
            if (is_symbol(p.key))
                tSymbols.push_back(p.key);
            else if (auto const index = array_index(p.key))
                tIndices.emplace_back(*index, p.key);
            else
                tStrings.push_back(p.key);
        });
        std::sort(tIndices.begin(), tIndices.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        value_list result;
        result.reserve(tIndices.size() + tStrings.size() + tSymbols.size());
        for (auto const& i : tIndices)
            result.push_back(i.second);
        result.insert(result.end(), tStrings.begin(), tStrings.end());
        result.insert(result.end(), tSymbols.begin(), tSymbols.end());
        return result;
    }

    value_list object::own_property_keys()
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

    value make_array(value_list aElements)
    {
        auto& a = the_heap().make<array_object>(the_realm().arrayPrototype);
        a.length = static_cast<std::uint32_t>(aElements.size());
        a.elements.assign(aElements.begin(), aElements.end());
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
        table.set_root(type_root_shape<array_object>());   // (an array's shapes are its own)
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
            auto const p = properties.find(key);
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

    value_list array_object::own_property_keys()
    {
        value_list result;
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
                value_list all{ boundArguments.begin(), boundArguments.end() };
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
                value_list all{ boundArguments.begin(), boundArguments.end() };
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
        if (auto existing = f.own(the_names().name))
            existing->set_data(name);
        else
            f.properties.add(the_names().name, name, Undefined, AttributeConfigurable);
    }

    void set_function_length(value aFunction, double aLength)
    {
        auto& f = obj(aFunction);
        if (auto existing = f.own(the_names().length))
            existing->set_data(number(aLength));
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
