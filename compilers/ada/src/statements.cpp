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
#include <functional>
#include <set>
#include "program.hpp"

namespace neos::compilers::ada
{
    // --- statements

    void program::statement_identifiers_check(node* aStatements)
    {
        // (the labels, loop names and block names of a body (its inner blocks' too, not its inner program units'): distinct
        // (5.1(14)))
        std::set<std::string> seen;
        std::function<void(node*)> walk = [&](node* aNode)
        {
            for (auto* c : aNode->children)
            {
                if (c->is(construct::Declarations) || c->is(construct::Private))
                    continue;
                node* identifier = nullptr;
                if (c->is(construct::Label))
                    identifier = c;
                else if ((c->is(construct::Loop) || c->is(construct::Block)) && !c->children.empty() && c->child(0)->is(construct::Word))
                    identifier = c->child(0);
                if (identifier != nullptr)
                {
                    auto text = std::string{ identifier->source };
                    for (auto const* delimiter : { "<<", ">>", ":" })
                        for (auto at = text.find(delimiter); at != std::string::npos; at = text.find(delimiter))
                            text.erase(at, std::string{ delimiter }.size());
                    auto const name = name_text(text);
                    if (!seen.insert(name).second)
                        recover([&]() { error(identifier, "the statement identifier \"" + name + "\" is already used in this body"); });
                }
                walk(c);
            }
        };
        walk(aStatements);
    }

