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

// Ada: the semantic model of a program: its entities (the things declarations declare), its types and subtypes, its 
// declarative regions (scopes) and static values

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "syntax.hpp"
#include "bignum.hpp"

namespace neos::compilers::ada
{
    struct entity;
    struct ada_type;
    struct region;
    struct subprogram_info;

    // --- static values

    // a static value: an integer (any size: universal_integer and discrete values (positions)), a real (universal_real and 
    // floating and fixed point values: exact), a string (a static string expression) or none
    struct static_value
    {
        std::variant<std::monostate, bigint, rational, std::u32string> value;

        static_value() = default;
        static_value(bigint aValue) : value{ std::move(aValue) } {}
        static_value(rational aValue) : value{ std::move(aValue) } {}
        static_value(std::u32string aValue) : value{ std::move(aValue) } {}
        bool known() const { return value.index() != 0u; }
        bool is_integer() const { return value.index() == 1u; }
        bool is_real() const { return value.index() == 2u; }
        bool is_string() const { return value.index() == 3u; }
        bigint const& integer() const { return std::get<bigint>(value); }
        rational real() const { return is_integer() ? rational{ integer() } : std::get<rational>(value); }
        std::u32string const& string() const { return std::get<std::u32string>(value); }
    };

    // --- types

    enum class type_kind
    {
        Enumeration,            ///< (Boolean, the character types and other enumeration types)
        Integer,                ///< (signed)
        Modular,
        Float,
        Fixed,                  ///< (ordinary fixed point)
        Decimal,                ///< (decimal fixed point)
        Array,
        Record,                 ///< (tagged or not; also the anonymous types of single protected objects and tasks: see Task/Protected)
        Access,                 ///< (to an object)
        AccessSubprogram,
        Task,
        Protected,
        Interface,
        Private,                ///< (a partial view, or an incomplete type, whose full view is not known (yet): see full)
        ClassWide,              ///< (T'Class)
        UniversalInteger,
        UniversalReal,
        UniversalFixed,
        UniversalAccess,        ///< (the type of null)
        String,                 ///< (the type of a string literal before resolution: any one dimensional array of a character type)
        Aggregate,              ///< (the type of an aggregate before resolution: any composite type)
        CharacterLiteral,       ///< (the type of a character literal before resolution: any character type (that is not an enumeration type of the program's own))
        Void                    ///< (no type: e.g. a procedure call)
    };

    // a component of a record type (or a discriminant)
    struct component_info
    {
        entity* ent = nullptr;
        ada_type* type = nullptr;
        node* default_expression = nullptr;
        bool discriminant = false;
        bool stored = false;                ///< (a discriminant of the parent of a type with new discriminants that is not one of them (see
                                            ///< types.cpp): its value (default_expression: the derivation's constraint's), for the
                                            ///< parent's variant parts and views; not a component of the type)
        bool aliased = false;
        // (its position: the variant that it is in (none: the record's own components) and its offset in the record)
        int variant = -1;                   ///< (an index in the record's variants)
        std::optional<std::uint64_t> offset;            ///< (static offset)
        std::optional<std::uint64_t> bit_position;      ///< (a component clause's)
        std::optional<std::uint64_t> bit_size;
        bool bits = false;                  ///< (a bit field: stored in its clause's bits (see program::bit_field))
        bool depends = false;               ///< (an array whose bounds depend on discriminants: evaluated for each object)
        bool dynamic = false;               ///< (... whose data is in the object's tail (its size is not bounded): its descriptor is in place)
    };

    // a variant of a variant part
    struct variant_info
    {
        node* choices = nullptr;
        int parent = -1;                    ///< (the variant whose component list it is in)
        entity* discriminant = nullptr;
        std::vector<std::pair<bigint, bigint>> ranges;  ///< (its choices' values; empty with others: the remaining values)
        bool others = false;
    };

    // a primitive operation slot of a tagged type (its dispatch table)
    struct dispatch_slot
    {
        entity* operation = nullptr;
    };

