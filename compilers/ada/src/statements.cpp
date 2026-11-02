/*
  statements.cpp

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

// Ada: the analysis of statements and aggregates

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include "program.hpp"

namespace neos::compilers::ada
{
    // --- statements

    void program::statements(node* aStatements, region* aRegion)
    {
        if (aStatements == nullptr)
            return;
        if (aStatements->is(construct::Handled))
        {
            statements(aStatements->first(construct::Statements), aRegion);
            for (auto* handler : aStatements->all(construct::Handler))
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[handler] = r;
                for (auto* c : handler->children)
                {
                    if (c->is(construct::Defining))
                    {
                        auto* e = declare(entity_kind::Constant, c, r);
                        e->type = library_type("ada.exceptions", "exception_occurrence");
                        e->where = storage::None;
                        handler->ent = e;
                    }
                    else if (c->is(construct::Identifier) || c->is(construct::Selected))
                    {
                        auto* e = resolve_entity(c, aRegion);
                        if (e == nullptr || e->kind != entity_kind::Exception)
                            error(c, "exception name expected");
                    }
                    else if (c->is(construct::Statements))
                        statements(c, r);
                }
            }
            return;
        }
        // (labels are declared at the start of the statements' sequence)
        static bool const trace = std::getenv("NEOS_ADA_TRACE") != nullptr;
        for (auto* s : aStatements->children)
        {
            if (trace)
                std::fprintf(stderr, "statement: %.60s\n", std::string{ s->source.substr(0u, 60u) }.c_str());
            statement(s, aRegion);
        }
    }

    void program::iterator_specification(node* aSpecification, region* aRegion)
    {
        auto* defining = aSpecification->first(construct::Defining);
        if (aSpecification->is(construct::ForRange))
        {
            node* range = nullptr;
            for (auto* c : aSpecification->children)
                if (c != defining && !c->is(construct::Flag) && !c->is(construct::Filter))
                    range = c;
            // (an iterator: for C in Container.Iterate)
            ada_type* t = nullptr;
            bool iterator = false;
            if (range->is(construct::SubtypeIndication) && range->size() == 1u && !is_subtype_name(range->child(0), aRegion) &&
                !(range->child(0)->is(construct::Attribute) && name_text(range->child(0)->child(1)->source) == "range"))
            {
                auto const options = interpretations(range->child(0), aRegion);
                for (auto const& o : options)
                    if (o.type != nullptr && !o.type->view()->is_discrete())
                    {
                        iterator = true;
                        break;
                    }
            }
            auto* e = declare(entity_kind::LoopParameter, defining, aRegion);
            if (iterator)
            {
                auto* iteratorType = resolve(range->child(0), nullptr, aRegion);
                e->type = iterator_cursor_type(iteratorType);
                aSpecification->mark = 1u;  // (a generalized iterator)
            }
            else
            {
                t = discrete_range(range, nullptr, aRegion);
                e->type = t;
            }
            if (auto* filter = aSpecification->first(construct::Filter))
                condition(filter->child(0), aRegion);
            return;
        }
        // (for X of Array_Or_Container)
        node* iterable = nullptr;
        node* subtype = nullptr;
        for (auto* c : aSpecification->children)
            if (c != defining && !c->is(construct::Flag) && !c->is(construct::Filter))
            {
                if (iterable != nullptr)
                    subtype = iterable;
                iterable = c;
            }
        auto* t = resolve(iterable, nullptr, aRegion);
        auto* e = declare(entity_kind::Variable, defining, aRegion);
        auto* v = t != nullptr ? t->view() : nullptr;
        if (v != nullptr && v->kind == type_kind::Access)
            v = designated_of(v)->view();
        if (v != nullptr && v->kind == type_kind::Array)
            e->type = v->component;
        else
            e->type = iterator_element_type(t);
        if (subtype != nullptr)
            subtype_indication(subtype, aRegion);
        e->where = storage::Renaming;
        e->renamed_name = iterable;
        if (auto* filter = aSpecification->first(construct::Filter))
            condition(filter->child(0), aRegion);
    }

    void program::statement(node* aStatement, region* aRegion)
    {
        switch (aStatement->kind)
        {
        case construct::NullStatement:
        case construct::Label:
            break;
        case construct::Pragma:
            pragma(aStatement, aRegion);
            break;
        case construct::Assignment:
            {
                auto* target = aStatement->child(0);
                variable_name(target);
                auto const options = interpretations(target, aRegion);
                ada_type* t = nullptr;
                for (auto const& o : options)
                    if (o.type != nullptr && o.type != iVoid)
                    {
                        t = o.type;
                        break;
                    }
                // (several: one that the value can be of (a reference's designated type, e.g. an overloaded generalized indexing))
                if (options.size() > 1u)
                {
                    auto const values = interpretations(aStatement->child(1), aRegion);
                    bool found = false;
                    // (an allocator: of an access type (whose designated type its subtype is of))
                    if (aStatement->child(1)->is(construct::Allocator))
                        for (auto const& o : options)
                            if (!found && o.type != nullptr && o.type->view()->kind == type_kind::Access)
                            {
                                t = o.type;
                                found = true;
                            }
                    for (auto const& o : options)
                    {
                        if (o.type == nullptr || o.type == iVoid || found)
                            continue;
                        auto* d = implicit_dereference(o.type);
                        for (auto const& v : values)
                            if (v.type != nullptr && (covers(o.type, v.type) || (d != nullptr && covers(d, v.type))))
                            {
                                t = covers(o.type, v.type) ? o.type : d;
                                found = true;
                                break;
                            }
                    }
                }
                t = resolve(target, t, aRegion);
                auto* saved = iTargetType;
                iTargetType = t;
                resolve(aStatement->child(1), t, aRegion);
                iTargetType = saved;
                break;
            }
        case construct::Call:
            {
                auto* name = aStatement->child(0);
                resolve(name, iVoid, aRegion);
                break;
            }
        case construct::Code:
            resolve(aStatement->child(0), nullptr, aRegion);
            break;
        case construct::If:
            for (auto* c : aStatement->children)
            {
                if (c->is(construct::Statements))
                    statements(c, aRegion);
                else if (c->is(construct::Elsif))
                {
                    condition(c->child(0), aRegion);
                    statements(c->child(1), aRegion);
                }
                else if (c->is(construct::Else))
                    statements(c->child(0), aRegion);
                else
                    condition(c, aRegion);
            }
            break;
        case construct::Case:
            {
                auto* selector = aStatement->child(0);
                auto const options = interpretations(selector, aRegion);
                ada_type* t = nullptr;
                for (auto const& o : options)
                    if (o.type != nullptr && o.type->view()->is_discrete() && !o.type->is_universal())
                        t = o.type;
                t = resolve(selector, t, aRegion);
                for (auto* alternative : aStatement->all(construct::Alternative))
                {
                    for (auto* choice : alternative->child(0)->children)
                        if (!choice->is(construct::Others))
                            choice_range(choice, t != nullptr ? t->base_type() : nullptr, aRegion);
                    statements(alternative->first(construct::Statements), aRegion);
                }
                break;
            }
        case construct::Loop:
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[aStatement] = r;
                if (auto* name = aStatement->first(construct::Word))
                {
                    auto* e = new_entity(entity_kind::Loop, name_text(name->source), name, aRegion);
                    aRegion->add(e);
                    e->inner = r;
                    r->owner = e;
                    aStatement->ent = e;
                }
                if (auto* w = aStatement->first(construct::While))
                    condition(w->child(0), r);
                if (auto* f = aStatement->first(construct::For))
                {
                    for (auto* c : f->children)
                        if (c->is(construct::ForRange) || c->is(construct::ForIterator))
                            iterator_specification(c, r);
                }
                statements(aStatement->first(construct::Statements), r);
                break;
            }
        case construct::Block:
            {
                auto* r = new_region(nullptr, aRegion);
                r->master = true;
                iStatementRegions[aStatement] = r;
                if (auto* name = aStatement->first(construct::Word))
                {
                    auto* e = new_entity(entity_kind::Block, name_text(name->source), name, aRegion);
                    aRegion->add(e);
                    e->inner = r;
                    r->owner = e;
                    aStatement->ent = e;
                }
                declarations(aStatement->first(construct::Declarations), r);
                statements(aStatement->first(construct::Handled), r);
                break;
            }
        case construct::Exit:
            for (auto* c : aStatement->children)
                if (!c->is(construct::Word))
                    condition(c, aRegion);
            break;
        case construct::Goto:
            break;
        case construct::Return:
            if (aStatement->size() > 0u)
            {
                ada_type* t = nullptr;
                auto* owner = return_owner(aRegion);
                if (owner != nullptr && owner->sub != nullptr)
                    t = owner->sub->result;
                resolve(aStatement->child(0), t, aRegion);
            }
            break;
        case construct::ReturnExtended:
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[aStatement] = r;
                auto* defining = aStatement->first(construct::Defining);
                node* typeNode = nullptr;
                node* init = nullptr;
                for (auto* c : aStatement->children)
                {
                    if (c == defining || c->is(construct::Flag) || c->is(construct::Aspects) || c->is(construct::Handled))
                        continue;
                    if (typeNode == nullptr)
                        typeNode = c;
                    else
                        init = c;
                }
                auto* t = typeNode->is(construct::AccessDefinition) ? access_definition(typeNode, aRegion) : subtype_indication(typeNode, aRegion);
                auto* e = declare(aStatement->has_flag("constant") ? entity_kind::Constant : entity_kind::Variable, defining, r);
                e->type = t;
                e->initial = init;
                aStatement->ent = e;
                if (init != nullptr)
                    resolve(init, t, aRegion);
                statements(aStatement->first(construct::Handled), r);
                break;
            }
        case construct::Raise:
            if (aStatement->size() >= 1u)
            {
                auto* e = resolve_entity(aStatement->child(0), aRegion);
                if (e == nullptr || e->kind != entity_kind::Exception)
                    error(aStatement->child(0), "exception name expected");
                if (aStatement->size() >= 2u)
                    resolve(aStatement->child(1), iString, aRegion);
            }
            break;
        case construct::Delay:
            for (auto* c : aStatement->children)
                if (!c->is(construct::Flag))
                    resolve(c, aStatement->has_flag("until") ? nullptr : iDuration, aRegion);
            break;
        case construct::Abort:
            for (auto* c : aStatement->children)
                resolve(c, nullptr, aRegion);
            break;
        case construct::Requeue:
            resolve_requeue(aStatement, aRegion);
            break;
        case construct::Accept:
            accept_statement(aStatement, aRegion);
            break;
        case construct::SelectAccept:
            for (auto* c : aStatement->children)
            {
                if (c->is(construct::SelectAlternative))
                {
                    for (auto* a : c->children)
                    {
                        if (a->is(construct::Guard))
                            condition(a->child(0), aRegion);
                        else if (a->is(construct::Statements))
                            statements(a, aRegion);
                        else if (a->is(construct::Terminate))
                            ;
                        else
                            statement(a, aRegion);
                    }
                }
                else if (c->is(construct::Statements))
                    statements(c, aRegion);
            }
            break;
        case construct::SelectTimed:
        case construct::SelectConditional:
        case construct::SelectAsynchronous:
            for (auto* c : aStatement->children)
            {
                if (c->is(construct::SelectCall) || c->is(construct::SelectDelay) || c->is(construct::SelectTrigger))
                {
                    for (auto* a : c->children)
                        if (a->is(construct::Statements))
                            statements(a, aRegion);
                        else
                            statement(a, aRegion);
                }
                else if (c->is(construct::Statements))
                    statements(c, aRegion);
            }
            break;
        case construct::Parallel:
            for (auto* c : aStatement->children)
                statements(c, aRegion);
            break;
        default:
            break;
        }
    }

    entity* program::return_owner(region* aRegion)
    {
        // (the subprogram (or entry or accept) that a return statement returns from)
        for (auto* r = aRegion; r != nullptr; r = r->parent)
            if (r->owner != nullptr && (r->owner->kind == entity_kind::Function || r->owner->kind == entity_kind::Procedure || r->owner->kind == entity_kind::Entry))
                return r->owner;
        return iCurrentSubprogram;
    }

    void program::accept_statement(node* aAccept, region* aRegion)
    {
        auto* name = aAccept->child(0);
        auto const entryName = name_text(name->source);
        // (the entry of the enclosing task (several of the name: the one whose profile is the accept's parameters'))
        std::vector<ada_type*> profile;
        for (auto* c : aAccept->children)
            if (c != name && c->is(construct::Parameter))
            {
                // (its subtype mark: the first name after its defining names and mode (a default expression follows))
                ada_type* type = nullptr;
                for (auto* t : c->children)
                    if (t->is(construct::Identifier) || t->is(construct::Selected) || t->is(construct::AccessDefinition))
                    {
                        type = t->is(construct::AccessDefinition) ? nullptr : subtype_mark(t, aRegion);
                        break;
                    }
                for (auto* d : c->all(construct::Defining))
                {
                    (void)d;
                    profile.push_back(type);
                }
            }
        auto const matches = [&](entity* aEntry)
        {
            if (aEntry->sub == nullptr || aEntry->sub->parameters.size() != profile.size())
                return false;
            for (std::size_t k = 0u; k < profile.size(); ++k)
                if (profile[k] != nullptr && aEntry->sub->parameters[k].type != nullptr && !same_type(profile[k], aEntry->sub->parameters[k].type))
                    return false;
            return true;
        };
        entity* entry = nullptr;
        for (auto* r = aRegion; r != nullptr && entry == nullptr; r = r->parent)
        {
            std::vector<entity*> candidates;
            if (r->owner != nullptr && r->owner->kind == entity_kind::Task && r->owner->inner != nullptr)
            {
                auto range = r->owner->inner->names.equal_range(entryName);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->kind == entity_kind::Entry)
                        candidates.push_back(i->second);
            }
            if (candidates.empty())
            {
                auto range = r->names.equal_range(entryName);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->kind == entity_kind::Entry)
                        candidates.push_back(i->second);
            }
            for (auto* c : candidates)
                if (entry == nullptr && matches(c))
                    entry = c;
            if (entry == nullptr && !candidates.empty())
                entry = candidates.back();
        }
        if (entry == nullptr)
            error(name, "entry \"" + entryName + "\" not found");
        // (several entries of the same name: the one whose profile matches the accept's parameters)
        name->ent = entry;
        aAccept->ent = entry;
        auto* r = new_region(entry, aRegion);
        r->owner = nullptr;
        iStatementRegions[aAccept] = r;
        r->level = aRegion->level;
        std::size_t index = 0u;
        for (auto* c : aAccept->children)
        {
            if (c == name)
                continue;
            if (c->is(construct::Parameter))
            {
                // (the accept's parameters: the entry's (their entities are the accept's own))
                for (auto* d : c->all(construct::Defining))
                {
                    auto* p = declare(entity_kind::Parameter, d, r);
                    if (index < entry->sub->parameters.size())
                    {
                        p->type = entry->sub->parameters[index].type;
                        p->index = static_cast<std::int32_t>(index);
                    }
                    ++index;
                }
            }
            else if (c->is(construct::Handled))
                statements(c, r);
            else if (c != aAccept->children.back() || !c->is(construct::Identifier) || aAccept->first(construct::Handled) == nullptr)
            {
                // (the family index)
                if (entry->sub->family_type != nullptr)
                    resolve(c, entry->sub->family_type, aRegion);
            }
        }
    }

    // --- aggregates

    bool program::in_ancestor_part(component_info const& aComponent, std::size_t aIndex, ada_type* aAncestor)
    {
        // (an extension aggregate's ancestor part's component: one of the ancestor type's (its discriminants too, unless the type
        // has new discriminants (then its own: in the aggregate)))
        if (aAncestor == nullptr)
            return false;
        auto* at = aAncestor->view();
        if (at->kind == type_kind::ClassWide)
            at = at->specific->view();
        auto const& ancestor = at->base_type()->view()->components;
        if (aComponent.ent == nullptr)
            return aIndex < ancestor.size();
        for (auto const& ac : ancestor)
            if (ac.ent == aComponent.ent || (ac.ent != nullptr && !aComponent.discriminant && ac.ent->name == aComponent.ent->name))
                return true;
        return false;
    }

    ada_type* program::resolve_aggregate(node* aAggregate, ada_type* aExpected, region* aRegion)
    {
        if (aExpected == nullptr)
            error(aAggregate, "type of aggregate cannot be determined from its context");
        auto* t = aExpected->view();
        set_meaning(aAggregate, meaning::Value);
        if (t->kind == type_kind::ClassWide)
            error(aAggregate, "aggregate of a class-wide type");
        if (aAggregate->is(construct::AggregateDelta))
        {
            resolve(aAggregate->child(0), aExpected, aRegion);
            for (std::size_t i = 1u; i < aAggregate->size(); ++i)
            {
                auto* association = aAggregate->child(i);
                auto* value = association->children.back();
                if (t->kind == type_kind::Array)
                {
                    for (auto* choice : association->child(0)->children)
                        if (choice->is(construct::Range) || is_discrete_range(choice, aRegion))
                            discrete_range(choice, t->indexes[0], aRegion);
                        else
                            resolve(choice, t->indexes[0]->base_type(), aRegion);
                    resolve(value, t->component, aRegion);
                }
                else
                {
                    ada_type* componentType = nullptr;
                    for (auto* choice : association->child(0)->children)
                        for (auto const& c : t->base_type()->view()->components)
                            if (c.ent != nullptr && c.ent->name == name_text(choice->source))
                            {
                                choice->ent = c.ent;
                                componentType = c.type;
                            }
                    resolve(value, componentType, aRegion);
                }
            }
            return aExpected;
        }
        if (t->kind == type_kind::Array)
            return resolve_array_aggregate(aAggregate, aExpected, 0u, aRegion);
        if (aAggregate->is(construct::AggregateBracket))
            return resolve_container_aggregate(aAggregate, aExpected, aRegion);
        if (t->kind != type_kind::Record && !(t->kind == type_kind::Private && t->full == nullptr))
            error(aAggregate, "aggregate is not of a composite type");
        auto* record = t->base_type()->view();
        // (an extension aggregate's ancestor part: its components are the ancestor's)
        std::size_t first = 0u;
        std::size_t ancestorComponents = 0u;
        ada_type* ancestorType = nullptr;
        if (aAggregate->is(construct::AggregateExtension))
        {
            auto* ancestor = aAggregate->child(0);
            if (is_subtype_name(ancestor, aRegion))
            {
                ancestorType = subtype_mark(ancestor, aRegion);
                set_meaning(ancestor, meaning::Type);
            }
            else
            {
                // (an expression of an ancestor type of the aggregate's type)
                auto const options = interpretations(ancestor, aRegion);
                // (a proper ancestor of the aggregate's type, preferably)
                for (auto const& o : options)
                    if (o.type != nullptr && o.type->view()->tagged && o.type->view()->kind != type_kind::ClassWide && !same_type(o.type, t) &&
                        is_descendant(t, o.type))
                    {
                        ancestorType = o.type;
                        break;
                    }
                if (ancestorType == nullptr)
                    for (auto const& o : options)
                        if (o.type != nullptr && (o.type->view()->tagged || o.type->view()->kind == type_kind::ClassWide))
                        {
                            ancestorType = o.type;
                            break;
                        }
                resolve(ancestor, ancestorType, aRegion);
                if (ancestorType == nullptr)
                    ancestorType = ancestor->type;
            }
            if (ancestorType != nullptr)
            {
                auto* at = ancestorType->view();
                if (at->kind == type_kind::ClassWide)
                    at = at->specific->view();
                ancestorComponents = at->base_type()->view()->components.size();
            }
            first = 1u;
        }
        auto& values = iAggregates[aAggregate];
        values.assign(record->components.size(), nullptr);
        std::vector<bool> present(record->components.size(), false);
        // (the components in the aggregate: all but the ancestor's (an extension aggregate; the discriminants of a type extension
        // that has new ones are its own))
        std::vector<std::size_t> order;
        for (std::size_t c = 0u; c < record->components.size(); ++c)
            if (ancestorComponents == 0u || !in_ancestor_part(record->components[c], c, ancestorType))
                order.push_back(c);
        std::size_t positional = 0u;
        // (discriminant values (static) choose the variant: the components of other variants are not in the aggregate)
        std::map<entity*, bigint> discriminantValues;
        auto const in_variant = [&](component_info const& aComponent) -> bool
        {
            for (int v = aComponent.variant; v >= 0; v = record->variants[static_cast<std::size_t>(v)].parent)
            {
                auto const& variant = record->variants[static_cast<std::size_t>(v)];
                auto d = discriminantValues.find(variant.discriminant);
                if (d == discriminantValues.end())
                    return true;
                bool selected = false;
                for (auto const& [low, high] : variant.ranges)
                    selected = selected || (d->second >= low && d->second <= high);
                if (variant.others && !selected)
                {
                    // (others: no other variant of the variant part has the value)
                    selected = true;
                    for (auto const& other : record->variants)
                        if (&other != &variant && other.discriminant == variant.discriminant && other.parent == variant.parent)
                            for (auto const& [low, high] : other.ranges)
                                if (d->second >= low && d->second <= high)
                                    selected = false;
                }
                if (!selected)
                    return false;
            }
            return true;
        };
        for (std::size_t i = first; i < aAggregate->size(); ++i)
        {
            auto* item = aAggregate->child(i);
            if (item->is(construct::Association))
            {
                auto* choices = item->child(0);
                auto* value = item->children.back();
                bool const box = value->is(construct::Box);
                for (auto* choice : choices->children)
                {
                    if (choice->is(construct::Others))
                    {
                        // (discriminants first: their values choose the variant of the other components it stands for)
                        for (auto c : order)
                            if (!present[c] && record->components[c].discriminant)
                            {
                                present[c] = true;
                                values[c] = box ? nullptr : value;
                                if (!box)
                                {
                                    resolve(value, record->components[c].type, aRegion);
                                    if (auto v = evaluate(value, aRegion); v.is_integer())
                                        discriminantValues[record->components[c].ent] = v.integer();
                                }
                            }
                        for (auto c : order)
                            if (!present[c] && in_variant(record->components[c]))
                            {
                                present[c] = true;
                                values[c] = box ? nullptr : value;
                            }
                        continue;
                    }
                    auto const name = name_text(choice->source);
                    bool found = false;
                    for (auto c : order)
                        if (record->components[c].ent != nullptr && record->components[c].ent->name == name)
                        {
                            present[c] = true;
                            values[c] = box ? nullptr : value;
                            choice->ent = record->components[c].ent;
                            found = true;
                            if (record->components[c].discriminant && !box)
                            {
                                resolve(value, record->components[c].type, aRegion);
                                auto v = evaluate(value, aRegion);
                                if (v.is_integer())
                                    discriminantValues[record->components[c].ent] = v.integer();
                            }
                        }
                    if (!found)
                        error(choice, "\"" + name + "\" is not a component of the type");
                }
            }
            else
            {
                while (positional < order.size() && (present[order[positional]] || !in_variant(record->components[order[positional]])))
                    ++positional;
                if (positional >= order.size())
                    error(item, "too many components in aggregate");
                auto const c = order[positional++];
                present[c] = true;
                values[c] = item;
                if (record->components[c].discriminant)
                {
                    resolve(item, record->components[c].type, aRegion);
                    auto v = evaluate(item, aRegion);
                    if (v.is_integer())
                        discriminantValues[record->components[c].ent] = v.integer();
                }
            }
        }
        // (the values: of their components' types (a value given for several components is resolved for each))
        for (std::size_t c = 0u; c < values.size(); ++c)
        {
            if (values[c] == nullptr)
                continue;
            auto* componentType = record->components[c].type;
            // (an anonymous access component of a type with a discriminant constraint)
            resolve(values[c], componentType, aRegion);
        }
        for (auto c : order)
            if (!present[c] && in_variant(record->components[c]) && !aAggregate->is(construct::AggregateNullRecord))
            {
                // (a component not given: allowed only with <> (its default))
                if (record->components[c].default_expression == nullptr && !record->components[c].discriminant)
                    ;
            }
        return aExpected;
    }

    ada_type* program::resolve_array_aggregate(node* aAggregate, ada_type* aType, std::size_t aDimension, region* aRegion)
    {
        auto* t = aType->view();
        auto const dimensions = t->indexes.size();
        auto* indexType = t->indexes[aDimension];
        bool const last = aDimension + 1u == dimensions;
        auto const component = [&](node* aValue)
        {
            if (last)
                resolve(aValue, t->component, aRegion);
            else if (aValue->is(construct::Aggregate) || aValue->is(construct::AggregateBracket))
                resolve_array_aggregate(aValue, aType, aDimension + 1u, aRegion);
            else if (aValue->is(construct::Operator))
            {
                // (a string literal as a subaggregate of the last dimension)
                set_meaning(aValue, meaning::StringLiteral);
                aValue->type = t->component;
            }
            else
                resolve(aValue, aType, aRegion);
            aValue->mark |= aDimension + 1u < dimensions ? 0x4000u : 0u;
        };
        set_meaning(aAggregate, meaning::Value);
        for (auto* item : aAggregate->children)
        {
            if (item->is(construct::Association))
            {
                auto* value = item->children.back();
                for (auto* choice : item->child(0)->children)
                {
                    if (choice->is(construct::Others))
                        continue;
                    if (is_discrete_range(choice, aRegion))
                        discrete_range(choice, indexType, aRegion);
                    else
                        resolve(choice, indexType->base_type(), aRegion);
                }
                if (!value->is(construct::Box))
                    component(value);
            }
            else if (item->is(construct::Iterated))
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[item] = r;
                auto* specification = item->child(0);
                if (specification->is(construct::ForRange))
                {
                    auto* defining = specification->first(construct::Defining);
                    node* range = nullptr;
                    for (auto* c : specification->children)
                        if (c != defining && !c->is(construct::Flag) && !c->is(construct::Filter))
                            range = c;
                    auto* e = declare(entity_kind::LoopParameter, defining, r);
                    e->type = discrete_range(range, indexType, aRegion);
                }
                else
                    iterator_specification(specification, r);
                auto* value = item->children.back();
                auto* saved = item;
                (void)saved;
                if (last)
                    resolve(value, t->component, r);
                else
                    resolve_array_aggregate(value, aType, aDimension + 1u, r);
            }
            else
                component(item);
        }
        aAggregate->type = aType;
        return aType;
    }
}
