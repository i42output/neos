/*
  gen_ops.cpp

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

// Ada: the code generation of predefined operators, attributes, aggregates and allocators

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

        constexpr std::uint64_t DescriptorHeader(std::size_t aDimensions) { return 8u + 16u * aDimensions; }
    }

    // --- tags (a tagged type's dispatch table: its parent's tag, its expanded name, then its slots)

    std::uint64_t program::tag_table_cell()
    {
        if (iTagTableCell == 0u)
            iTagTableCell = static_data(8u, 8u);
        return iTagTableCell;
    }

    std::uint64_t program::tag_of(ada_type* aType)
    {
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view()->base_type()->view();
        if (t->partial != nullptr && t->tag_address == 0u && t->partial->tag_address != 0u)
            t->tag_address = t->partial->tag_address;
        if (t->tag_address != 0u)
            return t->tag_address;
        auto const slots = t->slots.size();
        auto const address = static_data(TagSlots + 8u * std::max<std::size_t>(slots, 1u), 8u);
        t->tag_address = address;
        iTagAddresses.push_back(address);
        if (t->partial != nullptr)
            t->partial->tag_address = address;
        std::string header(TagSlots, '\0');
        std::int64_t const parent = t->parent != nullptr && t->parent->view()->tagged ? static_cast<std::int64_t>(tag_of(t->parent)) : 0;
        std::string name = full_name(t->decl != nullptr ? t->decl : iStandardPackage);
        for (auto& c : name)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        std::int64_t const nameAddress = static_cast<std::int64_t>(static_string(name));
        std::memcpy(header.data(), &parent, 8u);
        std::memcpy(header.data() + 8, &nameAddress, 8u);
        // (a dynamically sized type's: its size descriptor's address, negated)
        layout(t);
        std::int64_t const objectSize = t->dynamic_size ? -static_cast<std::int64_t>(size_descriptor(t)) : static_cast<std::int64_t>(object_size(t));
        std::memcpy(header.data() + 16, &objectSize, 8u);
        std::int64_t const level = type_level(t);
        std::memcpy(header.data() + TagLevel, &level, 8u);
        std::int64_t const flags = t->abstract ? 1 : 0;
        std::memcpy(header.data() + TagFlags, &flags, 8u);
        // (its specified external tag (13.3(75/3)))
        if (auto* external = type_aspect(t, "external_tag"))
            if (auto const v = evaluate(external, nullptr); v.is_string())
            {
                std::string bytes;
                for (auto c : v.string())
                    bytes += static_cast<char>(c);
                std::int64_t const externalAddress = static_cast<std::int64_t>(static_string(bytes));
                std::memcpy(header.data() + TagExternal, &externalAddress, 8u);
            }
        iStaticBytes.emplace_back(address, header);
        // (a type with controlled parts: its deep Adjust and Finalize (class-wide objects' are called through their tags))
        if (t->kind == type_kind::Record && needs_finalization(t))
            for (bool finalize : { false, true })
                if (auto* deep = deep_operation(t, finalize))
                {
                    define_subprogram(deep);
                    iFunctionSlots.emplace_back(address + (finalize ? TagFinalize : TagAdjust), deep->sub->function_index);
                }
        for (std::size_t i = 0u; i < slots; ++i)
        {
            auto* op = t->slots[i].operation;
            if (std::getenv("NEOS_ADA_DEBUG"))
                std::fprintf(stderr, "tag %s slot %zu: %s%s%s\n", t->name.c_str(), i, op != nullptr ? op->name.c_str() : "-",
                    op != nullptr && op->sub != nullptr && op->sub->inherited ? " (inherited)" : "", op != nullptr && op->sub != nullptr && op->sub->is_abstract ? " (abstract)" : "");
            if (op == nullptr || op->sub == nullptr)
                continue;
            // (the slot's operation: the type's own, or the one it inherits)
            auto* target = op;
            for (int k = 0; k < 64 && target->sub != nullptr && target->sub->inherited && target->sub->parent_operation != nullptr &&
                !target->sub->null_extension_result; ++k)
                target = target->sub->parent_operation;
            if (target->sub == nullptr || target->sub->is_abstract || (target->sub->body == nullptr && target->sub->op == intrinsic::None &&
                !target->sub->null_extension_result))
                continue;
            if (target->sub->op != intrinsic::None)
                continue;
            define_subprogram(target);
            iFunctionSlots.emplace_back(address + TagSlots + 8u * i, target->sub->function_index);
        }
        // (the interfaces it implements (its own, its ancestors', theirs): for each, its operations' implementations (by slot))
        std::vector<ada_type*> interfaces;
        std::function<void(ada_type*)> collect = [&](ada_type* aInterface)
        {
            auto* i = aInterface->view()->base_type()->view();
            if (std::find(interfaces.begin(), interfaces.end(), i) != interfaces.end())
                return;
            interfaces.push_back(i);
            for (auto* j : i->interfaces)
                collect(j);
            if (i->parent != nullptr && i->parent->view()->kind == type_kind::Interface)
                collect(i->parent);
        };
        for (auto* a = t; a != nullptr; a = a->parent != nullptr ? a->parent->view()->base_type()->view() : nullptr)
        {
            for (auto* i : a->interfaces)
                collect(i);
            if (a->partial != nullptr)
                for (auto* i : a->partial->interfaces)
                    collect(i);
            if (a->parent == nullptr)
                break;
        }
        if (t->kind == type_kind::Interface)
            std::erase(interfaces, t);
        if (!interfaces.empty())
        {
            // (the table: its count, then (interface tag, operations' table) pairs)
            auto const table = static_data(8u + 16u * interfaces.size(), 8u);
            std::string bytes(8u + 16u * interfaces.size(), '\0');
            std::int64_t const count = static_cast<std::int64_t>(interfaces.size());
            std::memcpy(bytes.data(), &count, 8u);
            // (the type's operations (its dispatch table's, then its primitives (e.g. a partial view's)))
            std::vector<entity*> operations;
            for (auto const& s : t->slots)
                if (s.operation != nullptr)
                    operations.push_back(s.operation);
            for (auto* list : { &t->primitives, t->partial != nullptr ? &t->partial->primitives : nullptr })
                if (list != nullptr)
                    for (auto* op : *list)
                        if (std::find(operations.begin(), operations.end(), op) == operations.end())
                            operations.push_back(op);
            for (std::size_t n = 0u; n < interfaces.size(); ++n)
            {
                auto* i = interfaces[n];
                std::int64_t const interfaceTag = static_cast<std::int64_t>(tag_of(i));
                auto const islots = i->slots.size();
                // (its operations' implementations; then, for each, the entry that implements it (kind: 1 a task's, 2 a protected
                // object's; its index): a dispatching requeue's target (9.5.4(5/3)))
                auto const map = static_data(24u * std::max<std::size_t>(islots, 1u), 8u);
                std::int64_t const mapAddress = static_cast<std::int64_t>(map);
                std::memcpy(bytes.data() + 8u + 16u * n, &interfaceTag, 8u);
                std::memcpy(bytes.data() + 16u + 16u * n, &mapAddress, 8u);
                for (std::size_t k = 0u; k < islots; ++k)
                {
                    auto* iop = i->slots[k].operation;
                    if (iop == nullptr || iop->sub == nullptr)
                        continue;
                    // (its implementation: the type's operation of the same name and profile shape (the last (overriding) one))
                    entity* implementation = nullptr;
                    for (auto* op : operations)
                        if (op->name == iop->name && op->sub != nullptr && op->sub->parameters.size() == iop->sub->parameters.size() &&
                            (op->sub->result == nullptr) == (iop->sub->result == nullptr))
                        {
                            auto* target = op;
                            for (int z = 0; z < 64 && target->sub != nullptr && target->sub->inherited && target->sub->parent_operation != nullptr &&
                                !target->sub->null_extension_result; ++z)
                                target = target->sub->parent_operation;
                            if (target->sub != nullptr && !target->sub->is_abstract && target->sub->op == intrinsic::None &&
                                (target->sub->body != nullptr || target->sub->renames != nullptr || target->sub->is_null || target->sub->null_extension_result))
                                implementation = target;
                        }
                    if (implementation == nullptr)
                        implementation = entry_implementation(t, iop);
                    if (implementation == nullptr)
                        continue;
                    if (auto* entry = implementation->sub->implementing_entry; entry != nullptr && entry->kind == entity_kind::Entry)
                    {
                        std::int64_t index = 0;
                        if (auto* r = entry->scope)
                            for (auto* d : r->declarations)
                            {
                                if (d == entry)
                                    break;
                                if (d->kind == entity_kind::Entry)
                                    ++index;
                            }
                        auto* owner = entry->scope != nullptr ? entry->scope->owner : nullptr;
                        std::int64_t const descriptor[2] = { owner != nullptr && owner->kind == entity_kind::Protected ? 2 : 1, index };
                        std::string entryBytes(16u, '\0');
                        std::memcpy(entryBytes.data(), descriptor, 16u);
                        iStaticBytes.emplace_back(map + 8u * islots + 16u * k, entryBytes);
                    }
                    implementation = implementation->sub->renames != nullptr && implementation->sub->body == nullptr ? implementation->sub->renames : implementation;
                    define_subprogram(implementation);
                    iFunctionSlots.emplace_back(map + 8u * k, implementation->sub->function_index);
                }
            }
            iStaticBytes.emplace_back(table, bytes);
            std::string pointer(8u, '\0');
            std::int64_t const tableAddress = static_cast<std::int64_t>(table);
            std::memcpy(pointer.data(), &tableAddress, 8u);
            iStaticBytes.emplace_back(address + TagInterfaces, pointer);
        }
        return address;
    }

    entity* program::deep_operation(ada_type* aType, bool aFinalize)
    {
        // (a procedure of the object (declared where the type is, so that its static link is the type's primitives'))
        auto* t = aType->view()->base_type()->view();
        auto* scope = t->declared_in != nullptr ? t->declared_in : (t->partial != nullptr ? t->partial->declared_in : nullptr);
        if (scope == nullptr)
            return nullptr;
        auto* f = new_entity(entity_kind::Procedure, t->name + (aFinalize ? "%deep_finalize" : "%deep_adjust"), nullptr, scope);
        auto* r = new_region(f, scope);
        r->is_subprogram = true;
        r->level = scope->level + 1;
        auto* object = new_entity(entity_kind::Parameter, "%object", nullptr, r);
        object->type = t;
        r->add(object);
        parameter_info parameter;
        parameter.ent = object;
        parameter.type = t;
        parameter.mode = parameter_mode::InOut;
        f->sub->parameters.push_back(parameter);
        f->sub->level = r->level;
        f->sub->deep_type = t;
        f->sub->deep_finalize = aFinalize;
        f->scope = scope;
        iSubprogramsToGenerate.push_back(f);
        return f;
    }

    std::uint64_t program::stream_size(ada_type* aType)
    {
        for (auto* t = aType != nullptr ? aType->view()->base_type()->view() : nullptr; t != nullptr;
            t = t->parent != nullptr && t->parent->view()->base_type()->view() != t ? t->parent->view()->base_type()->view() : nullptr)
            if (auto* clause = type_aspect(t, "stream_size"))
                if (auto const v = evaluate(clause, nullptr); v.is_integer() && v.integer().to_int64() > 0)
                    return static_cast<std::uint64_t>((v.integer().to_int64() + 7) / 8);
        return std::max<std::uint64_t>(scalar_size(aType), 1u);
    }

    std::uint64_t program::standard_pool()
    {
        // (an object of System.Storage_Pools.Standard_Pool (its tag): the pool of the access types whose pools aren't specified)
        if (iStandardPool != 0u)
            return iStandardPool;
        auto* type = library_type("system.storage_pools", "standard_pool");
        if (type == nullptr)
            error(nullptr, "internal error: no System.Storage_Pools.Standard_Pool");
        iStandardPool = static_data(16u, 8u);
        std::string bytes(16u, '\0');
        std::int64_t const tag = static_cast<std::int64_t>(tag_of(type));
        std::memcpy(bytes.data(), &tag, 8u);
        iStaticBytes.emplace_back(iStandardPool, bytes);
        return iStandardPool;
    }

    std::optional<std::int64_t> program::invalid_value(ada_type* aType)
    {
        // (a discrete subtype whose objects' representation has values outside its range: the most negative (signed), else the greatest)
        auto* t = aType != nullptr ? aType->view() : nullptr;
        if (t == nullptr || !t->is_discrete() || !t->low.is_integer() || !t->high.is_integer())
            return {};
        auto const size = scalar_size(t);
        if (size == 0u || size > 4u)
            return {};
        auto const bits = 8u * size;
        bool const isSigned = t->low.integer().negative() || t->base_type()->view()->kind == type_kind::Integer;
        auto const least = isSigned ? -(std::int64_t{ 1 } << (bits - 1u)) : 0;
        auto const greatest = isSigned ? (std::int64_t{ 1 } << (bits - 1u)) - 1 : (std::int64_t{ 1 } << bits) - 1;
        if (bigint{ least } < t->low.integer())
            return least;
        if (bigint{ greatest } > t->high.integer())
            return greatest;
        return {};
    }

    std::int64_t program::type_level(ada_type* aType)
    {
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::ClassWide && t->specific != nullptr)
            t = t->specific->view()->base_type()->view();
        if (t->declared_in == nullptr && t->partial != nullptr)
            t = t->partial;
        return t->declared_in != nullptr ? accessibility_level(t->declared_in) : 0;
    }

    std::int64_t program::accessibility_level(region* aRegion)
    {
        // (outward from a region: to where its owner is declared (an instance's: where it is instantiated, not the generic's place))
        std::int64_t level = 0;
        region* jump = nullptr;
        int steps = 0;
        for (auto* r = aRegion; r != nullptr && ++steps < 1024; )
        {
            if (r->is_subprogram || r->master)
                ++level;
            if (r->owner != nullptr && r->owner->scope != nullptr && r->owner->scope != r)
            {
                jump = nullptr;
                r = r->owner->scope;
            }
            else
                r = outward(r, jump);
        }
        return level;
    }

    entity* program::entry_implementation(ada_type* aType, entity* aOperation)
    {
        // (a task or protected type's entry of an interface operation's name, its parameters the operation's but the first: a
        // procedure of the operation's profile (its first parameter the object) that calls it (9.4(11.1/3), 9.1(9.2/3)))
        auto* t = aType->view()->base_type()->view();
        if ((t->kind != type_kind::Task && t->kind != type_kind::Protected) || t->unit == nullptr || t->unit->inner == nullptr ||
            aOperation->sub == nullptr || aOperation->sub->parameters.empty())
            return nullptr;
        auto const key = std::make_pair(t, aOperation);
        if (auto const existing = iEntryImplementations.find(key); existing != iEntryImplementations.end())
            return existing->second;
        entity* entry = nullptr;
        for (auto* d : t->unit->inner->declarations)
            if ((d->kind == entity_kind::Entry || d->kind == entity_kind::Procedure || d->kind == entity_kind::Function) && d->name == aOperation->name &&
                d->sub != nullptr && d->sub->parameters.size() + 1u == aOperation->sub->parameters.size() &&
                (d->sub->result == nullptr) == (aOperation->sub->result == nullptr))
                entry = d;
        entity* f = nullptr;
        if (entry != nullptr)
        {
            auto* scope = t->unit->scope;
            f = new_entity(aOperation->sub->result != nullptr ? entity_kind::Function : entity_kind::Procedure, t->name + "%" + aOperation->name, nullptr, scope);
            auto* r = new_region(f, scope);
            r->is_subprogram = true;
            r->level = scope->level + 1;
            for (std::size_t i = 0u; i < aOperation->sub->parameters.size(); ++i)
            {
                auto parameter = aOperation->sub->parameters[i];
                auto* p = new_entity(entity_kind::Parameter, parameter.ent != nullptr ? parameter.ent->name : std::string{}, nullptr, r);
                if (i == 0u)
                    parameter.type = t;     // (the object (by reference))
                p->type = parameter.type;
                r->add(p);
                parameter.ent = p;
                f->sub->parameters.push_back(parameter);
            }
            f->sub->result = aOperation->sub->result;
            f->sub->level = r->level;
            f->sub->implementing_entry = entry;
            f->scope = scope;
            iSubprogramsToGenerate.push_back(f);
        }
        iEntryImplementations[key] = f;
        return f;
    }

    // --- predefined operators

    void function_generator::intrinsic_call(entity* aCallee, std::vector<node*> const& aOperands, ada_type* aResult, node* aWhere)
    {
        auto& sub = *aCallee->sub;
        auto const op = sub.op;
        auto* t = sub.operator_type != nullptr ? sub.operator_type : (aOperands.empty() ? aResult : aOperands[0]->type);
        auto* operandType = !sub.parameters.empty() ? sub.parameters[0].type : t;
        if (operandType != nullptr && operandType->is_universal() && !aOperands.empty() && aOperands[0]->type != nullptr && !aOperands[0]->type->is_universal())
            operandType = aOperands[0]->type;
        if (operandType != nullptr && operandType->is_universal() && aResult != nullptr && !aResult->is_universal())
            operandType = aResult;
        switch (op)
        {
        case intrinsic::Concat:
            concatenation(aCallee, aOperands, aResult);
            return;
        case intrinsic::Eq:
        case intrinsic::Ne:
        case intrinsic::Lt:
        case intrinsic::Le:
        case intrinsic::Gt:
        case intrinsic::Ge:
            // (the equality of a formal type in an instance whose actual is an untagged record type with a user-defined primitive
            // "=": that (it does not reemerge, 4.5.2(15/3)))
            if ((op == intrinsic::Eq || op == intrinsic::Ne) && aOperands.size() == 2u && operandType != nullptr &&
                std::any_of(aOperands.begin(), aOperands.end(), [](node* o) { return o->type != nullptr && o->type->formal_view != nullptr; }) &&
                !operandType->view()->base_type()->view()->tagged && primitive_equality(operandType) != nullptr)
            {
                for (auto* o : aOperands)
                {
                    if (is_aggregate_node(o))
                        aggregate(o, operandType);
                    else
                        expression(o, operandType);
                }
                component_equality(operandType);
                if (op == intrinsic::Ne)
                    this->op(opcode::I32Eqz);
                to_boolean();
                return;
            }
            comparison(op, operandType, aOperands);
            to_boolean();
            return;
        case intrinsic::UncheckedConversion:
            {
                auto* source = aOperands[0]->type;
                expression(aOperands[0], source);
                // (an access-to-array value (it designates the array's descriptor) and an address (of the array's data, 13.3(14)):
                // its data's address; an address, an access-to-constrained-array value: a descriptor of its own (the subtype's
                // bounds) for the data there)
                auto const arrayAccess = [&](ada_type* aType)
                {
                    auto* v = aType != nullptr ? aType->view()->base_type()->view() : nullptr;
                    return v != nullptr && v->kind == type_kind::Access && iProgram.designated_of(v) != nullptr &&
                        iProgram.designated_of(v)->view()->kind == type_kind::Array;
                };
                if (aResult != nullptr && source != nullptr && arrayAccess(source) != arrayAccess(aResult) && !aResult->view()->is_composite() &&
                    !source->view()->is_composite())
                {
                    if (arrayAccess(source))
                    {
                        data_of_access();
                        return;
                    }
                    auto* designated = iProgram.designated_of(aResult->view()->base_type()->view())->view();
                    if (auto* d = iProgram.designated_of(aResult->view()) != nullptr ? iProgram.designated_of(aResult->view())->view() : nullptr;
                        d != nullptr && d->constrained)
                        designated = d;
                    if (designated->constrained)
                    {
                        auto const address = temporary(type::I64);
                        set(address);
                        get(address);
                        this->op(opcode::I64Eqz);
                        begin_if(type::I64);
                        i64(0);
                        else_();
                        auto const descriptor = local(type::I64);
                        i64(static_cast<std::int64_t>(DescriptorHeader(designated->indexes.size())));
                        runtime("allocate", { type::I64 }, type::I64);
                        set(descriptor);
                        init_descriptor(designated, descriptor);
                        get(descriptor);
                        get(address);
                        get(descriptor);
                        this->op(opcode::I64Sub);
                        store64(0u);
                        get(descriptor);
                        end();
                        release(address);
                        return;
                    }
                }
                if (aResult != nullptr && source != nullptr && !aResult->view()->is_composite() && !source->view()->is_composite())
                {
                    // (an enumeration type's values: their representation codes (13.9(10)))
                    enumeration_code(source);
                    auto const sk = kind_of(source);
                    auto const dk = kind_of(aResult);
                    if (sk == vkind::F64 && dk == vkind::I64)
                        op_reinterpret(true);
                    else if (sk == vkind::I64 && dk == vkind::F64)
                        op_reinterpret(false);
                    else if (sk == vkind::F32 && dk == vkind::I64)
                    {
                        this->op(opcode::I32ReinterpretF32);
                        this->op(opcode::I64UConvertI32);
                    }
                    else if (sk == vkind::I64 && dk == vkind::F32)
                    {
                        this->op(opcode::I32ConvertI64);
                        this->op(opcode::F32ReinterpretI32);
                    }
                    else if (sk == vkind::I64 && dk == vkind::I64)
                    {
                        // (the target's bits: masked (an unsigned target) or sign extended (a signed one) to its size)
                        auto const size = iProgram.scalar_size(aResult);
                        if (size < 8u)
                        {
                            auto const bits = static_cast<std::int64_t>(size * 8u);
                            // (an enumeration type's codes: signed if any is negative)
                            auto* rb = aResult->view()->base_type()->view();
                            bool const signedCodes = rb->kind == type_kind::Enumeration &&
                                std::any_of(rb->representation.begin(), rb->representation.end(), [](bigint const& c) { return c.negative(); });
                            if (is_unsigned(aResult) && !signedCodes)
                            {
                                i64((std::int64_t{ 1 } << bits) - 1);
                                this->op(opcode::I64And);
                            }
                            else
                            {
                                i64(64 - bits);
                                this->op(opcode::I64Shl);
                                i64(64 - bits);
                                this->op(opcode::I64ShrS);
                            }
                        }
                    }
                    enumeration_position(aResult);
                }
                else if (aResult != nullptr && source != nullptr)
                {
                    // (the source's bytes (a scalar's, an array's data, a record's): its address and size)
                    auto* sv = source->view();
                    auto* rv = aResult->view();
                    auto const from = local(type::I64);
                    auto const size = local(type::I64);
                    auto const original = local(type::I64);
                    if (sv->is_composite())
                    {
                        tee(original);
                    }
                    if (!sv->is_composite())
                    {
                        enumeration_code(source);
                        materialize(source);
                        set(from);
                        i64(static_cast<std::int64_t>(iProgram.scalar_size(source)));
                        set(size);
                    }
                    else if (sv->kind == type_kind::Array)
                    {
                        auto const descriptor = local(type::I64);
                        set(descriptor);
                        data_address(descriptor);
                        set(from);
                        array_length(descriptor, sv->base_type()->view()->indexes.size());
                        element_size(sv);
                        this->op(opcode::I64Mul);
                        set(size);
                    }
                    else
                    {
                        set(from);
                        record_size(source, from);
                        set(size);
                    }
                    if (!rv->is_composite())
                    {
                        if (auto* component = sv->kind == type_kind::Array ? sv->base_type()->view()->component : nullptr;
                            component != nullptr && component->view()->base_type()->view()->boolean && sv->base_type()->view()->indexes.size() == 1u)
                        {
                            // (an array of Boolean: its components the bits, the first the least significant)
                            auto const esize = iProgram.component_size(sv->base_type()->view());
                            auto const value = local(type::I64);
                            auto const i = local(type::I64);
                            auto const n = local(type::I64);
                            get(size);
                            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(esize, 1u)));
                            this->op(opcode::I64DivU);
                            set(n);
                            i64(0);
                            set(value);
                            i64(0);
                            set(i);
                            begin(control_kind::Block);
                            auto const exit = iControls.size() - 1u;
                            begin(control_kind::Loop);
                            auto const head = iControls.size() - 1u;
                            get(i);
                            get(n);
                            this->op(opcode::I64GeS);
                            get(i);
                            i64(64);
                            this->op(opcode::I64GeS);
                            this->op(opcode::I32Ior);
                            branch(exit, true);
                            get(value);
                            get(from);
                            get(i);
                            i64(static_cast<std::int64_t>(esize));
                            this->op(opcode::I64Mul);
                            this->op(opcode::I64Add);
                            load(kind_of(component), iProgram.scalar_size(component), 0u, false);
                            i64(1);
                            this->op(opcode::I64And);
                            get(i);
                            this->op(opcode::I64Shl);
                            this->op(opcode::I64Ior);
                            set(value);
                            get(i);
                            i64(1);
                            this->op(opcode::I64Add);
                            set(i);
                            branch(head);
                            end();
                            end();
                            // (a real target: the bits its representation; a signed target: sign extended from its last bit)
                            if (kind_of(aResult) == vkind::F32 || kind_of(aResult) == vkind::F64)
                            {
                                get(value);
                                if (kind_of(aResult) == vkind::F32)
                                {
                                    this->op(opcode::I32ConvertI64);
                                    this->op(opcode::F32ReinterpretI32);
                                }
                                else
                                    op_reinterpret(false);
                                return;
                            }
                            if (!is_unsigned(aResult))
                            {
                                auto const shift = temporary(type::I64);
                                i64(64);
                                get(n);
                                this->op(opcode::I64Sub);
                                set(shift);
                                get(value);
                                get(shift);
                                this->op(opcode::I64Shl);
                                get(shift);
                                this->op(opcode::I64ShrS);
                                get(value);
                                get(n);
                                i64(64);
                                this->op(opcode::I64LtS);
                                get(n);
                                i64(0);
                                this->op(opcode::I64GtS);
                                this->op(opcode::I32And);
                                this->op(opcode::Select);
                                release(shift);
                            }
                            else
                                get(value);
                            enumeration_position(aResult);
                            return;
                        }
                        get(from);
                        load(kind_of(aResult), iProgram.scalar_size(aResult), 0u, !is_unsigned(aResult));
                        enumeration_position(aResult);
                        return;
                    }
                    if (rv->kind == type_kind::Array && rv->constrained)
                    {
                        // (a new array of the target subtype (its bounds), its data the source's bytes (as many as fit))
                        auto const dimensions = rv->base_type()->view()->indexes.size();
                        auto const dataSize = local(type::I64);
                        i64(1);
                        for (std::size_t d = 0u; d < dimensions; ++d)
                        {
                            auto const n = temporary(type::I64);
                            scalar_high(rv->indexes[d]);
                            scalar_low(rv->indexes[d]);
                            this->op(opcode::I64Sub);
                            i64(1);
                            this->op(opcode::I64Add);
                            tee(n);
                            i64(0);
                            get(n);
                            i64(0);
                            this->op(opcode::I64GtS);
                            this->op(opcode::Select);
                            release(n);
                            this->op(opcode::I64Mul);
                        }
                        element_size(rv);
                        this->op(opcode::I64Mul);
                        set(dataSize);
                        auto const object = local(type::I64);
                        get(dataSize);
                        i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
                        this->op(opcode::I64Add);
                        runtime("ss_allocate", { type::I64 }, type::I64);
                        set(object);
                        init_descriptor(rv, object);
                        if (auto* component = rv->base_type()->view()->component; !sv->is_composite() && component != nullptr &&
                            component->view()->base_type()->view()->boolean && dimensions == 1u)
                        {
                            // (an array of Boolean: the source's bits, the first component its least significant bit (its
                            // representation: an enumeration type's code))
                            // (its bits (a real's representation's))
                            auto const value = local(type::I64);
                            get(from);
                            load(vkind::I64, iProgram.scalar_size(source), 0u, kind_of(source) == vkind::I64 && !is_unsigned(source));
                            set(value);
                            auto const esize = iProgram.component_size(rv->base_type()->view());
                            auto const i = local(type::I64);
                            auto const n = local(type::I64);
                            get(dataSize);
                            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(esize, 1u)));
                            this->op(opcode::I64DivU);
                            set(n);
                            i64(0);
                            set(i);
                            begin(control_kind::Block);
                            auto const exit = iControls.size() - 1u;
                            begin(control_kind::Loop);
                            auto const head = iControls.size() - 1u;
                            get(i);
                            get(n);
                            this->op(opcode::I64GeS);
                            branch(exit, true);
                            get(object);
                            i64(static_cast<std::int64_t>(DescriptorHeader(1u)));
                            this->op(opcode::I64Add);
                            get(i);
                            i64(static_cast<std::int64_t>(esize));
                            this->op(opcode::I64Mul);
                            this->op(opcode::I64Add);
                            get(value);
                            get(i);
                            i64(63);
                            get(i);
                            i64(63);
                            this->op(opcode::I64LtS);
                            this->op(opcode::Select);
                            this->op(opcode::I64ShrU);
                            i64(1);
                            this->op(opcode::I64And);
                            get(i);
                            i64(64);
                            this->op(opcode::I64LtS);
                            this->op(opcode::I64UConvertI32);
                            this->op(opcode::I64And);
                            store(kind_of(component), iProgram.scalar_size(component), 0u);
                            get(i);
                            i64(1);
                            this->op(opcode::I64Add);
                            set(i);
                            branch(head);
                            end();
                            end();
                            get(object);
                            return;
                        }
                        get(object);
                        i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
                        this->op(opcode::I64Add);
                        get(from);
                        get(size);
                        get(dataSize);
                        get(size);
                        get(dataSize);
                        this->op(opcode::I64LtU);
                        this->op(opcode::Select);
                        runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                        // (components of an enumeration type with a representation clause: their codes (13.9(10)), the source's
                        // values as codes, the target's codes as values)
                        auto const coded = [](ada_type* aComponent)
                        {
                            auto* b = aComponent != nullptr ? aComponent->view()->base_type()->view() : nullptr;
                            return b != nullptr && b->kind == type_kind::Enumeration && !b->representation.empty() ? aComponent : nullptr;
                        };
                        auto* sourceComponent = sv->kind == type_kind::Array ? coded(sv->base_type()->view()->component) : nullptr;
                        auto* targetComponent = coded(rv->base_type()->view()->component);
                        if ((sourceComponent != nullptr || targetComponent != nullptr) && sv->kind == type_kind::Array && dimensions == 1u &&
                            sv->base_type()->view()->indexes.size() == 1u && sv->base_type()->view()->component != nullptr &&
                            sv->base_type()->view()->component->view()->is_discrete() && rv->base_type()->view()->component->view()->is_discrete())
                        {
                            // (element by element (their sizes can differ): each source element's code, the target element's value)
                            auto* sc = sv->base_type()->view()->component;
                            auto* tc = rv->base_type()->view()->component;
                            auto const ssize = iProgram.component_size(sv->base_type()->view());
                            auto const tsize = iProgram.component_size(rv->base_type()->view());
                            auto const n = local(type::I64);
                            auto const i = local(type::I64);
                            get(size);
                            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(ssize, 1u)));
                            this->op(opcode::I64DivU);
                            get(dataSize);
                            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(tsize, 1u)));
                            this->op(opcode::I64DivU);
                            tee(n);
                            get(n);
                            get(size);
                            i64(static_cast<std::int64_t>(std::max<std::uint64_t>(ssize, 1u)));
                            this->op(opcode::I64DivU);
                            this->op(opcode::I64LtU);
                            this->op(opcode::Select);
                            set(n);
                            i64(0);
                            set(i);
                            begin(control_kind::Block);
                            auto const exit = iControls.size() - 1u;
                            begin(control_kind::Loop);
                            auto const head = iControls.size() - 1u;
                            get(i);
                            get(n);
                            this->op(opcode::I64GeU);
                            branch(exit, true);
                            get(object);
                            i64(static_cast<std::int64_t>(DescriptorHeader(1u)));
                            this->op(opcode::I64Add);
                            get(i);
                            i64(static_cast<std::int64_t>(tsize));
                            this->op(opcode::I64Mul);
                            this->op(opcode::I64Add);
                            get(from);
                            get(i);
                            i64(static_cast<std::int64_t>(ssize));
                            this->op(opcode::I64Mul);
                            this->op(opcode::I64Add);
                            load(vkind::I64, ssize, 0u, !is_unsigned(sc));
                            if (sourceComponent != nullptr)
                                enumeration_code(sc);
                            if (targetComponent != nullptr)
                                enumeration_position(tc);
                            store(vkind::I64, tsize, 0u);
                            get(i);
                            i64(1);
                            this->op(opcode::I64Add);
                            set(i);
                            branch(head);
                            end();
                            end();
                        }
                        get(object);
                        return;
                    }
                    if (auto const targetSize = iProgram.object_size(aResult); rv->kind != type_kind::Array && targetSize != 0u)
                    {
                        // (a new record, its bytes the source's (as many as fit))
                        auto const object = local(type::I64);
                        i64(static_cast<std::int64_t>(std::max<std::uint64_t>(targetSize, 8u)));
                        runtime("ss_allocate", { type::I64 }, type::I64);
                        set(object);
                        get(object);
                        get(from);
                        get(size);
                        i64(static_cast<std::int64_t>(targetSize));
                        get(size);
                        i64(static_cast<std::int64_t>(targetSize));
                        this->op(opcode::I64LtU);
                        this->op(opcode::Select);
                        runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                        get(object);
                        return;
                    }
                    // (otherwise: the source itself (an array: its descriptor; a scalar: its cell))
                    get(sv->is_composite() ? original : from);
                }
                return;
            }
        case intrinsic::UniversalFixedMultiply:
        case intrinsic::UniversalFixedDivide:
            {
                // (where a specific fixed point type is expected, of operands of fixed point (or integer) types: exact, from their
                // counts (4.5.5(19.1)))
                if (aResult != nullptr && aResult->view()->kind != type_kind::UniversalFixed && aOperands.size() == 2u)
                {
                    auto const small_of = [](ada_type* aType) -> std::optional<rational>
                    {
                        auto* b = aType != nullptr ? aType->view()->base_type()->view() : nullptr;
                        if (b == nullptr)
                            return {};
                        if (b->is_fixed() && b->kind != type_kind::UniversalFixed && b->small.sign() > 0)
                            return b->small;
                        if (b->is_integer() && !b->is_universal())
                            return rational{ bigint{ 1 } };
                        return {};
                    };
                    auto* rb = aResult->view()->base_type()->view();
                    auto const left = small_of(aOperands[0]->type);
                    auto const right = small_of(aOperands[1]->type);
                    auto const result = rb->is_fixed() ? small_of(rb) : std::optional<rational>{};
                    bool const divide = op == intrinsic::UniversalFixedDivide;
                    if (left && right && result && kind_of(aOperands[0]->type) == vkind::I64 && kind_of(aOperands[1]->type) == vkind::I64)
                    {
                        auto const ratio = divide ? *left / (*right * *result) : (*left * *right) / *result;
                        if (ratio.sign() > 0 && ratio.numerator().fits_int64() && ratio.denominator().fits_int64())
                        {
                            expression(aOperands[0], aOperands[0]->type);
                            expression(aOperands[1], aOperands[1]->type);
                            i64(ratio.numerator().to_int64());
                            i64(ratio.denominator().to_int64());
                            i32(divide ? 1 : 0);
                            i32(rb->kind == type_kind::Decimal ? 1 : 0);
                            runtime("fixed_operation", { type::I64, type::I64, type::I64, type::I64, type::I32, type::I32 }, type::I64);
                            check();
                            return;
                        }
                    }
                }
                // (the operands' values (f64: count * small), multiplied or divided: a value of universal_fixed)
                for (std::size_t i = 0u; i < 2u; ++i)
                {
                    auto* ot = aOperands[i]->type;
                    if (ot != nullptr && ot->is_universal())
                        ot = iProgram.universal_real();
                    expression(aOperands[i], ot);
                    auto* ob = ot != nullptr ? ot->view()->base_type()->view() : nullptr;
                    auto const k = kind_of(ot);
                    if (k == vkind::I64)
                        this->op(opcode::F64SConvertI64);
                    else if (k == vkind::F32)
                        this->op(opcode::F64ConvertF32);
                    if (ob != nullptr && ob->is_fixed() && ob->kind != type_kind::UniversalFixed)
                    {
                        f64(ob->small.to_double());
                        this->op(opcode::F64Mul);
                    }
                }
                if (op == intrinsic::UniversalFixedDivide)
                {
                    auto const divisor = temporary(type::F64);
                    tee(divisor);
                    f64(0.0);
                    this->op(opcode::F64Eq);
                    begin_if();
                    raise_check(0, aWhere, "divide by zero");
                    end();
                    get(divisor);
                    release(divisor);
                    this->op(opcode::F64Div);
                }
                else
                    this->op(opcode::F64Mul);
                // (where a specific fixed point type is expected: implicitly converted to it (4.5.5(19.1)))
                if (aResult != nullptr && aResult->view()->base_type()->view()->is_fixed() && aResult->view()->kind != type_kind::UniversalFixed)
                {
                    auto* rb = aResult->view()->base_type()->view();
                    f64(rb->small.to_double());
                    this->op(opcode::F64Div);
                    if (rb->kind == type_kind::Decimal)
                        decimal_truncation();
                    round_to_integer();
                }
                return;
            }
        case intrinsic::UncheckedDeallocation:
            {
                // (Free (X): the object deallocated, X null)
                auto* actual = aOperands[0];
                expression(actual, actual->type);
                // (its protected objects finalized (their queued calls fail, 9.4(20.1/2)), before its controlled parts)
                if (auto* d = iProgram.designated_of(actual->type); d != nullptr && d->view()->base_type()->view()->kind != type_kind::Access)
                {
                    auto const object = local(type::I64);
                    tee(object);
                    this->op(opcode::I64Eqz);
                    this->op(opcode::I32Eqz);
                    begin_if();
                    unit_parts(d, object, type_kind::Protected, [&](std::uint32_t aPart)
                        {
                            get(aPart);
                            load64(8u);
                            runtime("po_finalize", { type::I64 }, type::Void);
                        });
                    end();
                    get(object);
                }
                if (auto* d = iProgram.designated_of(actual->type); d != nullptr && iProgram.needs_finalization(d))
                {
                    // (its controlled parts finalized)
                    auto const object = local(type::I64);
                    tee(object);
                    this->op(opcode::I64Eqz);
                    this->op(opcode::I32Eqz);
                    begin_if();
                    controlled_finalize(d, object);
                    // (not again by its collection)
                    controlled_parts(d, object, false, [&](ada_type*, std::uint32_t aPart) { controlled_unregister(aPart); });
                    end();
                    get(object);
                }
                if (auto* pool = storage_pool_of(actual->type))
                {
                    // (its access type's storage pool's Deallocate (unless null), of the object's size (13.11.2(9/3)))
                    auto const object = local(type::I64);
                    set(object);
                    get(object);
                    this->op(opcode::I64Eqz);
                    this->op(opcode::I32Eqz);
                    begin_if();
                    auto const poolAddress = local(type::I64);
                    address(pool);
                    set(poolAddress);
                    auto const size = local(type::I64);
                    auto* d = iProgram.designated_of(actual->type);
                    auto* dv = d != nullptr ? d->view() : nullptr;
                    if (dv != nullptr && dv->kind == type_kind::Array)
                    {
                        // (its data (from the pool; its descriptor from the heap, see allocate_array))
                        auto const dimensions = dv->base_type()->view()->indexes.size();
                        auto const data = local(type::I64);
                        data_address(object);
                        set(data);
                        array_length(object, dimensions);
                        element_size(dv);
                        this->op(opcode::I64Mul);
                        set(size);
                        pool_call("deallocate", poolAddress, data, size, storage_alignment(dv));
                        get(object);
                        runtime("deallocate", { type::I64 }, type::Void);
                        end();
                        i64(0);
                        assign_value(actual);
                        return;
                    }
                    else if (dv != nullptr && (dv->kind == type_kind::ClassWide || (dv->kind == type_kind::Record && is_runtime_sized(dv))))
                        record_size(d, object);
                    else
                        i64(static_cast<std::int64_t>(dv != nullptr ? std::max<std::uint64_t>(iProgram.object_size(dv), 1u) : 1u));
                    set(size);
                    pool_call("deallocate", poolAddress, object, size, storage_alignment(dv));
                    end();
                }
                else
                    runtime("deallocate", { type::I64 }, type::Void);
                i64(0);
                assign_value(actual);
                return;
            }
        default:
            break;
        }
        for (std::size_t i = 0u; i < aOperands.size(); ++i)
        {
            auto* pt = i < sub.parameters.size() ? sub.parameters[i].type : operandType;
            if (pt != nullptr && pt->is_universal() && operandType != nullptr && !operandType->is_universal())
                pt = operandType;
            // (the exponent of "**": an Integer)
            if (op == intrinsic::Power && i == 1u)
                pt = aOperands[1]->type;
            expression(aOperands[i], pt);
            // (root_real times (or divided by) root_integer: the integer as a real (4.5.5(19)))
            if ((op == intrinsic::RootMultiply || op == intrinsic::RootDivide) && pt != nullptr && kind_of(pt) == vkind::I64 &&
                aResult != nullptr && (kind_of(aResult) == vkind::F64 || kind_of(aResult) == vkind::F32) &&
                !(aResult->view()->base_type()->view()->is_fixed()))
                this->op(kind_of(operandType != nullptr && !operandType->is_universal() ? operandType : aResult) == vkind::F32 ? opcode::F32SConvertI64 :
                    opcode::F64SConvertI64);
        }
        intrinsic_values(aCallee, aResult, aWhere, operandType);
    }

    void function_generator::op_reinterpret(bool aToInteger)
    {
        op(aToInteger ? opcode::I64ReinterpretF64 : opcode::F64ReinterpretI64);
    }

    void function_generator::intrinsic_values(entity* aCallee, ada_type* aResult, node* aWhere, ada_type* aOperandType)
    {
        // (the operands' values on the stack: the operation's result)
        auto& sub = *aCallee->sub;
        auto* t = aOperandType != nullptr ? aOperandType : (sub.operator_type != nullptr ? sub.operator_type : aResult);
        if (t != nullptr && t->is_universal() && aResult != nullptr && !aResult->is_universal())
            t = aResult;
        auto* base = t != nullptr ? t->view()->base_type()->view() : nullptr;
        auto const k = kind_of(t);
        bool const modular = base != nullptr && base->kind == type_kind::Modular;
        // (a fixed point type, or an integer times or divided by one (4.5.5(14)): counts of the fixed type's small)
        auto const fixed_result = [&]()
        {
            auto* r = aResult != nullptr ? aResult->view()->base_type()->view() : nullptr;
            return r != nullptr && r->is_fixed() && (sub.op == intrinsic::Multiply || sub.op == intrinsic::Divide);
        };
        bool const fixed = (base != nullptr && base->is_fixed()) || fixed_result();
        auto const wrap = [&]()
        {
            // (a modular result: reduced modulo the modulus)
            if (!modular)
                return;
            auto const& m = base->modulus;
            if (m.bits() <= 64u && m == bigint::pow(bigint{ 2 }, m.bits() - 1u) && m.bits() - 1u < 64u)
            {
                i64(static_cast<std::int64_t>(m.to_uint64() - 1u));
                op(opcode::I64And);
            }
            else if (m.fits_uint64())
            {
                i64(static_cast<std::int64_t>(m.to_uint64()));
                op(opcode::I64RemU);
            }
        };
        // (a signed integer type of 64 bits: overflow detected from the operands, before the operation wraps)
        bool const wide = !modular && base != nullptr && base->kind == type_kind::Integer && base->size && *base->size >= 8u;
        auto const raise_if = [&]()
        {
            // (an i32 condition on the stack: Constraint_Error when true)
            begin_if();
            raise_check(0, aWhere, "overflow check failed");
            end();
        };
        auto const wide_binary = [&](opcode aOp)
        {
            // (the operands on the stack: their sum, difference or product)
            auto const left = temporary(type::I64);
            auto const right = temporary(type::I64);
            auto const result = temporary(type::I64);
            set(right);
            set(left);
            if (aOp == opcode::I64Mul)
            {
                // (-1 * First and First * -1 overflow; otherwise a non zero left: the product divided by it is the right)
                get(left);
                i64(-1);
                op(opcode::I64Eq);
                get(right);
                i64(std::numeric_limits<std::int64_t>::min());
                op(opcode::I64Eq);
                op(opcode::I32And);
                get(right);
                i64(-1);
                op(opcode::I64Eq);
                get(left);
                i64(std::numeric_limits<std::int64_t>::min());
                op(opcode::I64Eq);
                op(opcode::I32And);
                op(opcode::I32Ior);
                raise_if();
                get(left);
                get(right);
                op(opcode::I64Mul);
                set(result);
                get(left);
                i64(0);
                op(opcode::I64Ne);
                begin_if();
                get(result);
                get(left);
                op(opcode::I64DivS);
                get(right);
                op(opcode::I64Ne);
                raise_if();
                end();
            }
            else
            {
                // (a sum overflows when its sign differs from both operands'; a difference when the operands' signs
                // differ and its sign differs from the left's)
                get(left);
                get(right);
                op(aOp);
                set(result);
                if (aOp == opcode::I64Add)
                {
                    get(left);
                    get(result);
                    op(opcode::I64Xor);
                    get(right);
                    get(result);
                    op(opcode::I64Xor);
                }
                else
                {
                    get(left);
                    get(right);
                    op(opcode::I64Xor);
                    get(left);
                    get(result);
                    op(opcode::I64Xor);
                }
                op(opcode::I64And);
                i64(0);
                op(opcode::I64LtS);
                raise_if();
            }
            get(result);
            release(result);
            release(right);
            release(left);
        };
        auto const wide_first_check = [&]()
        {
            // (a value on the stack, kept: Constraint_Error if it is the most negative (its negation overflows))
            if (!wide)
                return;
            auto const v = temporary(type::I64);
            tee(v);
            i64(std::numeric_limits<std::int64_t>::min());
            op(opcode::I64Eq);
            raise_if();
            get(v);
            release(v);
        };
        auto const binary_int = [&](opcode aOp, bool aCheck)
        {
            if (wide && aCheck)
            {
                wide_binary(aOp);
                return;
            }
            op(aOp);
            if (modular)
                wrap();
            else if (aCheck)
                overflow_check(t, aWhere);
        };
        switch (sub.op)
        {
        case intrinsic::Plus:
            return;
        case intrinsic::Negate:
            if (k == vkind::I64)
            {
                wide_first_check();
                auto const v = temporary(type::I64);
                set(v);
                i64(0);
                get(v);
                op(opcode::I64Sub);
                release(v);
                if (modular && base->modulus.fits_uint64() && base->modulus != bigint::pow(bigint{ 2 }, base->modulus.bits() - 1u))
                {
                    // (a modulus that is not a power of two: modulus - value, reduced)
                    i64(static_cast<std::int64_t>(base->modulus.to_uint64()));
                    op(opcode::I64Add);
                    wrap();
                }
                else if (modular)
                    wrap();
                else
                    overflow_check(t, aWhere);
            }
            else
                op(k == vkind::F32 ? opcode::F32Neg : opcode::F64Neg);
            return;
        case intrinsic::Abs:
            if (k == vkind::I64)
            {
                wide_first_check();
                auto const v = temporary(type::I64);
                set(v);
                i64(0);
                get(v);
                op(opcode::I64Sub);
                get(v);
                get(v);
                i64(0);
                op(opcode::I64LtS);
                op(opcode::Select);
                // (select picks the first when the condition is non zero: negative -> 0 - v)
                release(v);
                overflow_check(t, aWhere);
            }
            else
                op(k == vkind::F32 ? opcode::F32Abs : opcode::F64Abs);
            return;
        case intrinsic::Not:
            if (base != nullptr && base->boolean)
            {
                op(opcode::I64Eqz);
                to_boolean();
            }
            else if (modular)
            {
                // (modulus - 1 - value)
                auto const v = temporary(type::I64);
                set(v);
                i64(static_cast<std::int64_t>((base->modulus - bigint{ 1 }).to_uint64()));
                get(v);
                op(opcode::I64Sub);
                release(v);
            }
            else if (base != nullptr && base->kind == type_kind::Array)
                boolean_array_operation(sub.op, aResult != nullptr ? aResult : t);
            return;
        case intrinsic::Add:
            if (k == vkind::I64)
                binary_int(opcode::I64Add, true);
            else
                op(k == vkind::F32 ? opcode::F32Add : opcode::F64Add);
            return;
        case intrinsic::Subtract:
            if (k == vkind::I64 && modular && base->modulus.fits_uint64() && base->modulus != bigint::pow(bigint{ 2 }, base->modulus.bits() - 1u))
            {
                // (a modulus that is not a power of two: left + (modulus - right), reduced)
                auto const right = temporary(type::I64);
                set(right);
                i64(static_cast<std::int64_t>(base->modulus.to_uint64()));
                get(right);
                op(opcode::I64Sub);
                release(right);
                binary_int(opcode::I64Add, false);
            }
            else if (k == vkind::I64)
                binary_int(opcode::I64Sub, true);
            else
                op(k == vkind::F32 ? opcode::F32Sub : opcode::F64Sub);
            return;
        case intrinsic::Multiply:
        case intrinsic::UniversalFixedMultiply:
        case intrinsic::RootMultiply:
            if (fixed)
            {
                // (a fixed point value times an integer: the count times it)
                wide_binary(opcode::I64Mul);
                return;
            }
            if (k == vkind::I64)
                binary_int(opcode::I64Mul, true);
            else
                op(k == vkind::F32 ? opcode::F32Mul : opcode::F64Mul);
            return;
        case intrinsic::Divide:
        case intrinsic::UniversalFixedDivide:
        case intrinsic::RootDivide:
            if (k == vkind::I64)
            {
                auto const divisor = temporary(type::I64);
                tee(divisor);
                op(opcode::I64Eqz);
                begin_if();
                raise_check(0, aWhere, "divide by zero");
                end();
                if (wide || fixed)
                {
                    // (First / -1 overflows)
                    auto const dividend = temporary(type::I64);
                    tee(dividend);
                    i64(std::numeric_limits<std::int64_t>::min());
                    op(opcode::I64Eq);
                    get(divisor);
                    i64(-1);
                    op(opcode::I64Eq);
                    op(opcode::I32And);
                    raise_if();
                    get(dividend);
                    release(dividend);
                }
                get(divisor);
                op(modular ? opcode::I64DivU : opcode::I64DivS);
                release(divisor);
                if (!modular && !fixed)
                    overflow_check(t, aWhere);
            }
            else
                op(k == vkind::F32 ? opcode::F32Div : opcode::F64Div);
            return;
        case intrinsic::Rem:
        case intrinsic::Mod:
            {
                auto const divisor = temporary(type::I64);
                auto const dividend = temporary(type::I64);
                set(divisor);
                set(dividend);
                get(divisor);
                op(opcode::I64Eqz);
                begin_if();
                raise_check(0, aWhere, "divide by zero");
                end();
                get(dividend);
                get(divisor);
                op(modular ? opcode::I64RemU : opcode::I64RemS);
                if (sub.op == intrinsic::Mod && !modular)
                {
                    // (mod: the sign of the divisor: r + d if r /= 0 and sign (r) /= sign (d))
                    auto const r = temporary(type::I64);
                    set(r);
                    get(r);
                    get(divisor);
                    op(opcode::I64Add);
                    get(r);
                    get(r);
                    op(opcode::I64Eqz);
                    op(opcode::I32Eqz);
                    get(r);
                    get(divisor);
                    op(opcode::I64Xor);
                    i64(0);
                    op(opcode::I64LtS);
                    op(opcode::I32And);
                    op(opcode::Select);
                    // (select (r + d, r, c): r + d when c)
                    release(r);
                }
                release(divisor);
                release(dividend);
                return;
            }
        case intrinsic::Power:
            {
                // (an exponent: Natural for integers (a negative one: Constraint_Error), Integer for reals)
                auto const exponent = temporary(type::I64);
                set(exponent);
                if (k == vkind::I64)
                {
                    get(exponent);
                    i64(0);
                    op(opcode::I64LtS);
                    begin_if();
                    raise_check(0, aWhere, "negative exponent");
                    end();
                    get(exponent);
                    if (modular)
                    {
                        // (the modulus (0: 2 ** 64))
                        i64(base->modulus.fits_uint64() ? static_cast<std::int64_t>(base->modulus.to_uint64()) : 0);
                        runtime("power_modular", { type::I64, type::I64, type::I64 }, type::I64);
                    }
                    else
                        runtime("power_integer", { type::I64, type::I64 }, type::I64);
                    if (modular)
                        ;
                    else
                    {
                        check();
                        overflow_check(t, aWhere);
                    }
                }
                else
                {
                    if (k == vkind::F32)
                        op(opcode::F64ConvertF32);
                    get(exponent);
                    runtime("power_real", { type::F64, type::I64 }, type::F64);
                    if (k == vkind::F32)
                        op(opcode::F32ConvertF64);
                }
                release(exponent);
                return;
            }
        case intrinsic::And:
        case intrinsic::Or:
        case intrinsic::Xor:
            if (base != nullptr && base->kind == type_kind::Array)
            {
                boolean_array_operation(sub.op, aResult != nullptr ? aResult : t);
                return;
            }
            op(sub.op == intrinsic::And ? opcode::I64And : sub.op == intrinsic::Or ? opcode::I64Ior : opcode::I64Xor);
            // (a modulus that is not a power of two: the result reduced (4.5.1(3)))
            if (modular && base->modulus.fits_uint64() && base->modulus != bigint::pow(bigint{ 2 }, base->modulus.bits() - 1u))
                wrap();
            return;
        case intrinsic::Eq:
        case intrinsic::Ne:
        case intrinsic::Lt:
        case intrinsic::Le:
        case intrinsic::Gt:
        case intrinsic::Ge:
            comparison_values(sub.op, t);
            to_boolean();
            return;
        default:
            iProgram.error(aWhere, "operator not supported yet (" + std::to_string(static_cast<int>(sub.op)) + ")");
        }
    }

    void function_generator::boolean_array_operation(intrinsic aOp, ada_type* aType)
    {
        // (logical operators of Boolean arrays: element by element (runtime))
        auto const element = static_cast<std::int64_t>(iProgram.component_size(aType->view()->base_type()->view()));
        if (aOp == intrinsic::Not)
        {
            i64(element);
            runtime("array_not", { type::I64, type::I64 }, type::I64);
            return;
        }
        i64(element);
        i32(aOp == intrinsic::And ? 0 : aOp == intrinsic::Or ? 1 : 2);
        runtime("array_logical", { type::I64, type::I64, type::I64, type::I32 }, type::I64);
        check();
    }

    bool program::inferable_discriminants(node* aExpression)
    {
        // (B.3.3(13/2): a name of an object, a qualified expression, a type conversion or a function call whose nominal subtype is
        // constrained; an aggregate (its discriminants are given))
        auto* e = aExpression;
        while (e != nullptr && e->is(construct::Parenthesized))
            e = e->child(0);
        if (e == nullptr)
            return false;
        if (e->is(construct::Aggregate) || e->is(construct::AggregateExtension) || e->is(construct::AggregateDelta))
            return true;
        ada_type* nominal = e->type;
        if (!e->is(construct::Qualified) && meaning_of(e) != meaning::Conversion && meaning_of(e) != meaning::Call && e->ent != nullptr &&
            (e->ent->is_object() || e->ent->kind == entity_kind::Component) && e->ent->type != nullptr)
            nominal = e->ent->type;
        if (nominal == nullptr)
            return false;
        auto* v = nominal->view();
        auto* base = v->base_type()->view();
        bool const discriminated = std::any_of(base->components.begin(), base->components.end(), [](component_info const& c) { return c.discriminant; });
        return !discriminated || !v->discriminant_constraint.empty();
    }

    bool program::unconstrained_union_part(ada_type* aType)
    {
        // (a subcomponent (of a record, or an array's components) of an unchecked union type whose nominal subtype is unconstrained)
        std::function<bool(ada_type*, int)> part = [&](ada_type* aPart, int aDepth) -> bool
        {
            if (aPart == nullptr || aDepth > 16)
                return false;
            auto* v = aPart->view();
            if (v->kind == type_kind::Array)
            {
                auto* c = v->base_type()->view()->component;
                if (c == nullptr)
                    return false;
                auto* cv = c->view();
                auto* cb = cv->base_type()->view();
                if (cb->unchecked_union && cv->discriminant_constraint.empty())
                    return true;
                return part(c, aDepth + 1);
            }
            if (v->kind != type_kind::Record)
                return false;
            for (auto const& c : v->base_type()->view()->components)
            {
                if (c.discriminant || c.type == nullptr)
                    continue;
                auto* cv = c.type->view();
                auto* cb = cv->base_type()->view();
                if (cb->kind == type_kind::Record && cb->unchecked_union && cv->discriminant_constraint.empty() &&
                    std::any_of(cb->components.begin(), cb->components.end(), [](component_info const& d) { return d.discriminant; }))
                    return true;
                if (part(c.type, aDepth + 1))
                    return true;
            }
            return false;
        };
        return part(aType, 0);
    }

    void function_generator::data_of_access()
    {
        // (an access-to-array value on the stack: the address of the array's data (null: null))
        auto const p = temporary(type::I64);
        tee(p);
        op(opcode::I64Eqz);
        begin_if(type::I64);
        i64(0);
        else_();
        get(p);
        get(p);
        load64(0u);
        op(opcode::I64Add);
        end();
        release(p);
    }

    void function_generator::comparison(intrinsic aOp, ada_type* aType, std::vector<node*> const& aOperands)
    {
        // (an i32: two values compared (scalars directly; arrays and records by their components))
        auto* t = aType != nullptr ? aType->view() : nullptr;
        auto* base = t != nullptr ? t->base_type()->view() : nullptr;
        bool const composite = base != nullptr && (base->kind == type_kind::Array || base->kind == type_kind::Record || base->kind == type_kind::Private ||
            base->kind == type_kind::ClassWide);
        for (std::size_t i = 0u; i < 2u; ++i)
        {
            if (composite && is_aggregate_node(aOperands[i]))
                aggregate(aOperands[i], aType);
            else
                expression(aOperands[i], aType);
        }
        // (an unchecked union's predefined equality: an operand that lacks inferable discriminants, Program_Error (B.3.3(23/2)))
        if ((aOp == intrinsic::Eq || aOp == intrinsic::Ne) && base != nullptr && base->unchecked_union &&
            (!iProgram.inferable_discriminants(aOperands[0]) || !iProgram.inferable_discriminants(aOperands[1])))
        {
            drop();
            drop();
            raise_check(1, aOperands[0], "unchecked union operand without inferable discriminants");
            i32(0);
            return;
        }
        comparison_values(aOp, aType);
    }

    void function_generator::comparison_values(intrinsic aOp, ada_type* aType)
    {
        // (two values on the stack (composites: their addresses): an i32, their comparison)
        auto* t = aType != nullptr ? aType->view() : nullptr;
        auto* base = t != nullptr ? t->base_type()->view() : nullptr;
        if (base != nullptr && (base->kind == type_kind::Array || base->kind == type_kind::Record || base->kind == type_kind::Private ||
            base->kind == type_kind::ClassWide))
        {
            if (aOp == intrinsic::Eq || aOp == intrinsic::Ne)
            {
                // (a type with a subcomponent of an unchecked union type whose nominal subtype is unconstrained: Program_Error
                // (B.3.3(24/2)))
                if (iProgram.unconstrained_union_part(base))
                {
                    drop();
                    drop();
                    raise_check(1, nullptr, "equality of a type with an unconstrained unchecked union part");
                    i32(0);
                    return;
                }
                equality_values(aType);
                if (aOp == intrinsic::Ne)
                    op(opcode::I32Eqz);
                return;
            }
            // (lexicographic: discrete components (a signed type's: their size negated))
            auto const elementSize = static_cast<std::int64_t>(iProgram.component_size(base));
            auto const valueSize = static_cast<std::int64_t>(base->component != nullptr ? iProgram.scalar_size(base->component) : elementSize);
            i64(elementSize);
            i64(base->component != nullptr && !is_unsigned(base->component) ? -valueSize : valueSize);
            runtime("compare_arrays_padded", { type::I64, type::I64, type::I64, type::I64 }, type::I32);
            i32(0);
            switch (aOp)
            {
            case intrinsic::Lt: op(opcode::I32LtS); break;
            case intrinsic::Le: op(opcode::I32LeS); break;
            case intrinsic::Gt: op(opcode::I32GtS); break;
            default: op(opcode::I32GeS); break;
            }
            return;
        }
        // (access-to-protected-subprogram values: equal if both are null, or if they designate the same procedure of the same object
        // (their descriptors may be different copies, 4.5.2(10.1/3)))
        if (base != nullptr && base->kind == type_kind::AccessSubprogram && base->protected_access && (aOp == intrinsic::Eq || aOp == intrinsic::Ne))
        {
            runtime("protected_access_equal", { type::I64, type::I64 }, type::I32);
            if (aOp == intrinsic::Ne)
                op(opcode::I32Eqz);
            return;
        }
        // (access-to-array values: equal if they designate the same array, its data (an access value made from an address has a
        // descriptor of its own: see unchecked_conversion))
        if (base != nullptr && base->kind == type_kind::Access && (aOp == intrinsic::Eq || aOp == intrinsic::Ne) && iProgram.designated_of(base) != nullptr &&
            iProgram.designated_of(base)->view()->kind == type_kind::Array)
        {
            auto const right = temporary(type::I64);
            set(right);
            data_of_access();
            get(right);
            release(right);
            data_of_access();
            op(aOp == intrinsic::Eq ? opcode::I64Eq : opcode::I64Ne);
            return;
        }
        auto const k = kind_of(aType);
        bool const unsignedCompare = base != nullptr && base->kind == type_kind::Modular;
        if (k == vkind::I64)
        {
            switch (aOp)
            {
            case intrinsic::Eq: op(opcode::I64Eq); break;
            case intrinsic::Ne: op(opcode::I64Ne); break;
            case intrinsic::Lt: op(unsignedCompare ? opcode::I64LtU : opcode::I64LtS); break;
            case intrinsic::Le: op(unsignedCompare ? opcode::I64LeU : opcode::I64LeS); break;
            case intrinsic::Gt: op(unsignedCompare ? opcode::I64GtU : opcode::I64GtS); break;
            default: op(unsignedCompare ? opcode::I64GeU : opcode::I64GeS); break;
            }
            return;
        }
        bool const single = k == vkind::F32;
        switch (aOp)
        {
        case intrinsic::Eq: op(single ? opcode::F32Eq : opcode::F64Eq); break;
        case intrinsic::Ne: op(single ? opcode::F32Ne : opcode::F64Ne); break;
        case intrinsic::Lt: op(single ? opcode::F32Lt : opcode::F64Lt); break;
        case intrinsic::Le: op(single ? opcode::F32Le : opcode::F64Le); break;
        case intrinsic::Gt: op(single ? opcode::F32Gt : opcode::F64Gt); break;
        default: op(single ? opcode::F32Ge : opcode::F64Ge); break;
        }
    }

    bool function_generator::limited_type(ada_type* aType)
    {
        // (a limited type: limited, or with a part that is)
        auto* t = aType->view();
        if (t->limited || t->kind == type_kind::Task || t->kind == type_kind::Protected || (t->partial != nullptr && t->partial->limited))
            return true;
        if (t->kind == type_kind::Array)
            return t->component != nullptr && limited_type(t->component);
        if (t->kind == type_kind::Record)
            for (auto const& c : t->base_type()->view()->components)
                if (c.type != nullptr && limited_type(c.type))
                    return true;
        return false;
    }

    entity* function_generator::primitive_equality(ada_type* aType, bool aLimited)
    {
        // (a record type's user-defined primitive "=" (its own, or an untagged type's inherited one): predefined equality of
        // types with components of the type uses it (4.5.2); null: none (predefined equality))
        auto* t = aType->view();
        if (aLimited && t->kind != type_kind::Record && limited_type(t))
        {
            // (a limited type's other than a record's (a membership test uses it, 4.5.2(28.1/4)))
            auto* base = t->base_type()->view();
            for (auto const* list : { &base->primitives, base->partial != nullptr ? &base->partial->primitives : nullptr })
                if (list != nullptr)
                    for (auto* op : *list)
                        if (op != nullptr && op->sub != nullptr && op->name == "=" && op->sub->parameters.size() == 2u && op->sub->result != nullptr &&
                            op->sub->op == intrinsic::None && !op->sub->is_abstract && iProgram.same_type(op->sub->parameters[0].type, base) &&
                            iProgram.same_type(op->sub->parameters[1].type, base))
                            return op;
            return nullptr;
        }
        if (t->kind != type_kind::Record && t->kind != type_kind::Private)
            return nullptr;
        auto* base = t->base_type()->view();
        if (base->kind != type_kind::Record)
            return nullptr;
        std::vector<entity*> const* lists[] = { &base->primitives, base->partial != nullptr ? &base->partial->primitives : nullptr };
        for (auto const* list : lists)
        {
            if (list == nullptr)
                continue;
            for (auto* op : *list)
            {
                if (op == nullptr || op->sub == nullptr || op->name != "=" || op->sub->parameters.size() != 2u || op->sub->result == nullptr)
                    continue;
                if (!iProgram.same_type(op->sub->parameters[0].type, base) || !iProgram.same_type(op->sub->parameters[1].type, base))
                    continue;
                auto* e = op;
                if (e->sub->inherited)
                {
                    // (a type extension's inherited "=": not used (its predefined equality composes the parent's))
                    if (base->tagged)
                        continue;
                    for (int k = 0; k < 64 && e != nullptr && e->sub != nullptr && e->sub->inherited && e->sub->parent_operation != nullptr; ++k)
                        e = e->sub->parent_operation;
                }
                if (e == nullptr || e->sub == nullptr || e->sub->op != intrinsic::None || e->sub->is_abstract)
                    continue;
                return ultimate(e);
            }
        }
        return nullptr;
    }

    void function_generator::component_equality(ada_type* aType, bool aLimited)
    {
        // (two composite values' addresses on the stack (components of a composite): an i32, whether they are equal (a record
        // type's user-defined primitive "=", else predefined equality))
        auto* eq = primitive_equality(aType, aLimited);
        if (eq == nullptr)
        {
            equality_values(aType, true);
            return;
        }
        auto const right = temporary(type::I64);
        auto const left = temporary(type::I64);
        set(right);
        set(left);
        if (!eq->sub->defined)
            iProgram.define_subprogram(eq);
        static_link_for(eq);
        get(left);
        get(right);
        call_function(eq);
        check();
        op(opcode::I32ConvertI64);
        release(left);
        release(right);
    }

    void function_generator::equality_values(ada_type* aType, bool aComposed)
    {
        // (two composite values' addresses on the stack: an i32, whether they are equal (by their components))
        auto* t = aType->view();
        (void)aComposed;
        auto const right = local(type::I64);
        auto const left = local(type::I64);
        set(right);
        set(left);
        if (t->kind == type_kind::Array)
        {
            auto* c = t->component != nullptr ? t->component->view() : nullptr;
            auto const dimensions = t->base_type()->view()->indexes.size();
            if (c != nullptr && (c->is_discrete() || c->is_access()) && dimensions == 1u && !t->base_type()->view()->component_size_clause)
            {
                get(left);
                get(right);
                i64(static_cast<std::int64_t>(iProgram.component_size(t)));
                runtime("compare_arrays", { type::I64, type::I64, type::I64 }, type::I32);
                op(opcode::I32Eqz);
                return;
            }
            // (element by element: the lengths (in each dimension; or both null: equal, 4.5.2(23)), then each element)
            auto const n = local(type::I64);
            array_length(left, dimensions);
            tee(n);
            array_length(right, dimensions);
            op(opcode::I64Eq);
            for (std::size_t d = 0u; d < dimensions && dimensions > 1u; ++d)
            {
                // (the same length in this dimension, or no components)
                auto const length = [&](std::uint32_t aArray)
                {
                    auto const k = temporary(type::I64);
                    bounds(aArray, d, false);
                    bounds(aArray, d, true);
                    op(opcode::I64Sub);
                    i64(1);
                    op(opcode::I64Add);
                    tee(k);
                    i64(0);
                    get(k);
                    i64(0);
                    op(opcode::I64GtS);
                    op(opcode::Select);
                    release(k);
                };
                length(left);
                length(right);
                op(opcode::I64Eq);
                get(n);
                op(opcode::I64Eqz);
                op(opcode::I32Ior);
                op(opcode::I32And);
            }
            begin_if(type::I32);
            {
                auto const result = local(type::I32);
                i32(1);
                set(result);
                auto const lp = local(type::I64);
                auto const rp = local(type::I64);
                data_address(left);
                set(lp);
                data_address(right);
                set(rp);
                begin(control_kind::Block);
                auto const exit = iControls.size() - 1u;
                begin(control_kind::Loop);
                auto const head = iControls.size() - 1u;
                get(n);
                op(opcode::I64Eqz);
                branch(exit, true);
                if (c != nullptr && c->is_composite())
                {
                    get(lp);
                    get(rp);
                    component_equality(c);
                }
                else
                {
                    get(lp);
                    load(kind_of(c), iProgram.scalar_size(c), 0u, !is_unsigned(c));
                    get(rp);
                    load(kind_of(c), iProgram.scalar_size(c), 0u, !is_unsigned(c));
                    op(kind_of(c) == vkind::I64 ? opcode::I64Eq : kind_of(c) == vkind::F32 ? opcode::F32Eq : opcode::F64Eq);
                }
                op(opcode::I32Eqz);
                begin_if();
                i32(0);
                set(result);
                branch(exit);
                end();
                get(lp);
                element_size(t);
                op(opcode::I64Add);
                set(lp);
                get(rp);
                element_size(t);
                op(opcode::I64Add);
                set(rp);
                get(n);
                i64(1);
                op(opcode::I64Sub);
                set(n);
                branch(head);
                end();
                end();
                get(result);
            }
            else_();
            i32(0);
            end();
            return;
        }
        // (a record: each component (of the variant that the discriminants select: all, here), a user-defined "=" of a
        // component's type is not used for predefined equality of untagged records' non-limited components (except tagged))
        auto* base = t->kind == type_kind::ClassWide ? t->specific->view()->base_type()->view() : t->base_type()->view();
        iProgram.layout(base);
        i32(1);
        // (a type extension whose ancestor has a user-defined "=": the parent part's equality is that's (then the extension's
        // components))
        // (the parent part: its components (by name); a new discriminant is the extension's)
        std::set<std::string> parentPart;
        for (auto* p = base->tagged ? base->parent : nullptr; p != nullptr; p = p->view()->base_type()->view()->parent)
        {
            auto* pb = p->view()->base_type()->view();
            if (!pb->tagged)
                break;
            if (auto* eq = primitive_equality(pb))
            {
                get(left);
                get(right);
                component_equality(pb);
                op(opcode::I32And);
                iProgram.layout(pb);
                for (auto const& pc : pb->components)
                    if (pc.ent != nullptr)
                        parentPart.insert(pc.ent->name);
                (void)eq;
                break;
            }
        }
        auto const extension_part = [&](component_info const& c) { return c.ent == nullptr || !parentPart.contains(c.ent->name); };
        std::size_t const firstComponent = 0u;
        // (the discriminants first: the other components are compared only if they are equal (their variants, and their
        // components' bounds, are then the same))
        bool const discriminated = std::any_of(base->components.begin() + static_cast<std::ptrdiff_t>(firstComponent), base->components.end(),
            [&](component_info const& c) { return c.discriminant && c.offset && c.type != nullptr && extension_part(c); });
        for (int pass = discriminated ? 0 : 1; pass < 2; ++pass)
        {
        if (pass == 1 && discriminated)
        {
            begin_if(type::I32);
            i32(1);
        }
        for (std::size_t ci = firstComponent; ci < base->components.size(); ++ci)
        {
            auto const& c = base->components[ci];
            if (!c.offset || c.type == nullptr || !extension_part(c))
                continue;
            if (discriminated && c.discriminant != (pass == 0))
                continue;
            auto* ct = c.type->view();
            // (a component of a variant: compared if the (equal) discriminants select it)
            if (c.variant >= 0)
            {
                variant_selected(base, c, left);
                begin_if(type::I32);
            }
            if (ct->is_composite())
            {
                component_address(base, c, left);
                component_address(base, c, right);
                component_equality(ct);
            }
            else
            {
                get(left);
                component_load(c);
                get(right);
                component_load(c);
                auto const k = kind_of(ct);
                op(k == vkind::F32 ? opcode::F32Eq : k == vkind::F64 ? opcode::F64Eq : opcode::I64Eq);
            }
            if (c.variant >= 0)
            {
                else_();
                i32(1);
                end();
            }
            op(opcode::I32And);
        }
        if (pass == 1 && discriminated)
        {
            else_();
            i32(0);
            end();
        }
        }
        if (base->tagged && t->kind == type_kind::ClassWide)
        {
            // (class-wide operands: and the same tag (a specific type's equality: of its own components, e.g. of view
            // conversions of objects of its descendants))
            get(left);
            load64(0u);
            get(right);
            load64(0u);
            op(opcode::I64Eq);
            op(opcode::I32And);
        }
    }

    void function_generator::concatenation(entity* aOperator, std::vector<node*> const& aOperands, ada_type* aType)
    {
        // (a new array on the secondary stack: the left operand's elements then the right's; its first: the left's (a null
        // left: the right itself), or the index subtype's first)
        auto* t = (aType != nullptr ? aType : aOperator->sub->result)->view();
        auto* arrayType = t->base_type()->view();
        auto const elementSize = iProgram.component_size(arrayType);
        auto* index = arrayType->indexes.empty() ? nullptr : arrayType->indexes[0];
        std::uint32_t descriptors[2];
        for (std::size_t i = 0u; i < 2u; ++i)
        {
            auto const& p = aOperator->sub->parameters[i];
            descriptors[i] = local(type::I64);
            bool const component = !iProgram.same_type(p.type, arrayType);
            if (component)
            {
                // (a component: a one element array (its first: the index subtype's first))
                auto const value = temporary(wasm_of(p.type));
                if (is_aggregate_node(aOperands[i]))
                    aggregate(aOperands[i], p.type);
                else
                    expression(aOperands[i], p.type);
                set(value);
                i64(static_cast<std::int64_t>(24u + elementSize + 8u));
                runtime("ss_allocate", { type::I64 }, type::I64);
                tee(descriptors[i]);
                i64(24);
                store64(0u);
                get(descriptors[i]);
                if (index != nullptr)
                    scalar_low(index);
                else
                    i64(1);
                store64(8u);
                get(descriptors[i]);
                get(descriptors[i]);
                load64(8u);
                store64(16u);
                get(descriptors[i]);
                if (p.type->view()->is_composite())
                {
                    i64(24);
                    op(opcode::I64Add);
                    get(value);
                    i64(static_cast<std::int64_t>(elementSize));
                    runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                }
                else
                {
                    get(value);
                    store(kind_of(p.type), elementSize, 24u);
                }
                release(value);
            }
            else
            {
                if (is_aggregate_node(aOperands[i]))
                    aggregate(aOperands[i], aOperands[i]->type != nullptr ? aOperands[i]->type : arrayType);
                else
                    expression(aOperands[i], aOperands[i]->type);
                set(descriptors[i]);
            }
        }
        i64(static_cast<std::int64_t>(elementSize));
        get(descriptors[0]);
        get(descriptors[1]);
        if (index != nullptr)
            scalar_low(index);
        else
            i64(1);
        runtime("concatenate", { type::I64, type::I64, type::I64, type::I64 }, type::I64);
        check();
    }

    // --- attributes

    void function_generator::attribute(node* aAttribute)
    {
        auto const name = name_text(aAttribute->child(1)->source);
        auto* prefix = aAttribute->child(0);
        auto const arguments = attribute_arguments(aAttribute);
        auto* resultType = aAttribute->type;
        // (static: a constant (not of an implicit dereference: its prefix is checked not to be null))
        if (resultType != nullptr && resultType->view()->is_scalar() && !(prefix->mark & 0x2000u))
        {
            auto v = iProgram.evaluate(aAttribute, nullptr);
            if (v.known() && !v.is_string())
            {
                constant_value(v, resultType);
                return;
            }
        }
        bool const typePrefix = meaning_of(prefix) == meaning::Type || (prefix->is(construct::Attribute) && meaning_of(prefix) != meaning::Value &&
            prefix->first(construct::Arguments) == nullptr);
        auto* pt = prefix->type;
        if (prefix->mark & 0x2000u)
            pt = iProgram.designated_of(pt);
        auto* v = pt != nullptr ? pt->view() : nullptr;
        if (name == "first" || name == "last" || name == "length")
        {
            std::size_t dimension = 0u;
            if (!arguments.empty())
            {
                auto d = iProgram.evaluate(arguments[0], nullptr);
                if (d.is_integer())
                    dimension = static_cast<std::size_t>(d.integer().to_int64() - 1);
            }
            if (v != nullptr && v->kind == type_kind::Array)
            {
                if (typePrefix)
                {
                    auto* index = v->indexes[dimension];
                    if (name == "first")
                        scalar_low(index);
                    else if (name == "last")
                        scalar_high(index);
                    else
                    {
                        scalar_high(index);
                        scalar_low(index);
                        op(opcode::I64Sub);
                        i64(1);
                        op(opcode::I64Add);
                        auto const n = temporary(type::I64);
                        tee(n);
                        i64(0);
                        get(n);
                        i64(0);
                        op(opcode::I64GtS);
                        op(opcode::Select);
                        release(n);
                    }
                    return;
                }
                if (prefix->mark & 0x2000u)
                {
                    expression(prefix, prefix->type);
                    null_check(aAttribute);
                }
                else
                    address(prefix);
                auto const descriptor = temporary(type::I64);
                set(descriptor);
                if (name == "first")
                    bounds(descriptor, dimension, true);
                else if (name == "last")
                    bounds(descriptor, dimension, false);
                else
                {
                    bounds(descriptor, dimension, false);
                    bounds(descriptor, dimension, true);
                    op(opcode::I64Sub);
                    i64(1);
                    op(opcode::I64Add);
                    auto const n = temporary(type::I64);
                    tee(n);
                    i64(0);
                    get(n);
                    i64(0);
                    op(opcode::I64GtS);
                    op(opcode::Select);
                    release(n);
                }
                release(descriptor);
                return;
            }
            if (v == nullptr)
                iProgram.error(aAttribute, "internal error: attribute prefix has no type");
            if (name == "first")
                scalar_low(v);
            else
                scalar_high(v);
            return;
        }
        if (name == "pos" || name == "enum_rep")
        {
            expression(arguments[0], v->base_type());
            if (name == "enum_rep")
                enumeration_code(v);
            return;
        }
        if (name == "val" || name == "enum_val")
        {
            expression(arguments[0], arguments[0]->type);
            if (auto* base = v->base_type()->view(); name == "enum_val" && base->kind == type_kind::Enumeration && !base->representation.empty())
            {
                // (the value of the code: none, Constraint_Error)
                enumeration_position(v);
                auto const value = temporary(type::I64);
                tee(value);
                i64(static_cast<std::int64_t>(base->representation.size()));
                op(opcode::I64GeU);
                begin_if();
                raise_check(0, aAttribute, "range check failed");
                end();
                get(value);
                release(value);
                return;
            }
            range_check(v->base_type(), arguments[0]->type, aAttribute);
            return;
        }
        if (name == "succ" || name == "pred")
        {
            expression(arguments[0], v->base_type());
            if (v->base_type()->view()->kind == type_kind::Float)
            {
                bool const single = kind_of(v) == vkind::F32;
                if (single)
                    op(opcode::F64ConvertF32);
                i32(single ? 1 : 0);
                runtime(name == "succ" ? "float_succ" : "float_pred", { type::F64, type::I32 }, type::F64);
                if (single)
                    op(opcode::F32ConvertF64);
                return;
            }
            // (checked: not the base type's last (first))
            auto const value = temporary(type::I64);
            tee(value);
            if (v->base_type()->view()->kind == type_kind::Modular)
            {
                i64(name == "succ" ? 1 : -1);
                op(opcode::I64Add);
                auto const& m = v->base_type()->view()->modulus;
                if (m.fits_uint64())
                {
                    // ((value + m) mod m)
                    i64(static_cast<std::int64_t>(m.to_uint64()));
                    op(opcode::I64Add);
                    i64(static_cast<std::int64_t>(m.to_uint64()));
                    op(opcode::I64RemU);
                }
                release(value);
                return;
            }
            name == "succ" ? scalar_high(v->base_type()) : scalar_low(v->base_type());
            op(opcode::I64Eq);
            begin_if();
            raise_check(0, aAttribute, "range check failed");
            end();
            get(value);
            i64(name == "succ" ? 1 : -1);
            op(opcode::I64Add);
            release(value);
            return;
        }
        if (name == "min" || name == "max")
        {
            auto const k = kind_of(v);
            auto const a = temporary(wasm(k));
            auto const b = temporary(wasm(k));
            expression(arguments[0], v->base_type());
            set(a);
            expression(arguments[1], v->base_type());
            set(b);
            if (k == vkind::I64)
            {
                get(a);
                get(b);
                get(a);
                get(b);
                op(name == "min" ? opcode::I64LtS : opcode::I64GtS);
                op(opcode::Select);
            }
            else
            {
                get(a);
                get(b);
                op(name == "min" ? (k == vkind::F32 ? opcode::F32Min : opcode::F64Min) : (k == vkind::F32 ? opcode::F32Max : opcode::F64Max));
            }
            release(a);
            release(b);
            return;
        }
        if (name == "image" || name == "wide_image" || name == "wide_wide_image" || name == "img")
        {
            if (!arguments.empty())
                image(v, arguments[0]);
            else
                image(v, prefix);
            // ('Wide_Image, 'Wide_Wide_Image: the image's characters as wide characters)
            if (name != "image" && name != "img" && resultType != nullptr && resultType->view()->kind == type_kind::Array)
                widen_string(resultType);
            return;
        }
        if (name == "value" || name == "wide_value" || name == "wide_wide_value")
        {
            value_attribute(v, arguments[0]);
            return;
        }
        if (name == "input")
        {
            stream_attribute(aAttribute, false);
            return;
        }
        if (name == "address" || name == "access" || name == "unchecked_access" || name == "unrestricted_access")
        {
            if (meaning_of(prefix) == meaning::Subprogram && prefix->ent != nullptr)
            {
                subprogram_access(prefix->ent, prefix->is(construct::Selected) && iProgram.self_type(ultimate(prefix->ent)) != nullptr ? prefix->child(0) : nullptr);
                return;
            }
            address(prefix);
            // (X'Access of a named access type: X's accessibility level not deeper than its (3.10.2(29/4); static except an access
            // parameter's dereference's, and in an instance))
            if ((name == "access" || name == "unchecked_access") && aAttribute->type != nullptr)
                class_constraint_check(aAttribute->type, aAttribute);
            if (name == "access" && aAttribute->type != nullptr && aAttribute->type->view()->kind == type_kind::Access &&
                !aAttribute->type->view()->anonymous_access)
            {
                auto const level = object_level(prefix);
                if (!level || *level > 0)
                    accessibility_check(level, iProgram.type_level(aAttribute->type), aAttribute);
            }
            if (name == "address" && v != nullptr && v->kind == type_kind::Array && !typePrefix)
            {
                // (an array's address: its data's (its first component's))
                auto const descriptor = temporary(type::I64);
                set(descriptor);
                data_address(descriptor);
                release(descriptor);
            }
            return;
        }
        if (name == "size" || name == "object_size")
        {
            // (an object whose size is specified: that)
            if (name == "size" && !typePrefix && prefix->ent != nullptr && prefix->ent->is_object() && prefix->ent->size_clause != 0u)
            {
                i64(static_cast<std::int64_t>(prefix->ent->size_clause));
                return;
            }
            if (v != nullptr && v->kind == type_kind::Array && !typePrefix)
            {
                address(prefix);
                auto const descriptor = temporary(type::I64);
                set(descriptor);
                array_length(descriptor, v->base_type()->view()->indexes.size());
                element_size(v);
                op(opcode::I64Mul);
                i64(8);
                op(opcode::I64Mul);
                release(descriptor);
                return;
            }
            if (v != nullptr && v->kind == type_kind::Array && typePrefix && v->constrained)
            {
                // (a constrained array subtype: its data's size (its bounds may be dynamic))
                subtype_descriptor(v);
                auto const descriptor = temporary(type::I64);
                set(descriptor);
                array_length(descriptor, v->base_type()->view()->indexes.size());
                element_size(v);
                op(opcode::I64Mul);
                i64(8);
                op(opcode::I64Mul);
                release(descriptor);
                return;
            }
            if (v != nullptr && typePrefix && v->kind == type_kind::Record && is_runtime_sized(v))
            {
                // (a dynamically sized record subtype (its discriminants' values: its constraint's, else their defaults): its objects'
                // size)
                record_subtype_size(v);
                i64(8);
                op(opcode::I64Mul);
                return;
            }
            if (v != nullptr && !typePrefix && (v->kind == type_kind::ClassWide || (v->kind == type_kind::Record && is_runtime_sized(v))))
            {
                // (a class-wide or dynamically sized record object: its size (its tag's, or its tail's too))
                address(prefix);
                auto const object = temporary(type::I64);
                set(object);
                record_size(v, object);
                i64(8);
                op(opcode::I64Mul);
                release(object);
                return;
            }
            // (a function call's: evaluated (its value's size is static))
            if (!typePrefix && (meaning_of(prefix) == meaning::Call || (prefix->is(construct::Attribute) && prefix->first(construct::Arguments) != nullptr)))
            {
                expression(prefix, prefix->type);
                drop();
            }
            i64(static_cast<std::int64_t>(iProgram.object_size(v) * 8u));
            return;
        }
        if (name == "truncation" || name == "floor" || name == "ceiling" || name == "rounding" || name == "unbiased_rounding" || name == "machine_rounding")
        {
            auto const k = kind_of(v);
            expression(arguments[0], v->base_type());
            if (name == "rounding")
            {
                // (half away from zero: its integral part, plus one away from zero if its fraction is at least a half)
                bool const single = k == vkind::F32;
                auto const x = temporary(wasm(k));
                auto const t = temporary(wasm(k));
                set(x);
                get(x);
                op(single ? opcode::F32Trunc : opcode::F64Trunc);
                set(t);
                get(t);
                if (single)
                    f32(1.0f);
                else
                    f64(1.0);
                get(x);
                op(single ? opcode::F32CopySign : opcode::F64CopySign);
                op(single ? opcode::F32Add : opcode::F64Add);
                get(t);
                get(x);
                get(t);
                op(single ? opcode::F32Sub : opcode::F64Sub);
                op(single ? opcode::F32Abs : opcode::F64Abs);
                if (single)
                    f32(0.5f);
                else
                    f64(0.5);
                op(single ? opcode::F32Ge : opcode::F64Ge);
                op(opcode::Select);
                release(t);
                release(x);
                return;
            }
            opcode o = opcode::F64Trunc;
            if (name == "floor")
                o = k == vkind::F32 ? opcode::F32Floor : opcode::F64Floor;
            else if (name == "ceiling")
                o = k == vkind::F32 ? opcode::F32Ceil : opcode::F64Ceil;
            else if (name == "truncation")
                o = k == vkind::F32 ? opcode::F32Trunc : opcode::F64Trunc;
            else
                o = k == vkind::F32 ? opcode::F32NearestInt : opcode::F64NearestInt;
            op(o);
            return;
        }
        if (name == "copy_sign")
        {
            auto const k = kind_of(v);
            expression(arguments[0], v->base_type());
            expression(arguments[1], v->base_type());
            op(k == vkind::F32 ? opcode::F32CopySign : opcode::F64CopySign);
            return;
        }
        if (name == "mod")
        {
            expression(arguments[0], arguments[0]->type);
            auto const& m = v->base_type()->view()->modulus;
            i64(static_cast<std::int64_t>(m.to_uint64()));
            runtime("mod_integer", { type::I64, type::I64 }, type::I64);
            return;
        }
        if (name == "valid")
        {
            // (a scalar object's value: in its subtype)
            expression(prefix, prefix->type);
            auto const k = kind_of(v);
            auto const x = temporary(wasm(k));
            tee(x);
            if (k == vkind::I64)
            {
                scalar_low(v);
                op(opcode::I64GeS);
                get(x);
                scalar_high(v);
                op(opcode::I64LeS);
            }
            else
            {
                scalar_low(v);
                op(k == vkind::F32 ? opcode::F32Ge : opcode::F64Ge);
                get(x);
                scalar_high(v);
                op(k == vkind::F32 ? opcode::F32Le : opcode::F64Le);
            }
            op(opcode::I32And);
            // (and its predicate (13.9.2(3/4)))
            if (auto* predicate = prefix->type != nullptr && prefix->type->predicate != nullptr ? prefix->type->predicate : v->predicate)
            {
                begin_if(type::I32);
                predicate_test(predicate, x, false);
                else_();
                i32(0);
                end();
            }
            to_boolean();
            release(x);
            return;
        }
        if (name == "constrained")
        {
            // (an object: a formal's actual's; a constant, a value or a dereference: constrained (3.7.2(3/3)))
            if (!typePrefix && prefix->ent != nullptr && iUnconstrainedFlags.contains(prefix->ent))
            {
                get(iUnconstrainedFlags[prefix->ent]);
                i64(1);
                op(opcode::I64Xor);
                return;
            }
            if (!typePrefix && mutable_record(prefix->type))
            {
                unconstrained_view(prefix);
                i64(1);
                op(opcode::I64Xor);
                return;
            }
            // (an object of a type whose discriminants have no defaults (or a constrained subtype's): constrained)
            i64(!typePrefix || (v != nullptr && v->constrained) ? 1 : 0);
            return;
        }
        if (name == "tag")
        {
            if (typePrefix)
            {
                i64(static_cast<std::int64_t>(iProgram.tag_of(v)));
                return;
            }
            address(prefix);
            load64(0u);
            return;
        }
        if (name == "old" || name == "loop_entry")
        {
            if (auto v = iOldValues.find(aAttribute); v != iOldValues.end())
            {
                get(v->second);
                return;
            }
            expression(prefix, prefix->type);
            return;
        }
        if (name == "result")
        {
            get(iResult);
            return;
        }
        if (name == "range")
        {
            iProgram.error(aAttribute, "'Range is not a value");
        }
        if (name == "identity" && meaning_of(prefix) == meaning::Exception && prefix->ent != nullptr)
        {
            auto* x = prefix->ent;
            while (x->renamed != nullptr)
                x = x->renamed;
            i64(static_cast<std::int64_t>(x->exception_id));
            return;
        }
        if (name == "callable" || name == "terminated" || name == "count" || name == "identity" || name == "caller" || name == "storage_size")
        {
            tasking_attribute(aAttribute, name);
            return;
        }
        if (name == "exponent" || name == "fraction" || name == "scaling" || name == "compose" || name == "remainder" || name == "adjacent" ||
            name == "leading_part" || name == "machine" || name == "model" || name == "round")
        {
            float_attribute(aAttribute, name, v, arguments);
            return;
        }
        if (name == "fore" && v != nullptr && v->base_type()->view()->is_fixed())
        {
            // (a subtype whose bounds are not static: the integer part's digits of its greatest magnitude, and a sign (at least 2))
            auto* base = v->base_type()->view();
            auto const magnitude = local(type::F64);
            auto const fore = local(type::I64);
            auto const absolute = [&](auto aBound)
            {
                aBound();
                auto const x = temporary(type::I64);
                set(x);
                i64(0);
                get(x);
                op(opcode::I64Sub);
                get(x);
                get(x);
                i64(0);
                op(opcode::I64LtS);
                op(opcode::Select);
                release(x);
                op(opcode::F64SConvertI64);
                f64(base->small.to_double());
                op(opcode::F64Mul);
            };
            absolute([&]() { scalar_low(v); });
            absolute([&]() { scalar_high(v); });
            op(opcode::F64Max);
            op(opcode::F64Floor);
            set(magnitude);
            i64(2);
            set(fore);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            get(magnitude);
            f64(10.0);
            op(opcode::F64Lt);
            branch(exit, true);
            get(magnitude);
            f64(10.0);
            op(opcode::F64Div);
            op(opcode::F64Floor);
            set(magnitude);
            get(fore);
            i64(1);
            op(opcode::I64Add);
            set(fore);
            branch(head);
            end();
            end();
            get(fore);
            if (resultType != nullptr && kind_of(resultType) != vkind::I64)
                op(opcode::F64SConvertI64);
            return;
        }
        if ((name == "width" || name == "wide_width" || name == "wide_wide_width") && v != nullptr && v->is_discrete())
        {
            // (a subtype whose bounds are not static: the longest image of its values (0: a null range))
            auto* base = v->base_type()->view();
            auto const low = local(type::I64);
            auto const high = local(type::I64);
            auto const result = local(type::I64);
            scalar_low(v);
            set(low);
            scalar_high(v);
            set(high);
            i64(0);
            set(result);
            get(low);
            get(high);
            op(opcode::I64LeS);
            begin_if();
            if (base->kind == type_kind::Enumeration && base->standard_character)
            {
                i64(12);
                i64(3);
                get(high);
                i64(255);
                op(opcode::I64GtS);
                op(opcode::Select);
                set(result);
            }
            else if (base->kind == type_kind::Enumeration)
            {
                // (the literals' lengths: a table)
                std::string lengths;
                for (auto* l : base->literals)
                    lengths += static_cast<char>(std::min<std::size_t>(l->name.size(), 255u));
                auto const table = iProgram.static_string(lengths) + 24u;
                auto const i = local(type::I64);
                get(low);
                set(i);
                begin(control_kind::Block);
                auto const exit = iControls.size() - 1u;
                begin(control_kind::Loop);
                auto const head = iControls.size() - 1u;
                get(i);
                get(high);
                op(opcode::I64GtS);
                branch(exit, true);
                auto const n = temporary(type::I64);
                i64(static_cast<std::int64_t>(table));
                get(i);
                op(opcode::I64Add);
                load(vkind::I64, 1u, 0u, false);
                tee(n);
                get(result);
                get(n);
                get(result);
                op(opcode::I64GtS);
                op(opcode::Select);
                set(result);
                release(n);
                get(i);
                i64(1);
                op(opcode::I64Add);
                set(i);
                branch(head);
                end();
                end();
            }
            else
            {
                // (an integer's image: a sign or a space, then its digits)
                for (auto const bound : { low, high })
                {
                    auto const m = temporary(type::I64);
                    auto const digits = temporary(type::I64);
                    // (its magnitude: -bound if it is negative, else bound)
                    i64(0);
                    get(bound);
                    op(opcode::I64Sub);
                    get(bound);
                    get(bound);
                    i64(0);
                    op(opcode::I64LtS);
                    op(opcode::Select);
                    set(m);
                    i64(2);
                    set(digits);
                    begin(control_kind::Block);
                    auto const exit = iControls.size() - 1u;
                    begin(control_kind::Loop);
                    auto const head = iControls.size() - 1u;
                    get(m);
                    i64(10);
                    op(opcode::I64LtU);
                    branch(exit, true);
                    get(m);
                    i64(10);
                    op(opcode::I64DivU);
                    set(m);
                    get(digits);
                    i64(1);
                    op(opcode::I64Add);
                    set(digits);
                    branch(head);
                    end();
                    end();
                    get(digits);
                    get(result);
                    get(digits);
                    get(result);
                    op(opcode::I64GtS);
                    op(opcode::Select);
                    set(result);
                    release(m);
                    release(digits);
                }
            }
            end();
            get(result);
            return;
        }
        if (name == "component_size" && v != nullptr && v->kind == type_kind::Array)
        {
            i64(static_cast<std::int64_t>(iProgram.component_size(v->base_type()->view()) * 8u));
            return;
        }
        if (name == "external_tag" && v != nullptr)
        {
            // (the type's tag's name)
            auto* specific = v->kind == type_kind::ClassWide ? v->specific->view() : v;
            if (typePrefix)
                i64(static_cast<std::int64_t>(iProgram.tag_of(specific)));
            else
            {
                address(prefix);
                load64(0u);
            }
            // (its specified external tag, else its expanded name)
            auto const tag = temporary(type::I64);
            tee(tag);
            load64(static_cast<std::uint32_t>(program::TagExternal));
            get(tag);
            load64(8u);
            get(tag);
            load64(static_cast<std::uint32_t>(program::TagExternal));
            op(opcode::I64Eqz);
            op(opcode::I32Eqz);
            op(opcode::Select);
            release(tag);
            return;
        }
        if (name == "storage_pool")
        {
            // (an access type's storage pool: the one specified (its ancestor's), else the standard pool (the heap's))
            if (auto* pool = storage_pool_of(prefix->type))
            {
                address(pool);
                return;
            }
            i64(static_cast<std::int64_t>(iProgram.standard_pool()));
            return;
        }
        if (name == "max_size_in_storage_elements" && v != nullptr)
        {
            auto const size = iProgram.object_size(v);
            if (size == 0u && v->kind == type_kind::Array && v->constrained)
            {
                // (a constrained array subtype whose bounds are dynamic: its data's size)
                subtype_descriptor(v);
                auto const descriptor = temporary(type::I64);
                set(descriptor);
                array_length(descriptor, v->base_type()->view()->indexes.size());
                element_size(v);
                op(opcode::I64Mul);
                release(descriptor);
                return;
            }
            if (size == 0u && v->kind == type_kind::Record && is_runtime_sized(v))
            {
                record_subtype_size(v);
                return;
            }
            i64(size != 0u ? static_cast<std::int64_t>(size) : std::int64_t{ 0x7FFFFFFF });
            return;
        }
        if (name == "alignment")
        {
            // (a scalar's: its size; a composite's: 8 (its components' largest alignment, at most 8))
            auto* at = v;
            std::uint64_t alignment = 8u;
            if (at != nullptr && (at->is_scalar() || at->is_access()))
                alignment = std::max<std::uint64_t>(iProgram.scalar_size(at), 1u);
            // (specified: for the object, or its subtype)
            if (auto* pe = !typePrefix ? prefix->ent : nullptr; pe != nullptr && pe->is_object() && iProgram.object_alignment(pe) != 0u)
                alignment = iProgram.object_alignment(pe);
            else if (auto const specified = iProgram.type_alignment_clause(pt); specified != 0u)
                alignment = specified;
            i64(static_cast<std::int64_t>(alignment));
            return;
        }
        if ((name == "has_same_storage" || name == "overlaps_storage") && !arguments.empty())
        {
            // (two objects' storage: (its start, its size in bytes) each; the same, or overlapping (13.3(73.1/3)))
            auto const storage = [&](node* aObject, std::uint32_t aStart, std::uint32_t aSize)
            {
                auto* ot = aObject->type != nullptr ? aObject->type->view() : nullptr;
                address(aObject);
                if (ot != nullptr && ot->kind == type_kind::Array)
                {
                    auto const descriptor = temporary(type::I64);
                    set(descriptor);
                    data_address(descriptor);
                    set(aStart);
                    array_length(descriptor, ot->base_type()->view()->indexes.size());
                    element_size(ot);
                    op(opcode::I64Mul);
                    set(aSize);
                    release(descriptor);
                }
                else if (ot != nullptr && (ot->kind == type_kind::Record || ot->kind == type_kind::ClassWide) && is_runtime_sized(ot))
                {
                    set(aStart);
                    record_size(ot, aStart);
                    set(aSize);
                }
                else
                {
                    set(aStart);
                    i64(static_cast<std::int64_t>(ot == nullptr ? 8u : ot->is_scalar() || ot->is_access() ? iProgram.scalar_size(ot) : iProgram.object_size(ot)));
                    set(aSize);
                }
            };
            auto const xs = local(type::I64), xn = local(type::I64), ys = local(type::I64), yn = local(type::I64);
            storage(prefix, xs, xn);
            storage(arguments[0], ys, yn);
            if (name == "has_same_storage")
            {
                get(xs);
                get(ys);
                op(opcode::I64Eq);
                get(xn);
                get(yn);
                op(opcode::I64Eq);
                op(opcode::I32And);
                get(xn);
                i64(0);
                op(opcode::I64Ne);
                op(opcode::I32And);
            }
            else
            {
                // (xs < ys + yn and ys < xs + xn, both not empty)
                get(xs);
                get(ys);
                get(yn);
                op(opcode::I64Add);
                op(opcode::I64LtU);
                get(ys);
                get(xs);
                get(xn);
                op(opcode::I64Add);
                op(opcode::I64LtU);
                op(opcode::I32And);
                get(xn);
                i64(0);
                op(opcode::I64Ne);
                op(opcode::I32And);
                get(yn);
                i64(0);
                op(opcode::I64Ne);
                op(opcode::I32And);
            }
            to_boolean();
            return;
        }
        iProgram.error(aAttribute, "attribute '" + name + " not supported yet");
    }

    void function_generator::float_attribute(node* aAttribute, std::string const& aName, ada_type* aType, std::vector<node*> const& aArguments)
    {
        auto const k = kind_of(aType);
        if (aName == "round")
        {
            // (a decimal fixed point type's rounding of a value: converted (rounded))
            bool const rounding = iRounding;
            iRounding = true;
            conversion(aArguments[0], aArguments[0]->type, aType, aAttribute);
            iRounding = rounding;
            return;
        }
        if (aName == "machine" || aName == "model")
        {
            expression(aArguments[0], aType->base_type());
            return;
        }
        expression(aArguments[0], aType->base_type());
        if (k == vkind::F32)
            op(opcode::F64ConvertF32);
        if (aArguments.size() >= 2u)
        {
            expression(aArguments[1], aArguments[1]->type);
            if (kind_of(aArguments[1]->type) == vkind::F32)
                op(opcode::F64ConvertF32);
            runtime("float_" + aName, { type::F64, kind_of(aArguments[1]->type) == vkind::I64 ? type::I64 : type::F64 }, aName == "exponent" ? type::I64 : type::F64);
        }
        else
            runtime("float_" + aName, { type::F64 }, aName == "exponent" ? type::I64 : type::F64);
        if (aName != "exponent" && k == vkind::F32)
            op(opcode::F32ConvertF64);
    }

    void function_generator::attribute_call(entity* aCallee, std::vector<node*> const& aActuals, node* aCall)
    {
        // (an attribute as a subprogram (e.g. a generic actual): its prefix's attribute of the actuals)
        auto* attributeNode = aCallee->decl;
        if (attributeNode == nullptr || !attributeNode->is(construct::Attribute))
            iProgram.error(aCall, "attribute subprogram not supported yet");
        auto const name = name_text(attributeNode->child(1)->source);
        auto* t = attributeNode->child(0)->type;
        if (name == "image" || name == "wide_image" || name == "wide_wide_image")
        {
            image(t, aActuals[0]);
            if (name != "image" && aCallee->sub != nullptr && aCallee->sub->result != nullptr && aCallee->sub->result->view()->kind == type_kind::Array)
                widen_string(aCallee->sub->result);
            return;
        }
        if (name == "value")
        {
            value_attribute(t, aActuals[0]);
            return;
        }
        if (name == "succ" || name == "pred" || name == "pos" || name == "val")
        {
            expression(aActuals[0], aActuals[0]->type);
            if (name == "succ" || name == "pred")
            {
                i64(name == "succ" ? 1 : -1);
                op(opcode::I64Add);
            }
            return;
        }
        if (name == "min" || name == "max")
        {
            auto const a = temporary(type::I64);
            auto const b = temporary(type::I64);
            expression(aActuals[0], t);
            set(a);
            expression(aActuals[1], t);
            set(b);
            get(a);
            get(b);
            get(a);
            get(b);
            op(name == "min" ? opcode::I64LtS : opcode::I64GtS);
            op(opcode::Select);
            release(a);
            release(b);
            return;
        }
        iProgram.error(aCall, "attribute '" + name + " as a subprogram not supported yet");
    }

    void function_generator::image(ada_type* aType, node* aValue)
    {
        // (a scalar's image (on the secondary stack))
        auto* base = aType->view()->base_type()->view();
        expression(aValue, base);
        if (base->kind == type_kind::Enumeration)
        {
            if (base->standard_character)
            {
                runtime("image_character", { type::I64 }, type::I64);
                return;
            }
            auto const names = iProgram.enumeration_names(base);
            auto const index = temporary(type::I64);
            set(index);
            i64(static_cast<std::int64_t>(names));
            get(index);
            runtime("image_enumeration", { type::I64, type::I64 }, type::I64);
            release(index);
            return;
        }
        if (base->kind == type_kind::Modular)
        {
            runtime("image_unsigned", { type::I64 }, type::I64);
            return;
        }
        if (base->kind == type_kind::Float || base->kind == type_kind::UniversalReal)
        {
            if (kind_of(base) == vkind::F32)
                op(opcode::F64ConvertF32);
            // (universal_real: as root_real, of System.Max_Digits (an f64's 15))
            i64(base->kind == type_kind::Float ? base->digits : 15);
            runtime("image_float", { type::F64, type::I64 }, type::I64);
            return;
        }
        if (base->is_fixed())
        {
            auto v = iProgram.evaluate_attribute_static(aType, "aft");
            auto const aft = v.is_integer() ? v.integer().to_int64() : 1;
            if (base->small.numerator().fits_int64() && base->small.denominator().fits_int64())
            {
                // (exactly: the count of smalls times small's numerator over its denominator)
                i64(base->small.numerator().to_int64());
                i64(base->small.denominator().to_int64());
                i64(aft);
                runtime("image_fixed_exact", { type::I64, type::I64, type::I64, type::I64 }, type::I64);
                return;
            }
            f64(base->small.to_double());
            i64(aft);
            runtime("image_fixed", { type::I64, type::F64, type::I64 }, type::I64);
            return;
        }
        runtime("image_integer", { type::I64 }, type::I64);
    }

    void function_generator::widen_string(ada_type* aWideString)
    {
        // (a String (its address on the stack): a new string of the wide string type, of its bounds and characters)
        auto const source = local(type::I64);
        set(source);
        allocate_array(aWideString, source, true);
        auto const target = local(type::I64);
        set(target);
        auto const n = local(type::I64);
        array_length(source, 1u);
        set(n);
        auto const from = local(type::I64);
        data_address(source);
        set(from);
        auto const to = local(type::I64);
        data_address(target);
        set(to);
        auto* component = aWideString->view()->component;
        auto const size = iProgram.scalar_size(component);
        begin(control_kind::Block);
        auto const exit = iControls.size() - 1u;
        begin(control_kind::Loop);
        auto const head = iControls.size() - 1u;
        get(n);
        op(opcode::I64Eqz);
        branch(exit, true);
        get(to);
        get(from);
        load(vkind::I64, 1u, 0u, false);
        store(vkind::I64, size, 0u);
        get(from);
        i64(1);
        op(opcode::I64Add);
        set(from);
        get(to);
        i64(static_cast<std::int64_t>(size));
        op(opcode::I64Add);
        set(to);
        get(n);
        i64(1);
        op(opcode::I64Sub);
        set(n);
        branch(head);
        end();
        end();
        get(target);
    }

    void function_generator::value_attribute(ada_type* aType, node* aString)
    {
        auto* base = aType->view()->base_type()->view();
        expression(aString, aString->type);
        // ('Wide_Value, 'Wide_Wide_Value: a wide string's characters as a String (UTF-8 above 127))
        if (auto* st = aString->type != nullptr ? aString->type->view() : nullptr; st != nullptr && st->kind == type_kind::Array && st->component != nullptr &&
            iProgram.scalar_size(st->component) > 1u)
        {
            i64(static_cast<std::int64_t>(iProgram.scalar_size(st->component)));
            runtime("narrow_string", { type::I64, type::I64 }, type::I64);
        }
        if (base->kind == type_kind::Enumeration && !base->standard_character)
        {
            auto const s = temporary(type::I64);
            set(s);
            i64(static_cast<std::int64_t>(iProgram.enumeration_names(base)));
            i64(static_cast<std::int64_t>(base->literals.size()));
            get(s);
            runtime("value_enumeration", { type::I64, type::I64, type::I64 }, type::I64);
            release(s);
        }
        else if (base->kind == type_kind::Enumeration)
            runtime("value_character", { type::I64 }, type::I64);
        else if (base->kind == type_kind::Float)
        {
            runtime("value_float", { type::I64 }, type::F64);
            if (kind_of(base) == vkind::F32)
                op(opcode::F32ConvertF64);
        }
        else if (base->is_fixed())
        {
            runtime("value_float", { type::I64 }, type::F64);
            check();
            f64(base->small.to_double());
            op(opcode::F64Div);
            round_to_integer();
        }
        else
            runtime("value_integer", { type::I64 }, type::I64);
        check();
        range_check(aType->view()->base_type(), nullptr, aString);     // (S'Value: of S'Base (3.5(52)))
    }

    void function_generator::subprogram_access(entity* aSubprogram, node* aObject)
    {
        // (an access-to-subprogram value: a descriptor (its function index, its static link) on the stack (a library level
        // subprogram's: static); a protected subprogram's: its object's address too (allocated))
        auto* target = ultimate(aSubprogram);
        if (target->sub == nullptr)
            iProgram.error(iWhere, "'Access of a subprogram that has no body");
        iProgram.define_subprogram(target);
        if (iProgram.self_type(target) != nullptr)
        {
            auto const d = temporary(type::I64);
            i64(24);
            heap_allocate();
            set(d);
            get(d);
            function_index(target->sub->function_index);
            op(opcode::I64UConvertI32);
            store64(0u);
            get(d);
            static_link_for(target);
            store64(8u);
            get(d);
            if (aObject != nullptr)
                address(aObject);
            else if (iHasSelf)
                get(iSelf);
            else
                i64(0);
            store64(16u);
            get(d);
            release(d);
            return;
        }
        if (target->sub->level <= 1)
        {
            i64(static_cast<std::int64_t>(iProgram.subprogram_descriptor(target)));
            return;
        }
        // (in this frame if the subprogram is declared in this subprogram (its static link is this frame); else (it can outlive this 
        // frame, e.g. a function's result) allocated)
        auto const d = temporary(type::I64);
        i64(16);
        auto const s = temporary(type::I64);
        set(s);
        if (static_cast<int>(target->sub->level) == iLevel + 1)
            stack_allocate(s);
        else
        {
            get(s);
            heap_allocate();
        }
        tee(d);
        function_index(target->sub->function_index);
        op(opcode::I64UConvertI32);
        store64(0u);
        get(d);
        static_link_for(target);
        store64(8u);
        get(d);
        release(d);
        release(s);
    }

    std::uint64_t program::subprogram_descriptor(entity* aSubprogram)
    {
        auto existing = iSubprogramDescriptors.find(aSubprogram);
        if (existing != iSubprogramDescriptors.end())
            return existing->second;
        auto const address = static_data(16u, 8u);
        iFunctionSlots.emplace_back(address, aSubprogram->sub->function_index);
        iSubprogramDescriptors[aSubprogram] = address;
        return address;
    }

    // --- aggregates

    void function_generator::aggregate(node* aAggregate, ada_type* aType, std::optional<std::uint32_t> aTarget)
    {
        // (an i64: the address of the aggregate's value (built at the target, or in a new temporary))
        auto* t = (aAggregate->type != nullptr ? aAggregate->type : aType)->view();
        if (aType != nullptr && aType->view()->constrained && aType->view()->kind == type_kind::Array)
            t = aType->view();
        if (t->kind == type_kind::Array)
        {
            array_aggregate(aAggregate, t, aTarget);
            return;
        }
        auto* base = t->base_type()->view();
        iProgram.layout(base);
        auto const size = base->dynamic_size ? *base->size : iProgram.object_size(base);
        if (size == 0u)
            iProgram.error(aAggregate, "aggregates of dynamically sized records are not supported yet");
        std::uint32_t target;
        // (a dynamically sized record's discriminants (the aggregate's, else its subtype's constraint (an extension aggregate's: the
        // parent subtype's)))
        auto const* discriminantValues = iProgram.aggregate_of(aAggregate);
        std::size_t dci = 0u;
        auto const discriminant_value = [&](std::size_t aIndex, component_info const& aComponent) -> node*
        {
            node* value = nullptr;
            for (; dci < base->components.size() && value == nullptr; ++dci)
                if (&base->components[dci] == &aComponent)
                    value = discriminantValues != nullptr && dci < discriminantValues->size() ? (*discriminantValues)[dci] : nullptr;
            if (value == nullptr && aIndex < t->discriminant_constraint.size())
                value = t->discriminant_constraint[aIndex];
            if (value != nullptr && value->is(construct::Association))
                value = value->children.back();
            return value;
        };
        if (aTarget)
        {
            target = *aTarget;
            if (base->dynamic_size)
            {
                // (built in place (e.g. an array aggregate's element): its discriminants, then its structure (its components'
                // descriptors, its tail))
                store_discriminants(base, target, discriminant_value);
                bool const structureOnly = iStructureOnly;
                iStructureOnly = true;
                iDiscriminantsSet = true;
                default_initialize(base, target);
                iDiscriminantsSet = false;
                iStructureOnly = structureOnly;
            }
        }
        else if (base->dynamic_size)
        {
            // (its discriminants' values (the aggregate's), its size: theirs)
            auto const* values = iProgram.aggregate_of(aAggregate);
            auto const scratch = local(type::I64);
            i64(static_cast<std::int64_t>(size));
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(scratch);
            std::size_t ci = 0u;
            store_discriminants(base, scratch, [&](std::size_t aIndex, component_info const& aComponent) -> node*
                {
                    // (the aggregate's value, else its subtype's constraint (an extension aggregate's: the parent subtype's))
                    node* value = nullptr;
                    for (; ci < base->components.size() && value == nullptr; ++ci)
                        if (&base->components[ci] == &aComponent)
                            value = values != nullptr && ci < values->size() ? (*values)[ci] : nullptr;
                    if (value == nullptr && aIndex < t->discriminant_constraint.size())
                        value = t->discriminant_constraint[aIndex];
                    if (value != nullptr && value->is(construct::Association))
                        value = value->children.back();
                    return value;
                });
            bool const structureOnly = iStructureOnly;
            iStructureOnly = true;      // (its components: the aggregate's (their defaults evaluated only for <> and the absent ones))
            dynamic_record(base, scratch, 2);
            iStructureOnly = structureOnly;
            target = local(type::I64);
            set(target);
        }
        else
        {
            target = local(type::I64);
            i64(static_cast<std::int64_t>(size));
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(target);
        }
        if (base->tagged)
        {
            get(target);
            i64(static_cast<std::int64_t>(iProgram.tag_of(base)));
            store64(0u);
        }
        if (aAggregate->is(construct::AggregateDelta))
        {
            // (the base expression's value, then the components given)
            expression(aAggregate->child(0), aType);
            auto const source = temporary(type::I64);
            set(source);
            copy_value(base, target, source);
            release(source);
            for (std::size_t i = 1u; i < aAggregate->size(); ++i)
            {
                auto* association = aAggregate->child(i);
                for (auto* choice : association->child(0)->children)
                    for (auto const& c : base->components)
                        if (c.ent == choice->ent && c.offset)
                            store_component(target, c, association->children.back(), base);
            }
            get(target);
            return;
        }
        std::size_t first = 0u;
        std::optional<std::uint32_t> ancestorSource;     // (an ancestor part's value whose layout is not the extension's)
        bool ancestorDefaults = false;  // (a subtype mark's components: the extension's loop initializes them)
        // (the ancestor part's type)
        auto const ancestorBase = [&]() -> ada_type*
        {
            auto* at = aAggregate->child(0)->type->view();
            auto* ab = at->kind == type_kind::ClassWide ? at->specific->view() : at->base_type()->view();
            iProgram.layout(ab);
            return ab;
        };
        // (an ancestor type's components where they are in the extension's objects)
        auto const same_layout = [&](ada_type* aAncestor, ada_type* aExtension)
        {
            iProgram.layout(aExtension);
            for (auto const& pc : aAncestor->components)
            {
                if (!pc.offset)
                    continue;
                bool found = false;
                for (auto const& c : aExtension->components)
                    if (c.ent == pc.ent)
                        found = c.offset == pc.offset;
                if (!found)
                    return false;
            }
            return true;
        };
        if (aAggregate->is(construct::AggregateExtension))
        {
            // (the ancestor part's components: its value's, or its type's defaults)
            auto* ancestor = aAggregate->child(0);

            if (meaning_of(ancestor) == meaning::Type && !same_layout(ancestorBase(), base))
            {
                // (its components' defaults: initialized as the extension's (in its layout), with its discriminants)
                ancestorDefaults = true;
                if (base->tagged)
                {
                    get(target);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(base)));
                    store64(0u);
                }
            }
            else if (meaning_of(ancestor) == meaning::Type)
            {
                auto* at = ancestor->type->view();
                auto* ab = at->kind == type_kind::ClassWide ? at->specific->view() : at->base_type()->view();
                iProgram.layout(ab);
                // (its discriminants that the extension's derivation constrains by static values: those (its components depend on
                // them); the others are given by the aggregate)
                auto const constrainedDiscriminants = [&]()
                {
                    for (auto* d = base; d != nullptr && d->parent != nullptr;)
                    {
                        auto* ps = d->parent->view();
                        auto* pb = ps->base_type()->view();
                        if (pb == d)
                            break;
                        std::size_t k = 0u;
                        for (auto const& pc : pb->components)
                            if (pc.discriminant)
                            {
                                if (k < ps->discriminant_values.size() && ps->discriminant_values[k].known() && pc.type != nullptr)
                                    for (auto const& c : base->components)
                                        if (c.ent == pc.ent && c.offset && (c.discriminant || c.stored))
                                        {
                                            get(target);
                                            constant_value(ps->discriminant_values[k], pc.type);
                                            component_store(c);
                                        }
                                ++k;
                            }
                        d = pb;
                    }
                };
                constrainedDiscriminants();
                default_initialize(ab, target);
                constrainedDiscriminants();
                // (its controlled parts initialized (7.6(10)))
                controlled_initialized(ab, target, true, false, false);
                if (base->tagged)
                {
                    get(target);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(base)));
                    store64(0u);
                }
            }
            else if (auto* a = unqualified(ancestor); is_aggregate_node(a) && same_layout(ancestorBase(), base))
            {
                // (an aggregate: built in place (its type can be limited))
                auto* ab = ancestorBase();
                aggregate(a, ab, target);
                drop();
                if (base->tagged)
                {
                    get(target);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(base)));
                    store64(0u);
                }
            }
            else if (!same_layout(ancestorBase(), base) || iProgram.object_size(ancestorBase()) == 0u)
            {
                // (its value (an aggregate's: an object of its own), its components copied to the extension's (whose layout is
                // not the ancestor's: the extension has new discriminants; or that is dynamically sized) and adjusted)
                auto* ab = ancestorBase();
                if (auto* a = unqualified(ancestor); is_aggregate_node(a))
                    aggregate(a, ab);
                else
                {
                    moved_result(ancestor);
                    expression(ancestor, ancestor->type);
                }
                ancestorSource = local(type::I64);
                set(*ancestorSource);
                for (auto const& pc : ab->components)
                {
                    if (!pc.offset || pc.type == nullptr)
                        continue;
                    component_info const* c = nullptr;
                    for (auto const& bc : base->components)
                        if (bc.ent == pc.ent && bc.offset)
                            c = &bc;
                    if (c == nullptr)
                        continue;   // (a discriminant that the extension constrains: checked below)
                    auto* ct = pc.type->view();
                    auto const to = local(type::I64);
                    component_address(base, *c, target);
                    set(to);
                    auto const from = local(type::I64);
                    component_address(ab, pc, *ancestorSource);
                    set(from);
                    if (ct->is_composite())
                    {
                        if (ct->kind == type_kind::Array && c->depends && !c->dynamic)
                        {
                            // (its bounds: the ancestor's (they depend on its discriminants, that the extension's determine)), its
                            // data in place
                            auto const dimensions = ct->base_type()->view()->indexes.size();
                            get(to);
                            i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
                            store64(0u);
                            get(to);
                            i64(8);
                            op(opcode::I64Add);
                            get(from);
                            i64(8);
                            op(opcode::I64Add);
                            i64(static_cast<std::int64_t>(16u * dimensions));
                            runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                        }
                        else if (ct->kind == type_kind::Array && !c->depends)
                            init_descriptor(c->type, to);
                        copy_value(ct, to, from);
                        if (!is_temporary_value(ancestor))
                            controlled_initialized(ct, to, false, true, false);
                    }
                    else
                    {
                        get(to);
                        get(from);
                        load(kind_of(ct), ct->is_access() ? 8u : iProgram.scalar_size(ct), 0u, !is_unsigned(ct));
                        store(kind_of(ct), ct->is_access() ? 8u : iProgram.scalar_size(ct), 0u);
                    }
                }
                if (base->tagged)
                {
                    get(target);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(base)));
                    store64(0u);
                }
            }
            else
            {
                // (its value: moved (a function's result), else copied and its controlled parts adjusted)
                moved_result(ancestor);
                expression(ancestor, ancestor->type);
                auto const source = temporary(type::I64);
                set(source);
                auto* ab = ancestorBase();
                auto const ancestorSize = iProgram.object_size(ab);
                get(target);
                i64(8);
                op(opcode::I64Add);
                get(source);
                i64(8);
                op(opcode::I64Add);
                i64(static_cast<std::int64_t>(ancestorSize - 8u));
                runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                release(source);
                if (!is_temporary_value(ancestor) && iProgram.needs_finalization(ab))
                    controlled_parts(ab, target, false, [&](ada_type* aPartType, std::uint32_t aPart)
                        {
                            controlled_call(aPartType, aPart, "adjust");
                        });
            }
            first = 1u;
        }
        (void)first;
        auto const* values = iProgram.aggregate_of(aAggregate);
        // (its components' default expressions name its discriminants: this aggregate's (stored first))
        iInstances.emplace_back(base, target);
        for (std::size_t c = 0u; c < base->components.size(); ++c)
        {
            auto const& component = base->components[c];
            if (!component.offset)
                continue;
            node* value = values != nullptr && c < values->size() ? (*values)[c] : nullptr;
            if (values == nullptr)
                continue;
            if (value != nullptr && value->is(construct::Box))
                value = nullptr;    // (<>: its default)
            if (value == nullptr)
            {
                // (not given: its default (an extension aggregate's ancestor's components are the ancestor part's))
                bool inAncestor = false;
                if (aAggregate->is(construct::AggregateExtension))
                {
                    auto* ancestor = aAggregate->child(0);
                    auto* at = ancestor->type != nullptr ? ancestor->type->view() : nullptr;
                    if (at != nullptr)
                        inAncestor = iProgram.in_ancestor_part(component, c, at);
                }
                if (inAncestor && !ancestorDefaults)
                    continue;
                // (a component of a variant that the discriminants don't select: none)
                if (component.variant >= 0)
                {
                    variant_selected(base, component, target);
                    begin_if();
                }
                if (component.default_expression != nullptr)
                    store_component(target, component, component.default_expression, base);
                else if (component.type != nullptr && component.type->view()->is_composite())
                {
                    auto const a = temporary(type::I64);
                    get(target);
                    i64(static_cast<std::int64_t>(*component.offset));
                    op(opcode::I64Add);
                    set(a);
                    if (component.type->view()->kind == type_kind::Array)
                        init_descriptor(component.type, a);
                    default_initialize(component.type, a);
                    controlled_initialized(component.type, a, true, false, false);
                    release(a);
                }
                else if (component.type != nullptr && component.type->view()->is_access())
                {
                    get(target);
                    i64(0);
                    store64(static_cast<std::uint32_t>(*component.offset));
                }
                else if (auto* defaultValue = component.type != nullptr ? iProgram.default_value_of(component.type, false) : nullptr)
                    store_component(target, component, defaultValue, base);     // (a scalar's Default_Value)
                if (component.variant >= 0)
                    end();
                continue;
            }
            store_component(target, component, value, base);
        }
        if (aAggregate->is(construct::AggregateExtension))
        {
            // (the ancestor part's discriminants that the extension (or an ancestor's extension) constrains: the constraint's
            // values (a new discriminant's: the aggregate's) (4.3.2(8/3)))
            auto const located = [&](entity* aDiscriminant) -> component_info const*
            {
                for (auto const& c : base->components)
                    if (c.ent == aDiscriminant && c.offset && c.discriminant)
                        return &c;
                return nullptr;
            };
            // (a new discriminant named by a parent's constraint (its entity is the one the constraint was resolved with))
            auto const named = [&](entity* aDiscriminant) -> component_info const*
            {
                for (auto const& c : base->components)
                    if (c.ent != nullptr && c.ent->name == aDiscriminant->name && c.offset && c.discriminant)
                        return &c;
                return nullptr;
            };
            // (a discriminant's value as the constraints give it (false: none constrains it))
            std::function<bool(entity*, bool)> push = [&](entity* aDiscriminant, bool aStored) -> bool
            {
                if (aStored)
                    if (auto const* c = located(aDiscriminant) != nullptr ? located(aDiscriminant) : named(aDiscriminant))
                    {
                        get(target);
                        component_load(*c);
                        return true;
                    }
                for (auto* d = base; d != nullptr && d->parent != nullptr;)
                {
                    auto* ps = d->parent->view();
                    auto* pb = ps->base_type()->view();
                    if (pb == d)
                        break;
                    std::size_t k = 0u;
                    for (auto const& pc : pb->components)
                    {
                        if (!pc.discriminant)
                            continue;
                        if ((pc.ent == aDiscriminant || (pc.ent != nullptr && pc.ent->name == aDiscriminant->name)) && k < ps->discriminant_constraint.size())
                        {
                            auto* value = ps->discriminant_constraint[k];
                            if (value != nullptr && value->is(construct::Association))
                                value = value->children.back();
                            if (value == nullptr)
                                return false;
                            if (value->is(construct::Identifier) && value->ent != nullptr && value->ent->kind == entity_kind::Discriminant)
                                return push(value->ent, true);
                            expression(value, pc.type);
                            return true;
                        }
                        ++k;
                    }
                    d = pb;
                }
                return false;
            };
            auto* ab = ancestorBase();
            auto* ancestorNode = aAggregate->child(0);
            auto* ancestorSubtype = meaning_of(ancestorNode) == meaning::Type ? ancestorNode->type->view() : nullptr;
            std::size_t k = 0u;
            for (auto const& pc : ab->components)
            {
                if (!pc.discriminant)
                    continue;
                // (a subtype mark's: its constraint's value (none: no check))
                node* constraint = nullptr;
                if (ancestorSubtype != nullptr)
                {
                    constraint = k < ancestorSubtype->discriminant_constraint.size() ? ancestorSubtype->discriminant_constraint[k] : nullptr;
                    if (constraint != nullptr && constraint->is(construct::Association))
                        constraint = constraint->children.back();
                }
                ++k;
                if (!pc.offset || pc.type == nullptr || kind_of(pc.type) != vkind::I64)
                    continue;
                auto const* stored = located(pc.ent);
                if (ancestorSubtype != nullptr ? constraint == nullptr : (stored == nullptr && !ancestorSource))
                    continue;
                if (!push(pc.ent, false))
                    continue;
                if (constraint != nullptr)
                    expression(constraint, pc.type);
                else if (stored != nullptr)
                {
                    get(target);
                    component_load(*stored);
                }
                else
                {
                    get(*ancestorSource);
                    component_load(pc);
                }
                op(opcode::I64Ne);
                begin_if();
                raise_check(0, aAggregate, "discriminant check failed");
                end();
            }
        }
        iInstances.pop_back();
        get(target);
    }

    node* function_generator::unqualified(node* aExpression)
    {
        // (an expression without its qualifications and parentheses)
        while (aExpression->is(construct::Qualified) || aExpression->is(construct::Parenthesized))
            aExpression = aExpression->children.back();
        return aExpression;
    }

    void function_generator::store_component(std::uint32_t aRecord, component_info const& aComponent, node* aValue, ada_type* aRecordType)
    {
        auto* ct = aComponent.type->view();
        if (ct->is_composite())
        {
            auto const a = local(type::I64);
            component_address(aRecordType != nullptr ? aRecordType->view()->base_type()->view() : nullptr, aComponent, aRecord);
            set(a);
            if (ct->kind == type_kind::Array && !aComponent.depends)
                init_descriptor(ct, a);
            else if (ct->kind == type_kind::Array && !aComponent.dynamic && aRecordType != nullptr)
            {
                component_bounds(aRecordType->view()->base_type()->view(), aComponent, aRecord, a);   // (its bounds: the discriminants' (stored))
                index_compatibility_check(ct, a);   // (of its index subtypes (3.6.1(7)))
            }
            if (is_aggregate_node(aValue))
            {
                aggregate(aValue, aComponent.type, a);
                drop();
                discriminant_constraint_check(aComponent.type, a, aValue);
            }
            else
            {
                moved_result(aValue);
                expression(aValue, aComponent.type);
                auto const v = temporary(type::I64);
                set(v);
                if (ct->kind == type_kind::Array)
                    length_check(a, v, ct->base_type()->view()->indexes.size(), aValue);
                discriminant_constraint_check(aComponent.type, v, aValue);
                if (ct->kind == type_kind::Record && ct->base_type()->view()->tagged)
                {
                    get(a);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(ct)));
                    store64(0u);
                }
                copy_value(ct, a, v);
                release(v);
                if (!is_temporary_value(aValue))
                    controlled_initialized(ct, a, false, true, false);
            }
            return;
        }
        get(aRecord);
        expression(aValue, aComponent.type);
        range_check(aComponent.type, aValue->type, aValue);
        component_store(aComponent);
    }

    void function_generator::array_aggregate(node* aAggregate, ada_type* aType, std::optional<std::uint32_t> aTarget)
    {
        // (a one or more dimensional array aggregate: its bounds (the context's constrained subtype, else from the aggregate:
        // positional: the index subtype's first and the number of components; named: the choices' least and greatest), then
        // its components)
        auto* t = aType->view();
        auto* base = t->base_type()->view();
        auto const dimensions = base->indexes.size();
        std::vector<std::uint32_t> lows(dimensions), highs(dimensions);
        // (the next dimension's subaggregate: the first)
        auto const nextLevel = [&](node* aLevel) -> node*
        {
            if (aLevel != nullptr && !aLevel->is(construct::Operator))
                for (auto* item : aLevel->children)
                {
                    auto* v = item->is(construct::Association) ? item->children.back() : item;
                    if (is_aggregate_node(v) || v->is(construct::Operator))
                        return v;
                }
            return nullptr;
        };
        // (two ranges of the same length (else Constraint_Error))
        auto const lengthCheck = [&](std::uint32_t aLow1, std::uint32_t aHigh1, std::uint32_t aLow2, std::uint32_t aHigh2)
        {
            auto const a = temporary(type::I64);
            auto const b = temporary(type::I64);
            get(aHigh1);
            get(aLow1);
            op(opcode::I64Sub);
            set(a);
            get(aHigh2);
            get(aLow2);
            op(opcode::I64Sub);
            set(b);
            get(a);
            get(b);
            op(opcode::I64Ne);
            get(a);
            i64(0);
            op(opcode::I64GeS);
            get(b);
            i64(0);
            op(opcode::I64GeS);
            op(opcode::I32Ior);
            op(opcode::I32And);
            begin_if();
            raise_check(0, aAggregate, "length check failed");
            end();
            release(a);
            release(b);
        };
        // (an aggregate's own (non-null) bounds: of the index subtype (4.3.3(28-29)))
        auto const indexCheck = [&](std::size_t aDimension, std::uint32_t aLow, std::uint32_t aHigh)
        {
            auto* index = base->indexes[aDimension];
            get(aHigh);
            get(aLow);
            op(opcode::I64GeS);
            get(aLow);
            scalar_low(index);
            op(opcode::I64LtS);
            get(aHigh);
            scalar_high(index);
            op(opcode::I64GtS);
            op(opcode::I32Ior);
            op(opcode::I32And);
            begin_if();
            raise_check(0, aAggregate, "index check failed");
            end();
        };
        std::uint32_t target;
        if (aTarget)
        {
            // (the target's bounds; the aggregate's own (if they aren't the target's): of the same lengths (it slides))
            target = *aTarget;
            node* level = aAggregate;
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                lows[d] = local(type::I64);
                highs[d] = local(type::I64);
                bounds(target, d, true);
                set(lows[d]);
                bounds(target, d, false);
                set(highs[d]);
                if (level != nullptr && !aggregate_has_others(level))
                {
                    auto const low = local(type::I64);
                    auto const high = local(type::I64);
                    aggregate_bounds(level, t, d, low, high);
                    iAggregateBounded.insert(level);
                    indexCheck(d, low, high);
                    lengthCheck(lows[d], highs[d], low, high);
                    lows[d] = low;
                    highs[d] = high;
                }
                level = nextLevel(level);
            }
        }
        else
        {
            node* level = aAggregate;
            bool own = false;
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                lows[d] = local(type::I64);
                highs[d] = local(type::I64);
                if (level != nullptr)
                    iAggregateBounded.insert(level);
                if (aggregate_bounds(level, t, d, lows[d], highs[d]))
                {
                    indexCheck(d, lows[d], highs[d]);
                    own = true;
                }
                level = nextLevel(level);
            }
            // (a descriptor of the bounds, then the object (on the secondary stack))
            auto const descriptor = local(type::I64);
            i64(static_cast<std::int64_t>(DescriptorHeader(dimensions)));
            runtime("ss_allocate", { type::I64 }, type::I64);
            set(descriptor);
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                get(descriptor);
                get(lows[d]);
                store64(static_cast<std::uint32_t>(8u + 16u * d));
                get(descriptor);
                get(highs[d]);
                store64(static_cast<std::uint32_t>(16u + 16u * d));
            }
            allocate_array(t, descriptor, true);
            target = local(type::I64);
            set(target);
            if (own && t->constrained)
            {
                // (its own bounds: converted to the constrained subtype (sliding: of the same lengths))
                array_aggregate_components(aAggregate, t, target, 0u, lows, highs, iProgram.component_size(t));
                subtype_descriptor(t);
                auto const slid = local(type::I64);
                set(slid);
                length_check(slid, target, dimensions, aAggregate);
                get(slid);
                data_address(target);
                get(slid);
                op(opcode::I64Sub);
                store64(0u);
                get(slid);
                return;
            }
        }
        // (the components: written at their positions)
        auto const element = iProgram.component_size(t);
        array_aggregate_components(aAggregate, t, target, 0u, lows, highs, element);
        get(target);
    }

    bool function_generator::aggregate_bounds(node* aLevel, ada_type* aType, std::size_t aDimension, std::uint32_t aLow, std::uint32_t aHigh)
    {
        // (the bounds of an aggregate's dimension (4.3.3(24-27)): with an others choice (or none to determine them), the applicable 
        // index constraint's; positional: from the applicable index constraint's (else the index subtype's) lower bound; named: the 
        // least and greatest choices) (true: the aggregate's own (not the constraint's))
        auto* t = aType->view();
        auto* index = t->constrained ? t->indexes[aDimension] : t->base_type()->view()->indexes[aDimension];
        if (t->constrained && (aLevel == nullptr || aggregate_has_others(aLevel)))
        {
            scalar_low(index);
            set(aLow);
            scalar_high(index);
            set(aHigh);
            return false;
        }
        if (aLevel == nullptr)
        {
            scalar_low(index);
            set(aLow);
            get(aLow);
            i64(1);
            op(opcode::I64Sub);
            set(aHigh);
            return true;
        }
        if (aLevel->is(construct::Operator))
        {
            // (a string literal subaggregate)
            scalar_low(index);
            set(aLow);
            get(aLow);
            i64(static_cast<std::int64_t>(literal_string(aLevel).size()) - 1);
            op(opcode::I64Add);
            set(aHigh);
            return true;
        }
        // (named: the least and greatest choices; positional: first and count)
        bool named = false;
        std::size_t positional = 0u;
        for (auto* item : aLevel->children)
        {
            if (item->is(construct::Association))
            {
                bool others = false;
                for (auto* choice : item->child(0)->children)
                    others = others || choice->is(construct::Others);
                if (!others)
                    named = true;
            }
            else if (item->is(construct::Iterated))
                named = true;
            else
                ++positional;
        }
        if (!named)
        {
            scalar_low(index);
            set(aLow);
            get(aLow);
            i64(static_cast<std::int64_t>(positional) - 1);
            op(opcode::I64Add);
            set(aHigh);
            return true;
        }
        i64(std::numeric_limits<std::int64_t>::max());
        set(aLow);
        i64(std::numeric_limits<std::int64_t>::min());
        set(aHigh);
        auto const update = [&](std::uint32_t aValue, bool aLowSide)
        {
            get(aValue);
            get(aLowSide ? aLow : aHigh);
            get(aValue);
            get(aLowSide ? aLow : aHigh);
            op(aLowSide ? opcode::I64LtS : opcode::I64GtS);
            op(opcode::Select);
            set(aLowSide ? aLow : aHigh);
        };
        for (auto* item : aLevel->children)
        {
            node* choices = item->is(construct::Association) ? item->child(0) : nullptr;
            if (item->is(construct::Iterated))
            {
                auto* spec = item->child(0);
                if (auto* p = spec->first(construct::Defining); p != nullptr && p->ent != nullptr)
                {
                    auto const l = temporary(type::I64);
                    auto const h = temporary(type::I64);
                    discrete_bounds(spec, p->ent->type, l, h);
                    update(l, true);
                    update(h, false);
                    release(l);
                    release(h);
                }
                continue;
            }
            if (choices == nullptr)
                continue;
            for (auto* choice : choices->children)
            {
                if (choice->is(construct::Others))
                    continue;
                // (evaluated once: kept for the components)
                auto const l = local(type::I64);
                auto const h = local(type::I64);
                choice_bounds(choice, index, l, h);
                iChoiceBounds[choice] = { l, h };
                update(l, true);
                update(h, false);
            }
        }
        return true;
    }

    bool function_generator::aggregate_has_others(node* aLevel)
    {
        if (aLevel == nullptr || !is_aggregate_node(aLevel))
            return false;
        for (auto* item : aLevel->children)
            if (item->is(construct::Association))
                for (auto* choice : item->child(0)->children)
                    if (choice->is(construct::Others))
                        return true;
        return false;
    }

    void function_generator::choice_bounds(node* aChoice, ada_type* aIndex, std::uint32_t aLow, std::uint32_t aHigh)
    {
        if (aChoice->is(construct::Range))
        {
            // (its bounds, if they have been elaborated already (as a subtype's dynamic bounds): not evaluated again)
            // (not if they name a discriminant: the record's being initialized)
            for (auto const& b : iProgram.dynamic_bounds())
                if (b.low == aChoice->child(0) && b.high == aChoice->child(1) && b.lowObject->where != storage::None && b.highObject->where != storage::None &&
                    !iProgram.refers_to_discriminant(aChoice))
                {
                    load_object(b.lowObject);
                    set(aLow);
                    load_object(b.highObject);
                    set(aHigh);
                    return;
                }
            expression(aChoice->child(0), aIndex->base_type());
            set(aLow);
            expression(aChoice->child(1), aIndex->base_type());
            set(aHigh);
            return;
        }
        if (aChoice->is(construct::Attribute) && name_text(aChoice->child(1)->source) == "range")
        {
            range_attribute_bounds(aChoice, aLow, aHigh);
            return;
        }
        if (aChoice->type != nullptr && (aChoice->is(construct::SubtypeIndication) || meaning_of(aChoice) == meaning::Type))
        {
            scalar_low(aChoice->type);
            set(aLow);
            scalar_high(aChoice->type);
            set(aHigh);
            return;
        }
        expression(aChoice, aIndex->base_type());
        tee(aLow);
        set(aHigh);
    }

    void function_generator::array_aggregate_components(node* aLevel, ada_type* aType, std::uint32_t aTarget, std::size_t aDimension,
        std::vector<std::uint32_t> const& aLows, std::vector<std::uint32_t> const& aHighs, std::uint64_t aElement)
    {
        // (the components of dimension aDimension (for the index prefix in iAggregateOffset): positional ones in order, named
        // ones at their choices, others at the rest)
        auto* t = aType->view();
        auto const dimensions = aLows.size();
        bool const last = aDimension + 1u == dimensions;
        auto* index = t->base_type()->view()->indexes[aDimension];
        // (the stride of this dimension: the number of elements of the dimensions after it)
        auto const stride = local(type::I64);
        i64(1);
        for (std::size_t d = aDimension + 1u; d < dimensions; ++d)
        {
            get(aHighs[d]);
            get(aLows[d]);
            op(opcode::I64Sub);
            i64(1);
            op(opcode::I64Add);
            op(opcode::I64Mul);
        }
        set(stride);
        if (iAggregateOffsets.size() <= aDimension)
            iAggregateOffsets.resize(aDimension + 1u);
        auto const base = aDimension == 0u ? std::optional<std::uint32_t>{} : std::optional<std::uint32_t>{ iAggregateOffsets[aDimension - 1u] };
        auto const element_at = [&](std::uint32_t aIndexValue, node* aValue)
        {
            // (the element (or subarray) at an index: its offset = base + (index - low) * stride)
            auto const offset = local(type::I64);
            get(aIndexValue);
            get(aLows[aDimension]);
            op(opcode::I64Sub);
            get(stride);
            op(opcode::I64Mul);
            if (base)
            {
                get(*base);
                op(opcode::I64Add);
            }
            set(offset);
            if (!last)
            {
                iAggregateOffsets[aDimension] = offset;
                if (is_aggregate_node(aValue) && !iAggregateBounded.contains(aValue) && !aggregate_has_others(aValue))
                {
                    // (each subaggregate: of the same bounds as the first's (4.3.3(30)))
                    auto const low = local(type::I64);
                    auto const high = local(type::I64);
                    aggregate_bounds(aValue, aType, aDimension + 1u, low, high);
                    get(low);
                    get(aLows[aDimension + 1u]);
                    op(opcode::I64Ne);
                    get(high);
                    get(aHighs[aDimension + 1u]);
                    op(opcode::I64Ne);
                    op(opcode::I32Ior);
                    begin_if();
                    raise_check(0, aValue, "index check failed");
                    end();
                }
                if (aValue->is(construct::Operator))
                    string_subaggregate(aValue, aTarget, offset, aElement, t);
                else
                    array_aggregate_components(aValue, aType, aTarget, aDimension + 1u, aLows, aHighs, aElement);
                return;
            }
            // (the element's address: data + offset * size)
            auto const a = temporary(type::I64);
            data_address(aTarget);
            get(offset);
            if (aElement != 0u)
                i64(static_cast<std::int64_t>(aElement));
            else
                element_size(aType);
            op(opcode::I64Mul);
            op(opcode::I64Add);
            set(a);
            auto* ct = t->component;
            if (aValue->is(construct::Box))
            {
                if (ct->view()->is_composite())
                {
                    if (ct->view()->kind == type_kind::Array)
                        init_descriptor(ct, a);
                    default_initialize(ct, a);
                    controlled_initialized(ct, a, true, false, false);
                    // (default-initialized: its type's invariants (7.3.2(10/4)))
                    if (!invariants_of(ct).empty() && !ct->unknown_discriminants && !ct->base_type()->unknown_discriminants)
                        invariant_check(ct, a, aValue);
                }
                else if (ct->view()->is_access())
                {
                    get(a);
                    i64(0);
                    store64(0u);
                }
                else if (auto* defaultValue = iProgram.default_value_of(t, true) != nullptr ? iProgram.default_value_of(t, true) : iProgram.default_value_of(ct, false))
                {
                    // (the array type's Default_Component_Value, else the scalar's Default_Value)
                    get(a);
                    expression(defaultValue, ct);
                    store(kind_of(ct), aElement);
                }
            }
            else if (ct->view()->is_composite())
            {
                if (ct->view()->kind == type_kind::Array)
                    init_descriptor(ct, a);
                if (is_aggregate_node(aValue))
                {
                    aggregate(aValue, ct, a);
                    drop();
                    discriminant_constraint_check(ct, a, aValue);
                }
                else
                {
                    moved_result(aValue);
                    expression(aValue, ct);
                    auto const v = temporary(type::I64);
                    set(v);
                    if (ct->view()->kind == type_kind::Array)
                        length_check(a, v, ct->view()->base_type()->view()->indexes.size(), aValue);
                    discriminant_constraint_check(ct, v, aValue);
                    if (ct->view()->kind == type_kind::Record && ct->view()->base_type()->view()->tagged)
                    {
                        get(a);
                        i64(static_cast<std::int64_t>(iProgram.tag_of(ct)));
                        store64(0u);
                    }
                    // (a dynamically sized record element: not initialized yet (its discriminants: the value's, checked))
                    if (ct->view()->kind != type_kind::Array && is_runtime_sized(ct))
                        copy_whole(ct, a, v);
                    else
                        copy_value(ct, a, v);
                    release(v);
                    if (!is_temporary_value(aValue))
                        controlled_initialized(ct, a, false, true, false);
                }
            }
            else
            {
                get(a);
                expression(aValue, ct);
                range_check(ct, aValue->type, aValue);
                store(kind_of(ct), aElement);
            }
            release(a);
        };
        if (aLevel != nullptr && aLevel->is(construct::Operator))
        {
            string_subaggregate(aLevel, aTarget, base.value_or(0u), aElement, t);
            return;
        }
        // (positional components, then named ones and others)
        auto const position = local(type::I64);
        get(aLows[aDimension]);
        set(position);
        auto const covered = local(type::I64);  // (positional: the next index)
        node* othersValue = nullptr;
        std::vector<node*> named;
        for (auto* item : aLevel->children)
        {
            if (item->is(construct::Association))
            {
                bool others = false;
                for (auto* choice : item->child(0)->children)
                    others = others || choice->is(construct::Others);
                if (others)
                    othersValue = item->children.back();
                else
                    named.push_back(item);
                continue;
            }
            if (item->is(construct::Iterated))
            {
                named.push_back(item);
                continue;
            }
            // (beyond the bounds (the applicable index constraint's): Constraint_Error)
            get(position);
            get(aHighs[aDimension]);
            op(opcode::I64GtS);
            begin_if();
            raise_check(0, item, "index check failed");
            end();
            element_at(position, item);
            get(position);
            i64(1);
            op(opcode::I64Add);
            set(position);
        }
        get(position);
        set(covered);
        // (the named components' ranges (evaluated once))
        struct named_range
        {
            node* item;
            node* choice;
            std::uint32_t low;
            std::uint32_t high;
        };
        std::vector<named_range> ranges;
        for (auto* item : named)
        {
            if (item->is(construct::Iterated))
            {
                auto* spec = item->child(0);
                auto* parameter = spec->first(construct::Defining) != nullptr ? spec->first(construct::Defining)->ent : nullptr;
                if (parameter == nullptr)
                    continue;
                auto const l = local(type::I64);
                auto const h = local(type::I64);
                discrete_bounds(spec, parameter->type, l, h);
                ranges.push_back(named_range{ item, nullptr, l, h });
                continue;
            }
            for (auto* choice : item->child(0)->children)
            {
                auto const l = local(type::I64);
                auto const h = local(type::I64);
                if (auto evaluated = iChoiceBounds.find(choice); evaluated != iChoiceBounds.end())
                {
                    // (evaluated already (for the aggregate's bounds))
                    get(evaluated->second.first);
                    set(l);
                    get(evaluated->second.second);
                    set(h);
                    iChoiceBounds.erase(evaluated);
                }
                else
                    choice_bounds(choice, index, l, h);
                ranges.push_back(named_range{ item, choice, l, h });
            }
        }
        // (others: every index not given by a positional or named component (each evaluated once for each of its components 
        // (4.3.3(31))))
        if (othersValue != nullptr)
        {
            auto const i = local(type::I64);
            get(covered);
            set(i);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            get(i);
            get(aHighs[aDimension]);
            op(opcode::I64GtS);
            branch(exit, true);
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            i32(0);
            for (auto const& r : ranges)
            {
                get(i);
                get(r.low);
                op(opcode::I64GeS);
                get(i);
                get(r.high);
                op(opcode::I64LeS);
                op(opcode::I32And);
                op(opcode::I32Ior);
            }
            op(opcode::I32Eqz);
            begin_if();
            element_at(i, othersValue);
            end();
            get(i);
            get(aHighs[aDimension]);
            op(opcode::I64Eq);
            branch(exit, true);
            get(i);
            i64(1);
            op(opcode::I64Add);
            set(i);
            branch(head);
            end();
            end();
        }
        for (auto const& r : ranges)
        {
            auto* item = r.item;
            if (item->is(construct::Iterated))
            {
                auto* spec = item->child(0);
                auto* parameter = spec->first(construct::Defining)->ent;
                parameter->where = storage::None;
                assign_storage(parameter);
                begin(control_kind::Block);
                auto const exit = iControls.size() - 1u;
                get(r.low);
                store_object(parameter);
                get(r.low);
                get(r.high);
                op(opcode::I64GtS);
                branch(exit, true);
                begin(control_kind::Loop);
                auto const head = iControls.size() - 1u;
                auto const pv = local(type::I64);
                load_object(parameter);
                set(pv);
                element_at(pv, item->children.back());
                get(pv);
                get(r.high);
                op(opcode::I64Eq);
                branch(exit, true);
                load_object(parameter);
                i64(1);
                op(opcode::I64Add);
                store_object(parameter);
                branch(head);
                end();
                end();
                continue;
            }
            auto* value = item->children.back();
            auto* choice = r.choice;
            auto const l = r.low;
            auto const h = r.high;
            // (each index of the choice's range (checked against the aggregate's bounds))
            get(l);
            get(h);
            op(opcode::I64LeS);
            begin_if();
            get(l);
            get(aLows[aDimension]);
            op(opcode::I64LtS);
            get(h);
            get(aHighs[aDimension]);
            op(opcode::I64GtS);
            op(opcode::I32Ior);
            begin_if();
            raise_check(0, choice, "index check failed");
            end();
            end();
            // (the last index ends the loop before it is incremented: a range up to the base type's last value)
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            get(l);
            get(h);
            op(opcode::I64GtS);
            branch(exit, true);
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            element_at(l, value);
            get(l);
            get(h);
            op(opcode::I64Eq);
            branch(exit, true);
            get(l);
            i64(1);
            op(opcode::I64Add);
            set(l);
            branch(head);
            end();
            end();
        }
    }

    void function_generator::string_subaggregate(node* aLiteral, std::uint32_t aTarget, std::uint32_t aOffset, std::uint64_t aElement, ada_type* aType)
    {
        // (a string literal as the last dimension's subaggregate: its characters at the offset)
        auto const text = literal_string(aLiteral);
        auto* t = aType != nullptr ? aType->view() : nullptr;
        auto* component = t != nullptr && t->component != nullptr ? t->component->view()->base_type()->view() : nullptr;
        for (std::size_t i = 0u; i < text.size(); ++i)
        {
            data_address(aTarget);
            get(aOffset);
            i64(static_cast<std::int64_t>(i));
            op(opcode::I64Add);
            i64(static_cast<std::int64_t>(aElement));
            op(opcode::I64Mul);
            op(opcode::I64Add);
            std::uint64_t code = text[i];
            // (a user-defined character type: the literal's position)
            if (component != nullptr && component->character_type && !component->standard_character)
                for (std::size_t l = 0u; l < component->literals.size(); ++l)
                    if (component->literals[l]->name.size() == 3u && static_cast<unsigned char>(component->literals[l]->name[1]) == code)
                        code = l;
            i64(static_cast<std::int64_t>(code));
            store(vkind::I64, aElement);
        }
    }

    // --- allocators

    void function_generator::allocator_object(node* aAllocator, ada_type* aType)
    {
        // (a new object on the heap: initialized by the qualified expression, or by default; its address)
        evaluate_dynamic_bounds(aAllocator);
        compatibility_checks(aAllocator, [&](node const* aNode)
            {
                for (auto const* n = aNode; n != nullptr; n = n->parent)
                    if (n == aAllocator)
                        return true;
                return false;
            });
        auto* accessType = aType != nullptr ? aType : aAllocator->type;
        auto* designated = iProgram.designated_of(accessType);
        auto* indication = aAllocator->children.back();
        node* qualified = nullptr;
        if (indication->is(construct::Qualified))
            qualified = indication;
        else if (indication->is(construct::SubtypeIndication) && indication->size() == 1u && indication->child(0)->is(construct::Qualified))
            qualified = indication->child(0);
        ada_type* objectType = qualified != nullptr ? qualified->type : (indication->type != nullptr ? indication->type : designated);
        auto* v = objectType->view();
        auto const result = local(type::I64);
        if (qualified != nullptr)
            moved_result(qualified);
        if (is_runtime_sized(v))
        {
            // (a class-wide or dynamically sized record: a copy of the value, or default initialized)
            std::optional<std::uint32_t> source;
            if (qualified != nullptr)
            {
                if (is_aggregate_node(qualified->child(1)))
                    aggregate(qualified->child(1), qualified->type);
                else
                    expression(qualified, qualified->type);
                source = local(type::I64);
                set(*source);
                // (a class-wide value: its tag's type not deeper than the access type (4.8(10.1/3)))
                if (auto* at = accessType->view()->base_type()->view(); v->kind == type_kind::ClassWide && !at->anonymous_access && at->declared_in != nullptr)
                {
                    get(*source);
                    load64(0u);
                    load64(static_cast<std::uint32_t>(program::TagLevel));
                    i64(iProgram.type_level(at));
                    op(opcode::I64GtS);
                    begin_if();
                    raise_check(1, aAllocator, "accessibility check failed");
                    end();
                }
            }
            dynamic_object(v, source, 1);
            set(result);
            controlled_initialized(v, result, !source, source && !is_temporary_value(qualified->child(1)), false);
            get(result);
            return;
        }
        if (v->kind == type_kind::Array)
        {
            std::uint32_t source = 0u;
            if (qualified != nullptr)
            {
                expression(qualified, qualified->type);
                source = local(type::I64);
                set(source);
                if (v->constrained)
                {
                    subtype_descriptor(v);
                    auto const descriptor = local(type::I64);
                    set(descriptor);
                    allocate_array(v, descriptor, false, true);
                }
                else
                    allocate_array(v, source, false, true);
                set(result);
                copy_value(v, result, source);
                controlled_initialized(v, result, false, !is_temporary_value(qualified->child(1)), false);
            }
            else
            {
                subtype_descriptor(v);
                auto const descriptor = local(type::I64);
                set(descriptor);
                allocate_array(v, descriptor, false, true);
                set(result);
                default_initialize(v, result);
                controlled_initialized(v, result, true, false, false);
            }
            get(result);
            return;
        }
        // (from a storage pool: the object's size (its Deallocate's, 13.11(18)); the heap's at least 8)
        auto const size = std::max<std::uint64_t>(iProgram.object_size(v->kind == type_kind::ClassWide ? v->specific : v), iPool ? 1u : 8u);
        if (v->is_composite())
        {
            if (qualified != nullptr && (v->kind == type_kind::ClassWide || v->tagged))
            {
                // (a class-wide or tagged value: its own size)
                expression(qualified, qualified->type);
                auto const source = local(type::I64);
                set(source);
                i64(static_cast<std::int64_t>(size));
                heap_allocate();
                set(result);
                get(result);
                get(source);
                i64(static_cast<std::int64_t>(size));
                runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                if (v->kind != type_kind::ClassWide)
                    controlled_initialized(v, result, false, !is_temporary_value(qualified->child(1)), false);
                get(result);
                return;
            }
            i64(static_cast<std::int64_t>(size));
            heap_allocate();
            set(result);
            if (qualified != nullptr)
            {
                if (is_aggregate_node(qualified->child(1)))
                {
                    if (v->tagged)
                    {
                        get(result);
                        i64(static_cast<std::int64_t>(iProgram.tag_of(v)));
                        store64(0u);
                    }
                    aggregate(qualified->child(1), qualified->type, result);
                    drop();
                }
                else
                {
                    expression(qualified, qualified->type);
                    auto const source = temporary(type::I64);
                    set(source);
                    if (v->tagged)
                    {
                        get(result);
                        i64(static_cast<std::int64_t>(iProgram.tag_of(v)));
                        store64(0u);
                    }
                    copy_value(v, result, source);
                    release(source);
                }
            }
            else
                default_initialize(v, result);
            controlled_initialized(v, result, qualified == nullptr, qualified != nullptr && !is_temporary_value(qualified->child(1)), false);
            get(result);
            return;
        }
        i64(static_cast<std::int64_t>(size));
        heap_allocate();
        set(result);
        if (qualified != nullptr)
        {
            get(result);
            expression(qualified, qualified->type);
            range_check(v, nullptr, qualified->child(1));
            // (and of the designated subtype (the object's, 4.8(10/2)))
            if (designated != nullptr && designated->view() != v && (designated->view()->is_scalar() || designated->view()->is_access()))
                range_check(designated, nullptr, qualified->child(1));
            store(kind_of(v), iProgram.scalar_size(v));
        }
        else if (v->is_access())
        {
            get(result);
            i64(0);
            if (v->not_null)
                range_check(v, nullptr, nullptr);   // (null: not of a null-excluding subtype)
            store64(0u);
        }
        else if (auto* d = v->is_scalar() ? iProgram.default_value_of(v, false) : nullptr)
        {
            // (a scalar type's Default_Value)
            get(result);
            expression(d, objectType);
            store(kind_of(v), iProgram.scalar_size(v));
        }
        else if (auto invalid = iProgram.iNormalizeScalars && v->is_scalar() ? iProgram.invalid_value(v) : std::nullopt)
        {
            // (Normalize_Scalars: an invalid value (H.1(4)))
            get(result);
            i64(*invalid);
            store(kind_of(v), iProgram.scalar_size(v));
        }
        get(result);
    }

    // --- iterators

    void function_generator::loop_body(node* aBody)
    {
        // (a loop's statements, or a quantified expression's predicate test (see quantified))
        if (iLoopBody)
        {
            auto body = std::move(iLoopBody);
            body();
            iLoopBody = std::move(body);
        }
        else
            statements(aBody);
    }

    void function_generator::quantified(node* aExpression)
    {
        // (an i32: whether the predicate holds for all (some) of the iterator's values (4.5.8): a loop that ends at the first value
        // that decides it)
        bool const all = aExpression->has_flag("all");
        node* specification = nullptr;
        for (auto* c : aExpression->children)
            if (!c->is(construct::Flag) && specification == nullptr)
                specification = c;
        auto* predicate = aExpression->children.back();
        auto const result = local(type::I32);
        i32(all ? 1 : 0);
        set(result);
        begin(control_kind::Block);
        auto const done = iControls.size() - 1u;
        auto saved = std::move(iLoopBody);
        iLoopBody = [&]()
        {
            condition(predicate);
            if (all)
                op(opcode::I32Eqz);
            begin_if();
            i32(all ? 0 : 1);
            set(result);
            // (its loop left: the objects since its start finalized (e.g. a container iterator's element reference))
            for (auto i = iControls.size(); i-- > done + 1u;)
            {
                if (iControls[i].onExit != nullptr)
                    (*iControls[i].onExit)();
                if (iControls[i].kind == control_kind::Breakable && iControls[i].finMark >= 0)
                {
                    get(static_cast<std::uint32_t>(iControls[i].finMark));
                    runtime("fin_release", { type::I64 }, type::Void);
                    check();
                }
            }
            branch(done);
            end();
        };
        if (specification->is(construct::ForRange) && specification->mark == 1u)
            generalized_iterator_loop(aExpression, specification, nullptr);
        else if (specification->is(construct::ForRange))
        {
            auto* parameter = specification->first(construct::Defining)->ent;
            bool const reverse = specification->has_flag("reverse");
            auto const low = local(type::I64);
            auto const high = local(type::I64);
            discrete_bounds(specification, parameter->type, low, high);
            parameter->where = storage::None;
            assign_storage(parameter);
            begin(control_kind::Breakable);
            auto const exit = iControls.size() - 1u;
            get(low);
            get(high);
            op(opcode::I64GtS);
            branch(exit, true);
            get(reverse ? high : low);
            store_object(parameter);
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            if (auto* filter = specification->first(construct::Filter))
            {
                condition(filter->child(0));
                begin_if();
                loop_body(nullptr);
                end();
            }
            else
                loop_body(nullptr);
            load_object(parameter);
            get(reverse ? low : high);
            op(opcode::I64Eq);
            branch(exit, true);
            load_object(parameter);
            i64(reverse ? -1 : 1);
            op(opcode::I64Add);
            store_object(parameter);
            branch(head);
            end();
            end();
        }
        else
            iterator_loop(aExpression, specification, nullptr);
        iLoopBody = std::move(saved);
        end();
        get(result);
    }

    void function_generator::iterator_loop(node* aLoop, node* aSpecification, node* aBody)
    {
        // (for X of an array: each element (X renames it))
        auto* parameter = aSpecification->first(construct::Defining)->ent;
        node* iterable = parameter->renamed_name;
        auto* t = iterable->type != nullptr ? iterable->type->view() : nullptr;
        if (t != nullptr && t->kind == type_kind::Access)
            t = iProgram.designated_of(t)->view();
        if (t == nullptr || t->kind != type_kind::Array)
        {
            container_iterator_loop(aLoop, aSpecification, aBody);
            return;
        }
        bool const reverse = aSpecification->has_flag("reverse");
        address(iterable);
        auto const descriptor = local(type::I64);
        set(descriptor);
        auto const held = hold_temporaries();
        auto const n = local(type::I64);
        array_length(descriptor, t->base_type()->view()->indexes.size());
        set(n);
        // (convention Fortran: column major canonical order (the first index varies fastest, 5.5.2(10/3)): the k-th element's
        // position in the (row major) data)
        auto const dimensions = t->base_type()->view()->indexes.size();
        bool const columnMajor = t->base_type()->view()->fortran && dimensions > 1u;
        std::uint32_t k = 0u;
        if (columnMajor)
        {
            k = local(type::I64);
            if (reverse)
            {
                get(n);
                i64(1);
                op(opcode::I64Sub);
            }
            else
                i64(0);
            set(k);
        }
        auto const element = local(type::I64);
        data_address(descriptor);
        if (reverse)
        {
            get(n);
            i64(1);
            op(opcode::I64Sub);
            element_size(t);
            op(opcode::I64Mul);
            op(opcode::I64Add);
        }
        set(element);
        indirect_storage(parameter);
        begin(control_kind::Breakable, {}, aLoop->ent);
        auto const exit = iControls.size() - 1u;
        begin(control_kind::Loop);
        auto const head = iControls.size() - 1u;
        get(n);
        op(opcode::I64Eqz);
        branch(exit, true);
        if (columnMajor)
        {
            auto const rest = temporary(type::I64);
            auto const offset = temporary(type::I64);
            auto const stride = temporary(type::I64);
            auto const length = temporary(type::I64);
            get(k);
            set(rest);
            i64(0);
            set(offset);
            for (std::size_t d = 0u; d < dimensions; ++d)
            {
                // (stride: the product of the later dimensions' lengths)
                i64(1);
                set(stride);
                for (std::size_t e = d + 1u; e < dimensions; ++e)
                {
                    bounds(descriptor, e, false);
                    bounds(descriptor, e, true);
                    op(opcode::I64Sub);
                    i64(1);
                    op(opcode::I64Add);
                    get(stride);
                    op(opcode::I64Mul);
                    set(stride);
                }
                bounds(descriptor, d, false);
                bounds(descriptor, d, true);
                op(opcode::I64Sub);
                i64(1);
                op(opcode::I64Add);
                set(length);
                get(rest);
                get(length);
                op(opcode::I64RemS);
                get(stride);
                op(opcode::I64Mul);
                get(offset);
                op(opcode::I64Add);
                set(offset);
                get(rest);
                get(length);
                op(opcode::I64DivS);
                set(rest);
            }
            data_address(descriptor);
            get(offset);
            element_size(t);
            op(opcode::I64Mul);
            op(opcode::I64Add);
            set(element);
            release(length);
            release(stride);
            release(offset);
            release(rest);
        }
        get(element);
        set_indirect(parameter);
        if (auto* filter = aSpecification->first(construct::Filter))
        {
            condition(filter->child(0));
            begin_if();
            loop_body(aBody);
            end();
        }
        else
            loop_body(aBody);
        if (columnMajor)
        {
            get(k);
            i64(reverse ? -1 : 1);
            op(opcode::I64Add);
            set(k);
        }
        else
        {
            get(element);
            element_size(t);
            op(reverse ? opcode::I64Sub : opcode::I64Add);
            set(element);
        }
        get(n);
        i64(1);
        op(opcode::I64Sub);
        set(n);
        branch(head);
        end();
        end();
        release_held(held);
    }

    entity* function_generator::iterator_operation(ada_type* aIterator, std::string const& aName)
    {
        // (an iterator type's (or interface's) operation (First, Next, Last, Previous): its own or an interface's it implements)
        auto* t = aIterator->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view();
        std::vector<ada_type*> seen;
        std::function<entity*(ada_type*)> search = [&](ada_type* aType) -> entity*
        {
            auto* a = aType->view()->base_type()->view();
            if (std::find(seen.begin(), seen.end(), a) != seen.end())
                return nullptr;
            seen.push_back(a);
            for (auto* list : { &a->primitives, a->partial != nullptr ? &a->partial->primitives : nullptr })
                if (list != nullptr)
                    for (auto* op : *list)
                        if (op->name == aName && op->sub != nullptr &&
                            op->sub->parameters.size() == (aName == "first" || aName == "last" ? 1u : 2u))
                            return op;
            for (auto* i : a->interfaces)
                if (auto* op = search(i))
                    return op;
            if (a->parent != nullptr)
                return search(a->parent);
            return nullptr;
        };
        return search(t);
    }

    void function_generator::iterator_call(entity* aOperation, std::uint32_t aIterator, std::optional<std::uint32_t> aCursor, ada_type* aCursorType)
    {
        // (an iterator's operation called (dispatching on the iterator: an interface's through its tag's interface table): its
        // result (a composite's address) on the stack)
        auto* operation = aOperation;
        auto* controlling = operation->sub->controlling != nullptr ? operation->sub->controlling->view()->base_type()->view() : nullptr;
        bool const cursorScalar = aCursorType != nullptr && !aCursorType->view()->is_composite();
        std::vector<type> signature = { type::I64, type::I64 };
        get(aIterator);
        load64(0u);
        load64(static_cast<std::uint32_t>(program::TagLink));
        get(aIterator);
        if (aCursor)
        {
            get(*aCursor);
            if (cursorScalar)
                load(kind_of(aCursorType), iProgram.scalar_size(aCursorType), 0u, !is_unsigned(aCursorType));
            signature.push_back(cursorScalar ? wasm_of(aCursorType) : type::I64);
        }
        get(aIterator);
        load64(0u);
        if (controlling != nullptr && controlling->kind == type_kind::Interface)
        {
            i64(static_cast<std::int64_t>(iProgram.tag_of(controlling)));
            i64(operation->sub->slot);
            runtime("interface_function", { type::I64, type::I64, type::I64 }, type::I64);
        }
        else
            load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(std::max(operation->sub->slot, 0))));
        op(opcode::I32ConvertI64);
        call_indirect(signature, operation->sub->result != nullptr ? wasm_of(operation->sub->result) : type::Void);
        check();
    }

    void function_generator::cursor_loop(node* aLoop, node* aSpecification, node* aBody, std::uint32_t aIterator, ada_type* aIteratorType,
        ada_type* aCursorType, std::function<void(std::uint32_t)> const& aIteration, std::function<void()> const* aIterationEnd, bool aOwnsIterator)
    {
        // (a loop over an iterator's cursors: First (Last), then while Has_Element, Next (Previous); each cursor in a cell (its
        // composite value copied there; the secondary stack released after each call))
        bool const reverse = aSpecification->has_flag("reverse");
        auto* first = iterator_operation(aIteratorType, reverse ? "last" : "first");
        auto* next = iterator_operation(aIteratorType, reverse ? "previous" : "next");
        if (first == nullptr || next == nullptr)
            iProgram.error(aSpecification, "iterator without First and Next");
        if (std::getenv("NEOS_ADA_DEBUG"))
            std::fprintf(stderr, "iterator: first %s/%zu slot %d controlling %s; next %s/%zu slot %d controlling %s\n", first->name.c_str(), first->sub->parameters.size(),
                first->sub->slot, first->sub->controlling != nullptr ? first->sub->controlling->name.c_str() : "-", next->name.c_str(), next->sub->parameters.size(),
                next->sub->slot, next->sub->controlling != nullptr ? next->sub->controlling->name.c_str() : "-");
        // (Has_Element: the cursor type's)
        entity* hasElement = nullptr;
        auto* cb = aCursorType->view()->base_type()->view();
        for (auto* list : { &cb->primitives, cb->partial != nullptr ? &cb->partial->primitives : nullptr })
            if (list != nullptr && hasElement == nullptr)
                for (auto* op : *list)
                    if (op->name == "has_element" && op->sub != nullptr && op->sub->parameters.size() == 1u)
                        hasElement = op;
        if (hasElement == nullptr)
            if (auto* r = cb->declared_in)
            {
                auto range = r->names.equal_range("has_element");
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->sub != nullptr && i->second->sub->parameters.size() == 1u)
                        hasElement = i->second;
            }
        if (hasElement == nullptr)
        {
            // (the iterator interfaces instance's Has_Element formal (its actual, whatever its name, e.g. Is_Prime))
            std::vector<ada_type*> pending{ aIteratorType->view()->kind == type_kind::ClassWide ? aIteratorType->view()->specific : aIteratorType };
            std::vector<ada_type*> seen;
            while (!pending.empty() && hasElement == nullptr)
            {
                auto* a = pending.back()->view()->base_type()->view();
                pending.pop_back();
                if (std::find(seen.begin(), seen.end(), a) != seen.end())
                    continue;
                seen.push_back(a);
                // (in the instance's region, or its formal part's (its parent))
                for (auto* r : { a->declared_in, a->declared_in != nullptr ? a->declared_in->parent : nullptr })
                    if (r != nullptr && hasElement == nullptr)
                    {
                        auto range = r->names.equal_range("has_element");
                        for (auto i = range.first; i != range.second; ++i)
                            if (i->second->sub != nullptr && i->second->sub->parameters.size() == 1u)
                                hasElement = i->second;
                    }
                for (auto* i : a->interfaces)
                    pending.push_back(i);
                if (a->parent != nullptr && a->parent->view()->base_type()->view() != a)
                    pending.push_back(a->parent);
            }
        }
        if (hasElement == nullptr)
            iProgram.error(aSpecification, "cursor type without Has_Element");
        bool const scalar = !aCursorType->view()->is_composite();
        auto const size = scalar ? 8u : std::max<std::uint64_t>(iProgram.object_size(aCursorType), 8u);
        iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
        auto const cellOffset = iFrameSize;
        iFrameSize += size;
        auto const cell = local(type::I64);
        get(iFrame);
        i64(static_cast<std::int64_t>(cellOffset));
        op(opcode::I64Add);
        set(cell);
        auto const store_cursor = [&]()
        {
            // ([the result] -> the cell)
            if (scalar)
            {
                auto const v = temporary(wasm_of(aCursorType));
                set(v);
                get(cell);
                get(v);
                store(kind_of(aCursorType), iProgram.scalar_size(aCursorType), 0u);
                release(v);
            }
            else
            {
                auto const v = temporary(type::I64);
                set(v);
                get(cell);
                get(v);
                i64(static_cast<std::int64_t>(size));
                runtime("copy", { type::I64, type::I64, type::I64 }, type::Void);
                release(v);
            }
        };
        // (a controlled iterator object (e.g. a container's, that locks it against tampering): registered for its finalization
        // (by its master if the loop is left abnormally), finalized when the loop ends)
        bool const controlled = aOwnsIterator && iProgram.has_controlled();
        auto const finalization = [&](bool aRegister)
        {
            // (for each root (Controlled, Limited_Controlled): if the iterator's type is derived from it, its Finalize (its slot))
            for (auto const* root : { "controlled", "limited_controlled" })
            {
                auto* rt = iProgram.library_type("ada.finalization", root);
                int slot = -1;
                for (auto* t : { rt, rt->view(), rt->view()->base_type()->view() })
                    for (std::size_t k = 0u; k < t->slots.size() && slot < 0; ++k)
                        if (t->slots[k].operation != nullptr && t->slots[k].operation->name == "finalize")
                            slot = static_cast<int>(k);
                if (slot < 0)
                    continue;
                get(aIterator);
                load64(0u);
                i64(static_cast<std::int64_t>(iProgram.tag_of(rt)));
                runtime("is_descendant", { type::I64, type::I64 }, type::I32);
                begin_if();
                if (aRegister)
                {
                    get(aIterator);
                    get(aIterator);
                    load64(0u);
                    load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(slot)));
                    get(aIterator);
                    load64(0u);
                    load64(static_cast<std::uint32_t>(program::TagLink));
                    runtime("fin_register", { type::I64, type::I64, type::I64 }, type::Void);
                }
                else
                {
                    get(aIterator);
                    runtime("fin_unregister", { type::I64 }, type::Void);
                    get(aIterator);
                    load64(0u);
                    load64(static_cast<std::uint32_t>(program::TagLink));
                    get(aIterator);
                    get(aIterator);
                    load64(0u);
                    load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(slot)));
                    op(opcode::I32ConvertI64);
                    call_indirect({ type::I64, type::I64 }, type::Void);
                    check();
                }
                end();
            }
        };
        if (controlled)
            finalization(true);
        auto const mark = local(type::I64);
        runtime("ss_mark", {}, type::I64);
        set(mark);
        iterator_call(first, aIterator, {}, aCursorType);
        store_cursor();
        get(mark);
        runtime("ss_release", { type::I64 }, type::Void);
        begin(control_kind::Breakable, {}, aLoop->ent);
        auto const exit = iControls.size() - 1u;
        iControls.back().onExit = aIterationEnd;
        begin(control_kind::Loop);
        auto const head = iControls.size() - 1u;
        // (while Has_Element (Cursor))
        static_link_for(hasElement);
        get(cell);
        if (scalar)
            load(kind_of(aCursorType), iProgram.scalar_size(aCursorType), 0u, !is_unsigned(aCursorType));
        call_function(ultimate(hasElement));
        check();
        op(opcode::I64Eqz);
        branch(exit, true);
        aIteration(cell);
        if (auto* filter = aSpecification->first(construct::Filter))
        {
            condition(filter->child(0));
            begin_if();
            loop_body(aBody);
            end();
        }
        else
            loop_body(aBody);
        if (aIterationEnd != nullptr)
            (*aIterationEnd)();
        iterator_call(next, aIterator, cell, aCursorType);
        store_cursor();
        get(mark);
        runtime("ss_release", { type::I64 }, type::Void);
        branch(head);
        end();
        end();
        if (controlled)
            finalization(false);
    }

    void function_generator::container_iterator_loop(node* aLoop, node* aSpecification, node* aBody)
    {
        // (for E of Container: its Default_Iterator function's iterator's cursors; E: the element each denotes (by the container's
        // Variable_Indexing (else Constant_Indexing) function's reference))
        auto* parameter = aSpecification->first(construct::Defining)->ent;
        node* iterable = parameter->renamed_name;
        auto* containerType = iterable->type;
        auto* iterate = iProgram.aspect_subprogram(containerType, "default_iterator");
        if (iterate == nullptr)
            iProgram.error(aSpecification, "the container type has no Default_Iterator");
        // (its overload with one parameter)
        if (iterate->scope != nullptr)
        {
            auto range = iterate->scope->names.equal_range(iterate->name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->sub != nullptr && i->second->sub->parameters.size() == 1u)
                    iterate = i->second;
        }
        auto* iteratorType = iterate->sub->result;
        auto* cursorType = iProgram.iterator_cursor_type(iteratorType);
        // (Variable_Indexing for a variable container, else Constant_Indexing (5.5.2(6/3)))
        std::function<bool(node*)> const variable = [&](node* aName) -> bool
        {
            if (aName->is(construct::Parenthesized))
                return variable(aName->child(0));
            if (aName->is(construct::All))
                return true;
            auto const m = meaning_of(aName);
            if (m == meaning::Object && aName->ent != nullptr)
            {
                auto* e = aName->ent;
                while (e->renamed != nullptr)
                    e = e->renamed;
                if (e->kind == entity_kind::Variable)
                    return true;
                if (e->kind == entity_kind::Parameter && e->scope != nullptr && e->scope->owner != nullptr && e->scope->owner->sub != nullptr)
                    for (auto const& p : e->scope->owner->sub->parameters)
                        if (p.ent == e)
                            return p.mode != parameter_mode::In;
                return false;
            }
            if ((m == meaning::Component || m == meaning::Index || m == meaning::Slice) && aName->size() > 0u)
                return variable(aName->child(0));
            return m == meaning::Dereference;
        };
        bool const isVariable = variable(iterable);
        entity* indexing = nullptr;
        for (auto const* aspect : { isVariable ? "variable_indexing" : "constant_indexing", isVariable ? "constant_indexing" : "variable_indexing" })
            if (indexing == nullptr)
                if (auto* f = iProgram.aspect_subprogram(containerType, aspect); f != nullptr && f->scope != nullptr)
                {
                    auto range = f->scope->names.equal_range(f->name);
                    for (auto i = range.first; i != range.second; ++i)
                        if (i->second->sub != nullptr && i->second->sub->parameters.size() == 2u &&
                            iProgram.same_type(i->second->sub->parameters[1].type, cursorType))
                            indexing = i->second;
                }
        if (indexing == nullptr)
            iProgram.error(aSpecification, "the container type has no indexing function for its cursors");
        // (the container (its address), its iterator)
        address(iterable);
        auto const container = local(type::I64);
        set(container);
        auto const held = hold_temporaries();
        static_link_for(iterate);
        get(container);
        call_function(ultimate(iterate));
        check();
        auto const iterator = local(type::I64);
        set(iterator);
        // (E: an object at the address of the element that the reference designates)
        parameter->renamed_name = nullptr;
        indirect_storage(parameter);
        // (each element's reference: finalized at the end of its iteration (e.g. a container's lock released))
        auto const reference = local(type::I64);
        auto* referenceType = indexing->sub->result;
        std::function<void()> const finalizeReference = [&]() { controlled_finalize(referenceType, reference); };
        cursor_loop(aLoop, aSpecification, aBody, iterator, iteratorType, cursorType, [&](std::uint32_t aCursor)
            {
                static_link_for(indexing);
                get(container);
                get(aCursor);
                if (!cursorType->view()->is_composite())
                    load(kind_of(cursorType), iProgram.scalar_size(cursorType), 0u, !is_unsigned(cursorType));
                call_function(ultimate(indexing));
                check();
                if (!referenceType->view()->is_composite())
                    materialize(referenceType);     // (an indexing function whose result is the element's value (a scalar's): a copy)
                tee(reference);
                if (auto const offset = iProgram.reference_offset(referenceType))
                    load64(static_cast<std::uint32_t>(*offset));
                set_indirect(parameter);
            }, iProgram.needs_finalization(referenceType) ? &finalizeReference : nullptr);
        release_held(held);
    }

    void function_generator::generalized_iterator_loop(node* aLoop, node* aSpecification, node* aBody)
    {
        // (for C in Iterator: its cursors)
        auto* parameter = aSpecification->first(construct::Defining)->ent;
        node* range = nullptr;
        for (auto* c : aSpecification->children)
            if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Filter))
                range = c;
        auto* iteratorNode = range->is(construct::SubtypeIndication) ? range->child(0) : range;
        auto* iteratorType = iteratorNode->type;
        // (an iterator that is a function's result: the loop's (finalized when it ends); else (an object): the loop's temporaries
        // (the iterator's prefix's) held until it ends)
        bool const owned = is_temporary_value(iteratorNode);
        moved_result(iteratorNode);
        expression(iteratorNode, iteratorType);
        auto const iterator = local(type::I64);
        set(iterator);
        auto const held = hold_temporaries();
        auto* cursorType = parameter->type;
        bool const scalar = !cursorType->view()->is_composite();
        // (C: the cursor in its cell)
        indirect_storage(parameter);
        (void)scalar;
        cursor_loop(aLoop, aSpecification, aBody, iterator, iteratorType, cursorType, [&](std::uint32_t aCursor)
            {
                get(aCursor);
                set_indirect(parameter);
            }, nullptr, owned);
        release_held(held);
    }

}

namespace neos::compilers::ada
{
    // --- support

    entity* program::library_exception(std::string const& aUnit, std::string const& aName)
    {
        auto* unit = library_unit(aUnit);
        if (unit == nullptr)
            return nullptr;
        for (auto* e : selected(unit, aName, iLibrary))
            if (e->kind == entity_kind::Exception)
                return e;
        return nullptr;
    }

    entity* program::assertion_error()
    {
        auto* unit = library_unit("ada.assertions");
        if (unit == nullptr)
            return nullptr;
        for (auto* e : selected(unit, "assertion_error", iLibrary))
            if (e->kind == entity_kind::Exception)
                return e;
        return nullptr;
    }

    bool program::raising_import(std::string const& aImport) const
    {
        // (runtime functions that never raise: no check of the pending exception after calling them)
        static std::set<std::string> const sNonRaising =
        {
            "ada::ss_mark", "ada::ss_release", "ada::ss_allocate", "ada::copy", "ada::fill", "ada::compare", "ada::compare_arrays",
            "ada::put_string", "ada::put_line", "ada::put_int", "ada::image_integer", "ada::image_unsigned", "ada::image_float",
            "ada::image_fixed", "ada::image_fixed_exact", "ada::image_enumeration", "ada::image_character"
        };
        return sNonRaising.find(aImport) == sNonRaising.end();
    }

    static_value program::evaluate_attribute_static(ada_type* aType, std::string const& aAttribute)
    {
        auto* v = aType->view();
        auto* base = v->base_type()->view();
        if (aAttribute == "aft" && base->is_fixed())
        {
            auto const delta = v->delta.sign() != 0 ? v->delta : base->delta;
            std::int64_t aft = 1;
            while (aft < 40 && delta * rational{ bigint::pow(bigint{ 10 }, static_cast<std::uint64_t>(aft)) } < rational{ bigint{ 1 } })
                ++aft;
            return static_value{ bigint{ aft } };
        }
        return {};
    }
}
