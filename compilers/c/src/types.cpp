/*
  types.cpp

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

// C: types (6.2.5): the types' representation (LP64: char 8 bits, short 16, int 32, long and long long 64, pointers 64; float and
// double IEC 60559 binary32 and binary64; long double as double), compatibility and composite types (6.2.7), conversions' types 
// (6.3.1) and structure layout

#include <algorithm>
#include "program.hpp"

namespace neos::compilers::c
{
    bool c_type::is_integer() const
    {
        switch (kind)
        {
        case type_kind::Bool:
        case type_kind::Char:
        case type_kind::SChar:
        case type_kind::UChar:
        case type_kind::Short:
        case type_kind::UShort:
        case type_kind::Int:
        case type_kind::UInt:
        case type_kind::Long:
        case type_kind::ULong:
        case type_kind::LongLong:
        case type_kind::ULongLong:
        case type_kind::BitInt:
        case type_kind::UBitInt:
        case type_kind::Enum:
            return true;
        default:
            return false;
        }
    }

    bool c_type::is_signed() const
    {
        switch (kind)
        {
        case type_kind::Char:
        case type_kind::SChar:
        case type_kind::Short:
        case type_kind::Int:
        case type_kind::Long:
        case type_kind::LongLong:
        case type_kind::BitInt:
            return true;
        case type_kind::Enum:
            return enumeration != nullptr && enumeration->underlying && enumeration->underlying->is_signed();
        case type_kind::Float:
        case type_kind::Double:
        case type_kind::LongDouble:
            return true;
        default:
            return false;
        }
    }

    bool c_type::is_complete() const
    {
        switch (kind)
        {
        case type_kind::Void:
            return false;
        case type_kind::Array:
            return length.has_value() || vla;
        case type_kind::Struct:
        case type_kind::Union:
            return record != nullptr && record->complete;
        case type_kind::Enum:
            return enumeration != nullptr && (enumeration->complete || enumeration->fixed);
        case type_kind::Function:
            return false;
        default:
            return true;
        }
    }

    bool c_type::is_variably_modified() const
    {
        if (kind == type_kind::Array)
            return vla || (base && base->is_variably_modified());
        if (kind == type_kind::Pointer)
            return base && base->is_variably_modified();
        if (kind == type_kind::Function)
            return base && base->is_variably_modified();
        return false;
    }

    bool record_info::find(std::string const& aName, std::vector<member_info const*>& aPath) const
    {
        for (auto const& m : members)
        {
            if (m.name == aName)
            {
                aPath.push_back(&m);
                return true;
            }
            if (m.name.empty() && m.type && m.type->is_record() && m.type->record != nullptr)
            {
                aPath.push_back(&m);
                if (m.type->record->find(aName, aPath))
                    return true;
                aPath.pop_back();
            }
        }
        return false;
    }

    c_type const* program::basic(type_kind aKind)
    {
        auto const existing = iBasic.find(aKind);
        if (existing != iBasic.end())
            return existing->second;
        auto& t = iTypes.emplace_back();
        t.kind = aKind;
        iBasic[aKind] = &t;
        return &t;
    }

    c_type const* program::pointer_to(qtype aType)
    {
        auto const key = std::make_pair(aType.type, aType.qualifiers);
        auto const existing = iPointers.find(key);
        if (existing != iPointers.end())
            return existing->second;
        auto& t = iTypes.emplace_back();
        t.kind = type_kind::Pointer;
        t.base = aType;
        iPointers[key] = &t;
        return &t;
    }

    c_type const* program::array_of(qtype aElement, std::optional<std::uint64_t> aLength, bool aVla, node* aSize)
    {
        auto& t = iTypes.emplace_back();
        t.kind = type_kind::Array;
        t.base = aElement;
        t.length = aLength;
        t.vla = aVla;
        t.vlaSize = aSize;
        return &t;
    }

    qtype program::qualified(qtype aType, std::uint8_t aQualifiers)
    {
        // (an array type's qualifiers: its element type's (6.7.4))
        if (aType.type != nullptr && aType->is_array() && aQualifiers != 0u)
            return qtype{ array_of(qualified(aType->base, aQualifiers), aType->length, aType->vla, aType->vlaSize), aType.qualifiers };
        return aType.with(aQualifiers);
    }

    c_type const* program::function_of(qtype aResult, std::vector<parameter_type> const& aParameters, bool aVariadic, bool aPrototype)
    {
        auto& t = iTypes.emplace_back();
        t.kind = type_kind::Function;
        t.base = aResult;
        t.parameters = aParameters;
        t.variadic = aVariadic;
        t.prototype = aPrototype;
        return &t;
    }

    c_type const* program::bit_int(std::uint32_t aWidth, bool aSigned)
    {
        auto const key = std::make_pair(aWidth, aSigned);
        auto const existing = iBitInts.find(key);
        if (existing != iBitInts.end())
            return existing->second;
        auto& t = iTypes.emplace_back();
        t.kind = aSigned ? type_kind::BitInt : type_kind::UBitInt;
        t.width = aWidth;
        iBitInts[key] = &t;
        return &t;
    }

    c_type const* program::record_type(record_info* aRecord)
    {
        auto const existing = iRecordTypes.find(aRecord);
        if (existing != iRecordTypes.end())
            return existing->second;
        auto& t = iTypes.emplace_back();
        t.kind = aRecord->isUnion ? type_kind::Union : type_kind::Struct;
        t.record = aRecord;
        iRecordTypes[aRecord] = &t;
        return &t;
    }

    c_type const* program::enum_type(enum_info* aEnum)
    {
        auto const existing = iEnumTypes.find(aEnum);
        if (existing != iEnumTypes.end())
            return existing->second;
        auto& t = iTypes.emplace_back();
        t.kind = type_kind::Enum;
        t.enumeration = aEnum;
        iEnumTypes[aEnum] = &t;
        return &t;
    }

    c_type const* program::integer_type(c_type const* aType) const
    {
        if (aType != nullptr && aType->kind == type_kind::Enum && aType->enumeration != nullptr && aType->enumeration->underlying)
            return aType->enumeration->underlying.type;
        return aType;
    }

    std::uint32_t program::width_of(c_type const* aType) const
    {
        aType = integer_type(aType);
        switch (aType->kind)
        {
        case type_kind::Bool: return 1u;
        case type_kind::Char:
        case type_kind::SChar:
        case type_kind::UChar: return 8u;
        case type_kind::Short:
        case type_kind::UShort: return 16u;
        case type_kind::Int:
        case type_kind::UInt: return 32u;
        case type_kind::BitInt:
        case type_kind::UBitInt: return aType->width;
        case type_kind::Enum: return 32u;
        default: return 64u;
        }
    }

    std::uint64_t program::size_of(qtype aType, node const* aWhere) const
    {
        auto const* t = aType.type;
        if (t == nullptr)
            return 0u;
        switch (t->kind)
        {
        case type_kind::Void: return 1u;        // (GNU: arithmetic on void pointers)
        case type_kind::Bool:
        case type_kind::Char:
        case type_kind::SChar:
        case type_kind::UChar: return 1u;
        case type_kind::Short:
        case type_kind::UShort: return 2u;
        case type_kind::Int:
        case type_kind::UInt:
        case type_kind::Float: return 4u;
        case type_kind::Long:
        case type_kind::ULong:
        case type_kind::LongLong:
        case type_kind::ULongLong:
        case type_kind::Double:
        case type_kind::LongDouble:
        case type_kind::NullPtr:
        case type_kind::Pointer: return 8u;
        case type_kind::BitInt:
        case type_kind::UBitInt: return t->width <= 8u ? 1u : t->width <= 16u ? 2u : t->width <= 32u ? 4u : 8u;
        case type_kind::Enum:
            if (t->enumeration != nullptr && t->enumeration->underlying)
                return size_of(t->enumeration->underlying);
            return 4u;
        case type_kind::Array:
            if (!t->length)
                return 0u;
            return *t->length * size_of(t->base);
        case type_kind::Struct:
        case type_kind::Union:
            if (t->record == nullptr || !t->record->complete)
            {
                if (aWhere != nullptr)
                    error(aWhere, "invalid use of incomplete type '" + type_name(aType) + "'");
                return 0u;
            }
            return t->record->size;
        case type_kind::Function: return 1u;
        }
        return 0u;
    }

    std::uint64_t program::align_of(qtype aType) const
    {
        auto const* t = aType.type;
        if (t == nullptr)
            return 1u;
        switch (t->kind)
        {
        case type_kind::Array: return align_of(t->base);
        case type_kind::Struct:
        case type_kind::Union: return t->record != nullptr ? t->record->alignment : 1u;
        case type_kind::Void:
        case type_kind::Function: return 1u;
        default: return std::max<std::uint64_t>(size_of(aType), 1u);
        }
    }

    vkind program::kind_of(qtype aType) const
    {
        auto const* t = aType.type;
        if (t == nullptr || t->is_void())
            return vkind::None;
        if (t->kind == type_kind::Float)
            return vkind::F32;
        if (t->kind == type_kind::Double || t->kind == type_kind::LongDouble)
            return vkind::F64;
        if (t->is_integer())
            return size_of(aType) <= 4u ? vkind::I32 : vkind::I64;
        return vkind::I64;     // (pointers, nullptr_t, and the addresses of aggregates)
    }

    int program::rank(c_type const* aType) const
    {
        // (integer conversion rank (6.3.1.1): bool < char < short < int < long < long long; a bit-precise integer's by its width 
        // (below a standard type of its width))
        aType = integer_type(aType);
        switch (aType->kind)
        {
        case type_kind::Bool: return 1;
        case type_kind::Char:
        case type_kind::SChar:
        case type_kind::UChar: return 20;
        case type_kind::Short:
        case type_kind::UShort: return 30;
        case type_kind::Int:
        case type_kind::UInt:
        case type_kind::Enum: return 40;
        case type_kind::Long:
        case type_kind::ULong: return 50;
        case type_kind::LongLong:
        case type_kind::ULongLong: return 60;
        case type_kind::BitInt:
        case type_kind::UBitInt:
            return aType->width <= 8u ? 19 : aType->width <= 16u ? 29 : aType->width <= 32u ? 39 : 59;
        default: return 0;
        }
    }

    c_type const* program::unsigned_of(c_type const* aType)
    {
        aType = integer_type(aType);
        switch (aType->kind)
        {
        case type_kind::Char:
        case type_kind::SChar: return basic(type_kind::UChar);
        case type_kind::Short: return basic(type_kind::UShort);
        case type_kind::Int: return basic(type_kind::UInt);
        case type_kind::Long: return basic(type_kind::ULong);
        case type_kind::LongLong: return basic(type_kind::ULongLong);
        case type_kind::BitInt: return bit_int(aType->width, false);
        default: return aType;
        }
    }

    qtype program::promote(qtype aType)
    {
        // (the integer promotions (6.3.1.1): a type of rank below int's (a bit-field's too) is int (all its values fit); bit-precise
        // integer types are not promoted)
        auto const* t = aType.type;
        if (t == nullptr || !t->is_integer())
            return aType.unqualified();
        if (t->kind == type_kind::BitInt || t->kind == type_kind::UBitInt)
            return aType.unqualified();
        auto const* i = integer_type(t);
        if (rank(i) < rank(basic(type_kind::Int)))
            return int_type();
        if (t->kind == type_kind::Enum)
            return qtype{ i };
        return aType.unqualified();
    }

    qtype program::usual_arithmetic(qtype aA, qtype aB)
    {
        // (6.3.1.8)
        auto const* a = aA.type;
        auto const* b = aB.type;
        if (a->kind == type_kind::LongDouble || b->kind == type_kind::LongDouble)
            return qtype{ basic(type_kind::LongDouble) };
        if (a->kind == type_kind::Double || b->kind == type_kind::Double)
            return qtype{ basic(type_kind::Double) };
        if (a->kind == type_kind::Float || b->kind == type_kind::Float)
            return qtype{ basic(type_kind::Float) };
        auto pa = promote(aA);
        auto pb = promote(aB);
        a = integer_type(pa.type);
        b = integer_type(pb.type);
        if (a == b)
            return qtype{ a };
        if (a->is_signed() == b->is_signed())
            return qtype{ rank(a) >= rank(b) ? a : b };
        auto const* u = a->is_signed() ? b : a;
        auto const* s = a->is_signed() ? a : b;
        if (rank(u) >= rank(s))
            return qtype{ u };
        if (width_of(s) > width_of(u))
            return qtype{ s };
        return qtype{ unsigned_of(s) };
    }

    bool program::compatible(qtype aA, qtype aB, bool aIgnoreQualifiers) const
    {
        // (6.2.7)
        if (!aIgnoreQualifiers && aA.qualifiers != aB.qualifiers)
            return false;
        auto const* a = aA.type;
        auto const* b = aB.type;
        if (a == b)
            return true;
        if (a == nullptr || b == nullptr)
            return false;
        // (an enumerated type is compatible with its underlying type)
        if (a->kind == type_kind::Enum && b->kind != type_kind::Enum)
            return integer_type(a) == b;
        if (b->kind == type_kind::Enum && a->kind != type_kind::Enum)
            return integer_type(b) == a;
        if (a->kind != b->kind)
            return false;
        switch (a->kind)
        {
        case type_kind::Pointer:
            return compatible(a->base, b->base);
        case type_kind::Array:
            if (!compatible(a->base, b->base))
                return false;
            return !a->length || !b->length || *a->length == *b->length;
        case type_kind::Function:
            if (!compatible(a->base.unqualified(), b->base.unqualified()))
                return false;
            if (!a->prototype || !b->prototype)
                return true;
            if (a->variadic != b->variadic || a->parameters.size() != b->parameters.size())
                return false;
            for (std::size_t i = 0u; i < a->parameters.size(); ++i)
                if (!compatible(a->parameters[i].type.unqualified(), b->parameters[i].type.unqualified()))
                    return false;
            return true;
        case type_kind::Struct:
        case type_kind::Union:
        {
            // (C23: structures with the same tag and members are compatible)
            if (a->record == b->record)
                return true;
            if (a->record == nullptr || b->record == nullptr || a->record->tag != b->record->tag)
                return false;
            // (anonymous: compatible only with those of other translation units with the same members)
            if (a->record->tag.empty() && (a->record->decl == nullptr || b->record->decl == nullptr || a->record->decl->unit == b->record->decl->unit))
                return false;
            if (!a->record->complete || !b->record->complete)
                return true;
            if (a->record->members.size() != b->record->members.size())
                return false;
            for (std::size_t i = 0u; i < a->record->members.size(); ++i)
                if (a->record->members[i].name != b->record->members[i].name ||
                    !compatible(a->record->members[i].type, b->record->members[i].type))
                    return false;
            return true;
        }
        case type_kind::Enum:
            return a->enumeration == b->enumeration || (a->enumeration != nullptr && b->enumeration != nullptr &&
                a->enumeration->tag == b->enumeration->tag && !a->enumeration->tag.empty());
        case type_kind::BitInt:
        case type_kind::UBitInt:
            return a->width == b->width;
        default:
            return false;
        }
    }

    qtype program::composite(qtype aA, qtype aB)
    {
        auto const* a = aA.type;
        auto const* b = aB.type;
        if (a == b || a == nullptr || b == nullptr || a->kind != b->kind)
            return aA;
        if (a->kind == type_kind::Array)
        {
            auto const element = composite(a->base, b->base);
            auto const length = a->length ? a->length : b->length;
            if (element == a->base && length == a->length)
                return aA;
            return qtype{ array_of(element, length, a->vla && b->vla), aA.qualifiers };
        }
        if (a->kind == type_kind::Function)
        {
            if (!a->prototype)
                return aB;
            if (!b->prototype)
                return aA;
            std::vector<parameter_type> parameters = a->parameters;
            for (std::size_t i = 0u; i < parameters.size() && i < b->parameters.size(); ++i)
            {
                parameters[i].type = composite(parameters[i].type, b->parameters[i].type);
                if (parameters[i].name.empty())
                    parameters[i].name = b->parameters[i].name;
            }
            return qtype{ function_of(composite(a->base, b->base), parameters, a->variadic), aA.qualifiers };
        }
        if (a->kind == type_kind::Pointer)
        {
            auto const base = composite(a->base, b->base);
            return qtype{ pointer_to(base), aA.qualifiers };
        }
        return aA;
    }

    std::string program::type_name(qtype aType) const
    {
        std::string q;
        if (aType.qualifiers & QConst) q += "const ";
        if (aType.qualifiers & QVolatile) q += "volatile ";
        if (aType.qualifiers & QRestrict) q += "restrict ";
        if (aType.qualifiers & QAtomic) q += "_Atomic ";
        auto const* t = aType.type;
        if (t == nullptr)
            return q + "?";
        switch (t->kind)
        {
        case type_kind::Void: return q + "void";
        case type_kind::Bool: return q + "bool";
        case type_kind::Char: return q + "char";
        case type_kind::SChar: return q + "signed char";
        case type_kind::UChar: return q + "unsigned char";
        case type_kind::Short: return q + "short";
        case type_kind::UShort: return q + "unsigned short";
        case type_kind::Int: return q + "int";
        case type_kind::UInt: return q + "unsigned int";
        case type_kind::Long: return q + "long";
        case type_kind::ULong: return q + "unsigned long";
        case type_kind::LongLong: return q + "long long";
        case type_kind::ULongLong: return q + "unsigned long long";
        case type_kind::BitInt: return q + "_BitInt(" + std::to_string(t->width) + ")";
        case type_kind::UBitInt: return q + "unsigned _BitInt(" + std::to_string(t->width) + ")";
        case type_kind::Float: return q + "float";
        case type_kind::Double: return q + "double";
        case type_kind::LongDouble: return q + "long double";
        case type_kind::NullPtr: return q + "nullptr_t";
        case type_kind::Enum: return q + "enum " + (t->enumeration != nullptr && !t->enumeration->tag.empty() ? t->enumeration->tag : "<anonymous>");
        case type_kind::Struct: return q + "struct " + (t->record != nullptr && !t->record->tag.empty() ? t->record->tag : "<anonymous>");
        case type_kind::Union: return q + "union " + (t->record != nullptr && !t->record->tag.empty() ? t->record->tag : "<anonymous>");
        case type_kind::Pointer:
            if (t->base && t->base->is_function())
                return type_name(t->base->base) + " (*" + (q.empty() ? "" : " " + q.substr(0u, q.size() - 1u)) + ")(...)";
            return type_name(t->base) + " *" + (q.empty() ? "" : q.substr(0u, q.size() - 1u));
        case type_kind::Array:
            return type_name(t->base) + "[" + (t->length ? std::to_string(*t->length) : t->vla ? "*" : "") + "]";
        case type_kind::Function:
        {
            std::string r = type_name(t->base) + "(";
            for (std::size_t i = 0u; i < t->parameters.size(); ++i)
                r += (i > 0u ? ", " : "") + type_name(t->parameters[i].type);
            if (t->variadic)
                r += t->parameters.empty() ? "..." : ", ...";
            else if (t->parameters.empty() && t->prototype)
                r += "void";
            return r + ")";
        }
        }
        return q + "?";
    }

    void program::layout(record_info& aRecord, node const* aWhere)
    {
        // (members at increasing offsets, each aligned (a union's all at 0); bit-fields packed into storage units of their declared 
        // type's size (as the System V ABI does): a bit-field that would straddle its unit starts a new one; an unnamed bit-field of
        // width 0 starts a new unit; a member's specified alignment (alignas, gnu::aligned) raises its alignment; gnu::packed (the 
        // record's or the member's): aligned to 1 (bit-fields at the next bit); #pragma pack(n): members aligned at most to n (bit-fields 
        // at the next bit, as GCC's are))
        std::uint64_t offset = 0u;
        std::uint64_t alignment = 1u;
        std::uint64_t size = 0u;
        std::uint64_t bitPosition = 0u;     ///< (the next bit (structures: from the record's start))
        bool inBits = false;
        for (std::size_t i = 0u; i < aRecord.members.size(); ++i)
        {
            auto& m = aRecord.members[i];
            auto const memberSize = m.type->is_array() && !m.type->length ? 0u : size_of(m.type, aWhere);
            auto const natural = align_of(m.type);
            bool const packed = aRecord.packed || m.packed || aRecord.pack != 0u;
            auto memberAlign = std::max(aRecord.packed || m.packed ? 1u : natural, m.alignment);
            if (aRecord.pack != 0u)
                memberAlign = std::min(memberAlign, aRecord.pack);
            if (m.bitWidth)
            {
                auto const unitBits = memberSize * 8u;
                if (aRecord.isUnion)
                {
                    m.offset = 0u;
                    m.bitOffset = 0u;
                    size = std::max(size, (static_cast<std::uint64_t>(*m.bitWidth) + 7u) / 8u);
                    size = std::max(size, *m.bitWidth == 0u ? 0u : memberSize);
                    if (!m.name.empty())
                        alignment = std::max(alignment, memberAlign);
                    continue;
                }
                if (!inBits)
                {
                    bitPosition = offset * 8u;
                    inBits = true;
                }
                if (*m.bitWidth == 0u)
                {
                    // (a new unit)
                    bitPosition = (bitPosition + unitBits - 1u) / unitBits * unitBits;
                    continue;
                }
                if (m.alignment != 0u)
                    bitPosition = (bitPosition + memberAlign * 8u - 1u) / (memberAlign * 8u) * (memberAlign * 8u);
                if (packed)
                {
                    // (packed: at the next bit; its storage unit from the byte that holds that bit)
                    m.offset = bitPosition / 8u;
                    m.bitOffset = static_cast<std::uint32_t>(bitPosition % 8u);
                    m.unitSize = 1u;
                    while (m.unitSize * 8u < m.bitOffset + *m.bitWidth)
                        m.unitSize *= 2u;
                    // (16: a 64-bit bit-field in 9 bytes (accessed as 8 bytes and 1))
                    bitPosition += *m.bitWidth;
                    offset = (bitPosition + 7u) / 8u;
                    size = std::max(size, offset);
                    if (!m.name.empty())
                        alignment = std::max(alignment, memberAlign);
                    continue;
                }
                auto unitStart = bitPosition / unitBits * unitBits;
                if (bitPosition + *m.bitWidth > unitStart + unitBits)
                    unitStart = (bitPosition + unitBits - 1u) / unitBits * unitBits, bitPosition = unitStart;
                m.offset = unitStart / 8u;
                m.bitOffset = static_cast<std::uint32_t>(bitPosition - unitStart);
                bitPosition += *m.bitWidth;
                offset = (bitPosition + 7u) / 8u;
                if (!m.name.empty())
                    alignment = std::max(alignment, memberAlign);
                size = std::max(size, m.offset + memberSize);
                continue;
            }
            inBits = false;
            if (aRecord.isUnion)
            {
                m.offset = 0u;
                size = std::max(size, memberSize);
            }
            else
            {
                offset = (offset + memberAlign - 1u) / memberAlign * memberAlign;
                m.offset = offset;
                offset += memberSize;
                size = std::max(size, offset);
            }
            alignment = std::max(alignment, memberAlign);
        }
        alignment = std::max(alignment, aRecord.aligned);
        aRecord.alignment = alignment;
        aRecord.size = (std::max(size, offset) + alignment - 1u) / alignment * alignment;
        aRecord.complete = true;
    }
}
