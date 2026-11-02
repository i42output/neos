/*
  program.hpp

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

// Ada: a program's analysis (its units' static semantics: names, types, overload resolution, static values) and code 
// generation (a WebAssembly function for each subprogram; the entry point elaborates the library units and calls the main 
// subprogram).
//
// Values: discrete, fixed point and access values are i64s; floating point values are f32s (digits <= 6) or f64s; composite
// values are the i64 addresses (in the Ada memory: external memory 3) of their objects. An array's "reference" is the address
// of its descriptor: its data's address then each dimension's first and last (i64s); a constrained array object's descriptor
// precedes its data. Composite results are returned on the secondary stack (their reference). Scalar parameters of mode in
// are passed by value; other parameters by reference (out and in out scalars: the address of a copy). A nested subprogram is
// passed its static link (the frame of the subprogram that encloses it) first. An exception sets the global %ada.exc (and the
// runtime has the occurrence): code that called a subprogram that may raise one checks it and branches to the handlers or
// returns.

#pragma once

#include <functional>
#include <list>
#include <set>
#include "compiler.hpp"
#include "model.hpp"

namespace neos::compilers::ada
{
    // an interpretation of an expression (overload resolution)
    struct interpretation
    {
        ada_type* type = nullptr;           ///< (its type)
        entity* ent = nullptr;              ///< (the entity (e.g. the function called) that it is)
    };

    // a library unit (or a subunit) of the program
    struct unit_info
    {
        std::string name;                   ///< (lower case, e.g. "ada.text_io")
        node* unit = nullptr;               ///< (the compilation unit construct)
        node* item = nullptr;               ///< (its library item (or proper body of a subunit))
        bool body = false;
        bool subunit = false;
        bool main_file = false;
        std::vector<std::string> withs;
        std::vector<std::string> limited_withs;     ///< (limited with clauses: not dependences)
        std::vector<std::string> elaborates;        ///< (the units its context's pragmas Elaborate and Elaborate_All name: their bodies are elaborated first)
        entity* ent = nullptr;
        region* context = nullptr;          ///< (the names its context clauses make visible)
        bool analysed = false;
        bool analysing = false;
    };

    class function_generator;

    std::u32string literal_string(node const* aNode);       ///< (a string literal's characters, as written)
    char32_t literal_character(node const* aNode);
    bool is_operator_symbol(node const* aNode);              ///< (a string literal that is an operator symbol)

    class program
    {
        friend class function_generator;
    public:
        program(compiler& aCompiler, std::vector<node*> const& aCompilations);
        ~program();
    public:
        void generate();
        compiler& comp() { return iCompiler; }
        i_code_generator& gen() { return iCompiler.gen(); }
        [[noreturn]] void error(node const* aNode, std::string const& aMessage);
        // --- the model
    public:
        entity* new_entity(entity_kind aKind, std::string const& aName, node* aDecl, region* aScope);
        ada_type* new_type(type_kind aKind, std::string const& aName = {});
        region* new_region(entity* aOwner, region* aParent);
        ada_type* new_subtype(ada_type* aOf, std::string const& aName = {});
        // --- units
    private:
        void collect_units();
        void order_units();
        void analyse_unit(unit_info& aUnit);
        unit_info* find_unit(std::string const& aName, bool aBody);
        void setup_standard();
        void apply_context(node* aContext, region* aRegion);
        // --- declarations (analysis of a declarative part: entities, types, and code for their elaboration)
    public:
        void declarations(node* aDeclarations, region* aRegion);
        void declaration(node* aDecl, region* aRegion);
        void package_declaration(node* aDecl, region* aRegion, entity* aEntity = nullptr);
        void package_body(node* aDecl, region* aRegion);
        entity* subprogram_declaration(node* aDecl, region* aRegion);
        void subprogram_body(node* aDecl, region* aRegion);
        void subprogram_specification(node* aSpec, entity* aEntity, region* aParameters);
        void type_declaration(node* aDecl, region* aRegion);
        void subtype_declaration(node* aDecl, region* aRegion);
        void object_declaration(node* aDecl, region* aRegion);
        void number_declaration(node* aDecl, region* aRegion);
        void exception_declaration(node* aDecl, region* aRegion);
        void renaming_declaration(node* aDecl, region* aRegion);
        void use_clause(node* aClause, region* aRegion);
        void pragma(node* aPragma, region* aRegion);
        void representation_clause(node* aClause, region* aRegion);
        void generic_declaration(node* aDecl, region* aRegion);
        void instantiation(node* aDecl, region* aRegion);
        void task_declaration(node* aDecl, region* aRegion);
        void protected_declaration(node* aDecl, region* aRegion);
        void body_stub(node* aDecl, region* aRegion);
        // --- types
    public:
        ada_type* subtype_indication(node* aIndication, region* aRegion, bool aAllowUnconstrained = true);
        ada_type* subtype_mark(node* aMark, region* aRegion);
        ada_type* type_definition(node* aDefinition, std::string const& aName, entity* aEntity, region* aRegion, node* aDiscriminants);
        ada_type* constrain(ada_type* aType, node* aConstraint, region* aRegion);
        ada_type* constrain_view(ada_type* aType, node* aConstraint, region* aRegion);
        void implicit_inequality(entity* aEquality, region* aRegion);
        node* default_value_of(ada_type* aType, bool aComponent);
        region* iResolving = nullptr;              ///< (the region of the expression being resolved (where the full views of private types are visible: see covers))
        std::set<node const*> iVariableNames;      ///< (names in a variable context (an assignment's target, an out or in out actual, a renamed object, and their prefixes): a generalized indexing's Variable_Indexing (4.1.6(17/3)))
        void variable_name(node* aName);
        std::vector<entity*> indexing_candidates(ada_type* aType, entity* aFunction);
        bool in_ancestor_part(component_info const& aComponent, std::size_t aIndex, ada_type* aAncestor);
        std::map<node const*, ada_type*> iImplicitDereferences;     ///< (expressions of a reference type implicitly dereferenced: that type)
        ada_type* implicit_dereference_of(node const* aNode) const { auto i = iImplicitDereferences.find(aNode); return i != iImplicitDereferences.end() ? i->second : nullptr; }
        std::optional<std::uint64_t> reference_offset(ada_type* aReference);
        bool covers_dereferenced(ada_type* aExpected, ada_type* aActual)
        {
            // (covered, or a reference type whose designated type is covered (implicitly dereferenced))
            if (covers(aExpected, aActual))
                return true;
            if (aExpected == nullptr || aActual == nullptr || aExpected == iVoid || (aActual->view()->kind != type_kind::Record && aActual->view()->kind != type_kind::Private))
                return false;
            auto* d = implicit_dereference(aActual);
            return d != nullptr && d != aActual && covers(aExpected, d);
        }
        std::set<entity*> iImplicitInequalities;
        ada_type* full_subtype(ada_type* aSubtype, ada_type* aFull);
        void derive_operations(ada_type* aDerived, ada_type* aParent, region* aRegion);
        region* formal_actual_instance(ada_type* aType, region* aRegion);
        static bool instance_hidden(entity* aOperation, region* aRegion);
        void add_primitive(ada_type* aType, entity* aOperation);
        void layout(ada_type* aType);
        bool bit_field(component_info const& aComponent);          ///< (a scalar component that a component clause places at bits other than whole storage units)
        std::uint64_t bit_field_span(component_info const& aComponent);    ///< (the storage units it is loaded and stored in (1, 2, 4 or 8))
        component_info const* bit_field_of(ada_type* aRecord, entity* aComponent);  ///< (a record type's component, if it is a bit field)
        bool covers(ada_type* aExpected, ada_type* aActual);                ///< (an expression of type aActual can be of type aExpected)
        bool same_type(ada_type* a, ada_type* b);
        bool is_descendant(ada_type* aType, ada_type* aAncestor);   ///< (a tagged type derived from (or the same as) another (class-wide: its specific type's))
        ada_type* universal_integer() { return iUniversalInteger; }
        ada_type* universal_real() { return iUniversalReal; }
        ada_type* boolean() { return iBoolean; }
        ada_type* integer() { return iInteger; }
        ada_type* character() { return iCharacter; }
        ada_type* string_type() { return iString; }
        std::optional<bigint> low_bound(ada_type* aType);           ///< (static)
        std::optional<bigint> high_bound(ada_type* aType);
        // --- names and expressions
    public:
        std::vector<entity*> visible(std::string const& aName, region* aRegion);    ///< (the entities that a direct name denotes)
        std::vector<entity*> selected(entity* aPrefix, std::string const& aName, region* aRegion);
        void prefixed_view_object(node* aView, region* aRegion);
        std::uint64_t standard_pool();
        std::uint64_t stream_size(ada_type* aType);                 ///< (an elementary type's stream elements per value: its specified Stream_Size's (its ancestor's), else its size's (13.13.2(1.2/3)))                               ///< (the standard storage pool's object (its address))
        void fixed_base_range(ada_type* aFirst);
        bool per_object_access_constraint(component_info const& aComponent) const;   ///< (a component with an access discriminant constrained by a per-object expression (initialized after the others, 3.3.1(20.1/3)))
        std::vector<component_info const*> initialization_order(ada_type* aRecord);   ///< (a record type's components in the order in which they are initialized)
        entity* resolve_entity(node* aName, region* aRegion);                      ///< (a name that denotes a non-overloadable entity)
        ada_type* resolve(node* aExpression, ada_type* aExpected, region* aRegion);  ///< (resolves it (annotating it): its type)
        ada_type* condition(node* aCondition, region* aRegion);
        ada_type* resolve_in_class(node* aExpression, region* aRegion, bool (*aInClass)(ada_type const*));
        bool class_wide_access_actual(ada_type* aFormal, ada_type* aActual);  ///< (resolves a condition (of any boolean type): its type)
        std::vector<interpretation> interpretations(node* aExpression, region* aRegion);
        std::vector<interpretation> compute_interpretations(node* aExpression, region* aRegion);
        struct cached_interpretations { region* where = nullptr; std::uint64_t generation = 0u; std::vector<interpretation> result; };
        std::unordered_map<node*, cached_interpretations> iInterpretations;
        static_value evaluate(node* aExpression, region* aRegion);                 ///< (its static value, if static)
        bool is_static(node* aExpression);
        // --- statements
    public:
        void statements(node* aStatements, region* aRegion);
        void statement(node* aStatement, region* aRegion);
        // --- code generation
    public:
        std::uint32_t runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);
        std::uint32_t global(std::string const& aName, type aType);
        std::uint64_t static_data(std::uint64_t aSize, std::uint64_t aAlignment = 8u, std::uint64_t aSkew = 0u);     ///< (its address (aligned at aSkew bytes from it))
        std::uint64_t static_string(std::string const& aBytes);   ///< (an array descriptor (1 .. n) and its data: the descriptor's address)
        void define_subprogram(entity* aSubprogram);
        std::uint32_t define_procedure(std::string const& aName);
        void generate_code();
        std::uint64_t static_bytes(std::string const& aBytes);    ///< (static data initialized with the bytes: its address)
        void add_static_bytes(std::uint64_t aAddress, std::string const& aBytes) { iStaticBytes.emplace_back(aAddress, aBytes); }
        std::uint64_t size_descriptor(ada_type* aBase);             ///< (a dynamically sized record type's objects' size's description (see record_size_dynamic))
        std::map<ada_type*, std::uint64_t> iSizeDescriptors;
        std::uint64_t enumeration_names(ada_type* aType);         ///< (an enumeration type's literals' images (NUL separated))
        std::uint64_t tag_of(ada_type* aType);
        std::int64_t accessibility_level(region* aRegion);         ///< (the accessibility level of a region's declarations: the number of masters (subprogram, task and entry bodies, blocks) it is within (0: library level))
        std::int64_t type_level(ada_type* aType);                  ///< (a type's accessibility level: its declaration's static depth)
        entity* entry_implementation(ada_type* aType, entity* aOperation);
        entity* deep_operation(ada_type* aType, bool aFinalize);
        bool iNormalizeScalars = false;                         ///< (pragma Normalize_Scalars or Initialize_Scalars in the partition (H.1))
        bool iCeilingLocking = false;                           ///< (pragma Locking_Policy (Ceiling_Locking) in the partition (D.3))
        bool iPriorityQueuing = false;                          ///< (pragma Queuing_Policy (Priority_Queuing) in the partition (D.4))
        std::vector<entity*> iInterruptHandlers;                ///< (the protected procedures that can be interrupt handlers (C.3.1))
        std::map<entity*, std::vector<std::pair<entity*, node*>>> iAttachHandlers;  ///< (by protected unit: its procedures attached to interrupts (C.3.1))
        std::optional<std::int64_t> invalid_value(ada_type* aType);   ///< (a scalar subtype's invalid representation (an out of range one), if any)  ///< (a tagged type's deep Adjust or Finalize: its controlled parts' (see TagAdjust))    ///< (a task or protected type's entry implementing an interface's operation: a procedure that calls it)
        std::map<std::pair<ada_type*, entity*>, entity*> iEntryImplementations;                    ///< (a tagged type's tag: its dispatch table's address)
        std::uint64_t tag_table_cell();                           ///< (a cell: the address of a table of the program's tags)
        std::vector<std::uint64_t> iTagAddresses;
        std::uint64_t iTagTableCell = 0u;
        std::uint64_t subprogram_descriptor(entity* aSubprogram); ///< (a library level subprogram's access value (static))
        std::uint64_t scalar_size(ada_type* aType);
        std::uint64_t object_size(ada_type* aType);
        std::uint64_t component_size(ada_type* aArray);
        std::uint64_t maximum_size(ada_type* aType);
        std::uint32_t sp_global() const { return iSpGlobal; }
        std::uint32_t limit_global() const { return iLimitGlobal; }
        std::uint32_t exc_global() const { return iExcGlobal; }
        std::uint32_t denoted_global() const { return iDenotedGlobal; }
        std::vector<entity*> contract_chain(entity* aOperation);   ///< (a primitive operation and those it overrides (their declarations), its own first)
        ada_type* void_type() const { return iVoid; }
        entity* library_exception(std::string const& aUnit, std::string const& aName);   ///< (an exception declared in a library package, if it is in the program)
        entity* assertion_error();                                ///< (Ada.Assertions.Assertion_Error, if withed)
        bool raising_import(std::string const& aImport) const;    ///< (an imported runtime function that may raise an exception)
        static_value evaluate_attribute_static(ada_type* aType, std::string const& aAttribute);
        struct dynamic_bound_info
        {
            ada_type* subtype;
            node* low;
            node* high;
            entity* lowObject;
            entity* highObject;
        };
        std::vector<dynamic_bound_info> const& dynamic_bounds() const { return iDynamicBounds; }
        struct dynamic_constraint_info
        {
            node* value;                ///< (a discriminant constraint's value that is not static (nor depends on a discriminant))
            entity* object;             ///< (the hidden constant that holds it, elaborated where the constraint is)
        };
        std::vector<dynamic_constraint_info> const& dynamic_constraints() const { return iDynamicConstraints; }
        struct compatibility_check_info
        {
            node* range;            ///< (a range constraint's range, an index constraint's discrete range)
            ada_type* mark;         ///< (the subtype it constrains (its type mark's, an index subtype))
            ada_type* subtype;      ///< (the subtype of the range; null: a discriminant constraint's value (range) of the discriminant's subtype (mark))
        };
        std::map<entity*, node*> iObjectSubtypes;     ///< (an object (not the first) of a multiple object declaration: its own subtype indication (3.3.1(7)))
        std::vector<compatibility_check_info> iCompatibilityChecks;     ///< (3.2.2(11), 3.6.1(7): checked when elaborated (a non-null range within its type mark's))
        void compatibility_check(node* aRange, ada_type* aMark, ada_type* aSubtype);
        bool refers_to_discriminant(node const* aExpression) const;
        struct instance_constant
        {
            entity* constant;
            entity* instance;
        };
        std::vector<instance_constant> const& instance_constants() const { return iInstanceConstants; }
        // (a tag: its parent's tag, its expanded name's address, its objects' size (0: dynamic), then its dispatch slots)
        static constexpr std::uint64_t TagLink = 24u;       ///< (a tag's static link: its type's primitive operations' (0: library level))
        static constexpr std::uint64_t TagInterfaces = 32u; ///< (a tag's interface table: (its count, then) each interface's tag and its operations' table)
        static constexpr std::uint64_t TagLevel = 40u;      ///< (a tag's type's accessibility level (its declaration's static depth))
        static constexpr std::uint64_t TagAdjust = 48u;     ///< (a tag's type's deep Adjust (its controlled parts', for class-wide objects; 0: none))
        static constexpr std::uint64_t TagFinalize = 56u;   ///< (a tag's type's deep Finalize (likewise))
        static constexpr std::uint64_t TagFlags = 64u;      ///< (a tag's type's flags: bit 0, abstract)
        static constexpr std::uint64_t TagExternal = 72u;   ///< (a tag's type's specified external tag (a static string; 0: its expanded name))
        static constexpr std::uint64_t TagSlots = 80u;
        static constexpr std::uint32_t TaskHandle = 8u;     ///< (a task or protected object's runtime handle (a task's id, a protected object's), after its tag (a type with interfaces'))
        static constexpr std::uint64_t StaticBase = 0x10000u;
        bool has_controlled();
        std::uint64_t fixed_size(ada_type* aType);
        bool has_masters() { return has_controlled() || !iTaskUnits.empty(); }
        ada_type* self_type(entity* aSubprogram);                 ///< (a task body's, or a protected operation's: the type of its object)
        enum class subprogram_kind { Normal, ProtectedEntry, Barrier };
        subprogram_kind subprogram_form(entity* aSubprogram);
        std::uint64_t protected_table(ada_type* aType);           ///< (a protected type's entries' barriers and bodies (static))                                    ///< (Ada.Finalization is part of the program)
        bool is_controlled(ada_type* aType);
        bool needs_finalization(ada_type* aType);
        bool inferable_discriminants(node* aExpression);         ///< (an expression of an unchecked union type: B.3.3(13/2))
        bool unconstrained_union_part(ada_type* aType);           ///< (a subcomponent of an unchecked union type whose nominal subtype is unconstrained)
        int controlled_slot(std::string const& aOperation, ada_type* aType = nullptr);
        void note_function_type(std::uint32_t aFunction, std::vector<type> const& aParameters, type aResult);
        void note_indirect_type(std::uint32_t aSignature, std::vector<type> const& aParameters, type aResult);
        void note_global_type(std::uint32_t aGlobal, type aType);
    private:
        std::map<std::uint32_t, std::string> iFunctionTypes;    ///< (for validation: functions' signatures (result, parameters: "lli"))
        std::map<std::uint32_t, std::string> iIndirectTypes;
        std::map<std::uint32_t, char> iGlobalTypes;
        std::uint32_t iSpGlobal = 0u;
        std::uint32_t iLimitGlobal = 0u;
        std::uint32_t iExcGlobal = 0u;
        std::uint32_t iDenotedGlobal = 0u;     ///< (a dispatching call of an operation with class-wide preconditions: its controlling type's tag (6.1.1(33/3)))
        std::uint32_t iFunctionCount = 0u;
        std::vector<std::pair<std::uint64_t, std::uint32_t>> iFunctionSlots;   ///< (static data that holds functions' indexes: address, function)
        std::map<ada_type*, std::uint64_t> iEnumerationNames;
        std::map<entity*, std::uint64_t> iSubprogramDescriptors;
        std::map<ada_type*, bool> iNeedsFinalization;
        std::map<ada_type*, std::uint64_t> iProtectedTables;
        struct deferred_aspect { ada_type* type; std::string name; node* value; entity* ent = nullptr; entity* type_entity = nullptr; };
        void resolve_deferred_aspects(region* aRegion);
        void current_instance_components(node* aExpression, entity* aType);   ///< (an aspect of a record type: its components' names denote the current instance's)
        std::map<region*, std::vector<deferred_aspect>> iDeferredAspects;
        bool is_static_resolvable(node* aExpression, region* aRegion);
    public:
        std::map<ada_type*, entity*> iObjectCollections;   ///< (access types whose allocators create objects that need finalization: their collections (hidden objects))
        std::map<ada_type*, entity*> iCollections;     ///< (access types whose allocators create tasks: their collections (hidden objects))
        bool needs_tasks(ada_type* aType);
    private:
    private:
        compiler& iCompiler;
        std::vector<node*> iCompilations;
        std::deque<entity> iEntities;
        std::deque<ada_type> iTypes;
        std::deque<region> iRegions;
        std::deque<predicate_info> iPredicates;
        std::map<region*, std::map<std::string, bool>> iAssertionPolicies;
        std::map<region*, std::vector<std::pair<char const*, node*>>> iDefaultPools;   ///< (pragma Default_Storage_Pool in a region (from where), or an instance's aspect (from its start): the pool (null: none))
        void default_storage_pool(ada_type* aAccess, node* aDecl, region* aRegion);    ///< (pragma Assertion_Policy: the assertion kinds (or "") ignored (or checked) in a region)
        std::set<node const*> iIgnoredAssertions;                            ///< (pragma Assert ... whose policy is Ignore)
        bool assertion_ignored(std::string const& aKind, region* aRegion) const;
        std::deque<subprogram_info> iSubprograms;
        std::vector<unit_info> iUnits;
        std::vector<unit_info*> iOrder;
        region* iStandard = nullptr;        ///< (package Standard's region)
        region* iLibrary = nullptr;         ///< (the library: the parent of Standard's region, where library units are declared)
        entity* iStandardPackage = nullptr;
        ada_type* iUniversalInteger = nullptr;
        ada_type* iUniversalReal = nullptr;
        ada_type* iUniversalFixed = nullptr;
        ada_type* iUniversalAccess = nullptr;
        ada_type* iStringLiteral = nullptr;
        ada_type* iCharacterLiteral = nullptr;
        ada_type* iBracketAggregateType = nullptr;
        ada_type* iAggregateType = nullptr;
        ada_type* iVoid = nullptr;
        ada_type* iBoolean = nullptr;
        ada_type* iInteger = nullptr;
        ada_type* iNatural = nullptr;
        ada_type* iCharacter = nullptr;
        ada_type* iWideCharacter = nullptr;
        ada_type* iWideWideCharacter = nullptr;
        ada_type* iString = nullptr;
        ada_type* iFloat = nullptr;
        ada_type* iLongFloat = nullptr;
        ada_type* iDuration = nullptr;
        ada_type* iLongLongInteger = nullptr;
        ada_type* iRootInteger = nullptr;
        ada_type* iRootReal = nullptr;
        std::uint64_t iNextTypeId = 1u;
        // (code generation)
        std::map<std::string, std::uint32_t> iGlobals;
        std::uint32_t iGlobalsIndexed = 0u;
        std::uint64_t iStaticSize = 0u;
        std::vector<std::pair<std::uint64_t, std::string>> iStaticBytes;    ///< (static data's initial bytes: address, bytes)
        std::map<std::string, std::uint64_t> iStaticStrings;
        std::vector<entity*> iElaboration;  ///< (the library units' elaboration procedures, in order)
        entity* iMain = nullptr;
        unit_info* iCurrentUnit = nullptr;
        unit_info* iDeclaringUnit = nullptr;            ///< (the library unit whose declaration is being analysed (until it registers))
        std::vector<entity*> iSubprogramsToGenerate;    ///< (the subprograms whose bodies have code, in order)
        std::vector<std::pair<node*, region*>> iPackageElaborations;  ///< (library packages (specification, body) to elaborate)
        std::uint64_t iNextExceptionId = 1u;
    public:
        void register_library_unit(entity* aEntity);
        entity* declare(entity_kind aKind, node* aDefining, region* aRegion);     ///< (a declaration's defining name: its entity)
        node* clone(node* aNode);
        bool indexed_attribute(node* aAttribute);
        bool indexed_result_fits(ada_type* aResult, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion);   ///< (a function's array result indexed by the arguments: they fit)   ///< (X'Old (I) ...: rewritten as an Apply of the attribute)
        entity* find_completion(entity* aSubprogram, region* aRegion);
        entity* generic_completion(node* aSpec, region* aRegion);
        bool type_conformant(subprogram_info const& a, subprogram_info const& b);
        bool formal_distinct(subprogram_info const& a, subprogram_info const& b);
        std::uint64_t object_alignment(entity* aObject);
        std::uint64_t type_alignment_clause(ada_type* aType);
        void declare_predefined_operators(ada_type* aType, region* aRegion);
        std::vector<entity*> const& operators_of(ada_type* aType);
        ada_type* access_definition(node* aDefinition, region* aRegion);
        ada_type* class_wide(ada_type* aType);
        ada_type* discrete_range(node* aRange, ada_type* aIndexType, region* aRegion);
        ada_type* resolve_range_bounds(node* aRange, ada_type* aExpected, region* aRegion);
        void dynamic_bounds(ada_type* aSubtype, node* aLow, node* aHigh, region* aRegion);
        void discriminant_part(node* aPart, ada_type* aType, region* aRegion);
        void component_list(node* aComponents, ada_type* aType, region* aRegion, int aVariant);
        std::pair<std::optional<bigint>, std::optional<bigint>> choice_range(node* aChoice, ada_type* aType, region* aRegion);
        ada_type* derive(ada_type* aParent, std::string const& aName, entity* aEntity, region* aRegion, node* aExtension, node* aDiscriminants);
        subprogram_info* profile_of(node* aProfile, region* aRegion);
        bool is_subtype_name(node* aName, region* aRegion);
        ada_type* range_of(node* aRangeAttribute, region* aRegion);     ///< (X'Range: the subtype of its range)
        entity* resolve_subprogram_name(node* aName, entity* aProfile, region* aRegion);   ///< (a renamed or actual subprogram whose profile matches)
        void aspects(node* aDecl, entity* aEntity, region* aRegion);
        void add_primitive_if(entity* aSubprogram, region* aRegion);
        intrinsic intrinsic_operation(entity* aSubprogram);
        void pragma_extra(node* aPragma, std::string const& aName, std::vector<node*> const& aArguments, region* aRegion);
        void stream_or_other_attribute_clause(node* aClause, ada_type* aType, std::string const& aAttribute, node* aExpression, region* aRegion);
        ada_type* enumeration_rep_array(ada_type* aType);
        void task_body(node* aDecl, region* aRegion);
        void protected_body(node* aDecl, region* aRegion);
    private:
        std::map<ada_type*, std::vector<entity*>> iOperators;
        std::deque<node> iClones;
        std::map<node*, region*> iBodyRegions;
        entity* iCurrentSubprogram = nullptr;
        entity* iClassWideContract = nullptr;   ///< (resolving a class-wide precondition or postcondition: its subprogram)
        std::vector<entity*> iExceptions;
        std::vector<dynamic_bound_info> iDynamicBounds;
        std::map<node const*, call_info> iCalls;
    public:
        call_info const* call_of(node const* aNode) const { auto c = iCalls.find(aNode); return c != iCalls.end() ? &c->second : nullptr; }
        std::string full_name(entity* aEntity) const;       ///< (its expanded name, e.g. "ada.text_io.put_line")
        bool private_part_visible(entity* aPackage, region* aFrom) const;
        bool within_scope(region* aScope, region* aFrom) const;
        std::vector<entity*> use_visible(std::string const& aName, region* aRegion);
        std::vector<interpretation> name_interpretations(node* aName, region* aRegion);
        ada_type* resolve_name(node* aName, ada_type* aExpected, region* aRegion);
        ada_type* resolve_apply(node* aApply, ada_type* aExpected, region* aRegion);
        ada_type* resolve_selected(node* aSelected, ada_type* aExpected, region* aRegion);
        bool class_wide_actual(ada_type* aActual, ada_type* aFormal);
        bool match_call(entity* aCallee, std::vector<node*> const& aArguments, node* aObject, region* aRegion, std::vector<node*>* aActuals, bool aExact);
        ada_type* finish_call(node* aCall, entity* aCallee, std::vector<node*> const& aArguments, node* aObject, region* aRegion);
        std::vector<entity*> prefix_entities(node* aPrefix, region* aRegion);    ///< (the entities that a prefix (a direct name or expanded name) denotes; empty: a value)
        ada_type* designated_of(ada_type* aType);           ///< (an access type's designated subtype (its full view))
        bool is_discrete_range(node* aNode, region* aRegion);
        ada_type* resolve_operator(node* aOperator, ada_type* aExpected, region* aRegion);
        ada_type* resolve_attribute(node* aAttribute, ada_type* aExpected, region* aRegion);
        ada_type* resolve_aggregate(node* aAggregate, ada_type* aExpected, region* aRegion);
        std::vector<entity*> operator_candidates(std::string const& aDesignator, std::vector<node*> const& aOperands, region* aRegion);
        region* selected_operator_scope(node* aPrefix, region* aRegion);
        bool within(entity* aUnit, region* aRegion) const;
        std::vector<entity*> prefixed_candidates(ada_type* aType, std::string const& aName);
        void note_reference(entity* aObject, region* aRegion);
        void resolve_conversion_operand(node* aOperand, ada_type* aTarget, region* aRegion);
        ada_type* resolve_indexing(node* aApply, node* aPrefix, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion);
        ada_type* resolve_generalized_indexing(node* aApply, node* aPrefix, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion);
        std::vector<entity*> prefer(std::vector<entity*> const& aCandidates, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion);
        ada_type* attribute_type(node* aAttribute, region* aRegion);    ///< (an attribute's type, without resolving it)
        static_value evaluate_attribute(node* aAttribute, region* aRegion);
        static_value fold(intrinsic aOp, ada_type* aType, std::vector<static_value> const& aOperands);
        ada_type* aggregate_component_type(ada_type* aType, std::size_t aDimension);
        ada_type* target_type() const { return iTargetType; }
        void set_target_type(ada_type* aType) { iTargetType = aType; }
        ada_type* string_type_for(ada_type* aCharacter);
        void iterator_specification(node* aSpecification, region* aRegion);
        ada_type* string_type_named(std::string const& aName);     ///< (a type of Standard, e.g. "wide_string")
        ada_type* system_type(std::string const& aName);           ///< (a type of package System (if withed), e.g. "address")
        ada_type* tags_type(std::string const& aName);
        ada_type* task_identification_type();
        ada_type* library_type(std::string const& aUnit, std::string const& aName);
        ada_type* resolve_array_aggregate(node* aAggregate, ada_type* aType, std::size_t aDimension, region* aRegion);
        std::vector<node*> const* aggregate_of(node const* aAggregate) const { auto a = iAggregates.find(aAggregate); return a != iAggregates.end() ? &a->second : nullptr; }
        void instantiate(node* aInstantiation, entity* aGeneric, entity* aInstance, region* aRegion, entity* aParentInstance = nullptr);  ///< (aParentInstance: a generic child's, named through an instance of its parent (10.1.1(19)))
        node* generic_body(entity* aGeneric);
        ada_type* iterator_cursor_type(ada_type* aIterator);
        ada_type* iterator_element_type(ada_type* aContainer);
        entity* return_owner(region* aRegion);
        void accept_statement(node* aAccept, region* aRegion);
        void resolve_requeue(node* aRequeue, region* aRegion);
        ada_type* resolve_container_aggregate(node* aAggregate, ada_type* aType, region* aRegion);
        entity* aspect_subprogram(ada_type* aType, std::string const& aAspect, bool aInherited = true);   ///< (e.g. Constant_Indexing's function)    ///< (a loop parameter or iterator specification: its parameter declared in the region)
    private:
        ada_type* iTargetType = nullptr;
        std::map<node const*, region*> iDeclareRegions;
        std::map<node const*, std::vector<node*>> iAggregates;     ///< (a record aggregate's values, by component index (null: default))
        std::map<entity*, node*> iGenericBodies;
        node* type_aspect(ada_type* aType, std::string const& aAspect);
        std::map<entity*, std::uint64_t> iElaborationFlags;
        std::uint64_t elaboration_flag(entity* aSubprogram)
        {
            // (a subprogram's body is elaborated: a static cell (zero until it is))
            auto existing = iElaborationFlags.find(aSubprogram);
            if (existing != iElaborationFlags.end())
                return existing->second;
            return iElaborationFlags[aSubprogram] = static_data(8u, 8u);
        }
        bool generic_body_follows(entity* aGeneric, node const* aInstantiation)
        {
            // (a generic unit's body: in the same source after an instantiation (its elaboration is then checked))
            node const* body = generic_body(aGeneric);
            return body != nullptr && body->file == aInstantiation->file && body->source.data() > aInstantiation->source.data();
        }
        bool elaboration_checked(entity* aSubprogram) const
        {
            // (a call of a subprogram declared (not at library level) before its body: checked to be after the body's elaboration)
            // (a library package's too (3.11(14)))
            return aSubprogram != nullptr && aSubprogram->sub != nullptr && aSubprogram->sub->separate_body && aSubprogram->scope != nullptr &&
                !(aSubprogram->scope->owner != nullptr && (aSubprogram->scope->owner->kind == entity_kind::Protected ||
                aSubprogram->scope->owner->kind == entity_kind::Task));
        }
        std::map<ada_type*, std::map<std::string, node*>> iTypeAspects;     ///< (the aspects of types that later analysis uses)
        std::map<std::string, ada_type*> iStandins;
        std::vector<instance_constant> iInstanceConstants;
        std::vector<dynamic_constraint_info> iDynamicConstraints;
        std::unordered_map<ada_type*, ada_type*> iConstrainedClassWide;
        std::unordered_map<entity const*, entity*> iAccessLevelObjects;
        std::uint64_t iStandardPool = 0u;
        std::map<ada_type*, std::uint64_t> iStorageTotals;    ///< (access types whose Storage_Size is specified: the cells of their allocations' totals)   ///< (stand-alone objects of anonymous access types: their values' accessibility levels' hidden objects (3.10.2(19.2/3)))   ///< (S'Class for constrained subtypes S)
        struct instance_info
        {
            entity* instance;
            node* spec;
            node* body;
            node* instantiation;
        };
        std::vector<instance_info> iInstances;
        std::map<std::string, region*> iStubs;
        std::map<std::string, node*> iStubNodes;            ///< (a subunit's name: its stub)
        std::map<node const*, node*> iStubBodies;           ///< (a stub: its subunit's proper body)
    public:
        node* stub_body(node const* aStub) const { auto b = iStubBodies.find(aStub); return b != iStubBodies.end() ? b->second : nullptr; }
    private:
    public:
        ada_type* implicit_dereference(ada_type* aType);
        void entry_declaration(node* aEntry, region* aRegion);
        entity* unit_named(std::string const& aName, region* aRegion, entity_kind aKind);
        std::map<std::string, entity*> iTaskUnits;
        std::map<entity*, entity*> iTaskBodies;     ///< (task units' bodies (procedures))
        instance_info const* instance_of(node const* aInstantiation) const { for (auto const& i : iInstances) if (i.instantiation == aInstantiation) return &i; return nullptr; }
    private:
        std::map<node const*, region*> iStatementRegions;          ///< (blocks, loops, handlers, accepts ...: their regions)
    public:
        region* statement_region(node const* aNode) const { auto r = iStatementRegions.find(aNode); return r != iStatementRegions.end() ? r->second : nullptr; }
        region* body_region(node const* aNode) const { auto r = iBodyRegions.find(const_cast<node*>(aNode)); return r != iBodyRegions.end() ? r->second : nullptr; }
        region* declare_region(node const* aNode) const { auto r = iDeclareRegions.find(aNode); return r != iDeclareRegions.end() ? r->second : nullptr; }
    private:
        std::map<std::string, entity*> iLibraryUnits;  ///< (library units by name (specs, or bodies without specs))
        std::map<std::string, entity*> iLimitedViews;  ///< (packages named by limited with clauses before they are analysed: their limited views (10.1.1(12.1/2)))
        std::map<std::string, std::vector<entity*>> iLimitedChildViews;   ///< (limited views of child units named by a parent's limited with clauses: added to the parent's region (by full parent name))
        entity* limited_view(std::string const& aName);
        std::map<std::string, region*> iLibraryBodies; ///< (the regions of library package bodies (for subunits))
    public:
        region* current_context() const { return iContextOverride != nullptr ? iContextOverride : iCurrentUnit != nullptr ? iCurrentUnit->context : nullptr; }
        region* iContextOverride = nullptr;     ///< (an instance's analysis: its generic's units' contexts)
        region* generic_context(entity* aGeneric);
        entity* library_unit(std::string const& aName) const { auto u = iLibraryUnits.find(aName); return u != iLibraryUnits.end() ? u->second : nullptr; }
    };
}
