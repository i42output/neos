/*
  units2.cpp

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

// Ada: generic units and their instances, tasks and protected units, subunits, aspects and the predefined library's types

#include <neolib/neolib.hpp>
#include <cstdio>
#include <cstdlib>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <functional>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        node* specification_of(node* aItem)
        {
            for (auto* c : aItem->children)
                if (c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification))
                    return c;
            return nullptr;
        }
    }

    // --- the predefined library's types

    ada_type* program::string_type_named(std::string const& aName)
    {
        auto range = iStandard->names.equal_range(aName);
        for (auto i = range.first; i != range.second; ++i)
            if (i->second->is_type())
                return i->second->type;
        return iString;
    }

    ada_type* program::library_type(std::string const& aUnit, std::string const& aName)
    {
        if (auto* unit = library_unit(aUnit); unit != nullptr && unit->inner != nullptr)
        {
            auto range = unit->inner->names.equal_range(aName);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->is_type())
                    return i->second->type;
        }
        // (a stand-in when the unit is not part of the program)
        auto& standin = iStandins[aUnit + "." + aName];
        if (standin == nullptr)
        {
            standin = new_type(type_kind::Integer, aName);
            standin->size = 8u;
            standin->low = static_value{ bigint{ std::numeric_limits<std::int64_t>::min() } };
            standin->high = static_value{ bigint{ std::numeric_limits<std::int64_t>::max() } };
        }
        return standin;
    }

    ada_type* program::system_type(std::string const& aName)
    {
        return library_type("system", aName);
    }

    ada_type* program::tags_type(std::string const& aName)
    {
        return library_type("ada.tags", aName);
    }

    ada_type* program::task_identification_type()
    {
        return library_type("ada.task_identification", "task_id");
    }

    // --- aspects

    void program::aspects(node* aDecl, entity* aEntity, region* aRegion)
    {
        auto* aspects = aDecl != nullptr ? aDecl->first(construct::Aspects) : nullptr;
        if (aspects == nullptr || aEntity == nullptr)
            return;
        std::string convention;
        std::string external;
        bool import = false;
        for (auto* aspect : aspects->children)
        {
            // (a class-wide aspect (Pre'Class ...): its name's Class)
            auto const name = name_text(aspect->child(0)->source) + (aspect->size() > 2u && aspect->child(1)->is(construct::Word) &&
                name_text(aspect->child(1)->source) == "class" ? "'class" : "");
            auto* value = aspect->size() > 1u ? aspect->children.back() : nullptr;
            if (value == aspect->child(0))
                value = nullptr;
            if ((name == "priority" || name == "interrupt_priority") && (aEntity->kind == entity_kind::Task ||
                aEntity->kind == entity_kind::Protected || aEntity->sub != nullptr))
            {
                // (as the pragma: see pragma)
                aEntity->interrupt_priority = name == "interrupt_priority";
                aEntity->priority = value;
                if (value != nullptr)
                    resolve(value, integer(), aEntity->inner != nullptr ? aEntity->inner : aRegion);
            }
            else if ((name == "interrupt_handler" || name == "attach_handler") && aEntity->scope != nullptr && aEntity->scope->owner != nullptr &&
                aEntity->scope->owner->kind == entity_kind::Protected && aEntity->kind == entity_kind::Procedure)
            {
                // (see pragma)
                if (aEntity->scope->owner->priority == nullptr)
                    aEntity->scope->owner->interrupt_priority = true;
                if (name == "attach_handler" || value == nullptr || name_text(value->source) == "true")
                    iInterruptHandlers.push_back(aEntity);
                if (name == "attach_handler" && value != nullptr)
                {
                    resolve(value, nullptr, aRegion);   // (of Ada.Interrupts.Interrupt_Id)
                    iAttachHandlers[aEntity->scope->owner].emplace_back(aEntity, value);
                }
            }
            else if ((name == "volatile" || name == "atomic" || name == "volatile_components" || name == "atomic_components") && aEntity->is_object() &&
                (value == nullptr || name_text(value->source) == "true"))
                (name == "volatile" || name == "atomic" ? aEntity->volatile_object : aEntity->volatile_components) = true;   // (see pragma)
            else if ((name == "volatile" || name == "atomic" || name == "volatile_components" || name == "atomic_components") && aEntity->is_type() &&
                aEntity->type != nullptr && value == nullptr)
                iTypeAspects[aEntity->type->base_type()][name] = aspect;    // (a by-reference type (C.6(18/3)): see volatile_type)
            else if (name == "import")
                import = value == nullptr || name_text(value->source) == "true";
            else if (name == "convention")
                convention = value != nullptr ? name_text(value->source) : std::string{};
            else if (name == "external_name" || name == "link_name")
            {
                if (value != nullptr)
                {
                    auto text = std::string{ value->file != nullptr ? value->file->original_of(value->source) : value->source };
                    if (text.size() >= 2u && text.front() == '\"')
                        external = text.substr(1u, text.size() - 2u);
                }
            }
            else if (name == "machine_radix" && aEntity->is_type() && aEntity->type != nullptr && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                if (auto v = evaluate(value, aRegion); v.is_integer())
                    aEntity->type->view()->base_type()->view()->machine_radix = static_cast<std::uint32_t>(v.integer().to_int64());
            }
            else if (name == "no_return")
            {
                if (aEntity->sub != nullptr && (value == nullptr || name_text(value->source) == "true"))
                    aEntity->sub->no_return = true;
            }
            else if (name == "pre" || name == "pre'class" || name == "post" || name == "post'class")
            {
                // (of a subprogram: not of an instance or a package, nor (a specific one) of a null procedure or an abstract subprogram
                // (6.1.1))
                if (iInstanceBodies == 0 && (aEntity->sub == nullptr || aDecl->is(construct::Instantiation) ||
                    ((name == "pre" || name == "post") && (aEntity->sub->is_abstract || aDecl->is(construct::SubprogramNull) ||
                        aDecl->is(construct::SubprogramAbstract)))))
                    recover([&]() { error(aspect, "aspect " + name + " cannot be specified here"); });
                // (a class-wide one: not of a protected operation (not a primitive subprogram) (6.1.1(5/3)))
                else if ((name == "pre'class" || name == "post'class") && iInstanceBodies == 0 && aRegion != nullptr && aRegion->owner != nullptr &&
                    (aRegion->owner->kind == entity_kind::Protected || aRegion->owner->kind == entity_kind::Task))
                    recover([&]() { error(aspect, "aspect " + name + " cannot be specified for a protected operation"); });
                // (resolved at the end of the declarative part (they can name declarations after the subprogram's))
                if (aEntity->sub != nullptr && value != nullptr && !assertion_ignored(name, aRegion))
                {
                    (name == "pre" ? aEntity->sub->preconditions : name == "pre'class" ? aEntity->sub->class_preconditions :
                        name == "post" ? aEntity->sub->postconditions : aEntity->sub->class_postconditions).push_back(value);
                    iDeferredAspects[aRegion].push_back({ nullptr, name, value, aEntity });
                }
            }
            else if (name == "address" && aEntity->is_object() && value != nullptr)
            {
                // (an object at a specified address)
                resolve(value, system_type("address"), aRegion);
                aEntity->address_clause = value;
            }
            else if (name == "pack")
            {
                if (aEntity->is_type())
                    aEntity->type->packed = aEntity->type->view()->base_type()->view()->packed = true;
            }
            else if (name == "unchecked_union")
            {
                if (aEntity->is_type())
                    aEntity->type->view()->base_type()->view()->unchecked_union = true;
            }
            else if (name == "alignment" && (aEntity->is_type() || aEntity->is_object()) && value != nullptr && !is_static_resolvable(value, aRegion))
                iDeferredAspects[aRegion].push_back({ aEntity->is_type() ? aEntity->type : nullptr, name, value, aEntity->is_object() ? aEntity : nullptr });
            else if (name == "alignment" && (aEntity->is_type() || aEntity->is_object()) && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                if (auto v = evaluate(value, aRegion); v.is_integer())
                {
                    if (aEntity->is_type())
                        aEntity->type->alignment_clause = v.integer().to_uint64();
                    else
                        aEntity->alignment_clause = v.integer().to_uint64();
                }
            }
            else if ((name == "size" || name == "small") && aEntity->is_type() && value != nullptr && !is_static_resolvable(value, aRegion))
                iDeferredAspects[aRegion].push_back({ aEntity->type, name, value });
            else if (name == "size" && aEntity->is_object() && value != nullptr && is_static_resolvable(value, aRegion))
            {
                resolve(value, nullptr, aRegion);
                if (auto v = evaluate(value, aRegion); v.is_integer())
                    aEntity->size_clause = v.integer().to_uint64();
            }
            else if (name == "size" && aEntity->is_type() && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                auto v = evaluate(value, aRegion);
                if (v.is_integer())
                    aEntity->type->size_clause = v.integer().to_uint64();
            }
            else if (name == "small" && aEntity->is_type() && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                auto v = evaluate(value, aRegion);
                if (v.known())
                {
                    aEntity->type->base_type()->small = v.real();
                    aEntity->type->small = v.real();
                }
            }
            else if ((name == "static_predicate" || name == "dynamic_predicate") && !aEntity->is_type() && aEntity->kind != entity_kind::Task &&
                aEntity->kind != entity_kind::Protected)
                recover([&]() { error(aspect, "aspect " + name + " can be specified only for a type or subtype"); });
            else if ((name == "static_predicate" || name == "dynamic_predicate" || name == "predicate") && aEntity->is_type() && value != nullptr)
            {
                // (not of an incomplete view (3.10.1(2.2/2)))
                if (aEntity->type != nullptr && aEntity->type->incomplete && aEntity->type->full == nullptr)
                    recover([&]() { error(aspect, "aspect " + name + " cannot be specified for an incomplete type"); });
                // (a predicate: of this subtype and its subtypes (its function: at the end of the declarative part); a subtype
                // declared without a constraint: a subtype of its own (not its type's))
                if (aEntity->kind == entity_kind::Subtype && aEntity->type->name != aEntity->name)
                    aEntity->type = new_subtype(aEntity->type, aEntity->name);
                auto& predicate = iPredicates.emplace_back();
                predicate.parent = aEntity->type->predicate;
                predicate.expression = value;
                predicate.is_static = name == "static_predicate";
                predicate.checked = !assertion_ignored(name == "predicate" ? "dynamic_predicate" : name, aRegion);
                aEntity->type->predicate = &predicate;
                iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            }
            else if ((name == "type_invariant" || name == "type_invariant'class" || name == "invariant") && aEntity->is_type() && value != nullptr)
            {
                // (a type invariant (7.3.2): its function at the end of the declarative part (unless its policy is Ignore))
                if (!assertion_ignored(name == "invariant" ? "type_invariant" : name, aRegion))
                    iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            }
            else if (name == "predicate_failure" && (!aEntity->is_type() || [&]()
                {
                    // (of a subtype with a predicate specified with it (3.2.4(14.2/5)))
                    for (auto* a : aspects->children)
                        if (auto const n = name_text(a->child(0)->source); n == "static_predicate" || n == "dynamic_predicate" || n == "predicate")
                            return false;
                    return true;
                }()))
                recover([&]() { error(aspect, "aspect Predicate_Failure can be specified only for a subtype with a predicate"); });
            else if (name == "predicate_failure" && aEntity->is_type() && value != nullptr)
                iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            else if (aEntity->is_type() && value != nullptr)
            {
                // (Constant_Indexing, Variable_Indexing, Default_Iterator, Iterator_Element, Implicit_Dereference, Aggregate,
                // Default_Value, Type_Invariant, predicates ...: used later)
                iTypeAspects[aEntity->type->base_type()][name] = value;
                // (Stream_Size, Storage_Size, External_Tag: resolved here (values, used later))
                if (name == "stream_size" || name == "storage_size" || name == "external_tag")
                    resolve(value, nullptr, aRegion);
                // (Default_Value, Default_Component_Value: resolved at the end of the declarative part (they can name later
                // declarations, 13.1.1(11/3)), of the type, of its component type)
                if (name == "default_value" || (name == "default_component_value" && aEntity->type->view()->kind == type_kind::Array))
                    iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            }
        }
        if (import && aEntity->sub != nullptr)
        {
            aEntity->imported = true;
            if (convention == "neos")
            {
                aEntity->sub->op = intrinsic::Import;
                aEntity->sub->import_name = external;
            }
            else if (convention == "intrinsic")
                aEntity->sub->op = intrinsic_operation(aEntity);
            aEntity->sub->external_name = external;
            aEntity->sub->body = aDecl;
            // (a foreign subprogram (no neos program provides it): its body raises Program_Error when it is called)
            if (convention != "neos" && convention != "intrinsic")
                iSubprogramsToGenerate.push_back(aEntity);
        }
        else if (import)
            aEntity->imported = true;
        else if (convention == "fortran" && aEntity->is_type() && aEntity->type != nullptr)
            aEntity->type->view()->base_type()->view()->fortran = true;
        else if (convention == "intrinsic" && aEntity->sub != nullptr && intrinsic_operation(aEntity) != intrinsic::Import)
        {
            // (an operator of convention Intrinsic (e.g. System's comparisons of addresses): predefined)
            aEntity->sub->op = intrinsic_operation(aEntity);
            aEntity->sub->body = aDecl;
        }
    }

    bool program::is_static_resolvable(node* aExpression, region* aRegion)
    {
        // (an expression whose names are all declared (now))
        try
        {
            speculation guard{ *this };
            (void)interpretations(aExpression, aRegion);
            if ((aExpression->is(construct::Identifier) || aExpression->is(construct::Selected)) && prefix_entities(aExpression, aRegion).empty())
                return false;
        }
        catch (compile_error const&)
        {
            return false;
        }
        if (aExpression->is(construct::Identifier))
            return !visible(name_text(aExpression->source), aRegion).empty();
        for (auto* c : aExpression->children)
            if (!is_static_resolvable(c, aRegion))
                return false;
        return true;
    }

    intrinsic program::intrinsic_operation(entity* aSubprogram)
    {
        auto const& n = aSubprogram->name;
        if (n == "+") return aSubprogram->sub->parameters.size() == 1u ? intrinsic::Plus : intrinsic::Add;
        if (n == "-") return aSubprogram->sub->parameters.size() == 1u ? intrinsic::Negate : intrinsic::Subtract;
        if (n == "*") return intrinsic::Multiply;
        if (n == "/") return intrinsic::Divide;
        if (n == "mod") return intrinsic::Mod;
        if (n == "rem") return intrinsic::Rem;
        if (n == "**") return intrinsic::Power;
        if (n == "abs") return intrinsic::Abs;
        if (n == "not") return intrinsic::Not;
        if (n == "and") return intrinsic::And;
        if (n == "or") return intrinsic::Or;
        if (n == "xor") return intrinsic::Xor;
        if (n == "=") return intrinsic::Eq;
        if (n == "/=") return intrinsic::Ne;
        if (n == "<") return intrinsic::Lt;
        if (n == "<=") return intrinsic::Le;
        if (n == ">") return intrinsic::Gt;
        if (n == ">=") return intrinsic::Ge;
        if (n == "&") return intrinsic::Concat;
        return intrinsic::Import;
    }

    void program::pragma_extra(node* aPragma, std::string const& aName, std::vector<node*> const& aArguments, region* aRegion)
    {
        auto const value = [&](std::size_t aIndex) -> node*
        {
            return aIndex < aArguments.size() ? aArguments[aIndex]->children.back() : nullptr;
        };
        if (aName == "assertion_policy")
        {
            // (the policy (Check or Ignore) of the assertion kinds named (else of all) from here to the end of the region (11.4.2(7/3)))
            auto& policies = iAssertionPolicies[aRegion];
            for (auto* a : aArguments)
            {
                std::string kind;
                std::size_t words = 0u;
                for (auto* c : a->children)
                    if (c->is(construct::Word) && c != a->children.back())
                        kind += (words++ > 0u ? "'" : "") + name_text(c->source);
                auto const policy = name_text(a->children.back()->source);
                bool const ignore = policy == "ignore" || policy == "disable";
                if (kind.empty())
                    policies.clear();
                policies[kind] = ignore;
            }
            return;
        }
        if (aName == "default_storage_pool")
        {
            // (the default pool of the access types declared after it in the region (13.11.3(4/3)); null: the standard pool)
            if (auto* pool = value(0))
            {
                if (!pool->is(construct::Null))
                    resolve(pool, nullptr, aRegion);
                iDefaultPools[aRegion].emplace_back(aPragma->source.data(), pool->is(construct::Null) ? nullptr : pool);
            }
            return;
        }
        if (aName == "assert" || aName == "assume" || aName == "loop_invariant")
        {
            if (assertion_ignored(aName == "assert" ? "assert" : aName, aRegion))
                iIgnoredAssertions.insert(aPragma);
            if (auto* condition = value(0))
                this->condition(condition, aRegion);
            if (auto* message = value(1))
                resolve(message, iString, aRegion);
        }
        else if (aName == "convention" || aName == "export" || aName == "volatile" || aName == "atomic" || aName == "controlled" ||
            aName == "priority" || aName == "storage_size" || aName == "interrupt_priority" || aName == "discard_names" || aName == "no_return" ||
            aName == "unchecked_union" || aName == "atomic_components" || aName == "volatile_components" || aName == "independent" ||
            aName == "restrictions" || aName == "reviewable" || aName == "normalize_scalars" || aName == "linker_options" || aName == "queuing_policy" ||
            aName == "locking_policy" || aName == "task_dispatching_policy" || aName == "detect_blocking" || aName == "asynchronous" ||
            aName == "all_calls_remote" || aName == "remote_types" || aName == "remote_call_interface" || aName == "shared_passive" ||
            aName == "inspection_point" || aName == "partition_elaboration_policy" || aName == "profile" || aName == "assertion_policy" ||
            aName == "default_storage_pool" || aName == "relative_deadline" || aName == "cpu" || aName == "dispatching_domain" ||
            aName == "attach_handler" || aName == "interrupt_handler" || aName == "preelaborable_initialization" || aName == "independent_components" ||
            aName == "unreferenced" || aName == "warnings" || aName == "style_checks" || aName == "ada_95" || aName == "ada_2005" ||
            aName == "ada_2012" || aName == "ada_2022" || aName == "extensions_allowed" || aName == "initialize_scalars")
            ;
        (void)aPragma;
    }

    bool program::assertion_ignored(std::string const& aKind, region* aRegion) const
    {
        // (the innermost policy for the kind (or for all kinds) in effect: Check by default)
        for (auto* r = aRegion; r != nullptr; r = r->parent)
            if (auto p = iAssertionPolicies.find(r); p != iAssertionPolicies.end())
            {
                if (auto k = p->second.find(aKind); k != p->second.end())
                    return k->second;
                if (auto all = p->second.find(std::string{}); all != p->second.end())
                    return all->second;
            }
        return false;
    }

    void program::stream_or_other_attribute_clause(node* aClause, ada_type* aType, std::string const& aAttribute, node* aExpression, region* aRegion)
    {
        (void)aClause;
        if (aAttribute == "read" || aAttribute == "write" || aAttribute == "input" || aAttribute == "output" || aAttribute == "put_image")
        {
            iTypeAspects[aType->base_type()][aAttribute] = aExpression;
            return;
        }
        resolve(aExpression, nullptr, aRegion);
        iTypeAspects[aType->base_type()][aAttribute] = aExpression;
        // (a storage pool object: in memory (its allocators may be in nested subprograms))
        if (aAttribute == "storage_pool")
            for (auto* n = aExpression; n != nullptr; n = n->children.empty() ? nullptr : n->child(0))
                if (n->ent != nullptr && n->ent->is_object())
                {
                    n->ent->captured = true;
                    break;
                }
    }

    node* program::type_aspect(ada_type* aType, std::string const& aAspect)
    {
        // (a type's aspect (or attribute definition clause): recorded with its partial view or its full view (e.g. 
        // Implicit_Dereference on a private type's declaration, its stream attributes on its full type's))
        if (aType == nullptr)
            return nullptr;
        auto* base = aType->view()->base_type()->view();
        for (auto* key : { base, base->partial, aType->base_type(), aType->base_type()->partial, aType })
        {
            if (key == nullptr)
                continue;
            auto aspects = iTypeAspects.find(key);
            if (aspects == iTypeAspects.end())
                continue;
            auto a = aspects->second.find(aAspect);
            if (a != aspects->second.end())
                return a->second;
        }
        return nullptr;
    }

    node* program::default_value_of(ada_type* aType, bool aComponent)
    {
        // (a scalar type's Default_Value, an array type's Default_Component_Value (its own, or its parent's))
        if (aType == nullptr)
            return nullptr;
        for (auto* t = aType->view()->base_type()->view(); t != nullptr; t = t->parent != nullptr ? t->parent->view()->base_type()->view() : nullptr)
        {
            if (auto* a = type_aspect(t, aComponent ? "default_component_value" : "default_value"))
                return a;
            if (t->parent == t)
                break;
        }
        return nullptr;
    }

    entity* program::aspect_subprogram(ada_type* aType, std::string const& aAspect, bool aInherited)
    {
        for (auto* t = aType->view()->base_type()->view(); t != nullptr; t = t->parent != nullptr ? t->parent->base_type()->view() : nullptr)
        {
            if (auto* aspect = type_aspect(t, aAspect))
            {
                {
                    auto* name = aspect;
                    if (name->ent != nullptr)
                        return name->ent;
                    // (the aspect's subprogram: a primitive of the type with the name)
                    auto const text = name_text(name->source);
                    for (auto* op : t->primitives)
                        if (op->name == text)
                            return op;
                    if (t->declared_in != nullptr)
                    {
                        auto range = t->declared_in->names.equal_range(text);
                        for (auto i = range.first; i != range.second; ++i)
                            if (i->second->sub != nullptr)
                                return i->second;
                    }
                }
            }
            if (!aInherited)
                break;
            if (t->parent == nullptr)
                break;
        }
        return nullptr;
    }

    void program::variable_name(node* aName)
    {
        // (the name and the prefixes it is a component, slice or element of)
        for (auto* n = aName; n != nullptr; )
        {
            while (n->is(construct::Parenthesized) && !n->children.empty())
                n = n->child(0);
            iVariableNames.insert(n);
            if ((n->is(construct::Selected) || n->is(construct::Apply)) && !n->children.empty())
                n = n->child(0);
            else
                break;
        }
    }

    std::vector<entity*> program::indexing_candidates(ada_type* aType, entity* aFunction)
    {
        std::vector<entity*> result;
        for (auto* op : prefixed_candidates(aType, aFunction->name))
            if (op->sub != nullptr && std::find(result.begin(), result.end(), op) == result.end())
                result.push_back(op);
        if (aFunction->scope != nullptr)
        {
            auto range = aFunction->scope->names.equal_range(aFunction->name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->sub != nullptr && std::find(result.begin(), result.end(), i->second) == result.end())
                    result.push_back(i->second);
        }
        // (an inherited operation overridden: the overriding one only)
        std::erase_if(result, [&](entity* op)
        {
            for (auto* other : result)
                if (other != op && other->sub->inherited == false && op->sub->inherited && type_conformant(*other->sub, *op->sub))
                    return true;
            return false;
        });
        return result;
    }

    ada_type* program::resolve_generalized_indexing(node* aApply, node* aPrefix, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion)
    {
        // (Container (Index): a call of the type's Constant_Indexing or Variable_Indexing function)
        auto const options = interpretations(aPrefix, aRegion);
        for (auto const& o : options)
        {
            if (o.type == nullptr)
                continue;
            // (Constant_Indexing unless the name is in a variable context (or there is none))
            bool const variable = iVariableNames.contains(aApply);
            for (auto const* aspect : { variable ? "variable_indexing" : "constant_indexing", variable ? "constant_indexing" : "variable_indexing" })
            {
                auto* function = aspect_subprogram(o.type, aspect);
                if (function == nullptr)
                    continue;
                // (the function's overloads: those of its name in its region, and the type's primitive operations of its name (a
                // derived type's overriding ones and new overloads, 4.1.6(10/3)))
                std::vector<entity*> candidates = indexing_candidates(o.type, function);
                // (of those whose actuals match: one whose result (or its dereference) is of the expected type)
                std::erase_if(candidates, [&](entity* c) { return !match_call(c, aArguments, aPrefix, aRegion, nullptr, true); });
                if (aExpected != nullptr && candidates.size() > 1u)
                    std::stable_partition(candidates.begin(), candidates.end(), [&](entity* c)
                    {
                        auto* r = c->sub->result;
                        return r != nullptr && (covers(aExpected, r) || covers(aExpected, implicit_dereference(r)));
                    });
                for (auto* c : candidates)
                    {
                        auto* r = finish_call(aApply, c, aArguments, aPrefix, aRegion);
                        (void)aExpected;
                        // (generalized indexing: its result (a reference) is implicitly dereferenced)
                        if (auto* d = implicit_dereference(r); d != r)
                        {
                            iImplicitDereferences[aApply] = r;
                            aApply->type = d;
                            return d;
                        }
                        return r;
                    }
            }
        }
        return nullptr;
    }

    std::optional<std::uint64_t> program::reference_offset(ada_type* aReference)
    {
        // (a reference type's access discriminant's offset)
        auto* base = aReference->view()->base_type()->view();
        auto* aspect = type_aspect(aReference, "implicit_dereference");
        if (aspect == nullptr)
            return {};
        layout(base);
        auto const discriminant = name_text(aspect->source);
        for (auto const& c : base->components)
            if (c.discriminant && c.ent->name == discriminant)
                return c.offset;
        return {};
    }

    ada_type* program::implicit_dereference(ada_type* aType)
    {
        // (a reference type (Implicit_Dereference aspect): the type its access discriminant designates)
        if (aType == nullptr)
            return nullptr;
        auto* base = aType->view()->base_type()->view();
        auto* aspect = type_aspect(aType, "implicit_dereference");
        if (aspect == nullptr)
            return aType;
        auto const discriminant = name_text(aspect->source);
        for (auto const& c : base->components)
            if (c.discriminant && c.ent->name == discriminant && c.type != nullptr && c.type->view()->kind == type_kind::Access)
                return designated_of(c.type);
        return aType;
    }

    ada_type* program::resolve_container_aggregate(node* aAggregate, ada_type* aType, region* aRegion)
    {
        // ([...] of a type with the Aggregate aspect: resolved against its Add_Unnamed / Add_Named / New_Indexed operations'
        // element types)
        ada_type* element = iterator_element_type(aType);
        for (auto* item : aAggregate->children)
        {
            if (item->is(construct::Association))
            {
                for (auto* choice : item->child(0)->children)
                    resolve(choice, nullptr, aRegion);
                resolve(item->children.back(), element, aRegion);
            }
            else if (item->is(construct::Iterated))
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[item] = r;
                iterator_specification(item->child(0), r);
                resolve(item->children.back(), element, r);
            }
            else
                resolve(item, element, aRegion);
        }
        set_meaning(aAggregate, meaning::Value);
        aAggregate->type = aType;
        return aType;
    }

    ada_type* program::iterator_cursor_type(ada_type* aIterator)
    {
        // (the cursor type of an iterator type: the result of its First function)
        if (aIterator == nullptr)
            return nullptr;
        auto* t = aIterator->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view();
        for (auto* a = t; a != nullptr; a = a->parent != nullptr ? a->parent->base_type()->view() : nullptr)
        {
            for (auto* op : a->primitives)
                if (op->name == "first" && op->sub != nullptr && op->sub->result != nullptr)
                    return op->sub->result;
            for (auto* i : a->interfaces)
                for (auto* op : i->view()->primitives)
                    if (op->name == "first" && op->sub != nullptr && op->sub->result != nullptr)
                        return op->sub->result;
            if (a->parent == nullptr)
                break;
        }
        return iInteger;
    }

    ada_type* program::iterator_element_type(ada_type* aContainer)
    {
        if (aContainer == nullptr)
            return nullptr;
        auto* t = aContainer->view()->base_type()->view();
        for (auto* a = t; a != nullptr; a = a->parent != nullptr ? a->parent->base_type()->view() : nullptr)
        {
            if (auto* e = type_aspect(a, "iterator_element"))
            {
                if (e->type == nullptr)
                    try
                    {
                        speculation guard{ *this };
                        e->type = subtype_mark(e, a->declared_in != nullptr ? a->declared_in : iStandard);
                    }
                    catch (compile_error const&)
                    {
                    }
                if (e->type != nullptr)
                    return e->type;
            }
            if (a->parent == nullptr)
                break;
        }
        return nullptr;
    }

    void program::resolve_requeue(node* aRequeue, region* aRegion)
    {
        auto* name = aRequeue->child(0);
        // (an entry of the same or another task or protected object)
        try
        {
            speculation guard{ *this };
            resolve(name, iVoid, aRegion);
        }
        catch (compile_error const&)
        {
            // (a procedure of a synchronized interface (implemented by an entry): a dispatching requeue on the object (9.5.4(3/3)))
            if (name->is(construct::Selected))
            {
                auto* prefix = name->child(0);
                auto const selector = name_text(name->child(1)->source);
                ada_type* pt = nullptr;
                try { speculation guard{ *this }; pt = resolve(prefix, nullptr, aRegion); } catch (compile_error const&) { pt = nullptr; }
                if (pt != nullptr && pt->view()->is_access())
                    pt = designated_of(pt);
                auto* specific = pt != nullptr && pt->view()->kind == type_kind::ClassWide ? pt->view()->specific : pt;
                if (auto* iv = specific != nullptr ? specific->view()->base_type()->view() : nullptr; iv != nullptr && iv->kind == type_kind::Interface)
                    for (auto const& s : iv->slots)
                        if (s.operation != nullptr && s.operation->name == selector && s.operation->kind == entity_kind::Procedure)
                        {
                            name->ent = s.operation;
                            set_meaning(name, meaning::Entry);
                            auto& call = iCalls[name];
                            call.callee = s.operation;
                            call.object = prefix;
                            return;
                        }
            }
            auto const entities = prefix_entities(name, aRegion);
            if (entities.empty())
                throw;
            name->ent = entities.front();
            set_meaning(name, meaning::Entry);
        }
        if (iInstanceBodies != 0)
            return;
        // (the target: no actual parameters; its profile: none, or type conformant with the enclosing entry's (9.5.4(3/3)-(5/3)))
        auto const* call = call_of(name);
        entity* target = call != nullptr && call->callee != nullptr ? call->callee : name->ent;
        if (call != nullptr)
            for (auto* actual : call->actuals)
                if (actual != nullptr && actual != call->object && actual->parent != nullptr &&
                    (actual->parent == name || (actual->parent->is(construct::Association) && actual->parent->parent == name)))
                {
                    recover([&]() { error(name, "a requeue cannot give actual parameters"); });
                    break;
                }
        entity* enclosing = nullptr;
        for (auto* p = aRequeue->parent; p != nullptr && enclosing == nullptr; p = p->parent)
        {
            if (p->is(construct::Accept))
                if (auto r = iStatementRegions.find(p); r != iStatementRegions.end())
                    if (auto e = iAcceptEntries.find(r->second); e != iAcceptEntries.end())
                        enclosing = e->second;
            if (p->is(construct::EntryBody) || p->is(construct::SubprogramBody) || p->is(construct::TaskBody))
                break;
        }
        if (enclosing == nullptr && iCurrentSubprogram != nullptr && iCurrentSubprogram->kind == entity_kind::Entry)
            enclosing = iCurrentSubprogram;
        if (target != nullptr && enclosing != nullptr && target->sub != nullptr && enclosing->sub != nullptr && !target->sub->parameters.empty() &&
            !type_conformant(*target->sub, *enclosing->sub))
            recover([&]() { error(name, "the target of a requeue must have no parameters or a profile type conformant with the enclosing entry's"); });
    }

    // --- generic units

    void program::generic_declaration(node* aDecl, region* aRegion)
    {
        auto* formals = aDecl->first(construct::GenericFormals);
        entity* e = nullptr;
        for (auto* c : aDecl->children)
        {
            if (c->is(construct::Package))
            {
                e = declare(entity_kind::GenericPackage, c->first(construct::Defining), aRegion);
                c->ent = e;
            }
            else if (c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification))
            {
                // (a generic subprogram's name: an identifier, not an operator symbol (12.1(3)))
                if (c->child(0)->is(construct::DefiningOperator))
                    recover([&]() { error(c->child(0), "a generic subprogram cannot be an operator"); });
                e = declare(c->is(construct::FunctionSpecification) ? entity_kind::GenericFunction : entity_kind::GenericProcedure, c->child(0), aRegion);
                e->sub->spec = c;
            }
        }
        // (a generic procedure's No_Return aspect: its instances are nonreturning (its other aspects are its instances'))
        for (auto* holder : { aDecl, e != nullptr && e->sub != nullptr ? e->sub->spec : nullptr })
            if (auto* aspects = holder != nullptr ? holder->first(construct::Aspects) : nullptr)
                for (auto* aspect : aspects->children)
                    if (name_text(aspect->child(0)->source) == "no_return" && e != nullptr && e->sub != nullptr &&
                        (aspect->size() < 2u || aspect->children.back() == aspect->child(0) || name_text(aspect->children.back()->source) == "true"))
                        e->sub->no_return = true;
        if (e == nullptr)
            return;
        e->decl = aDecl;
        aDecl->ent = e;
        // (Pre'Class and Post'Class: only of a primitive subprogram of a tagged type, not of a generic (6.1.1(5/3)))
        if (iInstanceBodies == 0 && e->sub != nullptr)
            for (auto* holder : { aDecl, e->sub->spec })
                if (auto* aspects = holder != nullptr ? holder->first(construct::Aspects) : nullptr)
                    for (auto* aspect : aspects->children)
                        if (auto const n = name_text(aspect->child(0)->source); (n == "pre" || n == "post") && aspect->size() > 2u &&
                            aspect->child(1)->is(construct::Word) && name_text(aspect->child(1)->source) == "class")
                            recover([&]() { error(aspect, "aspect " + n + "'Class can be specified only for a primitive subprogram of a tagged type"); });
        // (the formals' forms (12.4(2/3), 12.5(2.1/3)-(2.3/3)))
        if (formals != nullptr && iInstanceBodies == 0)
            for (auto* f : formals->children)
                recover([&]()
                {
                    if (f->is(construct::FormalObject) && f->has_flag("out") && !f->has_flag("in"))
                        error(f, "a formal object's mode must be in or in out");
                    if (f->is(construct::FormalObject))
                    {
                        // (its subtype: a subtype mark (no constraint); a default only for mode in (12.4(2/3), 12.4(5)))
                        node* mark = nullptr;
                        node* defaultExpression = nullptr;
                        for (auto* c : f->children)
                            if (c->is(construct::ConstraintRange) || c->is(construct::ConstraintDigits) || c->is(construct::ConstraintDelta) ||
                                c->is(construct::ConstraintIndex))
                                error(c, "a formal object's subtype must be a subtype mark (without a constraint)");
                            else if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                                (mark == nullptr ? mark : defaultExpression) = c;
                        // (an index constraint: parsed as a name)
                        if (mark != nullptr && mark->is(construct::Apply))
                            error(mark, "a formal object's subtype must be a subtype mark (without a constraint)");
                        if (defaultExpression != nullptr && f->has_flag("out"))
                            error(defaultExpression, "a formal object of mode in out cannot have a default expression");
                        // (a null exclusion: of a subtype that does not exclude null)
                        if (mark != nullptr && f->has_flag("not null") && !mark->is(construct::AccessDefinition) && !mark->is(construct::Apply))
                        {
                            ada_type* mt = nullptr;
                            {
                                speculation guard{ *this };
                                try { mt = subtype_mark(mark, aRegion); } catch (compile_error const&) {}
                            }
                            null_exclusion_check(mt, mark);
                        }
                    }
                    // (a formal array type's component subtype: a subtype mark (12.5.3(2)))
                    if (f->is(construct::FormalType))
                        for (auto* d : f->children)
                            if (d->is(construct::TypeArray) || d->is(construct::TypeArrayUnconstrained))
                                if (auto* component = d->first(construct::ComponentDefinition))
                                    for (auto* c : component->children)
                                        if (c->is(construct::SubtypeIndication))
                                        {
                                            std::size_t parts = 0u;
                                            for (auto* x : c->children)
                                                if (!x->is(construct::Flag))
                                                    ++parts;
                                            if (parts > 1u || (parts == 1u && c->children.back()->is(construct::Apply)))
                                                error(c, "a formal array type's component subtype must be a subtype mark (without a constraint)");
                                        }
                    // (a formal constrained array type's index subtypes: subtype marks (12.5.3(2)))
                    if (f->is(construct::FormalType))
                        for (auto* d : f->children)
                            if (d->is(construct::TypeArray))
                                for (auto* index : d->children)
                                    if (!index->is(construct::ComponentDefinition) && !index->is(construct::Identifier) && !index->is(construct::Selected) &&
                                        !(index->is(construct::SubtypeIndication) && index->size() == 1u &&
                                            (index->child(0)->is(construct::Identifier) || index->child(0)->is(construct::Selected))))
                                        error(index, "a formal array type's index subtypes must be subtype marks");
                    // (a formal private type: [abstract] tagged ... in that order (12.5(2.1/3), 12.5.1(2/3)))
                    if (f->is(construct::FormalType))
                        if (auto* fp = f->first(construct::FormalPrivate))
                        {
                            std::vector<std::string> words;
                            for (auto* c : fp->children)
                                if (c->is(construct::Flag))
                                    words.push_back(name_text(c->source));
                            auto const at = [&](std::string const& w) { return static_cast<int>(std::find(words.begin(), words.end(), w) - words.begin()); };
                            int const n = static_cast<int>(words.size());
                            if (at("abstract") < n && at("tagged") == n)
                                error(fp, "an abstract formal private type must be tagged");
                            else if ((at("abstract") < n && at("abstract") > at("tagged")) || (at("limited") < n && at("tagged") < n && at("tagged") > at("limited")) ||
                                (at("abstract") < n && at("abstract") > at("private")) || (at("tagged") < n && at("tagged") > at("private")) ||
                                (at("limited") < n && at("limited") > at("private")))
                                error(fp, "the reserved words of a formal private type must be in the order abstract, tagged, limited");
                        }
                    // (an abstract formal derived type: a private extension (12.5.1(5/3)))
                    if (f->is(construct::FormalType))
                        if (auto* fd = f->first(construct::FormalDerived); fd != nullptr && fd->has_flag("abstract") && !fd->has_flag("private"))
                            error(fd, "an abstract formal derived type must be a formal private extension (with private)");
                    // (a formal type's discriminants: no defaults)
                    if (f->is(construct::FormalType))
                        if (auto* discriminants = f->first(construct::Discriminants))
                            for (auto* d : discriminants->children)
                            {
                                std::size_t defining = 0u;
                                node* last = nullptr;
                                for (auto* x : d->children)
                                    if (x->is(construct::Defining))
                                        ++defining;
                                    else if (!x->is(construct::Flag))
                                        last = x;
                                std::size_t others = 0u;
                                for (auto* x : d->children)
                                    if (!x->is(construct::Defining) && !x->is(construct::Flag))
                                        ++others;
                                if (others > 1u && last != nullptr)
                                    error(last, "a formal type's discriminants cannot have defaults");
                            }
                    if (f->is(construct::FormalType) && f->first(construct::Discriminants) != nullptr)
                        for (auto* d : f->children)
                            if (d->is(construct::FormalDiscrete) || d->is(construct::FormalInteger) || d->is(construct::FormalModular) ||
                                d->is(construct::FormalFloat) || d->is(construct::FormalFixed) || d->is(construct::FormalDecimal) ||
                                d->is(construct::TypeArray) || d->is(construct::TypeArrayUnconstrained) || d->is(construct::TypeAccess) ||
                                d->is(construct::TypeAccessSubprogram) || d->is(construct::TypeInterface))
                                error(f->first(construct::Discriminants), "a formal scalar, array, access or interface type cannot have discriminants");
                });
        // (the formals' names: distinct (unless overloaded), and not redeclared in the generic's specification (the same declarative
        // region, 8.3(26/2)))
        if (formals != nullptr && iInstanceBodies == 0)
        {
            auto const designator = [](node* aDefining)
            {
                auto text = name_text(aDefining->source);
                if (text.size() >= 2u && text.front() == '"')
                    text = text.substr(1u, text.size() - 2u);
                return text;
            };
            std::set<std::string> objects;
            std::set<std::string> subprograms;
            for (auto* f : formals->children)
            {
                // (a program unit pragma: not in a generic formal part (10.1.5(5/1)))
                if (f->is(construct::Pragma) && !f->children.empty())
                    if (auto const pn = name_text(f->child(0)->source); pn == "elaborate_body" || pn == "pure" || pn == "preelaborate" ||
                        pn == "inline" || pn == "remote_types" || pn == "shared_passive" || pn == "remote_call_interface" || pn == "all_calls_remote")
                        recover([&]() { error(f, "pragma " + pn + " is not allowed in a generic formal part"); });
                if (f->is(construct::FormalSubprogram))
                {
                    if (auto* spec = specification_of(f); spec != nullptr && !spec->children.empty())
                    {
                        auto const n = designator(spec->child(0));
                        // (an operator symbol: one of the operators (6.1(10/3)))
                        if (spec->child(0)->is(construct::DefiningOperator))
                            recover([&]() { operator_symbol_check(spec->child(0)); });
                        if (objects.contains(n))
                            recover([&]() { error(spec->child(0), "\"" + n + "\" is already declared in this generic formal part"); });
                        subprograms.insert(n);
                    }
                    continue;
                }
                for (auto* d : f->all(construct::Defining))
                {
                    auto const n = designator(d);
                    if (objects.contains(n) || subprograms.contains(n))
                        recover([&]() { error(d, "\"" + n + "\" is already declared in this generic formal part"); });
                    objects.insert(n);
                }
            }
            if (auto* package = aDecl->first(construct::Package))
                for (auto* part : { package->first(construct::Declarations), package->first(construct::Private) })
                    if (part != nullptr)
                        for (auto* d : part->children)
                        {
                            std::vector<node*> defining = d->all(construct::Defining);
                            if (auto* spec = specification_of(d); spec != nullptr && !spec->children.empty() && defining.empty())
                                defining.push_back(spec->child(0));
                            for (auto* x : defining)
                                if (auto const n = designator(x); objects.contains(n))
                                    recover([&]() { error(x, "\"" + n + "\" is already declared in the generic formal part"); });
                        }
        }
        register_library_unit(e);
        iGenerics.push_back(e);
        // (the generic's region (for expanded names within it))
        e->inner = new_region(e, aRegion);
    }

    ada_type* program::template_formal_type(node* aFormal, std::string const& aName, region* aFormalRegion)
    {
        // (a formal type of a generic being checked (check_generic): a type of its category with no more properties than its contract
        // gives it (12.5))
        node* definition = nullptr;
        node* discriminants = nullptr;
        for (auto* c : aFormal->children)
            if (c->is(construct::Discriminants) || c->is(construct::DiscriminantsUnknown))
                discriminants = c;
            else if (!c->is(construct::Defining) && !c->is(construct::Aspects))
                definition = c;
        auto* owner = new_entity(entity_kind::Type, aName, aFormal->first(construct::Defining), aFormalRegion);
        auto const scalar = [&](type_kind aKind)
        {
            auto* t = new_type(aKind, aName);
            t->decl = owner;
            t->first_subtype = true;
            t->declared_in = aFormalRegion;
            t->is_formal = true;
            t->size = 8u;
            if (aKind == type_kind::Float)
                t->digits = 15;
            owner->type = t;
            return t;
        };
        if (definition == nullptr || aFormal->is(construct::FormalTypeIncomplete))
        {
            auto* t = scalar(type_kind::Private);
            t->incomplete = aFormal->is(construct::FormalTypeIncomplete);
            t->tagged = aFormal->has_flag("tagged") || std::string_view{ aFormal->source }.find(" tagged") != std::string_view::npos;
            t->constrained = discriminants == nullptr;
            t->unknown_discriminants = discriminants != nullptr && discriminants->is(construct::DiscriminantsUnknown);
            return t;
        }
        switch (definition->kind)
        {
        case construct::FormalDiscrete:
            return scalar(type_kind::Enumeration);
        case construct::FormalInteger:
            return scalar(type_kind::Integer);
        case construct::FormalModular:
            return scalar(type_kind::Modular);
        case construct::FormalFloat:
            return scalar(type_kind::Float);
        case construct::FormalFixed:
            return scalar(type_kind::Fixed);
        case construct::FormalDecimal:
            return scalar(type_kind::Decimal);
        case construct::FormalPrivate:
            {
                auto* t = scalar(type_kind::Private);
                t->size.reset();
                t->limited = definition->has_flag("limited");
                t->tagged = definition->has_flag("tagged");
                t->abstract = definition->has_flag("abstract");
                t->constrained = discriminants == nullptr;
                t->unknown_discriminants = discriminants != nullptr && discriminants->is(construct::DiscriminantsUnknown);
                if (discriminants != nullptr && discriminants->is(construct::Discriminants))
                    discriminant_part(discriminants, t, new_region(owner, aFormalRegion));
                return t;
            }
        case construct::FormalDerived:
            {
                auto* mark = definition->first(construct::Identifier) != nullptr ? definition->first(construct::Identifier) :
                    definition->first(construct::Selected) != nullptr ? definition->first(construct::Selected) :
                    definition->first(construct::Attribute) != nullptr ? definition->first(construct::Attribute) : definition->first(construct::SubtypeIndication);
                if (mark == nullptr)
                    return nullptr;
                // (no known discriminant part (12.5.1(5.1/3)))
                if (discriminants != nullptr && discriminants->is(construct::Discriminants))
                    error(discriminants, "a formal derived type cannot have a known discriminant part");
                auto* parent = mark->is(construct::SubtypeIndication) ? subtype_indication(mark, aFormalRegion) : subtype_mark(mark, aFormalRegion);
                // (the ancestor: not class-wide (12.5.1(5/3)))
                if (parent != nullptr && parent->view()->kind == type_kind::ClassWide)
                    error(mark, "the ancestor of a formal derived type cannot be class-wide");
                auto* t = derive(parent, aName, owner, aFormalRegion, nullptr, discriminants);
                t->is_formal = true;
                t->declared_in = aFormalRegion;
                // (its progenitors: interfaces (3.9.4(9/2)), of compatible kinds)
                bool const limitedFormal = definition->has_flag("limited") || definition->has_flag("synchronized");
                bool const synchronizedFormal = definition->has_flag("synchronized");
                recover([&]() { progenitor_check(parent, mark, limitedFormal, synchronizedFormal, type_kind::Record); });
                for (auto* i : definition->children)
                    if (i != mark && (i->is(construct::Identifier) || i->is(construct::Selected)))
                        recover([&]() { t->interfaces.push_back(progenitor(i, aFormalRegion)); progenitor_check(t->interfaces.back(), i, limitedFormal, synchronizedFormal, type_kind::Record); });
                    else if (i->is(construct::Interfaces))
                        for (auto* j : i->children)
                            recover([&]() { t->interfaces.push_back(progenitor(j, aFormalRegion)); progenitor_check(t->interfaces.back(), j, limitedFormal, synchronizedFormal, type_kind::Record); });
                recover([&]() { progenitor_mix_check(parent, t->interfaces, definition); });
                if (std::string_view{ definition->source }.find("private") != std::string_view::npos)
                {
                    t->kind = type_kind::Private;
                    t->tagged = true;
                }
                t->abstract = definition->has_flag("abstract");
                owner->type = t;
                return t;
            }
        default:
            {
                // (an array, access or interface type: its definition (the formal hidden within it, 8.3(16)))
                ada_type* t = nullptr;
                {
                    // (in the formal region while it is declared: hiding an outer homograph too)
                    auto const where = aFormalRegion->names.emplace(aName, owner);
                    struct unlist { region* r; decltype(where) w; ~unlist() { r->names.erase(w); } } unlisted{ aFormalRegion, where };
                    declaring hiding;
                    hiding.add(owner);
                    t = type_definition(definition, aName, owner, aFormalRegion, nullptr);
                }
                if (t != nullptr)
                {
                    t->is_formal = true;
                    t->base_type()->is_formal = true;
                }
                owner->type = t;
                return t;
            }
        }
    }

    void program::check_generic(entity* aGeneric)
    {
        // (the legality of a generic unit's declaration and body: an instance whose actuals are its formals (types of their categories
        // known only by their contracts, the formal objects and subprograms themselves); not one with formal packages)
        auto* declaration = aGeneric->decl;
        if (declaration == nullptr || !declaration->is(construct::Generic))
            return;
        if (auto* formals = declaration->first(construct::GenericFormals))
            for (auto* f : formals->children)
                if (f->is(construct::FormalPackage))
                    return;
        auto const kind = aGeneric->kind == entity_kind::GenericPackage ? entity_kind::Package :
            aGeneric->kind == entity_kind::GenericFunction ? entity_kind::Function : entity_kind::Procedure;
        auto* scope = aGeneric->scope != nullptr ? aGeneric->scope : iStandard;
        auto* instance = new_entity(kind, aGeneric->name, nullptr, scope);
        bool const saved = iTemplateCheck;
        struct restore { bool& target; bool value; ~restore() { target = value; } } restoreCheck{ iTemplateCheck, saved };
        iTemplateCheck = true;
        instantiate(declaration, aGeneric, instance, scope, nullptr);
    }

    node* program::generic_body(entity* aGeneric)
    {
        auto* g = aGeneric;
        while (g->renamed != nullptr)
            g = g->renamed;
        auto existing = iGenericBodies.find(g);
        if (existing != iGenericBodies.end())
            return existing->second;
        if (g->sub != nullptr && g->sub->body != nullptr)
            return g->sub->body;
        // (a library generic's body unit)
        if (g->library)
            if (auto* u = find_unit(full_name(g), true); u != nullptr)
                return u->item->is(construct::Subunit) ? u->item->child(1) : u->item;
        // (a body declared later in the declarative part that the generic is in, or its stub's subunit)
        if (g->scope != nullptr)
        {
            auto* declaration = g->decl;
            for (auto* p = declaration != nullptr ? declaration->parent : nullptr; p != nullptr; p = p->parent)
            {
                if (!p->is(construct::Declarations) && !p->is(construct::Private))
                    continue;
                std::vector<node*> scopes = { p };
                // (the package body of the package declaration that declares it)
                for (auto* s : scopes)
                    for (auto* d : s->children)
                    {
                        // (a body stub: its subunit's proper body)
                        if (d->is(construct::Stub))
                        {
                            std::string stubName;
                            if (auto* spec = specification_of(d))
                                stubName = name_text(spec->child(0)->source);
                            else if (auto* defining = d->first(construct::Defining))
                                stubName = name_text(defining->source);
                            if (stubName == g->name)
                            {
                                std::string parent;
                                for (auto* r = g->scope; r != nullptr; r = r->parent)
                                    if (r->owner != nullptr && !r->owner->name.empty() && r->owner->name[0] != '%' && (r->owner->is_package() ||
                                        r->owner->is_subprogram() || r->owner->kind == entity_kind::Task || r->owner->kind == entity_kind::Protected))
                                    {
                                        parent = full_name(r->owner);
                                        break;
                                    }
                                for (auto const& u : iUnits)
                                    if (u.subunit && u.name == parent + "." + g->name && u.item != nullptr)
                                        return u.item->is(construct::Subunit) ? u.item->child(1) : u.item;
                            }
                        }
                        if ((d->is(construct::PackageBody) && g->kind == entity_kind::GenericPackage && name_text(d->first(construct::Defining)->source) == g->name) ||
                            ((d->is(construct::SubprogramBody) || d->is(construct::SubprogramExpression) || d->is(construct::SubprogramNull)) && g->kind != entity_kind::GenericPackage && specification_of(d) != nullptr &&
                                name_text(specification_of(d)->child(0)->source) == g->name))
                            return d;
                    }
                break;
            }
            // (in the body of the package that declares the generic)
            if (auto* owner = g->scope->owner; owner != nullptr && owner->is_package())
            {
                // (a declaration of that body: the generic's body, or its stub (its subunit's proper body))
                auto const body_in = [&](node* d) -> node*
                {
                    if (d->is(construct::Stub))
                    {
                        std::string stubName;
                        if (auto* spec = specification_of(d))
                            stubName = name_text(spec->child(0)->source);
                        else if (auto* defining = d->first(construct::Defining))
                            stubName = name_text(defining->source);
                        if (stubName == g->name)
                            for (auto const& u : iUnits)
                                if (u.subunit && u.name == full_name(owner) + "." + g->name && u.item != nullptr)
                                    return u.item->is(construct::Subunit) ? u.item->child(1) : u.item;
                        return nullptr;
                    }
                    if ((d->is(construct::PackageBody) && g->kind == entity_kind::GenericPackage && name_text(d->first(construct::Defining)->source) == g->name) ||
                        ((d->is(construct::SubprogramBody) || d->is(construct::SubprogramExpression) || d->is(construct::SubprogramNull)) && g->kind != entity_kind::GenericPackage && specification_of(d) != nullptr &&
                            name_text(specification_of(d)->child(0)->source) == g->name))
                        return d;
                    return nullptr;
                };
                for (auto& [declNode, bodyRegion] : iBodyRegions)
                    if (bodyRegion->owner == owner)
                        for (auto* d : declNode->first(construct::Declarations)->children)
                            if (auto* b = body_in(d))
                                    return b;
                // (the package body declared later in the declarative part that declares the package)
                for (auto* p = owner->decl != nullptr ? owner->decl->parent : nullptr; p != nullptr; p = p->parent)
                {
                    if (!p->is(construct::Declarations) && !p->is(construct::Private))
                        continue;
                    for (auto* b : p->children)
                        if (b->is(construct::PackageBody) && b->first(construct::Defining) != nullptr && name_text(b->first(construct::Defining)->source) == owner->name)
                            if (auto* decls = b->first(construct::Declarations))
                                for (auto* d : decls->children)
                                    if (auto* b = body_in(d))
                                    return b;
                    break;
                }
                if (owner->library)
                    if (auto* u = find_unit(full_name(owner), true); u != nullptr)
                        if (auto* decls = u->item->first(construct::Declarations))
                            for (auto* d : decls->children)
                                if (auto* b = body_in(d))
                                    return b;
            }
        }
        return nullptr;
    }

    bool program::statically_matching(ada_type* a, ada_type* b)
    {
        // (subtypes that statically match (4.9.1(1.2/2)): of the same type, with statically matching constraints (or none))
        if (a == nullptr || b == nullptr)
            return a == b;
        if (a == b || a->view() == b->view())
            return true;
        auto* x = a->view();
        auto* y = b->view();
        if (!same_type(x, y))
            return false;
        if (x->is_scalar())
        {
            auto const same = [](static_value const& p, static_value const& q)
            {
                if (!p.known() || !q.known())
                    return false;
                return p.is_integer() && q.is_integer() ? p.integer() == q.integer() : p.real() == q.real();
            };
            // (bounds not static: the same subtype (its bounds' hidden constants))
            if (x->low_object != nullptr && x->low_object == y->low_object && x->high_object == y->high_object)
                return true;
            return same(x->low, y->low) && same(x->high, y->high);
        }
        // (access subtypes: both or neither excluding null, of statically matching designated subtypes (4.9.1(2/3)))
        if (x->kind == type_kind::Access)
        {
            if (!a->anonymous_access && !b->anonymous_access && (a->not_null || x->not_null) != (b->not_null || y->not_null))
                return false;
            if (x->designated == y->designated || x->designated == nullptr || y->designated == nullptr)
                return true;
            auto* dx = x->designated->view();
            auto* dy = y->designated->view();
            if (dx->kind == type_kind::Array || dy->kind == type_kind::Array || !dx->discriminant_values.empty() || !dy->discriminant_values.empty())
                return statically_matching(x->designated, y->designated);
            return true;
        }
        if (x->kind == type_kind::Array)
        {
            if (x->constrained != y->constrained || x->indexes.size() != y->indexes.size())
                return false;
            if (!x->constrained)
                return true;
            for (std::size_t i = 0u; i < x->indexes.size(); ++i)
                if (!statically_matching(x->indexes[i], y->indexes[i]))
                    return false;
            return true;
        }
        if (x->constrained != y->constrained)
            return false;
        if (x->discriminant_values.size() != y->discriminant_values.size())
            return false;
        for (std::size_t i = 0u; i < x->discriminant_values.size(); ++i)
        {
            auto const& p = x->discriminant_values[i];
            auto const& q = y->discriminant_values[i];
            if (!p.known() || !q.known() || p.integer() != q.integer())
                return false;
        }
        return true;
    }

    std::string program::formal_type_mismatch(node* aFormal, ada_type* aActual, region* aFormalRegion, region* aRegion)
    {
        // (the matching rules for formal types (12.5.1-12.5.5): a message if the actual subtype doesn't match the formal)
        if (aActual == nullptr)
            return {};
        // (the actual's view at the instantiation: a private type's partial view outside the scope of its full view)
        auto* t = aActual;
        if (aActual->kind == type_kind::Private && aActual->full != nullptr)
        {
            auto* package = aActual->base_type()->declared_in != nullptr ? aActual->base_type()->declared_in->owner : nullptr;
            if (package == nullptr || !private_part_visible(package, aRegion))
                t = aActual;
            else
                t = aActual->view();
        }
        else
            t = aActual->view();
        auto* base = t->base_type();
        auto const kind = base->kind == type_kind::Private && base->full != nullptr && t != aActual->view() ? type_kind::Private : t->base_type()->view()->kind;
        bool const privateView = t->kind == type_kind::Private && t->full != nullptr && t != t->view();
        auto* v = privateView ? t : t->view();
        auto const limited = [&]()
        {
            auto* b = v->base_type();
            return v->limited || b->limited || b->kind == type_kind::Task || b->kind == type_kind::Protected ||
                (b->view()->kind == type_kind::ClassWide && b->view()->specific != nullptr && b->view()->specific->view()->limited) ||
                (iInstanceBodies == 0 && limited_type(aActual, aRegion));
        };
        auto const definite = [&]()
        {
            auto* b = v->view();
            if (b->kind == type_kind::ClassWide || v->unknown_discriminants || b->unknown_discriminants)
                return false;
            if (b->kind == type_kind::Array)
                return b->constrained;
            if (b->constrained || privateView)
                return !(privateView && !v->constrained);
            // (an unconstrained discriminated type: definite if its discriminants have defaults)
            for (auto const& c : b->base_type()->view()->components)
                if (c.discriminant && c.default_expression == nullptr)
                    return false;
            return true;
        };
        node* definition = nullptr;
        node* discriminants = nullptr;
        for (auto* c : aFormal->children)
            if (c->is(construct::Discriminants) || c->is(construct::DiscriminantsUnknown))
                discriminants = c;
            else if (!c->is(construct::Defining) && !c->is(construct::Aspects))
                definition = c;
        if (definition == nullptr)
            return {};
        bool const discrete = !privateView && v->is_discrete();
        switch (definition->kind)
        {
        case construct::FormalDiscrete:
            return discrete ? std::string{} : "must be a discrete type";
        case construct::FormalInteger:
            return !privateView && v->base_type()->view()->kind == type_kind::Integer ? std::string{} : "must be a signed integer type";
        case construct::FormalModular:
            return !privateView && v->base_type()->view()->kind == type_kind::Modular ? std::string{} : "must be a modular type";
        case construct::FormalFloat:
            return !privateView && v->base_type()->view()->kind == type_kind::Float ? std::string{} : "must be a floating point type";
        case construct::FormalFixed:
            return !privateView && v->base_type()->view()->kind == type_kind::Fixed ? std::string{} : "must be an ordinary fixed point type";
        case construct::FormalDecimal:
            return !privateView && v->base_type()->view()->kind == type_kind::Decimal ? std::string{} : "must be a decimal fixed point type";
        case construct::FormalPrivate:
            {
                if (!definition->has_flag("limited") && limited())
                    return "must not be limited";
                if (definition->has_flag("tagged") && !v->base_type()->view()->tagged && v->view()->kind != type_kind::ClassWide)
                    return "must be tagged";
                if (!definition->has_flag("abstract") && v->base_type()->view()->abstract && v->view()->kind != type_kind::ClassWide)
                    return "must not be abstract";
                if (discriminants == nullptr && !definite())
                    return "must be definite";
                if (discriminants != nullptr && discriminants->is(construct::Discriminants))
                {
                    // (known discriminants: an unconstrained subtype with as many discriminants, of statically matching subtypes)
                    auto* b = v->view();
                    if (b->constrained && !b->base_type()->view()->components.empty() && b != b->base_type()->view())
                        return "must be unconstrained";
                    std::vector<ada_type*> actuals;
                    for (auto const& c : b->base_type()->view()->components)
                        if (c.discriminant && !c.stored)
                            actuals.push_back(c.type);
                    std::vector<ada_type*> formals;
                    for (auto* spec : discriminants->children)
                        if (spec->is(construct::Discriminant))
                        {
                            node* mark = nullptr;
                            for (auto* c : spec->children)
                                if (!c->is(construct::Defining) && !c->is(construct::Flag) && mark == nullptr)
                                    mark = c;
                            ada_type* ft = nullptr;
                            if (mark != nullptr && !mark->is(construct::AccessDefinition))
                            {
                                speculation guard{ *this };
                                try { ft = subtype_mark(mark, aFormalRegion); } catch (compile_error const&) {}
                            }
                            for (std::size_t k = 0u, n = spec->all(construct::Defining).size(); k < n; ++k)
                                formals.push_back(ft);
                        }
                    if (formals.size() != actuals.size())
                        return "must have " + std::to_string(formals.size()) + " discriminant" + (formals.size() == 1u ? "" : "s");
                    if (b->constrained && !formals.empty())
                        return "must be unconstrained";
                    for (std::size_t k = 0u; k < formals.size(); ++k)
                        if (formals[k] != nullptr && !statically_matching(formals[k], actuals[k]))
                            return "must have discriminants of statically matching subtypes";
                }
                return {};
            }
        case construct::FormalDerived:
            {
                auto* mark = definition->first(construct::Identifier) != nullptr ? definition->first(construct::Identifier) : definition->first(construct::Selected);
                if (mark == nullptr)
                    return {};
                ada_type* ancestor = nullptr;
                {
                    speculation guard{ *this };
                    try { ancestor = subtype_mark(mark, aFormalRegion); } catch (compile_error const&) {}
                }
                if (ancestor == nullptr)
                    return {};
                auto const derived = [&](ada_type* aType, ada_type* aAncestor)
                {
                    for (auto* x = aType->view()->base_type()->view(); x != nullptr; x = x->parent != nullptr && x->parent->view()->base_type()->view() != x ?
                        x->parent->view()->base_type()->view() : nullptr)
                        if (same_type(x, aAncestor))
                            return true;
                    return false;
                };
                if (!derived(aActual, ancestor) && !is_descendant(aActual, ancestor))
                    return "must be derived from the ancestor type";
                if (definition->first(construct::Extension) != nullptr || definition->has_flag("private") || definition->has_flag("with"))
                    if (!aActual->view()->base_type()->view()->tagged && aActual->view()->kind != type_kind::ClassWide)
                        return "must be tagged";
                // (a formal derived type is indefinite if its ancestor subtype is (12.5.1(9)))
                auto* a = ancestor->view();
                bool ancestorDefinite = a->kind != type_kind::ClassWide && !ancestor->unknown_discriminants && !a->unknown_discriminants &&
                    (a->kind != type_kind::Array || a->constrained);
                if (ancestorDefinite && a->kind != type_kind::Array && !a->constrained)
                    for (auto const& c : a->base_type()->view()->components)
                        if (c.discriminant && c.default_expression == nullptr)
                            ancestorDefinite = false;
                if (discriminants == nullptr && ancestorDefinite && !definite())
                    return "must be definite";
                // (no discriminant part and a constrained ancestor subtype: the actual constrained and statically compatible with it
                // (12.5.1(7/3)))
                if (discriminants == nullptr && aActual->view()->kind != type_kind::ClassWide)
                {
                    auto* act = aActual->view();
                    if (a->is_scalar() && act->is_scalar() && a->low.known() && a->high.known() && act->low.known() && act->high.known() &&
                        !(ancestor->first_subtype && a->kind == type_kind::Float))
                    {
                        auto const less = [](static_value const& x, static_value const& y) { return x.real() < y.real(); };
                        bool const nullRange = less(act->high, act->low);
                        if (!nullRange && (less(act->low, a->low) || less(a->high, act->high)))
                            return "must be statically compatible with the ancestor subtype";
                    }
                    else if ((a->kind == type_kind::Array || (a->kind == type_kind::Record && !a->discriminant_values.empty())) &&
                        a->constrained && !a->is_formal)
                    {
                        if (!act->constrained && act->discriminant_values.empty())
                            return "must be constrained (its ancestor subtype is)";
                        // (the constraints' values (the actual can be of a type derived from the ancestor's))
                        auto const same = [](static_value const& p, static_value const& q)
                        {
                            return p.known() && q.known() && (p.is_integer() && q.is_integer() ? p.integer() == q.integer() : p.real() == q.real());
                        };
                        bool compatible = true;
                        if (a->kind == type_kind::Array)
                        {
                            compatible = a->indexes.size() == act->indexes.size();
                            for (std::size_t i = 0u; compatible && i < a->indexes.size(); ++i)
                            {
                                auto* p = a->indexes[i]->view();
                                auto* q = act->indexes[i]->view();
                                compatible = !p->low.known() || !p->high.known() || !q->low.known() || !q->high.known() ||
                                    (same(p->low, q->low) && same(p->high, q->high));
                            }
                        }
                        else
                        {
                            compatible = a->discriminant_values.size() == act->discriminant_values.size();
                            for (std::size_t i = 0u; compatible && i < a->discriminant_values.size(); ++i)
                                compatible = !a->discriminant_values[i].known() || !act->discriminant_values[i].known() ||
                                    same(a->discriminant_values[i], act->discriminant_values[i]);
                        }
                        if (!compatible)
                            return "must be statically compatible with the ancestor subtype";
                    }
                    // (an unconstrained record (with discriminants) or access-to-record ancestor subtype: the actual unconstrained
                    // (12.5.1(7/3)))
                    else if (a->kind == type_kind::Array && !a->constrained && !ancestor->constrained && act->kind == type_kind::Array &&
                        (act->constrained || aActual->constrained) && !a->is_formal)
                        return "must be unconstrained (its ancestor subtype is)";
                    else if (a->kind == type_kind::Record && a->discriminant_values.empty() && !act->discriminant_values.empty() &&
                        [&]() { for (auto const& c : a->base_type()->view()->components) if (c.discriminant && !c.stored) return true; return false; }())
                        return "must be unconstrained (its ancestor subtype is)";
                    else if (a->kind == type_kind::Access && act->kind == type_kind::Access && a->designated != nullptr && act->designated != nullptr &&
                        a->designated->discriminant_values.empty() && !act->designated->discriminant_values.empty())
                        return "must be unconstrained (its ancestor subtype is)";
                    else if (a->kind == type_kind::Access && act->kind == type_kind::Access && a->designated != nullptr &&
                        !a->designated->discriminant_values.empty())
                    {
                        // (an access subtype with a discriminant constraint)
                        auto* q = act->designated;
                        if (q == nullptr || q->discriminant_values.empty())
                            return "must be constrained (its ancestor subtype is)";
                        bool compatible = q->discriminant_values.size() == a->designated->discriminant_values.size();
                        for (std::size_t i = 0u; compatible && i < q->discriminant_values.size(); ++i)
                        {
                            auto const& p = a->designated->discriminant_values[i];
                            auto const& v = q->discriminant_values[i];
                            compatible = !p.known() || !v.known() || (p.is_integer() && v.is_integer() ? p.integer() == v.integer() : p.real() == v.real());
                        }
                        if (!compatible)
                            return "must be statically compatible with the ancestor subtype";
                    }
                }
                return {};
            }
        case construct::TypeArray:
        case construct::TypeArrayUnconstrained:
            {
                auto* b = v->view();
                if (privateView || b->kind != type_kind::Array)
                    return "must be an array type";
                bool const constrained = definition->is(construct::TypeArray);
                if (b->constrained != constrained)
                    return constrained ? "must be a constrained array type" : "must be an unconstrained array type";
                std::vector<node*> indexes;
                node* component = nullptr;
                for (auto* c : definition->children)
                    if (c->is(construct::ComponentDefinition))
                        component = c;
                    else if (!c->is(construct::Flag))
                        indexes.push_back(c);
                if (indexes.size() != b->indexes.size())
                    return "must have " + std::to_string(indexes.size()) + " dimension" + (indexes.size() == 1u ? "" : "s");
                speculation guard{ *this };
                try
                {
                    for (std::size_t k = 0u; k < indexes.size(); ++k)
                    {
                        auto* i = indexes[k];
                        auto* markNode = i->is(construct::IndexSubtype) || i->is(construct::SubtypeIndication) ? i->child(0) : i;
                        auto* ft = subtype_mark(markNode, aFormalRegion);
                        auto* at = b->indexes[k];
                        // (the same index types; statically matching index subtypes (12.5.3(6)))
                        if (!same_type(ft, at) || !statically_matching(ft, at))
                            return "must have index subtypes that statically match the formal's";
                    }
                    if (component != nullptr)
                    {
                        node* ci = nullptr;
                        for (auto* c : component->children)
                            if (!c->is(construct::Flag))
                                ci = c;
                        if (ci != nullptr && !ci->is(construct::AccessDefinition))
                        {
                            auto* ct = ci->is(construct::SubtypeIndication) && ci->size() == 1u ? subtype_mark(ci->child(0), aFormalRegion) : nullptr;
                            if (ct != nullptr && !statically_matching(ct, b->component))
                                return "must have a component subtype that statically matches the formal's";
                        }
                        if (component->has_flag("aliased") && !b->aliased_components)
                            return "must have aliased components (as the formal has)";
                    }
                }
                catch (compile_error const&)
                {
                }
                return {};
            }
        case construct::TypeAccess:
            {
                auto* b = v->view();
                if (privateView || b->kind != type_kind::Access)
                    return "must be an access-to-object type";
                // (access-to-constant if and only if the formal is; a general access-to-variable type if the formal is (12.5.4(3)))
                if (definition->has_flag("constant") != b->access_constant)
                    return definition->has_flag("constant") ? "must be an access-to-constant type" : "must not be an access-to-constant type";
                if (definition->has_flag("all") && !b->access_all)
                    return "must be a general access-to-variable type";
                if (auto* ind = definition->first(construct::SubtypeIndication); ind != nullptr && ind->size() == 1u)
                {
                    speculation guard{ *this };
                    try
                    {
                        auto* dt = subtype_mark(ind->child(0), aFormalRegion);
                        auto* designated = b->base_type()->view()->designated != nullptr ? b->base_type()->view()->designated : b->designated;
                        if (dt != nullptr && designated != nullptr && !statically_matching(dt, designated))
                            return "must have a designated subtype that statically matches the formal's";
                    }
                    catch (compile_error const&)
                    {
                    }
                }
                return {};
            }
        case construct::TypeAccessSubprogram:
            return !privateView && v->view()->kind == type_kind::AccessSubprogram ? std::string{} : "must be an access-to-subprogram type";
        case construct::TypeInterface:
            return !privateView && v->view()->kind == type_kind::Interface ? std::string{} : "must be an interface type";
        default:
            return {};
        }
    }

    void program::instantiation(node* aDecl, region* aRegion)
    {
        // (Pre and Post: not of an instance (6.1.1))
        if (auto* aspects = aDecl->first(construct::Aspects); aspects != nullptr && iInstanceBodies == 0)
            for (auto* aspect : aspects->children)
                if (auto const a = name_text(aspect->child(0)->source); a == "pre" || a == "post")
                    recover([&]() { error(aspect, "aspect " + a + " cannot be specified for an instance"); });
        auto* defining = aDecl->first(construct::Defining);
        if (defining == nullptr)
            defining = aDecl->first(construct::DefiningOperator);
        node* name = nullptr;
        for (auto* c : aDecl->children)
            if (c != defining && !c->is(construct::Flag) && !c->is(construct::Aspects))
                name = c;
        node* genericName = name->is(construct::Apply) ? name->child(0) : name;
        // (its actuals: a conditional, quantified or declare expression parenthesized unless it is the only one (4.5.7(7/3)))
        if (name->is(construct::Apply) && iInstanceBodies == 0)
            for (std::size_t i = 1u; i < name->size(); ++i)
            {
                auto* actual = name->child(i)->is(construct::Association) ? name->child(i)->children.back() : name->child(i);
                if (actual->is(construct::IfExpression) || actual->is(construct::CaseExpression) || actual->is(construct::Quantified) ||
                    actual->is(construct::DeclareExpression))
                    parenthesis_check(actual);
            }
        auto* generic = resolve_entity(genericName, aRegion);
        while (generic != nullptr && generic->renamed != nullptr && generic->renamed->is_generic())
            generic = generic->renamed;
        if (generic == nullptr || !generic->is_generic())
            error(genericName, "generic unit name expected");
        entity_kind kind = generic->kind == entity_kind::GenericPackage ? entity_kind::Package :
            generic->kind == entity_kind::GenericFunction ? entity_kind::Function : entity_kind::Procedure;
        auto* instance = declare(kind, defining, aRegion);
        instance->generic = generic;
        aDecl->ent = instance;
        register_library_unit(instance);
        // (a generic child unit named through an instance of its parent: instantiated as the instance's child (10.1.1(19)))
        entity* parentInstance = nullptr;
        if (genericName->is(construct::Selected))
            if (auto* p = resolve_entity(genericName->child(0), aRegion); p != nullptr && p->generic != nullptr && generic->scope != nullptr &&
                generic->scope->owner == p->generic)
                parentInstance = p;
        // (named by its simple name within an instance of its parent: that instance)
        if (parentInstance == nullptr && genericName->is(construct::Identifier) && generic->scope != nullptr && generic->scope->owner != nullptr &&
            generic->scope->owner->is_generic())
            for (region* jump = nullptr, *r = aRegion; r != nullptr && parentInstance == nullptr; r = outward(r, jump))
                if (r->owner != nullptr && r->owner->is_package() && r->owner->generic == generic->scope->owner)
                    parentInstance = r->owner;
        instantiate(aDecl, generic, instance, aRegion, parentInstance);
        // (its actuals: no deferred constant before its full declaration (7.4(9/5)))
        if (name->is(construct::Apply) && iInstanceBodies == 0)
        {
            std::function<node*(node*)> deferred = [&](node* n) -> node*
            {
                if (n->ent != nullptr && n->ent->kind == entity_kind::Constant && n->ent->initial == nullptr && !n->ent->completed &&
                    n->ent->completion == nullptr && !n->ent->imported && n->ent->renamed == nullptr && !(n->ent->decl != nullptr && n->ent->decl->parent != nullptr &&
                        n->ent->decl->parent->is(construct::FormalObject)) && n->ent->renamed_name == nullptr &&
                    n->is(construct::Identifier))
                    return n;
                for (auto* c : n->children)
                    if (auto* x = deferred(c))
                        return x;
                return nullptr;
            };
            for (std::size_t i = 1u; i < name->size(); ++i)
                if (auto* x = deferred(name->child(i)))
                    recover([&]() { error(x, "the deferred constant \"" + x->ent->name + "\" is used before its full declaration"); });
        }
        // (a library subprogram instance: elaborated (its formal objects' values))
        if (instance->library && instance->is_subprogram())
            iPackageElaborations.emplace_back(aDecl, aRegion);
        // (a subprogram instance can be a primitive operation (overriding an inherited one))
        if (instance->is_subprogram() && instance->sub != nullptr)
        {
            bool const overridesImplicit = implicit_homograph(instance, aRegion);
            add_primitive_if(instance, aRegion);
            implicit_inequality(instance, aRegion);
            if (iInstanceBodies == 0)
                recover([&]() { overriding_indicator_check(instance, aDecl, overridesImplicit, aRegion); });
        }
    }

    region* program::generic_context(entity* aGeneric)
    {
        // (the context clauses of the library unit that declares a generic (its specification's and its body's), with the current
        // unit's: visible in an instance of it)
        entity* library = nullptr;
        for (auto* e = aGeneric; e != nullptr && library == nullptr; e = e->scope != nullptr ? e->scope->owner : nullptr)
            if (e->library)
                library = e;
        if (library == nullptr)
            return nullptr;
        auto const name = full_name(library);
        auto* result = new_region(nullptr, nullptr);
        auto const merge = [&](region* aContext)
        {
            if (aContext == nullptr)
                return;
            for (auto* e : aContext->declarations)
                if (std::find(result->declarations.begin(), result->declarations.end(), e) == result->declarations.end())
                    result->add(e);
            result->used.insert(result->used.end(), aContext->used.begin(), aContext->used.end());
            result->used_types.insert(result->used_types.end(), aContext->used_types.begin(), aContext->used_types.end());
            result->used_all_types.insert(result->used_all_types.end(), aContext->used_all_types.begin(), aContext->used_all_types.end());
        };
        merge(current_context());
        for (bool body : { false, true })
            if (auto* u = find_unit(name, body); u != nullptr)
            {
                if (u->context != nullptr)
                    merge(u->context);
                else if (auto* clauses = u->unit->first(construct::Context))
                {
                    auto* r = new_region(nullptr, nullptr);
                    try { speculation guard{ *this }; apply_context(clauses, r); } catch (compile_error const&) {}
                    merge(r);
                }
            }
        return result;
    }

    void program::instantiate(node* aInstantiation, entity* aGeneric, entity* aInstance, region* aRegion, entity* aParentInstance)
    {
        // (checking a generic itself (check_generic): this instance's formals; the instances within it are ordinary)
        bool const templateCheck = iTemplateCheck;
        struct restore_check { bool& target; bool value; ~restore_check() { target = value; } } restoreCheck{ iTemplateCheck, templateCheck };
        iTemplateCheck = false;
        struct site
        {
            std::vector<node*>& sites;
            std::vector<region*>& regions;
            bool pushed;
            ~site() { if (pushed) { sites.pop_back(); regions.pop_back(); } }
        } instantiationSite{ iInstantiationSites, iInstantiationRegions, !templateCheck };
        if (!templateCheck)
        {
            iInstantiationSites.push_back(aInstantiation);
            iInstantiationRegions.push_back(aRegion);
        }
        // (the generic's units' context clauses: visible in the instance)
        auto* const savedContext = iContextOverride;
        auto* const genericContext = generic_context(aGeneric);
        struct restore { region*& target; region* value; ~restore() { target = value; } } restoreContext{ iContextOverride, savedContext };
        if (genericContext != nullptr)
            iContextOverride = genericContext;
        auto* declaration = aGeneric->decl;
        if (declaration == nullptr || !declaration->is(construct::Generic))
            error(aInstantiation, "generic declaration of \"" + aGeneric->name + "\" not found");
        // (the instance: a copy of the generic's declaration (and body) analysed with its formals denoting the actuals)
        auto* copy = clone(declaration);
        auto* bodyNode = generic_body(aGeneric);
        if (std::getenv("NEOS_ADA_DEBUG"))
            std::fprintf(stderr, "instantiate %s of %s (library %d): body %s\n", aInstance->name.c_str(), full_name(aGeneric).c_str(), aGeneric->library, bodyNode != nullptr ? "found" : "none");
        auto* bodyCopy = bodyNode != nullptr ? clone(bodyNode) : nullptr;
        auto* instanceRegion = new_region(aInstance, aRegion);
        instanceRegion->is_package = aInstance->kind == entity_kind::Package;
        instanceRegion->level = aRegion->level;
        // (the formals' region: the generic's parent region's declarations are visible)
        auto* genericParent = aParentInstance != nullptr && aParentInstance->inner != nullptr ? aParentInstance->inner : aGeneric->scope != nullptr ? aGeneric->scope : iStandard;
        auto* formalRegion = new_region(aInstance, genericParent);
        formalRegion->level = aRegion->level;
        formalRegion->generic_formals = true;
        formalRegion->visibility_cutoff = aParentInstance == nullptr ? declaration : nullptr;
        // (its Default_Storage_Pool aspect: the default pool of the access types within it (13.11.3(5.1/4)))
        if (auto* aspects = aInstantiation->first(construct::Aspects))
            for (auto* aspect : aspects->children)
            {
                if (aspect->size() >= 2u && name_text(aspect->child(0)->source) == "default_storage_pool")
                {
                    auto* pool = aspect->children.back();
                    if (!pool->is(construct::Null))
                        resolve(pool, nullptr, aRegion);
                    iDefaultPools[formalRegion].emplace_back(nullptr, pool->is(construct::Null) ? nullptr : pool);
                }
            }
        // (the actuals)
        node* name = nullptr;
        for (auto* c : aInstantiation->children)
            if (!c->is(construct::Defining) && !c->is(construct::DefiningOperator) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                name = c;
        std::vector<node*> actuals;
        if (name != nullptr && name->is(construct::Apply))
            actuals.assign(name->children.begin() + 1, name->children.end());
        std::size_t positional = 0u;
        bool othersBox = false;
        auto const actual_for = [&](std::string const& aFormal) -> node*
        {
            for (auto* a : actuals)
                if (a->is(construct::Association))
                {
                    for (auto* choice : a->child(0)->children)
                    {
                        if (choice->is(construct::Others))
                            othersBox = true;
                        else
                        {
                            // (an operator symbol: its designator, without its quotes)
                            auto text = name_text(choice->source);
                            if (text.size() >= 2u && (text.front() == '\"' || text.front() == '%'))
                                text = text.substr(1u, text.size() - 2u);
                            if (text == aFormal)
                                return a->children.back();
                        }
                    }
                }
            return nullptr;
        };
        auto const next_positional = [&]() -> node*
        {
            while (positional < actuals.size())
            {
                auto* a = actuals[positional++];
                if (!a->is(construct::Association))
                    return a;
                --positional;
                break;
            }
            return nullptr;
        };
        // (the formals in order: their entities in the formal region)
        auto* formals = copy->first(construct::GenericFormals);
        // (the associations: positional ones first, named ones naming formals (12.3(9)))
        if (!templateCheck)
        {
            std::vector<std::string> formalNames;
            for (auto* f : formals->children)
            {
                if (f->is(construct::Pragma) || f->is(construct::Use) || f->is(construct::UseType))
                    continue;
                for (auto* d : f->all(construct::Defining))
                    formalNames.push_back(name_text(d->source));
                if (f->is(construct::FormalSubprogram))
                    if (auto* spec = specification_of(f); spec != nullptr && !spec->children.empty())
                    {
                        auto text = name_text(spec->child(0)->source);
                        if (text.size() >= 2u && text.front() == '\"')
                            text = text.substr(1u, text.size() - 2u);
                        formalNames.push_back(text);
                    }
            }
            bool named = false;
            std::size_t positional = 0u;
            std::set<std::string> given;
            for (auto* a : actuals)
                if (a->is(construct::Association))
                {
                    named = true;
                    for (auto* choice : a->child(0)->children)
                        if (!choice->is(construct::Others))
                        {
                            auto text = name_text(choice->source);
                            if (text.size() >= 2u && (text.front() == '\"' || text.front() == '%'))
                                text = text.substr(1u, text.size() - 2u);
                            if (std::find(formalNames.begin(), formalNames.end(), text) == formalNames.end())
                                error(choice, "\"" + text + "\" is not a formal parameter of the generic");
                            // (overloaded formal subprograms can share a designator)
                            else if (!given.insert(text).second && std::count(formalNames.begin(), formalNames.end(), text) == 1)
                                error(choice, "the formal \"" + text + "\" is given more than one actual");
                        }
                }
                else if (named)
                    error(a, "a positional association cannot follow a named one");
                else if (positional >= formalNames.size())
                    error(a, "too many generic actual parameters");
                else
                    given.insert(formalNames[positional++]);
        }
        // (formal derived types' operations (views of their ancestors'): those of a later formal derived from one)
        std::map<entity*, std::vector<entity*>> formalOperations;
        for (auto* f : formals->children)
        {
            auto const formal = [&]()
            {
                switch (f->kind)
                {
                case construct::FormalType:
                case construct::FormalTypeIncomplete:
                    {
                        auto* d = f->first(construct::Defining);
                        auto const formalName = name_text(d->source);
                        auto* actual = actual_for(formalName);
                        if (actual == nullptr)
                            actual = next_positional();
                        ada_type* actualType = nullptr;
                        if (actual != nullptr)
                        {
                            incomplete_use use{ *this, incomplete_uses::Allowed };
                            actualType = subtype_indication(actual, aRegion);
                            // (a subtype mark (12.5(2/3)): no constraint)
                            std::size_t parts = 0u;
                            for (auto* c : actual->children)
                                if (!c->is(construct::Flag))
                                    ++parts;
                            if (iInstanceBodies == 0 && ((actual->is(construct::SubtypeIndication) && parts > 1u) ||
                                (actual->is(construct::Apply) && is_subtype_name(actual->child(0), aRegion))))
                                recover([&]() { error(actual, "the actual for a formal type must be a subtype mark (without a constraint)"); });
                        }
                        else if (auto* def = f->children.back(); def->is(construct::Identifier) || def->is(construct::Selected))
                            actualType = subtype_mark(def, formalRegion);
                        // (checking the generic itself: a type of the formal's category, known only by its contract)
                        if (actualType == nullptr && templateCheck)
                            actualType = template_formal_type(f, formalName, formalRegion);
                        if (actualType == nullptr)
                            error(aInstantiation, "no actual for formal type \"" + formalName + "\"");
                        if (actual != nullptr && f->is(construct::FormalType))
                            if (auto const mismatch = formal_type_mismatch(f, actualType, formalRegion, aRegion); !mismatch.empty())
                                error(actual, "the actual for formal type \"" + formalName + "\" " + mismatch);
                        // (an instantiation freezes its actual types (13.14(5/3)): completely defined (but a formal incomplete type's))
                        if (actual != nullptr && !f->is(construct::FormalTypeIncomplete) && not_completely_defined(actualType, aRegion))
                            error(actual, "the actual type must be completely defined (its full declaration has not occurred yet)");
                        // (a formal type specified Atomic, Atomic_Components, Volatile or Volatile_Components: the actual's aspect True too (an
                        // atomic type's Volatile; its own or an ancestor's) (C.6(8/3, 12.1/5)))
                        if (auto* aspects = f->first(construct::Aspects); aspects != nullptr && actualType != nullptr)
                            for (auto* aspect : aspects->children)
                            {
                                auto const name = name_text(aspect->child(0)->source);
                                auto* value = aspect->size() > 1u && aspect->children.back() != aspect->child(0) ? aspect->children.back() : nullptr;
                                if ((name != "atomic" && name != "volatile" && name != "atomic_components" && name != "volatile_components") ||
                                    (value != nullptr && name_text(value->source) != "true"))
                                    continue;
                                bool specified = false;
                                for (auto* t = actualType->view()->base_type()->view(); t != nullptr && !specified;
                                    t = t->parent != nullptr && t->parent != t ? t->parent->view()->base_type()->view() : nullptr)
                                    specified = type_aspect(t, name) != nullptr ||
                                        (name == "volatile" && type_aspect(t, "atomic") != nullptr) ||
                                        (name == "volatile_components" && type_aspect(t, "atomic_components") != nullptr);
                                if (!specified)
                                    error(aInstantiation, "the actual for formal type \"" + formalName + "\" must be " +
                                        (name == "atomic" ? "atomic" : name == "volatile" ? "volatile" : name == "atomic_components" ?
                                        "specified Atomic_Components" : "specified Volatile_Components") + " (C.6(12.1/5))");
                            }
                        auto* e = declare(entity_kind::Subtype, d, formalRegion);
                        e->type = actualType;
                        // (an untagged formal type: its own subtype (of the actual), distinguishing the declarations whose profiles differ in
                        // it from others in the instance (12.3(11)))
                        if (actualType != nullptr && f->is(construct::FormalType) && !actualType->view()->base_type()->view()->tagged &&
                            actualType->view()->kind != type_kind::ClassWide)
                        {
                            auto* view = new_subtype(actualType, formalName);
                            view->formal_view = view;
                            e->type = view;
                        }
                        // (a formal type with known discriminants: its discriminants' names denote the actual's discriminants (12.5.1))
                        if (auto* discriminants = f->first(construct::Discriminants); discriminants != nullptr && actualType != nullptr)
                        {
                            std::vector<std::string> names;
                            for (auto* spec : discriminants->children)
                                if (spec->is(construct::Discriminant))
                                    for (auto* dd : spec->all(construct::Defining))
                                        names.push_back(name_text(dd->source));
                            bool renamed = false;
                            std::size_t k = 0u;
                            for (auto const& c : actualType->view()->base_type()->view()->components)
                                if (c.discriminant && c.ent != nullptr)
                                    renamed = renamed || (k < names.size() && names[k++] != c.ent->name);
                            if (renamed)
                            {
                                auto* view = new_subtype(actualType->view(), formalName);
                                view->formal_view = nullptr;
                                view->formal_discriminants = names;
                                view->formal_scope = formalRegion;
                                e->type = view;
                            }
                        }
                        // (an untagged formal type: within the instance its operators are the predefined ones (not the actual's user-defined
                        // ones), as are those its derived types inherit (12.3(15)))
                        if (actualType != nullptr && f->first(construct::FormalDerived) == nullptr && !actualType->view()->base_type()->view()->tagged)
                            formalRegion->formal_actuals.push_back(actualType->view()->base_type()->view());
                        // (a formal derived type: the operations it inherits from its ancestor (the ancestor's parameter names and defaults),
                        // denoting the actual's corresponding operations (12.5.1(21)))
                        if (auto* derived = f->first(construct::FormalDerived))
                            if (auto* ancestorMark = derived->first(construct::Identifier) != nullptr ? derived->first(construct::Identifier) : derived->first(construct::Selected))
                            {
                                auto* ancestor = subtype_mark(ancestorMark, formalRegion)->view()->base_type()->view();
                                // (a class-wide actual: its specific type's operations, its calls dispatching (12.5.1(21/3)))
                                auto* actualSpecific = actualType->view()->kind == type_kind::ClassWide && actualType->view()->specific != nullptr ?
                                    actualType->view()->specific : actualType;
                                auto* actualBase = actualSpecific->view()->base_type()->view();
                                // (an ancestor that is an earlier formal derived type: its operations (views of its ancestor's, 12.5.1(21/3)))
                                auto* ancestorFormal = resolve_entity(ancestorMark, formalRegion);
                                auto const viaFormal = ancestorFormal != nullptr ? formalOperations.find(ancestorFormal) : formalOperations.end();
                                if (ancestor->tagged)
                                    formalRegion->formal_ancestor_slots.emplace_back(actualBase, std::max(ancestor->slots.size(), ancestor->partial != nullptr ? ancestor->partial->slots.size() : 0u));
                                std::vector<entity*> operations = ancestor->primitives;
                                if (ancestor->partial != nullptr)
                                    for (auto* op : ancestor->partial->primitives)
                                        if (std::find(operations.begin(), operations.end(), op) == operations.end())
                                            operations.push_back(op);
                                if (viaFormal != formalOperations.end() && !ancestor->tagged)
                                    operations = viaFormal->second;
                                auto& ownOperations = formalOperations[e];
                                std::vector<entity*> actualOperations = actualBase->primitives;
                                if (actualBase->partial != nullptr)
                                    for (auto* op : actualBase->partial->primitives)
                                        if (std::find(actualOperations.begin(), actualOperations.end(), op) == actualOperations.end())
                                            actualOperations.push_back(op);
                                auto const substitute = [&](ada_type* aType) { return aType != nullptr && same_type(aType, ancestor) ? actualSpecific : aType; };
                                for (auto* op : operations)
                                {
                                    if (op->sub == nullptr || op->sub->op == intrinsic::NotEqual)
                                        continue;
                                    // (the actual's operation: the one in the same slot (tagged); an untagged ancestor's own operation (that a formal
                                    // ancestor's view denotes))
                                    entity* target = ancestor->tagged ? nullptr : (op->sub->renames != nullptr && viaFormal != formalOperations.end() ? op->sub->renames : op);
                                    for (auto* a : ancestor->tagged ? actualOperations : std::vector<entity*>{})
                                        if (a->name == op->name && a->sub != nullptr && a->sub->parameters.size() == op->sub->parameters.size())
                                        {
                                            if (op->sub->slot >= 0 && a->sub->slot == op->sub->slot)
                                            {
                                                target = a;
                                                break;
                                            }
                                            bool same = true;
                                            for (std::size_t i = 0u; i < a->sub->parameters.size() && same; ++i)
                                                same = same_type(a->sub->parameters[i].type, substitute(op->sub->parameters[i].type));
                                            if (same && (a->sub->result == nullptr) == (op->sub->result == nullptr) &&
                                                (a->sub->result == nullptr || same_type(a->sub->result, substitute(op->sub->result))))
                                                target = target != nullptr ? target : a;
                                        }
                                    if (target == nullptr)
                                        continue;
                                    auto* alias = new_entity(op->kind, op->name, op->decl, formalRegion);
                                    alias->sub->parameters = op->sub->parameters;
                                    for (auto& p : alias->sub->parameters)
                                        p.type = substitute(p.type);
                                    alias->sub->result = substitute(op->sub->result);
                                    alias->type = alias->sub->result;
                                    alias->sub->renames = target;
                                    alias->sub->slot = target->sub->slot;
                                    alias->sub->controlling = target->sub->controlling;
                                    alias->sub->op = target->sub->op;
                                    alias->sub->operator_type = target->sub->operator_type;
                                    alias->sub->level = target->sub->level;
                                    alias->sub->defined = target->sub->defined;
                                    formalRegion->add(alias);
                                    ownOperations.push_back(alias);
                                }
                            }
                        break;
                    }
                case construct::FormalObject:
                    {
                        bool const inOut = f->has_flag("out");
                        ada_type* formalType = nullptr;
                        node* defaultExpression = nullptr;
                        for (auto* c : f->children)
                        {
                            if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects) || c->is(construct::ConstraintRange) ||
                                c->is(construct::ConstraintDigits) || c->is(construct::ConstraintDelta) || c->is(construct::ConstraintIndex))
                                continue;
                            if (formalType == nullptr)
                                formalType = c->is(construct::AccessDefinition) ? access_definition(c, formalRegion) : subtype_mark(c, formalRegion);
                            else
                                defaultExpression = c;
                        }
                        if (formalType != nullptr && f->has_flag("not null") && formalType->view()->is_access() && !formalType->view()->not_null)
                        {
                            // (a null exclusion)
                            formalType = new_subtype(formalType->view());
                            formalType->not_null = true;
                        }
                        // (a formal object of mode in: not of an abstract type (3.9.3(8/3)))
                        if (templateCheck && !inOut && abstract_type(formalType, formalRegion))
                            recover([&]() { error(f, "a formal object of mode in cannot be of an abstract type"); });
                        // (checking the generic itself: the formal objects are objects of their subtypes; a default is resolved)
                        if (templateCheck)
                        {
                            declaring hiding;
                            for (auto* d : f->all(construct::Defining))
                            {
                                auto* e = declare(inOut ? entity_kind::Variable : entity_kind::Constant, d, formalRegion);
                                e->type = formalType;
                                hiding.add(e);
                            }
                            if (defaultExpression != nullptr && formalType != nullptr)
                                resolve(defaultExpression, formalType, formalRegion);
                            break;
                        }
                        for (auto* d : f->all(construct::Defining))
                        {
                            auto const formalName = name_text(d->source);
                            auto* actual = actual_for(formalName);
                            if (actual == nullptr)
                                actual = next_positional();
                            bool const useDefault = actual == nullptr;
                            if (useDefault)
                                actual = defaultExpression;
                            if (actual == nullptr)
                                error(aInstantiation, "no actual for formal object \"" + formalName + "\"");
                            auto* e = declare(inOut ? entity_kind::Variable : entity_kind::Constant, d, formalRegion);
                            e->type = formalType;
                            resolve(actual, formalType, useDefault ? formalRegion : aRegion);
                            // (the actual of an in out formal object: a variable (12.4(7)))
                            if (inOut && !is_variable(actual))
                                error(actual, "the actual for an in out formal object must be a variable");
                            // (nor a subcomponent that depends on discriminants of an unconstrained variable (12.4(9.3/2) via 8.5.1(5/3)))
                            if (inOut && iInstanceBodies == 0 && discriminant_dependent(actual))
                                recover([&]() { error(actual, "the actual for an in out formal object cannot be a subcomponent that depends on discriminants of an unconstrained variable"); });
                            if (inOut)
                            {
                                // (a view of the actual: of its subtype (12.4(10/2)))
                                // (not where the formal type's discriminants are named differently (they name the actual's within the instance))
                                if (actual->type != nullptr && formalType != nullptr && same_type(actual->type, formalType) &&
                                    formalType->view()->formal_discriminants.empty())
                                    e->type = actual->type;
                                e->where = storage::Renaming;
                                e->renamed_name = actual;
                                e->renamed = actual->ent;
                                iInstanceConstants.push_back({ e, aInstance });     // (its address: evaluated at the instantiation)
                            }
                            else
                            {
                                e->initial = actual;
                                auto v = formalType != nullptr && formalType->view()->is_scalar() ? evaluate(actual, aRegion) : static_value{};
                                if (v.known())
                                    e->value = v;
                                iInstanceConstants.push_back({ e, aInstance });
                            }
                        }
                        break;
                    }
                case construct::FormalSubprogram:
                    {
                        auto* spec = specification_of(f);
                        auto* d = spec->child(0);
                        auto const kind = spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure;
                        std::string formalName = d->is(construct::DefiningOperator) ? name_text(d->source) : name_text(d->source);
                        if (formalName.size() >= 2u && formalName.front() == '\"')
                            formalName = formalName.substr(1u, formalName.size() - 2u);
                        auto* e = new_entity(kind, formalName, d, formalRegion);
                        d->ent = e;
                        auto* parameters = new_region(e, formalRegion);
                        parameters->level = formalRegion->level + 1;
                        subprogram_specification(spec, e, parameters);
                        auto* actual = actual_for(formalName);
                        if (actual == nullptr)
                            actual = next_positional();
                        entity* target = nullptr;
                        if (actual != nullptr)
                        {
                            target = resolve_subprogram_name(actual, e, aRegion);
                            e->sub->renamed_name = actual;
                            // (a prefixed view: a subprogram (its body calls the operation with the object and the parameters))
                            if (auto* call = call_of(actual); call != nullptr && call->prefixed && call->object != nullptr && actual->is(construct::Selected) &&
                                target != nullptr && target->kind != entity_kind::Entry && target->sub != nullptr &&
                                target->sub->parameters.size() == e->sub->parameters.size() + 1u)
                            {
                                prefixed_view_object(actual, aRegion);
                                parameters->is_subprogram = true;
                                e->sub->level = parameters->level;
                                e->sub->prefixed_actual = true;
                                e->sub->body = actual;
                                for (auto* root = call_of(actual)->object; root != nullptr; root = root->children.empty() ? nullptr : root->child(0))
                                    if (root->is(construct::Identifier))
                                    {
                                        if (root->ent != nullptr && root->ent->is_object())
                                            note_reference(root->ent, parameters);
                                        break;
                                    }
                                iSubprogramsToGenerate.push_back(e);
                                target = nullptr;
                            }
                        }
                        else
                        {
                            // (the default: is <> (a visible subprogram of the same name and profile at the instantiation), is null, or a name)
                            node* defaultNode = nullptr;
                            for (auto* c : f->children)
                                if (c != spec && !c->is(construct::Aspects))
                                    defaultNode = c;
                            if (templateCheck && (defaultNode == nullptr || defaultNode->is(construct::Box)))
                                ;   // (checking the generic itself: the formal subprogram)
                            else if (defaultNode != nullptr && defaultNode->is(construct::Box))
                            {
                                auto* n = &iClones.emplace_back();
                                n->kind = d->is(construct::DefiningOperator) ? construct::Operator : construct::Identifier;
                                n->source = d->source;
                                n->file = d->file;
                                target = resolve_subprogram_name(n, e, aRegion);
                                e->sub->renamed_name = n;
                            }
                            else if (defaultNode != nullptr && defaultNode->is(construct::Flag))
                            {
                                e->sub->is_null = true;
                                e->sub->body = defaultNode;
                            }
                            else if (defaultNode != nullptr)
                            {
                                target = resolve_subprogram_name(defaultNode, e, formalRegion);
                                e->sub->renamed_name = defaultNode;
                            }
                            else
                                error(aInstantiation, "no actual for formal subprogram \"" + formalName + "\"");
                        }
                        if (target != nullptr)
                        {
                            e->sub->renames = target;
                            if (target->sub != nullptr)
                            {
                                e->sub->op = target->sub->op;
                                e->sub->operator_type = target->sub->operator_type;
                                e->sub->import_name = target->sub->import_name;
                                // (a nonabstract formal subprogram's actual: not abstract (12.6(8.5/2)))
                                bool const formalAbstract = f->has_flag("abstract") || std::string_view{ f->source }.find(" abstract") != std::string_view::npos;
                                if (!formalAbstract && target->sub->is_abstract && !templateCheck)
                                    recover([&]() { error(actual != nullptr ? actual : aInstantiation, "the actual for a nonabstract formal subprogram cannot be abstract"); });
                                // (a formal abstract subprogram: a dispatching operation (its calls with class-wide actuals dispatch,
                                // 12.6(8.4/3)))
                                if ((f->has_flag("abstract") || std::string_view{ f->source }.find(" abstract") != std::string_view::npos) &&
                                    target->sub->slot >= 0 && target->sub->controlling != nullptr)
                                {
                                    e->sub->slot = target->sub->slot;
                                    e->sub->controlling = target->sub->controlling;
                                }
                            }
                        }
                        formalRegion->add(e);
                        break;
                    }
                case construct::FormalPackage:
                    {
                        auto* d = f->first(construct::Defining);
                        auto const formalName = name_text(d->source);
                        auto* actual = actual_for(formalName);
                        if (actual == nullptr)
                            actual = next_positional();
                        if (actual == nullptr)
                            error(aInstantiation, "no actual for formal package \"" + formalName + "\"");
                        auto* target = resolve_entity(actual, aRegion);
                        auto* e = declare(entity_kind::Package, d, formalRegion);
                        e->renamed = target;
                        e->inner = target != nullptr ? target->inner : nullptr;
                        e->generic = target != nullptr ? target->generic : nullptr;
                        break;
                    }
                case construct::Use:
                case construct::UseType:
                    use_clause(f, formalRegion);
                    break;
                case construct::Pragma:
                    break;
                default:
                    break;
                }
            };
            // (checking the generic itself: each formal declaration in turn (its errors reported, the next checked))
            if (templateCheck)
                recover(formal);
            else
                formal();
        }
        (void)othersBox;
        // (the instance's declarations and body: the formal region is the parent of its region)
        instanceRegion->parent = formalRegion;
        if (aGeneric->kind == entity_kind::GenericPackage)
        {
            auto* package = copy->first(construct::Package);
            package->first(construct::Defining)->ent = aInstance;
            // (within the instance, the generic's name denotes the instance)
            auto* alias = new_entity(entity_kind::Package, aGeneric->name, nullptr, formalRegion);
            alias->renamed = aInstance;
            alias->inner = instanceRegion;
            formalRegion->add(alias);
            aInstance->inner = instanceRegion;
            if (!templateCheck)
                iInstancePackages.insert(package);
            package_declaration(package, aRegion, aInstance);
            iInstances.push_back({ aInstance, package, nullptr, aInstantiation });
            auto const instanceIndex = iInstances.size() - 1u;     // (the body's analysis can instantiate others)
            if (bodyCopy != nullptr)
            {
                bool const saved = instanceRegion->private_visible;
                instanceRegion->private_visible = true;
                // (a generic body declared in a package body: that body's declarations are visible in the instance's body (after
                // the formals))
                region* const savedFormalParent = formalRegion->parent;
                for (auto& [declNode, enclosing] : iBodyRegions)
                    if (auto* decls = declNode->first(construct::Declarations); decls != nullptr &&
                        std::find(decls->children.begin(), decls->children.end(), bodyNode) != decls->children.end() && enclosing->parent == savedFormalParent)
                    {
                        formalRegion->parent = enclosing;
                        break;
                    }
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = bodyNode;
                auto* bodyRegion = new_region(nullptr, instanceRegion);
                bodyRegion->is_package = true;
                bodyRegion->package_body = true;
                bodyRegion->owner = aInstance;
                iBodyRegions[bodyCopy] = bodyRegion;
                bodyCopy->ent = aInstance;
                {
                    instance_body inBody{ *this, !templateCheck };
                    declarations(bodyCopy->first(construct::Declarations), bodyRegion);
                    // (the private part's incomplete types: completed there or in the body (3.10.1(3)))
                    incomplete_check(instanceRegion->private_part, "in the private part or the package body",
                        bodyCopy->first(construct::End) != nullptr ? bodyCopy->first(construct::End) : bodyCopy);
                    if (auto* handled = bodyCopy->first(construct::Handled))
                        statements(handled, bodyRegion);
                }
                formalRegion->parent = savedFormalParent;
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = declaration;
                instanceRegion->private_visible = saved;
                iInstances[instanceIndex].body = bodyCopy;
                // (a library instance's body: elaborated (its declarations, its statements) after its specification)
                if (aInstance->library)
                    iPackageElaborations.emplace_back(bodyCopy, bodyRegion);
            }
        }
        else
        {
            // (a subprogram instance: the body's copy, declared in the instance's region, is the instance (its formal objects are
            // elaborated where it is declared))
            iInstances.push_back({ aInstance, nullptr, nullptr, aInstantiation });
            auto* spec = specification_of(copy);
            auto* parameters = new_region(aInstance, instanceRegion);
            parameters->is_subprogram = true;
            parameters->level = aRegion->level + 1;
            subprogram_specification(spec, aInstance, parameters);
            aInstance->sub->level = parameters->level;
            // (an operator instance: its profile (6.6))
            if (aInstance->decl != nullptr && aInstance->decl->is(construct::DefiningOperator) && iInstanceBodies == 0)
                recover([&]() { operator_profile_check(aInstance, aInstance->decl); });
            // (a generic function's result: not of an abstract type (nor an access result designating one) (3.9.3(8/3)))
            if (templateCheck && aInstance->kind == entity_kind::Function && aInstance->sub->result != nullptr)
            {
                auto* result = aInstance->sub->result;
                if (result->anonymous_access && result->designated != nullptr)
                    result = result->designated;
                if (abstract_type(result, parameters))
                    recover([&]() { error(spec, "a generic function cannot have an abstract result type"); });
            }
            if (bodyCopy != nullptr)
            {
                auto* bodySpec = specification_of(bodyCopy);
                aInstance->inner = parameters;
                // (the body's parameters are its own)
                auto* bodyParameters = new_region(aInstance, instanceRegion);
                bodyParameters->is_subprogram = true;
                bodyParameters->level = aRegion->level + 1;
                auto* own = new_entity(aInstance->kind, aInstance->name, bodySpec->child(0), instanceRegion);
                subprogram_specification(bodySpec, own, bodyParameters);
                // (checking the generic itself: its body conforms to its declaration (6.3.1))
                if (templateCheck)
                    recover([&]() { conformance_check(*aInstance->sub, *own->sub, aGeneric->name, bodySpec->child(0)); });
                aInstance->sub->parameters = own->sub->parameters;
                aInstance->inner = bodyParameters;
                bodyParameters->owner = aInstance;
                aInstance->sub->body = bodyCopy;
                aInstance->sub->spec = bodySpec;
                bodyCopy->ent = aInstance;
                auto* saved = iCurrentSubprogram;
                iCurrentSubprogram = aInstance;
                auto* alias = new_entity(aInstance->kind, aGeneric->name, nullptr, formalRegion);
                alias->sub->renames = aInstance;
                alias->sub->parameters = aInstance->sub->parameters;
                alias->sub->result = aInstance->sub->result;
                formalRegion->add(alias);
                // (a generic body declared in a package body: that body's declarations are visible in the instance's body (after the
                // formals), as for a package instance's)
                region* const savedFormalParent = formalRegion->parent;
                for (auto& [declNode, enclosing] : iBodyRegions)
                    if (auto* decls = declNode->first(construct::Declarations); decls != nullptr &&
                        std::find(decls->children.begin(), decls->children.end(), bodyNode) != decls->children.end() && enclosing->parent == savedFormalParent)
                    {
                        formalRegion->parent = enclosing;
                        break;
                    }
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = bodyNode;
                instance_body inBody{ *this, !templateCheck };
                // (a null procedure (6.7))
                if (bodyCopy->is(construct::SubprogramNull))
                    aInstance->sub->is_null = true;
                if (bodyCopy->is(construct::SubprogramExpression))
                {
                    for (auto* c : bodyCopy->children)
                        if (!c->is(construct::Flag) && c != bodySpec && !c->is(construct::Aspects))
                            aInstance->sub->expression = c;
                    resolve(aInstance->sub->expression, aInstance->sub->result, bodyParameters);
                }
                declarations(bodyCopy->first(construct::Declarations), bodyParameters);
                if (auto* handled = bodyCopy->first(construct::Handled))
                    statements(handled, bodyParameters);
                formalRegion->parent = savedFormalParent;
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = declaration;
                iCurrentSubprogram = saved;
                iSubprogramsToGenerate.push_back(aInstance);
            }
            else if (aGeneric->sub != nullptr && aGeneric->sub->op != intrinsic::None)
            {
                // (an instance of a predefined generic subprogram, e.g. Ada.Unchecked_Conversion)
                aInstance->sub->op = aGeneric->sub->op;
                aInstance->sub->import_name = aGeneric->sub->import_name;
            }
            else if (aGeneric->imported || (aGeneric->sub != nullptr && aGeneric->sub->body != nullptr && aGeneric->sub->body->is(construct::Pragma)))
            {
                aInstance->sub->op = aGeneric->sub->op;
                aInstance->sub->import_name = aGeneric->sub->import_name;
            }
            // (a nonreturning generic procedure's instances: nonreturning)
            if (aGeneric->sub != nullptr && aGeneric->sub->no_return)
                aInstance->sub->no_return = true;
            // (predefined generic subprograms: intrinsic)
            auto const genericName = full_name(aGeneric);
            if (genericName == "ada.unchecked_conversion" || genericName == "unchecked_conversion")
                aInstance->sub->op = intrinsic::UncheckedConversion;
            else if (genericName == "ada.unchecked_deallocation" || genericName == "unchecked_deallocation")
                aInstance->sub->op = intrinsic::UncheckedDeallocation;
        }
    }

    // --- subunits

    void program::body_stub(node* aDecl, region* aRegion)
    {
        if (aDecl->is(construct::Subunit))
        {
            // (a subunit: analysed in the region of its stub)
            auto* proper = aDecl->child(1);
            auto stub = iStubs.find(iCurrentUnit != nullptr ? iCurrentUnit->name : std::string{});
            region* r = stub != iStubs.end() ? stub->second : aRegion;
            if (proper == nullptr)
                return;
            // (a subunit of a generic unit's body: analysed in its instances (see the stub))
            if (stub == iStubs.end() && iCurrentUnit != nullptr)
                for (auto prefix = iCurrentUnit->name; prefix.find('.') != std::string::npos;)
                {
                    prefix = prefix.substr(0u, prefix.rfind('.'));
                    if (auto* u = library_unit(prefix); u != nullptr && u->is_generic())
                        return;
                }
            if (auto s = iStubNodes.find(iCurrentUnit != nullptr ? iCurrentUnit->name : std::string{}); s != iStubNodes.end())
                iStubBodies[s->second] = proper;
            switch (proper->kind)
            {
            case construct::SubprogramBody:
                // (a generic subprogram's body: analysed in its instances (see the stub))
                if (auto* spec = specification_of(proper))
                {
                    auto const name = name_text(spec->child(0)->source);
                    auto const dot = name.rfind('.');
                    bool generic = false;
                    // (the generic: declared in the stub's region, or in the specification of the package whose body that is)
                    for (auto* g : visible(dot == std::string::npos ? name : name.substr(dot + 1u), r))
                        if (g->is_generic() && g->sub != nullptr && g->scope != nullptr && (g->scope == r || (r->owner != nullptr && g->scope->owner == r->owner)))
                        {
                            g->sub->body = proper;
                            proper->ent = g;
                            spec->child(0)->ent = g;
                            generic = true;
                        }
                    if (generic)
                    {
                        if (iDeclaringUnit != nullptr)
                            iDeclaringUnit = nullptr;
                        break;
                    }
                }
                subprogram_body(proper, r);
                break;
            case construct::PackageBody:
                package_body(proper, r);
                break;
            case construct::TaskBody:
                task_body(proper, r);
                break;
            case construct::ProtectedBody:
                protected_body(proper, r);
                break;
            default:
                break;
            }
            return;
        }
        // (a body stub: its subunit is analysed (later) in this region; a subprogram stub declares the subprogram if it has no
        // declaration)
        std::string name;
        if (auto* spec = specification_of(aDecl))
        {
            name = name_text(spec->child(0)->source);
            // (a generic subprogram's stub: its specification names the generic's formals (it completes the generic))
            bool generic = false;
            for (auto [i, end] = aRegion->names.equal_range(name); i != end; ++i)
                generic = generic || i->second->is_generic();
            // (declared in the specification of the package whose body this is)
            for (auto* e : visible(name, aRegion))
                generic = generic || (e->is_generic() && e->scope != nullptr && aRegion->owner != nullptr && e->scope->owner == aRegion->owner);
            if (generic)
                goto named;
            auto* own = new_entity(spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure, name, spec->child(0), aRegion);
            auto* parameters = new_region(own, aRegion);
            parameters->level = aRegion->level + 1;
            subprogram_specification(spec, own, parameters);
            if (auto* declared = find_completion(own, aRegion); declared == nullptr)
            {
                aRegion->add(own);
                bool const overridesImplicit = implicit_homograph(own, aRegion);
                add_primitive_if(own, aRegion);
                implicit_inequality(own, aRegion);
                if (iInstanceBodies == 0)
                    recover([&]() { overriding_indicator_check(own, aDecl, overridesImplicit, aRegion); });
            }
            else
            {
                if (iInstanceBodies == 0 && declared->sub != nullptr && (aDecl->has_flag("overriding") || aDecl->has_flag("not overriding")))
                    recover([&]() { overriding_indicator_check(declared, aDecl, implicit_homograph(declared, aRegion), aRegion); });
                completion_aspects_check(aDecl);
            }
        }
        else if (auto* d = aDecl->first(construct::Defining))
            name = name_text(d->source);
    named:
        // (the subunit's full name: the enclosing units' names and this)
        std::string parent;
        entity* instance = nullptr;
        for (auto* r = aRegion; r != nullptr; r = r->parent)
            if (r->owner != nullptr && !r->owner->name.empty() && r->owner->name[0] != '%' && (r->owner->is_package() || r->owner->is_subprogram() ||
                r->owner->kind == entity_kind::Task || r->owner->kind == entity_kind::Protected))
            {
                parent = full_name(r->owner);
                if (r->owner->generic != nullptr)
                {
                    instance = r->owner;
                    parent = full_name(r->owner->generic);
                }
                break;
            }
        // (a stub in an instance's body: its subunit's proper body analysed here, in this instance (the generic's subunit))
        if (instance != nullptr)
        {
            for (auto const& u : iUnits)
                if (u.subunit && u.name == parent + "." + name && u.item != nullptr && u.item->is(construct::Subunit) && u.item->child(1) != nullptr)
                {
                    auto* proper = clone(u.item->child(1));
                    iStubBodies[aDecl] = proper;
                    switch (proper->kind)
                    {
                    case construct::SubprogramBody:
                        subprogram_body(proper, aRegion);
                        break;
                    case construct::PackageBody:
                        package_body(proper, aRegion);
                        break;
                    case construct::TaskBody:
                        task_body(proper, aRegion);
                        break;
                    case construct::ProtectedBody:
                        protected_body(proper, aRegion);
                        break;
                    default:
                        break;
                    }
                    return;
                }
        }
        iStubs[parent + "." + name] = aRegion;
        iStubNodes[parent + "." + name] = aDecl;
        // (a generic subprogram's stub: its subunit's body is the generic's (instantiations before the subunit is analysed use it))
        if (auto* spec = specification_of(aDecl))
            for (auto* g : visible(name, aRegion))
                if (g->is_generic() && g->sub != nullptr && g->scope != nullptr && (g->scope == aRegion || g->scope->owner == aRegion->owner))
                    for (auto const& u : iUnits)
                        if (u.subunit && u.name == parent + "." + name && u.item != nullptr)
                        {
                            auto* proper = u.item->is(construct::Subunit) ? u.item->child(1) : u.item;
                            if (proper != nullptr && proper->is(construct::SubprogramBody))
                                g->sub->body = proper;
                        }
        (void)0;
    }
}