    // a subtype's predicate (3.2.4): its function (of the current instance), then those of the subtype it is of
    struct predicate_info
    {
        entity* function = nullptr;         ///< (resolved at the end of the declarative part: a function of the current instance)
        predicate_info* parent = nullptr;   ///< (the predicate of the subtype it is a subtype of)
        node* expression = nullptr;
        node* failure = nullptr;            ///< (Predicate_Failure: its exception or message)
        entity* failure_function = nullptr; ///< (... a function of the current instance: raises the exception or returns the message)
        bool is_static = false;
        bool checked = true;                ///< (its checks are enabled (the assertion policy where it is specified is Check))
    };

    struct ada_type
    {
        type_kind kind = type_kind::Void;
        std::string name;                   ///< (as declared (lower case); empty: anonymous)
        entity* decl = nullptr;             ///< (the type's (or subtype's) entity)
        ada_type* base = nullptr;           ///< (the type of a subtype (a type: itself))
        ada_type* parent = nullptr;         ///< (a derived type: its parent type (subtype))
        ada_type* root = nullptr;           ///< (a derived type's ultimate ancestor; else itself)
        ada_type* full = nullptr;           ///< (a private type's or incomplete type's full view)
        bool incomplete = false;            ///< (an incomplete type (not a private type))
        ada_type* partial = nullptr;        ///< (a full view's partial view)
        bool first_subtype = false;
        bool is_subtype = false;            ///< (a subtype (not a type): its constraints are its own)
        bool limited = false;
        bool tagged = false;
        bool untagged_partial = false;
        bool completing = false;            ///< (an incomplete view whose completion is being declared)      ///< (a partial view declared untagged (its full view may be tagged))
        bool abstract = false;
        bool is_formal = false;             ///< (a generic formal type)
        bool character_type = false;        ///< (an enumeration type with character literals)
        bool standard_character = false;    ///< (Character, Wide_Character, Wide_Wide_Character: the literals are the code points)
        bool boolean = false;               ///< (Standard.Boolean or a type derived from it)
        bool anonymous_access = false;
        bool controlled = false;            ///< (it has controlled parts (needs finalization))
        bool synchronized_interface = false;
        bool task_interface = false;
        bool protected_interface = false;
        bool packed = false;
        bool unchecked_union = false;       ///< (aspect Unchecked_Union (B.3.3): its discriminants are not checked; Program_Error where
                                            ///< they would be needed and a view lacks inferable discriminants)
        bool fortran = false;
        std::uint32_t machine_radix = 0u;   ///< (a Machine_Radix clause's (0: none, 2))               ///< (an array type of convention Fortran: its canonical order is column major (5.5.2(10/3)))
        bool has_tasks = false;

        // (scalar types: their range (static: low and high; else dynamic: the entities (hidden constants) that hold the bounds))
        static_value low;
        static_value high;
        entity* low_object = nullptr;
        entity* high_object = nullptr;
        node* range_node = nullptr;
        // (enumeration types: the literals)
        std::vector<entity*> literals;
        std::vector<bigint> representation;     ///< (an enumeration representation clause's codes)
        // (modular: the modulus)
        bigint modulus;
        // (floating point: digits; machine: 4 (single) or 8 (double) bytes)
        std::int64_t digits = 0;
        // (fixed point: delta and small; decimal: digits and scale)
        rational delta;
        rational small;
        std::int64_t scale = 0;

