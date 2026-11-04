/*
  model.hpp

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

// C: the syntax tree (built from the constructs that the schema's concepts emit) and the program's model: types, entities (objects,
// functions, typedef names, enumeration constants), scopes, and what analysis annotates the tree with

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <neos/language/c.hpp>
#include "preprocessor.hpp"

namespace neos::compilers::c
{
    using language::c::construct;

    struct c_type;
    struct entity;
    struct translation_unit;
    struct record_info;
    struct enum_info;
    struct function_info;
    struct node;

    // --- types

    enum class type_kind : std::uint8_t
    {
        Void,
        Bool,
        Char,           ///< (plain char: signed)
        SChar,
        UChar,
        Short,
        UShort,
        Int,
        UInt,
        Long,
        ULong,
        LongLong,
        ULongLong,
        BitInt,         ///< (signed _BitInt(N))
        UBitInt,        ///< (unsigned _BitInt(N))
        Float,
        Double,
        LongDouble,
        NullPtr,        ///< (nullptr_t)
        Enum,
        Pointer,
        Array,
        Function,
        Struct,
        Union
    };

    enum qualifier : std::uint8_t
    {
        QConst = 0x01u,
        QVolatile = 0x02u,
        QRestrict = 0x04u,
        QAtomic = 0x08u
    };

    // a qualified type
    struct qtype
    {
        c_type const* type = nullptr;
        std::uint8_t qualifiers = 0u;

        qtype() = default;
        qtype(c_type const* aType, std::uint8_t aQualifiers = 0u) : type{ aType }, qualifiers{ aQualifiers } {}
        c_type const* operator->() const { return type; }
        explicit operator bool() const { return type != nullptr; }
        bool is_const() const { return (qualifiers & QConst) != 0u; }
        bool is_volatile() const { return (qualifiers & QVolatile) != 0u; }
        qtype unqualified() const { return qtype{ type }; }
        qtype with(std::uint8_t aQualifiers) const { return qtype{ type, static_cast<std::uint8_t>(qualifiers | aQualifiers) }; }
        friend bool operator==(qtype const& a, qtype const& b) { return a.type == b.type && a.qualifiers == b.qualifiers; }
    };

    struct parameter_type
    {
        qtype type;
        std::string name;
    };

    struct c_type
    {
        type_kind kind = type_kind::Void;
        // (pointer: the type pointed to; array: the element type; function: the result)
        qtype base;
        // (array: its length (none: unknown, or a variable length array's (vla)); bit-precise integer: its width)
        std::optional<std::uint64_t> length;
        bool vla = false;
        node* vlaSize = nullptr;                ///< (a variable length array's size expression; null: [*])
        std::uint32_t width = 0u;
        // (function)
        std::vector<parameter_type> parameters;
        bool variadic = false;
        bool prototype = true;                  ///< (false: an old style declaration's (no information about the parameters))
        // (structure, union, enumeration)
        record_info* record = nullptr;
        enum_info* enumeration = nullptr;

        bool is(type_kind aKind) const { return kind == aKind; }
        bool is_integer() const;                ///< (an integer type: char, signed and unsigned integer types, enumerated types, bool)
        bool is_signed() const;
        bool is_unsigned() const { return is_integer() && !is_signed(); }
        bool is_floating() const { return kind == type_kind::Float || kind == type_kind::Double || kind == type_kind::LongDouble; }
        bool is_arithmetic() const { return is_integer() || is_floating(); }
        bool is_pointer() const { return kind == type_kind::Pointer; }
        bool is_scalar() const { return is_arithmetic() || is_pointer() || kind == type_kind::NullPtr; }
        bool is_array() const { return kind == type_kind::Array; }
        bool is_function() const { return kind == type_kind::Function; }
        bool is_record() const { return kind == type_kind::Struct || kind == type_kind::Union; }
        bool is_aggregate() const { return is_array() || is_record(); }
        bool is_void() const { return kind == type_kind::Void; }
        bool is_complete() const;
        bool is_object() const { return !is_function(); }
        bool is_variably_modified() const;
    };

    // a member of a structure or union
    struct member_info
    {
        std::string name;                       ///< (empty: an anonymous structure or union member, or an unnamed bit-field)
        qtype type;
        std::uint64_t offset = 0u;
        std::optional<std::uint32_t> bitWidth;  ///< (a bit-field: its width; bitOffset: its first bit within the storage unit at offset)
        std::uint32_t bitOffset = 0u;
        std::uint32_t unitSize = 0u;            ///< (a bit-field's storage unit's size (0: its type's; a packed bit-field's: 1, 2, 4 or 8))
        std::uint64_t alignment = 0u;           ///< (alignas or gnu::aligned: its alignment (0: its type's))
        bool packed = false;                    ///< (gnu::packed: aligned to 1 (unless alignment))
        node* decl = nullptr;
    };

    struct record_info
    {
        std::string tag;
        bool isUnion = false;
        bool complete = false;
        bool flexible = false;                  ///< (a structure whose last member is a flexible array member)
        bool packed = false;                    ///< (gnu::packed: its members aligned to 1 (unless their alignment is specified))
        std::uint64_t aligned = 0u;             ///< (gnu::aligned: its alignment at least this)
        std::uint64_t pack = 0u;                ///< (#pragma pack: its members' alignment at most this (0: no limit))
        std::vector<member_info> members;
        std::uint64_t size = 0u;
        std::uint64_t alignment = 1u;
        node* decl = nullptr;
        // (a member by name, also within anonymous members: the members' path from the record to it)
        bool find(std::string const& aName, std::vector<member_info const*>& aPath) const;
    };

    struct enum_info
    {
        std::string tag;
        bool complete = false;
        bool fixed = false;                     ///< (an underlying type was specified)
        qtype underlying;                       ///< (an integer type)
        node* decl = nullptr;
    };

    // --- values (of constant expressions)

    struct constant
    {
        enum class kind : std::uint8_t { None, Integer, Floating, Address };
        kind k = kind::None;
        std::uint64_t integer = 0u;             ///< (Integer: its bits (sign extended from its type's width); Address: the offset)
        double floating = 0.0;
        entity* object = nullptr;               ///< (Address: the object or function whose address it is (null: a string literal's, or an absolute address))
        node* literal = nullptr;                ///< (Address: a string literal's (or a compound literal's) node, if object is null)
        bool is_integer() const { return k == kind::Integer; }
        std::int64_t s() const { return static_cast<std::int64_t>(integer); }
    };

    // --- entities

    enum class storage_class : std::uint8_t { None, Auto, Register, Static, Extern, Typedef, Constexpr, ThreadLocal };
    enum class linkage : std::uint8_t { None, Internal, External };
    enum class entity_kind : std::uint8_t { Object, Function, Typedef, EnumConstant };

    // where an object's storage is
    enum class object_storage : std::uint8_t
    {
        Unassigned,
        Static,             ///< (static storage duration: at its address in the static data)
        Frame,              ///< (automatic, in the function's frame: at its offset from the frame pointer)
        Local,              ///< (automatic, a scalar whose address is not taken: a WebAssembly local)
        Dynamic             ///< (a variable length array: its address in a local (allocated on the C stack when its declaration is reached))
    };

    struct entity
    {
        entity_kind kind = entity_kind::Object;
        std::string name;
        qtype type;
        storage_class storage = storage_class::None;
        enum linkage linkage = linkage::None;
        bool defined = false;                   ///< (an object: a definition (not a tentative one); a function: its body)
        bool tentative = false;
        bool parameter = false;
        bool addressTaken = false;
        bool used = false;
        bool isInline = false;
        bool noreturn = false;
        bool isConstexpr = false;
        node* decl = nullptr;                   ///< (its (first) declaration)
        node* definition = nullptr;             ///< (a function's definition; an object's initializer's declaration)
        node* initializer = nullptr;
        entity* cleanup = nullptr;              ///< (an automatic object's gnu::cleanup function: called with its address when its scope is left)
        translation_unit* unit = nullptr;
        // (an object's storage)
        object_storage where = object_storage::Unassigned;
        std::uint64_t address = 0u;             ///< (Static: its address)
        std::int64_t frameOffset = 0;           ///< (Frame: its offset)
        std::uint32_t local = 0u;               ///< (Local: its WebAssembly local; Dynamic: the local holding its address)
        std::uint32_t sizeLocal = 0u;           ///< (Dynamic: the local holding its size)
        std::uint32_t markLocal = 0u;           ///< (a variable length array: the stack pointer before it was allocated (0: not yet, in its block))
        // (an enumeration constant's value; a constexpr object's value)
        std::int64_t value = 0;
        std::optional<constant> constantValue;
        // (a function)
        function_info* function = nullptr;
        entity* canonical = nullptr;            ///< (the entity that this one's declarations refer to (the same object or function), if not itself)
        entity* self() { return canonical != nullptr ? canonical : this; }
    };

    struct function_info
    {
        entity* ent = nullptr;
        std::uint32_t index = 0u;               ///< (its WebAssembly function's index)
        bool defined = false;                   ///< (its WebAssembly function has been defined)
        bool generated = false;
        bool hasCleanups = false;               ///< (it has an object with gnu::cleanup)
        bool computedGoto = false;              ///< (it has a goto to a label's address (GNU))
        std::vector<node*> labelValues;         ///< (the labeled statements whose labels' addresses it takes: a label's address is its index + 1)
        bool runtime = false;                   ///< (a function of the C library (the runtime plugin's))
        bool linked = false;                    ///< (a function that another translation unit of a linked program defines (an import 
                                                ///< of its name: see bytecode::LinkNamespace))
        std::string runtimeName;
        std::optional<std::uint64_t> descriptor;    ///< (the address of its descriptor (its function's index), if its address is taken)
        std::optional<std::uint32_t> wrapper;   ///< (a function that calls it, for its descriptor (a function of the runtime's cannot be 
                                                ///< called indirectly))
        std::vector<entity*> parameters;
        std::vector<entity*> locals;            ///< (its objects with automatic storage duration (and its block scope static objects))
        node* body = nullptr;
        bool usesSetjmp = false;
        bool hasGoto = false;
        bool irregularSwitch = false;           ///< (a switch statement's case labels are not all in its body's top level)
        bool usesAlloca = false;
        bool hasVla = false;
    };

    // --- the syntax tree

    // a syntax tree node: its construct, its source (in the compiler's copy of its translation unit's preprocessed text) and its 
    // children; analysis annotates it
    struct node
    {
        construct kind;
        std::string_view source;
        std::vector<node*> children;
        node* parent = nullptr;
        translation_unit* unit = nullptr;
        // (analysis: an expression's type and value category; a name's entity; a member's path)
        qtype type;
        bool lvalue = false;
        bool bitField = false;
        entity* ent = nullptr;
        std::vector<member_info const*> members;    ///< (a member access: the member, as its path from the record)
        std::optional<constant> value;          ///< (an integer constant expression's (or a literal's) value)
        std::uint64_t literalAddress = 0u;      ///< (a string literal (or compound literal at file scope): its static address)
        node* target = nullptr;                 ///< (a break, continue, case or default: its statement; goto: its label)
        node* selected = nullptr;               ///< (a generic selection: the expression selected)
        std::string bytes;                      ///< (a string literal: its array's bytes (its terminating null included))
        std::uint32_t builtin = 0u;             ///< (a call of a builtin function: which (see program::builtin_kind))
        std::uint32_t mark = 0u;

        bool is(construct aKind) const { return kind == aKind; }
        node* child(std::size_t aIndex) const { return aIndex < children.size() ? children[aIndex] : nullptr; }
        std::size_t size() const { return children.size(); }
        std::string text() const { return std::string{ source }; }
        node* first(construct aKind) const { for (auto* c : children) if (c->kind == aKind) return c; return nullptr; }
        std::vector<node*> all(construct aKind) const { std::vector<node*> r; for (auto* c : children) if (c->kind == aKind) r.push_back(c); return r; }
    };

    // a translation unit: its source file and the compiler's copy of its preprocessed text
    struct translation_unit
    {
        std::string path;
        std::string text;                       ///< (the preprocessor's output, as the parser has it)
        preprocessor::output output;
        std::vector<std::string> files;         ///< (the files that it includes, by the preprocessor's indexes)
        node* root = nullptr;
        bool prelude = false;
        std::uint32_t index = 0u;
        source_location location_of(std::string_view aSource) const;
        std::string file_of(source_location const& aWhere) const { return aWhere.file < files.size() ? files[aWhere.file] : path; }
    };

    // the text of a name construct (a typedef name without its mark)
    inline std::string name_of(node const* aNode)
    {
        auto s = aNode->source;
        if (!s.empty() && s.front() == '@')
            s.remove_prefix(1u);
        return std::string{ s };
    }
}
