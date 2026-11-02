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
        if (auto* family = aEntry->first(construct::EntryFamily))
            e->sub->family_type = discrete_range(family->child(0), nullptr, aRegion);
    }

    void program::task_declaration(node* aDecl, region* aRegion)
    {
        auto* d = aDecl->first(construct::Defining);
        bool const single = aDecl->is(construct::TaskSingle);
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
                t->interfaces.push_back(subtype_mark(i, aRegion)->base_type());
        if (auto* definition = aDecl->first(construct::TaskDefinition))
            for (auto* item : definition->children)
            {
                if (item->is(construct::Entry))
                    entry_declaration(item, r);
                else if (item->is(construct::Private))
                    for (auto* p : item->children)
                        if (p->is(construct::Entry))
                            entry_declaration(p, r);
                        else
                            declaration(p, r);
                else
                    declaration(item, r);
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
            auto range = aRegion->names.equal_range(e->name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second != e && i->second->is_type() && i->second->type->kind == type_kind::Private && i->second->type->full == nullptr)
                {
                    i->second->type->full = t;
                    t->partial = i->second->type;
                }
        }
        iTaskUnits[unit->name + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(aRegion))] = unit;
        aspects(aDecl, unit, aRegion);
    }

    void program::protected_declaration(node* aDecl, region* aRegion)
    {
        auto* d = aDecl->first(construct::Defining);
        bool const single = aDecl->is(construct::ProtectedSingle);
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
                t->interfaces.push_back(subtype_mark(i, aRegion)->base_type());
        auto const item = [&](node* aItem)
        {
            if (aItem->is(construct::Entry))
                entry_declaration(aItem, r);
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
                        item(p);
                else
                    item(i);
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
            auto range = aRegion->names.equal_range(e->name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second != e && i->second->is_type() && i->second->type->kind == type_kind::Private && i->second->type->full == nullptr)
                {
                    i->second->type->full = t;
                    t->partial = i->second->type;
                }
        }
        iTaskUnits[unit->name + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(aRegion))] = unit;
        aspects(aDecl, unit, aRegion);
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
        for (auto* item : aDecl->children)
        {
            if (item->is(construct::EntryBody))
            {
                auto* ed = item->first(construct::Defining);
                auto const name = name_text(ed->source);
                entity* entry = nullptr;
                auto range = unit->inner->names.equal_range(name);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->kind == entity_kind::Entry)
                        entry = i->second;
                if (entry == nullptr)
                    error(item, "entry body \"" + name + "\" has no declaration");
                item->ent = entry;
                ed->ent = entry;
                auto* er = new_region(entry, r);
                er->is_subprogram = true;
                er->level = aRegion->level + 1;
                // (the body's parameters are the entry's (their entities are the body's own))
                node spec{ construct::ProcedureSpecification, item->source };
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
                    entry->sub->family_index = ie;
                }
                if (auto* barrier = item->first(construct::Barrier))
                {
                    condition(barrier->child(0), er);
                    entry->sub->barrier = barrier->child(0);
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