        // (arrays: the index subtypes (for an unconstrained array type, the index subtype marks) and the component subtype)
        std::vector<ada_type*> indexes;
        ada_type* component = nullptr;
        bool constrained = true;            ///< (an array subtype with bounds; a record subtype whose discriminants are constrained)
        bool aliased_components = false;
        // (records: discriminants and components (discriminants first); variants; a tagged type's primitive operations)
        std::vector<component_info> components;
        std::vector<variant_info> variants;
        std::vector<node*> discriminant_constraint;  ///< (a constrained record subtype: its discriminants' values (expressions), in order)
        std::vector<static_value> discriminant_values;
        std::vector<entity*> discriminant_objects;   ///< (dynamic values: hidden constants)
        bool known_discriminants = false;
        region* formal_scope = nullptr;                     ///< (... the instance's formal region: the names denote them within it)
        ada_type* formal_view = nullptr;                    ///< (a generic formal type's subtype in an instance (denoting the actual's): itself (and its subtypes: it); declarations whose profiles differ in it are distinct, as in the generic (12.3(11)))
        std::vector<std::string> formal_discriminants;     ///< (a formal type with known discriminants within an instance: its discriminants' names (those of the actual's discriminants, by position))
        bool unknown_discriminants = false;
        std::vector<dispatch_slot> slots;   ///< (a tagged type's dispatch table)
        std::vector<ada_type*> interfaces;  ///< (the interfaces it implements)
        predicate_info* predicate = nullptr;    ///< (a subtype with a predicate (its own, or of the subtype it is of))
        bool nonstatic = false;                 ///< (a scalar subtype with a nonstatic constraint, or a subtype of one (4.9(26/3)))
        ada_type* derived_partial = nullptr;    ///< (a type derived from a partial view whose full view is not visible where it is declared: that partial view (its characteristics are the partial view's where that full view is not visible, 7.3.1(5/1)))
        std::vector<std::pair<entity*, bool>> invariants;   ///< (its type invariants' functions (of the current instance); class-wide: inherited)
        ada_type* class_wide = nullptr;     ///< (a tagged type's T'Class)
        ada_type* specific = nullptr;       ///< (a class-wide type's T)
        ada_type* class_subtype = nullptr;  ///< (S'Class, S a constrained subtype of T: a subtype of T'Class (its values those that, converted to T, belong to S, 3.9(14)): S)
        // (access types: the designated subtype; all, constant)
        ada_type* designated = nullptr;
        bool access_all = false;
        bool access_constant = false;
        bool not_null = false;
        entity* storage_pool = nullptr;
        // (access to subprogram: its profile)
        subprogram_info* profile = nullptr;
        bool protected_access = false;
        // (tasks and protected types: their declarations)
        entity* unit = nullptr;             ///< (the task or protected unit (its entity: entries, operations, private components))
        // (the primitive operations (for derivation and dispatching), in the order declared)
        std::vector<entity*> primitives;
        region* declared_in = nullptr;      ///< (the region of its declaration (its primitives are declared there))

        // (representation: the size of its objects in bytes (static) or none (dynamic: see size_object), and alignment)
        std::optional<std::uint64_t> size;
        bool dynamic_size = false;          ///< (a record with components in its objects' tails: size is its fixed part's)
        entity* size_object = nullptr;      ///< (a dynamic size: a hidden constant)
        std::uint64_t alignment = 8u;
        std::optional<std::uint64_t> size_clause;   ///< (T'Size use: in bits)
        std::optional<std::uint64_t> alignment_clause;  ///< (T'Alignment use: in storage elements)
        std::optional<std::uint64_t> component_size_clause;     ///< (an array type's Component_Size clause: in bits (whole storage units, at least its components' size))
        std::uint64_t id = 0u;              ///< (a unique number: e.g. the tag's)
        std::uint64_t tag_address = 0u;     ///< (a tagged type's dispatch table (static data))

        ada_type* base_type() { return base != nullptr ? base : this; }
        ada_type* view() { return full != nullptr ? full->view() : this; }    ///< (the full view, if known)
        bool is_discrete() const { return kind == type_kind::Enumeration || kind == type_kind::Integer || kind == type_kind::Modular || kind == type_kind::UniversalInteger; }
        bool is_integer() const { return kind == type_kind::Integer || kind == type_kind::Modular || kind == type_kind::UniversalInteger; }
        bool is_real() const { return kind == type_kind::Float || kind == type_kind::Fixed || kind == type_kind::Decimal || kind == type_kind::UniversalReal || kind == type_kind::UniversalFixed; }
        bool is_fixed() const { return kind == type_kind::Fixed || kind == type_kind::Decimal || kind == type_kind::UniversalFixed; }
        bool is_numeric() const { return is_integer() || is_real(); }
        bool is_scalar() const { return is_discrete() || is_real(); }
        bool is_access() const { return kind == type_kind::Access || kind == type_kind::AccessSubprogram || kind == type_kind::UniversalAccess; }
        bool is_composite() const { return kind == type_kind::Array || kind == type_kind::Record || kind == type_kind::Task || kind == type_kind::Protected || kind == type_kind::ClassWide || kind == type_kind::Interface; }
        bool is_universal() const { return kind == type_kind::UniversalInteger || kind == type_kind::UniversalReal || kind == type_kind::UniversalFixed || kind == type_kind::UniversalAccess; }
    };

