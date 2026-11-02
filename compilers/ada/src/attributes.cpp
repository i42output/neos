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
        }
        ada_type* t = nullptr;
        // (a subtype, T'Base or T'Class; another attribute (e.g. X'Image (Y)) is a value)
        if (is_subtype_name(prefix, aRegion) || (prefix->is(construct::Attribute) && arguments_of(prefix).empty() &&
            (name_text(prefix->child(1)->source) == "base" || name_text(prefix->child(1)->source) == "class")))
            t = subtype_mark(prefix, aRegion);
        else
        {
            t = resolve(prefix, nullptr, aRegion);
            if (t != nullptr && t->view()->kind == type_kind::Access)
            {
                t = designated_of(t);
                prefix->mark |= 0x2000u;
            }
        }
        if (t == nullptr)
            error(aRangeAttribute, "prefix of 'Range has no range");
        auto* v = t->view();
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
                try { entities = prefix_entities(prefix, aRegion); } catch (compile_error const&) {}
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
            prefixType = subtype_mark(prefix, aRegion);
            set_meaning(prefix, meaning::Type);
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
                aAttribute->type = aExpected;
                return aExpected;
            }
            ada_type* expectedObject = aExpected != nullptr && aExpected->view()->kind == type_kind::Access ? designated_of(aExpected) : nullptr;
            if (expectedObject != nullptr && expectedObject->view()->kind == type_kind::ClassWide)
                expectedObject = nullptr;
            prefixType = resolve(prefix, expectedObject, aRegion);
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
            if (!arguments.empty())
                resolve(arguments[0], nullptr, aRegion);
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
        else if (name == "input")
        {
            if (!arguments.empty())
                resolve(arguments[0], nullptr, aRegion);
            result = prefixType;
        }
        else if (name == "read" || name == "write" || name == "output" || name == "put_image")
        {
            if (arguments.size() >= 1u)
                resolve(arguments[0], nullptr, aRegion);
            if (arguments.size() >= 2u)
                resolve(arguments[1], prefixType, aRegion);
            result = iVoid;
        }
        else if (name == "storage_pool")
        {
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
        if (result == nullptr)
            error(aAttribute, "unsupported attribute '" + name);
        // (a universal result used as the expected type)
        if (result->is_universal() && aExpected != nullptr && !aExpected->is_universal() && covers(aExpected, result))
            result = aExpected->base_type();
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
