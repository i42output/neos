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
                        // (in the handler's region: its choice parameter hides an exception of its name (8.3))
                        auto* e = resolve_entity(c, iInstanceBodies == 0 ? r : aRegion);
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
                // (a discrete subtype definition (5.5(5)))
                if (t != nullptr && !t->view()->is_discrete() && !t->view()->is_formal && t->view()->kind != type_kind::Private)
                    recover([&]() { error(range, "the range of a loop parameter specification must be discrete"); });
                // (not a subtype with a dynamic predicate, nor a nonstatic one with a predicate (3.2.4(29.3/4)))
                if (t != nullptr && !static_subtype(t))
                    predicate_check(t, range);
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
                        recover([&]() { error(aCase, "others must be in the last alternative"); });
                }
        }
        // (each value of the selector's subtype covered once (5.4(6-10)): the nominal subtype of a name whose subtype is static,
        // otherwise the base range)
        if (aType != nullptr && complete && aType->view()->is_discrete())
            recover([&]()
            {
                std::sort(covered.begin(), covered.end(), [](auto const& a, auto const& b) { return std::get<0>(a) < std::get<0>(b); });
                for (std::size_t i = 1u; i < covered.size(); ++i)
                    if (std::get<0>(covered[i]) <= std::get<1>(covered[i - 1u]))
                    {
                        // (reported at the later of the two choices)
                        auto* a = std::get<2>(covered[i]);
                        auto* b = std::get<2>(covered[i - 1u]);
                        error(a->source.data() < b->source.data() ? b : a, "a value is covered by more than one case choice");
                    }
                auto* s = aType->view();
                // (a name (an object, a function call, a type conversion, a qualified expression) (5.4(7/4)))
                bool const name = aSelector->is(construct::Identifier) || aSelector->is(construct::Selected) ||
                    aSelector->is(construct::Qualified) || aSelector->is(construct::Apply) ||
                    (aSelector->is(construct::Attribute) && aSelector->size() >= 2u && name_text(aSelector->child(1)->source) == "old");
                bool const nominal = name && static_subtype(aType);
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
                    if (range.first != range.second && range.first->second->kind != entity_kind::Loop && range.first->second->kind != entity_kind::Block)
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
                if (!is_variable(target))
                    recover([&]() { error(target, "the target of an assignment must be a variable"); });
                // (the target's type: not abstract (3.9.3(8/3)))
                if (abstract_type(t, aRegion))
                    recover([&]() { error(target, "the target of an assignment cannot be of an abstract type"); });
                // (not of a limited type (7.5(8/2)))
                else if (t != nullptr && iInstanceBodies == 0 && !t->view()->is_formal && limited_type(t, aRegion))
                    recover([&]() { error(target, "the target of an assignment cannot be of a limited type"); });
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
                for (auto const& o : options)
                    if (o.type != nullptr && o.type->view()->is_discrete() && !o.type->is_universal())
                        t = o.type;
                recover([&]() { t = resolve(selector, t, aRegion); });
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
                    auto* e = new_entity(entity_kind::Loop, name_text(name->source), name, aRegion);
                    aRegion->add(e);
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
                // (the return subtype: not abstract (3.9.3(8/3)))
                if (abstract_type(t, aRegion))
                    recover([&]() { error(typeNode, "the return object cannot be of an abstract type"); });
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
                if (!accept)
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
        auto* r = new_region(entry, aRegion);
        r->owner = nullptr;
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
        // (the value of a discriminant that governs a variant part: static (4.3.1(17/5)))
        auto const governing = [&](std::size_t aComponent, node* aValue)
        {
            for (auto const& variant : record->variants)
                if (variant.discriminant == record->components[aComponent].ent && !static_expression(aValue))
                {
                    recover([&]() { error(aValue, "the value of a discriminant governing a variant part must be static"); });
                    return;
                }
        };
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
                                if (!box)
                                {
                                    resolve(value, record->components[c].type, aRegion);
                                    governing(c, value);
                                    if (auto v = evaluate(value, aRegion); v.is_integer())
                                        discriminantValues[record->components[c].ent] = v.integer();
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
                            if (record->components[c].discriminant && !box)
                            {
                                resolve(value, record->components[c].type, aRegion);
                                governing(c, value);
                                auto v = evaluate(value, aRegion);
                                if (v.is_integer())
                                    discriminantValues[record->components[c].ent] = v.integer();
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
        if (requireStatic && iInstanceBodies == 0 && iSpeculation == 0)
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
