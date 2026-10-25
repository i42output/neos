/*
  javascript.hpp

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

// JavaScript's constructs: the JavaScript concepts (neos.javascript) emit them with i_compiler::emit_language_construct(Language,
// construct, source) (each concept of the JavaScript schema is one); the JavaScript compiler builds its syntax tree from them (a
// construct's children are the constructs emitted before it whose source is within its source; a postfix construct also takes
// the construct emitted before those as its first child)

#include <cstdint>
#include <string_view>

namespace neos::language::javascript
{
    // (the language as its schema names it)
    inline constexpr char Language[] = "javascript";

#define NEOS_JAVASCRIPT_CONSTRUCTS(X) \
    X(Script, "javascript.script", false) \
    X(Identifier, "javascript.identifier", false) \
    X(Binding, "javascript.binding", false) \
    X(Label, "javascript.label", false) \
    X(Name, "javascript.name", false) \
    X(Private, "javascript.private", false) \
    X(Null, "javascript.null", false) \
    X(True, "javascript.true", false) \
    X(False, "javascript.false", false) \
    X(This, "javascript.this", false) \
    X(Number, "javascript.number", false) \
    X(String, "javascript.string", false) \
    X(Template, "javascript.template", false) \
    X(TemplateString, "javascript.template.string", false) \
    X(RegExp, "javascript.regexp", false) \
    X(Parenthesized, "javascript.parenthesized", false) \
    X(Array, "javascript.array", false) \
    X(Hole, "javascript.hole", false) \
    X(Spread, "javascript.spread", false) \
    X(Object, "javascript.object", false) \
    X(Property, "javascript.property", false) \
    X(Shorthand, "javascript.shorthand", false) \
    X(ShorthandDefault, "javascript.shorthand.default", false) \
    X(Computed, "javascript.computed", false) \
    X(Method, "javascript.method", false) \
    X(Getter, "javascript.getter", false) \
    X(Setter, "javascript.setter", false) \
    X(GeneratorMethod, "javascript.method.generator", false) \
    X(AsyncMethod, "javascript.method.async", false) \
    X(AsyncGeneratorMethod, "javascript.method.async.generator", false) \
    X(FunctionExpression, "javascript.function.expression", false) \
    X(AsyncFunctionExpression, "javascript.function.expression.async", false) \
    X(Parameters, "javascript.parameters", false) \
    X(Body, "javascript.body", false) \
    X(Arrow, "javascript.arrow", false) \
    X(AsyncArrow, "javascript.arrow.async", false) \
    X(ClassExpression, "javascript.class.expression", false) \
    X(Extends, "javascript.extends", false) \
    X(Static, "javascript.static", false) \
    X(Field, "javascript.field", false) \
    X(StaticBlock, "javascript.static.block", false) \
    X(Dot, "javascript.dot", true) \
    X(DotPrivate, "javascript.dot.private", true) \
    X(Index, "javascript.index", true) \
    X(Tagged, "javascript.tagged", true) \
    X(SuperDot, "javascript.super.dot", false) \
    X(SuperIndex, "javascript.super.index", false) \
    X(NewTarget, "javascript.new.target", false) \
    X(ImportMeta, "javascript.import.meta", false) \
    X(New, "javascript.new", false) \
    X(Arguments, "javascript.arguments", false) \
    X(Call, "javascript.call", true) \
    X(SuperCall, "javascript.super.call", false) \
    X(ImportCall, "javascript.import.call", false) \
    X(Optional, "javascript.optional", false) \
    X(OptionalCall, "javascript.optional.call", true) \
    X(OptionalIndex, "javascript.optional.index", true) \
    X(OptionalDot, "javascript.optional.dot", true) \
    X(OptionalPrivate, "javascript.optional.private", true) \
    X(PreIncrement, "javascript.preincrement", false) \
    X(PreDecrement, "javascript.predecrement", false) \
    X(PostIncrement, "javascript.postincrement", true) \
    X(PostDecrement, "javascript.postdecrement", true) \
    X(Delete, "javascript.delete", false) \
    X(Void, "javascript.void", false) \
    X(TypeOf, "javascript.typeof", false) \
    X(Plus, "javascript.plus", false) \
    X(Negate, "javascript.negate", false) \
    X(BitNot, "javascript.bitnot", false) \
    X(Not, "javascript.not", false) \
    X(Await, "javascript.await", false) \
    X(Exponent, "javascript.exponent", true) \
    X(Multiply, "javascript.multiply", true) \
    X(Divide, "javascript.divide", true) \
    X(Remainder, "javascript.remainder", true) \
    X(Add, "javascript.add", true) \
    X(Subtract, "javascript.subtract", true) \
    X(ShiftLeft, "javascript.shiftleft", true) \
    X(ShiftRight, "javascript.shiftright", true) \
    X(ShiftRightUnsigned, "javascript.shiftrightunsigned", true) \
    X(LessThan, "javascript.lessthan", true) \
    X(GreaterThan, "javascript.greaterthan", true) \
    X(LessEqual, "javascript.lessequal", true) \
    X(GreaterEqual, "javascript.greaterequal", true) \
    X(InstanceOf, "javascript.instanceof", true) \
    X(In, "javascript.in", true) \
    X(PrivateIn, "javascript.private.in", false) \
    X(StrictEqual, "javascript.strictequal", true) \
    X(StrictNotEqual, "javascript.strictnotequal", true) \
    X(Equal, "javascript.equal", true) \
    X(NotEqual, "javascript.notequal", true) \
    X(BitAnd, "javascript.bitand", true) \
    X(BitXor, "javascript.bitxor", true) \
    X(BitOr, "javascript.bitor", true) \
    X(And, "javascript.and", true) \
    X(Or, "javascript.or", true) \
    X(Coalesce, "javascript.coalesce", true) \
    X(Conditional, "javascript.conditional", true) \
    X(Assign, "javascript.assign", false) \
    X(Yield, "javascript.yield", false) \
    X(Comma, "javascript.comma", true) \
    X(Initializer, "javascript.initializer", false) \
    X(ObjectPattern, "javascript.pattern.object", false) \
    X(PatternProperty, "javascript.pattern.property", false) \
    X(Rest, "javascript.pattern.rest", false) \
    X(ArrayPattern, "javascript.pattern.array", false) \
    X(Block, "javascript.block", false) \
    X(Empty, "javascript.empty", false) \
    X(ExpressionStatement, "javascript.expression", false) \
    X(Debugger, "javascript.debugger", false) \
    X(Var, "javascript.var", false) \
    X(Let, "javascript.let", false) \
    X(Const, "javascript.const", false) \
    X(Using, "javascript.using", false) \
    X(AwaitUsing, "javascript.await.using", false) \
    X(Declarator, "javascript.declarator", false) \
    X(If, "javascript.if", false) \
    X(Do, "javascript.do", false) \
    X(While, "javascript.while", false) \
    X(For, "javascript.for", false) \
    X(ForInit, "javascript.for.init", false) \
    X(ForTest, "javascript.for.test", false) \
    X(ForUpdate, "javascript.for.update", false) \
    X(ForIn, "javascript.for.in", false) \
    X(ForOf, "javascript.for.of", false) \
    X(ForAwaitOf, "javascript.for.await.of", false) \
    X(ForVar, "javascript.for.var", false) \
    X(ForLet, "javascript.for.let", false) \
    X(ForConst, "javascript.for.const", false) \
    X(ForUsing, "javascript.for.using", false) \
    X(ForAwaitUsing, "javascript.for.await.using", false) \
    X(Continue, "javascript.continue", false) \
    X(Break, "javascript.break", false) \
    X(Return, "javascript.return", false) \
    X(Throw, "javascript.throw", false) \
    X(With, "javascript.with", false) \
    X(Switch, "javascript.switch", false) \
    X(Case, "javascript.case", false) \
    X(Default, "javascript.default", false) \
    X(Labelled, "javascript.labelled", false) \
    X(Try, "javascript.try", false) \
    X(Catch, "javascript.catch", false) \
    X(Finally, "javascript.finally", false) \
    X(Function, "javascript.function", false) \
    X(AsyncFunction, "javascript.function.async", false) \
    X(Class, "javascript.class", false) \
    X(Import, "javascript.import", false) \
    X(ImportDefault, "javascript.import.default", false) \
    X(ImportNamespace, "javascript.import.namespace", false) \
    X(ImportNamed, "javascript.import.named", false) \
    X(ImportSpecifier, "javascript.import.specifier", false) \
    X(ImportAttributes, "javascript.import.attributes", false) \
    X(Export, "javascript.export", false) \
    X(ExportDefault, "javascript.export.default", false) \
    X(ExportNamed, "javascript.export.named", false) \
    X(ExportSpecifier, "javascript.export.specifier", false)

    enum class construct : std::uint32_t
    {
#define NEOS_JAVASCRIPT_CONSTRUCT_ENUM(aConstruct, aName, aPostfix) aConstruct,
        NEOS_JAVASCRIPT_CONSTRUCTS(NEOS_JAVASCRIPT_CONSTRUCT_ENUM)
#undef NEOS_JAVASCRIPT_CONSTRUCT_ENUM
        COUNT
    };

    struct construct_info
    {
        construct id;
        std::string_view conceptName; ///< (the concept's name)
        std::string_view name;      ///< (the construct's name)
        bool postfix;
    };

    inline constexpr construct_info Constructs[] =
    {
#define NEOS_JAVASCRIPT_CONSTRUCT_INFO(aConstruct, aName, aPostfix) { construct::aConstruct, aName, #aConstruct, aPostfix },
        NEOS_JAVASCRIPT_CONSTRUCTS(NEOS_JAVASCRIPT_CONSTRUCT_INFO)
#undef NEOS_JAVASCRIPT_CONSTRUCT_INFO
    };

    inline constexpr construct_info const& info(construct aConstruct)
    {
        return Constructs[static_cast<std::uint32_t>(aConstruct)];
    }

    // the runtime's heap as generated code reads it: the runtime's region (its cells and the storage that code reads) is an external 
    // memory (see neos_rt_host::external_memory_at) of index Memory; addresses are offsets in the region. A value's cell (its handle: 
    // the value's low 32 bits) is found by the handle table (at RegionHandles: a u64 per handle, the cell's offset). The offsets of 
    // the fields that code reads (the runtime checks them when it starts)
    namespace layout
    {
        inline constexpr std::uint32_t Memory = 2u;                 ///< (the external memory's index: JavaScript's)
        inline constexpr std::uint32_t RegionHandles = 65536u;      ///< (the handle table's offset (fixed: the region's second page))
        inline constexpr std::uint32_t ObjectProto = 24u;           ///< (value: an object's prototype)
        inline constexpr std::uint32_t ObjectShape = 32u;           ///< (u64: its properties' shape (0: none))
        inline constexpr std::uint32_t ObjectProperties = 40u;      ///< (u64: the offset of its properties (property))
        inline constexpr std::uint32_t ObjectPropertyCount = 48u;   ///< (u64: their number)
        inline constexpr std::uint32_t PropertySize = 32u;          ///< (a property: its key, its value (an accessor's getter), its setter, its attributes)
        inline constexpr std::uint32_t PropertyKey = 0u;
        inline constexpr std::uint32_t PropertyData = 8u;
        inline constexpr std::uint32_t PropertyAttributes = 24u;    ///< (u8)
        inline constexpr std::uint32_t EnvironmentParent = 24u;     ///< (value: an environment's parent)
        inline constexpr std::uint32_t EnvironmentSlots = 32u;      ///< (u64: the offset of its slots (values))
        inline constexpr std::uint32_t FunctionEnvironment = 208u;   ///< (value: a function's environment (its closure's))
        inline constexpr std::uint32_t AttributeWritable = 0x01u;
        inline constexpr std::uint32_t AttributeAccessor = 0x08u;
        // (a program's property cache sites (a get or a set of a property; numbered by the compiler from 0): SiteEntries entries each 
        // (the most recently used first), in a table that the runtime makes for the program (property_sites), whose offset is in a 
        // global of the program's; an entry's fields are at these offsets in it)
        inline constexpr std::uint32_t SiteEntries = 2u;
        inline constexpr std::uint32_t SiteEntrySize = 80u;
        inline constexpr std::uint32_t SiteSize = SiteEntries * SiteEntrySize;
        inline constexpr std::uint32_t SiteKey = 0u;                ///< (value: the property's key)
        inline constexpr std::uint32_t SiteShape = 8u;              ///< (u64: the receiver's shape)
        inline constexpr std::uint32_t SiteKind = 16u;              ///< (u8: SiteOwn, SitePrototype ...)
        inline constexpr std::uint32_t SiteDepth = 17u;             ///< (u8: the prototypes that are checked)
        inline constexpr std::uint32_t SitePosition = 20u;          ///< (u32: the property's position in its holder's properties)
        inline constexpr std::uint32_t SitePrototype = 24u;         ///< (value: the receiver's prototype; then its shape (u64), the
                                                                    ///< prototype's prototype and its shape)
        inline constexpr std::uint32_t SiteAdded = 56u;             ///< (u64: a set that adds the property: the receiver's shape after)
        inline constexpr std::uint32_t SiteFunction = 64u;          ///< (value: the property's value, a function of the program's ...)
        inline constexpr std::uint32_t SiteNumber = 72u;            ///< (i32: ... whose number (for the dispatcher) this is (else -1))
        enum : std::uint8_t
        {
            SiteNone = 0u,
            SiteOwn = 1u,           ///< (the receiver's own data property)
            SiteHolder = 2u,        ///< (a data property of a prototype (depth 1: the receiver's prototype))
            SiteString = 3u,        ///< (a data property of %String.prototype% (the receiver a string))
            SiteAdd = 4u            ///< (a set that adds a data property to the receiver)
        };
    }
}