    // --- entities

    enum class entity_kind
    {
        Package,
        Procedure,
        Function,
        Entry,                  ///< (an entry, or an entry family)
        GenericPackage,
        GenericProcedure,
        GenericFunction,
        Type,
        Subtype,
        Variable,
        Constant,
        NamedNumber,
        Parameter,
        LoopParameter,
        Component,
        Discriminant,
        EnumerationLiteral,
        Exception,
        Label,
        Loop,                   ///< (a loop's name)
        Block,                  ///< (a block's name)
        Task,                   ///< (a task unit (type or single))
        Protected,              ///< (a protected unit)
        Attribute               ///< (an attribute treated as a function, e.g. T'Succ as a generic actual)
    };

    enum class parameter_mode { In, Out, InOut, Access };

    struct parameter_info
    {
        entity* ent = nullptr;
        parameter_mode mode = parameter_mode::In;
        ada_type* type = nullptr;
        node* default_expression = nullptr;
        bool aliased = false;
        bool not_null = false;
    };

    // intrinsic operations (predefined operators and attributes as functions)
    enum class intrinsic
    {
        None,
        Add, Subtract, Multiply, Divide, Mod, Rem, Power, Negate, Plus, Abs, Not, And, Or, Xor, AndThen, OrElse,
        Eq, Ne, Lt, Le, Gt, Ge, Concat,
        UniversalFixedMultiply, UniversalFixedDivide, RootMultiply, RootDivide,
        UncheckedConversion, UncheckedDeallocation, Attribute, Import,    ///< (Import: a runtime plugin function (Convention Neos))
        NotEqual        ///< (the "/=" implicitly declared with a user-defined "=" (6.6): not its result (complement_of: the "="))
    };

