/*
  codegen.hpp

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

// Ada: the code generation of one WebAssembly function (a subprogram's body, a library unit's elaboration, a task body ...)

#pragma once

#include <functional>
#include "program.hpp"

namespace neos::compilers::ada
{
    using subprogram_kind = program::subprogram_kind;

    // the WebAssembly value of an Ada type's values
    enum class vkind { None, I64, F32, F64 };

    class function_generator
    {
    public:
        // (the signature's locals: the static link, then the formals)
        static constexpr std::uint32_t StaticLinkLocal = 0u;
    public:
        function_generator(program& aProgram, entity* aSubprogram, std::uint32_t aIndex, int aLevel);
    public:
        void generate_subprogram();
        void denote(entity* aCallee);           ///< (a dispatching call: its denoted operation's controlling tag (for its class-wide preconditions))
        void generate_elaboration(node* aUnit, region* aRegion, bool aBody);
        void finish(std::vector<type> const& aParameters);
        bool validate(std::string const& aName);           ///< (a diagnostic: the code's types are valid (NEOS_ADA_VALIDATE))
        // --- code
    public:
        void op(opcode aOpcode);
        void u32(std::uint32_t aValue);
        void i32(std::int32_t aValue);
        void i64(std::int64_t aValue);
        void f32(float aValue);
        void f64(double aValue);
        void get(std::uint32_t aLocal);
        void set(std::uint32_t aLocal);
        void tee(std::uint32_t aLocal);
        void drop();
        void unit_priority(entity* aUnit, ada_type* aType, std::uint32_t aAddress);
        bool volatile_actual(node* aActual);
        bool volatile_type(ada_type* aType, int aDepth = 0);
        std::uint32_t local(type aType);
        std::uint32_t temporary(type aType);
        void release(std::uint32_t aLocal);
        void call(std::uint32_t aFunction);
        void call_indirect(std::vector<type> const& aParameters, type aResult);
        void function_index(std::uint32_t aFunction);       ///< (an i32: a function's index (relocated))
        void runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);
        void string_constant(std::string const& aText);     ///< (an i32: a NUL terminated string's address (a runtime function's STRING argument))
        void load(vkind aKind, std::uint64_t aSize, std::uint32_t aOffset = 0u, bool aSigned = true);    ///< ([address] -> value)
        void store(vkind aKind, std::uint64_t aSize, std::uint32_t aOffset = 0u);   ///< ([address, value] -> ())
        void load64(std::uint32_t aOffset = 0u) { load(vkind::I64, 8u, aOffset); }
        void store64(std::uint32_t aOffset = 0u) { store(vkind::I64, 8u, aOffset); }
        // --- control
    public:
        enum class control_kind { Block, Loop, Exit, Handler, Breakable };
        struct control
        {
            control_kind kind;
            entity* loop = nullptr;         ///< (a loop's (its exit target))
            std::int64_t ssMark = -1;       ///< (a local holding the secondary stack's mark to release when leaving it (an exit))
            std::int64_t finMark = -1;      ///< (a loop's: a local holding the finalization chain's mark at its start (an exit finalizes the objects since))
            std::function<void()> const* onExit = nullptr;  ///< (a loop's: what an exit does first (e.g. an iteration's element reference finalized))
        };
        void begin(control_kind aKind, std::optional<type> aResult = {}, entity* aLoop = nullptr);
        void begin_if(std::optional<type> aResult = {});
        void else_();
        void end();
        void branch(std::size_t aControl, bool aConditional = false);
        void leave(std::size_t aControl);
        void renamed_address(entity* aObject);
        void composite_actual(ada_type* aFormal, node* aActual);
        bool representations_differ(ada_type* aSource, ada_type* aTarget);   ///< (untagged record types related by derivation whose layouts differ (13.6))
        void representation_copy(ada_type* aSource, std::uint32_t aFrom, ada_type* aTarget, std::uint32_t aTo, bool aDiscriminants);   ///< (component by component; the target's discriminants that the source's type constrains too)
        static bool mutable_record(ada_type* aType);        ///< (an unconstrained subtype of an untagged record type whose discriminants have defaults)
        static bool discriminated_record(ada_type* aType);  ///< (an untagged record type with discriminants)
        void unconstrained_view(node* aActual);
        void record_subtype_size(ada_type* aSubtype);
        void dynamic_record_size(ada_type* aBase, std::uint32_t aScratch);
        std::optional<std::int64_t> object_level(node* aObject);   ///< (an object's accessibility level: static; or (nullopt) an i64 pushed (an access parameter's dereference's))
        std::optional<std::int64_t> access_level(node* aValue);    ///< (the accessibility level of the object an access value designates (likewise))
        std::int64_t static_depth(node const* aWhere);             ///< (the accessibility level of the innermost master of a construct of this subprogram)
        void accessibility_check(std::optional<std::int64_t> aLevel, std::int64_t aTarget, node const* aWhere);    ///< (a level (or, nullopt, the i64 pushed) not deeper than a target's (else Program_Error))
        bool passes_levels(entity* aCallee) const;
        void heap_allocate();                                      ///< (an i64 size on the stack: the address of storage allocated for it (from iPool's Allocate, or the heap))
        node* storage_pool_of(ada_type* aAccess);                  ///< (an access type's specified storage pool (its name), if any (its ancestor's))
        void pool_call(std::string const& aOperation, std::uint32_t aPool, std::uint32_t aAddress, std::uint32_t aSize, std::uint64_t aAlignment = 8u,
            std::optional<std::uint32_t> aSubpool = {});
        std::uint64_t storage_alignment(ada_type* aType);         ///< (the alignment of a type's objects' storage (an array's: its components'))   ///< (a pool's Allocate (its Storage_Address: aAddress, a frame cell's address) or Deallocate)
        void raise_exception(entity* aException, node const* aWhere);   ///< (an exception raised (at a construct))
        void relocate_tasks(ada_type* aType, std::uint32_t aAddress, bool aUnregister = false);
        void unit_parts(ada_type* aType, std::uint32_t aAddress, type_kind aKind, std::function<void(std::uint32_t)> const& aAction);   ///< (an object's parts that are tasks (or protected objects): each one's address)    ///< (an object with tasks moved from where they were created (a function's return object): each task's object now there)
        void store_access_level(entity* aObject, node* aValue);    ///< (a stand-alone object of an anonymous access type given a value: the value's accessibility level, in its hidden object)
        void class_constraint_check(ada_type* aAccess, node const* aWhere);   ///< (an access value (on the stack, kept) of a type designating S'Class, S constrained: the object designated (unless null) in S (4.6(51/4), 3.10.2(31))                 ///< (whether a call of a subprogram passes its access parameters' accessibility levels (in their values' bits LevelShift..))
        void unconstrained_flag(entity* aFormal, std::uint32_t aAddress);  ///< (the flag of a formal's address (in a local): removed from it, in iUnconstrainedFlags)             ///< (an i64: 1 if the object (of a mutable record type) is an unconstrained view (3.7.2))
        void designated_constraint_test(ada_type* aAccess, std::uint32_t aValue);
        void variant_selected(ada_type* aBase, component_info const& aComponent, std::uint32_t aRecord);                     ///< (an exit from a loop: its objects finalized, then a branch to its end)
        std::size_t depth_of(std::size_t aControl) const { return iControls.size() - 1u - aControl; }
        std::size_t handler_target() const;                 ///< (where a pending exception branches: the innermost handler, or the exit)
        void check();                                       ///< (a pending exception branches to the handler target)
        void raise_check(int aKind, node const* aWhere, std::string const& aWhat);
        void data_of_access();          ///< (an access-to-array value: its array's data's address)   ///< (Constraint_Error ... then the handler target)
        void failed_assertion(node const* aWhere, std::string const& aWhat);          ///< (Assertion_Error raised, then the handler target)
        void assertion(node* aCondition, std::string const& aWhat);                 ///< (Assertion_Error raised unless the condition holds)
        // --- values and types
    public:
        vkind kind_of(ada_type* aType);
        type wasm(vkind aKind);
        type wasm_of(ada_type* aType) { return wasm(kind_of(aType)); }
        std::uint64_t size_of(ada_type* aType);             ///< (static size of its objects, bytes (0: dynamic))
        void dynamic_size(ada_type* aType, std::optional<std::uint32_t> aDescriptor = {});   ///< (an i64: its objects' size (an array's: of its bounds (a descriptor in a local)))
        bool by_copy(parameter_info const& aParameter);     ///< (a scalar of mode in: passed by value)
        void to_boolean();                                  ///< (an i32 condition -> an i64 Boolean)
        void scalar_low(ada_type* aType);                   ///< (an i64/f64 bound of a scalar subtype)
        void scalar_high(ada_type* aType);
        void range_check(ada_type* aTarget, ada_type* aSource, node const* aWhere);  ///< (the value on the stack is in the target subtype (else Constraint_Error))
        void constraint_check(ada_type* aTarget, ada_type* aSource, node const* aWhere);  ///< (... its constraint (else Constraint_Error))
        void predicate_check(ada_type* aTarget, ada_type* aSource, node const* aWhere, bool aWhereValue = true);   ///< (... its predicate (else Assertion_Error))
        bool static_predicate_bounds(ada_type* aType, std::int64_t& aLow, std::int64_t& aHigh);    ///< (a subtype with static predicates: its least and greatest values that satisfy them)
        std::vector<entity*> invariants_of(ada_type* aType);       ///< (its type invariants' functions: its own, and its ancestors' class-wide ones)
        bool outside_scope_of(ada_type* aType);                     ///< (the code generated is outside the immediate scope of the type's declaration)
        bool boundary_of(entity* aSubprogram, ada_type* aType);
        bool inherited_call(entity* aCallee, ada_type* aType);      ///< (a call of an ancestor's operation with an object of a descendant type)     ///< (a subprogram visible outside the immediate scope of the type (its invariants checked on return, 7.3.2(19/5)))
        void invariant_check(ada_type* aType, std::uint32_t aValue, node const* aWhere);
        void invariant_part_types(ada_type* aType, std::vector<ada_type*>& aTypes, int aDepth = 0);   ///< (the types with invariants of its composite parts (itself included))
        bool invariant_boundary(entity* aSubprogram, ada_type* aType);     ///< (a boundary subprogram (or an inherited operation called) for a type with invariants of its parts)
        void deep_invariant_check(ada_type* aType, std::uint32_t aAddress, node const* aWhere);   ///< (its parts of types with invariants (itself included) checked)
        void parts_invariant_check(ada_type* aType, std::uint32_t aAddress, node const* aWhere);   ///< (a default-initialized object's parts of types with invariants)   ///< (the object (its address) satisfies its type's invariants (else Assertion_Error))
        void call_invariant_checks(entity* aCallee, call_info const& aInfo, std::vector<parameter_info> const& aFormals);   ///< (after a call: its out and in out actuals' (and view conversions') invariants)
        void copy_back_check(ada_type* aTarget, ada_type* aSource, node const* aActual);  ///< (an out or in out parameter's value copied back to the actual: of its subtype)
        void predicate_test(predicate_info* aPredicate, std::uint32_t aValue, bool aChecks);            ///< (an i32: whether the value satisfies the predicate)
        void overflow_check(ada_type* aType, node const* aWhere);
        void enumeration_code(ada_type* aType);             ///< (an enumeration value's position -> its representation code (13.4))
        void enumeration_position(ada_type* aType);         ///< (a representation code -> its position (none: the number of values))
        // --- objects
    public:
        void assign_storage(entity* aObject);
        void address_of(entity* aObject);                   ///< (an i64: an object's address (memory resident))
        void frame_of_level(int aLevel);                    ///< (an i64: the frame of the subprogram at a level that encloses this one)
        void load_object(entity* aObject);
        void bit_field_load(component_info const& aComponent);     ///< ([its first storage unit's address] -> its value)
        void bit_field_store(component_info const& aComponent);    ///< ([its first storage unit's address, value] -> ())
        component_info const* bit_field_name(node* aName);         ///< (a name of a record component that is a bit field: it)
        void component_load(component_info const& aComponent);     ///< ([a record's address] -> a scalar component's value (a bit field's too))
        void component_store(component_info const& aComponent);    ///< ([a record's address, value] -> ())
        void store_object(entity* aObject);                 ///< ([value] -> ())
        bool memory_resident(entity* aObject);
        // --- expressions
    public:
        void expression(node* aExpression, ada_type* aType = nullptr);   ///< (its value, of its type (converted to aType's representation if given))
        void condition(node* aExpression);                  ///< (an i32)
        void statement_condition(node* aExpression);        ///< (an i32: a statement's condition, its temporaries finalized)
        void temporary_result(ada_type* aType);             ///< ([address] -> [address]: a controlled function result that is not moved into an object: finalized when its statement ends)
        void moved_result(node* aExpression);               ///< (an expression whose value (a function result) is moved into an object: not a temporary)
        void release_temporaries(bool aCheck = true);
        std::uint32_t hold_temporaries();                   ///< (a compound statement's header's temporaries: kept (until release_held) apart from its statements')
        void release_held(std::uint32_t aHeld);                         ///< (the temporaries of the statement (or declaration) generated: finalized)
        void address(node* aName);                          ///< (an i64: the address of the object (or a composite value) a name denotes)
        void store_to(node* aTarget, ada_type* aType);      ///< ([value] -> (): assignment to a name)
        void call_expression(node* aCall, bool aStatement);
        void intrinsic_call(entity* aCallee, std::vector<node*> const& aOperands, ada_type* aResult, node* aWhere);
        void attribute(node* aAttribute);
        void aggregate(node* aAggregate, ada_type* aType, std::optional<std::uint32_t> aTarget = {});    ///< (an i64: the address of the value (built at aTarget's address, or in a new temporary))
        void array_descriptor(node* aExpression, ada_type* aType);   ///< (an i64: an array value's descriptor)
        void bounds(std::uint32_t aDescriptor, std::size_t aDimension, bool aFirst);   ///< (an i64: a bound read from a descriptor (local))
        void allocate_array(ada_type* aType, std::uint32_t aSourceDescriptor, bool aSecondary, bool aHeap = false);
        void widen_string(ada_type* aWideString);   ///< (a String's address on the stack: a new wide string of its characters)   ///< (an i64: a new array object (descriptor and data) with the bounds of the source descriptor)
        void copy_value(ada_type* aType, std::uint32_t aTarget, std::uint32_t aSource);    ///< (a composite value copied)
        void default_initialize(ada_type* aType, std::uint32_t aAddress);
        void conversion(node* aOperand, ada_type* aSource, ada_type* aTarget, node* aWhere);
        void string_literal(node* aLiteral, ada_type* aType);
        void concatenation(entity* aOperator, std::vector<node*> const& aOperands, ada_type* aType);
        void comparison(intrinsic aOp, ada_type* aType, std::vector<node*> const& aOperands);
        void image(ada_type* aType, node* aValue);
        void value_attribute(ada_type* aType, node* aString);
        // --- statements
    public:
        void statements(node* aStatements);
        void statement(node* aStatement);
        void declarations(node* aDeclarations, bool aActivate = true);    ///< (aActivate: the tasks it creates activated at its end (not a package specification's visible part))
        void declaration(node* aDecl);
        void deferred_activation_declarations(node* aDeclarations, entity* aPackage = nullptr);
        void object_declaration(node* aDecl);
        void handled(node* aHandled);
        void elaborate_dynamic_bounds(node* aDecl);
        void assignment(node* aStatement);
        void if_statement(node* aStatement);
        void case_statement(node* aStatement);
        void loop_statement(node* aStatement);
        void block_statement(node* aStatement);
        void return_statement(node* aStatement);
        void raise_statement(node* aStatement);
        void exit_statement(node* aStatement);
        void pragma_statement(node* aStatement);
        // --- calls
    public:
        void static_link_for(entity* aCallee);
        std::uint32_t function_of(entity* aSubprogram);     ///< (its WebAssembly function (defined if need be))
        entity* ultimate(entity* aSubprogram);              ///< (a renaming's renamed subprogram (and so on))
        // --- more
    public:
        bool is_unsigned(ada_type* aType);
        void with_secondary_stack(std::function<void()> const& aGenerate);
        void prologue();
        void renaming_body();
        void return_value(ada_type* aType, bool aAdjust = false);
        void epilogue();
        entity* resolve_renaming(entity* aObject);
        bool is_simple_name(node* aName);
        void object_declaration_of(entity* aObject);
        void initialize_object(entity* e);
        bool is_aggregate(node* aNode);
        void indirect_storage(entity* aObject);
        bool current_instance(node* aName);
        void index_compatibility_check(ada_type* aArrayType, std::uint32_t aDescriptor);
        void variant_check(ada_type* aRecordType, entity* aComponent, node const* aWhere);
        void compatibility_checks(node* aConstruct, std::function<bool(node const*)> const& aWithin);
        void set_indirect(entity* aObject);
        void choice_test(node* aChoice, std::uint32_t aValue);
        void discrete_bounds(node* aSpecification, ada_type* aRange, std::uint32_t aLow, std::uint32_t aHigh);
        void constant_value(static_value const& aValue, ada_type* aType);
        void convert_representation(ada_type* aSource, ada_type* aTarget);
        void name_value(node* aName, ada_type* aType);
        void apply_value(node* aApply, ada_type* aType);
        void dereference(node* aAll);
        void null_check(node const* aWhere);
        void if_expression(node* aExpression, ada_type* aType);
        void case_expression(node* aExpression, ada_type* aType);
        void membership(node* aExpression);
        void materialize(ada_type* aType);
        std::uint64_t component_offset(ada_type* aRecord, entity* aComponent);
        bool indirect_component(ada_type* aRecord, entity* aComponent);
        void component_address(ada_type* aBase, component_info const& aComponent, std::uint32_t aRecord);
        void data_address(std::uint32_t aDescriptor);
        void index_address(node* aApply);
        void element_size(ada_type* aArray);
        void slice_descriptor(node* aApply);
        void range_attribute_bounds(node* aAttribute, std::uint32_t aLow, std::uint32_t aHigh);
        void subtype_descriptor(ada_type* aArray);
        void init_descriptor(ada_type* aArray, std::uint32_t aAddress);
        void array_length(std::uint32_t aDescriptor, std::size_t aDimensions);
        void stack_allocate(std::uint32_t aSize);
        void stack_check();
        void allocate_record(ada_type* aType, std::optional<std::uint32_t> aValue, bool aHeap);
        void length_check(std::uint32_t aTarget, std::uint32_t aSource, std::size_t aDimensions, node const* aWhere);
        void total_size(ada_type* aType, std::uint32_t aValue);
        void copy_to_secondary(ada_type* aType, std::uint32_t aValue);
        void component_descriptor(ada_type* aRecord, component_info const& aComponent, std::uint32_t aRecordAddress, std::uint32_t aComponentAddress);
        void round_to_integer();
        void call_function(entity* aCallee);
        void assign_value(node* aTarget);
        void op_reinterpret(bool aToInteger);
        void intrinsic_values(entity* aCallee, ada_type* aResult, node* aWhere, ada_type* aOperandType = nullptr);
        void boolean_array_operation(intrinsic aOp, ada_type* aType);
        void equality_values(ada_type* aType, bool aComposed = false);
        void comparison_values(intrinsic aOp, ada_type* aType);
        void tag_link(node* aDecl);
        entity* iterator_operation(ada_type* aIterator, std::string const& aName);
        void iterator_call(entity* aOperation, std::uint32_t aIterator, std::optional<std::uint32_t> aCursor, ada_type* aCursorType);
        void cursor_loop(node* aLoop, node* aSpecification, node* aBody, std::uint32_t aIterator, ada_type* aIteratorType, ada_type* aCursorType,
            std::function<void(std::uint32_t)> const& aIteration, std::function<void()> const* aIterationEnd = nullptr, bool aOwnsIterator = true);
        bool implicitly_dereferenced(node* aExpression, bool aValue);
        std::set<node*> iDereferencing;
        void evaluate_dynamic_bounds(node* aNode);
        int stream_slot(std::string const& aOperation);
        void stream_elementary(bool aRead, ada_type* aType, std::uint32_t aStream, std::uint32_t aAddress);
        void stream_object(bool aRead, ada_type* aType, std::uint32_t aStream, std::uint32_t aAddress);
        void stream_attribute(node* aAttribute, bool aStatement);
        void stream_output(ada_type* aType, std::uint32_t aStream, std::uint32_t aItem);   ///< (S'Output of an item (elementary: a cell with its value))
        void stream_input(ada_type* aType, std::uint32_t aStream, node* aWhere);           ///< (S'Input: its value (composite: its address, on the secondary stack))
        std::vector<ada_type*> class_members(ada_type* aRoot);     ///< (the specific types of a class whose objects can be here (for class-wide stream attributes))
        void class_wide_stream(node* aAttribute, std::string const& aName, ada_type* aClassWide, std::uint32_t aStream, std::vector<node*> const& aArguments);
        void decimal_truncation();
        void goto_statement(node* aStatement);
        struct goto_scope { std::vector<std::pair<std::string, std::size_t>> labels; std::size_t top; std::uint32_t state; };
        std::vector<goto_scope> iGotoScopes;
        std::vector<std::pair<std::uint32_t, std::int64_t>> iTagLinks;
        std::map<node const*, std::uint32_t> iOldValues;
        ada_type* iContractType = nullptr;      ///< (generating a primitive operation's contracts: its controlling type (class-wide conditions call its operations))     ///< (the postconditions' X'Old: their values' locals (evaluated at entry))
        bool limited_type(ada_type* aType);
        entity* primitive_equality(ada_type* aType, bool aLimited = false);
        void component_equality(ada_type* aType, bool aLimited = false);   ///< (aLimited: a limited type's primitive "=" too (a membership test's))
        void float_attribute(node* aAttribute, std::string const& aName, ada_type* aType, std::vector<node*> const& aArguments);
        void attribute_call(entity* aCallee, std::vector<node*> const& aActuals, node* aCall);
        void subprogram_access(entity* aSubprogram, node* aObject = nullptr);
        void store_component(std::uint32_t aRecord, component_info const& aComponent, node* aValue, ada_type* aRecordType = nullptr);
        void array_aggregate(node* aAggregate, ada_type* aType, std::optional<std::uint32_t> aTarget);
        bool aggregate_bounds(node* aLevel, ada_type* aType, std::size_t aDimension, std::uint32_t aLow, std::uint32_t aHigh);
        bool aggregate_has_others(node* aLevel);
        void loop_body(node* aBody);
        void quantified(node* aExpression);
        node* unqualified(node* aExpression);
        void choice_bounds(node* aChoice, ada_type* aIndex, std::uint32_t aLow, std::uint32_t aHigh);
        void array_aggregate_components(node* aLevel, ada_type* aType, std::uint32_t aTarget, std::size_t aDimension, std::vector<std::uint32_t> const& aLows, std::vector<std::uint32_t> const& aHighs, std::uint64_t aElement);
        void string_subaggregate(node* aLiteral, std::uint32_t aTarget, std::uint32_t aOffset, std::uint64_t aElement, ada_type* aType);
        void allocator(node* aAllocator, ada_type* aType);
        void allocator_object(node* aAllocator, ada_type* aType);
        std::vector<std::size_t> iAcceptControls;
        std::vector<std::pair<ada_type*, std::uint32_t>> iInstances;   ///< (records being initialized: their base types and addresses (locals))
        std::uint32_t iTail = 0u;
        bool iDiscriminantsSet = false;                 ///< (the record being default initialized has its discriminants already)                       ///< (a dynamically sized record being initialized: its tail's next free byte (a local))
        bool is_runtime_sized(ada_type* aType);
        bool instance_member(entity* aObject, std::uint64_t& aOffset, std::uint32_t& aInstance);
        void index_bound(ada_type* aIndex, bool aLow);
        void record_size(ada_type* aType, std::uint32_t aValue);
        void copy_whole(ada_type* aType, std::uint32_t aTarget, std::uint32_t aSource);
        void component_bounds(ada_type* aRecord, component_info const& aComponent, std::uint32_t aRecordAddress, std::uint32_t aComponentAddress);
        void store_discriminants(ada_type* aType, std::uint32_t aAddress, std::function<node*(std::size_t, component_info const&)> const& aValue);
        void dynamic_record(ada_type* aType, std::uint32_t aScratch, int aWhere);
        void dynamic_object(ada_type* aType, std::optional<std::uint32_t> aValue, int aWhere);
        void discriminant_check(ada_type* aType, std::uint32_t aTarget, std::uint32_t aSource, node const* aWhere);
        void discriminant_constraint_check(ada_type* aSubtype, std::uint32_t aValue, node const* aWhere, std::vector<std::uint32_t> const* aValues = nullptr);
        std::vector<std::uint32_t> discriminant_constraint_values(ada_type* aSubtype);       ///< (the accept statements being generated: their bodies' blocks)
        std::optional<std::pair<std::uint64_t, node*>> iStorageLimit;   ///< (an allocator's access type's specified Storage_Size (no pool): its total's cell, the size)
        std::uint64_t iPoolAlignment = 8u;              ///< (the alignment of the objects that iPool's allocator allocates)
        std::optional<std::uint32_t> iPool;             ///< (an allocator's access type's storage pool (its address, a local), if specified)
        std::optional<std::uint32_t> iSubpool;          ///< (an allocator's subpool (its handle, a local), if specified (13.11.4))
        std::optional<std::uint32_t> iCollection;       ///< (tasks being created by an allocator: its access type's collection (a local))
        void access_collection(node* aDecl);
        void iterator_loop(node* aLoop, node* aSpecification, node* aBody);
        void container_iterator_loop(node* aLoop, node* aSpecification, node* aBody);
        void generalized_iterator_loop(node* aLoop, node* aSpecification, node* aBody);
        void controlled_parts(ada_type* aType, std::uint32_t aAddress, bool aReverse, std::function<void(ada_type*, std::uint32_t)> const& aAction,
            bool aDefaulted = false);   ///< (aDefaulted: not the components initialized by their default expressions)
        entity* controlled_operation(ada_type* aType, std::string const& aOperation);
        void controlled_call(ada_type* aType, std::uint32_t aAddress, std::string const& aOperation);
        void controlled_guarded(std::function<void()> const& aParts);  ///< (Adjust or Finalize calls: an exception that one propagates is Program_Error once the others are done (7.6.1(14/1-20)))
        std::optional<std::uint32_t> iControlledFailed;
        bool iRounding = false;             ///< (a conversion for a decimal type's 'Round: rounded, not truncated (3.5.10(12)))     ///< (within controlled_guarded: whether an Adjust or Finalize propagated an exception)
        void controlled_initialized(ada_type* aType, std::uint32_t aAddress, bool aInitialize, bool aAdjust, bool aRegister);
        void controlled_finalize(ada_type* aType, std::uint32_t aAddress);
        void controlled_unregister(std::uint32_t aAddress);
        bool is_temporary_value(node* aExpression);
        bool tasking_statement(node* aStatement);
        bool tasking_call(node* aCall, call_info const& aInfo, entity* aCallee, bool aStatement);
        void tasking_attribute(node* aAttribute, std::string const& aName);
    public:
        std::vector<std::uint32_t> iOccurrences;        ///< (the locals holding the occurrences of the handlers being generated)
        std::vector<node*> iTargetNames;                ///< (assignments' targets (@))
        std::vector<std::uint32_t> iTargetAddresses;
        bool iDeferredActivation = false;
        bool iActivateAtBegin = false;
        std::map<entity*, std::pair<std::uint32_t, std::uint32_t>> iSpecActivationMarks;   ///< (nested package specifications that created tasks: the number pending before them (a local), for their bodies' activation)
        std::optional<std::pair<std::uint32_t, std::uint32_t>> iSpecActivation;   ///< (a package body's specification's tasks (its pending tasks' first and end): activated with the body's)
        std::optional<std::uint32_t> iActivationMark;  ///< (... the number of tasks pending activation before its declarative part (a local): only those after it are its)      ///< (the tasks a body's or block's declarative part created: activated at the start of its handled part (Tasking_Error is raised there, 9.2(5)))   ///< (a nested package specification created tasks: activated at its body's begin (or the enclosing declarative part's end))
        // (an out or in out formal of a mutable record type: whether its actual is unconstrained (a local: an i64, 1 unconstrained),
        // passed in its address's bit UnconstrainedBit)
        std::unordered_map<entity*, std::uint32_t> iUnconstrainedFlags;
        std::unordered_map<entity const*, std::uint32_t> iAccessLevels;
        std::int64_t iDynamicLevelOwner = 0;     ///< (the last dynamic level pushed (object_level, access_level): the level of the subprogram whose access parameter's it is)    ///< (this subprogram's access parameters: their accessibility levels' locals)
        static constexpr std::int64_t UnconstrainedBit = 62;
        static constexpr std::int64_t LevelShift = 48;                ///< (an access parameter's value passed: its accessibility level in bits 48..61)
        std::vector<std::uint32_t> iAggregateOffsets;
        std::uint32_t iFinMark = 0u;
        std::uint32_t iTemporaryMark = 0u;              ///< (the finalization chain's length (+ 1) before the current statement's temporaries; 0: none)
        bool iTemporaries = false;                      ///< (temporaries were registered since they were last released)
        std::set<node const*> iMovedResults;
        std::set<node const*> iCompatibilityChecked;
        std::vector<entity*> iReturnObjects;
        bool iStructureOnly = false;                     ///< (a new object whose components an aggregate gives: its descriptors and tail laid out, its components' defaults not evaluated)
        bool iAllocating = false;                        ///< (an allocator's object being initialized (its protected objects are not finalized by the current master))
        ada_type* iDefaultControlling = nullptr;        ///< (a call's default expression being generated for a controlling formal: the call's controlling type (its tag-indeterminate calls are that type's operations))
        entity* iDispatchingTag = nullptr;              ///< (pragma Dispatching_Tag: the Tag object a tag-indeterminate call in a return statement of this subprogram dispatches on)
        std::uint32_t iDispatchTagAddress = 0u;         ///< (a local: the address of the object whose tag a tag-indeterminate call being generated dispatches on)            ///< (the extended return statements being generated: their return objects)
        std::map<node const*, std::pair<std::uint32_t, std::uint32_t>> iChoiceBounds;
        std::function<void()> iLoopBody;      ///< (set: a quantified expression's predicate test, as its loop's body)
        std::set<node const*> iAggregateBounded;     ///< (array (sub)aggregates whose bounds have been evaluated (for the aggregate's))    ///< (an array aggregate's choices' bounds, evaluated for its bounds)
        std::uint32_t iSelf = 0u;                       ///< (the task or protected object: its parameter (see iHasSelf))
        bool iHasSelf = false;
        std::size_t iTasksCreated = 0u;                 ///< (task objects created (their activation follows the declarative part))
        void self_address(entity* aMember);             ///< (an i64: the address of the task or protected object that a component or discriminant is of)
        bool is_self_member(entity* aObject);
        void task_object_init(ada_type* aType, std::uint32_t aAddress);
        void protected_object_init(ada_type* aType, std::uint32_t aAddress);
        std::int64_t entry_key(entity* aEntry);
        void entry_family_offset(entity* aEntry, node* aIndex);  ///< (an i64: an entry family member's offset)
        void entry_call(node* aCall, call_info const& aInfo, entity* aEntry, std::optional<std::pair<std::uint32_t, std::uint32_t>> aTiming,
            std::function<std::uint32_t(std::uint32_t)> const* aAsynchronous = nullptr, std::function<void()> const* aEvaluateTiming = nullptr);
        std::uint32_t abortable_part(std::uint32_t aDepth, node* aStatements);
        static constexpr std::int64_t AtcSignalBase = std::numeric_limits<std::int64_t>::max() - 1024;
        void accept_body(node* aAccept, std::uint32_t aCall);
        void delay_value(node* aDelay, std::uint32_t aKind, std::uint32_t aValue);                    ///< (an i64 local: the finalization chain's length at entry (0: none))
        bool iRegistered = false;                       ///< (controlled objects were registered)   ///< (array aggregates: the element offsets of the enclosing dimensions)
    public:
        program& iProgram;
        entity* iSubprogram;
        std::uint32_t iIndex;
        int iLevel;
        std::vector<std::uint8_t> iCode;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> iRelocations;
        std::vector<type> iParameters;
        std::vector<type> iLocals;
        std::vector<std::uint32_t> iFreeTemporaries;
        std::vector<control> iControls;
        std::uint32_t iFrame = 0u;              ///< (an i64 local: the frame's base)
        std::uint32_t iStackTop = 0u;           ///< (an i64 local: the frame's end (checked against the stack's limit))
        std::uint64_t iFrameSize = 16u;         ///< (bytes: offset 0 is the static link)
        std::size_t iFrameSizePatch = 0u;       ///< (the code position of the frame size's constant)
        std::uint32_t iResult = 0u;             ///< (the result's local)
        std::uint32_t iSsMark = 0u;             ///< (an i64 local: the secondary stack's mark at entry)
        bool iHasResult = false;
        node const* iWhere = nullptr;           ///< (the construct being generated (for checks' messages))
        std::map<entity*, std::uint32_t> iLoopExits;
    };
}
