/*
  gen_final.cpp

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

// Ada: controlled types (7.6): Initialize, Adjust and Finalize of the controlled parts of objects. A controlled part of an
// object that is initialized is registered on the task's finalization chain (the runtime's: its address and the slot of
// Finalize in its type's dispatch table); a master (a subprogram, a block) notes the chain's length when it starts and
// finalizes the parts registered after that, in the reverse order, when it ends (normally or by an exception); the library
// level's are finalized when the main subprogram has ended. Assignment finalizes the target's controlled parts, copies the
// value and adjusts them. A function's result and an aggregate are the object they initialize (built in place, in effect):
// neither adjusted nor finalized.

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <cstdio>
#include <cstdlib>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::ada
{
    // --- the model

    bool program::has_controlled()
    {
        return library_unit("ada.finalization") != nullptr;
    }

    bool program::is_controlled(ada_type* aType)
    {
        if (aType == nullptr || !has_controlled())
            return false;
        auto* t = aType->view()->base_type()->view();
        if (!t->tagged || t->kind == type_kind::ClassWide)
            return false;
        auto* controlled = library_type("ada.finalization", "controlled");
        auto* limitedControlled = library_type("ada.finalization", "limited_controlled");
        bool const result = is_descendant(t, controlled) || is_descendant(t, limitedControlled);
        if (std::getenv("NEOS_ADA_DEBUG"))
            std::fprintf(stderr, "is_controlled %s: %d (parent %s)\n", t->name.c_str(), result, t->parent != nullptr ? t->parent->name.c_str() : "-");
        return result;
    }

    bool program::needs_finalization(ada_type* aType)
    {
        if (aType == nullptr || !has_controlled())
            return false;
        auto* t = aType->view()->base_type()->view();
        // (a class-wide type of a tagged type: its objects may be of a type with controlled parts (decided by their tags))
        if (t->kind == type_kind::ClassWide)
            return t->specific != nullptr && t->specific->view()->base_type()->view()->tagged;
        auto existing = iNeedsFinalization.find(t);
        if (existing != iNeedsFinalization.end())
            return existing->second;
        iNeedsFinalization[t] = false;  // (recursion)
        bool result = false;
        if (is_controlled(t))
            result = true;
        else if (t->kind == type_kind::Array)
            result = needs_finalization(t->component);
        else if (t->kind == type_kind::Record || t->kind == type_kind::Protected || t->kind == type_kind::Task)
        {
            // (a protected object's (or task's) components too)
            for (auto const& c : t->components)
                if (c.type != nullptr && needs_finalization(c.type))
                    result = true;
        }
        iNeedsFinalization[t] = result;
        return result;
    }

    int program::controlled_slot(std::string const& aOperation, ada_type* aType)
    {
        // (the slot of Initialize, Adjust or Finalize in the dispatch tables of the types derived from the type's root
        // (Controlled or Limited_Controlled: their slots differ, Limited_Controlled having no Adjust))
        auto* limitedControlled = library_type("ada.finalization", "limited_controlled");
        bool const limited = aType != nullptr && is_descendant(aType->view()->base_type()->view(), limitedControlled);
        for (auto const* root : { "controlled", "limited_controlled" })
        {
            if (aType != nullptr && limited != (std::string{ root } == "limited_controlled"))
                continue;
            auto* partial = library_type("ada.finalization", root);
            for (auto* t : { partial, partial->view(), partial->view()->base_type()->view() })
                for (std::size_t i = 0u; i < t->slots.size(); ++i)
                    if (t->slots[i].operation != nullptr && t->slots[i].operation->name == aOperation)
                    {
                        if (std::getenv("NEOS_ADA_DEBUG"))
                            std::fprintf(stderr, "slot of %s: %zu\n", aOperation.c_str(), i);
                        return static_cast<int>(i);
                    }
        }
        if (std::getenv("NEOS_ADA_DEBUG"))
            std::fprintf(stderr, "no slot for %s\n", aOperation.c_str());
        return -1;
    }

    // --- code

    void function_generator::controlled_parts(ada_type* aType, std::uint32_t aAddress, bool aReverse, std::function<void(ada_type*, std::uint32_t)> const& aAction,
        bool aDefaulted)
    {
        // (the controlled parts of a composite object (an address in a local), components first (the reverse: the object first))
        auto* t = aType->view();
        if (!iProgram.needs_finalization(t))
            return;
        // (a class-wide object: as a whole (its tag's deep operations, see controlled_call))
        if (t->kind == type_kind::ClassWide)
        {
            aAction(t, aAddress);
            return;
        }
        bool const self = iProgram.is_controlled(t);
        if (self && aReverse)
            aAction(t, aAddress);
        if (t->kind == type_kind::Array)
        {
            auto* c = t->component;
            auto const n = local(type::I64);
            auto const p = local(type::I64);
            array_length(aAddress, t->base_type()->view()->indexes.size());
            set(n);
            data_address(aAddress);
            if (aReverse)
            {
                // (the last element first)
                get(n);
                i64(1);
                op(opcode::I64Sub);
                element_size(t);
                op(opcode::I64Mul);
                op(opcode::I64Add);
            }
            set(p);
            begin(control_kind::Block);
            auto const exit = iControls.size() - 1u;
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            get(n);
            i64(0);
            op(opcode::I64LeS);
            branch(exit, true);
            controlled_parts(c, p, aReverse, aAction, aDefaulted);
            get(p);
            element_size(t);
            op(aReverse ? opcode::I64Sub : opcode::I64Add);
            set(p);
            get(n);
            i64(1);
            op(opcode::I64Sub);
            set(n);
            branch(head);
            end();
            end();
        }
        else
        {
            auto* base = t->base_type()->view();
            iProgram.layout(base);
            std::vector<component_info const*> components;
            for (auto const* c : iProgram.initialization_order(base))
                if (c->offset && c->type != nullptr && iProgram.needs_finalization(c->type) && !(aDefaulted && c->default_expression != nullptr))
                    components.push_back(c);
            if (aReverse)
                std::reverse(components.begin(), components.end());
            for (auto const* c : components)
            {
                // (a component of a variant: if the discriminants select it)
                if (c->variant >= 0)
                {
                    variant_selected(base, *c, aAddress);
                    begin_if();
                }
                auto const a = local(type::I64);
                component_address(base, *c, aAddress);
                set(a);
                controlled_parts(c->type, a, aReverse, aAction, aDefaulted);
                if (c->variant >= 0)
                    end();
            }
        }
        if (self && !aReverse)
            aAction(t, aAddress);
    }

    entity* function_generator::controlled_operation(ada_type* aType, std::string const& aOperation)
    {
        // (the type's Initialize, Adjust or Finalize (its own, or the one it inherits); null: the one of Ada.Finalization (a null
        // procedure: nothing to call))
        auto* t = aType->view()->base_type()->view();
        auto const slot = iProgram.controlled_slot(aOperation, t);
        if (slot < 0)
            return nullptr;
        auto const* slots = &t->slots;
        if (t->partial != nullptr && t->partial->slots.size() > slots->size())
            slots = &t->partial->slots;
        if (static_cast<std::size_t>(slot) >= slots->size())
            return nullptr;
        auto* op = (*slots)[static_cast<std::size_t>(slot)].operation;
        for (int k = 0; k < 64 && op != nullptr && op->sub != nullptr && op->sub->inherited && op->sub->parent_operation != nullptr; ++k)
            op = op->sub->parent_operation;
        if (op == nullptr || op->sub == nullptr || op->sub->is_abstract)
            return nullptr;
        if (op->sub->is_null && op->scope != nullptr && op->scope->owner != nullptr && iProgram.full_name(op->scope->owner) == "ada.finalization")
            return nullptr;
        return ultimate(op);
    }

    void function_generator::controlled_call(ada_type* aType, std::uint32_t aAddress, std::string const& aOperation)
    {
        // (Initialize, Adjust or Finalize of a controlled object of a type (the object's specific type): its own (or inherited) 
        // operation called)
        // (abort-deferred (9.8(9)): an abort (or an asynchronous select's) takes effect after it)
        if (aType->view()->kind == type_kind::ClassWide)
        {
            // (a class-wide object: its tag's deep Adjust or Finalize (if its type has controlled parts), with its tag's static link)
            if (aOperation == "initialize")
                return;
            runtime("abort_defer", {}, type::Void);
            auto const tag = temporary(type::I64);
            auto const function = temporary(type::I64);
            get(aAddress);
            load64(0u);
            tee(tag);
            load64(static_cast<std::uint32_t>(aOperation == "finalize" ? program::TagFinalize : program::TagAdjust));
            tee(function);
            op(opcode::I64Eqz);
            op(opcode::I32Eqz);
            begin_if();
            get(tag);
            load64(static_cast<std::uint32_t>(program::TagLink));
            get(aAddress);
            get(function);
            op(opcode::I32ConvertI64);
            call_indirect({ type::I64, type::I64 }, type::Void);
            end();
            release(function);
            release(tag);
            i64(1);
            runtime("abort_undefer", { type::I64 }, type::Void);
            return;
        }
        auto* op = controlled_operation(aType, aOperation);
        if (op == nullptr)
            return;
        runtime("abort_defer", {}, type::Void);
        static_link_for(op);
        get(aAddress);
        call_function(op);
        // (an Initialize: the object registered for its finalization before the abort takes effect)
        i64(aOperation == "initialize" ? 0 : 1);
        runtime("abort_undefer", { type::I64 }, type::Void);
        if (iControlledFailed && aOperation != "initialize")
        {
            // (an exception (not an abort signal): noted, and the other parts' operations done)
            this->op(opcode::GlobalGet);
            u32(iProgram.exc_global());
            i64(AtcSignalBase);
            this->op(opcode::I64LtU);
            this->op(opcode::GlobalGet);
            u32(iProgram.exc_global());
            i64(0);
            this->op(opcode::I64Ne);
            this->op(opcode::I32And);
            begin_if();
            runtime("save_occurrence", {}, type::I64);
            runtime("release_occurrence", { type::I64 }, type::Void);
            i64(0);
            this->op(opcode::GlobalSet);
            u32(iProgram.exc_global());
            i64(1);
            set(*iControlledFailed);
            end();
        }
        check();
    }

    void function_generator::controlled_guarded(std::function<void()> const& aParts)
    {
        auto const failed = local(type::I64);
        i64(0);
        set(failed);
        auto const saved = iControlledFailed;
        iControlledFailed = failed;
        aParts();
        iControlledFailed = saved;
        get(failed);
        op(opcode::I32ConvertI64);
        begin_if();
        raise_check(1, nullptr, "an Adjust or Finalize procedure propagated an exception");
        end();
    }

    void function_generator::controlled_initialized(ada_type* aType, std::uint32_t aAddress, bool aInitialize, bool aAdjust, bool aRegister)
    {
        // (an object's controlled parts, after its default initialization (Initialize) or its copy (Adjust): registered for
        // their finalization)
        if (!iProgram.needs_finalization(aType))
            return;
        // (Initialize: not of the components initialized by their default expressions (7.6(10)))
        if (aInitialize)
            controlled_parts(aType, aAddress, false, [&](ada_type* aPartType, std::uint32_t aPart)
                {
                    controlled_call(aPartType, aPart, "initialize");
                }, true);
        if (!aAdjust && !aRegister)
            return;
        auto const parts = [&]()
        {
        controlled_parts(aType, aAddress, false, [&](ada_type* aPartType, std::uint32_t aPart)
            {
                if (aAdjust)
                    controlled_call(aPartType, aPart, "adjust");
                if (aRegister && aPartType->view()->kind == type_kind::ClassWide)
                {
                    // (a class-wide object: registered with its tag's deep Finalize (if its type has controlled parts))
                    auto const tag = temporary(type::I64);
                    get(aPart);
                    load64(0u);
                    tee(tag);
                    load64(static_cast<std::uint32_t>(program::TagFinalize));
                    op(opcode::I64Eqz);
                    op(opcode::I32Eqz);
                    begin_if();
                    get(aPart);
                    get(tag);
                    load64(static_cast<std::uint32_t>(program::TagFinalize));
                    get(tag);
                    load64(static_cast<std::uint32_t>(program::TagLink));
                    runtime("fin_register", { type::I64, type::I64, type::I64 }, type::Void);
                    end();
                    release(tag);
                }
                else if (aRegister)
                    if (auto* finalize = controlled_operation(aPartType, "finalize"))
                    {
                        // (registered: the object, its Finalize and that's static link)
                        get(aPart);
                        if (!finalize->sub->defined)
                            iProgram.define_subprogram(finalize);
                        function_index(finalize->sub->function_index);
                        op(opcode::I64UConvertI32);
                        static_link_for(finalize);
                        runtime("fin_register", { type::I64, type::I64, type::I64 }, type::Void);
                    }
            });
        };
        if (aAdjust)
            controlled_guarded(parts);
        else
            parts();
        if (aRegister)
            iRegistered = true;
        // (initialized (and registered): a pending abort takes effect)
        if (aInitialize)
        {
            runtime("abort_check", {}, type::Void);
            check();
        }
    }

    void function_generator::controlled_finalize(ada_type* aType, std::uint32_t aAddress)
    {
        // (an object's controlled parts finalized (e.g. an assignment's target; a deallocated object))
        if (!iProgram.needs_finalization(aType))
            return;
        controlled_guarded([&]()
            {
                controlled_parts(aType, aAddress, true, [&](ada_type* aPartType, std::uint32_t aPart) { controlled_call(aPartType, aPart, "finalize"); });
            });
    }

    void function_generator::controlled_unregister(std::uint32_t aAddress)
    {
        // (an object (e.g. one that is deallocated) whose parts are finalized now: not again by its master)
        get(aAddress);
        runtime("fin_unregister", { type::I64 }, type::Void);
    }

    void function_generator::moved_result(node* aExpression)
    {
        // (a function result moved into an object (an initialization, an assignment, a return, an aggregate's component, an 
        // allocator): that object's, not a temporary)
        if (aExpression == nullptr)
            return;
        if (aExpression->is(construct::Parenthesized))
            return moved_result(aExpression->child(0));
        if (aExpression->is(construct::Qualified))
            return moved_result(aExpression->child(1));
        if (is_temporary_value(aExpression))
            iMovedResults.insert(aExpression);
    }

    void function_generator::temporary_result(ada_type* aType)
    {
        // ([address] -> [address]: a function result that needs finalization and isn't moved into an object: a temporary,
        // registered (the chain's length before the statement's first one remembered) to be finalized when the statement
        // (its master, 7.6.1(3)) ends)
        if (aType == nullptr || !iProgram.needs_finalization(aType))
            return;
        auto const result = local(type::I64);
        set(result);
        if (iTemporaryMark == 0u)
            iTemporaryMark = local(type::I64);
        get(iTemporaryMark);
        op(opcode::I64Eqz);
        begin_if();
        runtime("fin_mark", {}, type::I64);
        i64(1);
        op(opcode::I64Add);
        set(iTemporaryMark);
        end();
        controlled_initialized(aType, result, false, false, true);
        iTemporaries = true;
        get(result);
    }

    void function_generator::release_temporaries(bool aCheck)
    {
        // (the temporaries registered since the statement (or declaration) began: finalized)
        if (!iTemporaries)
            return;
        iTemporaries = false;
        get(iTemporaryMark);
        i64(0);
        op(opcode::I64Ne);
        begin_if();
        get(iTemporaryMark);
        i64(1);
        op(opcode::I64Sub);
        runtime("fin_release", { type::I64 }, type::Void);
        i64(0);
        set(iTemporaryMark);
        end();
        // (an exception propagated by a Finalize: Program_Error)
        if (aCheck)
            check();
    }

    std::uint32_t function_generator::hold_temporaries()
    {
        // (the temporaries of a compound statement's header that live until the statement ends (e.g. an iterated container 
        // that is a function result): their mark moved aside, so its statements' temporaries are finalized without them)
        if (!iTemporaries)
            return 0u;
        iTemporaries = false;
        auto const held = local(type::I64);
        get(iTemporaryMark);
        set(held);
        i64(0);
        set(iTemporaryMark);
        return held;
    }

    void function_generator::release_held(std::uint32_t aHeld)
    {
        if (aHeld == 0u)
            return;
        get(aHeld);
        i64(0);
        op(opcode::I64Ne);
        begin_if();
        get(aHeld);
        i64(1);
        op(opcode::I64Sub);
        runtime("fin_release", { type::I64 }, type::Void);
        end();
    }

    bool function_generator::is_temporary_value(node* aExpression)
    {
        // (an expression whose value is a new object (a function result, an aggregate, a concatenation): moved, not adjusted)
        if (aExpression == nullptr)
            return false;
        if (aExpression->is(construct::Parenthesized))
            return is_temporary_value(aExpression->child(0));
        if (aExpression->is(construct::Qualified))
            return is_temporary_value(aExpression->child(1));
        if (is_aggregate(aExpression))
            return true;
        auto const m = meaning_of(aExpression);
        if (m == meaning::Call)
        {
            auto const* call = iProgram.call_of(aExpression);
            return call == nullptr || call->callee == nullptr || call->callee->sub == nullptr || call->callee->sub->op == intrinsic::None ||
                call->callee->sub->op == intrinsic::Import;
        }
        return false;
    }
}
