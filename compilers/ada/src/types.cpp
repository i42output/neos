/*
  types.cpp

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

// Ada: types and subtypes (their declarations, constraints, derivation, predefined operators and the relations between them)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <set>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        std::uint64_t machine_bytes(bigint const& aLow, bigint const& aHigh, bool aUnsigned)
        {
            for (std::uint64_t bytes : { 1u, 2u, 4u, 8u })
            {
                auto const bits = bytes * 8u;
                bigint const low = aUnsigned ? bigint{ 0 } : -bigint::pow(bigint{ 2 }, bits - 1u);
                bigint const high = aUnsigned ? bigint::pow(bigint{ 2 }, bits) - bigint{ 1 } : bigint::pow(bigint{ 2 }, bits - 1u) - bigint{ 1 };
                if (aLow >= low && aHigh <= high)
                    return bytes;
            }
            return 8u;
        }

        // (the largest power of two not greater than a value)
        rational power_of_two_at_most(rational const& aValue)
        {
            rational p{ bigint{ 1 } };
            if (aValue.sign() <= 0)
                return p;
            while (p > aValue)
                p = p / rational{ bigint{ 2 } };
            while (p * rational{ bigint{ 2 } } <= aValue)
                p = p * rational{ bigint{ 2 } };
            return p;
        }
    }

    // --- relations between types

    bool program::same_type(ada_type* a, ada_type* b)
    {
        if (a == nullptr || b == nullptr)
            return false;
        auto* x = a->view()->base_type()->view();
        auto* y = b->view()->base_type()->view();
        if (x == y)
            return true;
        // (a partial view and its full view are the same type)
        auto const partial_of = [](ada_type* t) { return t->partial != nullptr ? t->partial->base_type() : t; };
        if (partial_of(x) == partial_of(y))
            return true;
        // (an incomplete type and its completion; two views of a formal type's actual)
        if (x->kind == type_kind::ClassWide && y->kind == type_kind::ClassWide)
            return same_type(x->specific, y->specific);
        return false;
    }

    namespace
    {
        // (whether a type is in the derivation class of another (a tagged type derived from it, or it))
        bool derived_from(ada_type* aType, ada_type* aAncestor, std::function<bool(ada_type*, ada_type*)> const& aSame)
        {
            for (auto* t = aType; t != nullptr; t = t->parent != nullptr ? t->parent->base_type()->view() : nullptr)
            {
                if (aSame(t, aAncestor))
                    return true;
                for (auto* i : t->interfaces)
                    if (derived_from(i->view(), aAncestor, aSame))
                        return true;
                if (t->parent == nullptr)
                    break;
            }
            return false;
        }
    }

    bool program::is_descendant(ada_type* aType, ada_type* aAncestor)
    {
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view()->base_type()->view();
        auto* a = aAncestor->view()->base_type()->view();
        if (a->kind == type_kind::ClassWide)
            a = a->specific->view()->base_type()->view();
        return derived_from(t, a, [this](ada_type* x, ada_type* y) { return same_type(x, y); });
    }

    bool program::covers(ada_type* aExpected, ada_type* aActual)
    {
        if (aExpected == nullptr || aActual == nullptr)
            return true;
        auto* e = aExpected->view()->base_type()->view();
        auto* a = aActual->view()->base_type()->view();
        if (same_type(e, a))
            return true;
        auto const same = [this](ada_type* x, ada_type* y) { return same_type(x, y); };
        // (a private type outside the scope of its full view: not of a numeric type (no literals, 7.3(4)))
        auto const hidden = [&]()
        {
            auto* declared = aExpected->base_type();
            if (declared->kind != type_kind::Private && declared->partial != nullptr)
                declared = declared->partial;
            return declared->kind == type_kind::Private && !declared->incomplete && declared->full != nullptr && declared->declared_in != nullptr &&
                !declared->declared_in->private_visible && !(iResolving != nullptr && declared->declared_in->owner != nullptr &&
                declared->declared_in->owner->is_package() && private_part_visible(declared->declared_in->owner, iResolving));
        };
        switch (a->kind)
        {
        case type_kind::UniversalInteger:
            return e->is_integer() && !hidden();
        case type_kind::UniversalReal:
            return !hidden() && e->kind == type_kind::Float || e->kind == type_kind::Fixed || e->kind == type_kind::Decimal || e->kind == type_kind::UniversalFixed;
        case type_kind::UniversalFixed:
            return e->is_fixed();
        case type_kind::UniversalAccess:
            return e->is_access();
        case type_kind::String:
            return e->kind == type_kind::Array && e->indexes.size() == 1u && e->component != nullptr && e->component->view()->base_type()->character_type;
        case type_kind::CharacterLiteral:
            // (a character type (its literals need not be visible: 4.2(3)))
            return e->kind == type_kind::Enumeration && (e->standard_character || e->character_type);
        case type_kind::Aggregate:
            {
                // (a container aggregate ([...]) is of an array type or a container type (one with the Aggregate aspect); a record or
                // array aggregate ((...)) of an array or record type (4.3(3/5)): not of a private type outside its full view's scope)
                bool const container = e->kind != type_kind::Array && type_aspect(aExpected, "aggregate") != nullptr;
                if (a == iBracketAggregateType)
                    return e->kind == type_kind::Array || container || e->kind == type_kind::Private && e->full == nullptr;
                // (a private type: only where its full view is visible)
                auto* declared = aExpected->base_type();
                if (declared->kind != type_kind::Private && declared->partial != nullptr)
                    declared = declared->partial;
                if (declared->kind == type_kind::Private && !declared->incomplete && declared->full != nullptr && declared->declared_in != nullptr &&
                    !declared->declared_in->private_visible)
                    return false;
                return e->kind == type_kind::Array || e->kind == type_kind::Record || e->kind == type_kind::Private && e->full == nullptr ||
                    (e->kind == type_kind::Private && e->full != nullptr && e->full->is_composite()) || e->kind == type_kind::ClassWide;
            }
        default:
            break;
        }
        if (e->kind == type_kind::UniversalInteger)
            return a->is_integer();
        if (e->kind == type_kind::UniversalReal)
            return a->is_real();
        if (e->kind == type_kind::UniversalFixed)
            return a->is_fixed();
        // (a class-wide type covers the types derived from its specific type)
        if (e->kind == type_kind::ClassWide)
        {
            auto* specific = e->specific->view()->base_type()->view();
            if (a->kind == type_kind::ClassWide)
                return derived_from(a->specific->view()->base_type()->view(), specific, same);
            return derived_from(a, specific, same);
        }
        // (an anonymous access type: an access type whose designated type it designates (or covers))
        if (e->kind == type_kind::Access && e->anonymous_access && a->kind == type_kind::Access)
            return e->designated == nullptr || a->designated == nullptr || covers(e->designated, a->designated);
        if (e->kind == type_kind::AccessSubprogram && e->anonymous_access && a->kind == type_kind::AccessSubprogram)
            return true;
        if (e->kind == type_kind::Access && a->kind == type_kind::Access && a->anonymous_access)
            return e->designated == nullptr || a->designated == nullptr || covers(e->designated, a->designated);
        return false;
    }

    std::optional<bigint> program::low_bound(ada_type* aType)
    {
        auto* t = aType->view();
        if (t->low.is_integer())
            return t->low.integer();
        return {};
    }

    std::optional<bigint> program::high_bound(ada_type* aType)
    {
        auto* t = aType->view();
        if (t->high.is_integer())
            return t->high.integer();
        return {};
    }

    // --- subtype marks and indications

    ada_type* program::subtype_mark(node* aMark, region* aRegion)
    {
        if (aMark->is(construct::SubtypeIndication))
            return subtype_indication(aMark, aRegion);
        // ('Class, 'Base)
        if (aMark->is(construct::Attribute))
        {
            auto const attribute = name_text(aMark->child(1)->source);
            auto* prefix = subtype_mark(aMark->child(0), aRegion);
            aMark->type = prefix;
            if (attribute == "class")
                return class_wide(prefix);
            if (attribute == "base")
                return prefix->base_type();
            error(aMark, "subtype mark expected");
        }
        auto* e = resolve_entity(aMark, aRegion);
        if (e == nullptr)
            error(aMark, "subtype mark expected");
        if (e->kind == entity_kind::Task || e->kind == entity_kind::Protected)
            if (e->type != nullptr)
                return e->type;
        if (!e->is_type())
            error(aMark, "\"" + e->name + "\" is not a type");
        aMark->ent = e;
        aMark->type = e->type;
        return e->type;
    }

    ada_type* program::class_wide(ada_type* aType)
    {
        auto* specific = aType->base_type();
        if (specific->partial != nullptr)
            specific = specific->partial->base_type();
        if (specific->class_wide == nullptr)
        {
            auto* cw = new_type(type_kind::ClassWide, specific->name + "'class");
            cw->specific = specific;
            cw->tagged = true;
            cw->limited = specific->limited;
            cw->constrained = false;
            cw->declared_in = specific->declared_in;
            specific->class_wide = cw;
            if (specific->full != nullptr)
                specific->full->base_type()->class_wide = cw;
        }
        // (S'Class, S constrained by a discriminant constraint: a subtype of T'Class)
        auto* v = aType->view();
        if (v->kind == type_kind::Record && !v->discriminant_constraint.empty() && v != specific->view())
        {
            auto& constrained = iConstrainedClassWide[aType];
            if (constrained == nullptr)
            {
                auto* cw = specific->class_wide;
                constrained = new_type(type_kind::ClassWide, cw->name);
                constrained->specific = cw->specific;
                constrained->tagged = true;
                constrained->limited = cw->limited;
                constrained->constrained = false;
                constrained->declared_in = cw->declared_in;
                constrained->base = cw;
                constrained->class_subtype = aType;
            }
            return constrained;
        }
        return specific->class_wide;
    }

    ada_type* program::subtype_indication(node* aIndication, region* aRegion, bool)
    {
        if (!aIndication->is(construct::SubtypeIndication))
            return subtype_mark(aIndication, aRegion);
        node* mark = nullptr;
        node* constraint = nullptr;
        for (auto* c : aIndication->children)
        {
            if (c->is(construct::Flag))
                continue;
            if (mark == nullptr)
                mark = c;
            else
                constraint = c;
        }
        // (a name with an index or discriminant constraint parsed as a name with arguments: T(1 .. 10))
        if (constraint == nullptr && mark->is(construct::Apply))
        {
            auto* t = subtype_mark(mark->child(0), aRegion);
            auto* result = constrain(t, mark, aRegion);
            aIndication->type = result;
            return result;
        }
        auto* t = subtype_mark(mark, aRegion);
        if (aIndication->has_flag("not null"))
        {
            t = new_subtype(t);
            t->not_null = true;
        }
        if (constraint != nullptr)
            t = constrain(t, constraint, aRegion);
        aIndication->type = t;
        return t;
    }

    ada_type* program::full_subtype(ada_type* aSubtype, ada_type* aFull)
    {
        // (a constrained subtype of a partial view: its full view (the full type, constrained likewise))
        auto* f = new_subtype(aFull);
        f->name = aSubtype->name;
        f->partial = aSubtype;
        if (aSubtype->constrained)
        {
            f->constrained = true;
            if (f->kind == type_kind::Array)
                f->indexes = aSubtype->indexes;
            else
            {
                f->discriminant_constraint = aSubtype->discriminant_constraint;
                f->discriminant_values = aSubtype->discriminant_values;
            }
        }
        if (f->is_scalar())
        {
            if (aSubtype->low.known())
                f->low = aSubtype->low;
            if (aSubtype->high.known())
                f->high = aSubtype->high;
            if (aSubtype->low_object != nullptr)
                f->low_object = aSubtype->low_object;
            if (aSubtype->high_object != nullptr)
                f->high_object = aSubtype->high_object;
        }
        return f;
    }

    ada_type* program::constrain(ada_type* aType, node* aConstraint, region* aRegion)
    {
        // (a partial view's constrained subtype: its full view too (else the full view's (unconstrained) is its view))
        auto* result = constrain_view(aType, aConstraint, aRegion);
        if (result != aType && result->kind == type_kind::Private && result->full != nullptr && result->full == aType->full &&
            (result->constrained || result->full->view()->is_scalar()))
            result->full = full_subtype(result, result->full->view());
        return result;
    }

    ada_type* program::constrain_view(ada_type* aType, node* aConstraint, region* aRegion)
    {
        auto* view = aType->view();
        switch (aConstraint->kind)
        {
        case construct::ConstraintRange:
            {
                auto* range = aConstraint->child(0);
                auto* result = new_subtype(aType);
                if (range->is(construct::Range))
                {
                    resolve(range->child(0), aType->base_type(), aRegion);
                    resolve(range->child(1), aType->base_type(), aRegion);
                    result->low = evaluate(range->child(0), aRegion);
                    result->high = evaluate(range->child(1), aRegion);
                    result->range_node = range;
                    if (!result->low.known() || !result->high.known())
                        dynamic_bounds(result, range->child(0), range->child(1), aRegion);
                    compatibility_check(range, aType, result);
                }
                else
                {
                    // (a range attribute: X'Range)
                    auto* bounds = range_of(range, aRegion);
                    result->low = bounds->low;
                    result->high = bounds->high;
                    result->low_object = bounds->low_object;
                    result->high_object = bounds->high_object;
                    result->range_node = range;
                    compatibility_check(range, aType, result);
                }
                return result;
            }
        case construct::ConstraintDigits:
        case construct::ConstraintDelta:
            {
                auto* result = new_subtype(aType);
                resolve(aConstraint->child(0), nullptr, aRegion);
                auto v = evaluate(aConstraint->child(0), aRegion);
                if (aConstraint->is(construct::ConstraintDigits) && v.is_integer())
                {
                    if (view->kind == type_kind::Decimal)
                        result->digits = v.integer().to_int64();
                    else
                        result->digits = v.integer().to_int64();
                }
                else if (v.known())
                    result->delta = v.real();
                // (a delta (digits) constraint whose delta is less (digits greater) than the subtype's: Constraint_Error when it is
                // elaborated (J.3(9))
                bool const incompatible = aConstraint->is(construct::ConstraintDelta) ? (v.known() && view->is_fixed() && v.real() < view->delta) :
                    (v.is_integer() && view->kind == type_kind::Float && view->digits > 0 && v.integer().to_int64() > view->digits);
                if (incompatible)
                    iCompatibilityChecks.push_back({ aConstraint->child(0), nullptr, nullptr });
                if (auto* r = aConstraint->child(1); r != nullptr && r->is(construct::Range))
                {
                    // (its range constraint: compatible with the subtype's (3.5(5)))
                    resolve(r->child(0), aType->base_type(), aRegion);
                    resolve(r->child(1), aType->base_type(), aRegion);
                    result->low = evaluate(r->child(0), aRegion);
                    result->high = evaluate(r->child(1), aRegion);
                    result->range_node = r;
                    if (!result->low.known() || !result->high.known())
                        dynamic_bounds(result, r->child(0), r->child(1), aRegion);
                    compatibility_check(r, aType, result);
                }
                else if (r != nullptr)
                {
                    // (its range constraint: compatible with the subtype's (3.5(5)))
                    auto* constrained = constrain(aType, r, aRegion);
                    result->low = constrained->low;
                    result->high = constrained->high;
                    result->low_object = constrained->low_object;
                    result->high_object = constrained->high_object;
                }
                return result;
            }
        case construct::ConstraintIndex:
        case construct::Apply:
            {
                std::vector<node*> items;
                for (auto* c : aConstraint->children)
                    if (aConstraint->is(construct::ConstraintIndex) || c != aConstraint->child(0))
                        items.push_back(c);
                auto* target = view;
                if (target->kind == type_kind::Access && target->designated != nullptr)
                {
                    // (a constrained access subtype: its designated subtype is constrained (a private type's: of its full view))
                    auto* result = new_subtype(aType->kind == type_kind::Private ? target : aType);
                    result->designated = constrain(target->designated, aConstraint, aRegion);
                    return result;
                }
                if (target->kind == type_kind::Array)
                {
                    auto* result = new_subtype(aType);
                    result->constrained = true;
                    result->indexes.clear();
                    std::size_t i = 0u;
                    for (auto* item : items)
                    {
                        auto* indexType = i < target->indexes.size() ? target->indexes[i] : nullptr;
                        result->indexes.push_back(discrete_range(item, indexType, aRegion));
                        if (indexType != nullptr)
                            compatibility_check(item, indexType, result->indexes.back());
                        ++i;
                    }
                    return result;
                }
                // (a discriminant constraint)
                auto* result = new_subtype(aType);
                result->constrained = true;
                result->discriminant_constraint.clear();
                result->discriminant_values.clear();
                std::vector<component_info*> discriminants;
                for (auto& c : target->components)
                    if (c.discriminant)
                        discriminants.push_back(&c);
                std::vector<node*> values(discriminants.size(), nullptr);
                std::size_t position = 0u;
                for (auto* item : items)
                {
                    if (item->is(construct::Association))
                    {
                        auto* choices = item->child(0);
                        for (auto* choice : choices->children)
                        {
                            auto const name = name_text(choice->source);
                            // (a formal type's discriminants' names, within the instance)
                            auto const& formalNames = aType->view()->formal_discriminants;
                            bool const formal = !formalNames.empty() && within_scope(aType->view()->formal_scope, aRegion);
                            for (std::size_t d = 0u; d < discriminants.size(); ++d)
                                if (formal ? d < formalNames.size() && formalNames[d] == name : discriminants[d]->ent->name == name)
                                {
                                    values[d] = item->children.back();
                                    choice->ent = discriminants[d]->ent;
                                }
                        }
                    }
                    else if (position < values.size())
                        values[position++] = item;
                }
                // (a constraint that depends on a discriminant: its values checked when an object is created (3.7.1(12)))
                for (std::size_t d = 0u; d < discriminants.size(); ++d)
                    if (values[d] != nullptr)
                        resolve(values[d], discriminants[d]->type, aRegion);
                bool dependent = false;
                for (auto* v : values)
                    dependent = dependent || (v != nullptr && refers_to_discriminant(v));
                for (std::size_t d = 0u; d < discriminants.size(); ++d)
                {
                    if (values[d] != nullptr && !evaluate(values[d], aRegion).known() && !refers_to_discriminant(values[d]) &&
                        discriminants[d]->type != nullptr && discriminants[d]->type->view()->is_scalar() &&
                        !(values[d]->is(construct::Identifier) && values[d]->ent != nullptr && values[d]->ent->is_object() &&
                          values[d]->ent->kind != entity_kind::Variable))   // (a variable's value: the one when the constraint is elaborated)
                    {
                        // (a value that is not static: evaluated where the constraint is elaborated (once), into a hidden constant (not in
                        // an allocator, that evaluates it itself))
                        bool allocator = false;
                        for (auto const* n = values[d]; n != nullptr && !allocator; n = n->parent)
                            allocator = n->is(construct::Allocator);
                        if (!allocator)
                        {
                            auto* object = new_entity(entity_kind::Constant, "%discriminant", values[d], aRegion);
                            object->type = values[d]->type != nullptr ? values[d]->type->base_type() : discriminants[d]->type;
                            object->initial = values[d];
                            object->captured = true;
                            aRegion->declarations.push_back(object);
                            iDynamicConstraints.push_back({ values[d], object });
                            auto& reference = iClones.emplace_back();
                            reference.kind = construct::Identifier;
                            reference.source = values[d]->source;
                            reference.file = values[d]->file;
                            reference.parent = values[d]->parent;
                            reference.ent = object;
                            reference.type = object->type;
                            set_meaning(&reference, meaning::Object);
                            values[d] = &reference;
                        }
                    }
                    result->discriminant_constraint.push_back(values[d]);
                    result->discriminant_values.push_back(values[d] != nullptr ? evaluate(values[d], aRegion) : static_value{});
                    // (a value in its discriminant's subtype (3.7.1(11)): checked when the constraint is elaborated, unless static and it is)
                    auto* dt = discriminants[d]->type != nullptr ? discriminants[d]->type->view() : nullptr;
                    if (values[d] != nullptr && dt != nullptr && dt->is_scalar())
                    {
                        auto const& v = result->discriminant_values.back();
                        bool const within = v.is_integer() && dt->low.is_integer() && dt->high.is_integer() && v.integer() >= dt->low.integer() &&
                            v.integer() <= dt->high.integer();
                        bool const anonymousAccess = values[d]->type != nullptr && values[d]->type->view()->is_access();
                        if (!within && !anonymousAccess && !dependent)
                            iCompatibilityChecks.push_back({ values[d], discriminants[d]->type, nullptr });
                    }
                }
                return result;
            }
        default:
            return aType;
        }
    }

    void program::fixed_base_range(ada_type* aFirst)
    {
        // (an ordinary fixed point type's base range: symmetric, enough to hold its first subtype's (and a little more), within its
        // representation's (a 64-bit count of smalls); again if its small is specified)
        auto* first = aFirst->view();
        auto* base = first->base_type()->view();
        if (base == first || !base->is_fixed() || base->kind == type_kind::Decimal || !first->low.known() || !first->high.known() || base->small.sign() <= 0)
            return;
        auto const magnitude = std::max(first->low.real().abs(), first->high.real().abs());
        base->low = static_value{ -(magnitude + base->small) };
        base->high = static_value{ magnitude + base->small };
        auto const lowest = rational{ -bigint::pow(bigint{ 2 }, 63u) } * base->small;
        auto const highest = rational{ bigint::pow(bigint{ 2 }, 63u) - bigint{ 1 } } * base->small;
        if (base->low.real() < lowest)
            base->low = static_value{ lowest };
        if (base->high.real() > highest)
            base->high = static_value{ highest };
    }

    void program::default_storage_pool(ada_type* aAccess, node* aDecl, region* aRegion)
    {
        // (the Default_Storage_Pool in effect where an access type is declared: its pool unless it specifies one (13.11.3(6/3)))
        char const* const where = aDecl != nullptr ? aDecl->source.data() : nullptr;
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
        {
            auto const pools = iDefaultPools.find(r);
            if (pools == iDefaultPools.end())
                continue;
            node* const* found = nullptr;
            bool any = false;
            for (auto const& [position, pool] : pools->second)
                if (position == nullptr || where == nullptr || position < where || (aDecl->file != nullptr && !aDecl->file->text.empty() &&
                    (position < aDecl->file->text.data() || position >= aDecl->file->text.data() + aDecl->file->text.size())))
                {
                    found = &pool;
                    any = true;
                }
            if (!any)
                continue;
            if (*found != nullptr)
            {
                iTypeAspects[aAccess]["storage_pool"] = *found;
                // (the pool object: in memory (its allocators may be in nested subprograms))
                for (auto* n = *found; n != nullptr; n = n->children.empty() ? nullptr : n->child(0))
                    if (n->ent != nullptr && n->ent->is_object())
                    {
                        n->ent->captured = true;
                        break;
                    }
            }
            return;
        }
    }

    bool program::per_object_access_constraint(component_info const& aComponent) const
    {
        auto* t = aComponent.type != nullptr ? aComponent.type->view() : nullptr;
        if (t == nullptr || t->kind != type_kind::Record || aComponent.discriminant)
            return false;
        for (auto const* v : t->discriminant_constraint)
            if (v != nullptr && v->type != nullptr && v->type->view()->is_access() && refers_to_discriminant(v))
                return true;
        return false;
    }

    std::vector<component_info const*> program::initialization_order(ada_type* aRecord)
    {
        // (the components in declaration order, but those with per-object access discriminant constraints last (3.3.1(20.1/3)))
        std::vector<component_info const*> result;
        auto* base = aRecord->view()->base_type()->view();
        for (auto const& c : base->components)
            if (!per_object_access_constraint(c))
                result.push_back(&c);
        for (auto const& c : base->components)
            if (per_object_access_constraint(c))
                result.push_back(&c);
        return result;
    }

    bool program::refers_to_discriminant(node const* aExpression) const
    {
        // (a constraint of a component that depends on a discriminant: checked when an object is created, not here)
        if (aExpression == nullptr)
            return false;
        if (aExpression->ent != nullptr && aExpression->ent->kind == entity_kind::Discriminant)
            return true;
        // (the name of the type being declared: its current instance (a per-object expression, 3.8(18/2)))
        if (aExpression->ent != nullptr && aExpression->ent->is_type())
            for (auto const* n = aExpression->parent; n != nullptr; n = n->parent)
                if (n->is(construct::Type) && n->ent != nullptr && (n->ent == aExpression->ent || (n->ent->name == aExpression->ent->name &&
                    aExpression->ent->type != nullptr && aExpression->ent->type->kind == type_kind::Private)))
                    return true;
        for (auto const* c : aExpression->children)
            if (refers_to_discriminant(c))
                return true;
        return false;
    }

    void program::compatibility_check(node* aRange, ada_type* aMark, ada_type* aSubtype)
    {
        // (a range that must be compatible with its type mark: checked when it is elaborated, unless it is static and is)
        if (aRange == nullptr || aMark == nullptr || aSubtype == nullptr || aMark->view()->kind == type_kind::Private || refers_to_discriminant(aRange))
            return;
        auto* m = aMark->view();
        auto* s = aSubtype->view();
        if (!m->is_scalar() || !s->is_scalar() || m->is_universal())
            return;
        if (s->low.known() && s->high.known() && m->low.known() && m->high.known())
        {
            auto const less = [](static_value const& a, static_value const& b)
            {
                if (a.is_integer() && b.is_integer())
                    return a.integer() < b.integer();
                return a.real() < b.real();
            };
            if (less(s->high, s->low) || (!less(s->low, m->low) && !less(m->high, s->high)))
                return;     // (null, or within: compatible)
        }
        else if (!s->low_object && !s->high_object && !(m->low_object || m->high_object))
            return;
        iCompatibilityChecks.push_back({ aRange, aMark, aSubtype });
    }

    ada_type* program::discrete_range(node* aRange, ada_type* aIndexType, region* aRegion)
    {
        // (a discrete range: L .. H (of the index type, or of the type of its bounds), a subtype indication, or X'Range)
        if (aRange->is(construct::Range))
        {
            auto* lowType = resolve_range_bounds(aRange, aIndexType, aRegion);
            // (of a private type: of its full view (a partial view's subtype would have the full view's range))
            if (lowType != nullptr && lowType->kind == type_kind::Private && lowType->full != nullptr && lowType->full->view()->is_scalar())
                lowType = lowType->full->view()->base_type();
            auto* result = new_subtype(lowType);
            result->low = evaluate(aRange->child(0), aRegion);
            result->high = evaluate(aRange->child(1), aRegion);
            result->range_node = aRange;
            if (!result->low.known() || !result->high.known())
                dynamic_bounds(result, aRange->child(0), aRange->child(1), aRegion);
            aRange->type = result;
            return result;
        }
        if (aRange->is(construct::SubtypeIndication) && aRange->size() == 1u && aRange->child(0)->is(construct::Attribute))
        {
            auto* t = discrete_range(aRange->child(0), aIndexType, aRegion);
            aRange->type = t;
            return t;
        }
        if (aRange->is(construct::SubtypeIndication))
        {
            auto* t = subtype_indication(aRange, aRegion);
            aRange->type = t;
            return t;
        }
        if (aRange->is(construct::Attribute))
        {
            auto const attribute = name_text(aRange->child(1)->source);
            if (attribute == "range" || attribute == "reverse_range")
            {
                auto* t = range_of(aRange, aRegion);
                aRange->type = t;
                return t;
            }
        }
        // (a subtype mark)
        auto* t = subtype_mark(aRange, aRegion);
        aRange->type = t;
        return t;
    }

    ada_type* program::resolve_range_bounds(node* aRange, ada_type* aExpected, region* aRegion)
    {
        auto* low = aRange->child(0);
        auto* high = aRange->child(1);
        ada_type* type = aExpected != nullptr ? aExpected->base_type() : nullptr;
        if (type == nullptr)
        {
            // (the type of the bounds: a type that both can have (universal_integer: Integer))
            auto const li = interpretations(low, aRegion);
            auto const hi = interpretations(high, aRegion);
            for (auto const& l : li)
            {
                if (l.type == nullptr || l.type->kind == type_kind::UniversalInteger || l.type->kind == type_kind::UniversalReal)
                    continue;
                for (auto const& h : hi)
                    if (covers(l.type, h.type))
                    {
                        type = l.type->base_type();
                        break;
                    }
                if (type != nullptr)
                    break;
            }
            if (type == nullptr)
                for (auto const& h : hi)
                    if (h.type != nullptr && h.type->kind != type_kind::UniversalInteger && h.type->kind != type_kind::UniversalReal)
                    {
                        type = h.type->base_type();
                        break;
                    }
            if (type == nullptr)
                type = iInteger;
        }
        resolve(low, type, aRegion);
        resolve(high, type, aRegion);
        return type;
    }

    void program::dynamic_bounds(ada_type* aSubtype, node* aLow, node* aHigh, region* aRegion)
    {
        // (bounds that are not static: hidden constants that hold them, elaborated where the subtype is)
        auto* lowObject = new_entity(entity_kind::Constant, "%low", aLow, aRegion);
        lowObject->type = aSubtype->base_type();
        lowObject->initial = aLow;
        auto* highObject = new_entity(entity_kind::Constant, "%high", aHigh, aRegion);
        highObject->type = aSubtype->base_type();
        highObject->initial = aHigh;
        // (in memory: nested subprograms may use the subtype's bounds)
        lowObject->captured = true;
        highObject->captured = true;
        aRegion->declarations.push_back(lowObject);
        aRegion->declarations.push_back(highObject);
        aSubtype->low_object = lowObject;
        aSubtype->high_object = highObject;
        aSubtype->low = static_value{};
        aSubtype->high = static_value{};
        iDynamicBounds.push_back({ aSubtype, aLow, aHigh, lowObject, highObject });
    }

    // --- type declarations

    void program::type_declaration(node* aDecl, region* aRegion)
    {
        auto* defining = aDecl->first(construct::Defining);
        auto const name = name_text(defining->source);
        // (the completion of an incomplete or private type declared earlier in the region)
        entity* partial = nullptr;
        if (!aDecl->is(construct::TypeIncomplete))
        {
            auto range = aRegion->names.equal_range(name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->is_type() && i->second->type->kind == type_kind::Private && i->second->type->full == nullptr && i->second->decl != defining)
                    partial = i->second;
            // (an incomplete type declared in the private part of the package whose body this is (3.10.1(3)))
            if (partial == nullptr && aRegion->owner != nullptr && aRegion->parent != nullptr && aRegion->parent->owner == aRegion->owner &&
                aRegion->owner->is_package())
            {
                auto outer = aRegion->parent->names.equal_range(name);
                for (auto i = outer.first; i != outer.second; ++i)
                    if (i->second->is_type() && i->second->type->kind == type_kind::Private && i->second->type->full == nullptr &&
                        i->second->decl != nullptr && i->second->decl->parent != nullptr && i->second->decl->parent->is(construct::TypeIncomplete))
                        partial = i->second;
            }
        }
        node* discriminants = aDecl->first(construct::Discriminants);
        if (discriminants == nullptr)
            discriminants = aDecl->first(construct::DiscriminantsUnknown);
        if (aDecl->is(construct::TypeIncomplete) || aDecl->is(construct::TypePrivate))
        {
            // (an incomplete or private type: a partial view (completed later))
            if (aDecl->is(construct::TypeIncomplete))
            {
                auto range = aRegion->names.equal_range(name);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->is_type())
                    {
                        aDecl->ent = i->second;
                        defining->ent = i->second;
                        return;     // (an incomplete view of a type already declared (e.g. in a private part))
                    }
            }
            auto* e = declare(entity_kind::Type, defining, aRegion);
            auto* t = new_type(type_kind::Private, name);
            t->decl = e;
            t->first_subtype = true;
            t->declared_in = aRegion;
            t->incomplete = aDecl->is(construct::TypeIncomplete);
            t->tagged = aDecl->has_flag("tagged");
            t->limited = aDecl->has_flag("limited");
            t->abstract = aDecl->has_flag("abstract");
            t->constrained = discriminants == nullptr;
            e->type = t;
            aDecl->ent = e;
            if (discriminants != nullptr)
            {
                auto* r = new_region(e, aRegion);
                discriminant_part(discriminants, t, r);
            }
            aspects(aDecl, e, aRegion);
            return;
        }
        if (aDecl->is(construct::TypePrivateExtension))
        {
            auto* e = declare(entity_kind::Type, defining, aRegion);
            node* parentNode = aDecl->first(construct::SubtypeIndication);
            auto* parent = subtype_indication(parentNode, aRegion);
            auto* t = derive(parent, name, e, aRegion, nullptr, nullptr);
            e->type = t;
            t->kind = type_kind::Private;
            t->tagged = true;
            if (auto* parentBase = parent->view()->base_type()->view(); parentBase->kind == type_kind::Interface)
            {
                // (a private extension of an interface: it implements the interface (and its progenitors))
                t->synchronized_interface = false;
                t->interfaces.push_back(parentBase);
                for (auto* i : parentBase->interfaces)
                    if (std::find(t->interfaces.begin(), t->interfaces.end(), i) == t->interfaces.end())
                        t->interfaces.push_back(i);
            }
            t->abstract = aDecl->has_flag("abstract");
            t->limited = aDecl->has_flag("limited") || parent->view()->limited;
            for (auto* i : aDecl->children)
                if (i != parentNode && (i->is(construct::Identifier) || i->is(construct::Selected)))
                    t->interfaces.push_back(subtype_mark(i, aRegion)->base_type());
            if (discriminants != nullptr)
                discriminant_part(discriminants, t, new_region(e, aRegion));
            aDecl->ent = e;
            aspects(aDecl, e, aRegion);
            return;
        }
        // (a full type declaration)
        auto* e = declare(entity_kind::Type, defining, aRegion);
        aDecl->ent = e;
        node* definition = nullptr;
        for (auto* c : aDecl->children)
            if (c != defining && c != discriminants && !c->is(construct::Aspects))
                definition = c;
        auto* t = type_definition(definition, name, e, aRegion, discriminants);
        e->type = t;
        if (partial != nullptr)
        {
            // (the full view of a partial view: the partial view's entity denotes the type (the full view is its completion))
            auto* p = partial->type;
            p->full = t;    // (the first subtype (e.g. a constrained array type's: constrained))
            // (a partial view whose first subtype is constrained (e.g. a private extension of a constrained subtype): its base's
            // full view too)
            if (p->base_type() != p && p->base_type()->full == nullptr)
                p->base_type()->full = t->base_type();
            // (its constrained subtypes declared before: their full views)
            for (std::size_t i = 0u, n = iTypes.size(); i < n; ++i)
            {
                auto& s = iTypes[i];
                if (&s != p && s.is_subtype && s.full == nullptr && s.base == p && s.kind == type_kind::Private && s.constrained)
                    s.full = full_subtype(&s, p->full);
            }
            t->base_type()->partial = p;
            if (t != t->base_type())
                t->partial = p;
            partial->completion = e;
            e->completion = nullptr;
            if (p->class_wide != nullptr)
                t->base_type()->class_wide = p->class_wide;
            // (the full view's inherited operations that the partial view's explicit ones override (declared in the visible
            // part): not inherited again (hidden: removed from the region))
            {
                auto& fullPrimitives = t->base_type()->primitives;
                for (auto* op : p->primitives)
                {
                    if (op == nullptr || op->sub == nullptr || op->sub->inherited)
                        continue;
                    for (auto i = fullPrimitives.begin(); i != fullPrimitives.end();)
                    {
                        auto* inherited = *i;
                        if (inherited != op && inherited->sub != nullptr && inherited->sub->inherited && inherited->name == op->name &&
                            type_conformant(*inherited->sub, *op->sub))
                        {
                            if (op->sub->slot < 0 && inherited->sub->slot >= 0)
                                op->sub->slot = inherited->sub->slot;
                            if (op->sub->parent_operation == nullptr)
                                op->sub->parent_operation = inherited->sub->parent_operation;
                            if (auto* r = inherited->scope)
                                for (auto n = r->names.begin(); n != r->names.end(); ++n)
                                    if (n->second == inherited)
                                    {
                                        r->names.erase(n);
                                        break;
                                    }
                            inherited->visible_in_body = false;
                            for (auto& slot : t->base_type()->slots)
                                if (slot.operation == inherited)
                                    slot.operation = op;
                            i = fullPrimitives.erase(i);
                        }
                        else
                            ++i;
                    }
                }
            }
            // (the partial view's primitives are the full type's)
            for (auto* op : p->primitives)
                if (std::find(t->base_type()->primitives.begin(), t->base_type()->primitives.end(), op) == t->base_type()->primitives.end())
                    t->base_type()->primitives.push_back(op);
            p->tagged = t->base_type()->tagged || p->tagged;
            // (and its dispatch table: the partial view's slots first)
            auto* full = t->base_type();
            if (p->parent == nullptr && full->parent != nullptr && !p->slots.empty() && full->tagged)
            {
                // (a tagged private type completed by a type extension: the full view's inherited slots first, then the partial
                // view's operations (overriding the inherited ones of the same name and profile, else in new slots))
                auto merged = full->slots;
                for (auto const& s : p->slots)
                {
                    auto* op = s.operation;
                    if (op == nullptr || op->sub == nullptr)
                        continue;
                    bool placed = false;
                    for (std::size_t i = 0u; i < merged.size() && !placed; ++i)
                    {
                        auto* inherited = merged[i].operation;
                        if (inherited != nullptr && inherited != op && inherited->name == op->name && inherited->sub != nullptr &&
                            inherited->sub->parameters.size() == op->sub->parameters.size() && (inherited->sub->result == nullptr) == (op->sub->result == nullptr))
                        {
                            merged[i] = s;
                            op->sub->slot = static_cast<std::int32_t>(i);
                            placed = true;
                        }
                    }
                    if (!placed)
                    {
                        op->sub->slot = static_cast<std::int32_t>(merged.size());
                        merged.push_back(s);
                    }
                }
                full->slots = merged;
            }
            else if (!p->slots.empty())
            {
                // (the partial view's operations (overriding in the visible part), except where the full view has its own
                // (overriding in the private part); the full view's inherited ones don't replace them)
                auto merged = p->slots.size() >= full->slots.size() ? p->slots : full->slots;
                auto* parentType = full->parent != nullptr ? full->parent->view()->base_type()->view() : nullptr;
                for (std::size_t i = 0u; i < merged.size(); ++i)
                {
                    auto* op = i < full->slots.size() ? full->slots[i].operation : nullptr;
                    auto* parentOp = parentType != nullptr && i < parentType->slots.size() ? parentType->slots[i].operation : nullptr;
                    bool const own = op != nullptr && op != parentOp && (op->sub == nullptr || !op->sub->inherited);
                    auto* partialOp = i < p->slots.size() ? p->slots[i].operation : nullptr;
                    // (the partial view's own operation (not its parent's, copied when the partial view was derived: the parent's
                    // can have been overridden since, in its private part))
                    bool const partialOwn = partialOp != nullptr && partialOp->sub != nullptr && !partialOp->sub->inherited &&
                        (partialOp->sub->controlling == p || partialOp->sub->controlling == full);
                    if (own)
                        merged[i] = full->slots[i];
                    else if (partialOwn || (partialOp != nullptr && op == nullptr))
                        merged[i] = p->slots[i];
                    else if (op != nullptr)
                        merged[i] = full->slots[i];
                }
                full->slots = merged;
            }
            p->slots = full->slots;
        }
        aspects(aDecl, e, aRegion);
    }

    void program::discriminant_part(node* aPart, ada_type* aType, region* aRegion)
    {
        if (aPart->is(construct::DiscriminantsUnknown))
        {
            aType->unknown_discriminants = true;
            aType->constrained = false;
            return;
        }
        aType->known_discriminants = true;
        aType->constrained = false;
        std::int32_t index = 0;
        for (auto* spec : aPart->children)
        {
            if (!spec->is(construct::Discriminant))
                continue;
            ada_type* type = nullptr;
            node* defaultExpression = nullptr;
            for (auto* c : spec->children)
            {
                if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects))
                    continue;
                if (type == nullptr)
                    type = c->is(construct::AccessDefinition) ? access_definition(c, aRegion) : subtype_mark(c, aRegion);
                else
                    defaultExpression = c;
            }
            if (type != nullptr && spec->has_flag("not null") && type->view()->is_access() && !type->view()->not_null)
            {
                // (a null exclusion)
                type = new_subtype(type->view());
                type->not_null = true;
            }
            for (auto* d : spec->all(construct::Defining))
            {
                auto* de = declare(entity_kind::Discriminant, d, aRegion);
                de->type = type;
                de->owner = aType->decl;
                de->index = index++;
                component_info info;
                info.ent = de;
                info.type = type;
                info.default_expression = defaultExpression;
                info.discriminant = true;
                aType->components.push_back(info);
            }
            if (defaultExpression != nullptr)
                resolve(defaultExpression, type, aRegion);
        }
        // (discriminants with defaults: objects of the type are unconstrained (mutable))
    }

    ada_type* program::type_definition(node* aDefinition, std::string const& aName, entity* aEntity, region* aRegion, node* aDiscriminants)
    {
        ada_type* result = nullptr;
        auto const finish = [&](ada_type* aType)
        {
            aType->decl = aEntity;
            aType->declared_in = aRegion;
            aType->first_subtype = true;
            if (aType->base_type() != aType)
            {
                aType->base_type()->decl = aEntity;
                aType->base_type()->declared_in = aRegion;
                aType->base_type()->name = aName;
            }
            return aType;
        };
        switch (aDefinition->kind)
        {
        case construct::TypeEnumeration:
            {
                auto* t = new_type(type_kind::Enumeration, aName);
                std::int64_t position = 0;
                for (auto* literal : aDefinition->children)
                {
                    auto* le = declare(entity_kind::EnumerationLiteral, literal, aRegion);
                    le->type = t;
                    le->owner = aEntity;
                    le->value = static_value{ bigint{ position++ } };
                    if (literal->is(construct::DefiningCharacter))
                        t->character_type = true;
                    t->literals.push_back(le);
                }
                t->low = static_value{ bigint{ 0 } };
                t->high = static_value{ bigint{ position - 1 } };
                t->size = position <= 256 ? 1u : position <= 65536 ? 2u : 4u;
                t->alignment = *t->size;
                result = finish(t);
                break;
            }
        case construct::TypeInteger:
            {
                resolve_in_class(aDefinition->child(0), aRegion, [](ada_type const* t) { return t->is_integer(); });
                resolve_in_class(aDefinition->child(1), aRegion, [](ada_type const* t) { return t->is_integer(); });
                auto low = evaluate(aDefinition->child(0), aRegion);
                auto high = evaluate(aDefinition->child(1), aRegion);
                if (!low.is_integer() || !high.is_integer())
                    error(aDefinition, "integer type's bounds must be static integers");
                auto const bytes = machine_bytes(low.integer(), high.integer(), false);
                auto* base = new_type(type_kind::Integer, aName);
                base->low = static_value{ -bigint::pow(bigint{ 2 }, bytes * 8u - 1u) };
                base->high = static_value{ bigint::pow(bigint{ 2 }, bytes * 8u - 1u) - bigint{ 1 } };
                base->size = bytes;
                base->alignment = bytes;
                auto* first = new_subtype(base, aName);
                first->low = low;
                first->high = high;
                result = finish(first);
                break;
            }
        case construct::TypeModular:
            {
                resolve_in_class(aDefinition->child(0), aRegion, [](ada_type const* t) { return t->is_integer(); });
                auto modulus = evaluate(aDefinition->child(0), aRegion);
                if (!modulus.is_integer())
                    error(aDefinition, "modulus must be a static integer");
                auto* t = new_type(type_kind::Modular, aName);
                t->modulus = modulus.integer();
                t->low = static_value{ bigint{ 0 } };
                t->high = static_value{ modulus.integer() - bigint{ 1 } };
                t->size = machine_bytes(bigint{ 0 }, modulus.integer() - bigint{ 1 }, true);
                t->alignment = *t->size;
                result = finish(t);
                break;
            }
        case construct::TypeFloat:
            {
                resolve_in_class(aDefinition->child(0), aRegion, [](ada_type const* t) { return t->is_integer(); });
                auto digits = evaluate(aDefinition->child(0), aRegion);
                if (!digits.is_integer())
                    error(aDefinition, "digits must be a static integer");
                auto* base = new_type(type_kind::Float, aName);
                base->digits = digits.integer().to_int64();
                bool const single = base->digits <= 6;
                base->size = single ? 4u : 8u;
                base->alignment = *base->size;
                base->low = static_value{ rational::from_double(single ? -3.4028234663852886e38 : -1.7976931348623157e308) };
                base->high = static_value{ rational::from_double(single ? 3.4028234663852886e38 : 1.7976931348623157e308) };
                auto* first = new_subtype(base, aName);
                if (auto* r = aDefinition->child(1))
                {
                    // (the bounds: of any real type (RM 3.5.7(5)))
                    resolve_in_class(r->child(0), aRegion, [](ada_type const* t) { return t->is_real(); });
                    resolve_in_class(r->child(1), aRegion, [](ada_type const* t) { return t->is_real(); });
                    first->low = evaluate(r->child(0), aRegion);
                    first->high = evaluate(r->child(1), aRegion);
                    first->range_node = r;
                    if (!first->low.known() || !first->high.known())
                        dynamic_bounds(first, r->child(0), r->child(1), aRegion);
                }
                result = finish(first);
                break;
            }
        case construct::TypeFixed:
        case construct::TypeDecimal:
            {
                bool const decimal = aDefinition->is(construct::TypeDecimal);
                resolve_in_class(aDefinition->child(0), aRegion, [](ada_type const* t) { return t->is_real(); });
                auto delta = evaluate(aDefinition->child(0), aRegion);
                if (!delta.known())
                    error(aDefinition, "delta must be static");
                auto* base = new_type(decimal ? type_kind::Decimal : type_kind::Fixed, aName);
                base->delta = delta.real();
                base->small = decimal ? delta.real() : power_of_two_at_most(delta.real());
                base->size = 8u;
                base->alignment = 8u;
                node* rangeNode = nullptr;
                if (decimal)
                {
                    resolve_in_class(aDefinition->child(1), aRegion, [](ada_type const* t) { return t->is_integer(); });
                    auto digits = evaluate(aDefinition->child(1), aRegion);
                    base->digits = digits.is_integer() ? digits.integer().to_int64() : 18;
                    // (scale: the power of ten of delta)
                    std::int64_t scale = 0;
                    for (rational d = base->delta; d < rational{ bigint{ 1 } } && scale < 40; d = d * rational{ bigint{ 10 } })
                        ++scale;
                    base->scale = scale;
                    auto const limit = rational{ bigint::pow(bigint{ 10 }, static_cast<std::uint64_t>(base->digits)) - bigint{ 1 } } * base->delta;
                    base->low = static_value{ -limit };
                    base->high = static_value{ limit };
                    rangeNode = aDefinition->child(2);
                }
                else
                {
                    rangeNode = aDefinition->child(1);
                }
                auto* first = new_subtype(base, aName);
                if (rangeNode != nullptr)
                {
                    // (the bounds: of any real type (RM 3.5.9(6)))
                    resolve_in_class(rangeNode->child(0), aRegion, [](ada_type const* t) { return t->is_real(); });
                    resolve_in_class(rangeNode->child(1), aRegion, [](ada_type const* t) { return t->is_real(); });
                    first->low = evaluate(rangeNode->child(0), aRegion);
                    first->high = evaluate(rangeNode->child(1), aRegion);
                    first->range_node = rangeNode;
                    if (!decimal)
                        fixed_base_range(first);
                }
                result = finish(first);
                break;
            }
        case construct::TypeArray:
        case construct::TypeArrayUnconstrained:
            {
                auto* t = new_type(type_kind::Array, aName);
                auto* componentDefinition = aDefinition->first(construct::ComponentDefinition);
                t->aliased_components = componentDefinition->has_flag("aliased");
                for (auto* c : componentDefinition->children)
                    if (!c->is(construct::Flag))
                        t->component = c->is(construct::AccessDefinition) ? access_definition(c, aRegion) : subtype_indication(c, aRegion);
                if (aDefinition->is(construct::TypeArrayUnconstrained))
                {
                    for (auto* index : aDefinition->children)
                        if (index->is(construct::IndexSubtype))
                            t->indexes.push_back(subtype_mark(index->child(0), aRegion));
                    t->constrained = false;
                    result = finish(t);
                }
                else
                {
                    // (a constrained array type: an anonymous unconstrained type, and its first subtype constrained)
                    std::vector<ada_type*> ranges;
                    for (auto* index : aDefinition->children)
                        if (index != componentDefinition)
                            ranges.push_back(discrete_range(index, nullptr, aRegion));
                    // (its index subtypes: the ranges' (3.6(13)))
                    for (auto* r : ranges)
                        t->indexes.push_back(r);
                    t->constrained = false;
                    auto* first = new_subtype(t, aName);
                    first->indexes = ranges;
                    first->constrained = true;
                    result = finish(first);
                }
                break;
            }
        case construct::TypeRecord:
            {
                auto* t = new_type(type_kind::Record, aName);
                t->tagged = aDefinition->has_flag("tagged");
                t->limited = aDefinition->has_flag("limited");
                t->abstract = aDefinition->has_flag("abstract");
                t->decl = aEntity;
                auto* r = new_region(aEntity, aRegion);
                if (aDiscriminants != nullptr)
                    discriminant_part(aDiscriminants, t, r);
                if (auto* components = aDefinition->first(construct::Components))
                    component_list(components, t, r, -1);
                if (t->tagged)
                    t->class_wide = nullptr;
                result = finish(t);
                break;
            }
        case construct::TypeDerived:
            {
                auto* parentNode = aDefinition->first(construct::SubtypeIndication);
                // (the parent subtype's constraint can name the new discriminants)
                region* parentRegion = aRegion;
                if (aDiscriminants != nullptr && !aDiscriminants->is(construct::DiscriminantsUnknown))
                {
                    parentRegion = new_region(aEntity, aRegion);
                    auto* scratch = new_type(type_kind::Record, aName);
                    discriminant_part(aDiscriminants, scratch, parentRegion);
                }
                auto* parent = subtype_indication(parentNode, parentRegion);
                auto* extension = aDefinition->first(construct::Extension);
                auto* t = derive(parent, aName, aEntity, aRegion, extension, aDiscriminants);
                t->abstract = aDefinition->has_flag("abstract");
                // (derived from a formal type with known discriminants: its discriminants' names (within the instance))
                if (aDiscriminants == nullptr && !parent->view()->formal_discriminants.empty())
                {
                    t->formal_discriminants = parent->view()->formal_discriminants;
                    t->formal_scope = parent->view()->formal_scope;
                }
                // (the parent's Size: inherited (13.1(15/3)), unless it is specified for the derived type)
                if (!t->size_clause && extension == nullptr)
                    for (auto* p : { parent, parent->view() })
                        if (p->size_clause)
                        {
                            t->size_clause = p->size_clause;
                            break;
                        }
                if (auto* interfaces = aDefinition->first(construct::Interfaces))
                    for (auto* i : interfaces->children)
                        t->base_type()->interfaces.push_back(subtype_mark(i, aRegion)->base_type());
                result = t;
                result->decl = aEntity;
                result->first_subtype = true;
                break;
            }
        case construct::TypeAccess:
            {
                auto* t = new_type(type_kind::Access, aName);
                t->access_all = aDefinition->has_flag("all");
                t->access_constant = aDefinition->has_flag("constant");
                t->size = 8u;
                if (auto* parent = aDefinition->parent; parent != nullptr && parent->has_flag("not null"))
                    t->not_null = true;
                result = finish(t);
                // (the designated subtype (may be an incomplete type))
                t->designated = subtype_indication(aDefinition->first(construct::SubtypeIndication), aRegion);
                default_storage_pool(t, aDefinition, aRegion);
                break;
            }
        case construct::TypeAccessSubprogram:
            {
                auto* t = new_type(type_kind::AccessSubprogram, aName);
                t->protected_access = aDefinition->has_flag("protected");
                t->size = 8u;
                auto* profile = aDefinition->first(construct::Profile);
                t->profile = profile_of(profile, aRegion);
                result = finish(t);
                break;
            }
        case construct::TypeInterface:
            {
                auto* t = new_type(type_kind::Interface, aName);
                t->tagged = true;
                t->abstract = true;
                t->limited = aDefinition->has_flag("limited") || aDefinition->has_flag("task") || aDefinition->has_flag("protected") || aDefinition->has_flag("synchronized");
                t->synchronized_interface = aDefinition->has_flag("task") || aDefinition->has_flag("protected") || aDefinition->has_flag("synchronized");
                if (auto* interfaces = aDefinition->first(construct::Interfaces))
                    for (auto* i : interfaces->children)
                        t->interfaces.push_back(subtype_mark(i, aRegion)->base_type());
                result = finish(t);
                break;
            }
        default:
            error(aDefinition, "unsupported type definition");
        }
        return result;
    }

    subprogram_info* program::profile_of(node* aProfile, region* aRegion)
    {
        // (an access-to-subprogram's profile: a subprogram entity of its own (its parameters in a region of their own))
        auto* e = new_entity(aProfile->source.substr(0u, 8u) == "function" ? entity_kind::Function : entity_kind::Procedure, "%profile", aProfile, aRegion);
        auto* parameters = new_region(e, aRegion);
        parameters->level = aRegion->level + 1;
        subprogram_specification(aProfile, e, parameters);
        return e->sub.get();
    }

    ada_type* program::access_definition(node* aDefinition, region* aRegion)
    {
        if (auto* profile = aDefinition->first(construct::Profile))
        {
            auto* t = new_type(type_kind::AccessSubprogram, {});
            t->anonymous_access = true;
            t->protected_access = aDefinition->has_flag("protected");
            t->not_null = aDefinition->has_flag("not null") || (aDefinition->parent != nullptr && aDefinition->parent->has_flag("not null"));
            t->profile = profile_of(profile, aRegion);
            t->size = 8u;
            t->declared_in = aRegion;
            return t;
        }
        auto* t = new_type(type_kind::Access, {});
        t->anonymous_access = true;
        t->access_all = true;
        t->access_constant = aDefinition->has_flag("constant");
        t->not_null = aDefinition->has_flag("not null");
        t->size = 8u;
        t->declared_in = aRegion;
        for (auto* c : aDefinition->children)
            if (!c->is(construct::Flag))
                t->designated = subtype_mark(c, aRegion);
        return t;
    }

    void program::component_list(node* aComponents, ada_type* aType, region* aRegion, int aVariant)
    {
        for (auto* c : aComponents->children)
        {
            if (c->is(construct::Component))
            {
                auto* definition = c->first(construct::ComponentDefinition);
                ada_type* type = nullptr;
                node* indication = nullptr;
                for (auto* d : definition->children)
                    if (!d->is(construct::Flag))
                    {
                        type = d->is(construct::AccessDefinition) ? access_definition(d, aRegion) : subtype_indication(d, aRegion);
                        indication = d->is(construct::AccessDefinition) ? nullptr : d;
                    }
                node* defaultExpression = nullptr;
                for (auto* d : c->children)
                    if (!d->is(construct::Defining) && d != definition && !d->is(construct::Aspects))
                        defaultExpression = d;
                bool first = true;
                for (auto* d : c->all(construct::Defining))
                {
                    auto* ce = declare(entity_kind::Component, d, aRegion);
                    // (a multiple component declaration is a series of single ones (3.8(9)): each component's subtype indication
                    // elaborated for it)
                    if (!first && indication != nullptr && indication->is(construct::SubtypeIndication) &&
                        !(indication->size() == 1u && !indication->child(0)->is(construct::Apply)))
                    {
                        auto* own = clone(indication);
                        own->parent = indication->parent;
                        type = subtype_indication(own, aRegion);
                    }
                    first = false;
                    ce->type = type;
                    ce->owner = aType->decl;
                    ce->index = static_cast<std::int32_t>(aType->components.size());
                    ce->aliased = definition->has_flag("aliased");
                    component_info info;
                    info.ent = ce;
                    info.type = type;
                    info.default_expression = defaultExpression;
                    info.variant = aVariant;
                    info.aliased = ce->aliased;
                    aType->components.push_back(info);
                }
                if (defaultExpression != nullptr)
                    resolve(defaultExpression, type, aRegion);
            }
            else if (c->is(construct::Variants))
            {
                auto* discriminantName = c->child(0);
                auto* discriminant = resolve_entity(discriminantName, aRegion);
                for (auto* v : c->children)
                {
                    if (!v->is(construct::Variant))
                        continue;
                    variant_info info;
                    info.choices = v->child(0);
                    info.parent = aVariant;
                    info.discriminant = discriminant;
                    for (auto* choice : info.choices->children)
                    {
                        if (choice->is(construct::Others))
                        {
                            info.others = true;
                            continue;
                        }
                        auto [low, high] = choice_range(choice, discriminant != nullptr ? discriminant->type : nullptr, aRegion);
                        if (low && high)
                            info.ranges.emplace_back(*low, *high);
                    }
                    auto const index = static_cast<int>(aType->variants.size());
                    aType->variants.push_back(info);
                    if (auto* list = v->first(construct::Components))
                        component_list(list, aType, aRegion, index);
                }
            }
            else if (c->is(construct::Pragma))
                pragma(c, aRegion);
            else if (c->is(construct::RepresentationAttribute) || c->is(construct::RepresentationRecord))
                representation_clause(c, aRegion);
        }
    }

    std::pair<std::optional<bigint>, std::optional<bigint>> program::choice_range(node* aChoice, ada_type* aType, region* aRegion)
    {
        // (a discrete choice's values: an expression, a range, or a subtype)
        if (aChoice->is(construct::Range))
        {
            resolve(aChoice->child(0), aType, aRegion);
            resolve(aChoice->child(1), aType, aRegion);
            auto low = evaluate(aChoice->child(0), aRegion);
            auto high = evaluate(aChoice->child(1), aRegion);
            if (low.is_integer() && high.is_integer())
                return { low.integer(), high.integer() };
            return {};
        }
        if (aChoice->is(construct::SubtypeIndication) || is_subtype_name(aChoice, aRegion))
        {
            auto* t = discrete_range(aChoice, aType, aRegion);
            return { low_bound(t), high_bound(t) };
        }
        if (aChoice->is(construct::Attribute) && name_text(aChoice->child(1)->source) == "range")
        {
            auto* t = range_of(aChoice, aRegion);
            return { low_bound(t), high_bound(t) };
        }
        resolve(aChoice, aType, aRegion);
        auto v = evaluate(aChoice, aRegion);
        if (v.is_integer())
            return { v.integer(), v.integer() };
        return {};
    }

    ada_type* program::derive(ada_type* aParent, std::string const& aName, entity* aEntity, region* aRegion, node* aExtension, node* aDiscriminants)
    {
        auto* parentView = aParent->view();
        auto* parentBase = parentView->base_type()->view();
        auto* t = new_type(parentBase->kind, aName);
        auto const id = t->id;
        *t = *parentBase;
        t->id = id;
        t->name = aName;
        t->decl = aEntity;
        t->base = t;
        t->parent = aParent;
        t->root = parentBase->root != nullptr ? parentBase->root : parentBase;
        t->full = nullptr;
        t->partial = nullptr;
        t->is_subtype = false;
        t->first_subtype = true;
        t->declared_in = aRegion;
        t->class_wide = nullptr;
        t->primitives.clear();
        t->literals.clear();
        t->interfaces.clear();
        t->is_formal = false;
        if (parentBase->kind == type_kind::Interface && aExtension != nullptr)
        {
            // (a record extension of an interface: a tagged record type that implements it (and its progenitors) (3.9.4(9/2)))
            t->kind = type_kind::Record;
            t->tagged = true;
            t->synchronized_interface = false;
            t->interfaces.push_back(parentBase);
            for (auto* i : parentBase->interfaces)
                if (std::find(t->interfaces.begin(), t->interfaces.end(), i) == t->interfaces.end())
                    t->interfaces.push_back(i);
        }
        auto* r = new_region(aEntity, aRegion);
        // (enumeration literals: the derived type's own (the same positions))
        if (t->kind == type_kind::Enumeration && !t->standard_character)
            for (auto* literal : parentBase->literals)
            {
                auto* le = new_entity(entity_kind::EnumerationLiteral, literal->name, literal->decl, aRegion);
                le->type = t;
                le->value = literal->value;
                le->owner = aEntity;
                // (implicitly declared where the derived type is: in a private part, private)
                le->is_private = aRegion->private_start != static_cast<std::size_t>(-1) && aRegion->declarations.size() >= aRegion->private_start;
                aRegion->add(le);
                t->literals.push_back(le);
            }
        // (records: the components (a new discriminant part replaces the parent's discriminants); an extension's own)
        if (t->kind == type_kind::Record || t->kind == type_kind::Private)
        {
            if (aDiscriminants != nullptr)
            {
                // (the parent's discriminants: constrained by the parent subtype (its components whose bounds are discriminants: 
                // of the constraint's values))
                std::vector<std::pair<entity*, static_value>> constrained;
                {
                    std::size_t d = 0u;
                    for (auto const& c : t->components)
                        if (c.discriminant)
                        {
                            if (d < parentView->discriminant_values.size() && parentView->discriminant_values[d].known())
                                constrained.emplace_back(c.ent, parentView->discriminant_values[d]);
                            ++d;
                        }
                }
                auto const value_of = [&](entity* aObject) -> static_value const*
                {
                    if (aObject == nullptr || aObject->initial == nullptr || aObject->initial->ent == nullptr)
                        return nullptr;
                    for (auto const& [discriminant, value] : constrained)
                        if (discriminant == aObject->initial->ent)
                            return &value;
                    return nullptr;
                };
                std::vector<component_info> inherited;
                for (auto const& c : t->components)
                    if (!c.discriminant)
                    {
                        inherited.push_back(c);
                        auto* ct = c.type != nullptr ? c.type->view() : nullptr;
                        if (ct != nullptr && ct->kind == type_kind::Array && ct->constrained && !constrained.empty())
                        {
                            bool substituted = false;
                            auto* nt = new_subtype(ct);
                            nt->indexes.clear();
                            for (auto* index : ct->indexes)
                            {
                                auto* iv = index->view();
                                auto const* low = value_of(iv->low_object);
                                auto const* high = value_of(iv->high_object);
                                if (low == nullptr && high == nullptr)
                                {
                                    nt->indexes.push_back(index);
                                    continue;
                                }
                                auto* ni = new_subtype(index->base_type());
                                ni->low = low != nullptr ? *low : iv->low;
                                ni->low_object = low != nullptr ? nullptr : iv->low_object;
                                ni->high = high != nullptr ? *high : iv->high;
                                ni->high_object = high != nullptr ? nullptr : iv->high_object;
                                nt->indexes.push_back(ni);
                                substituted = true;
                            }
                            if (substituted)
                            {
                                inherited.back().type = nt;
                                inherited.back().depends = false;
                                inherited.back().dynamic = false;
                            }
                        }
                    }
                // (the parent's discriminants: each as one of the new discriminants (the first that the constraint names it with; a
                // tagged type's where the parent's objects have it (its view is the object): see program::layout), else stored (the
                // constraint's value: e.g. for the parent's variant parts and views))
                std::vector<component_info> stored;
                {
                    auto const& constraint = parentView->discriminant_constraint;
                    std::set<std::string> named;
                    std::size_t k = 0u;
                    for (auto const& pc : parentView->base_type()->view()->components)
                        if (pc.discriminant)
                        {
                            node* value = k < constraint.size() ? constraint[k] : nullptr;
                            ++k;
                            if (value != nullptr && value->is(construct::Association))
                                value = value->children.back();
                            if (value == nullptr)
                                continue;
                            if (value->is(construct::Identifier) && value->ent != nullptr && value->ent->kind == entity_kind::Discriminant &&
                                named.insert(value->ent->name).second)
                                continue;
                            component_info s = pc;
                            s.discriminant = false;
                            s.stored = true;
                            s.default_expression = value;
                            s.variant = -1;
                            s.offset.reset();
                            stored.push_back(s);
                        }
                }
                t->components.clear();
                t->size.reset();
                t->dynamic_size = false;
                discriminant_part(aDiscriminants, t, r);
                // (a new discriminant that the constraint names (it appears alone, 3.7(18)): this one (the constraint was resolved
                // with an earlier entity of its name))
                for (auto& c : stored)
                {
                    if (c.default_expression->is(construct::Identifier) && c.default_expression->ent != nullptr &&
                        c.default_expression->ent->kind == entity_kind::Discriminant)
                        for (auto const& o : t->components)
                            if (o.discriminant && o.ent != nullptr && o.ent->name == c.default_expression->ent->name)
                                c.default_expression->ent = o.ent;
                    t->components.push_back(c);
                }
                for (auto const& c : inherited)
                    t->components.push_back(c);
            }
            for (auto const& c : t->components)
                if (c.ent != nullptr && !c.stored)
                    r->add(c.ent);
            if (aExtension != nullptr)
            {
                t->tagged = true;
                if (auto* list = aExtension->first(construct::Components))
                    component_list(list, t, r, -1);
                if (aExtension->has_flag("private"))
                    t->kind = type_kind::Private;
            }
        }
        // (the first subtype: constrained as the parent subtype is)
        ada_type* result = t;
        if (aParent->is_subtype || (aParent->low.known() && t->is_scalar()) || (aParent->kind == type_kind::Array && aParent->constrained) ||
            (!aParent->discriminant_constraint.empty() && aDiscriminants == nullptr))
        {
            if (t->is_scalar())
            {
                // (the base range: the parent's base range)
                t->low = parentBase->low;
                t->high = parentBase->high;
                result = new_subtype(t, aName);
                result->low = aParent->view()->low;
                result->high = aParent->view()->high;
                result->low_object = aParent->view()->low_object;
                result->high_object = aParent->view()->high_object;
                result->digits = aParent->view()->digits;
                result->delta = aParent->view()->delta;     // (a delta constraint's)
            }
            else if (aParent->view()->kind == type_kind::Array && aParent->view()->constrained)
            {
                t->constrained = false;
                result = new_subtype(t, aName);
                result->indexes = aParent->view()->indexes;
                result->constrained = true;
                t->indexes.clear();
                for (auto* i : result->indexes)
                    t->indexes.push_back(i->base_type());
            }
            else if (!aParent->view()->discriminant_constraint.empty() && aDiscriminants == nullptr)
            {
                result = new_subtype(t, aName);
                result->discriminant_constraint = aParent->view()->discriminant_constraint;
                result->discriminant_values = aParent->view()->discriminant_values;
                result->constrained = true;
            }
            else if (aParent->view()->kind == type_kind::Access && aParent->view()->designated != parentBase->designated)
            {
                // (a constrained access subtype: its designated subtype's constraint)
                result = new_subtype(t, aName);
                result->designated = aParent->view()->designated;
            }
        }
        if (result != t)
        {
            result->decl = aEntity;
            result->declared_in = aRegion;
        }
        t->boolean = parentBase->boolean;
        derive_operations(t, parentBase, aRegion);
        return result;
    }

    void program::derive_operations(ada_type* aDerived, ada_type* aParent, region* aRegion)
    {
        // (the parent's primitive operations: inherited, their parameters and results of the parent type replaced by the derived type)
        // (the partial view's operations first (they include the ones overriding in the visible part), then the full view's that
        // aren't the same operation)
        std::vector<entity*> primitives;
        if (aParent->partial != nullptr)
            primitives = aParent->partial->primitives;
        for (auto* op : aParent->primitives)
        {
            if (std::find(primitives.begin(), primitives.end(), op) != primitives.end())
                continue;
            bool replaced = false;
            if (op->sub != nullptr && op->sub->inherited)
                for (auto* other : primitives)
                    if (other->name == op->name && other->sub != nullptr && type_conformant(*other->sub, *op->sub))
                        replaced = true;
            if (!replaced)
                primitives.push_back(op);
        }
        auto const substitute = [&](ada_type* aType) -> ada_type*
        {
            if (aType == nullptr)
                return nullptr;
            // (a subtype of the parent type other than the parent subtype: the corresponding subtype of the derived type (its
            // constraint, 3.4(18/3)))
            if (same_type(aType, aParent) && aType->view() != aParent->view() && aType->view()->is_subtype && aType->view()->is_scalar() &&
                aType->view() != aParent->view()->base_type()->view())
            {
                auto* v = aType->view();
                auto* corresponding = new_subtype(aDerived->base_type(), v->name);
                corresponding->is_subtype = true;
                corresponding->low = v->low;
                corresponding->high = v->high;
                corresponding->low_object = v->low_object;
                corresponding->high_object = v->high_object;
                corresponding->digits = v->digits;
                corresponding->delta = v->delta;
                return corresponding;
            }
            if (same_type(aType, aParent))
                return aDerived;
            if (aType->anonymous_access && aType->designated != nullptr && same_type(aType->designated, aParent))
            {
                auto* a = new_type(type_kind::Access, {});
                *a = *aType;
                a->id = iNextTypeId++;
                a->base = a;
                a->designated = aDerived;
                return a;
            }
            return aType;
        };
        // (an untagged parent's operation overridden in its private part: the overriding one where that is visible, else the
        // inherited one)
        bool const privateVisible = aParent->declared_in != nullptr && aParent->declared_in->owner != nullptr && aParent->declared_in->owner->is_package() &&
            private_part_visible(aParent->declared_in->owner, aRegion);
        std::vector<entity*> hiding;
        for (auto* op : primitives)
            if (op->sub != nullptr && op->sub->hidden_by != nullptr)
                hiding.push_back(op->sub->hidden_by);
        std::erase_if(primitives, [&](entity* aOperation)
        {
            if (aParent->tagged)
                return false;   // (both have their slots)
            if (aOperation->sub != nullptr && aOperation->sub->hidden_by != nullptr)
                return privateVisible;
            return !privateVisible && std::find(hiding.begin(), hiding.end(), aOperation) != hiding.end();
        });
        // (derived from an untagged formal type's actual in an instance: the actual's user-defined operations it inherits are not
        // visible within the instance (12.3(15)); the predefined "=" of a record type is composed of user-defined ones anyway)
        region* const formalInstance = aParent->tagged ? nullptr : formal_actual_instance(aParent, aRegion);
        // (an operation of the parent that overrides one that the parent inherits (its slot is its parent's))
        auto* grandparent = aParent->view()->base_type()->view()->parent;
        std::size_t const inheritedSlots = grandparent != nullptr ? grandparent->view()->base_type()->view()->slots.size() : 0u;
        auto const overrides = [&](entity* aOperation)
        {
            return aOperation->sub->slot >= 0 && static_cast<std::size_t>(aOperation->sub->slot) < inheritedSlots;
        };
        for (auto* op : primitives)
        {
            if (op->sub == nullptr)
                continue;
            // (overridden later by an explicit declaration: see add_primitive)
            auto* inherited = new_entity(op->kind, op->name, op->decl, aRegion);
            // (implicitly declared where the derived type is: in a private part, private)
            inherited->is_private = aRegion->private_start != static_cast<std::size_t>(-1) && aRegion->declarations.size() >= aRegion->private_start;
            inherited->sub->parameters = op->sub->parameters;
            for (auto& p : inherited->sub->parameters)
                p.type = substitute(p.type);
            inherited->sub->result = substitute(op->sub->result);
            inherited->type = inherited->sub->result;
            inherited->sub->inherited = true;
            inherited->sub->instance_hidden = op->sub->instance_hidden;
            if (inherited->sub->instance_hidden == nullptr && formalInstance != nullptr && (op->sub->op == intrinsic::None || op->sub->op == intrinsic::NotEqual) &&
                !(aParent->kind == type_kind::Record && (op->name == "=" || op->name == "/=")))
                inherited->sub->instance_hidden = formalInstance;
            inherited->sub->parent_operation = op->sub->parent_operation != nullptr && op->sub->inherited ? op->sub->parent_operation : op;
            inherited->sub->derived_from = aParent;
            inherited->sub->op = op->sub->op;
            inherited->sub->operator_type = op->sub->op != intrinsic::None ? aDerived : nullptr;
            inherited->sub->import_name = op->sub->import_name;
            inherited->sub->is_abstract = op->sub->is_abstract;
            inherited->sub->slot = op->sub->slot;
            inherited->sub->controlling = aDerived->tagged ? aDerived : nullptr;
            inherited->sub->level = op->sub->level;
            inherited->inner = op->inner;
            if (aDerived->tagged && !aDerived->limited && op->name == "=" && op->sub->op == intrinsic::None && !op->sub->is_abstract && inherited->sub->parameters.size() == 2u &&
                same_type(inherited->sub->parameters[0].type, aDerived) && same_type(inherited->sub->parameters[1].type, aDerived))
            {
                // (a type extension's "=": predefined (composed of its parent's user-defined "=" and its extension's components)
                // (4.5.2))
                inherited->sub->op = intrinsic::Eq;
                inherited->sub->operator_type = aDerived;
                inherited->sub->parent_operation = nullptr;
            }
            if (aDerived->tagged && !aDerived->limited && op->sub->op == intrinsic::NotEqual && inherited->sub->parameters.size() == 2u &&
                same_type(inherited->sub->parameters[0].type, aDerived) && same_type(inherited->sub->parameters[1].type, aDerived))
            {
                // (the complement of the type extension's "=" (predefined))
                inherited->sub->op = intrinsic::Ne;
                inherited->sub->operator_type = aDerived;
                inherited->sub->parent_operation = nullptr;
            }
            // (an operation declared in a private part not visible here (not overriding an inherited one): never declared (not
            // overridable, not visible by name), but its body is the type's (dispatching) (7.3.1(6)))
            if (aDerived->tagged && op->sub->undeclared)
                inherited->sub->undeclared = true;
            else if (aDerived->tagged && op->is_private && !op->sub->inherited && !overrides(op) && op->scope != nullptr && op->scope->owner != nullptr && op->scope->owner->is_package() &&
                op->scope->is_package && !private_part_visible(op->scope->owner, aRegion))
                inherited->sub->undeclared = true;
            else
                aRegion->add(inherited);
            aDerived->primitives.push_back(inherited);
        }
        if (aDerived->tagged)
        {
            // (the parent's dispatch table (its partial view's, if the full view's is not complete yet))
            aDerived->slots = aParent->view()->slots;
            if (aParent->partial != nullptr && aParent->partial->slots.size() > aDerived->slots.size())
                aDerived->slots = aParent->partial->slots;
            if (aParent->slots.size() > aDerived->slots.size())
                aDerived->slots = aParent->slots;
            // (a null extension (a record extension with no components of its own): its inherited functions with a controlling
            // result are not abstract, their results the parent's extended (3.9.3(4/2)): functions of its own)
            auto* parentView = aParent->view()->base_type()->view();
            if (aDerived->kind == type_kind::Record && parentView->kind == type_kind::Record && aDerived->components.size() == parentView->components.size())
                for (auto* inherited : aDerived->primitives)
                    if (inherited->sub != nullptr && inherited->sub->inherited && inherited->kind == entity_kind::Function && !inherited->sub->is_abstract &&
                        inherited->sub->op == intrinsic::None && inherited->sub->result != nullptr && inherited->sub->parent_operation != nullptr &&
                        inherited->sub->result->view()->kind != type_kind::ClassWide && same_type(inherited->sub->result, aDerived) &&
                        !inherited->sub->null_extension_result)
                    {
                        inherited->sub->null_extension_result = true;
                        iSubprogramsToGenerate.push_back(inherited);
                        if (inherited->sub->slot >= 0 && static_cast<std::size_t>(inherited->sub->slot) < aDerived->slots.size())
                            aDerived->slots[static_cast<std::size_t>(inherited->sub->slot)].operation = inherited;
                    }
        }
    }

    void program::add_primitive_if(entity* aSubprogram, region* aRegion)
    {
        // (a subprogram declared in the same package specification (or declarative part) as a type that it has a parameter or
        // result of: a primitive operation of the type; one with the same name and profile as an inherited operation overrides it)
        if (aSubprogram->sub == nullptr || aSubprogram->sub->op == intrinsic::Attribute)
            return;
        std::vector<ada_type*> types;
        auto const consider = [&](ada_type* aType)
        {
            if (aType == nullptr)
                return;
            if (aType->anonymous_access && aType->designated != nullptr)
                aType = aType->designated;
            auto* base = aType->base_type();
            if (base->kind == type_kind::ClassWide || base->is_universal())
                return;
            if (base->declared_in != aRegion && !(base->partial != nullptr && base->partial->declared_in == aRegion))
            {
                // (a type declared in the package's declaration (for a body declared in the package body))
                if (!(aRegion->parent != nullptr && aRegion->owner != nullptr && base->declared_in == aRegion->parent && aRegion->parent->owner == aRegion->owner))
                    return;
            }
            if (std::find(types.begin(), types.end(), base) == types.end())
                types.push_back(base);
        };
        for (auto const& p : aSubprogram->sub->parameters)
            consider(p.type);
        consider(aSubprogram->sub->result);
        for (auto* t : types)
            add_primitive(t, aSubprogram);
    }

    void program::add_primitive(ada_type* aType, entity* aOperation)
    {
        auto* t = aType->base_type();
        auto& primitives = t->primitives;
        // (overriding an inherited operation (or an earlier declaration being completed); the full view's inherited operations
        // too (e.g. a tagged private type completed by a derivation, whose overriding declarations are in the private part))
        auto* other = t->kind == type_kind::Private && t->full != nullptr ? t->full->base_type() : t->partial;
        if (other == t)
            other = nullptr;
        // (a type derived in an instance from a tagged formal derived type's actual: in the private part, only the operations
        // corresponding to those the generic's formal type has (its ancestor's) are overridable (12.3(18)))
        std::size_t overridableSlots = static_cast<std::size_t>(-1);
        if (auto* parent = (t->tagged ? t : other != nullptr && other->tagged ? other : nullptr) != nullptr ? (t->parent != nullptr ? t->parent : other != nullptr ? other->parent : nullptr) : nullptr;
            parent != nullptr && t->declared_in != nullptr)
        {
            auto* parentBase = parent->view()->base_type()->view();
            for (region* jump = nullptr, *r = t->declared_in; r != nullptr && overridableSlots == static_cast<std::size_t>(-1); r = outward(r, jump))
                for (auto const& [actual, slots] : r->formal_ancestor_slots)
                    if (actual == parentBase)
                        overridableSlots = slots;
        }
        bool newOperation = false;
        for (auto* list : { &primitives, other != nullptr ? &other->primitives : nullptr })
        for (auto i = list != nullptr ? list->begin() : primitives.end(); list != nullptr && i != list->end() && !newOperation; ++i)
        {
            auto* existing = *i;
            if (existing == aOperation)
                return;
            if (existing->sub != nullptr && existing->sub->undeclared)
                continue;
            if (existing->name == aOperation->name && existing->sub != nullptr && type_conformant(*existing->sub, *aOperation->sub))
            {
                if (existing->sub->inherited && aOperation->is_private && existing->sub->slot >= 0 && static_cast<std::size_t>(existing->sub->slot) >= overridableSlots)
                {
                    // (not overriding: a new operation (its own slot), hiding the inherited one where it is visible)
                    existing->sub->hidden_by = aOperation;
                    newOperation = true;
                    break;
                }
                // (declared in the package body (not a primitive operation, but hiding the inherited one there))
                bool const inBody = aOperation->scope != nullptr && aOperation->scope != t->declared_in && aOperation->scope->parent == t->declared_in &&
                    t->declared_in != nullptr && t->declared_in->owner != nullptr && t->declared_in->owner->is_package();
                if (existing->sub->inherited && (aOperation->is_private || inBody) && !existing->is_private && !t->tagged && (other == nullptr || !other->tagged))
                {
                    // (an untagged type: the inherited one stays visible outside, with its parent's body (and is what types derived
                    // there inherit); the overriding one hides it where it is visible (8.3(12)))
                    existing->sub->hidden_by = aOperation;
                    primitives.push_back(aOperation);
                    return;
                }
                if (existing->sub->inherited && aOperation->is_private && !existing->is_private)
                {
                    // (overriding in the private part an operation inherited in the visible part: the inherited one stays visible
                    // (outside, where the overriding one is not), its calls executing the overriding body (7.3.1(6)))
                    existing->sub->overridden_by = aOperation;
                }
                if (existing->sub->inherited)
                {
                    if (list != &primitives)
                    {
                        // (the full view's inherited operation: overridden there, and the partial view's primitive)
                        *i = aOperation;
                        primitives.push_back(aOperation);
                        aOperation->sub->slot = existing->sub->slot;
                        aOperation->sub->parent_operation = existing->sub->parent_operation;
                        if (auto* r = existing->scope; r != nullptr && existing->sub->overridden_by == nullptr)
                            for (auto n = r->names.begin(); n != r->names.end(); ++n)
                                if (n->second == existing)
                                {
                                    r->names.erase(n);
                                    break;
                                }
                        if (existing->sub->overridden_by == nullptr)
                            existing->visible_in_body = false;
                        if (aOperation->sub->slot >= 0)
                            for (auto* view : { t, other })
                                if (static_cast<std::size_t>(aOperation->sub->slot) < view->slots.size())
                                    view->slots[static_cast<std::size_t>(aOperation->sub->slot)].operation = aOperation;
                        aOperation->sub->controlling = t->tagged ? t : other->tagged ? other : nullptr;
                        return;
                    }
                    aOperation->sub->slot = existing->sub->slot;
                    aOperation->sub->parent_operation = existing->sub->parent_operation;
                    // (the inherited operation is hidden: removed from its region (unless it stays visible outside))
                    if (auto* r = existing->scope; r != nullptr && existing->sub->overridden_by == nullptr)
                        for (auto n = r->names.begin(); n != r->names.end(); ++n)
                            if (n->second == existing)
                            {
                                r->names.erase(n);
                                break;
                            }
                    if (existing->sub->overridden_by == nullptr)
                        existing->visible_in_body = false;
                    *i = aOperation;
                    // (its slot: in both views' dispatch tables)
                    for (auto* view : { t, other })
                        if (view != nullptr && aOperation->sub->slot >= 0 && static_cast<std::size_t>(aOperation->sub->slot) < view->slots.size())
                            view->slots[static_cast<std::size_t>(aOperation->sub->slot)].operation = aOperation;
                    if (other != nullptr)
                        std::replace(other->primitives.begin(), other->primitives.end(), existing, aOperation);
                    aOperation->sub->controlling = t->tagged ? t : nullptr;
                    return;
                }
            }
        }
        primitives.push_back(aOperation);
        bool controlling = false;
        if (t->tagged || (t->partial != nullptr && t->partial->tagged))
        {
            for (auto& p : aOperation->sub->parameters)
                if (p.type != nullptr && (same_type(p.type, t) || (p.type->anonymous_access && p.type->designated != nullptr && same_type(p.type->designated, t))))
                {
                    controlling = true;
                    // (a controlling access parameter: null-excluding (3.10(13.1/2)))
                    if (p.type->anonymous_access && p.type->kind == type_kind::Access)
                    {
                        p.type->not_null = true;
                        p.not_null = true;
                    }
                }
            if (aOperation->sub->result != nullptr && same_type(aOperation->sub->result, t))
                controlling = true;
        }
        if (controlling)
        {
            aOperation->sub->controlling = t;
            if (aOperation->sub->slot < 0)
            {
                aOperation->sub->slot = static_cast<std::int32_t>(t->slots.size());
                t->slots.push_back(dispatch_slot{ aOperation });
            }
            if (t->partial != nullptr)
                t->partial->slots = t->slots;
            if (t->full != nullptr)
                t->full->base_type()->slots = t->slots;
        }
    }

    void program::subtype_declaration(node* aDecl, region* aRegion)
    {
        auto* defining = aDecl->first(construct::Defining);
        auto* e = declare(entity_kind::Subtype, defining, aRegion);
        auto* indication = aDecl->first(construct::SubtypeIndication);
        auto* t = subtype_indication(indication, aRegion);
        // (a subtype of its own (its name), with the constraints of the indication)
        auto* s = new_subtype(t, e->name);
        s->is_subtype = true;
        s->decl = e;
        s->base = t->base_type();
        s->full = nullptr;
        if (t->view() != t && t->kind == type_kind::Private)
            s->partial = t;
        e->type = t->kind == type_kind::Private || t->kind == type_kind::ClassWide ? t : s;
        aDecl->ent = e;
        aspects(aDecl, e, aRegion);
    }

    // --- predefined operators

    region* program::formal_actual_instance(ada_type* aType, region* aRegion)
    {
        auto* t = aType->view()->base_type()->view();
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            if (std::find(r->formal_actuals.begin(), r->formal_actuals.end(), t) != r->formal_actuals.end())
                return r;
        return nullptr;
    }

    bool program::instance_hidden(entity* aOperation, region* aRegion)
    {
        if (aOperation->sub == nullptr || aOperation->sub->instance_hidden == nullptr)
            return false;
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            if (r == aOperation->sub->instance_hidden)
                return true;
        return false;
    }

    std::vector<entity*> const& program::operators_of(ada_type* aType)
    {
        auto* t = aType->view()->base_type()->view();

        auto existing = iOperators.find(t);
        if (existing != iOperators.end())
            return existing->second;
        auto& ops = iOperators[t];
        auto const make = [&](std::string const& aName, intrinsic aOp, std::vector<ada_type*> const& aParameters, ada_type* aResult)
        {
            auto* e = new_entity(entity_kind::Function, aName, nullptr, t->declared_in != nullptr ? t->declared_in : iStandard);
            static char const* const sNames[] = { "left", "right" };
            for (std::size_t i = 0u; i < aParameters.size(); ++i)
            {
                parameter_info p;
                p.type = aParameters[i];
                p.ent = new_entity(entity_kind::Parameter, aParameters.size() == 1u ? "right" : sNames[i], nullptr, nullptr);
                p.ent->type = aParameters[i];
                e->sub->parameters.push_back(p);
            }
            e->sub->result = aResult;
            e->type = aResult;
            e->sub->op = aOp;
            e->sub->operator_type = t;
            ops.push_back(e);
        };
        auto* boolean = iBoolean != nullptr ? iBoolean : t;
        bool limited = t->limited || t->kind == type_kind::Task || t->kind == type_kind::Protected;
        // (a class-wide type whose specific type has a user-defined primitive "=": that one (dispatching), not a predefined one)
        if (t->kind == type_kind::ClassWide && t->specific != nullptr)
        {
            auto* specific = t->specific->view()->base_type()->view();
            for (auto const* list : { &specific->primitives, specific->partial != nullptr ? &specific->partial->primitives : nullptr })
                if (list != nullptr)
                    for (auto* op : *list)
                        if (op != nullptr && op->name == "=" && op->sub != nullptr && op->sub->op == intrinsic::None && op->sub->parameters.size() == 2u &&
                            same_type(op->sub->parameters[0].type, specific) && same_type(op->sub->parameters[1].type, specific))
                            limited = true;
        }
        if (!limited && t->kind != type_kind::Void)
        {
            make("=", intrinsic::Eq, { t, t }, boolean);
            make("/=", intrinsic::Ne, { t, t }, boolean);
        }
        if (t->is_scalar() || (t->kind == type_kind::Array && t->indexes.size() == 1u && t->component != nullptr && t->component->view()->is_discrete()))
        {
            make("<", intrinsic::Lt, { t, t }, boolean);
            make("<=", intrinsic::Le, { t, t }, boolean);
            make(">", intrinsic::Gt, { t, t }, boolean);
            make(">=", intrinsic::Ge, { t, t }, boolean);
        }
        if (t->is_numeric())
        {
            make("+", intrinsic::Plus, { t }, t);
            make("-", intrinsic::Negate, { t }, t);
            make("abs", intrinsic::Abs, { t }, t);
            make("+", intrinsic::Add, { t, t }, t);
            make("-", intrinsic::Subtract, { t, t }, t);
            if (t->is_fixed())
            {
                auto* integer = iInteger != nullptr ? iInteger : t;
                make("*", intrinsic::Multiply, { t, integer }, t);
                make("*", intrinsic::Multiply, { integer, t }, t);
                make("/", intrinsic::Divide, { t, integer }, t);
                // (the operators of any two fixed point types: universal_fixed (4.5.5))
                if (t->kind != type_kind::UniversalFixed && iUniversalFixed != nullptr)
                {
                    make("*", intrinsic::UniversalFixedMultiply, { iUniversalFixed, iUniversalFixed }, iUniversalFixed);
                    make("/", intrinsic::UniversalFixedDivide, { iUniversalFixed, iUniversalFixed }, iUniversalFixed);
                }
            }
            else
            {
                make("*", intrinsic::Multiply, { t, t }, t);
                make("/", intrinsic::Divide, { t, t }, t);
            }
            if (t->is_integer())
            {
                make("mod", intrinsic::Mod, { t, t }, t);
                make("rem", intrinsic::Rem, { t, t }, t);
            }
            if (!t->is_fixed())
            {
                // (the exponent: Natural (integers), Integer (reals); universal_integer for the universal types (and before Standard's types exist))
                ada_type* exponent = t->is_integer() ? (iNatural != nullptr ? iNatural : iUniversalInteger) :
                    (iInteger != nullptr ? iInteger : iUniversalInteger);
                make("**", intrinsic::Power, { t, exponent }, t);
            }
        }
        if (t->boolean || t->kind == type_kind::Modular ||
            (t->kind == type_kind::Array && t->indexes.size() == 1u && t->component != nullptr && t->component->view()->base_type()->boolean))
        {
            make("and", intrinsic::And, { t, t }, t);
            make("or", intrinsic::Or, { t, t }, t);
            make("xor", intrinsic::Xor, { t, t }, t);
            make("not", intrinsic::Not, { t }, t);
        }
        if (t->kind == type_kind::Array && t->indexes.size() == 1u && !limited)
        {
            auto* component = t->component;
            make("&", intrinsic::Concat, { t, t }, t);
            make("&", intrinsic::Concat, { t, component }, t);
            make("&", intrinsic::Concat, { component, t }, t);
            make("&", intrinsic::Concat, { component, component }, t);
        }
        if (t->kind == type_kind::UniversalInteger)
        {
            make("*", intrinsic::RootMultiply, { t, iUniversalReal }, iUniversalReal);
        }
        if (t->kind == type_kind::UniversalReal)
        {
            make("*", intrinsic::RootMultiply, { t, iUniversalInteger }, t);
            make("*", intrinsic::RootMultiply, { iUniversalInteger, t }, t);
            make("/", intrinsic::RootDivide, { t, iUniversalInteger }, t);
        }
        if (t->kind == type_kind::UniversalFixed)
        {
            make("*", intrinsic::UniversalFixedMultiply, { t, t }, t);
            make("/", intrinsic::UniversalFixedDivide, { t, t }, t);
        }
        return ops;
    }

    ada_type* program::enumeration_rep_array(ada_type* aType)
    {
        // (the type of an enumeration representation clause's aggregate: an array indexed by the enumeration type of
        // universal_integer)
        auto* t = new_type(type_kind::Array, {});
        t->indexes.push_back(aType);
        t->component = iUniversalInteger;
        t->constrained = false;
        return t;
    }

    bool program::is_subtype_name(node* aName, region* aRegion)
    {
        if (!aName->is(construct::Identifier) && !aName->is(construct::Selected))
            return false;
        auto const candidates = aName->is(construct::Identifier) ? visible(name_text(aName->source), aRegion) : std::vector<entity*>{};
        if (aName->is(construct::Selected))
        {
            try
            {
                // (without resolving the name (that would change the meanings of its nodes, invalidating cached interpretations))
                auto const entities = prefix_entities(aName, aRegion);
                return entities.size() == 1u && entities.front()->is_type();
            }
            catch (compile_error const&)
            {
                return false;
            }
        }
        return candidates.size() == 1u && candidates[0]->is_type();
    }
}