    struct subprogram_info
    {
        std::vector<parameter_info> parameters;
        ada_type* result = nullptr;         ///< (a function's result subtype)
        bool result_not_null = false;
        bool result_access = false;         ///< (an access result (anonymous access type))
        intrinsic op = intrinsic::None;
        ada_type* operator_type = nullptr;  ///< (an intrinsic operator: the type it is for)
        std::string import_name;            ///< (an imported subprogram: the runtime function, e.g. "ada::file_open")
        std::string external_name;
        bool is_abstract = false;
        bool is_null = false;               ///< (a null procedure)
        bool no_return = false;             ///< (a nonreturning procedure (6.5.1): returning raises Program_Error)
        node* expression = nullptr;         ///< (an expression function's expression)
        node* body = nullptr;               ///< (its body (subprogram body construct))
        node* spec = nullptr;               ///< (its specification)
        entity* renames = nullptr;          ///< (a renaming-as-body or a renaming declaration: the renamed subprogram)
        node* renamed_name = nullptr;
        entity* dispatching_type_owner = nullptr;
        ada_type* controlling = nullptr;    ///< (a primitive operation of a tagged type: the type)
        std::int32_t slot = -1;             ///< (its dispatch table slot)
        bool inherited = false;             ///< (an implicitly declared inherited operation)
        bool undeclared = false;            ///< (an inherited operation of a private part not visible where its type is derived: never declared, its slot only (7.3.1(6)))
        entity* overridden_by = nullptr;     ///< (an inherited operation (visible) overridden in a private part: the overriding one (its body))
        region* instance_hidden = nullptr;  ///< (an operation inherited by a type derived from a formal type's actual (untagged) in an instance from the actual's user-defined one: not visible within that instance, where the formal type's (predefined) ones apply (12.3(15)))
        entity* hidden_by = nullptr;         ///< (an untagged type's inherited operation (visible) overridden in a private part: the overriding one (hiding it where it is visible; elsewhere the inherited one remains, with its parent's body))
        bool separate_body = false;         ///< (its body completes an earlier declaration (calls are checked to be after its elaboration, 3.11(14)))
        entity* parent_operation = nullptr; ///< (an inherited operation: the parent's operation it inherits)
        entity* complement_of = nullptr;    ///< (an implicit "/= (intrinsic NotEqual): the "=" it is the complement of)
        ada_type* derived_from = nullptr;   ///< (an inherited operation: the parent type (whose values are converted))
        std::uint32_t function_index = 0u;  ///< (its WebAssembly function)
        bool defined = false;               ///< (its WebAssembly function is defined)
        bool generated = false;             ///< (its code has been generated)
        int level = 0;                      ///< (its nesting level: library level subprograms are 1)
        entity* family_index = nullptr;     ///< (an entry family: the index)
        ada_type* family_type = nullptr;
        node* barrier = nullptr;
        entity* barrier_function = nullptr;     ///< (a protected entry: its barrier's function (a synthetic function))
        entity* barrier_of = nullptr;           ///< (a barrier's function: its entry)
        std::vector<node*> preconditions;           ///< (specific (Pre))
        std::vector<node*> postconditions;          ///< (specific (Post))
        std::vector<node*> class_preconditions;     ///< (class-wide (Pre'Class): inherited by its overridings)
        std::vector<node*> class_postconditions;    ///< (class-wide (Post'Class): inherited by its overridings)
        ada_type* deep_type = nullptr;          ///< (a type's deep Adjust (or Finalize, deep_finalize): its controlled parts' operations, for class-wide objects (see TagAdjust))
        bool deep_finalize = false;
        bool prefixed_actual = false;       ///< (a formal subprogram whose actual is a prefixed view: a subprogram that calls the operation with the object (its renamed_name))
        entity* renamed_access = nullptr;    ///< (a renaming of an access-to-subprogram value's dereference (8.5.4(2/3)): the hidden constant holding the value, evaluated where the renaming is elaborated)
        entity* implementing_entry = nullptr;   ///< (a task or protected type's implementation of an interface's operation by an entry or protected subprogram (9.4(11.1/3)): it, called on its first parameter)
        bool null_extension_result = false;     ///< (a function with a controlling result inherited by a null extension: its own body (the parent's result, of the extension's tag), 3.9.3(4/2))
        std::vector<entity*> spec_parameters;   ///< (a completed declaration: its own parameters' entities (its contracts name them))
    };

    enum class storage
    {
        None,
        Local,                  ///< (a WebAssembly local)
        Frame,                  ///< (in its subprogram's frame (the Ada memory): offset from the frame's base)
        Static,                 ///< (static data: an address)
        Indirect,               ///< (its address is in a WebAssembly local (e.g. a by-reference parameter, or a dynamically sized object))
        IndirectFrame,          ///< (its address is in a frame slot)
        IndirectStatic,         ///< (its address is in static data)
        Renaming,               ///< (a renamed object: see renamed)
        Constant                ///< (a static constant: no storage (its value is static))
    };

