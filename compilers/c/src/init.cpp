/*
  init.cpp

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

// C: initializers (6.7.11): the parts of an object that an initializer's items initialize, with designations and brace elision.
// An initializer list is walked with a cursor: a stack of the aggregates (subobjects) being initialized, each with its next
// member (or element); an item initializes the next subobject, descending into aggregates whose initializers' braces are elided

#include <algorithm>
#include "program.hpp"

namespace neos::compilers::c
{
    void program::walk_initializer(qtype aType, node* aInitializer, std::uint64_t aOffset, initializer_visitor const& aVisitor)
    {
        auto const strip = [](node* aNode)
        {
            while (aNode->is(construct::Paren))
                aNode = aNode->child(0);
            return aNode;
        };
        auto const characters = [&](qtype aT) { return aT->is_array() && aT->base->is_integer() && size_of(aT->base) <= 4u; };
        if (!aInitializer->is(construct::Braced))
        {
            if (aType->is_array())
            {
                auto* s = strip(aInitializer);
                if (!s->is(construct::Strings) || !characters(aType))
                    error(aInitializer, "array initializer must be an initializer list or a string literal");
                aVisitor.characters(aType, s, aOffset);
                return;
            }
            if (aType->is_record())
            {
                aVisitor.record(aType, aInitializer, aOffset);
                return;
            }
            aVisitor.scalar(aType, aInitializer, aOffset);
            return;
        }
        struct frame
        {
            qtype type;
            std::uint64_t offset = 0u;
            std::uint64_t index = 0u;
            std::uint64_t limit = 0u;
        };
        auto const limit_of = [&](qtype aT) -> std::uint64_t
        {
            if (aT->is_array())
                return aT->length ? *aT->length : ~std::uint64_t{};
            if (aT->is_record())
                return aT->record->isUnion ? std::min<std::uint64_t>(1u, aT->record->members.size()) : aT->record->members.size();
            return 1u;
        };
        // (a frame's subobject at its index: its type and offset (a member: its information))
        auto const subobject = [&](frame const& f, member_info const** aMember) -> std::pair<qtype, std::uint64_t>
        {
            *aMember = nullptr;
            if (f.type->is_array())
                return { f.type->base, f.offset + f.index * size_of(f.type->base) };
            if (f.type->is_record())
            {
                auto const& m = f.type->record->members[f.index];
                *aMember = &m;
                return { m.type, f.offset + m.offset };
            }
            return { f.type, f.offset };
        };
        // (a record's members that items initialize: not unnamed bit-fields)
        auto const skip_unnamed = [&](frame& f)
        {
            if (!f.type->is_record())
                return;
            auto const& members = f.type->record->members;
            while (f.index < f.limit && f.index < members.size() && members[f.index].bitWidth && members[f.index].name.empty())
                ++f.index;
        };
        std::vector<node*> items{ aInitializer->children.begin(), aInitializer->children.end() };
        std::vector<frame> stack;
        stack.push_back(frame{ aType, aOffset, 0u, limit_of(aType) });
        skip_unnamed(stack.back());
        auto const advance = [&]()
        {
            auto& top = stack.back();
            if (top.type->is_record() && top.type->record->isUnion)
                top.index = top.limit;
            else
            {
                ++top.index;
                skip_unnamed(top);
            }
        };
        // (the cursor at a subobject that is not exhausted: frames whose subobjects are all initialized popped)
        auto const settle = [&]() -> bool
        {
            for (;;)
            {
                skip_unnamed(stack.back());
                if (stack.back().index < stack.back().limit)
                    return true;
                if (stack.size() == 1u)
                    return false;
                stack.pop_back();
                advance();
            }
        };
        // (an item: its designation (a designator that is a range (GNU): of the index given), then the subobject it initializes)
        auto const apply = [&](node* item, std::optional<std::int64_t> aRangeIndex) -> bool
        {
            node* init = item;
            if (item->is(construct::Designated))
            {
                // (a designation: the cursor from the list's object to the subobject it designates)
                stack.resize(1u);
                auto const designators = item->children.size() - 1u;
                init = item->children.back();
                for (std::size_t d = 0u; d < designators; ++d)
                {
                    auto* designator = item->child(d);
                    auto& top = stack.back();
                    if (designator->is(construct::DesignatorIndex))
                    {
                        if (!top.type->is_array())
                            error(designator, "array index in non-array initializer");
                        auto const index = designator->size() > 1u && aRangeIndex ? *aRangeIndex :
                            integer_constant(designator->child(0), "array index in initializer");
                        if (index < 0 || static_cast<std::uint64_t>(index) >= top.limit)
                            error(designator, "array index in initializer exceeds array bounds");
                        top.index = static_cast<std::uint64_t>(index);
                    }
                    else
                    {
                        if (!top.type->is_record())
                            error(designator, "field name not in record or union initializer");
                        std::vector<member_info const*> path;
                        auto const name = name_of(designator->child(0));
                        if (!top.type->record->find(name, path))
                            error(designator, "unknown field '" + name + "' specified in initializer");
                        // (within anonymous members: each is a subobject on the way)
                        for (std::size_t p = 0u; p < path.size(); ++p)
                        {
                            auto& t = stack.back();
                            t.index = static_cast<std::uint64_t>(path[p] - t.type->record->members.data());
                            if (t.type->record->isUnion)
                                t.limit = t.index + 1u;
                            if (p + 1u < path.size())
                            {
                                member_info const* m = nullptr;
                                auto const [st, so] = subobject(t, &m);
                                stack.push_back(frame{ st, so, 0u, limit_of(st) });
                            }
                        }
                    }
                    if (d + 1u < designators)
                    {
                        member_info const* m = nullptr;
                        auto const [st, so] = subobject(stack.back(), &m);
                        if (!st->is_aggregate())
                            error(item->child(d + 1u), "designator into a non-aggregate");
                        stack.push_back(frame{ st, so, 0u, limit_of(st) });
                    }
                }
            }
            else if (!settle())
            {
                warning(item, "excess elements in initializer");
                return false;
            }
            // (the item initializes the subobject at the cursor (or, with its braces elided, the subobject's first scalar))
            for (;;)
            {
                member_info const* member = nullptr;
                auto const [type, offset] = subobject(stack.back(), &member);
                auto* x = strip(init);
                if (member != nullptr && member->bitWidth)
                {
                    if (init->is(construct::Braced))
                    {
                        if (init->size() != 1u)
                            error(init, "braced initializer for a bit-field");
                        aVisitor.bits(*member, init->child(0), offset);
                    }
                    else
                        aVisitor.bits(*member, init, offset);
                    break;
                }
                if (init->is(construct::Braced))
                {
                    walk_initializer(type, init, offset, aVisitor);
                    break;
                }
                if (characters(type) && x->is(construct::Strings))
                {
                    aVisitor.characters(type, x, offset);
                    break;
                }
                if (type->is_record() && compatible(value_type(init).unqualified(), type.unqualified(), true))
                {
                    aVisitor.record(type, init, offset);
                    break;
                }
                if (type->is_aggregate())
                {
                    // (brace elision: its first subobject)
                    stack.push_back(frame{ type, offset, 0u, limit_of(type) });
                    skip_unnamed(stack.back());
                    if (stack.back().limit == 0u || stack.back().index >= stack.back().limit)
                        error(init, "initializer for an empty aggregate");
                    continue;
                }
                aVisitor.scalar(type, init, offset);
                break;
            }
            advance();
            return true;
        };
        for (std::size_t i = 0u; i < items.size(); ++i)
        {
            auto* item = items[i];
            node* range = nullptr;
            if (item->is(construct::Designated))
                for (std::size_t d = 0u; d + 1u < item->size(); ++d)
                    if (item->child(d)->is(construct::DesignatorIndex) && item->child(d)->size() > 1u)
                        range = item->child(d);
            if (range == nullptr)
            {
                if (!apply(item, {}))
                    break;
                continue;
            }
            auto const low = integer_constant(range->child(0), "array index in initializer");
            auto const high = integer_constant(range->child(1), "array index in initializer");
            if (high < low)
                error(range, "empty index range in initializer");
            for (auto index = low; index <= high; ++index)
                apply(item, index);
        }
    }
}
