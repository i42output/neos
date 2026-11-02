/*
  gen_expr.cpp

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

// Ada: the code generation of expressions (values, names, calls, operators, attributes, aggregates) and the representation of
// objects (sizes, record layouts, array descriptors)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cmath>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::ada
{
    using namespace emitter;

    namespace
    {
        bool is_aggregate_node(node const* aNode)
        {
            return aNode->is(construct::Aggregate) || aNode->is(construct::AggregateBracket) || aNode->is(construct::AggregateNullRecord) ||
                aNode->is(construct::AggregateExtension) || aNode->is(construct::AggregateDelta);
        }

        std::vector<node*> attribute_arguments(node* aAttribute)
        {
            std::vector<node*> result;
            if (auto* arguments = aAttribute->first(construct::Arguments))
                for (auto* a : arguments->children)
                    result.push_back(a);
            return result;
        }

        constexpr std::uint64_t DescriptorHeader(std::size_t aDimensions) { return 8u + 16u * aDimensions; }
    }

    // --- representation (the program's)

    std::uint64_t program::scalar_size(ada_type* aType)
    {
        if (aType == nullptr)
            return 8u;
        auto* t = aType->view()->base_type()->view();
        if (t->is_access() || t->is_fixed())
            return 8u;
        if (t->size && *t->size <= 8u)
            return *t->size;
        return 8u;
    }

    std::uint64_t program::object_size(ada_type* aType)
    {
        // (the size of its objects in bytes (0: dynamic): a scalar's; an array's descriptor and data; a record's components)
        if (aType == nullptr)
            return 8u;
        auto* t = aType->view();
        if (t->is_scalar() || t->is_access())
            return std::max<std::uint64_t>(scalar_size(t), 1u);
        switch (t->kind)
        {
        case type_kind::Array:
            {
                if (!t->constrained)
                    return 0u;
                std::uint64_t length = 1u;
                for (auto* index : t->indexes)
                {
                    auto* iv = index->view();
                    if (!iv->low.is_integer() || !iv->high.is_integer())
                        return 0u;
                    auto const n = iv->high.integer() - iv->low.integer() + bigint{ 1 };
                    if (n.negative() || n.is_zero())
                        length = 0u;
                    else
                        length *= n.to_uint64();
                }
                auto const element = component_size(t);
                if (element == 0u)
                    return 0u;
                return DescriptorHeader(t->indexes.size()) + ((length * element + 7u) & ~std::uint64_t{ 7u });
            }
        case type_kind::Record:
        case type_kind::Private:
        case type_kind::Protected:
        case type_kind::Task:
            layout(t->base_type()->view());
            if (t->base_type()->view()->dynamic_size)
                return 0u;
            return t->base_type()->view()->size.value_or(0u);
        case type_kind::ClassWide:
        case type_kind::Interface:
            return 0u;
        default:
            return 8u;
        }
    }

    bool program::bit_field(component_info const& aComponent)
    {
        return aComponent.bits;
    }

    static bool bit_field_candidate(program& aProgram, component_info const& aComponent)
    {
        // (a discrete or fixed point component (not a discriminant) that a component clause places at a bit offset, or in fewer bits
        // than its objects' size: stored in those bits (13.5.1), within 8 storage units)
        if (!aComponent.bit_position || !aComponent.bit_size || aComponent.discriminant || aComponent.type == nullptr)
            return false;
        auto* cv = aComponent.type->view();
        if (!cv->is_discrete() && !cv->is_fixed())
            return false;
        auto const bit = *aComponent.bit_position % 8u;
        auto const bits = *aComponent.bit_size;
        if (bit == 0u && bits >= 8u * aProgram.scalar_size(cv))
            return false;
        return bits >= 1u && bits <= 56u;
    }

    std::uint64_t program::bit_field_span(component_info const& aComponent)
    {
        auto const units = (*aComponent.bit_position % 8u + *aComponent.bit_size + 7u) / 8u;
        return units <= 1u ? 1u : units <= 2u ? 2u : units <= 4u ? 4u : 8u;
    }

    component_info const* program::bit_field_of(ada_type* aRecord, entity* aComponent)
    {
        if (aRecord == nullptr || aComponent == nullptr)
            return nullptr;
        auto* base = aRecord->view()->base_type()->view();
        if (base->kind != type_kind::Record)
            return nullptr;
        layout(base);
        for (auto const& c : base->components)
            if (c.ent == aComponent)
                return bit_field(c) ? &c : nullptr;
        return nullptr;
    }

    std::uint64_t program::component_size(ada_type* aArray)
    {
        if (auto const& clause = aArray->view()->base_type()->view()->component_size_clause)
            return *clause / 8u;
        auto* c = aArray->view()->component;
        if (c == nullptr)
            return 8u;
        auto* cv = c->view();
        if (cv->is_scalar() || cv->is_access())
            return scalar_size(cv);
        return object_size(cv);
    }

    void program::layout(ada_type* aType)
    {
        // (a record's components' offsets (a tagged record's tag first; variants' components are not overlaid))
        auto* t = aType;
        if (t->size)
            return;
        std::uint64_t offset = 0u;
        if (t->tagged || t->kind == type_kind::Task || t->kind == type_kind::Protected)
            offset = 8u;
        if (t->kind == type_kind::Task || t->kind == type_kind::Protected)
            offset = 16u;     // (the runtime's handle (a task's id, a protected object's))
        // (a type extension: its parent part as the parent's objects are (its size, padding included: a view conversion's copy
        // of the parent's part doesn't overwrite the extension's components))
        std::size_t parentComponents = 0u;
        std::uint64_t parentSize = 0u;
        ada_type* parentBase = nullptr;
        ada_type* mappedParent = nullptr;
        if (t->tagged && t->parent != nullptr && t->kind == type_kind::Record)
        {
            auto* pb = t->parent->view()->base_type()->view();
            if (pb != t && pb->kind == type_kind::Record)
            {
                layout(pb);
                bool prefix = pb->size && !pb->dynamic_size && pb->components.size() <= t->components.size();
                for (std::size_t i = 0u; prefix && i < pb->components.size(); ++i)
                    prefix = pb->components[i].ent == t->components[i].ent || (pb->components[i].ent != nullptr && t->components[i].ent != nullptr &&
                        pb->components[i].ent->name == t->components[i].ent->name);
                if (prefix)
                {
                    parentComponents = pb->components.size();
                    parentSize = *pb->size;
                    parentBase = pb;
                }
                else if (pb->size)
                {
                    // (an extension with discriminants of its own (declared first) of a parent without: its parent's components where
                    // they are in the parent's objects (as the inherited operations expect), its own after them)
                    // (the parent's discriminants: each constrained by (mapped to) one of its own (that is where the parent's is in its
                    // objects (as the inherited operations, and class-wide views, expect)))
                    auto const& constraint = t->parent->view()->discriminant_constraint;
                    std::size_t k = 0u;
                    bool all = true;
                    for (auto const& pc : pb->components)
                    {
                        bool found = !pc.discriminant;
                        if (pc.discriminant)
                        {
                            node const* value = k < constraint.size() ? constraint[k] : nullptr;
                            ++k;
                            if (value != nullptr && value->is(construct::Association))
                                value = value->children.back();
                            // (a new discriminant (the first that the constraint names it with), else stored (see component_info::stored))
                            for (auto const& c : t->components)
                                found = found || (c.stored && c.ent == pc.ent);
                            if (value != nullptr && value->is(construct::Identifier) && value->ent != nullptr && value->ent->kind == entity_kind::Discriminant)
                                for (auto const& c : t->components)
                                    found = found || (c.discriminant && (c.ent == value->ent || (c.ent != nullptr && c.ent->name == value->ent->name)));
                        }
                        else
                        {
                            found = false;
                            for (auto const& c : t->components)
                                found = found || c.ent == pc.ent || (c.ent != nullptr && pc.ent != nullptr && c.ent->name == pc.ent->name && !c.discriminant);
                        }
                        all = all && found;
                    }
                    if (all)
                    {
                        mappedParent = pb;
                        parentSize = *pb->size;
                    }
                }
            }
        }
        // (the scalar (or access) components that component clauses place at a storage unit, in as many storage units as their
        // objects' size: there (13.5.1); the others after them)
        auto const placed = [&](component_info const& aComponent) -> std::optional<std::uint64_t>
        {
            if (!aComponent.bit_position || !aComponent.bit_size || *aComponent.bit_position % 8u != 0u || aComponent.type == nullptr)
                return {};
            auto* cv = aComponent.type->view();
            if (!cv->is_scalar() && !cv->is_access())
                return {};
            auto const size = scalar_size(cv);
            if ((*aComponent.bit_size + 7u) / 8u < size)
                return {};
            return *aComponent.bit_position / 8u;
        };
        // (its bit fields: not sharing storage units with the components that the clauses place in whole storage units (e.g. a
        // discriminant), which are loaded and stored whole)
        for (auto& c : t->components)
            c.bits = bit_field_candidate(*this, c);
        for (auto& c : t->components)
            if (c.bits)
            {
                auto const first = *c.bit_position / 8u;
                auto const last = first + (*c.bit_position % 8u + *c.bit_size + 7u) / 8u;
                for (auto const& d : t->components)
                    if (&d != &c && !d.bits && d.bit_position && d.bit_size && d.type != nullptr)
                    {
                        auto const dFirst = *d.bit_position / 8u;
                        auto const dSize = d.type->view()->is_scalar() || d.type->view()->is_access() ? scalar_size(d.type->view()) : (*d.bit_size + 7u) / 8u;
                        auto const dLast = dFirst + std::max<std::uint64_t>(dSize, (*d.bit_position % 8u + *d.bit_size + 7u) / 8u);
                        if (dFirst < last && first < dLast)
                            c.bits = false;
                    }
            }
        for (auto const& c : t->components)
            if (auto const at = placed(c))
                offset = std::max(offset, *at + scalar_size(c.type->view()));
            else if (bit_field(c))
                offset = std::max(offset, *c.bit_position / 8u + bit_field_span(c));
        std::size_t index = 0u;
        if (mappedParent != nullptr)
            offset = std::max(offset, parentSize);
        for (auto& c : t->components)
        {
            if (mappedParent != nullptr && c.discriminant)
            {
                // (its discriminant that constrains the parent's: where the parent's is)
                auto const& constraint = t->parent->view()->discriminant_constraint;
                std::size_t k = 0u;
                std::optional<std::uint64_t> at;
                for (auto const& pc : mappedParent->components)
                    if (pc.discriminant)
                    {
                        node const* value = k < constraint.size() ? constraint[k] : nullptr;
                        ++k;
                        if (value != nullptr && value->is(construct::Association))
                            value = value->children.back();
                        if (!at && value != nullptr && value->is(construct::Identifier) && value->ent != nullptr && c.ent != nullptr &&
                            (value->ent == c.ent || value->ent->name == c.ent->name))
                            at = pc.offset;
                    }
                if (at)
                {
                    c.offset = at;
                    continue;
                }
            }
            if (mappedParent != nullptr && !c.discriminant)
            {
                component_info const* parental = nullptr;
                for (auto const& pc : mappedParent->components)
                    if (pc.ent == c.ent || (pc.ent != nullptr && c.ent != nullptr && pc.ent->name == c.ent->name))
                        parental = &pc;
                if (parental != nullptr && parental->offset)
                {
                    c.offset = parental->offset;
                    c.depends = parental->depends;
                    c.dynamic = parental->dynamic;
                    if (c.dynamic)
                        t->dynamic_size = true;
                    continue;
                }
            }
            if (bit_field(c) && !(index < parentComponents))
            {
                // (a bit field: its first storage unit)
                c.offset = *c.bit_position / 8u;
                if (parentComponents != 0u && index++ == parentComponents)
                    offset = std::max(offset, parentSize);
                continue;
            }
            if (auto const at = placed(c); at && !(index < parentComponents))
            {
                c.offset = *at;
                if (parentComponents != 0u && index++ == parentComponents)
                    offset = std::max(offset, parentSize);
                continue;
            }
            // (the parent's components: where they are in the parent's objects)
            if (index < parentComponents && parentBase->components[index].offset)
            {
                auto const& pc = parentBase->components[index++];
                c.offset = pc.offset;
                c.depends = pc.depends;
                c.dynamic = pc.dynamic;
                offset = std::max(offset, index == parentComponents ? parentSize : *pc.offset + 1u);
                continue;
            }
            if (parentComponents != 0u && index++ == parentComponents)
                offset = std::max(offset, parentSize);
            auto* ct = c.type;
            std::uint64_t size = 8u;
            std::uint64_t alignment = 8u;
            if (ct != nullptr)
            {
                auto* cv = ct->view();
                if (cv->is_scalar() || cv->is_access())
                {
                    size = scalar_size(cv);
                    alignment = size;
                }
                else
                {
                    size = object_size(cv);
                    alignment = 8u;
                    if (size == 0u)
                    {
                        // (a component whose size depends on a discriminant: its maximum, if its index subtype is static; else its
                        // data is in the object's tail)
                        if (cv->kind == type_kind::Record && cv->base_type()->view() != t)
                        {
                            // (a dynamically sized record subtype: its object in the tail (where: in place, its offset in the
                            // record))
                            layout(cv->base_type()->view());
                            if (cv->base_type()->view()->dynamic_size)
                            {
                                c.depends = true;
                                c.dynamic = true;
                                t->dynamic_size = true;
                                offset = (offset + 7u) & ~std::uint64_t{ 7u };
                                c.offset = offset;
                                offset += 8u;
                                continue;
                            }
                        }
                        if (cv->kind != type_kind::Array)
                        {
                            t->size = 0u;
                            return;
                        }
                        c.depends = true;
                        size = maximum_size(cv);
                        if (size == 0u)
                        {
                            c.dynamic = true;
                            t->dynamic_size = true;
                            size = 8u + 16u * cv->base_type()->view()->indexes.size();
                        }
                    }
                }
            }
            offset = (offset + alignment - 1u) & ~(alignment - 1u);
            c.offset = offset;
            offset += size;
        }
        t->size = std::max<std::uint64_t>((offset + 7u) & ~std::uint64_t{ 7u }, 8u);
    }

    std::uint64_t program::maximum_size(ada_type* aType)
    {
        // (an array subtype whose bounds are discriminants: of its index subtypes' ranges (if small enough))
        auto* t = aType->view();
        if (t->kind != type_kind::Array)
            return 0u;
        std::uint64_t length = 1u;
        auto* base = t->base_type()->view();
        // (a bound: static, or a discriminant (its subtype's range), else its index subtype's range)
        auto const range_of_bound = [&](ada_type* aIndex, bool aLow, ada_type* aBaseIndex) -> std::optional<std::pair<bigint, bigint>>
        {
            auto* v = aIndex->view();
            auto const& value = aLow ? v->low : v->high;
            if (value.is_integer())
                return std::make_pair(value.integer(), value.integer());
            auto* object = aLow ? v->low_object : v->high_object;
            // (a static bound of a range whose other bound is not: its hidden constant's value)
            if (object != nullptr && object->initial != nullptr)
                if (auto const known = evaluate(object->initial, nullptr); known.is_integer())
                    return std::make_pair(known.integer(), known.integer());
            if (object != nullptr && object->initial != nullptr && object->initial->ent != nullptr && object->initial->ent->kind == entity_kind::Discriminant)
            {
                // (its subtype's range, within the index subtype's (a value outside it: Constraint_Error when an object is created))
                auto* dt = object->initial->ent->type;
                auto l = low_bound(dt);
                auto h = high_bound(dt);
                auto* bv = aBaseIndex->view();
                if (l && bv->low.is_integer() && bv->low.integer() > *l)
                    l = bv->low.integer();
                if (h && bv->high.is_integer() && bv->high.integer() < *h)
                    h = bv->high.integer();
                if (l && h)
                    return std::make_pair(*l, *h);
            }
            auto* bv = aBaseIndex->view();
            if (bv->low.is_integer() && bv->high.is_integer())
                return std::make_pair(bv->low.integer(), bv->high.integer());
            return {};
        };
        for (std::size_t i = 0u; i < base->indexes.size(); ++i)
        {
            auto* index = i < t->indexes.size() ? t->indexes[i] : base->indexes[i];
            auto const low = range_of_bound(index, true, base->indexes[i]);
            auto const high = range_of_bound(index, false, base->indexes[i]);
            if (!low || !high)
                return 0u;
            auto n = high->second - low->first + bigint{ 1 };
            if (n.negative())
                n = bigint{ 0 };
            if (!n.fits_int64() || n.to_int64() > 65536)
                return 0u;
            length *= n.to_uint64();
        }
        auto const element = component_size(t);
        if (element == 0u || length * element > 65536u)
            return 0u;      // (dynamically sized components, or too big: in the tail)
        return DescriptorHeader(base->indexes.size()) + ((length * element + 7u) & ~std::uint64_t{ 7u });
    }

    // --- constants and bounds

    void function_generator::constant_value(static_value const& aValue, ada_type* aType)
    {
        auto const k = kind_of(aType);
        auto* t = aType != nullptr ? aType->view()->base_type()->view() : nullptr;
        if (k == vkind::F32 || k == vkind::F64)
        {
            double const d = aValue.is_integer() ? aValue.integer().to_double() : aValue.is_real() ? aValue.real().to_double() : 0.0;
            if (k == vkind::F32)
                f32(static_cast<float>(d));
            else
                f64(d);
            return;
        }
        if (t != nullptr && t->is_fixed() && aValue.known())
        {
            // (a fixed point value: its count of smalls)
            auto const count = (aValue.real() / t->small).round();
            // (a bound beyond the representation (e.g. 2.0 ** (Max_Mantissa + 1) with a delta of 2.0): its nearest representable
            // value (the bounds need not be model numbers, 3.5.9(13)))
            if (!count.fits_int64())
                i64(count.negative() ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max());
            else
                i64(count.to_int64());
            return;
        }
        if (aValue.is_integer())
        {
            // (a value of a modular type of 64 bits beyond Integer_64'Last: its bits)
            auto const& v = aValue.integer();
            i64(!v.fits_int64() && v.fits_uint64() ? static_cast<std::int64_t>(v.to_uint64()) : v.to_int64());
            return;
        }
        if (aValue.is_real())
        {
            i64(aValue.real().round().to_int64());
            return;
        }
        if (aValue.is_string())
        {
            std::string bytes;
            for (auto c : aValue.string())
                bytes += static_cast<char>(c);
            i64(static_cast<std::int64_t>(iProgram.static_string(bytes)));
            return;
        }
        i64(0);
    }

    void function_generator::scalar_low(ada_type* aType)
    {
        auto* t = aType->view();
        if (t->low.known())
            constant_value(t->low, t);
        else if (t->low_object != nullptr && t->low_object->initial != nullptr && iProgram.refers_to_discriminant(t->low_object->initial))
            expression(t->low_object->initial, t->low_object->type);    // (a discriminant's: the record's being built (see index_bound))
        else if (t->low_object != nullptr)
            load_object(t->low_object);
        else if (t->range_node != nullptr && t->range_node->is(construct::Attribute))
        {
            auto const l = temporary(type::I64);
            auto const h = temporary(type::I64);
            range_attribute_bounds(t->range_node, l, h);
            get(l);
            release(l);
            release(h);
        }
        else if (t->base != nullptr && t->base->view() != t)
            scalar_low(t->base);
        else
            i64(std::numeric_limits<std::int64_t>::min());
    }

    void function_generator::scalar_high(ada_type* aType)
    {
        auto* t = aType->view();
        if (t->high.known())
            constant_value(t->high, t);
        else if (t->high_object != nullptr && t->high_object->initial != nullptr && iProgram.refers_to_discriminant(t->high_object->initial))
            expression(t->high_object->initial, t->high_object->type);
        else if (t->high_object != nullptr)
            load_object(t->high_object);
        else if (t->range_node != nullptr && t->range_node->is(construct::Attribute))
        {
            auto const l = temporary(type::I64);
            auto const h = temporary(type::I64);
            range_attribute_bounds(t->range_node, l, h);
            get(h);
            release(l);
            release(h);
        }
        else if (t->base != nullptr && t->base->view() != t)
            scalar_high(t->base);
        else
            i64(std::numeric_limits<std::int64_t>::max());
    }

    void function_generator::designated_constraint_test(ada_type* aAccess, std::uint32_t aValue)
    {
        // (an i32: whether an access value (in a local) of a constrained access subtype's type is null or designates an object of
        // its designated subtype (its discriminants, or its bounds); a constraint that depends on a discriminant of an enclosing
        // record (a component's subtype): not checked here)
        auto* d = aAccess->view()->designated->view();
        auto const result = local(type::I32);
        i32(1);
        set(result);
        bool dependent = false;
        for (auto* value : d->discriminant_constraint)
            dependent = dependent || iProgram.refers_to_discriminant(value);
        if (d->kind == type_kind::Array)
            for (auto* index : d->indexes)
                for (auto* object : { index->view()->low_object, index->view()->high_object })
                    dependent = dependent || (object != nullptr && iProgram.refers_to_discriminant(object->initial));
        if (dependent)
        {
            get(result);
            return;
        }
        get(aValue);
        i64(0);
        op(opcode::I64Ne);
        begin_if();
        if (d->kind == type_kind::Record)
        {
            auto* base = d->base_type()->view();
            iProgram.layout(base);
            std::size_t n = 0u;
            for (auto const& c : base->components)
            {
                if (!c.discriminant)
                    continue;
                auto* value = n < d->discriminant_constraint.size() ? d->discriminant_constraint[n] : nullptr;
                ++n;
                if (value == nullptr || !c.offset || c.type == nullptr || !c.type->view()->is_scalar())
                    continue;
                auto const k = kind_of(c.type);
                get(result);
                get(aValue);
                load(k, iProgram.scalar_size(c.type), static_cast<std::uint32_t>(*c.offset), !is_unsigned(c.type));
                expression(value, c.type);
                op(k == vkind::I64 ? opcode::I64Eq : k == vkind::F32 ? opcode::F32Eq : opcode::F64Eq);
                op(opcode::I32And);
                set(result);
            }
        }
        else
            for (std::size_t i = 0u; i < d->indexes.size(); ++i)
            {
                get(result);
                bounds(aValue, i, true);
                scalar_low(d->indexes[i]);
                op(opcode::I64Eq);
                op(opcode::I32And);
                bounds(aValue, i, false);
                scalar_high(d->indexes[i]);
                op(opcode::I64Eq);
                op(opcode::I32And);
                set(result);
            }
        end();
        get(result);
    }

    void function_generator::range_check(ada_type* aTarget, ada_type* aSource, node const* aWhere)
    {
        // (the value on the stack: of the target's subtype (its constraint, then its predicate))
        constraint_check(aTarget, aSource, aWhere);
        predicate_check(aTarget, aSource, aWhere);
    }

    bool function_generator::static_predicate_bounds(ada_type* aType, std::int64_t& aLow, std::int64_t& aHigh)
    {
        // (a subtype with static predicates: the least and greatest values that satisfy them (within the bounds), if they can be
        // determined (3.2.4(15/3)))
        using interval_set = std::vector<std::pair<std::int64_t, std::int64_t>>;
        auto const normalize = [](interval_set aSet)
        {
            std::sort(aSet.begin(), aSet.end());
            interval_set result;
            for (auto const& i : aSet)
            {
                if (i.first > i.second)
                    continue;
                if (!result.empty() && i.first <= result.back().second + 1)
                    result.back().second = std::max(result.back().second, i.second);
                else
                    result.push_back(i);
            }
            return result;
        };
        auto const intersect = [&](interval_set const& a, interval_set const& b)
        {
            interval_set result;
            for (auto const& x : a)
                for (auto const& y : b)
                    result.emplace_back(std::max(x.first, y.first), std::min(x.second, y.second));
            return normalize(result);
        };
        auto const unite = [&](interval_set a, interval_set const& b)
        {
            a.insert(a.end(), b.begin(), b.end());
            return normalize(a);
        };
        auto const complement = [&](interval_set const& a, std::pair<std::int64_t, std::int64_t> aUniverse)
        {
            interval_set result;
            auto next = aUniverse.first;
            for (auto const& x : a)
            {
                if (x.first > next)
                    result.emplace_back(next, x.first - 1);
                next = std::max(next, x.second == std::numeric_limits<std::int64_t>::max() ? x.second : x.second + 1);
            }
            if (next <= aUniverse.second)
                result.emplace_back(next, aUniverse.second);
            return normalize(result);
        };
        auto const value_of = [&](node* aNode, std::int64_t& aValue)
        {
            auto v = iProgram.evaluate(aNode, nullptr);
            if (!v.known() || !v.is_integer())
                return false;
            aValue = v.integer().to_int64();
            return true;
        };
        std::function<bool(ada_type*, interval_set&)> subtype_set;
        std::function<bool(node*, entity*, std::pair<std::int64_t, std::int64_t>, interval_set&)> set_of =
            [&](node* e, entity* aInstance, std::pair<std::int64_t, std::int64_t> u, interval_set& aResult) -> bool
        {
            auto const is_instance = [&](node* n) { while (n->is(construct::Parenthesized)) n = n->child(0); return n->is(construct::Identifier) && n->ent == aInstance; };
            switch (e->kind)
            {
            case construct::Parenthesized:
                return set_of(e->child(0), aInstance, u, aResult);
            case construct::And:
            case construct::AndThen:
            case construct::Or:
            case construct::OrElse:
                {
                    interval_set a, b;
                    if (!set_of(e->child(0), aInstance, u, a) || !set_of(e->child(1), aInstance, u, b))
                        return false;
                    aResult = e->is(construct::And) || e->is(construct::AndThen) ? intersect(a, b) : unite(a, b);
                    return true;
                }
            case construct::Not:
                {
                    interval_set a;
                    if (!set_of(e->child(0), aInstance, u, a))
                        return false;
                    aResult = complement(a, u);
                    return true;
                }
            case construct::In:
                {
                    if (!is_instance(e->child(0)))
                        return false;
                    interval_set a;
                    for (std::size_t i = 1u; i < e->size(); ++i)
                    {
                        auto* c = e->child(i);
                        if (c->is(construct::Flag))
                            continue;
                        std::int64_t x = 0, y = 0;
                        if (c->is(construct::Range))
                        {
                            if (!value_of(c->child(0), x) || !value_of(c->child(1), y))
                                return false;
                            a.emplace_back(x, y);
                        }
                        else if (c->type != nullptr && (meaning_of(c) == meaning::Type || c->is(construct::SubtypeIndication)))
                        {
                            interval_set s;
                            if (!subtype_set(c->type, s))
                                return false;
                            a.insert(a.end(), s.begin(), s.end());
                        }
                        else if (value_of(c, x))
                            a.emplace_back(x, x);
                        else
                            return false;
                    }
                    a = intersect(normalize(a), { u });
                    aResult = e->has_flag("not") ? complement(a, u) : a;
                    return true;
                }
            case construct::Eq:
            case construct::Ne:
            case construct::Lt:
            case construct::Le:
            case construct::Gt:
            case construct::Ge:
                {
                    std::int64_t v = 0;
                    bool swapped = false;
                    if (is_instance(e->child(0)) && value_of(e->child(1), v))
                        ;
                    else if (is_instance(e->child(1)) && value_of(e->child(0), v))
                        swapped = true;
                    else
                        return false;
                    auto k = e->kind;
                    if (swapped)
                        k = k == construct::Lt ? construct::Gt : k == construct::Gt ? construct::Lt : k == construct::Le ? construct::Ge : k == construct::Ge ? construct::Le : k;
                    interval_set a;
                    switch (k)
                    {
                    case construct::Eq: a = { { v, v } }; break;
                    case construct::Ne: a = complement({ { v, v } }, u); break;
                    case construct::Lt: a = { { u.first, v - 1 } }; break;
                    case construct::Le: a = { { u.first, v } }; break;
                    case construct::Gt: a = { { v + 1, u.second } }; break;
                    default: a = { { v, u.second } }; break;
                    }
                    aResult = intersect(normalize(a), { u });
                    return true;
                }
            default:
                {
                    // (a static Boolean (e.g. True))
                    auto v = iProgram.evaluate(e, nullptr);
                    if (!v.known() || !v.is_integer())
                        return false;
                    aResult = v.integer().is_zero() ? interval_set{} : interval_set{ u };
                    return true;
                }
            }
        };
        subtype_set = [&](ada_type* aSubtype, interval_set& aResult) -> bool
        {
            auto* v = aSubtype->view();
            if (!v->low.is_integer() || !v->high.is_integer())
                return false;
            std::pair<std::int64_t, std::int64_t> const u{ v->low.integer().to_int64(), v->high.integer().to_int64() };
            aResult = { u };
            for (auto* p = aSubtype->predicate != nullptr ? aSubtype->predicate : v->predicate; p != nullptr; p = p->parent)
            {
                if (!p->is_static || p->function == nullptr || p->function->sub->parameters.empty())
                    return false;
                interval_set s;
                if (!set_of(p->expression, p->function->sub->parameters[0].ent, u, s))
                    return false;
                aResult = intersect(aResult, s);
            }
            return true;
        };
        interval_set result;
        if (aType == nullptr || !subtype_set(aType, result))
            return false;
        if (result.empty())
        {
            aLow = 1;
            aHigh = 0;
            return true;
        }
        aLow = result.front().first;
        aHigh = result.back().second;
        return true;
    }

    std::vector<entity*> function_generator::invariants_of(ada_type* aType)
    {
        // (its own (specified on its partial or full view), then its ancestors' class-wide ones)
        std::vector<entity*> result;
        bool own = true;
        for (auto* p = aType != nullptr ? aType->base_type() : nullptr; p != nullptr;)
        {
            for (auto* v : { p, p->view(), p->view()->base_type(), p->partial, p->view()->base_type()->partial })
                for (auto const& [f, classWide] : v != nullptr ? v->invariants : std::vector<std::pair<entity*, bool>>{})
                    if ((own || classWide) && std::find(result.begin(), result.end(), f) == result.end())
                        result.push_back(f);
            own = false;
            auto* parent = p->view()->parent != nullptr ? p->view()->parent : p->parent;
            p = parent != nullptr ? parent->base_type() : nullptr;
        }
        return result;
    }

    bool function_generator::outside_scope_of(ada_type* aType)
    {
        auto* t = aType->base_type();
        auto* scope = t->declared_in != nullptr ? t->declared_in : t->view()->declared_in;
        if (scope == nullptr)
            return true;
        for (auto* r = iSubprogram != nullptr ? iSubprogram->scope : nullptr; r != nullptr; r = r->parent)
            if (r == scope)
                return false;
        return true;
    }

    bool function_generator::boundary_of(entity* aSubprogram, ada_type* aType)
    {
        auto* t = aType->base_type();
        auto* scope = t->declared_in != nullptr ? t->declared_in : t->view()->declared_in;
        // (declared visibly where the type is, or an operation inherited by it (implicitly declared with it))
        return aSubprogram != nullptr && scope != nullptr && ((aSubprogram->scope == scope && !aSubprogram->is_private) ||
            (aSubprogram->sub != nullptr && aSubprogram->sub->inherited && !aSubprogram->is_private));
    }

    bool function_generator::inherited_call(entity* aCallee, ada_type* aType)
    {
        // (a call of an ancestor's primitive operation with an object of a descendant type: of its inherited operation)
        auto* c = aCallee != nullptr && aCallee->sub != nullptr ? aCallee->sub->controlling : nullptr;
        return c != nullptr && aType != nullptr && c->view()->base_type()->view() != aType->view()->base_type()->view() &&
            c->view()->kind != type_kind::ClassWide && aType->view()->kind != type_kind::ClassWide;
    }

    void function_generator::invariant_check(ada_type* aType, std::uint32_t aValue, node const* aWhere)
    {
        for (auto* f : invariants_of(aType))
        {
            static_link_for(f);
            get(aValue);
            call_function(f);
            check();
            op(opcode::I64Eqz);
            begin_if();
            failed_assertion(aWhere, "failed invariant");
            end();
        }
    }

    void function_generator::invariant_part_types(ada_type* aType, std::vector<ada_type*>& aTypes, int aDepth)
    {
        if (aType == nullptr || aDepth > 8 || !aType->view()->is_composite())
            return;
        if (!invariants_of(aType).empty())
        {
            if (std::find(aTypes.begin(), aTypes.end(), aType->base_type()) == aTypes.end())
                aTypes.push_back(aType->base_type());
            return;
        }
        auto* t = aType->view();
        if (t->kind == type_kind::Array)
            invariant_part_types(t->component, aTypes, aDepth + 1);
        else if (t->kind == type_kind::Record)
            for (auto const& c : t->base_type()->view()->components)
                if (!c.discriminant && c.type != nullptr && c.type->view()->base_type()->view() != t->base_type()->view())
                    invariant_part_types(c.type, aTypes, aDepth + 1);
    }

    bool function_generator::invariant_boundary(entity* aSubprogram, ada_type* aType)
    {
        std::vector<ada_type*> types;
        invariant_part_types(aType, types);
        for (auto* t : types)
            if (boundary_of(aSubprogram, t) || inherited_call(aSubprogram, aType))
                return true;
        return false;
    }

    void function_generator::deep_invariant_check(ada_type* aType, std::uint32_t aAddress, node const* aWhere)
    {
        // (an object (its address): its parts of types with invariants (itself, or its components') checked (7.3.2(19/5): "has a part"))
        if (!invariants_of(aType).empty())
        {
            invariant_check(aType, aAddress, aWhere);
            return;
        }
        std::vector<ada_type*> types;
        invariant_part_types(aType, types);
        if (types.empty())
            return;
        auto* t = aType->view();
        if (t->kind == type_kind::Array)
        {
            auto* c = t->component;
            auto const n = local(type::I64);
            auto const p = local(type::I64);
            array_length(aAddress, t->base_type()->view()->indexes.size());
            set(n);
            data_address(aAddress);
            set(p);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            get(n);
            op(opcode::I64Eqz);
            branch(exit, true);
            deep_invariant_check(c, p, aWhere);
            get(p);
            element_size(t);
            op(opcode::I64Add);
            set(p);
            get(n);
            i64(1);
            op(opcode::I64Sub);
            set(n);
            branch(head);
            end();
            end();
        }
        else if (t->kind == type_kind::Record)
        {
            auto* base = t->base_type()->view();
            iProgram.layout(base);
            for (auto const& c : base->components)
            {
                std::vector<ada_type*> partTypes;
                if (!c.discriminant && c.type != nullptr && c.type->view()->base_type()->view() != base)
                    invariant_part_types(c.type, partTypes);
                if (!partTypes.empty() && c.offset && c.variant < 0 && !c.dynamic)
                {
                    auto const part = local(type::I64);
                    get(aAddress);
                    i64(static_cast<std::int64_t>(*c.offset));
                    op(opcode::I64Add);
                    set(part);
                    deep_invariant_check(c.type, part, aWhere);
                }
            }
        }
    }

    void function_generator::parts_invariant_check(ada_type* aType, std::uint32_t aAddress, node const* aWhere)
    {
        // (a default-initialized object's parts of types with invariants (unless their partial views have unknown discriminants):
        // checked (7.3.2(10/4)))
        auto const checked = [&](ada_type* aPart)
        {
            return aPart != nullptr && aPart->view()->is_composite() && !aPart->unknown_discriminants && !aPart->base_type()->unknown_discriminants &&
                !invariants_of(aPart).empty();
        };
        auto* t = aType->view();
        if (t->kind == type_kind::Array && checked(t->component))
        {
            auto* c = t->component;
            auto const n = local(type::I64);
            auto const p = local(type::I64);
            array_length(aAddress, t->base_type()->view()->indexes.size());
            set(n);
            data_address(aAddress);
            set(p);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            get(n);
            op(opcode::I64Eqz);
            branch(exit, true);
            invariant_check(c, p, aWhere);
            get(p);
            element_size(t);
            op(opcode::I64Add);
            set(p);
            get(n);
            i64(1);
            op(opcode::I64Sub);
            set(n);
            branch(head);
            end();
            end();
        }
        else if (t->kind == type_kind::Record)
        {
            auto* base = t->base_type()->view();
            iProgram.layout(base);
            for (auto const& c : base->components)
                if (checked(c.type) && c.offset && c.variant < 0 && !c.dynamic)
                {
                    auto const part = local(type::I64);
                    get(aAddress);
                    i64(static_cast<std::int64_t>(*c.offset));
                    op(opcode::I64Add);
                    set(part);
                    invariant_check(c.type, part, aWhere);
                }
        }
    }

    void function_generator::call_invariant_checks(entity* aCallee, call_info const& aInfo, std::vector<parameter_info> const& aFormals)
    {
        // (after a call: its out and in out actuals of a type with invariants (if the subprogram is visible outside its scope), and
        // its view conversions (to an ancestor) of such objects (outside its scope), 7.3.2(19/5, 12/3))
        for (std::size_t i = 0u; i < aFormals.size() && i < aInfo.actuals.size(); ++i)
        {
            auto const& f = aFormals[i];
            auto* a = aInfo.actuals[i];
            if (a == nullptr || f.type == nullptr || f.mode == parameter_mode::In || !f.type->view()->is_composite())
                continue;
            auto* actual = a;
            while (actual->is(construct::Parenthesized))
                actual = actual->child(0);
            if (actual->is(construct::Apply) && meaning_of(actual) == meaning::Conversion && actual->size() > 1u && actual->child(1)->type != nullptr &&
                !invariants_of(actual->child(1)->type).empty() && outside_scope_of(actual->child(1)->type))
            {
                address(actual->child(1));
                auto const value = local(type::I64);
                set(value);
                invariant_check(actual->child(1)->type, value, actual);
            }
            else if (auto* t = actual->type != nullptr && !invariants_of(actual->type).empty() ? actual->type : f.type; invariant_boundary(aCallee, t))
            {
                address(actual);
                auto const value = local(type::I64);
                set(value);
                deep_invariant_check(t, value, actual);
            }
        }
    }

    void function_generator::copy_back_check(ada_type* aTarget, ada_type* aSource, node const* aActual)
    {
        // (an out or in out parameter's value copied back: of the actual's subtype (its predicate checked even if the actual's
        // subtype is the formal's: the value is the formal's))
        constraint_check(aTarget, aSource, aActual);
        predicate_check(aTarget, aSource, aActual, false);
    }

    void function_generator::predicate_check(ada_type* aTarget, ada_type* aSource, node const* aWhere, bool aWhereValue)
    {
        // (the value on the stack (a composite one's address): satisfies the target subtype's predicate (3.2.4(31/5)), unless the
        // source subtype's is the same)
        if (aTarget == nullptr)
            return;
        auto* predicate = aTarget->predicate != nullptr ? aTarget->predicate : aTarget->view()->predicate;
        if (predicate == nullptr)
            return;
        // (an object of a subtype with the same predicate: satisfies it (an expression's subtype can be the expected one: checked))
        if (aWhereValue && aWhere != nullptr && (aWhere->is(construct::Identifier) || aWhere->is(construct::Selected)) && aWhere->ent != nullptr &&
            aWhere->ent->is_object() && aWhere->ent->type != nullptr && (aWhere->ent->type->predicate == predicate || aWhere->ent->type->view()->predicate == predicate))
            return;
        (void)aSource;
        bool enabled = false;
        for (auto* p = predicate; p != nullptr; p = p->parent)
            enabled = enabled || p->checked;
        if (!enabled)
            return;
        auto const value = temporary(wasm_of(aTarget));
        tee(value);
        // (the predicates of the subtypes it is of, then its own: the first that fails raises Assertion_Error (or as its
        // Predicate_Failure specifies))
        std::vector<predicate_info*> chain;
        for (auto* p = predicate; p != nullptr; p = p->parent)
            chain.insert(chain.begin(), p);
        for (auto* p : chain)
        {
            if (p->function == nullptr || !p->checked)
                continue;
            static_link_for(p->function);
            get(value);
            call_function(p->function);
            check();
            op(opcode::I64Eqz);
            begin_if();
            if (p->failure_function != nullptr)
            {
                // (its message: Assertion_Error raised with it (unless it raises an exception itself))
                auto* assertion = iProgram.assertion_error();
                static_link_for(p->failure_function);
                get(value);
                call_function(p->failure_function);
                check();
                auto const message = local(type::I64);
                set(message);
                if (assertion != nullptr)
                {
                    i64(static_cast<std::int64_t>(assertion->exception_id));
                    get(message);
                    runtime("raise", { type::I64, type::I64 }, type::Void);
                    branch(handler_target());
                }
                else
                    raise_check(1, aWhere, "predicate check failed");
            }
            else
                failed_assertion(aWhere, "predicate check failed");
            end();
        }
        release(value);
    }

    void function_generator::predicate_test(predicate_info* aPredicate, std::uint32_t aValue, bool aChecks)
    {
        // (an i32: whether the value satisfies the predicate (its own, then those of the subtypes it is of: the first that fails
        // decides))
        auto const holds = local(type::I32);
        i32(1);
        set(holds);
        std::vector<predicate_info*> chain;
        for (auto* p = aPredicate; p != nullptr; p = p->parent)
            chain.insert(chain.begin(), p);
        for (auto* p : chain)
        {
            if (p->function == nullptr || (aChecks && !p->checked))
                continue;
            get(holds);
            begin_if();
            static_link_for(p->function);
            get(aValue);
            call_function(p->function);
            check();
            op(opcode::I64Eqz);
            op(opcode::I32Eqz);
            set(holds);
            end();
        }
        get(holds);
    }

    void function_generator::constraint_check(ada_type* aTarget, ada_type* aSource, node const* aWhere)
    {
        // (the scalar value on the stack: in the target's subtype)
        if (aTarget == nullptr)
            return;
        auto* t = aTarget->view();
        // (a conversion's value: its operand's subtype's; null: of no null-excluding subtype)
        if (aWhere != nullptr && aWhere->is(construct::Apply) && meaning_of(aWhere) == meaning::Conversion && aWhere->size() > 1u)
            aSource = aWhere->child(1)->type;
        if (aWhere != nullptr && aWhere->is(construct::Null))
            aSource = nullptr;
        if ((t->kind == type_kind::Access || t->kind == type_kind::AccessSubprogram) && t->not_null && !(aSource != nullptr && aSource->view()->not_null))
        {
            // (a null-excluding subtype: not null (4.6(49/2)))
            auto const value = temporary(type::I64);
            tee(value);
            get(value);
            op(opcode::I64Eqz);
            begin_if();
            raise_check(0, aWhere, "access check failed");
            end();
            release(value);
        }
        if (t->kind == type_kind::Access && t->designated != nullptr &&
            ((t->is_subtype && t->base_type()->view()->designated != t->designated &&
              (aSource == nullptr || aSource->view()->designated != t->designated || (aWhere != nullptr && aWhere->is(construct::Allocator)))) ||
             (aWhere != nullptr && aWhere->is(construct::Allocator))))
        {
            // (an allocator: its object of the designated subtype (4.8(10/2)); an initialized allocator's array value slides
            // (4.8(7/2)))
            // (an access value of a constrained access subtype: null, or designating an object of the constraint (4.6(51/4)))
            auto* d = t->designated->view();
            auto const qualified = [](node const* aAllocator)
            {
                if (aAllocator->first(construct::Qualified) != nullptr)
                    return true;
                auto* si = aAllocator->first(construct::SubtypeIndication);
                return si != nullptr && si->first(construct::Qualified) != nullptr;
            };
            // (the designated subtype of the access type (not a constrained access subtype's constraint: the bounds then are checked,
            // 4.6(51/4)))
            if (d->kind == type_kind::Array && d->constrained && aWhere != nullptr && aWhere->is(construct::Allocator) && qualified(aWhere) &&
                !(t->is_subtype && t->base_type()->view()->designated != t->designated))
            {
                // (an initialized allocator's array: of the designated subtype's lengths (it slides))
                auto const value = temporary(type::I64);
                tee(value);
                // (the qualifying subtype's bounds: checked by the qualified expression (then the object slides: see allocator))
                for (std::size_t k = 0u; k < d->indexes.size(); ++k)
                {
                    auto const length = [&](std::function<void()> const& aHigh, std::function<void()> const& aLow)
                    {
                        auto const n = temporary(type::I64);
                        aHigh();
                        aLow();
                        op(opcode::I64Sub);
                        i64(1);
                        op(opcode::I64Add);
                        tee(n);
                        i64(0);
                        get(n);
                        i64(0);
                        op(opcode::I64GtS);
                        op(opcode::Select);
                        release(n);
                    };
                    length([&]() { scalar_high(d->indexes[k]); }, [&]() { scalar_low(d->indexes[k]); });
                    length([&]() { bounds(value, k, false); }, [&]() { bounds(value, k, true); });
                    op(opcode::I64Ne);
                    begin_if();
                    raise_check(0, aWhere, "length check failed");
                    end();
                }
                release(value);
            }
            else if ((d->kind == type_kind::Record && !d->discriminant_constraint.empty()) || (d->kind == type_kind::Array && d->constrained))
            {
                auto const value = temporary(type::I64);
                tee(value);
                designated_constraint_test(t, value);
                op(opcode::I32Eqz);
                begin_if();
                raise_check(0, aWhere, d->kind == type_kind::Record ? "discriminant check failed" : "index check failed");
                end();
                release(value);
            }
            return;
        }
        if (!t->is_scalar() || t->kind == type_kind::UniversalInteger || t->kind == type_kind::UniversalReal)
            return;
        if (t->kind == type_kind::Float && !t->is_subtype && t == t->base_type())
            return;
        // (a static value: checked now (its subtype can be the expected one, e.g. a literal's))
        if (aWhere != nullptr && aSource != nullptr)
            if (auto v = iProgram.evaluate(const_cast<node*>(aWhere), nullptr); v.known() && v.is_integer() && t->low.is_integer() && t->high.is_integer())
            {
                if (v.integer() >= t->low.integer() && v.integer() <= t->high.integer())
                    return;
                aSource = nullptr;
            }
        // (the source's range within the target's: no check)
        if (aSource != nullptr)
        {
            auto* s = aSource->view();
            if (s->low.known() && s->high.known() && t->low.known() && t->high.known() && s->low.is_integer() && t->low.is_integer() &&
                s->low.integer() >= t->low.integer() && s->high.integer() <= t->high.integer())
                return;
        }
        if (t->low.is_integer() && t->high.is_integer() && t == t->base_type() && t->kind != type_kind::Modular && t->kind != type_kind::Enumeration &&
            t->size && *t->size == 8u)
            return;
        // (a modular type whose values are all the 64-bit patterns)
        if (t->kind == type_kind::Modular && t == t->base_type() && t->modulus.bits() > 64u)
            return;
        auto const k = kind_of(t);
        auto const value = temporary(wasm(k));
        tee(value);
        if (k == vkind::I64)
        {
            if (t->kind == type_kind::Modular || (t->kind == type_kind::Enumeration && t->low.is_integer() && t->low.integer().is_zero() && false))
            {
                scalar_low(t);
                op(opcode::I64LtU);
                get(value);
                scalar_high(t);
                op(opcode::I64GtU);
            }
            else
            {
                scalar_low(t);
                op(opcode::I64LtS);
                get(value);
                scalar_high(t);
                op(opcode::I64GtS);
            }
        }
        else
        {
            scalar_low(t);
            op(k == vkind::F32 ? opcode::F32Lt : opcode::F64Lt);
            get(value);
            scalar_high(t);
            op(k == vkind::F32 ? opcode::F32Gt : opcode::F64Gt);
        }
        op(opcode::I32Ior);
        begin_if();
        raise_check(0, aWhere, "range check failed");
        end();
        get(value);
        release(value);
    }

    void function_generator::overflow_check(ada_type* aType, node const* aWhere)
    {
        // (an integer result (i64 on the stack): within its base type's range (a type narrower than 64 bits))
        auto* t = aType != nullptr ? aType->view()->base_type()->view() : nullptr;
        if (t == nullptr || t->kind != type_kind::Integer || !t->size || *t->size >= 8u)
            return;
        auto const value = temporary(type::I64);
        tee(value);
        auto const bits = *t->size * 8u;
        i64(-(std::int64_t{ 1 } << (bits - 1u)));
        op(opcode::I64LtS);
        get(value);
        i64((std::int64_t{ 1 } << (bits - 1u)) - 1);
        op(opcode::I64GtS);
        op(opcode::I32Ior);
        begin_if();
        raise_check(0, aWhere, "overflow check failed");
        end();
        get(value);
        release(value);
    }

    // --- expressions

    void function_generator::condition(node* aExpression)
    {
        // (an i32: short circuits and relations directly)
        if (aExpression->is(construct::Parenthesized))
        {
            condition(aExpression->child(0));
            return;
        }
        if (aExpression->is(construct::AndThen) || aExpression->is(construct::OrElse))
        {
            bool const isAnd = aExpression->is(construct::AndThen);
            condition(aExpression->child(0));
            begin_if(type::I32);
            if (isAnd)
            {
                condition(aExpression->child(1));
                else_();
                i32(0);
            }
            else
            {
                i32(1);
                else_();
                condition(aExpression->child(1));
            }
            end();
            return;
        }
        if (aExpression->is(construct::In))
        {
            membership(aExpression);
            return;
        }
        if (aExpression->is(construct::Quantified))
        {
            quantified(aExpression);
            return;
        }
        expression(aExpression, aExpression->type);
        op(opcode::I32ConvertI64);
    }

    bool function_generator::implicitly_dereferenced(node* aExpression, bool aValue)
    {
        // (an expression of a reference type implicitly dereferenced: its access discriminant's object (the address, or a scalar's
        // value))
        auto* reference = iProgram.implicit_dereference_of(aExpression);
        if (reference == nullptr || iDereferencing.contains(aExpression))
            return false;
        auto* designated = aExpression->type;
        iDereferencing.insert(aExpression);
        aExpression->type = reference;
        expression(aExpression, reference);
        aExpression->type = designated;
        iDereferencing.erase(aExpression);
        auto const offset = iProgram.reference_offset(reference);
        load64(static_cast<std::uint32_t>(offset.value_or(0u)));
        null_check(aExpression);
        if (aValue && designated != nullptr && !designated->view()->is_composite())
            load(kind_of(designated), iProgram.scalar_size(designated), 0u, !is_unsigned(designated));
        return true;
    }

    void function_generator::expression(node* aExpression, ada_type* aType)
    {
        if (implicitly_dereferenced(aExpression, true))
            return;
        // (a universal_fixed value (a product or quotient of fixed point values) where a fixed point type is expected: converted
        // (4.5.5(19.1)))
        if (aType != nullptr && aExpression->type != nullptr && aExpression->type->view()->kind == type_kind::UniversalFixed &&
            aType->view()->is_fixed() && aType->view()->kind != type_kind::UniversalFixed)
        {
            conversion(aExpression, aExpression->type, aType, aExpression);
            return;
        }
        auto* t = aType != nullptr ? aType : aExpression->type;
        // (the negation of a floating point zero: a negative zero (T'Signed_Zeros); a static value has no sign of zero)
        if (t != nullptr && aExpression->is(construct::Negate) && t->view()->base_type()->view()->kind == type_kind::Float)
            if (auto v = iProgram.evaluate(aExpression, nullptr); v.known() && !v.is_string() && v.real().sign() == 0)
            {
                if (kind_of(t) == vkind::F32)
                    f32(-0.0f);
                else
                    f64(-0.0);
                return;
            }
        // (a static scalar value: a constant)
        if (t != nullptr && t->view()->is_scalar() && !aExpression->is(construct::Apply) && meaning_of(aExpression) != meaning::Call &&
            aExpression->kind != construct::Attribute)
        {
            auto v = iProgram.evaluate(aExpression, nullptr);
            if (v.known() && !v.is_string())
            {
                constant_value(v, t);
                return;
            }
        }
        else if (t != nullptr && t->view()->is_scalar())
        {
            auto v = iProgram.evaluate(aExpression, nullptr);
            if (v.known() && !v.is_string() && (aExpression->kind != construct::Apply || meaning_of(aExpression) == meaning::Conversion || meaning_of(aExpression) == meaning::Call))
            {
                if (meaning_of(aExpression) != meaning::Call || (iProgram.call_of(aExpression) != nullptr && iProgram.call_of(aExpression)->callee->sub != nullptr &&
                    iProgram.call_of(aExpression)->callee->sub->op != intrinsic::None && iProgram.call_of(aExpression)->callee->sub->op != intrinsic::Import))
                {
                    constant_value(v, t);
                    return;
                }
            }
        }
        auto const* previous = iWhere;
        iWhere = aExpression;
        switch (aExpression->kind)
        {
        case construct::Number:
        case construct::Character:
            constant_value(iProgram.evaluate(aExpression, nullptr), t);
            break;
        case construct::Null:
            i64(0);
            break;
        case construct::Parenthesized:
            expression(aExpression->child(0), t);
            break;
        case construct::Qualified:
            // (a named array aggregate, its subaggregates too, without others)
            if (auto* qt = aExpression->type != nullptr ? aExpression->type->view() : nullptr; is_aggregate_node(aExpression->child(1)) &&
                qt != nullptr && qt->kind == type_kind::Array && qt->constrained && aExpression->child(1)->is(construct::Aggregate) &&
                [&]()
                {
                    std::function<bool(node*, std::size_t)> named = [&](node* aLevel, std::size_t aDimensions) -> bool
                    {
                        if (aLevel->children.empty())
                            return false;
                        for (auto* a : aLevel->children)
                        {
                            if (!a->is(construct::Association) || a->first(construct::Others) != nullptr ||
                                (a->first(construct::Choices) != nullptr && a->first(construct::Choices)->first(construct::Others) != nullptr))
                                return false;
                            if (aDimensions > 1u && !(a->children.back()->is(construct::Aggregate) && named(a->children.back(), aDimensions - 1u)))
                                return false;
                        }
                        return true;
                    };
                    return named(aExpression->child(1), qt->indexes.size());
                }())
            {
                // (a named array aggregate without others: its own bounds, which must be the subtype's (no sliding, 4.7(4)))
                array_aggregate(aExpression->child(1), qt->base_type()->view(), {});
                auto const value = local(type::I64);
                set(value);
                for (std::size_t k = 0u; k < qt->indexes.size(); ++k)
                {
                    bounds(value, k, true);
                    scalar_low(qt->indexes[k]);
                    op(opcode::I64Ne);
                    bounds(value, k, false);
                    scalar_high(qt->indexes[k]);
                    op(opcode::I64Ne);
                    op(opcode::I32Ior);
                    begin_if();
                    raise_check(0, aExpression, "index check failed");
                    end();
                }
                get(value);
            }
            else if (is_aggregate_node(aExpression->child(1)))
                aggregate(aExpression->child(1), aExpression->type);
            else
            {
                expression(aExpression->child(1), aExpression->type);
                auto* qt = aExpression->type != nullptr ? aExpression->type->view() : nullptr;
                // (the operand: of the subtype (4.7(4)))
                if (qt != nullptr && qt->is_scalar())
                    range_check(aExpression->type, aExpression->child(1)->type, aExpression);
                else if (qt != nullptr && qt->is_access())
                    range_check(aExpression->type, nullptr, aExpression);  // (an allocator's value is of the expected subtype)
                else if (qt != nullptr && qt->kind == type_kind::Array && qt->constrained && aExpression->child(1)->type != nullptr &&
                    aExpression->child(1)->type->view()->kind == type_kind::Array && !(aExpression->child(1)->type->view()->constrained &&
                    iProgram.same_type(aExpression->child(1)->type, qt) && aExpression->child(1)->type->view() == qt))
                {
                    // (an array: its bounds the subtype's)
                    auto const value = local(type::I64);
                    tee(value);
                    drop();
                    for (std::size_t k = 0u; k < qt->indexes.size(); ++k)
                    {
                        bounds(value, k, true);
                        scalar_low(qt->indexes[k]);
                        op(opcode::I64Ne);
                        bounds(value, k, false);
                        scalar_high(qt->indexes[k]);
                        op(opcode::I64Ne);
                        op(opcode::I32Ior);
                        begin_if();
                        raise_check(0, aExpression, "index check failed");
                        end();
                    }
                    get(value);
                }
                else if (qt != nullptr && qt->kind == type_kind::Record && !qt->discriminant_constraint.empty())
                {
                    auto const value = local(type::I64);
                    tee(value);
                    discriminant_constraint_check(qt, value, aExpression);
                }
            }
            // (a composite operand: of the subtype's predicate (a scalar's: see range_check))
            if (aExpression->type != nullptr && aExpression->type->view()->is_composite())
                predicate_check(aExpression->type, aExpression->child(1)->type, aExpression->child(1));
            break;
        case construct::Identifier:
        case construct::Selected:
        case construct::Operator:
            name_value(aExpression, t);
            break;
        case construct::Apply:
            apply_value(aExpression, t);
            break;
        case construct::Attribute:
            attribute(aExpression);
            break;
        case construct::AndThen:
        case construct::OrElse:
        case construct::In:
        case construct::Quantified:
            condition(aExpression);
            to_boolean();
            break;
        case construct::All:
            // (a call through an access-to-function value with no actuals, else a dereference)
            if (meaning_of(aExpression) == meaning::Call)
                call_expression(aExpression, false);
            else
                dereference(aExpression);
            break;
        case construct::Allocator:
            allocator(aExpression, t);
            break;
        case construct::Aggregate:
        case construct::AggregateBracket:
        case construct::AggregateExtension:
        case construct::AggregateDelta:
        case construct::AggregateNullRecord:
            aggregate(aExpression, t);
            break;
        case construct::IfExpression:
            if_expression(aExpression, t);
            break;
        case construct::CaseExpression:
            case_expression(aExpression, t);
            break;
        case construct::DeclareExpression:
            {
                for (std::size_t i = 0u; i + 1u < aExpression->size(); ++i)
                    declaration(aExpression->child(i));
                expression(aExpression->children.back(), t);
                break;
            }
        case construct::RaiseExpression:
            {
                auto* x = aExpression->child(0)->ent;
                while (x != nullptr && x->renamed != nullptr)
                    x = x->renamed;
                i64(static_cast<std::int64_t>(x != nullptr ? x->exception_id : 0u));
                if (aExpression->size() > 1u)
                    expression(aExpression->child(1), iProgram.string_type());
                else
                    i64(0);
                runtime("raise", { type::I64, type::I64 }, type::Void);
                branch(handler_target());
                // (unreachable: a value of the type)
                constant_value(static_value{ bigint{ 0 } }, t);
                break;
            }
        case construct::Target:
            if (!iTargetNames.empty())
                expression(iTargetNames.back(), t);
            else
                i64(0);
            break;
        default:
            if (meaning_of(aExpression) == meaning::Call)
                call_expression(aExpression, false);
            else
                iProgram.error(aExpression, "expression not supported yet");
            break;
        }
        iWhere = previous;
        // (a universal or integer value wanted as a floating point type, or vice versa: converted)
        if (aType != nullptr && aExpression->type != nullptr && kind_of(aType) != kind_of(aExpression->type) && aType->view()->is_scalar() &&
            aExpression->type->view()->is_scalar())
            convert_representation(aExpression->type, aType);
    }

    void function_generator::enumeration_code(ada_type* aType)
    {
        auto* base = aType->view()->base_type()->view();
        if (base->kind != type_kind::Enumeration || base->representation.empty())
            return;
        std::string table;
        for (auto const& code : base->representation)
        {
            auto const c = code.to_int64();
            table.append(reinterpret_cast<char const*>(&c), 8u);
        }
        i64(8);
        op(opcode::I64Mul);
        i64(static_cast<std::int64_t>(iProgram.static_bytes(table)));
        op(opcode::I64Add);
        load64(0u);
    }

    void function_generator::enumeration_position(ada_type* aType)
    {
        auto* base = aType->view()->base_type()->view();
        if (base->kind != type_kind::Enumeration || base->representation.empty())
            return;
        std::string table;
        for (auto const& code : base->representation)
        {
            auto const c = code.to_int64();
            table.append(reinterpret_cast<char const*>(&c), 8u);
        }
        auto const address = static_cast<std::int64_t>(iProgram.static_bytes(table));
        auto const code = temporary(type::I64);
        auto const i = temporary(type::I64);
        set(code);
        i64(0);
        set(i);
        begin(control_kind::Block);
        auto const exit = iControls.size() - 1u;
        begin(control_kind::Loop);
        auto const head = iControls.size() - 1u;
        get(i);
        i64(static_cast<std::int64_t>(base->representation.size()));
        op(opcode::I64GeS);
        branch(exit, true);
        get(i);
        i64(8);
        op(opcode::I64Mul);
        i64(address);
        op(opcode::I64Add);
        load64(0u);
        get(code);
        op(opcode::I64Eq);
        branch(exit, true);
        get(i);
        i64(1);
        op(opcode::I64Add);
        set(i);
        branch(head);
        end();
        end();
        get(i);
        release(i);
        release(code);
    }

    void function_generator::convert_representation(ada_type* aSource, ada_type* aTarget)
    {
        auto const s = kind_of(aSource);
        auto const d = kind_of(aTarget);
        if (s == d)
            return;
        if (s == vkind::F32 && d == vkind::F64)
            op(opcode::F64ConvertF32);
        else if (s == vkind::F64 && d == vkind::F32)
            op(opcode::F32ConvertF64);
        else if (s == vkind::I64 && d == vkind::F64)
            op(opcode::F64SConvertI64);
        else if (s == vkind::I64 && d == vkind::F32)
            op(opcode::F32SConvertI64);
        else if (s == vkind::F64 && d == vkind::I64)
            round_to_integer();
        else if (s == vkind::F32 && d == vkind::I64)
        {
            op(opcode::F64ConvertF32);
            round_to_integer();
        }
    }

    void function_generator::name_value(node* aName, ada_type* aType)
    {
        switch (meaning_of(aName))
        {
        case meaning::Call:
            call_expression(aName, false);
            return;
        case meaning::Literal:
            constant_value(iProgram.evaluate(aName, nullptr), aName->type);
            return;
        case meaning::StringLiteral:
            string_literal(aName, aType != nullptr ? aType : aName->type);
            return;
        case meaning::Value:
            constant_value(iProgram.evaluate(aName, nullptr), aType != nullptr ? aType : aName->type);
            return;
        case meaning::Component:
            {
                address(aName);
                auto* t = aName->type;
                if (auto const* bitField = bit_field_name(aName))
                    bit_field_load(*bitField);
                else if (t != nullptr && !t->view()->is_composite())
                    load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
                return;
            }
        case meaning::Object:
            if (aName->ent != nullptr)
            {
                load_object(aName->ent);
                return;
            }
            break;
        default:
            break;
        }
        if (aName->is(construct::Operator))
        {
            string_literal(aName, aType != nullptr ? aType : aName->type);
            return;
        }
        if (current_instance(aName))
            return;
        iProgram.error(aName, "name \"" + name_text(aName->source) + "\" has no value");
    }

    bool function_generator::current_instance(node* aName)
    {
        // (the name of a task type (or single task) within its body: its current instance (its object's address), 9.1(9))
        auto* e = aName->ent;
        if (e == nullptr)
            return false;
        ada_type* t = e->is_type() || e->kind == entity_kind::Task ? e->type : nullptr;
        if (t == nullptr)
            return false;
        auto* v = t->view()->base_type()->view();
        // (the name of a record type within its declaration (a per-object expression, 3.8(18/2)): the object being initialized)
        // (or of an ancestor of the record being initialized (an inherited component's): its ancestor part, the same object)
        for (auto i = iInstances.size(); i-- > 0u;)
            for (auto* d = iInstances[i].first; d != nullptr;)
            {
                if (d == v)
                {
                    get(iInstances[i].second);
                    return true;
                }
                auto* p = d->parent != nullptr ? d->parent->view()->base_type()->view() : nullptr;
                d = p != d ? p : nullptr;
            }
        if (v->kind != type_kind::Task)
            return false;
        auto* unit = v->unit;
        auto body = unit != nullptr ? iProgram.iTaskBodies.find(unit) : iProgram.iTaskBodies.end();
        if (body == iProgram.iTaskBodies.end())
            return false;
        bool within = false;
        for (auto* s = iSubprogram; s != nullptr && !within; s = s->scope != nullptr ? s->scope->owner : nullptr)
            within = s == body->second;
        if (!within)
            return false;
        auto const level = static_cast<int>(body->second->sub->level);
        if (iLevel == level && iHasSelf)
            get(iSelf);
        else
        {
            frame_of_level(level);
            load64(8u);
        }
        return true;
    }

    void function_generator::apply_value(node* aApply, ada_type* aType)
    {
        switch (meaning_of(aApply))
        {
        case meaning::Call:
            call_expression(aApply, false);
            return;
        case meaning::Conversion:
            conversion(aApply->child(1), aApply->child(1)->type, aApply->type, aApply);
            // (a composite value: of the target's predicate (a scalar's: see conversion))
            if (aApply->type != nullptr && aApply->type->view()->is_composite())
                predicate_check(aApply->type, aApply->child(1)->type, aApply->child(1));
            return;
        case meaning::Index:
            {
                address(aApply);
                auto* t = aApply->type;
                if (t != nullptr && !t->view()->is_composite())
                    load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
                return;
            }
        case meaning::Slice:
            address(aApply);
            return;
        case meaning::Literal:
            constant_value(iProgram.evaluate(aApply, nullptr), aApply->type);
            return;
        default:
            break;
        }
        (void)aType;
        iProgram.error(aApply, "expression not supported yet");
    }

    void function_generator::dereference(node* aAll)
    {
        // (an access value's designated object: a scalar's value, a composite's address)
        expression(aAll->child(0), aAll->child(0)->type);
        null_check(aAll);
        auto* t = aAll->type;
        if (t != nullptr && !t->view()->is_composite())
            load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
    }

    void function_generator::null_check(node const* aWhere)
    {
        auto const value = temporary(type::I64);
        tee(value);
        op(opcode::I64Eqz);
        begin_if();
        raise_check(0, aWhere, "access check failed");
        end();
        get(value);
        release(value);
    }

    void function_generator::if_expression(node* aExpression, ada_type* aType)
    {
        auto const k = wasm_of(aType);
        condition(aExpression->child(0));
        begin_if(k);
        expression(aExpression->child(1), aType);
        std::size_t opened = 1u;
        bool elsePart = false;
        for (std::size_t i = 2u; i < aExpression->size(); ++i)
        {
            auto* c = aExpression->child(i);
            else_();
            if (c->is(construct::Elsif))
            {
                condition(c->child(0));
                begin_if(k);
                ++opened;
                expression(c->child(1), aType);
            }
            else if (c->is(construct::Else))
            {
                expression(c->child(0), aType);
                elsePart = true;
            }
        }
        if (!elsePart)
        {
            // (a Boolean if expression without else: True)
            else_();
            i64(1);
        }
        while (opened-- > 0u)
            end();
    }

    void function_generator::case_expression(node* aExpression, ada_type* aType)
    {
        auto const k = wasm_of(aType);
        auto const value = local(type::I64);
        expression(aExpression->child(0), aExpression->child(0)->type);
        set(value);
        std::size_t opened = 0u;
        for (std::size_t i = 1u; i < aExpression->size(); ++i)
        {
            auto* alternative = aExpression->child(i);
            bool others = false;
            i32(0);
            for (auto* choice : alternative->child(0)->children)
            {
                if (choice->is(construct::Others))
                {
                    others = true;
                    continue;
                }
                choice_test(choice, value);
                op(opcode::I32Ior);
            }
            if (others)
            {
                drop();
                i32(1);
            }
            begin_if(k);
            ++opened;
            expression(alternative->children.back(), aType);
            else_();
        }
        raise_check(0, aExpression, "case expression's value not covered");
        constant_value(static_value{ bigint{ 0 } }, aType);
        while (opened-- > 0u)
            end();
    }

    void function_generator::membership(node* aExpression)
    {
        // (an i32: whether the tested value is any of the choices (a value, a range or a subtype))
        auto* tested = aExpression->child(0);
        auto* t = tested->type;
        bool const negated = aExpression->has_flag("not");
        std::vector<node*> choices;
        for (std::size_t i = 1u; i < aExpression->size(); ++i)
            if (!aExpression->child(i)->is(construct::Flag))
                choices.push_back(aExpression->child(i));
        auto const k = kind_of(t);
        bool const composite = t != nullptr && t->view()->is_composite();
        auto const value = local(composite ? type::I64 : wasm(k));
        expression(tested, t);
        set(value);
        // (the choices: evaluated in order until one is satisfied (4.5.2(27/4)))
        auto const result = local(type::I32);
        i32(0);
        set(result);
        for (auto* c : choices)
        {
            get(result);
            op(opcode::I32Eqz);
            begin_if();
            // (a modular type's values: unsigned)
            bool const unsignedValues = t != nullptr && t->view()->base_type()->view()->kind == type_kind::Modular;
            if (c->is(construct::Range))
            {
                get(value);
                expression(c->child(0), t);
                op(k == vkind::F64 ? opcode::F64Ge : k == vkind::F32 ? opcode::F32Ge : unsignedValues ? opcode::I64GeU : opcode::I64GeS);
                get(value);
                expression(c->child(1), t);
                op(k == vkind::F64 ? opcode::F64Le : k == vkind::F32 ? opcode::F32Le : unsignedValues ? opcode::I64LeU : opcode::I64LeS);
                op(opcode::I32And);
            }
            else if (c->type != nullptr && (c->is(construct::SubtypeIndication) || meaning_of(c) == meaning::Type ||
                (c->is(construct::Attribute) && (name_text(c->child(1)->source) == "range" || name_text(c->child(1)->source) == "base"))))
            {
                auto* s = c->type->view();
                if (s->kind == type_kind::Record && s->base_type()->view()->unchecked_union)
                {
                    // (an unchecked union subtype: its discriminants are not checked; a constrained one, of a value that lacks
                    // inferable discriminants: Program_Error (B.3.3(25/2)))
                    if (!s->discriminant_constraint.empty() && !iProgram.inferable_discriminants(tested))
                        raise_check(1, c, "membership of an unchecked union value without inferable discriminants");
                    i32(1);
                }
                else if (s->is_scalar())
                {
                    get(value);
                    scalar_low(s);
                    op(k == vkind::F64 ? opcode::F64Ge : k == vkind::F32 ? opcode::F32Ge : unsignedValues ? opcode::I64GeU : opcode::I64GeS);
                    get(value);
                    scalar_high(s);
                    op(k == vkind::F64 ? opcode::F64Le : k == vkind::F32 ? opcode::F32Le : unsignedValues ? opcode::I64LeU : opcode::I64LeS);
                    op(opcode::I32And);
                }
                else if (s->kind == type_kind::ClassWide || s->tagged)
                {
                    // (a tagged object's tag: of the type, or a type derived from it ('Class))
                    get(value);
                    if (t->view()->kind == type_kind::Access)
                        ;
                    load64(0u);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(s->kind == type_kind::ClassWide ? s->specific : s)));
                    if (s->kind == type_kind::ClassWide)
                        runtime("is_descendant", { type::I64, type::I64 }, type::I32);
                    else
                        op(opcode::I64Eq);
                }
                else if (s->is_access())
                {
                    // (a null exclusion; a constrained access subtype's designated subtype)
                    if (s->not_null)
                    {
                        get(value);
                        i64(0);
                        op(opcode::I64Ne);
                    }
                    else
                        i32(1);
                    // (an access-to-variable type, the tested value of an access-to-constant one: not convertible (4.5.2(30.1/4), 4.6(24.20/3)))
                    if (s->kind == type_kind::Access && !s->base_type()->view()->access_constant && t != nullptr && t->view()->kind == type_kind::Access &&
                        t->view()->base_type()->view()->access_constant)
                    {
                        op(opcode::Drop);
                        i32(0);
                    }
                    // (a designated tagged type: the designated object (unless null) of it (or a descendant, if its class) as a
                    // conversion would check (4.5.2(30.2/4)))
                    if (s->kind == type_kind::Access && t != nullptr && t->view()->kind == type_kind::Access)
                        if (auto* dd = iProgram.designated_of(s) != nullptr ? iProgram.designated_of(s)->view() : nullptr; dd != nullptr && dd->tagged)
                            if (auto* sd = iProgram.designated_of(t->view()) != nullptr ? iProgram.designated_of(t->view())->view() : nullptr;
                                sd != nullptr && sd->kind == type_kind::ClassWide)
                            {
                                auto* targetSpecific = dd->kind == type_kind::ClassWide ? dd->specific : dd;
                                if (dd->kind != type_kind::ClassWide || !iProgram.is_descendant(sd->specific, targetSpecific))
                                {
                                    get(value);
                                    op(opcode::I64Eqz);
                                    begin_if(type::I32);
                                    i32(1);
                                    else_();
                                    get(value);
                                    load64(0u);
                                    i64(static_cast<std::int64_t>(iProgram.tag_of(targetSpecific)));
                                    if (dd->kind == type_kind::ClassWide)
                                        runtime("is_descendant", { type::I64, type::I64 }, type::I32);
                                    else
                                        op(opcode::I64Eq);
                                    end();
                                    op(opcode::I32And);
                                }
                            }
                    // (a named access type, the tested value of an anonymous one: the designated object not deeper than it (4.5.2(30.3/4)))
                    if (s->kind == type_kind::Access && !s->anonymous_access && t != nullptr && t->view()->kind == type_kind::Access &&
                        t->view()->anonymous_access)
                    {
                        auto const target = iProgram.type_level(s);
                        auto const level = access_level(tested);
                        if (level)
                            i32(*level <= target ? 1 : 0);
                        else if (target >= iDynamicLevelOwner)
                        {
                            drop();
                            i32(1);
                        }
                        else
                        {
                            i64(target);
                            op(opcode::I64LeS);
                        }
                        op(opcode::I32And);
                    }
                    if (s->kind == type_kind::Access && s->designated != nullptr && s->base_type()->view()->designated != s->designated &&
                        ((s->designated->view()->kind == type_kind::Record && !s->designated->view()->discriminant_constraint.empty()) ||
                         (s->designated->view()->kind == type_kind::Array && s->designated->view()->constrained)))
                    {
                        designated_constraint_test(s, value);
                        op(opcode::I32And);
                    }
                }
                else if (s->kind == type_kind::Array && s->constrained)
                {
                    // (an array subtype's index constraint: the same bounds)
                    i32(1);
                    for (std::size_t d = 0u; d < s->indexes.size(); ++d)
                    {
                        bounds(value, d, true);
                        scalar_low(s->indexes[d]);
                        op(opcode::I64Eq);
                        op(opcode::I32And);
                        bounds(value, d, false);
                        scalar_high(s->indexes[d]);
                        op(opcode::I64Eq);
                        op(opcode::I32And);
                    }
                }
                else if (!s->discriminant_constraint.empty())
                {
                    // (a discriminant constraint: the same discriminants)
                    auto* base = s->base_type()->view();
                    iProgram.layout(base);
                    i32(1);
                    std::size_t d = 0u;
                    for (auto const& component : base->components)
                    {
                        if (!component.discriminant || !component.offset)
                            continue;
                        auto* constraint = d < s->discriminant_constraint.size() ? s->discriminant_constraint[d] : nullptr;
                        ++d;
                        if (constraint == nullptr)
                            continue;
                        get(value);
                        load(kind_of(component.type), iProgram.scalar_size(component.type), static_cast<std::uint32_t>(*component.offset), !is_unsigned(component.type));
                        expression(constraint, component.type);
                        op(kind_of(component.type) == vkind::I64 ? opcode::I64Eq : kind_of(component.type) == vkind::F32 ? opcode::F32Eq : opcode::F64Eq);
                        op(opcode::I32And);
                    }
                }
                else
                    i32(1);
                // (its predicate, if the value satisfies its constraint (4.5.2(29/3)))
                if (auto* predicate = c->type->predicate != nullptr ? c->type->predicate : s->predicate)
                {
                    begin_if(type::I32);
                    predicate_test(predicate, value, false);
                    else_();
                    i32(0);
                    end();
                }
            }
            else if (composite)
            {
                // (a record type's primitive "=" (4.5.2(28.1/4)), else predefined equality)
                get(value);
                if (is_aggregate_node(c))
                    aggregate(c, t);
                else
                    expression(c, t);
                component_equality(t, true);
            }
            else if (k == vkind::I64 && t != nullptr && limited_type(t) && primitive_equality(t, true) != nullptr)
            {
                // (a limited type's primitive "=" (4.5.2(28.1/4)))
                get(value);
                expression(c, t);
                component_equality(t, true);
            }
            else
            {
                get(value);
                expression(c, t);
                op(k == vkind::F64 ? opcode::F64Eq : k == vkind::F32 ? opcode::F32Eq : opcode::I64Eq);
            }
            set(result);
            end();
        }
        get(result);
        if (negated)
            op(opcode::I32Eqz);
    }

    // --- names as objects

    void function_generator::address(node* aName)
    {
        if (implicitly_dereferenced(aName, false))
            return;
        // (an i64: the address of the object a name denotes (an array: its descriptor's))
        switch (aName->kind)
        {
        case construct::Parenthesized:
            address(aName->child(0));
            return;
        case construct::Identifier:
        case construct::Selected:
            if (meaning_of(aName) == meaning::Component)
            {
                // (a record's component: the record's address plus its offset)
                auto* prefix = aName->child(0);
                if (aName->mark & MarkDeref)
                {
                    expression(prefix, prefix->type);
                    null_check(aName);
                }
                else
                    address(prefix);
                auto* component = aName->ent;
                auto* recordType = prefix->type;
                if (aName->mark & MarkDeref)
                    recordType = iProgram.designated_of(recordType);
                variant_check(recordType, component, aName);
                auto const offset = component_offset(recordType, component);
                if (indirect_component(recordType, component))
                {
                    // (a dynamically sized record component: in the tail (its offset in place))
                    auto const record = temporary(type::I64);
                    tee(record);
                    load64(static_cast<std::uint32_t>(offset));
                    get(record);
                    op(opcode::I64Add);
                    release(record);
                    return;
                }
                if (offset != 0u)
                {
                    i64(static_cast<std::int64_t>(offset));
                    op(opcode::I64Add);
                }
                return;
            }
            if (aName->ent != nullptr && aName->ent->is_object())
            {
                address_of(aName->ent);
                return;
            }
            if (current_instance(aName))
                return;
            if (meaning_of(aName) == meaning::Call)
            {
                // (a function's result: its value's address)
                call_expression(aName, false);
                materialize(aName->type);
                return;
            }
            break;
        case construct::All:
            expression(aName->child(0), aName->child(0)->type);
            null_check(aName);
            return;
        case construct::Apply:
            switch (meaning_of(aName))
            {
            case meaning::Index:
                index_address(aName);
                return;
            case meaning::Slice:
                slice_descriptor(aName);
                return;
            case meaning::Conversion:
                if (aName->type != nullptr && aName->type->view()->is_composite())
                {
                    // (a view conversion of a composite object: its address)
                    expression(aName, aName->type);
                    return;
                }
                break;
            case meaning::Call:
                call_expression(aName, false);
                materialize(aName->type);
                return;
            default:
                break;
            }
            break;
        case construct::Qualified:
            expression(aName, aName->type);
            materialize(aName->type);
            return;
        case construct::Target:
            if (!iTargetAddresses.empty())
            {
                get(iTargetAddresses.back());
                return;
            }
            break;
        default:
            break;
        }
        // (a value: in a temporary)
        expression(aName, aName->type);
        materialize(aName->type);
    }

    void function_generator::materialize(ada_type* aType)
    {
        // (a scalar value on the stack: stored in a temporary in the frame (its address))
        if (aType == nullptr || aType->view()->is_composite())
            return;
        auto const value = temporary(wasm_of(aType));
        set(value);
        iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
        auto const offset = iFrameSize;
        iFrameSize += 8u;
        get(iFrame);
        get(value);
        store(kind_of(aType), iProgram.scalar_size(aType), static_cast<std::uint32_t>(offset));
        get(iFrame);
        i64(static_cast<std::int64_t>(offset));
        op(opcode::I64Add);
        release(value);
    }

    bool function_generator::indirect_component(ada_type* aRecord, entity* aComponent)
    {
        // (a component of a dynamically sized record subtype (its object in the record's tail))
        auto* t = aRecord->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view();
        auto* base = t->base_type()->view();
        if (base->kind != type_kind::Record && base->kind != type_kind::Protected && base->kind != type_kind::Task)
            return false;
        iProgram.layout(base);
        for (auto const& c : base->components)
            if (c.ent == aComponent || (c.ent != nullptr && aComponent != nullptr && c.ent->name == aComponent->name))
                return c.dynamic && c.type != nullptr && c.type->view()->kind != type_kind::Array;
        return false;
    }

    void function_generator::component_address(ada_type* aBase, component_info const& aComponent, std::uint32_t aRecord)
    {
        // (an i64: a component's address (a dynamically sized record component's: in the tail))
        get(aRecord);
        if (aComponent.dynamic && aComponent.type != nullptr && aComponent.type->view()->kind != type_kind::Array)
        {
            load64(static_cast<std::uint32_t>(*aComponent.offset));
            get(aRecord);
            op(opcode::I64Add);
            return;
        }
        (void)aBase;
        i64(static_cast<std::int64_t>(*aComponent.offset));
        op(opcode::I64Add);
    }

    std::uint64_t function_generator::component_offset(ada_type* aRecord, entity* aComponent)
    {
        auto* t = aRecord->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view();
        auto* base = t->base_type()->view();
        if (base->kind == type_kind::Protected)
            base = base->base_type();
        iProgram.layout(base);
        // (the component itself; else one of its name (an inherited component's entity is the ancestor's))
        for (auto const& c : base->components)
            if (c.ent == aComponent)
                return c.offset.value_or(0u);
        for (auto const& c : base->components)
            if (c.ent != nullptr && aComponent != nullptr && c.ent->name == aComponent->name)
                return c.offset.value_or(0u);
        iProgram.error(iWhere, "internal error: component \"" + (aComponent != nullptr ? aComponent->name : std::string{}) + "\" has no offset");
    }

    void function_generator::bounds(std::uint32_t aDescriptor, std::size_t aDimension, bool aFirst)
    {
        get(aDescriptor);
        load64(static_cast<std::uint32_t>(8u + 16u * aDimension + (aFirst ? 0u : 8u)));
    }

    void function_generator::data_address(std::uint32_t aDescriptor)
    {
        get(aDescriptor);
        get(aDescriptor);
        load64(0u);
        op(opcode::I64Add);
    }

    void function_generator::index_address(node* aApply)
    {
        // (an array's element: the descriptor's data plus the (row major) offset of the indexes (each checked))
        auto* prefix = aApply->child(0);
        if (aApply->mark & MarkDeref)
        {
            expression(prefix, prefix->type);
            null_check(aApply);
        }
        else
            address(prefix);
        auto const descriptor = local(type::I64);
        set(descriptor);
        auto* arrayType = prefix->type->view();
        if (aApply->mark & MarkDeref)
            arrayType = iProgram.designated_of(prefix->type)->view();
        std::size_t const dimensions = arrayType->indexes.size();
        auto const offset = local(type::I64);
        i64(0);
        set(offset);
        for (std::size_t d = 0u; d < dimensions; ++d)
        {
            auto const index = temporary(type::I64);
            expression(aApply->child(d + 1u), arrayType->indexes[d]->base_type());
            set(index);
            // (checked against the bounds)
            get(index);
            bounds(descriptor, d, true);
            op(opcode::I64LtS);
            get(index);
            bounds(descriptor, d, false);
            op(opcode::I64GtS);
            op(opcode::I32Ior);
            begin_if();
            raise_check(0, aApply, "index check failed");
            end();
            // offset = offset * length(d) + (index - first(d))
            if (d > 0u)
            {
                get(offset);
                bounds(descriptor, d, false);
                bounds(descriptor, d, true);
                op(opcode::I64Sub);
                i64(1);
                op(opcode::I64Add);
                op(opcode::I64Mul);
                set(offset);
            }
            get(offset);
            get(index);
            bounds(descriptor, d, true);
            op(opcode::I64Sub);
            op(opcode::I64Add);
            set(offset);
            release(index);
        }
        data_address(descriptor);
        get(offset);
        element_size(arrayType);
        op(opcode::I64Mul);
        op(opcode::I64Add);
    }

    void function_generator::element_size(ada_type* aArray)
    {
        auto const size = iProgram.component_size(aArray);
        if (size == 0u)
        {
            auto* c = aArray->view()->component != nullptr ? aArray->view()->component->view() : nullptr;
            if (c != nullptr && (c->kind == type_kind::Record || c->kind == type_kind::Private) && is_runtime_sized(c) && c->kind != type_kind::ClassWide)
            {
                // (a dynamically sized record subtype: its objects' size)
                record_subtype_size(c);
                return;
            }
            if (c == nullptr || c->kind != type_kind::Array || !c->constrained)
                iProgram.error(iWhere, "arrays of dynamically sized components are not supported yet");
            // (a constrained array subtype of dynamic bounds: its objects' size (a descriptor, then the data (aligned)))
            auto const dimensions = c->indexes.size();
            i64(1);
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                // max(0, last - first + 1)
                auto const n = temporary(type::I64);
                scalar_high(c->indexes[d]);
                scalar_low(c->indexes[d]);
                op(opcode::I64Sub);
                i64(1);
                op(opcode::I64Add);
                tee(n);
                i64(0);
                get(n);
                i64(0);
                op(opcode::I64GtS);
                op(opcode::Select);
                op(opcode::I64Mul);
                release(n);
            }
            element_size(c);
            op(opcode::I64Mul);
            i64(static_cast<std::int64_t>(DescriptorHeader(dimensions) + 7u));
            op(opcode::I64Add);
            i64(~std::int64_t{ 7 });
            op(opcode::I64And);
            return;
        }
        i64(static_cast<std::int64_t>(size));
    }

    void function_generator::slice_descriptor(node* aApply)
    {
        // (a slice: a new descriptor (on the secondary stack) of the range, whose data is in the prefix's)
        auto* prefix = aApply->child(0);
        if (aApply->mark & MarkDeref)
        {
            expression(prefix, prefix->type);
            null_check(aApply);
        }
        else
            address(prefix);
        auto const descriptor = local(type::I64);
        set(descriptor);
        auto* arrayType = prefix->type->view();
        if (aApply->mark & MarkDeref)
            arrayType = iProgram.designated_of(prefix->type)->view();
        auto const low = local(type::I64);
        auto const high = local(type::I64);
        auto* range = aApply->child(1);
        if (range->is(construct::Range))
        {
            expression(range->child(0), arrayType->indexes[0]->base_type());
            set(low);
            expression(range->child(1), arrayType->indexes[0]->base_type());
            set(high);
        }
        else if (range->is(construct::Attribute))
            range_attribute_bounds(range, low, high);
        else
        {
            scalar_low(range->type);
            set(low);
            scalar_high(range->type);
            set(high);
        }
        // (a non null slice's bounds: within the prefix's)
        get(low);
        get(high);
        op(opcode::I64LeS);
        begin_if();
        get(low);
        bounds(descriptor, 0u, true);
        op(opcode::I64LtS);
        get(high);
        bounds(descriptor, 0u, false);
        op(opcode::I64GtS);
        op(opcode::I32Ior);
        begin_if();
        raise_check(0, aApply, "range check failed");
        end();
        end();
        auto const result = local(type::I64);
        i64(24);
        runtime("ss_allocate", { type::I64 }, type::I64);
        set(result);
        // (data: the prefix's data + (low - first) * size, relative to the new descriptor)
        get(result);
        data_address(descriptor);
        get(low);
        bounds(descriptor, 0u, true);
        op(opcode::I64Sub);
        element_size(arrayType);
        op(opcode::I64Mul);
        op(opcode::I64Add);
        get(result);
        op(opcode::I64Sub);
        store64(0u);
        get(result);
        get(low);
        store64(8u);
        get(result);
        get(high);
        store64(16u);
        get(result);
    }

    void function_generator::range_attribute_bounds(node* aAttribute, std::uint32_t aLow, std::uint32_t aHigh)
    {
        // (X'Range (N): its bounds (an array object's: from its descriptor; a subtype's))
        auto* prefix = aAttribute->child(0);
        std::size_t dimension = 0u;
        auto const arguments = attribute_arguments(aAttribute);
        if (!arguments.empty())
        {
            auto v = iProgram.evaluate(arguments[0], nullptr);
            if (v.is_integer())
                dimension = static_cast<std::size_t>(v.integer().to_int64() - 1);
        }
        auto* t = prefix->type != nullptr ? prefix->type->view() : nullptr;
        if (prefix->mark & 0x2000u)
            t = iProgram.designated_of(prefix->type)->view();
        if (meaning_of(prefix) == meaning::Type || t == nullptr || t->is_scalar())
        {
            auto* s = t;
            if (s != nullptr && s->kind == type_kind::Array)
                s = s->indexes[dimension]->view();
            scalar_low(s);
            set(aLow);
            scalar_high(s);
            set(aHigh);
            return;
        }
        if (prefix->mark & 0x2000u)
        {
            expression(prefix, prefix->type);
            null_check(aAttribute);
        }
        else
            address(prefix);
        auto const descriptor = temporary(type::I64);
        set(descriptor);
        bounds(descriptor, dimension, true);
        set(aLow);
        bounds(descriptor, dimension, false);
        set(aHigh);
        release(descriptor);
    }

    // --- arrays

    void function_generator::subtype_descriptor(ada_type* aArray)
    {
        // (an i64: a new descriptor (on the secondary stack) of a constrained array subtype's bounds (data: none))
        auto* t = aArray->view();
        auto const dimensions = t->indexes.size();
        auto const result = local(type::I64);
        i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
        runtime("ss_allocate", { type::I64 }, type::I64);
        set(result);
        for (std::size_t d = 0u; d < dimensions; ++d)
        {
            // (a bound that depends on a discriminant: of the record being built or named (its instance))
            get(result);
            index_bound(t->indexes[d], true);
            store64(static_cast<std::uint32_t>(8u + 16u * d));
            get(result);
            index_bound(t->indexes[d], false);
            store64(static_cast<std::uint32_t>(16u + 16u * d));
        }
        get(result);
    }

    void function_generator::init_descriptor(ada_type* aArray, std::uint32_t aAddress)
    {
        // (a constrained array object's descriptor (its data follows it): its subtype's bounds)
        auto* t = aArray->view();
        auto const dimensions = t->indexes.size();
        get(aAddress);
        i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
        store64(0u);
        for (std::size_t d = 0u; d < dimensions; ++d)
        {
            get(aAddress);
            scalar_low(t->indexes[d]);
            store64(static_cast<std::uint32_t>(8u + 16u * d));
            get(aAddress);
            scalar_high(t->indexes[d]);
            store64(static_cast<std::uint32_t>(16u + 16u * d));
        }
    }

    void function_generator::array_length(std::uint32_t aDescriptor, std::size_t aDimensions)
    {
        // (an i64: the number of elements of an array (its descriptor))
        i64(1);
        for (std::size_t d = 0u; d < aDimensions; ++d)
        {
            // max(0, last - first + 1)
            auto const n = temporary(type::I64);
            bounds(aDescriptor, d, false);
            bounds(aDescriptor, d, true);
            op(opcode::I64Sub);
            i64(1);
            op(opcode::I64Add);
            tee(n);
            i64(0);
            get(n);
            i64(0);
            op(opcode::I64GtS);
            op(opcode::Select);
            op(opcode::I64Mul);
            release(n);
        }
    }

    void function_generator::allocate_array(ada_type* aType, std::uint32_t aSourceDescriptor, bool aSecondary, bool aHeap)
    {
        // (an i64: a new array object (its descriptor, then its data) with the source descriptor's bounds: on the stack, the
        // secondary stack or the heap)
        auto* t = aType->view();
        auto const dimensions = t->base_type()->view()->indexes.size();
        auto const size = local(type::I64);
        array_length(aSourceDescriptor, dimensions);
        element_size(t);
        op(opcode::I64Mul);
        i64(static_cast<std::int64_t>(DescriptorHeader(dimensions) + 7u));
        op(opcode::I64Add);
        i64(~std::int64_t{ 7 });
        op(opcode::I64And);
        set(size);
        auto const result = local(type::I64);
        if (aHeap && iPool)
        {
            // (from a storage pool: its data (the designated object's storage, 13.11(17)); its descriptor from the heap (the data
            // as an offset from it))
            auto const data = local(type::I64);
            i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
            runtime("allocate", { type::I64 }, type::I64);
            set(result);
            array_length(aSourceDescriptor, dimensions);
            element_size(t);
            op(opcode::I64Mul);
            heap_allocate();
            set(data);
            get(result);
            get(data);
            get(result);
            op(opcode::I64Sub);
            store64(0u);
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                get(result);
                bounds(aSourceDescriptor, d, true);
                store64(static_cast<std::uint32_t>(8u + 16u * d));
                get(result);
                bounds(aSourceDescriptor, d, false);
                store64(static_cast<std::uint32_t>(16u + 16u * d));
            }
            get(result);
            return;
        }
        if (aHeap)
        {
            get(size);
            heap_allocate();
        }
        else if (aSecondary)
        {
            get(size);
            runtime("ss_allocate", { type::I64 }, type::I64);
        }
        else
            stack_allocate(size);
        set(result);
        get(result);
        i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
        store64(0u);
        for (std::size_t d = 0u; d < dimensions; ++d)
        {
            get(result);
            bounds(aSourceDescriptor, d, true);
            store64(static_cast<std::uint32_t>(8u + 16u * d));
            get(result);
            bounds(aSourceDescriptor, d, false);
            store64(static_cast<std::uint32_t>(16u + 16u * d));
        }
        get(result);
    }

    void function_generator::stack_allocate(std::uint32_t aSize)
    {
        // (an i64: storage on the primary stack (released when the enclosing frame or block ends); beyond its limit (or a size
        // that wraps): Storage_Error)
        auto const top = local(type::I64);
        op(opcode::GlobalGet);
        u32(iProgram.sp_global());
        get(aSize);
        i64(15);
        op(opcode::I64Add);
        i64(~std::int64_t{ 15 });
        op(opcode::I64And);
        op(opcode::I64Add);
        tee(top);
        op(opcode::GlobalGet);
        u32(iProgram.limit_global());
        op(opcode::I64GtU);
        get(top);
        op(opcode::GlobalGet);
        u32(iProgram.sp_global());
        op(opcode::I64LtU);
        op(opcode::I32Ior);
        begin_if();
        runtime("stack_overflow", {}, type::Void);
        check();
        end();
        op(opcode::GlobalGet);
        u32(iProgram.sp_global());
        get(top);
        op(opcode::GlobalSet);
        u32(iProgram.sp_global());
    }

    void function_generator::allocate_record(ada_type* aType, std::optional<std::uint32_t> aValue, bool aHeap)
    {
        if (is_runtime_sized(aType))
        {
            dynamic_object(aType, aValue, aHeap ? 1 : 0);
            return;
        }
        auto const size = iProgram.object_size(aType);
        if (size == 0u)
            iProgram.error(iWhere, "dynamically sized records are not supported yet");
        (void)aValue;
        i64(static_cast<std::int64_t>(size));
        if (aHeap)
            heap_allocate();
        else
        {
            auto const s = temporary(type::I64);
            set(s);
            stack_allocate(s);
            release(s);
        }
    }

    void function_generator::length_check(std::uint32_t aTarget, std::uint32_t aSource, std::size_t aDimensions, node const* aWhere)
    {
        // (two arrays (descriptors): the same length in each dimension (else Constraint_Error))
        auto const a = temporary(type::I64);
        auto const b = temporary(type::I64);
        for (std::size_t d = 0u; d < std::max<std::size_t>(aDimensions, 1u); ++d)
        {
            // (last - first)
            bounds(aTarget, static_cast<std::uint32_t>(d), false);
            bounds(aTarget, static_cast<std::uint32_t>(d), true);
            op(opcode::I64Sub);
            set(a);
            bounds(aSource, static_cast<std::uint32_t>(d), false);
            bounds(aSource, static_cast<std::uint32_t>(d), true);
            op(opcode::I64Sub);
            set(b);
            // (null ranges of any bounds are the same length)
            get(a);
            get(b);
            op(opcode::I64Ne);
            get(a);
            i64(0);
            op(opcode::I64GeS);
            get(b);
            i64(0);
            op(opcode::I64GeS);
            op(opcode::I32Ior);
            op(opcode::I32And);
            begin_if();
            raise_check(0, aWhere, "length check failed");
            end();
        }
        release(a);
        release(b);
    }

    void function_generator::copy_value(ada_type* aType, std::uint32_t aTarget, std::uint32_t aSource)
    {
        // (a composite value's representation copied: an array's data (its descriptor is the target's); a record's components)
        auto* t = aType->view();
        if (t->kind == type_kind::Array)
        {
            data_address(aTarget);
            data_address(aSource);
            array_length(aSource, t->base_type()->view()->indexes.size());
            element_size(t);
            op(opcode::I64Mul);
            runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
            return;
        }
        // (a record: its bytes (a tagged object's tag is the target's own))
        if (is_runtime_sized(t))
        {
            // (the same discriminants (or tag); its bytes, after the tag)
            discriminant_check(t, aTarget, aSource, iWhere);
            bool const tagged = t->kind == type_kind::ClassWide || t->base_type()->view()->tagged;
            get(aTarget);
            if (tagged)
            {
                i64(8);
                op(opcode::I64Add);
            }
            get(aSource);
            if (tagged)
            {
                i64(8);
                op(opcode::I64Add);
            }
            record_size(t, aSource);
            if (tagged)
            {
                i64(8);
                op(opcode::I64Sub);
            }
            runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
            return;
        }
        auto const size = iProgram.object_size(t);
        if (size == 0u)
        {
            iProgram.error(iWhere, "copy of a dynamically sized object is not supported yet");
        }
        std::uint64_t const skip = t->tagged || t->kind == type_kind::ClassWide ? 8u : 0u;
        get(aTarget);
        if (skip != 0u)
        {
            i64(static_cast<std::int64_t>(skip));
            op(opcode::I64Add);
        }
        get(aSource);
        if (skip != 0u)
        {
            i64(static_cast<std::int64_t>(skip));
            op(opcode::I64Add);
        }
        i64(static_cast<std::int64_t>(size - skip));
        runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
    }

    void function_generator::total_size(ada_type* aType, std::uint32_t aValue)
    {
        // (an i64: a composite value's total size (an array: its descriptor and data))
        auto* t = aType->view();
        if (t->kind == type_kind::Array)
        {
            array_length(aValue, t->base_type()->view()->indexes.size());
            element_size(t);
            op(opcode::I64Mul);
            i64(static_cast<std::int64_t>(DescriptorHeader(t->base_type()->view()->indexes.size())));
            op(opcode::I64Add);
            return;
        }
        if (is_runtime_sized(t))
        {
            record_size(t, aValue);
            return;
        }
        i64(static_cast<std::int64_t>(iProgram.object_size(t)));
    }

    void function_generator::denote(entity* aCallee)
    {
        // (a dispatching call: the operation it denotes, if it has class-wide preconditions (the ones checked, 6.1.1(33/3)))
        for (auto* o : iProgram.contract_chain(aCallee))
            if (!o->sub->class_preconditions.empty() && aCallee->sub->controlling != nullptr)
            {
                i64(static_cast<std::int64_t>(iProgram.tag_of(aCallee->sub->controlling->view()->base_type()->view())));
                op(opcode::GlobalSet);
                u32(iProgram.denoted_global());
                return;
            }
    }

    void function_generator::copy_to_secondary(ada_type* aType, std::uint32_t aValue)
    {
        // (an i64: a copy of a composite value on the secondary stack)
        auto* t = aType->view();
        auto const result = local(type::I64);
        if (t->kind == type_kind::Array)
        {
            allocate_array(t, aValue, true);
            set(result);
            copy_value(t, result, aValue);
        }
        else if (is_runtime_sized(t))
        {
            dynamic_object(t, aValue, 2);
            set(result);
        }
        else
        {
            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(iProgram.object_size(t), 8u)));
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(result);
            get(result);
            get(aValue);
            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(iProgram.object_size(t), 8u)));
            runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
        }
        get(result);
    }

    void function_generator::variant_selected(ada_type* aBase, component_info const& aComponent, std::uint32_t aRecord)
    {
        // (an i32: whether a record's (its address in a local) discriminants select the variant that a component is in)
        auto* base = aBase;
        auto const discriminant_value = [&](entity* aDiscriminant) -> bool
        {
            for (auto const& c : base->components)
                if (c.ent == aDiscriminant && c.offset)
                {
                    get(aRecord);
                    load(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(*c.offset), !is_unsigned(c.type));
                    return true;
                }
            return false;
        };
        // (a variant's choices: its ranges (others: none of its siblings'))
        auto const selects = [&](variant_info const& aVariant, std::vector<std::pair<bigint, bigint>> const& aRanges)
        {
            i32(0);
            for (auto const& [low, high] : aRanges)
            {
                if (!discriminant_value(aVariant.discriminant))
                {
                    i32(1);
                    op(opcode::I32Ior);
                    continue;
                }
                auto const v = temporary(type::I64);
                tee(v);
                i64(low.to_int64());
                op(opcode::I64GeS);
                get(v);
                i64(high.to_int64());
                op(opcode::I64LeS);
                op(opcode::I32And);
                release(v);
                op(opcode::I32Ior);
            }
        };
        i32(1);
        for (int v = aComponent.variant; v >= 0; v = base->variants[static_cast<std::size_t>(v)].parent)
        {
            auto const& variant = base->variants[static_cast<std::size_t>(v)];
            if (variant.discriminant == nullptr)
                continue;
            if (variant.others)
            {
                std::vector<std::pair<bigint, bigint>> siblings;
                for (auto const& other : base->variants)
                    if (&other != &variant && other.discriminant == variant.discriminant && other.parent == variant.parent)
                        siblings.insert(siblings.end(), other.ranges.begin(), other.ranges.end());
                selects(variant, variant.ranges);
                selects(variant, siblings);
                op(opcode::I32Eqz);
                op(opcode::I32Ior);
            }
            else
                selects(variant, variant.ranges);
            op(opcode::I32And);
        }
    }

    void function_generator::variant_check(ada_type* aRecordType, entity* aComponent, node const* aWhere)
    {
        // ([address] -> [address]: a component of a variant: the record's discriminants select it (else Constraint_Error, 4.1.3(15)))
        if (aRecordType == nullptr || aComponent == nullptr)
            return;
        auto* t = aRecordType->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view();
        auto* base = t->base_type()->view();
        if (base->kind != type_kind::Record || base->variants.empty())
            return;
        component_info const* component = nullptr;
        for (auto const& c : base->components)
            if (c.ent == aComponent)
                component = &c;
        if (component == nullptr || component->variant < 0)
            return;
        iProgram.layout(base);
        auto const record = temporary(type::I64);
        tee(record);
        variant_selected(base, *component, record);
        op(opcode::I32Eqz);
        begin_if();
        raise_check(0, aWhere, "discriminant check failed");
        end();
        release(record);
    }

    void function_generator::index_compatibility_check(ada_type* aArrayType, std::uint32_t aDescriptor)
    {
        // (an array's bounds (e.g. a component's, that depend on discriminants): each non-null range within its index subtype)
        auto* base = aArrayType->view()->base_type()->view();
        for (std::size_t d = 0u; d < base->indexes.size(); ++d)
        {
            auto* index = base->indexes[d];
            auto const low = temporary(type::I64);
            auto const high = temporary(type::I64);
            bounds(aDescriptor, d, true);
            set(low);
            bounds(aDescriptor, d, false);
            set(high);
            get(low);
            get(high);
            op(opcode::I64LeS);
            get(low);
            scalar_low(index);
            op(opcode::I64LtS);
            get(high);
            scalar_high(index);
            op(opcode::I64GtS);
            op(opcode::I32Ior);
            op(opcode::I32And);
            begin_if();
            raise_check(0, iWhere, "index check failed");
            end();
            release(low);
            release(high);
        }
    }

    void function_generator::default_initialize(ada_type* aType, std::uint32_t aAddress)
    {
        // (a composite object's default initialization: its tag, its components' defaults (and their nested arrays' descriptors))
        auto* t = aType->view();
        if (t->kind == type_kind::Array)
        {
            auto* c = t->component != nullptr ? t->component->view() : nullptr;
            node* defaultValue = c != nullptr && c->is_scalar() ? iProgram.default_value_of(t, true) : nullptr;
            if (defaultValue == nullptr && c != nullptr && c->is_scalar())
                defaultValue = iProgram.default_value_of(c, false);
            if (c == nullptr || (!c->is_composite() && !c->is_access() && defaultValue == nullptr) || (iStructureOnly && !c->is_composite()))
                return;
            // (each element: an access value null, a composite initialized)
            auto const n = local(type::I64);
            auto const p = local(type::I64);
            array_length(aAddress, t->base_type()->view()->indexes.size());
            set(n);
            data_address(aAddress);
            set(p);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            get(n);
            op(opcode::I64Eqz);
            branch(exit, true);
            if (c->is_access())
            {
                get(p);
                i64(0);
                if (c->not_null)
                    range_check(c, nullptr, nullptr);   // (null: not of a null-excluding subtype)
                store64(0u);
            }
            else if (defaultValue != nullptr)
            {
                get(p);
                expression(defaultValue, c);
                store(kind_of(c), iProgram.scalar_size(c), 0u);
            }
            else
            {
                if (c->kind == type_kind::Array)
                    init_descriptor(c, p);
                default_initialize(c, p);
            }
            get(p);
            element_size(t);
            op(opcode::I64Add);
            set(p);
            get(n);
            i64(1);
            op(opcode::I64Sub);
            set(n);
            branch(head);
            end();
            end();
            return;
        }
        if (t->kind == type_kind::Record || t->kind == type_kind::Task || t->kind == type_kind::Protected || (t->kind == type_kind::Private && t->view() != t))
        {
            auto* base = t->base_type()->view();
            iProgram.layout(base);
            if (base->tagged)
            {
                get(aAddress);
                i64(static_cast<std::int64_t>(iProgram.tag_of(base)));
                store64(0u);
            }
            // (its components' expressions name its discriminants: this object's)
            iInstances.emplace_back(base, aAddress);
            auto const savedTail = iTail;
            if (base->dynamic_size)
            {
                iTail = local(type::I64);
                i64(static_cast<std::int64_t>(*base->size));
                set(iTail);
            }
            // (the discriminants: the subtype's constraint, else their defaults (unless they are set already))
            bool const discriminantsSet = iDiscriminantsSet;
            iDiscriminantsSet = false;
            std::size_t d = 0u;
            for (auto const* pc : iProgram.initialization_order(base))
            {
                auto const& c = *pc;
                if (!c.offset)
                    continue;
                node* value = nullptr;
                if (c.discriminant && discriminantsSet)
                {
                    ++d;
                    continue;
                }
                if (c.discriminant)
                {
                    if (d < t->discriminant_constraint.size())
                        value = t->discriminant_constraint[d];
                    ++d;
                    if (value == nullptr)
                        value = c.default_expression;
                }
                else if (!iStructureOnly || c.stored)
                    value = c.default_expression;
                auto* ct = c.type != nullptr ? c.type->view() : nullptr;
                if (ct == nullptr)
                    continue;
                if (ct->is_composite())
                {
                    auto const component = local(type::I64);
                    if (c.dynamic && ct->kind != type_kind::Array)
                    {
                        // (a dynamically sized record component: its object at the tail's end (its offset in place), its size
                        // its subtype's (the discriminants' values))
                        get(aAddress);
                        get(iTail);
                        store64(static_cast<std::uint32_t>(*c.offset));
                        get(aAddress);
                        get(iTail);
                        op(opcode::I64Add);
                        set(component);
                        get(iTail);
                        record_subtype_size(ct);
                        op(opcode::I64Add);
                        set(iTail);
                    }
                    else
                    {
                        get(aAddress);
                        i64(static_cast<std::int64_t>(*c.offset));
                        op(opcode::I64Add);
                        set(component);
                    }
                    if (ct->kind == type_kind::Array && c.depends)
                        component_bounds(base, c, aAddress, component);
                    else if (ct->kind == type_kind::Array)
                        component_descriptor(base, c, aAddress, component);
                    // (a component of a variant: initialized (and its constraint checked) if the discriminants select it)
                    if (c.variant >= 0)
                    {
                        variant_selected(base, c, aAddress);
                        begin_if();
                    }
                    if (ct->kind == type_kind::Array && c.depends)
                        index_compatibility_check(ct, component);
                    if (value != nullptr)
                    {
                        if (is_aggregate_node(value) && !(c.dynamic && ct->kind != type_kind::Array))
                        {
                            aggregate(value, ct, component);
                            drop();
                        }
                        else if (is_aggregate_node(value))
                        {
                            // (a dynamically sized record component: the aggregate's value (an object of its own) copied)
                            aggregate(value, ct);
                            auto const v = temporary(type::I64);
                            set(v);
                            discriminant_constraint_check(ct, v, value);
                            copy_whole(ct, component, v);
                            release(v);
                        }
                        else
                        {
                            // (a function's result: moved into the component; another object's value: copied and adjusted)
                            moved_result(value);
                            expression(value, ct);
                            auto const v = temporary(type::I64);
                            set(v);
                            if (ct->kind == type_kind::Record && ct->base_type()->view()->tagged)
                            {
                                get(component);
                                i64(static_cast<std::int64_t>(iProgram.tag_of(ct)));
                                store64(0u);
                            }
                            copy_value(ct, component, v);
                            release(v);
                            if (!is_temporary_value(value))
                                controlled_initialized(ct, component, false, true, false);
                        }
                    }
                    else
                        default_initialize(ct, component);
                    if (c.variant >= 0)
                        end();
                    continue;
                }
                if (value == nullptr && ct->is_scalar() && !iStructureOnly)
                    value = iProgram.default_value_of(ct, false);
                if (c.variant >= 0 && (value != nullptr || ct->is_access()))
                {
                    variant_selected(base, c, aAddress);
                    begin_if();
                }
                if (value != nullptr)
                {
                    get(aAddress);
                    expression(value, ct);
                    range_check(ct, value->type, value);
                    component_store(c);
                }
                else if (ct->is_access())
                {
                    get(aAddress);
                    i64(0);
                    if (ct->not_null)
                        range_check(ct, nullptr, nullptr);   // (null: not of a null-excluding subtype)
                    store64(static_cast<std::uint32_t>(*c.offset));
                }
                else if (auto invalid = iProgram.iNormalizeScalars && ct->is_scalar() && !iStructureOnly ? iProgram.invalid_value(ct) : std::nullopt)
                {
                    // (Normalize_Scalars: an invalid value (H.1(4)))
                    get(aAddress);
                    i64(*invalid);
                    component_store(c);
                }
                if (c.variant >= 0 && (value != nullptr || ct->is_access()))
                    end();
            }
            iInstances.pop_back();
            iTail = savedTail;
            // (a task or protected type with interfaces: its tag)
            if ((base->kind == type_kind::Task || base->kind == type_kind::Protected) &&
                (!base->interfaces.empty() || (base->partial != nullptr && !base->partial->interfaces.empty())))
            {
                get(aAddress);
                i64(static_cast<std::int64_t>(iProgram.tag_of(base)));
                store64(0u);
            }
            // (a task: created; a protected object: its runtime handle)
            if (iStructureOnly)
                ;
            else if (base->kind == type_kind::Task)
                task_object_init(base, aAddress);
            else if (base->kind == type_kind::Protected)
                protected_object_init(base, aAddress);
        }
    }

    void function_generator::component_descriptor(ada_type* aRecord, component_info const& aComponent, std::uint32_t aRecordAddress, std::uint32_t aComponentAddress)
    {
        // (an array component's descriptor: its subtype's bounds (a discriminant's value for a bound that is one))
        auto* ct = aComponent.type->view();
        (void)aRecord;
        (void)aRecordAddress;
        init_descriptor(ct, aComponentAddress);
    }

    void function_generator::string_literal(node* aLiteral, ada_type* aType)
    {
        // (a string literal: static data (a descriptor (1 .. n, or the index subtype's first) and its characters, of the
        // component's size))
        auto const text = literal_string(aLiteral);
        auto* t = aType != nullptr ? aType->view() : nullptr;
        std::uint64_t elementSize = 1u;
        std::int64_t first = 1;
        if (t != nullptr && t->kind == type_kind::Array)
        {
            elementSize = iProgram.component_size(t);
            if (t->constrained && !t->indexes.empty() && t->indexes[0]->view()->low.is_integer())
                first = t->indexes[0]->view()->low.integer().to_int64();
            else if (!t->indexes.empty() && t->indexes[0]->view()->low.is_integer())
                first = t->indexes[0]->view()->low.integer().to_int64();
        }
        std::string bytes;
        auto* component = t != nullptr && t->component != nullptr ? t->component->view()->base_type()->view() : nullptr;
        for (auto c : text)
        {
            std::uint64_t code = c;
            // (a user-defined character type: the literal's position)
            if (component != nullptr && component->character_type && !component->standard_character)
                for (std::size_t i = 0u; i < component->literals.size(); ++i)
                    if (component->literals[i]->name.size() == 3u && static_cast<unsigned char>(component->literals[i]->name[1]) == code)
                        code = i;
            for (std::uint64_t b = 0u; b < elementSize; ++b)
                bytes += static_cast<char>((code >> (8u * b)) & 0xFFu);
        }
        // (its characters: of the component subtype (4.2(11)))
        if (!text.empty() && t != nullptr && t->kind == type_kind::Array && t->component != nullptr && t->component->view() != component)
        {
            std::uint64_t least = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t greatest = 0u;
            for (std::size_t i = 0u; i < bytes.size(); i += elementSize)
            {
                std::uint64_t code = 0u;
                for (std::uint64_t b = 0u; b < elementSize; ++b)
                    code |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[i + b])) << (8u * b);
                least = std::min(least, code);
                greatest = std::max(greatest, code);
            }
            i64(static_cast<std::int64_t>(least));
            scalar_low(t->component);
            op(opcode::I64LtS);
            i64(static_cast<std::int64_t>(greatest));
            scalar_high(t->component);
            op(opcode::I64GtS);
            op(opcode::I32Ior);
            begin_if();
            raise_check(0, aLiteral, "range check failed");
            end();
        }
        // (a null literal whose lower bound is its index base type's first: its upper bound has no predecessor (4.2(11)))
        if (text.empty() && t != nullptr && t->kind == type_kind::Array && !t->constrained && !t->indexes.empty())
        {
            auto* index = t->indexes[0]->view();
            auto* indexBase = index->base_type()->view();
            if (indexBase->low.is_integer() && index->low.is_integer())
            {
                if (index->low.integer() == indexBase->low.integer())
                    raise_check(0, aLiteral, "range check failed");
            }
            else if (indexBase->low.is_integer() && indexBase->low.integer().fits_int64())
            {
                scalar_low(t->indexes[0]);
                i64(indexBase->low.integer().to_int64());
                op(opcode::I64Eq);
                begin_if();
                raise_check(0, aLiteral, "range check failed");
                end();
            }
        }
        auto const address = iProgram.static_string(bytes);
        if (t != nullptr && t->kind == type_kind::Array && t->constrained && !t->indexes.empty() && !t->indexes[0]->view()->low.is_integer())
        {
            // (of a constrained subtype of a dynamic lower bound (the applicable index constraint's, 4.3.3(11)): a descriptor of
            // its own (on the secondary stack))
            auto const length = static_cast<std::int64_t>(text.size());
            auto const descriptor = local(type::I64);
            i64(24);
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(descriptor);
            get(descriptor);
            i64(static_cast<std::int64_t>(address + 24u));
            get(descriptor);
            op(opcode::I64Sub);
            store64(0u);
            get(descriptor);
            scalar_low(t->indexes[0]);
            store64(8u);
            get(descriptor);
            get(descriptor);
            load64(8u);
            i64(length - 1);
            op(opcode::I64Add);
            store64(16u);
            get(descriptor);
            return;
        }
        if (first == 1 && elementSize == 1u)
        {
            i64(static_cast<std::int64_t>(address));
            return;
        }
        // (a descriptor of its own: its first and the length in elements)
        auto const length = static_cast<std::int64_t>(text.size());
        auto const descriptor = iProgram.static_data(24u, 8u);
        std::string image(24u, '\0');
        std::int64_t const data = static_cast<std::int64_t>(address + 24u) - static_cast<std::int64_t>(descriptor);
        std::int64_t const last = first + length - 1;
        std::memcpy(image.data(), &data, 8u);
        std::memcpy(image.data() + 8, &first, 8u);
        std::memcpy(image.data() + 16, &last, 8u);
        iProgram.add_static_bytes(descriptor, image);
        i64(static_cast<std::int64_t>(descriptor));
    }

    // --- conversions

    void function_generator::conversion(node* aOperand, ada_type* aSource, ada_type* aTarget, node* aWhere)
    {
        // (this conversion's rounding ('Round), not its operand's conversions')
        bool const rounding = iRounding;
        iRounding = false;
        auto* s = aSource != nullptr ? aSource->view() : nullptr;
        auto* d = aTarget->view();
        if (d->is_composite() || (s != nullptr && s->is_composite()))
        {
            // (a composite conversion: its address (an array: a descriptor of the target's bounds if it is constrained))
            if (is_aggregate_node(aOperand))
                aggregate(aOperand, aSource != nullptr ? aSource : aTarget);
            else
                expression(aOperand, aSource);
            // (from an unchecked union type to an unconstrained type that is not one, of a value that lacks inferable
            // discriminants: Program_Error (B.3.3(26/2)))
            if (s != nullptr && s->kind == type_kind::Record && d->kind == type_kind::Record && s->base_type()->view()->unchecked_union &&
                !d->base_type()->view()->unchecked_union && d->discriminant_constraint.empty() &&
                std::any_of(d->base_type()->view()->components.begin(), d->base_type()->view()->components.end(), [](component_info const& c) { return c.discriminant; }) &&
                !iProgram.inferable_discriminants(aOperand))
                raise_check(1, aWhere != nullptr ? aWhere : aOperand, "conversion of an unchecked union value without inferable discriminants");
            // (between untagged record types related by derivation whose representations differ: component by component
            // (13.6))
            if (s != nullptr && representations_differ(s, d))
            {
                auto* db = d->base_type()->view();
                auto const from = local(type::I64);
                set(from);
                auto const to = local(type::I64);
                i64(static_cast<std::int64_t>(std::max<std::uint64_t>(iProgram.object_size(db), 8u)));
                runtime("ss_allocate", { type::I64 }, type::I64);
                set(to);
                representation_copy(s, from, d, to, true);
                get(to);
            }
            else if (d->kind == type_kind::Array && d->constrained && s != nullptr && s->kind == type_kind::Array)
            {
                auto const value = local(type::I64);
                set(value);
                subtype_descriptor(d);
                auto const descriptor = local(type::I64);
                set(descriptor);
                length_check(descriptor, value, d->base_type()->view()->indexes.size(), aWhere);
                get(descriptor);
                data_address(value);
                get(descriptor);
                op(opcode::I64Sub);
                store64(0u);
                get(descriptor);
            }
            else if (d->kind == type_kind::Array && !d->constrained && s != nullptr && s->kind == type_kind::Array)
            {
                // (to an unconstrained array type: each non-null dimension's bounds in the target's index subtype (4.6(38)))
                auto const value = local(type::I64);
                tee(value);
                drop();
                auto const& indexes = d->base_type()->view()->indexes;
                for (std::size_t k = 0u; k < indexes.size(); ++k)
                {
                    bounds(value, k, true);
                    scalar_low(indexes[k]);
                    op(opcode::I64LtS);
                    bounds(value, k, false);
                    scalar_high(indexes[k]);
                    op(opcode::I64GtS);
                    op(opcode::I32Ior);
                    bounds(value, k, false);
                    bounds(value, k, true);
                    op(opcode::I64GeS);
                    op(opcode::I32And);
                    begin_if();
                    raise_check(0, aWhere, "range check failed");
                    end();
                }
                get(value);
            }
            else if (d->tagged && s != nullptr && (s->kind == type_kind::ClassWide || s->tagged) && !iProgram.same_type(d, s))
            {
                // (a view conversion to a specific tagged type: a tag check if from a class-wide type; to a class-wide type: if the
                // target's type is not an ancestor of the operand's (4.6(42)))
                auto* targetSpecific = d->kind == type_kind::ClassWide ? d->specific : d;
                auto* sourceSpecific = s->kind == type_kind::ClassWide ? s->specific : s;
                if (s->kind == type_kind::ClassWide && (d->kind != type_kind::ClassWide || !iProgram.is_descendant(sourceSpecific, targetSpecific)))
                {
                    auto const value = temporary(type::I64);
                    tee(value);
                    load64(0u);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(targetSpecific)));
                    runtime("is_descendant", { type::I64, type::I64 }, type::I32);
                    op(opcode::I32Eqz);
                    begin_if();
                    raise_check(0, aWhere, "tag check failed");
                    end();
                    get(value);
                    release(value);
                }
            }
            else if (d->kind == type_kind::Record && !d->discriminant_constraint.empty() && s != nullptr && s->kind == type_kind::Record &&
                iProgram.same_type(d->base_type(), s->base_type()))
            {
                // (to a subtype whose discriminants are constrained: a discriminant check (4.6(51/4)))
                auto const value = local(type::I64);
                tee(value);
                discriminant_constraint_check(d, value, aWhere);
            }
            return;
        }
        // (a ratio of smalls as a fraction of 64-bit integers (positive), if it is one)
        auto const fraction = [](rational const& aRatio, std::int64_t& aNumerator, std::int64_t& aDenominator)
        {
            if (aRatio.sign() <= 0 || !aRatio.numerator().fits_int64() || !aRatio.denominator().fits_int64())
                return false;
            aNumerator = aRatio.numerator().to_int64();
            aDenominator = aRatio.denominator().to_int64();
            return true;
        };
        auto const small_of = [](ada_type* aType) -> std::optional<rational>
        {
            // (a fixed point type's small; an integer type's: 1)
            auto* b = aType != nullptr ? aType->view()->base_type()->view() : nullptr;
            if (b == nullptr)
                return {};
            if (b->is_fixed() && b->kind != type_kind::UniversalFixed && b->small.sign() > 0)
                return b->small;
            if (b->is_integer() && !b->is_universal())
                return rational{ bigint{ 1 } };
            return {};
        };
        // (a product or quotient of fixed point (or integer) values converted to a fixed point or integer type: exact (4.5.5(19.1),
        // 4.6(31)), from the operands' counts)
        if (s != nullptr && d != nullptr && s->kind == type_kind::UniversalFixed && kind_of(d) == vkind::I64 && small_of(d))
            if (auto const* call = iProgram.call_of(aOperand); call != nullptr && call->callee != nullptr && call->callee->sub != nullptr &&
                (call->callee->sub->op == intrinsic::UniversalFixedMultiply || call->callee->sub->op == intrinsic::UniversalFixedDivide) &&
                call->actuals.size() == 2u && call->actuals[0] != nullptr && call->actuals[1] != nullptr)
            {
                auto const left = small_of(call->actuals[0]->type);
                auto const right = small_of(call->actuals[1]->type);
                bool const divide = call->callee->sub->op == intrinsic::UniversalFixedDivide;
                std::int64_t num = 0;
                std::int64_t den = 0;
                if (left && right && kind_of(call->actuals[0]->type) == vkind::I64 && kind_of(call->actuals[1]->type) == vkind::I64 &&
                    fraction(divide ? *left / (*right * *small_of(d)) : (*left * *right) / *small_of(d), num, den))
                {
                    expression(call->actuals[0], call->actuals[0]->type);
                    expression(call->actuals[1], call->actuals[1]->type);
                    i64(num);
                    i64(den);
                    i32(divide ? 1 : 0);
                    i32(d->base_type()->view()->kind == type_kind::Decimal && !rounding ? 1 : 0);
                    runtime("fixed_operation", { type::I64, type::I64, type::I64, type::I64, type::I32, type::I32 }, type::I64);
                    check();
                    range_check(aTarget, nullptr, aWhere);
                    return;
                }
            }
        expression(aOperand, aSource);
        if (s == nullptr || d == nullptr)
            return;
        // (scalar conversions: between numeric types (fixed point by their smalls; real to integer rounded), access types,
        // and derived types of the same representation)
        auto const sk = kind_of(s);
        auto const dk = kind_of(d);
        // (between fixed point (and integer) types: exact, by the ratio of their smalls)
        if (sk == vkind::I64 && dk == vkind::I64 && (s->base_type()->view()->is_fixed() || d->base_type()->view()->is_fixed()) && small_of(s) && small_of(d))
        {
            std::int64_t num = 0;
            std::int64_t den = 0;
            if (fraction(*small_of(s) / *small_of(d), num, den))
            {
                if (num != den)
                {
                    i64(num);
                    i64(den);
                    i32(d->base_type()->view()->kind == type_kind::Decimal && !rounding ? 1 : 0);
                    runtime("fixed_scale", { type::I64, type::I64, type::I64, type::I32 }, type::I64);
                    check();
                }
                if (d->is_scalar())
                    range_check(aTarget, nullptr, aWhere);
                return;
            }
        }
        bool const sFixed = s->base_type()->view()->is_fixed() && s->kind != type_kind::UniversalFixed;
        bool const dFixed = d->base_type()->view()->is_fixed();
        if (sFixed || dFixed)
        {
            // (through f64: value = count * small)
            double const sSmall = sFixed ? s->base_type()->view()->small.to_double() : 1.0;
            double const dSmall = dFixed ? d->base_type()->view()->small.to_double() : 1.0;
            if (sk == vkind::I64)
                op(opcode::F64SConvertI64);
            else if (sk == vkind::F32)
                op(opcode::F64ConvertF32);
            if (sFixed)
            {
                f64(sSmall);
                op(opcode::F64Mul);
            }
            if (dFixed)
            {
                f64(dSmall);
                op(opcode::F64Div);
            }
            if (dk == vkind::I64)
            {
                if (dFixed && d->base_type()->view()->kind == type_kind::Decimal && !rounding)
                    decimal_truncation();
                round_to_integer();
            }
            else if (dk == vkind::F32)
                op(opcode::F32ConvertF64);
        }
        else if ((sk == vkind::F32 || sk == vkind::F64) && dk == vkind::I64)
        {
            if (sk == vkind::F32)
                op(opcode::F64ConvertF32);
            round_to_integer();
        }
        else
            convert_representation(s, d);
        // (from an access-to-class-wide type to an access type whose designated type is not an ancestor of its: the designated
        // object's tag (unless null) checked (4.6(42)))
        if (s->kind == type_kind::Access && d->kind == type_kind::Access)
            if (auto* sd = iProgram.designated_of(s) != nullptr ? iProgram.designated_of(s)->view() : nullptr;
                sd != nullptr && sd->kind == type_kind::ClassWide)
                if (auto* dd = iProgram.designated_of(d) != nullptr ? iProgram.designated_of(d)->view() : nullptr; dd != nullptr && dd->tagged)
                {
                    auto* targetSpecific = dd->kind == type_kind::ClassWide ? dd->specific : dd;
                    if (dd->kind != type_kind::ClassWide || !iProgram.is_descendant(sd->specific, targetSpecific))
                    {
                        auto const value = temporary(type::I64);
                        tee(value);
                        op(opcode::I64Eqz);
                        op(opcode::I32Eqz);
                        begin_if();
                        get(value);
                        load64(0u);
                        i64(static_cast<std::int64_t>(iProgram.tag_of(targetSpecific)));
                        runtime("is_descendant", { type::I64, type::I64 }, type::I32);
                        op(opcode::I32Eqz);
                        begin_if();
                        raise_check(0, aWhere, "tag check failed");
                        end();
                        end();
                        get(value);
                        release(value);
                    }
                }
        if (s != nullptr && s->kind == type_kind::Access && d->kind == type_kind::Access)
            class_constraint_check(aTarget, aWhere);
        // (to a named access type: the designated object's accessibility level not deeper than its (4.6(48/3); static except an
        // access parameter's, and in an instance))
        if (s != nullptr && s->kind == type_kind::Access && d->kind == type_kind::Access && !d->anonymous_access && aOperand != nullptr)
        {
            auto const level = access_level(aOperand);
            if (!level || *level > 0)
                accessibility_check(level, iProgram.type_level(d), aWhere);
        }
        if (d->is_scalar() || d->kind == type_kind::Access || d->kind == type_kind::AccessSubprogram)
            range_check(aTarget, nullptr, aWhere);
    }

    void function_generator::decimal_truncation()
    {
        // (an f64 (a decimal result's count of smalls): truncated toward zero (4.6(31)), but a value within rounding error of
        // an integer is it)
        auto const x = temporary(type::F64);
        set(x);
        get(x);
        op(opcode::F64NearestInt);
        get(x);
        op(opcode::F64Trunc);
        get(x);
        get(x);
        op(opcode::F64NearestInt);
        op(opcode::F64Sub);
        op(opcode::F64Abs);
        get(x);
        op(opcode::F64Abs);
        f64(1.0);
        op(opcode::F64Max);
        f64(1e-9);
        op(opcode::F64Mul);
        op(opcode::F64Lt);
        op(opcode::Select);
        release(x);
    }

    void function_generator::round_to_integer()
    {
        // (an f64 rounded to the nearest integer (away from zero at halves) as an i64 (out of range: Constraint_Error))
        auto const v = temporary(type::F64);
        tee(v);
        f64(9.2233720368547758e18);
        op(opcode::F64Ge);
        get(v);
        f64(-9.2233720368547758e18);
        op(opcode::F64Lt);
        op(opcode::I32Ior);
        get(v);
        get(v);
        op(opcode::F64Ne);
        op(opcode::I32Ior);
        begin_if();
        raise_check(0, iWhere, "overflow check failed");
        end();
        // (exactly: its integral part, plus one away from zero if its fraction is at least a half (adding a half could round up a
        // value just below one))
        auto const t = temporary(type::F64);
        get(v);
        op(opcode::F64Trunc);
        set(t);
        get(t);
        f64(1.0);
        get(v);
        op(opcode::F64CopySign);
        op(opcode::F64Add);
        get(t);
        get(v);
        get(t);
        op(opcode::F64Sub);
        op(opcode::F64Abs);
        f64(0.5);
        op(opcode::F64Ge);
        op(opcode::Select);
        // (within the range of i64 (checked above): the plain conversion (it cannot trap))
        op(opcode::I64SConvertF64);
        release(t);
        release(v);
    }

    // --- calls

    bool function_generator::volatile_type(ada_type* aType, int aDepth)
    {
        // (a type specified (or with a subcomponent specified) Volatile or Atomic: a by-reference type (C.6(18/3)))
        if (aType == nullptr || aDepth > 16)
            return false;
        for (auto const* aspect : { "volatile", "atomic", "volatile_components", "atomic_components" })
            if (iProgram.type_aspect(aType, aspect) != nullptr)
                return true;
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::Array)
            return volatile_type(t->component, aDepth + 1);
        if (t->kind == type_kind::Record)
            for (auto const& c : t->components)
                if (volatile_type(c.type, aDepth + 1))
                    return true;
        return false;
    }

    bool function_generator::volatile_actual(node* aActual)
    {
        // (a part of a volatile object, or a component of an object with volatile components (C.6))
        bool component = false;
        for (auto* n = aActual; n != nullptr;)
        {
            if (n->ent != nullptr && n->ent->is_object() && n->ent->kind != entity_kind::Component)
            {
                auto* e = n->ent;
                while (e->renamed != nullptr && e->renamed != e)
                    e = e->renamed;
                return e->volatile_object || n->ent->volatile_object || (component && (e->volatile_components || n->ent->volatile_components));
            }
            if ((n->is(construct::Apply) || n->is(construct::Selected)) && n->size() > 0u)
            {
                component = true;
                n = n->child(0);
                continue;
            }
            break;
        }
        return false;
    }

    entity* function_generator::ultimate(entity* aSubprogram)
    {
        auto* s = aSubprogram;
        for (int i = 0; i < 64 && s != nullptr && s->sub != nullptr && s->sub->renames != nullptr && s->sub->renames != s; ++i)
        {
            if (s->sub->body != nullptr && !s->sub->body->is(construct::RenamingSubprogram) && s->sub->defined)
                break;
            s = s->sub->renames;
        }
        // (an inherited operation: the operation it inherits (or the one overriding it in a private part); a null extension's
        // function with a controlling result has a body of its own)
        for (int i = 0; i < 64 && s != nullptr && s->sub != nullptr && s->sub->inherited && !(s->sub->null_extension_result && s->sub->overridden_by == nullptr) &&
            (s->sub->overridden_by != nullptr || s->sub->parent_operation != nullptr); ++i)
            s = s->sub->overridden_by != nullptr ? s->sub->overridden_by : s->sub->parent_operation;
        if (s != nullptr && s->sub != nullptr && s->sub->renames != nullptr && s->sub->renames != s && !s->sub->defined)
            return ultimate(s->sub->renames);
        return s;
    }

    void function_generator::static_link_for(entity* aCallee)
    {
        auto const level = aCallee->sub != nullptr ? aCallee->sub->level : 1;
        if (level <= 1)
        {
            i64(0);
            return;
        }
        frame_of_level(level - 1);
    }

    void function_generator::call_function(entity* aCallee)
    {
        if (aCallee->sub == nullptr || !aCallee->sub->defined)
        {
            if (aCallee->sub != nullptr && aCallee->sub->body == nullptr && !aCallee->sub->is_abstract)
                iProgram.error(iWhere, "subprogram \"" + aCallee->name + "\" has no body");
            iProgram.define_subprogram(aCallee);
        }
        call(aCallee->sub->function_index);
    }

    void function_generator::call_expression(node* aCall, bool aStatement)
    {
        auto const* info = iProgram.call_of(aCall);
        if (info == nullptr || info->callee == nullptr)
            iProgram.error(aCall, "internal error: unresolved call");
        auto* declared = info->callee;
        auto* callee = ultimate(declared);
        // (an operation of a private extension inherited from its ancestor: the body is the full type's (inherited from its
        // parent, perhaps overridden there) (7.3.1(6), 3.4(27)))
        if (declared->sub != nullptr && declared->sub->inherited && declared->sub->slot >= 0 && declared->sub->controlling != nullptr)
            if (auto* partial = declared->sub->controlling; partial->kind == type_kind::Private && partial->full != nullptr)
            {
                auto* fv = partial->full->base_type()->view();
                auto const slot = static_cast<std::size_t>(declared->sub->slot);
                if (slot < fv->slots.size() && fv->slots[slot].operation != nullptr && fv->slots[slot].operation != declared)
                    callee = ultimate(fv->slots[slot].operation);
            }
        // (a tag-indeterminate call in a default expression of a controlling formal: the operation of the call's controlling type
        // (an inherited operation's default calls its type's operation, AI95-00239))
        if (auto* dc = iDefaultControlling; dc != nullptr && iDispatchTagAddress == 0u && declared->sub != nullptr && declared->sub->slot >= 0 && declared->sub->controlling != nullptr &&
            declared->sub->result != nullptr && iProgram.same_type(declared->sub->result, declared->sub->controlling) &&
            std::all_of(info->actuals.begin(), info->actuals.end(), [](node* a) { return a == nullptr; }))
        {
            auto* t = dc->view()->base_type()->view();
            auto const slot = static_cast<std::size_t>(declared->sub->slot);
            if (slot < t->slots.size() && t->slots[slot].operation != nullptr && iProgram.is_descendant(t, declared->sub->controlling))
                callee = ultimate(t->slots[slot].operation);
        }
        struct no_default_controlling
        {
            ada_type*& where;
            ada_type* saved;
            explicit no_default_controlling(ada_type*& aWhere) : where{ aWhere }, saved{ aWhere } { where = nullptr; }
            ~no_default_controlling() { where = saved; }
        } const noDefaultControlling{ iDefaultControlling };
        // (a default expression of a controlling formal: generated with the call's controlling type)
        auto const defaultControlling = [&](parameter_info const& aFormal) -> ada_type*
        {
            auto* c = declared->sub != nullptr ? declared->sub->controlling : nullptr;
            return c != nullptr && aFormal.type != nullptr && iProgram.same_type(aFormal.type, c) ? c : nullptr;
        };
        // (a renaming of an entry or protected subprogram of an object (or an entry family member): the renamed name's object)
        call_info renamedCall;
        if (info->object == nullptr)
            for (auto* r = declared; r != nullptr && r->sub != nullptr && r->sub->renames != nullptr && r->sub->renames != r; r = r->sub->renames)
                if (auto const* ri = r->sub->renamed_name != nullptr ? iProgram.call_of(r->sub->renamed_name) : nullptr;
                    ri != nullptr && (ri->object != nullptr || ri->family_index != nullptr) && !ri->prefixed)
                {
                    renamedCall = *info;
                    renamedCall.object = ri->object;
                    renamedCall.family_index = ri->family_index;
                    // (the renaming's defaults, not the renamed entity's (8.5.4(8)): given as actuals)
                    renamedCall.actuals.resize(declared->sub->parameters.size(), nullptr);
                    for (std::size_t k = 0u; k < renamedCall.actuals.size(); ++k)
                        if (renamedCall.actuals[k] == nullptr)
                            renamedCall.actuals[k] = declared->sub->parameters[k].default_expression;
                    info = &renamedCall;
                    break;
                }
        auto const* previous = iWhere;
        iWhere = aCall;
        // (an enumeration literal (renamed as a function))
        if (callee->kind == entity_kind::EnumerationLiteral)
        {
            constant_value(callee->value, callee->type);
            iWhere = previous;
            return;
        }
        if (callee->kind == entity_kind::Entry || (callee->scope != nullptr && callee->scope->owner != nullptr && callee->scope->owner->kind == entity_kind::Protected &&
            callee->is_subprogram()))
        {
            if (tasking_call(aCall, *info, callee, aStatement))
            {
                iWhere = previous;
                return;
            }
        }
        auto& sub = *callee->sub;
        // (a function whose result has tasks, not an object's initial value nor an allocator's: its result's tasks activated after
        // it returns (9.2(2/2)), those created since)
        std::optional<std::uint32_t> taskMark;
        if (sub.result != nullptr && iProgram.needs_tasks(sub.result) && !aStatement)
        {
            auto const* parent = aCall->parent;
            bool const initial = parent != nullptr && (parent->is(construct::Object) || parent->is(construct::Qualified) ||
                parent->is(construct::ReturnExtended) || parent->is(construct::Return));
            if (!initial)
            {
                taskMark = local(type::I64);
                runtime("task_pending", {}, type::I64);
                set(*taskMark);
            }
        }
        auto const& formals = declared->sub->parameters;
        // (a renaming's (or generic formal subprogram's) call: the renamed subprogram's parameter subtypes apply too (8.5.4(7/1),
        // 12.6(10/2)))
        auto const renamed_subtype = [&](std::size_t aIndex) -> ada_type*
        {
            if (declared == callee || callee->sub == nullptr || callee->sub->parameters.size() != formals.size())
                return nullptr;
            auto* t = callee->sub->parameters[aIndex].type;
            return t != formals[aIndex].type ? t : nullptr;
        };
        if (sub.op == intrinsic::NotEqual)
        {
            // (an implicit "/=": its "=" called, its result's complement)
            auto* equality = callee;
            for (int k = 0; k < 64 && equality->sub->complement_of == nullptr && equality->sub->parent_operation != nullptr; ++k)
                equality = equality->sub->parent_operation;
            auto& call = const_cast<call_info&>(*info);
            call.callee = equality->sub->complement_of;
            call_expression(aCall, false);
            call.callee = declared;
            op(opcode::I64Eqz);
            op(opcode::I64UConvertI32);
            if (aStatement)
                drop();
            iWhere = previous;
            return;
        }
        if (sub.op != intrinsic::None && sub.op != intrinsic::Import && sub.op != intrinsic::Attribute)
        {
            intrinsic_call(callee, info->actuals, aCall->type, aCall);
            if (aStatement && aCall->type != nullptr && aCall->type != iProgram.void_type())
                drop();
            iWhere = previous;
            return;
        }
        if (sub.op == intrinsic::Attribute)
        {
            attribute_call(callee, info->actuals, aCall);
            iWhere = previous;
            return;
        }
        bool const imported = sub.op == intrinsic::Import;
        if (imported && sub.import_name == "neos::tag_table")
        {
            // (the cell that holds the address of the table of the program's tags (Ada.Tags))
            i64(static_cast<std::int64_t>(iProgram.tag_table_cell()));
            if (aStatement)
                drop();
            iWhere = previous;
            return;
        }
        // (out and in out scalars: copies in the frame (their addresses passed), copied back after the call)
        struct copy_back
        {
            node* actual;
            ada_type* type;
            std::uint64_t offset;
            std::uint32_t address;  // (the actual's address (a name other than a variable's or a conversion: evaluated once))
            component_info const* bitField = nullptr;   // (... a record component that is a bit field: its first storage unit's)
        };
        std::vector<copy_back> copies;
        // (actuals passed to an inherited subprogram with a change of representation: copied back after the call)
        struct representation_back
        {
            ada_type* actualType;
            std::uint32_t actual;
            ada_type* formalType;
            std::uint32_t copy;
        };
        std::vector<representation_back> representationCopies;
        // (volatile actuals passed by copy (C.6(19/3)): the actual's address, the copy's, the size; copied back after the call)
        struct volatile_back
        {
            std::uint32_t actual;
            std::uint32_t copy;
            std::uint64_t size;
        };
        std::vector<volatile_back> volatileCopies;
        bool const indirect = (aCall->mark & 0x800u) != 0u;
        std::uint32_t target = 0u;
        if (indirect)
        {
            // (an access-to-subprogram value: its descriptor (function index, static link))
            expression(info->object, info->object->type);
            null_check(aCall);
            target = local(type::I64);
            set(target);
        }
        bool const dispatching = info->dispatching && sub.slot >= 0;
        if (dispatching && !imported && !indirect && sub.result != nullptr && callee->name == "=" && formals.size() == 2u && !by_copy(formals[0]) &&
            !by_copy(formals[1]) && info->actuals.size() == 2u && info->actuals[0] != nullptr && info->actuals[1] != nullptr && sub.controlling != nullptr &&
            sub.controlling->view()->kind != type_kind::Interface)
        {
            // (a dispatching "=": operands with different tags are not equal (3.9.2(16)); else the operation of their tag)
            std::uint32_t operands[2];
            for (std::size_t i = 0u; i < 2u; ++i)
            {
                if (is_aggregate_node(info->actuals[i]))
                    aggregate(info->actuals[i], formals[i].type);
                else
                    address(info->actuals[i]);
                operands[i] = local(type::I64);
                set(operands[i]);
            }
            get(operands[0]);
            load64(0u);
            get(operands[1]);
            load64(0u);
            op(opcode::I64Eq);
            begin_if(wasm_of(sub.result));
            get(operands[0]);
            load64(0u);
            load64(static_cast<std::uint32_t>(program::TagLink));
            get(operands[0]);
            get(operands[1]);
            get(operands[0]);
            load64(0u);
            load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(sub.slot)));
            op(opcode::I32ConvertI64);
            call_indirect({ type::I64, type::I64, type::I64 }, wasm_of(sub.result));
            else_();
            i64(0);
            end();
            check();
            if (aStatement)
                drop();
            iWhere = previous;
            return;
        }
        std::uint32_t tag = 0u;
        // (the first controlling operand (its tag dispatches): a formal of the controlling type, or an access parameter designating it)
        std::size_t controllingIndex = 0u;
        if (dispatching && sub.controlling != nullptr)
            for (std::size_t i = 0u; i < formals.size(); ++i)
            {
                auto* ft = formals[i].type;
                if (ft == nullptr)
                    continue;
                if (iProgram.same_type(ft, sub.controlling) ||
                    (ft->view()->kind == type_kind::Access && ft->view()->anonymous_access && ft->view()->designated != nullptr &&
                     iProgram.same_type(ft->view()->designated, sub.controlling)))
                {
                    controllingIndex = i;
                    break;
                }
            }
        // (a dispatching call whose controlling operand is passed by reference: that operand first (its tag gives the static
        // link of the operation called: the one its type's declaration was elaborated with))
        bool const controllingFirst = dispatching && !imported && !indirect && !formals.empty() && !by_copy(formals[0]) && controllingIndex == 0u;
        // (a tag-indeterminate call (of a function with a controlling result and no controlling operands): dispatching on the
        // controlling tag of the enclosing call (3.9.2(19)); this call's: dispatching on its enclosing call's)
        bool const redispatch = iDispatchTagAddress != 0u && !dispatching && !imported && !indirect && sub.slot >= 0 && sub.result != nullptr &&
            sub.controlling != nullptr && iProgram.same_type(sub.result, sub.controlling);
        auto const dispatchTagAddress = iDispatchTagAddress;
        iDispatchTagAddress = 0u;
        struct restore_dispatch { std::uint32_t& target; std::uint32_t value; ~restore_dispatch() { target = value; } } restoreDispatch{ iDispatchTagAddress, dispatchTagAddress };
        auto const indeterminate = [&](node* aActual)
        {
            if (aActual == nullptr || meaning_of(aActual) != meaning::Call)
                return false;
            auto const* c = iProgram.call_of(aActual);
            if (c == nullptr || c->callee == nullptr || c->dispatching || c->callee->sub == nullptr)
                return false;
            auto const& s = *c->callee->sub;
            return s.slot >= 0 && s.result != nullptr && s.controlling != nullptr && iProgram.same_type(s.result, s.controlling);
        };
        // (the determining operand: a controlling operand that is not tag-indeterminate, evaluated first if others are)
        std::uint32_t determinant = 0u;
        std::size_t determinantIndex = static_cast<std::size_t>(-1);
        if (dispatching && !imported && !indirect && sub.controlling != nullptr)
        {
            bool anyIndeterminate = false;
            for (std::size_t i = 0u; i < formals.size() && i < info->actuals.size(); ++i)
                if (iProgram.same_type(formals[i].type, sub.controlling) && indeterminate(info->actuals[i]))
                    anyIndeterminate = true;
            if (anyIndeterminate)
                for (std::size_t i = 0u; i < formals.size() && i < info->actuals.size(); ++i)
                    if (iProgram.same_type(formals[i].type, sub.controlling) && !by_copy(formals[i]) && info->actuals[i] != nullptr &&
                        !indeterminate(info->actuals[i]) && !is_aggregate_node(info->actuals[i]))
                    {
                        address(info->actuals[i]);
                        determinant = local(type::I64);
                        set(determinant);
                        determinantIndex = i;
                        break;
                    }
        }
        if (controllingFirst)
        {
            if (determinant != 0u)
                iDispatchTagAddress = determinant;
            node* actual = !info->actuals.empty() ? info->actuals[0] : nullptr;
            bool const fromObject = info->prefixed && info->object != nullptr && actual == info->object;
            auto const& f = formals[0];
            if (actual == nullptr)
            {
                iDefaultControlling = defaultControlling(f);
                if (f.default_expression != nullptr && is_aggregate_node(f.default_expression))
                    aggregate(f.default_expression, f.type);
                else
                    expression(f.default_expression, f.type);
                iDefaultControlling = nullptr;
            }
            else if (fromObject && (aCall->mark & 0x400u))
            {
                expression(actual, actual->type);
                null_check(aCall);     // (the prefixed view's object: implicitly dereferenced (4.1.3(15)))
            }
            else if (determinantIndex == 0u)
                get(determinant);
            else if (is_aggregate_node(actual))
                aggregate(actual, f.type);
            else
                address(actual);
            iDispatchTagAddress = 0u;
            tag = local(type::I64);
            set(tag);
        }
        if (!imported)
        {
            if (indirect)
            {
                get(target);
                load64(8u);
            }
            else if (controllingFirst)
            {
                get(tag);
                load64(0u);
                load64(static_cast<std::uint32_t>(program::TagLink));
            }
            else if (redispatch)
            {
                get(dispatchTagAddress);
                load64(0u);
                load64(static_cast<std::uint32_t>(program::TagLink));
            }
            else
                static_link_for(callee);
        }
        // (a protected operation: its object (the prefix's, or (an internal call) this operation's own))
        auto* selfType = imported ? nullptr : iProgram.self_type(callee);
        std::optional<std::uint32_t> externalSelf;
        // (a call through an access-to-protected-subprogram value: its object (the descriptor's third word))
        bool const protectedIndirect = indirect && info->object != nullptr && info->object->type != nullptr &&
            info->object->type->view()->kind == type_kind::AccessSubprogram && info->object->type->view()->protected_access;
        if (protectedIndirect)
        {
            get(target);
            load64(16u);
            externalSelf = local(type::I64);
            tee(*externalSelf);
        }
        if (selfType != nullptr)
        {
            if (info->object != nullptr)
            {
                if (info->object->type != nullptr && info->object->type->view()->is_access())
                {
                    expression(info->object, info->object->type);
                    null_check(aCall);
                }
                else
                    address(info->object);
                externalSelf = local(type::I64);
                tee(*externalSelf);
            }
            else if (iHasSelf)
            {
                if (iLevel == (callee->scope->owner->inner != nullptr ? callee->scope->owner->inner->level + 1 : iLevel))
                    get(iSelf);
                else
                {
                    frame_of_level(callee->scope->owner->inner->level + 1);
                    load64(8u);
                }
            }
            else
            {
                frame_of_level(callee->scope->owner->inner->level + 1);
                load64(8u);
            }
        }
        std::vector<type> parameterTypes;
        for (std::size_t i = 0u; i < formals.size(); ++i)
        {
            auto const& f = formals[i];
            node* actual = i < info->actuals.size() ? info->actuals[i] : nullptr;
            bool const fromObject = i == 0u && info->prefixed && info->object != nullptr && actual == info->object;
            if (i == 0u && controllingFirst)
            {
                get(tag);
                parameterTypes.push_back(type::I64);
                continue;
            }
            if (by_copy(f))
            {
                if (actual == nullptr)
                {
                    iDefaultControlling = defaultControlling(f);
                    expression(f.default_expression, f.type);
                    iDefaultControlling = nullptr;
                    range_check(f.type, f.default_expression->type, f.default_expression);
                    if (auto* rt = renamed_subtype(i); rt != nullptr && (rt->view()->is_scalar() || rt->view()->is_access()))
                        range_check(rt, f.type, f.default_expression);
                }
                else if (fromObject && (aCall->mark & 0x200u))
                    address(actual);
                else if (fromObject && (aCall->mark & 0x400u))
                {
                    expression(actual, actual->type);
                    null_check(aCall);
                    load(kind_of(f.type), iProgram.scalar_size(f.type), 0u, !is_unsigned(f.type));
                }
                else
                {
                    if (f.type != nullptr && f.type->view()->kind == type_kind::Access && f.type->view()->anonymous_access && actual->type != nullptr &&
                        !actual->type->view()->is_access())
                        address(actual);
                    else
                    {
                        expression(actual, f.type);
                        // (a prefixed view's object of an access type: implicitly dereferenced (then its 'Access) (4.1.3(15)))
                        if (fromObject && actual->type != nullptr && actual->type->view()->is_access())
                            null_check(aCall);
                    }
                    range_check(f.type, actual->type, actual);
                    if (auto* rt = renamed_subtype(i); rt != nullptr && (rt->view()->is_scalar() || rt->view()->is_access()))
                        range_check(rt, f.type, actual);
                }
                auto const t = imported && kind_of(f.type) == vkind::F32 ? type::F64 : wasm_of(f.type);
                if (imported && kind_of(f.type) == vkind::F32)
                    op(opcode::F64ConvertF32);
                parameterTypes.push_back(t);
                if (dispatching && i == controllingIndex && tag == 0u && f.type != nullptr && f.type->view()->is_access())
                {
                    // (a controlling access parameter: the object it designates (its tag))
                    tag = local(type::I64);
                    tee(tag);
                }
                if (f.mode == parameter_mode::Access && f.type != nullptr && f.type->view()->kind == type_kind::Access && !imported &&
                    passes_levels(callee))
                {
                    // (an access parameter: the actual's accessibility level passed (3.10.2(13/3)))
                    std::optional<std::int64_t> level = 0;
                    if (actual == nullptr)
                        level = access_level(f.default_expression);
                    else if (fromObject && (aCall->mark & 0x200u))
                        level = object_level(actual);
                    else if (f.type->view()->anonymous_access && actual->type != nullptr && !actual->type->view()->is_access())
                        level = object_level(actual);
                    else
                        level = access_level(actual);
                    if (!level || *level != 0)
                    {
                        if (level)
                            i64(std::min<std::int64_t>(*level, 0x3FFF) << LevelShift);
                        else
                        {
                            i64(LevelShift);
                            op(opcode::I64Shl);
                        }
                        op(opcode::I64Ior);
                    }
                }
                continue;
            }
            auto* ft = f.type != nullptr ? f.type->view() : nullptr;
            if (ft != nullptr && !ft->is_composite() && f.mode != parameter_mode::In)
            {
                // (a scalar out or in out parameter: a copy)
                iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                auto const offset = iFrameSize;
                iFrameSize += 8u;
                std::uint32_t actualAddress = 0u;
                component_info const* actualBitField = nullptr;
                if (actual != nullptr)
                {
                    auto* a = actual;
                    while (a->is(construct::Parenthesized))
                        a = a->child(0);
                    if (!a->is(construct::Identifier) && meaning_of(a) != meaning::Conversion && meaning_of(a) != meaning::Object)
                    {
                        address(a);
                        actualAddress = local(type::I64);
                        set(actualAddress);
                        actualBitField = bit_field_name(a);
                    }
                }
                // (in out; out of an access type or a scalar type with Default_Value: the actual's value passed in (6.4.1(13/3, 13.1/4)),
                // unchecked for out)
                bool const passedIn = f.mode == parameter_mode::InOut || (f.mode == parameter_mode::Out && ft != nullptr &&
                    (ft->is_access() || (ft->is_scalar() && iProgram.default_value_of(ft, false) != nullptr)));
                // (Normalize_Scalars: an out scalar formal's initial value invalid (H.1(4)))
                if (!passedIn && f.mode == parameter_mode::Out && iProgram.iNormalizeScalars && ft != nullptr && ft->is_scalar())
                    if (auto invalid = iProgram.invalid_value(f.type))
                    {
                        get(iFrame);
                        i64(*invalid);
                        store(kind_of(f.type), iProgram.scalar_size(f.type), static_cast<std::uint32_t>(offset));
                    }
                if (passedIn && actual != nullptr)
                {
                    get(iFrame);
                    if (actualAddress != 0u)
                    {
                        get(actualAddress);
                        if (actualBitField != nullptr)
                            bit_field_load(*actualBitField);
                        else
                            load(kind_of(actual->type), iProgram.scalar_size(actual->type), 0u, !is_unsigned(actual->type));
                    }
                    else if (f.mode == parameter_mode::Out && actual->is(construct::Apply) && meaning_of(actual) == meaning::Conversion && actual->size() > 1u)
                        expression(actual->child(1), actual->child(1)->type);   // (out: without checking the conversion's constraint)
                    else
                        expression(actual, actual->type);
                    if (f.mode == parameter_mode::InOut)
                    {
                        range_check(f.type, actual->type, actual);     // (in out: the actual's value of the formal's subtype)
                        if (auto* rt = renamed_subtype(i); rt != nullptr && (rt->view()->is_scalar() || rt->view()->is_access()))
                            range_check(rt, f.type, actual);
                    }
                    store(kind_of(f.type), iProgram.scalar_size(f.type), static_cast<std::uint32_t>(offset));
                }
                get(iFrame);
                i64(static_cast<std::int64_t>(offset));
                op(opcode::I64Add);
                if (actual != nullptr)
                    copies.push_back(copy_back{ actual, f.type, offset, actualAddress, actualBitField });
                parameterTypes.push_back(type::I64);
                continue;
            }
            // (by reference: the actual's address (an array's descriptor; a value: its temporary))
            if (actual == nullptr)
            {
                iDefaultControlling = defaultControlling(f);
                // (a dispatching call: a tag-indeterminate default dispatching on its controlling tag (3.9.2(19)))
                if (iDefaultControlling != nullptr && dispatching)
                    iDispatchTagAddress = determinant != 0u ? determinant : controllingFirst ? tag : 0u;
                if (f.default_expression != nullptr && is_aggregate_node(f.default_expression))
                    aggregate(f.default_expression, f.type);
                else
                    expression(f.default_expression, f.type);
                iDefaultControlling = nullptr;
                iDispatchTagAddress = 0u;
                if (ft != nullptr && !ft->is_composite())
                    materialize(f.type);
            }
            else if (fromObject && (aCall->mark & 0x400u))
            {
                expression(actual, actual->type);
                null_check(aCall);     // (the prefixed view's object: implicitly dereferenced (4.1.3(15)))
            }
            else if (i == determinantIndex)
                get(determinant);
            else if (is_aggregate_node(actual))
                aggregate(actual, f.type);
            else if (ft != nullptr && ft->is_composite())
            {
                if (determinant != 0u && indeterminate(actual))
                    iDispatchTagAddress = determinant;
                else if (dispatching && controllingFirst && indeterminate(actual))
                    iDispatchTagAddress = tag;
                address(actual);
                iDispatchTagAddress = 0u;
            }
            else
                address(actual);
            // (a default expression's too)
            if (auto* a = actual != nullptr ? actual : f.default_expression; a != nullptr && ft != nullptr && ft->is_composite())
            {
                composite_actual(f.type, a);
                if (auto* rt = renamed_subtype(i); rt != nullptr && rt->view()->is_composite())
                    composite_actual(rt, a);
            }
            // (an in or in out actual: of the formal's predicate)
            if (actual != nullptr && f.mode != parameter_mode::Out && ft != nullptr && ft->is_composite())
                predicate_check(f.type, actual->type, actual);
            // (an untagged derived type's inherited subprogram whose parent type's representation differs: the parent's subprogram
            // called with the actual converted to it (13.6), copied back if it is out or in out)
            if (actual != nullptr && callee != declared && callee->sub != nullptr && i < callee->sub->parameters.size() && actual->type != nullptr &&
                ft != nullptr && ft->is_composite() && !is_aggregate_node(actual) && i != determinantIndex)
                if (auto* pt = callee->sub->parameters[i].type; pt != nullptr && representations_differ(actual->type, pt))
                {
                    auto const from = local(type::I64);
                    set(from);
                    auto const to = local(type::I64);
                    i64(static_cast<std::int64_t>(std::max<std::uint64_t>(iProgram.object_size(pt->view()->base_type()->view()), 8u)));
                    runtime("ss_allocate", { type::I64 }, type::I64);
                    set(to);
                    representation_copy(actual->type, from, pt, to, true);
                    get(to);
                    if (f.mode != parameter_mode::In)
                        representationCopies.push_back({ actual->type, from, pt, to });
                }
            // (a volatile actual (a part of a volatile object, or a component of an object with volatile components) of a type that is
            // not a by-reference type, for a formal that is not volatile: passed by copy (C.6(19/3)))
            if (actual != nullptr && ft != nullptr && ft->kind == type_kind::Record && !ft->tagged && !limited_type(ft) &&
                !iProgram.needs_finalization(ft) && !ft->base_type()->view()->dynamic_size && !volatile_type(ft) && volatile_actual(actual) &&
                !(f.ent != nullptr && f.ent->volatile_object) && i != determinantIndex)
                if (auto const size = iProgram.object_size(ft); size != 0u)
                {
                    auto const from = local(type::I64);
                    set(from);
                    auto const to = local(type::I64);
                    i64(static_cast<std::int64_t>(size));
                    runtime("ss_allocate", { type::I64 }, type::I64);
                    set(to);
                    get(to);
                    get(from);
                    i64(static_cast<std::int64_t>(size));
                    runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                    get(to);
                    if (f.mode != parameter_mode::In)
                        volatileCopies.push_back({ from, to, size });
                }
            // (an out or in out formal of a mutable record type: whether the actual is constrained (3.7.2(4)))
            if (actual != nullptr && !imported && f.mode != parameter_mode::In && mutable_record(f.type) && !is_aggregate_node(actual) &&
                !(fromObject && (aCall->mark & 0x400u)) && i != determinantIndex)
            {
                unconstrained_view(actual);
                i64(UnconstrainedBit);
                op(opcode::I64Shl);
                op(opcode::I64Ior);
            }
            if (dispatching && i == controllingIndex)
            {
                tag = local(type::I64);
                tee(tag);
                op(opcode::Drop);
                get(tag);
            }
            parameterTypes.push_back(type::I64);
        }
        // (an external call of a protected operation: its protected action begins (the caller's priority within its ceiling, D.3(13)))
        if (externalSelf)
        {
            get(*externalSelf);
            runtime("po_enter", { type::I64 }, type::Void);
            check();
        }
        if (imported)
        {
            auto const resultType = sub.result != nullptr ? (kind_of(sub.result) == vkind::F32 ? type::F64 : wasm_of(sub.result)) : type::Void;
            auto name = sub.import_name;
            if (name.find("::") == std::string::npos)
                name = "ada::" + name;
            call(iProgram.runtime(name, parameterTypes, resultType));
            if (sub.result != nullptr && kind_of(sub.result) == vkind::F32)
                op(opcode::F32ConvertF64);
        }
        else if (indirect)
        {
            std::vector<type> signature = { type::I64 };
            if (protectedIndirect)
                signature.push_back(type::I64);
            signature.insert(signature.end(), parameterTypes.begin(), parameterTypes.end());
            get(target);
            load64(0u);
            op(opcode::I32ConvertI64);
            call_indirect(signature, sub.result != nullptr ? wasm_of(sub.result) : type::Void);
        }
        else if (dispatching)
        {
            // (the controlling operand's tag: its dispatch table's slot (an interface's operation: its implementation's, from the
            // tag's interface table))
            std::vector<type> signature = { type::I64 };
            signature.insert(signature.end(), parameterTypes.begin(), parameterTypes.end());
            auto* controlling = sub.controlling != nullptr ? sub.controlling->view()->base_type()->view() : nullptr;
            if (info->contract_dispatching && iContractType != nullptr)
                i64(static_cast<std::int64_t>(iProgram.tag_of(iContractType)));
            else
            {
                get(tag);
                load64(0u);
            }
            if (controlling != nullptr && controlling->kind == type_kind::Interface)
            {
                i64(static_cast<std::int64_t>(iProgram.tag_of(controlling)));
                i64(sub.slot);
                runtime("interface_function", { type::I64, type::I64, type::I64 }, type::I64);
            }
            else
                load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(sub.slot)));
            op(opcode::I32ConvertI64);
            denote(callee);
            call_indirect(signature, sub.result != nullptr ? wasm_of(sub.result) : type::Void);
        }
        else if (redispatch)
        {
            // (the operation of the enclosing call's controlling tag)
            std::vector<type> signature = { type::I64 };
            signature.insert(signature.end(), parameterTypes.begin(), parameterTypes.end());
            get(dispatchTagAddress);
            load64(0u);
            load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(sub.slot)));
            op(opcode::I32ConvertI64);
            denote(callee);
            call_indirect(signature, wasm_of(sub.result));
        }
        else
        {
            // (a subprogram whose body (or renaming-as-body, the called name's) is elaborated later: not yet (else Program_Error))
            for (auto* checked : { declared != callee ? declared : nullptr, callee })
                if (checked != nullptr && iProgram.elaboration_checked(checked))
                {
                    i64(static_cast<std::int64_t>(iProgram.elaboration_flag(checked)));
                    load64(0u);
                    op(opcode::I64Eqz);
                    begin_if();
                    raise_check(1, aCall, "access before elaboration");
                    end();
                }
            call_function(callee);
        }
        // (an external call of a protected operation: its protected action ends (even if it propagates an exception); a procedure's
        // serves the object's queued entry calls whose barriers it opened)
        if (externalSelf)
        {
            get(*externalSelf);
            runtime(sub.result == nullptr ? "po_service" : "po_leave", { type::I64 }, type::Void);
        }
        if (!imported || iProgram.raising_import(sub.import_name))
            check();
        // (the copies of out and in out actuals passed with a change of representation: converted back to the actuals)
        if (!representationCopies.empty() || !volatileCopies.empty())
        {
            std::optional<std::uint32_t> result;
            if (sub.result != nullptr)
            {
                result = local(wasm_of(sub.result));
                set(*result);
            }
            for (auto const& c : representationCopies)
                representation_copy(c.formalType, c.copy, c.actualType, c.actual, false);
            for (auto const& c : volatileCopies)
            {
                get(c.actual);
                get(c.copy);
                i64(static_cast<std::int64_t>(c.size));
                runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
            }
            if (result)
                get(*result);
        }
        // (the copies of out and in out scalars: assigned to the actuals (checked))
        if (!copies.empty())
        {
            std::optional<std::uint32_t> result;
            if (sub.result != nullptr)
            {
                result = local(wasm_of(sub.result));
                set(*result);
            }
            for (auto const& c : copies)
            {
                if (c.address != 0u)
                    get(c.address);
                get(iFrame);
                load(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(c.offset), !is_unsigned(c.type));
                copy_back_check(c.actual->type, c.type, c.actual);
                if (c.address != 0u && c.bitField != nullptr)
                    bit_field_store(*c.bitField);
                else if (c.address != 0u)
                    store(kind_of(c.actual->type), iProgram.scalar_size(c.actual->type));
                else
                    assign_value(c.actual);
            }
            if (result)
                get(*result);
        }
        // (by-reference out and in out actuals of subtypes with predicates: their predicates (3.2.4(31/5)))
        {
            std::optional<std::uint32_t> result;
            for (std::size_t i = 0u; i < formals.size() && i < info->actuals.size(); ++i)
            {
                auto const& f = formals[i];
                auto* a = info->actuals[i];
                if (a == nullptr || f.mode == parameter_mode::In || by_copy(f) || a->type == nullptr || !a->type->view()->is_composite() ||
                    (a->type->predicate == nullptr && a->type->view()->predicate == nullptr))
                    continue;
                if (!result && sub.result != nullptr)
                {
                    result = local(wasm_of(sub.result));
                    set(*result);
                }
                address(a);
                predicate_check(a->type, f.type, a, false);
                drop();
            }
            if (result)
                get(*result);
        }
        // (type invariants: of its out and in out actuals, and its result (7.3.2(19/5)))
        {
            bool actuals = false;
            for (std::size_t i = 0u; i < formals.size() && i < info->actuals.size() && !actuals; ++i)
                if (info->actuals[i] != nullptr && formals[i].mode != parameter_mode::In && formals[i].type != nullptr &&
                    formals[i].type->view()->is_composite())
                {
                    auto* a = info->actuals[i];
                    while (a->is(construct::Parenthesized))
                        a = a->child(0);
                    auto* t = a->type != nullptr && !invariants_of(a->type).empty() ? a->type : formals[i].type;
                    // (an object of a descendant type passed to the ancestor's operation: a call of its inherited operation)
                    actuals = invariant_boundary(callee, t) ||
                        (a->is(construct::Apply) && meaning_of(a) == meaning::Conversion && a->size() > 1u && a->child(1)->type != nullptr &&
                         !invariants_of(a->child(1)->type).empty());
                }
            bool const result = sub.result != nullptr && sub.result->view()->is_composite() && invariant_boundary(callee, sub.result) &&
                !inherited_call(callee, sub.result);
            if (actuals || result)
            {
                std::optional<std::uint32_t> value;
                if (sub.result != nullptr)
                {
                    value = local(wasm_of(sub.result));
                    set(*value);
                }
                if (result)
                    deep_invariant_check(sub.result, *value, aCall);
                if (actuals)
                    call_invariant_checks(callee, *info, formals);
                if (value)
                    get(*value);
            }
        }
        if (taskMark)
        {
            // (the result's tasks: of this master, activated now)
            auto const value = local(type::I64);
            set(value);
            relocate_tasks(sub.result, value);
            get(*taskMark);
            runtime("task_activate_from", { type::I64 }, type::Void);
            check();
            get(value);
        }
        if (sub.result != nullptr && !aStatement && !iMovedResults.count(aCall))
            temporary_result(sub.result);
        if (aStatement && sub.result != nullptr)
            drop();
        iWhere = previous;
    }

    bool function_generator::mutable_record(ada_type* aType)
    {
        if (aType == nullptr)
            return false;
        auto* v = aType->view();
        if (v->kind != type_kind::Record || !v->discriminant_constraint.empty())
            return false;
        auto* base = v->base_type()->view();
        if (base->tagged)
            return false;
        bool discriminants = false;
        for (auto const& c : base->components)
            if (c.discriminant)
            {
                if (c.default_expression == nullptr)
                    return false;
                discriminants = true;
            }
        return discriminants;
    }

    bool function_generator::discriminated_record(ada_type* aType)
    {
        if (aType == nullptr)
            return false;
        auto* base = aType->view()->base_type()->view();
        if (base->kind != type_kind::Record || base->tagged)
            return false;
        for (auto const& c : base->components)
            if (c.discriminant)
                return true;
        return false;
    }

    void function_generator::unconstrained_flag(entity* aFormal, std::uint32_t aAddress)
    {
        auto const flag = local(type::I64);
        get(aAddress);
        i64(UnconstrainedBit);
        op(opcode::I64ShrU);
        i64(1);
        op(opcode::I64And);
        set(flag);
        get(aAddress);
        i64(~(std::int64_t{ 1 } << UnconstrainedBit));
        op(opcode::I64And);
        set(aAddress);
        iUnconstrainedFlags[aFormal] = flag;
    }

    void function_generator::unconstrained_view(node* aActual)
    {
        // (a variable or component of an unconstrained subtype: unconstrained; a formal: as its actual is; a dereference (an
        // allocated object), a constant, a view conversion to an indefinite subtype or a value: constrained)
        auto* a = aActual;
        while (a->is(construct::Parenthesized))
            a = a->child(0);
        if (a->mark & MarkDeref)
        {
            i64(0);
            return;
        }
        switch (meaning_of(a))
        {
        case meaning::Object:
            if (auto* e = a->ent; e != nullptr && !a->is(construct::Apply))
            {
                while (e->renamed != nullptr && e->renamed->is_object())
                    e = e->renamed;
                if (auto flag = iUnconstrainedFlags.find(e); flag != iUnconstrainedFlags.end())
                {
                    get(flag->second);
                    return;
                }
                if ((e->kind == entity_kind::Variable || e->kind == entity_kind::Component) && mutable_record(e->type))
                {
                    i64(1);
                    return;
                }
                if (e->kind == entity_kind::Parameter && mutable_record(e->type) && e->scope != nullptr && e->scope->owner != nullptr &&
                    e->scope->owner->sub != nullptr)
                    for (auto const& p : e->scope->owner->sub->parameters)
                        if (p.ent == e && p.mode != parameter_mode::In)
                        {
                            i64(1);     // (an outer subprogram's out or in out formal: not known here)
                            return;
                        }
            }
            break;
        case meaning::Component:
        case meaning::Index:
            if (mutable_record(a->type))
            {
                i64(1);
                return;
            }
            break;
        case meaning::Conversion:
            if (a->size() > 1u && mutable_record(a->type))
            {
                unconstrained_view(a->children.back());
                return;
            }
            break;
        default:
            break;
        }
        i64(0);
    }

    bool function_generator::representations_differ(ada_type* aSource, ada_type* aTarget)
    {
        auto* d = aTarget->view();
        auto* s = aSource->view();
        auto* db = d->kind == type_kind::Record && !d->tagged ? d->base_type()->view() : nullptr;
        auto* sb = db != nullptr && s->kind == type_kind::Record ? s->base_type()->view() : nullptr;
        if (sb == nullptr || sb == db || db->dynamic_size)
            return false;
        iProgram.layout(db);
        iProgram.layout(sb);
        if (sb->dynamic_size)
            return false;
        bool any = false;
        bool differ = false;
        for (auto const& dc : db->components)
        {
            bool found = false;
            for (auto const& sc : sb->components)
                if (dc.ent != nullptr && sc.ent != nullptr && dc.ent->name == sc.ent->name)
                {
                    any = true;
                    found = true;
                    differ = differ || dc.offset != sc.offset;
                }
            // (a discriminant of the target that the source's type constrains: not stored in the source)
            differ = differ || (dc.discriminant && !found);
        }
        return differ && any;
    }

    void function_generator::representation_copy(ada_type* aSource, std::uint32_t aFrom, ada_type* aTarget, std::uint32_t aTo, bool aDiscriminants)
    {
        // (the components of the same names; the target's discriminants that the source doesn't store: the values that its type's
        // derivation constrains them to (an ancestor's, or a descendant's own (unchanged)))
        auto* sb = aSource->view()->base_type()->view();
        auto* db = aTarget->view()->base_type()->view();
        // (a new target: its discriminants first, then its structure (its components' descriptors), then its other components)
        for (int pass = aDiscriminants ? 0 : 1; pass < 2; ++pass)
        for (auto const& dc : db->components)
        {
            if (pass == 1 && aDiscriminants && &dc == &db->components.front())
            {
                bool const structureOnly = iStructureOnly;
                iStructureOnly = true;
                iDiscriminantsSet = true;
                default_initialize(db, aTo);
                iDiscriminantsSet = false;
                iStructureOnly = structureOnly;
            }
            if (!dc.offset || dc.type == nullptr || dc.ent == nullptr || dc.discriminant != (pass == 0))
                continue;
            component_info const* source = nullptr;
            for (auto const& sc : sb->components)
                if (sc.offset && sc.ent != nullptr && sc.ent->name == dc.ent->name)
                {
                    source = &sc;
                    break;
                }
            auto* ct = dc.type->view();
            if (source == nullptr && dc.discriminant && db->parent != nullptr && db->parent->view()->base_type()->view() == sb)
            {
                // (a new discriminant of the target's type, which constrains the source's (parent's) discriminant: that one's value)
                auto const& constraint = db->parent->view()->discriminant_constraint;
                std::size_t k = 0u;
                for (auto const& sc : sb->components)
                {
                    if (!sc.discriminant)
                        continue;
                    auto* value = k < constraint.size() ? constraint[k] : nullptr;
                    ++k;
                    if (value != nullptr && value->is(construct::Association))
                        value = value->children.back();
                    if (value != nullptr && value->is(construct::Identifier) && name_text(value->source) == dc.ent->name && sc.offset)
                    {
                        source = &sc;
                        break;
                    }
                }
            }
            if (source == nullptr)
            {
                if (!dc.discriminant || ct->is_composite())
                    continue;
                // (an ancestor's discriminant that the source's type constrains: its value for the source object)
                get(aTo);
                iInstances.emplace_back(sb, aFrom);
                load_object(dc.ent);
                iInstances.pop_back();
                store(kind_of(ct), ct->is_access() ? 8u : iProgram.scalar_size(ct), static_cast<std::uint32_t>(*dc.offset));
                continue;
            }
            if (ct->is_composite())
            {
                auto const a = local(type::I64);
                component_address(db, dc, aTo);
                set(a);
                auto const b = local(type::I64);
                component_address(sb, *source, aFrom);
                set(b);
                if (ct->kind == type_kind::Array && !dc.depends && aDiscriminants)
                    init_descriptor(dc.type, a);
                copy_value(ct, a, b);
            }
            else
            {
                get(aTo);
                get(aFrom);
                component_load(*source);
                component_store(dc);
            }
        }
    }

    void function_generator::bit_field_load(component_info const& aComponent)
    {
        // (its storage units (unsigned), shifted and masked; a signed type's value sign extended)
        auto const bit = static_cast<std::int64_t>(*aComponent.bit_position % 8u);
        auto const bits = static_cast<std::int64_t>(*aComponent.bit_size);
        load(vkind::I64, iProgram.bit_field_span(aComponent), 0u, false);
        if (bit != 0)
        {
            i64(bit);
            op(opcode::I64ShrU);
        }
        i64((std::int64_t{ 1 } << bits) - 1);
        op(opcode::I64And);
        if (!is_unsigned(aComponent.type))
        {
            i64(64 - bits);
            op(opcode::I64Shl);
            i64(64 - bits);
            op(opcode::I64ShrS);
        }
    }

    void function_generator::bit_field_store(component_info const& aComponent)
    {
        // (its storage units: the other bits kept, its bits replaced by the value's)
        auto const bit = static_cast<std::int64_t>(*aComponent.bit_position % 8u);
        auto const bits = static_cast<std::int64_t>(*aComponent.bit_size);
        auto const span = iProgram.bit_field_span(aComponent);
        auto const mask = ((std::int64_t{ 1 } << bits) - 1) << bit;
        auto const value = temporary(type::I64);
        set(value);
        auto const address = temporary(type::I64);
        set(address);
        get(address);
        get(address);
        load(vkind::I64, span, 0u, false);
        i64(~mask);
        op(opcode::I64And);
        get(value);
        if (bit != 0)
        {
            i64(bit);
            op(opcode::I64Shl);
        }
        i64(mask);
        op(opcode::I64And);
        op(opcode::I64Ior);
        store(vkind::I64, span, 0u);
        release(address);
        release(value);
    }

    void function_generator::component_load(component_info const& aComponent)
    {
        auto* ct = aComponent.type->view();
        if (iProgram.bit_field(aComponent))
        {
            i64(static_cast<std::int64_t>(*aComponent.offset));
            op(opcode::I64Add);
            bit_field_load(aComponent);
            return;
        }
        load(kind_of(ct), ct->is_access() ? 8u : iProgram.scalar_size(ct), static_cast<std::uint32_t>(*aComponent.offset), !is_unsigned(ct));
    }

    void function_generator::component_store(component_info const& aComponent)
    {
        auto* ct = aComponent.type->view();
        if (iProgram.bit_field(aComponent))
        {
            auto const value = temporary(type::I64);
            set(value);
            i64(static_cast<std::int64_t>(*aComponent.offset));
            op(opcode::I64Add);
            get(value);
            release(value);
            bit_field_store(aComponent);
            return;
        }
        store(kind_of(ct), ct->is_access() ? 8u : iProgram.scalar_size(ct), static_cast<std::uint32_t>(*aComponent.offset));
    }

    component_info const* function_generator::bit_field_name(node* aName)
    {
        while (aName != nullptr && aName->is(construct::Parenthesized))
            aName = aName->child(0);
        if (aName == nullptr || !aName->is(construct::Selected) || meaning_of(aName) != meaning::Component)
            return nullptr;
        auto* recordType = aName->child(0)->type;
        if (aName->mark & MarkDeref)
            recordType = iProgram.designated_of(recordType);
        return iProgram.bit_field_of(recordType, aName->ent);
    }

    void function_generator::composite_actual(ada_type* aFormal, node* aActual)
    {
        // ([address] -> [address]: a composite actual converted to its formal's subtype (6.4.1(11)): a constrained array subtype's 
        // bounds (sliding: of the same lengths), a constrained record subtype's discriminants)
        auto* ft = aFormal->view();
        if (ft->kind == type_kind::Array && ft->constrained && aActual->type != nullptr && aActual->type->view()->kind == type_kind::Array &&
            (aActual->type->view() != ft || meaning_of(aActual) == meaning::StringLiteral) && !is_aggregate_node(aActual))
        {
            auto const value = local(type::I64);
            set(value);
            subtype_descriptor(ft);
            auto const descriptor = local(type::I64);
            set(descriptor);
            length_check(descriptor, value, ft->base_type()->view()->indexes.size(), aActual);
            get(descriptor);
            data_address(value);
            get(descriptor);
            op(opcode::I64Sub);
            store64(0u);
            get(descriptor);
        }
        else if (ft->kind == type_kind::Record && !ft->discriminant_constraint.empty())
        {
            auto const value = local(type::I64);
            tee(value);
            discriminant_constraint_check(aFormal, value, aActual);
        }
    }

    void function_generator::assign_value(node* aTarget)
    {
        // ([value] -> (): assigned to a name (a scalar))
        auto* t = aTarget->type;
        if (aTarget->is(construct::Parenthesized))
        {
            assign_value(aTarget->child(0));
            return;
        }
        if (meaning_of(aTarget) == meaning::Conversion)
        {
            // (a view conversion as an out parameter: converted back)
            auto* inner = aTarget->child(1);
            convert_representation(aTarget->type, inner->type);
            range_check(inner->type, nullptr, inner);
            assign_value(inner);
            return;
        }
        if ((aTarget->is(construct::Identifier) || aTarget->is(construct::Selected)) && aTarget->ent != nullptr && aTarget->ent->is_object() &&
            meaning_of(aTarget) != meaning::Component)
        {
            store_object(aTarget->ent);
            return;
        }
        auto const value = temporary(wasm_of(t));
        set(value);
        address(aTarget);
        get(value);
        if (auto const* bitField = bit_field_name(aTarget))
            bit_field_store(*bitField);
        else
            store(kind_of(t), iProgram.scalar_size(t));
        release(value);
    }
}

namespace neos::compilers::ada
{
    // --- accessibility levels (3.10.2)

    std::int64_t function_generator::static_depth(node const* aWhere)
    {
        // (this subprogram's body's level, and the blocks enclosing the construct within it)
        std::int64_t depth = 0;
        if (iSubprogram != nullptr)
            depth = iSubprogram->inner != nullptr ? iProgram.accessibility_level(iSubprogram->inner) :
                (iSubprogram->scope != nullptr ? iProgram.accessibility_level(iSubprogram->scope) + 1 : 1);
        auto const* body = iSubprogram != nullptr && iSubprogram->sub != nullptr ? iSubprogram->sub->body : nullptr;
        for (auto const* n = aWhere != nullptr ? aWhere->parent : nullptr; n != nullptr && n != body; n = n->parent)
            if (n->is(construct::Block))
                ++depth;
        return depth;
    }

    std::optional<std::int64_t> function_generator::access_level(node* aValue)
    {
        // (the level of the object an access value designates: an access parameter's (passed); an access type's; X'Access's: X's;
        // an allocator's (of an anonymous type: its innermost master's))
        auto* v = aValue;
        while (v->is(construct::Parenthesized))
            v = v->child(0);
        if (v->is(construct::Null))
            return 0;
        if (v->is(construct::Allocator))
        {
            auto* t = v->type != nullptr ? v->type->view() : nullptr;
            if (t != nullptr && !t->anonymous_access)
                return iProgram.type_level(t);
            return static_depth(v) + 1;
        }
        if (v->is(construct::Attribute))
        {
            auto const name = name_text(v->child(1)->source);
            if ((name == "access" || name == "unchecked_access") && meaning_of(v->child(0)) != meaning::Subprogram)
                return object_level(v->child(0));
            return 0;
        }
        auto* t = v->type != nullptr ? v->type->view() : nullptr;
        if (t == nullptr || t->kind != type_kind::Access)
            return 0;
        if (!t->anonymous_access)
        {
            // (a value of a named access type: its type's level)
            return iProgram.type_level(t);
        }
        // (an access parameter: its level passed)
        if ((v->is(construct::Identifier) || v->is(construct::Selected)) && meaning_of(v) == meaning::Object && v->ent != nullptr)
        {
            auto* e = v->ent;
            while (e->renamed != nullptr)
                e = e->renamed;
            if (auto const l = iAccessLevels.find(e); l != iAccessLevels.end())
            {
                get(l->second);
                iDynamicLevelOwner = e->scope != nullptr ? iProgram.accessibility_level(e->scope) : 1;
                return std::nullopt;
            }
            if (e->kind == entity_kind::Parameter)
                return 0;
            // (a stand-alone object of an anonymous access type: its value's level (3.10.2(19.2/3)))
            if (auto const l = iProgram.iAccessLevelObjects.find(e); l != iProgram.iAccessLevelObjects.end() && l->second->where != storage::None)
            {
                load_object(l->second);
                iDynamicLevelOwner = e->scope != nullptr ? iProgram.accessibility_level(e->scope) : 1;
                return std::nullopt;
            }
            if (e->kind == entity_kind::Variable || e->kind == entity_kind::Constant)
                return e->scope != nullptr ? iProgram.accessibility_level(e->scope) : 0;
        }
        // (an access discriminant (of an object that is not allocated): its object's level (3.10.2(12.4/3)))
        if (meaning_of(v) == meaning::Component && v->ent != nullptr && v->ent->kind == entity_kind::Discriminant && v->is(construct::Selected))
            return (v->mark & MarkDeref) ? access_level(v->child(0)) : object_level(v->child(0));
        if (meaning_of(v) == meaning::Conversion && v->is(construct::Apply) && v->size() > 1u)
            return access_level(v->child(1));
        return 0;
    }

    std::optional<std::int64_t> function_generator::object_level(node* aObject)
    {
        auto* o = aObject;
        while (o->is(construct::Parenthesized))
            o = o->child(0);
        if (o->is(construct::All))
            return access_level(o->child(0));
        // (a generalized reference (4.1.5): its reference discriminant's object (of an unknown level here))
        if (iProgram.implicit_dereference_of(o) != nullptr)
            return 0;
        switch (meaning_of(o))
        {
        case meaning::Object:
            if (o->ent != nullptr)
            {
                auto* e = o->ent;
                // (a renaming: the renamed object's)
                if (e->renamed_name != nullptr && e->is_object())
                    return object_level(e->renamed_name);
                if (e->renamed != nullptr && e->renamed != e)
                    e = e->renamed;
                return e->scope != nullptr ? iProgram.accessibility_level(e->scope) : 0;
            }
            return 0;
        case meaning::Component:
        case meaning::Index:
        case meaning::Slice:
            if (o->mark & MarkDeref)
                return access_level(o->child(0));
            return object_level(o->child(0));
        case meaning::Conversion:
            if (o->is(construct::Apply) && o->size() > 1u)
                return object_level(o->child(1));
            return 0;
        case meaning::Call:
            return static_depth(o) + 1;
        default:
            return 0;
        }
    }

    void function_generator::accessibility_check(std::optional<std::int64_t> aLevel, std::int64_t aTarget, node const* aWhere)
    {
        if (aLevel)
        {
            // (static: deeper only in an instance (else illegal, 3.10.2(28/3, 32/3)))
            if (*aLevel > aTarget)
                raise_check(1, aWhere, "accessibility check failed");
            return;
        }
        // (a target declared within the subprogram whose access parameter's level it is: not deeper (the designated object exists
        // before the call, 3.10.2(7/3)))
        if (aTarget >= iDynamicLevelOwner)
        {
            drop();
            return;
        }
        i64(aTarget);
        op(opcode::I64GtS);
        begin_if();
        raise_check(1, aWhere, "accessibility check failed");
        end();
    }

    void function_generator::class_constraint_check(ada_type* aAccess, node const* aWhere)
    {
        auto* a = aAccess != nullptr ? aAccess->view() : nullptr;
        auto* d = a != nullptr && a->kind == type_kind::Access ? iProgram.designated_of(a) : nullptr;
        if (d == nullptr || d->view()->kind != type_kind::ClassWide)
            return;
        auto* specific = d->view()->class_subtype;
        if (specific == nullptr || specific->view()->kind != type_kind::Record || specific->view()->discriminant_constraint.empty())
            return;
        auto const value = local(type::I64);
        tee(value);
        op(opcode::I64Eqz);
        op(opcode::I32Eqz);
        begin_if();
        discriminant_constraint_check(specific, value, aWhere, nullptr);
        end();
        get(value);
    }

    void function_generator::store_access_level(entity* aObject, node* aValue)
    {
        auto* t = aObject->type != nullptr ? aObject->type->view() : nullptr;
        if (t == nullptr || t->kind != type_kind::Access || !t->anonymous_access || (aObject->kind != entity_kind::Variable &&
            aObject->kind != entity_kind::Constant) || aObject->renamed != nullptr || aObject->renamed_name != nullptr)
            return;
        auto& level = iProgram.iAccessLevelObjects[aObject];
        if (level == nullptr)
        {
            level = iProgram.new_entity(entity_kind::Variable, "%level", aObject->decl, aObject->scope);
            level->type = iProgram.integer();
            level->captured = true;
        }
        if (level->where == storage::None)
        {
            level->function_level = static_cast<std::uint32_t>(iLevel);
            assign_storage(level);
        }
        auto const value = aValue != nullptr ? access_level(aValue) : std::optional<std::int64_t>{ 0 };
        if (value)
            i64(*value);
        store_object(level);
    }

    bool function_generator::passes_levels(entity* aCallee) const
    {
        // (a subprogram whose body is generated (its prologue takes the levels from its access parameters' values): not an entry,
        // an imported or intrinsic subprogram)
        if (aCallee == nullptr || aCallee->sub == nullptr || aCallee->kind == entity_kind::Entry || aCallee->kind == entity_kind::Attribute)
            return false;
        return aCallee->sub->import_name.empty() && !aCallee->imported && aCallee->sub->op == intrinsic::None;
    }
}