    void program::statements(node* aStatements, region* aRegion)
    {
        if (aStatements == nullptr)
            return;
        if (aStatements->is(construct::Handled))
        {
            if (auto* body = aStatements->parent; body != nullptr && iInstanceBodies == 0 && iInstantiationSites.empty() &&
                (body->is(construct::SubprogramBody) || body->is(construct::PackageBody) || body->is(construct::TaskBody) ||
                    body->is(construct::EntryBody)))
                statement_identifiers_check(aStatements);
            // (its labels: implicitly declared at the end of the declarative part of the innermost block or body that encloses 
            // them (5.1(12)), as names that 'Address can have as its prefix (13.3(11)))
            // (and its loops' and blocks' names likewise (5.5(7), 5.6(4)): they hide the implicitly declared (inherited) homographs
            // of the region (8.3(12/3)))
            auto const implicitly_declared = [](entity* x)
            {
                return (x->sub != nullptr && x->sub->inherited) || (x->kind == entity_kind::EnumerationLiteral && x->type != nullptr &&
                    x->type->view()->base_type()->view()->parent != nullptr);
            };
            auto const declare_name = [&](node* aStatement, std::string const& aName, entity_kind aKind)
            {
                auto range = aRegion->names.equal_range(aName);
                for (auto i = range.first; i != range.second; ++i)
                    if (!implicitly_declared(i->second) && i->second->visible_in_body)
                    {
                        // (a loop or block name: not a homograph of a declaration of the body (5.1(12), 8.3(26/2)))
                        if (aKind != entity_kind::Label && iInstanceBodies == 0 && iInstantiationSites.empty() && i->second->kind != entity_kind::Label &&
                            i->second->kind != entity_kind::Loop && i->second->kind != entity_kind::Block)
                            recover([&]() { error(aStatement->first(construct::Word) != nullptr ? aStatement->first(construct::Word) : aStatement,
                                "\"" + aName + "\" is already declared in this declarative region"); });
                        return;
                    }
                // (a generic body: the same declarative region as its formal part)
                if (aRegion->owner != nullptr && iInstanceBodies == 0)
                    for (auto [r, steps] = std::pair{ aRegion->parent, 0 }; r != nullptr && steps < 2; r = r->parent, ++steps)
                        if (r->generic_formals && r->owner != nullptr && r->owner->name == aRegion->owner->name)
                        {
                            if (r->names.count(aName) != 0u)
                                recover([&]() { error(aStatement->is(construct::Label) ? aStatement : aStatement->first(construct::Word) != nullptr ?
                                    aStatement->first(construct::Word) : aStatement, "\"" + aName + "\" is already declared in this declarative region"); });
                            break;
                        }
                for (auto i = range.first; i != range.second; ++i)
                    i->second->visible_in_body = false;
                auto* e = new_entity(aKind, aName, aStatement->is(construct::Label) ? aStatement : aStatement->first(construct::Word), aRegion);
                aRegion->add(e);
                iStatementNames[aStatement] = e;
                if (aStatement->is(construct::Label))
                    aStatement->ent = e;
            };
            std::function<void(node*)> labels = [&](node* aNode)
            {
                for (auto* c : aNode->children)
                {
                    if (c->is(construct::Label) && !iStatementNames.contains(c))
                    {
                        auto text = std::string{ c->source };
                        for (auto const* delimiter : { "<<", ">>" })
                            for (auto at = text.find(delimiter); at != std::string::npos; at = text.find(delimiter))
                                text.erase(at, 2u);
                        declare_name(c, name_text(text), entity_kind::Label);
                    }
                    else if ((c->is(construct::Loop) || c->is(construct::Block)) && !iStatementNames.contains(c) && c->first(construct::Word) != nullptr)
                        declare_name(c, name_text(c->first(construct::Word)->source), c->is(construct::Loop) ? entity_kind::Loop : entity_kind::Block);
                    if (!c->is(construct::Block) && !c->is(construct::Handler) && !c->is(construct::Label))
                        labels(c);
                }
            };
            if (auto* sequence = aStatements->first(construct::Statements))
                labels(sequence);
            statements(aStatements->first(construct::Statements), aRegion);
            // (the exceptions that the handlers name: each once; others alone, in the last handler (11.2))
            std::vector<entity*> handled;
            auto const handlers = aStatements->all(construct::Handler);
            for (auto* handler : handlers)
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[handler] = r;
                // (those of the earlier handlers: the same exception can be named twice in one handler (11.2(7)))
                auto const earlier = static_cast<std::ptrdiff_t>(handled.size());
                std::size_t choices = 0u;
                node* others = nullptr;
                for (auto* c : handler->children)
                    if (c->is(construct::Identifier) || c->is(construct::Selected) || c->is(construct::Others))
                    {
                        ++choices;
                        if (c->is(construct::Others))
                            others = c;
                    }
                // (a later handler with others too: the error is reported there)
                bool const laterOthers = std::any_of(std::find(handlers.begin(), handlers.end(), handler) + 1, handlers.end(),
                    [](node* h) { return h->first(construct::Others) != nullptr; });
                if (others != nullptr && iInstanceBodies == 0 && (choices > 1u || (handler != handlers.back() && !laterOthers)))
                    recover([&]() { error(others, "others must be the only choice of the last exception handler (11.2)"); });
                else if (others != nullptr && iInstanceBodies == 0 && handler == handlers.back() && std::any_of(handlers.begin(), handlers.end() - 1,
                    [](node* h) { return h->first(construct::Others) != nullptr; }))
                    recover([&]() { error(others, "only one exception handler can have others (11.2)"); });
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
                        recover([&]()
                        {
                            // (in the handler's region: its choice parameter hides an exception of its name (8.3))
                            auto* e = resolve_entity(c, iInstanceBodies == 0 ? r : aRegion);
                            if (e == nullptr || e->kind != entity_kind::Exception)
                                error(c, "exception name expected");
                            auto* x = e;
                            while (x->renamed != nullptr && x->renamed != x && x->kind == entity_kind::Exception)
                                x = x->renamed;
                            if (iInstanceBodies == 0 && std::find(handled.begin(), handled.begin() + earlier, x) != handled.begin() + earlier)
                                error(c, "an exception cannot be covered by two handlers of a handled sequence of statements (11.2(7))");
                            // (not an exception declared in a generic formal package (11.2(8)))
                            if (iInstanceBodies == 0 && [&]()
                                {
                                    int n = 0;
                                    for (auto* o = x->scope != nullptr ? x->scope->owner : nullptr; o != nullptr && n < 64; o = o->scope != nullptr ? o->scope->owner : nullptr, ++n)
                                        if (iFormalPackageTemplates.contains(o))
                                            return true;
                                    return false;
                                }())
                                error(c, "a choice cannot denote an exception declared in a generic formal package (11.2(8))");
                            handled.push_back(x);
                        });
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
            recover([&]() { statement(s, aRegion); });
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
            // (hidden from all visibility within its own specification (8.3(16)))
            e->declaring = true;
            struct restore_declaring { entity* e; ~restore_declaring() { e->declaring = false; } } restoreDeclaring{ e };
            if (iterator)
            {
                auto* iteratorType = resolve(range->child(0), nullptr, aRegion);
                e->type = iterator_cursor_type(iteratorType);
                aSpecification->mark = 1u;  // (a generalized iterator)
                // (the iterator name: of an iterator type (5.5.2(3/3)))
                if (iInstanceBodies == 0 && iteratorType != nullptr && !iterator_type(iteratorType))
                    recover([&]() { error(range, "the name of a generalized iterator must be of an iterator type (5.5.2(3/3))"); });
                // (reverse: of a reversible iterator type (5.5.2(4/3)))
                if (aSpecification->has_flag("reverse") && iInstanceBodies == 0 && !reversible_iterator(iteratorType))
                    recover([&]() { error(range, "the iterator of a reverse iteration must be of a reversible iterator type (5.5.2(4/3))"); });
                // (the cursor type: not limited here (the loop parameter is a constant of it, 5.5.2))
                if (iInstanceBodies == 0 && e->type != nullptr && !e->type->view()->is_formal && limited_type(e->type, aRegion))
                    recover([&]() { error(range, "the cursor type of a generalized iterator cannot be limited (5.5.2)"); });
                // (a prefixed view call's object (Container.Iterate): not a subcomponent depending on discriminants of an unconstrained
                // variable (5.5.2(6.1/4)))
                if (iInstanceBodies == 0)
                    if (auto const* call = call_of(range->child(0)); call != nullptr && call->prefixed && call->object != nullptr && discriminant_dependent(call->object))
                        recover([&]() { error(range, "the iterator's container cannot be a subcomponent that depends on discriminants of an unconstrained variable (5.5.2(6.1/4))"); });
            }
            else
            {
                t = discrete_range(range, nullptr, aRegion);
                e->type = t;
                // (a discrete subtype definition (5.5(5)))
                if (t != nullptr && !t->view()->is_formal && ((!t->view()->is_discrete() && t->view()->kind != type_kind::Private) ||
                    (iInstanceBodies == 0 && partial_view_here(t, aRegion))))
                    recover([&]() { error(range, "the range of a loop parameter specification must be discrete"); });
                // (not a subtype with a dynamic predicate, nor a nonstatic one with a predicate (3.2.4(29.3/4)))
                if (t != nullptr && !static_subtype(t))
                    predicate_check(t, range);
            }
            e->declaring = false;   // (visible in its filter)
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
        // (the iterable name: an object (not a type) of an array or iterable container type (5.5.2(3/3)))
        if (iInstanceBodies == 0 && is_subtype_name(iterable, aRegion))
            recover([&]() { error(iterable, "the iterable name of an iterator cannot denote a type (5.5.2(3/3))"); });
        auto* t = resolve(iterable, nullptr, aRegion);
        auto* e = declare(entity_kind::Variable, defining, aRegion);
        auto* v = t != nullptr ? t->view() : nullptr;
        if (iInstanceBodies == 0 && t != nullptr && !is_subtype_name(iterable, aRegion) && !v->is_formal && !v->base_type()->view()->is_formal)
        {
            auto* w = v->kind == type_kind::Access && designated_of(v) != nullptr ? designated_of(v)->view() : v;
            bool iterable_type = w->kind == type_kind::Array;
            for (auto* a = w->base_type()->view(); !iterable_type && a != nullptr; a = a->parent != nullptr && a->parent->base_type()->view() != a ?
                a->parent->base_type()->view() : nullptr)
                iterable_type = type_aspect(a, "default_iterator") != nullptr && type_aspect(a, "iterator_element") != nullptr;
            if (!iterable_type && w->kind == type_kind::ClassWide && w->specific != nullptr)
                for (auto* a = w->specific->view()->base_type()->view(); !iterable_type && a != nullptr; a = a->parent != nullptr &&
                    a->parent->base_type()->view() != a ? a->parent->base_type()->view() : nullptr)
                    iterable_type = type_aspect(a, "default_iterator") != nullptr && type_aspect(a, "iterator_element") != nullptr;
            if (!iterable_type && w->kind != type_kind::Private)
                recover([&]() { error(iterable, "the iterable name of an iterator must be of an array or iterable container type (5.5.2(3/3))"); });
        }
        if (v != nullptr && v->kind == type_kind::Access)
            v = designated_of(v)->view();
        if (v != nullptr && v->kind == type_kind::Array)
            e->type = v->component;
        else
        {
            e->type = iterator_element_type(t);
            // (reverse: the default iterator's type reversible (5.5.2(4/3)))
            if (aSpecification->has_flag("reverse") && iInstanceBodies == 0 && t != nullptr)
                for (auto* a = t->view()->base_type()->view(); a != nullptr; a = a->parent != nullptr && a->parent->base_type()->view() != a ?
                    a->parent->base_type()->view() : nullptr)
                    if (auto* aspect = type_aspect(a, "default_iterator"))
                    {
                        // (its function (of one parameter): the iterator type)
                        auto* scope = a->declared_in != nullptr ? a->declared_in : a->partial != nullptr ? a->partial->declared_in : nullptr;
                        if (scope != nullptr)
                        {
                            auto range = scope->names.equal_range(name_text(aspect->source));
                            for (auto i = range.first; i != range.second; ++i)
                                if (i->second->kind == entity_kind::Function && i->second->sub != nullptr && i->second->sub->result != nullptr &&
                                    i->second->sub->parameters.size() == 1u && !reversible_iterator(i->second->sub->result))
                                    recover([&]() { error(iterable, "the default iterator of a reverse container element iteration must be of a "
                                        "reversible iterator type (5.5.2(4/3))"); });
                        }
                        break;
                    }
        }
        if (subtype != nullptr)
        {
            // (an array component iterator's subtype: statically matching the component subtype; the element subtype's for a
            // container (5.5.2(5/5)))
            auto* st = subtype_indication(subtype, aRegion);
            if (iInstanceBodies == 0 && st != nullptr && e->type != nullptr && !statically_matching(st, e->type))
                recover([&]() { error(subtype, "the subtype of an iterator must statically match the component (or element) subtype (5.5.2(5/5))"); });
        }
        // (an array component or container element iterator: not of a discriminant-dependent subcomponent of an unconstrained variable
        // (5.5.2(6.1/4)))
        if (iInstanceBodies == 0 && v != nullptr && discriminant_dependent(iterable))
            recover([&]() { error(iterable, std::string{ "the iterable name of an " } + (v->kind == type_kind::Array ? "array component" : "container element") +
                " iterator cannot denote a discriminant-dependent subcomponent of an unconstrained variable (5.5.2(6.1/4))"); });
        // (a container element iterator: the call of its default iterator legal (its container parameter a variable if it is in out, 5.5.2(10/3)))
        if (iInstanceBodies == 0 && v != nullptr && v->kind != type_kind::Array && !v->is_formal && t != nullptr)
            if (auto* iterate = aspect_subprogram(t, "default_iterator"); iterate != nullptr && iterate->sub != nullptr && !iterate->sub->parameters.empty())
            {
                auto const& first = iterate->sub->parameters[0];
                bool const variable = first.mode == parameter_mode::InOut || first.mode == parameter_mode::Out ||
                    (first.type != nullptr && first.type->anonymous_access && first.type->view()->kind == type_kind::Access && !first.type->view()->access_constant);
                if (variable && !is_variable(iterable))
                    recover([&]() { error(iterable, "the call of the default iterator of the iterable name is illegal: its container parameter must be a variable "
                        "(5.5.2(10/3), 6.4.1(5/3))"); });
                // (its default cursor subtype (the Cursor of the Iterator_Interfaces instance of its iterator type): not limited here
                // (5.5.2(6.3/4)))
                if (auto* r = iterate->sub->result; r != nullptr)
                {
                    auto* rv = r->view();
                    if (rv->kind == type_kind::ClassWide && rv->specific != nullptr)
                        rv = rv->specific->view();
                    auto* instance = rv->base_type()->declared_in != nullptr ? rv->base_type()->declared_in->owner : nullptr;
                    if (auto formals = instance != nullptr ? iInstanceFormalRegions.find(instance) : iInstanceFormalRegions.end(); formals != iInstanceFormalRegions.end())
                    {
                        auto range = formals->second->names.equal_range("cursor");
                        for (auto i = range.first; i != range.second; ++i)
                            if (i->second->is_type() && i->second->type != nullptr && limited_type(i->second->type, aRegion))
                            {
                                recover([&]() { error(iterable, "the default cursor subtype of a container element iterator cannot be limited here (5.5.2(6.3/4))"); });
                                break;
                            }
                    }
                }
            }
        // (a container element iterator of a constant: its type's Constant_Indexing specified (5.5.2(6.2/4)... 5.5.1(11/3)))
        if (iInstanceBodies == 0 && v != nullptr && v->kind != type_kind::Array && !v->is_formal && t != nullptr &&
            aspect_subprogram(t, "constant_indexing") == nullptr && !is_variable(iterable))
            recover([&]() { error(iterable, "the iterable name of a container element iterator is a constant, so its type must specify Constant_Indexing (5.5.2(6.2/4))"); });
        e->where = storage::Renaming;
        e->renamed_name = iterable;
        // (a container element iterator without Variable_Indexing: a constant loop parameter (5.5.2(12/3)))
        if (v != nullptr && v->kind != type_kind::Array && !v->is_formal && t != nullptr && aspect_subprogram(t, "variable_indexing") == nullptr &&
            aspect_subprogram(t, "constant_indexing") != nullptr)
            e->kind = entity_kind::Constant;
        if (auto* filter = aSpecification->first(construct::Filter))
            condition(filter->child(0), aRegion);
    }

    bool program::iterator_type(ada_type* aType)
    {
        // (a descendant of an instance of Ada.Iterator_Interfaces' Forward_Iterator (its class-wide type); unknown: assumed)
        if (aType == nullptr)
            return true;
        auto* v = aType->view();
        if (v->kind == type_kind::ClassWide && v->specific != nullptr)
            v = v->specific->view();
        if (v->is_formal || v->base_type()->view()->is_formal || v->kind == type_kind::Private)
            return true;
        std::set<ada_type*> seen;
        std::function<bool(ada_type*, int)> iterator = [&](ada_type* aType, int aDepth) -> bool
        {
            if (aType == nullptr || aDepth > 32 || !seen.insert(aType).second)
                return false;
            auto* b = aType->view()->base_type()->view();
            for (auto const* n : { "forward_iterator", "reversible_iterator", "parallel_iterator", "parallel_reversible_iterator" })
                if (b->name == n || aType->base_type()->name == n)
                    return true;
            for (auto* i : b->interfaces)
                if (iterator(i, aDepth + 1))
                    return true;
            return b->parent != nullptr && b->parent->base_type()->view() != b && iterator(b->parent, aDepth + 1);
        };
        return iterator(v, 0);
    }

    bool program::reversible_iterator(ada_type* aIterator)
    {
        // (a descendant of Ada.Iterator_Interfaces' Reversible_Iterator (an instance's); unknown: assumed)
        if (aIterator == nullptr)
            return true;
        auto* v = aIterator->view();
        if (v->kind == type_kind::ClassWide && v->specific != nullptr)
            v = v->specific->view();
        if (v->is_formal || v->base_type()->view()->is_formal)
            return true;
        std::set<ada_type*> seen;
        std::function<bool(ada_type*, int)> reversible = [&](ada_type* aType, int aDepth) -> bool
        {
            if (aType == nullptr || aDepth > 32 || !seen.insert(aType).second)
                return false;
            auto* b = aType->view()->base_type()->view();
            if (b->name == "reversible_iterator" || b->name == "parallel_reversible_iterator" || aType->base_type()->name == "reversible_iterator")
                return true;
            for (auto* i : b->interfaces)
                if (reversible(i, aDepth + 1))
                    return true;
            return b->parent != nullptr && b->parent->base_type()->view() != b && reversible(b->parent, aDepth + 1);
        };
        if (v->kind != type_kind::Interface && !v->tagged)
            return true;
        return reversible(v, 0);
    }

    void program::case_choices_check(node* aCase, node* aSelector, ada_type* aType, std::vector<node*> const& aAlternatives, region* aRegion)
    {
        // (a case statement's or expression's choices: static, of the selector's type, covering each value of its subtype once)
        std::vector<std::tuple<bigint, bigint, node*>> covered;
        bool others = false;
        bool complete = true;
        for (auto* alternative : aAlternatives)
        {
            for (auto* choice : alternative->child(0)->children)
                if (!choice->is(construct::Others))
                    recover([&]()
                    {
                        auto [low, high] = choice_range(choice, aType != nullptr ? aType->base_type() : nullptr, aRegion);
                        // (static (5.4(5)))
                        if (!static_choice(choice, aType != nullptr ? aType->base_type() : nullptr, aRegion))
                        {
                            complete = false;
                            error(choice, "a case choice must be static");
                        }
                        // (a subtype with a predicate covers only the values that satisfy it (a static one's: known))
                        if (auto* ct = choice->type; ct != nullptr && (choice->is(construct::SubtypeIndication) || meaning_of(choice) == meaning::Type) &&
                            (ct->predicate != nullptr || ct->view()->predicate != nullptr))
                        {
                            if (auto values = static_values(ct, aRegion))
                                for (auto const& [l, h] : *values)
                                    covered.emplace_back(l, h, choice);
                            else
                                complete = false;
                        }
                        else if (low && high)
                        {
                            if (*low <= *high)
                                covered.emplace_back(*low, *high, choice);
                        }
                        else
                            complete = false;
                    });
                else
                {
                    others = true;
                    // (others: alone, in the last alternative (5.4(5)))
                    if (alternative->child(0)->size() > 1u)
                        recover([&]() { error(choice, "others must be the only choice of its alternative"); });
                    else if (alternative != aAlternatives.back())
                        recover([&]() { error(aCase->is(construct::CaseExpression) ? aCase : choice, "others must be in the last alternative"); });
                }
        }
        // (a selector of a universal type: its values cannot all be covered but by others (5.4(10)))
        if (aType != nullptr && aType->is_universal() && !others && iInstanceBodies == 0)
            recover([&]() { error(aCase, "the case choices do not cover every value of the selector's subtype (others is required)"); });
        // (each value of the selector's subtype covered once (5.4(6-10)): the nominal subtype of a name whose subtype is static,
        // otherwise the base range)
        if (aType != nullptr && complete && aType->view()->is_discrete())
            recover([&]()
            {
                std::sort(covered.begin(), covered.end(), [](auto const& a, auto const& b) { return std::get<0>(a) < std::get<0>(b); });
                // (each choice that overlaps one before it (by value): reported at the later of the two in the text)
                std::size_t widest = 0u;
                for (std::size_t i = 1u; i < covered.size(); ++i)
                {
                    if (std::get<0>(covered[i]) <= std::get<1>(covered[widest]))
                    {
                        auto* a = std::get<2>(covered[i]);
                        auto* b = std::get<2>(covered[widest]);
                        recover([&]() { error(a->source.data() < b->source.data() ? b : a, "a value is covered by more than one case choice"); });
                    }
                    if (std::get<1>(covered[i]) > std::get<1>(covered[widest]))
                        widest = i;
                }
                auto* s = aType->view();
                // (a name (an object, a function call, a type conversion, a qualified expression) (5.4(7/4)))
                bool const name = aSelector->is(construct::Identifier) || aSelector->is(construct::Selected) ||
                    aSelector->is(construct::Qualified) || aSelector->is(construct::Apply) ||
                    (aSelector->is(construct::Attribute) && aSelector->size() >= 2u && name_text(aSelector->child(1)->source) == "old");
                // (a generic formal in out object: of its actual's subtype (nonstatic, 12.4(9/5)))
                bool const formalInOut = aSelector->is(construct::Identifier) && aSelector->ent != nullptr && aSelector->ent->decl != nullptr &&
                    aSelector->ent->decl->parent != nullptr && aSelector->ent->decl->parent->is(construct::FormalObject) &&
                    aSelector->ent->decl->parent->has_flag("out");
                bool const nominal = name && static_subtype(aType) && !formalInOut;
                if (!nominal)
                    s = s->base_type()->view();
                if (!s->low.is_integer() || !s->high.is_integer() || s->is_formal || s->base_type()->view()->is_formal)
                    return;
                // (the values to cover: the nominal subtype's (those satisfying its static predicate), else the base range's)
                std::vector<std::pair<bigint, bigint>> required{ { s->low.integer(), s->high.integer() } };
                if (nominal && (aType->predicate != nullptr || aType->view()->predicate != nullptr))
                {
                    auto values = static_values(aType, aRegion);
                    if (!values)
                        return;     // (a dynamic predicate: its values are not known)
                    required = *values;
                }
                // (no value outside the subtype (5.4(7/4)))
                bool outside = false;
                if (nominal)
                    for (auto const& [low, high, choice] : covered)
                    {
                        bigint next = low;
                        for (auto const& [l, h] : required)
                            if (!(next < l) && !(h < next))
                                next = h + bigint{ 1 };
                        if (!(high < next))
                        {
                            outside = true;
                            recover([&]() { error(choice, "a case choice covers values outside the selector's subtype"); });
                        }
                    }
                if (outside || others)
                    return;
                // (every required value covered)
                for (auto const& [rl, rh] : required)
                {
                    bigint next = rl;
                    for (auto const& [low, high, choice] : covered)
                    {
                        if (high < next)
                            continue;
                        if (next < low)
                            break;
                        next = high + bigint{ 1 };
                    }
                    if (!(rh < next))
                    {
                        error(aCase, "the case choices do not cover every value of the selector's subtype");
                        break;
                    }
                }
            });
    }

    void program::statement(node* aStatement, region* aRegion)
    {
        if (aStatement->is(construct::Loop) || aStatement->is(construct::Block) || aStatement->is(construct::Accept))
            recover([&]() { end_name_check(aStatement); });
        switch (aStatement->kind)
        {
        case construct::NullStatement:
            break;
        case construct::Label:
            // (a label: implicitly declared in the innermost enclosing block or body (5.1(12)); not a homograph of a declaration
            // there, nor hidden there by an inner one (a loop parameter's))
            if (iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                auto text = std::string{ aStatement->source };
                for (auto const* delimiter : { "<<", ">>" })
                    for (auto at = text.find(delimiter); at != std::string::npos; at = text.find(delimiter))
                        text.erase(at, 2u);
                auto const name = name_text(text);
                for (auto* r = aRegion; r != nullptr; r = r->parent)
                {
                    auto range = r->names.equal_range(name);
                    if (range.first != range.second && range.first->second->kind != entity_kind::Loop && range.first->second->kind != entity_kind::Block &&
                        range.first->second != aStatement->ent && std::any_of(range.first, range.second, [&](auto const& x) { return x.second->visible_in_body && x.second != aStatement->ent; }))
                    {
                        error(aStatement, "the label \"" + name + "\" is already declared (or hidden) here");
                        break;
                    }
                    if (r->package_body)
                        continue;
                    if (r->master || r->is_subprogram || r->is_package || (r->owner != nullptr && r->owner->kind != entity_kind::Block &&
                        r->owner->kind != entity_kind::Loop))
                        break;
                }
            }
            break;
        case construct::Pragma:
            // (Suppress, Unsuppress: only in a declarative part, a package specification or as configuration pragmas (11.5))
            if (iInstanceBodies == 0 && !aStatement->children.empty())
                if (auto const n = name_text(aStatement->child(0)->source); n == "suppress" || n == "unsuppress")
                {
                    recover([&]() { error(aStatement, "pragma " + n + " is allowed only in a declarative part or a package specification (11.5)"); });
                    break;
                }
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
                // (the target: a variable (5.2(5/2)))
                // (a type conversion: a variable only as a view conversion, of tagged types here (4.6(5/2)))
                auto const valueConversion = [&]()
                {
                    auto* n = target;
                    while (n != nullptr && n->is(construct::Parenthesized))
                        n = n->child(0);
                    if (n == nullptr || !n->is(construct::Apply) || meaning_of(n) != meaning::Conversion || n->size() != 2u)
                        return false;
                    auto const tagged = [](ada_type* aType)
                    {
                        auto* v = aType != nullptr ? aType->view() : nullptr;
                        return v == nullptr || v->is_formal || v->tagged || v->kind == type_kind::ClassWide || v->base_type()->view()->tagged;
                    };
                    return !tagged(n->type) || !tagged(n->child(1)->type);
                };
                if (!is_variable(target) || (iInstanceBodies == 0 && valueConversion()))
                    recover([&]() { error(target, "the target of an assignment must be a variable"); });
                // (the target's type: not abstract (3.9.3(8/3)))
                if (abstract_type(t, aRegion))
                    recover([&]() { error(target, "the target of an assignment cannot be of an abstract type"); });
                // (not of a limited type (7.5(8/2)))
                else if (t != nullptr && iInstanceBodies == 0 && (!t->view()->is_formal || t->view()->limited || t->view()->base_type()->view()->limited) &&
                    limited_type(t, aRegion))
                    recover([&]() { error(target, "the target of an assignment cannot be of a limited type"); });
                auto* saved = iTargetType;
                iTargetType = t;
                resolve(aStatement->child(1), t, aRegion);
                iTargetType = saved;
                // (a static value: in its type's base range (4.9(34/3)))
                if (iInstanceBodies == 0 && t != nullptr)
                    static_range_check(aStatement->child(1), aStatement->child(1), t, true, aRegion);
                break;
            }
        case construct::Call:
            {
                auto* name = aStatement->child(0);
                resolve(name, iVoid, aRegion);
                // (a call of an entry family's member: one index (9.5.3(3)))
                if (auto const* call = call_of(name); call != nullptr && call->callee != nullptr && call->callee->kind == entity_kind::Entry &&
                    call->callee->sub != nullptr && call->callee->sub->family_type != nullptr && call->family_index == nullptr)
                    recover([&]() { error(name, "a call of an entry family must give one entry index"); });
                // (within a protected function: no internal call of a protected procedure or entry of the same object (it is a constant
                // there, 9.5.1(2/3)))
                if (auto const* call = call_of(name); call != nullptr && call->callee != nullptr && iInstanceBodies == 0 &&
                    (call->callee->kind == entity_kind::Procedure || call->callee->kind == entity_kind::Entry) && call->callee->scope != nullptr &&
                    call->callee->scope->owner != nullptr && call->callee->scope->owner->kind == entity_kind::Protected && iCurrentSubprogram != nullptr &&
                    iCurrentSubprogram->kind == entity_kind::Function && iCurrentSubprogram->scope != nullptr &&
                    iCurrentSubprogram->scope->owner == call->callee->scope->owner &&
                    (name->is(construct::Identifier) || (name->is(construct::Apply) && name->child(0)->is(construct::Identifier))))
                    recover([&]() { error(name, "a protected function cannot call a protected procedure or entry of its own object"); });
                // (a call without actuals: its parameters all have defaults (6.4(9)))
                if (auto const* call = call_of(name); call != nullptr && call->callee != nullptr && call->callee->sub != nullptr && !name->is(construct::Apply) &&
                    iInstanceBodies == 0)
                {
                    auto const& formals = call->callee->sub->parameters;
                    for (std::size_t i = call->prefixed ? 1u : 0u; i < formals.size(); ++i)
                        if ((i >= call->actuals.size() || call->actuals[i] == nullptr) && formals[i].default_expression == nullptr)
                        {
                            recover([&]() { error(name, "missing actual parameter in call to \"" + call->callee->name + "\""); });
                            break;
                        }
                }
                else if (call == nullptr && !name->is(construct::Apply) && iInstanceBodies == 0)
                    if (auto* callee = name->ent; callee != nullptr && callee->kind == entity_kind::Entry && callee->sub != nullptr)
                        for (auto const& formal : callee->sub->parameters)
                            if (formal.default_expression == nullptr)
                            {
                                recover([&]() { error(name, "missing actual parameter in call to \"" + callee->name + "\""); });
                                break;
                            }
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
                    recover([&]() { condition(c->child(0), aRegion); });
                    statements(c->child(1), aRegion);
                }
                else if (c->is(construct::Else))
                    statements(c->child(0), aRegion);
                else
                    recover([&]() { condition(c, aRegion); });
            }
            break;
        case construct::Case:
            {
                auto* selector = aStatement->child(0);
                auto const options = interpretations(selector, aRegion);
                ada_type* t = nullptr;
                std::size_t discrete = 0u;
                for (auto const& o : options)
                    if (o.type != nullptr && o.type->view()->is_discrete() && !o.type->is_universal())
                    {
                        if (t == nullptr || !same_type(t, o.type))
                            ++discrete;
                        t = o.type;
                    }
                // (the selector: of a single discrete type, not a partial view here (5.4(4)))
                // (a character literal: of every character type (Character, Wide_Character ...): ambiguous)
                if ((discrete > 1u || selector->is(construct::Character)) && iInstanceBodies == 0)
                    recover([&]() { error(selector, "the selector of a case statement is ambiguous"); });
                recover([&]() { t = resolve(selector, t, aRegion); });
                if (t != nullptr && iInstanceBodies == 0 && !t->view()->is_formal && !t->base_type()->view()->is_formal &&
                    (!t->view()->is_discrete() || partial_view_here(t, aRegion)))
                    recover([&]() { error(selector, "the selector of a case statement must be of a discrete type"); });
                // (an ambiguous selector: its choices not checked against a guessed type (no cascade))
                if (!((discrete > 1u || selector->is(construct::Character)) && iInstanceBodies == 0))
                    case_choices_check(aStatement, selector, t, aStatement->all(construct::Alternative), aRegion);
                for (auto* alternative : aStatement->all(construct::Alternative))
                    statements(alternative->first(construct::Statements), aRegion);
                break;
            }
        case construct::Loop:
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[aStatement] = r;
                if (auto* name = aStatement->first(construct::Word))
                {
                    // (declared already (see statements), else now)
                    auto* e = iStatementNames.contains(aStatement) ? iStatementNames[aStatement] : nullptr;
                    if (e == nullptr)
                    {
                        e = new_entity(entity_kind::Loop, name_text(name->source), name, aRegion);
                        aRegion->add(e);
                    }
                    e->inner = r;
                    r->owner = e;
                    aStatement->ent = e;
                }
                if (auto* w = aStatement->first(construct::While))
                    recover([&]() { condition(w->child(0), r); });
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
                    // (declared already (see statements), else now)
                    auto* e = iStatementNames.contains(aStatement) ? iStatementNames[aStatement] : nullptr;
                    if (e == nullptr)
                    {
                        e = new_entity(entity_kind::Block, name_text(name->source), name, aRegion);
                        aRegion->add(e);
                    }
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
            // (within a loop (the one it names), not outside a body or accept statement (5.7(4)))
            if (iInstanceBodies == 0)
            {
                auto* name = aStatement->first(construct::Word);
                bool found = false;
                for (auto* n = aStatement->parent; n != nullptr && !found; n = n->parent)
                {
                    if (n->is(construct::SubprogramBody) || n->is(construct::EntryBody) || n->is(construct::Accept) || n->is(construct::TaskBody) ||
                        n->is(construct::PackageBody))
                        break;
                    if (n->is(construct::Loop))
                    {
                        auto* loopName = n->first(construct::Word);
                        found = name == nullptr || (loopName != nullptr && name_text(loopName->source) == name_text(name->source));
                    }
                }
                if (!found)
                    recover([&]() { error(aStatement, name != nullptr ? "an exit statement must be within the loop that it names (5.7(4))" :
                        "an exit statement must be within a loop (5.7(4))"); });
            }
            break;
        case construct::Goto:
            // (its target: a labeled statement of a sequence of statements that encloses the goto statement, not outside an enclosing
            // body or accept statement (5.8(4)))
            if (iInstanceBodies == 0 && !aStatement->children.empty())
            {
                // (its simple name (an expanded name's last))
                auto label = name_text(aStatement->child(0)->source);
                if (auto const dot = label.rfind('.'); dot != std::string::npos)
                    label = label.substr(dot + 1u);
                auto const label_of = [](node const* c)
                {
                    auto text = std::string{ c->source };
                    for (auto const* delimiter : { "<<", ">>" })
                        for (auto at = text.find(delimiter); at != std::string::npos; at = text.find(delimiter))
                            text.erase(at, 2u);
                    return name_text(text);
                };
                bool found = false;
                for (auto* n = aStatement->parent; n != nullptr && !found; n = n->parent)
                {
                    if (n->is(construct::Statements))
                        for (auto* c : n->children)
                            if (c->is(construct::Label) && label_of(c) == label)
                                found = true;
                    if (n->is(construct::SubprogramBody) || n->is(construct::PackageBody) || n->is(construct::TaskBody) || n->is(construct::EntryBody) ||
                        n->is(construct::ProtectedBody) || n->is(construct::Accept) || n->is(construct::Unit))
                        break;
                }
                if (!found)
                    recover([&]() { error(aStatement->child(0), "the goto statement's label \"" + label + "\" is not of a statement of a sequence of statements enclosing it (5.8(4))"); });
                // (the name: of the label, as visible here (an inner declaration of the name hides it, 8.3))
                else
                {
                    entity* denoted = nullptr;
                    {
                        speculation guard{ *this };
                        try { denoted = resolve_entity(aStatement->child(0), aRegion); } catch (compile_error const&) {}
                    }
                    if (denoted != nullptr && denoted->kind != entity_kind::Label)
                        recover([&]() { error(aStatement->child(0), "\"" + label + "\" does not denote a label here (it is hidden by an inner declaration)"); });
                }
            }
            break;
        case construct::Return:
            // (the construct it applies to (6.5(4/2)): a function body (with an expression); a procedure body, entry body, accept
            // statement or extended return statement (without) (6.5(5/2, 6/2)))
            if (iInstanceBodies == 0)
            {
                node const* applies = nullptr;
                for (auto* n = aStatement->parent; n != nullptr && applies == nullptr; n = n->parent)
                    if (n->is(construct::SubprogramBody) || n->is(construct::EntryBody) || n->is(construct::Accept) || n->is(construct::ReturnExtended) ||
                        n->is(construct::TaskBody) || n->is(construct::PackageBody))
                        applies = n;
                bool const function = applies != nullptr && applies->is(construct::SubprogramBody) &&
                    std::any_of(applies->children.begin(), applies->children.end(), [](node const* c) { return c->is(construct::FunctionSpecification); });
                if (applies != nullptr && (applies->is(construct::TaskBody) || applies->is(construct::PackageBody)))
                    recover([&]() { error(aStatement, "a return statement must be within a subprogram body, entry body or accept statement (6.5(4/2))"); });
                else if (applies != nullptr && aStatement->size() > 0u && !function)
                    recover([&]() { error(aStatement, "a return statement with an expression applies only to a function body (6.5(5/2))"); });
                else if (applies != nullptr && aStatement->size() == 0u && function)
                    recover([&]() { error(aStatement, "a return statement in a function body must have an expression (6.5(5/2))"); });
            }
            // (not of a nonreturning procedure (6.5.1))
            if (iInstanceBodies == 0)
                if (auto* owner = return_owner(aRegion); owner != nullptr && owner->kind == entity_kind::Procedure && owner->sub != nullptr && (owner->sub->no_return ||
                    // (check_generic's instance: of a nonreturning generic procedure)
                    (iTemplateInstances.contains(owner) && std::any_of(iTemplateInstanceOf.begin(), iTemplateInstanceOf.end(), [&](auto const& g)
                        { return g.second == owner && g.first->sub != nullptr && g.first->sub->no_return; }))))
                    recover([&]() { error(aStatement, "a return statement cannot apply to a nonreturning procedure (6.5.1)"); });
            if (aStatement->size() > 0u)
            {
                ada_type* t = nullptr;
                auto* owner = return_owner(aRegion);
                if (owner != nullptr && owner->sub != nullptr)
                    t = owner->sub->result;
                resolve(aStatement->child(0), t, aRegion);
                if (owner != nullptr && owner->kind == entity_kind::Function)
                {
                    limited_value_check(aStatement->child(0), t, aRegion);
                    if (iSpeculation == 0)
                        others_check(aStatement->child(0), t);  // (an unconstrained array result: no applicable index constraint)
                }
                // (a class-wide result: the expression's (specific) type not deeper than the function (6.5(8/5), 3.10.2))
                if (owner != nullptr && t != nullptr && t->view()->kind == type_kind::ClassWide && iInstanceBodies == 0 && iSpeculation == 0 &&
                    !within_generic_body(aRegion) && owner->scope != nullptr)
                    if (auto* et = aStatement->child(0)->type; et != nullptr && et->view()->kind != type_kind::ClassWide && et->view()->tagged &&
                        type_level(et) > accessibility_level(owner->scope))
                        recover([&]() { error(aStatement->child(0), "the type of the return expression is deeper than the function (its result is class-wide) (6.5(8/5))"); });
            }
            break;
        case construct::ReturnExtended:
            {
                // (in a function body, not within another extended return statement (6.5(4/2)))
                if (iInstanceBodies == 0)
                {
                    node const* applies = nullptr;
                    for (auto* n = aStatement->parent; n != nullptr && applies == nullptr; n = n->parent)
                        if (n->is(construct::SubprogramBody) || n->is(construct::EntryBody) || n->is(construct::Accept) || n->is(construct::ReturnExtended) ||
                            n->is(construct::TaskBody) || n->is(construct::PackageBody))
                            applies = n;
                    bool const function = applies != nullptr && applies->is(construct::SubprogramBody) &&
                        std::any_of(applies->children.begin(), applies->children.end(), [](node const* c) { return c->is(construct::FunctionSpecification); });
                    if (applies != nullptr && !function)
                        recover([&]() { error(aStatement, "an extended return statement applies only to a function body (6.5(4/2))"); });
                }
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
                // (the return subtype: not abstract (3.9.3(8/3)))
                if (abstract_type(t, aRegion))
                    recover([&]() { error(typeNode, "the return object cannot be of an abstract type"); });
                // (the return subtype and the function's result subtype (6.5(5.2/3-5.5/3)))
                if (auto* owner = return_owner(aRegion); owner != nullptr && owner->sub != nullptr && owner->sub->result != nullptr && t != nullptr &&
                    iInstanceBodies == 0)
                {
                    auto* result = owner->sub->result;
                    bool const accessResult = result->anonymous_access;
                    bool const accessReturn = typeNode->is(construct::AccessDefinition);
                    bool const resultNotNull = owner->sub->result_not_null || result->not_null || result->view()->not_null;
                    bool const returnNotNull = t->not_null || t->view()->not_null || aStatement->has_flag("not null") || typeNode->has_flag("not null");
                    // (elementary: statically matching, null exclusions included)
                    auto const matching = [&]() -> bool
                    {
                        auto* x = t->view();
                        auto* y = result->view();
                        if (x->is_access() || y->is_access())
                        {
                            if (returnNotNull != resultNotNull)
                                return false;
                            if (accessResult)
                                return x->kind == y->kind && x->access_constant == y->access_constant && (x->kind != type_kind::Access ||
                                    x->designated == nullptr || y->designated == nullptr || statically_matching(x->designated, y->designated));
                            return same_type(x, y) && (x->kind != type_kind::Access || x->designated == nullptr || y->designated == nullptr ||
                                x->designated == y->designated || statically_matching(x->designated, y->designated));
                        }
                        return statically_matching(t, result);
                    };
                    if (accessResult != accessReturn)
                        recover([&]() { error(typeNode, accessResult ? "the return object of a function whose result is an access definition must be declared by an access definition (6.5(5.3/2))" :
                            "the return object must be declared by a subtype indication when the function's result subtype is a subtype mark (6.5(5.2/3))"); });
                    else if (accessResult)
                    {
                        if (!matching())
                            recover([&]() { error(typeNode, "the return object's access definition must statically match the function's result subtype (6.5(5.3/2))"); });
                    }
                    else if (!covers(result->kind == type_kind::ClassWide || result->view()->kind == type_kind::ClassWide ? result : result->base_type(), t))
                        recover([&]() { error(typeNode, "the return object's type must be covered by the function's result type (6.5(5.2/3))"); });
                    else if ((result->view()->is_scalar() || result->view()->is_access()) && !matching())
                        recover([&]() { error(typeNode, "the return object's subtype must statically match the function's (elementary) result subtype (6.5(5.2/3))"); });
                    else if ((result->view()->kind == type_kind::Array || !result->view()->discriminant_constraint.empty()) && result->view()->constrained &&
                        (!t->view()->constrained || !statically_matching(t, result)))
                        recover([&]() { error(typeNode, "the return object's subtype must be statically compatible with the function's result subtype (6.5(5.2/3))"); });
                    if (!accessReturn && init == nullptr && indefinite_subtype(t, typeNode, aRegion))
                        recover([&]() { error(typeNode, "a return object of an indefinite subtype must be initialized (6.5(5.2/3))"); });
                }
                // (a class-wide result: the return object's (specific) type, and its initial value's, not deeper than the function (6.5(8/5)))
                if (auto* owner = return_owner(aRegion); owner != nullptr && owner->sub != nullptr && owner->sub->result != nullptr &&
                    owner->sub->result->view()->kind == type_kind::ClassWide && t != nullptr && iInstanceBodies == 0 && !within_generic_body(aRegion) &&
                    owner->scope != nullptr)
                {
                    auto const level = accessibility_level(owner->scope);
                    // (a class-wide return subtype: its specific type's level)
                    auto* rt = t->view()->kind == type_kind::ClassWide && t->view()->specific != nullptr ? t->view()->specific : t;
                    if (rt->view()->kind != type_kind::ClassWide && rt->view()->tagged && type_level(rt) > level)
                        recover([&]() { error(typeNode, "the return object's type is deeper than the function (its result is class-wide) (6.5(8/5))"); });
                }
                // (an aliased return object: of an immutably limited type (6.5(5.10/3)))
                if (aStatement->has_flag("aliased") && iInstanceBodies == 0 && t != nullptr && !immutably_limited(t))
                    recover([&]() { error(defining, "an aliased return object must be of an immutably limited type (6.5(5.10/3))"); });
                // (a constant return object: initialized (6.5(5.1/3)... 3.3.1))
                if (aStatement->has_flag("constant") && init == nullptr && iInstanceBodies == 0)
                    recover([&]() { error(aStatement, "a constant return object must have an expression (6.5(2.1/3))"); });
                auto* e = declare(aStatement->has_flag("constant") ? entity_kind::Constant : entity_kind::Variable, defining, r);
                e->aliased_declared = aStatement->has_flag("aliased");
                e->type = t;
                e->initial = init;
                aStatement->ent = e;
                if (init != nullptr)
                {
                    resolve(init, t, aRegion);
                    limited_value_check(init, t, aRegion);
                    if (iSpeculation == 0)
                        others_check(init, t);  // (an unconstrained array return object: no applicable index constraint)
                    if (auto* owner = return_owner(aRegion); owner != nullptr && t != nullptr && t->view()->kind == type_kind::ClassWide && iInstanceBodies == 0 &&
                        !within_generic_body(aRegion) && owner->scope != nullptr && init->type != nullptr && init->type->view()->kind != type_kind::ClassWide &&
                        init->type->view()->tagged && type_level(init->type) > accessibility_level(owner->scope))
                        recover([&]() { error(init, "the type of the return object's initial value is deeper than the function (its result is class-wide) (6.5(8/5))"); });
                }
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
            // (a reraise: within a handler, not within a body nested in it (11.3(3)))
            else if (iInstanceBodies == 0)
            {
                bool inHandler = false;
                for (auto* n = aStatement->parent; n != nullptr && !inHandler; n = n->parent)
                {
                    if (n->is(construct::Handler))
                        inHandler = true;
                    else if (n->is(construct::SubprogramBody) || n->is(construct::PackageBody) || n->is(construct::TaskBody) || n->is(construct::EntryBody) ||
                        n->is(construct::ProtectedBody))
                        break;
                }
                if (!inHandler)
                    recover([&]() { error(aStatement, "a raise statement without an exception name must be within an exception handler (not a body nested in it) (11.3(3))"); });
            }
            break;
        case construct::Delay:
            for (auto* c : aStatement->children)
                if (!c->is(construct::Flag))
                {
                    auto* t = resolve(c, aStatement->has_flag("until") ? nullptr : iDuration, aRegion);
                    // (a delay until's expression: of a time type (Calendar.Time, Real_Time.Time) (9.6(5)))
                    if (aStatement->has_flag("until") && iInstanceBodies == 0 && t != nullptr)
                    {
                        bool time = false;
                        for (auto const* package : { "ada.calendar", "ada.real_time" })
                            if (auto* tt = library_type(package, "time"); tt != nullptr && tt->view()->base_type()->view() == t->view()->base_type()->view())
                                time = true;
                        if (!time && !t->view()->is_formal)
                            recover([&]() { error(c, "the expression of a delay until statement must be of a time type (9.6(5))"); });
                    }
                }
            break;
        case construct::Abort:
            for (auto* c : aStatement->children)
            {
                auto* t = resolve(c, nullptr, aRegion);
                // (a task object (9.8(3)); a task type's name within its body: the current instance)
                bool currentInstance = false;
                if (meaning_of(c) == meaning::Type && c->ent != nullptr)
                    for (region* jump = nullptr, *r = aRegion; r != nullptr && !currentInstance; r = outward(r, jump))
                        for (auto* o = r->owner; o != nullptr && !currentInstance; o = o->owner != o ? o->owner : nullptr)
                        {
                            currentInstance = o == c->ent || (o->kind == entity_kind::Task && o->type != nullptr && o->type == c->ent->type);
                            if (o->owner == nullptr)
                                break;
                        }
                if (iInstanceBodies == 0 && !currentInstance && (meaning_of(c) == meaning::Type || (t != nullptr && t->view()->kind != type_kind::Task &&
                    !(t->view()->kind == type_kind::ClassWide && t->view()->specific != nullptr && t->view()->specific->view()->task_interface) &&
                    !t->view()->task_interface && !t->view()->is_formal)))
                    recover([&]() { error(c, "the name in an abort statement must denote a task object (9.8(3))"); });
            }
            break;
        case construct::Requeue:
            resolve_requeue(aStatement, aRegion);
            break;
        case construct::Accept:
            accept_statement(aStatement, aRegion);
            break;
        case construct::SelectAccept:
            {
                // (a selective wait: an accept alternative at least (9.7.1(2)))
                bool accept = false;
                for (auto* c : aStatement->children)
                    if (c->is(construct::SelectAlternative))
                        for (auto* a : c->children)
                            if (!a->is(construct::Guard))
                            {
                                accept = accept || a->is(construct::Accept);
                                break;
                            }
                // (directly within a task body (9.7.1(9))... reported at its start; else its missing accept alternative at its end)
                if (!accept && [&]()
                    {
                        for (auto* r = aRegion; r != nullptr; r = r->parent)
                        {
                            if (r->owner != nullptr && r->owner->sub != nullptr && r->owner->owner != nullptr && r->owner->owner->kind == entity_kind::Task)
                                return false;   // (the task body)
                            if (r->is_package || (r->owner != nullptr && r->owner->sub != nullptr && r->is_subprogram))
                                return true;
                        }
                        return true;
                    }())
                    recover([&]() { error(aStatement, "a selective wait must be within a task body and have an accept alternative (9.7.1)"); });
                else if (!accept)
                    recover([&]() { error(end_position(aStatement), "a selective wait must have an accept alternative"); });
                // (at most one terminate alternative; not with a delay alternative or an else part; no delay alternative with an else
                // part (9.7.1(12)))
                node* terminate = nullptr;
                node* delay = nullptr;
                node* elsePart = aStatement->first(construct::Statements);
                for (auto* c : aStatement->children)
                    if (c->is(construct::SelectAlternative))
                        for (auto* a : c->children)
                            if (!a->is(construct::Guard))
                            {
                                if (a->is(construct::Terminate))
                                {
                                    if (terminate != nullptr)
                                        recover([&]() { error(a, "a selective wait can have only one terminate alternative"); });
                                    else if (delay != nullptr)
                                        recover([&]() { error(a, "a selective wait cannot have both a terminate and a delay alternative"); });
                                    terminate = a;
                                }
                                else if (a->is(construct::Delay))
                                {
                                    if (terminate != nullptr && delay == nullptr)
                                        recover([&]() { error(a, "a selective wait cannot have both a terminate and a delay alternative"); });
                                    delay = a;
                                }
                                break;
                            }
                if (elsePart != nullptr && (terminate != nullptr || delay != nullptr))
                {
                    // (reported at its "else")
                    node* at = elsePart;
                    if (auto const before = std::string_view{ aStatement->source.data(), static_cast<std::size_t>(elsePart->source.data() - aStatement->source.data()) };
                        elsePart->source.data() > aStatement->source.data())
                        if (auto const e = before.rfind("else"); e != std::string_view::npos)
                        {
                            auto& n = iSyntheticNodes.emplace_back(node{ construct::Else, before.substr(e, 4u) });
                            n.file = aStatement->file;
                            n.parent = aStatement;
                            at = &n;
                        }
                    recover([&]() { error(at, "a selective wait with a terminate or delay alternative cannot have an else part"); });
                }
            }
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
                        {
                            statement(a, aRegion);
                            // (a timed or conditional entry call: of an entry, or of a procedure implemented by an entry (a dispatching
                            // operation of a synchronized interface) (9.7.2(3.2/2), 9.7.3))
                            if (c->is(construct::SelectCall) && !aStatement->is(construct::SelectAsynchronous) && a == c->children.front())
                            {
                                entity* callee = a->ent;
                                if (callee == nullptr && !a->children.empty())
                                    callee = a->child(0)->ent;
                                if (callee != nullptr && callee->kind == entity_kind::Procedure && callee->sub != nullptr &&
                                    !(callee->sub->controlling != nullptr && callee->sub->controlling->view()->base_type()->view()->kind == type_kind::Interface) &&
                                    callee->sub->renames == nullptr)
                                    recover([&]() { error(a, "a timed or conditional entry call must call an entry"); });
                            }
                        }
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

    bool program::immutably_limited(ada_type* aType, int aDepth)
    {
        // (an explicitly limited record type, a task, protected or synchronized interface type, a tagged limited private type, or a
        // descendant of one (7.5(8.1/3)))
        if (aType == nullptr || aDepth > 16)
            return false;
        auto* v = aType->view();
        if (v->kind == type_kind::ClassWide && v->specific != nullptr)
            v = v->specific->view();
        auto* b = v->base_type()->view();
        if (b->kind == type_kind::Task || b->kind == type_kind::Protected)
            return true;
        if (b->kind == type_kind::Interface)
            return b->synchronized_interface || b->task_interface || b->protected_interface;
        if (b->is_formal)
            return false;
        // (a partial view: if tagged limited private)
        if (auto* partial = aType->kind == type_kind::Private ? aType : aType->base != nullptr && aType->base->kind == type_kind::Private ? aType->base : nullptr)
            return partial->tagged && partial->limited;
        // (an explicitly limited record type or record extension, or derived from one)
        return b->kind == type_kind::Record && b->limited;
    }

    entity* program::return_owner(region* aRegion)
    {
        // (the subprogram (or entry or accept) that a return statement returns from)
        for (auto* r = aRegion; r != nullptr; r = r->parent)
            if (r->owner != nullptr && (r->owner->kind == entity_kind::Function || r->owner->kind == entity_kind::Procedure || r->owner->kind == entity_kind::Entry))
                return r->owner;
        return iCurrentSubprogram;
    }

    node* program::end_position(node* aNode)
    {
        // (a position for an error: the construct's closing "end" (a synthetic node there))
        auto const at = aNode->source.rfind("end");
        if (at == std::string_view::npos)
            return aNode;
        auto& n = iSyntheticNodes.emplace_back(node{ construct::End, aNode->source.substr(at, 3u) });
        n.file = aNode->file;
        n.parent = aNode;
        return &n;
    }

    void program::accept_statement(node* aAccept, region* aRegion)
    {
        auto* name = aAccept->child(0);
        // (an expanded name (illegal, diagnosed below): its selector)
        auto const entryName = name_text((name->is(construct::Selected) && !name->children.empty() ? name->children.back() : name)->source);
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
                        // (a tagged incomplete view: allowed (3.10.1(8.3/2)))
                        incomplete_use use{ *this, incomplete_uses::TaggedOnly };
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
            // (the innermost task body's task's entries only (9.5.2(14)))
            if (r->owner != nullptr && r->owner->sub != nullptr && r->owner->owner != nullptr && r->owner->owner->kind == entity_kind::Task)
                break;
        }
        if (entry == nullptr)
            error(name, "entry \"" + entryName + "\" not found");
        // (several entries of the same name: the one whose profile matches the accept's parameters)
        name->ent = entry;
        aAccept->ent = entry;
        // (the entry's direct name, in the task body (not in a subprogram, package or protected unit within it, nor in an accept
        // statement for the same entry) (9.5.2(13)-(15)))
        if (name->is(construct::Selected))
            recover([&]() { error(name, "an accept statement must name the entry by its direct name"); });
        for (auto* x = aRegion; x != nullptr; x = x->parent)
        {
            if (x->owner != nullptr && x->owner->sub != nullptr && x->owner->owner != nullptr && x->owner->owner->kind == entity_kind::Task)
                break;      // (the task body)
            if (x->is_package || (x->owner != nullptr && (x->owner->kind == entity_kind::Protected || x->owner->kind == entity_kind::Task)) ||
                (x->owner != nullptr && x->owner->sub != nullptr && x->owner->kind != entity_kind::Entry && x->is_subprogram))
            {
                recover([&]() { error(aAccept, "an accept statement must be directly within the task body of its entry's task"); });
                break;
            }
            if (x->owner == entry)
            {
                recover([&]() { error(aAccept, "an accept statement cannot be within an accept statement for the same entry"); });
                break;
            }
        }
        // (its formal part: fully conformant with the entry declaration's (9.5.2(16)): the same names, modes, subtypes and default
        // expressions)
        if (iInstanceBodies == 0 && entry->sub != nullptr && entry->sub->spec != nullptr)
        {
            struct formal { node* name; parameter_mode mode; node* subtype; node* initial; };
            auto const formals = [&](node* aParent)
            {
                std::vector<formal> result;
                for (auto* p : aParent->children)
                {
                    if (!p->is(construct::Parameter))
                        continue;
                    auto mode = p->has_flag("out") ? (p->has_flag("in") ? parameter_mode::InOut : parameter_mode::Out) : parameter_mode::In;
                    node* subtype = nullptr;
                    node* initial = nullptr;
                    for (auto* c : p->children)
                        if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                        {
                            if (subtype == nullptr)
                                subtype = c;
                            else if (initial == nullptr)
                                initial = c;
                        }
                    for (auto* d : p->all(construct::Defining))
                        result.push_back({ d, mode, subtype, initial });
                }
                return result;
            };
            auto const declared = formals(entry->sub->spec);
            auto const accepted = formals(aAccept);
            if (declared.size() == accepted.size())
                for (std::size_t k = 0u; k < declared.size(); ++k)
                {
                    auto const& a = declared[k];
                    auto const& b = accepted[k];
                    if (b.initial != nullptr && b.initial->type == nullptr && k < entry->sub->parameters.size())
                        recover([&]() { resolve(b.initial, entry->sub->parameters[k].type, aRegion); });
                    if (b.initial != nullptr && k < entry->sub->parameters.size())
                        others_check(b.initial, entry->sub->parameters[k].type);   // (an unconstrained array formal's default: no applicable index constraint)
                    bool const conformant = name_text(a.name->source) == name_text(b.name->source) && a.mode == b.mode &&
                        (k >= profile.size() || profile[k] == nullptr || k >= entry->sub->parameters.size() || entry->sub->parameters[k].type == nullptr ||
                            statically_matching(entry->sub->parameters[k].type, profile[k])) &&
                        (a.initial == nullptr) == (b.initial == nullptr) && (a.initial == nullptr || fully_conformant(a.initial, b.initial));
                    if (!conformant)
                    {
                        recover([&]() { error(b.name, "the accept statement's formal part must be fully conformant with the entry declaration's (9.5.2(16))"); });
                        break;
                    }
                }
        }
        auto* r = new_region(entry, aRegion);
        r->owner = nullptr;
        r->master = true;   // (an accept statement: a master (7.6.1(3/2)))
        iStatementRegions[aAccept] = r;
        iAcceptEntries[r] = entry;
        // (not within an accept statement for the same entry (9.5.2(14)))
        for (auto* p = aAccept->parent; p != nullptr && !p->is(construct::TaskBody); p = p->parent)
        {
            // (nor within an asynchronous select (9.5.2(14)))
            if (p->is(construct::SelectAsynchronous) && iInstanceBodies == 0)
            {
                recover([&]() { error(aAccept, "an accept statement cannot be within an asynchronous select"); });
                break;
            }
            if (p->is(construct::Accept))
                if (auto outer = iStatementRegions.find(p); outer != iStatementRegions.end())
                    if (auto e = iAcceptEntries.find(outer->second); e != iAcceptEntries.end() && e->second == entry && iInstanceBodies == 0)
                    {
                        recover([&]() { error(aAccept, "an accept statement cannot be within an accept statement for the same entry"); });
                        break;
                    }
        }
        r->level = aRegion->level;
        bool familyIndex = false;
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
            else if (c->is(construct::End))
                ;   // (the entry's name repeated: see end_name_check)
            else if (c != aAccept->children.back() || !c->is(construct::Identifier) || aAccept->first(construct::Handled) == nullptr)
            {
                // (the family index: an entry family's only (9.5.2(9)))
                if (entry->sub->family_type != nullptr)
                {
                    // (one index, an expression (not a range: no slices of a family))
                    if (familyIndex || c->is(construct::Range))
                        recover([&]() { error(c, "an accept statement for an entry family must give one entry index (an expression)"); });
                    else
                        resolve(c, entry->sub->family_type, aRegion);
                    familyIndex = true;
                }
                else
                    recover([&]() { error(c, "an accept statement for an entry that is not a family cannot have an index"); });
            }
        }
        if (entry->sub->family_type != nullptr && !familyIndex)
            recover([&]() { error(aAccept, "an accept statement for an entry family must give an entry index"); });
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
        // (the type of an aggregate: not abstract (3.9.3(8/3)))
        if (abstract_type(aExpected, aRegion) && !aAggregate->is(construct::AggregateDelta))
            error(aAggregate, "the type of an aggregate cannot be abstract");
        // (a record aggregate's type: not a partial view, nor a record extension of one (where its full view is not visible) (4.3.1(8/2)))
        if (aAggregate->is(construct::Aggregate) || aAggregate->is(construct::AggregateNullRecord))
        {
            auto const hidden = [&](ada_type* aView)
            {
                if (aView == nullptr)
                    return false;
                auto* partial = aView->kind == type_kind::Private ? aView : aView->base != nullptr && aView->base->kind == type_kind::Private ? aView->base : nullptr;
                if (partial == nullptr || partial->is_formal || partial->incomplete)
                    return false;
                auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                return package == nullptr || !private_part_visible(package, aRegion) || partial->full == nullptr;
            };
            bool bad = hidden(aExpected);
            for (auto* x = t->base_type(); !bad && x != nullptr && x->parent != nullptr; x = x->parent->view()->base_type())
            {
                bad = hidden(x->parent);
                // (derived from a partial view where its full view was not visible: its parent's full view is visible for it only within
                // its own package's private part or body (AI05-0115-1))
                if (!bad && x->parent_partial)
                {
                    auto* package = x->declared_in != nullptr ? x->declared_in->owner : nullptr;
                    bad = package == nullptr || !private_part_visible_here(package, aRegion);
                }
                if (x->parent->view()->base_type() == x)
                    break;
            }
            if (bad && t->kind == type_kind::Record)
                error(aAggregate, "the type of a record aggregate cannot be a private type or an extension of one (where its full view is not visible)");
        }
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
                limited_value_check(ancestor, ancestorType, aRegion);
            }
            // (the ancestor: of a tagged type (an expression: resolved without the aggregate's type, not dynamically tagged; a subtype
            // mark: not class-wide), a proper ancestor of the aggregate's type, not through a private extension (4.3.2(4/2-5/3)); the
            // aggregate's type: not a partial view (4.3.2(5/3)))
            if (iSpeculation == 0 && iInstanceBodies == 0)
            {
                bool const subtypeMark = meaning_of(ancestor) == meaning::Type;
                if (!subtypeMark)
                {
                    std::vector<ada_type*> tagged;
                    for (auto const& o : interpretations(ancestor, aRegion))
                        if (o.type != nullptr && (o.type->view()->tagged || o.type->view()->kind == type_kind::ClassWide) &&
                            std::none_of(tagged.begin(), tagged.end(), [&](ada_type* x) { return same_type(x, o.type); }))
                            tagged.push_back(o.type);
                    if (tagged.size() > 1u)
                        error(ancestor, "the ancestor expression of an extension aggregate is ambiguous (it is resolved without the aggregate's type, 4.3.2(4/2))");
                    if (dynamically_tagged(ancestor))
                        error(ancestor, "the ancestor expression of an extension aggregate cannot be dynamically tagged (4.3.2(5/3))");
                }
                // (a subtype mark: not of a view with unknown discriminants (its own, or inherited by extensions without a known discriminant
                // part) (4.3.2(5.1/3)))
                if (subtypeMark && ancestorType != nullptr)
                {
                    int steps = 0;
                    for (auto* x = ancestorType; x != nullptr && ++steps < 64; )
                    {
                        auto* xb = x->base_type();
                        auto* partial = xb->kind == type_kind::Private ? xb : xb->partial;
                        if (xb->unknown_discriminants || (partial != nullptr && partial->unknown_discriminants))
                        {
                            error(ancestor, "the ancestor subtype of an extension aggregate cannot denote a view with unknown discriminants (4.3.2(5.1/3))");
                            break;
                        }
                        if (xb->known_discriminants || (partial != nullptr && partial->known_discriminants))
                            break;
                        x = xb->parent != nullptr ? xb->parent : xb->view() != xb ? xb->view()->parent : nullptr;
                    }
                }
                auto* av = ancestorType != nullptr ? ancestorType->view() : nullptr;
                if (av != nullptr && av->kind == type_kind::ClassWide)
                {
                    if (subtypeMark)
                        error(ancestor, "the ancestor subtype of an extension aggregate cannot be class-wide (4.3.2(5/3))");
                }
                else if (av != nullptr && !av->base_type()->view()->tagged && !av->is_formal)
                    error(ancestor, "the ancestor of an extension aggregate must be of a tagged type (4.3.2(4/2))");
                else if (av != nullptr && !av->is_formal && !t->is_formal)
                {
                    auto* ab = av->base_type()->view();
                    if (same_type(ancestorType, aExpected) || is_descendant(ancestorType, aExpected))
                        error(ancestor, "the ancestor type of an extension aggregate must be a proper ancestor of the aggregate's type (4.3.2(5/3))");
                    else if (!is_descendant(aExpected, ancestorType))
                        error(ancestor, "the ancestor type of an extension aggregate must be an ancestor of the aggregate's type (4.3.2(5/3))");
                    else
                    {
                        // (the derivation from the ancestor: not through a private extension (whose full view is not visible here))
                        auto const hidden = [&](ada_type* aView)
                        {
                            if (aView == nullptr)
                                return false;
                            auto* partial = aView->kind == type_kind::Private ? aView : aView->base != nullptr && aView->base->kind == type_kind::Private ? aView->base :
                                aView->base_type()->partial;
                            if (partial == nullptr || partial->is_formal || partial->incomplete)
                                return false;
                            auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                            return package == nullptr || !private_part_visible(package, aRegion) || partial->full == nullptr;
                        };
                        bool through = false;
                        int steps = 0;
                        for (auto* x = aExpected; x != nullptr && !through && ++steps < 64; )
                        {
                            auto* xb = x->view()->base_type()->view();
                            if (xb == ab)
                                break;
                            through = hidden(x) || hidden(xb);
                            x = xb->parent;
                        }
                        if (through)
                            error(aAggregate, "the type of an extension aggregate cannot be derived from its ancestor through a private extension (4.3.2(5/3))");
                    }
                }
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
            if (!record->components[c].stored && (ancestorComponents == 0u || !in_ancestor_part(record->components[c], c, ancestorType)))
                order.push_back(c);
        std::size_t positional = 0u;
        // (discriminant values (static) choose the variant: the components of other variants are not in the aggregate)
        std::map<entity*, bigint> discriminantValues;
        // (a discriminant's value; the parent's (stored) discriminants that the derivation's constraint gives it to (see
        // component_info::stored) have it too)
        auto const set_discriminant = [&](entity* aDiscriminant, bigint const& aValue)
        {
            discriminantValues[aDiscriminant] = aValue;
            for (auto const& sc : record->components)
                if (sc.stored && sc.ent != nullptr && sc.default_expression != nullptr && sc.default_expression->is(construct::Identifier) &&
                    (sc.default_expression->ent == aDiscriminant || name_text(sc.default_expression->source) == aDiscriminant->name))
                    discriminantValues[sc.ent] = aValue;
            // (the parent's discriminants that the derivation's constraint gives it to (a type with new discriminants))
            if (auto* parent = record->parent; parent != nullptr && !parent->discriminant_constraint.empty())
            {
                std::vector<entity*> parentDiscriminants;
                for (auto const& pc : parent->base_type()->view()->components)
                    if (pc.discriminant && !pc.stored)
                        parentDiscriminants.push_back(pc.ent);
                for (std::size_t i = 0u; i < parent->discriminant_constraint.size() && i < parentDiscriminants.size(); ++i)
                {
                    auto* x = parent->discriminant_constraint[i];
                    while (x != nullptr && (x->is(construct::Association) || x->is(construct::Parenthesized)) && !x->children.empty())
                        x = x->children.back();
                    if (x != nullptr && x->is(construct::Identifier) && (x->ent == aDiscriminant || name_text(x->source) == aDiscriminant->name))
                        discriminantValues[parentDiscriminants[i]] = aValue;
                }
            }
        };
        // (a variant is on the path that the discriminants' values select (unknown values: assumed))
        auto const path_selected = [&](int aVariant) -> bool
        {
            for (int v = aVariant; v >= 0; v = record->variants[static_cast<std::size_t>(v)].parent)
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
        auto const in_variant = [&](component_info const& aComponent) -> bool
        {
            return path_selected(aComponent.variant);
        };
        // (the value of a discriminant that governs a variant part: static, unless the variant part is within a variant that the other
        // discriminants do not select (4.3.1(17/5)))
        auto const governing = [&](std::size_t aComponent, node* aValue)
        {
            for (auto const& variant : record->variants)
                if (variant.discriminant == record->components[aComponent].ent && path_selected(variant.parent) && !static_expression(aValue))
                    error(aValue, "the value of a discriminant governing a variant part must be static (4.3.1(17/5))");  // (the aggregate's other components: unknown)
        };
        // (a parent's discriminant that a derivation's constraint gives a nonstatic value, governing a variant part (4.3.1(17/5)))
        if (iInstanceBodies == 0)
            for (auto const& sc : record->components)
                if (sc.stored && sc.ent != nullptr && sc.default_expression != nullptr && !static_expression(sc.default_expression))
                    for (auto const& variant : record->variants)
                        if (variant.discriminant == sc.ent && path_selected(variant.parent))
                            error(aAggregate, "the value of a discriminant governing a variant part must be static (4.3.1(17/5))");
        bool named = false;
        for (std::size_t i = first; i < aAggregate->size(); ++i)
        {
            auto* item = aAggregate->child(i);
            if (item->is(construct::Association))
            {
                named = true;
                auto* choices = item->child(0);
                auto* value = item->children.back();
                bool const box = value->is(construct::Box);
                for (auto* choice : choices->children)
                {
                    if (choice->is(construct::Others))
                    {
                        // (others: alone, in the last association (4.3.1(3)))
                        if (choices->size() != 1u)
                            error(choice, "others must be the only choice of its association");
                        if (i + 1u != aAggregate->size())
                            error(choice, "others must be the last association");
                        // (discriminants first: their values choose the variant of the other components it stands for)
                        std::size_t associated = 0u;
                        for (auto c : order)
                            if (!present[c] && record->components[c].discriminant)
                            {
                                ++associated;
                                present[c] = true;
                                values[c] = box ? nullptr : value;
                                // (<> for a discriminant: only one with a default (4.3.1(17.1/5)))
                                if (box && record->components[c].default_expression == nullptr && iInstanceBodies == 0 && record->components[c].ent != nullptr)
                                    recover([&]() { error(choice, "the discriminant \"" + record->components[c].ent->name + "\" has no default: <> cannot be its value (4.3.1)"); });
                                if (!box)
                                {
                                    resolve(value, record->components[c].type, aRegion);
                                    governing(c, value);
                                    if (auto v = evaluate(value, aRegion); v.is_integer())
                                        set_discriminant(record->components[c].ent, v.integer());
                                }
                            }
                        for (auto c : order)
                            if (!present[c] && in_variant(record->components[c]))
                            {
                                ++associated;
                                present[c] = true;
                                values[c] = box ? nullptr : value;
                            }
                        // (an association has a component (but others => <>) (4.3.1(16/3)))
                        if (associated == 0u && !box)
                            recover([&]() { error(choice, "others has no associated component"); });
                        continue;
                    }
                    auto const name = name_text(choice->source);
                    bool found = false;
                    for (auto c : order)
                        if (record->components[c].ent != nullptr && record->components[c].ent->name == name)
                        {
                            // (each component once (4.3.1(16/3)))
                            if (present[c])
                                error(choice, "component \"" + name + "\" is given more than once");
                            present[c] = true;
                            values[c] = box ? nullptr : value;
                            choice->ent = record->components[c].ent;
                            found = true;
                            // (<> for a discriminant: only one with a default (4.3.1(17.1/5)))
                            if (record->components[c].discriminant && box && record->components[c].default_expression == nullptr && iInstanceBodies == 0)
                                recover([&]() { error(choice, "the discriminant \"" + name + "\" has no default: <> cannot be its value (4.3.1)"); });
                            if (record->components[c].discriminant && !box)
                            {
                                resolve(value, record->components[c].type, aRegion);
                                governing(c, value);
                                auto v = evaluate(value, aRegion);
                                if (v.is_integer())
                                    set_discriminant(record->components[c].ent, v.integer());
                            }
                        }
                    if (!found)
                        error(choice, "\"" + name + "\" is not a component of the type");
                }
                // (its components: of the same type (4.3.1(16/3)))
                if (!box)
                {
                    ada_type* same = nullptr;
                    bool differ = false;
                    for (auto c : order)
                        if (values[c] == value && record->components[c].type != nullptr)
                        {
                            if (same == nullptr)
                                same = record->components[c].type;
                            else if (!same_type(same, record->components[c].type) &&
                                !(same->anonymous_access && record->components[c].type->anonymous_access && same->designated != nullptr &&
                                    record->components[c].type->designated != nullptr && same_type(same->designated, record->components[c].type->designated)))
                                differ = true;
                        }
                    if (differ)
                        recover([&]() { error(item, "the components of an association with an expression must be of the same type"); });
                }
            }
            else
            {
                // (positional associations first (4.3.1(3)); no box (4.3.1(4/2)); not a single one (a parenthesized expression))
                if (named)
                    error(item, "a positional association cannot follow a named one");
                if (item->is(construct::Box))
                    recover([&]() { error(item, "a box (<>) can only be given in a named association"); });
                if (aAggregate->size() == 1u)
                    recover([&]() { error(item, "a record aggregate with one association must use named notation"); });
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
                    governing(c, item);
                    auto v = evaluate(item, aRegion);
                    if (v.is_integer())
                        set_discriminant(record->components[c].ent, v.integer());
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
            limited_value_check(values[c], componentType, aRegion);
        }
        for (auto c : order)
            if (!present[c] && in_variant(record->components[c]) && !aAggregate->is(construct::AggregateNullRecord))
            {
                // (a component not given (4.3.1(16/3)); one of a variant: if the discriminants that govern it are known)
                bool decided = true;
                for (int v = record->components[c].variant; v >= 0 && decided; v = record->variants[static_cast<std::size_t>(v)].parent)
                    decided = discriminantValues.contains(record->variants[static_cast<std::size_t>(v)].discriminant);
                if (record->components[c].ent != nullptr && decided)
                    error(aAggregate, "no value given for component \"" + record->components[c].ent->name + "\"");
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
            {
                resolve(aValue, t->component, aRegion);
                limited_value_check(aValue, t->component, aRegion);
            }
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
        // (the form of the associations (4.3.3(3-6, 17/3)): positional ones, then others; or named ones; others alone, last; a choice
        // that is not static only if it is the single choice of the only association)
        bool requireStatic = false;
        {
            std::size_t associations = 0u;
            std::size_t choices = 0u;
            bool positional = false;
            bool named = false;
            for (std::size_t i = 0u; i < aAggregate->size(); ++i)
            {
                auto* item = aAggregate->child(i);
                if (item->is(construct::Association))
                {
                    ++associations;
                    for (auto* choice : item->child(0)->children)
                    {
                        ++choices;
                        if (choice->is(construct::Others))
                        {
                            if (item->child(0)->size() != 1u)
                                error(choice, "others must be the only choice of its association");
                            if (i + 1u != aAggregate->size())
                                error(choice, "others must be the last association");
                        }
                        else
                            named = true;
                    }
                }
                else if (item->is(construct::Iterated))
                    named = true, ++associations, ++choices;
                else
                {
                    positional = true;
                    if (named)
                        error(item, "a positional association cannot follow a named one");
                }
            }
            if (positional && named)
                error(aAggregate, "an array aggregate cannot have both positional and named associations (other than others)");
            requireStatic = associations > 1u || choices > 1u || positional;
        }
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
                    {
                        auto* ct = discrete_range(choice, indexType, aRegion);
                        // (a nonstatic subtype with a predicate: not a choice (3.2.4(29.4/4)))
                        if (ct != nullptr && (choice->is(construct::SubtypeIndication) || is_subtype_name(choice, aRegion)) && !static_subtype(ct))
                            predicate_check(ct, choice);
                    }
                    else
                        resolve(choice, indexType->base_type(), aRegion);
                    if (requireStatic && !static_choice(choice, indexType, aRegion))
                        error(choice, "a choice of an array aggregate with more than one choice must be static");
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
        // (named static choices: no null range among several, no value covered twice, and without others no missing values between
        // the smallest and the largest (4.3.3(18)))
        // (a choice that is a subtype with a static predicate: covers only the values that satisfy it, so it too must be contiguous when
        // it is the only one (4.3.3))
        auto const predicated = [&](node* aChoice) -> ada_type*
        {
            auto* ct = aChoice->type;
            if (ct != nullptr && (aChoice->is(construct::SubtypeIndication) || meaning_of(aChoice) == meaning::Type) &&
                (ct->predicate != nullptr || ct->view()->predicate != nullptr))
                return ct;
            return nullptr;
        };
        bool const singlePredicated = !requireStatic && [&]()
        {
            for (auto* item : aAggregate->children)
                if (item->is(construct::Association))
                    for (auto* choice : item->child(0)->children)
                        if (predicated(choice) != nullptr)
                            return true;
            return false;
        }();
        if ((requireStatic || singlePredicated) && iInstanceBodies == 0 && iSpeculation == 0)
            recover([&]()
            {
                std::vector<std::tuple<bigint, bigint, node*>> covered;
                bool others = false;
                for (auto* item : aAggregate->children)
                {
                    if (item->is(construct::Iterated) || !item->is(construct::Association))
                        return;
                    for (auto* choice : item->child(0)->children)
                    {
                        if (choice->is(construct::Others))
                        {
                            others = true;
                            continue;
                        }
                        if (auto* ct = predicated(choice))
                        {
                            auto values = static_values(ct, aRegion);
                            if (!values)
                                return;
                            for (auto const& [l, h] : *values)
                                covered.emplace_back(l, h, choice);
                            continue;
                        }
                        auto [low, high] = choice_range(choice, indexType->base_type(), aRegion);
                        if (!low || !high)
                            return;
                        if (*low > *high)
                            error(choice, "a null range choice is allowed only as the single choice of an array aggregate");
                        covered.emplace_back(*low, *high, choice);
                    }
                }
                std::sort(covered.begin(), covered.end(), [](auto const& a, auto const& b) { return std::get<0>(a) < std::get<0>(b); });
                for (std::size_t i = 1u; i < covered.size(); ++i)
                {
                    if (std::get<0>(covered[i]) <= std::get<1>(covered[i - 1u]))
                    {
                        auto* a = std::get<2>(covered[i]);
                        auto* b = std::get<2>(covered[i - 1u]);
                        error(a->source.data() < b->source.data() ? b : a, "a value is covered by more than one choice of the array aggregate");
                    }
                    if (!others && std::get<0>(covered[i]) > std::get<1>(covered[i - 1u]) + bigint{ 1 })
                        error(aAggregate, "the choices of an array aggregate without others must cover a contiguous range of values");
                }
            });
        aAggregate->type = aType;
        return aType;
    }
}
