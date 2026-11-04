/*
  attributes.cpp

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

// Ada: attributes (their types, the resolution of their prefixes and arguments, and their static values)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        std::vector<node*> arguments_of(node* aAttribute)
        {
            std::vector<node*> result;
            if (auto* arguments = aAttribute->first(construct::Arguments))
                for (auto* a : arguments->children)
                    result.push_back(a);
            return result;
        }

        // (the image of an enumeration literal or a static value, for 'Width)
        std::size_t image_length(bigint const& aValue)
        {
            return aValue.to_string().size() + (aValue.negative() ? 0u : 1u);
        }
    }

    ada_type* program::range_of(node* aRangeAttribute, region* aRegion)
    {
        // (X'Range or X'Range (N): the subtype of the index range of an array (or of a scalar subtype))
        auto* prefix = aRangeAttribute->child(0);
        std::size_t dimension = 0u;
        auto const arguments = arguments_of(aRangeAttribute);
        if (!arguments.empty())
        {
            resolve(arguments[0], nullptr, aRegion);
            auto v = evaluate(arguments[0], aRegion);
            if (v.is_integer())
                dimension = static_cast<std::size_t>(v.integer().to_int64() - 1);
            // (the dimension: static (3.6.2(3)))
            else if (iSpeculation == 0 && iInstanceBodies == 0 && !static_expression(arguments[0]))
                recover([&]() { error(arguments[0], "the dimension of 'Range must be static"); });
        }
        ada_type* t = nullptr;
        bool typePrefix = false;
        // (a subtype, T'Base or T'Class; another attribute (e.g. X'Image (Y)) is a value)
        if (is_subtype_name(prefix, aRegion) || (prefix->is(construct::Attribute) && arguments_of(prefix).empty() &&
            (name_text(prefix->child(1)->source) == "base" || name_text(prefix->child(1)->source) == "class")))
        {
            t = subtype_mark(prefix, aRegion);
            typePrefix = true;
            // (not of a scalar subtype with a predicate (3.2.4(29.2/4)))
            if (t != nullptr && t->view()->is_scalar())
                predicate_check(t, prefix);
            // (not of a partial view (its full view not visible here), nor a type derived from one)
            if (t != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && !t->view()->is_formal && partial_view_here(t, aRegion))
                error(prefix, "the prefix of 'Range must be a scalar or array subtype (the full view of the private type is not visible)");
        }
        else
        {
            t = resolve(prefix, nullptr, aRegion);
            ada_type* seen = t;
            if (t != nullptr && t->view()->kind == type_kind::Access)
            {
                seen = t->view()->designated;
                t = designated_of(t);
                prefix->mark |= 0x2000u;
            }
            // (an object of a partial view (its full view not visible here): not an array (7.3(4)))
            if (seen != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && partial_view_here(seen, aRegion))
                error(prefix, "the prefix of 'Range must be an array (the full view of the private type is not visible)");
        }
        if (t == nullptr)
            error(aRangeAttribute, "prefix of 'Range has no range");
        auto* v = t->view();
        // (an array (object or subtype) or a scalar subtype; a dimension for an array only (3.5(14), 3.6.2(7)))
        if (iSpeculation == 0 && !v->is_formal && v->kind != type_kind::Private &&
            ((v->kind != type_kind::Array && (!typePrefix || !arguments.empty() || !v->is_scalar()))))
            error(aRangeAttribute, "the prefix of 'Range must be an array or a scalar subtype");
        aRangeAttribute->type = nullptr;
        set_meaning(aRangeAttribute, meaning::Value);
        if (v->kind == type_kind::Array)
        {
            if (dimension >= v->indexes.size())
                error(aRangeAttribute, "dimension out of range");
            auto* index = v->indexes[dimension];
            if (v->constrained && meaning_of(prefix) == meaning::Type)
            {
                aRangeAttribute->type = index;
                return index;
            }
            // (an object's range: its bounds are read when it is evaluated (a subtype of the index type, not static unless the
            // object's subtype is))
            auto* result = new_subtype(index->base_type());
            // (an implicit dereference's: read (its prefix checked not to be null))
            if (v->constrained && !(prefix->mark & 0x2000u))
            {
                result->low = index->view()->low;
                result->high = index->view()->high;
                result->low_object = index->view()->low_object;
                result->high_object = index->view()->high_object;
            }
            else
            {
                result->low = static_value{};
                result->high = static_value{};
            }
            result->range_node = aRangeAttribute;
            aRangeAttribute->type = result;
            return result;
        }
        if (v->is_scalar())
        {
            aRangeAttribute->type = t;
            return t;
        }
        error(aRangeAttribute, "prefix of 'Range has no range");
    }

    ada_type* program::attribute_type(node* aAttribute, region* aRegion)
    {
        auto const name = name_text(aAttribute->child(1)->source);
        auto* prefix = aAttribute->child(0);
        auto const prefix_type = [&]() -> ada_type*
        {
            // (T'Base, T'Class: subtypes; another attribute (e.g. X'Image): a value)
            if (is_subtype_name(prefix, aRegion) || (prefix->is(construct::Attribute) && arguments_of(prefix).empty() &&
                (name_text(prefix->child(1)->source) == "base" || name_text(prefix->child(1)->source) == "class")))
                return subtype_mark(prefix, aRegion);
            auto const options = interpretations(prefix, aRegion);
            for (auto const& o : options)
                if (o.type != nullptr)
                {
                    if (o.type->view()->kind == type_kind::Access && (name == "first" || name == "last" || name == "length" || name == "range"))
                        return designated_of(o.type);
                    return o.type;
                }
            return nullptr;
        };
        if (name == "first" || name == "last" || name == "succ" || name == "pred" || name == "min" || name == "max" || name == "val" ||
            name == "value" || name == "wide_value" || name == "wide_wide_value" || name == "floor" || name == "ceiling" || name == "rounding" ||
            name == "truncation" || name == "unbiased_rounding" || name == "machine_rounding" || name == "machine" || name == "model" ||
            name == "remainder" || name == "adjacent" || name == "copy_sign" || name == "fraction" || name == "scaling" || name == "compose" ||
            name == "leading_part" || name == "round" || name == "safe_first" || name == "safe_last" || name == "model_small" || name == "small" ||
            name == "delta" || name == "model_epsilon" || name == "base" || name == "mod" || name == "enum_val")
        {
            auto* t = prefix_type();
            if (t == nullptr)
                return nullptr;
            if (t->view()->kind == type_kind::Array && (name == "first" || name == "last"))
            {
                // (the dimension's index type (the argument: static))
                std::size_t dimension = 0u;
                if (auto* arguments = aAttribute->first(construct::Arguments); arguments != nullptr && arguments->size() >= 1u)
                {
                    auto* a = arguments->child(0);
                    if (a->is(construct::Number))
                        dimension = static_cast<std::size_t>(std::max<long long>(1, std::stoll(std::string{ a->source })) - 1);
                    else
                    {
                        auto v = evaluate(a, aRegion);
                        if (v.is_integer())
                            dimension = static_cast<std::size_t>(std::max<std::int64_t>(1, v.integer().to_int64()) - 1);
                    }
                }
                auto const& indexes = t->view()->indexes;
                return dimension < indexes.size() ? indexes[dimension]->base_type() : nullptr;
            }
            if (name == "small" || name == "delta" || name == "model_small" || name == "model_epsilon")
                return iUniversalReal;
            return t->base_type();
        }
        if (name == "pos" || name == "length" || name == "size" || name == "width" || name == "wide_width" || name == "wide_wide_width" ||
            name == "digits" || name == "aft" || name == "fore" || name == "exponent" || name == "count" || name == "component_size" ||
            name == "alignment" || name == "position" || name == "first_bit" || name == "last_bit" || name == "modulus" || name == "enum_rep" ||
            name == "machine_mantissa" || name == "machine_emax" || name == "machine_emin" || name == "machine_radix" || name == "model_mantissa" || name == "mantissa" ||
            name == "model_emin" || name == "scale" || name == "object_size" || name == "max_size_in_storage_elements" || name == "storage_size" ||
            name == "value_size" || name == "max_alignment_for_allocation" || name == "partition_id" || name == "priority" || name == "stream_size")
        {
            if (name == "pos" || name == "enum_rep")
                return iUniversalInteger;
            return iUniversalInteger;
        }
        if (name == "image" || name == "img" || name == "external_tag" || name == "body_version" || name == "version")
            return iString;
        if (name == "wide_image")
            return string_type_named("wide_string");
        if (name == "wide_wide_image")
            return string_type_named("wide_wide_string");
        if (name == "valid" || name == "constrained" || name == "callable" || name == "terminated" || name == "definite" || name == "signed_zeros" ||
            name == "denorm" || name == "machine_overflows" || name == "machine_rounds" || name == "has_same_storage" || name == "overlaps_storage" ||
            name == "preelaborable_initialization" || name == "has_tagged_values" || name == "valid_scalars")
            return iBoolean;
        if (name == "access" || name == "unchecked_access" || name == "unrestricted_access")
        {
            // (a subprogram's: any access-to-subprogram type (its profile is checked when it is resolved))
            if (prefix->is(construct::Identifier) || prefix->is(construct::Selected) || prefix->is(construct::Operator))
            {
                std::vector<entity*> entities;
                try { speculation guard{ *this }; entities = prefix_entities(prefix, aRegion); } catch (compile_error const&) {}
                if (!entities.empty() && std::all_of(entities.begin(), entities.end(), [](entity* e) { return e->is_subprogram() || e->kind == entity_kind::Entry; }))
                    return iUniversalAccess;
                // (a protected object's subprogram: Object.Operation'Access)
                if (entities.empty() && prefix->is(construct::Selected))
                    for (auto const& i : interpretations(prefix->child(0), aRegion))
                        if (i.type != nullptr && i.type->view()->kind == type_kind::Protected && i.type->view()->unit != nullptr &&
                            i.type->view()->unit->inner != nullptr && i.type->view()->unit->inner->names.contains(name_text(prefix->child(1)->source)))
                            return iUniversalAccess;
            }
            // (a dereference that may be of an access-to-subprogram value: likewise)
            if (prefix->is(construct::All))
                for (auto const& i : interpretations(prefix->child(0), aRegion))
                    if (i.type != nullptr && i.type->view()->kind == type_kind::AccessSubprogram)
                        return iUniversalAccess;
            auto* t = new_type(type_kind::Access, {});
            t->anonymous_access = true;
            t->size = 8u;
            t->designated = prefix_type();
            return t;
        }
        if (name == "address")
            return system_type("address");
        // (an access type's storage pool: an object of Root_Storage_Pool'Class (13.11(13)))
        if (name == "storage_pool")
            if (auto* root = library_type("system.storage_pools", "root_storage_pool"))
                return class_wide(root);
        if (name == "tag")
            return tags_type("tag");
        if (name == "identity")
        {
            auto* prefix = aAttribute->child(0);
            if (prefix->is(construct::Identifier) || prefix->is(construct::Selected))
            {
                auto const entities = prefix_entities(prefix, aRegion);
                if (entities.size() == 1u && entities.front()->kind == entity_kind::Exception)
                    return library_type("ada.exceptions", "exception_id");
            }
            return task_identification_type();
        }
        if (name == "caller")
            return task_identification_type();
        if (name == "result")
        {
            // (F'Result: the function's result (its prefix names the function, not a call of it))
            ada_type* result = iCurrentSubprogram != nullptr && iCurrentSubprogram->sub != nullptr ? iCurrentSubprogram->sub->result : nullptr;
            if (iCurrentSubprogram == nullptr || iCurrentSubprogram->name != name_text(prefix->source))
                if (auto* e = resolve_entity(prefix, aRegion); e != nullptr && e->sub != nullptr && e->sub->result != nullptr)
                    result = e->sub->result;
            return result;
        }
        // (S'Class (X): a conversion to the class-wide type)
        if (name == "class" && !arguments_of(aAttribute).empty() && is_subtype_name(prefix, aRegion))
            if (auto* t = prefix_type(); t != nullptr)
                return t->view()->kind == type_kind::ClassWide ? t : class_wide(t);
        if (name == "old" || name == "loop_entry" || name == "class")
            return prefix_type();
        if (name == "range" || name == "reverse_range")
            return prefix_type();
        if (name == "input")
            return prefix_type();
        if (name == "reduce")
            return nullptr;
        return nullptr;
    }

    bool program::indexed_attribute(node* aAttribute)
    {
        // (X'Old (I), F'Result (I), X'Loop_Entry (I): an indexed component (or call, slice ...) of the attribute's value (they have
        // no parameters) (rewritten as one: Apply (X'Old, I)))
        if (!aAttribute->is(construct::Attribute) || aAttribute->size() < 2u)
            return false;
        auto const name = name_text(aAttribute->child(1)->source);
        auto const arguments = arguments_of(aAttribute);
        if ((name != "old" && name != "result" && name != "loop_entry") || arguments.empty())
            return false;
        auto* target = clone(aAttribute);
        std::erase_if(target->children, [](node* c) { return c->is(construct::Arguments); });
        target->source = std::string_view{ aAttribute->source.data(), static_cast<std::size_t>(aAttribute->child(1)->source.data() +
            aAttribute->child(1)->source.size() - aAttribute->source.data()) };
        aAttribute->kind = construct::Apply;
        aAttribute->children = { target };
        target->parent = aAttribute;
        for (auto* a : arguments)
        {
            aAttribute->children.push_back(a);
            a->parent = aAttribute;
        }
        aAttribute->mark &= ~0xFFu;
        return true;
    }

    std::optional<std::int64_t> program::static_level(node* aObject, region* aRegion)
    {
        // (the accessibility level of the object a name denotes, if static (3.10.2): an object's, where it is declared; a dereference's,
        // its access type's (an access parameter's or a stand-alone anonymous access object's: not static); a function result's, the
        // innermost master's)
        auto* o = aObject;
        while (o != nullptr && o->is(construct::Parenthesized))
            o = o->child(0);
        if (o == nullptr)
            return std::nullopt;
        auto const access = [&](node* aValue) -> std::optional<std::int64_t>
        {
            auto* t = aValue != nullptr && aValue->type != nullptr ? aValue->type->view() : nullptr;
            if (t == nullptr || t->kind != type_kind::Access || t->anonymous_access)
                return std::nullopt;
            return type_level(t);
        };
        if (o->is(construct::All))
            return access(o->child(0));
        switch (meaning_of(o))
        {
        case meaning::Object:
            if (auto* e = o->ent; e != nullptr)
            {
                if (e->renamed_name != nullptr && e->is_object())
                    return static_level(e->renamed_name, aRegion);
                if (e->renamed != nullptr && e->renamed != e)
                    e = e->renamed;
                // (a formal parameter: the level of the master representing the invocation (3.10.2(7/4)), its body's (an explicitly
                // aliased parameter of a function: statically, its body's too); a generic formal object: not static here)
                if (e->kind == entity_kind::Parameter && e->scope != nullptr && ((e->scope->owner != nullptr &&
                    e->scope->owner->is_subprogram() && !e->scope->owner->is_generic()) || (e->scope->owner == nullptr && iAcceptEntries.contains(e->scope))))
                    return accessibility_level(e->scope);
                // (a generic formal object in an instance (not a template): of mode in, a constant of the instance; of mode in out, a
                // renaming of the actual (12.4(10/2)))
                if (e->decl != nullptr && e->decl->parent != nullptr && e->decl->parent->is(construct::FormalObject) && e->scope != nullptr)
                {
                    bool template_ = false;
                    for (region* jump = nullptr, *r = e->scope; r != nullptr && !template_; r = outward(r, jump))
                        template_ = r->owner != nullptr && (r->owner->is_generic() || iTemplateInstances.contains(r->owner));
                    if (!template_)
                        return e->kind == entity_kind::Constant ? std::optional<std::int64_t>{ accessibility_level(e->scope) } : std::nullopt;
                }
                if (e->kind == entity_kind::Parameter || (e->decl != nullptr && e->decl->parent != nullptr && e->decl->parent->is(construct::FormalObject)))
                    return std::nullopt;
                if (e->scope == nullptr)
                    return std::nullopt;
                return accessibility_level(e->scope);
            }
            return std::nullopt;
        case meaning::Component:
        case meaning::Index:
        case meaning::Slice:
            if (o->mark & MarkDeref)
                return access(o->child(0));
            return static_level(o->child(0), aRegion);
        case meaning::Conversion:
            if (o->is(construct::Apply) && o->size() > 1u)
                return static_level(o->child(1), aRegion);
            return std::nullopt;
        case meaning::Call:
            // (a generalized reference's implicit dereference (4.1.5): its access discriminant's level, not static here)
            if (auto const* call = call_of(o); call != nullptr && call->callee != nullptr && call->callee->sub != nullptr &&
                call->callee->sub->result != nullptr && type_aspect(call->callee->sub->result, "implicit_dereference") != nullptr)
                return std::nullopt;
            return accessibility_level(aRegion) + 1;
        case meaning::Type:
            // (the current instance of a type: assumed deeper than any access type (the object might be declared anywhere))
            return std::numeric_limits<std::int64_t>::max();
        default:
            return std::nullopt;
        }
    }

    bool program::within_generic_body(region* aRegion) const
    {
        // (within the body of a generic unit (its template, or check_generic's instance of it): where accessibility is checked when an
        // instance runs)
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            if (r->owner != nullptr && (r->owner->is_generic() || iTemplateInstances.contains(r->owner)) && (r->package_body || r->is_subprogram))
                return true;
        return false;
    }

    void program::aliased_actuals_check(node* aExpression, ada_type* aAccess, region* aRegion)
    {
        // (the call that initializes an allocator (or is converted to a named access type): its master is the access type's; each
        // explicitly aliased actual: not statically deeper than it (6.4.1(6.4/3), 3.10.2(10.2/3, 10.3/3)))
        if (aExpression == nullptr || aAccess == nullptr || aAccess->view()->anonymous_access || iSpeculation != 0 || iInstanceBodies != 0 ||
            within_generic_body(aRegion))
            return;
        auto* e = aExpression;
        while (e != nullptr && (e->is(construct::Parenthesized) || e->is(construct::Qualified)))
            e = e->is(construct::Qualified) ? e->children.back() : e->child(0);
        auto const* call = e != nullptr ? call_of(e) : nullptr;
        if (call == nullptr || call->callee == nullptr || call->callee->sub == nullptr || call->callee->sub->result == nullptr)
            return;
        auto const& parameters = call->callee->sub->parameters;
        for (std::size_t i = 0u; i < parameters.size() && i < call->actuals.size(); ++i)
            if (parameters[i].aliased && call->actuals[i] != nullptr)
                if (auto const level = static_level(call->actuals[i], aRegion); level && *level > type_level(aAccess))
                    error(call->actuals[i], "an explicitly aliased parameter's actual cannot be statically deeper than the master of the call (6.4.1(6.4/3))");
    }

    bool program::aliased_view(node* aName)
    {
        // (an aliased view (3.10(9/3)): an object or component declared aliased, a dereference, a formal parameter or generic formal
        // object of a tagged type, the current instance of a type; a view conversion or renaming of one; when in doubt, aliased)
        auto* n = aName;
        while (n != nullptr && n->is(construct::Parenthesized))
            n = n->child(0);
        if (n == nullptr)
            return true;
        if (n->is(construct::All))
            return true;
        if (n->is(construct::Qualified))
            return aliased_view(n->child(1));
        switch (meaning_of(n))
        {
        case meaning::Object:
            {
                auto* e = n->ent;
                if (e == nullptr)
                    return true;
                if (e->renamed_name != nullptr)
                    return aliased_view(e->renamed_name);
                if (e->aliased_declared || e->kind == entity_kind::LoopParameter)
                    return true;
                auto* t = e->type != nullptr ? e->type->view() : nullptr;
                bool const tagged = t != nullptr && (t->tagged || t->kind == type_kind::ClassWide || t->base_type()->view()->tagged);
                if (e->kind == entity_kind::Parameter && tagged)
                    return true;
                if (e->decl != nullptr && e->decl->parent != nullptr && e->decl->parent->is(construct::FormalObject))
                    return tagged;
                if (e->kind == entity_kind::Variable || e->kind == entity_kind::Constant || e->kind == entity_kind::Parameter)
                    return t == nullptr || t->is_formal || (t->kind == type_kind::Private && t->full == nullptr);
                return true;
            }
        case meaning::Component:
            if (n->ent != nullptr && n->ent->kind == entity_kind::Component)
                return n->ent->aliased_declared;
            return true;
        case meaning::Index:
            if (n->is(construct::Apply) && !n->children.empty() && n->child(0)->type != nullptr)
            {
                auto* a = n->child(0)->type->view();
                if (a->kind == type_kind::Access && a->designated != nullptr)
                    a = a->designated->view();
                if (a->kind == type_kind::Array)
                    return a->base_type()->view()->aliased_components || a->aliased_components;
            }
            return true;
        case meaning::Slice:
            return false;   // (a slice: never aliased)
        case meaning::Conversion:
            if (n->is(construct::Apply) && n->size() > 1u)
                return aliased_view(n->child(1));
            return true;
        case meaning::Type:
            return true;    // (a current instance)
        default:
            return true;
        }
    }

    ada_type* program::resolve_attribute(node* aAttribute, ada_type* aExpected, region* aRegion)
    {
        auto const name = name_text(aAttribute->child(1)->source);
        auto* prefix = aAttribute->child(0);
        auto const arguments = arguments_of(aAttribute);
        set_meaning(aAttribute, meaning::Value);
        // (T'Base (X), T'Class (X): a conversion to the subtype T'Base (T'Class) (rewritten as one: Apply (T'Base, X)))
        if ((name == "base" || name == "class") && arguments.size() == 1u)
        {
            auto* target = clone(aAttribute);
            std::erase_if(target->children, [](node* c) { return c->is(construct::Arguments); });
            target->source = std::string_view{ aAttribute->source.data(), static_cast<std::size_t>(aAttribute->child(1)->source.data() +
                aAttribute->child(1)->source.size() - aAttribute->source.data()) };
            auto* operand = arguments[0];
            aAttribute->kind = construct::Apply;
            aAttribute->children = { target, operand };
            target->parent = aAttribute;
            operand->parent = aAttribute;
            aAttribute->mark &= ~0xFFu;
            return resolve(aAttribute, aExpected, aRegion);
        }
        // (X'Old (I), F'Result (I), X'Loop_Entry (I): an indexed component (or call, slice ...) of the attribute's value)
        if (indexed_attribute(aAttribute))
            return resolve(aAttribute, aExpected, aRegion);
        // (Standard's (implementation-defined) attributes: Standard'Maximum_Alignment ... (static))
        if (name == "maximum_alignment" || name == "storage_unit" || name == "word_size" || name == "max_integer_size" ||
            name == "address_size" || name == "system_allocator_alignment")
        {
            aAttribute->type = aExpected != nullptr && !aExpected->is_universal() && aExpected->view()->is_integer() ? aExpected->base_type() : iUniversalInteger;
            return aAttribute->type;
        }
        // (the prefix: a subtype (or a type's attribute), or an object or value (resolved without an expected type))
        bool const typePrefix = is_subtype_name(prefix, aRegion) || (prefix->is(construct::Attribute) && arguments_of(prefix).empty() &&
            (name_text(prefix->child(1)->source) == "base" || name_text(prefix->child(1)->source) == "class"));
        ada_type* prefixType = nullptr;
        if (typePrefix)
        {
            // (F'Result: of a function, not a type (6.1.1(29/3)))
            if (name == "result" && iSpeculation == 0 && iInstanceBodies == 0)
                error(aAttribute, "'Result can be used only in a postcondition of the function");
            prefixType = subtype_mark(prefix, aRegion);
            set_meaning(prefix, meaning::Type);
            // (T'Access of the current instance of a type, of a named access type: assumed statically deeper (3.10.2(28/3)))
            if (name == "access" && aExpected != nullptr && aExpected->view()->kind == type_kind::Access && !aExpected->view()->anonymous_access &&
                iSpeculation == 0 && (iInstanceBodies == 0 || iInstanceBodies == iInstancePrivateParts) && !within_generic_body(aRegion))
                error_in_private_part_too(prefix, "the current instance of a type is assumed statically deeper than a named access type (3.10.2(28/3))");
            // (the current instance: aliased only of an immutably limited type (3.10(9/3)))
            if ((name == "access" || name == "unchecked_access") && iSpeculation == 0 && iInstanceBodies == 0 && prefixType != nullptr &&
                !immutably_limited(prefixType) && [&]()
                {
                    // (a type being declared: as its declaration says (an explicitly limited record, an extension of an immutably limited
                    // parent))
                    auto* e = prefix->ent != nullptr && prefix->ent->is_type() ? prefix->ent : prefixType->base_type()->decl;
                    if (e != nullptr && e->completion != nullptr && e->completion->decl != nullptr)
                        e = e->completion;
                    node const* declaration = e != nullptr && e->decl != nullptr ? e->decl->parent : nullptr;
                    // (within the type's (full) declaration: that declaration)
                    for (node const* n = aAttribute->parent; n != nullptr; n = n->parent)
                        if (n->is(construct::Type))
                        {
                            if (auto* d = n->first(construct::Defining); d != nullptr && e != nullptr && name_text(d->source) == e->name)
                                declaration = n;
                            break;
                        }
                    if (declaration == nullptr || declaration->is(construct::TypeIncomplete) || prefixType->incomplete || prefixType->completing)
                        return declaration == nullptr;
                    if (auto* record = declaration->first(construct::TypeRecord); record != nullptr && (record->has_flag("limited") || declaration->has_flag("limited")))
                        return false;
                    if (auto* derived = declaration->first(construct::TypeDerived); derived != nullptr && !derived->children.empty())
                    {
                        auto* mark = derived->child(0)->is(construct::SubtypeIndication) && !derived->child(0)->children.empty() ? derived->child(0)->child(0) : derived->child(0);
                        if (auto* parent = resolve_entity(mark, aRegion); parent != nullptr && parent->is_type() && parent->type != nullptr &&
                            immutably_limited(parent->type))
                            return false;
                    }
                    return true;
                }())
                error(prefix, "the current instance of a type that is not immutably limited is not aliased (3.10(9/3))");
        }
        else if (name == "access" || name == "unchecked_access" || name == "unrestricted_access")
        {
            // ('Access of a subprogram: the one whose profile matches the expected access-to-subprogram type)
            if (aExpected != nullptr && aExpected->view()->kind == type_kind::AccessSubprogram && prefix->is(construct::All) &&
                aExpected->view()->profile != nullptr)
            {
                // (a dereference of an access-to-subprogram value (a subprogram): the value (its access-to-subprogram type's profile
                // type conformant))
                ada_type* valueType = nullptr;
                for (auto const& i : interpretations(prefix->child(0), aRegion))
                    if (i.type != nullptr && i.type->view()->kind == type_kind::AccessSubprogram && i.type->view()->profile != nullptr &&
                        type_conformant(*i.type->view()->profile, *aExpected->view()->profile))
                        valueType = i.type;
                if (valueType != nullptr)
                {
                    resolve(prefix->child(0), valueType, aRegion);
                    prefix->type = valueType;
                    set_meaning(prefix, meaning::Value);
                    incomplete_prefix_check(prefix, nullptr, false);
                    aAttribute->type = aExpected;
                    return aExpected;
                }
            }
            if (aExpected != nullptr && aExpected->view()->kind == type_kind::AccessSubprogram)
            {
                auto* holder = new_entity(aExpected->view()->profile->result != nullptr ? entity_kind::Function : entity_kind::Procedure, "%profile", nullptr, aRegion);
                holder->sub->parameters = aExpected->view()->profile->parameters;
                holder->sub->result = aExpected->view()->profile->result;
                auto* subprogram = resolve_subprogram_name(prefix, holder, aRegion);
                prefix->ent = subprogram;
                set_meaning(prefix, meaning::Subprogram);
                // (not a prefixed view, nor a renaming of one: intrinsic (6.3.1(10.1/2), 3.10.2(32/5)))
                if (iSpeculation == 0 && iInstanceBodies == 0)
                {
                    auto const prefixedView = [&](node* aName)
                    {
                        // (Object.Operation of a tagged object's primitive (not a protected object's subprogram, 4.1.3(9.2/3)))
                        if (aName == nullptr || !aName->is(construct::Selected) || aName->children.empty() || aName->child(0)->ent == nullptr ||
                            !aName->child(0)->ent->is_object() || meaning_of(aName->child(0)) != meaning::Object)
                            return false;
                        auto* ot = aName->child(0)->type != nullptr ? aName->child(0)->type : aName->child(0)->ent->type;
                        auto* v = ot != nullptr ? ot->view() : nullptr;
                        if (v != nullptr && v->kind == type_kind::Access && v->designated != nullptr)
                            v = v->designated->view();
                        return v != nullptr && v->kind != type_kind::Protected && v->kind != type_kind::Task && (v->tagged || v->kind == type_kind::ClassWide) &&
                            !v->synchronized_interface && !v->protected_interface && !v->task_interface;
                    };
                    if (prefixedView(prefix) || (subprogram != nullptr && subprogram->sub != nullptr && prefixedView(subprogram->sub->renamed_name)))
                        error(prefix, "the prefix of 'Access cannot be a prefixed view or a renaming of one (it is intrinsic, 3.10.2(32/5))");
                    // (nor of convention Intrinsic (a renaming of an intrinsic subprogram too, 6.3.1(4)))
                    for (auto* x = subprogram; x != nullptr; x = x->sub != nullptr && x->sub->renames != x ? x->sub->renames : nullptr)
                    {
                        if (x->convention == "intrinsic")
                        {
                            error(prefix, "the prefix of 'Access cannot be an intrinsic subprogram (3.10.2(32/5))");
                            break;
                        }
                        if (x->sub == nullptr)
                            break;
                    }
                    // (the designated profile: subtype conformant with the subprogram's; a protected subprogram for an access-to-protected-
                    // subprogram type, and only for one (3.10.2(32/3, 32.1/3)))
                    if (subprogram != nullptr && subprogram->sub != nullptr && subprogram->sub->op != intrinsic::Attribute &&
                        subprogram->kind != entity_kind::EnumerationLiteral && subprogram->kind != entity_kind::Entry)
                    {
                        auto* d = subprogram;
                        for (std::size_t n = 0u; d->sub != nullptr && d->sub->renames != nullptr && d->sub->renames != d && n < 16u; ++n)
                            d = d->sub->renames;
                        bool const protectedSubprogram = d->scope != nullptr && d->scope->owner != nullptr && (d->scope->owner->kind == entity_kind::Protected ||
                            (d->scope->owner->type != nullptr && d->scope->owner->type->view()->kind == type_kind::Protected));
                        if (protectedSubprogram != aExpected->view()->protected_access && d->kind != entity_kind::Entry)
                            error(prefix, protectedSubprogram ? "the prefix of 'Access of an access-to-subprogram type cannot be a protected subprogram (3.10.2(32/3))" :
                                "the prefix of 'Access of an access-to-protected-subprogram type must be a protected subprogram (3.10.2(32.1/3))");
                        else if ([&]()
                            {
                                // (a controlling access parameter: excludes null (3.10(13.1/2)))
                                auto profile = *subprogram->sub;
                                auto const& designated = aExpected->view()->profile->parameters;
                                for (std::size_t k = 0u; k < profile.parameters.size() && k < designated.size(); ++k)
                                {
                                    auto& p = profile.parameters[k];
                                    auto const& q = designated[k];
                                    bool const qTypeExcludes = q.type != nullptr && (q.type->not_null || q.type->view()->not_null);
                                    if (profile.controlling != nullptr && p.type != nullptr && p.type->anonymous_access && p.type->view()->kind == type_kind::Access &&
                                        p.type->view()->designated != nullptr && same_type(p.type->view()->designated, profile.controlling) &&
                                        !(p.not_null || p.type->not_null || p.type->view()->not_null) && (q.not_null || qTypeExcludes))
                                    {
                                        // (as the designated profile expresses it)
                                        p.not_null = q.not_null;
                                        if (qTypeExcludes)
                                        {
                                            auto* excluding = new_subtype(p.type);
                                            excluding->anonymous_access = true;
                                            excluding->not_null = true;
                                            p.type = excluding;
                                        }
                                    }
                                }
                                return !subtype_conformant(*aExpected->view()->profile, profile);
                            }())
                            error(prefix, "the subprogram must be subtype conformant with the designated profile of the access type (3.10.2(32/3))");
                    }
                }
                // (a named access-to-subprogram type: the subprogram not deeper than it (3.10.2(32/3)))
                // (a renaming's level: the renamed subprogram's (3.10.2(13.1/3)))
                auto* denoted = subprogram;
                for (std::size_t n = 0u; denoted != nullptr && denoted->sub != nullptr && denoted->sub->renames != nullptr && 
                    denoted->sub->renames != denoted && n < 16u; ++n)
                    denoted = denoted->sub->renames;
                // (also in an instance's private part (12.3(11)))
                if (name == "access" && denoted != nullptr && !aExpected->view()->anonymous_access && iSpeculation == 0 &&
                    (iInstanceBodies == 0 || iInstanceBodies == iInstancePrivateParts) && !within_generic_body(aRegion) && denoted->scope != nullptr &&
                    accessibility_level(denoted->scope) > type_level(aExpected))
                    error_in_private_part_too(prefix, "the subprogram is deeper than the access-to-subprogram type (3.10.2(32/3))");
                // (a subprogram declared within a generic unit, P'Access within its body: of a nonformal type declared within the generic
                // unit, or an access parameter's (3.10.2(32/3)))
                else if (name == "access" && denoted != nullptr && !aExpected->view()->anonymous_access && iSpeculation == 0 && iInstanceBodies == 0)
                {
                    // (a generic unit: or its instance for check_generic (its template))
                    auto const genericUnit = [&](entity* e) { return e != nullptr && (e->is_generic() || iTemplateInstances.contains(e)); };
                    // (outward through where each region's owner is declared: e.g. a formal package's instance, within its generic's formals)
                    entity* generic = nullptr;
                    int steps = 0;
                    for (auto* r = denoted->scope; r != nullptr && generic == nullptr && ++steps < 256;
                        r = r->owner != nullptr && r->owner->scope != nullptr && r->owner->scope != r ? r->owner->scope : r->parent)
                        if (genericUnit(r->owner))
                            generic = r->owner;
                    bool inBody = false;
                    for (region* jump = nullptr, *r = aRegion; generic != nullptr && r != nullptr && !inBody; r = outward(r, jump))
                        inBody = genericUnit(r->owner) && (r->package_body || r->is_subprogram) &&
                            (r->owner == generic || [&]() { for (region* j2 = nullptr, *q = r; q != nullptr; q = outward(q, j2))
                                if (q->owner == generic) return true; return false; }());
                    if (inBody)
                    {
                        auto* t = aExpected->view()->base_type()->view();
                        for (int n = 0; t->parent != nullptr && n < 64; ++n)
                            t = t->parent->view()->base_type()->view();
                        bool within = false;
                        for (region* jump = nullptr, *r = t->declared_in; r != nullptr && !within; r = outward(r, jump))
                            within = r->owner == generic;
                        if (!t->anonymous_access && (t->is_formal || !within))
                            error(prefix, "the access-to-subprogram type must be a nonformal type declared within the generic unit (3.10.2(32/3))");
                    }
                }
                // (a remote access-to-subprogram type's value: of a remote subprogram (E.2.2(12)); a remote subprogram's: of a remote
                // access-to-subprogram type (outside its unit))
                if (denoted != nullptr && iSpeculation == 0 && iInstanceBodies == 0)
                {
                    bool const remoteType = remote_access_type(aExpected);
                    bool const remote = remote_subprogram(denoted);
                    bool within = false;
                    for (auto* r = aRegion; r != nullptr && !within; r = r->parent)
                        within = denoted->scope != nullptr && r->owner != nullptr && r->owner == denoted->scope->owner;
                    if (remoteType && !remote)
                        error(prefix, "the prefix of an Access attribute of a remote access-to-subprogram type must denote a remote subprogram (E.2.2(12))");
                    if (!remoteType && remote && !within && !aExpected->view()->anonymous_access)
                        error(prefix, "the Access attribute of a remote subprogram must be of a remote access-to-subprogram type (E.2.2(12))");
                }
                incomplete_prefix_check(prefix, nullptr, false);    // (3.10.1(10/3))
                aAttribute->type = aExpected;
                return aExpected;
            }
            ada_type* expectedObject = aExpected != nullptr && aExpected->view()->kind == type_kind::Access ? designated_of(aExpected) : nullptr;
            if (expectedObject != nullptr && expectedObject->view()->kind == type_kind::ClassWide)
                expectedObject = nullptr;
            prefixType = resolve(prefix, expectedObject, aRegion);
            // (X'Access, X'Unchecked_Access: of an aliased view (3.10.2(24/1)))
            if (name != "unrestricted_access" && iSpeculation == 0 && iInstanceBodies == 0 && !aliased_view(prefix))
                error(prefix, "the prefix of '" + name + " must be an aliased view of an object");
            // (not a subcomponent depending on discriminants of an object not known to be constrained (3.10.2(26/3)))
            else if (name != "unrestricted_access" && iSpeculation == 0 && iInstanceBodies == 0 && discriminant_dependent(prefix))
                error(prefix, "the prefix of '" + name + " cannot be a subcomponent that depends on discriminants of an unconstrained object (3.10.2(26/3))");
            // (the view's type: covered by a tagged designated type D; else D's type, D statically matching the view's nominal subtype unless
            // D is discriminated and unconstrained (3.10.2(27/2)))
            if (name != "unrestricted_access" && iSpeculation == 0 && iInstanceBodies == 0 && aExpected != nullptr &&
                aExpected->view()->kind == type_kind::Access && aExpected->view()->designated != nullptr && prefixType != nullptr &&
                !aExpected->view()->is_formal && !prefixType->view()->is_formal && !designated_of(aExpected)->view()->is_formal)
            {
                auto* d = designated_of(aExpected);
                auto* dv = d->view();
                auto* v = prefixType->view();
                if (dv->kind == type_kind::ClassWide)
                {
                    if (!covers(d, prefixType))
                        error(prefix, "the prefix's type must be covered by the designated type (3.10.2(27/2))");
                }
                else if (!dv->tagged && !dv->base_type()->view()->tagged && (dv->kind == type_kind::Array || dv->kind == type_kind::Record) &&
                    same_type(dv, v))
                {
                    // (the nominal subtype: an object's declared subtype (not constrained by its initial value))
                    ada_type* nominal = prefixType;
                    if (auto* e = prefix->ent; e != nullptr && prefix->is(construct::Identifier) && meaning_of(prefix) == meaning::Object &&
                        (e->kind == entity_kind::Variable || e->kind == entity_kind::Constant) && e->renamed_name == nullptr && e->type != nullptr)
                    {
                        nominal = e->type;
                        if (e->decl != nullptr && e->decl->parent != nullptr && e->decl->parent->is(construct::Object))
                            for (auto* c : e->decl->parent->children)
                            {
                                if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects))
                                    continue;
                                if (c->is(construct::Identifier) || c->is(construct::Selected) || (c->is(construct::SubtypeIndication) &&
                                    c->size() == 1u && (c->child(0)->is(construct::Identifier) || c->child(0)->is(construct::Selected))))
                                    if (auto* mark = resolve_entity(c->is(construct::SubtypeIndication) ? c->child(0) : c, e->scope);
                                        mark != nullptr && (mark->kind == entity_kind::Type || mark->kind == entity_kind::Subtype) && mark->type != nullptr)
                                        nominal = mark->type;
                                break;
                            }
                    }
                    auto* full = dv->base_type()->view();
                    bool const discriminatedUnconstrained = dv->kind == type_kind::Record && full->known_discriminants && !dv->constrained &&
                        dv->discriminant_constraint.empty();
                    if (!discriminatedUnconstrained && !statically_matching(d, nominal))
                        error(prefix, "the designated subtype must statically match the prefix's nominal subtype (3.10.2(27/2))");
                }
            }
            // (an atomic (volatile) object's: of an access type whose designated type is atomic (volatile) (C.6(13/3)))
            if (name != "unrestricted_access" && iSpeculation == 0 && iInstanceBodies == 0 && aExpected != nullptr &&
                aExpected->view()->kind == type_kind::Access && aExpected->view()->designated != nullptr)
            {
                auto const [atomic, isVolatile] = atomic_view(prefix);
                auto* d = designated_of(aExpected);
                if (atomic && type_aspect(d, "atomic") == nullptr)
                    error(prefix, "the prefix of '" + name + " is atomic: the designated type must be atomic (C.6(13/3))");
                else if (isVolatile && type_aspect(d, "atomic") == nullptr && type_aspect(d, "volatile") == nullptr)
                    error(prefix, "the prefix of '" + name + " is volatile: the designated type must be volatile (C.6(13/3))");
            }
            // (of an access-to-variable type: a variable view (3.10.2(25)))
            if (name != "unrestricted_access" && iSpeculation == 0 && iInstanceBodies == 0 && aExpected != nullptr &&
                aExpected->view()->kind == type_kind::Access && !aExpected->view()->access_constant && !is_variable(prefix, 0))
                error(prefix, "the prefix of '" + name + " of an access-to-variable type must be a variable view (3.10.2(25))");
            // (X'Access of a named access type (or a component's anonymous access type, of the composite type's level): X not statically
            // deeper than it (3.10.2(28/3)); in a generic body: checked when run)
            // (also in an instance's private part (12.3(11)))
            if (name == "access" && aExpected != nullptr && aExpected->view()->kind == type_kind::Access &&
                (!aExpected->view()->anonymous_access || aExpected->view()->component_access) &&
                iSpeculation == 0 && (iInstanceBodies == 0 || iInstanceBodies == iInstancePrivateParts) && !within_generic_body(aRegion))
                if (auto const level = static_level(prefix, aRegion); level && *level > type_level(aExpected))
                    error_in_private_part_too(prefix, "the prefix of 'Access is statically deeper than the access type (3.10.2(28/3))");
            if (prefix->ent != nullptr && prefix->ent->is_object())
            {
                prefix->ent->aliased = true;
                prefix->ent->captured = prefix->ent->captured || prefix->ent->scope != nullptr && prefix->ent->scope->level > 0;
            }
            auto* result = aExpected;
            if (result == nullptr)
            {
                result = new_type(type_kind::Access, {});
                result->anonymous_access = true;
                result->designated = prefixType;
                result->size = 8u;
            }
            incomplete_prefix_check(prefix, nullptr, false);
            aAttribute->type = result;
            return result;
        }
        else if (name == "address" || name == "size" || name == "valid" || name == "constrained" || name == "first" || name == "last" ||
            name == "length" || name == "range" || name == "tag" || name == "callable" || name == "terminated" || name == "identity" ||
            name == "component_size" || name == "alignment" || name == "has_same_storage" || name == "overlaps_storage" || name == "old" ||
            name == "loop_entry" || name == "image" || name == "wide_image" || name == "wide_wide_image" || name == "count" || name == "position" ||
            name == "first_bit" || name == "last_bit" || name == "storage_size" || name == "external_tag" || name == "object_size" ||
            name == "enum_rep" || name == "img" || name == "valid_scalars")
        {
            // (X'Address: of an object, a program unit, a label or an entry (13.3(11)); X'Size: of an object or a subtype (13.3(40), (44)))
            if ((name == "address" || name == "size") && iSpeculation == 0 && iInstanceBodies == 0)
            {
                if (prefix->is(construct::Attribute) && prefix->size() >= 2u && arguments_of(prefix).empty())
                    if (auto const a = name_text(prefix->child(1)->source); a != "base" && a != "class" && a != "result" && a != "old" && a != "loop_entry")
                        error(aAttribute, "the prefix of '" + name + " cannot be an attribute's value");
                if (prefix->is(construct::Identifier) || prefix->is(construct::Selected))
                    if (auto const entities = prefix_entities(prefix, aRegion); !entities.empty())
                    {
                        auto const k = entities.front()->kind;
                        bool const unit = entities.front()->is_subprogram() || entities.front()->is_package() || k == entity_kind::GenericPackage ||
                            k == entity_kind::GenericProcedure || k == entity_kind::GenericFunction || k == entity_kind::Label;
                        if (k == entity_kind::NamedNumber || k == entity_kind::Exception || (name == "address" && (k == entity_kind::Type || k == entity_kind::Subtype)) ||
                            (name == "size" && unit && entities.size() == 1u && k != entity_kind::Function))
                            error(aAttribute, "the prefix of '" + name + " cannot denote " + (k == entity_kind::NamedNumber ? "a named number" :
                                k == entity_kind::Exception ? "an exception" : k == entity_kind::Type || k == entity_kind::Subtype ? "a subtype" : "a program unit, label or entry"));
                    }
            }
            if (prefix->is(construct::Identifier) || prefix->is(construct::Selected))
            {
                // (a task or protected unit's name, or an entry)
                auto const entities = prefix_entities(prefix, aRegion);
                if (name == "identity" && entities.size() == 1u && entities.front()->kind == entity_kind::Exception)
                {
                    // (an exception's identity: Ada.Exceptions.Exception_Id)
                    prefix->ent = entities.front();
                    set_meaning(prefix, meaning::Exception);
                    aAttribute->type = library_type("ada.exceptions", "exception_id");
                    return aAttribute->type;
                }
                // (a subprogram's 'Address; else a function's name here is a call (e.g. F'First: of its result))
                if (entities.size() >= 1u && (entities.front()->kind == entity_kind::Entry || (entities.front()->is_subprogram() && name == "address") ||
                    entities.front()->kind == entity_kind::Task || entities.front()->kind == entity_kind::Protected || entities.front()->is_package()))
                {
                    prefix->ent = entities.front();
                    set_meaning(prefix, entities.front()->kind == entity_kind::Entry ? meaning::Entry : meaning::Subprogram);
                    prefixType = entities.front()->type;
                }
                else
                    prefixType = resolve(prefix, aExpected != nullptr && (name == "old" || name == "loop_entry") ? aExpected : nullptr, aRegion);
            }
            else if (prefix->is(construct::Apply) && name == "count")
            {
                // (an entry family member's 'Count)
                prefixType = resolve(prefix, nullptr, aRegion);
                if (prefix->type == nullptr)
                    prefix->type = prefixType;
            }
            else
                prefixType = resolve(prefix, aExpected != nullptr && (name == "old" || name == "loop_entry") ? aExpected : nullptr, aRegion);
            // (E'Count: within the body of E's task or protected unit; a task's: not within a program unit inner to it (9.9(5)))
            if (name == "count" && iSpeculation == 0 && iInstanceBodies == 0)
            {
                // (of an entry)
                if (prefix->ent != nullptr && prefix->ent->kind != entity_kind::Entry && !prefix->is(construct::Apply))
                    error(prefix, "the prefix of 'Count must denote an entry");
                auto* entry = prefix->ent != nullptr && prefix->ent->kind == entity_kind::Entry ? prefix->ent :
                    prefix->is(construct::Apply) && !prefix->children.empty() && prefix->child(0)->ent != nullptr &&
                    prefix->child(0)->ent->kind == entity_kind::Entry ? prefix->child(0)->ent : nullptr;
                auto* unit = entry != nullptr && entry->scope != nullptr ? entry->scope->owner : nullptr;
                if (unit != nullptr)
                {
                    bool const task = unit->kind == entity_kind::Task || (unit->type != nullptr && unit->type->view()->kind == type_kind::Task);
                    bool within = false;
                    for (region* jump = nullptr, *r = aRegion; r != nullptr && !within; r = outward(r, jump))
                    {
                        auto* o = r->owner != nullptr && r->owner->owner != nullptr ? r->owner->owner : r->owner;
                        if (o == unit || (o != nullptr && unit->type != nullptr && o->type == unit->type && o->kind == unit->kind))
                            within = true;
                        else if (task && (r->is_subprogram || r->is_package || (r->owner != nullptr && (r->owner->kind == entity_kind::Task ||
                            r->owner->kind == entity_kind::Protected))))
                            break;
                    }
                    // (of the current instance: not an entry of another object of the type)
                    auto* name = prefix->is(construct::Apply) ? prefix->child(0) : prefix;
                    if (within && name->is(construct::Selected) && !name->children.empty() && name->child(0)->ent != nullptr &&
                        ((name->child(0)->ent->is_object() && name->child(0)->ent != unit) || name->child(0)->ent->kind == entity_kind::Subtype))
                        within = false;
                    // (a family: one of its members)
                    if (entry->sub != nullptr && entry->sub->family_type != nullptr && !prefix->is(construct::Apply))
                        recover([&]() { error(aAttribute, "the prefix of 'Count must denote a member of the entry family (an index is required)"); });
                    if (!within)
                        recover([&]() { error(aAttribute, "'Count can be used only within the body of the entry's task or protected unit"); });
                }
            }
            // (X'Old: only in a postcondition; its prefix: no 'Result or 'Old (6.1.1(24/3)-(26/4)))
            if (name == "old" && iSpeculation == 0 && iInstanceBodies == 0)
            {
                if (iPostconditionOf == nullptr)
                    recover([&]() { error(aAttribute, "'Old can be used only in a postcondition"); });
                std::function<bool(node*)> contains = [&](node* n)
                {
                    if (n->is(construct::Attribute) && n->size() >= 2u)
                        if (auto const a = name_text(n->child(1)->source); a == "result" || a == "old")
                            return true;
                    for (auto* c : n->children)
                        if (contains(c))
                            return true;
                    return false;
                };
                if (contains(prefix))
                    recover([&]() { error(aAttribute, "the prefix of 'Old cannot contain 'Result or 'Old"); });
                // (nor a name of a declaration within the postcondition (a quantified expression's parameter, a declare expression's
                // object), 6.1.1(24/3))
                std::function<bool(node*)> local = [&](node* n)
                {
                    if (n->ent != nullptr && n->ent->kind == entity_kind::LoopParameter)
                        return true;
                    if (n->ent != nullptr && n->ent->is_object() && n->ent->decl != nullptr)
                        for (auto const* d = n->ent->decl; d != nullptr; d = d->parent)
                            if (d->is(construct::DeclareExpression))
                                return true;
                    for (auto* c : n->children)
                        if (local(c))
                            return true;
                    return false;
                };
                if (local(prefix))
                    recover([&]() { error(aAttribute, "the prefix of 'Old cannot denote a declaration within the postcondition (6.1.1(24/3))"); });
                if (prefixType != nullptr && limited_type(prefixType, aRegion))
                    recover([&]() { error(aAttribute, "the prefix of 'Old cannot be of a limited type"); });
                // (in a potentially unevaluated expression: its prefix statically names an object (6.1.1(27/5)))
                bool unevaluated = false;
                for (node* c = aAttribute, *p = aAttribute->parent; p != nullptr && !unevaluated; c = p, p = p->parent)
                {
                    if (p->is(construct::IfExpression) || p->is(construct::CaseExpression))
                        unevaluated = c != p->child(0);
                    else if ((p->is(construct::AndThen) || p->is(construct::OrElse)) && p->size() == 2u)
                        unevaluated = c == p->child(1);
                    else if (p->is(construct::Quantified))
                        unevaluated = c == p->children.back();
                    else if (p->is(construct::In) && c != p->child(0))
                    {
                        // (a membership choice other than the first)
                        node* first = nullptr;
                        for (std::size_t i = 1u; i < p->size() && first == nullptr; ++i)
                            if (!p->child(i)->is(construct::Flag))
                                first = p->child(i);
                        unevaluated = c != first;
                    }
                    else if (p->is(construct::Aspect) || p->is(construct::Aspects) || p->is(construct::PragmaArgument))
                        break;
                }
                bool const statically = prefix->is(construct::Identifier) ||
                    (prefix->is(construct::Selected) && prefix->child(0)->ent != nullptr && prefix->child(0)->ent->is_package());
                if (unevaluated && !statically)
                    recover([&]() { error(aAttribute, "the prefix of 'Old in a potentially unevaluated expression must statically name an object"); });
            }
            if (name == "address" && prefix->ent != nullptr && prefix->ent->is_object())
                prefix->ent->aliased = true;    // ('Address: the object is in memory)
            if (prefixType != nullptr && prefixType->view()->kind == type_kind::Access && (name == "first" || name == "last" || name == "length" || name == "range"))
            {
                prefix->mark |= 0x2000u;    // (implicitly dereferenced)
                prefixType = designated_of(prefixType);
            }
        }
        else if (name == "result")
        {
            // (the function whose postcondition it is (its prefix names it), else the named one)
            prefixType = iCurrentSubprogram != nullptr && iCurrentSubprogram->sub != nullptr ? iCurrentSubprogram->sub->result : nullptr;
            if (iCurrentSubprogram == nullptr || iCurrentSubprogram->name != name_text(prefix->source))
                if (auto* e = resolve_entity(prefix, aRegion); e != nullptr && e->sub != nullptr && e->sub->result != nullptr)
                    prefixType = e->sub->result;
            // (only in a postcondition of the function (6.1.1(29/3)))
            if (iSpeculation == 0 && iInstanceBodies == 0)
            {
                std::string prefixName{ name_text(prefix->source) };
                if (auto const dot = prefixName.rfind('.'); dot != std::string::npos)
                    prefixName = prefixName.substr(dot + 1);
                if (prefixName.size() >= 2u && prefixName.front() == '"' && prefixName.back() == '"')
                    prefixName = prefixName.substr(1, prefixName.size() - 2u);
                if (iPostconditionOf == nullptr || prefixName != iPostconditionOf->name || iPostconditionOf->kind != entity_kind::Function)
                    error(aAttribute, "'Result can be used only in a postcondition of the function");
            }
        }
        else if (name == "reduce")
        {
            // (Prefix'Reduce (Reducer, Initial): of the expected type)
            resolve(prefix, nullptr, aRegion);
            if (arguments.size() == 2u)
                resolve(arguments[1], aExpected, aRegion);
            if (arguments.size() >= 1u && arguments[0]->is(construct::Reducer))
                if (arguments[0]->size() == 1u && !arguments[0]->child(0)->is(construct::Operator))
                    resolve_entity(arguments[0]->child(0), aRegion);
            aAttribute->type = aExpected;
            return aExpected;
        }
        else
            prefixType = resolve(prefix, nullptr, aRegion);
            if (prefix->type == nullptr)
                prefix->type = prefixType;
        auto* t = prefixType != nullptr ? prefixType->view() : nullptr;
        // (a partial view whose full view is not visible here: not a scalar type (7.3(4)))
        if (prefixType != nullptr && typePrefix && iSpeculation == 0 &&
            (name == "pos" || name == "val" || name == "succ" || name == "pred" || name == "digits" || name == "delta" || name == "small" ||
             name == "modulus" || name == "aft" || name == "fore" || name == "scale" || name == "min" || name == "max" || name == "value" ||
             name == "width" || name == "wide_value" || name == "wide_width" || name == "first" || name == "last" || name == "range" ||
             name == "base" || name == "wide_wide_value" || name == "wide_wide_width" || name == "model_mantissa" || name == "model_epsilon" ||
             name == "model_emin" || name == "model_small" || name == "machine_rounds" || name == "machine_overflows" || name == "machine_radix" ||
             name == "machine_mantissa" || name == "machine_emax" || name == "machine_emin" || name == "denorm" || name == "signed_zeros" ||
             name == "safe_first" || name == "safe_last" || name == "model" || name == "machine" || name == "rounding" || name == "truncation" ||
             name == "floor" || name == "ceiling" || name == "unbiased_rounding" || name == "machine_rounding" || name == "exponent" ||
             name == "fraction" || name == "compose" || name == "scaling" || name == "leading_part" || name == "remainder" || name == "adjacent" ||
             name == "copy_sign" || name == "round" || name == "enum_rep" || name == "enum_val" || name == "mantissa" || name == "emax" ||
             name == "epsilon" || name == "large" || name == "safe_emax" || name == "safe_large" || name == "safe_small"))
        {
            if (auto* partial = prefixType->kind == type_kind::Private ? prefixType : prefixType->base != nullptr && prefixType->base->kind == type_kind::Private ? prefixType->base : nullptr;
                partial != nullptr && !partial->is_formal)
            {
                if (auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                    package != nullptr && !private_part_visible(package, aRegion) && formal_actual_instance(partial, aRegion) == nullptr)
                    error(prefix, "the prefix of '" + name + " must be a scalar subtype (the full view of the private type is not visible)");
            }
            // (a type derived from a partial view)
            else if (iInstanceBodies == 0 && prefixType->kind != type_kind::Private && partial_view_here(prefixType, aRegion))
                error(prefix, "the prefix of '" + name + " must be a scalar subtype (the full view of the private type is not visible)");
        }
        // (an object of a partial view (its full view not visible here): not an array (7.3(4)))
        if (prefixType != nullptr && !typePrefix && iSpeculation == 0 && iInstanceBodies == 0 &&
            (name == "first" || name == "last" || name == "length" || name == "range") &&
            [&]()
            {
                // (as the prefix is seen: through an access value, its designated type's view)
                auto* seen = prefix->type != nullptr ? prefix->type : prefixType;
                if (seen->view()->kind == type_kind::Access && seen->view()->designated != nullptr)
                    seen = seen->view()->designated;
                return partial_view_here(seen, aRegion);
            }())
            error(prefix, "the prefix of '" + name + " must be an array (the full view of the private type is not visible)");
        if (prefixType != nullptr && !typePrefix && iSpeculation == 0 && iInstanceBodies == 0 && (name == "callable" || name == "terminated") &&
            !prefixType->view()->is_formal && partial_view_here(prefixType, aRegion))
            error(prefix, "the prefix of '" + name + " must be a task (the full view of the private type is not visible)");
        // (the prefix's kind for the attribute (K.2))
        if (t != nullptr && !t->is_formal && !t->base_type()->view()->is_formal && t->kind != type_kind::Private)
        {
            auto* b = t->base_type()->view();
            auto const require = [&](bool aOk, char const* aWhat)
            {
                if (!aOk)
                    error(prefix, "the prefix of '" + name + " must be " + aWhat);
            };
            if (name == "aft" || name == "fore" || name == "small" || name == "delta")
                require(b->kind == type_kind::Fixed || b->kind == type_kind::Decimal, "a fixed point subtype");
            else if (name == "scale")
                require(b->kind == type_kind::Decimal, "a decimal fixed point subtype");
            else if (name == "digits")
                require(b->kind == type_kind::Float || b->kind == type_kind::Decimal, "a floating point or decimal fixed point subtype");
            else if (name == "modulus")
                require(b->kind == type_kind::Modular, "a modular subtype");
            else if (name == "pos" || name == "val")
                require(typePrefix && b->is_discrete(), "a discrete subtype");
            else if (name == "succ" || name == "pred")
                require(typePrefix && b->is_scalar(), "a scalar subtype");
            else if (name == "callable" || name == "terminated")
            {
                require(b->kind == type_kind::Task || b->kind == type_kind::ClassWide || b->kind == type_kind::Interface ||
                    (prefixType->view()->kind == type_kind::Access && designated_of(prefixType) != nullptr), "a task");
                // (a task object (9.9(1-2)); a task type's name: only as its current instance, within its body)
                if (typePrefix && iInstanceBodies == 0 && iSpeculation == 0)
                {
                    auto* e = resolve_entity(prefix, aRegion);
                    bool current = false;
                    for (region* jump = nullptr, *r = aRegion; r != nullptr && e != nullptr && !current; r = outward(r, jump))
                    {
                        // (a task body's region: of its body procedure, whose owner is the task unit)
                        auto* o = r->owner != nullptr && r->owner->owner != nullptr ? r->owner->owner : r->owner;
                        current = o == e || (o != nullptr && e->kind == entity_kind::Type && e->type != nullptr && o->type == e->type);
                    }
                    if (!current)
                        error(prefix, "the prefix of '" + name + " must denote a task object");
                }
            }
            else if (name == "constrained")
            {
                // (an object of a discriminated type (3.7.2(3/3)); a private subtype (J.4))
                bool discriminated = false;
                for (auto const& c : b->components)
                    discriminated = discriminated || (c.discriminant && !c.stored);
                // (a private subtype (or one derived from a partial view): any (J.4))
                auto const privateView = [](ada_type const* x)
                {
                    return x != nullptr && (x->kind == type_kind::Private || (x->base != nullptr && x->base->kind == type_kind::Private));
                };
                bool const privateSubtype = privateView(prefixType) || privateView(prefixType->base_type()->parent) || privateView(b->parent);
                require(typePrefix ? (privateSubtype || (!b->is_scalar() && b->kind != type_kind::Array && b->kind != type_kind::Access)) :
                    (discriminated || b->kind == type_kind::Private || b->kind == type_kind::ClassWide), "an object of a discriminated type");
            }
            else if (name == "first" || name == "last" || name == "length" || name == "range")
            {
                // (an array (object or subtype), or a scalar subtype (3.5(10), 3.6.2(3)); a dimension for an array only)
                bool const array = b->kind == type_kind::Array || (b->kind == type_kind::Access && designated_of(prefixType) != nullptr &&
                    designated_of(prefixType)->view()->kind == type_kind::Array);
                if (!arguments.empty())
                    require(array, "an array");
                else if (typePrefix)
                {
                    require(array || b->is_scalar(), "a scalar or array subtype");
                    // (not of a scalar subtype with a predicate (3.2.4(29.2/4)))
                    if (b->is_scalar() && name != "length")
                        predicate_check(prefixType, prefix);
                }
                else
                    require(array, "an array");
            }
            else if (name == "first_bit" || name == "last_bit" || name == "position")
                require(!typePrefix && prefix->is(construct::Selected) && prefix->size() >= 2u && prefix->child(1)->ent != nullptr &&
                    (prefix->child(1)->ent->kind == entity_kind::Component || prefix->child(1)->ent->kind == entity_kind::Discriminant), "a component");
            else if (name == "machine_emax" || name == "machine_emin" || name == "machine_mantissa" || name == "model_emin" || name == "model_mantissa" ||
                name == "safe_first" || name == "safe_last" || name == "model_epsilon" || name == "denorm" || name == "signed_zeros")
                require(b->kind == type_kind::Float, "a floating point subtype");
            else if (name == "machine_radix" || name == "machine_rounds" || name == "machine_overflows")
                require(b->is_real(), "a real subtype");
            else if (name == "emax" || name == "epsilon" || name == "large" || name == "mantissa" || name == "safe_emax" || name == "safe_large" ||
                name == "safe_small")
                require(b->is_real(), "a real subtype");
            else if (name == "value" || name == "wide_value" || name == "wide_wide_value" || name == "width" || name == "wide_width" ||
                name == "wide_wide_width")
                require(typePrefix && b->is_scalar(), "a scalar subtype");
            else if (name == "storage_size")
                require(b->kind == type_kind::Access || b->kind == type_kind::Task, "an access or task subtype or a task object");
        }
        ada_type* result = nullptr;
        // (the arguments)
        if (name == "first" || name == "last" || name == "length" || name == "range")
        {
            if (!arguments.empty())
                resolve(arguments[0], nullptr, aRegion);
            if (t != nullptr && t->kind == type_kind::Array)
            {
                std::size_t dimension = 0u;
                if (!arguments.empty())
                {
                    auto v = evaluate(arguments[0], aRegion);
                    if (v.is_integer())
                        dimension = static_cast<std::size_t>(v.integer().to_int64() - 1);
                }
                if (dimension >= t->indexes.size())
                    error(aAttribute, "dimension out of range");
                result = name == "length" ? iUniversalInteger : t->indexes[dimension]->base_type();
                if (name == "range")
                    result = range_of(aAttribute, aRegion);
            }
            else if (t != nullptr)
                result = name == "length" ? iUniversalInteger : t->base_type();
        }
        else if (name == "pos" || name == "enum_rep")
        {
            if (!arguments.empty())
                resolve(arguments[0], prefixType->base_type(), aRegion);
            result = iUniversalInteger;
        }
        else if (name == "has_same_storage" || name == "overlaps_storage")
        {
            // (another object (of any type))
            if (!arguments.empty())
            {
                ada_type* at = nullptr;
                for (auto const& o : interpretations(arguments[0], aRegion))
                    if (o.type != nullptr)
                    {
                        at = o.type;
                        break;
                    }
                resolve(arguments[0], at, aRegion);
            }
            result = iBoolean;
        }
        else if (name == "val" || name == "enum_val")
        {
            if (!arguments.empty())
            {
                // (of any integer type: the universal interpretation preferred (root_integer's, 8.6(29)))
                auto const options = interpretations(arguments[0], aRegion);
                ada_type* at = nullptr;
                for (auto const& o : options)
                    if (o.type != nullptr && o.type->is_integer() && (at == nullptr || o.type->is_universal()))
                        at = o.type;
                // (of an integer type (3.5.5(5)))
                if (at == nullptr && name == "val" && iSpeculation == 0 && !options.empty() &&
                    std::none_of(options.begin(), options.end(), [](auto const& o) { return o.type == nullptr || o.type->view()->is_integer() || o.type->view()->is_formal; }))
                    error(arguments[0], "the argument of 'Val must be of an integer type");
                resolve(arguments[0], at != nullptr && !at->is_universal() ? at : nullptr, aRegion);
            }
            result = prefixType->base_type();
        }
        else if (name == "succ" || name == "pred" || name == "floor" || name == "ceiling" || name == "rounding" || name == "truncation" ||
            name == "unbiased_rounding" || name == "machine_rounding" || name == "machine" || name == "model" || name == "fraction" ||
            name == "exponent")
        {
            if (!arguments.empty())
                resolve(arguments[0], prefixType->base_type(), aRegion);
            result = name == "exponent" ? iUniversalInteger : prefixType->base_type();
        }
        else if (name == "min" || name == "max" || name == "remainder" || name == "adjacent" || name == "copy_sign")
        {
            for (auto* a : arguments)
                resolve(a, prefixType->base_type(), aRegion);
            result = prefixType->base_type();
        }
        else if (name == "scaling" || name == "leading_part" || name == "compose")
        {
            if (arguments.size() >= 1u)
                resolve(arguments[0], prefixType->base_type(), aRegion);
            if (arguments.size() >= 2u)
                resolve(arguments[1], name == "compose" ? iUniversalInteger : iUniversalInteger, aRegion);
            result = prefixType->base_type();
        }
        else if (name == "round")
        {
            if (!arguments.empty())
            {
                auto const options = interpretations(arguments[0], aRegion);
                ada_type* at = nullptr;
                for (auto const& o : options)
                    if (o.type != nullptr && o.type->is_real() && !o.type->is_universal())
                        at = o.type;
                resolve(arguments[0], at, aRegion);
            }
            result = prefixType->base_type();
        }
        else if (name == "mod")
        {
            // (S'Mod: of a modular subtype; its argument of any integer type (3.5.4(16.1/2)))
            if (iSpeculation == 0 && iInstanceBodies == 0 && (!typePrefix || prefixType->view()->base_type()->view()->kind != type_kind::Modular))
                error(aAttribute, "the prefix of 'Mod must be a modular subtype");
            if (!arguments.empty())
            {
                std::vector<ada_type*> integers;
                bool universal = false;
                for (auto const& i : interpretations(arguments[0], aRegion))
                    if (i.type != nullptr && i.type->view()->is_integer())
                    {
                        if (i.type->is_universal() || i.type == iIntegerLiteral)
                            universal = true;
                        else if (std::find(integers.begin(), integers.end(), i.type->base_type()) == integers.end())
                            integers.push_back(i.type->base_type());
                    }
                if (iSpeculation == 0 && iInstanceBodies == 0 && integers.empty() && !universal)
                    error(arguments[0], "the argument of 'Mod must be of an integer type");
                if (iSpeculation == 0 && iInstanceBodies == 0 && integers.size() > 1u)
                    error(arguments[0], "the argument of 'Mod is ambiguous");
                resolve(arguments[0], integers.size() == 1u ? integers.front() : nullptr, aRegion);
            }
            result = prefixType->base_type();
        }
        else if (name == "image" || name == "wide_image" || name == "wide_wide_image" || name == "img")
        {
            if (!arguments.empty())
                resolve(arguments[0], prefixType != nullptr ? prefixType->base_type() : nullptr, aRegion);
            result = name == "wide_image" ? string_type_named("wide_string") : name == "wide_wide_image" ? string_type_named("wide_wide_string") : iString;
        }
        else if (name == "value" || name == "wide_value" || name == "wide_wide_value")
        {
            if (!arguments.empty())
                resolve(arguments[0], name == "wide_value" ? string_type_named("wide_string") : name == "wide_wide_value" ? string_type_named("wide_wide_string") : iString, aRegion);
            result = prefixType->base_type();
        }
        else if (name == "width" || name == "wide_width" || name == "wide_wide_width")
            result = iUniversalInteger;
        else if (name == "input" || name == "read" || name == "write" || name == "output")
        {
            // (a stream-oriented attribute of a subtype (13.13.2): its first parameter an access to Root_Stream_Type'Class)
            if (!typePrefix && iSpeculation == 0 && iInstanceBodies == 0)
                error(prefix, "the prefix of '" + name + " must be a subtype");
            // (available (13.13.2(49/2)); T'Input: not of an abstract type)
            if (typePrefix && prefixType != nullptr && iSpeculation == 0 && iInstanceBodies == 0)
            {
                if (!stream_attribute_available(prefixType, name, aRegion))
                    error(aAttribute, "the stream attribute '" + name + " is not available for the limited type here (13.13.2(49/2))");
                if (name == "input" && prefixType->base_type()->kind != type_kind::ClassWide && prefixType->view()->base_type()->view()->abstract)
                    error(aAttribute, "the prefix of 'Input cannot be an abstract type (13.13.2(49/2))");
            }
            if (arguments.size() >= 1u)
            {
                ada_type* stream = nullptr;
                if (auto* root = library_type("ada.streams", "root_stream_type"))
                {
                    stream = new_type(type_kind::Access, {});
                    stream->anonymous_access = true;
                    stream->designated = class_wide(root);
                    stream->size = 8u;
                }
                resolve(arguments[0], stream, aRegion);
            }
            if (name != "input" && arguments.size() >= 2u)
                resolve(arguments[1], prefixType, aRegion);
            result = name == "input" ? prefixType : iVoid;
        }
        else if (name == "put_image")
        {
            if (arguments.size() >= 1u)
                resolve(arguments[0], nullptr, aRegion);
            if (arguments.size() >= 2u)
                resolve(arguments[1], prefixType, aRegion);
            result = iVoid;
        }
        else if (name == "storage_pool")
        {
            // (not of a remote access-to-class-wide type (E.2.2(17/4)))
            if (iSpeculation == 0 && iInstanceBodies == 0 && prefixType != nullptr && prefixType->view()->kind == type_kind::Access &&
                remote_access_type(prefixType))
                error(aAttribute, "Storage_Pool is not defined for a remote access-to-class-wide type (E.2.2(17/4))");
            // (an access type's storage pool: an object of Root_Storage_Pool'Class (13.11(13)))
            if (auto* root = library_type("system.storage_pools", "root_storage_pool"))
                result = class_wide(root);
        }
        else if (name == "class")
            result = class_wide(prefixType);
        else if (name == "base")
            result = prefixType->base_type();
        else if (name == "old" || name == "loop_entry" || name == "result")
            result = prefixType;
        else
            result = attribute_type(aAttribute, aRegion);
        // (an explicit dereference of an access-to-incomplete type, or an object of an incomplete view: not a prefix (3.10.1(10/3)))
        incomplete_prefix_check(prefix, nullptr, false);
        // (Storage_Size: not of a remote access-to-class-wide type (Ada 95 E.2.2(17); the tests))
        if (name == "storage_size" && iSpeculation == 0 && iInstanceBodies == 0 && prefixType != nullptr &&
            prefixType->view()->kind == type_kind::Access && remote_access_type(prefixType))
            error(aAttribute, "Storage_Size is not defined for a remote access-to-class-wide type (E.2.2(17))");
        if (result == nullptr)
            error(aAttribute, "unsupported attribute '" + name);
        // (a universal result used as the expected type)
        if (result->is_universal() && aExpected != nullptr && !aExpected->is_universal() && covers(aExpected, result))
            result = aExpected->base_type();
        // (of the expected type)
        if (aExpected != nullptr && aExpected != iVoid && result != iVoid && iSpeculation == 0 && iInstanceBodies == 0 &&
            !aExpected->view()->is_formal && !result->view()->is_formal && !aExpected->base_type()->view()->is_formal &&
            !result->base_type()->view()->is_formal && aExpected->view()->kind != type_kind::Private && result->view()->kind != type_kind::Private &&
            !covers(aExpected, result) && !covers(result, aExpected))
            error(aAttribute, "the attribute is not of the expected type");
        aAttribute->type = result;
        return result;
    }

    static_value program::evaluate_attribute(node* aAttribute, region* aRegion)
    {
        auto const name = name_text(aAttribute->child(1)->source);
        if (name == "maximum_alignment" || name == "system_allocator_alignment")
            return bigint{ 16 };
        if (name == "position" || name == "first_bit" || name == "last_bit")
        {
            // (a record component's (R.C): its offset in storage units, its first and last bits there)
            auto* p = aAttribute->child(0);
            if (!p->is(construct::Selected) || p->child(1)->ent == nullptr || p->child(0)->type == nullptr)
                return {};
            auto* rt = p->child(0)->type->view();
            if (rt->is_access())
                rt = designated_of(rt) != nullptr ? designated_of(rt)->view() : rt;
            auto* rb = rt->base_type()->view();
            layout(rb);
            for (auto const& c : rb->components)
                if (c.ent == p->child(1)->ent && c.offset)
                {
                    // (placed by a component clause: its bits)
                    if (c.bit_position && c.bit_size && *c.bit_position / 8u == *c.offset)
                    {
                        if (name == "position")
                            return bigint{ static_cast<std::int64_t>(*c.offset) };
                        auto const first = static_cast<std::int64_t>(*c.bit_position % 8u);
                        return bigint{ name == "first_bit" ? first : first + static_cast<std::int64_t>(*c.bit_size) - 1 };
                    }
                    if (name == "position")
                        return bigint{ static_cast<std::int64_t>(*c.offset) };
                    if (name == "first_bit")
                        return bigint{ 0 };
                    auto* ct = c.type->view();
                    auto const bytes = ct->is_scalar() || ct->is_access() ? scalar_size(ct) : object_size(ct);
                    return bigint{ static_cast<std::int64_t>(bytes * 8u) - 1 };
                }
            return {};
        }
        if (name == "storage_unit")
            return bigint{ 8 };
        if (name == "word_size" || name == "address_size" || name == "max_integer_size")
            return bigint{ 64 };
        auto* prefix = aAttribute->child(0);
        auto const arguments = arguments_of(aAttribute);
        bool const typePrefix = meaning_of(prefix) == meaning::Type || (prefix->is(construct::Attribute) && prefix->first(construct::Arguments) == nullptr);
        ada_type* t = typePrefix ? (prefix->type != nullptr ? prefix->type : nullptr) : (prefix->type != nullptr ? prefix->type : nullptr);
        if (prefix->is(construct::Attribute) && name_text(prefix->child(1)->source) == "base" && prefix->type != nullptr)
            t = prefix->type->base_type();
        if (t == nullptr)
            return {};
        // (an implicit dereference: not static (its prefix is checked not to be null))
        if (prefix->mark & 0x2000u)
            return {};
        auto* v = t->view();
        auto const argument = [&](std::size_t aIndex) -> static_value
        {
            return aIndex < arguments.size() ? evaluate(arguments[aIndex], aRegion) : static_value{};
        };
        // (a static subtype's or a constrained array subtype's bounds; an object's only if its subtype is static and constrained)
        bool const objectPrefix = !typePrefix;
        if (name == "first" || name == "last" || name == "length")
        {
            ada_type* s = v;
            if (v->kind == type_kind::Array)
            {
                // (a static string constant: statically constrained (4.9(32.4/3)), its bounds the initial value's (from its index
                // subtype's first))
                if (!v->constrained && objectPrefix && prefix->ent != nullptr && prefix->ent->kind == entity_kind::Constant && prefix->ent->value.is_string() &&
                    !v->indexes.empty() && v->indexes[0]->view()->low.is_integer())
                {
                    auto const first = v->indexes[0]->view()->low.integer();
                    auto const length = bigint{ static_cast<std::int64_t>(prefix->ent->value.string().size()) };
                    // (its bounds: a string literal's are its index subtype's first and the length's)
                    bool const literal = prefix->ent->initial != nullptr && meaning_of(prefix->ent->initial) == meaning::StringLiteral;
                    if (name == "length")
                        return static_value{ length };
                    if (!literal)
                        return {};
                    if (name == "first")
                        return static_value{ first };
                    return static_value{ first + length - bigint{ 1 } };
                }
                if (!v->constrained)
                    return {};
                std::size_t dimension = 0u;
                if (!arguments.empty())
                {
                    auto d = argument(0);
                    if (!d.is_integer())
                        return {};
                    dimension = static_cast<std::size_t>(d.integer().to_int64() - 1);
                }
                if (dimension >= v->indexes.size())
                    return {};
                s = v->indexes[dimension]->view();
            }
            else if (objectPrefix)
                return {};
            if (!s->low.known() || !s->high.known())
                return {};
            if (name == "first")
                return s->low;
            if (name == "last")
                return s->high;
            if (s->low.is_integer() && s->high.is_integer())
            {
                auto const length = s->high.integer() - s->low.integer() + bigint{ 1 };
                return static_value{ length.negative() ? bigint{ 0 } : length };
            }
            return {};
        }
        if (objectPrefix && name != "size" && name != "alignment" && name != "component_size")
            return {};
        // (a prefix that is a call (a function's, or an attribute's): evaluated, so not static)
        if (objectPrefix && (meaning_of(prefix) == meaning::Call || (prefix->is(construct::Attribute) && prefix->first(construct::Arguments) != nullptr)))
            return {};
        auto* base = v->base_type()->view();
        if (name == "pos" || name == "enum_rep")
        {
            auto a = argument(0);
            if (name == "enum_rep" && a.is_integer() && !base->representation.empty())
            {
                auto const i = a.integer().to_int64();
                if (i >= 0 && static_cast<std::size_t>(i) < base->representation.size())
                    return static_value{ base->representation[static_cast<std::size_t>(i)] };
            }
            return a;
        }
        if (name == "val" || name == "enum_val")
        {
            // (not static if out of the base range: Constraint_Error raised when evaluated)
            auto a = argument(0);
            if (name == "enum_val" && a.is_integer() && base->kind == type_kind::Enumeration && !base->representation.empty())
            {
                // (the value whose representation code it is (none: Constraint_Error raised when evaluated))
                auto const found = std::find(base->representation.begin(), base->representation.end(), a.integer());
                if (found == base->representation.end())
                    return {};
                return static_value{ bigint{ static_cast<std::int64_t>(found - base->representation.begin()) } };
            }
            if (a.is_integer() && base->low.is_integer() && base->high.is_integer() && (a.integer() < base->low.integer() || a.integer() > base->high.integer()))
                return {};
            return a;
        }
        if (name == "succ" || name == "pred")
        {
            auto a = argument(0);
            if (a.is_integer())
            {
                auto r = name == "succ" ? a.integer() + bigint{ 1 } : a.integer() - bigint{ 1 };
                if (base->kind == type_kind::Modular)
                    r = bigint::mod(r, base->modulus);
                else if (base->is_discrete() && base->low.is_integer() && base->high.is_integer() && (r < base->low.integer() || r > base->high.integer()))
                    return {};  // (beyond the base range: Constraint_Error when evaluated)
                return static_value{ r };
            }
            return {};
        }
        if (name == "adjacent" && base->kind == type_kind::Float)
        {
            // (the machine number adjacent to X towards Towards (X itself if they are equal); X not a machine number: the nearest
            // on that side of it)
            auto x = argument(0);
            auto towards = argument(1);
            if (!x.known() || !towards.known())
                return {};
            auto const exact = x.real();
            auto const target = towards.real();
            if (target == exact)
                return static_value{ exact };
            bool const up = exact < target;
            bool const single = base->digits <= 6;
            double const nearest = single ? static_cast<double>(static_cast<float>(exact.to_double())) : exact.to_double();
            auto const machine = rational::from_double(nearest);
            double result = nearest;
            if (up ? !(exact < machine) : !(machine < exact))
                result = single ? static_cast<double>(std::nextafter(static_cast<float>(nearest), up ? HUGE_VALF : -HUGE_VALF)) :
                    std::nextafter(nearest, up ? HUGE_VAL : -HUGE_VAL);
            return static_value{ rational::from_double(result) };
        }
        if (name == "min" || name == "max")
        {
            auto a = argument(0);
            auto b = argument(1);
            if (!a.known() || !b.known())
                return {};
            bool const less = a.is_integer() && b.is_integer() ? a.integer() < b.integer() : a.real() < b.real();
            return (name == "min") == less ? a : b;
        }
        if (name == "modulus")
            return base->kind == type_kind::Modular ? static_value{ base->modulus } : static_value{};
        if (name == "digits")
            return static_value{ bigint{ v->digits != 0 ? v->digits : base->digits } };
        if (name == "delta")
            return static_value{ v->delta.denominator().is_zero() ? base->delta : (v->delta.sign() != 0 ? v->delta : base->delta) };
        if (name == "small")
            return static_value{ base->small };
        if (name == "scale")
            return static_value{ bigint{ base->scale } };
        if (name == "size" || name == "object_size" || name == "value_size")
        {
            // (an object whose size is specified: that; a component of an array with a Component_Size clause: that)
            if (objectPrefix && name == "size" && prefix->ent != nullptr && prefix->ent->is_object() && prefix->ent->size_clause != 0u)
                return static_value{ bigint::from_unsigned(prefix->ent->size_clause) };
            if (objectPrefix && name == "size" && prefix->is(construct::Apply) && meaning_of(prefix) == meaning::Index && prefix->child(0)->type != nullptr)
                if (auto* at = prefix->child(0)->type->view(); at->kind == type_kind::Array && at->base_type()->view()->component_size_clause)
                    return static_value{ bigint::from_unsigned(*at->base_type()->view()->component_size_clause) };
            if (v->size_clause)
                return static_value{ bigint::from_unsigned(*v->size_clause) };
            if (v->is_discrete() && v->low.is_integer() && v->high.is_integer() && name == "size" && typePrefix)
            {
                // (the minimum number of bits for the subtype's values)
                auto low = v->low.integer();
                auto high = v->high.integer();
                if (high < low)
                    return static_value{ bigint{ 0 } };
                // (an enumeration type with a representation: its codes (increasing with the positions, 13.4(11/3)))
                if (auto const& codes = base->view()->representation; !codes.empty() && !low.negative() &&
                    high < bigint{ static_cast<std::int64_t>(codes.size()) })
                {
                    low = codes[static_cast<std::size_t>(low.to_int64())];
                    high = codes[static_cast<std::size_t>(high.to_int64())];
                }
                std::int64_t bits = 0;
                if (low.negative())
                {
                    while (bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(bits)) < high + bigint{ 1 } || -bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(bits)) > low)
                        ++bits;
                    return static_value{ bigint{ bits + 1 } };
                }
                while (bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(bits)) <= high)
                    ++bits;
                return static_value{ bigint{ bits } };
            }
            if (v->size || base->size)
                return static_value{ bigint::from_unsigned((v->size ? *v->size : *base->size) * 8u) };
            return {};
        }
        if (name == "stream_size" && (v->is_scalar() || v->is_access()))
            return static_value{ bigint{ static_cast<std::int64_t>(stream_size(v) * 8u) } };
        // (a scalar subtype's alignment (as generated: specified, or its size))
        if (name == "alignment" && typePrefix && (v->is_scalar() || v->is_access()))
        {
            auto const specified = type_alignment_clause(t);
            return static_value{ bigint{ static_cast<std::int64_t>(specified != 0u ? specified : std::max<std::uint64_t>(scalar_size(v), 1u)) } };
        }
        if (name == "definite")
        {
            // (a definite subtype: not an unconstrained array, nor unconstrained with discriminants without defaults, nor class-wide)
            if (v->kind == type_kind::ClassWide || (v->kind == type_kind::Array && !v->constrained))
                return static_value{ bigint{ 0 } };
            if (v->kind == type_kind::Record || v->kind == type_kind::Private || v->kind == type_kind::Task || v->kind == type_kind::Protected)
                if (v->discriminant_constraint.empty())
                    for (auto const& c : base->components)
                        if (c.discriminant && c.default_expression == nullptr)
                            return static_value{ bigint{ 0 } };
            if (t->kind == type_kind::Private && t->full == nullptr && t->unknown_discriminants)
                return static_value{ bigint{ 0 } };
            return static_value{ bigint{ 1 } };
        }
        if (name == "width")
        {
            // (the maximum length of the image of a value of the subtype)
            if (!v->low.is_integer() || !v->high.is_integer())
                return {};
            if (v->high.integer() < v->low.integer())
                return static_value{ bigint{ 0 } };
            if (base->kind == type_kind::Enumeration)
            {
                std::size_t width = 0u;
                if (base->standard_character)
                    width = v->high.integer() > bigint{ 255 } ? 12u : 3u;
                else
                {
                    auto const low = v->low.integer().to_int64();
                    auto const high = v->high.integer().to_int64();
                    for (auto i = low; i <= high && static_cast<std::size_t>(i) < base->literals.size(); ++i)
                        width = std::max(width, base->literals[static_cast<std::size_t>(i)]->name.size());
                }
                return static_value{ bigint{ static_cast<std::int64_t>(width) } };
            }
            return static_value{ bigint{ static_cast<std::int64_t>(std::max(image_length(v->low.integer()), image_length(v->high.integer()))) } };
        }
        if (name == "machine_radix")
            return static_value{ bigint{ base->machine_radix != 0u ? static_cast<std::int64_t>(base->machine_radix) : 2 } };
        if (name == "machine_mantissa")
            return static_value{ bigint{ base->size && *base->size == 4u ? 24 : 53 } };
        if (name == "machine_emax")
            return static_value{ bigint{ base->size && *base->size == 4u ? 128 : 1024 } };
        if (name == "machine_emin")
            return static_value{ bigint{ base->size && *base->size == 4u ? -125 : -1021 } };
        if (name == "model_mantissa")
        {
            // (the number of binary digits for its decimal digits: ceiling(digits * log2(10)) + 1)
            auto const d = v->digits != 0 ? v->digits : base->digits;
            return static_value{ bigint{ static_cast<std::int64_t>(std::ceil(static_cast<double>(d) * 3.3219280948873623)) + 1 } };
        }
        if (name == "mantissa")
        {
            // (Ada 83 (3.5.7(6), 3.5.9(6)); an implementation-defined attribute now: a floating point type's binary digits (as
            // Model_Mantissa); a fixed point type's: the bits for its largest magnitude in multiples of its small)
            if (base->kind == type_kind::Float)
            {
                auto const d = v->digits != 0 ? v->digits : base->digits;
                return static_value{ bigint{ static_cast<std::int64_t>(std::ceil(static_cast<double>(d) * 3.3219280948873623)) + 1 } };
            }
            if (!base->is_fixed() || !v->low.known() || !v->high.known() || base->small.sign() == 0)
                return {};
            auto const magnitude = std::max(v->low.real().abs(), v->high.real().abs()) / base->small;
            std::int64_t bits = 0;
            while (rational{ bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(bits)) } < magnitude)
                ++bits;
            return static_value{ bigint{ bits } };
        }
        if (name == "model_emin")
            return static_value{ bigint{ base->size && *base->size == 4u ? -125 : -1021 } };
        if (name == "model_epsilon")
        {
            auto const d = v->digits != 0 ? v->digits : base->digits;
            auto const m = static_cast<std::int64_t>(std::ceil(static_cast<double>(d) * 3.3219280948873623)) + 1;
            return static_value{ rational::pow(rational{ bigint{ 2 } }, 1 - m) };
        }
        if (name == "model_small")
        {
            if (base->kind == type_kind::Float)
                return static_value{ rational::pow(rational{ bigint{ 2 } }, base->size && *base->size == 4u ? -126 : -1022) };
            return static_value{ base->small };
        }
        if (name == "safe_first")
            return base->low;
        if (name == "safe_last")
            return base->high;
        if (name == "machine_overflows" || name == "machine_rounds" || name == "signed_zeros" || name == "denorm")
            return static_value{ bigint{ 1 } };
        if (name == "aft" || name == "fore")
        {
            if (!base->is_fixed())
                return {};
            auto const delta = v->delta.sign() != 0 ? v->delta : base->delta;
            if (name == "aft")
            {
                std::int64_t aft = 1;
                rational d = delta;
                while (d * rational{ bigint::pow(bigint{ 10 }, static_cast<std::uint64_t>(aft)) } < rational{ bigint{ 1 } })
                    ++aft;
                return static_value{ bigint{ aft } };
            }
            bigint magnitude;
            if (v->low.known() && v->high.known())
                magnitude = std::max(v->low.real().abs(), v->high.real().abs()).truncate();
            else if (v->low_object != nullptr || v->high_object != nullptr || v->range_node != nullptr)
                return {};      // (dynamic bounds: evaluated at run time)
            else if (base->small.sign() != 0)
                magnitude = (rational{ bigint::pow(bigint{ 2 }, 63u) } * base->small).truncate();     // (a base range: its representation's)
            else
                return {};
            std::int64_t fore = 2;
            for (auto m = magnitude; m >= bigint{ 10 }; m = m / bigint{ 10 })
                ++fore;
            return static_value{ bigint{ fore } };
        }
        if (name == "floor" || name == "ceiling" || name == "truncation" || name == "rounding" || name == "unbiased_rounding" || name == "machine_rounding")
        {
            auto a = argument(0);
            if (!a.known())
                return {};
            auto const r = a.real();
            if (name == "floor")
                return static_value{ rational{ r.floor() } };
            if (name == "ceiling")
                return static_value{ rational{ -((-r).floor()) } };
            if (name == "truncation")
                return static_value{ rational{ r.truncate() } };
            if (name == "rounding")
                return static_value{ rational{ r.round() } };
            // (unbiased: to even)
            auto const f = r.floor();
            auto const fraction = r - rational{ f };
            rational const half{ bigint{ 1 }, bigint{ 2 } };
            if (fraction < half)
                return static_value{ rational{ f } };
            if (fraction > half)
                return static_value{ rational{ f + bigint{ 1 } } };
            return static_value{ rational{ bigint::mod(f, bigint{ 2 }).is_zero() ? f : f + bigint{ 1 } } };
        }
        if (name == "copy_sign")
        {
            auto a = argument(0);
            auto b = argument(1);
            if (!a.known() || !b.known())
                return {};
            auto m = a.real().abs();
            return static_value{ b.real().sign() < 0 ? -m : m };
        }
        if (name == "image" || name == "img")
        {
            // (a static enumeration or integer value's image)
            auto a = arguments.empty() ? static_value{} : argument(0);
            if (!a.is_integer())
                return {};
            if (base->kind == type_kind::Enumeration && !base->standard_character)
            {
                auto const i = a.integer().to_int64();
                if (i < 0 || static_cast<std::size_t>(i) >= base->literals.size())
                    return {};
                std::u32string s;
                auto const& literal = base->literals[static_cast<std::size_t>(i)]->name;
                bool const character = !literal.empty() && literal.front() == '\'';
                for (auto c : literal)
                    s += static_cast<char32_t>(!character && c >= 'a' && c <= 'z' ? c - 'a' + 'A' : static_cast<unsigned char>(c));
                return static_value{ s };
            }
            if (base->is_integer())
            {
                std::u32string s;
                auto text = a.integer().to_string();
                if (!a.integer().negative())
                    s += U' ';
                for (auto c : text)
                    s += static_cast<char32_t>(c);
                return static_value{ s };
            }
            return {};
        }
        return {};
    }
}
