/*
  gen_streams.cpp

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

// Ada: the stream-oriented attributes (13.13.2): S'Write, S'Read, S'Output and S'Input. An elementary value is written as its
// bytes (its size's: little-endian) in a Stream_Element_Array passed to the stream's Write (a dispatching call, the slot of
// Ada.Streams.Root_Stream_Type's Write), and read likewise by its Read; a composite value is its components' (an array's in
// canonical order, a record's in positional aggregate order); S'Output writes an unconstrained array's bounds (and a
// discriminated record's discriminants) first, which S'Input reads to create the object it then reads. A type's (or an
// ancestor's) specified Read or Write (an aspect or an attribute definition clause) is called instead of the default.

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::ada
{
    using namespace emitter;

    namespace
    {
        bool is_aggregate_node(node const* aNode)
        {
            return aNode->is(construct::Aggregate) || aNode->is(construct::AggregateBracket) || aNode->is(construct::AggregateNullRecord) ||
                aNode->is(construct::AggregateExtension) || aNode->is(construct::AggregateDelta);
        }

        std::vector<node*> attribute_arguments(node* aAttribute)
        {
            std::vector<node*> result;
            if (auto* arguments = aAttribute->first(construct::Arguments))
                for (auto* a : arguments->children)
                    result.push_back(a);
            return result;
        }
    }

    int function_generator::stream_slot(std::string const& aOperation)
    {
        // (the dispatch slot of Root_Stream_Type's Read or Write)
        auto* root = iProgram.library_type("ada.streams", "root_stream_type");
        if (root == nullptr)
            iProgram.error(iWhere, "stream attributes need Ada.Streams");
        for (auto* t : { root, root->view(), root->view()->base_type()->view() })
            for (std::size_t i = 0u; i < t->slots.size(); ++i)
                if (t->slots[i].operation != nullptr && t->slots[i].operation->name == aOperation)
                    return static_cast<int>(i);
        iProgram.error(iWhere, "internal error: no slot for Ada.Streams." + aOperation);
    }

    void function_generator::stream_elementary(bool aRead, ada_type* aType, std::uint32_t aStream, std::uint32_t aAddress)
    {
        // (an elementary value at an address: written as its bytes, or read into it)
        auto* t = aType->view();
        auto const size = iProgram.stream_size(t);
        auto const objectSize = std::max<std::uint64_t>(iProgram.scalar_size(t), 1u);
        auto const k = kind_of(t);
        // (a Stream_Size other than the object's (an integer type's): its low order bytes)
        bool const resized = size != objectSize && k == vkind::I64;
        // (a Stream_Element_Array (1 .. size) on the secondary stack (and Read's Last after it))
        auto const buffer = temporary(type::I64);
        i64(static_cast<std::int64_t>(24u + ((size + 7u) & ~std::uint64_t{ 7u }) + 8u));
        runtime("ss_allocate", { type::I64 }, type::I64);
        set(buffer);
        get(buffer);
        i64(24);
        store64(0u);
        get(buffer);
        i64(1);
        store64(8u);
        get(buffer);
        i64(static_cast<std::int64_t>(size));
        store64(16u);
        if (!aRead && resized)
        {
            auto const value = temporary(type::I64);
            get(aAddress);
            load(k, objectSize, 0u, !is_unsigned(t));
            set(value);
            for (std::uint64_t i = 0u; i < size; ++i)
            {
                get(buffer);
                get(value);
                i64(static_cast<std::int64_t>(std::min<std::uint64_t>(8u * i, 63u)));
                op(i < 8u ? opcode::I64ShrU : opcode::I64ShrS);
                store(vkind::I64, 1u, static_cast<std::uint32_t>(24u + i));
            }
            release(value);
        }
        else if (!aRead)
        {
            get(buffer);
            get(aAddress);
            load(k, size, 0u, !is_unsigned(t));
            store(k, size, 24u);
        }
        // (the stream's operation (its tag's static link, the stream, the array (and Read's Last)))
        get(aStream);
        load64(0u);
        load64(static_cast<std::uint32_t>(program::TagLink));
        get(aStream);
        get(buffer);
        if (aRead)
        {
            get(buffer);
            i64(static_cast<std::int64_t>(24u + ((size + 7u) & ~std::uint64_t{ 7u })));
            op(opcode::I64Add);
        }
        get(aStream);
        load64(0u);
        load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(stream_slot(aRead ? "read" : "write"))));
        op(opcode::I32ConvertI64);
        if (aRead)
            call_indirect({ type::I64, type::I64, type::I64, type::I64 }, type::Void);
        else
            call_indirect({ type::I64, type::I64, type::I64 }, type::Void);
        check();
        if (aRead)
        {
            // (fewer elements than the value's: the end of the stream (End_Error, 13.13.2(37/1)))
            if (auto* endError = iProgram.library_exception("ada.io_exceptions", "end_error"))
            {
                get(buffer);
                load64(static_cast<std::uint32_t>(24u + ((size + 7u) & ~std::uint64_t{ 7u })));
                i64(static_cast<std::int64_t>(size));
                op(opcode::I64LtS);
                begin_if();
                raise_exception(endError, iWhere);
                end();
            }
            if (resized)
            {
                // (its bytes, the value's low order ones (sign extended, a signed type's))
                auto const value = temporary(type::I64);
                i64(0);
                set(value);
                for (std::uint64_t i = 0u; i < size && i < 8u; ++i)
                {
                    get(value);
                    get(buffer);
                    load(vkind::I64, 1u, static_cast<std::uint32_t>(24u + i), false);
                    i64(static_cast<std::int64_t>(8u * i));
                    op(opcode::I64Shl);
                    op(opcode::I64Ior);
                    set(value);
                }
                if (!is_unsigned(t) && size < 8u)
                {
                    get(value);
                    i64(static_cast<std::int64_t>(64u - 8u * size));
                    op(opcode::I64Shl);
                    i64(static_cast<std::int64_t>(64u - 8u * size));
                    op(opcode::I64ShrS);
                    set(value);
                }
                get(aAddress);
                get(value);
                store(k, objectSize, 0u);
                release(value);
            }
            else
            {
                get(aAddress);
                get(buffer);
                load(k, size, 24u, !is_unsigned(t));
                store(k, size, 0u);
            }
        }
        release(buffer);
    }

    void function_generator::stream_object(bool aRead, ada_type* aType, std::uint32_t aStream, std::uint32_t aAddress)
    {
        // (an object at an address (an array: its descriptor's) written or read: by its type's specified Read or Write, else by
        // default (elementary: its bytes; composite: its components'))
        auto* t = aType->view();
        // (a type extension's: its own (not its parent's: that's for its parent part))
        auto* tb = t->base_type()->view();
        bool const extension = tb->tagged && tb->parent != nullptr;
        if (auto* specified = iProgram.aspect_subprogram(t, aRead ? "read" : "write", !extension); specified != nullptr && specified->sub != nullptr)
        {
            auto* op = ultimate(specified);
            // (an untagged derived type's inherited attribute whose parent's representation differs: the item converted (13.6))
            auto* it = op->sub != nullptr && op->sub->parameters.size() == 2u ? op->sub->parameters[1].type : nullptr;
            std::optional<std::uint32_t> converted;
            if (it != nullptr && representations_differ(t, it))
            {
                converted = local(type::I64);
                i64(static_cast<std::int64_t>(std::max<std::uint64_t>(iProgram.object_size(it->view()->base_type()->view()), 8u)));
                runtime("ss_allocate", { type::I64 }, type::I64);
                set(*converted);
                representation_copy(t, aAddress, it, *converted, true);
            }
            static_link_for(op);
            get(aStream);
            get(converted ? *converted : aAddress);
            // (an elementary item of Write: by copy; Read's out item and composites: their addresses)
            if (!aRead && (t->is_scalar() || t->is_access()))
                load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
            call_function(op);
            check();
            if (converted && aRead)
                representation_copy(it, *converted, t, aAddress, false);
            return;
        }
        if (t->is_scalar() || t->is_access())
        {
            stream_elementary(aRead, t, aStream, aAddress);
            return;
        }
        // (an unchecked union type's default Read and Write: Program_Error (B.3.3(27/2)))
        if (t->kind == type_kind::Record && tb->unchecked_union)
        {
            raise_check(1, nullptr, "default stream attribute of an unchecked union type");
            return;
        }
        if (t->kind == type_kind::Array)
        {
            auto* c = t->component;
            auto const n = local(type::I64);
            auto const p = local(type::I64);
            array_length(aAddress, t->base_type()->view()->indexes.size());
            set(n);
            data_address(aAddress);
            set(p);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            get(n);
            i64(0);
            op(opcode::I64LeS);
            branch(exit, true);
            stream_object(aRead, c, aStream, p);
            get(p);
            element_size(t);
            op(opcode::I64Add);
            set(p);
            get(n);
            i64(1);
            op(opcode::I64Sub);
            set(n);
            branch(head);
            end();
            end();
            return;
        }
        if (t->kind == type_kind::Record || t->kind == type_kind::Private)
        {
            auto* base = t->base_type()->view();
            iProgram.layout(base);
            // (a type extension: its parent part by its parent's attribute, then its extension's components (not its parent's,
            // nor its new discriminants (Output's), 13.13.2(9/3)))
            std::map<std::string, std::size_t> inherited;
            if (extension)
            {
                auto* parent = base->parent->view()->base_type()->view();
                iProgram.layout(parent);
                stream_object(aRead, parent, aStream, aAddress);
                for (auto const& pc : parent->components)
                    if (pc.ent != nullptr)
                        ++inherited[pc.ent->name];
            }
            for (std::size_t ci = 0u; ci < base->components.size(); ++ci)
            {
                auto const& component = base->components[ci];
                if (!component.offset || component.type == nullptr)
                    continue;
                if (extension)
                {
                    if (component.discriminant)
                        continue;
                    if (component.ent != nullptr)
                        if (auto i = inherited.find(component.ent->name); i != inherited.end() && i->second > 0u)
                        {
                            --i->second;
                            continue;
                        }
                }
                // (a component of a variant: only if the discriminants (read already) select it (13.13.2(9/3)))
                if (component.variant >= 0)
                {
                    variant_selected(base, component, aAddress);
                    begin_if();
                }
                if (iProgram.bit_field(component))
                {
                    // (a bit field: its value in a cell)
                    auto const cell = local(type::I64);
                    i64(8);
                    runtime("ss_allocate", { type::I64 }, type::I64);
                    set(cell);
                    auto* ct = component.type->view();
                    if (!aRead)
                    {
                        get(cell);
                        get(aAddress);
                        component_load(component);
                        store(kind_of(ct), iProgram.scalar_size(ct), 0u);
                    }
                    stream_object(aRead, component.type, aStream, cell);
                    if (aRead)
                    {
                        get(aAddress);
                        get(cell);
                        load(kind_of(ct), iProgram.scalar_size(ct), 0u, !is_unsigned(ct));
                        component_store(component);
                    }
                    if (component.variant >= 0)
                        end();
                    continue;
                }
                auto const a = local(type::I64);
                component_address(base, component, aAddress);
                set(a);
                stream_object(aRead, component.type, aStream, a);
                if (component.variant >= 0)
                    end();
            }
            return;
        }
        iProgram.error(iWhere, "stream attributes of this type are not supported yet");
    }

    void function_generator::stream_attribute(node* aAttribute, bool aStatement)
    {
        // (S'Write (Stream, Item), S'Read (Stream, Item), S'Output (Stream, Item): statements; S'Input (Stream): a value)
        auto const name = name_text(aAttribute->child(1)->source);
        auto* prefix = aAttribute->child(0);
        auto const arguments = attribute_arguments(aAttribute);
        auto* t = prefix->type != nullptr ? prefix->type->view() : nullptr;
        // (T'Class'Attribute: class-wide)
        if (t != nullptr && t->kind != type_kind::ClassWide && prefix->is(construct::Attribute) && name_text(prefix->child(1)->source) == "class")
            t = iProgram.class_wide(t)->view();
        if (t == nullptr || arguments.empty())
            iProgram.error(aAttribute, "internal error: stream attribute without a type or a stream");
        bool const elementary = t->is_scalar() || t->is_access();
        auto const stream = local(type::I64);
        expression(arguments[0], arguments[0]->type);
        null_check(aAttribute);
        set(stream);
        if (t->kind == type_kind::ClassWide)
        {
            class_wide_stream(aAttribute, name, t, stream, arguments);
            return;
        }
        auto* base = t->base_type()->view();
        auto const discriminated = [&]()
        {
            if (t->kind != type_kind::Record && t->kind != type_kind::Private)
                return false;
            iProgram.layout(base);
            for (auto const& c : base->components)
                if (c.discriminant)
                    return true;
            return false;
        };
        if (name == "write" || name == "output")
        {
            auto const item = local(type::I64);
            if (elementary)
            {
                // (the value in a cell)
                i64(8);
                runtime("ss_allocate", { type::I64 }, type::I64);
                set(item);
                get(item);
                expression(arguments[1], t);
                store(kind_of(t), iProgram.scalar_size(t), 0u);
            }
            else if (is_aggregate_node(arguments[1]))
            {
                aggregate(arguments[1], t);
                set(item);
            }
            else
            {
                expression(arguments[1], t);
                set(item);
            }
            if (name == "output")
                stream_output(t, stream, item);
            else
                stream_object(false, t, stream, item);
            return;
        }
        if (name == "read")
        {
            if (elementary)
            {
                // (read into a cell, then assigned)
                auto const cell = local(type::I64);
                i64(8);
                runtime("ss_allocate", { type::I64 }, type::I64);
                set(cell);
                stream_object(true, t, stream, cell);
                get(cell);
                load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
                range_check(arguments[1]->type != nullptr ? arguments[1]->type : t, t, aAttribute);
                assign_value(arguments[1]);
            }
            else
            {
                auto const item = local(type::I64);
                address(arguments[1]);
                set(item);
                stream_object(true, t, stream, item);
            }
            return;
        }
        // (S'Input: its specified Input's result, or a new object (on the secondary stack))
        (void)aStatement;
        stream_input(t, stream, aAttribute);
    }

    void function_generator::stream_output(ada_type* aType, std::uint32_t aStream, std::uint32_t aItem)
    {
        // (S'Output: its specified Output, else an unconstrained array's bounds or a record's discriminants, then the item (S'Write))
        auto* t = aType->view();
        bool const elementary = t->is_scalar() || t->is_access();
        auto* base = t->base_type()->view();
        auto const discriminated = [&]()
        {
            if (t->kind != type_kind::Record && t->kind != type_kind::Private)
                return false;
            iProgram.layout(base);
            for (auto const& c : base->components)
                if (c.discriminant)
                    return true;
            return false;
        };
        auto* tb = t->base_type()->view();
        bool const extension = tb->tagged && tb->parent != nullptr;
        if (auto* specified = iProgram.aspect_subprogram(t, "output", !extension); specified != nullptr && specified->sub != nullptr)
            {
                // (its specified Output (an inherited one whose parent's representation differs: of the item converted, 13.6))
                auto* op = ultimate(specified);
                auto* it = op->sub != nullptr && op->sub->parameters.size() == 2u ? op->sub->parameters[1].type : nullptr;
                std::optional<std::uint32_t> converted;
                if (it != nullptr && representations_differ(t, it))
                {
                    converted = local(type::I64);
                    i64(static_cast<std::int64_t>(std::max<std::uint64_t>(iProgram.object_size(it->view()->base_type()->view()), 8u)));
                    runtime("ss_allocate", { type::I64 }, type::I64);
                    set(*converted);
                    representation_copy(t, aItem, it, *converted, true);
                }
                static_link_for(op);
                get(aStream);
                get(converted ? *converted : aItem);
                if (elementary)
                    load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
                call_function(op);
                check();
                return;
            }
        {
            if (t->kind == type_kind::Array && !t->constrained)
            {
                // (an unconstrained array's bounds first)
                auto const dimensions = base->indexes.size();
                for (std::size_t d = 0u; d < dimensions; ++d)
                    for (bool first : { true, false })
                    {
                        auto const cell = local(type::I64);
                        i64(8);
                        runtime("ss_allocate", { type::I64 }, type::I64);
                        set(cell);
                        get(cell);
                        bounds(aItem, d, first);
                        store(kind_of(base->indexes[d]), iProgram.scalar_size(base->indexes[d]), 0u);
                        stream_object(false, base->indexes[d]->base_type(), aStream, cell);
                    }
            }
            else if (discriminated())
            {
                // (a discriminated record's discriminants first)
                for (auto const& c : base->components)
                    if (c.discriminant && c.offset)
                    {
                        auto const a = local(type::I64);
                        get(aItem);
                        i64(static_cast<std::int64_t>(*c.offset));
                        op(opcode::I64Add);
                        set(a);
                        stream_object(false, c.type, aStream, a);
                    }
            }
        }
        stream_object(false, t, aStream, aItem);
    }

    void function_generator::stream_input(ada_type* aType, std::uint32_t aStream, node* aWhere)
    {
        // (S'Input: its specified Input's result, or a new object (on the secondary stack))
        auto* t = aType->view();
        bool const elementary = t->is_scalar() || t->is_access();
        auto* base = t->base_type()->view();
        auto const discriminated = [&]()
        {
            if (t->kind != type_kind::Record && t->kind != type_kind::Private)
                return false;
            iProgram.layout(base);
            for (auto const& c : base->components)
                if (c.discriminant)
                    return true;
            return false;
        };
        {
            auto* tb = t->base_type()->view();
            bool const extension = tb->tagged && tb->parent != nullptr;
            if (auto* specified = iProgram.aspect_subprogram(t, "input", !extension); specified != nullptr && specified->sub != nullptr)
            {
                auto* op = ultimate(specified);
                static_link_for(op);
                get(aStream);
                call_function(op);
                check();
                // (an inherited Input whose parent's representation differs: its result converted (13.6))
                if (auto* rt = op->sub != nullptr ? op->sub->result : nullptr; rt != nullptr && representations_differ(rt, t))
                {
                    auto const from = local(type::I64);
                    set(from);
                    auto const to = local(type::I64);
                    i64(static_cast<std::int64_t>(std::max<std::uint64_t>(iProgram.object_size(t->base_type()->view()), 8u)));
                    runtime("ss_allocate", { type::I64 }, type::I64);
                    set(to);
                    representation_copy(rt, from, t, to, true);
                    get(to);
                }
                return;
            }
        }
        if (elementary)
        {
            auto const cell = local(type::I64);
            i64(8);
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(cell);
            stream_object(true, t, aStream, cell);
            get(cell);
            load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
            return;
        }
        auto const object = local(type::I64);
        if (t->kind == type_kind::Array && !t->constrained)
        {
            // (its bounds read first, then its elements)
            auto const dimensions = base->indexes.size();
            std::vector<std::uint32_t> lows(dimensions), highs(dimensions);
            for (std::size_t d = 0u; d < dimensions; ++d)
                for (bool first : { true, false })
                {
                    auto const cell = local(type::I64);
                    i64(8);
                    runtime("ss_allocate", { type::I64 }, type::I64);
                    set(cell);
                    stream_object(true, base->indexes[d]->base_type(), aStream, cell);
                    auto const bound = local(type::I64);
                    get(cell);
                    load(kind_of(base->indexes[d]), iProgram.scalar_size(base->indexes[d]), 0u, !is_unsigned(base->indexes[d]));
                    set(bound);
                    (first ? lows : highs)[d] = bound;
                }
            // (its size: the descriptor's and its elements')
            auto const size = local(type::I64);
            i64(1);
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                auto const n = temporary(type::I64);
                get(highs[d]);
                get(lows[d]);
                op(opcode::I64Sub);
                i64(1);
                op(opcode::I64Add);
                tee(n);
                i64(0);
                get(n);
                i64(0);
                op(opcode::I64GtS);
                op(opcode::Select);
                op(opcode::I64Mul);
                release(n);
            }
            element_size(base);
            op(opcode::I64Mul);
            i64(static_cast<std::int64_t>(8u + 16u * dimensions));
            op(opcode::I64Add);
            set(size);
            get(size);
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(object);
            get(object);
            i64(static_cast<std::int64_t>(8u + 16u * dimensions));
            store64(0u);
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                get(object);
                get(lows[d]);
                store64(static_cast<std::uint32_t>(8u + 16u * d));
                get(object);
                get(highs[d]);
                store64(static_cast<std::uint32_t>(16u + 16u * d));
            }
            stream_object(true, t, aStream, object);
            get(object);
            return;
        }
        if (discriminated())
        {
            // (its discriminants read first (into the object), then its components)
            auto const size = iProgram.object_size(t);
            if (size == 0u)
                iProgram.error(aWhere, "S'Input of a dynamically sized record is not supported yet");
            i64(static_cast<std::int64_t>(size));
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(object);
            for (auto const& c : base->components)
                if (c.discriminant && c.offset)
                {
                    auto const a = local(type::I64);
                    get(object);
                    i64(static_cast<std::int64_t>(*c.offset));
                    op(opcode::I64Add);
                    set(a);
                    stream_object(true, c.type, aStream, a);
                }
            iDiscriminantsSet = true;
            default_initialize(t, object);
            iDiscriminantsSet = false;
        }
        else
        {
            auto const size = iProgram.object_size(t);
            if (size == 0u)
                iProgram.error(aWhere, "S'Input of a dynamically sized object is not supported yet");
            i64(static_cast<std::int64_t>(size));
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(object);
            if (t->kind == type_kind::Array)
                init_descriptor(t, object);
            default_initialize(t, object);
        }
        stream_object(true, t, aStream, object);
        get(object);
    }

    std::vector<ada_type*> function_generator::class_members(ada_type* aRoot)
    {
        // (the class's specific types that are not abstract (their tags), declared at library level or where the current subprogram
        // is (objects of others can't be here, nor their stream operations called))
        std::vector<ada_type*> result;
        auto* root = aRoot->view()->base_type()->view();
        std::vector<region*> enclosing;
        for (auto* r = iSubprogram != nullptr ? (iSubprogram->inner != nullptr ? iSubprogram->inner : iSubprogram->scope) : nullptr; r != nullptr; r = r->parent)
            enclosing.push_back(r);
        for (auto& candidate : iProgram.iTypes)
        {
            auto* c = &candidate;
            if (!c->tagged || c->is_subtype || c->full != nullptr || c->base_type() != c || c->kind != type_kind::Record || c->abstract)
                continue;
            bool member = false;
            for (auto* p = c; p != nullptr && !member;)
            {
                member = p == root;
                auto* next = p->parent != nullptr ? p->parent->view()->base_type()->view() : nullptr;
                p = next != p ? next : nullptr;
            }
            if (!member)
                continue;
            // (declared in a package or region within an enclosing subprogram (or at library level), not within another subprogram)
            auto* scope = c->declared_in != nullptr ? c->declared_in : (c->partial != nullptr ? c->partial->declared_in : nullptr);
            bool accessible = true;
            for (auto* r = scope; r != nullptr; r = r->parent)
            {
                if (std::find(enclosing.begin(), enclosing.end(), r) != enclosing.end())
                    break;
                if (r->is_subprogram)
                {
                    accessible = false;
                    break;
                }
            }
            if (!accessible)
                continue;
            if (std::find(result.begin(), result.end(), c) == result.end())
                result.push_back(c);
        }
        return result;
    }

    void function_generator::class_wide_stream(node* aAttribute, std::string const& aName, ada_type* aClassWide, std::uint32_t aStream,
        std::vector<node*> const& aArguments)
    {
        // (T'Class'Write/Read: dispatching to the item's type's; T'Class'Output: its external tag (String'Output) then its type's
        // Output; T'Class'Input: an external tag read (String'Input), then its type's Input (Tag_Error if it is not of the class)
        // (13.13.2(28/2-34/3)))
        auto* root = aClassWide->view()->specific != nullptr ? aClassWide->view()->specific : aClassWide;
        auto const members = class_members(root);
        auto const tag = local(type::I64);
        std::optional<std::uint32_t> item;
        if (aName != "input")
        {
            item = local(type::I64);
            if (aName == "read")
                address(aArguments[1]);
            else
                expression(aArguments[1], aClassWide);
            set(*item);
            get(*item);
            load64(0u);
            set(tag);
        }
        if (aName == "output")
        {
            // (its external tag (its specified one, else its expanded name))
            auto const name = local(type::I64);
            get(tag);
            runtime("tag_external", { type::I64 }, type::I64);
            set(name);
            stream_output(iProgram.string_type(), aStream, name);
        }
        if (aName == "input")
        {
            auto const name = local(type::I64);
            stream_input(iProgram.string_type(), aStream, aAttribute);
            set(name);
            get(name);
            i64(static_cast<std::int64_t>(iProgram.tag_table_cell()));
            runtime("tag_internal", { type::I64, type::I64 }, type::I64);
            set(tag);
            // (not a tag of the class: Tag_Error)
            get(tag);
            op(opcode::I64Eqz);
            get(tag);
            i64(static_cast<std::int64_t>(iProgram.tag_of(root->view())));
            runtime("is_descendant", { type::I64, type::I64 }, type::I32);
            op(opcode::I32Eqz);
            op(opcode::I32Ior);
            begin_if();
            if (auto* tagError = iProgram.library_exception("ada.tags", "tag_error"))
                raise_exception(tagError, aAttribute);
            else
                raise_check(1, aAttribute, "an external tag not of the class");
            end();
        }
        auto const result = local(type::I64);
        begin(control_kind::Block);
        auto const done = iControls.size() - 1u;
        for (auto* m : members)
        {
            get(tag);
            i64(static_cast<std::int64_t>(iProgram.tag_of(m)));
            op(opcode::I64Eq);
            begin_if();
            if (aName == "write")
                stream_object(false, m, aStream, *item);
            else if (aName == "read")
                stream_object(true, m, aStream, *item);
            else if (aName == "output")
                stream_output(m, aStream, *item);
            else
            {
                stream_input(m, aStream, aAttribute);
                set(result);
            }
            branch(done);
            end();
        }
        // (a type of the class declared in another subprogram: not supported)
        raise_check(1, aAttribute, "a class-wide stream attribute of a type declared in another subprogram");
        end();
        if (aName == "input")
            get(result);
    }
}
