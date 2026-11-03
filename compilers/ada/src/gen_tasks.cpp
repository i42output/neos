/*
  gen_tasks.cpp

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

// Ada: tasks and protected objects (9): see runtime/ada/src/tasking.cpp. A task object holds its tag (a type with interfaces',
// at offset 0), its task's id (at offset 8) and its discriminants (from offset 16); its body is a procedure (static link, the
// task object). A protected object holds its tag (likewise), the runtime's handle (offset 8) and its components (from offset
// 16); its operations have the object as a parameter (after the 
// static link), its entries' bodies and barriers are functions that the runtime calls. Entry calls pass a block of the actuals'
// addresses; an accept statement's (or an entry body's) parameters are the addresses in the block. An entry of a task is
// identified by its index in the task unit (shifted left 32 bits) plus a family member's offset.

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::ada
{
    // --- the model

    ada_type* program::self_type(entity* aSubprogram)
    {
        if (aSubprogram == nullptr)
            return nullptr;
        if (aSubprogram->owner != nullptr && aSubprogram->owner->kind == entity_kind::Task && aSubprogram->kind == entity_kind::Procedure &&
            aSubprogram->owner->completion == aSubprogram)
            return aSubprogram->owner->type;
        if (aSubprogram->sub != nullptr && aSubprogram->sub->barrier_of != nullptr)
            return self_type(aSubprogram->sub->barrier_of);
        if (aSubprogram->scope != nullptr && aSubprogram->scope->owner != nullptr && aSubprogram->scope->owner->kind == entity_kind::Protected &&
            aSubprogram->is_subprogram())
            return aSubprogram->scope->owner->type;
        return nullptr;
    }

    program::subprogram_kind program::subprogram_form(entity* aSubprogram)
    {
        if (aSubprogram->sub != nullptr && aSubprogram->sub->barrier_of != nullptr)
            return subprogram_kind::Barrier;
        if (aSubprogram->kind == entity_kind::Entry && self_type(aSubprogram) != nullptr)
            return subprogram_kind::ProtectedEntry;
        return subprogram_kind::Normal;
    }

    std::uint64_t program::protected_table(ada_type* aType)
    {
        // (by entry (in declaration order): its barrier's function, its body's function)
        auto* t = aType->view()->base_type()->view();
        auto existing = iProtectedTables.find(t);
        if (existing != iProtectedTables.end())
            return existing->second;
        std::vector<entity*> entries;
        if (t->unit != nullptr && t->unit->inner != nullptr)
            for (auto* d : t->unit->inner->declarations)
                if (d->kind == entity_kind::Entry)
                    entries.push_back(d);
        auto const address = static_data(16u * std::max<std::size_t>(entries.size(), 1u), 8u);
        for (std::size_t i = 0u; i < entries.size(); ++i)
        {
            auto* e = entries[i];
            if (e->sub->barrier_function != nullptr)
            {
                define_subprogram(e->sub->barrier_function);
                iFunctionSlots.emplace_back(address + 16u * i, e->sub->barrier_function->sub->function_index);
            }
            if (e->sub->body != nullptr)
            {
                define_subprogram(e);
                iFunctionSlots.emplace_back(address + 16u * i + 8u, e->sub->function_index);
            }
        }
        iProtectedTables[t] = address;
        return address;
    }

    // --- objects

    bool function_generator::is_self_member(entity* aObject)
    {
        if (aObject->kind != entity_kind::Component && aObject->kind != entity_kind::Discriminant)
            return false;
        auto* owner = aObject->owner != nullptr ? aObject->owner : aObject->scope != nullptr ? aObject->scope->owner : nullptr;
        if (owner == nullptr && aObject->scope != nullptr)
            owner = aObject->scope->owner;
        return owner != nullptr && (owner->kind == entity_kind::Task || owner->kind == entity_kind::Protected);
    }

    void function_generator::self_address(entity* aMember)
    {
        // (the object: this function's (its parameter), or that of the task body or protected operation that encloses it (its frame))
        auto* owner = aMember->owner != nullptr ? aMember->owner : aMember->scope->owner;
        auto* unit = owner;
        int const selfLevel = unit->inner != nullptr ? unit->inner->level + 1 : iLevel;
        if (iHasSelf && selfLevel == iLevel)
        {
            get(iSelf);
            return;
        }
        if (selfLevel < iLevel && selfLevel >= 1)
        {
            frame_of_level(selfLevel);
            load64(8u);
            return;
        }
        if (iHasSelf)
        {
            get(iSelf);
            return;
        }
        iProgram.error(iWhere, "internal error: \"" + aMember->name + "\" is not visible here");
    }

    void function_generator::task_object_init(ada_type* aType, std::uint32_t aAddress)
    {
        // (a task: created (activated after the declarative part that creates it); its master is the creator's)
        auto* t = aType->view()->base_type()->view();
        auto* unit = t->unit;
        auto b = unit != nullptr ? iProgram.iTaskBodies.find(unit) : iProgram.iTaskBodies.end();
        if (b == iProgram.iTaskBodies.end())
            iProgram.error(iWhere, "task \"" + (unit != nullptr ? unit->name : std::string{}) + "\" has no body");
        auto* body = b->second;
        get(aAddress);
        if (!body->sub->defined)
            iProgram.define_subprogram(body);
        function_index(body->sub->function_index);
        op(opcode::I64UConvertI32);
        static_link_for(body);
        get(aAddress);
        if (iCollection)
            get(*iCollection);
        else
            i64(0);
        runtime("task_create", { type::I64, type::I64, type::I64, type::I64 }, type::I64);
        store64(program::TaskHandle);
        check();
        // (its base priority, if specified (D.1(17)): its expression evaluated now (it may name the object's discriminants))
        if (unit->priority != nullptr || unit->interrupt_priority)
        {
            get(aAddress);
            load64(program::TaskHandle);
            unit_priority(unit, t, aAddress);
            runtime("task_priority", { type::I64, type::I64 }, type::Void);
            check();
        }
        // (a nested task type's: its body's elaboration flag (its activation before then raises Program_Error))
        if (unit->scope != nullptr && unit->scope->level > 0)
        {
            get(aAddress);
            load64(program::TaskHandle);
            i64(static_cast<std::int64_t>(iProgram.elaboration_flag(body)));
            runtime("task_elaboration", { type::I64, type::I64 }, type::Void);
        }
        ++iTasksCreated;
    }

    void function_generator::unit_parts(ada_type* aType, std::uint32_t aAddress, type_kind aKind, std::function<void(std::uint32_t)> const& aAction)
    {
        std::function<bool(ada_type*)> const contains = [&](ada_type* aPart) -> bool
        {
            if (aPart == nullptr)
                return false;
            auto* b = aPart->view()->base_type()->view();
            if (b->kind == aKind)
                return true;
            if (aKind == type_kind::Task && !iProgram.needs_tasks(b))
                return false;
            if (b->kind == type_kind::Array)
                return b->component != nullptr && b->component->view()->base_type()->view() != b && contains(b->component);
            if (b->kind == type_kind::Record)
                for (auto const& c : b->components)
                    if (c.type != nullptr && c.type->view()->base_type()->view() != b && contains(c.type))
                        return true;
            return false;
        };
        auto* t = aType->view();
        if (!contains(t))
            return;
        if (t->base_type()->view()->kind == aKind)
        {
            aAction(aAddress);
            return;
        }
        if (t->kind == type_kind::Array)
        {
            auto const n = local(type::I64);
            auto const p = local(type::I64);
            array_length(aAddress, t->base_type()->view()->indexes.size());
            set(n);
            data_address(aAddress);
            set(p);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            get(n);
            i64(0);
            op(opcode::I64LeS);
            branch(exit, true);
            unit_parts(t->component, p, aKind, aAction);
            get(p);
            element_size(t);
            op(opcode::I64Add);
            set(p);
            get(n);
            i64(1);
            op(opcode::I64Sub);
            set(n);
            branch(head);
            end();
            end();
            return;
        }
        if (t->kind != type_kind::Record)
            return;
        auto* base = t->base_type()->view();
        iProgram.layout(base);
        for (auto const& c : base->components)
            if (c.offset && c.type != nullptr && contains(c.type))
            {
                if (c.variant >= 0)
                {
                    variant_selected(base, c, aAddress);
                    begin_if();
                }
                auto const a = local(type::I64);
                component_address(base, c, aAddress);
                set(a);
                unit_parts(c.type, a, aKind, aAction);
                if (c.variant >= 0)
                    end();
            }
    }

    void function_generator::relocate_tasks(ada_type* aType, std::uint32_t aAddress, bool aUnregister)
    {
        // (each task: its object now here (or no longer its master's dependent))
        unit_parts(aType, aAddress, type_kind::Task, [&](std::uint32_t aTask)
            {
                get(aTask);
                load64(program::TaskHandle);
                if (aUnregister)
                    runtime("task_unregister", { type::I64 }, type::Void);
                else
                {
                    get(aTask);
                    runtime("task_relocate", { type::I64, type::I64 }, type::Void);
                }
            });
    }

    void function_generator::protected_object_init(ada_type* aType, std::uint32_t aAddress)
    {
        auto* t = aType->view()->base_type()->view();
        std::int64_t count = 0;
        entity* someEntry = nullptr;
        if (t->unit != nullptr && t->unit->inner != nullptr)
            for (auto* d : t->unit->inner->declarations)
                if (d->kind == entity_kind::Entry)
                {
                    ++count;
                    someEntry = d;
                }
        get(aAddress);
        i64(static_cast<std::int64_t>(iProgram.protected_table(t)));
        i64(count);
        // (the static link of its entries' bodies and barriers)
        if (someEntry != nullptr)
            static_link_for(someEntry);
        else
            i64(0);
        i64(iAllocating ? 0 : 1);
        runtime("po_create", { type::I64, type::I64, type::I64, type::I64, type::I64 }, type::I64);
        // (its ceiling priority, if specified (D.3(6)): its expression evaluated now (it may name the object's discriminants))
        if (t->unit != nullptr && (t->unit->priority != nullptr || t->unit->interrupt_priority))
        {
            unit_priority(t->unit, t, aAddress);
            runtime("po_ceiling", { type::I64, type::I64 }, type::Void);
            check();
        }
        else
            drop();
        // (its procedures attached to interrupts (C.3.1(7/3)): each interrupt's expression evaluated now (it may name the object's
        // discriminants); a handler: a descriptor (procedure, static link, object), as an access value's)
        if (auto handlers = iProgram.iAttachHandlers.find(t->unit); t->unit != nullptr && handlers != iProgram.iAttachHandlers.end())
            for (auto const& [procedure, interrupt] : handlers->second)
            {
                iProgram.define_subprogram(procedure);
                iInstances.emplace_back(t, aAddress);
                expression(interrupt, interrupt->type != nullptr ? interrupt->type : iProgram.integer());
                iInstances.pop_back();
                auto const d = temporary(type::I64);
                i64(24);
                runtime("allocate", { type::I64 }, type::I64);
                set(d);
                get(d);
                function_index(procedure->sub->function_index);
                op(opcode::I64UConvertI32);
                store64(0u);
                get(d);
                static_link_for(procedure);
                store64(8u);
                get(d);
                get(aAddress);
                store64(16u);
                get(d);
                release(d);
                runtime("interrupt_attach_static", { type::I64, type::I64 }, type::Void);
                check();
            }
    }

    void function_generator::unit_priority(entity* aUnit, ada_type* aType, std::uint32_t aAddress)
    {
        // (a task or protected unit's Priority or Interrupt_Priority (Interrupt_Priority'Last if it has none), of an object (its
        // discriminants))
        if (aUnit->priority == nullptr)
        {
            i64(31);
            return;
        }
        iInstances.emplace_back(aType, aAddress);
        expression(aUnit->priority, iProgram.integer());
        iInstances.pop_back();
    }

    void function_generator::allocator(node* aAllocator, ada_type* aType)
    {
        // (an allocator: the tasks that it creates are activated now; their master is the access type's (the library level's
        // for a library level access type, else (approximately) the allocator's))
        auto const created = iTasksCreated;
        auto* accessType = aType != nullptr ? aType : aAllocator->type;
        // (the tasks it creates: only those activated (not others of the enclosing declarative part, created but not activated yet))
        std::optional<std::uint32_t> pendingMark;
        if (accessType != nullptr && iProgram.needs_tasks(iProgram.designated_of(accessType)))
        {
            pendingMark = local(type::I64);
            runtime("task_pending", {}, type::I64);
            set(*pendingMark);
            // (an exception raised by its initialization: its tasks are never activated (they become terminated, 9.2(3/2), 4.8(10.3/2)))
            begin(control_kind::Handler);
        }
        auto const saved = iCollection;
        iCollection.reset();
        // (its access type's storage pool, if specified: its Allocate (13.11(16)))
        auto const savedPool = iPool;
        iPool.reset();
        auto const savedAlignment = iPoolAlignment;
        auto const savedLimit = iStorageLimit;
        iStorageLimit.reset();
        if (accessType != nullptr && storage_pool_of(accessType) == nullptr)
            for (auto* at = accessType->view()->base_type()->view(); at != nullptr && at->kind == type_kind::Access;
                at = at->parent != nullptr && at->parent->view()->base_type()->view() != at ? at->parent->view()->base_type()->view() : nullptr)
                if (auto* size = iProgram.type_aspect(at, "storage_size"))
                {
                    auto& cell = iProgram.iStorageTotals[at];
                    if (cell == 0u)
                        cell = iProgram.static_data(8u, 8u);
                    iStorageLimit = std::make_pair(cell, size);
                    break;
                }
        auto const savedSubpool = iSubpool;
        iSubpool.reset();
        if (auto* pool = accessType != nullptr ? storage_pool_of(accessType) : nullptr)
        {
            address(pool);
            iPool = local(type::I64);
            set(*iPool);
            iPoolAlignment = storage_alignment(iProgram.designated_of(accessType));
        }
        std::optional<std::uint32_t> subpool;
        if (auto* spec = aAllocator->first(construct::Subpool))
        {
            // (a subpool's handle: not null (else Constraint_Error); of the access type's pool (its Pool component, after its tag;
            // else Program_Error) (4.8(10.3/3)))
            subpool = local(type::I64);
            expression(spec->child(0), spec->child(0)->type);
            set(*subpool);
            get(*subpool);
            op(opcode::I64Eqz);
            begin_if();
            raise_check(0, aAllocator, "null subpool handle");
            end();
            get(*subpool);
            load64(8u);
            if (iPool)
                get(*iPool);
            else
                i64(0);
            op(opcode::I64Ne);
            begin_if();
            raise_check(1, aAllocator, "subpool of another pool");
            end();
            iSubpool = subpool;
        }
        if (accessType != nullptr)
            if (auto c = iProgram.iCollections.find(accessType->view()->base_type()->view()); c != iProgram.iCollections.end())
            {
                iCollection = local(type::I64);
                load_object(c->second);
                set(*iCollection);
            }
        bool const allocating = iAllocating;
        iAllocating = true;
        allocator_object(aAllocator, aType);
        iAllocating = allocating;
        iCollection = saved;
        iPool = savedPool;
        iSubpool = savedSubpool;
        iPoolAlignment = savedAlignment;
        iStorageLimit = savedLimit;
        // (its object of the designated subtype (4.8(10/2)), wherever the allocator is (an actual, an operand...))
        if (accessType != nullptr && accessType->view()->kind == type_kind::Access)
            range_check(accessType, aAllocator->type, aAllocator);
        // (an array of a constrained designated subtype: its bounds (it slides, 4.8(7/2)))
        if (auto* d = accessType != nullptr && accessType->view()->kind == type_kind::Access ? iProgram.designated_of(accessType) : nullptr;
            d != nullptr && d->view()->kind == type_kind::Array && d->view()->constrained)
        {
            auto* dv = d->view();
            auto const object = local(type::I64);
            tee(object);
            drop();
            for (std::size_t k = 0u; k < dv->indexes.size(); ++k)
            {
                get(object);
                index_bound(dv->indexes[k], true);
                store64(static_cast<std::uint32_t>(8u + 16u * k));
                get(object);
                index_bound(dv->indexes[k], false);
                store64(static_cast<std::uint32_t>(16u + 16u * k));
            }
            get(object);
        }
        if (accessType != nullptr)
            if (auto c = iProgram.iObjectCollections.find(accessType->view()->base_type()->view()); c != iProgram.iObjectCollections.end())
            {
                // (its controlled parts: in the access type's collection)
                auto const object = local(type::I64);
                tee(object);
                auto* d = iProgram.designated_of(accessType);
                controlled_parts(d, object, false, [&](ada_type* aPartType, std::uint32_t aPart)
                    {
                        if (auto* finalize = controlled_operation(aPartType, "finalize"))
                        {
                            load_object(c->second);
                            get(aPart);
                            if (!finalize->sub->defined)
                                iProgram.define_subprogram(finalize);
                            function_index(finalize->sub->function_index);
                            op(opcode::I64UConvertI32);
                            static_link_for(finalize);
                            // (allocated from a subpool: finalized when it is deallocated, if before the collection is (13.11.5(5/3)))
                            if (subpool)
                            {
                                get(*subpool);
                                runtime("fin_collection_add_subpool", { type::I64, type::I64, type::I64, type::I64, type::I64 }, type::Void);
                            }
                            else
                                runtime("fin_collection_add", { type::I64, type::I64, type::I64, type::I64 }, type::Void);
                        }
                    });
                drop();
                get(object);
            }
        if (iTasksCreated != created)
        {
            iTasksCreated = created;
            if (pendingMark)
            {
                get(*pendingMark);
                runtime("task_activate_from", { type::I64 }, type::Void);
            }
            else
                runtime("task_activate", {}, type::Void);
            check();
        }
        if (pendingMark)
        {
            auto const result = local(type::I64);
            set(result);
            end();
            get(*pendingMark);
            runtime("task_discard_from", { type::I64 }, type::Void);
            check();
            get(result);
        }
    }

    bool program::needs_tasks(ada_type* aType)
    {
        if (aType == nullptr || iTaskUnits.empty())
            return false;
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::Task)
            return true;
        if (t->kind == type_kind::Array)
            return t->component != nullptr && t->component != aType && needs_tasks(t->component);
        if (t->kind == type_kind::Record)
            for (auto const& c : t->components)
                if (c.type != nullptr && c.type->view()->base_type()->view() != t && needs_tasks(c.type))
                    return true;
        return false;
    }

    void function_generator::access_collection(node* aDecl)
    {
        // (an access type whose allocators can create tasks: its collection, awaited by the master that declares it)
        auto* e = aDecl->ent;
        if (e == nullptr || !e->is_type() || e->type == nullptr)
            return;
        auto* t = e->type->view()->base_type()->view();
        if (t->kind != type_kind::Access)
            return;
        if (iProgram.needs_finalization(iProgram.designated_of(t)))
        {
            // (its objects that need finalization: finalized when the master that declares it is left (7.6.1(11/3)))
            auto* c = iProgram.new_entity(entity_kind::Variable, "%objects", nullptr, e->scope);
            c->type = iProgram.integer();
            c->captured = true;
            iProgram.iObjectCollections[t] = c;
            assign_storage(c);
            runtime("fin_collection_create", {}, type::I64);
            store_object(c);
        }
        if (!iProgram.needs_tasks(iProgram.designated_of(t)))
            return;
        auto* c = iProgram.new_entity(entity_kind::Variable, "%collection", nullptr, e->scope);
        c->type = iProgram.integer();
        c->captured = true;
        iProgram.iCollections[t] = c;
        assign_storage(c);
        runtime("collection_create", {}, type::I64);
        store_object(c);
    }

    // --- entries

    std::int64_t function_generator::entry_key(entity* aEntry)
    {
        // (its index among its unit's entries)
        std::int64_t index = 0;
        auto* r = aEntry->scope;
        if (r != nullptr)
            for (auto* d : r->declarations)
            {
                if (d == aEntry)
                    break;
                if (d->kind == entity_kind::Entry)
                    ++index;
            }
        return index;
    }

    void function_generator::entry_family_offset(entity* aEntry, node* aIndex)
    {
        if (aIndex == nullptr || aEntry->sub->family_type == nullptr)
        {
            i64(0);
            return;
        }
        expression(aIndex, aEntry->sub->family_type->base_type());
        range_check(aEntry->sub->family_type, aIndex->type, aIndex);
        scalar_low(aEntry->sub->family_type);
        op(opcode::I64Sub);
    }

    std::uint32_t function_generator::abortable_part(std::uint32_t aDepth, node* aStatements)
    {
        // (an asynchronous select's abortable part (unless its triggering statement has completed already): an exception
        // (its abort signal, or another) ends it; then the triggering statement ended (1: it completed))
        auto const triggered = local(type::I64);
        get(aDepth);
        runtime("atc_triggered", { type::I64 }, type::I64);
        op(opcode::I64Eqz);
        begin_if();
        begin(control_kind::Handler);
        statements(aStatements);
        end();
        end();
        get(aDepth);
        runtime("atc_end", { type::I64 }, type::I64);
        set(triggered);
        check();
        return triggered;
    }

    void function_generator::entry_call(node* aCall, call_info const& aInfo, entity* aEntry, std::optional<std::pair<std::uint32_t, std::uint32_t>> aTiming,
        std::function<std::uint32_t(std::uint32_t)> const* aAsynchronous, std::function<void()> const* aEvaluateTiming)
    {
        // (the entry's name first: the task (or protected object) and the family index, then the actuals (6.4(10)), then a timed
        // call's delay)
        auto* owner = aEntry->scope != nullptr ? aEntry->scope->owner : nullptr;
        bool const isProtected = owner != nullptr && owner->kind == entity_kind::Protected;
        auto const target = local(type::I64);
        auto const key = local(type::I64);
        auto const familyOffset = local(type::I64);
        // (the task's id, or the protected object)
        if (aInfo.object != nullptr)
        {
            if (aInfo.object->type != nullptr && aInfo.object->type->view()->is_access())
            {
                expression(aInfo.object, aInfo.object->type);
                null_check(aCall);
            }
            else
                address(aInfo.object);
        }
        else if (isProtected)
            self_address(aEntry);
        else
        {
            // (a call of one of the running task's own entries)
            iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
            auto const cell = iFrameSize;
            iFrameSize += 8u + program::TaskHandle;
            get(iFrame);
            runtime("current_task", {}, type::I64);
            store64(static_cast<std::uint32_t>(cell + program::TaskHandle));
            get(iFrame);
            i64(static_cast<std::int64_t>(cell));
            op(opcode::I64Add);
        }
        if (isProtected)
        {
            set(target);
            i64(entry_key(aEntry));
            set(key);
            entry_family_offset(aEntry, aInfo.family_index);
            set(familyOffset);
        }
        else
        {
            load64(program::TaskHandle);
            set(target);
            i64(entry_key(aEntry) << 32);
            entry_family_offset(aEntry, aInfo.family_index);
            op(opcode::I64Add);
            set(key);
        }
        // (the actuals' addresses (a scalar's: a copy's, copied back after an out or in out one) in a block in the frame)
        auto const& formals = aEntry->sub->parameters;
        iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
        auto const block = iFrameSize;
        iFrameSize += 8u * std::max<std::size_t>(formals.size(), 1u);
        struct copy_back { node* actual; ada_type* type; std::uint64_t offset; std::uint32_t address; };
        std::vector<copy_back> copies;
        for (std::size_t i = 0u; i < formals.size(); ++i)
        {
            auto const& f = formals[i];
            node* actual = i < aInfo.actuals.size() ? aInfo.actuals[i] : nullptr;
            if (actual == nullptr)
                actual = f.default_expression;
            auto* ft = f.type != nullptr ? f.type->view() : nullptr;
            get(iFrame);
            if (ft != nullptr && !ft->is_composite())
            {
                iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                auto const cell = iFrameSize;
                iFrameSize += 8u;
                if (f.mode != parameter_mode::Out && actual != nullptr)
                {
                    get(iFrame);
                    expression(actual, f.type);
                    range_check(f.type, actual->type, actual);
                    store(kind_of(f.type), iProgram.scalar_size(f.type), static_cast<std::uint32_t>(cell));
                }
                if (f.mode != parameter_mode::In && actual != nullptr)
                {
                    // (the actual's address, unless it is a variable's or a conversion's: evaluated now, not upon return)
                    std::uint32_t actualAddress = 0u;
                    auto* a = actual;
                    while (a->is(construct::Parenthesized))
                        a = a->child(0);
                    if (!a->is(construct::Identifier) && meaning_of(a) != meaning::Conversion)
                    {
                        address(a);
                        actualAddress = local(type::I64);
                        set(actualAddress);
                    }
                    copies.push_back(copy_back{ actual, f.type, cell, actualAddress });
                }
                get(iFrame);
                i64(static_cast<std::int64_t>(cell));
                op(opcode::I64Add);
            }
            else if (actual != nullptr && is_aggregate(actual))
            {
                aggregate(actual, f.type);
                composite_actual(f.type, actual);
            }
            else if (actual != nullptr)
            {
                if (meaning_of(actual) == meaning::Object || meaning_of(actual) == meaning::Component || meaning_of(actual) == meaning::Index ||
                    meaning_of(actual) == meaning::Dereference || meaning_of(actual) == meaning::Slice)
                    address(actual);
                else
                    expression(actual, f.type);
                if (ft != nullptr)
                    composite_actual(f.type, actual);
                if (f.mode != parameter_mode::In && mutable_record(f.type))
                {
                    // (whether the actual is constrained (3.7.2(4)))
                    unconstrained_view(actual);
                    i64(UnconstrainedBit);
                    op(opcode::I64Shl);
                    op(opcode::I64Ior);
                }
            }
            else
                i64(0);
            store64(static_cast<std::uint32_t>(block + 8u * i));
        }
        get(target);
        get(key);
        if (isProtected)
            get(familyOffset);
        get(iFrame);
        i64(static_cast<std::int64_t>(block));
        op(opcode::I64Add);
        if (aAsynchronous != nullptr)
        {
            // (an asynchronous select's triggering call: queued, then its abortable part, then its out parameters copied back if
            // it completed)
            if (isProtected)
                runtime("atc_po_call", { type::I64, type::I64, type::I64, type::I64 }, type::I64);
            else
                runtime("atc_entry_call", { type::I64, type::I64, type::I64 }, type::I64);
            check();
            auto const depth = local(type::I64);
            set(depth);
            auto const triggered = (*aAsynchronous)(depth);
            get(triggered);
            op(opcode::I64Eqz);
            op(opcode::I32Eqz);
            begin_if();
            for (auto const& c : copies)
            {
                if (c.address != 0u)
                    get(c.address);
                get(iFrame);
                load(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(c.offset), !is_unsigned(c.type));
                copy_back_check(c.actual->type, c.type, c.actual);
                if (c.address != 0u)
                    store(kind_of(c.actual->type), iProgram.scalar_size(c.actual->type));
                else
                    assign_value(c.actual);
            }
            end();
            return;
        }
        if (aEvaluateTiming != nullptr)
        {
            // (the delay: after the actuals (9.7.2(5)); the stack's values kept meanwhile)
            auto const blockAddress = local(type::I64);
            set(blockAddress);
            auto const familyValue = local(type::I64);
            if (isProtected)
                set(familyValue);
            auto const keyValue = local(type::I64);
            set(keyValue);
            auto const targetValue = local(type::I64);
            set(targetValue);
            (*aEvaluateTiming)();
            get(targetValue);
            get(keyValue);
            if (isProtected)
                get(familyValue);
            get(blockAddress);
        }
        if (aTiming)
        {
            get(aTiming->first);
            get(aTiming->second);
        }
        else
        {
            i64(0);
            i64(0);
        }
        if (isProtected)
            runtime("po_call", { type::I64, type::I64, type::I64, type::I64, type::I64, type::I64 }, type::I64);
        else
            runtime("entry_call", { type::I64, type::I64, type::I64, type::I64, type::I64 }, type::I64);
        check();
        if (!aTiming)
            drop();
        for (auto const& c : copies)
        {
            if (c.address != 0u)
                get(c.address);
            get(iFrame);
            load(kind_of(c.type), iProgram.scalar_size(c.type), static_cast<std::uint32_t>(c.offset), !is_unsigned(c.type));
            copy_back_check(c.actual->type, c.type, c.actual);
            if (c.address != 0u)
                store(kind_of(c.actual->type), iProgram.scalar_size(c.actual->type));
            else
                assign_value(c.actual);
        }
    }

    bool function_generator::tasking_call(node* aCall, call_info const& aInfo, entity* aCallee, bool aStatement)
    {
        (void)aStatement;
        if (aCallee->kind == entity_kind::Entry)
        {
            entry_call(aCall, aInfo, aCallee, {});
            return true;
        }
        return false;
    }

    void function_generator::accept_body(node* aAccept, std::uint32_t aCall)
    {
        // (the accept's parameters: the addresses in the call's block; its statements; the call completed (an exception that they
        // propagate: the caller's too, and the acceptor's))
        auto* r = iProgram.statement_region(aAccept);
        std::size_t index = 0u;
        auto const block = local(type::I64);
        get(aCall);
        runtime("call_params", { type::I64 }, type::I64);
        set(block);
        if (r != nullptr)
            for (auto* c : aAccept->children)
                if (c->is(construct::Parameter))
                    for (auto* d : c->all(construct::Defining))
                    {
                        if (auto* p = d->ent)
                        {
                            // (its address: in a local, or in the frame if a nested subprogram refers to it)
                            p->function_level = static_cast<std::uint32_t>(iLevel);
                            if (p->captured)
                            {
                                iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                                p->where = storage::IndirectFrame;
                                p->offset = static_cast<std::int64_t>(iFrameSize);
                                iFrameSize += 8u;
                            }
                            else
                            {
                                p->where = storage::Indirect;
                                p->local = local(type::I64);
                            }
                            get(block);
                            load64(static_cast<std::uint32_t>(8u * index));
                            if (auto* entry = aAccept->ent; entry != nullptr && entry->sub != nullptr && index < entry->sub->parameters.size() &&
                                entry->sub->parameters[index].mode != parameter_mode::In && discriminated_record(entry->sub->parameters[index].type))
                            {
                                // (whether the actual is unconstrained: its address's bit UnconstrainedBit)
                                auto const address = local(type::I64);
                                set(address);
                                unconstrained_flag(p, address);
                                get(address);
                            }
                            set_indirect(p);
                        }
                        ++index;
                    }
        if (auto* handledPart = aAccept->first(construct::Handled))
        {
            begin(control_kind::Handler);
            iAcceptControls.push_back(iControls.size() - 1u);
            handled(handledPart);
            iAcceptControls.pop_back();
            end();
        }
        get(aCall);
        op(opcode::GlobalGet);
        u32(iProgram.exc_global());
        i64(0);
        op(opcode::I64Ne);
        op(opcode::I64UConvertI32);
        runtime("accept_done", { type::I64, type::I64 }, type::Void);
        check();
    }

    void function_generator::delay_value(node* aDelay, std::uint32_t aKind, std::uint32_t aValue)
    {
        // (a delay statement's kind (1: relative; 2: until a Calendar time; 3: until a Real_Time time) and value (nanoseconds))
        node* expressionNode = nullptr;
        for (auto* c : aDelay->children)
            if (!c->is(construct::Flag))
                expressionNode = c;
        bool const until = aDelay->has_flag("until");
        std::int64_t kind = 1;
        if (until)
        {
            kind = 3;
            auto* t = expressionNode->type != nullptr ? expressionNode->type->view()->base_type() : nullptr;
            if (t != nullptr && t->partial != nullptr)
                t = t->partial;
            entity* d = t != nullptr ? t->decl : nullptr;
            if (d == nullptr && expressionNode->type != nullptr)
                d = expressionNode->type->decl;
            if (d != nullptr && iProgram.full_name(d).find("calendar") != std::string::npos)
                kind = 2;
        }
        i64(kind);
        set(aKind);
        expression(expressionNode, expressionNode->type);
        if (kind_of(expressionNode->type) != vkind::I64)
        {
            // (a real delay (e.g. a universal real): seconds)
            if (kind_of(expressionNode->type) == vkind::F32)
                op(opcode::F64ConvertF32);
            f64(1.0e9);
            op(opcode::F64Mul);
            round_to_integer();
        }
        set(aValue);
    }

    // --- statements

    bool function_generator::tasking_statement(node* aStatement)
    {
        switch (aStatement->kind)
        {
        case construct::Delay:
            {
                auto const kind = local(type::I64);
                auto const value = local(type::I64);
                delay_value(aStatement, kind, value);
                get(kind);
                get(value);
                runtime("delay", { type::I64, type::I64 }, type::Void);
                check();
                return true;
            }
        case construct::Abort:
            for (auto* c : aStatement->children)
            {
                if (c->type != nullptr && c->type->view()->is_access())
                    expression(c, c->type);
                else
                    address(c);
                load64(program::TaskHandle);
                runtime("task_abort", { type::I64 }, type::Void);
                check();
            }
            return true;
        case construct::Accept:
            {
                auto* entry = aStatement->ent;
                node* index = nullptr;
                // (its entry index: not the closing name after a body)
                for (auto* c : aStatement->children)
                    if (c != aStatement->child(0) && !c->is(construct::Parameter) && !c->is(construct::Handled) && !c->is(construct::End) &&
                        !(c == aStatement->children.back() && c->is(construct::Identifier) && aStatement->first(construct::Handled) != nullptr))
                        index = c;
                i64(entry_key(entry) << 32);
                entry_family_offset(entry, index);
                op(opcode::I64Add);
                runtime("accept", { type::I64 }, type::I64);
                check();
                auto const call = local(type::I64);
                set(call);
                accept_body(aStatement, call);
                return true;
            }
        case construct::SelectAccept:
            {
                // (the open accept alternatives' entries (an array in the frame), the delay, terminate and else: the runtime's 
                // choice, then that alternative)
                std::vector<node*> alternatives;
                node* elsePart = nullptr;
                for (auto* c : aStatement->children)
                    if (c->is(construct::SelectAlternative))
                        alternatives.push_back(c);
                    else if (c->is(construct::Statements))
                        elsePart = c;
                std::vector<node*> accepts;
                std::vector<node*> delayAlternatives;
                node* terminateAlternative = nullptr;
                for (auto* a : alternatives)
                {
                    node* first = nullptr;
                    for (auto* c : a->children)
                        if (!c->is(construct::Guard))
                        {
                            first = c;
                            break;
                        }
                    if (first == nullptr)
                        continue;
                    if (first->is(construct::Accept))
                        accepts.push_back(a);
                    else if (first->is(construct::Delay))
                        delayAlternatives.push_back(a);
                    else if (first->is(construct::Terminate))
                        terminateAlternative = a;
                }
                auto const guard_of = [](node* aAlternative) { return aAlternative->first(construct::Guard); };
                auto const statement_of = [](node* aAlternative) -> node*
                {
                    for (auto* c : aAlternative->children)
                        if (!c->is(construct::Guard))
                            return c;
                    return nullptr;
                };
                // (the guards first (9.7.1(15)), then the open alternatives' entry indexes and delays)
                std::vector<std::uint32_t> opens(accepts.size(), 0u);
                for (std::size_t i = 0u; i < accepts.size(); ++i)
                    if (auto* g = guard_of(accepts[i]))
                    {
                        opens[i] = local(type::I32);
                        condition(g->child(0));
                        set(opens[i]);
                    }
                std::vector<std::uint32_t> delayOpens(delayAlternatives.size(), 0u);
                for (std::size_t i = 0u; i < delayAlternatives.size(); ++i)
                    if (auto* g = guard_of(delayAlternatives[i]))
                    {
                        delayOpens[i] = local(type::I32);
                        condition(g->child(0));
                        set(delayOpens[i]);
                    }
                std::uint32_t terminateOpen = 0u;
                if (terminateAlternative != nullptr)
                    if (auto* g = guard_of(terminateAlternative))
                    {
                        terminateOpen = local(type::I32);
                        condition(g->child(0));
                        set(terminateOpen);
                    }
                iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                auto const array = iFrameSize;
                iFrameSize += 8u * std::max<std::size_t>(accepts.size(), 1u);
                for (std::size_t i = 0u; i < accepts.size(); ++i)
                {
                    auto* accept = statement_of(accepts[i]);
                    auto* entry = accept->ent;
                    node* index = nullptr;
                    for (auto* c : accept->children)
                        if (c != accept->child(0) && !c->is(construct::Parameter) && !c->is(construct::Handled) && !c->is(construct::End) &&
                            !(c == accept->children.back() && c->is(construct::Identifier) && accept->first(construct::Handled) != nullptr))
                            index = c;
                    get(iFrame);
                    if (opens[i] != 0u)
                    {
                        // (closed: -1)
                        get(opens[i]);
                        begin_if(type::I64);
                        i64(entry_key(entry) << 32);
                        entry_family_offset(entry, index);
                        op(opcode::I64Add);
                        else_();
                        i64(-1);
                        end();
                    }
                    else
                    {
                        i64(entry_key(entry) << 32);
                        entry_family_offset(entry, index);
                        op(opcode::I64Add);
                    }
                    store64(static_cast<std::uint32_t>(array + 8u * i));
                }
                auto const delayKind = local(type::I64);
                auto const delayAmount = local(type::I64);
                i64(0);
                set(delayKind);
                i64(0);
                set(delayAmount);
                // (the open delay alternatives: the one whose expiration is the earliest (9.7.1(16)))
                auto const delayIndex = local(type::I64);
                i64(-1);
                set(delayIndex);
                for (std::size_t i = 0u; i < delayAlternatives.size(); ++i)
                {
                    if (delayOpens[i] != 0u)
                    {
                        get(delayOpens[i]);
                        begin_if();
                    }
                    auto const kind = local(type::I64);
                    auto const amount = local(type::I64);
                    delay_value(statement_of(delayAlternatives[i]), kind, amount);
                    get(delayKind);
                    op(opcode::I64Eqz);
                    get(kind);
                    get(delayKind);
                    op(opcode::I64Eq);
                    get(amount);
                    get(delayAmount);
                    op(opcode::I64LtS);
                    op(opcode::I32And);
                    op(opcode::I32Ior);
                    begin_if();
                    get(kind);
                    set(delayKind);
                    get(amount);
                    set(delayAmount);
                    i64(static_cast<std::int64_t>(i));
                    set(delayIndex);
                    end();
                    if (delayOpens[i] != 0u)
                        end();
                }
                auto const terminate = local(type::I64);
                i64(0);
                set(terminate);
                if (terminateAlternative != nullptr)
                {
                    if (terminateOpen != 0u)
                    {
                        get(terminateOpen);
                        op(opcode::I64UConvertI32);
                    }
                    else
                        i64(1);
                    set(terminate);
                }
                get(iFrame);
                i64(static_cast<std::int64_t>(array));
                op(opcode::I64Add);
                i64(static_cast<std::int64_t>(accepts.size()));
                get(delayKind);
                get(delayAmount);
                get(terminate);
                i64(elsePart != nullptr ? 1 : 0);
                runtime("select", { type::I64, type::I64, type::I64, type::I64, type::I64, type::I64 }, type::I64);
                check();
                auto const choice = local(type::I64);
                set(choice);
                for (std::size_t i = 0u; i < accepts.size(); ++i)
                {
                    get(choice);
                    i64(static_cast<std::int64_t>(i));
                    op(opcode::I64Eq);
                    begin_if();
                    auto const call = local(type::I64);
                    runtime("accepted_call", {}, type::I64);
                    set(call);
                    accept_body(statement_of(accepts[i]), call);
                    statements(accepts[i]->first(construct::Statements));
                    end();
                }
                for (std::size_t i = 0u; i < delayAlternatives.size(); ++i)
                {
                    get(choice);
                    i64(-1);
                    op(opcode::I64Eq);
                    get(delayIndex);
                    i64(static_cast<std::int64_t>(i));
                    op(opcode::I64Eq);
                    op(opcode::I32And);
                    begin_if();
                    statements(delayAlternatives[i]->first(construct::Statements));
                    end();
                }
                if (elsePart != nullptr)
                {
                    get(choice);
                    i64(-2);
                    op(opcode::I64Eq);
                    begin_if();
                    statements(elsePart);
                    end();
                }
                if (terminateAlternative != nullptr)
                {
                    // (terminate: the task body ends)
                    get(choice);
                    i64(-3);
                    op(opcode::I64Eq);
                    begin_if();
                    branch(0u);
                    end();
                }
                return true;
            }
        case construct::SelectTimed:
        case construct::SelectConditional:
            {
                auto* callPart = aStatement->first(construct::SelectCall);
                auto* delayPart = aStatement->first(construct::SelectDelay);
                node* elsePart = nullptr;
                for (auto* c : aStatement->children)
                    if (c->is(construct::Statements))
                        elsePart = c;
                node* callStatement = nullptr;
                for (auto* c : callPart->children)
                    if (!c->is(construct::Statements))
                    {
                        callStatement = c;
                        break;
                    }
                auto* callNode = callStatement->is(construct::Call) ? callStatement->child(0) : callStatement;
                auto const* info = iProgram.call_of(callNode);
                if (info == nullptr || info->callee == nullptr || info->callee->kind != entity_kind::Entry)
                {
                    // (not an entry call (e.g. a procedure that a requeue or renaming makes one): made, as a call)
                    statement(callStatement);
                    statements(callPart->first(construct::Statements));
                    return true;
                }
                auto const kind = local(type::I64);
                auto const value = local(type::I64);
                std::function<void()> const timing = [&]()
                {
                    if (delayPart != nullptr)
                    {
                        node* delayStatement = nullptr;
                        for (auto* c : delayPart->children)
                            if (c->is(construct::Delay))
                                delayStatement = c;
                        delay_value(delayStatement, kind, value);
                    }
                    else
                    {
                        i64(4);
                        set(kind);
                        i64(0);
                        set(value);
                    }
                };
                iWhere = callNode;
                entry_call(callNode, *info, info->callee, std::make_pair(kind, value), nullptr, &timing);
                op(opcode::I64Eqz);
                op(opcode::I32Eqz);
                begin_if();
                statements(callPart->first(construct::Statements));
                else_();
                if (delayPart != nullptr)
                    statements(delayPart->first(construct::Statements));
                else if (elsePart != nullptr)
                    statements(elsePart);
                end();
                return true;
            }
        case construct::SelectAsynchronous:
            {
                // (its triggering statement (a delay or an entry call) started; its abortable part run; then, if the triggering
                // statement completed, the sequence that follows it)
                auto* trigger = aStatement->first(construct::SelectTrigger);
                node* abortable = nullptr;
                for (auto* c : aStatement->children)
                    if (c->is(construct::Statements))
                        abortable = c;
                node* triggerStatement = nullptr;
                for (auto* c : trigger->children)
                    if (!c->is(construct::Statements))
                    {
                        triggerStatement = c;
                        break;
                    }
                auto* sequence = trigger->first(construct::Statements);
                std::uint32_t triggered = 0u;
                if (triggerStatement->is(construct::Delay))
                {
                    auto const kind = local(type::I64);
                    auto const value = local(type::I64);
                    delay_value(triggerStatement, kind, value);
                    get(kind);
                    get(value);
                    runtime("atc_delay", { type::I64, type::I64 }, type::I64);
                    auto const depth = local(type::I64);
                    set(depth);
                    triggered = abortable_part(depth, abortable);
                }
                else
                {
                    auto* callNode = triggerStatement->is(construct::Call) ? triggerStatement->child(0) : triggerStatement;
                    auto const* info = iProgram.call_of(callNode);
                    if (info == nullptr || info->callee == nullptr || info->callee->kind != entity_kind::Entry)
                    {
                        // (a procedure call: it completes before the abortable part could start)
                        statement(triggerStatement);
                        statements(sequence);
                        return true;
                    }
                    iWhere = callNode;
                    std::function<std::uint32_t(std::uint32_t)> const part = [&](std::uint32_t aDepth) { return abortable_part(aDepth, abortable); };
                    triggered = local(type::I64);
                    std::function<std::uint32_t(std::uint32_t)> const recorded = [&](std::uint32_t aDepth)
                    {
                        auto const t = part(aDepth);
                        get(t);
                        set(triggered);
                        return t;
                    };
                    entry_call(callNode, *info, info->callee, {}, &recorded);
                }
                get(triggered);
                op(opcode::I64Eqz);
                op(opcode::I32Eqz);
                begin_if();
                statements(sequence);
                end();
                return true;
            }
        case construct::Requeue:
            {
                // (the call requeued on the entry; the accept statement (or the entry body) ends)
                auto* name = aStatement->child(0);
                auto const* info = iProgram.call_of(name);
                entity* entry = info != nullptr && info->callee != nullptr ? info->callee : name->ent;
                node* object = info != nullptr ? info->object : nullptr;
                node* index = info != nullptr ? info->family_index : nullptr;
                auto* target = name;
                if (target->is(construct::Apply) && entry != nullptr && entry->sub->family_type != nullptr)
                {
                    index = target->child(1);
                    target = target->child(0);
                }
                if (object == nullptr && target->is(construct::Selected) && meaning_of(target->child(0)) != meaning::Unit)
                    object = target->child(0);
                if (entry != nullptr && entry->kind == entity_kind::Procedure && entry->sub != nullptr && entry->sub->controlling != nullptr &&
                    entry->sub->controlling->view()->base_type()->view()->kind == type_kind::Interface && object != nullptr)
                {
                    // (a synchronized interface's procedure: the entry that implements it for the object's type (a dispatching requeue,
                    // 9.5.4(5/3)); Program_Error if it is not implemented by an entry)
                    auto* iface = entry->sub->controlling->view()->base_type()->view();
                    bool const fromEntryBody = iProgram.subprogram_form(iSubprogram) == subprogram_kind::ProtectedEntry && iAcceptControls.empty();
                    std::string_view const statement = aStatement->source;
                    auto const nameEnd = static_cast<std::size_t>(name->source.data() + name->source.size() - statement.data());
                    bool const withAbort = nameEnd <= statement.size() && statement.substr(nameEnd).find("abort") != std::string_view::npos;
                    auto const obj = local(type::I64);
                    if (object->type != nullptr && object->type->view()->is_access())
                        expression(object, object->type);
                    else
                        address(object);
                    null_check(aStatement);
                    set(obj);
                    auto const descriptor = local(type::I64);
                    get(obj);
                    load64(0u);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(iface)));
                    i64(entry->sub->slot);
                    i64(static_cast<std::int64_t>(iface->slots.size()));
                    runtime("interface_entry", { type::I64, type::I64, type::I64, type::I64 }, type::I64);
                    tee(descriptor);
                    op(opcode::I64Eqz);
                    begin_if();
                    raise_check(1, aStatement, "the requeue target is not implemented by an entry");
                    end();
                    // (a protected object's entry: the object, its index; a task's: its handle, its index (shifted))
                    auto const isProtected = local(type::I64);
                    get(descriptor);
                    load64(0u);
                    i64(2);
                    op(opcode::I64Eq);
                    op(opcode::I64UConvertI32);
                    set(isProtected);
                    i64(fromEntryBody ? 1 : 0);
                    get(isProtected);
                    i64(withAbort ? 2 : 0);
                    op(opcode::I64Ior);
                    get(obj);
                    get(obj);
                    load64(program::TaskHandle);
                    get(isProtected);
                    op(opcode::I32ConvertI64);
                    op(opcode::Select);
                    get(descriptor);
                    load64(8u);
                    get(descriptor);
                    load64(8u);
                    i64(32);
                    op(opcode::I64Shl);
                    get(isProtected);
                    op(opcode::I32ConvertI64);
                    op(opcode::Select);
                    i64(0);
                    runtime("requeue", { type::I64, type::I64, type::I64, type::I64, type::I64 }, type::Void);
                    check();
                    branch(fromEntryBody ? 0u : iAcceptControls.back());
                    return true;
                }
                if (entry == nullptr || entry->kind != entity_kind::Entry)
                    iProgram.error(aStatement, "requeue target is not an entry");
                auto* owner = entry->scope != nullptr ? entry->scope->owner : nullptr;
                bool const isProtected = owner != nullptr && owner->kind == entity_kind::Protected;
                bool const fromEntryBody = iProgram.subprogram_form(iSubprogram) == subprogram_kind::ProtectedEntry && iAcceptControls.empty();
                // (with abort: the requeued call remains cancellable (9.5.4(13)); the keywords after the name)
                std::string_view const statement = aStatement->source;
                auto const nameEnd = static_cast<std::size_t>(name->source.data() + name->source.size() - statement.data());
                bool const withAbort = nameEnd <= statement.size() && statement.substr(nameEnd).find("abort") != std::string_view::npos;
                i64(fromEntryBody ? 1 : 0);
                i64((isProtected ? 1 : 0) | (withAbort ? 2 : 0));
                if (object != nullptr)
                {
                    if (object->type != nullptr && object->type->view()->is_access())
                        expression(object, object->type);
                    else
                        address(object);
                    if (!isProtected)
                        load64(program::TaskHandle);
                }
                else if (isProtected)
                    self_address(entry);
                else
                    runtime("current_task", {}, type::I64);
                if (isProtected)
                {
                    i64(entry_key(entry));
                    entry_family_offset(entry, index);
                }
                else
                {
                    i64(entry_key(entry) << 32);
                    entry_family_offset(entry, index);
                    op(opcode::I64Add);
                    i64(0);
                }
                runtime("requeue", { type::I64, type::I64, type::I64, type::I64, type::I64 }, type::Void);
                check();
                branch(fromEntryBody ? 0u : iAcceptControls.back());
                return true;
            }
        default:
            break;
        }
        return false;
    }

    void function_generator::tasking_attribute(node* aAttribute, std::string const& aName)
    {
        auto* prefix = aAttribute->child(0);
        if (aName == "callable" || aName == "terminated" || aName == "identity")
        {
            // (a task object (or an access to one))
            if (prefix->type != nullptr && prefix->type->view()->is_access())
            {
                expression(prefix, prefix->type);
                null_check(aAttribute);
            }
            else if (current_instance(prefix))
                ;
            else if (prefix->ent != nullptr && prefix->ent->kind == entity_kind::Task)
                iProgram.error(aAttribute, "attribute '" + aName + " of a task unit is not supported yet");
            else
                address(prefix);
            load64(program::TaskHandle);
            if (aName == "identity")
                return;
            runtime(aName == "callable" ? "task_callable" : "task_terminated", { type::I64 }, type::I64);
            return;
        }
        if (aName == "caller")
        {
            // (E'Caller (C.7.1(14/3)): the caller of the call being accepted (within an accept statement for E), or of the call that
            // the protected entry body is for)
            auto* entryName = prefix->is(construct::Apply) ? prefix->child(0) : prefix;
            auto* entry = entryName->ent != nullptr ? entryName->ent : prefix->ent;
            if (entry == nullptr || entry->kind != entity_kind::Entry)
                iProgram.error(aAttribute, "'Caller of a name that is not an entry");
            auto* owner = entry->scope != nullptr ? entry->scope->owner : nullptr;
            i64(owner != nullptr && owner->kind == entity_kind::Protected ? -1 : entry_key(entry) << 32);
            runtime("entry_caller", { type::I64 }, type::I64);
            return;
        }
        if (aName == "count")
        {
            auto* entryName = prefix->is(construct::Apply) ? prefix->child(0) : prefix;
            auto* entry = entryName->ent != nullptr ? entryName->ent : prefix->ent;
            if (entry == nullptr || entry->kind != entity_kind::Entry)
                iProgram.error(aAttribute, "'Count of a name that is not an entry");
            node* index = prefix->is(construct::Apply) && prefix->size() >= 2u ? prefix->child(1) : nullptr;
            auto* owner = entry->scope != nullptr ? entry->scope->owner : nullptr;
            if (owner != nullptr && owner->kind == entity_kind::Protected)
            {
                self_address(entry);
                i64(entry_key(entry));
                if (index != nullptr && entry->sub->family_type != nullptr)
                {
                    // (a family member's: its index checked (9.9(5)))
                    entry_family_offset(entry, index);
                    runtime("po_count_family", { type::I64, type::I64, type::I64 }, type::I64);
                    return;
                }
                runtime("po_count", { type::I64, type::I64 }, type::I64);
                return;
            }
            i64(entry_key(entry) << 32);
            entry_family_offset(entry, index);
            op(opcode::I64Add);
            runtime("entry_count", { type::I64 }, type::I64);
            return;
        }
        if (aName == "storage_size")
        {
            // (an access type's: its specified Storage_Size (13.11(16)))
            auto* pt = prefix->type != nullptr ? prefix->type->view() : nullptr;
            // (its own, else its ancestor's (a derived type's pool is its parent's, 13.11(22.1/3)))
            for (auto* at = pt != nullptr && pt->kind == type_kind::Access ? pt->base_type()->view() : nullptr; at != nullptr;
                at = at->parent != nullptr && at->parent->view()->base_type()->view() != at ? at->parent->view()->base_type()->view() : nullptr)
                if (auto* size = iProgram.type_aspect(at, "storage_size"))
                {
                    expression(size, size->type != nullptr ? size->type : iProgram.integer());
                    return;
                }
            i64(std::int64_t{ 1 } << 20);
            return;
        }
        iProgram.error(aAttribute, "attribute '" + aName + " is not supported yet");
    }
}

namespace neos::compilers::ada
{
    // --- storage pools (13.11)

    node* function_generator::storage_pool_of(ada_type* aAccess)
    {
        for (auto* t = aAccess != nullptr ? aAccess->view()->base_type()->view() : nullptr; t != nullptr;
            t = t->parent != nullptr && t->parent->view()->base_type()->view() != t ? t->parent->view()->base_type()->view() : nullptr)
            if (auto* pool = iProgram.type_aspect(t, "storage_pool"))
                return pool;
        return nullptr;
    }

    std::uint64_t function_generator::storage_alignment(ada_type* aType)
    {
        auto* t = aType != nullptr ? aType->view() : nullptr;
        if (t == nullptr)
            return 8u;
        if (t->is_scalar() || t->is_access())
            return std::max<std::uint64_t>(iProgram.scalar_size(t), 1u);
        if (t->kind == type_kind::Array && t->component != nullptr)
            return storage_alignment(t->component);
        return 8u;
    }

    void function_generator::pool_call(std::string const& aOperation, std::uint32_t aPool, std::uint32_t aAddress, std::uint32_t aSize,
        std::uint64_t aAlignment, std::optional<std::uint32_t> aSubpool)
    {
        // (Allocate (Pool, Storage_Address (out), Size, Alignment) or Deallocate (Pool, Storage_Address, Size, Alignment): dispatching on
        // the pool (Root_Storage_Pool's primitive's slot); Allocate_From_Subpool (..., Subpool): Root_Storage_Pool_With_Subpools's)
        auto* root = aSubpool ? iProgram.library_type("system.storage_pools.subpools", "root_storage_pool_with_subpools") :
            iProgram.library_type("system.storage_pools", "root_storage_pool");
        int slot = -1;
        if (root != nullptr)
            for (auto* t : { root, root->view(), root->view()->base_type()->view() })
                for (std::size_t i = 0u; i < t->slots.size() && slot < 0; ++i)
                    if (t->slots[i].operation != nullptr && t->slots[i].operation->name == aOperation)
                        slot = static_cast<int>(i);
        if (slot < 0)
            iProgram.error(iWhere, "internal error: no slot for System.Storage_Pools." + aOperation);
        get(aPool);
        load64(0u);
        load64(static_cast<std::uint32_t>(program::TagLink));
        get(aPool);
        get(aAddress);
        get(aSize);
        i64(static_cast<std::int64_t>(aAlignment));
        if (aSubpool)
            get(*aSubpool);
        get(aPool);
        load64(0u);
        load64(static_cast<std::uint32_t>(program::TagSlots + 8u * static_cast<std::uint32_t>(slot)));
        op(opcode::I32ConvertI64);
        if (aSubpool)
            call_indirect({ type::I64, type::I64, type::I64, type::I64, type::I64, type::I64 }, type::Void);
        else
            call_indirect({ type::I64, type::I64, type::I64, type::I64, type::I64 }, type::Void);
        check();
    }

    void function_generator::heap_allocate()
    {
        if (!iPool && iStorageLimit)
        {
            // (within the access type's Storage_Size)
            auto const size = temporary(type::I64);
            set(size);
            i64(static_cast<std::int64_t>(iStorageLimit->first));
            get(size);
            expression(iStorageLimit->second, iStorageLimit->second->type != nullptr ? iStorageLimit->second->type : iProgram.integer());
            runtime("allocate_limited", { type::I64, type::I64, type::I64 }, type::I64);
            release(size);
            check();
            return;
        }
        if (!iPool)
        {
            runtime("allocate", { type::I64 }, type::I64);
            return;
        }
        // (the access type's pool's Allocate: its out Storage_Address in a frame cell)
        auto const size = local(type::I64);
        set(size);
        iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
        auto const offset = iFrameSize;
        iFrameSize += 8u;
        auto const cell = local(type::I64);
        get(iFrame);
        i64(static_cast<std::int64_t>(offset));
        op(opcode::I64Add);
        set(cell);
        if (iSubpool)
            pool_call("allocate_from_subpool", *iPool, cell, size, iPoolAlignment, iSubpool);
        else
            pool_call("allocate", *iPool, cell, size, iPoolAlignment);
        get(cell);
        load64(0u);
    }
}