    struct entity
    {
        entity_kind kind;
        std::string name;                   ///< (lower case; an operator's designator, e.g. "+"; a character literal: its text, e.g. 'a')
        node* decl = nullptr;               ///< (its declaration)
        region* scope = nullptr;            ///< (the region it is declared in)
        ada_type* type = nullptr;           ///< (an object's, a function's result's, a type's or subtype's)
        region* inner = nullptr;            ///< (a package, subprogram, task, protected unit, loop or block: its region)
        std::unique_ptr<subprogram_info> sub;
        // (objects)
        static_value value;                 ///< (a static constant's, a named number's, an enumeration literal's position)
        storage where = storage::None;
        std::uint32_t local = 0u;           ///< (Local or Indirect: its local)
        std::int64_t offset = 0;            ///< (Frame: its offset; Static: its address)
        entity* renamed = nullptr;          ///< (a renaming: the renamed entity)
        std::uint64_t sequence = 0u;        ///< (the order of creation (declaration order))
        node* renamed_name = nullptr;       ///< (a renaming: the renamed name)
        node* address_clause = nullptr;     ///< (an object whose address is specified (Address aspect or clause): its address's expression)
        std::uint64_t alignment_clause = 0u;    ///< (an object whose alignment is specified (Alignment aspect or clause): in storage elements)
        std::uint64_t size_clause = 0u;         ///< (an object whose size is specified (Size aspect or clause): in bits)
        std::uint8_t packed_bits = 0u;          ///< (a renaming or loop parameter that denotes a component of a bit-packed array: its bits
                                                ///< (its address is a bit address))
        node* initial = nullptr;            ///< (an object's initial value)
        node* priority = nullptr;           ///< (a task or protected unit's, or the main subprogram's: its Priority or Interrupt_Priority (D.1, D.3))
        bool interrupt_priority = false;    ///< (Interrupt_Priority (its default: Interrupt_Priority'Last))
        bool volatile_object = false;       ///< (an object specified Volatile or Atomic (C.6))
        bool volatile_components = false;   ///< (an object specified Volatile_Components or Atomic_Components (C.6))
        bool aliased = false;
        bool by_reference = false;          ///< (a parameter passed by reference)
        bool captured = false;              ///< (a nested subprogram refers to it)
        bool is_private = false;            ///< (declared in a private part)
        bool child_unit = false;            ///< (a library unit that is a child)
        bool library = false;               ///< (a library unit)
        bool limited_view = false;
        bool completed = false;             ///< (a declaration that has been completed (e.g. a deferred constant))
        bool imported = false;
        bool visible_in_body = true;
        bool declaring = false;             ///< (hidden from all visibility: within its own declaration (8.3(16)))
        entity* completion = nullptr;       ///< (a deferred constant's full declaration; an incomplete type's full declaration)
        entity* generic = nullptr;          ///< (an instance: its generic unit)
        entity* parent = nullptr;           ///< (a child unit's parent unit; an inherited operation's parent type's operation)
        entity* owner = nullptr;            ///< (a component's record type; a discriminant's type; a literal's type; a loop parameter's loop)
        ada_type* designated_unit = nullptr;
        std::uint64_t exception_id = 0u;    ///< (an exception: its identity (static data address))
        std::uint32_t function_level = 0u;  ///< (an object: the level of the subprogram whose frame it is in)
        int elaborated = 0;
        std::int32_t index = 0;             ///< (a component's index in its record type; a parameter's index)
        bool overloadable() const { return kind == entity_kind::Procedure || kind == entity_kind::Function || kind == entity_kind::EnumerationLiteral || kind == entity_kind::Entry || kind == entity_kind::Attribute; }
        bool is_object() const { return kind == entity_kind::Variable || kind == entity_kind::Constant || kind == entity_kind::Parameter || kind == entity_kind::LoopParameter || kind == entity_kind::Component || kind == entity_kind::Discriminant; }
        bool is_subprogram() const { return kind == entity_kind::Procedure || kind == entity_kind::Function || kind == entity_kind::Entry; }
        bool is_type() const { return kind == entity_kind::Type || kind == entity_kind::Subtype; }
        bool is_package() const { return kind == entity_kind::Package || kind == entity_kind::GenericPackage; }
        bool is_generic() const { return kind == entity_kind::GenericPackage || kind == entity_kind::GenericProcedure || kind == entity_kind::GenericFunction; }
    };

