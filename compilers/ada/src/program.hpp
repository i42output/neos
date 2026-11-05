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

#include <cstdlib>
#include <functional>
#include <tuple>
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
        std::size_t compilation = 0u;       ///< (its compilation's (file's) place in the order compiled)
        bool given = false;                 ///< (of a file compiled (not one found on the search path))
        bool replaced = false;              ///< (a unit compiled later replaces it (10.1.4(4, 5)))
        bool declares = false;              ///< (a library unit declaration, or a subprogram body that is its declaration too (10.1.4(4/1)))
        std::vector<std::string> withs;
        std::vector<std::string> limited_withs;     ///< (limited with clauses: not dependences)
        std::vector<std::string> elaborates;        ///< (the units its context's pragmas Elaborate and Elaborate_All name: their bodies are elaborated first)
        entity* ent = nullptr;
        region* context = nullptr;          ///< (the names its context clauses make visible)
        bool analysed = false;
        bool analysing = false;
    };

    // a library unit's categorization: the library unit pragmas (or aspects) Pure and Preelaborate (10.2.1) and the categorization
    // pragmas of Annex E (E.2); their places (pragmas or aspects)
    struct categorization
    {
        enum kind { Pure, Preelaborate, SharedPassive, RemoteTypes, RemoteCallInterface, AllCallsRemote, Count };
        node* where[Count] = {};
        bool is(kind aKind) const { return where[aKind] != nullptr; }
        bool preelaborated() const { return is(Pure) || is(Preelaborate) || is(SharedPassive); }   ///< (10.2.1(11/3), E.2.1(9))
        bool categorized() const { return is(Pure) || is(SharedPassive) || is(RemoteTypes) || is(RemoteCallInterface); }  ///< (E.2(2/3))
    };

    class function_generator;

    std::u32string literal_string(node const* aNode);       ///< (a string literal's characters, as written)
    char32_t literal_character(node const* aNode);
    bool is_operator_symbol(node const* aNode);              ///< (a string literal that is an operator symbol)
    void others_choices(node* aAggregate, std::size_t aDimensions, std::vector<node*>& aResult);  ///< (an array aggregate's others choices (its subaggregates' too))

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
        [[noreturn]] void error_in_private_part_too(node const* aNode, std::string const& aMessage) { throw compile_error{ aNode, aMessage, true }; }
        /// (error recovery (NEOS_ADA_ALL_ERRORS): an error in aWork (a declaration, a statement, a unit) recorded and analysis continued,
        /// unless the error is part of a speculative resolution (`speculation`))
        template <typename Work>
        void recover(Work&& aWork)
        {
            if (!iRecovery || iSpeculation > 0)
            {
                aWork();
                return;
            }
            auto const entities = iEntities.size();
            try
            {
                aWork();
            }
            catch (compile_error const& error)
            {
                // (legality checking: an error within an instance's body is the generic body's (reported by check_generic, 12.3(11)))
                if (iInstanceBodies > 0 && iCheckOnly && !(error.private_part_too && iInstanceBodies == iInstancePrivateParts))
                    return;
                // (an error within an instance: reported at the (outermost) instantiation (12.3(11)); one in the instantiation itself (an
                // actual): there)
                bool inInstantiation = false;
                if (!iInstantiationSites.empty())
                    for (node const* n = error.where; n != nullptr && !inInstantiation; n = n->parent)
                        inInstantiation = n == iInstantiationSites.front();
                compile_error const e = iInstantiationSites.empty() || error.where == nullptr || inInstantiation ? error : compile_error{ iInstantiationSites.front(),
                    std::string{ error.what() } + " (in an instance)" };
                // (the entities that the failed work declared without completing them: hidden, typed (no cascade of crashes))
                for (auto i = entities; i < iEntities.size(); ++i)
                    if (iEntities[i].type == nullptr && (iEntities[i].is_type() || iEntities[i].is_object() || iEntities[i].kind == entity_kind::NamedNumber))
                    {
                        iEntities[i].type = iUniversalInteger;
                        iEntities[i].visible_in_body = false;
                    }
                // (once: an instance's copy of the node reports the same error at the same place)
                for (auto const& d : iDiagnostics)
                    if ((d.where == e.where || (d.where != nullptr && e.where != nullptr && d.where->source.data() == e.where->source.data())) &&
                        std::string{ d.what() } == e.what())
                        return;
                iDiagnostics.push_back(e);
            }
        }
        std::vector<compile_error> const& diagnostics() const { return iDiagnostics; }
        struct speculation      ///< (errors within its lifetime are speculative: caught by the resolution that tries something)
        {
            program& p;
            explicit speculation(program& aProgram) : p{ aProgram } { ++p.iSpeculation; }
            ~speculation() { --p.iSpeculation; }
        };
        // (where the name of an incomplete view can be used (3.10.1(5/3)-(9.4/3)): nowhere, anywhere such names are allowed (an
        // access type's designated subtype, a profile in a declaration, a generic actual), or (a tagged one) a body's parameter)
        enum class incomplete_uses { None, Allowed, TaggedOnly };
        struct incomplete_use
        {
            program& p;
            incomplete_uses saved;
            incomplete_use(program& aProgram, incomplete_uses aUses) : p{ aProgram }, saved{ aProgram.iIncompleteUses } { p.iIncompleteUses = aUses; }
            ~incomplete_use() { p.iIncompleteUses = saved; }
        };
        // --- the model
    public:
        entity* new_entity(entity_kind aKind, std::string const& aName, node* aDecl, region* aScope);
        ada_type* new_type(type_kind aKind, std::string const& aName = {});
        region* new_region(entity* aOwner, region* aParent);
        ada_type* new_subtype(ada_type* aOf, std::string const& aName = {});
        // --- units
    private:
        void collect_units();
        void obsolete_units();
        void limited_with_scope_check(unit_info& aUnit);
        std::vector<unit_info*> dependences(unit_info const& aUnit);   ///< (the units it depends on semantically, of its environment (10.1.1(26/2)))
        bool obsolete(unit_info const& aUnit, std::size_t aAsOf, int aDepth = 0);  ///< (a unit that depends on one compiled again after it (before aAsOf), 10.1.4(5))
        void order_units();
        void analyse_unit(unit_info& aUnit);
        unit_info* find_unit(std::string const& aName, bool aBody);
        void setup_standard();
        void private_child_with_check(node* aClause, node* aName, std::string const& aFull);
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
        std::vector<entity*> visible(std::string const& aName, region* aRegion);
        std::vector<entity*> visible_all(std::string const& aName, region* aRegion);    ///< (the entities that a direct name denotes)
        std::vector<entity*> selected(entity* aPrefix, std::string const& aName, region* aRegion);
        std::vector<entity*> selected_declared(entity* aPrefix, std::string const& aName, region* aRegion);
        struct declaring        ///< (entities hidden from all visibility while it lives: within their declarations (8.3(16)))
        {
            std::vector<entity*> entities;
            ~declaring() { for (auto* e : entities) e->declaring = false; }
            void add(entity* aEntity) { aEntity->declaring = true; entities.push_back(aEntity); }
        };
        void prefixed_view_object(node* aView, region* aRegion);
        std::uint64_t standard_pool();
        std::uint64_t stream_size(ada_type* aType);                 ///< (an elementary type's stream elements per value: its specified Stream_Size's (its ancestor's), else its size's (13.13.2(1.2/3)))                               ///< (the standard storage pool's object (its address))
        void fixed_base_range(ada_type* aFirst);
        bool per_object_access_constraint(component_info const& aComponent) const;   ///< (a component with an access discriminant constrained by a per-object expression (initialized after the others, 3.3.1(20.1/3)))
        std::vector<component_info const*> initialization_order(ada_type* aRecord);   ///< (a record type's components in the order in which they are initialized)
        entity* resolve_entity(node* aName, region* aRegion);                      ///< (a name that denotes a non-overloadable entity)
        ada_type* resolve(node* aExpression, ada_type* aExpected, region* aRegion);  ///< (resolves it (annotating it): its type)
        ada_type* resolve_expression(node* aExpression, ada_type* aExpected, region* aRegion);
        void static_string_check(node* aExpression, ada_type* aType, region* aRegion);
        bool statically_unevaluated(node const* aExpression, region* aRegion);   ///< (part of a static expression that is not evaluated (4.9(33/3)): a short circuit's right operand or a conditional expression's dependent expression not chosen)
        void static_range_check(node* aExpression, node* aValue, ada_type* aType, bool aBase, region* aRegion);  ///< (a static discrete value in a subtype's range (or its base range) (4.9(33/3, 34/3)))    ///< (a static string value's bounds in its index subtype (4.9(34/3), 4.2(11), 4.5.3(7)))
        ada_type* condition(node* aCondition, region* aRegion);
        void parenthesis_check(node* aExpression);
        void case_choices_check(node* aCase, node* aSelector, ada_type* aType, std::vector<node*> const& aAlternatives, region* aRegion);
        ada_type* resolve_in_class(node* aExpression, region* aRegion, bool (*aInClass)(ada_type const*));
        bool class_wide_access_actual(ada_type* aFormal, ada_type* aActual);  ///< (resolves a condition (of any boolean type): its type)
        std::vector<interpretation> interpretations(node* aExpression, region* aRegion);
        std::vector<interpretation> compute_interpretations(node* aExpression, region* aRegion);
        struct cached_interpretations { region* where = nullptr; std::uint64_t generation = 0u; std::vector<interpretation> result; };
        std::unordered_map<node*, cached_interpretations> iInterpretations;
        static_value evaluate(node* aExpression, region* aRegion);                 ///< (its static value, if static)
        bool is_static(node* aExpression);
        std::optional<std::pair<bigint, bigint>> array_range_bounds(node* aAttribute);    ///< (A'Range of a statically constrained array subtype or object: its bounds)
        bool static_expression(node* aExpression);                                 ///< (a static expression (4.9))
        bool static_subtype(ada_type* aType);
        void end_name_check(node* aDecl);
        void conformance_check(subprogram_info const& aDeclared, subprogram_info const& aBody, std::string const& aName, node* aWhere);
        bool static_choice(node* aChoice, ada_type* aType, region* aRegion);         ///< (a static discrete choice)
        bool is_variable(node* aName, int aDepth = 0);                             ///< (a name that denotes a variable (3.3(13-23)); when in doubt, true)
        void redeclaration_check(std::string const& aName, entity_kind aKind, node* aDefining, region* aRegion);
        bool statically_matching(ada_type* a, ada_type* b);                       ///< (subtypes that statically match (4.9.1))
        std::string formal_type_mismatch(node* aFormal, ada_type* aActual, region* aFormalRegion, region* aRegion);                                      ///< (a static subtype (4.9(26-28)))
        // --- statements
    public:
        void statement_identifiers_check(node* aStatements);
        void statements(node* aStatements, region* aRegion);
        void statement(node* aStatement, region* aRegion);
        // --- code generation
    public:
        std::uint32_t runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult);
        std::uint32_t global(std::string const& aName, type aType);
        std::uint64_t static_data(std::uint64_t aSize, std::uint64_t aAlignment = 8u, std::uint64_t aSkew = 0u);     ///< (its address (aligned at aSkew bytes from it))
        std::uint64_t static_string(std::string const& aBytes);   ///< (an array descriptor (1 .. n) and its data: the descriptor's address)
        void define_subprogram(entity* aSubprogram);
        void exports();                                 ///< (the functions of the exported subprograms and objects (B.1))
        std::map<std::string, entity*>& foreign_subprograms() { return iForeignSubprograms; }    ///< (the subprograms imported with 
                                                        ///< convention C, by their external names: the first of each name)
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
        std::vector<std::tuple<std::int64_t, std::int64_t, std::int64_t>> iDispatchingPolicies;    ///< (the partition's task dispatching policies: priority ranges and the runtime's policies (D.2.2))
        bool iGenerateDeadlines = false;                        ///< (pragma Generate_Deadlines in the partition (D.2.6))
        std::map<std::string, std::optional<std::int64_t>> iRestrictions;     ///< (the partition's restrictions (see restrictions_check): their values)
        std::map<node*, std::pair<std::int64_t, std::int64_t>> iPriorityRanges;   ///< (the pragmas Priority_Specific_Dispatching: their priorities' values)
        bool iPolls = false;                                    ///< (the program polls at its loops' iterations (the runtime's poll: its dispatching policy, execution-time events))
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
        std::uint64_t packed_bits(ada_type* aArray);          ///< (a bit-packed array type's components' bits (1, 2, 4 or 8); else 0)
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
        static constexpr std::uint64_t StaticBase = language::ada::layout::StaticBase;
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
        void aspect_names_check(entity* aType, node* aExpression, region* aRegion);
        void representation_aspect_check(entity* aType, std::string const& aAspect, node* aValue, region* aRegion);
        void current_instance_components(node* aExpression, entity* aType);   ///< (an aspect of a record type: its components' names denote the current instance's)
        std::map<region*, std::vector<deferred_aspect>> iDeferredAspects;
        std::map<region const*, entity*> iAcceptEntries;      ///< (an accept statement's region: its entry (the accept's parameters are the entry's))
        bool is_static_resolvable(node* aExpression, region* aRegion);
        std::string external_name(node* aExpression, region* aRegion);    ///< (an External_Name or Link_Name: a static string expression)
        void c_convention(ada_type* aType);             ///< (a type that Convention C specifies)
        bool intrinsic_unit(node* aItem);
        void restrictions_check();                      ///< (the partition's units obey its Restrictions (13.12, D.7, H.4))
        void tasking_policies();                        ///< (the partition's task dispatching and locking policies (D.2.2, D.3, D.13))
        void relative_deadline_resolve(node* aExpression, region* aRegion);    ///< (a Relative_Deadline: of Ada.Real_Time.Time_Span (D.2.6(9.2/3)))
        void cpu_resolve(entity* aEntity, node* aExpression, region* aRegion); ///< (a CPU: of System.Multiprocessors.CPU_Range (D.16(8/3)))
        categorization const& unit_categorization(std::string const& aUnit);    ///< (a library unit's categorization (10.2.1, E.2))
        void categorization_check(unit_info& aUnit);    ///< (a compilation unit of a categorized library unit obeys its category's rules)
        bool remote_access_type(ada_type* aType);         ///< (a named access type declared in the visible part of a remote types or RCI unit, or derived from one (E.2.2(9/3, 9.3/1)))
        bool remote_subprogram(entity* aSubprogram);     ///< (a subprogram declared in the visible part of an RCI unit, or one (E.2.3(7/3)))
        void remote_dereference_check(node* aNode);       ///< (remote access-to-class-wide values dereferenced only as controlling operands of dispatching calls (E.2.2(16/4)))
        std::uint64_t unit_address(entity* aUnit)      ///< (a program unit's (or label's) 'Address: a storage element of its own)
        {
            auto& address = iUnitAddresses[aUnit];
            if (address == 0u)
                address = static_data(1u, 1u, 0u);
            return address;
        }
    public:
        std::map<ada_type*, entity*> iObjectCollections;   ///< (access types whose allocators create objects that need finalization: their collections (hidden objects))
        std::map<ada_type*, entity*> iCollections;     ///< (access types whose allocators create tasks: their collections (hidden objects))
        bool needs_tasks(ada_type* aType);
    private:
    private:
        compiler& iCompiler;
        bool const iRecovery = std::getenv("NEOS_ADA_ALL_ERRORS") != nullptr;
        bool const iCheckOnly = std::getenv("NEOS_ADA_CHECK") != nullptr;
        int iSpeculation = 0;
        int iInstancePrivateParts = 0;
        entity* iPostconditionOf = nullptr;     ///< (resolving a postcondition: its subprogram ('Result))      ///< (of iInstanceBodies: instances' private parts)
        incomplete_uses iIncompleteUses = incomplete_uses::None;
        std::vector<compile_error> iDiagnostics;
        std::vector<node*> iCompilations;
        std::deque<entity> iEntities;
        std::deque<ada_type> iTypes;
        std::deque<region> iRegions;
        std::deque<predicate_info> iPredicates;
        std::map<region*, std::map<std::string, bool>> iAssertionPolicies;
        std::map<region*, std::vector<std::pair<char const*, node*>>> iDefaultPools;   ///< (pragma Default_Storage_Pool in a region (from where), or an instance's aspect (from its start): the pool (null: none))
        void default_storage_pool(ada_type* aAccess, node* aDecl, region* aRegion, bool aRecordPool = true);    ///< (pragma Assertion_Policy: the assertion kinds (or "") ignored (or checked) in a region)
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
        ada_type* iIntegerLiteral = nullptr;            ///< (a numeric literal's universal type (of a type with Integer_Literal too, 4.2.1))
        ada_type* iRealLiteral = nullptr;
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
        unit_info const* iAsOf = nullptr;   ///< (the unit whose environment (the units compiled before it) find_unit finds units of)
        unit_info* iDeclaringUnit = nullptr;            ///< (the library unit whose declaration is being analysed (until it registers))
        std::vector<entity*> iSubprogramsToGenerate;    ///< (the subprograms whose bodies have code, in order)
        std::vector<entity*> iExports;                  ///< (the subprograms and objects that Export specifies (B.1))
        std::map<std::string, entity*> iForeignSubprograms;
        std::map<entity*, std::uint64_t> iUnitAddresses;
        std::map<node*, entity*> iStatementNames;       ///< (the names of loops and blocks, and labels: declared at the start of their 
                                                        ///< body's (or block's) statements (5.1(12)))
        std::vector<node*> iInstantiating;              ///< (the generic declarations whose instances are being made (12.3(19)))
        std::map<std::string, categorization> iCategorizations;     ///< (library units' categorizations, by name)
        std::map<node*, node*> iTemplateBodies;         ///< (a generic's body: its copy analysed by check_generic)
        unit_info* iTemplateUnit = nullptr;             ///< (a library generic being checked (check_generic): its unit)
        bool iPrivateWithVisible = false;               ///< (names of private with clauses' units usable anywhere (error recovery))
        std::map<node*, std::pair<node*, node*>> iInstanceCopies;  ///< (an instantiation: its instance's declaration and body (copies of the generic's))
        std::vector<std::pair<node*, region*>> iPackageElaborations;  ///< (library packages (specification, body) to elaborate)
        std::uint64_t iNextExceptionId = 1u;
    public:
        void register_library_unit(entity* aEntity);
        entity* declare(entity_kind aKind, node* aDefining, region* aRegion);     ///< (a declaration's defining name: its entity)
        node* clone(node* aNode);
        node* clone_tree(node* aNode);
        bool indexed_attribute(node* aAttribute);
        bool indexed_result_fits(ada_type* aResult, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion);   ///< (a function's array result indexed by the arguments: they fit)   ///< (X'Old (I) ...: rewritten as an Apply of the attribute)
        entity* find_completion(entity* aSubprogram, region* aRegion);
        entity* generic_completion(node* aSpec, region* aRegion);
        bool type_conformant(subprogram_info const& a, subprogram_info const& b);
        bool subtype_conformant(subprogram_info const& a, subprogram_info const& b, bool aResultOnly = false);
        bool withed_here(std::string const& aUnit);
        bool generic_child_mentioned(entity* aChild);
        void incomplete_prefix_check(node* aPrefix, ada_type* aType, bool aDereference);  ///< (3.10.1(10/3))
        void prefixed_view_check(node* aObject, entity* aOperation);   ///< (4.1.3(13.1/2))
        bool private_with_hidden(entity* aUnit, region* aRegion);  ///< (a library unit named only by private with clauses, here in a visible part (10.1.2(12/3-16/2)))
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
        std::optional<std::vector<std::pair<bigint, bigint>>> static_values(ada_type* aType, region* aRegion);
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
        std::set<ada_type*> iProvisionalOperators;
        std::map<entity*, region*> iInstanceFormalRegions;
        bool iAllPlaces = false;    ///< (checking "not overriding" (8.3.1(6/2)): the views at any place of the declarative region (its private part and body))
        std::vector<std::tuple<entity*, node*, region*>> iNotOverriding;     ///< (the operations declared "not overriding": checked at the end of their region)     ///< (an instance: its formals' region (the actuals' denotations))     ///< (an array type's operators made before its component's full view was declared: completed later)
        std::deque<node> iClones;
        std::unordered_map<node const*, node*> iCloneOrigins;  ///< (a clone (its root): the node it is a copy of)
        std::deque<std::string> iSyntheticSources;      ///< (the text of nodes made by the analysis (e.g. a user-defined literal's string))
        std::map<node*, region*> iBodyRegions;
        entity* iCurrentSubprogram = nullptr;
        entity* iClassWideInvariant = nullptr;  ///< (resolving a class-wide type invariant: its current instance)
        entity* iClassWideContract = nullptr;   ///< (resolving a class-wide precondition or postcondition: its subprogram)
        std::set<ada_type const*> iNullPoolTypes;   ///< (access types declared where the default pool is null (no allocators, 13.11.3))
        node const* iControllingActual = nullptr;   ///< (resolving the actual of a controlling formal: it may be dynamically tagged (3.9.2(9/1)))
        std::vector<entity*> iExceptions;
        std::vector<dynamic_bound_info> iDynamicBounds;
        std::map<node const*, call_info> iCalls;
    public:
        call_info const* call_of(node const* aNode) const { auto c = iCalls.find(aNode); return c != iCalls.end() ? &c->second : nullptr; }
        std::string full_name(entity* aEntity) const;       ///< (its expanded name, e.g. "ada.text_io.put_line")
        bool private_part_visible(entity* aPackage, region* aFrom) const;
        bool private_part_visible_here(entity* aPackage, region* aFrom) const;
        bool discriminant_dependent(node* aName, region* aRegion = nullptr);      ///< (aRegion: where (within a generic body: formal types assumed the worst))
        bool partial_view_here(ada_type* aType, region* aRegion);
        void overriding_check(region* aRegion);
        bool requires_overriding(entity* aOp, ada_type* aType);
        bool precondition_conflict(entity* aInherited, ada_type* aType);
        bool predefined_operator_hidden(entity* aOperator, region* aRegion);
        bool renamed_limited_view_usable(entity* aRenaming, region* aRegion);
        void dereference_freeze_check(node* aName, region* aRegion);
        bool subprogram_frozen_before(entity* aSubprogram, node const* aAt);
        void deferred_constant_freeze_check(node* aAt, ada_type* aType, node* aExpression, region* aRegion, entity* aCompleting = nullptr);
        void universal_access_equality_check(entity* aOperator, node* aAt, std::vector<node*> const& aOperands, region* aRegion);
        void null_exclusion_check(ada_type* aType, node* aMark, region* aRegion = nullptr);   ///< (aRegion: where (a partial view there: not an access subtype))
        void predicate_check(ada_type* aType, node* aWhere, bool aDynamicOnly = false);
        bool limited_type(ada_type* aType, region* aRegion = nullptr, region* aComposite = nullptr) const;
        ada_type* progenitor(node* aMark, region* aRegion);
        bool not_completely_defined(ada_type* aType, region* aRegion, int aDepth = 0);
        void progenitor_mix_check(ada_type* aParent, std::vector<ada_type*> const& aInterfaces, node* aAt);
        void progenitor_check(ada_type* aProgenitor, node* aAt, bool aLimited, bool aSynchronized, type_kind aUnit);
        void tagged_type_checks(ada_type* aType, node* aDiscriminants, region* aRegion);
        node* end_position(node* aNode);
        std::deque<node> iSyntheticNodes;
        std::set<std::pair<entity*, std::string>> iRepresentationItems;    ///< (attribute definition clauses given: once each)   ///< (positions for errors (end_position))
        bool full_declaration_given(node* aDecl, std::vector<node*> const& aParts);
        bool implicit_homograph(entity* aSubprogram, region* aRegion);
        bool predicate_static(node* aExpression, entity* aCurrentInstance);
        void completion_aspects_check(node* aDecl);
        void others_check(node* aExpression, ada_type* aSubtype);
        void completion_check(node* aDeclarations, node* aCompletions, bool aPartition = false);
        void operator_symbol_check(node* aDefining);
        void homograph_check(entity* aSubprogram, node* aDefining, region* aRegion);
        void program_unit_pragma_check(node* aPragma, std::string const& aName, std::vector<node*> const& aArguments, region* aRegion);
        void operator_profile_check(entity* aOperator, node* aDefining);
        void overriding_indicator_check(entity* aSubprogram, node* aDecl, bool aOverridesImplicit, region* aRegion);
        void incomplete_check(node* aDeclarations, std::string const& aWhere, node* aAt = nullptr);
        void complete_partial_view(entity* aEntity, ada_type* aType, node* aDecl, region* aRegion);
        bool abstract_type(ada_type* aType, region* aRegion = nullptr) const;
        void abstract_subprogram_check(entity* aSubprogram, node* aDecl, region* aRegion);
        bool indefinite_subtype(ada_type* aType, node* aIndication, region* aRegion) const;
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
        ada_type* designated_of(ada_type* aType);
        ada_type* designated_view(ada_type* aType, region* aRegion);
        bool component_on_partial_view(ada_type* aView, entity const* aComponent, int aDepth = 0);
        void attribute_prefix_check(node* aPrefix, region* aRegion);
        bool untagged_view_here(ada_type* aType, region* aRegion);
        std::string synchronization_of(node* aDecl);
        void synchronization_check(entity* aSubprogram, node* aDecl, entity* aImplemented);           ///< (an access type's designated subtype (its full view))
        bool is_discrete_range(node* aNode, region* aRegion);
        ada_type* resolve_operator(node* aOperator, ada_type* aExpected, region* aRegion);
        std::optional<std::int64_t> static_level(node* aObject, region* aRegion);
        bool within_generic_body(region* aRegion) const;
        void aliased_actuals_check(node* aExpression, ada_type* aAccess, region* aRegion);    ///< (a function call whose master is an access type's: its explicitly aliased actuals not deeper (6.4.1(6.4/3), 3.10.2(10.2/3, 10.3/3)))
        void limited_value_check(node* aExpression, ada_type* aType, region* aRegion);   ///< (a limited type's value initializing an object: newly constructed (7.5(2.1/5)))
        bool package_requires_body(node* aSpec);    ///< (a package specification that requires a body (7.2(4), 3.11.1))
        static std::string conformance_text(node const* aNode);    ///< (an expression's lexical elements for conformance (6.3.1(19-21)))
        bool fully_conformant(node* a, node* b);    ///< (fully conformant expressions (6.3.1(18-21)))
        bool same_interfaces(ada_type* aPartial, ada_type* aFull);  ///< (a partial view and its full view descended from the same interfaces (7.3(7.3/2)))
        bool frozen_before(ada_type* aType, node const* aDecl);
        static bool subtype_attribute(node const* aNode);    ///< (a type frozen before a later declaration of its declarative part (13.14))
        bool immutably_limited(ada_type* aType, int aDepth = 0);    ///< (an immutably limited type (7.5(8.1/3)))
        void synchronized_operation_check(entity* aSubprogram, node* aDecl, entity* aUnit);   ///< (a protected or task operation implementing a progenitor's (9.4(11.1/3)-(11.5/3), 9.1(9.2/3)-(9.6/3)))
        std::pair<bool, bool> atomic_view(node* aName);    ///< (a name of an atomic object, of a volatile one (C.6(8/3)))
        bool aliased_view(node* aName);    ///< (an aliased view of an object (3.10(9/3)))
        ada_type* resolve_attribute(node* aAttribute, ada_type* aExpected, region* aRegion);
        ada_type* resolve_aggregate(node* aAggregate, ada_type* aExpected, region* aRegion);
        bool operators_visible(ada_type* aType, region* aRegion);
        bool partial_component_here(ada_type* aArray, region* aRegion);
        void synchronized_target_check(entity* aOperation, node* aObject, node* aCall);
        bool dynamically_tagged(node* aExpression);     ///< (a class-wide expression, or a dispatching call with a controlling result (3.9.2(5/2, 6/2)))
        void class_wide_contract_call_check(entity* aCallee, node* aCall, std::vector<node*> const& aActuals);    ///< (a call in a class-wide condition (6.1.1(18.2/4)))
        void incomplete_result_check(entity* aCallee, node* aCall);
        std::vector<entity*> operator_candidates(std::string const& aDesignator, std::vector<node*> const& aOperands, region* aRegion);
        region* selected_operator_scope(node* aPrefix, region* aRegion);
        bool within(entity* aUnit, region* aRegion) const;
        std::vector<entity*> prefixed_candidates(ada_type* aType, std::string const& aName, region* aRegion = nullptr);     ///< (aRegion: where (an untagged partial view there: none))
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
        void instantiate(node* aInstantiation, entity* aGeneric, entity* aInstance, region* aRegion, entity* aParentInstance = nullptr);
        void check_generic(entity* aGeneric);       ///< (a generic unit's legality: an instance whose actuals are its formals (NEOS_ADA_CHECK))
        ada_type* template_formal_type(node* aFormal, std::string const& aName, region* aFormalRegion);
        bool iTemplateCheck = false;                ///< (instantiating a generic for check_generic)
        node const* iCheckedGeneric = nullptr;      ///< (check_generic's generic's declaration: its template is checked where it is declared (later full type declarations have not occurred))
        std::set<entity const*> iTemplateInstances;
        std::set<entity const*> iFormalPackageTemplates;      ///< (check_generic's instances for formal packages: standing for the formal packages within the generic)
        std::set<entity const*> iFormalPackagesWithActuals;    ///< (check_generic's instances for formal packages with explicit actual parts: their formals not visible by selection (12.7(10/2))) ///< (check_generic's instances: standing for their generic units (their templates))
        std::map<entity const*, entity*> iTemplateInstanceOf;   ///< (a generic unit's check_generic instance (where its generic children are checked))
        std::vector<region*> iInstantiationRegions;  ///< (their regions)
        std::vector<node*> iInstantiationSites;     ///< (the instantiations being analysed (not check_generic's): errors are reported at the outermost)
        int iInstanceBodies = 0;
        std::set<node const*> iInstancePackages;    ///< (instances' package declarations: their private parts' errors are the generic's)                    ///< (analysing an instance's body (not check_generic's): its errors are the generic body's (12.3(11)))
        struct instance_body
        {
            program& p;
            bool counted;
            bool privatePart;
            instance_body(program& aProgram, bool aCounted, bool aPrivatePart = false) : p{ aProgram }, counted{ aCounted }, privatePart{ aPrivatePart }
            {
                if (counted)
                {
                    ++p.iInstanceBodies;
                    if (privatePart)
                        ++p.iInstancePrivateParts;
                }
            }
            ~instance_body()
            {
                if (counted)
                {
                    --p.iInstanceBodies;
                    if (privatePart)
                        --p.iInstancePrivateParts;
                }
            }
        };
        std::vector<entity*> iGenerics;             ///< (the generic units declared: checked by check_generic (NEOS_ADA_CHECK))  ///< (aParentInstance: a generic child's, named through an instance of its parent (10.1.1(19)))
        node* generic_body(entity* aGeneric);
        ada_type* iterator_cursor_type(ada_type* aIterator);
        ada_type* iterator_element_type(ada_type* aContainer);
        bool reversible_iterator(ada_type* aIterator);    ///< (an iterator type that is reversible (5.5.2(4/3)))
        bool iterator_type(ada_type* aType);              ///< (an iterator type (5.5.1(6/3)))
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
        void stream_subprogram_check(ada_type* aType, std::string const& aAttribute, bool aClassWide, node* aName, region* aRegion);
        void stream_size_check(ada_type* aType, node* aExpression, node* aAt, region* aRegion);
        void default_pool_check(node* aPool, region* aRegion);
        void storage_pool_check(ada_type* aAccess, node* aPool, region* aRegion);
        void record_stream_clause(ada_type* aType, std::string const& aAttribute, node* aClause, region* aRegion);
        bool stream_attribute_specified(ada_type* aType, std::string const& aAttribute, region* aRegion);
        bool stream_attribute_available(ada_type* aType, std::string const& aAttribute, region* aRegion, int aDepth = 0);
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
        struct stream_clause { node* clause = nullptr; region* scope = nullptr; bool in_private = false; };
        std::map<ada_type*, std::map<std::string, std::vector<stream_clause>>> iStreamClauses;  ///< (the stream attributes specified for types (attribute definition clauses, aspects; "write'class" ...): where (their availability, 13.13.2(39/2)-(48/2)))
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
        std::map<node const*, std::pair<entity*, entity*>> iStubEntities;  ///< (a subprogram body stub: the subprogram it completes (or declares), and its own profile)
        std::pair<entity*, entity*> iSubunitOf{};           ///< (a subunit's proper subprogram body being analysed: its stub's (see iStubEntities))
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
        entity* library_unit(std::string const& aName) const
        {
            if (auto u = iLibraryUnits.find(aName); u != iLibraryUnits.end())
                return u->second;
            // (a child named through a renaming of its parent (a library unit renaming: 10.1.1(?), 8.5.3)): the child of the renamed unit)
            for (auto dot = aName.rfind('.'); dot != std::string::npos && dot > 0u; dot = aName.rfind('.', dot - 1u))
                if (auto p = iLibraryUnits.find(aName.substr(0u, dot)); p != iLibraryUnits.end() && p->second->renamed != nullptr && p->second->renamed != p->second &&
                    p->second->renamed->is_package())
                {
                    for (auto const& [name, unit] : iLibraryUnits)
                        if (unit == p->second->renamed)
                            if (auto c = iLibraryUnits.find(name + aName.substr(dot)); c != iLibraryUnits.end())
                                return c->second;
                    break;
                }
            return nullptr;
        }
    };
}
