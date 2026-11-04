/*
  units2.cpp

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

// Ada: generic units and their instances, tasks and protected units, subunits, aspects and the predefined library's types

#include <neolib/neolib.hpp>
#include <cstdio>
#include <cstdlib>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        node* specification_of(node* aItem)
        {
            for (auto* c : aItem->children)
                if (c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification))
                    return c;
            return nullptr;
        }
    }

    // --- the predefined library's types

    ada_type* program::string_type_named(std::string const& aName)
    {
        auto range = iStandard->names.equal_range(aName);
        for (auto i = range.first; i != range.second; ++i)
            if (i->second->is_type())
                return i->second->type;
        return iString;
    }

    ada_type* program::library_type(std::string const& aUnit, std::string const& aName)
    {
        if (auto* unit = library_unit(aUnit); unit != nullptr && unit->inner != nullptr)
        {
            auto range = unit->inner->names.equal_range(aName);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->is_type())
                    return i->second->type;
        }
        // (a stand-in when the unit is not part of the program)
        auto& standin = iStandins[aUnit + "." + aName];
        if (standin == nullptr)
        {
            standin = new_type(type_kind::Integer, aName);
            standin->size = 8u;
            standin->low = static_value{ bigint{ std::numeric_limits<std::int64_t>::min() } };
            standin->high = static_value{ bigint{ std::numeric_limits<std::int64_t>::max() } };
        }
        return standin;
    }

    ada_type* program::system_type(std::string const& aName)
    {
        return library_type("system", aName);
    }

    ada_type* program::tags_type(std::string const& aName)
    {
        return library_type("ada.tags", aName);
    }

    ada_type* program::task_identification_type()
    {
        return library_type("ada.task_identification", "task_id");
    }

    // --- aspects

    void program::aspects(node* aDecl, entity* aEntity, region* aRegion)
    {
        auto* aspects = aDecl != nullptr ? aDecl->first(construct::Aspects) : nullptr;
        if (aspects == nullptr || aEntity == nullptr)
            return;
        std::string convention;
        std::string external;
        bool import = false;
        bool exported = false;
        std::set<std::string> specified;
        for (auto* aspect : aspects->children)
        {
            // (a class-wide aspect (Pre'Class ...): its name's Class)
            auto const name = name_text(aspect->child(0)->source) + (aspect->size() > 2u && aspect->child(1)->is(construct::Word) &&
                name_text(aspect->child(1)->source) == "class" ? "'class" : "");
            auto* value = aspect->size() > 1u ? aspect->children.back() : nullptr;
            if (value == aspect->child(0))
                value = nullptr;
            // (an aspect: specified once (13.1(10/3)))
            if (!specified.insert(name).second && iInstanceBodies == 0)
                recover([&]() { error(aspect, "the aspect " + name + " is already specified"); });
            // (Default_Value, Default_Component_Value: an explicit expression (3.5(56.3/3), 3.6(22.3/3)))
            if ((name == "default_value" || name == "default_component_value") && value == nullptr && iInstanceBodies == 0)
                recover([&]() { error(aspect, "the aspect " + name + " requires an expression"); });
            // (a type's representation aspects: checked at the end of the declarative part (representation_aspect_check))
            if ((name == "size" || name == "alignment" || name == "component_size" || name == "small") && aEntity->is_type() && value != nullptr)
                iDeferredAspects[aRegion].push_back({ aEntity->type, "check'" + name, value, nullptr, aEntity });
            if ((name == "priority" || name == "interrupt_priority") && (aEntity->kind == entity_kind::Task ||
                aEntity->kind == entity_kind::Protected || aEntity->sub != nullptr))
            {
                // (as the pragma: see pragma)
                aEntity->interrupt_priority = name == "interrupt_priority";
                aEntity->priority = value;
                if (value != nullptr)
                    resolve(value, integer(), aEntity->inner != nullptr ? aEntity->inner : aRegion);
            }
            else if ((name == "interrupt_handler" || name == "attach_handler") && aEntity->scope != nullptr && aEntity->scope->owner != nullptr &&
                aEntity->scope->owner->kind == entity_kind::Protected && aEntity->kind == entity_kind::Procedure)
            {
                // (see pragma)
                if (aEntity->scope->owner->priority == nullptr)
                    aEntity->scope->owner->interrupt_priority = true;
                if (name == "attach_handler" || value == nullptr || name_text(value->source) == "true")
                    iInterruptHandlers.push_back(aEntity);
                if (name == "attach_handler" && value != nullptr)
                {
                    resolve(value, nullptr, aRegion);   // (of Ada.Interrupts.Interrupt_Id)
                    iAttachHandlers[aEntity->scope->owner].emplace_back(aEntity, value);
                }
            }
            else if ((name == "volatile" || name == "atomic" || name == "volatile_components" || name == "atomic_components") && aEntity->is_object() &&
                (value == nullptr || name_text(value->source) == "true"))
            {
                (name == "volatile" || name == "atomic" ? aEntity->volatile_object : aEntity->volatile_components) = true;   // (see pragma)
                if (name == "atomic")
                    aEntity->atomic_object = true;
                if (name == "atomic_components")
                    aEntity->atomic_components = true;
            }
            else if ((name == "volatile" || name == "atomic" || name == "volatile_components" || name == "atomic_components") && aEntity->is_type() &&
                aEntity->type != nullptr && value == nullptr)
                iTypeAspects[aEntity->type->base_type()][name] = aspect;    // (a by-reference type (C.6(18/3)): see volatile_type)
            else if (name == "import")
                import = value == nullptr || name_text(value->source) == "true";
            else if (name == "export")
                exported = value == nullptr || name_text(value->source) == "true";
            else if (name == "convention")
                convention = value != nullptr ? name_text(value->source) : std::string{};
            else if (name == "external_name" || (name == "link_name" && external.empty()))
            {
                if (value != nullptr)
                    external = external_name(value, aRegion);
            }
            else if (name == "machine_radix" && aEntity->is_type() && aEntity->type != nullptr && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                if (auto v = evaluate(value, aRegion); v.is_integer())
                    aEntity->type->view()->base_type()->view()->machine_radix = static_cast<std::uint32_t>(v.integer().to_int64());
            }
            else if (name == "no_return")
            {
                if (aEntity->sub != nullptr && (value == nullptr || name_text(value->source) == "true"))
                    aEntity->sub->no_return = true;
            }
            else if (name == "pre" || name == "pre'class" || name == "post" || name == "post'class")
            {
                // (of a subprogram: not of an instance or a package, nor (a specific one) of a null procedure or an abstract subprogram
                // (6.1.1))
                if (iInstanceBodies == 0 && (aEntity->sub == nullptr || aDecl->is(construct::Instantiation) ||
                    ((name == "pre" || name == "post") && (aEntity->sub->is_abstract || aDecl->is(construct::SubprogramNull) ||
                        aDecl->is(construct::SubprogramAbstract)))))
                    recover([&]() { error(aspect, "aspect " + name + " cannot be specified here"); });
                // (a class-wide one: not of a protected operation (not a primitive subprogram) (6.1.1(5/3)))
                else if ((name == "pre'class" || name == "post'class") && iInstanceBodies == 0 && aRegion != nullptr && aRegion->owner != nullptr &&
                    (aRegion->owner->kind == entity_kind::Protected || aRegion->owner->kind == entity_kind::Task))
                    recover([&]() { error(aspect, "aspect " + name + " cannot be specified for a protected operation"); });
                // (resolved at the end of the declarative part (they can name declarations after the subprogram's))
                if (aEntity->sub != nullptr && value != nullptr && !assertion_ignored(name, aRegion))
                {
                    (name == "pre" ? aEntity->sub->preconditions : name == "pre'class" ? aEntity->sub->class_preconditions :
                        name == "post" ? aEntity->sub->postconditions : aEntity->sub->class_postconditions).push_back(value);
                    iDeferredAspects[aRegion].push_back({ nullptr, name, value, aEntity });
                }
            }
            else if (name == "address" && aEntity->is_object() && value != nullptr)
            {
                // (an object at a specified address)
                resolve(value, system_type("address"), aRegion);
                aEntity->address_clause = value;
            }
            else if (name == "pack")
            {
                if (aEntity->is_type())
                    aEntity->type->packed = aEntity->type->view()->base_type()->view()->packed = true;
            }
            else if (name == "unchecked_union")
            {
                if (aEntity->is_type())
                    aEntity->type->view()->base_type()->view()->unchecked_union = true;
            }
            else if (name == "preelaborable_initialization")
            {
                // (10.2.1(11.6/2); a formal type's (Ada 2022: Preelaborable_Initialization(Formal)): see preelaborable_initialization)
                if (aEntity->is_type() && aEntity->type != nullptr && (value == nullptr || name_text(value->source) != "false"))
                    aEntity->type->preelaborable_initialization = aEntity->type->base_type()->preelaborable_initialization = true;
            }
            else if (name == "alignment" && (aEntity->is_type() || aEntity->is_object()) && value != nullptr && !is_static_resolvable(value, aRegion))
                iDeferredAspects[aRegion].push_back({ aEntity->is_type() ? aEntity->type : nullptr, name, value, aEntity->is_object() ? aEntity : nullptr });
            else if (name == "alignment" && (aEntity->is_type() || aEntity->is_object()) && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                if (auto v = evaluate(value, aRegion); v.is_integer())
                {
                    if (aEntity->is_type())
                        aEntity->type->alignment_clause = v.integer().to_uint64();
                    else
                        aEntity->alignment_clause = v.integer().to_uint64();
                }
            }
            else if ((name == "size" || name == "small") && aEntity->is_type() && value != nullptr && !is_static_resolvable(value, aRegion))
                iDeferredAspects[aRegion].push_back({ aEntity->type, name, value });
            else if (name == "size" && aEntity->is_object() && value != nullptr && is_static_resolvable(value, aRegion))
            {
                resolve(value, nullptr, aRegion);
                if (auto v = evaluate(value, aRegion); v.is_integer())
                    aEntity->size_clause = v.integer().to_uint64();
            }
            else if (name == "size" && aEntity->is_type() && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                // (a static integer expression (13.3(48)))
                if (iInstanceBodies == 0 && !static_expression(value))
                    recover([&]() { error(value, "the Size aspect's value must be a static integer expression (13.3(48))"); });
                auto v = evaluate(value, aRegion);
                if (v.is_integer())
                    aEntity->type->size_clause = v.integer().to_uint64();
            }
            else if (name == "small" && aEntity->is_type() && value != nullptr)
            {
                resolve(value, nullptr, aRegion);
                auto v = evaluate(value, aRegion);
                if (v.known())
                {
                    aEntity->type->base_type()->small = v.real();
                    aEntity->type->small = v.real();
                }
            }
            else if ((name == "static_predicate" || name == "dynamic_predicate") && !aEntity->is_type() && aEntity->kind != entity_kind::Task &&
                aEntity->kind != entity_kind::Protected)
                recover([&]() { error(aspect, "aspect " + name + " can be specified only for a type or subtype"); });
            else if ((name == "static_predicate" || name == "dynamic_predicate" || name == "predicate") && aEntity->is_type() && value != nullptr)
            {
                // (not of an incomplete view (3.10.1(2.2/2)))
                if (aEntity->type != nullptr && aEntity->type->incomplete && aEntity->type->full == nullptr)
                    recover([&]() { error(aspect, "aspect " + name + " cannot be specified for an incomplete type"); });
                // (a predicate: of this subtype and its subtypes (its function: at the end of the declarative part); a subtype
                // declared without a constraint: a subtype of its own (not its type's))
                if (aEntity->kind == entity_kind::Subtype && aEntity->type->name != aEntity->name)
                    aEntity->type = new_subtype(aEntity->type, aEntity->name);
                auto& predicate = iPredicates.emplace_back();
                predicate.parent = aEntity->type->predicate;
                predicate.expression = value;
                predicate.is_static = name == "static_predicate";
                predicate.checked = !assertion_ignored(name == "predicate" ? "dynamic_predicate" : name, aRegion);
                aEntity->type->predicate = &predicate;
                iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            }
            else if ((name == "type_invariant" || name == "type_invariant'class" || name == "invariant") && aEntity->is_type() && value != nullptr)
            {
                // (a specific type invariant: not of an abstract type (7.3.2(6/3)))
                if (name != "type_invariant'class" && iInstanceBodies == 0 && aEntity->type != nullptr && (aEntity->type->abstract ||
                    aEntity->type->base_type()->abstract))
                    recover([&]() { error(aspect, "aspect Type_Invariant cannot be specified for an abstract type (7.3.2(6/3))"); });
                // (a type invariant (7.3.2): its function at the end of the declarative part (unless its policy is Ignore))
                if (!assertion_ignored(name == "invariant" ? "type_invariant" : name, aRegion))
                    iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            }
            else if (name == "predicate_failure" && (!aEntity->is_type() || [&]()
                {
                    // (of a subtype with a predicate specified with it (3.2.4(14.2/5)))
                    for (auto* a : aspects->children)
                        if (auto const n = name_text(a->child(0)->source); n == "static_predicate" || n == "dynamic_predicate" || n == "predicate")
                            return false;
                    return true;
                }()))
                recover([&]() { error(aspect, "aspect Predicate_Failure can be specified only for a subtype with a predicate"); });
            else if (name == "predicate_failure" && aEntity->is_type() && value != nullptr)
                iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            else if (aEntity->is_type() && value != nullptr)
            {
                // (Constant_Indexing, Variable_Indexing, Default_Iterator, Iterator_Element, Implicit_Dereference, Aggregate,
                // Default_Value, Type_Invariant, predicates ...: used later)
                iTypeAspects[aEntity->type->base_type()][name] = value;
                // (Constant_Indexing, Variable_Indexing: of a tagged type (4.1.6(2/3)), not of the full view of a tagged partial view, nor
                // of a derived type that inherits it (4.1.6(5/3)); its name: checked at the end of the declarative part (4.1.6(3/3, 4/3)))
                if ((name == "constant_indexing" || name == "variable_indexing") && iInstanceBodies == 0 && aEntity->type != nullptr)
                {
                    auto* t = aEntity->type->base_type();
                    if (!t->tagged)
                        recover([&]() { error(aspect, "aspect " + name + " can be specified only for a tagged type (4.1.6(2/3))"); });
                    else if (t->partial != nullptr && t->partial->tagged)
                        recover([&]() { error(aspect, "aspect " + name + " cannot be specified for the full view of a tagged partial view (4.1.6(2/3))"); });
                    else if (t->parent != nullptr && type_aspect(t->parent, name) != nullptr)
                        recover([&]() { error(aspect, "aspect " + name + " cannot be specified for a type that inherits it (4.1.6(5/3))"); });
                    else
                        iDeferredAspects[aRegion].push_back({ aEntity->type, "indexing'" + name, value, nullptr, aEntity });
                }
                // (Implicit_Dereference: an access discriminant of the type (4.1.5(2/3)); not specified for the full view of a private type
                // whose partial view has known discriminants (13.1.1(18.4/4)))
                if (name == "implicit_dereference" && iInstanceBodies == 0 && aEntity->type != nullptr)
                {
                    auto* t = aEntity->type->base_type();
                    if (t->partial != nullptr && t->partial->known_discriminants)
                        recover([&]() { error(aspect, "aspect Implicit_Dereference cannot be specified for the full view of a private type"); });
                    else
                    {
                        auto* v = t->view();
                        bool found = false;
                        bool anyDiscriminant = false;
                        if (value->is(construct::Identifier))
                            for (auto const& c : v->components)
                                if (c.discriminant && !c.stored && c.ent != nullptr)
                                {
                                    anyDiscriminant = true;
                                    if (c.ent->name == name_text(value->source) && c.type != nullptr && c.type->anonymous_access &&
                                        c.type->view()->kind == type_kind::Access)
                                        found = true;
                                }
                        if (!found && (anyDiscriminant || !value->is(construct::Identifier) || v->kind == type_kind::Record))
                            recover([&]() { error(value, "aspect Implicit_Dereference must name an access discriminant of the type (4.1.5(2/3))"); });
                        // (a descendant of a reference type: only a confirming value (the discriminant that corresponds to the parent's)
                        // (4.1.5))
                        else if (found && t->parent != nullptr)
                            if (auto* inherited = type_aspect(t->parent, "implicit_dereference"); inherited != nullptr)
                            {
                                auto const parentName = name_text(inherited->source);
                                std::string corresponding = parentName;
                                node* definition = aEntity->decl != nullptr && aEntity->decl->parent != nullptr ? aEntity->decl->parent->first(construct::TypeDerived) : nullptr;
                                node* indication = definition != nullptr ? definition->first(construct::SubtypeIndication) : nullptr;
                                node* apply = indication != nullptr ? indication->first(construct::Apply) : nullptr;
                                if (apply != nullptr && apply->size() >= 2u)
                                {
                                    std::vector<node*> items(apply->children.begin() + 1, apply->children.end());
                                    if (items.size() == 1u && items.front()->is(construct::Arguments))
                                        items = items.front()->children;
                                    std::vector<std::string> parentDiscriminants;
                                    for (auto const& c : t->parent->view()->base_type()->view()->components)
                                        if (c.discriminant && !c.stored && c.ent != nullptr)
                                            parentDiscriminants.push_back(c.ent->name);
                                    corresponding.clear();
                                    for (std::size_t k = 0u; k < items.size(); ++k)
                                    {
                                        node* expression = items[k];
                                        if (expression->is(construct::Association))
                                        {
                                            bool names = false;
                                            for (auto* choice : expression->child(0)->children)
                                                names = names || name_text(choice->source) == parentName;
                                            if (names)
                                                corresponding = name_text(expression->children.back()->source);
                                        }
                                        else if (k < parentDiscriminants.size() && parentDiscriminants[k] == parentName)
                                            corresponding = name_text(expression->source);
                                    }
                                }
                                if (!corresponding.empty() && corresponding != name_text(value->source))
                                    recover([&]() { error(value, "a descendant of a type with aspect Implicit_Dereference can specify only a confirming value (4.1.5)"); });
                            }
                    }
                }
                if (name == "read" || name == "write" || name == "input" || name == "output" || name == "read'class" || name == "write'class" ||
                    name == "input'class" || name == "output'class")
                {
                    record_stream_clause(aEntity->type, name, aspect, aRegion);
                    iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
                }
                // (Stream_Size, Storage_Size, External_Tag: resolved here (values, used later))
                if (name == "storage_size" || name == "external_tag")
                    resolve(value, nullptr, aRegion);
                // (Stream_Size, Storage_Pool: at the end of the declarative part (13.1.1(11/3)))
                if (name == "stream_size" || name == "storage_pool")
                    iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
                // (Default_Value, Default_Component_Value: resolved at the end of the declarative part (they can name later
                // declarations, 13.1.1(11/3)), of the type, of its component type)
                if (name == "default_value" || (name == "default_component_value" && aEntity->type->view()->kind == type_kind::Array))
                    iDeferredAspects[aRegion].push_back({ aEntity->type, name, value, nullptr, aEntity });
            }
        }
        if (!convention.empty())
            aEntity->convention = convention;
        if (exported)
        {
            // (Export (B.1): its link name, the external name (else the name, in lower case))
            if (!aEntity->exported)
                iExports.push_back(aEntity);
            aEntity->exported = true;
            aEntity->link_name = external.empty() ? aEntity->name : external;
            if (aEntity->sub != nullptr)
                aEntity->sub->external_name = aEntity->link_name;
            if (aEntity->is_object())
                aEntity->aliased = true;    // (in memory: its address is linked)
        }
        if (import && aEntity->is_object())
            aEntity->link_name = external.empty() ? aEntity->name : external;
        if (import && aEntity->sub != nullptr)
        {
            aEntity->imported = true;
            if (convention == "neos")
            {
                aEntity->sub->op = intrinsic::Import;
                aEntity->sub->import_name = external;
            }
            else if (convention == "intrinsic")
                aEntity->sub->op = intrinsic_operation(aEntity);
            aEntity->sub->external_name = external;
            aEntity->sub->body = aDecl;
            // (a foreign subprogram (no neos program provides it): its body raises Program_Error when it is called)
            if (convention != "neos" && convention != "intrinsic")
                iSubprogramsToGenerate.push_back(aEntity);
        }
        else if (import)
            aEntity->imported = true;
        else if (convention == "fortran" && aEntity->is_type() && aEntity->type != nullptr)
            aEntity->type->view()->base_type()->view()->fortran = true;
        else if (convention == "c" && aEntity->is_type() && aEntity->type != nullptr)
            c_convention(aEntity->type);
        else if (convention == "intrinsic" && aEntity->sub != nullptr && intrinsic_operation(aEntity) != intrinsic::Import)
        {
            // (an operator of convention Intrinsic (e.g. System's comparisons of addresses): predefined)
            aEntity->sub->op = intrinsic_operation(aEntity);
            aEntity->sub->body = aDecl;
        }
    }

    void program::restrictions_check()
    {
        // the partition's restrictions (pragma Restrictions and Profile: configuration pragmas (13.12(8/3)), in any unit's context or 
        // in the configuration pragmas' file): each restriction's value (a restriction without one: none)
        std::map<std::string, std::optional<std::int64_t>> restrictions;
        std::set<std::string> dependences;  // (No_Dependence's units)
        auto const restrict = [&](std::string const& aName, std::optional<std::int64_t> aValue = std::nullopt)
        {
            auto const existing = restrictions.find(aName);
            if (existing == restrictions.end() || (aValue && existing->second && *aValue < *existing->second))
                restrictions[aName] = aValue;
        };
        for (auto* u : iOrder)
            if (u->unit != nullptr)
                if (auto* context = u->unit->first(construct::Context))
                    for (auto* p : context->children)
                    {
                        if (!p->is(construct::Pragma) || p->children.empty())
                            continue;
                        auto const pragmaName = name_text(p->child(0)->source);
                        if (pragmaName == "profile" && p->size() > 1u)
                        {
                            // (the Ravenscar and Jorvik profiles' restrictions (D.13))
                            auto const profile = name_text(p->child(1)->children.back()->source);
                            if (profile != "ravenscar" && profile != "jorvik")
                                continue;
                            for (auto const* r : { "no_abort_statements", "no_dynamic_attachment", "no_dynamic_cpu_assignment", "no_dynamic_priorities", 
                                "no_local_protected_objects", "no_local_timing_events", "no_protected_type_allocators", "no_requeue_statements",
                                "no_specific_termination_handlers", "no_task_allocators", "no_task_hierarchy", "no_task_termination" })
                                restrict(r);
                            if (profile == "ravenscar")
                            {
                                for (auto const* r : { "no_implicit_heap_allocations", "no_relative_delay", "no_select_statements", "simple_barriers" })
                                    restrict(r);
                                restrict("max_entry_queue_length", 1);
                                restrict("max_protected_entries", 1);
                                restrict("max_task_entries", 0);
                                dependences.insert("ada.calendar");
                                dependences.insert("ada.synchronous_barriers");
                            }
                            dependences.insert("ada.asynchronous_task_control");
                            dependences.insert("ada.execution_time.group_budgets");
                            dependences.insert("ada.execution_time.timers");
                            dependences.insert("ada.task_attributes");
                            dependences.insert("system.multiprocessors.dispatching_domains");
                            continue;
                        }
                        if (pragmaName != "restrictions")
                            continue;
                        for (auto* a : p->all(construct::PragmaArgument))
                        {
                            if (a->size() >= 2u)
                            {
                                auto const name = name_text(a->child(0)->source);
                                if (name == "no_dependence" || name == "no_use_of_pragma" || name == "no_use_of_attribute" || name == "no_specification_of_aspect")
                                {
                                    if (name == "no_dependence")
                                    {
                                        std::string text{ a->children.back()->source };
                                        std::erase_if(text, [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
                                        dependences.insert(name_text(text));
                                    }
                                    else
                                        restrict(name + ":" + name_text(a->children.back()->source));
                                    continue;
                                }
                                std::optional<std::int64_t> value;
                                if (auto const v = evaluate(a->children.back(), iStandard); v.is_integer())
                                    value = v.integer().to_int64();
                                restrict(name, value);
                            }
                            else
                                restrict(name_text(a->children.back()->source));
                        }
                    }
        if (restrictions.empty() && dependences.empty())
            return;
        auto const restricted = [&](char const* aName) { return restrictions.contains(aName); };
        auto const maximum = [&](char const* aName) -> std::optional<std::int64_t>
        {
            auto const r = restrictions.find(aName);
            return r != restrictions.end() ? r->second : std::nullopt;
        };
        // (the units it applies to: the program's own (not the implementation's predefined library)
        auto const predefined = [&](unit_info const* aUnit)
        {
            auto const& folder = iCompiler.packages_folder();
            return aUnit->item == nullptr || aUnit->item->file == nullptr || (folder && aUnit->item->file->path.starts_with(*folder)) ||
                aUnit->name == "standard" || aUnit->name == compiler::ConfigurationUnit;
        };
        auto const violation = [&](node* aWhere, std::string const& aRestriction)
        {
            recover([&]() { error(aWhere, "this violates the partition's restriction " + aRestriction + " (13.12)"); });
        };
        // (semantic dependence (10.1.1(26/2)) on units that restrictions forbid: a unit's with clauses, and the declarations that they
        // name depend on the units that theirs name, and their parents)
        std::map<std::string, std::vector<std::string>> forbidden;     // (a unit: the restrictions that forbid depending on it)
        if (restricted("no_dynamic_priorities"))
            forbidden["ada.dynamic_priorities"].push_back("No_Dynamic_Priorities");
        if (restricted("no_asynchronous_control"))
            forbidden["ada.asynchronous_task_control"].push_back("No_Asynchronous_Control");
        if (restricted("no_unchecked_deallocation"))
            for (auto const* n : { "ada.unchecked_deallocation", "unchecked_deallocation" })
                forbidden[n].push_back("No_Unchecked_Deallocation");
        if (restricted("no_unchecked_conversion"))
            for (auto const* n : { "ada.unchecked_conversion", "unchecked_conversion" })
                forbidden[n].push_back("No_Unchecked_Conversion");
        if (restricted("no_io"))
            for (auto const* n : { "ada.sequential_io", "ada.direct_io", "ada.text_io", "ada.wide_text_io", "ada.wide_wide_text_io", "ada.streams.stream_io",
                "sequential_io", "direct_io", "text_io" })
                forbidden[n].push_back("No_IO");
        if (restricted("no_delay"))
            for (auto const* n : { "ada.calendar", "calendar" })
                forbidden[n].push_back("No_Delay");
        for (auto const& d : dependences)
            forbidden[d].push_back("No_Dependence => " + d);
        auto const forbids = [&](std::string const& aUnit) -> std::vector<std::string> const*
        {
            // (the unit, or a child of a forbidden unit (Text_IO's children are Text_IO's))
            for (auto const& [name, r] : forbidden)
                if (aUnit == name || aUnit.starts_with(name + "."))
                    return &r;
            return nullptr;
        };
        std::map<std::string, std::set<std::string>> closures;
        std::function<std::set<std::string> const&(std::string const&)> closure = [&](std::string const& aUnit) -> std::set<std::string> const&
        {
            if (auto const c = closures.find(aUnit); c != closures.end())
                return c->second;
            auto& result = closures[aUnit];
            result.insert(aUnit);
            if (auto* spec = find_unit(aUnit, false))
            {
                std::vector<std::string> next = spec->withs;
                if (auto const dot = aUnit.rfind('.'); dot != std::string::npos)
                    next.push_back(aUnit.substr(0u, dot));
                for (auto const& n : next)
                    if (!result.contains(n))
                        for (auto const& m : closure(n))
                            result.insert(m);
            }
            return result;
        };
        // (the constructs that restrictions forbid)
        std::function<void(node*, bool)> walk = [&](node* aNode, bool aLocal)
        {
            auto* const t = aNode->type != nullptr ? aNode->type->view() : nullptr;
            switch (aNode->kind)
            {
            case construct::Abort:
                if (restricted("no_abort_statements"))
                    violation(aNode, "No_Abort_Statements");
                break;
            case construct::Terminate:
                if (restricted("no_terminate_alternatives"))
                    violation(aNode, "No_Terminate_Alternatives");
                break;
            case construct::SelectAccept:
                if (auto const m = maximum("max_select_alternatives"); m && static_cast<std::int64_t>(aNode->all(construct::SelectAlternative).size()) > *m)
                    violation(aNode, "Max_Select_Alternatives => " + std::to_string(*m));
                [[fallthrough]];
            case construct::SelectTimed:
            case construct::SelectConditional:
            case construct::SelectAsynchronous:
                if (restricted("no_select_statements"))
                    violation(aNode, "No_Select_Statements");
                break;
            case construct::Requeue:
                if (restricted("no_requeue_statements"))
                    violation(aNode, "No_Requeue_Statements");
                break;
            case construct::Delay:
                if (restricted("no_delay"))
                    violation(aNode, "No_Delay");
                break;
            case construct::Raise:
            case construct::RaiseExpression:
            case construct::Handler:
                if (restricted("no_exceptions"))
                    violation(aNode, "No_Exceptions");
                break;
            case construct::TaskType:
            case construct::TaskSingle:
                if (auto const m = maximum("max_task_entries"))
                {
                    std::int64_t entries = 0;
                    if (auto* definition = aNode->first(construct::TaskDefinition))
                        for (auto* d : definition->children)
                            entries += d->is(construct::Entry) || d->is(construct::EntryFamily) ? 1 : 0;
                    if (entries > *m)
                        violation(aNode, "Max_Task_Entries => " + std::to_string(*m));
                }
                break;
            case construct::ProtectedType:
            case construct::ProtectedSingle:
                if (restricted("no_protected_types"))
                    violation(aNode, "No_Protected_Types");
                if (aLocal && restricted("no_local_protected_objects"))
                    violation(aNode, "No_Local_Protected_Objects");
                if (auto const m = maximum("max_protected_entries"))
                {
                    std::int64_t entries = 0;
                    if (auto* definition = aNode->first(construct::ProtectedDefinition))
                        for (auto* d : definition->children)
                            entries += d->is(construct::Entry) || d->is(construct::EntryFamily) ? 1 : 0;
                    if (entries > *m)
                        violation(aNode, "Max_Protected_Entries => " + std::to_string(*m));
                }
                break;
            case construct::Allocator:
                if (restricted("no_allocators"))
                    violation(aNode, "No_Allocators");
                if (aLocal && restricted("no_local_allocators"))
                    violation(aNode, "No_Local_Allocators");
                if (t != nullptr && t->kind == type_kind::Access && t->designated != nullptr && needs_tasks(t->designated))
                {
                    if (restricted("no_task_allocators"))
                        violation(aNode, "No_Task_Allocators");
                    if (aLocal && restricted("no_task_hierarchy"))
                        violation(aNode, "No_Task_Hierarchy");
                }
                if (t != nullptr && t->kind == type_kind::Access && t->designated != nullptr && t->designated->view()->kind == type_kind::Protected &&
                    restricted("no_protected_type_allocators"))
                    violation(aNode, "No_Protected_Type_Allocators");
                break;
            case construct::Object:
                if (aLocal && aNode->ent != nullptr && aNode->ent->type != nullptr && needs_tasks(aNode->ent->type) && restricted("no_task_hierarchy"))
                    violation(aNode, "No_Task_Hierarchy");
                if (aLocal && aNode->ent != nullptr && aNode->ent->type != nullptr && aNode->ent->type->view()->kind == type_kind::Protected &&
                    restricted("no_local_protected_objects"))
                    violation(aNode, "No_Local_Protected_Objects");
                break;
            case construct::TypeAccessSubprogram:
                if (restricted("no_access_subprograms"))
                    violation(aNode, "No_Access_Subprograms");
                break;
            case construct::TypeFloat:
                if (restricted("no_floating_point"))
                    violation(aNode, "No_Floating_Point");
                break;
            case construct::TypeFixed:
            case construct::TypeDecimal:
                if (restricted("no_fixed_point"))
                    violation(aNode, "No_Fixed_Point");
                break;
            case construct::Attribute:
                if (aNode->size() > 1u)
                {
                    auto const attribute = name_text(aNode->child(1)->source);
                    if (attribute == "unchecked_access" && restricted("no_unchecked_access"))
                        violation(aNode, "No_Unchecked_Access");
                    if (attribute == "class" && restricted("no_dispatch"))
                        violation(aNode, "No_Dispatch");
                    if (restrictions.contains("no_use_of_attribute:" + attribute))
                        violation(aNode, "No_Use_Of_Attribute => " + attribute);
                }
                break;
            case construct::Pragma:
                if (!aNode->children.empty())
                {
                    auto const p = name_text(aNode->child(0)->source);
                    if (restrictions.contains("no_use_of_pragma:" + p))
                        violation(aNode, "No_Use_Of_Pragma => " + p);
                    // (a representation pragma specifying the aspect (13.12.1(6.4/3)))
                    else if (restrictions.contains("no_specification_of_aspect:" + p))
                        violation(aNode, "No_Specification_Of_Aspect => " + p);
                }
                break;
            case construct::Aspects:
                for (auto* a : aNode->children)
                    if (!a->children.empty())
                        if (auto const n = name_text(a->child(0)->source); restrictions.contains("no_specification_of_aspect:" + n))
                            violation(a, "No_Specification_Of_Aspect => " + n);
                break;
            case construct::RepresentationAttribute:
                if (!aNode->children.empty() && aNode->child(0)->is(construct::Attribute) && aNode->child(0)->size() > 1u)
                    if (auto const n = name_text(aNode->child(0)->child(1)->source); restrictions.contains("no_specification_of_aspect:" + n))
                        violation(aNode, "No_Specification_Of_Aspect => " + n);
                break;
            default:
                break;
            }
            // (an expression of a floating (fixed) point type)
            if (t != nullptr && (t->kind == type_kind::Float || t->kind == type_kind::UniversalReal) && !aNode->is(construct::Number) &&
                restricted("no_floating_point") && aNode->type->view()->base_type()->view()->kind == type_kind::Float)
            {
                violation(aNode, "No_Floating_Point");
                return;
            }
            if (t != nullptr && (t->kind == type_kind::Fixed || t->kind == type_kind::Decimal) && restricted("no_fixed_point"))
            {
                violation(aNode, "No_Fixed_Point");
                return;
            }
            bool const local = aLocal || aNode->is(construct::SubprogramBody) || aNode->is(construct::TaskBody) || aNode->is(construct::EntryBody) ||
                aNode->is(construct::Block) || aNode->is(construct::ProtectedBody);
            for (auto* c : aNode->children)
                walk(c, local);
        };
        for (auto* u : iOrder)
        {
            if (predefined(u))
                continue;
            // (its dependences)
            std::vector<std::string> named = u->withs;
            if (auto const dot = u->name.rfind('.'); dot != std::string::npos && !u->subunit)
                named.push_back(u->name.substr(0u, dot));
            if (u->body && !u->subunit)
                named.push_back(u->name);
            std::set<std::string> reported;
            for (auto const& n : named)
                for (auto const& d : closure(n))
                    if (auto const* r = forbids(d))
                        for (auto const& restriction : *r)
                            if (reported.insert(restriction).second)
                                recover([&]() { error(u->unit, "this unit depends on " + d + ", which violates the partition's restriction " + restriction + " (13.12)"); });
            walk(u->item, false);
        }
    }

    void program::c_convention(ada_type* aType)
    {
        // (a type of convention C (B.3): an enumeration type's objects are C's int (B.3(65.1/4)), unless a Size clause says otherwise)
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::Enumeration && !t->size_clause && t->size && *t->size < 4u)
        {
            t->size = 4u;
            t->alignment = 4u;
        }
    }

    std::string program::external_name(node* aExpression, region* aRegion)
    {
        // (a string literal, else a static string expression (e.g. a constant's name: B.1(10.1/3)))
        auto text = std::string{ aExpression->file != nullptr ? aExpression->file->original_of(aExpression->source) : aExpression->source };
        if (text.size() >= 2u && text.front() == '\"' && text.back() == '\"' && text.find('\"', 1u) == text.size() - 1u)
            return text.substr(1u, text.size() - 2u);
        std::string result;
        recover([&]()
        {
            resolve(aExpression, iString, aRegion);
            if (auto const v = evaluate(aExpression, aRegion); v.is_string())
                for (auto c : v.string())
                    result += static_cast<char>(c);
            else
                error(aExpression, "an external name must be a static string expression");
        });
        return result;
    }

    bool program::is_static_resolvable(node* aExpression, region* aRegion)
    {
        // (an expression whose names are all declared (now))
        try
        {
            speculation guard{ *this };
            (void)interpretations(aExpression, aRegion);
            if ((aExpression->is(construct::Identifier) || aExpression->is(construct::Selected)) && prefix_entities(aExpression, aRegion).empty())
                return false;
        }
        catch (compile_error const&)
        {
            return false;
        }
        if (aExpression->is(construct::Identifier))
            return !visible(name_text(aExpression->source), aRegion).empty();
        for (auto* c : aExpression->children)
            if (!is_static_resolvable(c, aRegion))
                return false;
        return true;
    }

    intrinsic program::intrinsic_operation(entity* aSubprogram)
    {
        auto const& n = aSubprogram->name;
        if (n == "+") return aSubprogram->sub->parameters.size() == 1u ? intrinsic::Plus : intrinsic::Add;
        if (n == "-") return aSubprogram->sub->parameters.size() == 1u ? intrinsic::Negate : intrinsic::Subtract;
        if (n == "*") return intrinsic::Multiply;
        if (n == "/") return intrinsic::Divide;
        if (n == "mod") return intrinsic::Mod;
        if (n == "rem") return intrinsic::Rem;
        if (n == "**") return intrinsic::Power;
        if (n == "abs") return intrinsic::Abs;
        if (n == "not") return intrinsic::Not;
        if (n == "and") return intrinsic::And;
        if (n == "or") return intrinsic::Or;
        if (n == "xor") return intrinsic::Xor;
        if (n == "=") return intrinsic::Eq;
        if (n == "/=") return intrinsic::Ne;
        if (n == "<") return intrinsic::Lt;
        if (n == "<=") return intrinsic::Le;
        if (n == ">") return intrinsic::Gt;
        if (n == ">=") return intrinsic::Ge;
        if (n == "&") return intrinsic::Concat;
        return intrinsic::Import;
    }

    void program::pragma_extra(node* aPragma, std::string const& aName, std::vector<node*> const& aArguments, region* aRegion)
    {
        auto const value = [&](std::size_t aIndex) -> node*
        {
            return aIndex < aArguments.size() ? aArguments[aIndex]->children.back() : nullptr;
        };
        if (aName == "assertion_policy")
        {
            // (the policy (Check or Ignore) of the assertion kinds named (else of all) from here to the end of the region (11.4.2(7/3)))
            auto& policies = iAssertionPolicies[aRegion];
            for (auto* a : aArguments)
            {
                std::string kind;
                std::size_t words = 0u;
                for (auto* c : a->children)
                    if (c->is(construct::Word) && c != a->children.back())
                        kind += (words++ > 0u ? "'" : "") + name_text(c->source);
                auto const policy = name_text(a->children.back()->source);
                bool const ignore = policy == "ignore" || policy == "disable";
                if (kind.empty())
                    policies.clear();
                policies[kind] = ignore;
            }
            return;
        }
        if (aName == "asynchronous" && iInstanceBodies == 0)
        {
            // (a remote procedure, or the first subtype of a remote access-to-procedure type, whose parameters are all of mode in, or a
            // remote access-to-class-wide type (E.4.1(8/3)))
            auto* name = value(0);
            if (name == nullptr)
                return;
            auto const remote_place = [&](entity* aEntity, bool aRci)
            {
                if (aEntity == nullptr || aEntity->is_private || aEntity->scope == nullptr || aEntity->scope->owner == nullptr ||
                    !aEntity->scope->owner->library)
                    return false;
                auto const& c = unit_categorization(full_name(aEntity->scope->owner));
                return c.is(categorization::RemoteCallInterface) || (!aRci && c.is(categorization::RemoteTypes));
            };
            auto const in_only = [](subprogram_info const* aProfile)
            {
                if (aProfile == nullptr || aProfile->result != nullptr || aProfile->result_access)
                    return false;
                for (auto const& p : aProfile->parameters)
                    if (p.mode != parameter_mode::In)
                        return false;
                return true;
            };
            bool any = false;
            bool ok = false;
            auto range = aRegion->names.equal_range(name_text(name->source));
            for (auto i = range.first; i != range.second; ++i)
            {
                auto* e = i->second;
                if (e->kind == entity_kind::Procedure || e->kind == entity_kind::Function)
                {
                    any = true;
                    ok = ok || (e->kind == entity_kind::Procedure && in_only(e->sub.get()) && remote_place(e, true));
                }
                else if (e->kind == entity_kind::Type && e->type != nullptr)
                {
                    any = true;
                    auto* t = e->type->view();
                    if (t->kind == type_kind::AccessSubprogram)
                        ok = ok || (in_only(t->profile) && remote_place(e, false));
                    else if (t->kind == type_kind::Access)
                        ok = ok || (t->designated != nullptr && t->designated->view()->kind == type_kind::ClassWide && remote_place(e, false));
                }
                else if (e->kind == entity_kind::Subtype)
                    any = true;
            }
            if (any && !ok)
                recover([&]() { error(aPragma, "pragma Asynchronous applies only to a remote procedure or the first subtype of a remote "
                    "access-to-procedure type whose parameters are of mode in, or to a remote access-to-class-wide type (E.4.1(8/3))"); });
            return;
        }
        if (aName == "default_storage_pool")
        {
            // (the default pool of the access types declared after it in the region (13.11.3(4/3)); null: the standard pool)
            if (auto* pool = value(0))
            {
                if (!pool->is(construct::Null))
                    default_pool_check(pool, aRegion);
                iDefaultPools[aRegion].emplace_back(aPragma->source.data(), pool->is(construct::Null) ? nullptr : pool);
            }
            return;
        }
        if (aName == "assert" || aName == "assume" || aName == "loop_invariant")
        {
            if (assertion_ignored(aName == "assert" ? "assert" : aName, aRegion))
                iIgnoredAssertions.insert(aPragma);
            if (auto* condition = value(0))
                this->condition(condition, aRegion);
            if (auto* message = value(1))
                resolve(message, iString, aRegion);
        }
        else if (aName == "convention" || aName == "export" || aName == "volatile" || aName == "atomic" || aName == "controlled" ||
            aName == "priority" || aName == "storage_size" || aName == "interrupt_priority" || aName == "discard_names" || aName == "no_return" ||
            aName == "unchecked_union" || aName == "atomic_components" || aName == "volatile_components" || aName == "independent" ||
            aName == "restrictions" || aName == "reviewable" || aName == "normalize_scalars" || aName == "linker_options" || aName == "queuing_policy" ||
            aName == "locking_policy" || aName == "task_dispatching_policy" || aName == "detect_blocking" || aName == "asynchronous" ||
            aName == "all_calls_remote" || aName == "remote_types" || aName == "remote_call_interface" || aName == "shared_passive" ||
            aName == "inspection_point" || aName == "partition_elaboration_policy" || aName == "profile" || aName == "assertion_policy" ||
            aName == "default_storage_pool" || aName == "relative_deadline" || aName == "cpu" || aName == "dispatching_domain" ||
            aName == "attach_handler" || aName == "interrupt_handler" || aName == "preelaborable_initialization" || aName == "independent_components" ||
            aName == "unreferenced" || aName == "warnings" || aName == "style_checks" || aName == "ada_95" || aName == "ada_2005" ||
            aName == "ada_2012" || aName == "ada_2022" || aName == "extensions_allowed" || aName == "initialize_scalars")
            ;
        (void)aPragma;
    }

    bool program::assertion_ignored(std::string const& aKind, region* aRegion) const
    {
        // (the innermost policy for the kind (or for all kinds) in effect: Check by default)
        for (auto* r = aRegion; r != nullptr; r = r->parent)
            if (auto p = iAssertionPolicies.find(r); p != iAssertionPolicies.end())
            {
                if (auto k = p->second.find(aKind); k != p->second.end())
                    return k->second;
                if (auto all = p->second.find(std::string{}); all != p->second.end())
                    return all->second;
            }
        return false;
    }

    void program::stream_size_check(ada_type* aType, node* aExpression, node* aAt, region* aRegion)
    {
        // (Stream_Size (13.13.2(1.1/2)-(1.8/2)): of an elementary type; static, nonnegative, a multiple of Stream_Element'Size,
        // at least the first subtype's size; its expression not freezing the type (13.1(9/5)))
        recover([&]()
        {
            auto* v = aType->view();
            if (!v->is_scalar() && v->kind != type_kind::Access && v->kind != type_kind::AccessSubprogram)
                error(aAt, "Stream_Size can be specified only for an elementary type (13.13.2(1.1/2))");
            bool anyInteger = false;
            for (auto const& i : interpretations(aExpression, aRegion))
                anyInteger = anyInteger || (i.type != nullptr && i.type->view()->is_integer());
            if (!anyInteger)
                error(aExpression, "the expression of a Stream_Size clause must be of an integer type");
            std::function<bool(node*)> names = [&](node* aNode)
            {
                if (aNode->ent != nullptr && aNode->ent->is_type() && aNode->ent->type != nullptr && aNode->ent->type->base_type() == aType->base_type())
                    return true;
                for (auto* c : aNode->children)
                    if (c != nullptr && names(c))
                        return true;
                return false;
            };
            if (names(aExpression))
                error(aAt, "the Stream_Size clause's expression freezes the type (13.1(9/5))");
            if (!static_expression(aExpression))
                error(aExpression, "the expression of a Stream_Size clause must be static (13.13.2(1.5/2))");
            auto const value = evaluate(aExpression, aRegion);
            if (!value.is_integer())
                return;
            auto const size = value.integer();
            if (size.negative())
                error(aExpression, "the Stream_Size must be nonnegative (13.13.2(1.5/2))");
            if (!(size % bigint{ 8 } == bigint{ 0 }))
                error(aExpression, "the Stream_Size must be a multiple of Stream_Element'Size (13.13.2(1.5/2))");
            if (v->is_discrete() && v->low.is_integer() && v->high.is_integer() && !(v->high.integer() < v->low.integer()))
            {
                auto const bits = [](bigint aValue)
                {
                    std::uint64_t result = 0u;
                    while (aValue > bigint{ 0 })
                    {
                        aValue = aValue / bigint{ 2 };
                        ++result;
                    }
                    return result;
                };
                auto const low = v->low.integer();
                auto const high = v->high.integer();
                std::uint64_t const minimum = !low.negative() ? bits(high) : std::max(bits(high), bits(bigint{ 0 } - low - bigint{ 1 })) + 1u;
                if (size < bigint{ static_cast<std::int64_t>(minimum) })
                    error(aExpression, "the Stream_Size is less than the size of the first subtype (13.13.2(1.8/2))");
            }
        });
    }

    void program::stream_or_other_attribute_clause(node* aClause, ada_type* aType, std::string const& aAttribute, node* aExpression, region* aRegion)
    {
        (void)aClause;
        if (aAttribute == "read" || aAttribute == "write" || aAttribute == "input" || aAttribute == "output" || aAttribute == "put_image")
        {
            // (T'Class'Write ...: its class-wide type's)
            auto* prefix = aClause != nullptr && aClause->size() >= 1u && aClause->child(0)->size() >= 1u ? aClause->child(0)->child(0) : nullptr;
            bool const classWide = prefix != nullptr && prefix->is(construct::Attribute) && prefix->size() >= 2u && name_text(prefix->child(1)->source) == "class";
            iTypeAspects[aType->base_type()][classWide ? aAttribute + "'class" : aAttribute] = aExpression;
            record_stream_clause(aType, classWide ? aAttribute + "'class" : aAttribute, aClause, aRegion);
            if (aAttribute != "put_image" && aExpression != nullptr && iInstanceBodies == 0)
                stream_subprogram_check(aType, aAttribute, classWide, aExpression, aRegion);
            return;
        }
        // (Storage_Size, Alignment, Component_Size, Object_Size, Stream_Size: of any integer type (13.3, 13.11, 13.13.2))
        if (aAttribute == "storage_size" || aAttribute == "alignment" || aAttribute == "component_size" || aAttribute == "object_size" ||
            aAttribute == "stream_size")
        {
            ada_type* integer = nullptr;
            std::size_t count = 0u;
            for (auto const& i : interpretations(aExpression, aRegion))
                if (i.type != nullptr && i.type->view()->is_integer() && !i.type->is_universal() && i.type != integer)
                {
                    integer = i.type;
                    ++count;
                }
            resolve(aExpression, count == 1u ? integer : nullptr, aRegion);
            if (aAttribute == "stream_size" && iInstanceBodies == 0)
                stream_size_check(aType, aExpression, aClause, aRegion);
        }
        else
            resolve(aExpression, nullptr, aRegion);
        iTypeAspects[aType->base_type()][aAttribute] = aExpression;
        if (aAttribute == "storage_pool")
            storage_pool_check(aType, aExpression, aRegion);
    }

    void program::default_pool_check(node* aPool, region* aRegion)
    {
        // (Default_Storage_Pool's name: of a variable of a type in Root_Storage_Pool'Class (13.11.3(3.1/3)))
        if (iInstanceBodies == 0)
            if (auto* e = resolve_entity(aPool, aRegion); e != nullptr && !e->is_object() && e->kind != entity_kind::Function)
            {
                recover([&]() { error(aPool, "the name of a Default_Storage_Pool must denote a variable of a storage pool type (13.11.3(3.1/3))"); });
                return;
            }
        auto* root = library_type("system.storage_pools", "root_storage_pool");
        resolve(aPool, root != nullptr ? class_wide(root) : nullptr, aRegion);
        if (iInstanceBodies == 0 && !is_variable(aPool))
            recover([&]() { error(aPool, "the name of a Default_Storage_Pool must denote a variable (13.11.3(3.1/3))"); });
    }

    void program::storage_pool_check(ada_type* aAccess, node* aPool, region* aRegion)
    {
        // (a storage pool object: in memory (its allocators may be in nested subprograms))
        for (auto* n = aPool; n != nullptr; n = n->children.empty() ? nullptr : n->child(0))
            if (n->ent != nullptr && n->ent->is_object())
            {
                n->ent->captured = true;
                break;
            }
        // (a pool that supports subpools (13.11.4(22/3), (23/3)): of a pool-specific access type, not statically deeper than the pool
        // object, which is not part of a formal parameter nor of a dereference of a non-library-level general access type)
        if (iInstanceBodies != 0 || aPool->type == nullptr)
            return;
        auto* subpools = library_type("system.storage_pools.subpools", "root_storage_pool_with_subpools");
        if (subpools == nullptr || !is_descendant(aPool->type, subpools))
            return;
        recover([&]()
        {
            auto* v = aAccess->view()->base_type()->view();
            if (v->access_all || v->access_constant)
                error(aPool, "a storage pool that supports subpools cannot be specified for a general access type (13.11.4(22/3))");
            for (auto* n = aPool; n != nullptr; )
            {
                while (n->is(construct::Parenthesized))
                    n = n->child(0);
                // (a dereference: explicit, or implicit (a component's or an element's prefix of an access type))
                bool const implicit = (n->is(construct::Selected) || n->is(construct::Apply)) && !n->children.empty() &&
                    n->child(0)->type != nullptr && n->child(0)->type->view()->kind == type_kind::Access && meaning_of(n->child(0)) == meaning::Object;
                if (n->is(construct::All) || implicit)
                {
                    auto* t = n->child(0)->type != nullptr ? n->child(0)->type->view()->base_type()->view() : nullptr;
                    if (t != nullptr && t->kind == type_kind::Access && (t->access_all || t->access_constant || t->anonymous_access) && type_level(t) != 0)
                        error(aPool, "the storage pool object cannot be part of a dereference of a non-library-level general access type (13.11.4(23/3))");
                    break;
                }
                if ((n->is(construct::Selected) || n->is(construct::Apply)) && !n->children.empty() && meaning_of(n) != meaning::Object)
                {
                    n = n->child(0);
                    continue;
                }
                if (n->is(construct::Selected) && n->ent != nullptr && n->ent->kind == entity_kind::Component)
                {
                    n = n->child(0);
                    continue;
                }
                if (n->ent != nullptr && n->ent->kind == entity_kind::Parameter)
                    error(aPool, "the storage pool object cannot be part of a formal parameter (13.11.4(23/3))");
                break;
            }
            if (auto level = static_level(aPool, aRegion); level.has_value() && type_level(aAccess) > *level)
                error(aPool, "the access type cannot be statically deeper than the storage pool object (13.11.4(22/3))");
        });
    }

    node* program::type_aspect(ada_type* aType, std::string const& aAspect)
    {
        // (a type's aspect (or attribute definition clause): recorded with its partial view or its full view (e.g. 
        // Implicit_Dereference on a private type's declaration, its stream attributes on its full type's))
        if (aType == nullptr)
            return nullptr;
        auto* base = aType->view()->base_type()->view();
        for (auto* key : { base, base->partial, aType->base_type(), aType->base_type()->partial, aType })
        {
            if (key == nullptr)
                continue;
            auto aspects = iTypeAspects.find(key);
            if (aspects == iTypeAspects.end())
                continue;
            auto a = aspects->second.find(aAspect);
            if (a != aspects->second.end())
                return a->second;
        }
        return nullptr;
    }

    void program::stream_subprogram_check(ada_type* aType, std::string const& aAttribute, bool aClassWide, node* aName, region* aRegion)
    {
        // (the subprogram of a stream attribute's definition clause: statically denoted, with the attribute's profile (13.13.2(38/3)),
        // not abstract, a null procedure for an interface, its item's subtype the first subtype (or a scalar's base, 13.13.2(51/3)))
        bool const name = aName->is(construct::Identifier) || aName->is(construct::Selected);
        std::vector<entity*> candidates;
        if (name)
            for (auto* c : prefix_entities(aName, aRegion))
                if (c != nullptr && c->sub != nullptr)
                    candidates.push_back(c);
        if (!name || candidates.empty())
        {
            recover([&]() { error(aName, "the subprogram of a stream attribute definition clause must be statically denoted (13.13.2(38/3))"); });
            return;
        }
        auto* root = library_type("ada.streams", "root_stream_type");
        auto* item = aClassWide ? class_wide(aType) : aType;
        bool const input = aAttribute == "input";
        auto const sameType = [](ada_type* a, ada_type* b)
        {
            if (a == nullptr || b == nullptr)
                return false;
            auto* x = a->view()->base_type()->view();
            auto* y = b->view()->base_type()->view();
            return x == y;
        };
        auto const matches = [&](entity* c)
        {
            auto const& s = *c->sub;
            if (input != (s.result != nullptr) || s.parameters.size() != (input ? 1u : 2u))
                return false;
            auto const& stream = s.parameters[0];
            auto* st = stream.type != nullptr ? stream.type->view() : nullptr;
            if (st == nullptr || st->kind != type_kind::Access || !st->anonymous_access || st->designated == nullptr ||
                (root != nullptr && !sameType(st->designated, class_wide(root))))
                return false;
            if (input)
                return sameType(s.result, item);
            auto const& object = s.parameters[1];
            return object.mode == (aAttribute == "read" ? parameter_mode::Out : parameter_mode::In) && sameType(object.type, item);
        };
        entity* chosen = nullptr;
        for (auto* c : candidates)
            if (matches(c))
            {
                chosen = c;
                break;
            }
        if (chosen == nullptr)
        {
            recover([&]() { error(aName, "the subprogram does not have the profile of the stream attribute '" + aAttribute + " (13.13.2(38/3))"); });
            return;
        }
        aName->ent = chosen;
        if (chosen->sub->is_abstract)
            recover([&]() { error(aName, "the subprogram of a stream attribute definition clause cannot be abstract (13.13.2(38/3))"); });
        if (aType->view()->base_type()->view()->kind == type_kind::Interface && !aClassWide && !chosen->sub->is_null)
            recover([&]() { error(aName, "the stream attribute of an interface type must be specified by a null procedure (13.13.2(38/3))"); });
        if (!aClassWide)
        {
            auto* subtype = input ? chosen->sub->result : chosen->sub->parameters[1].type;
            bool const first = subtype == aType || subtype->view() == aType->view() || (aType->view()->is_scalar() && subtype == aType->base_type());
            if (!first)
                recover([&]() { error(aName, "the subtype of the stream attribute subprogram's item must be the first subtype (13.13.2(51/3))"); });
        }
    }

    void program::record_stream_clause(ada_type* aType, std::string const& aAttribute, node* aClause, region* aRegion)
    {
        // (where a stream attribute is specified: in a package's private part (visible where it is), or elsewhere)
        bool inPrivate = false;
        if (aRegion != nullptr && aRegion->private_part != nullptr)
            for (node const* n = aClause; n != nullptr && !inPrivate; n = n->parent)
                inPrivate = n == aRegion->private_part;
        iStreamClauses[aType->base_type()][aAttribute].push_back({ aClause, aRegion, inPrivate });
    }

    bool program::stream_attribute_specified(ada_type* aType, std::string const& aAttribute, region* aRegion)
    {
        // (specified by a visible attribute definition clause or aspect (13.13.2(44/2), (47/2)))
        if (aType == nullptr)
            return false;
        auto* base = aType->view()->base_type()->view();
        for (auto* key : { base, base->partial, aType->base_type(), aType->base_type()->partial, aType })
        {
            if (key == nullptr)
                continue;
            auto clauses = iStreamClauses.find(key);
            if (clauses == iStreamClauses.end())
                continue;
            auto c = clauses->second.find(aAttribute);
            if (c == clauses->second.end())
                continue;
            for (auto const& clause : c->second)
            {
                if (clause.scope == nullptr || aRegion == nullptr)
                    return true;
                // (within its region; a package's visible part's elsewhere too; its private part's where that is visible)
                for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
                    if (r == clause.scope)
                        return true;
                if (clause.scope->is_package && !clause.scope->package_body && clause.scope->owner != nullptr &&
                    (!clause.in_private || private_part_visible(clause.scope->owner, aRegion)))
                    return true;
            }
        }
        return false;
    }

    bool program::stream_attribute_available(ada_type* aType, std::string const& aAttribute, region* aRegion, int aDepth)
    {
        // (a stream-oriented attribute's availability (13.13.2(39/2)-(48/2)))
        if (aType == nullptr || aDepth > 32)
            return true;
        if (!limited_type(aType, aRegion))
            return true;
        auto* b = aType->base_type();
        if (b->kind == type_kind::ClassWide)
            return b->specific == nullptr || stream_attribute_specified(b->specific, aAttribute + "'class", aRegion) ||
                stream_attribute_available(b->specific, aAttribute, aRegion, aDepth + 1);
        if (stream_attribute_specified(aType, aAttribute, aRegion))
            return true;
        // (Input (Output): if Read (Write) is)
        if (aAttribute == "input" || aAttribute == "output")
            return stream_attribute_available(aType, aAttribute == "input" ? "read" : "write", aRegion, aDepth + 1);
        // (the view here: a partial view's full type where that is visible)
        auto* v = b;
        if (v->kind == type_kind::Private && v->full != nullptr && v->declared_in != nullptr && v->declared_in->owner != nullptr &&
            private_part_visible_here(v->declared_in->owner, aRegion))
            v = v->full->base_type();
        auto* parent = v->parent != nullptr && v->parent->base_type() != v ? v->parent : nullptr;
        if (parent == nullptr || v->kind == type_kind::Private)
            return false;
        if (!stream_attribute_available(parent, aAttribute, aRegion, aDepth + 1))
            return false;
        // (an untagged derived type: inherited (42/2); a record extension: and its extension components' (41/2))
        if (!v->tagged)
            return true;
        auto const& inherited = parent->view()->base_type()->view()->components;
        for (auto const& c : v->components)
        {
            if (c.discriminant || c.type == nullptr || c.ent == nullptr)
                continue;
            if (std::any_of(inherited.begin(), inherited.end(), [&](auto const& i) { return i.ent == c.ent || (i.ent != nullptr && i.ent->name == c.ent->name); }))
                continue;
            if (!stream_attribute_available(c.type, aAttribute, aRegion, aDepth + 1))
                return false;
        }
        return true;
    }

    node* program::default_value_of(ada_type* aType, bool aComponent)
    {
        // (a scalar type's Default_Value, an array type's Default_Component_Value (its own, or its parent's))
        if (aType == nullptr)
            return nullptr;
        for (auto* t = aType->view()->base_type()->view(); t != nullptr; t = t->parent != nullptr ? t->parent->view()->base_type()->view() : nullptr)
        {
            if (auto* a = type_aspect(t, aComponent ? "default_component_value" : "default_value"))
                return a;
            if (t->parent == t)
                break;
        }
        return nullptr;
    }

    entity* program::aspect_subprogram(ada_type* aType, std::string const& aAspect, bool aInherited)
    {
        for (auto* t = aType->view()->base_type()->view(); t != nullptr; t = t->parent != nullptr ? t->parent->base_type()->view() : nullptr)
        {
            if (auto* aspect = type_aspect(t, aAspect))
            {
                {
                    auto* name = aspect;
                    if (name->ent != nullptr)
                        return name->ent;
                    // (the aspect's subprogram: a primitive of the type with the name)
                    auto const text = name_text(name->source);
                    for (auto* op : t->primitives)
                        if (op->name == text)
                            return op;
                    if (t->declared_in != nullptr)
                    {
                        auto range = t->declared_in->names.equal_range(text);
                        for (auto i = range.first; i != range.second; ++i)
                            if (i->second->sub != nullptr)
                                return i->second;
                    }
                }
            }
            if (!aInherited)
                break;
            if (t->parent == nullptr)
                break;
        }
        return nullptr;
    }

    void program::variable_name(node* aName)
    {
        // (the name and the prefixes it is a component, slice or element of)
        for (auto* n = aName; n != nullptr; )
        {
            while (n->is(construct::Parenthesized) && !n->children.empty())
                n = n->child(0);
            iVariableNames.insert(n);
            if ((n->is(construct::Selected) || n->is(construct::Apply)) && !n->children.empty())
                n = n->child(0);
            else
                break;
        }
    }

    std::vector<entity*> program::indexing_candidates(ada_type* aType, entity* aFunction)
    {
        std::vector<entity*> result;
        for (auto* op : prefixed_candidates(aType, aFunction->name))
            if (op->sub != nullptr && std::find(result.begin(), result.end(), op) == result.end())
                result.push_back(op);
        if (aFunction->scope != nullptr)
        {
            auto range = aFunction->scope->names.equal_range(aFunction->name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->sub != nullptr && std::find(result.begin(), result.end(), i->second) == result.end())
                    result.push_back(i->second);
        }
        // (an inherited operation overridden: the overriding one only)
        std::erase_if(result, [&](entity* op)
        {
            for (auto* other : result)
                if (other != op && other->sub->inherited == false && op->sub->inherited && type_conformant(*other->sub, *op->sub))
                    return true;
            return false;
        });
        return result;
    }

    ada_type* program::resolve_generalized_indexing(node* aApply, node* aPrefix, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion)
    {
        // (Container (Index): a call of the type's Constant_Indexing or Variable_Indexing function)
        auto const options = interpretations(aPrefix, aRegion);
        for (auto const& o : options)
        {
            if (o.type == nullptr)
                continue;
            // (Constant_Indexing unless the name is in a variable context (or there is none) and its prefix denotes a variable
            // (4.1.6(16/3, 17/3)))
            if (aPrefix->ent == nullptr && aPrefix->is(construct::Identifier) && o.ent != nullptr && o.ent->is_object())
                aPrefix->ent = o.ent;   // (the object that the prefix denotes)
            bool const variable = iVariableNames.contains(aApply) && (aPrefix->ent == nullptr || is_variable(aPrefix));
            for (auto const* aspect : { variable ? "variable_indexing" : "constant_indexing", variable ? "constant_indexing" : "variable_indexing" })
            {
                auto* function = aspect_subprogram(o.type, aspect);
                if (function == nullptr)
                    continue;
                // (the function's overloads: those of its name in its region, and the type's primitive operations of its name (a
                // derived type's overriding ones and new overloads, 4.1.6(10/3)))
                std::vector<entity*> candidates = indexing_candidates(o.type, function);
                // (of those whose actuals match: one whose result (or its dereference) is of the expected type)
                std::erase_if(candidates, [&](entity* c) { return !match_call(c, aArguments, aPrefix, aRegion, nullptr, true); });
                if (aExpected != nullptr && candidates.size() > 1u)
                    std::stable_partition(candidates.begin(), candidates.end(), [&](entity* c)
                    {
                        auto* r = c->sub->result;
                        return r != nullptr && (covers(aExpected, r) || covers(aExpected, implicit_dereference(r)));
                    });
                for (auto* c : candidates)
                    {
                        auto* r = finish_call(aApply, c, aArguments, aPrefix, aRegion);
                        (void)aExpected;
                        // (generalized indexing: its result (a reference) is implicitly dereferenced)
                        if (auto* d = implicit_dereference(r); d != r)
                        {
                            iImplicitDereferences[aApply] = r;
                            aApply->type = d;
                            return d;
                        }
                        return r;
                    }
            }
        }
        return nullptr;
    }

    std::optional<std::uint64_t> program::reference_offset(ada_type* aReference)
    {
        // (a reference type's access discriminant's offset)
        auto* base = aReference->view()->base_type()->view();
        auto* aspect = type_aspect(aReference, "implicit_dereference");
        if (aspect == nullptr)
            return {};
        layout(base);
        auto const discriminant = name_text(aspect->source);
        for (auto const& c : base->components)
            if (c.discriminant && c.ent->name == discriminant)
                return c.offset;
        return {};
    }

    ada_type* program::implicit_dereference(ada_type* aType)
    {
        // (a reference type (Implicit_Dereference aspect): the type its access discriminant designates)
        if (aType == nullptr)
            return nullptr;
        auto* base = aType->view()->base_type()->view();
        auto* aspect = type_aspect(aType, "implicit_dereference");
        if (aspect == nullptr)
            return aType;
        auto const discriminant = name_text(aspect->source);
        for (auto const& c : base->components)
            if (c.discriminant && c.ent->name == discriminant && c.type != nullptr && c.type->view()->kind == type_kind::Access)
                return designated_of(c.type);
        return aType;
    }

    ada_type* program::resolve_container_aggregate(node* aAggregate, ada_type* aType, region* aRegion)
    {
        // ([...] of a type with the Aggregate aspect: resolved against its Add_Unnamed / Add_Named / New_Indexed operations'
        // element types)
        ada_type* element = iterator_element_type(aType);
        for (auto* item : aAggregate->children)
        {
            if (item->is(construct::Association))
            {
                for (auto* choice : item->child(0)->children)
                    resolve(choice, nullptr, aRegion);
                resolve(item->children.back(), element, aRegion);
            }
            else if (item->is(construct::Iterated))
            {
                auto* r = new_region(nullptr, aRegion);
                iStatementRegions[item] = r;
                iterator_specification(item->child(0), r);
                resolve(item->children.back(), element, r);
            }
            else
                resolve(item, element, aRegion);
        }
        set_meaning(aAggregate, meaning::Value);
        aAggregate->type = aType;
        return aType;
    }

    ada_type* program::iterator_cursor_type(ada_type* aIterator)
    {
        // (the cursor type of an iterator type: the result of its First function)
        if (aIterator == nullptr)
            return nullptr;
        auto* t = aIterator->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view();
        for (auto* a = t; a != nullptr; a = a->parent != nullptr ? a->parent->base_type()->view() : nullptr)
        {
            for (auto* op : a->primitives)
                if (op->name == "first" && op->sub != nullptr && op->sub->result != nullptr)
                    return op->sub->result;
            for (auto* i : a->interfaces)
                for (auto* op : i->view()->primitives)
                    if (op->name == "first" && op->sub != nullptr && op->sub->result != nullptr)
                        return op->sub->result;
            if (a->parent == nullptr)
                break;
        }
        return iInteger;
    }

    ada_type* program::iterator_element_type(ada_type* aContainer)
    {
        if (aContainer == nullptr)
            return nullptr;
        auto* t = aContainer->view()->base_type()->view();
        for (auto* a = t; a != nullptr; a = a->parent != nullptr ? a->parent->base_type()->view() : nullptr)
        {
            if (auto* e = type_aspect(a, "iterator_element"))
            {
                if (e->type == nullptr)
                    try
                    {
                        speculation guard{ *this };
                        e->type = subtype_mark(e, a->declared_in != nullptr ? a->declared_in : iStandard);
                    }
                    catch (compile_error const&)
                    {
                    }
                if (e->type != nullptr)
                    return e->type;
            }
            if (a->parent == nullptr)
                break;
        }
        return nullptr;
    }

    void program::resolve_requeue(node* aRequeue, region* aRegion)
    {
        auto* name = aRequeue->child(0);
        // (an entry of the same or another task or protected object)
        try
        {
            speculation guard{ *this };
            resolve(name, iVoid, aRegion);
        }
        catch (compile_error const&)
        {
            // (a procedure of a synchronized interface (implemented by an entry): a dispatching requeue on the object (9.5.4(3/3)))
            if (name->is(construct::Selected))
            {
                auto* prefix = name->child(0);
                auto const selector = name_text(name->child(1)->source);
                ada_type* pt = nullptr;
                try { speculation guard{ *this }; pt = resolve(prefix, nullptr, aRegion); } catch (compile_error const&) { pt = nullptr; }
                if (pt != nullptr && pt->view()->is_access())
                    pt = designated_of(pt);
                auto* specific = pt != nullptr && pt->view()->kind == type_kind::ClassWide ? pt->view()->specific : pt;
                if (auto* iv = specific != nullptr ? specific->view()->base_type()->view() : nullptr; iv != nullptr && iv->kind == type_kind::Interface)
                    for (auto const& s : iv->slots)
                        if (s.operation != nullptr && s.operation->name == selector && s.operation->kind == entity_kind::Procedure)
                        {
                            name->ent = s.operation;
                            set_meaning(name, meaning::Entry);
                            auto& call = iCalls[name];
                            call.callee = s.operation;
                            call.object = prefix;
                            return;
                        }
            }
            auto const entities = prefix_entities(name, aRegion);
            if (entities.empty())
                throw;
            name->ent = entities.front();
            set_meaning(name, meaning::Entry);
        }
        if (iInstanceBodies != 0)
            return;
        // (the target: no actual parameters; its profile: none, or type conformant with the enclosing entry's (9.5.4(3/3)-(5/3)))
        auto const* call = call_of(name);
        entity* target = call != nullptr && call->callee != nullptr ? call->callee : name->ent;
        if (call != nullptr)
            for (auto* actual : call->actuals)
                if (actual != nullptr && actual != call->object && actual->parent != nullptr &&
                    (actual->parent == name || (actual->parent->is(construct::Association) && actual->parent->parent == name)))
                {
                    recover([&]() { error(name, "a requeue cannot give actual parameters"); });
                    break;
                }
        entity* enclosing = nullptr;
        for (auto* p = aRequeue->parent; p != nullptr && enclosing == nullptr; p = p->parent)
        {
            if (p->is(construct::Accept))
                if (auto r = iStatementRegions.find(p); r != iStatementRegions.end())
                    if (auto e = iAcceptEntries.find(r->second); e != iAcceptEntries.end())
                        enclosing = e->second;
            if (p->is(construct::EntryBody) || p->is(construct::SubprogramBody) || p->is(construct::TaskBody))
                break;
        }
        if (enclosing == nullptr && iCurrentSubprogram != nullptr && iCurrentSubprogram->kind == entity_kind::Entry)
            enclosing = iCurrentSubprogram;
        if (target != nullptr && enclosing != nullptr && target->sub != nullptr && enclosing->sub != nullptr && !target->sub->parameters.empty() &&
            !type_conformant(*target->sub, *enclosing->sub))
            recover([&]() { error(name, "the target of a requeue must have no parameters or a profile type conformant with the enclosing entry's"); });
    }

    // --- generic units

    void program::generic_declaration(node* aDecl, region* aRegion)
    {
        auto* formals = aDecl->first(construct::GenericFormals);
        entity* e = nullptr;
        for (auto* c : aDecl->children)
        {
            if (c->is(construct::Package))
            {
                e = declare(entity_kind::GenericPackage, c->first(construct::Defining), aRegion);
                c->ent = e;
            }
            else if (c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification))
            {
                // (a generic subprogram's name: an identifier, not an operator symbol (12.1(3)))
                if (c->child(0)->is(construct::DefiningOperator))
                    recover([&]() { error(c->child(0), "a generic subprogram cannot be an operator"); });
                e = declare(c->is(construct::FunctionSpecification) ? entity_kind::GenericFunction : entity_kind::GenericProcedure, c->child(0), aRegion);
                e->sub->spec = c;
            }
        }
        // (a generic procedure's No_Return aspect: its instances are nonreturning (its other aspects are its instances'))
        for (auto* holder : { aDecl, e != nullptr && e->sub != nullptr ? e->sub->spec : nullptr })
            if (auto* aspects = holder != nullptr ? holder->first(construct::Aspects) : nullptr)
                for (auto* aspect : aspects->children)
                    if (name_text(aspect->child(0)->source) == "no_return" && e != nullptr && e->sub != nullptr &&
                        (aspect->size() < 2u || aspect->children.back() == aspect->child(0) || name_text(aspect->children.back()->source) == "true"))
                        e->sub->no_return = true;
        if (e == nullptr)
            return;
        e->decl = aDecl;
        aDecl->ent = e;
        // (Pre'Class and Post'Class: only of a primitive subprogram of a tagged type, not of a generic (6.1.1(5/3)))
        if (iInstanceBodies == 0 && e->sub != nullptr)
            for (auto* holder : { aDecl, e->sub->spec })
                if (auto* aspects = holder != nullptr ? holder->first(construct::Aspects) : nullptr)
                    for (auto* aspect : aspects->children)
                        if (auto const n = name_text(aspect->child(0)->source); (n == "pre" || n == "post") && aspect->size() > 2u &&
                            aspect->child(1)->is(construct::Word) && name_text(aspect->child(1)->source) == "class")
                            recover([&]() { error(aspect, "aspect " + n + "'Class can be specified only for a primitive subprogram of a tagged type"); });
        // (the formals' forms (12.4(2/3), 12.5(2.1/3)-(2.3/3)))
        if (formals != nullptr && iInstanceBodies == 0)
            for (auto* f : formals->children)
                recover([&]()
                {
                    if (f->is(construct::FormalObject) && f->has_flag("out") && !f->has_flag("in"))
                        error(f, "a formal object's mode must be in or in out");
                    if (f->is(construct::FormalObject))
                    {
                        // (its subtype: a subtype mark (no constraint); a default only for mode in (12.4(2/3), 12.4(5)))
                        node* mark = nullptr;
                        node* defaultExpression = nullptr;
                        for (auto* c : f->children)
                            if (c->is(construct::ConstraintRange) || c->is(construct::ConstraintDigits) || c->is(construct::ConstraintDelta) ||
                                c->is(construct::ConstraintIndex))
                                error(c, "a formal object's subtype must be a subtype mark (without a constraint)");
                            else if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                                (mark == nullptr ? mark : defaultExpression) = c;
                        // (an index constraint: parsed as a name)
                        if (mark != nullptr && mark->is(construct::Apply))
                            error(mark, "a formal object's subtype must be a subtype mark (without a constraint)");
                        if (defaultExpression != nullptr && f->has_flag("out"))
                            error(defaultExpression, "a formal object of mode in out cannot have a default expression");
                        // (not of an incomplete view (3.10.1(5/3-9.4/3)))
                        if (mark != nullptr && !mark->is(construct::AccessDefinition) && !mark->is(construct::Apply))
                        {
                            entity* me = nullptr;
                            {
                                speculation guard{ *this };
                                try { me = resolve_entity(mark, aRegion); } catch (compile_error const&) {}
                            }
                            if (me != nullptr && me->is_type() && me->type != nullptr && me->type->incomplete &&
                                ((me->scope != nullptr && me->scope->owner != nullptr && me->scope->owner->limited_view) ||
                                (me->type->full == nullptr && !me->type->completing && !me->completed)))
                                error(mark, "the name of an incomplete view cannot be used here (3.10.1(5/3))");
                        }
                        // (a null exclusion: of a subtype that does not exclude null)
                        if (mark != nullptr && f->has_flag("not null") && !mark->is(construct::AccessDefinition) && !mark->is(construct::Apply))
                        {
                            ada_type* mt = nullptr;
                            {
                                speculation guard{ *this };
                                try { mt = subtype_mark(mark, aRegion); } catch (compile_error const&) {}
                            }
                            null_exclusion_check(mt, mark);
                        }
                    }
                    // (a formal array type's component subtype: a subtype mark (12.5.3(2)))
                    if (f->is(construct::FormalType))
                        for (auto* d : f->children)
                            if (d->is(construct::TypeArray) || d->is(construct::TypeArrayUnconstrained))
                                if (auto* component = d->first(construct::ComponentDefinition))
                                    for (auto* c : component->children)
                                        if (c->is(construct::SubtypeIndication))
                                        {
                                            std::size_t parts = 0u;
                                            for (auto* x : c->children)
                                                if (!x->is(construct::Flag))
                                                    ++parts;
                                            if (parts > 1u || (parts == 1u && c->children.back()->is(construct::Apply)))
                                                error(c, "a formal array type's component subtype must be a subtype mark (without a constraint)");
                                        }
                    // (a formal constrained array type's index subtypes: subtype marks (12.5.3(2)))
                    if (f->is(construct::FormalType))
                        for (auto* d : f->children)
                            if (d->is(construct::TypeArray))
                                for (auto* index : d->children)
                                    if (!index->is(construct::ComponentDefinition) && !index->is(construct::Identifier) && !index->is(construct::Selected) &&
                                        !(index->is(construct::SubtypeIndication) && index->size() == 1u &&
                                            (index->child(0)->is(construct::Identifier) || index->child(0)->is(construct::Selected))))
                                        error(index, "a formal array type's index subtypes must be subtype marks");
                    // (a formal private type: [abstract] tagged ... in that order (12.5(2.1/3), 12.5.1(2/3)))
                    if (f->is(construct::FormalType))
                        if (auto* fp = f->first(construct::FormalPrivate))
                        {
                            std::vector<std::string> words;
                            for (auto* c : fp->children)
                                if (c->is(construct::Flag))
                                    words.push_back(name_text(c->source));
                            auto const at = [&](std::string const& w) { return static_cast<int>(std::find(words.begin(), words.end(), w) - words.begin()); };
                            int const n = static_cast<int>(words.size());
                            if (at("abstract") < n && at("tagged") == n)
                                error(fp, "an abstract formal private type must be tagged");
                            else if ((at("abstract") < n && at("abstract") > at("tagged")) || (at("limited") < n && at("tagged") < n && at("tagged") > at("limited")) ||
                                (at("abstract") < n && at("abstract") > at("private")) || (at("tagged") < n && at("tagged") > at("private")) ||
                                (at("limited") < n && at("limited") > at("private")))
                                error(fp, "the reserved words of a formal private type must be in the order abstract, tagged, limited");
                        }
                    // (an abstract formal derived type: a private extension (12.5.1(5/3)))
                    if (f->is(construct::FormalType))
                        if (auto* fd = f->first(construct::FormalDerived); fd != nullptr && fd->has_flag("abstract") && !fd->has_flag("private"))
                            error(fd, "an abstract formal derived type must be a formal private extension (with private)");
                    // (a formal type's discriminants: no defaults)
                    if (f->is(construct::FormalType))
                        if (auto* discriminants = f->first(construct::Discriminants))
                            for (auto* d : discriminants->children)
                            {
                                std::size_t defining = 0u;
                                node* last = nullptr;
                                for (auto* x : d->children)
                                    if (x->is(construct::Defining))
                                        ++defining;
                                    else if (!x->is(construct::Flag))
                                        last = x;
                                std::size_t others = 0u;
                                for (auto* x : d->children)
                                    if (!x->is(construct::Defining) && !x->is(construct::Flag))
                                        ++others;
                                if (others > 1u && last != nullptr)
                                    error(last, "a formal type's discriminants cannot have defaults");
                            }
                    if (f->is(construct::FormalType) && f->first(construct::Discriminants) != nullptr)
                        for (auto* d : f->children)
                            if (d->is(construct::FormalDiscrete) || d->is(construct::FormalInteger) || d->is(construct::FormalModular) ||
                                d->is(construct::FormalFloat) || d->is(construct::FormalFixed) || d->is(construct::FormalDecimal) ||
                                d->is(construct::TypeArray) || d->is(construct::TypeArrayUnconstrained) || d->is(construct::TypeAccess) ||
                                d->is(construct::TypeAccessSubprogram) || d->is(construct::TypeInterface))
                                error(f->first(construct::Discriminants), "a formal scalar, array, access or interface type cannot have discriminants");
                });
        // (the formals' names: distinct (unless overloaded), and not redeclared in the generic's specification (the same declarative
        // region, 8.3(26/2)))
        if (formals != nullptr && iInstanceBodies == 0)
        {
            auto const designator = [](node* aDefining)
            {
                auto text = name_text(aDefining->source);
                if (text.size() >= 2u && text.front() == '"')
                    text = text.substr(1u, text.size() - 2u);
                return text;
            };
            std::set<std::string> objects;
            std::set<std::string> subprograms;
            for (auto* f : formals->children)
            {
                // (a program unit pragma: not in a generic formal part (10.1.5(5/1)))
                if (f->is(construct::Pragma) && !f->children.empty())
                    if (auto const pn = name_text(f->child(0)->source); pn == "elaborate_body" || pn == "pure" || pn == "preelaborate" ||
                        pn == "inline" || pn == "remote_types" || pn == "shared_passive" || pn == "remote_call_interface" || pn == "all_calls_remote")
                        recover([&]() { error(f, "pragma " + pn + " is not allowed in a generic formal part"); });
                if (f->is(construct::FormalSubprogram))
                {
                    // (a null default: of a formal procedure that is not abstract (12.6(4.1/2), (8.4/3)))
                    auto const abstractFormal = [&]()
                    {
                        // (is abstract: after its specification (the keyword is not a flag))
                        auto* s = specification_of(f);
                        if (s == nullptr || s->source.data() + s->source.size() > f->source.data() + f->source.size())
                            return false;
                        std::string rest{ f->source.substr(static_cast<std::size_t>(s->source.data() + s->source.size() - f->source.data())) };
                        std::transform(rest.begin(), rest.end(), rest.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        return rest.find("abstract") != std::string::npos;
                    };
                    if (f->has_flag("null") && (f->first(construct::FunctionSpecification) != nullptr || abstractFormal()))
                        recover([&]() { error(f, "a subprogram default of null is allowed only for a formal procedure that is not abstract (12.6(4.1/2))"); });
                    if (auto* spec = specification_of(f); spec != nullptr && !spec->children.empty())
                    {
                        auto const n = designator(spec->child(0));
                        // (an operator symbol: one of the operators (6.1(10/3)))
                        if (spec->child(0)->is(construct::DefiningOperator))
                            recover([&]() { operator_symbol_check(spec->child(0)); });
                        if (objects.contains(n))
                            recover([&]() { error(spec->child(0), "\"" + n + "\" is already declared in this generic formal part"); });
                        subprograms.insert(n);
                    }
                    continue;
                }
                for (auto* d : f->all(construct::Defining))
                {
                    auto const n = designator(d);
                    if (objects.contains(n) || subprograms.contains(n))
                        recover([&]() { error(d, "\"" + n + "\" is already declared in this generic formal part"); });
                    objects.insert(n);
                }
            }
            // (a generic subprogram's parameters: not homographs of its formals (the same declarative region, 8.3(26/2)))
            if (auto* spec = specification_of(aDecl))
                for (auto* p : spec->children)
                    if (p->is(construct::Parameter))
                        for (auto* x : p->all(construct::Defining))
                            if (auto const n = designator(x); objects.contains(n))
                                recover([&]() { error(x, "\"" + n + "\" is already declared in the generic formal part"); });
            if (auto* package = aDecl->first(construct::Package))
                for (auto* part : { package->first(construct::Declarations), package->first(construct::Private) })
                    if (part != nullptr)
                        for (auto* d : part->children)
                        {
                            std::vector<node*> defining = d->all(construct::Defining);
                            if (auto* spec = specification_of(d); spec != nullptr && !spec->children.empty() && defining.empty())
                                defining.push_back(spec->child(0));
                            for (auto* x : defining)
                                if (auto const n = designator(x); objects.contains(n))
                                    recover([&]() { error(x, "\"" + n + "\" is already declared in the generic formal part"); });
                        }
        }
        register_library_unit(e);
        iGenerics.push_back(e);
        // (the generic's region (for expanded names within it))
        e->inner = new_region(e, aRegion);
    }

    ada_type* program::template_formal_type(node* aFormal, std::string const& aName, region* aFormalRegion)
    {
        // (a formal type of a generic being checked (check_generic): a type of its category with no more properties than its contract
        // gives it (12.5))
        node* definition = nullptr;
        node* discriminants = nullptr;
        for (auto* c : aFormal->children)
            if (c->is(construct::Discriminants) || c->is(construct::DiscriminantsUnknown))
                discriminants = c;
            else if (!c->is(construct::Defining) && !c->is(construct::Aspects))
                definition = c;
        auto* owner = new_entity(entity_kind::Type, aName, aFormal->first(construct::Defining), aFormalRegion);
        auto const scalar = [&](type_kind aKind)
        {
            auto* t = new_type(aKind, aName);
            t->decl = owner;
            t->first_subtype = true;
            t->declared_in = aFormalRegion;
            t->is_formal = true;
            t->size = 8u;
            if (aKind == type_kind::Float)
                t->digits = 15;
            owner->type = t;
            return t;
        };
        if (definition == nullptr || aFormal->is(construct::FormalTypeIncomplete))
        {
            auto* t = scalar(type_kind::Private);
            t->incomplete = aFormal->is(construct::FormalTypeIncomplete);
            t->tagged = aFormal->has_flag("tagged") || std::string_view{ aFormal->source }.find(" tagged") != std::string_view::npos;
            t->constrained = discriminants == nullptr;
            t->unknown_discriminants = discriminants != nullptr && discriminants->is(construct::DiscriminantsUnknown);
            return t;
        }
        switch (definition->kind)
        {
        case construct::FormalDiscrete:
            return scalar(type_kind::Enumeration);
        case construct::FormalInteger:
            return scalar(type_kind::Integer);
        case construct::FormalModular:
            return scalar(type_kind::Modular);
        case construct::FormalFloat:
            return scalar(type_kind::Float);
        case construct::FormalFixed:
            return scalar(type_kind::Fixed);
        case construct::FormalDecimal:
            return scalar(type_kind::Decimal);
        case construct::FormalPrivate:
            {
                auto* t = scalar(type_kind::Private);
                t->size.reset();
                t->limited = definition->has_flag("limited");
                t->tagged = definition->has_flag("tagged");
                t->abstract = definition->has_flag("abstract");
                t->constrained = discriminants == nullptr;
                t->unknown_discriminants = discriminants != nullptr && discriminants->is(construct::DiscriminantsUnknown);
                if (discriminants != nullptr && discriminants->is(construct::Discriminants))
                    discriminant_part(discriminants, t, new_region(owner, aFormalRegion));
                return t;
            }
        case construct::FormalDerived:
            {
                auto* mark = definition->first(construct::Identifier) != nullptr ? definition->first(construct::Identifier) :
                    definition->first(construct::Selected) != nullptr ? definition->first(construct::Selected) :
                    definition->first(construct::Attribute) != nullptr ? definition->first(construct::Attribute) : definition->first(construct::SubtypeIndication);
                if (mark == nullptr)
                    return nullptr;
                // (no known discriminant part (12.5.1(5.1/3)))
                if (discriminants != nullptr && discriminants->is(construct::Discriminants))
                    error(discriminants, "a formal derived type cannot have a known discriminant part");
                auto* parent = mark->is(construct::SubtypeIndication) ? subtype_indication(mark, aFormalRegion) : subtype_mark(mark, aFormalRegion);
                // (the ancestor: not class-wide (12.5.1(5/3)))
                if (parent != nullptr && parent->view()->kind == type_kind::ClassWide)
                    error(mark, "the ancestor of a formal derived type cannot be class-wide");
                auto* t = derive(parent, aName, owner, aFormalRegion, nullptr, discriminants);
                t->is_formal = true;
                t->declared_in = aFormalRegion;
                // (its progenitors: interfaces (3.9.4(9/2)), of compatible kinds)
                bool const limitedFormal = definition->has_flag("limited") || definition->has_flag("synchronized");
                bool const synchronizedFormal = definition->has_flag("synchronized");
                recover([&]() { progenitor_check(parent, mark, limitedFormal, synchronizedFormal, type_kind::Record); });
                for (auto* i : definition->children)
                    if (i != mark && (i->is(construct::Identifier) || i->is(construct::Selected)))
                        recover([&]() { t->interfaces.push_back(progenitor(i, aFormalRegion)); progenitor_check(t->interfaces.back(), i, limitedFormal, synchronizedFormal, type_kind::Record); });
                    else if (i->is(construct::Interfaces))
                        for (auto* j : i->children)
                            recover([&]() { t->interfaces.push_back(progenitor(j, aFormalRegion)); progenitor_check(t->interfaces.back(), j, limitedFormal, synchronizedFormal, type_kind::Record); });
                recover([&]() { progenitor_mix_check(parent, t->interfaces, definition); });
                if (std::string_view{ definition->source }.find("private") != std::string_view::npos)
                {
                    t->kind = type_kind::Private;
                    t->tagged = true;
                }
                t->abstract = definition->has_flag("abstract");
                owner->type = t;
                return t;
            }
        default:
            {
                // (an array, access or interface type: its definition (the formal hidden within it, 8.3(16)))
                ada_type* t = nullptr;
                {
                    // (in the formal region while it is declared: hiding an outer homograph too)
                    auto const where = aFormalRegion->names.emplace(aName, owner);
                    struct unlist { region* r; decltype(where) w; ~unlist() { r->names.erase(w); } } unlisted{ aFormalRegion, where };
                    declaring hiding;
                    hiding.add(owner);
                    t = type_definition(definition, aName, owner, aFormalRegion, nullptr);
                }
                if (t != nullptr)
                {
                    t->is_formal = true;
                    t->base_type()->is_formal = true;
                }
                owner->type = t;
                return t;
            }
        }
    }

    void program::check_generic(entity* aGeneric)
    {
        // (the legality of a generic unit's declaration and body: an instance whose actuals are its formals (types of their categories
        // known only by their contracts, the formal objects and subprograms themselves); not one with formal packages)
        auto* declaration = aGeneric->decl;
        if (declaration == nullptr || !declaration->is(construct::Generic))
            return;
        // (a formal package: its actual an instance of its generic whose actuals are its formals (as this check's), or its explicit
        // actual part's)
        auto const kind = aGeneric->kind == entity_kind::GenericPackage ? entity_kind::Package :
            aGeneric->kind == entity_kind::GenericFunction ? entity_kind::Function : entity_kind::Procedure;
        auto* scope = aGeneric->scope != nullptr ? aGeneric->scope : iStandard;
        // (a generic child of a generic library unit: checked as a child of its parent's check instance (its formals visible,
        // 10.1.1(12/3)))
        entity* parentInstance = nullptr;
        if (aGeneric->library)
            if (auto const name = full_name(aGeneric); name.rfind('.') != std::string::npos)
                if (auto* parent = library_unit(name.substr(0u, name.rfind('.'))); parent != nullptr && parent->is_generic())
                    if (auto const i = iTemplateInstanceOf.find(parent); i != iTemplateInstanceOf.end() && i->second->inner != nullptr)
                        parentInstance = i->second;
        auto* instance = new_entity(kind, aGeneric->name, nullptr, scope);
        iTemplateInstances.insert(instance);
        iTemplateInstanceOf[aGeneric] = instance;
        bool const saved = iTemplateCheck;
        struct restore { bool& target; bool value; ~restore() { target = value; } } restoreCheck{ iTemplateCheck, saved };
        iTemplateCheck = true;
        // (a library generic: checked as its unit (e.g. its private with clauses, 10.1.2(12/3)))
        auto* const savedUnit = iCurrentUnit;
        struct restore_unit { unit_info*& target; unit_info* value; ~restore_unit() { target = value; } } restoreUnit{ iCurrentUnit, savedUnit };
        if (aGeneric->library && iCurrentUnit == nullptr)
            if (auto* u = find_unit(full_name(aGeneric), false))
                iCurrentUnit = iTemplateUnit = u;
        struct clear_template { unit_info*& target; ~clear_template() { target = nullptr; } } clearTemplate{ iTemplateUnit };
        auto* const savedChecked = iCheckedGeneric;
        struct restore_checked { node const*& target; node const* value; ~restore_checked() { target = value; } } restoreChecked{ iCheckedGeneric, savedChecked };
        iCheckedGeneric = declaration;
        instantiate(declaration, aGeneric, instance, scope, parentInstance);
    }

    node* program::generic_body(entity* aGeneric)
    {
        auto* g = aGeneric;
        while (g->renamed != nullptr)
            g = g->renamed;
        auto existing = iGenericBodies.find(g);
        if (existing != iGenericBodies.end())
            return existing->second;
        if (g->sub != nullptr && g->sub->body != nullptr)
            return g->sub->body;
        // (a library generic's body unit)
        if (g->library)
            if (auto* u = find_unit(full_name(g), true); u != nullptr)
                return u->item->is(construct::Subunit) ? u->item->child(1) : u->item;
        // (a body declared later in the declarative part that the generic is in, or its stub's subunit)
        if (g->scope != nullptr)
        {
            auto* declaration = g->decl;
            for (auto* p = declaration != nullptr ? declaration->parent : nullptr; p != nullptr; p = p->parent)
            {
                if (!p->is(construct::Declarations) && !p->is(construct::Private))
                    continue;
                std::vector<node*> scopes = { p };
                // (the package body of the package declaration that declares it)
                for (auto* s : scopes)
                    for (auto* d : s->children)
                    {
                        // (a body stub: its subunit's proper body)
                        if (d->is(construct::Stub))
                        {
                            std::string stubName;
                            if (auto* spec = specification_of(d))
                                stubName = name_text(spec->child(0)->source);
                            else if (auto* defining = d->first(construct::Defining))
                                stubName = name_text(defining->source);
                            if (stubName == g->name)
                            {
                                std::string parent;
                                for (auto* r = g->scope; r != nullptr; r = r->parent)
                                    if (r->owner != nullptr && !r->owner->name.empty() && r->owner->name[0] != '%' && (r->owner->is_package() ||
                                        r->owner->is_subprogram() || r->owner->kind == entity_kind::Task || r->owner->kind == entity_kind::Protected))
                                    {
                                        parent = full_name(r->owner);
                                        break;
                                    }
                                for (auto const& u : iUnits)
                                    if (u.subunit && u.name == parent + "." + g->name && u.item != nullptr)
                                        return u.item->is(construct::Subunit) ? u.item->child(1) : u.item;
                            }
                        }
                        if ((d->is(construct::PackageBody) && g->kind == entity_kind::GenericPackage && name_text(d->first(construct::Defining)->source) == g->name) ||
                            ((d->is(construct::SubprogramBody) || d->is(construct::SubprogramExpression) || d->is(construct::SubprogramNull)) && g->kind != entity_kind::GenericPackage && specification_of(d) != nullptr &&
                                name_text(specification_of(d)->child(0)->source) == g->name))
                            return d;
                    }
                break;
            }
            // (in the body of the package that declares the generic)
            if (auto* owner = g->scope->owner; owner != nullptr && owner->is_package())
            {
                // (a declaration of that body: the generic's body, or its stub (its subunit's proper body))
                auto const body_in = [&](node* d) -> node*
                {
                    if (d->is(construct::Stub))
                    {
                        std::string stubName;
                        if (auto* spec = specification_of(d))
                            stubName = name_text(spec->child(0)->source);
                        else if (auto* defining = d->first(construct::Defining))
                            stubName = name_text(defining->source);
                        if (stubName == g->name)
                            for (auto const& u : iUnits)
                                if (u.subunit && u.name == full_name(owner) + "." + g->name && u.item != nullptr)
                                    return u.item->is(construct::Subunit) ? u.item->child(1) : u.item;
                        return nullptr;
                    }
                    if ((d->is(construct::PackageBody) && g->kind == entity_kind::GenericPackage && name_text(d->first(construct::Defining)->source) == g->name) ||
                        ((d->is(construct::SubprogramBody) || d->is(construct::SubprogramExpression) || d->is(construct::SubprogramNull)) && g->kind != entity_kind::GenericPackage && specification_of(d) != nullptr &&
                            name_text(specification_of(d)->child(0)->source) == g->name))
                        return d;
                    return nullptr;
                };
                for (auto& [declNode, bodyRegion] : iBodyRegions)
                    if (bodyRegion->owner == owner)
                        for (auto* d : declNode->first(construct::Declarations)->children)
                            if (auto* b = body_in(d))
                                    return b;
                // (the package body declared later in the declarative part that declares the package)
                for (auto* p = owner->decl != nullptr ? owner->decl->parent : nullptr; p != nullptr; p = p->parent)
                {
                    if (!p->is(construct::Declarations) && !p->is(construct::Private))
                        continue;
                    for (auto* b : p->children)
                        if (b->is(construct::PackageBody) && b->first(construct::Defining) != nullptr && name_text(b->first(construct::Defining)->source) == owner->name)
                            if (auto* decls = b->first(construct::Declarations))
                                for (auto* d : decls->children)
                                    if (auto* b = body_in(d))
                                    return b;
                    break;
                }
                if (owner->library)
                    if (auto* u = find_unit(full_name(owner), true); u != nullptr)
                        if (auto* decls = u->item->first(construct::Declarations))
                            for (auto* d : decls->children)
                                if (auto* b = body_in(d))
                                    return b;
            }
        }
        return nullptr;
    }

    bool program::statically_matching(ada_type* a, ada_type* b)
    {
        // (subtypes that statically match (4.9.1(1.2/2)): of the same type, with statically matching constraints (or none))
        if (a == nullptr || b == nullptr)
            return a == b;
        if (a == b || a->view() == b->view())
            return true;
        auto* x = a->view();
        auto* y = b->view();
        if (!same_type(x, y))
            return false;
        if (x->is_scalar())
        {
            auto const same = [](static_value const& p, static_value const& q)
            {
                if (!p.known() || !q.known())
                    return false;
                return p.is_integer() && q.is_integer() ? p.integer() == q.integer() : p.real() == q.real();
            };
            // (bounds not static: the same subtype (its bounds' hidden constants))
            if (x->low_object != nullptr && x->low_object == y->low_object && x->high_object == y->high_object)
                return true;
            return same(x->low, y->low) && same(x->high, y->high);
        }
        // (access subtypes: both or neither excluding null, of statically matching designated subtypes (4.9.1(2/3)))
        if (x->kind == type_kind::Access)
        {
            if (!a->anonymous_access && !b->anonymous_access && (a->not_null || x->not_null) != (b->not_null || y->not_null))
                return false;
            if (x->designated == y->designated || x->designated == nullptr || y->designated == nullptr)
                return true;
            auto* dx = x->designated->view();
            auto* dy = y->designated->view();
            if (dx->kind == type_kind::Array || dy->kind == type_kind::Array || !dx->discriminant_values.empty() || !dy->discriminant_values.empty())
                return statically_matching(x->designated, y->designated);
            return true;
        }
        if (x->kind == type_kind::Array)
        {
            if (x->constrained != y->constrained || x->indexes.size() != y->indexes.size())
                return false;
            if (!x->constrained)
                return true;
            for (std::size_t i = 0u; i < x->indexes.size(); ++i)
                if (!statically_matching(x->indexes[i], y->indexes[i]))
                    return false;
            return true;
        }
        if (x->constrained != y->constrained)
            return false;
        if (x->discriminant_values.size() != y->discriminant_values.size())
            return false;
        for (std::size_t i = 0u; i < x->discriminant_values.size(); ++i)
        {
            auto const& p = x->discriminant_values[i];
            auto const& q = y->discriminant_values[i];
            // (not static: the same constraint (a subtype of the other without a constraint of its own: the same evaluation))
            if ((!p.known() || !q.known()) && i < x->discriminant_constraint.size() && i < y->discriminant_constraint.size() &&
                x->discriminant_constraint[i] != nullptr && x->discriminant_constraint[i] == y->discriminant_constraint[i])
                continue;
            if (!p.known() || !q.known() || p.integer() != q.integer())
                return false;
        }
        return true;
    }

    bool program::subtype_conformant(subprogram_info const& a, subprogram_info const& b, bool aResultOnly)
    {
        // (subtype conformant profiles (6.3.1(17/3)), as far as the profiles say: the same modes, statically matching subtypes (of
        // access parameters: their designated subtypes), the same aliasing; aResultOnly: the results' subtypes only)
        auto const matching = [&](ada_type* x, ada_type* y)
        {
            if (x == nullptr || y == nullptr)
                return x == y;
            if (x->anonymous_access || y->anonymous_access)
            {
                if (x->anonymous_access != y->anonymous_access || x->view()->access_constant != y->view()->access_constant ||
                    (x->not_null || x->view()->not_null) != (y->not_null || y->view()->not_null))
                    return false;
                auto* xd = x->view()->designated;
                auto* yd = y->view()->designated;
                return xd == nullptr || yd == nullptr || statically_matching(xd, yd);
            }
            return statically_matching(x, y);
        };
        if ((a.result == nullptr) != (b.result == nullptr) || (a.result != nullptr && !matching(a.result, b.result)))
            return false;
        if (aResultOnly)
            return true;
        if (a.parameters.size() != b.parameters.size())
            return false;
        // (a parameter's null exclusion: its own, or its subtype's)
        auto const excludesNull = [](parameter_info const& p) { return p.not_null || (p.type != nullptr && (p.type->not_null || p.type->view()->not_null)); };
        for (std::size_t i = 0u; i < a.parameters.size(); ++i)
            if (a.parameters[i].mode != b.parameters[i].mode || a.parameters[i].aliased != b.parameters[i].aliased ||
                excludesNull(a.parameters[i]) != excludesNull(b.parameters[i]) || !matching(a.parameters[i].type, b.parameters[i].type))
                return false;
        return true;
    }

    std::string program::formal_type_mismatch(node* aFormal, ada_type* aActual, region* aFormalRegion, region* aRegion)
    {
        // (the matching rules for formal types (12.5.1-12.5.5): a message if the actual subtype doesn't match the formal)
        if (aActual == nullptr)
            return {};
        // (the actual's view at the instantiation: a private type's partial view outside the scope of its full view)
        auto* t = aActual;
        if (aActual->kind == type_kind::Private && aActual->full != nullptr)
        {
            auto* package = aActual->base_type()->declared_in != nullptr ? aActual->base_type()->declared_in->owner : nullptr;
            if (package == nullptr || !private_part_visible(package, aRegion))
                t = aActual;
            else
                t = aActual->view();
        }
        else
            t = aActual->view();
        auto* base = t->base_type();
        auto const kind = base->kind == type_kind::Private && base->full != nullptr && t != aActual->view() ? type_kind::Private : t->base_type()->view()->kind;
        bool const privateView = t->kind == type_kind::Private && t->full != nullptr && t != t->view();
        auto* v = privateView ? t : t->view();
        auto const limited = [&]()
        {
            auto* b = v->base_type();
            return v->limited || b->limited || b->kind == type_kind::Task || b->kind == type_kind::Protected ||
                (b->view()->kind == type_kind::ClassWide && b->view()->specific != nullptr && b->view()->specific->view()->limited) ||
                (iInstanceBodies == 0 && limited_type(aActual, aRegion));
        };
        auto const definite = [&]()
        {
            auto* b = v->view();
            if (b->kind == type_kind::ClassWide || v->unknown_discriminants || b->unknown_discriminants)
                return false;
            if (b->kind == type_kind::Array)
                return b->constrained;
            if (b->constrained || privateView)
                return !(privateView && !v->constrained);
            // (an unconstrained discriminated type: definite if its discriminants have defaults)
            for (auto const& c : b->base_type()->view()->components)
                if (c.discriminant && c.default_expression == nullptr)
                    return false;
            return true;
        };
        node* definition = nullptr;
        node* discriminants = nullptr;
        for (auto* c : aFormal->children)
            if (c->is(construct::Discriminants) || c->is(construct::DiscriminantsUnknown))
                discriminants = c;
            else if (!c->is(construct::Defining) && !c->is(construct::Aspects))
                definition = c;
        // (a formal incomplete type: a tagged actual if declared tagged (12.5(6/3)... 12.5.1))
        if (aFormal->is(construct::FormalTypeIncomplete))
        {
            bool const tagged = aFormal->has_flag("tagged") || (definition != nullptr && definition->is(construct::Flag) && name_text(definition->source) == "tagged");
            if (tagged && !v->base_type()->view()->tagged && v->view()->kind != type_kind::ClassWide && !(v->incomplete && v->tagged))
                return "must be tagged";
            return {};
        }
        if (definition == nullptr)
            return {};
        bool const discrete = !privateView && v->is_discrete();
        switch (definition->kind)
        {
        case construct::FormalDiscrete:
            return discrete ? std::string{} : "must be a discrete type";
        case construct::FormalInteger:
            return !privateView && v->base_type()->view()->kind == type_kind::Integer ? std::string{} : "must be a signed integer type";
        case construct::FormalModular:
            return !privateView && v->base_type()->view()->kind == type_kind::Modular ? std::string{} : "must be a modular type";
        case construct::FormalFloat:
            return !privateView && v->base_type()->view()->kind == type_kind::Float ? std::string{} : "must be a floating point type";
        case construct::FormalFixed:
            return !privateView && v->base_type()->view()->kind == type_kind::Fixed ? std::string{} : "must be an ordinary fixed point type";
        case construct::FormalDecimal:
            return !privateView && v->base_type()->view()->kind == type_kind::Decimal ? std::string{} : "must be a decimal fixed point type";
        case construct::FormalPrivate:
            {
                if (!definition->has_flag("limited") && limited())
                    return "must not be limited";
                if (definition->has_flag("tagged") && !v->base_type()->view()->tagged && v->view()->kind != type_kind::ClassWide)
                    return "must be tagged";
                if (!definition->has_flag("abstract") && v->base_type()->view()->abstract && v->view()->kind != type_kind::ClassWide)
                    return "must not be abstract";
                if (discriminants == nullptr && !definite())
                    return "must be definite";
                if (discriminants != nullptr && discriminants->is(construct::Discriminants))
                {
                    // (known discriminants: an unconstrained subtype with as many discriminants, of statically matching subtypes)
                    auto* b = v->view();
                    if (b->constrained && !b->base_type()->view()->components.empty() && b != b->base_type()->view())
                        return "must be unconstrained";
                    std::vector<ada_type*> actuals;
                    for (auto const& c : b->base_type()->view()->components)
                        if (c.discriminant && !c.stored)
                            actuals.push_back(c.type);
                    std::vector<ada_type*> formals;
                    for (auto* spec : discriminants->children)
                        if (spec->is(construct::Discriminant))
                        {
                            node* mark = nullptr;
                            for (auto* c : spec->children)
                                if (!c->is(construct::Defining) && !c->is(construct::Flag) && mark == nullptr)
                                    mark = c;
                            ada_type* ft = nullptr;
                            if (mark != nullptr && !mark->is(construct::AccessDefinition))
                            {
                                speculation guard{ *this };
                                try { ft = subtype_mark(mark, aFormalRegion); } catch (compile_error const&) {}
                            }
                            for (std::size_t k = 0u, n = spec->all(construct::Defining).size(); k < n; ++k)
                                formals.push_back(ft);
                        }
                    if (formals.size() != actuals.size())
                        return "must have " + std::to_string(formals.size()) + " discriminant" + (formals.size() == 1u ? "" : "s");
                    if (b->constrained && !formals.empty())
                        return "must be unconstrained";
                    for (std::size_t k = 0u; k < formals.size(); ++k)
                        if (formals[k] != nullptr && !statically_matching(formals[k], actuals[k]))
                            return "must have discriminants of statically matching subtypes";
                }
                return {};
            }
        case construct::FormalDerived:
            {
                auto* mark = definition->first(construct::Identifier) != nullptr ? definition->first(construct::Identifier) : definition->first(construct::Selected);
                if (mark == nullptr)
                    return {};
                ada_type* ancestor = nullptr;
                {
                    speculation guard{ *this };
                    try { ancestor = subtype_mark(mark, aFormalRegion); } catch (compile_error const&) {}
                }
                if (ancestor == nullptr)
                    return {};
                auto const derived = [&](ada_type* aType, ada_type* aAncestor)
                {
                    for (auto* x = aType->view()->base_type()->view(); x != nullptr; x = x->parent != nullptr && x->parent->view()->base_type()->view() != x ?
                        x->parent->view()->base_type()->view() : nullptr)
                        if (same_type(x, aAncestor))
                            return true;
                    return false;
                };
                if (!derived(aActual, ancestor) && !is_descendant(aActual, ancestor))
                    return "must be derived from the ancestor type";
                // (a volatile actual: of a volatile ancestor (C.6(12.1/4)))
                if ((type_aspect(aActual, "volatile") != nullptr || type_aspect(aActual, "atomic") != nullptr) &&
                    type_aspect(ancestor, "volatile") == nullptr && type_aspect(ancestor, "atomic") == nullptr)
                    return "must not be volatile (its ancestor is not)";
                // (a formal derived type not declared abstract: the actual not abstract (12.5.1(18)))
                if (!definition->has_flag("abstract") && aActual->view()->kind != type_kind::ClassWide && abstract_type(aActual, aRegion))
                    return "must not be abstract (the formal type is not)";
                if (definition->first(construct::Extension) != nullptr || definition->has_flag("private") || definition->has_flag("with"))
                    if (!aActual->view()->base_type()->view()->tagged && aActual->view()->kind != type_kind::ClassWide)
                        return "must be tagged";
                // (a formal derived type is indefinite if its ancestor subtype is (12.5.1(9)))
                auto* a = ancestor->view();
                bool ancestorDefinite = a->kind != type_kind::ClassWide && !ancestor->unknown_discriminants && !a->unknown_discriminants &&
                    (a->kind != type_kind::Array || a->constrained);
                if (ancestorDefinite && a->kind != type_kind::Array && !a->constrained)
                    for (auto const& c : a->base_type()->view()->components)
                        if (c.discriminant && c.default_expression == nullptr)
                            ancestorDefinite = false;
                if (discriminants == nullptr && ancestorDefinite && !definite())
                    return "must be definite";
                // (no discriminant part and an unconstrained discriminated ancestor subtype: the actual with the same number of
                // discriminants, each corresponding to one of the ancestor's (12.5.1(14)))
                if (discriminants == nullptr && a->kind == type_kind::Record && !a->constrained && a->discriminant_values.empty() && a->known_discriminants)
                {
                    if (aActual->view()->kind == type_kind::ClassWide || aActual->unknown_discriminants || aActual->view()->unknown_discriminants)
                        return "must not have unknown discriminants (its ancestor has known discriminants)";
                    auto const count = [](ada_type* x)
                    {
                        std::size_t n = 0u;
                        for (auto const& c : x->view()->base_type()->view()->components)
                            if (c.discriminant && !c.stored)
                                ++n;
                        return n;
                    };
                    if (count(aActual) != count(ancestor))
                        return "must have as many discriminants as its ancestor";
                    // (new discriminants: each of them the constraint of one of the parent's)
                    for (auto* x = aActual->view()->base_type()->view(); x != nullptr && !same_type(x, ancestor); )
                    {
                        auto* parent = x->parent;
                        if (parent == nullptr)
                            break;
                        auto const& constraint = parent->view()->discriminant_constraint;
                        if (!constraint.empty())
                            for (auto const& c : x->components)
                            {
                                if (!c.discriminant || c.stored || c.ent == nullptr)
                                    continue;
                                bool corresponds = false;
                                for (auto* v : constraint)
                                {
                                    while (v != nullptr && v->is(construct::Parenthesized))
                                        v = v->child(0);
                                    corresponds = corresponds || (v != nullptr && v->is(construct::Identifier) && name_text(v->source) == c.ent->name);
                                }
                                if (!corresponds)
                                    return "must have discriminants corresponding to its ancestor's";
                            }
                        auto* next = parent->view()->base_type()->view();
                        if (next == x)
                            break;
                        x = next;
                    }
                }
                // (no discriminant part and a constrained ancestor subtype: the actual constrained and statically compatible with it
                // (12.5.1(7/3)))
                if (discriminants == nullptr && aActual->view()->kind != type_kind::ClassWide)
                {
                    auto* act = aActual->view();
                    if (a->is_scalar() && act->is_scalar() && a->low.known() && a->high.known() && act->low.known() && act->high.known() &&
                        !(ancestor->first_subtype && a->kind == type_kind::Float))
                    {
                        auto const less = [](static_value const& x, static_value const& y) { return x.real() < y.real(); };
                        bool const nullRange = less(act->high, act->low);
                        if (!nullRange && (less(act->low, a->low) || less(a->high, act->high)))
                            return "must be statically compatible with the ancestor subtype";
                    }
                    else if ((a->kind == type_kind::Array || (a->kind == type_kind::Record && !a->discriminant_values.empty())) &&
                        a->constrained && !a->is_formal)
                    {
                        if (!act->constrained && act->discriminant_values.empty())
                            return "must be constrained (its ancestor subtype is)";
                        // (the constraints' values (the actual can be of a type derived from the ancestor's))
                        auto const same = [](static_value const& p, static_value const& q)
                        {
                            return p.known() && q.known() && (p.is_integer() && q.is_integer() ? p.integer() == q.integer() : p.real() == q.real());
                        };
                        bool compatible = true;
                        if (a->kind == type_kind::Array)
                        {
                            compatible = a->indexes.size() == act->indexes.size();
                            for (std::size_t i = 0u; compatible && i < a->indexes.size(); ++i)
                            {
                                auto* p = a->indexes[i]->view();
                                auto* q = act->indexes[i]->view();
                                compatible = !p->low.known() || !p->high.known() || !q->low.known() || !q->high.known() ||
                                    (same(p->low, q->low) && same(p->high, q->high));
                            }
                        }
                        else
                        {
                            compatible = a->discriminant_values.size() == act->discriminant_values.size();
                            for (std::size_t i = 0u; compatible && i < a->discriminant_values.size(); ++i)
                                compatible = !a->discriminant_values[i].known() || !act->discriminant_values[i].known() ||
                                    same(a->discriminant_values[i], act->discriminant_values[i]);
                        }
                        if (!compatible)
                            return "must be statically compatible with the ancestor subtype";
                    }
                    // (an unconstrained record (with discriminants) or access-to-record ancestor subtype: the actual unconstrained
                    // (12.5.1(7/3)))
                    else if (a->kind == type_kind::Array && !a->constrained && !ancestor->constrained && act->kind == type_kind::Array &&
                        (act->constrained || aActual->constrained) && !a->is_formal)
                        return "must be unconstrained (its ancestor subtype is)";
                    else if (a->kind == type_kind::Record && a->discriminant_values.empty() && !act->discriminant_values.empty() &&
                        [&]() { for (auto const& c : a->base_type()->view()->components) if (c.discriminant && !c.stored) return true; return false; }())
                        return "must be unconstrained (its ancestor subtype is)";
                    else if (a->kind == type_kind::Access && act->kind == type_kind::Access && a->designated != nullptr && act->designated != nullptr &&
                        a->designated->discriminant_values.empty() && !act->designated->discriminant_values.empty())
                        return "must be unconstrained (its ancestor subtype is)";
                    else if (a->kind == type_kind::Access && act->kind == type_kind::Access && a->designated != nullptr &&
                        !a->designated->discriminant_values.empty())
                    {
                        // (an access subtype with a discriminant constraint)
                        auto* q = act->designated;
                        if (q == nullptr || q->discriminant_values.empty())
                            return "must be constrained (its ancestor subtype is)";
                        bool compatible = q->discriminant_values.size() == a->designated->discriminant_values.size();
                        for (std::size_t i = 0u; compatible && i < q->discriminant_values.size(); ++i)
                        {
                            auto const& p = a->designated->discriminant_values[i];
                            auto const& v = q->discriminant_values[i];
                            compatible = !p.known() || !v.known() || (p.is_integer() && v.is_integer() ? p.integer() == v.integer() : p.real() == v.real());
                        }
                        if (!compatible)
                            return "must be statically compatible with the ancestor subtype";
                    }
                }
                return {};
            }
        case construct::TypeArray:
        case construct::TypeArrayUnconstrained:
            {
                auto* b = v->view();
                if (privateView || b->kind != type_kind::Array)
                    return "must be an array type";
                bool const constrained = definition->is(construct::TypeArray);
                if (b->constrained != constrained)
                    return constrained ? "must be a constrained array type" : "must be an unconstrained array type";
                std::vector<node*> indexes;
                node* component = nullptr;
                for (auto* c : definition->children)
                    if (c->is(construct::ComponentDefinition))
                        component = c;
                    else if (!c->is(construct::Flag))
                        indexes.push_back(c);
                if (indexes.size() != b->indexes.size())
                    return "must have " + std::to_string(indexes.size()) + " dimension" + (indexes.size() == 1u ? "" : "s");
                speculation guard{ *this };
                try
                {
                    for (std::size_t k = 0u; k < indexes.size(); ++k)
                    {
                        auto* i = indexes[k];
                        auto* markNode = i->is(construct::IndexSubtype) || i->is(construct::SubtypeIndication) ? i->child(0) : i;
                        auto* ft = subtype_mark(markNode, aFormalRegion);
                        auto* at = b->indexes[k];
                        // (the same index types; statically matching index subtypes (12.5.3(6)))
                        if (!same_type(ft, at) || !statically_matching(ft, at))
                            return "must have index subtypes that statically match the formal's";
                    }
                    if (component != nullptr)
                    {
                        node* ci = nullptr;
                        for (auto* c : component->children)
                            if (!c->is(construct::Flag))
                                ci = c;
                        if (ci != nullptr && !ci->is(construct::AccessDefinition))
                        {
                            auto* ct = ci->is(construct::SubtypeIndication) && ci->size() == 1u ? subtype_mark(ci->child(0), aFormalRegion) : nullptr;
                            if (ct != nullptr && !statically_matching(ct, b->component))
                                return "must have a component subtype that statically matches the formal's";
                        }
                        if (component->has_flag("aliased") && !b->aliased_components)
                            return "must have aliased components (as the formal has)";
                    }
                }
                catch (compile_error const&)
                {
                }
                return {};
            }
        case construct::TypeAccess:
            {
                auto* b = v->view();
                if (privateView || b->kind != type_kind::Access)
                    return "must be an access-to-object type";
                // (both or neither excluding null (12.5.4(4/2)))
                if ((definition->has_flag("not null") || (definition->parent != nullptr && definition->parent->has_flag("not null"))) != (v->not_null || v->view()->not_null))
                    return (definition->has_flag("not null") || (definition->parent != nullptr && definition->parent->has_flag("not null"))) ? "must exclude null (as the formal does, 12.5.4(4/2))" : "must not exclude null (as the formal does not, 12.5.4(4/2))";
                // (access-to-constant if and only if the formal is; a general access-to-variable type if the formal is (12.5.4(3)))
                if (definition->has_flag("constant") != b->access_constant)
                    return definition->has_flag("constant") ? "must be an access-to-constant type" : "must not be an access-to-constant type";
                if (definition->has_flag("all") && !b->access_all)
                    return "must be a general access-to-variable type";
                if (auto* ind = definition->first(construct::SubtypeIndication); ind != nullptr && ind->size() == 1u)
                {
                    speculation guard{ *this };
                    try
                    {
                        auto* dt = subtype_mark(ind->child(0), aFormalRegion);
                        auto* designated = b->base_type()->view()->designated != nullptr ? b->base_type()->view()->designated : b->designated;
                        if (dt != nullptr && designated != nullptr && !statically_matching(dt, designated))
                            return "must have a designated subtype that statically matches the formal's";
                    }
                    catch (compile_error const&)
                    {
                    }
                }
                return {};
            }
        case construct::TypeAccessSubprogram:
            {
                if (privateView || v->view()->kind != type_kind::AccessSubprogram)
                    return "must be an access-to-subprogram type";
                if ((definition->has_flag("not null") || (definition->parent != nullptr && definition->parent->has_flag("not null"))) != (v->not_null || v->view()->not_null))
                    return (definition->has_flag("not null") || (definition->parent != nullptr && definition->parent->has_flag("not null"))) ? "must exclude null (as the formal does, 12.5.4(4/2))" : "must not exclude null (as the formal does not, 12.5.4(4/2))";
                // (the designated profiles subtype conformant (12.5.4(5/3)): modes, subtypes statically matching, protected or not)
                auto* b = v->view()->base_type()->view();
                std::string result;
                speculation guard{ *this };
                try
                {
                    auto* formal = type_definition(definition, "%formal", nullptr, aFormalRegion, nullptr);
                    auto* fp = formal != nullptr ? formal->view()->profile : nullptr;
                    auto* ap = b->profile;
                    if (fp != nullptr && ap != nullptr)
                    {
                        if (formal->view()->protected_access != b->protected_access)
                            result = formal->view()->protected_access ? "must be an access-to-protected-subprogram type" : "must not be an access-to-protected-subprogram type";
                        else if (fp->parameters.size() != ap->parameters.size())
                            result = "must have a designated profile with as many parameters as the formal's";
                        else if ((fp->result == nullptr) != (ap->result == nullptr) || (fp->result != nullptr && !subtype_conformant(*fp, *ap, true)))
                            result = "must have a designated profile whose result subtype statically matches the formal's";
                        else if (!subtype_conformant(*fp, *ap))
                            result = "must have a designated profile that is subtype conformant with the formal's (12.5.4(5/3))";
                    }
                }
                catch (compile_error const&)
                {
                }
                return result;
            }
        case construct::TypeInterface:
            return !privateView && v->view()->kind == type_kind::Interface ? std::string{} : "must be an interface type";
        default:
            return {};
        }
    }

    void program::instantiation(node* aDecl, region* aRegion)
    {
        // (Pre and Post: not of an instance (6.1.1))
        if (auto* aspects = aDecl->first(construct::Aspects); aspects != nullptr && iInstanceBodies == 0)
            for (auto* aspect : aspects->children)
                if (auto const a = name_text(aspect->child(0)->source); a == "pre" || a == "post")
                    recover([&]() { error(aspect, "aspect " + a + " cannot be specified for an instance"); });
        auto* defining = aDecl->first(construct::Defining);
        if (defining == nullptr)
            defining = aDecl->first(construct::DefiningOperator);
        node* name = nullptr;
        for (auto* c : aDecl->children)
            if (c != defining && !c->is(construct::Flag) && !c->is(construct::Aspects))
                name = c;
        node* genericName = name->is(construct::Apply) ? name->child(0) : name;
        // (its actuals: a conditional, quantified or declare expression parenthesized unless it is the only one (4.5.7(7/3)))
        if (name->is(construct::Apply) && iInstanceBodies == 0)
            for (std::size_t i = 1u; i < name->size(); ++i)
            {
                auto* actual = name->child(i)->is(construct::Association) ? name->child(i)->children.back() : name->child(i);
                if (actual->is(construct::IfExpression) || actual->is(construct::CaseExpression) || actual->is(construct::Quantified) ||
                    actual->is(construct::DeclareExpression))
                    parenthesis_check(actual);
            }
        auto* generic = resolve_entity(genericName, aRegion);
        while (generic != nullptr && generic->renamed != nullptr && generic->renamed->is_generic())
            generic = generic->renamed;
        if (generic == nullptr || !generic->is_generic())
            error(genericName, "generic unit name expected");
        entity_kind kind = generic->kind == entity_kind::GenericPackage ? entity_kind::Package :
            generic->kind == entity_kind::GenericFunction ? entity_kind::Function : entity_kind::Procedure;
        auto* instance = declare(kind, defining, aRegion);
        instance->generic = generic;
        aDecl->ent = instance;
        register_library_unit(instance);
        // (a generic child unit named through an instance of its parent: instantiated as the instance's child (10.1.1(19)))
        entity* parentInstance = nullptr;
        if (genericName->is(construct::Selected))
            if (auto* p = resolve_entity(genericName->child(0), aRegion); p != nullptr && p->generic != nullptr && generic->scope != nullptr &&
                generic->scope->owner == p->generic)
                parentInstance = p;
        // (named by its simple name within an instance of its parent: that instance)
        if (parentInstance == nullptr && genericName->is(construct::Identifier) && generic->scope != nullptr && generic->scope->owner != nullptr &&
            generic->scope->owner->is_generic())
            for (region* jump = nullptr, *r = aRegion; r != nullptr && parentInstance == nullptr; r = outward(r, jump))
                if (r->owner != nullptr && r->owner->is_package() && r->owner->generic == generic->scope->owner)
                    parentInstance = r->owner;
        instantiate(aDecl, generic, instance, aRegion, parentInstance);
        // (its actuals: no deferred constant before its full declaration (7.4(9/5)))
        if (name->is(construct::Apply) && iInstanceBodies == 0)
        {
            std::function<node*(node*)> deferred = [&](node* n) -> node*
            {
                if (n->ent != nullptr && n->ent->kind == entity_kind::Constant && n->ent->initial == nullptr && !n->ent->completed &&
                    n->ent->completion == nullptr && !n->ent->imported && n->ent->renamed == nullptr && !(n->ent->decl != nullptr && n->ent->decl->parent != nullptr &&
                        n->ent->decl->parent->is(construct::FormalObject)) && n->ent->renamed_name == nullptr &&
                    n->is(construct::Identifier))
                    return n;
                for (auto* c : n->children)
                    if (auto* x = deferred(c))
                        return x;
                return nullptr;
            };
            for (std::size_t i = 1u; i < name->size(); ++i)
                if (auto* x = deferred(name->child(i)))
                    recover([&]() { error(x, "the deferred constant \"" + x->ent->name + "\" is used before its full declaration"); });
        }
        // (a library subprogram instance: elaborated (its formal objects' values))
        if (instance->library && instance->is_subprogram())
            iPackageElaborations.emplace_back(aDecl, aRegion);
        // (a subprogram instance can be a primitive operation (overriding an inherited one))
        if (instance->is_subprogram() && instance->sub != nullptr)
        {
            bool const overridesImplicit = implicit_homograph(instance, aRegion);
            add_primitive_if(instance, aRegion);
            implicit_inequality(instance, aRegion);
            if (iInstanceBodies == 0)
                recover([&]() { overriding_indicator_check(instance, aDecl, overridesImplicit, aRegion); });
        }
    }

    region* program::generic_context(entity* aGeneric)
    {
        // (the context clauses of the library unit that declares a generic (its specification's and its body's), with the current
        // unit's: visible in an instance of it)
        entity* library = nullptr;
        for (auto* e = aGeneric; e != nullptr && library == nullptr; e = e->scope != nullptr ? e->scope->owner : nullptr)
            if (e->library)
                library = e;
        if (library == nullptr)
            return nullptr;
        auto const name = full_name(library);
        auto* result = new_region(nullptr, nullptr);
        auto const merge = [&](region* aContext)
        {
            if (aContext == nullptr)
                return;
            for (auto* e : aContext->declarations)
                if (std::find(result->declarations.begin(), result->declarations.end(), e) == result->declarations.end())
                    result->add(e);
            result->used.insert(result->used.end(), aContext->used.begin(), aContext->used.end());
            result->used_types.insert(result->used_types.end(), aContext->used_types.begin(), aContext->used_types.end());
            result->used_all_types.insert(result->used_all_types.end(), aContext->used_all_types.begin(), aContext->used_all_types.end());
        };
        merge(current_context());
        for (bool body : { false, true })
            if (auto* u = find_unit(name, body); u != nullptr)
            {
                if (u->context != nullptr)
                    merge(u->context);
                else if (auto* clauses = u->unit->first(construct::Context))
                {
                    auto* r = new_region(nullptr, nullptr);
                    try { speculation guard{ *this }; apply_context(clauses, r); } catch (compile_error const&) {}
                    merge(r);
                }
            }
        return result;
    }

    void program::instantiate(node* aInstantiation, entity* aGeneric, entity* aInstance, region* aRegion, entity* aParentInstance)
    {
        // (checking a generic itself (check_generic): this instance's formals; the instances within it are ordinary)
        bool const templateCheck = iTemplateCheck;
        struct restore_check { bool& target; bool value; ~restore_check() { target = value; } } restoreCheck{ iTemplateCheck, templateCheck };
        iTemplateCheck = false;
        struct site
        {
            std::vector<node*>& sites;
            std::vector<region*>& regions;
            bool pushed;
            ~site() { if (pushed) { sites.pop_back(); regions.pop_back(); } }
        } instantiationSite{ iInstantiationSites, iInstantiationRegions, !templateCheck };
        if (!templateCheck)
        {
            iInstantiationSites.push_back(aInstantiation);
            iInstantiationRegions.push_back(aRegion);
        }
        // (the generic's units' context clauses: visible in the instance)
        auto* const savedContext = iContextOverride;
        auto* const genericContext = generic_context(aGeneric);
        struct restore { region*& target; region* value; ~restore() { target = value; } } restoreContext{ iContextOverride, savedContext };
        if (genericContext != nullptr)
            iContextOverride = genericContext;
        auto* declaration = aGeneric->decl;
        if (declaration == nullptr || !declaration->is(construct::Generic))
            error(aInstantiation, "generic declaration of \"" + aGeneric->name + "\" not found");
        // (not within an instance of itself, directly or not (12.3(19)): the generics whose instances are being made)
        if (std::find(iInstantiating.begin(), iInstantiating.end(), declaration) != iInstantiating.end())
            error(aInstantiation, "a generic unit cannot be instantiated within itself, directly or indirectly (12.3(19))");
        iInstantiating.push_back(declaration);
        struct instantiating { std::vector<node*>& generics; ~instantiating() { generics.pop_back(); } } const instantiatingGeneric{ iInstantiating };
        // (the instance: a copy of the generic's declaration (and body) analysed with its formals denoting the actuals)
        auto* copy = clone(declaration);
        auto* bodyNode = generic_body(aGeneric);
        if (std::getenv("NEOS_ADA_DEBUG"))
            std::fprintf(stderr, "instantiate %s of %s (library %d): body %s\n", aInstance->name.c_str(), full_name(aGeneric).c_str(), aGeneric->library, bodyNode != nullptr ? "found" : "none");
        auto* bodyCopy = bodyNode != nullptr ? clone(bodyNode) : nullptr;
        if (templateCheck && bodyNode != nullptr && aParentInstance == nullptr)
            iTemplateBodies.emplace(bodyNode, bodyCopy);   // (the generic's body as checked: its analysis (see categorization_check))
        else if (!templateCheck)
            iInstanceCopies[aInstantiation] = { copy, bodyCopy };
        auto* instanceRegion = new_region(aInstance, aRegion);
        instanceRegion->is_package = aInstance->kind == entity_kind::Package;
        instanceRegion->level = aRegion->level;
        // (the formals' region: the generic's parent region's declarations are visible)
        auto* genericParent = aParentInstance != nullptr && aParentInstance->inner != nullptr ? aParentInstance->inner : aGeneric->scope != nullptr ? aGeneric->scope : iStandard;
        auto* formalRegion = new_region(aInstance, genericParent);
        formalRegion->level = aRegion->level;
        formalRegion->generic_formals = true;
        formalRegion->visibility_cutoff = aParentInstance == nullptr ? declaration : nullptr;
        // (its Default_Storage_Pool aspect: the default pool of the access types within it (13.11.3(5.1/4)))
        if (auto* aspects = aInstantiation->first(construct::Aspects))
            for (auto* aspect : aspects->children)
            {
                if (aspect->size() >= 2u && name_text(aspect->child(0)->source) == "default_storage_pool")
                {
                    auto* pool = aspect->children.back();
                    if (!pool->is(construct::Null))
                        default_pool_check(pool, aRegion);
                    iDefaultPools[formalRegion].emplace_back(nullptr, pool->is(construct::Null) ? nullptr : pool);
                }
            }
        // (the actuals)
        node* name = nullptr;
        for (auto* c : aInstantiation->children)
            if (!c->is(construct::Defining) && !c->is(construct::DefiningOperator) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                name = c;
        std::vector<node*> actuals;
        if (name != nullptr && name->is(construct::Apply))
            actuals.assign(name->children.begin() + 1, name->children.end());
        std::size_t positional = 0u;
        bool othersBox = false;
        auto const actual_for = [&](std::string const& aFormal) -> node*
        {
            for (auto* a : actuals)
                if (a->is(construct::Association))
                {
                    for (auto* choice : a->child(0)->children)
                    {
                        if (choice->is(construct::Others))
                            othersBox = true;
                        else
                        {
                            // (an operator symbol: its designator, without its quotes)
                            auto text = name_text(choice->source);
                            if (text.size() >= 2u && (text.front() == '\"' || text.front() == '%'))
                                text = text.substr(1u, text.size() - 2u);
                            if (text == aFormal)
                                return a->children.back();
                        }
                    }
                }
            return nullptr;
        };
        auto const next_positional = [&]() -> node*
        {
            while (positional < actuals.size())
            {
                auto* a = actuals[positional++];
                if (!a->is(construct::Association))
                    return a;
                --positional;
                break;
            }
            return nullptr;
        };
        // (the formals in order: their entities in the formal region)
        auto* formals = copy->first(construct::GenericFormals);
        // (the associations: positional ones first, named ones naming formals (12.3(9)))
        if (!templateCheck)
        {
            std::vector<std::string> formalNames;
            for (auto* f : formals->children)
            {
                if (f->is(construct::Pragma) || f->is(construct::Use) || f->is(construct::UseType))
                    continue;
                for (auto* d : f->all(construct::Defining))
                    formalNames.push_back(name_text(d->source));
                if (f->is(construct::FormalSubprogram))
                    if (auto* spec = specification_of(f); spec != nullptr && !spec->children.empty())
                    {
                        auto text = name_text(spec->child(0)->source);
                        if (text.size() >= 2u && text.front() == '\"')
                            text = text.substr(1u, text.size() - 2u);
                        formalNames.push_back(text);
                    }
            }
            bool named = false;
            std::size_t positional = 0u;
            std::set<std::string> given;
            for (auto* a : actuals)
                if (a->is(construct::Association))
                {
                    named = true;
                    for (auto* choice : a->child(0)->children)
                        if (!choice->is(construct::Others))
                        {
                            auto text = name_text(choice->source);
                            if (text.size() >= 2u && (text.front() == '\"' || text.front() == '%'))
                                text = text.substr(1u, text.size() - 2u);
                            if (std::find(formalNames.begin(), formalNames.end(), text) == formalNames.end())
                                error(choice, "\"" + text + "\" is not a formal parameter of the generic");
                            // (overloaded formal subprograms (sharing a designator): not by named association (12.3(9)))
                            else if (std::count(formalNames.begin(), formalNames.end(), text) > 1)
                                error(choice, "the formals named \"" + text + "\" are overloaded: their actuals cannot be given by named association (12.3(9))");
                            else if (!given.insert(text).second)
                                error(choice, "the formal \"" + text + "\" is given more than one actual");
                        }
                }
                else if (named)
                    error(a, "a positional association cannot follow a named one");
                else if (positional >= formalNames.size())
                    error(a, "too many generic actual parameters");
                else
                    given.insert(formalNames[positional++]);
        }
        // (formal derived types' operations (views of their ancestors'): those of a later formal derived from one)
        std::map<entity*, std::vector<entity*>> formalOperations;
        for (auto* f : formals->children)
        {
            auto const formal = [&]()
            {
                switch (f->kind)
                {
                case construct::FormalType:
                case construct::FormalTypeIncomplete:
                    {
                        auto* d = f->first(construct::Defining);
                        auto const formalName = name_text(d->source);
                        auto* actual = actual_for(formalName);
                        if (actual == nullptr)
                            actual = next_positional();
                        ada_type* actualType = nullptr;
                        if (actual != nullptr)
                        {
                            incomplete_use use{ *this, incomplete_uses::Allowed };
                            actualType = subtype_indication(actual, aRegion);
                            // (a subtype mark (12.5(2/3)): no constraint)
                            std::size_t parts = 0u;
                            for (auto* c : actual->children)
                                if (!c->is(construct::Flag))
                                    ++parts;
                            if (iInstanceBodies == 0 && ((actual->is(construct::SubtypeIndication) && parts > 1u) ||
                                (actual->is(construct::Apply) && is_subtype_name(actual->child(0), aRegion))))
                                recover([&]() { error(actual, "the actual for a formal type must be a subtype mark (without a constraint)"); });
                        }
                        else if (auto* def = f->children.back(); def->is(construct::Identifier) || def->is(construct::Selected))
                            actualType = subtype_mark(def, formalRegion);
                        // (checking the generic itself: a type of the formal's category, known only by its contract)
                        if (actualType == nullptr && templateCheck)
                            actualType = template_formal_type(f, formalName, formalRegion);
                        if (actualType == nullptr)
                            error(aInstantiation, "no actual for formal type \"" + formalName + "\"");
                        if (actual != nullptr && (f->is(construct::FormalType) || f->is(construct::FormalTypeIncomplete)))
                            if (auto const mismatch = formal_type_mismatch(f, actualType, formalRegion, aRegion); !mismatch.empty())
                                error(actual, "the actual for formal type \"" + formalName + "\" " + mismatch);
                        // (an instantiation freezes its actual types (13.14(5/3)): completely defined (but a formal incomplete type's))
                        if (actual != nullptr && !f->is(construct::FormalTypeIncomplete) && not_completely_defined(actualType, aRegion))
                            error(actual, "the actual type must be completely defined (its full declaration has not occurred yet)");
                        // (a formal type specified Atomic, Atomic_Components, Volatile or Volatile_Components: the actual's aspect True too (an
                        // atomic type's Volatile; its own or an ancestor's) (C.6(8/3, 12.1/5)))
                        if (auto* aspects = f->first(construct::Aspects); aspects != nullptr && actualType != nullptr)
                            for (auto* aspect : aspects->children)
                            {
                                auto const name = name_text(aspect->child(0)->source);
                                auto* value = aspect->size() > 1u && aspect->children.back() != aspect->child(0) ? aspect->children.back() : nullptr;
                                if ((name != "atomic" && name != "volatile" && name != "atomic_components" && name != "volatile_components") ||
                                    (value != nullptr && name_text(value->source) != "true"))
                                    continue;
                                bool specified = false;
                                for (auto* t = actualType->view()->base_type()->view(); t != nullptr && !specified;
                                    t = t->parent != nullptr && t->parent != t ? t->parent->view()->base_type()->view() : nullptr)
                                    specified = type_aspect(t, name) != nullptr ||
                                        (name == "volatile" && type_aspect(t, "atomic") != nullptr) ||
                                        (name == "volatile_components" && type_aspect(t, "atomic_components") != nullptr);
                                if (!specified)
                                    error(aInstantiation, "the actual for formal type \"" + formalName + "\" must be " +
                                        (name == "atomic" ? "atomic" : name == "volatile" ? "volatile" : name == "atomic_components" ?
                                        "specified Atomic_Components" : "specified Volatile_Components") + " (C.6(12.1/5))");
                            }
                        auto* e = declare(entity_kind::Subtype, d, formalRegion);
                        e->type = actualType;
                        // (an untagged formal type: its own subtype (of the actual), distinguishing the declarations whose profiles differ in
                        // it from others in the instance (12.3(11)))
                        if (actualType != nullptr && f->is(construct::FormalType) && !actualType->view()->base_type()->view()->tagged &&
                            actualType->view()->kind != type_kind::ClassWide)
                        {
                            auto* view = new_subtype(actualType, formalName);
                            view->formal_view = view;
                            e->type = view;
                        }
                        // (a formal type with known discriminants: its discriminants' names denote the actual's discriminants (12.5.1))
                        if (auto* discriminants = f->first(construct::Discriminants); discriminants != nullptr && actualType != nullptr)
                        {
                            std::vector<std::string> names;
                            for (auto* spec : discriminants->children)
                                if (spec->is(construct::Discriminant))
                                    for (auto* dd : spec->all(construct::Defining))
                                        names.push_back(name_text(dd->source));
                            bool renamed = false;
                            std::size_t k = 0u;
                            for (auto const& c : actualType->view()->base_type()->view()->components)
                                if (c.discriminant && c.ent != nullptr)
                                    renamed = renamed || (k < names.size() && names[k++] != c.ent->name);
                            if (renamed)
                            {
                                auto* view = new_subtype(actualType->view(), formalName);
                                view->formal_view = nullptr;
                                view->formal_discriminants = names;
                                view->formal_scope = formalRegion;
                                e->type = view;
                            }
                        }
                        // (an untagged formal type: within the instance its operators are the predefined ones (not the actual's user-defined
                        // ones), as are those its derived types inherit (12.3(15)))
                        if (actualType != nullptr && f->first(construct::FormalDerived) == nullptr && !actualType->view()->base_type()->view()->tagged)
                            formalRegion->formal_actuals.push_back(actualType->view()->base_type()->view());
                        // (a formal derived type: the operations it inherits from its ancestor (the ancestor's parameter names and defaults),
                        // denoting the actual's corresponding operations (12.5.1(21)))
                        if (auto* derived = f->first(construct::FormalDerived))
                            if (auto* ancestorMark = derived->first(construct::Identifier) != nullptr ? derived->first(construct::Identifier) : derived->first(construct::Selected))
                            {
                                auto* ancestor = subtype_mark(ancestorMark, formalRegion)->view()->base_type()->view();
                                // (a class-wide actual: its specific type's operations, its calls dispatching (12.5.1(21/3)))
                                auto* actualSpecific = actualType->view()->kind == type_kind::ClassWide && actualType->view()->specific != nullptr ?
                                    actualType->view()->specific : actualType;
                                auto* actualBase = actualSpecific->view()->base_type()->view();
                                // (an ancestor that is an earlier formal derived type: its operations (views of its ancestor's, 12.5.1(21/3)))
                                auto* ancestorFormal = resolve_entity(ancestorMark, formalRegion);
                                auto const viaFormal = ancestorFormal != nullptr ? formalOperations.find(ancestorFormal) : formalOperations.end();
                                if (ancestor->tagged)
                                    formalRegion->formal_ancestor_slots.emplace_back(actualBase, std::max(ancestor->slots.size(), ancestor->partial != nullptr ? ancestor->partial->slots.size() : 0u));
                                std::vector<entity*> operations = ancestor->primitives;
                                if (ancestor->partial != nullptr)
                                    for (auto* op : ancestor->partial->primitives)
                                        if (std::find(operations.begin(), operations.end(), op) == operations.end())
                                            operations.push_back(op);
                                if (viaFormal != formalOperations.end() && !ancestor->tagged)
                                    operations = viaFormal->second;
                                auto& ownOperations = formalOperations[e];
                                std::vector<entity*> actualOperations = actualBase->primitives;
                                if (actualBase->partial != nullptr)
                                    for (auto* op : actualBase->partial->primitives)
                                        if (std::find(actualOperations.begin(), actualOperations.end(), op) == actualOperations.end())
                                            actualOperations.push_back(op);
                                auto const substitute = [&](ada_type* aType) { return aType != nullptr && same_type(aType, ancestor) ? actualSpecific : aType; };
                                for (auto* op : operations)
                                {
                                    if (op->sub == nullptr || op->sub->op == intrinsic::NotEqual)
                                        continue;
                                    // (the actual's operation: the one in the same slot (tagged); an untagged ancestor's own operation (that a formal
                                    // ancestor's view denotes))
                                    entity* target = ancestor->tagged ? nullptr : (op->sub->renames != nullptr && viaFormal != formalOperations.end() ? op->sub->renames : op);
                                    for (auto* a : ancestor->tagged ? actualOperations : std::vector<entity*>{})
                                        if (a->name == op->name && a->sub != nullptr && a->sub->parameters.size() == op->sub->parameters.size())
                                        {
                                            if (op->sub->slot >= 0 && a->sub->slot == op->sub->slot)
                                            {
                                                target = a;
                                                break;
                                            }
                                            bool same = true;
                                            for (std::size_t i = 0u; i < a->sub->parameters.size() && same; ++i)
                                                same = same_type(a->sub->parameters[i].type, substitute(op->sub->parameters[i].type));
                                            if (same && (a->sub->result == nullptr) == (op->sub->result == nullptr) &&
                                                (a->sub->result == nullptr || same_type(a->sub->result, substitute(op->sub->result))))
                                                target = target != nullptr ? target : a;
                                        }
                                    if (target == nullptr)
                                        continue;
                                    auto* alias = new_entity(op->kind, op->name, op->decl, formalRegion);
                                    alias->sub->parameters = op->sub->parameters;
                                    for (auto& p : alias->sub->parameters)
                                        p.type = substitute(p.type);
                                    alias->sub->result = substitute(op->sub->result);
                                    alias->type = alias->sub->result;
                                    alias->sub->renames = target;
                                    alias->sub->slot = target->sub->slot;
                                    alias->sub->controlling = target->sub->controlling;
                                    alias->sub->op = target->sub->op;
                                    alias->sub->operator_type = target->sub->operator_type;
                                    alias->sub->level = target->sub->level;
                                    alias->sub->defined = target->sub->defined;
                                    formalRegion->add(alias);
                                    ownOperations.push_back(alias);
                                }
                            }
                        break;
                    }
                case construct::FormalObject:
                    {
                        bool const inOut = f->has_flag("out");
                        ada_type* formalType = nullptr;
                        node* defaultExpression = nullptr;
                        for (auto* c : f->children)
                        {
                            if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects) || c->is(construct::ConstraintRange) ||
                                c->is(construct::ConstraintDigits) || c->is(construct::ConstraintDelta) || c->is(construct::ConstraintIndex))
                                continue;
                            if (formalType == nullptr)
                                formalType = c->is(construct::AccessDefinition) ? access_definition(c, formalRegion) : subtype_mark(c, formalRegion);
                            else
                                defaultExpression = c;
                        }
                        if (formalType != nullptr && f->has_flag("not null") && formalType->view()->is_access() && !formalType->view()->not_null)
                        {
                            // (a null exclusion)
                            formalType = new_subtype(formalType->view());
                            formalType->not_null = true;
                        }
                        // (a formal object of mode in: not of an abstract type (3.9.3(8/3)))
                        if (templateCheck && !inOut && abstract_type(formalType, formalRegion))
                            recover([&]() { error(f, "a formal object of mode in cannot be of an abstract type"); });
                        // (checking the generic itself: the formal objects are objects of their subtypes; a default is resolved)
                        if (templateCheck)
                        {
                            declaring hiding;
                            for (auto* d : f->all(construct::Defining))
                            {
                                auto* e = declare(inOut ? entity_kind::Variable : entity_kind::Constant, d, formalRegion);
                                e->type = formalType;
                                hiding.add(e);
                            }
                            if (defaultExpression != nullptr && formalType != nullptr)
                            {
                                resolve(defaultExpression, formalType, formalRegion);
                                others_check(defaultExpression, formalType);    // (an unconstrained array formal: no applicable index constraint)
                            }
                            break;
                        }
                        for (auto* d : f->all(construct::Defining))
                        {
                            auto const formalName = name_text(d->source);
                            auto* actual = actual_for(formalName);
                            if (actual == nullptr)
                                actual = next_positional();
                            bool const useDefault = actual == nullptr;
                            if (useDefault)
                                actual = defaultExpression;
                            if (actual == nullptr)
                                error(aInstantiation, "no actual for formal object \"" + formalName + "\"");
                            auto* e = declare(inOut ? entity_kind::Variable : entity_kind::Constant, d, formalRegion);
                            e->type = formalType;
                            resolve(actual, formalType, useDefault ? formalRegion : aRegion);
                            if (!useDefault && iSpeculation == 0)
                                others_check(actual, formalType);   // (an unconstrained array formal: no applicable index constraint)
                            // (the actual of an in out formal object: a variable (12.4(7)))
                            if (inOut && !is_variable(actual))
                                error(actual, "the actual for an in out formal object must be a variable");
                            // (a name denoting a variable: not a type conversion (12.4(7)))
                            if (inOut && actual != nullptr && iInstanceBodies == 0 && meaning_of(actual) == meaning::Conversion)
                                recover([&]() { error(actual, "the actual for an in out formal object cannot be a type conversion (12.4(7))"); });
                            // (a formal in out object with a null exclusion: the actual's subtype excludes null (12.4(8.1/5)); in a generic body, an
                            // actual that is a formal in out object of the generic: its declaration has a null exclusion (assume the worst))
                            if (inOut && actual != nullptr && iSpeculation == 0 && (iInstanceBodies == 0 || iInstanceBodies == iInstancePrivateParts) && actual->type != nullptr)
                            {
                                auto const explicitNotNull = [](node* aFormal) {
                                    if (aFormal->has_flag("not null"))
                                        return true;
                                    auto* access = aFormal->first(construct::AccessDefinition);
                                    return access != nullptr && access->has_flag("not null");
                                };
                                if (explicitNotNull(f))
                                {
                                    bool excludes = actual->type->not_null || actual->type->view()->not_null;
                                    auto* ent = actual->ent;
                                    if (ent != nullptr && ent->decl != nullptr && ent->decl->parent != nullptr && ent->decl->parent->is(construct::FormalObject) &&
                                        ent->decl->parent->has_flag("out") && within_generic_body(aRegion))
                                        excludes = explicitNotNull(ent->decl->parent);
                                    if (!excludes)
                                        recover([&]() { error_in_private_part_too(actual, "the subtype of the actual for an in out formal object with a null exclusion must exclude null (12.4(8.1/5))"); });
                                }
                            }
                            // (the actual's type: the formal's (resolved to it, not covered by it) (12.4(4)))
                            if (inOut && actual != nullptr && iInstanceBodies == 0 && formalType != nullptr && actual->type != nullptr &&
                                formalType->view()->kind == type_kind::ClassWide && !same_type(actual->type, formalType))
                                recover([&]() { error(actual, "the type of the actual for an in out formal object must be the formal's type (12.4(4))"); });
                            // (an atomic (volatile) actual: of an atomic (volatile) formal type (C.6(12/3)))
                            if (inOut && iInstanceBodies == 0 && formalType != nullptr)
                            {
                                auto const [atomic, isVolatile] = atomic_view(actual);
                                // (a formal type: not atomic (or volatile), whatever its actual)
                                bool const formal = formalType->formal_view != nullptr || formalType->view()->formal_view != nullptr;
                                if (atomic && (formal || type_aspect(formalType, "atomic") == nullptr))
                                    error(actual, "an atomic object can be the actual of an in out formal object only if the formal's type is atomic (C.6(12/3))");
                                else if (isVolatile && (formal || (type_aspect(formalType, "atomic") == nullptr && type_aspect(formalType, "volatile") == nullptr)))
                                    error(actual, "a volatile object can be the actual of an in out formal object only if the formal's type is volatile (C.6(12/3))");
                            }
                            // (nor a subcomponent that depends on discriminants of an unconstrained variable (12.4(9.3/2) via 8.5.1(5/3)))
                            if (inOut && iInstanceBodies == 0 && discriminant_dependent(actual))
                                recover([&]() { error(actual, "the actual for an in out formal object cannot be a subcomponent that depends on discriminants of an unconstrained variable"); });
                            if (inOut)
                            {
                                // (a view of the actual: of its subtype (12.4(10/2)))
                                // (not where the formal type's discriminants are named differently (they name the actual's within the instance))
                                if (actual->type != nullptr && formalType != nullptr && same_type(actual->type, formalType) &&
                                    formalType->view()->formal_discriminants.empty())
                                    e->type = actual->type;
                                e->where = storage::Renaming;
                                e->renamed_name = actual;
                                e->renamed = actual->ent;
                                iInstanceConstants.push_back({ e, aInstance });     // (its address: evaluated at the instantiation)
                            }
                            else
                            {
                                e->initial = actual;
                                auto v = formalType != nullptr && formalType->view()->is_scalar() ? evaluate(actual, aRegion) : static_value{};
                                if (v.known())
                                    e->value = v;
                                iInstanceConstants.push_back({ e, aInstance });
                            }
                        }
                        break;
                    }
                case construct::FormalSubprogram:
                    {
                        auto* spec = specification_of(f);
                        auto* d = spec->child(0);
                        auto const kind = spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure;
                        std::string formalName = d->is(construct::DefiningOperator) ? name_text(d->source) : name_text(d->source);
                        if (formalName.size() >= 2u && formalName.front() == '\"')
                            formalName = formalName.substr(1u, formalName.size() - 2u);
                        auto* e = new_entity(kind, formalName, d, formalRegion);
                        d->ent = e;
                        auto* parameters = new_region(e, formalRegion);
                        parameters->level = formalRegion->level + 1;
                        subprogram_specification(spec, e, parameters);
                        // (a formal abstract subprogram: exactly one controlling type, not incomplete (12.6(8.3/2)... (8.5/3)); its calls
                        // dispatching (12.6(8.5/2)), its actual a dispatching operation of that type's actual (12.6(8.5/3)))
                        bool const abstractFormal = [&]()
                            {
                                std::string rest{ f->source.substr(std::min(f->source.size(), static_cast<std::size_t>(spec->source.data() + spec->source.size() - f->source.data()))) };
                                std::transform(rest.begin(), rest.end(), rest.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                return rest.find("abstract") != std::string::npos;
                            }();
                        ada_type* abstractControlling = nullptr;
                        if (abstractFormal && iInstanceBodies == 0)
                        {
                            std::vector<ada_type*> controlling;
                            bool incomplete = false;
                            auto const consider = [&](ada_type* aType)
                            {
                                if (aType == nullptr)
                                    return;
                                if (aType->anonymous_access && aType->designated != nullptr)
                                    aType = aType->designated;
                                auto* v = aType->view();
                                if (v->kind == type_kind::ClassWide || !(v->tagged || v->base_type()->view()->tagged))
                                    return;
                                if (aType->incomplete || v->incomplete)
                                    incomplete = true;
                                auto* b = v->base_type()->view();
                                if (std::find(controlling.begin(), controlling.end(), b) == controlling.end())
                                    controlling.push_back(b);
                            };
                            for (auto const& p : e->sub->parameters)
                                consider(p.type);
                            consider(e->sub->result);
                            if (templateCheck && controlling.size() != 1u)
                                recover([&]() { error(f, "a formal abstract subprogram must have exactly one controlling type (12.6(8.3/2))"); });
                            else if (templateCheck && incomplete)
                                recover([&]() { error(f, "the controlling type of a formal abstract subprogram cannot be incomplete (12.6(8.5/3))"); });
                            if (controlling.size() == 1u)
                                abstractControlling = controlling.front();
                            if (templateCheck && abstractControlling != nullptr)
                            {
                                e->sub->is_abstract = true;
                                e->sub->controlling = abstractControlling;
                            }
                        }
                        auto* actual = actual_for(formalName);
                        if (actual == nullptr)
                            actual = next_positional();
                        entity* target = nullptr;
                        if (actual != nullptr)
                        {
                            target = resolve_subprogram_name(actual, e, aRegion);
                            e->sub->renamed_name = actual;
                            // (null exclusions of the formal's parameters and result: the actual's too (12.6(8.6/3)))
                            if (target != nullptr && target->sub != nullptr && !templateCheck && iInstanceBodies == 0 &&
                                target->sub->parameters.size() == e->sub->parameters.size())
                            {
                                auto const excludes = [](parameter_info const& p) { return p.not_null || (p.type != nullptr && (p.type->not_null || p.type->view()->not_null)); };
                                bool mismatch = false;
                                for (std::size_t k = 0u; k < e->sub->parameters.size(); ++k)
                                    if (excludes(e->sub->parameters[k]) && !excludes(target->sub->parameters[k]))
                                        mismatch = true;
                                auto const resultExcludes = [](subprogram_info const& s) { return s.result_not_null || (s.result != nullptr && (s.result->not_null ||
                                    s.result->view()->not_null)); };
                                if (resultExcludes(*e->sub) && target->sub->result != nullptr && !resultExcludes(*target->sub))
                                    mismatch = true;
                                if (mismatch)
                                    recover([&]() { error(actual, "the actual subprogram's parameters and result must exclude null where the formal's do (12.6(8.6/3))"); });
                            }
                            if (abstractFormal && target != nullptr && target->sub != nullptr && !templateCheck && iInstanceBodies == 0 &&
                                (abstractControlling == nullptr || target->sub->controlling == nullptr ||
                                 target->sub->controlling->view()->base_type()->view() != abstractControlling))
                                recover([&]() { error(actual, "the actual of a formal abstract subprogram must be a dispatching operation of its controlling type (12.6(8.5/3))"); });
                            // (a prefixed view: a subprogram (its body calls the operation with the object and the parameters))
                            if (auto* call = call_of(actual); call != nullptr && call->prefixed && call->object != nullptr && actual->is(construct::Selected) &&
                                target != nullptr && target->kind != entity_kind::Entry && target->sub != nullptr &&
                                target->sub->parameters.size() == e->sub->parameters.size() + 1u)
                            {
                                prefixed_view_object(actual, aRegion);
                                parameters->is_subprogram = true;
                                e->sub->level = parameters->level;
                                e->sub->prefixed_actual = true;
                                e->sub->body = actual;
                                for (auto* root = call_of(actual)->object; root != nullptr; root = root->children.empty() ? nullptr : root->child(0))
                                    if (root->is(construct::Identifier))
                                    {
                                        if (root->ent != nullptr && root->ent->is_object())
                                            note_reference(root->ent, parameters);
                                        break;
                                    }
                                iSubprogramsToGenerate.push_back(e);
                                target = nullptr;
                            }
                        }
                        else
                        {
                            // (the default: is <> (a visible subprogram of the same name and profile at the instantiation), is null, or a name)
                            node* defaultNode = nullptr;
                            for (auto* c : f->children)
                                if (c != spec && !c->is(construct::Aspects))
                                    defaultNode = c;
                            if (templateCheck && (defaultNode == nullptr || defaultNode->is(construct::Box)))
                                ;   // (checking the generic itself: the formal subprogram)
                            else if (defaultNode != nullptr && defaultNode->is(construct::Box))
                            {
                                auto* n = &iClones.emplace_back();
                                n->kind = d->is(construct::DefiningOperator) ? construct::Operator : construct::Identifier;
                                n->source = d->source;
                                n->file = d->file;
                                target = resolve_subprogram_name(n, e, aRegion);
                                e->sub->renamed_name = n;
                            }
                            else if (defaultNode != nullptr && defaultNode->is(construct::Flag))
                            {
                                e->sub->is_null = true;
                                e->sub->body = defaultNode;
                                e->convention = "intrinsic";    // (a null procedure default: of convention Intrinsic (12.6(16.2/2)))
                            }
                            else if (defaultNode != nullptr)
                            {
                                // (a default that is the formal's own designator: the formal itself, hidden from all visibility within its
                                // declaration, hiding its homographs (all those that the default could denote) (8.3(16)))
                                if (templateCheck && (defaultNode->is(construct::Identifier) || defaultNode->is(construct::Operator)) &&
                                    [](std::string a, std::string b) { auto const strip = [](std::string& t) { if (t.size() >= 2u && t.front() == '\"') t = t.substr(1u, t.size() - 2u); };
                                        strip(a); strip(b); return a == b; }(name_text(defaultNode->source), name_text(d->source)))
                                    recover([&]() { error(defaultNode, "the default of a formal subprogram cannot be its own designator (the formal hides it, 8.3(16))"); });
                                target = resolve_subprogram_name(defaultNode, e, formalRegion);
                                e->sub->renamed_name = defaultNode;
                            }
                            else
                                error(aInstantiation, "no actual for formal subprogram \"" + formalName + "\"");
                        }
                        if (target != nullptr)
                        {
                            e->sub->renames = target;
                            if (target->sub != nullptr)
                            {
                                // (the actual (or default): mode conformant with the formal (12.6(6)))
                                if (!templateCheck && iInstanceBodies == 0 && target->sub->parameters.size() == e->sub->parameters.size())
                                    for (std::size_t k = 0u; k < e->sub->parameters.size(); ++k)
                                        if (e->sub->parameters[k].mode != target->sub->parameters[k].mode)
                                        {
                                            recover([&]() { error(actual != nullptr ? actual : aInstantiation, "the actual subprogram must be mode conformant with the formal "
                                                "(parameter modes differ) (12.6(6))"); });
                                            break;
                                        }
                                e->sub->op = target->sub->op;
                                e->sub->operator_type = target->sub->operator_type;
                                e->sub->import_name = target->sub->import_name;
                                // (a nonabstract formal subprogram's actual: not abstract (12.6(8.5/2)))
                                bool const formalAbstract = f->has_flag("abstract") || std::string_view{ f->source }.find(" abstract") != std::string_view::npos;
                                if (!formalAbstract && target->sub->is_abstract && !templateCheck)
                                    recover([&]() { error(actual != nullptr ? actual : aInstantiation, "the actual for a nonabstract formal subprogram cannot be abstract"); });
                                // (a formal abstract subprogram: a dispatching operation (its calls with class-wide actuals dispatch,
                                // 12.6(8.4/3)))
                                if ((f->has_flag("abstract") || std::string_view{ f->source }.find(" abstract") != std::string_view::npos) &&
                                    target->sub->slot >= 0 && target->sub->controlling != nullptr)
                                {
                                    e->sub->slot = target->sub->slot;
                                    e->sub->controlling = target->sub->controlling;
                                }
                            }
                        }
                        formalRegion->add(e);
                        break;
                    }
                case construct::FormalPackage:
                    {
                        auto* d = f->first(construct::Defining);
                        auto const formalName = name_text(d->source);
                        auto* actual = actual_for(formalName);
                        if (actual == nullptr)
                            actual = next_positional();
                        entity* target = nullptr;
                        if (actual == nullptr && templateCheck)
                        {
                            // (checking the generic itself: an instance of the formal package's generic whose actuals are its formals)
                            node* genericName = nullptr;
                            if (auto* apply = f->first(construct::Apply); apply != nullptr && apply->size() >= 1u)
                                genericName = apply->child(0);
                            auto* generic = genericName != nullptr ? resolve_entity(genericName, formalRegion) : nullptr;
                            while (generic != nullptr && generic->renamed != nullptr && generic->renamed != generic)
                                generic = generic->renamed;
                            if (generic == nullptr || generic->kind != entity_kind::GenericPackage || generic->decl == nullptr)
                                error(f, "the generic package of formal package \"" + formalName + "\" is not found");
                            target = new_entity(entity_kind::Package, formalName, nullptr, formalRegion);
                            bool const saved = iTemplateCheck;
                            struct restore { bool& t; bool v; ~restore() { t = v; } } restoring{ iTemplateCheck, saved };
                            iTemplateCheck = true;
                            // (an explicit actual part: those actuals (the others' synthetic))
                            bool const box = [&]() { auto* apply = f->first(construct::Apply); return apply == nullptr || (apply->size() == 2u && apply->child(1)->is(construct::Box)); }();
                            instantiate(box ? generic->decl : f, generic, target, formalRegion, nullptr);
                            target->generic = generic;
                            // (an explicit actual part (not (<>)): the template's formals are not part of its visible part (12.7(10/2)))
                            if (auto* apply = f->first(construct::Apply); apply != nullptr && !(apply->size() == 2u && apply->child(1)->is(construct::Box)))
                                iFormalPackagesWithActuals.insert(target);
                        }
                        else if (actual == nullptr)
                            error(aInstantiation, "no actual for formal package \"" + formalName + "\"");
                        else
                        {
                            target = resolve_entity(actual, aRegion);
                            // (an instance of the formal package's generic (12.7(5)))
                            if (iInstanceBodies == 0)
                            {
                                node* genericName = nullptr;
                                if (auto* apply = f->first(construct::Apply); apply != nullptr && apply->size() >= 1u)
                                    genericName = apply->child(0);
                                entity* generic = nullptr;
                                {
                                    speculation guard{ *this };
                                    try { generic = genericName != nullptr ? resolve_entity(genericName, formalRegion) : nullptr; } catch (compile_error const&) {}
                                }
                                while (generic != nullptr && generic->renamed != nullptr && generic->renamed != generic)
                                    generic = generic->renamed;
                                auto* instance = target;
                                while (instance != nullptr && instance->renamed != nullptr && instance->renamed != instance && instance->generic == nullptr)
                                    instance = instance->renamed;
                                auto* of = instance != nullptr ? instance->generic : nullptr;
                                while (of != nullptr && of->renamed != nullptr && of->renamed != of)
                                    of = of->renamed;
                                if (generic != nullptr && generic->kind == entity_kind::GenericPackage && (instance == nullptr || !instance->is_package() ||
                                    instance->kind == entity_kind::GenericPackage || of != generic))
                                    error(actual, "the actual for formal package \"" + formalName + "\" must be an instance of its generic package (12.7(5))");
                                // (an explicit actual part: the instance's actuals match it (12.7(6-8)): statically matching subtypes, the same
                                // subprograms and packages, equal static values)
                                auto* apply = f->first(construct::Apply);
                                auto* formalsNode = generic != nullptr && generic->decl != nullptr ? generic->decl->first(construct::GenericFormals) : nullptr;
                                region* instanceFormals = instance != nullptr && instance->inner != nullptr && instance->inner->parent != nullptr &&
                                    instance->inner->parent->generic_formals ? instance->inner->parent : nullptr;
                                if (apply != nullptr && !(apply->size() == 2u && apply->child(1)->is(construct::Box)) && formalsNode != nullptr && instanceFormals != nullptr)
                                {
                                    std::vector<std::string> names;
                                    for (auto* g : formalsNode->children)
                                        if (auto* d = g->first(construct::Defining))
                                            names.push_back(name_text(d->source));
                                        else if (auto* spec = g->children.empty() ? nullptr : g->child(0); spec != nullptr &&
                                            (spec->is(construct::ProcedureSpecification) || spec->is(construct::FunctionSpecification)) && !spec->children.empty())
                                            names.push_back(name_text(spec->child(0)->source));
                                    auto const ultimate = [](entity* e)
                                    {
                                        for (int n = 0; e != nullptr && n < 32; ++n)
                                        {
                                            if (e->sub != nullptr && e->sub->renames != nullptr && e->sub->renames != e)
                                                e = e->sub->renames;
                                            else if (e->renamed != nullptr && e->renamed != e)
                                                e = e->renamed;
                                            else
                                                break;
                                        }
                                        return e;
                                    };
                                    std::size_t position = 0u;
                                    for (std::size_t k = 1u; k < apply->size(); ++k)
                                    {
                                        auto* a = apply->child(k);
                                        std::string formal;
                                        node* value = a;
                                        if (a->is(construct::Association))
                                        {
                                            if (a->child(0)->children.empty() || a->child(0)->child(0)->is(construct::Others))
                                                continue;
                                            formal = name_text(a->child(0)->child(0)->source);
                                            if (formal.size() >= 2u && formal.front() == '"')
                                                formal = formal.substr(1u, formal.size() - 2u);
                                            value = a->children.back();
                                        }
                                        else if (position < names.size())
                                            formal = names[position++];
                                        if (formal.empty() || value == nullptr || value->is(construct::Box))
                                            continue;
                                        auto [i, end] = instanceFormals->names.equal_range(formal);
                                        if (i == end)
                                            continue;
                                        auto* own = i->second;
                                        bool matches = true;
                                        try
                                        {
                                            speculation guard{ *this };
                                            if (own->is_type() && own->type != nullptr)
                                            {
                                                if (auto* t = subtype_mark(value, formalRegion); t != nullptr)
                                                    matches = statically_matching(own->type, t) || same_type(own->type, t) && own->type->view() == t->view();
                                            }
                                            else if (own->sub != nullptr && (own->kind == entity_kind::Function || own->kind == entity_kind::Procedure))
                                            {
                                                auto* mine = ultimate(own);
                                                bool any = false;
                                                for (auto* e : prefix_entities(value, formalRegion))
                                                    any = any || ultimate(e) == mine;
                                                matches = any;
                                            }
                                            else if (own->is_package())
                                            {
                                                auto* e = resolve_entity(value, formalRegion);
                                                matches = e == nullptr || ultimate(e) == ultimate(own);
                                            }
                                            else if (own->kind == entity_kind::Constant && own->initial != nullptr)
                                            {
                                                // (static expressions of the same value, or names statically denoting the same constant, or
                                                // both null)
                                                resolve(value, own->type, formalRegion);
                                                bool const xs = static_expression(own->initial);
                                                bool const ys = static_expression(value);
                                                // (an enclosing instance's formal object: its actual (the formals replaced by the actuals))
                                                std::function<entity*(node*, int)> denoted = [&](node* n, int depth) -> entity*
                                                {
                                                    while (n != nullptr && n->is(construct::Parenthesized))
                                                        n = n->child(0);
                                                    auto* e = n != nullptr && (n->is(construct::Identifier) || n->is(construct::Selected)) && n->ent != nullptr &&
                                                        n->ent->kind == entity_kind::Constant ? ultimate(n->ent) : nullptr;
                                                    if (e != nullptr && depth < 8 && e->scope != nullptr && e->scope->generic_formals && e->initial != nullptr &&
                                                        e->scope != instanceFormals)
                                                        if (auto* actualConstant = denoted(e->initial, depth + 1))
                                                            return actualConstant;
                                                    return e;
                                                };
                                                if (xs && ys)
                                                {
                                                    auto const x = evaluate(own->initial, instanceFormals);
                                                    auto const y = evaluate(value, formalRegion);
                                                    if (x.known() && y.known())
                                                        matches = (x.is_integer() && y.is_integer() && x.integer() == y.integer()) ||
                                                            (x.is_real() && y.is_real() && x.real() == y.real()) ||
                                                            (x.is_string() && y.is_string() && x.string() == y.string());
                                                }
                                                else if (own->initial->is(construct::Null) && value->is(construct::Null))
                                                    matches = true;
                                                else
                                                    matches = denoted(own->initial, 0) != nullptr && denoted(own->initial, 0) == denoted(value, 0);
                                            }
                                        }
                                        catch (compile_error const&) {}
                                        if (!matches)
                                            error(actual, "the actual for formal package \"" + formalName + "\" must have actuals matching its actual part (\"" + formal + "\") (12.7(6-8))");
                                    }
                                }
                            }
                        }
                        auto* e = declare(entity_kind::Package, d, formalRegion);
                        e->renamed = target;
                        e->inner = target != nullptr ? target->inner : nullptr;
                        e->generic = target != nullptr ? target->generic : nullptr;
                        break;
                    }
                case construct::Use:
                case construct::UseType:
                    use_clause(f, formalRegion);
                    break;
                case construct::Pragma:
                    break;
                default:
                    break;
                }
            };
            // (checking the generic itself: each formal declaration in turn (its errors reported, the next checked))
            if (templateCheck)
                recover(formal);
            else
                formal();
        }
        (void)othersBox;
        // (the instance's declarations and body: the formal region is the parent of its region)
        instanceRegion->parent = formalRegion;
        if (aGeneric->kind == entity_kind::GenericPackage)
        {
            auto* package = copy->first(construct::Package);
            package->first(construct::Defining)->ent = aInstance;
            // (within the instance, the generic's name denotes the instance)
            auto* alias = new_entity(entity_kind::Package, aGeneric->name, nullptr, formalRegion);
            alias->renamed = aInstance;
            alias->inner = instanceRegion;
            formalRegion->add(alias);
            aInstance->inner = instanceRegion;
            if (!templateCheck)
                iInstancePackages.insert(package);
            package_declaration(package, aRegion, aInstance);
            iInstances.push_back({ aInstance, package, nullptr, aInstantiation });
            auto const instanceIndex = iInstances.size() - 1u;     // (the body's analysis can instantiate others)
            if (bodyCopy != nullptr)
            {
                bool const saved = instanceRegion->private_visible;
                instanceRegion->private_visible = true;
                // (a generic body declared in a package body: that body's declarations are visible in the instance's body (after
                // the formals))
                region* const savedFormalParent = formalRegion->parent;
                for (auto& [declNode, enclosing] : iBodyRegions)
                    if (auto* decls = declNode->first(construct::Declarations); decls != nullptr &&
                        std::find(decls->children.begin(), decls->children.end(), bodyNode) != decls->children.end() && enclosing->parent == savedFormalParent)
                    {
                        formalRegion->parent = enclosing;
                        break;
                    }
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = bodyNode;
                auto* bodyRegion = new_region(nullptr, instanceRegion);
                bodyRegion->is_package = true;
                bodyRegion->package_body = true;
                bodyRegion->owner = aInstance;
                iBodyRegions[bodyCopy] = bodyRegion;
                bodyCopy->ent = aInstance;
                {
                    instance_body inBody{ *this, !templateCheck };
                    declarations(bodyCopy->first(construct::Declarations), bodyRegion);
                    // (the private part's incomplete types: completed there or in the body (3.10.1(3)))
                    incomplete_check(instanceRegion->private_part, "in the private part or the package body",
                        bodyCopy->first(construct::End) != nullptr ? bodyCopy->first(construct::End) : bodyCopy);
                    if (auto* handled = bodyCopy->first(construct::Handled))
                        statements(handled, bodyRegion);
                }
                formalRegion->parent = savedFormalParent;
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = declaration;
                instanceRegion->private_visible = saved;
                iInstances[instanceIndex].body = bodyCopy;
                // (a library instance's body: elaborated (its declarations, its statements) after its specification)
                if (aInstance->library)
                    iPackageElaborations.emplace_back(bodyCopy, bodyRegion);
            }
        }
        else
        {
            // (a subprogram instance: the body's copy, declared in the instance's region, is the instance (its formal objects are
            // elaborated where it is declared))
            iInstances.push_back({ aInstance, nullptr, nullptr, aInstantiation });
            auto* spec = specification_of(copy);
            auto* parameters = new_region(aInstance, instanceRegion);
            parameters->is_subprogram = true;
            parameters->level = aRegion->level + 1;
            subprogram_specification(spec, aInstance, parameters);
            aInstance->sub->level = parameters->level;
            // (an operator instance: its profile (6.6))
            if (aInstance->decl != nullptr && aInstance->decl->is(construct::DefiningOperator) && iInstanceBodies == 0)
                recover([&]() { operator_profile_check(aInstance, aInstance->decl); });
            // (a generic function's result: not of an abstract type (nor an access result designating one) (3.9.3(8/3)))
            if (templateCheck && aInstance->kind == entity_kind::Function && aInstance->sub->result != nullptr)
            {
                auto* result = aInstance->sub->result;
                if (result->anonymous_access && result->designated != nullptr)
                    result = result->designated;
                if (abstract_type(result, parameters))
                    recover([&]() { error(spec, "a generic function cannot have an abstract result type"); });
            }
            if (bodyCopy != nullptr)
            {
                auto* bodySpec = specification_of(bodyCopy);
                aInstance->inner = parameters;
                // (the body's parameters are its own)
                auto* bodyParameters = new_region(aInstance, instanceRegion);
                bodyParameters->is_subprogram = true;
                bodyParameters->level = aRegion->level + 1;
                auto* own = new_entity(aInstance->kind, aInstance->name, bodySpec->child(0), instanceRegion);
                subprogram_specification(bodySpec, own, bodyParameters);
                // (checking the generic itself: its body conforms to its declaration (6.3.1))
                if (templateCheck)
                    recover([&]() { conformance_check(*aInstance->sub, *own->sub, aGeneric->name, bodySpec->child(0)); });
                aInstance->sub->parameters = own->sub->parameters;
                aInstance->inner = bodyParameters;
                bodyParameters->owner = aInstance;
                aInstance->sub->body = bodyCopy;
                aInstance->sub->spec = bodySpec;
                bodyCopy->ent = aInstance;
                auto* saved = iCurrentSubprogram;
                iCurrentSubprogram = aInstance;
                auto* alias = new_entity(aInstance->kind, aGeneric->name, nullptr, formalRegion);
                alias->sub->renames = aInstance;
                alias->sub->parameters = aInstance->sub->parameters;
                alias->sub->result = aInstance->sub->result;
                formalRegion->add(alias);
                // (a generic body declared in a package body: that body's declarations are visible in the instance's body (after the
                // formals), as for a package instance's)
                region* const savedFormalParent = formalRegion->parent;
                for (auto& [declNode, enclosing] : iBodyRegions)
                    if (auto* decls = declNode->first(construct::Declarations); decls != nullptr &&
                        std::find(decls->children.begin(), decls->children.end(), bodyNode) != decls->children.end() && enclosing->parent == savedFormalParent)
                    {
                        formalRegion->parent = enclosing;
                        break;
                    }
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = bodyNode;
                instance_body inBody{ *this, !templateCheck };
                // (a null procedure (6.7))
                if (bodyCopy->is(construct::SubprogramNull))
                    aInstance->sub->is_null = true;
                if (bodyCopy->is(construct::SubprogramExpression))
                {
                    for (auto* c : bodyCopy->children)
                        if (!c->is(construct::Flag) && c != bodySpec && !c->is(construct::Aspects))
                            aInstance->sub->expression = c;
                    resolve(aInstance->sub->expression, aInstance->sub->result, bodyParameters);
                }
                declarations(bodyCopy->first(construct::Declarations), bodyParameters);
                if (auto* handled = bodyCopy->first(construct::Handled))
                    statements(handled, bodyParameters);
                formalRegion->parent = savedFormalParent;
                if (formalRegion->visibility_cutoff != nullptr)
                    formalRegion->visibility_cutoff = declaration;
                iCurrentSubprogram = saved;
                iSubprogramsToGenerate.push_back(aInstance);
            }
            else if (aGeneric->sub != nullptr && aGeneric->sub->op != intrinsic::None)
            {
                // (an instance of a predefined generic subprogram, e.g. Ada.Unchecked_Conversion)
                aInstance->sub->op = aGeneric->sub->op;
                aInstance->sub->import_name = aGeneric->sub->import_name;
            }
            else if (aGeneric->imported || (aGeneric->sub != nullptr && aGeneric->sub->body != nullptr && aGeneric->sub->body->is(construct::Pragma)))
            {
                aInstance->sub->op = aGeneric->sub->op;
                aInstance->sub->import_name = aGeneric->sub->import_name;
            }
            // (a nonreturning generic procedure's instances: nonreturning)
            if (aGeneric->sub != nullptr && aGeneric->sub->no_return)
                aInstance->sub->no_return = true;
            // (predefined generic subprograms: intrinsic)
            auto const genericName = full_name(aGeneric);
            if (genericName == "ada.unchecked_conversion" || genericName == "unchecked_conversion")
                aInstance->sub->op = intrinsic::UncheckedConversion;
            else if (genericName == "ada.unchecked_deallocation" || genericName == "unchecked_deallocation")
                aInstance->sub->op = intrinsic::UncheckedDeallocation;
        }
    }

    // --- subunits

    void program::body_stub(node* aDecl, region* aRegion)
    {
        if (aDecl->is(construct::Subunit))
        {
            // (a subunit: analysed in the region of its stub)
            auto* proper = aDecl->child(1);
            auto stub = iStubs.find(iCurrentUnit != nullptr ? iCurrentUnit->name : std::string{});
            region* r = stub != iStubs.end() ? stub->second : aRegion;
            if (proper == nullptr)
                return;
            // (the stub's region as seen from the subunit: its declarations before the stub (the subunit is as if at the stub, 10.1.3(17)))
            struct subunit_view_guard
            {
                region* r = nullptr;
                node* cutoff = nullptr;
                bool view = false;
                ~subunit_view_guard() { if (r != nullptr) { r->subunit_view = view; r->visibility_cutoff = cutoff; } }
            } guard;
            if (auto s = iStubNodes.find(iCurrentUnit != nullptr ? iCurrentUnit->name : std::string{}); s != iStubNodes.end() && stub != iStubs.end() &&
                s->second != nullptr && s->second->parent != nullptr && !r->generic_formals)
            {
                guard.r = r;
                guard.cutoff = r->visibility_cutoff;
                guard.view = r->subunit_view;
                r->subunit_view = true;
                r->visibility_cutoff = s->second;
            }
            // (a subunit of a generic unit's body: analysed in its instances (see the stub))
            if (stub == iStubs.end() && iCurrentUnit != nullptr)
                for (auto prefix = iCurrentUnit->name; prefix.find('.') != std::string::npos;)
                {
                    prefix = prefix.substr(0u, prefix.rfind('.'));
                    if (auto* u = library_unit(prefix); u != nullptr && u->is_generic())
                        return;
                }
            if (auto s = iStubNodes.find(iCurrentUnit != nullptr ? iCurrentUnit->name : std::string{}); s != iStubNodes.end())
                iStubBodies[s->second] = proper;
            switch (proper->kind)
            {
            case construct::SubprogramBody:
                // (a generic subprogram's body: analysed in its instances (see the stub))
                if (auto* spec = specification_of(proper))
                {
                    auto const name = name_text(spec->child(0)->source);
                    auto const dot = name.rfind('.');
                    bool generic = false;
                    // (the generic: declared in the stub's region, or in the specification of the package whose body that is)
                    for (auto* g : visible(dot == std::string::npos ? name : name.substr(dot + 1u), r))
                        if (g->is_generic() && g->sub != nullptr && g->scope != nullptr && (g->scope == r || (r->owner != nullptr && g->scope->owner == r->owner)))
                        {
                            g->sub->body = proper;
                            proper->ent = g;
                            spec->child(0)->ent = g;
                            generic = true;
                        }
                    if (generic)
                    {
                        if (iDeclaringUnit != nullptr)
                            iDeclaringUnit = nullptr;
                        break;
                    }
                }
                subprogram_body(proper, r);
                break;
            case construct::PackageBody:
                package_body(proper, r);
                break;
            case construct::TaskBody:
                task_body(proper, r);
                break;
            case construct::ProtectedBody:
                protected_body(proper, r);
                break;
            default:
                break;
            }
            return;
        }
        // (a body stub: its subunit is analysed (later) in this region; a subprogram stub declares the subprogram if it has no
        // declaration)
        // (immediately within the declarative part of a body that is a compilation unit (a library unit body or a subunit) (10.1.3(8/2)))
        if (iInstanceBodies == 0 && iInstantiationSites.empty())
        {
            auto* body = aDecl->parent != nullptr && aDecl->parent->is(construct::Declarations) ? aDecl->parent->parent : nullptr;
            bool const outermost = body != nullptr && (body->is(construct::SubprogramBody) || body->is(construct::PackageBody) ||
                body->is(construct::TaskBody) || body->is(construct::ProtectedBody)) && body->parent != nullptr &&
                (body->parent->is(construct::Unit) || body->parent->is(construct::Subunit));
            if (!outermost)
                recover([&]() { error(aDecl, "a body stub must be immediately within the declarative part of a library unit body or a subunit (10.1.3(8/2))"); });
            // (the stubs of a declarative part: distinct names (10.1.3(9)))
            auto const stub_name = [&](node* n) -> std::string
            {
                if (auto* spec = specification_of(n); spec != nullptr && !spec->children.empty())
                    return name_text(spec->child(0)->source);
                if (auto* d = n->first(construct::Defining))
                    return name_text(d->source);
                return {};
            };
            if (aDecl->parent != nullptr)
                for (auto* other : aDecl->parent->children)
                {
                    if (other == aDecl)
                        break;
                    if (other->is(construct::Stub) && !stub_name(aDecl).empty() && stub_name(other) == stub_name(aDecl))
                    {
                        recover([&]() { error(aDecl, "the body stubs of a declarative part must have distinct names (10.1.3(9))"); });
                        break;
                    }
                }
        }
        std::string name;
        if (auto* spec = specification_of(aDecl))
        {
            // (a subunit's designator: an identifier, not an operator symbol (10.1.3(3): a subunit's name is a defining program unit name))
            if (spec->child(0)->is(construct::DefiningOperator) && iInstanceBodies == 0)
                recover([&]() { error(aDecl, "a subprogram body stub cannot have an operator symbol as its designator (its subunit's name is an identifier, 10.1.3)"); });
            name = name_text(spec->child(0)->source);
            // (a generic subprogram's stub: its specification names the generic's formals (it completes the generic))
            bool generic = false;
            for (auto [i, end] = aRegion->names.equal_range(name); i != end; ++i)
                generic = generic || i->second->is_generic();
            // (declared in the specification of the package whose body this is)
            for (auto* e : visible(name, aRegion))
                generic = generic || (e->is_generic() && e->scope != nullptr && aRegion->owner != nullptr && e->scope->owner == aRegion->owner);
            if (generic)
                goto named;
            auto* own = new_entity(spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure, name, spec->child(0), aRegion);
            auto* parameters = new_region(own, aRegion);
            parameters->level = aRegion->level + 1;
            subprogram_specification(spec, own, parameters);
            if (auto* declared = find_completion(own, aRegion); declared == nullptr)
            {
                aRegion->add(own);
                bool const overridesImplicit = implicit_homograph(own, aRegion);
                add_primitive_if(own, aRegion);
                implicit_inequality(own, aRegion);
                if (iInstanceBodies == 0)
                    recover([&]() { overriding_indicator_check(own, aDecl, overridesImplicit, aRegion); });
            }
            else
            {
                if (iInstanceBodies == 0 && declared->sub != nullptr && (aDecl->has_flag("overriding") || aDecl->has_flag("not overriding")))
                    recover([&]() { overriding_indicator_check(declared, aDecl, implicit_homograph(declared, aRegion), aRegion); });
                completion_aspects_check(aDecl);
            }
        }
        else if (auto* d = aDecl->first(construct::Defining))
        {
            name = name_text(d->source);
            // (a package, task or protected body stub: of a declaration of the unit given earlier (10.1.3(10/2)))
            if (iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                std::string text{ aDecl->source.substr(0u, std::min<std::size_t>(aDecl->source.size(), 16u)) };
                for (auto& ch : text)
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                bool const package = text.starts_with("package");
                bool const task = text.starts_with("task");
                bool declared = false;
                for (auto* e : visible(name, aRegion))
                    declared = declared || (package && e->is_package() && e->kind != entity_kind::GenericPackage ? true : package && e->kind == entity_kind::GenericPackage) ||
                        (!package && (e->kind == entity_kind::Task || e->kind == entity_kind::Protected || ((e->is_type() || e->is_object()) && e->type != nullptr &&
                            (e->type->view()->kind == (task ? type_kind::Task : type_kind::Protected)))));
                if (!declared)
                    recover([&]() { error(aDecl, "a body stub must complete a declaration of the " + std::string{ package ? "package" : task ? "task" : "protected unit" } +
                        " given earlier (10.1.3(10/2))"); });
            }
        }
    named:
        // (the subunit's full name: the enclosing units' names and this)
        std::string parent;
        entity* instance = nullptr;
        for (auto* r = aRegion; r != nullptr; r = r->parent)
            if (r->owner != nullptr && !r->owner->name.empty() && r->owner->name[0] != '%' && (r->owner->is_package() || r->owner->is_subprogram() ||
                r->owner->kind == entity_kind::Task || r->owner->kind == entity_kind::Protected))
            {
                parent = full_name(r->owner);
                if (r->owner->generic != nullptr)
                {
                    instance = r->owner;
                    parent = full_name(r->owner->generic);
                }
                break;
            }
        // (a stub in an instance's body: its subunit's proper body analysed here, in this instance (the generic's subunit))
        if (instance != nullptr)
        {
            for (auto const& u : iUnits)
                if (u.subunit && u.name == parent + "." + name && u.item != nullptr && u.item->is(construct::Subunit) && u.item->child(1) != nullptr)
                {
                    auto* proper = clone(u.item->child(1));
                    iStubBodies[aDecl] = proper;
                    switch (proper->kind)
                    {
                    case construct::SubprogramBody:
                        subprogram_body(proper, aRegion);
                        break;
                    case construct::PackageBody:
                        package_body(proper, aRegion);
                        break;
                    case construct::TaskBody:
                        task_body(proper, aRegion);
                        break;
                    case construct::ProtectedBody:
                        protected_body(proper, aRegion);
                        break;
                    default:
                        break;
                    }
                    return;
                }
        }
        iStubs[parent + "." + name] = aRegion;
        iStubNodes[parent + "." + name] = aDecl;
        // (a generic subprogram's stub: its subunit's body is the generic's (instantiations before the subunit is analysed use it))
        if (auto* spec = specification_of(aDecl))
            for (auto* g : visible(name, aRegion))
                if (g->is_generic() && g->sub != nullptr && g->scope != nullptr && (g->scope == aRegion || g->scope->owner == aRegion->owner))
                    for (auto const& u : iUnits)
                        if (u.subunit && u.name == parent + "." + name && u.item != nullptr)
                        {
                            auto* proper = u.item->is(construct::Subunit) ? u.item->child(1) : u.item;
                            if (proper != nullptr && proper->is(construct::SubprogramBody))
                                g->sub->body = proper;
                        }
        (void)0;
    }
}
