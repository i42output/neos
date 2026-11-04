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
#include <functional>
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
        // (a numeric literal's type: universal_integer's or universal_real's)
        if (x == iIntegerLiteral)
            x = iUniversalInteger;
        else if (x == iRealLiteral)
            x = iUniversalReal;
        if (y == iIntegerLiteral)
            y = iUniversalInteger;
        else if (y == iRealLiteral)
            y = iUniversalReal;
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
            // (a type with the Integer_Literal aspect (4.2.1))
            return (e->is_integer() && !hidden()) || (a == iIntegerLiteral && !e->is_numeric() && type_aspect(aExpected, "integer_literal") != nullptr);
        case type_kind::UniversalReal:
            return (!hidden() && (e->kind == type_kind::Float || e->kind == type_kind::Fixed || e->kind == type_kind::Decimal || e->kind == type_kind::UniversalFixed)) ||
                (a == iRealLiteral && !e->is_numeric() && type_aspect(aExpected, "real_literal") != nullptr);
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

    namespace
    {
        // (a subtype mark of an access definition (or access type definition): it denotes the type itself (8.6(17/3)))
        bool designated_mark(node const* aMark)
        {
            for (auto const* n = aMark->parent; n != nullptr; n = n->parent)
            {
                if (n->is(construct::AccessDefinition) || n->is(construct::TypeAccess) || n->is(construct::Attribute))
                    return true;
                if (!n->is(construct::SubtypeIndication) && !n->is(construct::Selected))
                    return false;
            }
            return false;
        }
    }

    ada_type* program::subtype_mark(node* aMark, region* aRegion)
    {
        if (aMark->is(construct::SubtypeIndication))
            return subtype_indication(aMark, aRegion);
        // ('Class, 'Base)
        if (aMark->is(construct::Attribute))
        {
            auto const attribute = name_text(aMark->child(1)->source);
            ada_type* prefix = nullptr;
            {
                // (a tagged incomplete view: the prefix of 'Class (3.10.1(9.2/3)))
                incomplete_use use{ *this, attribute == "class" && iIncompleteUses != incomplete_uses::None ? incomplete_uses::TaggedOnly : iIncompleteUses };
                prefix = subtype_mark(aMark->child(0), aRegion);
            }
            aMark->type = prefix;
            if (attribute == "class")
                return class_wide(prefix);
            if (attribute == "base")
                return prefix->base_type();
            error(aMark, "subtype mark expected");
        }
        // (the name of an enclosing task or protected unit: its current instance (8.6(17/3)), not a subtype)
        if (aMark->is(construct::Identifier) && iSpeculation == 0 && iInstanceBodies == 0 && !designated_mark(aMark))
            for (auto* r = aRegion; r != nullptr; r = r->parent)
                for (auto* o = r->owner; o != nullptr; o = o->kind == entity_kind::Procedure ? o->owner : nullptr)
                    if ((o->kind == entity_kind::Task || o->kind == entity_kind::Protected) && o->name == name_text(aMark->source) &&
                        o->type != nullptr && !o->type->name.empty())
                        error(aMark, "\"" + o->name + "\" denotes the current instance of the unit here, not a subtype (8.6(17/3))");
        auto* e = resolve_entity(aMark, aRegion);
        if (e == nullptr)
            error(aMark, "subtype mark expected");
        if (e->kind == entity_kind::Task || e->kind == entity_kind::Protected)
            if (e->type != nullptr)
            {
                // (within the unit: its current instance, not a subtype (8.6(17/3), 9.1(9), 9.4(9)))
                if (iSpeculation == 0 && iInstanceBodies == 0 && !designated_mark(aMark))
                    for (auto* r = aRegion; r != nullptr; r = r->parent)
                        if (r->owner == e)
                            error(aMark, "\"" + e->name + "\" denotes the current instance of the unit here, not a subtype (8.6(17/3))");
                return e->type;
            }
        if (!e->is_type())
            error(aMark, "\"" + e->name + "\" is not a type");
        aMark->ent = e;
        aMark->type = e->type;
        // (within its own declaration (a record, task or protected type's region): the current instance, not a subtype (8.6(17/3)); a
        // subtype mark of an access definition denotes the type)
        if (iSpeculation == 0 && iInstanceBodies == 0 && !designated_mark(aMark) && e->kind == entity_kind::Type)
            for (auto* r = aRegion; r != nullptr; r = r->parent)
            {
                auto* unit = e->type != nullptr ? e->type->unit : nullptr;
                if (r->owner != nullptr && (r->owner == e || (unit != nullptr && (r->owner == unit || r->owner->owner == unit))))
                    error(aMark, "\"" + e->name + "\" denotes the current instance of the type here, not a subtype (8.6(17/3))");
            }
        // (the name of an incomplete view: only where allowed (3.10.1(5/3)-(9.4/3)))
        bool const limitedView = e->scope != nullptr && e->scope->owner != nullptr && e->scope->owner->limited_view;
        if (auto* t = e->type; t != nullptr && t->incomplete && (limitedView || (t->full == nullptr && !t->completing && !e->completed)) &&
            iSpeculation == 0 && iIncompleteUses != incomplete_uses::Allowed && !(iIncompleteUses == incomplete_uses::TaggedOnly && t->tagged))
            recover([&]() { error(aMark, "the name of an incomplete view cannot be used here"); });     // (the rest analysed)
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
            // (an index or discriminant constraint: of an unconstrained subtype (3.6.1(4), 3.7.1(5/2)); an access subtype's designated
            // subtype too (3.10(14/3)))
            auto* designated = t->designated != nullptr ? t->designated : t->view()->designated;
            if (auto* v = t->view(); iInstanceBodies == 0 &&
                ((v->kind == type_kind::Array && (v->constrained || (t->kind != type_kind::Private && t->constrained))) ||
                 (v->kind == type_kind::Record && (!v->discriminant_values.empty() || (v == t && !t->discriminant_values.empty()))) ||
                 (v->kind == type_kind::Access && designated != nullptr &&
                  ((designated->view()->kind == type_kind::Array && (designated->constrained || designated->view()->constrained)) ||
                   !designated->discriminant_values.empty()))))
                recover([&]() { error(mark, "a constraint cannot be given for a subtype that is already constrained"); });
            // (an access subtype's constraint: of a designated array or discriminated type (3.10(14/3)))
            else if (auto* v = t->view(); iInstanceBodies == 0 && v->kind == type_kind::Access && designated != nullptr &&
                designated->view()->kind != type_kind::Array && designated->view()->kind != type_kind::Record &&
                designated->view()->kind != type_kind::Private && designated->view()->kind != type_kind::Task &&
                designated->view()->kind != type_kind::Protected && !designated->view()->is_formal)
                recover([&]() { error(mark, "an index or discriminant constraint needs a designated array or discriminated type"); });
            // (not of an incomplete view, unless a discriminant constraint of one with a known discriminant part (3.10.1(9.2/3)))
            else if (auto* v = t->view(); iInstanceBodies == 0 && v->kind == type_kind::Access && designated != nullptr &&
                (designated->incomplete || designated->view()->incomplete) && !designated->view()->is_formal &&
                std::none_of(designated->view()->components.begin(), designated->view()->components.end(), [](component_info const& c) { return c.discriminant; }))
                recover([&]() { error(mark, "an access subtype designating an incomplete view cannot be constrained (3.10.1(9.2/3))"); });
            auto* result = constrain(t, mark, aRegion);
            aIndication->type = result;
            return result;
        }
        auto* t = subtype_mark(mark, aRegion);
        if (aIndication->has_flag("not null"))
        {
            null_exclusion_check(t, mark);
            t = new_subtype(t);
            t->not_null = true;
        }
        if (constraint != nullptr)
            t = constrain(t, constraint, aRegion);
        aIndication->type = t;
        return t;
    }

    void program::null_exclusion_check(ada_type* aType, node* aMark)
    {
        // (a null exclusion: of an access subtype that does not exclude null (3.10(14.1/2)))
        if (aType == nullptr || iInstanceBodies != 0 || !iInstantiationSites.empty() || aType->view()->is_formal)
            return;
        if (!aType->view()->is_access() && aType->view()->kind != type_kind::Private)
            recover([&]() { error(aMark, "a null exclusion needs an access subtype"); });
        else if (aType->not_null || aType->view()->not_null)
            recover([&]() { error(aMark, "a null exclusion cannot be given for a subtype that excludes null"); });
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
        // (a scalar subtype with a nonstatic constraint: not static, nor its subtypes (4.9(26/3)))
        if (result != aType && result->view()->is_scalar() && (!result->view()->low.known() || !result->view()->high.known()))
            result->nonstatic = true;
        // (nor one formed by constraining a nonstatic subtype (4.9(26/3)))
        if (result != aType && result->view()->is_scalar() && aType->view()->is_scalar() && !aType->is_universal() && !static_subtype(aType))
            result->nonstatic = true;
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
                // (static (3.5.9(16), J.3(3)))
                if (iInstanceBodies == 0 && iSpeculation == 0 && !static_expression(aConstraint->child(0)))
                    recover([&]() { error(aConstraint->child(0), "the expression of a digits or delta constraint must be static"); });
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
                // (an index constraint: of an unconstrained array subtype (3.6.1(3)); a discriminant constraint: of an unconstrained
                // discriminated subtype (3.7.1(7/3)); neither of a scalar subtype)
                if (iInstanceBodies == 0 && iSpeculation == 0)
                {
                    if (target->is_scalar())
                        recover([&]() { error(aConstraint, "an index or discriminant constraint cannot be applied to a scalar subtype"); });
                    else if (target->kind == type_kind::Array && aType->view()->constrained && !target->is_formal)
                        recover([&]() { error(aConstraint, "an index constraint cannot be applied to a constrained array subtype (3.6.1(3))"); });
                }
                if (target->kind == type_kind::Access && target->designated != nullptr)
                {
                    // (a constrained access subtype: its designated subtype is constrained (a private type's: of its full view))
                    auto* result = new_subtype(aType->kind == type_kind::Private ? target : aType);
                    result->designated = constrain(target->designated, aConstraint, aRegion);
                    return result;
                }
                if (target->kind == type_kind::Array)
                {
                    // (a range for each index (3.6.1(3)))
                    if (items.size() != target->indexes.size() && iInstanceBodies == 0 && iSpeculation == 0)
                        recover([&]() { error(aConstraint, "an index constraint must give a discrete range for each index (3.6.1(3))"); });
                    auto* result = new_subtype(aType);
                    result->constrained = true;
                    result->indexes.clear();
                    std::size_t i = 0u;
                    for (auto* item : items)
                    {
                        auto* indexType = i < target->indexes.size() ? target->indexes[i] : nullptr;
                        result->indexes.push_back(discrete_range(item, indexType, aRegion));
                        predicate_check(result->indexes.back(), item);
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
                bool named = false;
                bool const check = iInstanceBodies == 0 && iSpeculation == 0 && !discriminants.empty();
                for (auto* item : items)
                {
                    if (item->is(construct::Association))
                    {
                        named = true;
                        auto* choices = item->child(0);
                        ada_type* associationType = nullptr;
                        for (auto* choice : choices->children)
                        {
                            auto const name = name_text(choice->source);
                            // (a formal type's discriminants' names, within the instance)
                            auto const& formalNames = aType->view()->formal_discriminants;
                            bool const formal = !formalNames.empty() && within_scope(aType->view()->formal_scope, aRegion);
                            bool found = false;
                            for (std::size_t d = 0u; d < discriminants.size(); ++d)
                                if (formal ? d < formalNames.size() && formalNames[d] == name : discriminants[d]->ent->name == name)
                                {
                                    found = true;
                                    // (a discriminant: given one value (3.7.1(8)); the discriminants of an association: of the same type (3.7.1(9)))
                                    if (check && values[d] != nullptr)
                                        recover([&]() { error(choice, "the discriminant \"" + name + "\" is given more than one value (3.7.1(8))"); });
                                    if (check && associationType != nullptr && discriminants[d]->type != nullptr &&
                                        associationType->base_type() != discriminants[d]->type->base_type())
                                        recover([&]() { error(choice, "the discriminants of a discriminant association must be of the same type (3.7.1(9))"); });
                                    if (discriminants[d]->type != nullptr)
                                        associationType = discriminants[d]->type;
                                    values[d] = item->children.back();
                                    choice->ent = discriminants[d]->ent;
                                }
                            // (the name of a discriminant of the type (3.7.1(5)))
                            if (check && !found && !formal)
                                recover([&]() { error(choice, "\"" + name + "\" is not a discriminant of the type (3.7.1(5))"); });
                        }
                    }
                    else
                    {
                        // (positional associations: before named ones (3.7.1(3)), at most one for each discriminant)
                        if (check && named)
                            recover([&]() { error(item, "a positional discriminant association cannot follow a named one (3.7.1(3))"); });
                        if (position < values.size())
                            values[position++] = item;
                        else if (check)
                            recover([&]() { error(item, "too many discriminant values (3.7.1(8))"); });
                    }
                }
                // (a value for each discriminant (3.7.1(8)))
                if (check)
                    for (std::size_t d = 0u; d < discriminants.size(); ++d)
                        if (values[d] == nullptr)
                        {
                            recover([&]() { error(aConstraint, "the discriminant \"" + discriminants[d]->ent->name + "\" is not given a value (3.7.1(8))"); });
                            break;
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

    void program::default_storage_pool(ada_type* aAccess, node* aDecl, region* aRegion, bool aRecordPool)
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
            // (a null default pool: no allocators of the type (unless it specifies a pool))
            if (*found == nullptr)
                iNullPoolTypes.insert(aAccess);
            if (*found != nullptr && aRecordPool)
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
            // (an incompatible static constraint: the subtype is not static (4.9(26/3)))
            aSubtype->nonstatic = true;
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

    bool program::same_interfaces(ada_type* aPartial, ada_type* aFull)
    {
        // (a partial view and its full view: descended from the same interfaces (7.3(7.3/2)))
        std::function<void(ada_type*, std::set<ada_type const*>&, int)> collect = [&](ada_type* x, std::set<ada_type const*>& out, int depth)
        {
            if (x == nullptr || depth > 32)
                return;
            auto* v = x->view();
            if (v->kind == type_kind::ClassWide && v->specific != nullptr)
                v = v->specific->view();
            v = v->base_type()->view();
            if (v->kind == type_kind::Interface)
                out.insert(v);
            for (auto* i : x->interfaces)
                collect(i, out, depth + 1);
            if (x != v)
                for (auto* i : v->interfaces)
                    collect(i, out, depth + 1);
            if (v->parent != nullptr)
                collect(v->parent, out, depth + 1);
        };
        std::set<ada_type const*> partialSet, fullSet;
        for (auto* i : aPartial->interfaces)
            collect(i, partialSet, 0);
        if (aPartial->parent != nullptr)
            collect(aPartial->parent, partialSet, 0);
        auto* full = aFull->base_type();
        for (auto* i : full->interfaces)
            collect(i, fullSet, 0);
        if (full->kind == type_kind::Interface)
            fullSet.insert(full->view());
        if (full->parent != nullptr)
            collect(full->parent, fullSet, 0);
        return partialSet == fullSet;
    }

    bool program::not_completely_defined(ada_type* aType, region* aRegion, int aDepth)
    {
        // (a private (or incomplete) type declared in an enclosing declarative region whose full declaration has not occurred yet, or a
        // composite type with a subcomponent of one: freezing it is illegal (13.14(17), 7.3(5)))
        if (aType == nullptr || aDepth > 8)
            return false;
        auto* t = aType->kind == type_kind::Private || (aType->base != nullptr && aType->base->kind == type_kind::Private) ?
            (aType->kind == type_kind::Private ? aType : aType->base) : aType->base_type();
        if (t->kind == type_kind::Private)
        {
            // (checking a generic (its template, where it is declared): a full type declaration after (the end of) it has not occurred)
            bool const later = t->full != nullptr && iCheckedGeneric != nullptr && t->full->decl != nullptr && t->full->decl->decl != nullptr &&
                t->full->decl->decl->file == iCheckedGeneric->file &&
                t->full->decl->decl->source.data() >= iCheckedGeneric->source.data() + iCheckedGeneric->source.size();
            if ((t->full != nullptr && !later) || t->is_formal || t->declared_in == nullptr)
                return false;
            // (a type derived from a generic formal type (e.g. a record extension of a formal tagged private type): complete; it is
            // a view of the formal's kind)
            // (or of a formal derived type (its root not formal))
            if (t->parent != nullptr && t->full == nullptr && !t->untagged_partial)
                for (auto* a = t->parent; a != nullptr; a = a->view()->base_type()->view()->parent)
                    if (a->view()->is_formal || a->view()->base_type()->view()->is_formal)
                        return false;
            for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
                if (r == t->declared_in)
                    return true;
            return false;
        }
        if (t->kind == type_kind::Array)
            return not_completely_defined(t->component, aRegion, aDepth + 1);
        if (t->kind == type_kind::Record)
            for (auto const& c : t->components)
                if (c.type != nullptr && !c.type->is_access() && not_completely_defined(c.type, aRegion, aDepth + 1))
                    return true;
        return false;
    }

    void program::progenitor_mix_check(ada_type* aParent, std::vector<ada_type*> const& aInterfaces, node* aAt)
    {
        // (not a descendant of both a task interface and a protected interface (3.9.4(11/2)))
        bool task = false;
        bool prot = false;
        std::function<void(ada_type*)> scan = [&](ada_type* aType)
        {
            auto* v = aType != nullptr ? aType->view()->base_type()->view() : nullptr;
            if (v == nullptr || v->kind != type_kind::Interface)
                return;
            task = task || v->task_interface;
            prot = prot || v->protected_interface;
            for (auto* i : v->interfaces)
                scan(i);
        };
        scan(aParent);
        for (auto* i : aInterfaces)
            scan(i);
        if (task && prot)
            error_in_private_part_too(aAt, "a type cannot be a descendant of both a task interface and a protected interface");
    }

    void program::progenitor_check(ada_type* aProgenitor, node* aAt, bool aLimited, bool aSynchronized, type_kind aUnit)
    {
        // (a descendant of a nonlimited interface: nonlimited; of a synchronized interface: synchronized (a task or protected type, or
        // a synchronized interface or private extension); of a task (protected) interface: a task (protected) type or interface
        // (3.9.4(10/2)-(11/2)))
        auto* p = aProgenitor != nullptr ? aProgenitor->view()->base_type()->view() : nullptr;
        if (p == nullptr || p->kind != type_kind::Interface)
            return;
        // (a synchronized private extension: completed by a task or protected type)
        bool const either = aSynchronized && aUnit == type_kind::Record;
        if ((!p->limited && aLimited) || (p->synchronized_interface && !aSynchronized) ||
            (p->task_interface && aUnit != type_kind::Task && !either) || (p->protected_interface && aUnit != type_kind::Protected && !either))
            error_in_private_part_too(aAt, "an interface of an incompatible kind (limited, synchronized, task or protected) cannot be a progenitor here");
    }

    ada_type* program::progenitor(node* aMark, region* aRegion)
    {
        // (a subtype mark in an interface list: an interface (3.9.4(9/2)))
        auto* t = subtype_mark(aMark, aRegion);
        if (t->view()->base_type()->view()->kind != type_kind::Interface)
            error_in_private_part_too(aMark, "an interface list must name interface types");
        return t->base_type();
    }

    void program::predicate_check(ada_type* aType, node* aWhere, bool aDynamicOnly)
    {
        // (a subtype with a predicate (a dynamic one): not an index subtype, an index or slice range, an entry family's, the prefix
        // of 'First, 'Last or 'Range (or, a dynamic one, a for loop's) (3.2.4(29/3)-(29.4/5)))
        if (aType == nullptr || iInstanceBodies != 0 || iSpeculation != 0)
            return;
        auto* p = aType->predicate != nullptr ? aType->predicate : aType->view()->predicate;
        if (p == nullptr)
            return;
        if (aDynamicOnly)
        {
            bool dynamic = false;
            for (auto* q = p; q != nullptr; q = q->parent)
                dynamic = dynamic || !q->is_static;
            if (!dynamic)
                return;
        }
        recover([&]() { error_in_private_part_too(aWhere, aDynamicOnly ? "a subtype with a dynamic predicate cannot be used here" : "a subtype with a predicate cannot be used here"); });
    }

    bool program::limited_type(ada_type* aType, region* aRegion, region* aComposite) const
    {
        // (a limited type (7.5): declared limited (or a view of one), a task, protected or limited interface type, or a composite
        // type with a limited component; its view (a partial view: its own))
        if (aType == nullptr)
            return false;
        // (a partial view: its own limitedness unless its full view is visible here)
        if (aType->kind == type_kind::Private || (aType->base != nullptr && aType->base->kind == type_kind::Private))
        {
            auto* partial = aType->kind == type_kind::Private ? aType : aType->base;
            auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
            // (in an instance: as at the (outermost) instantiation)
            if (aRegion != nullptr && !iInstantiationRegions.empty() && package != nullptr && !private_part_visible_here(package, aRegion))
                aRegion = iInstantiationRegions.front();
            if (partial->full == nullptr || aRegion == nullptr || package == nullptr || !private_part_visible_here(package, aRegion))
                return partial->limited;
            // (a component of a composite type: its full view's characteristics are the composite's only if the composite is declared
            // within the package (7.3.1(5/1)))
            if (aComposite != nullptr)
            {
                bool within = false;
                for (region* jump = nullptr, *r = aComposite; r != nullptr && !within; r = outward(r, jump))
                    within = r->owner == package;
                // (and here: within the composite type's immediate scope (7.3.1(5/1)))
                if (!within || (iInstanceBodies == 0 && !within_scope(aComposite, aRegion)))
                    return partial->limited;
            }
            return limited_type(partial->full, aRegion);
        }
        auto* b = aType->base_type();
        if (b->limited || b->kind == type_kind::Task || b->kind == type_kind::Protected)
            return true;
        if (b->kind == type_kind::ClassWide)
            return b->specific != nullptr && limited_type(b->specific, aRegion);
        // (a derived type: limited if its parent (its view) is (7.5(3/3)); not if the parent is an interface (unless declared limited, 7.5(6.1/3)))
        if (b->parent != nullptr && b->parent != aType && b->parent->base_type() != b && b->parent->view()->base_type()->view()->kind != type_kind::Interface &&
            limited_type(b->parent, aRegion, b->declared_in))
            return true;
        // (a composite type's components)
        if (b->kind == type_kind::Array)
            return limited_type(b->component, aRegion, b->declared_in);
        if (b->kind == type_kind::Record && !b->tagged)
            for (auto const& c : b->components)
                if (!c.discriminant && limited_type(c.type, aRegion, b->declared_in))
                    return true;
        return false;
    }

    void program::tagged_type_checks(ada_type* aType, node* aDiscriminants, region* aRegion)
    {
        auto* t = aType->base_type();
        if (!t->tagged || (t->kind != type_kind::Record && t->kind != type_kind::Private))
            return;
        // (a type derived from an untagged partial view (its full view tagged, not visible here): untagged (7.3(7.3/2), 7.3.1(5/1)))
        if (t->derived_partial != nullptr)
            return;
        // (a nonlimited tagged type's discriminants: no defaults (3.7(10/3)))
        if (aDiscriminants != nullptr && !t->limited)
            for (auto const& c : t->components)
                if (c.discriminant && !c.stored && c.default_expression != nullptr && c.ent != nullptr && c.ent->decl != nullptr &&
                    [&]() { for (auto* n = c.ent->decl; n != nullptr; n = n->parent) if (n == aDiscriminants) return true; return false; }())
                    error(c.default_expression, "a discriminant of a nonlimited tagged type cannot have a default");
        // (a nonlimited tagged type's components: nonlimited (7.5(1/2)))
        if (!t->limited)
            for (auto const& c : t->components)
                if (!c.discriminant && c.type != nullptr && c.ent != nullptr && c.ent->owner == t->decl && limited_type(c.type, aRegion))
                    error(c.ent->decl != nullptr ? c.ent->decl : t->decl->decl, "a component of a nonlimited tagged type must be nonlimited");
    }

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
        // (a private type or private extension: in a package's visible part only (7.3(4)))
        bool const privateExtension = aDecl->is(construct::TypePrivateExtension) || [&]()
        {
            // (parsed as a derived type definition where a private extension is not expected: an extension "with private")
            auto* d = aDecl->first(construct::TypeDerived);
            auto* x = d != nullptr ? d->first(construct::Extension) : nullptr;
            if (x == nullptr)
                return false;
            std::string text{ x->source };
            for (auto& ch : text)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return text.find("record") == std::string::npos && text.find("private") != std::string::npos;
        }();
        if ((aDecl->is(construct::TypePrivate) || privateExtension) && iInstanceBodies == 0 &&
            (!aRegion->is_package || aRegion->package_body || (aRegion->private_start != static_cast<std::size_t>(-1) &&
                aRegion->declarations.size() >= aRegion->private_start)))
            recover([&]() { error(aDecl, "a private type or private extension can be declared only in the visible part of a package"); });
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
                        // (an incomplete type declaration declares a new type: not a view of one already declared (8.3(26/2)))
                        error(defining, "\"" + name + "\" is already declared in this declarative region");
                        return;
                    }
            }
            auto* e = declare(entity_kind::Type, defining, aRegion);
            // (an incomplete view completed by a partial view (3.10.1(3)))
            if (partial != nullptr && partial->type->incomplete)
                partial->completed = true;
            auto* t = new_type(type_kind::Private, name);
            t->decl = e;
            t->first_subtype = true;
            t->declared_in = aRegion;
            t->incomplete = aDecl->is(construct::TypeIncomplete);
            t->tagged = aDecl->has_flag("tagged");
            t->untagged_partial = !t->tagged && aDecl->is(construct::TypePrivate);
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
            if (partial != nullptr && partial->type->incomplete)
                partial->completed = true;
            node* parentNode = aDecl->first(construct::SubtypeIndication);
            auto* parent = subtype_indication(parentNode, aRegion);
            if (parent->view()->kind == type_kind::ClassWide)
                error_in_private_part_too(parentNode, "the parent type of a derived type cannot be class-wide");
            auto* t = derive(parent, name, e, aRegion, nullptr, nullptr);
            if (aDecl->first(construct::Discriminants) != nullptr || aDecl->first(construct::DiscriminantsUnknown) != nullptr)
                t->inherited_unknown_discriminants = false;
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
            // (limited if its parent is, unless the parent is an interface (7.5(6.1/2)))
            t->limited = aDecl->has_flag("limited") || aDecl->has_flag("synchronized") ||
                (parent->view()->limited && parent->view()->base_type()->view()->kind != type_kind::Interface);
            bool const limitedExtension = aDecl->has_flag("limited") || aDecl->has_flag("synchronized");
            progenitor_check(parent, parentNode, limitedExtension, aDecl->has_flag("synchronized"), type_kind::Record);
            for (auto* i : aDecl->children)
                if (i != parentNode && (i->is(construct::Identifier) || i->is(construct::Selected)))
                {
                    t->interfaces.push_back(progenitor(i, aRegion));
                    progenitor_check(t->interfaces.back(), i, limitedExtension, aDecl->has_flag("synchronized"), type_kind::Record);
                }
            progenitor_mix_check(parent, t->interfaces, aDecl);
            if (discriminants != nullptr)
                discriminant_part(discriminants, t, new_region(e, aRegion));
            aDecl->ent = e;
            aspects(aDecl, e, aRegion);
            return;
        }
        // (a full type declaration)
        auto* e = declare(entity_kind::Type, defining, aRegion);
        aDecl->ent = e;
        // (within it, the name of the incomplete view it completes denotes the current instance (8.6(17/3)))
        struct completing_view
        {
            ada_type* view;
            ~completing_view() { if (view != nullptr) view->completing = false; }
        } completing{ partial != nullptr && partial->type != nullptr && partial->type->incomplete ? partial->type : nullptr };
        if (completing.view != nullptr)
            completing.view->completing = true;
        node* definition = nullptr;
        for (auto* c : aDecl->children)
            if (c != defining && c != discriminants && !c->is(construct::Aspects))
                definition = c;
        // (hidden from all visibility until the end of the declaration, except a record type's or extension's, until the reserved
        // word record (here: not hidden) (8.3(16)))
        ada_type* t = nullptr;
        {
            declaring hiding;
            if (!definition->is(construct::TypeRecord) && definition->first(construct::Extension) == nullptr)
                hiding.add(e);
            t = type_definition(definition, name, e, aRegion, discriminants);
        }
        e->type = t;
        if (t != nullptr && iInstanceBodies == 0)
            recover([&]() { tagged_type_checks(t, discriminants, aRegion); });
        if (partial != nullptr)
        {
            // (the full view of a partial view: the partial view's entity denotes the type (the full view is its completion))
            auto* p = partial->type;
            // (a private type's or private extension's full declaration: in the private part (7.3(4)))
            if (!p->incomplete && iInstanceBodies == 0 && aRegion->is_package && !aRegion->package_body &&
                (aRegion->private_start == static_cast<std::size_t>(-1) || aRegion->declarations.size() < aRegion->private_start))
                recover([&]() { error(defining, "the full declaration of a private type or private extension must be in the private part (7.3(4))"); });
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
            partial->completed = true;
            // (a partial view with known discriminants: its full view inherits Implicit_Dereference only if the partial view has the same
            // aspect (13.1.1(18.4/4)))
            if (iInstanceBodies == 0 && !p->incomplete && p->known_discriminants && t->base_type()->parent != nullptr)
                if (auto* inherited = type_aspect(t->base_type()->parent, "implicit_dereference"); inherited != nullptr)
                {
                    auto* own = type_aspect(p, "implicit_dereference");
                    if (own == nullptr && p->base_type()->parent != nullptr)
                        own = type_aspect(p->base_type()->parent, "implicit_dereference");     // (the partial view's own inherited aspect)
                    if (own == nullptr || name_text(own->source) != name_text(inherited->source))
                        recover([&]() { error(definition != nullptr ? definition : defining, "the full view inherits aspect Implicit_Dereference, which its partial view "
                            "(with known discriminants) does not have (13.1.1(18.4/4))"); });
                }
            // (a tagged incomplete type: completed by a tagged type (3.10.1(2.1/2)))
            // (the view's: a derived type without an extension is tagged if its parent's view is)
            bool tagged = t->base_type()->tagged || t->base_type()->kind == type_kind::Interface;
            if (tagged && definition != nullptr && definition->is(construct::TypeDerived) && definition->first(construct::Extension) == nullptr &&
                t->base_type()->parent != nullptr)
            {
                // (an untagged partial view, its full view not visible here)
                auto* parentBase = t->base_type()->parent->base_type();
                if (parentBase->partial != nullptr)
                    parentBase = parentBase->partial;
                if (parentBase->untagged_partial)
                    if (auto* package = parentBase->declared_in != nullptr ? parentBase->declared_in->owner : nullptr;
                        package != nullptr && !private_part_visible(package, aRegion))
                        tagged = false;
            }
            // (a nonabstract partial view: its full view nonabstract (3.9.3))
            if (!p->incomplete && !p->abstract && t->base_type()->abstract)
                error(defining, "the full view of a nonabstract partial view cannot be abstract");
            // (a partial view without a discriminant part: its full view definite (7.3(4)))
            // (a derived type: its parent subtype's definiteness (the derived first subtype is constrained if it is))
            if (!p->incomplete && p->constrained && !p->unknown_discriminants &&
                (definition->is(construct::TypeDerived) && definition->first(construct::SubtypeIndication) != nullptr && t->base_type()->parent != nullptr ?
                    indefinite_subtype(t->base_type()->parent, definition->first(construct::SubtypeIndication), aRegion) :
                    indefinite_subtype(t, definition, aRegion)))
                recover([&]() { error(defining, "the full view of a partial view without discriminants must be definite"); });
            // (a partial view with a known discriminant part: the full view's fully conforming (7.3(13), 6.3.1(14)): the same
            // discriminants' names, statically matching subtypes, fully conformant defaults)
            if (!p->incomplete && iInstanceBodies == 0 && iInstantiationSites.empty() && partial->decl != nullptr && partial->decl->parent != nullptr &&
                partial->decl->parent->first(construct::Discriminants) != nullptr)
            {
                std::vector<component_info const*> a, b;
                for (auto const& c : p->components)
                    if (c.discriminant)
                        a.push_back(&c);
                if (discriminants != nullptr && discriminants->is(construct::Discriminants))
                    for (auto const& c : t->base_type()->view()->components)
                        if (c.discriminant && !c.stored && c.ent != nullptr && c.ent->decl != nullptr &&
                            [&]() { for (auto* n = c.ent->decl; n != nullptr; n = n->parent) if (n == discriminants) return true; return false; }())
                            b.push_back(&c);
                bool conformant = discriminants != nullptr && discriminants->is(construct::Discriminants) && a.size() == b.size();
                for (std::size_t i = 0u; conformant && i < a.size(); ++i)
                    conformant = a[i]->ent != nullptr && b[i]->ent != nullptr && a[i]->ent->name == b[i]->ent->name &&
                        (a[i]->type == nullptr || b[i]->type == nullptr || (a[i]->type->anonymous_access && b[i]->type->anonymous_access) ||
                            statically_matching(a[i]->type, b[i]->type)) &&
                        (a[i]->default_expression == nullptr) == (b[i]->default_expression == nullptr) &&
                        (a[i]->default_expression == nullptr || fully_conformant(a[i]->default_expression, b[i]->default_expression));
                if (!conformant)
                    recover([&]() { error(defining, "the full view's discriminant part must fully conform to the partial view's (7.3(13))"); });
            }
            // (a nonlimited partial view: its full view nonlimited (7.5(3/3)))
            if (!p->incomplete && !p->limited && iInstanceBodies == 0 && iInstantiationSites.empty() && limited_type(t, aRegion) &&
                !(p->parent != nullptr && limited_type(p->parent, aRegion)))
                recover([&]() { error(defining, "the full view of a nonlimited partial view must be nonlimited"); });
            // (a limited tagged partial view: its full view limited (7.3(7.1/2)... 7.5(3/3)); a private extension: its full view a record
            // extension (7.3(10)))
            if (!p->incomplete && p->tagged && p->limited && iInstanceBodies == 0 && iInstantiationSites.empty() && !limited_type(t, aRegion) &&
                t->base_type()->tagged)
                recover([&]() { error(defining, "the full view of a limited tagged partial view must be limited"); });
            auto const privateExtension = [&]()
            {
                auto* derived = partial->decl != nullptr && partial->decl->parent != nullptr ? partial->decl->parent->first(construct::TypeDerived) : nullptr;
                auto* extension = derived != nullptr ? derived->first(construct::Extension) : nullptr;
                return extension != nullptr && extension->has_flag("private");
            };
            if (!p->incomplete && privateExtension() && definition != nullptr &&
                !(definition->is(construct::TypeDerived) && definition->first(construct::Extension) != nullptr) && iInstanceBodies == 0)
                recover([&]() { error(defining, "the full view of a private extension must be a record extension"); });
            // (a tagged partial view: descended from an interface if and only if its full view is (7.3(7.3/2)))
            if (!p->incomplete && p->tagged && iInstanceBodies == 0 && iInstantiationSites.empty() && !same_interfaces(p, t))
                recover([&]() { error(defining, "a tagged partial view and its full view must be descended from the same interfaces (7.3(7.3/2))"); });
            // (a synchronized private extension: completed by a task or protected type (7.3(7.2/3)))
            if (!p->incomplete && iInstanceBodies == 0 && partial->decl != nullptr && partial->decl->parent != nullptr)
                if (auto* declaration = partial->decl->parent; declaration->has_flag("synchronized") ||
                    (declaration->first(construct::TypeDerived) != nullptr && declaration->first(construct::TypeDerived)->has_flag("synchronized")))
                    recover([&]() { error(defining, "a synchronized private extension must be completed by a task or protected type (7.3(7.2/3))"); });
            // (a private extension's full view: limited if and only if the private extension is (7.3(10.1/3)))
            if (!p->incomplete && privateExtension() && definition != nullptr && definition->is(construct::TypeDerived) && iInstanceBodies == 0)
            {
                auto* declaration = partial->decl->parent;
                auto* derived = declaration->first(construct::TypeDerived);
                bool const partialLimited = declaration->has_flag("limited") || (derived != nullptr && derived->has_flag("limited"));
                bool const fullLimited = definition->has_flag("limited") || (definition->parent != nullptr && definition->parent->has_flag("limited"));
                if (partialLimited != fullLimited)
                    recover([&]() { error(definition, partialLimited ? "the full view of a limited private extension must be declared limited (7.3(10.1/3))" :
                        "the full view of a private extension that is not declared limited cannot be declared limited (7.3(10.1/3))"); });
            }
            // (... whose parent is derived from the private extension's ancestor (7.3(10)))
            if (!p->incomplete && privateExtension() && p->parent != nullptr && t->base_type()->parent != nullptr && iInstanceBodies == 0 &&
                !is_descendant(t->base_type()->parent, p->parent) &&
                !(p->parent->view()->base_type()->view()->kind == type_kind::Interface && is_descendant(t->base_type(), p->parent)))
                recover([&]() { error(defining, "the parent type of the full view of a private extension must be derived from its ancestor type"); });
            // (a private extension whose ancestor subtype constrains its discriminants: its full view's parent subtype statically matching
            // constraints on them (7.3(13)))
            if (!p->incomplete && privateExtension() && iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                auto const values = [&](ada_type* aType, bool aOwn) -> std::optional<std::vector<static_value>>
                {
                    // (the ancestor's discriminants' values: the first subtype's constraint, or the stored values (new discriminants)); the
                    // partial view's own (not its full view's))
                    auto* v = aOwn ? aType : aType->view();
                    if (!v->discriminant_constraint.empty())
                    {
                        std::vector<static_value> r;
                        for (auto* c : v->discriminant_constraint)
                            r.push_back(c != nullptr ? evaluate(c, aRegion) : static_value{});
                        return r;
                    }
                    std::vector<static_value> r;
                    for (auto const& c : (aOwn ? v->base_type() : v->base_type()->view())->components)
                        if (c.stored)
                            r.push_back(c.default_expression != nullptr ? evaluate(c.default_expression, aRegion) : static_value{});
                    if (!r.empty())
                        return r;
                    return std::nullopt;
                };
                auto const pv = values(p, true);
                auto const tv = values(t, false);
                bool mismatch = false;
                if (pv && !tv)
                    mismatch = true;
                else if (pv && tv && pv->size() == tv->size())
                    for (std::size_t k = 0u; k < pv->size(); ++k)
                        if ((*pv)[k].is_integer() && (*tv)[k].is_integer() && (*pv)[k].integer() != (*tv)[k].integer())
                            mismatch = true;
                if (mismatch)
                    recover([&]() { error(defining, "the parent subtype of the full view must constrain the discriminants as the private extension's ancestor subtype does (7.3(13))"); });
            }
            // (a tagged partial view: its full view tagged (7.3(7)))
            if (!p->incomplete && p->tagged && !t->base_type()->tagged && t->base_type()->kind != type_kind::Interface)
                error(defining, "the full view of a tagged partial view must be tagged");
            if (p->incomplete && p->tagged && !tagged)
                error(defining, "the completion of a tagged incomplete type must be a tagged type");
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
                            // (overriding it here: the explicit declaration cannot be "not overriding" (8.3.1(6/2)))
                            if (iInstanceBodies == 0 && !inherited->sub->undeclared && op->sub->spec != nullptr && op->sub->spec->parent != nullptr &&
                                op->sub->spec->parent->has_flag("not overriding"))
                                recover([&]() { error(op->sub->spec, "\"" + op->name + "\" overrides an inherited subprogram"); });
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
        std::size_t defaults = 0u;
        std::size_t specifications = 0u;
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
                {
                    type = c->is(construct::AccessDefinition) ? access_definition(c, aRegion) : subtype_mark(c, aRegion);
                    // (a discriminant's type: discrete or access (3.7(5/2)); a partial view's, as seen here)
                    auto* seen = type != nullptr ? type->view() : nullptr;
                    if (type != nullptr && (type->kind == type_kind::Private || (type->base != nullptr && type->base->kind == type_kind::Private)))
                    {
                        auto* partial = type->kind == type_kind::Private ? type : type->base;
                        auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                        if (!partial->is_formal && (package == nullptr || !private_part_visible(package, aRegion)))
                            seen = partial;
                    }
                    if (seen != nullptr && !seen->is_discrete() && !seen->is_access() && !seen->is_formal && iInstanceBodies == 0)
                        recover([&]() { error(c, "a discriminant's type must be discrete or an access type"); });
                }
                else
                    defaultExpression = c;
            }
            ++specifications;
            if (defaultExpression != nullptr)
                ++defaults;
            // (defaults: for all the discriminants or none (3.7(10/3)))
            if (specifications > 1u && defaults != 0u && defaults != specifications && iInstanceBodies == 0)
                recover([&]() { error(spec, "either all or none of the discriminants must have defaults"); });
            if (type != nullptr && spec->has_flag("not null") && spec->first(construct::AccessDefinition) == nullptr)
                null_exclusion_check(type, spec);
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
        // (a discriminant part: of a composite type (a record, task or protected type, a type derived from one), 3.7(1), (8))
        if (aDiscriminants != nullptr && iInstanceBodies == 0 && (aDefinition->is(construct::TypeEnumeration) || aDefinition->is(construct::TypeInteger) ||
            aDefinition->is(construct::TypeModular) || aDefinition->is(construct::TypeFloat) || aDefinition->is(construct::TypeFixed) ||
            aDefinition->is(construct::TypeDecimal) || aDefinition->is(construct::TypeArray) || aDefinition->is(construct::TypeArrayUnconstrained) ||
            aDefinition->is(construct::TypeAccess) || aDefinition->is(construct::TypeAccessSubprogram) || aDefinition->is(construct::TypeInterface)))
            recover([&]() { error(aDiscriminants, "a discriminant part is allowed only for a composite type (3.7(8))"); });
        switch (aDefinition->kind)
        {
        case construct::TypeEnumeration:
            {
                auto* t = new_type(type_kind::Enumeration, aName);
                std::int64_t position = 0;
                for (auto* literal : aDefinition->children)
                {
                    // (distinct literals (3.5.1(5)); not the type's name (a homograph in the same declarative region, 8.3(26/2)))
                    auto const literalName = literal->is(construct::DefiningCharacter) ? std::string{ literal->source } : name_text(literal->source);
                    for (auto* other : t->literals)
                        if (other->name == literalName)
                            error(literal, "duplicate enumeration literal");
                    if (literalName == aName)
                        error(literal, "\"" + literalName + "\" is already declared in this declarative region");
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
                // (static expressions (4.9): not only of known value)
                if (iInstanceBodies == 0)
                    for (auto* bound : { aDefinition->child(0), aDefinition->child(1) })
                        if (!static_expression(bound))
                            recover([&]() { error(bound, "the bounds of an integer type must be static"); });
                // (in System.Min_Int .. System.Max_Int (3.5.4(6)))
                if (iInstanceBodies == 0)
                    for (auto const& [bound, v] : { std::pair{ aDefinition->child(0), low }, std::pair{ aDefinition->child(1), high } })
                        if (v.is_integer() && (v.integer() < -bigint::pow(bigint{ 2 }, 63u) || v.integer() > bigint::pow(bigint{ 2 }, 63u) - bigint{ 1 }))
                        {
                            recover([&]() { error(bound, "the bounds of an integer type must be in System.Min_Int .. System.Max_Int (3.5.4(6))"); });
                            low = high = static_value{ bigint{ 0 } };
                        }
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
                if (iInstanceBodies == 0 && !static_expression(aDefinition->child(0)))
                    recover([&]() { error(aDefinition->child(0), "the modulus must be static (3.5.4(7))"); });
                // (positive, at most System.Max_Binary_Modulus (a power of 2) or System.Max_Nonbinary_Modulus (3.5.4(7)))
                if (iInstanceBodies == 0)
                {
                    auto m = modulus.integer();
                    bool power = m > bigint{ 0 };
                    for (auto x = m; power && x > bigint{ 1 }; x = x / bigint{ 2 })
                        power = x % bigint{ 2 } == bigint{ 0 };
                    if (!(m > bigint{ 0 }) || (power ? m > bigint::pow(bigint{ 2 }, 64u) : m > bigint::pow(bigint{ 2 }, 32u)))
                    {
                        recover([&]() { error(aDefinition->child(0), "the modulus must be positive and at most System.Max_Binary_Modulus (or System.Max_Nonbinary_Modulus) (3.5.4(7))"); });
                        modulus = static_value{ bigint{ 256 } };
                    }
                }
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
                if (iInstanceBodies == 0 && !static_expression(aDefinition->child(0)))
                    recover([&]() { error(aDefinition->child(0), "the digits must be static (3.5.7(5))"); });
                // (positive, at most System.Max_Digits (3.5.7(6)))
                if (digits.integer() <= bigint{ 0 } || digits.integer() > bigint{ 15 })
                    recover([&]() { error(aDefinition->child(0), "digits must be positive and at most System.Max_Digits"); });
                if (auto* r = aDefinition->child(1))
                    for (auto* bound : { r->child(0), r->child(1) })
                        if (bound != nullptr)
                        {
                            // (the bounds: of a real type (3.5.7(5)))
                            bool real = false;
                            for (auto const& o : interpretations(bound, aRegion))
                                real = real || (o.type != nullptr && o.type->view()->is_real());
                            if (!real)
                                recover([&]() { error(bound, "the bounds of a floating point type's range must be real"); });
                        }
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
                    // (static (3.5.7(5)))
                    if (iInstanceBodies == 0)
                        for (auto* bound : { r->child(0), r->child(1) })
                            if (!static_expression(bound))
                                recover([&]() { error(bound, "the bounds of a floating point type's range must be static"); });
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
                if (iInstanceBodies == 0 && !static_expression(aDefinition->child(0)))
                    recover([&]() { error(aDefinition->child(0), "the delta must be static (3.5.9(6))"); });
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
                    // (a fixed point type's range: static (3.5.9(5)))
                    for (auto* bound : { rangeNode->child(0), rangeNode->child(1) })
                        if (!static_expression(bound))
                            recover([&]() { error(bound, "the bounds of a fixed point type's range must be static"); });
                    // (its values' mantissas: at most System.Max_Mantissa bits (3.5.9(12)); with its Small (specified later in the
                    // declarative part))
                    if (!decimal && iInstanceBodies == 0)
                        iDeferredAspects[aRegion].push_back({ first, "check'mantissa", rangeNode, nullptr, nullptr });
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
                    {
                        t->component = c->is(construct::AccessDefinition) ? access_definition(c, aRegion) : subtype_indication(c, aRegion);
                        if (c->is(construct::AccessDefinition) && t->component != nullptr)
                            t->component->component_access = true;
                        // (the component subtype: definite (3.6(10)), not abstract (3.9.3(8/3)))
                        if (indefinite_subtype(t->component, c, aRegion))
                            error(c, "a component subtype must be definite");
                        if (abstract_type(t->component, aRegion))
                            error(c, "the type of a component cannot be abstract");
                    }
                if (aDefinition->is(construct::TypeArrayUnconstrained))
                {
                    for (auto* index : aDefinition->children)
                        if (index->is(construct::IndexSubtype))
                        {
                            t->indexes.push_back(subtype_mark(index->child(0), aRegion));
                            predicate_check(t->indexes.back(), index);
                            // (an index subtype: discrete (3.6(8)))
                            if (auto* it = t->indexes.back(); it != nullptr && !it->view()->is_discrete() && !it->view()->is_formal &&
                                it->view()->kind != type_kind::Private)
                                recover([&]() { error(index, "an index subtype must be discrete"); });
                        }
                    t->constrained = false;
                    result = finish(t);
                }
                else
                {
                    // (a constrained array type: an anonymous unconstrained type, and its first subtype constrained)
                    std::vector<ada_type*> ranges;
                    for (auto* index : aDefinition->children)
                        if (index != componentDefinition)
                        {
                            ranges.push_back(discrete_range(index, nullptr, aRegion));
                            predicate_check(ranges.back(), index);
                            if (auto* it = ranges.back(); it != nullptr && !it->view()->is_discrete() && !it->view()->is_formal &&
                                it->view()->kind != type_kind::Private)
                                recover([&]() { error(index, "an index subtype must be discrete"); });
                        }
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
                // (the parent subtype: not class-wide (3.4(1/2)))
                if (parent->view()->kind == type_kind::ClassWide)
                    error_in_private_part_too(parentNode, "the parent type of a derived type cannot be class-wide");
                // (new discriminants: the parent subtype constrained if it has discriminants (3.7(13)))
                if (aDiscriminants != nullptr && aDiscriminants->is(construct::Discriminants) && !parent->constrained &&
                    parent->discriminant_values.empty() && !(parentNode->size() > 1u))
                    for (auto const& c : parent->view()->base_type()->view()->components)
                        if (c.discriminant && !c.stored)
                        {
                            recover([&]() { error(parentNode, "the parent subtype of a derived type with new discriminants must be constrained"); });
                            break;
                        }
                // (an untagged derived type's new discriminants: each used in the parent subtype's constraint (3.7(13)): its parent
                // discriminated)
                if (aDiscriminants != nullptr && aDiscriminants->is(construct::Discriminants) && aDefinition->first(construct::Extension) == nullptr &&
                    iInstanceBodies == 0 && !parent->view()->is_formal && !parent->view()->base_type()->view()->tagged &&
                    parent->view()->kind != type_kind::ClassWide && parent->view()->kind != type_kind::Private &&
                    !parent->view()->base_type()->view()->unknown_discriminants &&
                    std::none_of(parent->view()->base_type()->view()->components.begin(), parent->view()->base_type()->view()->components.end(),
                        [](component_info const& c) { return c.discriminant; }))
                    recover([&]() { error(aDiscriminants, "an untagged derived type's new discriminants must constrain its parent's discriminants (3.7(13))"); });
                // (the parent type: completely defined (it is frozen, 13.14(7)); not by a private extension ("with private"))
                bool const privateExtension = [&]()
                {
                    auto* x = aDefinition->first(construct::Extension);
                    if (x == nullptr)
                        return false;
                    std::string text{ x->source };
                    for (auto& ch : text)
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    return text.find("record") == std::string::npos && text.find("private") != std::string::npos;
                }();
                if (!privateExtension && not_completely_defined(parent, aRegion))
                    error(parentNode, "the parent type of a derived type must be completely defined (its full declaration has not occurred yet)");
                // (an untagged partial view whose full view is tagged: no derivatives of the partial view within its immediate scope (e.g. in
                // a child unit's visible part) (7.3(7.3/2)))
                if (iInstanceBodies == 0)
                    if (auto* pv = parent->kind == type_kind::Private ? parent : parent->base != nullptr && parent->base->kind == type_kind::Private ? parent->base : nullptr;
                        pv != nullptr && pv->untagged_partial && pv->full != nullptr && pv->full->base_type()->view()->tagged && pv->declared_in != nullptr &&
                        pv->declared_in->owner != nullptr && !private_part_visible_here(pv->declared_in->owner, aRegion))
                    {
                        bool within = false;
                        for (region* jump = nullptr, *r = aRegion; r != nullptr && !within; r = outward(r, jump))
                            within = r == pv->declared_in;
                        if (within)
                            recover([&]() { error(parentNode, "a type cannot be derived from an untagged partial view whose full view is tagged within its immediate scope (7.3(7.3/2))"); });
                    }
                auto* extension = aDefinition->first(construct::Extension);
                // (within a generic body: a type extension not a descendant of a formal type of the generic (3.9.1(4/2)))
                // (a formal of the generic (or of a formal package of it) whose body encloses the declaration)
                auto const formalOfEnclosingBody = [&](ada_type* aFormal)
                {
                    auto* d = aFormal->view()->base_type()->view()->declared_in;
                    for (int hops = 0; d != nullptr && hops < 8; ++hops, d = d->owner != nullptr ? d->owner->scope : nullptr)
                        if (d->generic_formals && d->owner != nullptr)
                            for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
                                if (r->owner == d->owner && (r->package_body || r->is_subprogram))
                                    return true;
                    return false;
                };
                if (extension != nullptr && iInstanceBodies == 0 && iSpeculation == 0 && within_generic_body(aRegion))
                    for (auto* a = parent; a != nullptr; a = a->view()->base_type()->view()->parent)
                        if ((a->view()->is_formal || a->view()->base_type()->view()->is_formal) && formalOfEnclosingBody(a))
                        {
                            recover([&]() { error(parentNode, "a type extension in a generic body cannot be a descendant of a formal type of the generic (3.9.1(4/2))"); });
                            break;
                        }
                // (a tagged parent: a record extension part (or a private extension) (3.4(5/2)))
                // (not the full view of an untagged partial view, where that full view is not visible (7.3(7.3/2)))
                bool const untaggedPartial = [&]()
                {
                    auto* partial = parent->kind == type_kind::Private ? parent : parent->base != nullptr && parent->base->kind == type_kind::Private ? parent->base :
                        parent->base_type()->partial;
                    if (partial == nullptr || !(partial->untagged_partial || !partial->tagged) || partial->is_formal)
                        return false;
                    auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                    return package != nullptr && package->is_package() && !private_part_visible(package, aRegion);
                }();
                if (extension == nullptr && iInstanceBodies == 0 && parent->view()->kind != type_kind::ClassWide && !untaggedPartial &&
                    parent->view()->base_type()->view()->tagged && (!parent->view()->base_type()->view()->is_formal || !iInstantiationSites.empty()) &&
                    parent->view()->base_type()->view()->kind != type_kind::Interface)
                    recover([&]() { error(aDefinition, "a type derived from a tagged type must have a record extension part (3.4(5/2))"); });
                auto* t = derive(parent, aName, aEntity, aRegion, extension, aDiscriminants);
                t->abstract = aDefinition->has_flag("abstract");
                // (derived from an interface: limited only if declared limited (or synchronized) (7.5(6.1/3)))
                if (parent->view()->base_type()->view()->kind == type_kind::Interface)
                    t->limited = aDefinition->has_flag("limited") || aDefinition->has_flag("synchronized");
                // (a limited record extension of a type with a specified Read (Write) attribute: its own limited components with
                // Read (Write) attributes specified (13.13.2(9/1)... (9/3)))
                if (extension != nullptr && iInstanceBodies == 0 && limited_type(t, aRegion))
                    for (auto const* attribute : { "read", "write" })
                    {
                        bool specified = false;
                        for (auto* x = parent->view()->base_type(); x != nullptr && !specified; )
                        {
                            specified = type_aspect(x, attribute) != nullptr;
                            auto* next = x->view()->parent != nullptr ? x->view()->parent->view()->base_type() : nullptr;
                            x = next != x ? next : nullptr;
                        }
                        if (!specified)
                            continue;
                        for (auto const& c : t->components)
                            if (c.ent != nullptr && c.ent->owner == aEntity && !c.discriminant && c.type != nullptr && limited_type(c.type, aRegion))
                            {
                                bool available = false;
                                for (auto* x = c.type->view()->base_type(); x != nullptr && !available; )
                                {
                                    available = type_aspect(x, attribute) != nullptr;
                                    auto* next = x->view()->parent != nullptr ? x->view()->parent->view()->base_type() : nullptr;
                                    x = next != x ? next : nullptr;
                                }
                                if (!available)
                                {
                                    recover([&]() { error(aDefinition, std::string{ "a limited extension of a type with a specified " } + attribute +
                                        " attribute cannot have a limited component without one (13.13.2(9/1))"); });
                                    break;
                                }
                            }
                    }
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
                // (a limited (or synchronized) derived type's parent and progenitors)
                bool const limitedDerived = aDefinition->has_flag("limited") || aDefinition->has_flag("synchronized");
                progenitor_check(parent, parentNode, limitedDerived, aDefinition->has_flag("synchronized"), type_kind::Record);
                if (auto* interfaces = aDefinition->first(construct::Interfaces))
                    for (auto* i : interfaces->children)
                    {
                        t->base_type()->interfaces.push_back(progenitor(i, aRegion));
                        progenitor_check(t->base_type()->interfaces.back(), i, limitedDerived, aDefinition->has_flag("synchronized"), type_kind::Record);
                    }
                progenitor_mix_check(parent, t->base_type()->interfaces, aDefinition);
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
                incomplete_use use{ *this, incomplete_uses::Allowed };
                t->designated = subtype_indication(aDefinition->first(construct::SubtypeIndication), aRegion);
                // (of an incomplete view: no null exclusion, and only a discriminant constraint of one with known discriminants
                // (3.10.1(9.3/2)))
                if (auto* indication = aDefinition->first(construct::SubtypeIndication); indication != nullptr && iInstanceBodies == 0 &&
                    !indication->children.empty())
                {
                    std::vector<node*> parts;
                    for (auto* c : indication->children)
                        if (!c->is(construct::Flag))
                            parts.push_back(c);
                    auto* mark = parts.empty() ? indication->child(0) : parts.front();
                    bool const constrained = parts.size() > 1u || mark->is(construct::Apply);
                    if (mark->is(construct::Apply) && !mark->children.empty())
                        mark = mark->child(0);
                    auto* e = mark->ent;
                    auto* it = e != nullptr && e->is_type() ? e->type : nullptr;
                    bool const limitedView = e != nullptr && e->scope != nullptr && e->scope->owner != nullptr && e->scope->owner->limited_view;
                    if (it != nullptr && it->incomplete && (limitedView || (it->full == nullptr && !it->completing && !e->completed)) && (constrained ||
                        indication->has_flag("not null")))
                    {
                        node* declaration = e->decl;
                        while (declaration != nullptr && !declaration->is(construct::Type) && !declaration->is(construct::TypeIncomplete))
                            declaration = declaration->parent;
                        bool const discriminants = declaration != nullptr && (declaration->first(construct::Discriminants) != nullptr ||
                            (declaration->first(construct::TypeIncomplete) != nullptr &&
                            declaration->first(construct::TypeIncomplete)->first(construct::Discriminants) != nullptr));
                        bool const discriminantConstraint = parts.size() == 1u && parts.front()->is(construct::Apply) && !limitedView;
                        if (indication->has_flag("not null") || !(discriminants && discriminantConstraint))
                            recover([&]() { error(indication, "the designated subtype of an incomplete view can have only a discriminant constraint "
                                "(of a type with known discriminants), and no null exclusion (3.10.1(9.3/2))"); });
                    }
                }
                default_storage_pool(t, aDefinition, aRegion);
                break;
            }
        case construct::TypeAccessSubprogram:
            {
                auto* t = new_type(type_kind::AccessSubprogram, aName);
                t->protected_access = aDefinition->has_flag("protected");
                t->size = 8u;
                // (not null access procedure ...: its first subtype excludes null)
                if (auto* parent = aDefinition->parent; parent != nullptr && parent->has_flag("not null"))
                    t->not_null = true;
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
                t->task_interface = aDefinition->has_flag("task");
                t->protected_interface = aDefinition->has_flag("protected");
                if (auto* interfaces = aDefinition->first(construct::Interfaces))
                    for (auto* i : interfaces->children)
                    {
                        auto* p = progenitor(i, aRegion)->view()->base_type()->view();
                        t->interfaces.push_back(p);
                        // (a descendant of a nonlimited interface: nonlimited; of a synchronized, task or protected interface: of that kind
                        // (3.9.4(10/2)))
                        if (p->kind != type_kind::Interface)
                            continue;
                        if ((!p->limited && t->limited) || (p->synchronized_interface && !t->synchronized_interface) ||
                            (p->task_interface && !t->task_interface) || (p->protected_interface && !t->protected_interface))
                            error_in_private_part_too(i, "an interface cannot have a progenitor of an incompatible kind (limited, synchronized, task or protected)");
                    }
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
        incomplete_use use{ *this, incomplete_uses::Allowed };
        for (auto* c : aDefinition->children)
            if (!c->is(construct::Flag))
                t->designated = subtype_mark(c, aRegion);
        default_storage_pool(t, aDefinition, aRegion, false);   // (a null default pool's)
        return t;
    }

    void program::component_list(node* aComponents, ada_type* aType, region* aRegion, int aVariant)
    {
        for (auto* c : aComponents->children)
            recover([&]()
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
                            if (d->is(construct::AccessDefinition) && type != nullptr)
                                type->component_access = true;
                            indication = d->is(construct::AccessDefinition) ? nullptr : d;
                            // (the component subtype: definite (3.6(10)))
                            if (indefinite_subtype(type, indication, aRegion))
                                error(d, "a component subtype must be definite");
                            if (abstract_type(type, aRegion))
                                error(d, "the type of a component cannot be abstract");
                            // (not of the type being declared (its current instance: not a subtype here, 8.6(17/3)))
                            if (type != nullptr && type->base_type() == aType->base_type() && iInstanceBodies == 0)
                                error(d, "a component cannot be of the type being declared");
                            // (a discriminant in a component's constraint: alone, as a bound of an index constraint or a value of a
                            // discriminant constraint (3.8(12/3)))
                            if (indication != nullptr && aType->decl != nullptr)
                            {
                                std::function<void(node*, bool)> check = [&](node* aNode, bool aAlone)
                                {
                                    if (aNode->ent != nullptr && aNode->ent->kind == entity_kind::Discriminant && aNode->ent->owner == aType->decl &&
                                        (aNode->is(construct::Identifier) || aNode->is(construct::Selected)))
                                    {
                                        if (!aAlone)
                                            recover([&]() { error(aNode, "a discriminant in a component's constraint must appear alone"); });
                                        return;
                                    }
                                    for (auto* x : aNode->children)
                                        check(x, false);
                                };
                                // (an index constraint's bound or a discriminant constraint's value: alone)
                                auto const bound_check = [&](node* bound)
                                {
                                    if (bound->is(construct::Range))
                                        for (auto* b : bound->children)
                                            check(b, true);
                                    else if (bound->is(construct::Association))
                                        check(bound->children.back(), true);
                                    else if (bound->is(construct::SubtypeIndication))
                                    {
                                        // (a discrete subtype indication: its range constraint's bounds alone)
                                        for (auto* x : bound->children)
                                            if (x->is(construct::ConstraintRange))
                                                for (auto* r : x->children)
                                                    if (r->is(construct::Range))
                                                        for (auto* b : r->children)
                                                            check(b, true);
                                                    else
                                                        check(r, false);
                                    }
                                    else
                                        check(bound, true);
                                };
                                auto const constraint = [&](node* aConstraint, bool aIndexOrDiscriminant)
                                {
                                    if (!aIndexOrDiscriminant)
                                    {
                                        check(aConstraint, false);
                                        return;
                                    }
                                    for (auto* bound : aConstraint->children)
                                        bound_check(bound);
                                };
                                if (indication->is(construct::Apply) && indication->size() > 1u)
                                {
                                    for (std::size_t k = 1u; k < indication->size(); ++k)
                                        bound_check(indication->child(k));
                                }
                                else if (indication->is(construct::SubtypeIndication))
                                    for (auto* x : indication->children)
                                        if (x->is(construct::ConstraintRange) || x->is(construct::ConstraintDigits) || x->is(construct::ConstraintDelta))
                                            constraint(x, false);
                                        else if (x->is(construct::ConstraintIndex))
                                            constraint(x, true);
                                        else if (x->is(construct::Apply))
                                            for (std::size_t k = 1u; k < x->size(); ++k)
                                                bound_check(x->child(k));
                            }
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
                        ce->aliased_declared = ce->aliased;
                        component_info info;
                        info.ent = ce;
                        info.type = type;
                        info.default_expression = defaultExpression;
                        info.variant = aVariant;
                        info.aliased = ce->aliased;
                        aType->components.push_back(info);
                    }
                    if (defaultExpression != nullptr)
                        recover([&]() { resolve(defaultExpression, type, aRegion); limited_value_check(defaultExpression, type, aRegion); });
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
                            recover([&]()
                            {
                                auto [low, high] = choice_range(choice, discriminant != nullptr ? discriminant->type : nullptr, aRegion);
                                if (low && high)
                                    info.ranges.emplace_back(*low, *high);
                                // (of a static constrained discriminant subtype: within it (3.8.1(10)))
                                auto* dt = discriminant != nullptr && discriminant->type != nullptr ? discriminant->type->view() : nullptr;
                                if (low && high && !(*high < *low) && dt != nullptr && iInstanceBodies == 0 && static_subtype(discriminant->type) &&
                                    dt->low.is_integer() && dt->high.is_integer() && (*low < dt->low.integer() || dt->high.integer() < *high))
                                    error(choice, "a variant's choice covers values outside the discriminant's subtype (3.8.1(10))");
                                // (static (3.8.1(4)))
                                if (!static_choice(choice, discriminant != nullptr ? discriminant->type : nullptr, aRegion))
                                    error(choice, "a variant's choice must be static");
                            });
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
            });
    }

    std::pair<std::optional<bigint>, std::optional<bigint>> program::choice_range(node* aChoice, ada_type* aType, region* aRegion)
    {
        // (a discrete choice's values: an expression, a range, or a subtype)
        if (aChoice->is(construct::Range))
        {
            for (auto* bound : { aChoice->child(0), aChoice->child(1) })
                if (auto* bt = resolve(bound, aType, aRegion); bt != nullptr && aType != nullptr && iSpeculation == 0 && iInstanceBodies == 0 &&
                    !bt->is_universal() && !bt->view()->is_formal && !aType->view()->is_formal && !covers(aType, bt))
                    error(bound, "the bound of the choice is not of the expected type");
            auto low = evaluate(aChoice->child(0), aRegion);
            auto high = evaluate(aChoice->child(1), aRegion);
            if (low.is_integer() && high.is_integer())
                return { low.integer(), high.integer() };
            return {};
        }
        if (aChoice->is(construct::SubtypeIndication) || is_subtype_name(aChoice, aRegion))
        {
            auto* t = discrete_range(aChoice, aType, aRegion);
            // (of the selector's type (5.4(5)))
            if (t != nullptr && aType != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && !t->view()->is_formal && !aType->view()->is_formal &&
                !covers(aType, t))
                error(aChoice, "the subtype of the choice is not of the expected type");
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

    namespace
    {
        // (a set of discrete values: disjoint ascending intervals)
        using value_set = std::vector<std::pair<bigint, bigint>>;
        value_set normalized(value_set aSet)
        {
            std::sort(aSet.begin(), aSet.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
            value_set result;
            for (auto const& i : aSet)
            {
                if (i.second < i.first)
                    continue;
                if (!result.empty() && i.first <= result.back().second + bigint{ 1 })
                {
                    if (result.back().second < i.second)
                        result.back().second = i.second;
                }
                else
                    result.push_back(i);
            }
            return result;
        }
        value_set intersection(value_set const& a, value_set const& b)
        {
            value_set result;
            for (auto const& x : a)
                for (auto const& y : b)
                {
                    auto const low = x.first < y.first ? y.first : x.first;
                    auto const high = x.second < y.second ? x.second : y.second;
                    if (!(high < low))
                        result.emplace_back(low, high);
                }
            return normalized(result);
        }
        value_set complement(value_set const& a, bigint const& aLow, bigint const& aHigh)
        {
            value_set result;
            bigint next = aLow;
            for (auto const& x : normalized(a))
            {
                if (next < x.first)
                    result.emplace_back(next, x.first - bigint{ 1 });
                if (next < x.second + bigint{ 1 })
                    next = x.second + bigint{ 1 };
            }
            if (!(aHigh < next))
                result.emplace_back(next, aHigh);
            return result;
        }
    }

    std::optional<std::vector<std::pair<bigint, bigint>>> program::static_values(ada_type* aType, region* aRegion)
    {
        // (the values of a static discrete subtype: its range, less those that its static predicates exclude (3.2.4))
        if (aType == nullptr)
            return {};
        auto* v = aType->view();
        if (!v->is_discrete() || !v->low.is_integer() || !v->high.is_integer())
            return {};
        auto* b = v->base_type()->view();
        bigint const baseLow = b->low.is_integer() ? b->low.integer() : v->low.integer();
        bigint const baseHigh = b->high.is_integer() ? b->high.integer() : v->high.integer();
        value_set result{ { v->low.integer(), v->high.integer() } };
        for (auto* p = aType->predicate != nullptr ? aType->predicate : v->predicate; p != nullptr; p = p->parent)
        {
            // (not resolved yet (at the end of the declarative part): resolved now (here, at a body or a statement, it is frozen))
            if (p->is_static && p->function == nullptr)
                for (auto& [r, list] : iDeferredAspects)
                {
                    auto i = std::find_if(list.begin(), list.end(), [&](auto const& d) { return d.value == p->expression; });
                    if (i == list.end())
                        continue;
                    auto entry = *i;
                    list.erase(i);
                    auto* where = r;
                    auto rest = std::move(list);
                    iDeferredAspects[where] = { entry };
                    resolve_deferred_aspects(where);
                    auto& again = iDeferredAspects[where];
                    again.insert(again.begin(), rest.begin(), rest.end());
                    break;
                }
            if (!p->is_static || p->function == nullptr || p->function->sub == nullptr || p->function->sub->parameters.empty())
                return {};
            auto* instance = p->function->sub->parameters[0].ent;
            std::function<std::optional<value_set>(node*)> values = [&](node* e) -> std::optional<value_set>
            {
                if (e == nullptr)
                    return {};
                if (e->is(construct::Parenthesized))
                    return values(e->child(0));
                auto const current = [&](node* n) { return n != nullptr && n->ent != nullptr && n->ent == instance; };
                if (e->is(construct::And) || e->is(construct::AndThen) || e->is(construct::Or) || e->is(construct::OrElse))
                {
                    auto l = values(e->child(0));
                    auto r = values(e->child(1));
                    if (!l || !r)
                        return {};
                    if (e->is(construct::And) || e->is(construct::AndThen))
                        return intersection(*l, *r);
                    auto u = *l;
                    u.insert(u.end(), r->begin(), r->end());
                    return normalized(u);
                }
                if (e->is(construct::Not))
                {
                    auto x = values(e->child(0));
                    if (!x)
                        return {};
                    return complement(*x, baseLow, baseHigh);
                }
                if (e->is(construct::In) && current(e->child(0)))
                {
                    value_set u;
                    for (std::size_t i = 1u; i < e->size(); ++i)
                    {
                        auto* c = e->child(i);
                        if (c->is(construct::Flag))
                            continue;
                        if (c->is(construct::SubtypeIndication) || meaning_of(c) == meaning::Type)
                        {
                            auto* ct = c->type != nullptr ? c->type : nullptr;
                            auto x = static_values(ct, aRegion);
                            if (!x)
                                return {};
                            u.insert(u.end(), x->begin(), x->end());
                            continue;
                        }
                        node* lowNode = c->is(construct::Range) ? c->child(0) : c;
                        node* highNode = c->is(construct::Range) ? c->child(1) : c;
                        auto low = evaluate(lowNode, aRegion);
                        auto high = evaluate(highNode, aRegion);
                        if (!low.is_integer() || !high.is_integer())
                            return {};
                        u.emplace_back(low.integer(), high.integer());
                    }
                    u = normalized(u);
                    return e->has_flag("not") ? complement(u, baseLow, baseHigh) : u;
                }
                if ((e->is(construct::Eq) || e->is(construct::Ne) || e->is(construct::Lt) || e->is(construct::Le) || e->is(construct::Gt) ||
                    e->is(construct::Ge)) && e->size() == 2u && (current(e->child(0)) != current(e->child(1))))
                {
                    bool const left = current(e->child(0));
                    auto value = evaluate(left ? e->child(1) : e->child(0), aRegion);
                    if (!value.is_integer())
                        return {};
                    auto const c = value.integer();
                    // (X op c, or c op X: as X op' c)
                    auto kind = e->kind;
                    if (!left)
                        kind = kind == construct::Lt ? construct::Gt : kind == construct::Gt ? construct::Lt : kind == construct::Le ? construct::Ge :
                            kind == construct::Ge ? construct::Le : kind;
                    switch (kind)
                    {
                    case construct::Eq: return value_set{ { c, c } };
                    case construct::Ne: return complement({ { c, c } }, baseLow, baseHigh);
                    case construct::Lt: return value_set{ { baseLow, c - bigint{ 1 } } };
                    case construct::Le: return value_set{ { baseLow, c } };
                    case construct::Gt: return value_set{ { c + bigint{ 1 }, baseHigh } };
                    case construct::Ge: return value_set{ { c, baseHigh } };
                    default: return {};
                    }
                }
                // (a static Boolean expression)
                if (auto x = evaluate(e, aRegion); x.is_integer())
                    return x.integer().is_zero() ? value_set{} : value_set{ { baseLow, baseHigh } };
                return {};
            };
            auto set = values(p->expression);
            if (!set)
                return {};
            result = intersection(result, *set);
        }
        return result;
    }

    bool program::static_choice(node* aChoice, ada_type* aType, region* aRegion)
    {
        // (a discrete choice that is static (3.8.1(4), 5.4(5)): a static expression, a range with static bounds, a static subtype)
        if (aChoice->is(construct::Others))
            return true;
        if (aChoice->is(construct::Range))
            return static_expression(aChoice->child(0)) && static_expression(aChoice->child(1));
        if (aChoice->is(construct::SubtypeIndication) || is_subtype_name(aChoice, aRegion) ||
            (aChoice->is(construct::Attribute) && name_text(aChoice->child(1)->source) == "range"))
        {
            auto [low, high] = choice_range(aChoice, aType, aRegion);
            if (!low.has_value() || !high.has_value())
                return false;
            // (a static subtype: not one with a dynamic predicate, nor a subtype of a nonstatic one (4.9(26/3), 3.2.4(15/5)))
            if (auto* t = aChoice->type; t != nullptr && t->view()->is_scalar() && !static_subtype(t))
                return false;
            return true;
        }
        return static_expression(aChoice);
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
        t->derived_partial = nullptr;
        // (no discriminant part: the parent's view's unknown discriminants inherited (3.7(26)))
        if (aDiscriminants == nullptr)
        {
            auto* pv = aParent->kind == type_kind::Private ? aParent : aParent->base != nullptr && aParent->base->kind == type_kind::Private ? aParent->base : nullptr;
            bool unknown = parentBase->inherited_unknown_discriminants;
            if (pv != nullptr && pv->unknown_discriminants && !pv->is_formal)
            {
                auto* package = pv->declared_in != nullptr ? pv->declared_in->owner : nullptr;
                unknown = unknown || package == nullptr || !private_part_visible_here(package, aRegion);
            }
            t->inherited_unknown_discriminants = unknown;
        }
        else
            t->inherited_unknown_discriminants = false;
        // (a parent subtype that excludes null (e.g. new not null T): so does the first subtype (3.4(6/2)))
        if (t->is_access() && (aParent->not_null || parentView->not_null))
            t->not_null = true;
        // (derived from a partial view whose full view is not visible here: its characteristics are the partial view's (7.3.1(5/1)))
        // (a constrained subtype of the partial view: of its full type, whose partial view it is)
        if (auto* partial = aParent->kind == type_kind::Private ? aParent : aParent->base != nullptr && aParent->base->kind == type_kind::Private ? aParent->base :
            aParent->base_type()->partial; partial != nullptr && partial->full != nullptr && !partial->is_formal && (!partial->tagged || partial->untagged_partial))
            if (auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                package != nullptr && package->is_package() && !private_part_visible(package, aRegion))
                t->derived_partial = partial;
        if (aParent->view()->derived_partial != nullptr && t->derived_partial == nullptr)
            t->derived_partial = aParent->view()->derived_partial;
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
                std::vector<std::pair<entity*, std::string>> renamedDiscriminants;  // (a parent's discriminant: the new one that stands for it)
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
                            {
                                renamedDiscriminants.emplace_back(pc.ent, value->ent->name);
                                continue;
                            }
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
                // (a new discriminant: not a homograph of an inherited component, visible or not (8.3(26/2)))
                // (a component of a parent's full view that is not visible: only within its package or a descendant of it)
                bool const hiddenVisibleSomewhere = [&]()
                {
                    auto* pb = parentView->base_type();
                    auto* partial = pb->partial != nullptr ? pb->partial : pb->kind == type_kind::Private ? pb : nullptr;
                    if (partial == nullptr || partial->declared_in == nullptr || partial->declared_in->owner == nullptr)
                        return true;
                    auto const package = full_name(partial->declared_in->owner);
                    if (iCurrentUnit != nullptr && (iCurrentUnit->name == package || iCurrentUnit->name.starts_with(package + ".")))
                        return true;
                    for (auto* q = r; q != nullptr; q = q->parent)
                        if (q->owner == partial->declared_in->owner)
                            return true;
                    return false;
                }();
                if (iInstanceBodies == 0 && hiddenVisibleSomewhere)
                    for (auto const& c : inherited)
                        if (!c.discriminant && !c.stored && c.ent != nullptr)
                            for (auto const& d : t->components)
                                if (d.discriminant && !d.stored && d.ent != nullptr && d.ent->name == c.ent->name)
                                    recover([&]() { error(d.ent->decl != nullptr ? d.ent->decl : aDiscriminants, "\"" + d.ent->name +
                                        "\" is already declared in this declarative region"); });
                for (auto const& c : inherited)
                    t->components.push_back(c);
                // (the variant parts that a parent's discriminant governs: governed by the new discriminant that stands for it)
                for (auto& v : t->variants)
                    for (auto const& [old, name] : renamedDiscriminants)
                        if (v.discriminant == old)
                            for (auto const& o : t->components)
                                if (o.discriminant && o.ent != nullptr && o.ent->name == name)
                                    v.discriminant = o.ent;
            }
            for (auto const& c : t->components)
                if (c.ent != nullptr && !c.stored)
                    r->add(c.ent);
            if (aExtension != nullptr)
            {
                // (the parent of a type extension: tagged (3.9.1(2/2)))
                if (!parentBase->tagged && parentBase->kind != type_kind::Interface && !(aParent->kind == type_kind::Private && aParent->tagged) &&
                    !(aParent->base_type()->tagged) && iInstanceBodies == 0)
                    recover([&]() { error(aExtension, "the parent type of a type extension must be tagged"); });
                t->tagged = true;
                if (auto* list = aExtension->first(construct::Components))
                    component_list(list, t, r, -1);
                if (aExtension->has_flag("private"))
                    t->kind = type_kind::Private;
            }
        }
        // (the first subtype: constrained as the parent subtype is)
        ada_type* result = t;
        // (a partial view's: its full view's)
        if (aParent->is_subtype || (parentView->low.known() && t->is_scalar()) || (parentView->kind == type_kind::Array && parentView->constrained) ||
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
        // (hidden from all visibility until the end of the declaration (8.3(16)))
        ada_type* t = nullptr;
        {
            declaring hiding;
            hiding.add(e);
            // (an incomplete view: its subtype mark alone (no constraint or null exclusion) (3.10.1(8.2/2)))
            bool const markOnly = indication != nullptr && indication->size() == 1u && !indication->has_flag("not null") &&
                !indication->child(0)->is(construct::Apply);
            incomplete_use use{ *this, markOnly ? incomplete_uses::Allowed : iIncompleteUses };
            t = subtype_indication(indication, aRegion);
        }
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
        if (t == iIntegerLiteral)
            t = iUniversalInteger;
        else if (t == iRealLiteral)
            t = iUniversalReal;

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
        // (an array type's component: a partial view whose full view is not visible where the array type is declared is seen as
        // the partial view (the array type's characteristics are its (7.3.1(5/1))))
        ada_type* componentView = nullptr;
        if (t->kind == type_kind::Array && t->component != nullptr)
        {
            componentView = t->component->view();
            auto* c = t->component;
            if (auto* partial = c->kind == type_kind::Private ? c : c->base != nullptr && c->base->kind == type_kind::Private ? c->base : nullptr;
                partial != nullptr && partial->full != nullptr && !partial->is_formal && t->declared_in != nullptr)
                if (auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                    package != nullptr && package->is_package() && !private_part_visible(package, t->declared_in))
                    componentView = partial;
        }
        if (t->is_scalar() || (t->kind == type_kind::Array && t->indexes.size() == 1u && componentView != nullptr && componentView->is_discrete()))
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
            (t->kind == type_kind::Array && t->indexes.size() == 1u && componentView != nullptr && componentView->kind != type_kind::Private &&
                componentView->base_type()->boolean))
        {
            make("and", intrinsic::And, { t, t }, t);
            make("or", intrinsic::Or, { t, t }, t);
            make("xor", intrinsic::Xor, { t, t }, t);
            make("not", intrinsic::Not, { t }, t);
        }
        if (t->kind == type_kind::Array && t->indexes.size() == 1u && !limited && !(componentView != nullptr && componentView->kind == type_kind::Private &&
            componentView->limited))
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
                speculation guard{ *this };
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
