/*
  gen_records.cpp

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

// Ada: records whose components depend on their discriminants. An array component whose bounds depend on discriminants has 
// its bounds evaluated for each object (its record's discriminants: the "instance" whose discriminants the expressions name).
// If its size has a (small) maximum its data is in place (the maximum's room); else its descriptor is in place and its data
// is in the object's tail (after the record's fixed part, in the order of the components): the object's size is its fixed
// part's and its tail's (its discriminants'). Such an object is not mutable (an assignment with other discriminants raises
// Constraint_Error). A class-wide object's size is its tag's (its specific type's objects' size).

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::ada
{
    std::uint64_t program::fixed_size(ada_type* aType)
    {
        auto* t = aType->view()->base_type()->view();
        layout(t);
        return t->size.value_or(0u);
    }

    bool function_generator::is_runtime_sized(ada_type* aType)
    {
        auto* t = aType->view();
        if (t->kind == type_kind::ClassWide)
            return true;
        if (t->kind == type_kind::Record || t->kind == type_kind::Private || t->kind == type_kind::Protected || t->kind == type_kind::Task)
        {
            auto* base = t->base_type()->view();
            iProgram.layout(base);
            return base->dynamic_size;
        }
        return false;
    }

    bool function_generator::instance_member(entity* aObject, std::uint64_t& aOffset, std::uint32_t& aInstance)
    {
        // (a discriminant (or component) of the record whose object is being initialized)
        if (aObject->kind != entity_kind::Discriminant && aObject->kind != entity_kind::Component)
            return false;
        for (auto i = iInstances.size(); i-- > 0u;)
            for (auto const& c : iInstances[i].first->components)
                if (c.ent == aObject && c.offset)
                {
                    aOffset = *c.offset;
                    aInstance = iInstances[i].second;
                    return true;
                }
        // (a discriminant of an ancestor of a type with new discriminants: the new discriminant that constrains it (through the
        // intermediate types' constraints))
        if (aObject->kind == entity_kind::Discriminant)
            for (auto i = iInstances.size(); i-- > 0u;)
            {
                auto* base = iInstances[i].first;
                entity* target = aObject;
                for (int hops = 0; hops < 16 && target != nullptr; ++hops)
                {
                    entity* next = nullptr;
                    for (auto* d = base; d != nullptr && d->parent != nullptr && next == nullptr;)
                    {
                        auto* parentView = d->parent->view();
                        auto* pb = parentView->base_type()->view();
                        if (pb == d)
                            break;
                        std::size_t k = 0u;
                        for (auto const& pc : pb->components)
                        {
                            if (!pc.discriminant)
                                continue;
                            auto* value = k < parentView->discriminant_constraint.size() ? parentView->discriminant_constraint[k] : nullptr;
                            ++k;
                            if (pc.ent != target)
                                continue;
                            if (value != nullptr && value->is(construct::Association))
                                value = value->children.back();
                            if (value == nullptr || !value->is(construct::Identifier))
                                return false;
                            auto const name = name_text(value->source);
                            for (auto const& c : base->components)
                                if (c.discriminant && c.ent != nullptr && c.ent->name == name && c.offset)
                                {
                                    aOffset = *c.offset;
                                    aInstance = iInstances[i].second;
                                    return true;
                                }
                            // (a discriminant of the intermediate type d, constrained in turn)
                            for (auto const& dc : d->components)
                                if (dc.discriminant && dc.ent != nullptr && dc.ent->name == name)
                                    next = dc.ent;
                            break;
                        }
                        d = pb;
                    }
                    target = next;
                }
            }
        return false;
    }

    void function_generator::index_bound(ada_type* aIndex, bool aLow)
    {
        // (an i64: a bound of an index subtype (one that is not static: its expression, evaluated now))
        auto* v = aIndex->view();
        auto* object = aLow ? v->low_object : v->high_object;
        if (object != nullptr && object->initial != nullptr)
        {
            // (a static one: its value)
            if (auto value = iProgram.evaluate(object->initial, nullptr); value.known() && value.is_integer() && value.integer().fits_int64())
            {
                i64(value.integer().to_int64());
                return;
            }
            // (one that doesn't depend on a discriminant: evaluated where the constraint was elaborated (its hidden constant))
            if (object->where != storage::None && !iProgram.refers_to_discriminant(object->initial))
                load_object(object);
            else
                expression(object->initial, object->type);
            return;
        }
        if (aLow)
            scalar_low(aIndex);
        else
            scalar_high(aIndex);
    }

    void function_generator::record_size(ada_type* aType, std::uint32_t aValue)
    {
        // (an i64: the size of a record value (a class-wide one's: its tag's; a dynamically sized one's: its fixed part's and its
        // tail's))
        auto* t = aType->view();
        if (t->kind == type_kind::ClassWide)
        {
            // (its tag's size; a dynamically sized type's: its size descriptor's (negated), for the runtime to apply)
            auto const size = temporary(type::I64);
            get(aValue);
            load64(0u);
            load64(16u);
            tee(size);
            i64(0);
            op(opcode::I64LtS);
            begin_if(type::I64);
            get(aValue);
            i64(0);
            get(size);
            op(opcode::I64Sub);
            runtime("record_size_dynamic", { type::I64, type::I64 }, type::I64);
            else_();
            get(size);
            end();
            release(size);
            return;
        }
        auto* base = t->base_type()->view();
        iProgram.layout(base);
        if (!base->dynamic_size)
        {
            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(base->size.value_or(8u), 8u)));
            return;
        }
        i64(static_cast<std::int64_t>(*base->size));
        for (auto const& c : base->components)
            if (c.dynamic && c.offset && c.type->view()->kind != type_kind::Array)
            {
                // (a dynamically sized record component: its size (one of a variant that the discriminants don't select: the
                // space its object has in the tail, which is its subtype's (see default_initialize); it has no value))
                if (c.variant >= 0)
                {
                    variant_selected(base, c, aValue);
                    begin_if(type::I64);
                }
                auto const component = temporary(type::I64);
                component_address(base, c, aValue);
                set(component);
                record_size(c.type, component);
                release(component);
                if (c.variant >= 0)
                {
                    else_();
                    iInstances.emplace_back(base, aValue);
                    record_subtype_size(c.type);
                    iInstances.pop_back();
                    end();
                }
                i64(7);
                op(opcode::I64Add);
                i64(~std::int64_t{ 7 });
                op(opcode::I64And);
                op(opcode::I64Add);
            }
            else if (c.dynamic && c.offset)
            {
                auto const d = temporary(type::I64);
                get(aValue);
                i64(static_cast<std::int64_t>(*c.offset));
                op(opcode::I64Add);
                set(d);
                array_length(d, c.type->view()->base_type()->view()->indexes.size());
                if (auto const es = iProgram.component_size(c.type); es != 0u)
                    i64(static_cast<std::int64_t>(es));
                else
                    element_size(c.type);     // (dynamically sized elements)
                op(opcode::I64Mul);
                i64(7);
                op(opcode::I64Add);
                i64(~std::int64_t{ 7 });
                op(opcode::I64And);
                op(opcode::I64Add);
                release(d);
            }
    }

    void function_generator::copy_whole(ada_type* aType, std::uint32_t aTarget, std::uint32_t aSource)
    {
        // (a record value's bytes, its tag and tail too)
        get(aTarget);
        get(aSource);
        record_size(aType, aSource);
        runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
    }

    void function_generator::component_bounds(ada_type* aRecord, component_info const& aComponent, std::uint32_t aRecordAddress, std::uint32_t aComponentAddress)
    {
        // (an array component's descriptor: its bounds (its record's discriminants'), its data's offset: in place, or in the tail
        // (iTail: the tail's next free byte, as an offset in the record))
        auto* ct = aComponent.type->view();
        auto const dimensions = ct->base_type()->view()->indexes.size();
        iInstances.emplace_back(aRecord, aRecordAddress);
        for (std::size_t d = 0u; d < dimensions && d < ct->indexes.size(); ++d)
        {
            get(aComponentAddress);
            index_bound(ct->indexes[d], true);
            store64(static_cast<std::uint32_t>(8u + 16u * d));
            get(aComponentAddress);
            index_bound(ct->indexes[d], false);
            store64(static_cast<std::uint32_t>(16u + 16u * d));
        }
        iInstances.pop_back();
        get(aComponentAddress);
        if (!aComponent.dynamic)
            i64(static_cast<std::int64_t>(8u + 16u * dimensions));
        else
        {
            // (the tail: data offset = (record + tail) - descriptor)
            get(aRecordAddress);
            get(iTail);
            op(opcode::I64Add);
            get(aComponentAddress);
            op(opcode::I64Sub);
            // (the tail's next free byte)
            array_length(aComponentAddress, dimensions);
            if (auto const es = iProgram.component_size(ct); es != 0u)
                i64(static_cast<std::int64_t>(es));
            else
                element_size(ct);     // (dynamically sized elements)
            op(opcode::I64Mul);
            i64(7);
            op(opcode::I64Add);
            i64(~std::int64_t{ 7 });
            op(opcode::I64And);
            get(iTail);
            op(opcode::I64Add);
            set(iTail);
        }
        store64(0u);
    }

    void function_generator::store_discriminants(ada_type* aType, std::uint32_t aAddress, std::function<node*(std::size_t, component_info const&)> const& aValue)
    {
        // (a record's discriminants (their values: aValue's (null: their defaults)))
        auto* base = aType->view()->base_type()->view();
        std::size_t d = 0u;
        for (std::size_t i = 0u; i < base->components.size(); ++i)
        {
            auto const& c = base->components[i];
            if (!c.discriminant || !c.offset)
                continue;
            node* value = aValue(d++, c);
            if (value == nullptr)
                value = c.default_expression;
            if (value == nullptr)
                continue;
            get(aAddress);
            expression(value, c.type);
            range_check(c.type, value->type, value);
            store(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(*c.offset));
        }
        // (the parent's discriminants that are stored (see component_info::stored): the constraint's values (of the discriminants))
        iInstances.emplace_back(base, aAddress);
        for (auto const& c : base->components)
            if (c.stored && c.offset && c.default_expression != nullptr)
            {
                get(aAddress);
                expression(c.default_expression, c.type);
                store(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(*c.offset));
            }
        iInstances.pop_back();
    }

    void function_generator::dynamic_record(ada_type* aType, std::uint32_t aScratch, int aWhere)
    {
        // (an i64: a new object of a dynamically sized record type whose discriminants are in a scratch fixed part (its tail's
        // size: theirs): allocated (0: on the primary stack; 1: the heap; 2: the secondary stack), its discriminants copied, 
        // then default initialized)
        auto* base = aType->view()->base_type()->view();
        auto const size = local(type::I64);
        dynamic_record_size(base, aScratch);
        set(size);
        auto const object = local(type::I64);
        if (aWhere == 1)
        {
            get(size);
            heap_allocate();
        }
        else if (aWhere == 2)
        {
            get(size);
            runtime("ss_allocate", { type::I64 }, type::I64);
        }
        else
            stack_allocate(size);
        set(object);
        get(object);
        get(aScratch);
        i64(static_cast<std::int64_t>(*base->size));
        runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
        iDiscriminantsSet = true;
        default_initialize(aType, object);
        iDiscriminantsSet = false;
        get(object);
    }

    void function_generator::record_subtype_size(ada_type* aSubtype)
    {
        // (an i64: the size of the objects of a constrained subtype (or one whose discriminants have defaults) of a dynamically
        // sized record type: its fixed part's and its tail's (its discriminants' values'))
        auto* t = aSubtype->view();
        auto* base = t->base_type()->view();
        iProgram.layout(base);
        auto const scratch = local(type::I64);
        i64(static_cast<std::int64_t>(*base->size));
        runtime("ss_allocate", { type::I64 }, type::I64);
        set(scratch);
        store_discriminants(t, scratch, [&](std::size_t aIndex, component_info const&) -> node*
            {
                return aIndex < t->discriminant_constraint.size() ? t->discriminant_constraint[aIndex] : nullptr;
            });
        dynamic_record_size(base, scratch);
        i64(7);
        op(opcode::I64Add);
        i64(~std::int64_t{ 7 });
        op(opcode::I64And);
    }

    void function_generator::dynamic_record_size(ada_type* aBase, std::uint32_t aScratch)
    {
        // (an i64: a dynamically sized record's size for the discriminants in a fixed part: its fixed part's and its tail's)
        auto* base = aBase;
        i64(static_cast<std::int64_t>(*base->size));
        iInstances.emplace_back(base, aScratch);
        for (auto const& c : base->components)
            if (c.dynamic && c.offset && c.type->view()->kind != type_kind::Array)
            {
                // (a dynamically sized record component: its subtype's size (the discriminants' values))
                record_subtype_size(c.type);
                op(opcode::I64Add);
            }
            else if (c.dynamic && c.offset)
            {
                auto* ct = c.type->view();
                i64(1);
                for (std::size_t d = 0u; d < ct->indexes.size(); ++d)
                {
                    auto const n = temporary(type::I64);
                    index_bound(ct->indexes[d], false);
                    index_bound(ct->indexes[d], true);
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
                if (auto const es = iProgram.component_size(ct); es != 0u)
                    i64(static_cast<std::int64_t>(es));
                else
                    element_size(ct);     // (dynamically sized elements)
                op(opcode::I64Mul);
                i64(7);
                op(opcode::I64Add);
                i64(~std::int64_t{ 7 });
                op(opcode::I64And);
                op(opcode::I64Add);
            }
        iInstances.pop_back();
    }

    void function_generator::dynamic_object(ada_type* aType, std::optional<std::uint32_t> aValue, int aWhere)
    {
        // (an i64: a new object of a runtime sized type: a copy of a value, or default initialized (its subtype's discriminants))
        auto* t = aType->view();
        if (aValue)
        {
            auto const size = local(type::I64);
            record_size(t, *aValue);
            set(size);
            auto const object = local(type::I64);
            if (aWhere == 1)
            {
                get(size);
                heap_allocate();
            }
            else if (aWhere == 2)
            {
                get(size);
                runtime("ss_allocate", { type::I64 }, type::I64);
            }
            else
                stack_allocate(size);
            set(object);
            copy_whole(t, object, *aValue);
            get(object);
            return;
        }
        if (t->kind == type_kind::ClassWide)
            iProgram.error(iWhere, "class-wide object without an initial value");
        auto* base = t->base_type()->view();
        auto const scratch = local(type::I64);
        i64(static_cast<std::int64_t>(*base->size));
        runtime("ss_allocate", { type::I64 }, type::I64);
        set(scratch);
        store_discriminants(t, scratch, [&](std::size_t aIndex, component_info const&) -> node*
            {
                return aIndex < t->discriminant_constraint.size() ? t->discriminant_constraint[aIndex] : nullptr;
            });
        dynamic_record(t, scratch, aWhere);
    }

    std::vector<std::uint32_t> function_generator::discriminant_constraint_values(ada_type* aSubtype)
    {
        // (a subtype's discriminant constraint's values, evaluated (once, for an object's declaration, before its initial value))
        std::vector<std::uint32_t> result;
        auto* t = aSubtype->view();
        if (t->kind != type_kind::Record || t->discriminant_constraint.empty())
            return result;
        auto* base = t->base_type()->view();
        std::size_t d = 0u;
        for (auto const& c : base->components)
        {
            if (!c.discriminant)
                continue;
            auto* value = d < t->discriminant_constraint.size() ? t->discriminant_constraint[d] : nullptr;
            ++d;
            if (value == nullptr || c.type == nullptr || !(c.type->view()->is_scalar() || c.type->view()->is_access()))
            {
                result.push_back(0u);
                continue;
            }
            auto const v = local(wasm_of(c.type));
            expression(value, c.type);
            range_check(c.type, value->type, value);
            set(v);
            result.push_back(v);
        }
        return result;
    }

    void function_generator::discriminant_constraint_check(ada_type* aSubtype, std::uint32_t aValue, node const* aWhere, std::vector<std::uint32_t> const* aValues)
    {
        // (a record value of a subtype whose discriminants are constrained: its discriminants the constraint's (else Constraint_Error))
        auto* t = aSubtype->view();
        if (t->kind != type_kind::Record || t->discriminant_constraint.empty())
            return;
        auto* base = t->base_type()->view();
        iProgram.layout(base);
        std::size_t d = 0u;
        for (auto const& c : base->components)
        {
            if (!c.discriminant)
                continue;
            auto* value = d < t->discriminant_constraint.size() ? t->discriminant_constraint[d] : nullptr;
            auto const evaluated = aValues != nullptr && d < aValues->size() ? (*aValues)[d] : 0u;
            ++d;
            if (value == nullptr || !c.offset || c.type == nullptr || !c.type->view()->is_scalar())
                continue;
            auto const k = kind_of(c.type);
            get(aValue);
            load(k, iProgram.scalar_size(c.type), static_cast<std::uint32_t>(*c.offset), !is_unsigned(c.type));
            if (evaluated != 0u)
                get(evaluated);
            else
                expression(value, c.type);
            op(k == vkind::I64 ? opcode::I64Ne : k == vkind::F32 ? opcode::F32Ne : opcode::F64Ne);
            begin_if();
            raise_check(0, aWhere, "discriminant check failed");
            end();
        }
    }

    void function_generator::discriminant_check(ada_type* aType, std::uint32_t aTarget, std::uint32_t aSource, node const* aWhere)
    {
        // (two records of a type with discriminants: the same discriminants (else Constraint_Error))
        auto* base = aType->view()->base_type()->view();
        if (aType->view()->kind == type_kind::ClassWide)
        {
            get(aTarget);
            load64(0u);
            get(aSource);
            load64(0u);
            op(opcode::I64Ne);
            begin_if();
            raise_check(0, aWhere, "tag check failed");
            end();
            return;
        }
        for (auto const& c : base->components)
            if (c.discriminant && c.offset)
            {
                get(aTarget);
                load(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(*c.offset));
                get(aSource);
                load(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(*c.offset));
                op(kind_of(c.type) == vkind::I64 ? opcode::I64Ne : kind_of(c.type) == vkind::F32 ? opcode::F32Ne : opcode::F64Ne);
                begin_if();
                raise_check(0, aWhere, "discriminant check failed");
                end();
            }
    }
}
