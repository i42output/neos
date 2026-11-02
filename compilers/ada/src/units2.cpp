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
                    aEntity->type->packed = true;
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
            else if ((name == "static_predicate" || name == "dynamic_predicate" || name == "predicate") && aEntity->is_type() && value != nullptr)
            {
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
                // (Default_Value, Default_Component_Value: resolved here (of the type, of its component type))
                if (name == "default_value")
                    resolve(value, aEntity->type, aRegion);
                else if (name == "default_component_value" && aEntity->type->view()->kind == type_kind::Array)
                    resolve(value, aEntity->type->view()->component, aRegion);
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
                try { pt = resolve(prefix, nullptr, aRegion); } catch (compile_error const&) { pt = nullptr; }
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
        (void)formals;
        register_library_unit(e);
        // (the generic's region (for expanded names within it))
        e->inner = new_region(e, aRegion);
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
                            ((d->is(construct::SubprogramBody) || d->is(construct::SubprogramExpression)) && g->kind != entity_kind::GenericPackage && specification_of(d) != nullptr &&
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
                        ((d->is(construct::SubprogramBody) || d->is(construct::SubprogramExpression)) && g->kind != entity_kind::GenericPackage && specification_of(d) != nullptr &&
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

    void program::instantiation(node* aDecl, region* aRegion)
    {
        auto* defining = aDecl->first(construct::Defining);
        if (defining == nullptr)
            defining = aDecl->first(construct::DefiningOperator);
        node* name = nullptr;
        for (auto* c : aDecl->children)
            if (c != defining && !c->is(construct::Flag) && !c->is(construct::Aspects))
                name = c;
        node* genericName = name->is(construct::Apply) ? name->child(0) : name;
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
        // (a library subprogram instance: elaborated (its formal objects' values))
        if (instance->library && instance->is_subprogram())
            iPackageElaborations.emplace_back(aDecl, aRegion);
        // (a subprogram instance can be a primitive operation (overriding an inherited one))
        if (instance->is_subprogram() && instance->sub != nullptr)
        {
            add_primitive_if(instance, aRegion);
            implicit_inequality(instance, aRegion);
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
                    try { apply_context(clauses, r); } catch (compile_error const&) {}
                    merge(r);
                }
            }
        return result;
    }

    void program::instantiate(node* aInstantiation, entity* aGeneric, entity* aInstance, region* aRegion, entity* aParentInstance)
    {
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
                if (aspect->size() >= 2u && name_text(aspect->child(0)->source) == "default_storage_pool")
                {
                    auto* pool = aspect->children.back();
                    if (!pool->is(construct::Null))
                        resolve(pool, nullptr, aRegion);
                    iDefaultPools[formalRegion].emplace_back(nullptr, pool->is(construct::Null) ? nullptr : pool);
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
        // (formal derived types' operations (views of their ancestors'): those of a later formal derived from one)
        std::map<entity*, std::vector<entity*>> formalOperations;
        for (auto* f : formals->children)
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
                        actualType = subtype_indication(actual, aRegion);
                    else if (auto* def = f->children.back(); def->is(construct::Identifier) || def->is(construct::Selected))
                        actualType = subtype_mark(def, formalRegion);
                    if (actualType == nullptr)
                        error(aInstantiation, "no actual for formal type \"" + formalName + "\"");
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
                        if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects))
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
                        if (defaultNode != nullptr && defaultNode->is(construct::Box))
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
                bodyRegion->owner = aInstance;
                iBodyRegions[bodyCopy] = bodyRegion;
                bodyCopy->ent = aInstance;
                declarations(bodyCopy->first(construct::Declarations), bodyRegion);
                if (auto* handled = bodyCopy->first(construct::Handled))
                    statements(handled, bodyRegion);
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
            if (find_completion(own, aRegion) == nullptr)
            {
                aRegion->add(own);
                add_primitive_if(own, aRegion);
                implicit_inequality(own, aRegion);
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
