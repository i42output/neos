/*
  gen_expr.cpp

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

// C: the code generation of expressions: values (an integer type of up to 32 bits an i32, normalized: its value sign or zero 
// extended from its width; a 64-bit integer type, a pointer or nullptr_t an i64; float an f32; double and long double an f64; 
// an aggregate's value its address), conversions, lvalues (a WebAssembly local, or an address in the memory), calls

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cmath>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::c
{
    using namespace emitter;

    namespace
    {
        node* strip(node* aNode)
        {
            while (aNode->is(construct::Paren) || (aNode->is(construct::Generic) && aNode->selected != nullptr))
                aNode = aNode->is(construct::Paren) ? aNode->child(0) : aNode->selected;
            return aNode;
        }

        bool boolean_valued(node* aNode)
        {
            // (an expression whose value is 0 or 1)
            aNode = strip(aNode);
            switch (aNode->kind)
            {
            case construct::Less:
            case construct::Greater:
            case construct::LessEqual:
            case construct::GreaterEqual:
            case construct::Equal:
            case construct::NotEqual:
            case construct::And:
            case construct::Or:
            case construct::Not:
                return true;
            default:
                return aNode->type && aNode->type->kind == type_kind::Bool;
            }
        }
    }

    // --- conversions

    void function_generator::normalize(qtype aType)
    {
        auto const* t = iProgram.integer_type(aType.type);
        if (!t->is_integer())
            return;
        auto const k = kind_of(aType);
        auto const w = iProgram.width_of(t);
        if (t->kind == type_kind::Bool)
        {
            // (stored values of bool are 0 or 1: a conversion to bool is to_truth's)
            return;
        }
        if (k == vkind::I32)
        {
            if (w >= 32u)
                return;
            if (t->is_signed())
            {
                if (w == 8u)
                    op(opcode::I32SExtendI8);
                else if (w == 16u)
                    op(opcode::I32SExtendI16);
                else
                {
                    i32(static_cast<std::int32_t>(32u - w));
                    op(opcode::I32Shl);
                    i32(static_cast<std::int32_t>(32u - w));
                    op(opcode::I32ShrS);
                }
            }
            else
            {
                i32(static_cast<std::int32_t>((std::uint32_t{ 1 } << w) - 1u));
                op(opcode::I32And);
            }
        }
        else if (k == vkind::I64)
        {
            if (w >= 64u)
                return;
            if (t->is_signed())
            {
                if (w == 32u)
                {
                    op(opcode::I32ConvertI64);
                    op(opcode::I64SConvertI32);
                }
                else
                {
                    i64(static_cast<std::int64_t>(64u - w));
                    op(opcode::I64Shl);
                    i64(static_cast<std::int64_t>(64u - w));
                    op(opcode::I64ShrS);
                }
            }
            else
            {
                i64(static_cast<std::int64_t>((std::uint64_t{ 1 } << w) - 1u));
                op(opcode::I64And);
            }
        }
    }

    void function_generator::to_truth(qtype aType)
    {
        // ([value] -> i32 0 or 1)
        switch (kind_of(aType))
        {
        case vkind::I32:
            if (aType->kind != type_kind::Bool)
            {
                i32(0);
                op(opcode::I32Ne);
            }
            break;
        case vkind::I64:
            i64(0);
            op(opcode::I64Ne);
            break;
        case vkind::F32:
            f32(0.0f);
            op(opcode::F32Ne);
            break;
        case vkind::F64:
            f64(0.0);
            op(opcode::F64Ne);
            break;
        default:
            break;
        }
    }

    void function_generator::convert(qtype aFrom, qtype aTo)
    {
        auto const* from = aFrom.type;
        auto const* to = aTo.type;
        if (to == nullptr || from == nullptr)
            return;
        auto const fk = kind_of(aFrom);
        auto const tk = kind_of(aTo);
        if (to->is_void())
        {
            if (fk != vkind::None)
                drop();
            return;
        }
        if (from->is_aggregate() || to->is_aggregate() || from->is_function())
            return;     // (addresses)
        if (to->kind == type_kind::Bool)
        {
            if (from->kind != type_kind::Bool)
                to_truth(aFrom);
            return;
        }
        auto const* fi = iProgram.integer_type(from);
        auto const* ti = iProgram.integer_type(to);
        if (ti->is_integer())
        {
            if (from->is_floating())
            {
                bool const isSigned = ti->is_signed() || iProgram.width_of(ti) < 32u;
                if (tk == vkind::I32)
                    op(fk == vkind::F32 ? (isSigned ? opcode::I32SConvertF32 : opcode::I32UConvertF32) : (isSigned ? opcode::I32SConvertF64 : opcode::I32UConvertF64));
                else
                    op(fk == vkind::F32 ? (ti->is_signed() ? opcode::I64SConvertF32 : opcode::I64UConvertF32) : (ti->is_signed() ? opcode::I64SConvertF64 : opcode::I64UConvertF64));
                normalize(aTo);
                return;
            }
            if (fk == vkind::I32 && tk == vkind::I64)
                op(fi->is_integer() && !fi->is_signed() ? opcode::I64UConvertI32 : opcode::I64SConvertI32);
            else if (fk == vkind::I64 && tk == vkind::I32)
                op(opcode::I32ConvertI64);
            // (the value made the target's unless it is already one of its values)
            if (fi->is_integer())
            {
                auto const wf = iProgram.width_of(fi);
                auto const wt = iProgram.width_of(ti);
                bool const sf = fi->is_signed();
                bool const st = ti->is_signed();
                bool const fits = (sf == st && wf <= wt) || (!sf && st && wf < wt);
                if (!fits)
                    normalize(aTo);
            }
            else
                normalize(aTo);
            return;
        }
        if (to->is_floating())
        {
            if (from->is_floating())
            {
                if (fk == vkind::F32 && tk == vkind::F64)
                    op(opcode::F64ConvertF32);
                else if (fk == vkind::F64 && tk == vkind::F32)
                    op(opcode::F32ConvertF64);
                return;
            }
            bool const isSigned = !fi->is_integer() || fi->is_signed() || fi->kind == type_kind::Bool;
            if (tk == vkind::F32)
                op(fk == vkind::I32 ? (isSigned ? opcode::F32SConvertI32 : opcode::F32UConvertI32) : (isSigned ? opcode::F32SConvertI64 : opcode::F32UConvertI64));
            else
                op(fk == vkind::I32 ? (isSigned ? opcode::F64SConvertI32 : opcode::F64UConvertI32) : (isSigned ? opcode::F64SConvertI64 : opcode::F64UConvertI64));
            return;
        }
        // (to a pointer or nullptr_t)
        if (fk == vkind::I32)
            op(fi->is_integer() && !fi->is_signed() ? opcode::I64UConvertI32 : opcode::I64SConvertI32);
    }

    void function_generator::value_as(node* aExpression, qtype aType)
    {
        rvalue(aExpression);
        convert(iProgram.value_type(aExpression), aType);
    }

    void function_generator::truth(node* aExpression)
    {
        rvalue(aExpression);
        if (!boolean_valued(aExpression))
            to_truth(iProgram.value_type(aExpression));
    }

    void function_generator::constant_value(constant const& aValue, qtype aType)
    {
        switch (kind_of(aType))
        {
        case vkind::I32:
            i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(aValue.k == constant::kind::Floating ? static_cast<std::uint64_t>(static_cast<std::int64_t>(aValue.floating)) : aValue.integer)));
            break;
        case vkind::I64:
            if (aValue.k == constant::kind::Address)
            {
                std::uint64_t base = 0u;
                if (aValue.object != nullptr)
                {
                    auto* o = aValue.object->self();
                    base = o->kind == entity_kind::Function ? iProgram.descriptor_of(o) : o->address;
                }
                else if (aValue.literal != nullptr)
                    base = iProgram.string_literal_address(aValue.literal);
                i64(static_cast<std::int64_t>(base + aValue.integer));
            }
            else
                i64(static_cast<std::int64_t>(aValue.k == constant::kind::Floating ? static_cast<std::uint64_t>(static_cast<std::int64_t>(aValue.floating)) : aValue.integer));
            break;
        case vkind::F32:
            f32(static_cast<float>(aValue.k == constant::kind::Floating ? aValue.floating : static_cast<double>(aValue.s())));
            break;
        case vkind::F64:
            f64(aValue.k == constant::kind::Floating ? aValue.floating : static_cast<double>(aValue.s()));
            break;
        default:
            break;
        }
    }

    // --- lvalues

    bool function_generator::in_local(node const* aExpression, std::uint32_t& aLocal) const
    {
        auto* e = strip(const_cast<node*>(aExpression));
        if (e->is(construct::Identifier) && e->ent != nullptr && e->ent->kind == entity_kind::Object && e->ent->self()->where == object_storage::Local)
        {
            aLocal = e->ent->self()->local;
            return true;
        }
        return false;
    }

    void function_generator::address(node* aExpression)
    {
        auto* e = aExpression;
        switch (e->kind)
        {
        case construct::Paren:
            address(e->child(0));
            return;
        case construct::Generic:
            address(e->selected);
            return;
        case construct::Identifier:
            if (e->ent == nullptr)
                throw compile_error{ e, "builtin function '" + name_of(e) + "' must be directly called" };
            if (e->ent->kind == entity_kind::Function)
            {
                i64(static_cast<std::int64_t>(iProgram.descriptor_of(e->ent)));
                return;
            }
            object_address(e->ent);
            return;
        case construct::Strings:
            i64(static_cast<std::int64_t>(iProgram.string_literal_address(e)));
            return;
        case construct::CompoundLiteral:
            compound_literal(e);
            return;
        case construct::Subscript:
        {
            auto* a = e->child(0);
            auto* b = e->child(1);
            if (iProgram.value_type(a)->is_pointer())
                pointer_arithmetic(a, b, false);
            else
                pointer_arithmetic(b, a, false, true);
            return;
        }
        case construct::Indirection:
            rvalue(e->child(0));
            return;
        case construct::Member:
        case construct::Arrow:
        {
            if (e->is(construct::Arrow))
                rvalue(e->child(0));
            else
                address(e->child(0));
            std::uint64_t offset = 0u;
            for (auto const* m : e->members)
                offset += m->offset;
            if (offset != 0u)
            {
                i64(static_cast<std::int64_t>(offset));
                op(opcode::I64Add);
            }
            return;
        }
        case construct::Call:
            call_expression(e);
            return;
        case construct::Assign:
            assignment(e, true);
            return;
        case construct::Conditional:
            conditional(e, false);
            return;
        case construct::Comma:
            discard(e->child(0));
            address(e->child(1));
            return;
        case construct::StatementExpression:
            statement_expression(e, use::Address);
            return;
        case construct::Cast:
            address(e->child(1));
            return;
        default:
            break;
        }
        throw compile_error{ e, "internal error: the address of an expression that is not an lvalue" };
    }

    void function_generator::bit_field_load(member_info const& aMember)
    {
        // ([its storage unit's address] -> its value)
        auto const unit = aMember.type;
        auto const k = kind_of(unit);
        auto const size = iProgram.size_of(unit);
        bool const isSigned = iProgram.integer_type(unit.type)->is_signed();
        auto const width = *aMember.bitWidth;
        auto const bits = k == vkind::I64 ? 64u : 32u;
        // (the unit loaded unsigned, shifted so that the field's top bit is the value's top bit, then shifted back (arithmetically if signed))
        opcode loadOp = k == vkind::I64 ? opcode::I64LoadMem : size == 1u ? opcode::I32LoadMem8U : size == 2u ? opcode::I32LoadMem16U : opcode::I32LoadMem;
        op(loadOp);
        u32(0x40u);
        u32(language::c::layout::Memory);
        u32(0u);
        auto const left = bits - aMember.bitOffset - width;
        if (k == vkind::I64)
        {
            if (left != 0u)
            {
                i64(left);
                op(opcode::I64Shl);
            }
            if (bits - width != 0u)
            {
                i64(bits - width);
                op(isSigned ? opcode::I64ShrS : opcode::I64ShrU);
            }
        }
        else
        {
            if (left != 0u)
            {
                i32(static_cast<std::int32_t>(left));
                op(opcode::I32Shl);
            }
            if (bits - width != 0u)
            {
                i32(static_cast<std::int32_t>(bits - width));
                op(isSigned ? opcode::I32ShrS : opcode::I32ShrU);
            }
        }
    }

    void function_generator::bit_field_store(member_info const& aMember, bool aValueUsed)
    {
        // ([its storage unit's address, value] -> (the value stored, if used))
        auto const unit = aMember.type;
        auto const k = kind_of(unit);
        auto const t = wasm_of(unit);
        auto const size = iProgram.size_of(unit);
        auto const width = *aMember.bitWidth;
        auto const value = temporary(t);
        auto const where = temporary(type::I64);
        set(value);
        set(where);
        std::uint64_t const fieldMask = width >= 64u ? ~std::uint64_t{} : ((std::uint64_t{ 1 } << width) - 1u);
        std::uint64_t const mask = fieldMask << aMember.bitOffset;
        opcode loadOp = k == vkind::I64 ? opcode::I64LoadMem : size == 1u ? opcode::I32LoadMem8U : size == 2u ? opcode::I32LoadMem16U : opcode::I32LoadMem;
        opcode storeOp = k == vkind::I64 ? opcode::I64StoreMem : size == 1u ? opcode::I32StoreMem8 : size == 2u ? opcode::I32StoreMem16 : opcode::I32StoreMem;
        get(where);
        get(where);
        op(loadOp);
        u32(0x40u);
        u32(language::c::layout::Memory);
        u32(0u);
        if (k == vkind::I64)
        {
            i64(static_cast<std::int64_t>(~mask));
            op(opcode::I64And);
            get(value);
            i64(static_cast<std::int64_t>(fieldMask));
            op(opcode::I64And);
            if (aMember.bitOffset != 0u)
            {
                i64(aMember.bitOffset);
                op(opcode::I64Shl);
            }
            op(opcode::I64Ior);
        }
        else
        {
            i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(~mask)));
            op(opcode::I32And);
            get(value);
            i32(static_cast<std::int32_t>(static_cast<std::uint32_t>(fieldMask)));
            op(opcode::I32And);
            if (aMember.bitOffset != 0u)
            {
                i32(static_cast<std::int32_t>(aMember.bitOffset));
                op(opcode::I32Shl);
            }
            op(opcode::I32Ior);
        }
        op(storeOp);
        u32(0x40u);
        u32(language::c::layout::Memory);
        u32(0u);
        if (aValueUsed)
        {
            // (the value as the bit-field holds it)
            get(value);
            bool const isSigned = iProgram.integer_type(unit.type)->is_signed();
            auto const bits = k == vkind::I64 ? 64u : 32u;
            if (bits - width != 0u)
            {
                if (k == vkind::I64)
                {
                    i64(bits - width);
                    op(opcode::I64Shl);
                    i64(bits - width);
                    op(isSigned ? opcode::I64ShrS : opcode::I64ShrU);
                }
                else
                {
                    i32(static_cast<std::int32_t>(bits - width));
                    op(opcode::I32Shl);
                    i32(static_cast<std::int32_t>(bits - width));
                    op(isSigned ? opcode::I32ShrS : opcode::I32ShrU);
                }
            }
        }
        release(where);
        release(value);
    }

    void function_generator::dynamic_size(qtype aType)
    {
        // (an i64: the size of a type whose size may be variable (its size expressions evaluated))
        auto const* t = aType.type;
        if (!t->is_variably_modified() || !t->is_array())
        {
            i64(static_cast<std::int64_t>(iProgram.size_of(aType)));
            return;
        }
        if (t->length)
            i64(static_cast<std::int64_t>(*t->length));
        else if (t->vlaSize != nullptr)
            value_as(t->vlaSize, iProgram.size_type());
        else
            i64(0);
        dynamic_size(t->base);
        op(opcode::I64Mul);
    }

    // --- values

    void function_generator::discard(node* aExpression)
    {
        auto* e = aExpression;
        switch (e->kind)
        {
        case construct::Paren:
            discard(e->child(0));
            return;
        case construct::Assign:
        case construct::AssignMultiply:
        case construct::AssignDivide:
        case construct::AssignModulo:
        case construct::AssignAdd:
        case construct::AssignSubtract:
        case construct::AssignShiftLeft:
        case construct::AssignShiftRight:
        case construct::AssignBitAnd:
        case construct::AssignBitXor:
        case construct::AssignBitOr:
            assignment(e, false);
            return;
        case construct::PostIncrement:
        case construct::PostDecrement:
        case construct::PreIncrement:
        case construct::PreDecrement:
            increment(e, false);
            return;
        case construct::Comma:
            discard(e->child(0));
            discard(e->child(1));
            return;
        case construct::StatementExpression:
            statement_expression(e, use::Discard);
            return;
        case construct::Conditional:
            conditional(e, true);
            return;
        case construct::Cast:
            if (e->type->is_void())
            {
                discard(e->child(1));
                return;
            }
            break;
        default:
            break;
        }
        rvalue(e);
        auto const t = iProgram.value_type(e);
        if (kind_of(t) != vkind::None)
            drop();
    }

    void function_generator::compound_literal(node* aLiteral)
    {
        // (an i64: its object's address; an automatic one is initialized each time its expression is evaluated)
        auto* ent = aLiteral->ent;
        if (ent->where == object_storage::Static)
        {
            object_address(ent);
            return;
        }
        initialize(ent->type, ent->initializer, [&]() { object_address(ent); }, 0u, false);
        object_address(ent);
    }

    void function_generator::rvalue(node* aExpression)
    {
        auto* e = aExpression;
        auto const vt = iProgram.value_type(e);
        // (a floating constant expression (e.g. NAN, INFINITY): its value)
        if (vt->is_floating() && (e->is(construct::Add) || e->is(construct::Subtract) || e->is(construct::Multiply) || 
            e->is(construct::Divide) || e->is(construct::Minus) || e->is(construct::Plus) || e->is(construct::Cast)))
        {
            if (auto const v = iProgram.evaluate(e, false); v && v->k == constant::kind::Floating)
            {
                constant_value(*v, vt);
                return;
            }
        }
        switch (e->kind)
        {
        case construct::Paren:
            rvalue(e->child(0));
            return;
        case construct::Generic:
            rvalue(e->selected);
            return;
        case construct::Number:
        case construct::Character:
        case construct::True:
        case construct::False:
        case construct::Nullptr:
            constant_value(*e->value, e->type);
            return;
        case construct::Identifier:
        {
            if (e->value)
            {
                constant_value(*e->value, e->type);
                return;
            }
            if (e->ent == nullptr)
                throw compile_error{ e, "builtin function '" + name_of(e) + "' must be directly called" };
            auto* ent = e->ent->self();
            if (ent->kind == entity_kind::Function)
            {
                i64(static_cast<std::int64_t>(iProgram.descriptor_of(ent)));
                return;
            }
            if (ent->isConstexpr && ent->constantValue && ent->type->is_scalar())
            {
                constant_value(*ent->constantValue, ent->type);
                return;
            }
            if (ent->where == object_storage::Local)
            {
                get(ent->local);
                return;
            }
            object_address(ent);
            load(ent->type);
            return;
        }
        case construct::Strings:
            i64(static_cast<std::int64_t>(iProgram.string_literal_address(e)));
            return;
        case construct::CompoundLiteral:
            compound_literal(e);
            load(e->type);
            return;
        case construct::Subscript:
        case construct::Indirection:
        case construct::Member:
        case construct::Arrow:
        {
            if (e->is(construct::Indirection) && e->type->is_function())
            {
                rvalue(e->child(0));
                return;
            }
            if (e->is(construct::Member) && !e->child(0)->lvalue && !e->type->is_aggregate() && !e->bitField)
            {
                // (a member of a structure value (a call's result, ...): its address is a temporary's)
                address(e);
                load(e->type);
                return;
            }
            address(e);
            if (e->bitField)
                bit_field_load(*e->members.back());
            else
                load(e->type);
            return;
        }
        case construct::Address:
        {
            auto* target = strip(e->child(0));
            if (target->is(construct::Indirection))
            {
                // (&*E: E)
                rvalue(target->child(0));
                return;
            }
            address(e->child(0));
            return;
        }
        case construct::Call:
            call_expression(e);
            return;
        case construct::Assign:
        case construct::AssignMultiply:
        case construct::AssignDivide:
        case construct::AssignModulo:
        case construct::AssignAdd:
        case construct::AssignSubtract:
        case construct::AssignShiftLeft:
        case construct::AssignShiftRight:
        case construct::AssignBitAnd:
        case construct::AssignBitXor:
        case construct::AssignBitOr:
            assignment(e, true);
            return;
        case construct::PostIncrement:
        case construct::PostDecrement:
        case construct::PreIncrement:
        case construct::PreDecrement:
            increment(e, true);
            return;
        case construct::Plus:
            value_as(e->child(0), e->type);
            return;
        case construct::Minus:
            if (e->type->is_floating())
            {
                value_as(e->child(0), e->type);
                op(kind_of(e->type) == vkind::F32 ? opcode::F32Neg : opcode::F64Neg);
                return;
            }
            if (kind_of(e->type) == vkind::I64)
                i64(0);
            else
                i32(0);
            value_as(e->child(0), e->type);
            op(kind_of(e->type) == vkind::I64 ? opcode::I64Sub : opcode::I32Sub);
            normalize(e->type);
            return;
        case construct::Complement:
            value_as(e->child(0), e->type);
            if (kind_of(e->type) == vkind::I64)
            {
                i64(-1);
                op(opcode::I64Xor);
            }
            else
            {
                i32(-1);
                op(opcode::I32Xor);
            }
            normalize(e->type);
            return;
        case construct::Not:
            truth(e->child(0));
            op(opcode::I32Eqz);
            return;
        case construct::Sizeof:
        case construct::SizeofType:
        case construct::Alignof:
        {
            if (e->value)
            {
                constant_value(*e->value, e->type);
                return;
            }
            // (a variable length array's size: evaluated)
            auto* a = e->child(0);
            if (!a->is(construct::TypeName))
            {
                auto* s = strip(a);
                if (s->is(construct::Identifier) && s->ent != nullptr && s->ent->self()->where == object_storage::Dynamic)
                {
                    get(s->ent->self()->sizeLocal);
                    return;
                }
                if (s->is(construct::Indirection) || s->is(construct::Subscript))
                {
                    dynamic_size(a->type);
                    return;
                }
                dynamic_size(a->type);
                return;
            }
            dynamic_size(iProgram.type_name_type(a));
            return;
        }
        case construct::Cast:
        {
            auto* a = e->child(1);
            if (e->type->is_void())
            {
                discard(a);
                return;
            }
            value_as(a, e->type);
            return;
        }
        case construct::Multiply:
        case construct::Divide:
        case construct::Modulo:
        case construct::Add:
        case construct::Subtract:
        case construct::ShiftLeft:
        case construct::ShiftRight:
        case construct::BitAnd:
        case construct::BitXor:
        case construct::BitOr:
        case construct::Less:
        case construct::Greater:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
            binary(e);
            return;
        case construct::And:
        case construct::Or:
        {
            truth(e->child(0));
            begin_if(type::I32);
            if (e->is(construct::And))
            {
                truth(e->child(1));
                else_();
                i32(0);
            }
            else
            {
                i32(1);
                else_();
                truth(e->child(1));
            }
            end();
            return;
        }
        case construct::Conditional:
            conditional(e, false);
            return;
        case construct::Comma:
            discard(e->child(0));
            rvalue(e->child(1));
            return;
        case construct::StatementExpression:
            statement_expression(e, use::Value);
            return;
        default:
            break;
        }
        (void)vt;
        throw compile_error{ e, "internal error: no code for expression" };
    }

    void function_generator::statement_expression(node* aExpression, use aUse)
    {
        // (its statements (the enclosing statement's temporaries kept), then its value (its last statement's expression))
        auto* compound = aExpression->child(0);
        auto* value = iProgram.statement_expression_value(aExpression);
        auto const savedBase = iTemporaryBase;
        iTemporaryBase = iTemporaries;
        std::vector<node*> items{ compound->children.begin(), compound->children.end() };
        if (value != nullptr)
            items.pop_back();
        bool const stackMark = std::any_of(items.begin(), items.end(), [](node* aItem)
        {
            if (!aItem->is(construct::Declaration))
                return false;
            for (auto* c : aItem->children)
                if (c->is(construct::InitDeclarator) && c->ent != nullptr && c->ent->kind == entity_kind::Object && c->ent->where == object_storage::Dynamic)
                    return true;
            return false;
        });
        if (stackMark)
            throw compile_error{ aExpression, "a variable length array in a statement expression is not supported" };
        if (iDispatch && contains_target(compound))
            dispatched_items(aExpression, items);
        else
            block_items(items);
        iTemporaryBase = savedBase;
        if (value == nullptr)
        {
            if (aUse == use::Address)
                throw compile_error{ aExpression, "internal error: the address of a statement expression without a value" };
            return;
        }
        switch (aUse)
        {
        case use::Value:
            rvalue(value);
            break;
        case use::Address:
            address(value);
            break;
        case use::Discard:
            discard(value);
            break;
        }
    }

    void function_generator::conditional(node* aExpression, bool aDiscard)
    {
        auto* e = aExpression;
        auto const t = e->type;
        truth(e->child(0));
        if (aDiscard || t->is_void())
        {
            begin_if();
            discard(e->child(1));
            else_();
            discard(e->child(2));
            end();
            return;
        }
        auto const w = t->is_aggregate() ? type::I64 : wasm_of(t);
        begin_if(w);
        if (t->is_aggregate())
            address(e->child(1));
        else
            value_as(e->child(1), t);
        else_();
        if (t->is_aggregate())
            address(e->child(2));
        else
            value_as(e->child(2), t);
        end();
    }

    void function_generator::arithmetic(construct aOperator, qtype aType)
    {
        auto const k = kind_of(aType);
        bool const isSigned = iProgram.integer_type(aType.type)->is_signed();
        if (k == vkind::F32 || k == vkind::F64)
        {
            bool const f = k == vkind::F32;
            switch (aOperator)
            {
            case construct::Multiply: op(f ? opcode::F32Mul : opcode::F64Mul); break;
            case construct::Divide: op(f ? opcode::F32Div : opcode::F64Div); break;
            case construct::Add: op(f ? opcode::F32Add : opcode::F64Add); break;
            case construct::Subtract: op(f ? opcode::F32Sub : opcode::F64Sub); break;
            default: throw compile_error{ nullptr, "internal error: floating operator" };
            }
            return;
        }
        bool const l = k == vkind::I64;
        switch (aOperator)
        {
        case construct::Multiply: op(l ? opcode::I64Mul : opcode::I32Mul); break;
        case construct::Divide: op(l ? (isSigned ? opcode::I64DivS : opcode::I64DivU) : (isSigned ? opcode::I32DivS : opcode::I32DivU)); break;
        case construct::Modulo: op(l ? (isSigned ? opcode::I64RemS : opcode::I64RemU) : (isSigned ? opcode::I32RemS : opcode::I32RemU)); break;
        case construct::Add: op(l ? opcode::I64Add : opcode::I32Add); break;
        case construct::Subtract: op(l ? opcode::I64Sub : opcode::I32Sub); break;
        case construct::ShiftLeft: op(l ? opcode::I64Shl : opcode::I32Shl); break;
        case construct::ShiftRight: op(l ? (isSigned ? opcode::I64ShrS : opcode::I64ShrU) : (isSigned ? opcode::I32ShrS : opcode::I32ShrU)); break;
        case construct::BitAnd: op(l ? opcode::I64And : opcode::I32And); break;
        case construct::BitXor: op(l ? opcode::I64Xor : opcode::I32Xor); break;
        case construct::BitOr: op(l ? opcode::I64Ior : opcode::I32Ior); break;
        default: throw compile_error{ nullptr, "internal error: integer operator" };
        }
        // (a bit-precise integer narrower than its representation: its result wraps (made its type's))
        auto const w = iProgram.width_of(aType.type);
        if (w < (l ? 64u : 32u) && aOperator != construct::BitAnd && aOperator != construct::BitOr && aOperator != construct::BitXor &&
            aOperator != construct::ShiftRight && aOperator != construct::Divide && aOperator != construct::Modulo)
            normalize(aType);
    }

    void function_generator::compare(construct aOperator, qtype aType)
    {
        auto const k = kind_of(aType);
        if (k == vkind::F32 || k == vkind::F64)
        {
            bool const f = k == vkind::F32;
            switch (aOperator)
            {
            case construct::Less: op(f ? opcode::F32Lt : opcode::F64Lt); break;
            case construct::Greater: op(f ? opcode::F32Gt : opcode::F64Gt); break;
            case construct::LessEqual: op(f ? opcode::F32Le : opcode::F64Le); break;
            case construct::GreaterEqual: op(f ? opcode::F32Ge : opcode::F64Ge); break;
            case construct::Equal: op(f ? opcode::F32Eq : opcode::F64Eq); break;
            default: op(f ? opcode::F32Ne : opcode::F64Ne); break;
            }
            return;
        }
        bool const l = k == vkind::I64;
        bool const isSigned = aType->is_integer() && iProgram.integer_type(aType.type)->is_signed();
        switch (aOperator)
        {
        case construct::Less: op(l ? (isSigned ? opcode::I64LtS : opcode::I64LtU) : (isSigned ? opcode::I32LtS : opcode::I32LtU)); break;
        case construct::Greater: op(l ? (isSigned ? opcode::I64GtS : opcode::I64GtU) : (isSigned ? opcode::I32GtS : opcode::I32GtU)); break;
        case construct::LessEqual: op(l ? (isSigned ? opcode::I64LeS : opcode::I64LeU) : (isSigned ? opcode::I32LeS : opcode::I32LeU)); break;
        case construct::GreaterEqual: op(l ? (isSigned ? opcode::I64GeS : opcode::I64GeU) : (isSigned ? opcode::I32GeS : opcode::I32GeU)); break;
        case construct::Equal: op(l ? opcode::I64Eq : opcode::I32Eq); break;
        default: op(l ? opcode::I64Ne : opcode::I32Ne); break;
        }
    }

    void function_generator::pointer_arithmetic(node* aPointer, node* aInteger, bool aSubtract, bool aIntegerFirst)
    {
        // (an i64: the pointer's value plus (minus) the integer times the size of what it points to)
        auto const pt = iProgram.value_type(aPointer);
        auto const it = iProgram.value_type(aInteger);
        auto const elementSize = [&]()
        {
            if (pt->base->is_variably_modified())
                dynamic_size(pt->base);
            else
                i64(static_cast<std::int64_t>(std::max<std::uint64_t>(1u, iProgram.size_of(pt->base))));
        };
        auto const scaled = [&]()
        {
            auto const v = aInteger->value;
            if (v && v->is_integer() && !pt->base->is_variably_modified())
            {
                std::int64_t n = it->is_signed() || iProgram.size_of(it) == 8u ? v->s() : static_cast<std::int64_t>(v->integer);
                i64(n * static_cast<std::int64_t>(std::max<std::uint64_t>(1u, iProgram.size_of(pt->base))));
                return;
            }
            value_as(aInteger, qtype{ iProgram.basic(it->is_signed() ? type_kind::Long : type_kind::ULong) });
            auto const size = iProgram.size_of(pt->base);
            if (size != 1u || pt->base->is_variably_modified())
            {
                elementSize();
                op(opcode::I64Mul);
            }
        };
        if (aIntegerFirst)
        {
            scaled();
            rvalue(aPointer);
            op(opcode::I64Add);
            return;
        }
        rvalue(aPointer);
        scaled();
        op(aSubtract ? opcode::I64Sub : opcode::I64Add);
    }

    void function_generator::binary(node* aExpression)
    {
        auto* e = aExpression;
        auto* a = e->child(0);
        auto* b = e->child(1);
        auto const ta = iProgram.value_type(a);
        auto const tb = iProgram.value_type(b);
        switch (e->kind)
        {
        case construct::Add:
        case construct::Subtract:
            if (ta->is_pointer() && tb->is_integer())
            {
                pointer_arithmetic(a, b, e->is(construct::Subtract));
                return;
            }
            if (ta->is_integer() && tb->is_pointer())
            {
                pointer_arithmetic(b, a, false, true);
                return;
            }
            if (ta->is_pointer() && tb->is_pointer())
            {
                // (the difference of two pointers: in elements)
                rvalue(a);
                rvalue(b);
                op(opcode::I64Sub);
                auto const size = iProgram.size_of(ta->base);
                if (ta->base->is_variably_modified())
                {
                    dynamic_size(ta->base);
                    op(opcode::I64DivS);
                }
                else if (size > 1u)
                {
                    i64(static_cast<std::int64_t>(size));
                    op(opcode::I64DivS);
                }
                return;
            }
            break;
        case construct::ShiftLeft:
        case construct::ShiftRight:
        {
            value_as(a, e->type);
            // (the count: of the left operand's representation)
            auto const countType = kind_of(e->type) == vkind::I64 ? qtype{ iProgram.basic(type_kind::ULong) } : qtype{ iProgram.basic(type_kind::UInt) };
            rvalue(b);
            auto const bk = kind_of(tb);
            if (bk == vkind::I32 && kind_of(e->type) == vkind::I64)
                op(opcode::I64UConvertI32);
            else if (bk == vkind::I64 && kind_of(e->type) == vkind::I32)
                op(opcode::I32ConvertI64);
            (void)countType;
            arithmetic(e->kind, e->type);
            return;
        }
        case construct::Less:
        case construct::Greater:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
        {
            qtype common;
            if (ta->is_arithmetic() && tb->is_arithmetic())
                common = iProgram.usual_arithmetic(ta, tb);
            else
                common = qtype{ iProgram.basic(type_kind::ULong) };    // (pointers (and nullptr_t, and a null pointer constant))
            value_as(a, common);
            value_as(b, common);
            compare(e->kind, common);
            return;
        }
        default:
            break;
        }
        value_as(a, e->type);
        value_as(b, e->type);
        arithmetic(e->kind, e->type);
    }

    // --- assignment

    void function_generator::store_to(node* aTarget, std::function<void()> const& aValue, bool aValueUsed)
    {
        // (the target given the value (of its type); if used, the value (an aggregate: the target's address) remains)
        auto const t = aTarget->type.unqualified();
        std::uint32_t l = 0u;
        if (in_local(aTarget, l))
        {
            aValue();
            if (aValueUsed)
                tee(l);
            else
                set(l);
            return;
        }
        if (t->is_aggregate())
        {
            auto const destination = temporary(type::I64);
            address(aTarget);
            tee(destination);
            aValue();
            copy(iProgram.size_of(t));
            if (aValueUsed)
                get(destination);
            release(destination);
            return;
        }
        auto* target = strip(aTarget);
        if (target->bitField)
        {
            address(target);
            aValue();
            bit_field_store(*target->members.back(), aValueUsed);
            return;
        }
        address(aTarget);
        aValue();
        if (aValueUsed)
        {
            auto const v = temporary(wasm_of(t));
            tee(v);
            store(t);
            get(v);
            release(v);
        }
        else
            store(t);
    }

    void function_generator::assignment(node* aAssignment, bool aValueUsed)
    {
        auto* e = aAssignment;
        auto* target = e->child(0);
        auto* value = e->child(1);
        auto const t = target->type.unqualified();
        if (e->is(construct::Assign))
        {
            if (t->is_aggregate())
                store_to(target, [&]() { address(value); }, aValueUsed);
            else
                store_to(target, [&]() { value_as(value, t); }, aValueUsed);
            return;
        }
        construct operation;
        switch (e->kind)
        {
        case construct::AssignMultiply: operation = construct::Multiply; break;
        case construct::AssignDivide: operation = construct::Divide; break;
        case construct::AssignModulo: operation = construct::Modulo; break;
        case construct::AssignAdd: operation = construct::Add; break;
        case construct::AssignSubtract: operation = construct::Subtract; break;
        case construct::AssignShiftLeft: operation = construct::ShiftLeft; break;
        case construct::AssignShiftRight: operation = construct::ShiftRight; break;
        case construct::AssignBitAnd: operation = construct::BitAnd; break;
        case construct::AssignBitXor: operation = construct::BitXor; break;
        default: operation = construct::BitOr; break;
        }
        auto const tv = iProgram.value_type(value);
        // (E1 op= E2: E1 = E1 op E2, E1 evaluated once)
        auto const compute = [&](std::function<void()> const& aOld)
        {
            if (t->is_pointer())
            {
                aOld();
                if (auto const v = value->value; v && v->is_integer())
                {
                    std::int64_t n = tv->is_signed() || iProgram.size_of(tv) == 8u ? v->s() : static_cast<std::int64_t>(v->integer);
                    i64(n * static_cast<std::int64_t>(std::max<std::uint64_t>(1u, iProgram.size_of(t->base))));
                }
                else
                {
                    value_as(value, qtype{ iProgram.basic(tv->is_signed() ? type_kind::Long : type_kind::ULong) });
                    if (t->base->is_variably_modified())
                    {
                        dynamic_size(t->base);
                        op(opcode::I64Mul);
                    }
                    else if (iProgram.size_of(t->base) > 1u)
                    {
                        i64(static_cast<std::int64_t>(iProgram.size_of(t->base)));
                        op(opcode::I64Mul);
                    }
                }
                op(operation == construct::Subtract ? opcode::I64Sub : opcode::I64Add);
                return;
            }
            bool const shift = operation == construct::ShiftLeft || operation == construct::ShiftRight;
            auto const common = shift ? iProgram.promote(t) : iProgram.usual_arithmetic(t, tv);
            aOld();
            convert(t, common);
            if (shift)
            {
                rvalue(value);
                auto const bk = kind_of(tv);
                if (bk == vkind::I32 && kind_of(common) == vkind::I64)
                    op(opcode::I64UConvertI32);
                else if (bk == vkind::I64 && kind_of(common) == vkind::I32)
                    op(opcode::I32ConvertI64);
            }
            else
                value_as(value, common);
            arithmetic(operation, common);
            convert(common, t);
        };
        std::uint32_t l = 0u;
        if (in_local(target, l))
        {
            compute([&]() { get(l); });
            if (aValueUsed)
                tee(l);
            else
                set(l);
            return;
        }
        auto* s = strip(target);
        auto const where = temporary(type::I64);
        address(target);
        tee(where);
        if (s->bitField)
        {
            compute([&]() { get(where); bit_field_load(*s->members.back()); });
            bit_field_store(*s->members.back(), aValueUsed);
        }
        else
        {
            compute([&]() { get(where); load(t); });
            if (aValueUsed)
            {
                auto const v = temporary(wasm_of(t));
                tee(v);
                store(t);
                get(v);
                release(v);
            }
            else
                store(t);
        }
        release(where);
    }

    void function_generator::increment(node* aExpression, bool aValueUsed)
    {
        auto* e = aExpression;
        auto* target = e->child(0);
        auto const t = target->type.unqualified();
        bool const post = e->is(construct::PostIncrement) || e->is(construct::PostDecrement);
        bool const up = e->is(construct::PostIncrement) || e->is(construct::PreIncrement);
        auto const k = kind_of(t);
        auto const w = wasm_of(t);
        // ([old value] -> new value)
        auto const step = [&]()
        {
            if (t->kind == type_kind::Bool)
            {
                // (bool: ++ makes it true; -- inverts it)
                if (up)
                {
                    drop();
                    i32(1);
                }
                else
                    op(opcode::I32Eqz);
                return;
            }
            switch (k)
            {
            case vkind::F32:
                f32(1.0f);
                op(up ? opcode::F32Add : opcode::F32Sub);
                break;
            case vkind::F64:
                f64(1.0);
                op(up ? opcode::F64Add : opcode::F64Sub);
                break;
            case vkind::I64:
                if (t->is_pointer())
                {
                    if (t->base->is_variably_modified())
                        dynamic_size(t->base);
                    else
                        i64(static_cast<std::int64_t>(std::max<std::uint64_t>(1u, iProgram.size_of(t->base))));
                }
                else
                    i64(1);
                op(up ? opcode::I64Add : opcode::I64Sub);
                if (!t->is_pointer())
                    normalize(t);
                break;
            default:
                i32(1);
                op(up ? opcode::I32Add : opcode::I32Sub);
                normalize(t);
                break;
            }
        };
        std::uint32_t l = 0u;
        if (in_local(target, l))
        {
            get(l);
            if (post && aValueUsed)
            {
                auto const old = temporary(w);
                tee(old);
                step();
                set(l);
                get(old);
                release(old);
            }
            else
            {
                step();
                if (aValueUsed)
                    tee(l);
                else
                    set(l);
            }
            return;
        }
        auto* s = strip(target);
        auto const where = temporary(type::I64);
        address(target);
        tee(where);
        get(where);
        if (s->bitField)
            bit_field_load(*s->members.back());
        else
            load(t);
        std::optional<std::uint32_t> old;
        if (post && aValueUsed)
        {
            old = temporary(w);
            tee(*old);
        }
        step();
        if (s->bitField)
        {
            bit_field_store(*s->members.back(), aValueUsed && !post);
        }
        else if (aValueUsed && !post)
        {
            auto const v = temporary(w);
            tee(v);
            store(t);
            get(v);
            release(v);
        }
        else
            store(t);
        if (old)
        {
            get(*old);
            release(*old);
        }
        release(where);
    }

    // --- calls

    void function_generator::call_expression(node* aCall, std::optional<std::uint64_t> aResultAddress)
    {
        auto* e = aCall;
        if (e->builtin != 0u)
        {
            builtin_call(e);
            return;
        }
        auto* callee = strip(e->child(0));
        // (a call of a function by its name (or &name, or *name): a direct call)
        entity* direct = nullptr;
        {
            auto* c = callee;
            while (c->is(construct::Address) || c->is(construct::Indirection) || c->is(construct::Paren))
                c = strip(c->child(0));
            if (c->is(construct::Identifier) && c->ent != nullptr && c->ent->kind == entity_kind::Function)
                direct = c->ent->self();
        }
        auto const ct = iProgram.value_type(e->child(0));
        auto const* ft = ct->base.type;
        // (a function called by a declaration without a prototype, that has one (a later declaration's, or its definition's): its 
        // arguments passed as its parameters)
        if (direct != nullptr && !ft->prototype && direct->type->is_function() && direct->type.type->prototype)
            ft = direct->type.type;
        std::optional<std::uint32_t> pointer;
        if (direct == nullptr)
        {
            pointer = temporary(type::I64);
            rvalue(e->child(0));
            set(*pointer);
        }
        // (a structure or union result: in a temporary of the caller's frame)
        std::optional<std::int64_t> result;
        if (iProgram.returns_record(ft))
        {
            result = frame_temporary(iProgram.size_of(ft->base), iProgram.align_of(ft->base));
            get(iFramePointer);
            i64(*result);
            op(opcode::I64Add);
        }
        (void)aResultAddress;
        auto const args = e->size() - 1u;
        auto const fixed = std::min<std::size_t>(ft->parameters.size(), args);
        for (std::size_t i = 0u; i < fixed; ++i)
        {
            auto* a = e->child(i + 1u);
            auto const pt = ft->parameters[i].type.unqualified();
            if (pt->is_record())
            {
                // (a copy, in a temporary)
                auto const at = frame_temporary(iProgram.size_of(pt), iProgram.align_of(pt));
                get(iFramePointer);
                i64(at);
                op(opcode::I64Add);
                address(a);
                copy(iProgram.size_of(pt));
                get(iFramePointer);
                i64(at);
                op(opcode::I64Add);
            }
            else
                value_as(a, pt);
        }
        if (ft->prototype && fixed < ft->parameters.size())
        {
            // (fewer arguments than parameters (a call without a prototype): the rest zero)
            for (std::size_t i = fixed; i < ft->parameters.size(); ++i)
            {
                auto const pt = ft->parameters[i].type.unqualified();
                switch (pt->is_record() ? type::I64 : iProgram.wasm(kind_of(pt)))
                {
                case type::I32: i32(0); break;
                case type::F32: f32(0.0f); break;
                case type::F64: f64(0.0); break;
                default: i64(0); break;
                }
            }
        }
        if (ft->prototype && !ft->variadic && args > fixed)
        {
            // (more arguments than parameters (a call without a prototype): evaluated, and discarded)
            for (std::size_t i = fixed; i < args; ++i)
                discard(e->child(i + 1u));
        }
        std::vector<type> unprototyped;
        if (!ft->prototype)
        {
            // (a call without a prototype: the arguments promoted (a float as double; a structure or union: the address of a copy), 
            // the function called with their types (a function's parameters' types, if the call is valid))
            if (iProgram.returns_record(ft))
                unprototyped.push_back(type::I64);
            for (std::size_t i = 0u; i < args; ++i)
            {
                auto* a = e->child(i + 1u);
                auto const t = iProgram.value_type(a);
                if (t->is_record())
                {
                    auto const at = frame_temporary(iProgram.size_of(t), iProgram.align_of(t));
                    get(iFramePointer);
                    i64(at);
                    op(opcode::I64Add);
                    address(a);
                    copy(iProgram.size_of(t));
                    get(iFramePointer);
                    i64(at);
                    op(opcode::I64Add);
                    unprototyped.push_back(type::I64);
                    continue;
                }
                auto promoted = iProgram.promote(t);
                if (t->kind == type_kind::Float)
                    promoted = qtype{ iProgram.basic(type_kind::Double) };
                value_as(a, promoted);
                unprototyped.push_back(iProgram.wasm(kind_of(promoted)));
            }
        }
        if (ft->variadic)
        {
            // (the variable arguments: each (promoted) in 8-byte slots of an area in a temporary (an integer of 32 bits extended to 64: 
            // as its type's value; a float as double); a structure or union: its bytes (in slots enough for it))
            if (args == fixed)
                i64(0);
            else
            {
                std::uint64_t size = 0u;
                for (std::size_t i = fixed; i < args; ++i)
                {
                    auto const t = iProgram.value_type(e->child(i + 1u));
                    size += t->is_record() ? (iProgram.size_of(t) + 7u) / 8u * 8u : 8u;
                }
                auto const area = frame_temporary(size, 16u);
                std::uint64_t offset = 0u;
                for (std::size_t i = fixed; i < args; ++i)
                {
                    auto* a = e->child(i + 1u);
                    auto const t = iProgram.value_type(a);
                    get(iFramePointer);
                    i64(area + static_cast<std::int64_t>(offset));
                    op(opcode::I64Add);
                    if (t->is_record())
                    {
                        address(a);
                        copy(iProgram.size_of(t));
                        offset += (iProgram.size_of(t) + 7u) / 8u * 8u;
                        continue;
                    }
                    auto promoted = iProgram.promote(t);
                    if (t->kind == type_kind::Float)
                        promoted = qtype{ iProgram.basic(type_kind::Double) };
                    value_as(a, promoted);
                    if (kind_of(promoted) == vkind::I32)
                    {
                        op(iProgram.integer_type(promoted.type)->is_signed() ? opcode::I64SConvertI32 : opcode::I64UConvertI32);
                        store(qtype{ iProgram.basic(type_kind::Long) });
                    }
                    else
                        store(promoted);
                    offset += 8u;
                }
                get(iFramePointer);
                i64(area);
                op(opcode::I64Add);
            }
        }
        if (direct != nullptr && !ft->prototype)
            call(iProgram.unprototyped_call(direct, unprototyped));
        else if (direct != nullptr)
            call(iProgram.function_index(direct));
        else
        {
            get(*pointer);
            op(opcode::I64LoadMem);
            u32(0x43u);
            u32(language::c::layout::Memory);
            u32(0u);
            op(opcode::I32ConvertI64);
            call_indirect(ft->prototype ? iProgram.wasm_parameters(ft) : unprototyped, iProgram.wasm_result(ft));
            release(*pointer);
        }
        if (result)
        {
            get(iFramePointer);
            i64(*result);
            op(opcode::I64Add);
        }
        // (a result of a bit-precise integer type or bool: normalized by the callee)
    }

    void function_generator::builtin_call(node* aCall)
    {
        auto* e = aCall;
        auto const arg = [&](std::size_t i) { return e->child(i + 1u); };
        switch (static_cast<builtin_kind>(e->builtin))
        {
        case builtin_kind::VaStart:
            get(iVarargs);
            return;
        case builtin_kind::VaNext:
        {
            // (the address *ap; *ap advanced past an argument of the size (in 8-byte slots))
            auto const ap = temporary(type::I64);
            auto const current = temporary(type::I64);
            rvalue(arg(0));
            tee(ap);
            get(ap);
            op(opcode::I64LoadMem);
            u32(0x43u);
            u32(language::c::layout::Memory);
            u32(0u);
            tee(current);
            value_as(arg(1), iProgram.size_type());
            i64(7);
            op(opcode::I64Add);
            i64(-8);
            op(opcode::I64And);
            op(opcode::I64Add);
            op(opcode::I64StoreMem);
            u32(0x43u);
            u32(language::c::layout::Memory);
            u32(0u);
            get(current);
            release(current);
            release(ap);
            return;
        }
        case builtin_kind::Setjmp:
            // (its value: 0 when called, longjmp's when resumed (see function_generator::prepare_setjmp))
            if (!iDispatch)
                throw dispatch_needed{ e, "setjmp" };
            if (!iPreparedSetjmps.contains(e))
                throw compile_error{ e, "setjmp in this context (more than one in an expression, or in a loop's controlling expression) is not supported" };
            get(iSetjmpValue);
            return;
        case builtin_kind::Longjmp:
        {
            // (the jump pending: its environment and its value (0: 1); then this function returns, and its caller, and so on, 
            // until a call by the function whose activation called setjmp with the environment (see function_generator::jump_check))
            auto const v = temporary(type::I32);
            value_as(arg(1), iProgram.int_type());
            tee(v);
            get(v);
            op(opcode::I32Eqz);
            op(opcode::I32Ior);
            op(opcode::I64UConvertI32);
            op(opcode::GlobalSet);
            u32(iProgram.jump_value_global());
            release(v);
            rvalue(arg(0));
            op(opcode::GlobalSet);
            u32(iProgram.jump_global());
            unwind_return();
            return;
        }
        case builtin_kind::Unreachable:
        case builtin_kind::Trap:
            op(opcode::Unreachable);
            return;
        case builtin_kind::Exit:
            value_as(arg(0), iProgram.int_type());
            runtime("neos::exit", { type::I32 }, type::Void);
            return;
        case builtin_kind::Clz:
        case builtin_kind::Ctz:
        case builtin_kind::Popcount:
            value_as(arg(0), qtype{ iProgram.basic(type_kind::UInt) });
            op(e->builtin == static_cast<std::uint32_t>(builtin_kind::Clz) ? opcode::I32Clz : e->builtin == static_cast<std::uint32_t>(builtin_kind::Ctz) ? opcode::I32Ctz : opcode::I32Popcnt);
            return;
        case builtin_kind::Clzll:
        case builtin_kind::Ctzll:
        case builtin_kind::Popcountll:
            value_as(arg(0), qtype{ iProgram.basic(type_kind::ULongLong) });
            op(e->builtin == static_cast<std::uint32_t>(builtin_kind::Clzll) ? opcode::I64Clz : e->builtin == static_cast<std::uint32_t>(builtin_kind::Ctzll) ? opcode::I64Ctz : opcode::I64Popcnt);
            op(opcode::I32ConvertI64);
            return;
        case builtin_kind::Bswap16:
        {
            auto const v = temporary(type::I32);
            value_as(arg(0), qtype{ iProgram.basic(type_kind::UShort) });
            tee(v);
            i32(8);
            op(opcode::I32Shl);
            get(v);
            i32(8);
            op(opcode::I32ShrU);
            op(opcode::I32Ior);
            i32(0xFFFF);
            op(opcode::I32And);
            release(v);
            return;
        }
        case builtin_kind::Bswap32:
        case builtin_kind::Bswap64:
        {
            bool const l = e->builtin == static_cast<std::uint32_t>(builtin_kind::Bswap64);
            auto const v = temporary(l ? type::I64 : type::I32);
            auto const r = temporary(l ? type::I64 : type::I32);
            value_as(arg(0), qtype{ iProgram.basic(l ? type_kind::ULongLong : type_kind::UInt) });
            set(v);
            if (l) i64(0); else i32(0);
            set(r);
            int const bytes = l ? 8 : 4;
            for (int b = 0; b < bytes; ++b)
            {
                // (r |= ((v >> 8b) & 0xFF) << 8(bytes-1-b))
                get(r);
                get(v);
                if (l) { i64(8 * b); op(opcode::I64ShrU); i64(0xFF); op(opcode::I64And); i64(8 * (bytes - 1 - b)); op(opcode::I64Shl); op(opcode::I64Ior); }
                else { i32(8 * b); op(opcode::I32ShrU); i32(0xFF); op(opcode::I32And); i32(8 * (bytes - 1 - b)); op(opcode::I32Shl); op(opcode::I32Ior); }
                set(r);
            }
            get(r);
            release(r);
            release(v);
            return;
        }
        case builtin_kind::Expect:
            value_as(arg(0), qtype{ iProgram.basic(type_kind::Long) });
            discard(arg(1));
            return;
        case builtin_kind::Alloca:
        {
            // (the stack pointer lowered by the size (16-byte aligned); the stack checked)
            iDynamicStack = true;
            iFrameUsed = true;
            op(opcode::GlobalGet);
            u32(iProgram.sp_global());
            value_as(arg(0), iProgram.size_type());
            i64(15);
            op(opcode::I64Add);
            i64(-16);
            op(opcode::I64And);
            op(opcode::I64Sub);
            op(opcode::GlobalSet);
            u32(iProgram.sp_global());
            op(opcode::GlobalGet);
            u32(iProgram.sp_global());
            op(opcode::GlobalGet);
            u32(iProgram.limit_global());
            op(opcode::I64LtU);
            begin_if();
            runtime("c::rt.stack_overflow", {}, type::Void);
            op(opcode::Unreachable);
            end();
            op(opcode::GlobalGet);
            u32(iProgram.sp_global());
            return;
        }
        case builtin_kind::CkdAdd:
        case builtin_kind::CkdSub:
        case builtin_kind::CkdMul:
        {
            // (the runtime computes the exact result (operation, the operands' and the result's types' widths and signedness), stores 
            // it (wrapped) and returns whether it overflowed)
            auto const r = iProgram.value_type(arg(0))->base;
            auto const ta = iProgram.value_type(arg(1));
            auto const tb = iProgram.value_type(arg(2));
            auto const flags = [&](qtype aT) { return static_cast<std::int32_t>(iProgram.width_of(aT.type) | (iProgram.integer_type(aT.type)->is_signed() ? 0x100u : 0u)); };
            i32(e->builtin == static_cast<std::uint32_t>(builtin_kind::CkdAdd) ? 0 : e->builtin == static_cast<std::uint32_t>(builtin_kind::CkdSub) ? 1 : 2);
            i32(flags(ta) | (flags(tb) << 9) | (flags(r) << 18));
            value_as(arg(1), qtype{ iProgram.basic(iProgram.integer_type(ta.type)->is_signed() ? type_kind::Long : type_kind::ULong) });
            value_as(arg(2), qtype{ iProgram.basic(iProgram.integer_type(tb.type)->is_signed() ? type_kind::Long : type_kind::ULong) });
            rvalue(arg(0));
            runtime("c::rt.ckd", { type::I32, type::I32, type::I64, type::I64, type::I64 }, type::I32);
            return;
        }
        case builtin_kind::ConstantP:
            i32(iProgram.evaluate(arg(0), false) ? 1 : 0);
            return;
        case builtin_kind::Sqrt: value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) }); op(opcode::F64Sqrt); return;
        case builtin_kind::Sqrtf: value_as(arg(0), qtype{ iProgram.basic(type_kind::Float) }); op(opcode::F32Sqrt); return;
        case builtin_kind::Fabs: value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) }); op(opcode::F64Abs); return;
        case builtin_kind::Fabsf: value_as(arg(0), qtype{ iProgram.basic(type_kind::Float) }); op(opcode::F32Abs); return;
        case builtin_kind::Floor: value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) }); op(opcode::F64Floor); return;
        case builtin_kind::Ceil: value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) }); op(opcode::F64Ceil); return;
        case builtin_kind::Trunc: value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) }); op(opcode::F64Trunc); return;
        case builtin_kind::Nearbyint: value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) }); op(opcode::F64NearestInt); return;
        case builtin_kind::Copysign:
            value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) });
            value_as(arg(1), qtype{ iProgram.basic(type_kind::Double) });
            op(opcode::F64CopySign);
            return;
        case builtin_kind::Fmin:
        case builtin_kind::Fmax:
            // (fmin and fmax: a NaN operand is ignored (WebAssembly's min and max propagate NaNs): the library's)
            value_as(arg(0), qtype{ iProgram.basic(type_kind::Double) });
            value_as(arg(1), qtype{ iProgram.basic(type_kind::Double) });
            libc(e->builtin == static_cast<std::uint32_t>(builtin_kind::Fmin) ? "fmin" : "fmax");
            return;
        case builtin_kind::AtomicExchange:
        case builtin_kind::AtomicFetchAdd:
        case builtin_kind::AtomicFetchSub:
        case builtin_kind::AtomicFetchOr:
        case builtin_kind::AtomicFetchXor:
        case builtin_kind::AtomicFetchAnd:
        {
            // (one thread: the object's old value; its new value stored)
            auto const t = e->type;
            auto const where = temporary(type::I64);
            auto const old = temporary(wasm_of(t));
            rvalue(arg(0));
            tee(where);
            load(t);
            set(old);
            get(where);
            auto const kind = static_cast<builtin_kind>(e->builtin);
            if (kind == builtin_kind::AtomicExchange)
                value_as(arg(1), t);
            else if (t->is_pointer())
            {
                get(old);
                value_as(arg(1), qtype{ iProgram.basic(type_kind::Long) });
                i64(static_cast<std::int64_t>(std::max<std::uint64_t>(1u, iProgram.size_of(t->base))));
                op(opcode::I64Mul);
                op(kind == builtin_kind::AtomicFetchSub ? opcode::I64Sub : opcode::I64Add);
            }
            else
            {
                get(old);
                value_as(arg(1), t);
                arithmetic(kind == builtin_kind::AtomicFetchAdd ? construct::Add : kind == builtin_kind::AtomicFetchSub ? construct::Subtract :
                    kind == builtin_kind::AtomicFetchOr ? construct::BitOr : kind == builtin_kind::AtomicFetchXor ? construct::BitXor : construct::BitAnd, t);
                normalize(t);
            }
            store(t);
            get(old);
            release(old);
            release(where);
            return;
        }
        case builtin_kind::AtomicCompareExchange:
        {
            // (if *object equals *expected, *object = desired (true); else *expected = *object (false))
            auto const t = iProgram.value_type(arg(0))->base.unqualified();
            auto const where = temporary(type::I64);
            auto const expected = temporary(type::I64);
            auto const current = temporary(wasm_of(t));
            rvalue(arg(0));
            set(where);
            rvalue(arg(1));
            set(expected);
            get(where);
            load(t);
            tee(current);
            get(expected);
            load(t);
            compare(construct::Equal, t.type->is_pointer() ? qtype{ iProgram.basic(type_kind::ULong) } : t);
            begin_if(type::I32);
            get(where);
            value_as(arg(2), t);
            store(t);
            i32(1);
            else_();
            get(expected);
            get(current);
            store(t);
            i32(0);
            end();
            release(current);
            release(expected);
            release(where);
            return;
        }
        default:
            throw compile_error{ e, "internal error: builtin" };
        }
    }
}
