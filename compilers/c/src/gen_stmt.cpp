/*
  gen_stmt.cpp

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

// C: the code generation of statements and of automatic objects' declarations. Control flow is structured (WebAssembly blocks
// and loops): loops and switch statements as blocks that break and continue leave; a switch's dispatch (a br_table, or 
// comparisons) to the blocks before its case labels' statements; a label that gotos jump to: a region of its compound statement's
// items (a block ending at the label for gotos before it, a loop starting at it for gotos after it)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::c
{
    using namespace emitter;

    namespace
    {
        // (a labeled statement's label and its statement, through nested labels)
        node* label_of(node* aItem)
        {
            if (aItem->is(construct::Labeled))
                return aItem->child(0);
            if (aItem->is(construct::Label) || aItem->is(construct::Case) || aItem->is(construct::Default))
                return aItem;
            return nullptr;
        }

        node* labeled_statement(node* aItem)
        {
            if (aItem->is(construct::Labeled))
                return aItem->child(1);
            return aItem->children.back();
        }

        void find_gotos(node* aNode, std::vector<node*>& aGotos)
        {
            if (aNode->is(construct::Goto))
                aGotos.push_back(aNode);
            for (auto* c : aNode->children)
                find_gotos(c, aGotos);
        }

        bool contains_vla_declaration(node* aItem)
        {
            if (!aItem->is(construct::Declaration))
                return false;
            for (auto* c : aItem->children)
                if (c->is(construct::InitDeclarator) && c->ent != nullptr && c->ent->kind == entity_kind::Object &&
                    c->ent->where == object_storage::Dynamic)
                    return true;
            return false;
        }
    }

    // --- automatic objects

    void function_generator::initialize(qtype aType, node* aInitializer, std::function<void()> const& aAddress, std::uint64_t aOffset, bool aZeroed)
    {
        auto const at = [&](std::uint64_t aAt)
        {
            aAddress();
            if (aAt != 0u)
            {
                i64(static_cast<std::int64_t>(aAt));
                op(opcode::I64Add);
            }
        };
        if (aInitializer->is(construct::Braced) && !aZeroed)
        {
            at(aOffset);
            zero(iProgram.size_of(aType));
        }
        initializer_visitor visitor;
        visitor.scalar = [&](qtype aT, node* aValue, std::uint64_t aAt)
        {
            at(aAt);
            value_as(aValue, aT);
            store(aT);
            statement_end();
        };
        visitor.bits = [&](member_info const& aMember, node* aValue, std::uint64_t aAt)
        {
            at(aAt);
            value_as(aValue, aMember.type);
            bit_field_store(aMember, false);
            statement_end();
        };
        visitor.characters = [&](qtype aT, node* aStrings, std::uint64_t aAt)
        {
            auto const arraySize = aT->length ? *aT->length * iProgram.size_of(aT->base) : aStrings->bytes.size();
            auto const n = std::min<std::uint64_t>(aStrings->bytes.size(), arraySize);
            at(aAt);
            i64(static_cast<std::int64_t>(iProgram.string_literal_address(aStrings)));
            copy(n);
            if (arraySize > n && !aInitializer->is(construct::Braced) && !aZeroed)
            {
                at(aAt + n);
                zero(arraySize - n);
            }
        };
        visitor.record = [&](qtype aT, node* aValue, std::uint64_t aAt)
        {
            at(aAt);
            address(aValue);
            copy(iProgram.size_of(aT));
            statement_end();
        };
        iProgram.walk_initializer(aType, aInitializer, aOffset, visitor);
    }

    void function_generator::declare_object(entity* aObject, node* aDeclarator)
    {
        auto* e = aObject;
        (void)aDeclarator;
        if (e->where == object_storage::Static || e->kind != entity_kind::Object)
            return;
        if (e->where == object_storage::Dynamic && !e->parameter)
        {
            // (a variable length array: its size evaluated, its storage allocated on the stack)
            iDynamicStack = true;
            iFrameUsed = true;
            // (reached again in its block (by a jump back): the stack pointer as it was before its storage was allocated)
            get(e->markLocal);
            op(opcode::I64Eqz);
            begin_if();
            op(opcode::GlobalGet);
            u32(iProgram.sp_global());
            set(e->markLocal);
            else_();
            get(e->markLocal);
            op(opcode::GlobalSet);
            u32(iProgram.sp_global());
            end();
            dynamic_size(e->type);
            set(e->sizeLocal);
            op(opcode::GlobalGet);
            u32(iProgram.sp_global());
            get(e->sizeLocal);
            op(opcode::I64Sub);
            i64(-16);
            op(opcode::I64And);
            tee(e->local);
            op(opcode::GlobalSet);
            u32(iProgram.sp_global());
            get(e->local);
            op(opcode::GlobalGet);
            u32(iProgram.limit_global());
            op(opcode::I64LtU);
            begin_if();
            runtime("c::rt.stack_overflow", {}, type::Void);
            op(opcode::Unreachable);
            end();
            if (e->initializer != nullptr)
            {
                // (= {}: zeroed)
                get(e->local);
                i32(0);
                get(e->sizeLocal);
                libc("memset");
                drop();
            }
            return;
        }
        if (e->initializer == nullptr)
            return;
        auto* init = e->initializer;
        if (e->where == object_storage::Local)
        {
            if (init->is(construct::Braced))
            {
                if (init->children.empty())
                {
                    constant zero;
                    zero.k = constant::kind::Integer;
                    constant_value(zero, e->type);
                }
                else
                    value_as(init->child(0), e->type);
            }
            else
                value_as(init, e->type);
            set(e->local);
            statement_end();
            return;
        }
        if (!init->is(construct::Braced) && !e->type->is_aggregate())
        {
            object_address(e);
            value_as(init, e->type);
            store(e->type);
            statement_end();
            return;
        }
        initialize(e->type, init, [&]() { object_address(e); }, 0u, false);
        statement_end();
    }

    void function_generator::declaration(node* aDeclaration)
    {
        for (auto* c : aDeclaration->children)
            if (c->is(construct::InitDeclarator) && c->ent != nullptr && c->ent->kind == entity_kind::Object)
                declare_object(c->ent, c);
    }

    // --- labels

    bool function_generator::plan_labels(std::vector<node*> const& aItems, std::vector<label_region>& aRegions)
    {
        // (the labels among a compound statement's items that gotos jump to: each goto must be within one of the items; a goto before
        // the label: a block from the first such item to the label; after it (or within its statement): a loop from the label to
        // the last such item. The regions must nest)
        aRegions.clear();
        std::map<node const*, std::size_t> labelIndex;
        for (std::size_t i = 0u; i < aItems.size(); ++i)
            if (auto* l = label_of(aItems[i]); l != nullptr && l->is(construct::Label))
                labelIndex[aItems[i]] = i;
        if (labelIndex.empty())
            return true;
        std::map<node const*, std::pair<std::size_t, std::size_t>> forward;    // (first item, label index)
        std::map<node const*, std::pair<std::size_t, std::size_t>> backward;   // (label index, last item)
        for (std::size_t i = 0u; i < aItems.size(); ++i)
        {
            std::vector<node*> gotos;
            find_gotos(aItems[i], gotos);
            for (auto* g : gotos)
            {
                auto const found = labelIndex.find(g->target);
                if (found == labelIndex.end())
                    continue;   // (a label of another compound statement: its own plan)
                auto const k = found->second;
                if (i < k)
                {
                    auto& r = forward.try_emplace(g->target, i, k).first->second;
                    r.first = std::min(r.first, i);
                }
                else
                {
                    auto& r = backward.try_emplace(g->target, k, i).first->second;
                    r.second = std::max(r.second, i);
                }
            }
        }
        for (auto const& [l, r] : forward)
            aRegions.push_back(label_region{ l, r.first, r.second, false });
        for (auto const& [l, r] : backward)
            aRegions.push_back(label_region{ l, r.first, r.second + 1u, true });
        // (forward blocks that overlap: the later begins where the earlier does)
        for (bool changed = true; changed;)
        {
            changed = false;
            for (auto& a : aRegions)
                for (auto& b : aRegions)
                    if (&a != &b && a.first < b.first && b.first < a.last && a.last < b.last)
                    {
                        if (!b.backward)
                        {
                            b.first = a.first;
                            changed = true;
                        }
                        else if (!a.backward)
                        {
                            // (a loop that begins in a block and ends after it: the block cannot end at its label inside the loop)
                            return false;
                        }
                        else
                        {
                            a.last = b.last;
                            changed = true;
                        }
                    }
        }
        // (proper nesting)
        for (auto const& a : aRegions)
            for (auto const& b : aRegions)
                if (&a != &b && a.first < b.first && b.first < a.last && a.last < b.last)
                    return false;
        return true;
    }

    // --- statements

    void function_generator::block_items(std::vector<node*> const& aItems)
    {
        std::vector<label_region> regions;
        if (!plan_labels(aItems, regions))
            throw dispatch_needed{ aItems.empty() ? nullptr : aItems.front(), "goto that the C compiler cannot structure (jumps into overlapping regions): not supported yet" };
        // (at each item: the regions that begin there (outermost (last ending) first))
        for (std::size_t i = 0u; i < aItems.size(); ++i)
        {
            // (regions that end here: closed (innermost first))
            std::vector<label_region const*> starting;
            for (auto const& r : regions)
                if (r.first == i)
                    starting.push_back(&r);
            std::sort(starting.begin(), starting.end(), [](auto* a, auto* b) { return a->last > b->last || (a->last == b->last && a->backward && !b->backward); });
            for (auto* r : starting)
            {
                begin(r->backward ? control_kind::Loop : control_kind::Block);
                iControls.back().label = r->label;
            }
            statement(aItems[i]);
            // (regions that end after this item)
            for (;;)
            {
                bool closed = false;
                for (auto const& r : regions)
                    if (r.last == i + 1u && !iControls.empty() && iControls.back().label == r.label &&
                        (iControls.back().kind == control_kind::Loop) == r.backward)
                    {
                        end();
                        closed = true;
                        break;
                    }
                if (!closed)
                    break;
            }
        }
    }

    void function_generator::reset_vla_marks(std::vector<node*> const& aItems)
    {
        // (a block entered: its variable length arrays not yet allocated)
        for (auto* item : aItems)
            if (contains_vla_declaration(item))
                for (auto* c : item->children)
                    if (c->is(construct::InitDeclarator) && c->ent != nullptr && c->ent->kind == entity_kind::Object && c->ent->where == object_storage::Dynamic)
                    {
                        i64(0);
                        set(c->ent->markLocal);
                    }
    }

    void function_generator::declaration_cleanups(node* aDeclaration)
    {
        // (gnu::cleanup: the functions of the objects that a declaration declares, in reverse order)
        if (aDeclaration == nullptr || !aDeclaration->is(construct::Declaration))
            return;
        for (auto c = aDeclaration->children.rbegin(); c != aDeclaration->children.rend(); ++c)
            if ((*c)->is(construct::InitDeclarator) && (*c)->ent != nullptr && (*c)->ent->cleanup != nullptr)
            {
                auto* e = (*c)->ent;
                assign_storage(e);
                object_address(e);
                call(iProgram.function_index(e->cleanup));
                if (!e->cleanup->type->base->is_void())
                    op(opcode::Drop);
            }
    }

    void function_generator::cleanups(node* aCompound, std::size_t aFirst, std::size_t aEnd)
    {
        for (std::size_t i = std::min(aEnd, aCompound->size()); i-- > aFirst;)
            declaration_cleanups(aCompound->child(i));
    }

    void function_generator::jump_cleanups(node* aJump, node* aTarget)
    {
        // (the blocks that enclose the jump, innermost first: the objects declared before the jump's item; of a block that encloses the
        // target too: those declared from the target's item (a jump back over their declarations))
        if (!iInfo.hasCleanups)
            return;
        auto const item_index = [](node* aBlock, node* aNode) -> std::optional<std::size_t>
        {
            for (auto* n = aNode; n != nullptr && n->parent != nullptr; n = n->parent)
                if (n->parent == aBlock)
                    for (std::size_t i = 0u; i < aBlock->size(); ++i)
                        if (aBlock->child(i) == n)
                            return i;
            return std::nullopt;
        };
        auto const encloses = [](node* aOuter, node* aNode)
        {
            for (auto* n = aNode; n != nullptr; n = n->parent)
                if (n == aOuter)
                    return true;
            return false;
        };
        for (auto* n = aJump->parent; n != nullptr; n = n->parent)
        {
            if (n->is(construct::For))
            {
                // (a for statement's declaration: left by a jump out of the statement (its own break: its end's (see statement)))
                if (n != aTarget && !(aTarget != nullptr && encloses(n, aTarget)) && !encloses(n->child(0), aJump))
                    declaration_cleanups(n->child(0)->child(0));
                continue;
            }
            if (!n->is(construct::Compound))
                continue;
            auto const jumpItem = item_index(n, aJump);
            if (!jumpItem)
                continue;
            auto const targetItem = aTarget != nullptr ? item_index(n, aTarget) : std::nullopt;
            if (targetItem)
            {
                // (the target is in this block: it and the blocks around it are not left)
                if (*targetItem < *jumpItem)
                    cleanups(n, *targetItem, *jumpItem);
                break;
            }
            cleanups(n, 0u, *jumpItem);
        }
    }

    void function_generator::compound(node* aCompound)
    {
        std::vector<node*> items{ aCompound->children.begin(), aCompound->children.end() };
        bool const stackMark = std::any_of(items.begin(), items.end(), contains_vla_declaration);
        if (stackMark)
            reset_vla_marks(items);
        if (stackMark)
        {
            auto const mark = local(type::I64);
            op(opcode::GlobalGet);
            u32(iProgram.sp_global());
            set(mark);
            begin(control_kind::Block);
            iControls.back().stackMark = mark;
            block_items(items);
            if (iInfo.hasCleanups)
                cleanups(aCompound, 0u, items.size());
            end();
            get(mark);
            op(opcode::GlobalSet);
            u32(iProgram.sp_global());
            return;
        }
        block_items(items);
        if (iInfo.hasCleanups)
            cleanups(aCompound, 0u, items.size());
    }

    void function_generator::switch_statement(node* aSwitch)
    {
        auto* s = aSwitch;
        auto* selector = s->child(0);
        auto* body = s->child(1);
        auto const selectorType = iProgram.promote(iProgram.value_type(selector));
        auto const k = kind_of(selectorType);
        auto const w = wasm_of(selectorType);
        auto const value = temporary(w);
        value_as(selector, selectorType);
        set(value);
        statement_end();
        // (the body's items (a statement that is not compound: one item), in segments that begin at case labels)
        std::vector<node*> items;
        if (body->is(construct::Compound))
            items.assign(body->children.begin(), body->children.end());
        else
            items.push_back(body);
        struct segment
        {
            std::vector<node*> labels;      ///< (its case and default labels)
            std::vector<node*> items;
        };
        std::vector<segment> segments;
        std::vector<node*> before;          ///< (items before the first case label: reached only by goto)
        if (iInfo.hasCleanups)
            for (auto* item : items)
                if (item->is(construct::Declaration))
                    for (auto* c : item->children)
                        if (c->ent != nullptr && c->ent->cleanup != nullptr)
                            throw compile_error{ c, "an object with attribute 'cleanup' in a switch's block (not in a block of its own) is not supported" };
        for (auto* item : items)
        {
            auto* l = label_of(item);
            if (l != nullptr && (l->is(construct::Case) || l->is(construct::Default)) && l->target == s)
            {
                segment seg;
                // (consecutive labels: case 1: case 2: ...)
                auto* x = item;
                for (;;)
                {
                    auto* xl = label_of(x);
                    if (xl == nullptr || !(xl->is(construct::Case) || xl->is(construct::Default)) || xl->target != s)
                        break;
                    seg.labels.push_back(xl);
                    x = labeled_statement(x);
                }
                seg.items.push_back(x);
                segments.push_back(seg);
                continue;
            }
            if (segments.empty())
                before.push_back(item);
            else
                segments.back().items.push_back(item);
        }
        // (the cases' values (converted to the promoted selector's type; a case range (GNU): its first and last), and the default)
        struct case_range { std::int64_t low; std::int64_t high; std::size_t segment; };
        std::vector<case_range> cases;
        std::optional<std::size_t> defaultSegment;
        bool const isSigned = iProgram.integer_type(selectorType.type)->is_signed();
        auto const less = [&](std::int64_t a, std::int64_t b)
        {
            return isSigned ? a < b : static_cast<std::uint64_t>(a) < static_cast<std::uint64_t>(b);
        };
        for (std::size_t i = 0u; i < segments.size(); ++i)
            for (auto* l : segments[i].labels)
            {
                if (l->is(construct::Default))
                {
                    if (defaultSegment)
                        throw compile_error{ l, "multiple default labels in one switch" };
                    defaultSegment = i;
                    continue;
                }
                std::vector<node*> valueNodes;
                for (auto* c : l->children)
                    if (!c->is(construct::Attributes) && !c->is(construct::ExpressionStatement))
                        valueNodes.push_back(c);
                std::vector<std::int64_t> values;
                for (auto* valueNode : valueNodes)
                {
                    auto const v = iProgram.evaluate(valueNode, true);
                    std::int64_t n = v->s();
                    // (the value as the selector's promoted type's)
                    auto const width = iProgram.width_of(selectorType.type);
                    if (width < 64u)
                    {
                        auto const mask = (std::uint64_t{ 1 } << width) - 1u;
                        auto u = static_cast<std::uint64_t>(n) & mask;
                        if (isSigned && (u >> (width - 1u)) != 0u)
                            u |= ~mask;
                        n = static_cast<std::int64_t>(u);
                    }
                    values.push_back(n);
                }
                case_range r{ values.front(), values.back(), i };
                if (less(r.high, r.low))
                {
                    iProgram.warning(l, "empty range specified");
                    continue;
                }
                for (auto const& c : cases)
                    if (!less(r.high, c.low) && !less(c.high, r.low))
                        throw compile_error{ l, values.size() > 1u ? "duplicate (or overlapping) case value" : "duplicate case value" };
                cases.push_back(r);
            }
        // (blocks: the switch's (break leaves it), then one for each segment (its end: the segment's code), the innermost the first's)
        begin(control_kind::Block);
        iControls.back().breakOf = s;
        auto const breakControl = iControls.size() - 1u;
        std::vector<std::size_t> segmentControls(segments.size());
        for (std::size_t i = segments.size(); i-- > 0u;)
        {
            begin(control_kind::Block);
            segmentControls[i] = iControls.size() - 1u;
        }
        auto const targetOf = [&](std::optional<std::size_t> aSegment) { return aSegment ? segmentControls[*aSegment] : breakControl; };
        // (the dispatch: comparisons, a binary search of the sorted values (a few at its leaves compared in turn); none matching: the 
        // default (or the switch's end))
        auto sorted = cases;
        std::sort(sorted.begin(), sorted.end(), [&](auto const& a, auto const& b) { return less(a.low, b.low); });
        std::function<void(std::size_t, std::size_t)> dispatch = [&](std::size_t aLow, std::size_t aHigh)
        {
            if (aHigh - aLow <= 4u)
            {
                for (std::size_t i = aLow; i < aHigh; ++i)
                {
                    auto const& c = sorted[i];
                    get(value);
                    if (k == vkind::I64)
                    {
                        if (c.low == c.high)
                        {
                            i64(c.low);
                            op(opcode::I64Eq);
                        }
                        else
                        {
                            // (a range: (value - low) <= (high - low), unsigned)
                            i64(c.low);
                            op(opcode::I64Sub);
                            i64(static_cast<std::int64_t>(static_cast<std::uint64_t>(c.high) - static_cast<std::uint64_t>(c.low)));
                            op(opcode::I64LeU);
                        }
                    }
                    else
                    {
                        if (c.low == c.high)
                        {
                            i32(static_cast<std::int32_t>(c.low));
                            op(opcode::I32Eq);
                        }
                        else
                        {
                            i32(static_cast<std::int32_t>(c.low));
                            op(opcode::I32Sub);
                            i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(c.high) - static_cast<std::uint32_t>(c.low)));
                            op(opcode::I32LeU);
                        }
                    }
                    branch_to(segmentControls[c.segment], true);
                }
                branch_to(targetOf(defaultSegment));
                return;
            }
            auto const middle = (aLow + aHigh) / 2u;
            begin(control_kind::Block);
            get(value);
            if (k == vkind::I64)
            {
                i64(sorted[middle].low);
                op(isSigned ? opcode::I64GeS : opcode::I64GeU);
            }
            else
            {
                i32(static_cast<std::int32_t>(sorted[middle].low));
                op(isSigned ? opcode::I32GeS : opcode::I32GeU);
            }
            branch_to(iControls.size() - 1u, true);
            dispatch(aLow, middle);
            end();
            dispatch(middle, aHigh);
        };
        // (dense cases: a table (br_table) of the values from the least to the greatest; else the binary search)
        std::uint64_t entries = 0u;
        for (auto const& c : sorted)
            entries += static_cast<std::uint64_t>(c.high) - static_cast<std::uint64_t>(c.low) + 1u;
        auto const span = sorted.empty() ? 0u : static_cast<std::uint64_t>(sorted.back().high) - static_cast<std::uint64_t>(sorted.front().low) + 1u;
        bool const dense = sorted.size() >= 4u && span != 0u && span <= 4096u && span <= 4u * entries + 8u &&
            std::all_of(sorted.begin(), sorted.end(), [&](auto const& c) { return !less(sorted.back().high, c.high); });
        if (dense)
        {
            auto const low = sorted.front().low;
            std::vector<std::size_t> table(static_cast<std::size_t>(span), targetOf(defaultSegment));
            for (auto const& c : sorted)
                for (auto v = static_cast<std::uint64_t>(c.low) - static_cast<std::uint64_t>(low); v <= static_cast<std::uint64_t>(c.high) - static_cast<std::uint64_t>(low); ++v)
                    table[static_cast<std::size_t>(v)] = segmentControls[c.segment];
            get(value);
            if (k == vkind::I64)
            {
                // (the value's offset from the least: beyond the table (unsigned) is the default's)
                auto const offset = temporary(type::I64);
                i64(low);
                op(opcode::I64Sub);
                tee(offset);
                i64(static_cast<std::int64_t>(span));
                op(opcode::I64GeU);
                branch_to(targetOf(defaultSegment), true);
                get(offset);
                op(opcode::I32ConvertI64);
                release(offset);
            }
            else
            {
                i32(static_cast<std::int32_t>(low));
                op(opcode::I32Sub);
            }
            branch_table(table, targetOf(defaultSegment));
        }
        else
            dispatch(0u, sorted.size());
        release(value);
        // (the items before the first case label: reached by no case (only by a goto into the switch, which is not supported))
        for (auto* item : before)
        {
            if (item->is(construct::Declaration))
                continue;
        }
        for (std::size_t i = 0u; i < segments.size(); ++i)
        {
            end();
            block_items(segments[i].items);
        }
        end();
    }

    void function_generator::computed_goto(node* aGoto)
    {
        // (GNU: goto *p (a function with one is generated by the dispatch method): p a label's number (see program::function_definition);
        // the dispatch's state from a table of the labels' block numbers (in the static data), then the dispatch (all of the labels' 
        // blocks are entered by it))
        if (!iDispatch)
            throw compile_error{ aGoto, "internal error: a computed goto in a function generated by the structured method" };
        if (iInfo.hasCleanups)
            throw compile_error{ aGoto, "a computed goto in a function with an object with attribute 'cleanup' is not supported" };
        auto const& labels = iInfo.labelValues;
        if (labels.empty())
        {
            discard(aGoto->child(0));
            op(opcode::Unreachable);
            return;
        }
        std::vector<std::int32_t> numbers;
        for (auto* l : labels)
            numbers.push_back(block_number(l));
        auto& context = context_of(numbers[0]);
        for (auto n : numbers)
            if (&context_of(n) != &context)
                throw compile_error{ aGoto, "a computed goto to a label in a statement expression is not supported" };
        if (iPlanning)
            for (auto n : numbers)
                context.plan->entered.insert(n);
        std::uint64_t table = 0u;
        if (!iPlanning)
        {
            table = iProgram.static_data(4u * numbers.size(), 4u);
            for (std::size_t i = 0u; i < numbers.size(); ++i)
                for (std::size_t b = 0u; b < 4u; ++b)
                    iProgram.iStatic[table - language::c::layout::StaticBase + 4u * i + b] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(numbers[i]) >> (8u * b));
        }
        auto const index = temporary(type::I64);
        value_as(aGoto->child(0), iProgram.value_type(aGoto->child(0)));
        i64(1);
        op(opcode::I64Sub);
        tee(index);
        i64(static_cast<std::int64_t>(numbers.size()));
        op(opcode::I64GeU);
        op(opcode::If);
        iCode.push_back(0x40u);
        op(opcode::Unreachable);
        op(opcode::End);
        get(index);
        i64(2);
        op(opcode::I64Shl);
        i64(static_cast<std::int64_t>(table));
        op(opcode::I64Add);
        op(opcode::I32LoadMem);
        u32(0x40u);
        u32(language::c::layout::Memory);
        u32(0u);
        set(context.state);
        release(index);
        statement_end();
        restore_stack_to(context.loop);
        branch_to(context.loop);
    }

    void function_generator::return_statement(node* aReturn)
    {
        auto* value = aReturn->child(0);
        if (value != nullptr)
        {
            auto const result = iType->base;
            if (iReturnsRecord)
            {
                get(iReturnAddress);
                address(value);
                copy(iProgram.size_of(result));
            }
            else if (result->is_void())
                discard(value);
            else
            {
                value_as(value, result.unqualified());
                set(iResult);
            }
        }
        else if (!iType->base->is_void() && !iReturnsRecord)
        {
            // (a return without a value from a function with a result: 0)
            constant zero;
            zero.k = constant::kind::Integer;
            constant_value(zero, iType->base);
            set(iResult);
        }
        statement_end();
        jump_cleanups(aReturn, nullptr);
        branch_to(0u);
    }

    void function_generator::statement(node* aStatement)
    {
        auto* s = aStatement;
        if (iDispatch && s != iStructured && contains_target(s))
        {
            dispatched_statement(s);
            return;
        }
        switch (s->kind)
        {
        case construct::ExpressionStatement:
            for (auto* c : s->children)
                if (!c->is(construct::Attributes))
                    discard(c);
            statement_end();
            return;
        case construct::Compound:
            compound(s);
            return;
        case construct::Labeled:
        case construct::Label:
        case construct::Case:
        case construct::Default:
        {
            auto* l = label_of(s);
            if (l->is(construct::Case) || l->is(construct::Default))
                throw dispatch_needed{ l, "a case label within a nested statement of its switch" };
            statement(labeled_statement(s));
            return;
        }
        case construct::Declaration:
            declaration(s);
            return;
        case construct::StaticAssert:
            return;
        case construct::If:
        {
            truth(s->child(0));
            statement_end();
            begin_if();
            statement(s->child(1));
            if (auto* e = s->child(2); e != nullptr && e->is(construct::Else))
            {
                else_();
                statement(e->child(0));
            }
            end();
            return;
        }
        case construct::While:
        {
            begin(control_kind::Block);
            iControls.back().breakOf = s;
            begin(control_kind::Loop);
            auto const top = iControls.size() - 1u;
            truth(s->child(0));
            statement_end();
            op(opcode::I32Eqz);
            branch_to(top - 1u, true);
            begin(control_kind::Block);
            iControls.back().continueOf = s;
            statement(s->child(1));
            end();
            branch_to(top);
            end();
            end();
            return;
        }
        case construct::Do:
        {
            begin(control_kind::Block);
            iControls.back().breakOf = s;
            begin(control_kind::Loop);
            auto const top = iControls.size() - 1u;
            begin(control_kind::Block);
            iControls.back().continueOf = s;
            statement(s->child(0));
            end();
            truth(s->child(1));
            statement_end();
            branch_to(top, true);
            end();
            end();
            return;
        }
        case construct::For:
        {
            auto* init = s->child(0);
            auto* cond = s->child(1);
            auto* next = s->child(2);
            auto* body = s->child(3);
            if (auto* i = init->child(0); i != nullptr)
            {
                if (i->is(construct::Declaration))
                    declaration(i);
                else
                {
                    discard(i);
                    statement_end();
                }
            }
            begin(control_kind::Block);
            iControls.back().breakOf = s;
            begin(control_kind::Loop);
            auto const top = iControls.size() - 1u;
            if (auto* c = cond->child(0); c != nullptr)
            {
                truth(c);
                statement_end();
                op(opcode::I32Eqz);
                branch_to(top - 1u, true);
            }
            begin(control_kind::Block);
            iControls.back().continueOf = s;
            statement(body);
            end();
            if (auto* n = next->child(0); n != nullptr)
            {
                discard(n);
                statement_end();
            }
            branch_to(top);
            end();
            end();
            if (iInfo.hasCleanups)
                declaration_cleanups(init->child(0));
            return;
        }
        case construct::Switch:
            switch_statement(s);
            return;
        case construct::Break:
        {
            jump_cleanups(s, s->target);
            if (auto const d = iDispatchedBreaks.find(s->target); d != iDispatchedBreaks.end())
            {
                jump(d->second);
                return;
            }
            auto const target = find_break(s->target);
            restore_stack_to(target);
            branch_to(target);
            return;
        }
        case construct::Continue:
        {
            jump_cleanups(s, s->target);
            if (auto const d = iDispatchedContinues.find(s->target); d != iDispatchedContinues.end())
            {
                jump(d->second);
                return;
            }
            auto const target = find_continue(s->target);
            restore_stack_to(target);
            branch_to(target);
            return;
        }
        case construct::Goto:
        {
            jump_cleanups(s, s->target);
            if (iDispatch)
            {
                jump(block_number(s->target));
                return;
            }
            auto const target = find_label(s->target);
            if (target == ~std::size_t{})
                throw dispatch_needed{ s, "goto into a block (or a nested statement)" };
            restore_stack_to(target);
            branch_to(target);
            return;
        }
        case construct::Return:
            return_statement(s);
            return;
        case construct::ComputedGoto:
            computed_goto(s);
            return;
        default:
            throw compile_error{ s, "internal error: no code for statement" };
        }
    }

    // --- the dispatch method

    void function_generator::dispatch_body()
    {
        if (iInfo.usesSetjmp)
        {
            iSetjmpValue = local(type::I32);
            // (a frame of its own: its address identifies the function's activation (setjmp's environment))
            (void)frame_object(16u, 16u);
        }
        begin_dispatch(nullptr);
        dispatched_statement(iInfo.body);
        end_dispatch();
    }

    void function_generator::dispatched_items(node* aOwner, std::vector<node*> const& aItems)
    {
        // (a statement expression's statements, with a label: in a dispatch loop of its own (a goto may leave it, not enter it))
        begin_dispatch(aOwner);
        for (auto* item : aItems)
            dispatched_statement(item);
        end_dispatch();
    }

    void function_generator::begin_dispatch(node const* aOwner)
    {
        // (the blocks of a dispatch loop: the code of each follows the end of a block (in the loop) that the blocks after it enclose,
        // the innermost the first's; the dispatch (the code before the innermost block's end) branches out of the block before the 
        // code of the block whose number is the state. A block's code falls through to the next's; a jump forward branches out of 
        // the block before its target; a jump back sets the state and branches to the loop's start. Planning: each block's code in 
        // an if (whether the state is its number), in turn)
        iContexts.push_back(dispatch_context{ aOwner, local(type::I32) });
        auto& context = iContexts.back();
        context.plan = &iPlan->loops[aOwner];
        auto const first = new_block_number();
        i32(first);
        set(context.state);
        begin(control_kind::Loop);
        context.loop = iControls.size() - 1u;
        if (iPlanning)
        {
            context.plan->layout.clear();
            context.plan->entered.clear();
            context.plan->entered.insert(first);
            open_block(first);
            return;
        }
        auto const& layout = context.plan->layout;
        if (layout.empty() || layout.front() != first)
            throw compile_error{ aOwner, "internal error: the dispatch method's plan" };
        for (std::size_t p = 0u; p < layout.size(); ++p)
            context.positions[layout[p]] = p;
        context.blocks.resize(layout.size());
        for (std::size_t p = layout.size(); p-- > 0u;)
        {
            begin(control_kind::Block);
            context.blocks[p] = iControls.size() - 1u;
        }
        // (the dispatch: a binary search of the numbers of the blocks that it enters)
        std::vector<std::int32_t> const entered{ context.plan->entered.begin(), context.plan->entered.end() };
        std::function<void(std::size_t, std::size_t)> dispatch = [&](std::size_t aLow, std::size_t aHigh)
        {
            if (aHigh - aLow <= 4u)
            {
                for (std::size_t i = aLow; i < aHigh; ++i)
                {
                    get(context.state);
                    i32(entered[i]);
                    op(opcode::I32Eq);
                    branch_to(context.blocks[context.positions.at(entered[i])], true);
                }
                branch_to(context.blocks[0]);
                return;
            }
            auto const middle = (aLow + aHigh) / 2u;
            begin(control_kind::Block);
            get(context.state);
            i32(entered[middle]);
            op(opcode::I32GeS);
            branch_to(iControls.size() - 1u, true);
            dispatch(aLow, middle);
            end();
            dispatch(middle, aHigh);
        };
        // (the dispatch: a table (br_table) of the block numbers, if not too large; else a binary search of those that it enters)
        if (!entered.empty() && entered.back() >= 0 && entered.back() < 4096)
        {
            std::vector<std::size_t> table(static_cast<std::size_t>(entered.back()) + 1u, context.blocks[0]);
            for (auto const n : entered)
                table[static_cast<std::size_t>(n)] = context.blocks[context.positions.at(n)];
            get(context.state);
            branch_table(table, context.blocks[0]);
        }
        else
            dispatch(0u, entered.size());
        open_block(first);
    }

    void function_generator::end_dispatch()
    {
        auto& context = iContexts.back();
        if (iControls.size() - 1u != context.block)
            throw compile_error{ context.owner, "internal error: a block of the dispatch method within a statement's control" };
        if (iPlanning)
            end();
        end();
        iContexts.pop_back();
    }

    node const* function_generator::owner_of(node const* aTarget) const
    {
        for (auto const* n = aTarget; n != nullptr; n = n->parent)
            if (n->is(construct::StatementExpression))
                return n;
        return nullptr;
    }

    function_generator::dispatch_context& function_generator::context_of(std::int32_t aNumber)
    {
        auto const owner = iNumberOwners.find(aNumber);
        for (auto i = iContexts.size(); i-- > 0u;)
            if (owner == iNumberOwners.end() || iContexts[i].owner == owner->second)
                return iContexts[i];
        throw compile_error{ owner->second, "a jump into a statement expression" };
    }

    bool function_generator::contains_target(node* aNode)
    {
        auto const existing = iContainsTarget.find(aNode);
        if (existing != iContainsTarget.end())
            return existing->second;
        std::function<bool(node*, node const*)> search = [&](node* aSearched, node const* aExcluded) -> bool
        {
            if (aSearched->is(construct::Label))
                return true;
            if (aSearched->builtin == static_cast<std::uint32_t>(builtin_kind::Setjmp))
                return true;
            if ((aSearched->is(construct::Case) || aSearched->is(construct::Default)) && aSearched->target != aExcluded &&
                aSearched->target != nullptr && dispatched_switch(const_cast<node*>(aSearched->target)))
                return true;
            for (auto* c : aSearched->children)
                if (c != nullptr && search(c, aExcluded))
                    return true;
            return false;
        };
        bool const result = search(aNode, nullptr);
        iContainsTarget[aNode] = result;
        return result;
    }

    bool function_generator::dispatched_switch(node* aSwitch)
    {
        // (a switch statement with a case label that is not in its body's top level, or whose body contains another target (a 
        // label, a call of setjmp, or a case label of a dispatched switch))
        auto const existing = iDispatchedSwitches.find(aSwitch);
        if (existing != iDispatchedSwitches.end())
            return existing->second;
        iDispatchedSwitches[aSwitch] = false;   // (recursion)
        bool result = false;
        std::function<void(node*)> search = [&](node* aSearched)
        {
            if (result)
                return;
            if (aSearched->is(construct::Label) || aSearched->builtin == static_cast<std::uint32_t>(builtin_kind::Setjmp))
            {
                result = true;
                return;
            }
            if (aSearched->is(construct::Case) || aSearched->is(construct::Default))
            {
                if (aSearched->target == aSwitch)
                {
                    // (in the body's top level: the body, or an item of it (through labels))
                    node* x = aSearched;
                    if (x->parent != nullptr && x->parent->is(construct::Labeled) && x->parent->child(0) == x)
                        x = x->parent;
                    while (x->parent != nullptr && x->parent->is(construct::Labeled))
                        x = x->parent;
                    if (!(x->parent == aSwitch || (x->parent != nullptr && x->parent->is(construct::Compound) && x->parent->parent == aSwitch)))
                    {
                        result = true;
                        return;
                    }
                }
                else if (aSearched->target != nullptr && dispatched_switch(const_cast<node*>(aSearched->target)))
                {
                    result = true;
                    return;
                }
            }
            for (auto* c : aSearched->children)
                if (c != nullptr)
                    search(c);
        };
        search(aSwitch->child(1));
        iDispatchedSwitches[aSwitch] = result;
        return result;
    }

    std::int32_t function_generator::block_number(node const* aTarget)
    {
        auto const existing = iBlockNumbers.find(aTarget);
        if (existing != iBlockNumbers.end())
            return existing->second;
        auto const number = iNextNumber++;
        iNumberOwners[number] = owner_of(aTarget);
        return iBlockNumbers[aTarget] = number;
    }

    std::int32_t function_generator::new_block_number()
    {
        auto const number = iNextNumber++;
        iNumberOwners[number] = iContexts.back().owner;
        return number;
    }

    void function_generator::open_block(std::int32_t aNumber)
    {
        auto& context = iContexts.back();
        if (iPlanning)
        {
            context.plan->layout.push_back(aNumber);
            context.opened.insert(aNumber);
            get(context.state);
            i32(aNumber);
            op(opcode::I32Eq);
            begin_if();
            context.block = iControls.size() - 1u;
            return;
        }
        // (the end of the block before the block's code)
        auto const p = context.positions.find(aNumber);
        if (p == context.positions.end() || iControls.size() - 1u != context.blocks[p->second])
            throw compile_error{ context.owner, "internal error: the dispatch method's plan" };
        end();
        context.position = p->second;
        context.block = p->second + 1u < context.blocks.size() ? context.blocks[p->second + 1u] : context.loop;
    }

    void function_generator::next_block(std::int32_t aNumber)
    {
        auto& context = iContexts.back();
        if (iControls.size() - 1u != context.block)
            throw compile_error{ nullptr, "internal error: a block of the dispatch method within a statement's control" };
        if (iPlanning)
        {
            i32(aNumber);
            set(context.state);
            end();
        }
        open_block(aNumber);
    }

    void function_generator::jump(std::int32_t aNumber)
    {
        auto& context = context_of(aNumber);
        if (iPlanning)
        {
            // (a jump to a block begun already: back (by the dispatch))
            if (context.opened.contains(aNumber))
                context.plan->entered.insert(aNumber);
        }
        else if (auto const p = context.positions.at(aNumber); p > context.position)
        {
            // (forward: out of the block whose end begins the target's code)
            restore_stack_to(context.blocks[p]);
            branch_to(context.blocks[p]);
            return;
        }
        i32(aNumber);
        set(context.state);
        restore_stack_to(context.loop);
        branch_to(context.loop);
    }

    void function_generator::prepare_setjmp(node* aExpression)
    {
        if (aExpression == nullptr)
            return;
        std::vector<node*> calls;
        std::function<void(node*)> search = [&](node* aSearched)
        {
            if (aSearched->builtin == static_cast<std::uint32_t>(builtin_kind::Setjmp) && !iPreparedSetjmps.contains(aSearched))
                calls.push_back(aSearched);
            for (auto* c : aSearched->children)
                if (c != nullptr)
                    search(c);
        };
        search(aExpression);
        if (calls.empty())
            return;
        if (calls.size() > 1u)
            throw compile_error{ calls[1], "more than one call of setjmp in an expression is not supported" };
        // (the environment: the frame's address (the activation's), the number of the block that resumes, the stack pointer)
        auto* c = calls[0];
        if (iContexts.size() > 1u)
            throw compile_error{ c, "setjmp in a statement expression is not supported" };
        auto const resume = new_block_number();
        qtype const longType{ iProgram.basic(type_kind::Long) };
        auto const environment = temporary(type::I64);
        rvalue(c->child(1));
        set(environment);
        get(environment);
        get(iFramePointer);
        store(longType, 0u);
        get(environment);
        i64(resume);
        store(longType, 8u);
        get(environment);
        op(opcode::GlobalGet);
        u32(iProgram.sp_global());
        store(longType, 16u);
        release(environment);
        i32(0);
        set(iSetjmpValue);
        iPreparedSetjmps.insert(c);
        if (iPlanning)
            iContexts.back().plan->entered.insert(resume);   // (longjmp's resumption: by the dispatch)
        next_block(resume);
    }

    void function_generator::dispatched_statement(node* aStatement)
    {
        auto* s = aStatement;
        if (!contains_target(s))
        {
            statement(s);
            return;
        }
        // (a conditional jump: if the i32 on the stack is 0 (or not))
        auto const jump_if = [&](bool aIfZero, std::int32_t aNumber)
        {
            if (aIfZero)
                op(opcode::I32Eqz);
            begin_if();
            jump(aNumber);
            end();
        };
        switch (s->kind)
        {
        case construct::Compound:
        {
            // (variable length arrays: the stack pointer restored at its end (jumps out of it: at the function's end))
            bool const stackMark = std::any_of(s->children.begin(), s->children.end(), contains_vla_declaration);
            std::optional<std::uint32_t> mark;
            if (stackMark)
                reset_vla_marks(std::vector<node*>{ s->children.begin(), s->children.end() });
            if (stackMark)
            {
                mark = local(type::I64);
                op(opcode::GlobalGet);
                u32(iProgram.sp_global());
                set(*mark);
            }
            for (auto* item : s->children)
                dispatched_statement(item);
            if (iInfo.hasCleanups)
                cleanups(s, 0u, s->size());
            if (mark)
            {
                get(*mark);
                op(opcode::GlobalSet);
                u32(iProgram.sp_global());
            }
            return;
        }
        case construct::Labeled:
        case construct::Label:
        case construct::Case:
        case construct::Default:
        {
            auto* l = label_of(s);
            next_block(block_number(l->is(construct::Label) ? s : l));
            dispatched_statement(labeled_statement(s));
            return;
        }
        case construct::ExpressionStatement:
        case construct::Declaration:
        case construct::Return:
            prepare_setjmp(s);
            iStructured = s;
            statement(s);
            return;
        case construct::If:
        {
            prepare_setjmp(s->child(0));
            auto const otherwise = new_block_number();
            auto const after = new_block_number();
            auto* e = s->child(2);
            bool const hasElse = e != nullptr && e->is(construct::Else);
            truth(s->child(0));
            statement_end();
            jump_if(true, hasElse ? otherwise : after);
            dispatched_statement(s->child(1));
            if (hasElse)
            {
                jump(after);
                next_block(otherwise);
                dispatched_statement(e->child(0));
            }
            next_block(after);
            return;
        }
        case construct::While:
        {
            auto const top = new_block_number();
            auto const after = new_block_number();
            iDispatchedBreaks[s] = after;
            iDispatchedContinues[s] = top;
            next_block(top);
            prepare_setjmp(s->child(0));
            truth(s->child(0));
            statement_end();
            jump_if(true, after);
            dispatched_statement(s->child(1));
            jump(top);
            next_block(after);
            return;
        }
        case construct::Do:
        {
            auto const top = new_block_number();
            auto const next = new_block_number();
            auto const after = new_block_number();
            iDispatchedBreaks[s] = after;
            iDispatchedContinues[s] = next;
            next_block(top);
            dispatched_statement(s->child(0));
            next_block(next);
            prepare_setjmp(s->child(1));
            truth(s->child(1));
            statement_end();
            jump_if(false, top);
            next_block(after);
            return;
        }
        case construct::For:
        {
            auto* init = s->child(0);
            auto* cond = s->child(1);
            auto* next = s->child(2);
            auto* body = s->child(3);
            if (auto* i = init->child(0); i != nullptr)
            {
                prepare_setjmp(i);
                if (i->is(construct::Declaration))
                    declaration(i);
                else
                {
                    discard(i);
                    statement_end();
                }
            }
            auto const top = new_block_number();
            auto const step = new_block_number();
            auto const after = new_block_number();
            iDispatchedBreaks[s] = after;
            iDispatchedContinues[s] = step;
            next_block(top);
            if (auto* c = cond->child(0); c != nullptr)
            {
                prepare_setjmp(c);
                truth(c);
                statement_end();
                jump_if(true, after);
            }
            dispatched_statement(body);
            next_block(step);
            if (auto* n = next->child(0); n != nullptr)
            {
                prepare_setjmp(n);
                discard(n);
                statement_end();
            }
            jump(top);
            next_block(after);
            if (iInfo.hasCleanups)
                declaration_cleanups(init->child(0));
            return;
        }
        case construct::Switch:
            prepare_setjmp(s->child(0));
            if (dispatched_switch(s))
                dispatched_switch_statement(s);
            else
                switch_statement(s);
            return;
        default:
            iStructured = s;
            statement(s);
            return;
        }
    }

    void function_generator::dispatched_switch_statement(node* aSwitch)
    {
        // (the selector compared with each case's value (or range), a match jumping to its block; none matching: to the default's 
        // (or after the switch))
        auto* s = aSwitch;
        auto* selector = s->child(0);
        auto* body = s->child(1);
        auto const selectorType = iProgram.promote(iProgram.value_type(selector));
        auto const k = kind_of(selectorType);
        auto const value = temporary(wasm_of(selectorType));
        value_as(selector, selectorType);
        set(value);
        statement_end();
        bool const isSigned = iProgram.integer_type(selectorType.type)->is_signed();
        auto const width = iProgram.width_of(selectorType.type);
        auto const as_selector = [&](std::int64_t n)
        {
            if (width < 64u)
            {
                auto const mask = (std::uint64_t{ 1 } << width) - 1u;
                auto u = static_cast<std::uint64_t>(n) & mask;
                if (isSigned && (u >> (width - 1u)) != 0u)
                    u |= ~mask;
                n = static_cast<std::int64_t>(u);
            }
            return n;
        };
        std::vector<node*> labels;
        std::function<void(node*)> search = [&](node* aSearched)
        {
            if ((aSearched->is(construct::Case) || aSearched->is(construct::Default)) && aSearched->target == s)
                labels.push_back(aSearched);
            for (auto* c : aSearched->children)
                if (c != nullptr)
                    search(c);
        };
        search(body);
        node* defaultLabel = nullptr;
        std::vector<std::pair<std::int64_t, std::int64_t>> seen;
        for (auto* l : labels)
        {
            if (l->is(construct::Default))
            {
                if (defaultLabel != nullptr)
                    throw compile_error{ l, "multiple default labels in one switch" };
                defaultLabel = l;
                continue;
            }
            std::vector<std::int64_t> values;
            for (auto* c : l->children)
                if (!c->is(construct::Attributes) && !c->is(construct::ExpressionStatement))
                    values.push_back(as_selector(iProgram.evaluate(c, true)->s()));
            auto const low = values.front();
            auto const high = values.back();
            auto const less = [&](std::int64_t a, std::int64_t b) { return isSigned ? a < b : static_cast<std::uint64_t>(a) < static_cast<std::uint64_t>(b); };
            if (less(high, low))
                continue;
            for (auto const& [sl, sh] : seen)
                if (!less(high, sl) && !less(sh, low))
                    throw compile_error{ l, "duplicate case value" };
            seen.emplace_back(low, high);
            get(value);
            if (k == vkind::I64)
            {
                i64(low);
                if (low == high)
                    op(opcode::I64Eq);
                else
                {
                    op(opcode::I64Sub);
                    i64(static_cast<std::int64_t>(static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low)));
                    op(opcode::I64LeU);
                }
            }
            else
            {
                i32(static_cast<std::int32_t>(low));
                if (low == high)
                    op(opcode::I32Eq);
                else
                {
                    op(opcode::I32Sub);
                    i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(high) - static_cast<std::uint32_t>(low)));
                    op(opcode::I32LeU);
                }
            }
            begin_if();
            jump(block_number(l));
            end();
        }
        release(value);
        auto const after = new_block_number();
        iDispatchedBreaks[s] = after;
        jump(defaultLabel != nullptr ? block_number(defaultLabel) : after);
        dispatched_statement(body);
        next_block(after);
    }
}
