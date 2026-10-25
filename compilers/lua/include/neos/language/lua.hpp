/*
  lua.hpp

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

#pragma once

// Lua's constructs: the Lua concepts (neos.lua) emit them with i_compiler::emit_language_construct(Language, construct, source)
// (each concept of the Lua schema is one); the Lua compiler builds its syntax tree from them (a construct's children are the
// constructs emitted before it whose source is within its source; a postfix construct also takes the construct emitted before
// those as its first child); and its values, as the compiler and the runtime plugin (neos.runtime.lua) represent them

#include <cstdint>
#include <string_view>

namespace neos::language::lua
{
    // (the language as its schema names it)
    inline constexpr char Language[] = "lua";

#define NEOS_LUA_CONSTRUCTS(X) \
    X(Chunk, "lua.chunk", false) \
    X(Identifier, "lua.identifier", false) \
    X(Name, "lua.name", false) \
    X(Binding, "lua.binding", false) \
    X(Nil, "lua.nil", false) \
    X(True, "lua.true", false) \
    X(False, "lua.false", false) \
    X(Vararg, "lua.vararg", false) \
    X(Number, "lua.number", false) \
    X(String, "lua.string", false) \
    X(Or, "lua.or", true) \
    X(And, "lua.and", true) \
    X(Less, "lua.less", true) \
    X(Greater, "lua.greater", true) \
    X(LessEqual, "lua.lessequal", true) \
    X(GreaterEqual, "lua.greaterequal", true) \
    X(Equal, "lua.equal", true) \
    X(NotEqual, "lua.notequal", true) \
    X(BitOr, "lua.bitor", true) \
    X(BitXor, "lua.bitxor", true) \
    X(BitAnd, "lua.bitand", true) \
    X(ShiftLeft, "lua.shiftleft", true) \
    X(ShiftRight, "lua.shiftright", true) \
    X(Concat, "lua.concat", true) \
    X(Add, "lua.add", true) \
    X(Subtract, "lua.subtract", true) \
    X(Multiply, "lua.multiply", true) \
    X(Divide, "lua.divide", true) \
    X(FloorDivide, "lua.floordivide", true) \
    X(Modulo, "lua.modulo", true) \
    X(Not, "lua.not", false) \
    X(Negate, "lua.negate", false) \
    X(Length, "lua.length", false) \
    X(BitNot, "lua.bitnot", false) \
    X(Power, "lua.power", true) \
    X(Parenthesized, "lua.parenthesized", false) \
    X(Dot, "lua.dot", true) \
    X(Index, "lua.index", true) \
    X(Method, "lua.method", true) \
    X(Call, "lua.call", true) \
    X(Function, "lua.function", false) \
    X(Parameters, "lua.parameters", false) \
    X(Table, "lua.table", false) \
    X(FieldKeyed, "lua.field.keyed", false) \
    X(FieldNamed, "lua.field.named", false) \
    X(Empty, "lua.empty", false) \
    X(Label, "lua.label", false) \
    X(Break, "lua.break", false) \
    X(Goto, "lua.goto", false) \
    X(Do, "lua.do", false) \
    X(While, "lua.while", false) \
    X(Repeat, "lua.repeat", false) \
    X(If, "lua.if", false) \
    X(ElseIf, "lua.elseif", false) \
    X(Else, "lua.else", false) \
    X(NumericFor, "lua.for.numeric", false) \
    X(GenericFor, "lua.for.generic", false) \
    X(FunctionStatement, "lua.function.statement", false) \
    X(FunctionName, "lua.funcname", false) \
    X(MethodName, "lua.methodname", false) \
    X(LocalFunction, "lua.local.function", false) \
    X(Local, "lua.local", false) \
    X(AttributeName, "lua.attname", false) \
    X(Assign, "lua.assign", false) \
    X(VariableList, "lua.varlist", false) \
    X(CallStatement, "lua.callstatement", false) \
    X(Return, "lua.return", false)

    enum class construct : std::uint32_t
    {
#define NEOS_LUA_CONSTRUCT_ENUM(aConstruct, aName, aPostfix) aConstruct,
        NEOS_LUA_CONSTRUCTS(NEOS_LUA_CONSTRUCT_ENUM)
#undef NEOS_LUA_CONSTRUCT_ENUM
        COUNT
    };

    struct construct_info
    {
        construct id;
        std::string_view conceptName;   ///< (the concept's name)
        std::string_view name;          ///< (the construct's name)
        bool postfix;
    };

    inline constexpr construct_info Constructs[] =
    {
#define NEOS_LUA_CONSTRUCT_INFO(aConstruct, aName, aPostfix) { construct::aConstruct, aName, #aConstruct, aPostfix },
        NEOS_LUA_CONSTRUCTS(NEOS_LUA_CONSTRUCT_INFO)
#undef NEOS_LUA_CONSTRUCT_INFO
    };

    inline constexpr construct_info const& info(construct aConstruct)
    {
        return Constructs[static_cast<std::uint32_t>(aConstruct)];
    }

    // values: NaN-boxed i64s (the runtime plugin, neos.runtime.lua, implements them): a float is its IEEE double (a NaN is one
    // whose payload is 0: 0x7FF8... or 0xFFF8...); the others are tagged (their top 16 bits): nil, false and true; an integer that
    // fits in 48 bits (its low 48 bits, sign extended when it is unboxed; a larger one is a heap cell (TagOther)); and the heap's 
    // cells by their handles (32 bits)
    namespace values
    {
        inline constexpr std::uint64_t TagMask = 0xFFFF000000000000ull;
        inline constexpr std::uint64_t TagSpecial = 0xFFF9000000000000ull;
        inline constexpr std::uint64_t TagInteger = 0xFFFA000000000000ull;
        inline constexpr std::uint64_t TagString = 0xFFFB000000000000ull;
        inline constexpr std::uint64_t TagTable = 0xFFFC000000000000ull;
        inline constexpr std::uint64_t TagFunction = 0xFFFD000000000000ull;
        inline constexpr std::uint64_t TagOther = 0xFFFE000000000000ull;     ///< (userdata, threads, upvalue boxes, varargs and large integers)
        inline constexpr std::uint64_t Nil = TagSpecial | 0u;
        inline constexpr std::uint64_t False = TagSpecial | 1u;
        inline constexpr std::uint64_t True = TagSpecial | 2u;
        inline constexpr std::uint64_t Empty = TagSpecial | 4u;             ///< (no value: e.g. an absent table entry (never a Lua value))
        inline constexpr std::uint64_t Exception = TagSpecial | 5u;         ///< (a call's result: an error is pending in the runtime)
        inline constexpr std::uint64_t Dispatch = TagSpecial | 0x0000010000000000ull;   ///< (| a function's number: lua::invoke's result for the program's own Lua function (the dispatcher calls it))
        inline constexpr std::uint64_t PayloadMask = 0x0000FFFFFFFFFFFFull;
        inline constexpr std::int64_t MinInline = -(std::int64_t{ 1 } << 47);
        inline constexpr std::int64_t MaxInline = (std::int64_t{ 1 } << 47) - 1;
    }

    // the runtime's cells as generated code reads them: a cell's value's payload is its offset in the runtime's heap, which is the
    // external memory (memory 1: see neos_rt_host::external_memory); the offsets of the fields that code reads (the runtime checks them)
    namespace layout
    {
        inline constexpr std::uint32_t StringHash = 16u;            ///< (u64: an interned string's hash)
        inline constexpr std::uint32_t TableArray = 16u;            ///< (u64: the offset of the array part's values (the keys 1 to n))
        inline constexpr std::uint32_t TableArraySize = 24u;        ///< (u64: n)
        inline constexpr std::uint32_t TableArrayCapacity = 32u;    ///< (u64: its capacity (values))
        inline constexpr std::uint32_t TableMetatable = 40u;        ///< (value)
        inline constexpr std::uint32_t TableNodes = 48u;            ///< (u64: the offset of the hash part's nodes)
        inline constexpr std::uint32_t TableNodesSize = 56u;        ///< (u64: the number of nodes (a power of 2, or 0))
        inline constexpr std::uint32_t TableUsed = 136u;            ///< (u64: the number of nodes whose keys are not Empty)
        inline constexpr std::uint32_t NodeSize = 16u;              ///< (a node: its key (value) and its value (value))
        inline constexpr std::uint32_t NodeValue = 8u;
        inline constexpr std::uint32_t BoxValue = 16u;              ///< (value: an upvalue's)
        inline constexpr std::uint32_t FunctionUpvalues = 16u;      ///< (u64: the offset of a Lua function's upvalues (boxes))
        inline constexpr std::uint32_t FunctionEnvironment = 40u;   ///< (value: its environment (a box), or nil)
    }
}