    // what a name or expression node is (node::mark's low byte), and whether its prefix is implicitly dereferenced (MarkDeref)
    enum class meaning : std::uint32_t
    {
        None,
        Object,                 ///< (a name of an object (variable, constant, parameter, component, dereference ...))
        Value,                  ///< (an expression's value)
        Call,                   ///< (a function or procedure call: see call_info)
        Index,                  ///< (an indexed component)
        Slice,
        Conversion,             ///< (a type conversion)
        Component,              ///< (a selected component of a record (or protected) object)
        Package,
        Type,
        Subprogram,             ///< (a name of a subprogram that is not called (e.g. a prefix of 'Access))
        Literal,                ///< (an enumeration literal)
        Exception,
        Dereference,            ///< (an explicit dereference (.all))
        Entry,                  ///< (a name of an entry (or entry family member) that is not called)
        StringLiteral,
        Unit                    ///< (a name of a task, protected unit, block or loop (e.g. a prefix of an expanded name))
    };
    inline constexpr std::uint32_t MarkDeref = 0x100u;
    inline meaning meaning_of(node const* aNode) { return static_cast<meaning>(aNode->mark & 0xFFu); }
    // (the analysis's generation: changed whenever a node's meaning (or a declaration) changes (cached interpretations are of a
    // generation))
    inline std::uint64_t& analysis_generation() { static thread_local std::uint64_t sGeneration = 0u; return sGeneration; }
    inline void set_meaning(node* aNode, meaning aMeaning)
    {
        // (a change of meaning: interpretations computed before it are stale)
        auto const mark = (aNode->mark & ~0xFFu) | static_cast<std::uint32_t>(aMeaning);
        if (mark != aNode->mark)
        {
            aNode->mark = mark;
            ++analysis_generation();
        }
    }

    // a call's resolution: the subprogram called and its actuals (by formal; null: the default)
    struct call_info
    {
        entity* callee = nullptr;
        std::vector<node*> actuals;
        node* object = nullptr;             ///< (a prefixed view's object (its first actual), or an entry call's task or protected object)
        bool prefixed = false;
        bool dispatching = false;           ///< (a call of a primitive operation whose controlling operand is class-wide)
        bool contract_dispatching = false;  ///< (in a class-wide condition: its controlling operand is a controlling formal (the operation of the type of the subprogram the condition is checked for is called, 6.1.1(7/5)))
        node* family_index = nullptr;       ///< (an entry family's index)
    };

    // --- declarative regions

    struct region
    {
        entity* owner = nullptr;            ///< (the package, subprogram, task, protected unit, block, loop or record type; null: Standard's parent)
        region* parent = nullptr;
        std::unordered_multimap<std::string, entity*> names;    ///< (the declarations directly in it)
        std::vector<entity*> declarations;  ///< (in order)
        std::vector<entity*> used;          ///< (use clauses in it: packages)
        std::vector<ada_type*> used_types;  ///< (use type clauses)
        std::vector<ada_type*> used_all_types;  ///< (use all type clauses)
        std::size_t private_start = static_cast<std::size_t>(-1);  ///< (a package: the first declaration of its private part)
        bool private_visible = false;       ///< (a package's private part is visible (its body, its private part, or a private child))
        bool is_package = false;
        bool package_body = false;          ///< (a package body's declarations (its parent: the package's))
        node* private_part = nullptr;       ///< (a package: its private part)
        bool is_subprogram = false;
        bool master = false;                ///< (a block statement's (a master, 7.6.1(3/2)); subprograms' (is_subprogram) are too)
        bool generic_formals = false;
        std::vector<ada_type*> formal_actuals;  ///< (an instance's formal region: the actuals (base types) of its untagged formal types other than formal derived types)
        node* visibility_cutoff = nullptr;      ///< (an instance's formal region: its parent's declarations are visible only if declared before this (the generic's declaration, or its body when that is analysed), as in the generic (12.3(16)))
        std::vector<std::pair<ada_type*, std::size_t>> formal_ancestor_slots;  ///< (an instance's formal region: the actuals (base types) of its tagged formal derived types, with their ancestors' dispatch table sizes (the operations corresponding to the generic's implicit declarations, 12.3(18)))
        int level = 0;                      ///< (the nesting level of subprograms: 0 library (packages at library level))
        region* enclosing = nullptr;        ///< (a nested package body's: the region it is in (outward after its package's))
        void add(entity* aEntity) { names.emplace(aEntity->name, aEntity); declarations.push_back(aEntity); }
    };

    // (the next region outward: a nested package body's package's region, then the region the body is in)
    inline region* outward(region* aRegion, region*& aJump)
    {
        if (aRegion->enclosing != nullptr)
        {
            aJump = aRegion->enclosing;
            return aRegion->parent;
        }
        if (aJump != nullptr)
        {
            auto* next = aJump;
            aJump = nullptr;
            return next;
        }
        return aRegion->parent;
    }
}
