/*
  stmts.cpp

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

// C: statements (6.8): their constraints, scopes (a compound statement's, and C23's scopes of selection and iteration statements),
// labels, and the statements that break, continue, case and default refer to

#include <algorithm>
#include "program.hpp"

namespace neos::compilers::c
{
    void program::compound(node* aCompound, bool aNewScope)
    {
        if (aNewScope)
            push_scope();
        for (auto* item : aCompound->children)
            block_item(item);
        if (aNewScope)
            pop_scope();
    }

    void program::block_item(node* aItem)
    {
        switch (aItem->kind)
        {
        case construct::Declaration:
            declaration(aItem, false);
            break;
        case construct::StaticAssert:
            static_assertion(aItem);
            break;
        case construct::Label:
        case construct::Case:
        case construct::Default:
        {
            // (a label on its own (C23: a label at the end of a compound statement, or before a declaration))
            iSyntheticNodes.emplace_back();
            auto& empty = iSyntheticNodes.back();
            empty.kind = construct::ExpressionStatement;
            empty.unit = aItem->unit;
            empty.source = std::string_view{ aItem->source.data() + aItem->source.size(), 0u };
            empty.parent = aItem;
            aItem->children.push_back(&empty);
            aItem->mark = 1u;   // (a label alone: its statement is synthetic)
            statement(aItem);
            break;
        }
        default:
            statement(aItem);
            break;
        }
    }

    void program::statement(node* aStatement)
    {
        auto* s = aStatement;
        auto* f = iFunction != nullptr ? iFunction->function : nullptr;
        switch (s->kind)
        {
        case construct::Compound:
            compound(s);
            return;
        case construct::ExpressionStatement:
            for (auto* c : s->children)
                if (!c->is(construct::Attributes))
                    expression(c);
            return;
        case construct::Labeled:
        {
            // (a label, then its statement)
            auto* label = s->child(0);
            auto* body = s->child(1);
            if (label->is(construct::Label))
            {
                auto const name = name_of(label->first(construct::Name));
                if (iLabels.contains(name))
                    error(label, "duplicate label '" + name + "'");
                iLabels[name] = s;
            }
            else
            {
                // (case and default: of the innermost switch)
                if (iSwitches.empty())
                    error(label, label->is(construct::Case) ? "case label not within a switch statement" : "'default' label not within a switch statement");
                auto* sw = iSwitches.back();
                label->target = sw;
                if (label->is(construct::Case))
                {
                    // (its value (a case range (GNU): its first and last))
                    for (auto* value : label->children)
                        if (!value->is(construct::Attributes))
                        {
                            expression(value);
                            (void)integer_constant(value, "case label");
                        }
                }
                // (a case label not in the top level of its switch statement's body: the switch's code is generated by the general method)
                auto* parent = s->parent;
                bool topLevel = false;
                while (parent != nullptr && parent->is(construct::Labeled))
                    parent = parent->parent;
                if (parent != nullptr && parent->is(construct::Compound) && parent->parent == sw)
                    topLevel = true;
                if (parent == sw)
                    topLevel = true;
                if (!topLevel && f != nullptr)
                    f->irregularSwitch = true;
            }
            statement(body);
            return;
        }
        case construct::Label:
        case construct::Case:
        case construct::Default:
        {
            // (a label alone (block_item): as a labeled statement whose statement is empty)
            auto* body = s->children.back();
            node* label = s;
            if (label->is(construct::Label))
            {
                auto const name = name_of(label->first(construct::Name));
                if (iLabels.contains(name))
                    error(label, "duplicate label '" + name + "'");
                iLabels[name] = s;
            }
            else
            {
                if (iSwitches.empty())
                    error(label, label->is(construct::Case) ? "case label not within a switch statement" : "'default' label not within a switch statement");
                auto* sw = iSwitches.back();
                label->target = sw;
                if (label->is(construct::Case))
                {
                    node* value = nullptr;
                    for (auto* c : label->children)
                        if (!c->is(construct::Attributes) && c != body)
                            value = c;
                    expression(value);
                    integer_constant(value, "case label");
                }
                auto* parent = s->parent;
                if (!(parent != nullptr && parent->is(construct::Compound) && parent->parent == sw) && f != nullptr)
                    f->irregularSwitch = true;
            }
            statement(body);
            return;
        }
        case construct::If:
        {
            // (C23: a selection statement is a block, and so are its secondary blocks)
            push_scope();
            condition(s->child(0));
            push_scope();
            statement(s->child(1));
            pop_scope();
            if (auto* e = s->child(2); e != nullptr && e->is(construct::Else))
            {
                push_scope();
                statement(e->child(0));
                pop_scope();
            }
            pop_scope();
            return;
        }
        case construct::Switch:
        {
            push_scope();
            auto* selector = s->child(0);
            expression(selector);
            if (!value_type(selector)->is_integer())
                error(selector, "switch quantity not an integer");
            iBreakables.push_back(s);
            iSwitches.push_back(s);
            push_scope();
            statement(s->child(1));
            pop_scope();
            iSwitches.pop_back();
            iBreakables.pop_back();
            pop_scope();
            return;
        }
        case construct::While:
        {
            push_scope();
            condition(s->child(0));
            iBreakables.push_back(s);
            ++iLoopDepth;
            push_scope();
            statement(s->child(1));
            pop_scope();
            --iLoopDepth;
            iBreakables.pop_back();
            pop_scope();
            return;
        }
        case construct::Do:
        {
            push_scope();
            iBreakables.push_back(s);
            ++iLoopDepth;
            push_scope();
            statement(s->child(0));
            pop_scope();
            --iLoopDepth;
            iBreakables.pop_back();
            condition(s->child(1));
            pop_scope();
            return;
        }
        case construct::For:
        {
            push_scope();
            auto* init = s->child(0);
            auto* cond = s->child(1);
            auto* next = s->child(2);
            auto* body = s->child(3);
            if (init->child(0) != nullptr)
            {
                if (init->child(0)->is(construct::Declaration))
                {
                    auto* d = init->child(0);
                    for (auto* c : d->children)
                        if (c->is(construct::Storage) && c->source != "auto" && c->source != "register")
                            error(d, "declaration of non-automatic variable in 'for' loop initial declaration");
                    declaration(d, false);
                }
                else
                    expression(init->child(0));
            }
            if (cond->child(0) != nullptr)
                condition(cond->child(0));
            if (next->child(0) != nullptr)
                expression(next->child(0));
            iBreakables.push_back(s);
            ++iLoopDepth;
            push_scope();
            statement(body);
            pop_scope();
            --iLoopDepth;
            iBreakables.pop_back();
            pop_scope();
            return;
        }
        case construct::Goto:
            if (f != nullptr)
                f->hasGoto = true;
            iGotos.push_back(s);
            return;
        case construct::Continue:
        {
            node* loop = nullptr;
            for (auto i = iBreakables.rbegin(); i != iBreakables.rend(); ++i)
                if (!(*i)->is(construct::Switch))
                {
                    loop = *i;
                    break;
                }
            if (loop == nullptr)
                error(s, "continue statement not within a loop");
            s->target = loop;
            return;
        }
        case construct::Break:
            if (iBreakables.empty())
                error(s, "break statement not within loop or switch");
            s->target = iBreakables.back();
            return;
        case construct::Return:
        {
            auto const result = iFunction->type->base;
            if (auto* value = s->child(0); value != nullptr)
            {
                expression(value);
                if (result->is_void())
                {
                    if (!value_type(value)->is_void())
                        error(s, "'return' with a value, in function returning void");
                }
                else
                    check_assignable(result, value, s, "return");
            }
            else if (!result->is_void())
                warning(s, "'return' with no value, in function returning non-void");
            return;
        }
        case construct::Declaration:
            declaration(s, false);
            return;
        default:
            error(s, "expected statement");
        }
    }
}
