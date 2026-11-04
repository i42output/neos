/*
  tasks.cpp

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

// Ada: the analysis of task and protected units

#include <neolib/neolib.hpp>
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

    void program::complete_partial_view(entity* aEntity, ada_type* aType, node* aDecl, region* aRegion)
    {
        // (the completion of a partial or incomplete view declared earlier in the region, or (an incomplete one) in the private part of
        // the package whose body the region is (3.10.1(3)))
        for (auto* r = aRegion; r != nullptr; r = r->package_body ? r->parent : nullptr)
        {
            auto range = r->names.equal_range(aEntity->name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second != aEntity && i->second->is_type() && i->second->type != nullptr && i->second->type->kind == type_kind::Private &&
                    i->second->type->full == nullptr && (r == aRegion || i->second->type->incomplete))
                {
                    // (a tagged incomplete type: completed by a tagged type (3.10.1(2.1/2)))
                    if (i->second->type->incomplete && i->second->type->tagged && aType->interfaces.empty())
                        error(aDecl, "the completion of a tagged incomplete type must be a tagged type");
                    // (a tagged partial view completed by a task or protected type: a synchronized private extension (7.3(7.2/3)))
                    if (!i->second->type->incomplete && iInstanceBodies == 0 && i->second->decl != nullptr && i->second->decl->parent != nullptr)
                    {
                        auto* declaration = i->second->decl->parent;
                        auto* derived = declaration->first(construct::TypeDerived);
                        bool const synchronized = declaration->has_flag("synchronized") || (derived != nullptr && derived->has_flag("synchronized"));
                        if (!synchronized && i->second->type->tagged)
                            recover([&]() { error(aDecl, "a partial view that is not a synchronized private extension cannot be completed by a task or protected type (7.3(7.2/3))"); });
                        else if (i->second->type->tagged && !same_interfaces(i->second->type, aType))
                            recover([&]() { error(aDecl, "a tagged partial view and its full view must be descended from the same interfaces (7.3(7.3/2))"); });
                    }
                    i->second->type->full = aType;
                    i->second->completed = true;
                    aType->partial = i->second->type;
                }
            if (!r->package_body)
                break;
        }
    }

    void program::entry_declaration(node* aEntry, region* aRegion)
    {
        auto* d = aEntry->first(construct::Defining);
        auto* e = declare(entity_kind::Entry, d, aRegion);
        aEntry->ent = e;
        auto* parameters = new_region(e, aRegion);
        parameters->level = aRegion->level + 1;
        // (the parameters: a specification's)
        node spec{ construct::ProcedureSpecification, aEntry->source };
        for (auto* c : aEntry->children)
            if (c->is(construct::Parameter))
                spec.children.push_back(c);
        subprogram_specification(&spec, e, parameters);
        e->sub->spec = aEntry;
        e->sub->level = parameters->level;
        // (implementing a progenitor's procedure (9.4(11.1/3), 9.1(9.2/3)))
        if (auto* owner = aRegion->owner; owner != nullptr && (owner->kind == entity_kind::Protected || owner->kind == entity_kind::Task) && iInstanceBodies == 0)
            recover([&]() { synchronized_operation_check(e, aEntry, owner); });
        // (a task's entry: no access parameter (9.5.2(13/2)))
        if (aRegion->owner != nullptr && (aRegion->owner->kind == entity_kind::Task || (aRegion->owner->type != nullptr &&
            aRegion->owner->type->view()->kind == type_kind::Task)) && iInstanceBodies == 0)
            for (auto* c : spec.children)
                if (auto* access = c->first(construct::AccessDefinition))
                    recover([&]() { error(access, "an entry of a task cannot have an access parameter"); });
        // (Pre'Class and Post'Class: not of an entry (6.1.1(5/3)))
        if (auto* aspects = aEntry->first(construct::Aspects); aspects != nullptr && iInstanceBodies == 0)
            for (auto* aspect : aspects->children)
                if (auto const n = name_text(aspect->child(0)->source); (n == "pre" || n == "post") && aspect->size() > 2u &&
                    aspect->child(1)->is(construct::Word) && name_text(aspect->child(1)->source) == "class")
                    recover([&]() { error(aspect, "aspect " + n + "'Class cannot be specified for an entry"); });
        // (an entry family: not overloadable (a homograph of any declaration of its name in the region); entries: not homographs of each
        // other (8.3(26/2)))
        if (iInstanceBodies == 0)
            for (auto [i, end] = aRegion->names.equal_range(e->name); i != end; ++i)
                if (auto* other = i->second; other != e && other->kind == entity_kind::Entry && other->sub != nullptr &&
                    (aEntry->first(construct::EntryFamily) != nullptr || other->sub->family_type != nullptr))
                {
                    recover([&]() { error(d, "\"" + e->name + "\" is already declared in this declarative region (an entry family)"); });
                    break;
                }
                else if (other != e && other->kind == entity_kind::Entry && other->sub != nullptr && e->sub != nullptr && type_conformant(*other->sub, *e->sub))
                {
                    recover([&]() { error(d, "\"" + e->name + "\" is already declared in this declarative region (with the same profile)"); });
                    break;
                }
        if (auto* family = aEntry->first(construct::EntryFamily))
        {
            e->sub->family_type = discrete_range(family->child(0), nullptr, aRegion);
            predicate_check(e->sub->family_type, family->child(0));
        }
    }

    void program::task_declaration(node* aDecl, region* aRegion)
    {
        auto* d = aDecl->first(construct::Defining);
        bool const single = aDecl->is(construct::TaskSingle);
        redeclaration_check(name_text(d->source), entity_kind::Task, d, aRegion);
        auto* unit = new_entity(entity_kind::Task, name_text(d->source), d, aRegion);
        auto* t = new_type(type_kind::Task, single ? std::string{} : unit->name);
        t->limited = true;
        t->unit = unit;
        t->declared_in = aRegion;
        t->constrained = true;
        unit->type = t;
        auto* r = new_region(unit, aRegion);
        r->level = aRegion->level;
        if (auto* discriminants = aDecl->first(construct::Discriminants))
            discriminant_part(discriminants, t, r);
        if (auto* interfaces = aDecl->first(construct::Interfaces))
            for (auto* i : interfaces->children)
            {
                t->interfaces.push_back(progenitor(i, aRegion));
                progenitor_check(t->interfaces.back(), i, true, true, type_kind::Task);
            }
        if (auto* definition = aDecl->first(construct::TaskDefinition))
            for (auto* item : definition->children)
            {
                if (item->is(construct::Entry))
                    recover([&]() { entry_declaration(item, r); });
                else if (item->is(construct::Private))
                    for (auto* p : item->children)
                        if (p->is(construct::Entry))
                            recover([&]() { entry_declaration(p, r); });
                        else
                            recover([&]() { declaration(p, r); });
                else
                    recover([&]() { declaration(item, r); });
            }
        if (single)
        {
            // (a single task: an object of an anonymous task type)
            auto* object = declare(entity_kind::Variable, d, aRegion);
            object->type = t;
            unit->decl = d;
            aDecl->ent = object;
            iTaskUnits[unit->name + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(aRegion))] = unit;
        }
        else
        {
            auto* e = declare(entity_kind::Type, d, aRegion);
            e->type = t;
            t->decl = e;
            t->first_subtype = true;
            aDecl->ent = e;
            // (the completion of a partial view)
            complete_partial_view(e, t, aDecl, aRegion);
        }
        iTaskUnits[unit->name + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(aRegion))] = unit;
        aspects(aDecl, unit, aRegion);
        // (its operations' aspects: resolved with the enclosing declarative part's (at its end))
        if (auto deferred = iDeferredAspects.find(r); deferred != iDeferredAspects.end())
        {
            auto& outer = iDeferredAspects[aRegion];
            outer.insert(outer.end(), deferred->second.begin(), deferred->second.end());
            iDeferredAspects.erase(r);
        }
    }

    void program::protected_declaration(node* aDecl, region* aRegion)
    {
        auto* d = aDecl->first(construct::Defining);
        bool const single = aDecl->is(construct::ProtectedSingle);
        redeclaration_check(name_text(d->source), entity_kind::Protected, d, aRegion);
        auto* unit = new_entity(entity_kind::Protected, name_text(d->source), d, aRegion);
        auto* t = new_type(type_kind::Protected, single ? std::string{} : unit->name);
        t->limited = true;
        t->unit = unit;
        t->declared_in = aRegion;
        t->constrained = true;
        unit->type = t;
        auto* r = new_region(unit, aRegion);
        r->level = aRegion->level;
        if (auto* discriminants = aDecl->first(construct::Discriminants))
            discriminant_part(discriminants, t, r);
        if (auto* interfaces = aDecl->first(construct::Interfaces))
            for (auto* i : interfaces->children)
            {
                t->interfaces.push_back(progenitor(i, aRegion));
                progenitor_check(t->interfaces.back(), i, true, true, type_kind::Protected);
            }
        auto const item = [&](node* aItem)
        {
            if (aItem->is(construct::Entry))
                recover([&]() { entry_declaration(aItem, r); });
            else if (aItem->is(construct::Component))
            {
                // (a private component)
                auto* definition = aItem->first(construct::ComponentDefinition);
                ada_type* type = nullptr;
                for (auto* c : definition->children)
                    if (!c->is(construct::Flag))
                        type = c->is(construct::AccessDefinition) ? access_definition(c, r) : subtype_indication(c, r);
                node* defaultExpression = nullptr;
                for (auto* c : aItem->children)
                    if (!c->is(construct::Defining) && c != definition && !c->is(construct::Aspects))
                        defaultExpression = c;
                for (auto* cd : aItem->all(construct::Defining))
                {
                    auto* ce = declare(entity_kind::Component, cd, r);
                    ce->type = type;
                    ce->owner = unit;
                    ce->index = static_cast<std::int32_t>(t->components.size());
                    component_info info;
                    info.ent = ce;
                    info.type = type;
                    info.default_expression = defaultExpression;
                    t->components.push_back(info);
                }
                if (defaultExpression != nullptr)
                    resolve(defaultExpression, type, r);
            }
            else
                declaration(aItem, r);
        };
        if (auto* definition = aDecl->first(construct::ProtectedDefinition))
            for (auto* i : definition->children)
            {
                if (i->is(construct::Private))
                    for (auto* p : i->children)
                        recover([&]() { item(p); });
                else
                    recover([&]() { item(i); });
            }
        if (single)
        {
            auto* object = declare(entity_kind::Variable, d, aRegion);
            object->type = t;
            aDecl->ent = object;
        }
        else
        {
            auto* e = declare(entity_kind::Type, d, aRegion);
            e->type = t;
            t->decl = e;
            t->first_subtype = true;
            aDecl->ent = e;
            complete_partial_view(e, t, aDecl, aRegion);
        }
        iTaskUnits[unit->name + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(aRegion))] = unit;
        aspects(aDecl, unit, aRegion);
        // (its operations' aspects: resolved with the enclosing declarative part's (at its end))
        if (auto deferred = iDeferredAspects.find(r); deferred != iDeferredAspects.end())
        {
            auto& outer = iDeferredAspects[aRegion];
            outer.insert(outer.end(), deferred->second.begin(), deferred->second.end());
            iDeferredAspects.erase(r);
        }
    }

    entity* program::unit_named(std::string const& aName, region* aRegion, entity_kind aKind)
    {
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
        {
            auto u = iTaskUnits.find(aName + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(r)));
            if (u != iTaskUnits.end() && u->second->kind == aKind)
                return u->second;
            // (a body in a package body: the unit is declared in the package's declaration)
            if (r->owner != nullptr && r->owner->inner != nullptr && r->owner->inner != r)
            {
                auto v = iTaskUnits.find(aName + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(r->owner->inner)));
                if (v != iTaskUnits.end() && v->second->kind == aKind)
                    return v->second;
            }
        }
        return nullptr;
    }

    void program::task_body(node* aDecl, region* aRegion)
    {
        auto* d = aDecl->first(construct::Defining);
        auto* unit = unit_named(name_text(d->source), aRegion, entity_kind::Task);
        if (unit == nullptr)
            error(aDecl, "task body \"" + name_text(d->source) + "\" has no declaration");
        aDecl->ent = unit;
        d->ent = unit;
        // (the task body: a subprogram (its region's parent is the task unit's region (entries and discriminants are visible)))
        auto* body = new_entity(entity_kind::Procedure, unit->name, d, aRegion);
        body->sub->body = aDecl;
        // (its region: in the region of the body (the unit's entries and discriminants are visible in it too))
        auto* r = new_region(body, aRegion);
        for (auto const& [n, member] : unit->inner->names)
            r->names.emplace(n, member);
        r->is_subprogram = true;
        r->level = aRegion->level + 1;
        body->sub->level = r->level;
        body->owner = unit;
        unit->completion = body;
        iTaskBodies[unit] = body;
        auto* saved = iCurrentSubprogram;
        iCurrentSubprogram = body;
        declarations(aDecl->first(construct::Declarations), r);
        statements(aDecl->first(construct::Handled), r);
        iCurrentSubprogram = saved;
        iSubprogramsToGenerate.push_back(body);
    }

    void program::protected_body(node* aDecl, region* aRegion)
    {
        auto* d = aDecl->first(construct::Defining);
        auto* unit = unit_named(name_text(d->source), aRegion, entity_kind::Protected);
        if (unit == nullptr)
            error(aDecl, "protected body \"" + name_text(d->source) + "\" has no declaration");
        aDecl->ent = unit;
        d->ent = unit;
        auto* r = new_region(nullptr, aRegion);
        r->owner = unit;
        for (auto const& [n, member] : unit->inner->names)
            r->names.emplace(n, member);
        r->private_visible = true;
        r->level = aRegion->level;
        iBodyRegions[aDecl] = r;
        // (the subprograms of a protected body: no overriding indicators (they are not protected operations' declarations,
        // 9.4))
        for (auto* item : aDecl->children)
            if (item->has_flag("overriding") || item->has_flag("not overriding"))
                recover([&]() { error(item, "an overriding indicator is not allowed in a protected body"); });
        for (auto* item : aDecl->children)
        {
            if (item->is(construct::EntryBody))
            {
                if (iInstanceBodies == 0)
                    recover([&]() { end_name_check(item); });
                auto* ed = item->first(construct::Defining);
                auto const name = name_text(ed->source);
                entity* entry = nullptr;
                auto range = unit->inner->names.equal_range(name);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->kind == entity_kind::Entry)
                        entry = i->second;
                if (entry == nullptr)
                    error(item, "entry body \"" + name + "\" has no declaration");
                // (one body per entry (9.5.2))
                if (iInstanceBodies == 0 && entry->sub != nullptr && entry->sub->spec != nullptr && entry->sub->spec != item && entry->sub->spec->is(construct::EntryBody))
                    recover([&]() { error(item, "entry \"" + name + "\" already has a body"); });
                item->ent = entry;
                ed->ent = entry;
                auto* er = new_region(entry, r);
                er->is_subprogram = true;
                er->level = aRegion->level + 1;
                // (the body's parameters are the entry's (their entities are the body's own))
                node spec{ construct::ProcedureSpecification, item->source };
                spec.parent = item;
                for (auto* c : item->children)
                    if (c->is(construct::Parameter))
                        spec.children.push_back(c);
                auto defaults = entry->sub->parameters;
                subprogram_specification(&spec, entry, er);
                for (std::size_t i = 0u; i < entry->sub->parameters.size() && i < defaults.size(); ++i)
                    if (entry->sub->parameters[i].default_expression == nullptr)
                        entry->sub->parameters[i].default_expression = defaults[i].default_expression;
                entry->sub->spec = item;
                entry->inner = er;
                if (auto* index = item->first(construct::EntryIndex))
                {
                    auto* ie = declare(entity_kind::LoopParameter, index->first(construct::Defining), er);
                    ie->type = discrete_range(index->children.back(), nullptr, er);
                    predicate_check(ie->type, index->children.back());
                    entry->sub->family_index = ie;
                }
                // (an entry body: its barrier (9.5.2(5)))
                if (item->first(construct::Barrier) == nullptr && iInstanceBodies == 0)
                    recover([&]() { error(ed, "an entry body must have a barrier"); });
                if (auto* barrier = item->first(construct::Barrier))
                {
                    condition(barrier->child(0), er);
                    entry->sub->barrier = barrier->child(0);
                    // (not naming the entry's parameters (9.5.2(23/5)... a barrier cannot depend on the call's parameters))
                    if (iInstanceBodies == 0)
                    {
                        std::function<node*(node*)> parameter = [&](node* n) -> node*
                        {
                            if (n->ent != nullptr && n->ent->kind == entity_kind::Parameter && n->ent->scope == er)
                                return n;
                            for (auto* c : n->children)
                                if (auto* x = parameter(c))
                                    return x;
                            return nullptr;
                        };
                        if (auto* x = parameter(barrier->child(0)))
                            recover([&]() { error(x, "an entry barrier cannot name a formal parameter of the entry"); });
                    }
                    // (its function: called by the runtime (static link, the object, the family index))
                    auto* b = new_entity(entity_kind::Function, entry->name + "%barrier", ed, er);
                    b->sub->barrier_of = entry;
                    b->sub->result = iBoolean;
                    b->sub->level = er->level;
                    b->scope = entry->scope;
                    entry->sub->barrier_function = b;
                    iSubprogramsToGenerate.push_back(b);
                }
                entry->sub->body = item;
                entry->sub->level = er->level;
                auto* saved = iCurrentSubprogram;
                iCurrentSubprogram = entry;
                declarations(item->first(construct::Declarations), er);
                statements(item->first(construct::Handled), er);
                iCurrentSubprogram = saved;
                iSubprogramsToGenerate.push_back(entry);
            }
            else if (item->is(construct::SubprogramBody))
                subprogram_body(item, r);
            else if (item->is(construct::SubprogramDeclaration) || item->is(construct::SubprogramNull) || item->is(construct::SubprogramExpression))
                subprogram_declaration(item, r);
            else if (item->is(construct::Pragma))
                pragma(item, r);
        }
        (void)specification_of;
    }
}
