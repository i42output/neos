/*
  ada.hpp

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

// Ada's constructs: the Ada concepts (neos.ada) emit them with i_compiler::emit_language_construct(Language, construct, source)
// (each concept of the Ada schema is one); the Ada compiler builds its syntax tree from them (a construct's children are the
// constructs emitted before it whose source is within its source; a postfix construct also takes the construct emitted before
// those as its first child)

#include <cstdint>
#include <string_view>

namespace neos::language::ada
{
    // (the language as its schema names it)
    inline constexpr char Language[] = "ada";

#define NEOS_ADA_CONSTRUCTS(X) \
    X(Abort, "ada.abort", false) \
    X(Abs, "ada.abs", false) \
    X(Accept, "ada.accept", false) \
    X(AccessDefinition, "ada.access.definition", false) \
    X(Add, "ada.add", true) \
    X(Aggregate, "ada.aggregate", false) \
    X(AggregateBracket, "ada.aggregate.bracket", false) \
    X(AggregateDelta, "ada.aggregate.delta", false) \
    X(AggregateExtension, "ada.aggregate.extension", false) \
    X(AggregateNullRecord, "ada.aggregate.nullrecord", false) \
    X(All, "ada.all", true) \
    X(Allocator, "ada.allocator", false) \
    X(Alternative, "ada.alternative", false) \
    X(And, "ada.and", true) \
    X(AndThen, "ada.andthen", true) \
    X(Apply, "ada.apply", true) \
    X(Arguments, "ada.arguments", false) \
    X(Aspect, "ada.aspect", false) \
    X(Aspects, "ada.aspects", false) \
    X(Assignment, "ada.assignment", false) \
    X(Association, "ada.association", false) \
    X(Attribute, "ada.attribute", true) \
    X(Barrier, "ada.barrier", false) \
    X(Block, "ada.block", false) \
    X(Box, "ada.box", false) \
    X(Call, "ada.call", false) \
    X(Case, "ada.case", false) \
    X(CaseExpression, "ada.case.expression", false) \
    X(Character, "ada.character", false) \
    X(Choices, "ada.choices", false) \
    X(Chunk, "ada.chunk", false) \
    X(Code, "ada.code", false) \
    X(Compilation, "ada.compilation", false) \
    X(Component, "ada.component", false) \
    X(ComponentDefinition, "ada.component.definition", false) \
    X(Components, "ada.components", false) \
    X(Concat, "ada.concat", true) \
    X(ConstraintDelta, "ada.constraint.delta", false) \
    X(ConstraintDigits, "ada.constraint.digits", false) \
    X(ConstraintIndex, "ada.constraint.index", false) \
    X(ConstraintRange, "ada.constraint.range", false) \
    X(Context, "ada.context", false) \
    X(Declarations, "ada.declarations", false) \
    X(DeclareExpression, "ada.declare.expression", false) \
    X(Defining, "ada.defining", false) \
    X(DefiningCharacter, "ada.defining.character", false) \
    X(DefiningOperator, "ada.defining.operator", false) \
    X(Delay, "ada.delay", false) \
    X(Discriminant, "ada.discriminant", false) \
    X(Discriminants, "ada.discriminants", false) \
    X(DiscriminantsUnknown, "ada.discriminants.unknown", false) \
    X(Divide, "ada.divide", true) \
    X(Else, "ada.else", false) \
    X(Elsif, "ada.elsif", false) \
    X(End, "ada.end", false) \
    X(Entry, "ada.entry", false) \
    X(EntryBody, "ada.entry.body", false) \
    X(EntryFamily, "ada.entry.family", false) \
    X(EntryIndex, "ada.entry.index", false) \
    X(Eq, "ada.eq", true) \
    X(Exception, "ada.exception", false) \
    X(Exit, "ada.exit", false) \
    X(Extension, "ada.extension", false) \
    X(Filter, "ada.filter", false) \
    X(Flag, "ada.flag", false) \
    X(For, "ada.for", false) \
    X(ForIterator, "ada.for.iterator", false) \
    X(ForRange, "ada.for.range", false) \
    X(FormalDecimal, "ada.formal.decimal", false) \
    X(FormalDerived, "ada.formal.derived", false) \
    X(FormalDiscrete, "ada.formal.discrete", false) \
    X(FormalFixed, "ada.formal.fixed", false) \
    X(FormalFloat, "ada.formal.float", false) \
    X(FormalInteger, "ada.formal.integer", false) \
    X(FormalModular, "ada.formal.modular", false) \
    X(FormalObject, "ada.formal.object", false) \
    X(FormalPackage, "ada.formal.package", false) \
    X(FormalPrivate, "ada.formal.private", false) \
    X(FormalSubprogram, "ada.formal.subprogram", false) \
    X(FormalType, "ada.formal.type", false) \
    X(FormalTypeIncomplete, "ada.formal.type.incomplete", false) \
    X(FunctionSpecification, "ada.function.specification", false) \
    X(Ge, "ada.ge", true) \
    X(Generic, "ada.generic", false) \
    X(GenericFormals, "ada.generic.formals", false) \
    X(Goto, "ada.goto", false) \
    X(Gt, "ada.gt", true) \
    X(Guard, "ada.guard", false) \
    X(Handled, "ada.handled", false) \
    X(Handler, "ada.handler", false) \
    X(Identifier, "ada.identifier", false) \
    X(If, "ada.if", false) \
    X(IfExpression, "ada.if.expression", false) \
    X(In, "ada.in", true) \
    X(IndexSubtype, "ada.index.subtype", false) \
    X(Instantiation, "ada.instantiation", false) \
    X(Interfaces, "ada.interfaces", false) \
    X(Iterated, "ada.iterated", false) \
    X(Label, "ada.label", false) \
    X(Le, "ada.le", true) \
    X(Loop, "ada.loop", false) \
    X(Lt, "ada.lt", true) \
    X(Mod, "ada.mod", true) \
    X(Multiply, "ada.multiply", true) \
    X(Ne, "ada.ne", true) \
    X(Negate, "ada.negate", false) \
    X(Not, "ada.not", false) \
    X(Null, "ada.null", false) \
    X(NullStatement, "ada.null.statement", false) \
    X(Number, "ada.number", false) \
    X(NumberDeclaration, "ada.number.declaration", false) \
    X(Object, "ada.object", false) \
    X(Operator, "ada.operator", false) \
    X(Or, "ada.or", true) \
    X(OrElse, "ada.orelse", true) \
    X(Others, "ada.others", false) \
    X(Package, "ada.package", false) \
    X(PackageBody, "ada.package.body", false) \
    X(Parallel, "ada.parallel", false) \
    X(Parameter, "ada.parameter", false) \
    X(Parenthesized, "ada.parenthesized", false) \
    X(Plus, "ada.plus", false) \
    X(Power, "ada.power", true) \
    X(Pragma, "ada.pragma", false) \
    X(PragmaArgument, "ada.pragma.argument", false) \
    X(Private, "ada.private", false) \
    X(ProcedureSpecification, "ada.procedure.specification", false) \
    X(Profile, "ada.profile", false) \
    X(ProtectedBody, "ada.protected.body", false) \
    X(ProtectedDefinition, "ada.protected.definition", false) \
    X(ProtectedSingle, "ada.protected.single", false) \
    X(ProtectedType, "ada.protected.type", false) \
    X(Qualified, "ada.qualified", true) \
    X(Quantified, "ada.quantified", false) \
    X(Raise, "ada.raise", false) \
    X(RaiseExpression, "ada.raise.expression", false) \
    X(Range, "ada.range", false) \
    X(RecordNull, "ada.record.null", false) \
    X(Reducer, "ada.reducer", false) \
    X(Rem, "ada.rem", true) \
    X(RenamingException, "ada.renaming.exception", false) \
    X(RenamingGeneric, "ada.renaming.generic", false) \
    X(RenamingObject, "ada.renaming.object", false) \
    X(RenamingPackage, "ada.renaming.package", false) \
    X(RenamingSubprogram, "ada.renaming.subprogram", false) \
    X(RepresentationAt, "ada.representation.at", false) \
    X(RepresentationAttribute, "ada.representation.attribute", false) \
    X(RepresentationComponent, "ada.representation.component", false) \
    X(RepresentationEnumeration, "ada.representation.enumeration", false) \
    X(RepresentationMod, "ada.representation.mod", false) \
    X(RepresentationRecord, "ada.representation.record", false) \
    X(Requeue, "ada.requeue", false) \
    X(Result, "ada.result", false) \
    X(Return, "ada.return", false) \
    X(ReturnExtended, "ada.return.extended", false) \
    X(SelectAccept, "ada.select.accept", false) \
    X(SelectAlternative, "ada.select.alternative", false) \
    X(SelectAsynchronous, "ada.select.asynchronous", false) \
    X(SelectCall, "ada.select.call", false) \
    X(SelectConditional, "ada.select.conditional", false) \
    X(SelectDelay, "ada.select.delay", false) \
    X(SelectTimed, "ada.select.timed", false) \
    X(SelectTrigger, "ada.select.trigger", false) \
    X(Selected, "ada.selected", true) \
    X(Statements, "ada.statements", false) \
    X(Stub, "ada.stub", false) \
    X(Subpool, "ada.subpool", false) \
    X(SubprogramAbstract, "ada.subprogram.abstract", false) \
    X(SubprogramBody, "ada.subprogram.body", false) \
    X(SubprogramDeclaration, "ada.subprogram.declaration", false) \
    X(SubprogramExpression, "ada.subprogram.expression", false) \
    X(SubprogramNull, "ada.subprogram.null", false) \
    X(Subtract, "ada.subtract", true) \
    X(Subtype, "ada.subtype", false) \
    X(SubtypeIndication, "ada.subtype.indication", false) \
    X(Subunit, "ada.subunit", false) \
    X(Target, "ada.target", false) \
    X(TaskBody, "ada.task.body", false) \
    X(TaskDefinition, "ada.task.definition", false) \
    X(TaskSingle, "ada.task.single", false) \
    X(TaskType, "ada.task.type", false) \
    X(Terminate, "ada.terminate", false) \
    X(Type, "ada.type", false) \
    X(TypeAccess, "ada.type.access", false) \
    X(TypeAccessSubprogram, "ada.type.access.subprogram", false) \
    X(TypeArray, "ada.type.array", false) \
    X(TypeArrayUnconstrained, "ada.type.array.unconstrained", false) \
    X(TypeDecimal, "ada.type.decimal", false) \
    X(TypeDerived, "ada.type.derived", false) \
    X(TypeEnumeration, "ada.type.enumeration", false) \
    X(TypeFixed, "ada.type.fixed", false) \
    X(TypeFloat, "ada.type.float", false) \
    X(TypeIncomplete, "ada.type.incomplete", false) \
    X(TypeInteger, "ada.type.integer", false) \
    X(TypeInterface, "ada.type.interface", false) \
    X(TypeModular, "ada.type.modular", false) \
    X(TypePrivate, "ada.type.private", false) \
    X(TypePrivateExtension, "ada.type.private.extension", false) \
    X(TypeRecord, "ada.type.record", false) \
    X(Unit, "ada.unit", false) \
    X(Use, "ada.use", false) \
    X(UseType, "ada.use.type", false) \
    X(Variant, "ada.variant", false) \
    X(Variants, "ada.variants", false) \
    X(While, "ada.while", false) \
    X(With, "ada.with", false) \
    X(Word, "ada.word", false) \
    X(Xor, "ada.xor", true)

    enum class construct : std::uint32_t
    {
#define NEOS_ADA_CONSTRUCT_ENUM(aConstruct, aName, aPostfix) aConstruct,
        NEOS_ADA_CONSTRUCTS(NEOS_ADA_CONSTRUCT_ENUM)
#undef NEOS_ADA_CONSTRUCT_ENUM
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
#define NEOS_ADA_CONSTRUCT_INFO(aConstruct, aName, aPostfix) { construct::aConstruct, aName, #aConstruct, aPostfix },
        NEOS_ADA_CONSTRUCTS(NEOS_ADA_CONSTRUCT_INFO)
#undef NEOS_ADA_CONSTRUCT_INFO
    };

    inline constexpr construct_info const& info(construct aConstruct)
    {
        return Constructs[static_cast<std::uint32_t>(aConstruct)];
    }

    // the runtime's memory (the external memory at index Memory: see neos_rt_host::external_memory_at), which generated code
    // reads and writes: objects that are not scalars held in locals (composite objects, aliased objects, the objects of
    // allocators, frames that nested subprograms refer to) are at their addresses (offsets) in it; address 0 is null
    namespace layout
    {
        inline constexpr std::uint32_t Memory = 3u;                 ///< (the external memory's index: Ada's)
        // (the memory is the shared memory (see neos_rt_host::shared_memory), so that the code of other languages that share it (e.g. C's
        // (see neos::language::c::layout), whose range is below Ada's) has the same addresses for Ada's objects (Annex B): Ada's range is 
        // from Origin; the offsets below FirstAddress are not used; the program's static data begins at StaticBase)
        inline constexpr std::uint64_t Origin = std::uint64_t{ 64u } << 30u;        ///< (its range's offset)
        inline constexpr std::uint64_t Size = std::uint64_t{ 64u } << 30u;          ///< (its range's size)
        inline constexpr std::uint64_t FirstAddress = Origin + 0x10000u;
        inline constexpr std::uint64_t StaticBase = FirstAddress;                   ///< (the program's static data)
    }
}
