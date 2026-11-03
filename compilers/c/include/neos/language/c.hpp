/*
  c.hpp

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

// C's constructs: the C concepts (neos.c) emit them with i_compiler::emit_language_construct(Language, construct, source) (each
// concept of the C schema is one); the C compiler builds its syntax tree from them (a construct's children are the constructs 
// emitted before it whose source is within its source; a postfix construct also takes the construct emitted before those as its
// first child)

#include <cstdint>
#include <string_view>

namespace neos::language::c
{
    // (the language as its schema names it)
    inline constexpr char Language[] = "c";

#define NEOS_C_CONSTRUCTS(X) \
    X(Abstract, "c.abstract", false) \
    X(Add, "c.add", true) \
    X(Address, "c.address", false) \
    X(Alignas, "c.alignas", false) \
    X(Alignof, "c.alignof", false) \
    X(And, "c.and", true) \
    X(Array, "c.array", false) \
    X(ArrayStar, "c.array.star", false) \
    X(ArrayStatic, "c.array.static", false) \
    X(Arrow, "c.arrow", true) \
    X(Assign, "c.assign", true) \
    X(AssignAdd, "c.assign.add", true) \
    X(AssignBitAnd, "c.assign.bitand", true) \
    X(AssignBitOr, "c.assign.bitor", true) \
    X(AssignBitXor, "c.assign.bitxor", true) \
    X(AssignDivide, "c.assign.divide", true) \
    X(AssignModulo, "c.assign.modulo", true) \
    X(AssignMultiply, "c.assign.multiply", true) \
    X(AssignShiftLeft, "c.assign.shiftleft", true) \
    X(AssignShiftRight, "c.assign.shiftright", true) \
    X(AssignSubtract, "c.assign.subtract", true) \
    X(Atomic, "c.atomic", false) \
    X(Attribute, "c.attribute", false) \
    X(Attributes, "c.attributes", false) \
    X(BitAnd, "c.bitand", true) \
    X(BitInt, "c.bitint", false) \
    X(BitOr, "c.bitor", true) \
    X(BitXor, "c.bitxor", true) \
    X(Braced, "c.braced", false) \
    X(Break, "c.break", false) \
    X(Call, "c.call", true) \
    X(Case, "c.case", false) \
    X(Cast, "c.cast", false) \
    X(Character, "c.character", false) \
    X(Comma, "c.comma", true) \
    X(Complement, "c.complement", false) \
    X(Compound, "c.compound", false) \
    X(CompoundLiteral, "c.compound.literal", false) \
    X(Conditional, "c.conditional", true) \
    X(Continue, "c.continue", false) \
    X(Declaration, "c.declaration", false) \
    X(Declarator, "c.declarator", false) \
    X(DeclName, "c.declname", false) \
    X(Default, "c.default", false) \
    X(Designated, "c.designated", false) \
    X(DesignatorIndex, "c.designator.index", false) \
    X(DesignatorMember, "c.designator.member", false) \
    X(Divide, "c.divide", true) \
    X(Do, "c.do", false) \
    X(Ellipsis, "c.ellipsis", false) \
    X(Else, "c.else", false) \
    X(Enum, "c.enum", false) \
    X(EnumType, "c.enum.type", false) \
    X(Enumerator, "c.enumerator", false) \
    X(Equal, "c.equal", true) \
    X(ExpressionStatement, "c.expression.statement", false) \
    X(False, "c.false", false) \
    X(For, "c.for", false) \
    X(ForCondition, "c.for.condition", false) \
    X(ForInit, "c.for.init", false) \
    X(ForNext, "c.for.next", false) \
    X(Function, "c.function", false) \
    X(FunctionDefinition, "c.function.definition", false) \
    X(FunctionSpecifier, "c.function.specifier", false) \
    X(Generic, "c.generic", false) \
    X(GenericDefault, "c.generic.default", false) \
    X(GenericType, "c.generic.type", false) \
    X(Goto, "c.goto", false) \
    X(Greater, "c.greater", true) \
    X(GreaterEqual, "c.greaterequal", true) \
    X(Identifier, "c.identifier", false) \
    X(If, "c.if", false) \
    X(Indirection, "c.indirection", false) \
    X(InitDeclarator, "c.init.declarator", false) \
    X(Label, "c.label", false) \
    X(Labeled, "c.labeled", false) \
    X(Less, "c.less", true) \
    X(LessEqual, "c.lessequal", true) \
    X(Member, "c.member", true) \
    X(MemberDeclaration, "c.member.declaration", false) \
    X(MemberDeclarator, "c.member.declarator", false) \
    X(Minus, "c.minus", false) \
    X(Modulo, "c.modulo", true) \
    X(Multiply, "c.multiply", true) \
    X(Name, "c.name", false) \
    X(Not, "c.not", false) \
    X(NotEqual, "c.notequal", true) \
    X(Nullptr, "c.nullptr", false) \
    X(Number, "c.number", false) \
    X(Or, "c.or", true) \
    X(Parameter, "c.parameter", false) \
    X(Paren, "c.paren", false) \
    X(Plus, "c.plus", false) \
    X(Pointer, "c.pointer", false) \
    X(PostDecrement, "c.postdecrement", true) \
    X(PostIncrement, "c.postincrement", true) \
    X(PreDecrement, "c.predecrement", false) \
    X(PreIncrement, "c.preincrement", false) \
    X(Qualifier, "c.qualifier", false) \
    X(Return, "c.return", false) \
    X(ShiftLeft, "c.shiftleft", true) \
    X(ShiftRight, "c.shiftright", true) \
    X(Sizeof, "c.sizeof", false) \
    X(SizeofType, "c.sizeof.type", false) \
    X(StaticAssert, "c.static.assert", false) \
    X(StatementExpression, "c.statement.expression", false) \
    X(Storage, "c.storage", false) \
    X(String, "c.string", false) \
    X(Strings, "c.strings", false) \
    X(Struct, "c.struct", false) \
    X(Subscript, "c.subscript", true) \
    X(Subtract, "c.subtract", true) \
    X(Switch, "c.switch", false) \
    X(Tag, "c.tag", false) \
    X(TranslationUnit, "c.translation.unit", false) \
    X(True, "c.true", false) \
    X(Type, "c.type", false) \
    X(TypeName, "c.type.name", false) \
    X(TypedefName, "c.typedefname", false) \
    X(Typeof, "c.typeof", false) \
    X(While, "c.while", false)

    enum class construct : std::uint32_t
    {
#define NEOS_C_CONSTRUCT_ENUM(aConstruct, aName, aPostfix) aConstruct,
        NEOS_C_CONSTRUCTS(NEOS_C_CONSTRUCT_ENUM)
#undef NEOS_C_CONSTRUCT_ENUM
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
#define NEOS_C_CONSTRUCT_INFO(aConstruct, aName, aPostfix) { construct::aConstruct, aName, #aConstruct, aPostfix },
        NEOS_C_CONSTRUCTS(NEOS_C_CONSTRUCT_INFO)
#undef NEOS_C_CONSTRUCT_INFO
    };

    inline constexpr construct_info const& info(construct aConstruct)
    {
        return Constructs[static_cast<std::uint32_t>(aConstruct)];
    }

    // the runtime's memory (the external memory at index Memory: see neos_rt_host::external_memory_at), which generated code
    // reads and writes: C's objects that are not held in locals (objects of static and thread storage duration, allocated
    // objects, and automatic objects whose address is taken or that are not scalars, in a frame on the C stack) are at their 
    // addresses (offsets) in it. Address 0 is null; the addresses below RuntimeBase are not used; the runtime's own objects 
    // (the standard streams' FILE objects, errno, ...) are at fixed addresses from RuntimeBase; the program's static data begins
    // at StaticBase.
    namespace layout
    {
        inline constexpr std::uint32_t Memory = 4u;                         ///< (the external memory's index: C's)
        inline constexpr std::uint64_t RuntimeBase = 0x10000u;              ///< (the runtime's objects)
        inline constexpr std::uint64_t Errno = RuntimeBase;                 ///< (int errno)
        inline constexpr std::uint64_t Streams = RuntimeBase + 0x100u;      ///< (FILE stdin, stdout, stderr: StreamSize bytes each)
        inline constexpr std::uint64_t StreamSize = 0x40u;
        inline constexpr std::uint64_t StaticBase = 0x20000u;               ///< (the program's static data)
    }
}
