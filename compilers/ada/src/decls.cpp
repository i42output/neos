/*
  decls.cpp

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

// Ada: the analysis of declarations (entities, types and subtypes, subprograms, packages, renamings, exceptions)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <functional>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        bool is_specification(node const* aNode)
        {
            return aNode != nullptr && (aNode->is(construct::ProcedureSpecification) || aNode->is(construct::FunctionSpecification));
        }

        node* specification_of(node* aItem)
        {
            for (auto* c : aItem->children)
                if (is_specification(c))
                    return c;
            return nullptr;
        }

        // (an operator symbol's designator: its text without quotes, lower case)
        std::string designator_of(node* aNode)
        {
            auto text = name_text(aNode->source);
            if (text.size() >= 2u && (text.front() == '\"' || text.front() == '%'))
                text = text.substr(1u, text.size() - 2u);
            return text;
        }

        std::uint64_t machine_size(bigint const& aLow, bigint const& aHigh)
        {
            for (std::uint64_t bytes : { 1u, 2u, 4u, 8u })
            {
                auto const bits = bytes * 8u;
                bigint const low = -bigint::pow(bigint{ 2 }, bits - 1u);
                bigint const high = bigint::pow(bigint{ 2 }, bits - 1u) - bigint{ 1 };
                if (aLow >= low && aHigh <= high)
                    return bytes;
            }
            return 8u;
        }
    }

    void program::register_library_unit(entity* aEntity)
    {
        if (iDeclaringUnit == nullptr)
            return;
        aEntity->library = true;
        aEntity->child_unit = iDeclaringUnit->name.find('.') != std::string::npos;
        if (!iDeclaringUnit->body || iLibraryUnits.find(iDeclaringUnit->name) == iLibraryUnits.end())
            iLibraryUnits[iDeclaringUnit->name] = aEntity;
        iDeclaringUnit->ent = aEntity;
        iDeclaringUnit = nullptr;
    }

    void program::redeclaration_check(std::string const& aName, entity_kind aKind, node* aDefining, region* aRegion)
    {
        // (an illegal redeclaration: a homograph of an explicit declaration in the same declarative region (8.3(26/2)); not a
        // completion (of an incomplete or private type, a deferred constant))
        auto const plain = [](entity_kind k)
        {
            return k == entity_kind::Variable || k == entity_kind::Constant || k == entity_kind::NamedNumber || k == entity_kind::Type ||
                k == entity_kind::Subtype || k == entity_kind::Exception || k == entity_kind::Parameter || k == entity_kind::Component ||
                k == entity_kind::Discriminant;
        };
        auto const unit = [](entity_kind k)
        {
            return k == entity_kind::Package || k == entity_kind::Task || k == entity_kind::Protected || k == entity_kind::GenericPackage ||
                k == entity_kind::GenericProcedure || k == entity_kind::GenericFunction;
        };
        auto const generic = [](entity_kind k)
        {
            return k == entity_kind::GenericPackage || k == entity_kind::GenericProcedure || k == entity_kind::GenericFunction;
        };
        // (a subprogram: a homograph of a generic unit (not overloadable) (8.3(26/2)))
        bool const subprogram = aKind == entity_kind::Procedure || aKind == entity_kind::Function;
        if ((!plain(aKind) && !unit(aKind) && !subprogram) || aRegion->generic_formals)
            return;
        // (a package body: the same declarative region as its declaration (8.1(3)))
        std::vector<entity*> homographs;
        for (auto* r = aRegion; r != nullptr; r = r->package_body ? r->parent : nullptr)
        {
            auto range = r->names.equal_range(aName);
            for (auto i = range.first; i != range.second; ++i)
                homographs.push_back(i->second);
            if (!r->package_body)
                break;
        }
        // (a generic unit's body: the same declarative region as its formal part (8.1(3)); its parameters: the declaration's (reported
        // there))
        bool bodyParameter = false;
        if (aKind == entity_kind::Parameter)
            for (node const* n = aDefining; n != nullptr && !bodyParameter; n = n->parent)
                bodyParameter = n->is(construct::SubprogramBody);
        if (aRegion->owner != nullptr && !bodyParameter)
            for (auto [r, steps] = std::pair{ aRegion->parent, 0 }; r != nullptr && steps < 2; r = r->parent, ++steps)
                if (r->generic_formals && r->owner != nullptr && r->owner->name == aRegion->owner->name)
                {
                    auto range = r->names.equal_range(aName);
                    for (auto i = range.first; i != range.second; ++i)
                        if (i->second->is_type() || i->second->is_object())
                            homographs.push_back(i->second);
                    break;
                }
        for (auto* x : homographs)
        {
            if (x->decl == nullptr || x->decl == aDefining || !x->visible_in_body || (x->sub != nullptr && x->sub->inherited))
                continue;
            // (a subprogram or a generic unit against a generic unit or a subprogram)
            if (subprogram || generic(aKind))
            {
                if ((subprogram && generic(x->kind)) || (generic(aKind) && (generic(x->kind) || x->kind == entity_kind::Procedure ||
                    x->kind == entity_kind::Function || x->kind == entity_kind::Package)))
                    error(aDefining, "\"" + aName + "\" is already declared in this declarative region");
                // (a subprogram and a declaration that is not overloadable (an object, type, exception, package, unit))
                if (subprogram && (plain(x->kind) || x->kind == entity_kind::Package || x->kind == entity_kind::Task || x->kind == entity_kind::Protected) &&
                    x->kind != entity_kind::Component && x->kind != entity_kind::Discriminant)
                    error(aDefining, "\"" + aName + "\" is already declared in this declarative region");
                if (subprogram)
                    continue;
            }
            // (units against units: specifications and bodies are matched elsewhere)
            if (unit(aKind) && !plain(x->kind) && x->kind != entity_kind::Entry)
                continue;
            if (!plain(x->kind) && !unit(x->kind) && x->kind != entity_kind::EnumerationLiteral && x->kind != entity_kind::Function &&
                x->kind != entity_kind::Procedure && x->kind != entity_kind::Entry)
                continue;
            if ((aKind == entity_kind::Type || aKind == entity_kind::Task || aKind == entity_kind::Protected) && x->is_type() && x->type != nullptr)
            {
                // (an incomplete view completed by a partial view: both completed by the full type)
                auto* view = x->type;
                while (view->full != nullptr && view->full != view)
                    view = view->full;
                // (not by another incomplete declaration; a partial view only completes an incomplete one)
                bool const newIncomplete = aDefining->parent != nullptr && aDefining->parent->is(construct::TypeIncomplete);
                bool const newPartial = aDefining->parent != nullptr &&
                    (aDefining->parent->is(construct::TypePrivate) || aDefining->parent->is(construct::TypePrivateExtension));
                if (view->kind == type_kind::Private && view->full == nullptr && !newIncomplete && (!newPartial || x->type->incomplete))
                    continue;
            }
            if (aKind == entity_kind::Constant && x->kind == entity_kind::Constant && x->initial == nullptr && !x->completed)
                continue;
            // (a component or discriminant of another type (an extension's inherited ones: not visible if the parent's are private))
            if ((aKind == entity_kind::Component || aKind == entity_kind::Discriminant) &&
                (x->kind == entity_kind::Component || x->kind == entity_kind::Discriminant))
            {
                auto const declaration = [](node const* n)
                {
                    while (n != nullptr && !n->is(construct::Type) && !n->is(construct::TypePrivate) && !n->is(construct::TypeIncomplete) &&
                        !n->is(construct::FormalType) && !n->is(construct::TaskType) && !n->is(construct::ProtectedType))
                        n = n->parent;
                    return n;
                };
                // (an ancestor's: a homograph where its components are visible (8.3(26/2)), not where they are private)
                if (declaration(x->decl) != declaration(aDefining))
                {
                    auto* owner = x->owner != nullptr ? x->owner->type : nullptr;
                    // (an ancestor's discriminant: not inherited if the type has a known discriminant part of its own (3.7(18)))
                    if (owner == nullptr || (x->kind == entity_kind::Discriminant && [&]()
                        {
                            auto* d = declaration(aDefining);
                            return d == nullptr || d->first(construct::Discriminants) != nullptr;
                        }()))
                        continue;
                    auto* b = owner->base_type();
                    auto* partial = b->partial != nullptr ? b->partial->base_type() : nullptr;
                    if (partial != nullptr && x->kind != entity_kind::Discriminant)
                    {
                        auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                        if (package == nullptr || !private_part_visible(package, aRegion))
                            continue;
                    }
                }
            }
            // (a single task or protected object: its unit and its object)
            if ((x->kind == entity_kind::Task || x->kind == entity_kind::Protected) && x->decl != nullptr && x->decl == aDefining)
                continue;
            error(aDefining, "\"" + aName + "\" is already declared in this declarative region");
        }
    }

    entity* program::declare(entity_kind aKind, node* aDefining, region* aRegion)
    {
        ++analysis_generation();
        std::string name;
        if (aDefining->is(construct::DefiningOperator))
        {
            name = designator_of(aDefining);
            recover([&]() { operator_symbol_check(aDefining); });
        }
        else if (aDefining->is(construct::DefiningCharacter))
            name = std::string{ aDefining->source };
        else
        {
            name = name_text(aDefining->source);
            auto const dot = name.rfind('.');
            if (dot != std::string::npos)
                name = name.substr(dot + 1u);
        }
        redeclaration_check(name, aKind, aDefining, aRegion);
        auto* e = new_entity(aKind, name, aDefining, aRegion);
        aDefining->ent = e;
        e->is_private = aRegion->private_start != static_cast<std::size_t>(-1) && aRegion->declarations.size() >= aRegion->private_start;
        aRegion->add(e);
        return e;
    }

    node* program::clone(node* aNode)
    {
        if (aNode == nullptr)
            return nullptr;
        auto& copy = iClones.emplace_back();
        copy.kind = aNode->kind;
        copy.source = aNode->source;
        copy.file = aNode->file;
        for (auto* c : aNode->children)
        {
            auto* cc = clone(c);
            cc->parent = &copy;
            copy.children.push_back(cc);
        }
        return &copy;
    }

    // --- declarative parts

    void program::declarations(node* aDeclarations, region* aRegion)
    {
        if (aDeclarations == nullptr)
            return;
        std::vector<node*> incompletes;
        for (auto* d : aDeclarations->children)
        {
            // (a body freezes the incomplete types declared before it in the declarative part (13.14(3/5)): they must be complete
            // (3.11.1(8)))
            if ((d->is(construct::SubprogramBody) || d->is(construct::PackageBody) || d->is(construct::TaskBody) ||
                d->is(construct::ProtectedBody)) && iInstanceBodies == 0)
                for (auto i = incompletes.begin(); i != incompletes.end();)
                    if (auto* t = (*i)->ent != nullptr ? (*i)->ent->type : nullptr; t != nullptr && t->full == nullptr && !(*i)->ent->completed)
                    {
                        auto* incomplete = *i;
                        i = incompletes.erase(i);
                        recover([&]() { error(incomplete, "the incomplete type \"" + incomplete->ent->name + "\" is frozen by a later body before its completion"); });
                    }
                    else
                        i = incompletes.erase(i);
            recover([&]() { declaration(d, aRegion); });
            if (d->is(construct::TypeIncomplete) && d->ent != nullptr && d->ent->decl == d->first(construct::Defining))
                incompletes.push_back(d);
        }
        recover([&]() { resolve_deferred_aspects(aRegion); });
        // (a declarative part's incomplete types: completed in it (3.10.1(3)); a package specification's: checked by
        // package_declaration)
        if (!aRegion->is_package || aRegion->package_body)
        {
            incomplete_check(aDeclarations, "in the same declarative part");
            overriding_check(aRegion);
            completion_check(aDeclarations, aDeclarations);
            // (a package body: its specification's declarations too)
            if (aRegion->package_body && aDeclarations->parent != nullptr && aDeclarations->parent->is(construct::PackageBody))
                if (auto* e = aDeclarations->parent->ent; e != nullptr && e->decl != nullptr && e->decl->parent != nullptr &&
                    e->decl->parent->is(construct::Package))
                    for (auto* part : { e->decl->parent->first(construct::Declarations), e->decl->parent->first(construct::Private) })
                        completion_check(part, aDeclarations);
        }
    }

    bool program::package_requires_body(node* aSpec)
    {
        // (a generic subprogram completed (by an expression function, in a package specification))
        auto const generic_completed = [](node* aGeneric)
        {
            return aGeneric->ent != nullptr && aGeneric->ent->sub != nullptr && (aGeneric->ent->sub->body != nullptr || 
                aGeneric->ent->imported || aGeneric->ent->sub->op != intrinsic::None || aGeneric->ent->convention == "intrinsic");
        };
        // (an incomplete type whose completion was given in the specification (but in error))
        auto const declared_later = [](node* aSpec, node* aIncomplete)
        {
            auto* defining = aIncomplete->first(construct::Defining);
            if (defining == nullptr)
                return false;
            auto const name = name_text(defining->source);
            for (auto* part : { aSpec->first(construct::Declarations), aSpec->first(construct::Private) })
                if (part != nullptr)
                    for (auto* d : part->children)
                        if (d != aIncomplete && (d->is(construct::Type) || d->is(construct::TypePrivate) || d->is(construct::TypePrivateExtension) ||
                            d->is(construct::TaskType) || d->is(construct::ProtectedType)))
                            if (auto* other = d->first(construct::Defining); other != nullptr && name_text(other->source) == name)
                                return true;
            return false;
        };
        // (a package specification that requires a body: one with a declaration that requires completion)
        std::function<bool(node*)> requires_body = [&](node* aSpec)
        {
            for (auto* part : { aSpec->first(construct::Declarations), aSpec->first(construct::Private) })
                if (part != nullptr)
                    for (auto* d : part->children)
                    {
                        // (a subprogram declaration: unless completed within the specification (e.g. by an expression function in the
                        // private part))
                        auto const within = [&](node const* n) { for (; n != nullptr; n = n->parent) if (n == aSpec) return true; return false; };
                        if ((d->is(construct::SubprogramDeclaration) && (d->ent == nullptr || (!d->ent->imported && d->ent->sub != nullptr && !d->ent->sub->is_abstract &&
                            (d->ent->sub->body == nullptr || !within(d->ent->sub->body))))) || d->is(construct::TaskType) || d->is(construct::TaskSingle) || d->is(construct::ProtectedType) ||
                            d->is(construct::ProtectedSingle) || (d->is(construct::Generic) && ((d->first(construct::Package) == nullptr && !generic_completed(d)) ||
                                (d->first(construct::Package) != nullptr && requires_body(d->first(construct::Package))))) || (d->is(construct::TypeIncomplete) && part == aSpec->first(construct::Private) && (d->ent == nullptr ||
                            (d->ent->type != nullptr && d->ent->type->full == nullptr)) && !declared_later(aSpec, d)))
                            return true;
                        if (d->is(construct::Package) && requires_body(d))
                            return true;
                        if (d->is(construct::Pragma) && !d->children.empty() && name_text(d->child(0)->source) == "elaborate_body")
                            return true;
                    }
            // (aspect Elaborate_Body)
            if (auto* list = aSpec->first(construct::Aspects))
                for (auto* a : list->children)
                    if (a->is(construct::Aspect) && !a->children.empty() && name_text(a->child(0)->source) == "elaborate_body" &&
                        (a->size() < 2u || name_text(a->children.back()->source) != "false"))
                        return true;
            return false;
        };
        return requires_body(aSpec);
    }

    void program::completion_check(node* aDeclarations, node* aCompletions, bool aPartition)
    {
        // (declarations that require completion (3.11.1(1/3), 7.2(4), 9.1(9), 9.4(11)): their completions in the same declarative part
        // (a subprogram's body, stub, renaming, null procedure or expression function, or pragma Import; a package's body (if it
        // requires one); a task or protected unit's body)
        if (aDeclarations == nullptr || iInstanceBodies != 0 || !iInstantiationSites.empty())
            return;
        auto const name_of = [&](node* d) -> std::string
        {
            if (d->is(construct::Generic))
                for (auto* c : d->children)
                {
                    if (c->is(construct::Package))
                        if (auto* defining = c->first(construct::Defining))
                            return name_text(defining->source);
                    if ((c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification)) && !c->children.empty())
                        return name_text(c->child(0)->source);
                }
            if (auto* spec = specification_of(d); spec != nullptr && !spec->children.empty())
                return spec->child(0)->is(construct::DefiningOperator) ? designator_of(spec->child(0)) : name_text(spec->child(0)->source);
            if (auto* defining = d->first(construct::Defining))
                return name_text(defining->source);
            return {};
        };
        std::set<std::string> completed;
        for (auto* d : aCompletions->children)
        {
            if (d->is(construct::SubprogramBody) || d->is(construct::SubprogramNull) || d->is(construct::SubprogramExpression) ||
                d->is(construct::RenamingSubprogram) || d->is(construct::Stub) || d->is(construct::PackageBody) || d->is(construct::TaskBody) ||
                d->is(construct::ProtectedBody))
                completed.insert(name_of(d));
            if (d->is(construct::Pragma) && !d->children.empty())
                if (auto const p = name_text(d->child(0)->source); p == "import" || p == "interface")
                    for (auto* a : d->all(construct::PragmaArgument))
                        completed.insert(name_text(a->children.back()->source));
        }
        // (a generic subprogram completed (by an expression function, in a package specification))
        auto const generic_completed = [](node* aGeneric)
        {
            return aGeneric->ent != nullptr && aGeneric->ent->sub != nullptr && (aGeneric->ent->sub->body != nullptr || 
                aGeneric->ent->imported || aGeneric->ent->sub->op != intrinsic::None || aGeneric->ent->convention == "intrinsic");
        };
        auto const requires_body = [&](node* aSpec) { return package_requires_body(aSpec); };
        for (auto* d : aDeclarations->children)
        {
            auto const name = name_of(d);
            if (name.empty() || completed.contains(name))
                continue;
            bool required = false;
            if ((d->is(construct::SubprogramDeclaration) || d->is(construct::Generic)) && d->ent != nullptr && !d->ent->imported && d->ent->sub != nullptr &&
                !d->ent->sub->is_abstract && d->ent->sub->body == nullptr && d->ent->sub->op == intrinsic::None && d->ent->convention != "intrinsic")
                required = true;
            else if (d->is(construct::Generic) && d->first(construct::Package) != nullptr)
                required = requires_body(d->first(construct::Package));
            else if (d->is(construct::Generic))
                required = !generic_completed(d);
            else if (d->is(construct::Package) && requires_body(d))
                required = true;
            else if (d->is(construct::TaskType) || d->is(construct::TaskSingle) || d->is(construct::ProtectedType) || d->is(construct::ProtectedSingle))
                required = true;
            // (not when a homograph is declared in the completions' part (that error is reported there))
            if (required && d->is(construct::SubprogramDeclaration) && d->ent != nullptr && d->ent->sub != nullptr && aCompletions != aDeclarations)
                for (auto* c : aCompletions->children)
                    if (c->is(construct::SubprogramDeclaration) && c->ent != nullptr && c->ent != d->ent && c->ent->sub != nullptr && c->ent->name == d->ent->name &&
                        type_conformant(*c->ent->sub, *d->ent->sub))
                        required = false;
            if (required)
                recover([&]() { error(d->is(construct::Package) || d->is(construct::Generic) ? end_position(d) : d, "\"" + name + "\" requires a completion " + 
                    (aPartition ? "(a body) in the partition (10.2(5))" : "in the same declarative part")); });
        }
    }

    bool program::requires_overriding(entity* aOp, ada_type* aType)
    {
        // (an inherited subprogram of a nonabstract tagged type that must be overridden (3.9.3(6/2)): one that is abstract (or
        // implicitly abstract), or a function with a controlling result)
        auto* t = aType->base_type();
        if (t->kind == type_kind::Private && t->full != nullptr)
            t = t->full->base_type();
        if (aOp == nullptr || aOp->sub == nullptr || !aOp->sub->inherited || !t->tagged || abstract_type(t))
            return false;
        // (primitive subprograms (not those of a type declared in a declarative part, which are not inherited (3.2.3(6/5))))
        if (auto* origin = aOp->sub->parent_operation; origin != nullptr && origin->scope != nullptr &&
            (!origin->scope->is_package || origin->scope->package_body))
            return false;
        // (a function with a controlling result (or access result) inherited by an extension that is not a null extension
        // (3.9.3(6/2)))
        auto* parentView = t->parent != nullptr ? t->parent->view()->base_type()->view() : nullptr;
        bool const nullExtension = parentView == nullptr || parentView->components.size() == t->components.size();
        // (a controlling access result: even for a null extension)
        auto const controllingResult = [&](entity* aOp)
        {
            auto* r = aOp->sub->result;
            if (r == nullptr)
                return false;
            bool const access = r->anonymous_access && r->designated != nullptr;
            if (access)
                r = r->designated;
            else if (nullExtension)
                return false;
            auto* b = r->base_type();
            return b == t || b == t->partial || (t->partial != nullptr && b == t->partial->base_type());
        };
        // (an inherited function with a controlling result inherited by an abstract type: abstract (3.9.3(4/3)), and so are
        // the operations inheriting it)
        auto const abstractOperation = [&](entity* aOp)
        {
            if (aOp->sub->is_abstract)
                return true;
            if (aOp->kind != entity_kind::Function || aOp->sub->result == nullptr || aOp->sub->parent_operation == nullptr)
                return false;
            // (the ancestors that inherit it (not overriding it))
            for (auto* a = t->parent != nullptr ? t->parent->view()->base_type()->view() : nullptr; a != nullptr;
                a = a->parent != nullptr ? a->parent->view()->base_type()->view() : nullptr)
            {
                entity* own = nullptr;
                for (auto* list : { &a->primitives, a->partial != nullptr ? &a->partial->base_type()->primitives : nullptr })
                    if (list != nullptr)
                        for (auto* op : *list)
                            if (op == aOp->sub->parent_operation || (op->sub != nullptr && op->sub->inherited && op->sub->parent_operation == aOp->sub->parent_operation))
                                own = op;
                if (own == nullptr || !own->sub->inherited)
                    return false;
                auto* r = own->sub->result;
                if (r != nullptr && r->anonymous_access && r->designated != nullptr)
                    r = r->designated;
                if (r != nullptr && (abstract_type(a) || (a->partial != nullptr && abstract_type(a->partial))) &&
                    (r->base_type() == a || (a->partial != nullptr && r->base_type() == a->partial->base_type())))
                    return true;
            }
            return false;
        };
        return abstractOperation(aOp) || (aOp->kind == entity_kind::Function && controllingResult(aOp));
    }

    void program::overriding_check(region* aRegion)
    {
        // (a nonabstract tagged type declared in the region: its inherited abstract subprograms overridden (3.9.3(6/2)))
        if (iInstanceBodies > 0)
            return;
        for (auto* e : aRegion->declarations)
        {
            if (e->kind != entity_kind::Type || e->type == nullptr || e->decl == nullptr || !e->visible_in_body)
                continue;
            auto* t = e->type->base_type();
            if (t->kind == type_kind::Private && t->full != nullptr)
                t = t->full->base_type();
            // (a record extension of a formal private extension (in the generic): of a private kind, but an extension)
            auto const recordExtension = [&]() {
                auto* declaration = e->decl->parent;
                auto* derived = declaration == nullptr ? nullptr : declaration->is(construct::TypeDerived) ? declaration : declaration->first(construct::TypeDerived);
                return derived != nullptr && derived->first(construct::Extension) != nullptr;
            };
            if (!t->tagged || (t->kind != type_kind::Record && !(t->kind == type_kind::Private && t->full == nullptr && recordExtension())) ||
                abstract_type(t) || abstract_type(e->type) || t->is_formal || t->decl != e)
                continue;
            auto const overridden = [&](entity* aInherited)
            {
                for (auto* list : { &t->primitives, t->partial != nullptr ? &t->partial->base_type()->primitives : nullptr })
                    if (list != nullptr)
                        for (auto* op : *list)
                            if (op != aInherited && op->name == aInherited->name && op->sub != nullptr && !op->sub->inherited && !op->sub->is_abstract &&
                                type_conformant(*op->sub, *aInherited->sub))
                                return true;
                // (an interface's abstract operation: implemented by a nonabstract one inherited from the parent (8.3(9.1/2)))
                if (auto* from = aInherited->sub->derived_from; from != nullptr && from->view()->base_type()->view()->kind == type_kind::Interface)
                    for (auto* op : t->primitives)
                        if (op != aInherited && op->name == aInherited->name && op->sub != nullptr && op->sub->inherited && !op->sub->is_abstract &&
                            op->sub->derived_from != nullptr && op->sub->derived_from->view()->base_type()->view()->kind != type_kind::Interface &&
                            type_conformant(*op->sub, *aInherited->sub))
                            return true;
                return false;
            };
            for (auto* op : t->primitives)
                if (op->sub != nullptr && op->sub->inherited && !overridden(op) && requires_overriding(op, t))
                    recover([&]() { error(e->decl, "the inherited subprogram \"" + op->name + "\" must be overridden (the type \"" +
                        e->name + "\" is not abstract)"); });
            // (a type whose partial view is untagged or abstract: its explicitly declared abstract primitive subprograms (3.9.3(3/2)))
            if (t->partial != nullptr && (t->partial->base_type()->untagged_partial || t->partial->base_type()->abstract))
                for (auto* op : t->primitives)
                    if (op->sub != nullptr && !op->sub->inherited && op->sub->is_abstract && op->decl != nullptr)
                        recover([&]() { error(op->decl, "an abstract primitive subprogram of a tagged type requires the type to be abstract"); });
            // (a type whose partial view is untagged: its primitive subprograms not dispatching operations of another tagged type too
            // (3.9.2(12)))
            if (t->partial != nullptr && t->partial->base_type()->untagged_partial)
                for (auto* op : t->primitives)
                    if (op->sub != nullptr && !op->sub->inherited && op->decl != nullptr && op->sub->op == intrinsic::None)
                    {
                        bool other = false;
                        auto const consider = [&](ada_type* aType)
                        {
                            if (aType == nullptr)
                                return;
                            if (aType->anonymous_access && aType->designated != nullptr)
                                aType = aType->designated;
                            auto* b = aType->base_type();
                            auto* partial = b->partial != nullptr ? b->partial->base_type() : b;
                            auto* full = partial->full != nullptr ? partial->full->base_type() : b;
                            if (full == t || partial == t->partial->base_type() || b->kind == type_kind::ClassWide || !full->tagged)
                                return;
                            for (auto* list : { &partial->primitives, &full->primitives })
                                if (std::find(list->begin(), list->end(), op) != list->end())
                                    other = true;
                        };
                        for (auto const& p : op->sub->parameters)
                            consider(p.type);
                        consider(op->sub->result);
                        if (other)
                            recover([&]() { error(op->decl, "a subprogram cannot be a dispatching operation of more than one tagged type"); });
                    }
        }
    }

    bool program::full_declaration_given(node* aDecl, std::vector<node*> const& aParts)
    {
        // (a full type declaration for the type that aDecl declares in one of the parts (it failed: its errors are reported))
        auto* defining = aDecl->first(construct::Defining);
        if (defining == nullptr)
            return false;
        auto const name = name_text(defining->source);
        for (auto* part : aParts)
            if (part != nullptr)
                for (auto* d : part->children)
                    if (d != aDecl && (d->is(construct::Type) || d->is(construct::TaskType) || d->is(construct::ProtectedType)))
                        if (auto* other = d->first(construct::Defining); other != nullptr && name_text(other->source) == name)
                            return true;
        return false;
    }

    void program::incomplete_check(node* aDeclarations, std::string const& aWhere, node* aAt)
    {
        if (aDeclarations == nullptr || iInstanceBodies > 0)
            return;
        for (auto* d : aDeclarations->children)
            if (d->is(construct::TypeIncomplete) && d->ent != nullptr && d->ent->decl == d->first(construct::Defining) && d->ent->type != nullptr &&
                d->ent->type->incomplete && d->ent->type->full == nullptr && !d->ent->completed && !full_declaration_given(d, { aDeclarations }))
                recover([&]() { error(aAt != nullptr ? aAt : d, "the incomplete type \"" + d->ent->name + "\" is not completed " + aWhere); });
    }

    void program::current_instance_components(node* aExpression, entity* aType)
    {
        // (in an aspect of a record type: its components' (and discriminants') names directly visible, denoting the current
        // instance's (rewritten as selected components of it), 13.1.1(12/3))
        auto* v = aType->type != nullptr ? aType->type->view()->base_type()->view() : nullptr;
        auto* defining = aType->decl == nullptr ? nullptr : aType->decl->is(construct::Defining) ? aType->decl : aType->decl->first(construct::Defining);
        if (v == nullptr || v->kind != type_kind::Record || defining == nullptr)
            return;
        std::set<std::string> names;
        for (auto const& c : v->components)
            if (c.ent != nullptr)
                names.insert(c.ent->name);
        if (names.empty())
            return;
        std::function<void(node*)> rewrite = [&](node* aNode)
        {
            for (std::size_t i = 0u; i < aNode->children.size(); ++i)
            {
                auto* c = aNode->children[i];
                if (c->is(construct::Choices) || (aNode->is(construct::Selected) && i > 0u))
                    continue;
                if (c->is(construct::Identifier) && names.contains(name_text(c->source)))
                {
                    auto* instance = clone(c);
                    instance->source = defining->source;
                    instance->file = defining->file;
                    auto* selected = clone(c);
                    selected->kind = construct::Selected;
                    selected->children = { instance, c };
                    instance->parent = selected;
                    c->parent = selected;
                    selected->parent = aNode;
                    aNode->children[i] = selected;
                    continue;
                }
                rewrite(c);
            }
        };
        rewrite(aExpression);
    }

    void program::aspect_names_check(entity* aType, node* aExpression, region* aRegion)
    {
        // (a type's aspect's expression: its names of declarations before the type's freezing point (13.1.1(13/3)), not of deferred
        // constants (13.14(7.2/3)))
        std::function<void(node*)> names = [&](node* aNode)
        {
            if (auto* e = aNode->ent; e != nullptr && e->decl != nullptr && e->decl->parent != nullptr && e->scope == aRegion)
            {
                if (e->kind == entity_kind::Constant && e->completion != nullptr && e->completion != e)
                    error(aNode, "the aspect's expression cannot name a deferred constant (13.14(7.2/3))");
                // (or at it: an object of the type)
                if (frozen_before(aType->type, e->decl->parent) || (e->is_object() && e->type != nullptr && e->type->base_type() == aType->type->base_type()))
                    error(aNode, "the aspect's expression names a declaration after the type's freezing point (13.1.1(13/3))");
            }
            // (resolved where it was given (a static expression): the same at the end of the declarative part (13.1.1(13/3)))
            if (auto* e = aNode->ent; e != nullptr && aNode->is(construct::Identifier) && e->scope != aRegion && e->sub == nullptr)
                for (auto* other : visible(name_text(aNode->source), aRegion))
                    if (other != e && other->scope == aRegion && other->sub == nullptr)
                        error(aNode, "the aspect's expression denotes a different declaration at the end of the declarative part (13.1.1(13/3))");
            for (auto* c : aNode->children)
                if (c != nullptr)
                    names(c);
        };
        names(aExpression);
    }

    void program::representation_aspect_check(entity* aType, std::string const& aAspect, node* aValue, region* aRegion)
    {
        // (Size, Alignment, Component_Size of a type: of a first subtype, not a partial view (13.1(8/5), (9/5)); a static nonnegative
        // integer expression (13.3(25/2), (48), (70)) that does not freeze the type itself (13.1(9.2/5)); its names (aspect_names_check))
        if (aAspect == "component_size")
            resolve_in_class(aValue, aRegion, [](ada_type const* x) { return x->is_integer(); });
        if (aType->kind == entity_kind::Subtype)
            error(aValue, "a representation aspect of a type can be specified only for a first subtype (13.1(8/5))");
        if (aType->type->kind == type_kind::Private && !aType->type->is_formal)
            error(aValue, "a representation aspect cannot be specified for a partial view (13.1(9/5))");
        std::function<bool(node*)> freezes = [&](node* aNode)
        {
            if (aNode->ent != nullptr && aNode->ent->is_type() && aNode->ent->type != nullptr && aNode->ent->type->base_type() == aType->type->base_type())
                return true;
            for (auto* c : aNode->children)
                if (c != nullptr && freezes(c))
                    return true;
            return false;
        };
        if (freezes(aValue))
            error(aValue, "the aspect's expression freezes the type itself (13.1(9.2/5))");
        aspect_names_check(aType, aValue, aRegion);
        // (Small: a static positive real not greater than the delta (3.5.10(2/1)))
        if (aAspect == "small")
        {
            if (!static_expression(aValue))
                error(aValue, "the expression of a Small aspect must be static");
            auto const v = evaluate(aValue, aRegion);
            auto* tv = aType->type->view();
            if (v.known() && (v.real().sign() <= 0 || (tv->kind == type_kind::Fixed && tv->delta.sign() > 0 && v.real() > tv->delta)))
                error(aValue, "the Small must be positive and not greater than the delta (3.5.10(2/1))");
            return;
        }
        bool integer = false;
        for (auto const& o : interpretations(aValue, aRegion))
            integer = integer || (o.type != nullptr && o.type->view()->is_integer());
        if (!integer || !static_expression(aValue))
            error(aValue, "the aspect's value must be a static integer expression");
        if (auto v = evaluate(aValue, aRegion); v.is_integer() && v.integer().negative())
            error(aValue, "the aspect's value must be nonnegative");
    }

    void program::resolve_deferred_aspects(region* aRegion)
    {
        // (aspects whose expressions name later declarations: resolved at the end of the declarative part (their freezing point))
        auto deferred = std::move(iDeferredAspects[aRegion]);
        iDeferredAspects.erase(aRegion);
        // (the checks of representation aspects: after their resolution)
        std::stable_partition(deferred.begin(), deferred.end(), [](deferred_aspect const& a) { return !a.name.starts_with("check'"); });
        for (auto const& [type, name, value, ent, typeEntity] : deferred)
            recover([&, type = type, name = name, value = value, ent = ent, typeEntity = typeEntity]()
            {
                if (name == "check'mantissa")
                {
                    auto* base = type->base_type()->view();
                    if (type->low.known() && type->high.known() && base->small.sign() > 0)
                    {
                        auto const magnitude = [](rational const& r) { return r.sign() < 0 ? -r : r; };
                        auto const largest = std::max(magnitude(type->low.real()), magnitude(type->high.real()));
                        if (largest / base->small > rational{ bigint::pow(bigint{ 2 }, 63u) })
                            error(value, "the fixed point type requires more than System.Max_Mantissa bits (3.5.9(12))");
                    }
                    return;
                }
                if (name.starts_with("indexing'"))
                {
                    // (the functions that the name denotes: declared immediately in the same declaration list as the type, each with at least
                    // two parameters, the first of the type (or its class-wide type, or an access to either: to a constant for Constant_Indexing,
                    // to a variable for Variable_Indexing); Variable_Indexing's: returning a reference type with an access-to-variable
                    // discriminant (4.1.6(3/3, 4/3)))
                    bool const variable = name == "indexing'variable_indexing";
                    std::vector<entity*> functions;
                    if (value->is(construct::Identifier))
                        for (auto [i, end] = aRegion->names.equal_range(name_text(value->source)); i != end; ++i)
                            if (i->second->kind == entity_kind::Function && i->second->sub != nullptr && !i->second->sub->inherited)
                                functions.push_back(i->second);     // (not another type's inherited operation)
                    auto* t = type->base_type();
                    auto const ofType = [&](ada_type* aParameter)
                    {
                        if (aParameter == nullptr)
                            return false;
                        auto* v = aParameter->view();
                        if (v->kind == type_kind::Access && v->anonymous_access && v->designated != nullptr)
                        {
                            if (v->access_constant != !variable)
                                return false;
                            v = v->designated->view();
                        }
                        if (v->kind == type_kind::ClassWide && v->specific != nullptr)
                            v = v->specific->view();
                        return v->base_type()->view() == t->view() || v->base_type() == t;
                    };
                    if (functions.empty())
                        error(value, "the name of aspect " + name.substr(9u) + " must denote functions declared in the same declaration list as the type (4.1.6(3/3))");
                    for (auto* f : functions)
                    {
                        if (f->sub->parameters.size() < 2u)
                            error(value, "a function named by aspect " + name.substr(9u) + " must have at least two parameters (4.1.6(3/3))");
                        if (!ofType(f->sub->parameters.front().type))
                            error(value, "the first parameter of a function named by aspect " + name.substr(9u) + " must be of the type, its class-wide type, or an "
                                "access to either " + std::string{ variable ? "variable" : "constant" } + " (4.1.6(3/3))");
                        if (variable)
                        {
                            bool reference = false;
                            if (auto* r = f->sub->result; r != nullptr)
                                if (auto* aspect = type_aspect(r, "implicit_dereference"); aspect != nullptr)
                                    for (auto const& c : r->view()->components)
                                        if (c.discriminant && c.ent != nullptr && c.ent->name == name_text(aspect->source) && c.type != nullptr &&
                                            c.type->view()->kind == type_kind::Access && !c.type->view()->access_constant)
                                            reference = true;
                            if (!reference)
                                error(value, "a function named by aspect Variable_Indexing must return a reference type with an access-to-variable discriminant (4.1.6(4/3))");
                        }
                    }
                    return;
                }
                if (name.starts_with("check'"))
                {
                    if (typeEntity != nullptr && iInstanceBodies == 0)
                        representation_aspect_check(typeEntity, name.substr(6u), value, aRegion);
                    return;
                }
                if (name == "default_value" || name == "default_component_value")
                {
                    resolve(value, name == "default_value" ? type : type->view()->component, aRegion);
                    // (of a scalar type (an array type whose components are scalar) declared by a full type declaration (3.5(56.2/3),
                    // 3.6(22.2/3)); static (56.3/3); not freezing the type itself (13.1(9.2/5)); its names (aspect_names_check))
                    if (typeEntity != nullptr && iInstanceBodies == 0)
                    {
                        auto* v = type->view();
                        if (typeEntity->kind == entity_kind::Subtype || type->kind == type_kind::Private)
                            error(value, "the aspect " + name + " can be specified only by a full type declaration");
                        if (name == "default_value" ? !v->is_scalar() : (v->kind != type_kind::Array || v->component == nullptr || !v->component->view()->is_scalar()))
                            error(value, name == "default_value" ? "Default_Value can be specified only for a scalar type (3.5(56.2/3))" :
                                "Default_Component_Value can be specified only for an array type with scalar components (3.6(22.2/3))");
                        std::function<bool(node*)> freezes = [&](node* aNode)
                        {
                            if ((aNode->is(construct::Attribute) || aNode->is(construct::Qualified) || aNode->is(construct::Apply)) && !aNode->children.empty() &&
                                !subtype_attribute(aNode) && aNode->child(0)->ent != nullptr && aNode->child(0)->ent->is_type() &&
                                aNode->child(0)->ent->type != nullptr && aNode->child(0)->ent->type->base_type() == type->base_type())
                                return true;
                            for (auto* c : aNode->children)
                                if (c != nullptr && freezes(c))
                                    return true;
                            return false;
                        };
                        if (freezes(value))
                            error(value, "the aspect's expression freezes the type itself (13.1(9.2/5))");
                        aspect_names_check(typeEntity, value, aRegion);
                        if (!static_expression(value))
                            error(value, "the aspect " + name + " must be static (3.5(56.3/3))");
                    }
                    return;
                }
                if (typeEntity != nullptr && name == "storage_pool")
                {
                    resolve(value, nullptr, aRegion);
                    storage_pool_check(typeEntity->type, value, aRegion);
                    return;
                }
                if (typeEntity != nullptr && name == "stream_size")
                {
                    ada_type* integer = nullptr;
                    std::size_t count = 0u;
                    for (auto const& i : interpretations(value, aRegion))
                        if (i.type != nullptr && i.type->view()->is_integer() && !i.type->is_universal() && i.type != integer)
                        {
                            integer = i.type;
                            ++count;
                        }
                    resolve(value, count == 1u ? integer : nullptr, aRegion);
                    if (iInstanceBodies != 0)
                        return;
                    if (typeEntity->kind == entity_kind::Subtype)
                        error(value, "Stream_Size can be specified only for a first subtype (13.1(8.1/3))");
                    aspect_names_check(typeEntity, value, aRegion);
                    stream_size_check(typeEntity->type, value, value, aRegion);
                    return;
                }
                if (typeEntity != nullptr && (name == "read" || name == "write" || name == "input" || name == "output" || name == "read'class" ||
                    name == "write'class" || name == "input'class" || name == "output'class"))
                {
                    // (a stream attribute's subprogram: of a first subtype (13.1(8.1/3)))
                    if (iInstanceBodies != 0)
                        return;
                    if (typeEntity->kind == entity_kind::Subtype)
                        error(value, "a stream attribute can be specified only for a first subtype (13.1(8.1/3))");
                    bool const classWide = name.ends_with("'class");
                    stream_subprogram_check(typeEntity->type, classWide ? name.substr(0u, name.size() - 6u) : name, classWide, value, aRegion);
                    return;
                }
                if (name == "alignment")
                {
                    // (a type's or an object's alignment)
                    resolve(value, nullptr, aRegion);
                    if (auto v = evaluate(value, aRegion); v.is_integer())
                    {
                        if (ent != nullptr)
                            ent->alignment_clause = v.integer().to_uint64();
                        else if (type != nullptr)
                            type->alignment_clause = v.integer().to_uint64();
                    }
                    return;
                }
                if (typeEntity != nullptr && (name == "static_predicate" || name == "dynamic_predicate" || name == "predicate"))
                {
                    // (a predicate's function: its parameter is the current instance (named as the subtype is))
                    auto* predicate = type->predicate;
                    while (predicate != nullptr && predicate->expression != value)
                        predicate = predicate->parent;
                    if (predicate == nullptr)
                        return;
                    current_instance_components(value, typeEntity);
                    auto* f = new_entity(entity_kind::Function, typeEntity->name + "%predicate", nullptr, aRegion);
                    auto* r = new_region(f, aRegion);
                    r->is_subprogram = true;
                    r->level = aRegion->level + 1;
                    auto* instance = new_entity(entity_kind::Parameter, typeEntity->name, nullptr, r);
                    instance->type = type;
                    r->add(instance);
                    parameter_info parameter;
                    parameter.ent = instance;
                    parameter.type = type;
                    f->sub->parameters.push_back(parameter);
                    f->sub->result = iBoolean;
                    f->sub->expression = value;
                    f->sub->level = r->level;
                    f->scope = aRegion;
                    condition(value, r);
                    predicate->function = f;
                    // (a static predicate: of a scalar subtype, predicate-static (3.2.4(15/5)-(22/5)))
                    if (predicate->is_static && iInstanceBodies == 0)
                    {
                        if (!type->view()->is_scalar())
                            recover([&]() { error(value, "a static predicate can be specified only for a scalar subtype"); });
                        else if (!predicate_static(value, instance))
                            recover([&]() { error(value, "the expression of a static predicate must be predicate-static"); });
                    }
                    iSubprogramsToGenerate.push_back(f);
                    return;
                }
                if (typeEntity != nullptr && (name == "type_invariant" || name == "type_invariant'class" || name == "invariant"))
                {
                    // (a type invariant's function: its parameter is the current instance (named as the type is))
                    current_instance_components(value, typeEntity);
                    auto* f = new_entity(entity_kind::Function, typeEntity->name + "%invariant", nullptr, aRegion);
                    auto* r = new_region(f, aRegion);
                    r->is_subprogram = true;
                    r->level = aRegion->level + 1;
                    auto* instance = new_entity(entity_kind::Parameter, typeEntity->name, nullptr, r);
                    instance->type = typeEntity->type;
                    r->add(instance);
                    parameter_info parameter;
                    parameter.ent = instance;
                    parameter.type = typeEntity->type;
                    f->sub->parameters.push_back(parameter);
                    f->sub->result = iBoolean;
                    f->sub->expression = value;
                    f->sub->level = r->level;
                    f->scope = aRegion;
                    condition(value, r);
                    auto const entry = std::make_pair(f, name == "type_invariant'class");
                    typeEntity->type->invariants.push_back(entry);
                    if (typeEntity->type->view() != typeEntity->type)
                        typeEntity->type->view()->invariants.push_back(entry);
                    iSubprogramsToGenerate.push_back(f);
                    return;
                }
                if (typeEntity != nullptr && name == "predicate_failure")
                {
                    // (a failed predicate's Predicate_Failure: a function of the current instance (its message, or it raises), 3.2.4(31.1/5))
                    if (type->predicate != nullptr)
                    {
                        type->predicate->failure = value;
                        auto* f = new_entity(entity_kind::Function, typeEntity->name + "%predicate_failure", nullptr, aRegion);
                        auto* r = new_region(f, aRegion);
                        r->is_subprogram = true;
                        r->level = aRegion->level + 1;
                        auto* instance = new_entity(entity_kind::Parameter, typeEntity->name, nullptr, r);
                        instance->type = type;
                        r->add(instance);
                        parameter_info parameter;
                        parameter.ent = instance;
                        parameter.type = type;
                        f->sub->parameters.push_back(parameter);
                        f->sub->result = iString;
                        f->sub->expression = value;
                        f->sub->level = r->level;
                        f->scope = aRegion;
                        resolve(value, iString, r);
                        type->predicate->failure_function = f;
                        iSubprogramsToGenerate.push_back(f);
                    }
                    return;
                }
                if (ent != nullptr)
                {
                    // (a subprogram's precondition or postcondition (the latter can name its result))
                    // (a class-wide one: of a primitive subprogram of a tagged type (6.1.1(5/3)))
                    // (declared in a package body (or other body): not primitive)
                    if ((name == "pre'class" || name == "post'class") && iInstanceBodies == 0 && ent->sub != nullptr && ent->sub->controlling != nullptr &&
                        ent->scope != nullptr && (ent->scope->package_body || ent->scope->subprogram_body || ent->scope->is_subprogram))
                        recover([&]() { error(value, "aspect " + name + " can be specified only for a primitive subprogram of a tagged type"); });
                    else if ((name == "pre'class" || name == "post'class") && iInstanceBodies == 0 && ent->sub != nullptr && ent->sub->controlling == nullptr &&
                        [&]()
                        {
                            // (a task or protected type with progenitors is tagged)
                            for (auto const& p : ent->sub->parameters)
                                if (auto* pt = p.type != nullptr ? p.type->view()->base_type()->view() : nullptr; pt != nullptr &&
                                    (pt->kind == type_kind::Task || pt->kind == type_kind::Protected) && !pt->interfaces.empty() &&
                                    pt->declared_in == ent->scope)
                                    return false;
                            return true;
                        }())
                        recover([&]() { error(value, "aspect " + name + " can be specified only for a primitive subprogram of a tagged type"); });
                    auto* saved = iCurrentSubprogram;
                    auto* savedClassWide = iClassWideContract;
                    auto* savedPost = iPostconditionOf;
                    if (name == "post" || name == "post'class")
                    {
                        iCurrentSubprogram = ent;
                        iPostconditionOf = ent;
                    }
                    // (a class-wide one: calls with its controlling formals as controlling operands dispatch (6.1.1(7/4)))
                    iClassWideContract = name == "pre'class" || name == "post'class" ? ent : nullptr;
                    condition(value, ent->inner != nullptr ? ent->inner : aRegion);
                    iCurrentSubprogram = saved;
                    iClassWideContract = savedClassWide;
                    iPostconditionOf = savedPost;
                    return;
                }
                resolve(value, nullptr, aRegion);
                if (name == "small" && !static_expression(value))
                    recover([&]() { error(value, "the expression of a Small aspect must be static"); });
                auto v = evaluate(value, aRegion);
                if (name == "small" && v.known())
                {
                    type->base_type()->small = v.real();
                    type->small = v.real();
                    fixed_base_range(type);
                }
                else if (name == "size" && v.is_integer())
                    type->size_clause = v.integer().to_uint64();
            });
    }

    void program::end_name_check(node* aDecl)
    {
        // (the designator after end: the unit's, the entry's, the statement's (6.3(3), 7.1(3), 7.2(2), 9.1(3), 9.4(3), 9.5.2(4),
        // 5.5(5), 5.6(3)))
        auto* end = aDecl->first(construct::End);
        for (auto* c : aDecl->children)
            if (end == nullptr && (c->is(construct::TaskDefinition) || c->is(construct::ProtectedDefinition)))
                end = c->first(construct::End);
        node* defining = nullptr;
        if (aDecl->is(construct::Loop) || aDecl->is(construct::Block))
        {
            // (a statement identifier: repeated after end (5.5(5), 5.6(3)))
            auto* word = aDecl->first(construct::Word);
            if (word == nullptr && end != nullptr)
                error(end, "an end name is allowed only after a named statement");
            if (word != nullptr && end == nullptr)
                error(aDecl, "the statement's name must be repeated after end");
            defining = word;
        }
        else if (aDecl->is(construct::Accept))
            defining = aDecl->child(0)->is(construct::Apply) ? aDecl->child(0)->child(0) : aDecl->child(0);
        else
        {
            defining = aDecl->first(construct::Defining);
            if (defining == nullptr)
                if (auto* spec = specification_of(aDecl); spec != nullptr && !spec->children.empty())
                    defining = spec->child(0);
        }
        if (end == nullptr || end->source.empty() || defining == nullptr)
            return;
        auto const normal = [](std::string_view aText)
        {
            std::string result;
            for (auto c : aText)
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                    result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return result;
        };
        auto name = normal(defining->source);
        if (defining->is(construct::Word) && !name.empty() && name.back() == ':')
            name.pop_back();
        if (normal(end->source) != name)
            error(end, "\"" + std::string{ end->source } + "\" does not match the name of the unit (\"" + std::string{ defining->source } + "\")");
    }

    void program::declaration(node* aDecl, region* aRegion)
    {
        recover([&]() { end_name_check(aDecl); });
        switch (aDecl->kind)
        {
        case construct::Object:
            object_declaration(aDecl, aRegion);
            break;
        case construct::NumberDeclaration:
            number_declaration(aDecl, aRegion);
            break;
        case construct::Type:
        case construct::TypeIncomplete:
        case construct::TypePrivate:
        case construct::TypePrivateExtension:
            type_declaration(aDecl, aRegion);
            break;
        case construct::TaskType:
        case construct::TaskSingle:
            task_declaration(aDecl, aRegion);
            break;
        case construct::ProtectedType:
        case construct::ProtectedSingle:
            protected_declaration(aDecl, aRegion);
            break;
        case construct::Subtype:
            subtype_declaration(aDecl, aRegion);
            break;
        case construct::SubprogramDeclaration:
        case construct::SubprogramAbstract:
        case construct::SubprogramNull:
        case construct::SubprogramExpression:
            subprogram_declaration(aDecl, aRegion);
            break;
        case construct::SubprogramBody:
            subprogram_body(aDecl, aRegion);
            break;
        case construct::Package:
            package_declaration(aDecl, aRegion);
            break;
        case construct::PackageBody:
            package_body(aDecl, aRegion);
            break;
        case construct::RenamingObject:
        case construct::RenamingException:
        case construct::RenamingPackage:
        case construct::RenamingSubprogram:
        case construct::RenamingGeneric:
            renaming_declaration(aDecl, aRegion);
            break;
        case construct::Exception:
            exception_declaration(aDecl, aRegion);
            break;
        case construct::Generic:
            generic_declaration(aDecl, aRegion);
            break;
        case construct::Instantiation:
            instantiation(aDecl, aRegion);
            break;
        case construct::Use:
        case construct::UseType:
            use_clause(aDecl, aRegion);
            break;
        case construct::Pragma:
            pragma(aDecl, aRegion);
            break;
        case construct::RepresentationAttribute:
        case construct::RepresentationEnumeration:
        case construct::RepresentationRecord:
        case construct::RepresentationAt:
            representation_clause(aDecl, aRegion);
            break;
        case construct::Stub:
            body_stub(aDecl, aRegion);
            break;
        case construct::TaskBody:
            task_body(aDecl, aRegion);
            break;
        case construct::ProtectedBody:
            protected_body(aDecl, aRegion);
            break;
        default:
            break;
        }
    }

    // --- packages

    void program::package_declaration(node* aDecl, region* aRegion, entity* aEntity)
    {
        auto* defining = aDecl->first(construct::Defining);
        entity* e = aEntity;
        if (e == nullptr)
            e = declare(entity_kind::Package, defining, aRegion);
        else
            defining->ent = e;
        aDecl->ent = e;
        register_library_unit(e);
        // (Pre and Post: not of a package (6.1.1))
        if (auto* aspects = aDecl->first(construct::Aspects); aspects != nullptr && !iInstancePackages.contains(aDecl))
            for (auto* aspect : aspects->children)
                if (auto const a = name_text(aspect->child(0)->source); a == "pre" || a == "post")
                    recover([&]() { error(aspect, "aspect " + a + " cannot be specified for a package"); });
        auto* r = e->inner != nullptr ? e->inner : new_region(e, aRegion);
        r->is_package = true;
        // (limited views of its child units (its own limited with clauses))
        if (auto views = iLimitedChildViews.find(full_name(e)); views != iLimitedChildViews.end())
            for (auto* view : views->second)
                r->names.emplace(view->name.substr(view->name.rfind('.') + 1u), view);
        auto* visibleDecls = aDecl->first(construct::Declarations);
        declarations(visibleDecls, r);
        // (the visible part's incomplete types: completed in it (3.10.1(3)))
        incomplete_check(visibleDecls, "in the visible part");
        auto* priv = aDecl->first(construct::Private);
        r->private_part = priv;
        if (priv != nullptr)
        {
            r->private_start = r->declarations.size();
            bool const saved = r->private_visible;
            r->private_visible = true;
            // (an instance's private part: not rechecked (12.3(11)))
            instance_body inPrivate{ *this, iInstancePackages.contains(aDecl), true };
            for (auto* d : priv->children)
                recover([&]() { declaration(d, r); });
            recover([&]() { resolve_deferred_aspects(r); });
            r->private_visible = saved;
            // (an incomplete type of the private part used in the profile of a primitive subprogram declared there: completed there
            // (3.10.1(9.3/2)))
            if (!iInstancePackages.contains(aDecl))
                for (auto* d : priv->children)
                    if (d->ent != nullptr && d->ent->sub != nullptr && (d->ent->kind == entity_kind::Procedure || d->ent->kind == entity_kind::Function))
                    {
                        auto const deferred = [&](ada_type* x)
                        {
                            if (x == nullptr)
                                return false;
                            if (x->anonymous_access && x->designated != nullptr)
                                x = x->designated;
                            x = x->base_type();
                            return x->kind == type_kind::Private && x->incomplete && x->full == nullptr && x->declared_in == r &&
                                x->decl != nullptr && !x->decl->completed;
                        };
                        bool bad = deferred(d->ent->sub->result);
                        for (auto const& p : d->ent->sub->parameters)
                            bad = bad || deferred(p.type);
                        if (bad)
                            recover([&]() { error(d, "an incomplete type used in a primitive subprogram's profile must be completed in the private part"); });
                    }
        }
        overriding_check(r);
        // (the primitive subprograms of private or incomplete types completed as tagged: of one tagged type only (3.9.2(12)))
        if (iInstanceBodies == 0 && iInstantiationSites.empty())
            for (auto* d : r->declarations)
            {
                if (d->sub == nullptr || (d->kind != entity_kind::Procedure && d->kind != entity_kind::Function) || d->sub->inherited ||
                    d->sub->op != intrinsic::None || d->decl == nullptr)
                    continue;
                std::vector<ada_type*> tagged;
                bool completed = false;
                auto const consider = [&](ada_type* aType)
                {
                    if (aType == nullptr)
                        return;
                    if (aType->anonymous_access && aType->designated != nullptr)
                        aType = aType->designated;
                    auto* b = aType->base_type();
                    if (b->kind == type_kind::ClassWide || b->declared_in != r)
                        return;
                    auto* partial = b->partial != nullptr ? b->partial->base_type() : b;
                    auto* full = partial->full != nullptr ? partial->full->base_type() : b;
                    if (!full->tagged || std::find(tagged.begin(), tagged.end(), partial) != tagged.end())
                        return;
                    tagged.push_back(partial);
                    // (a type whose tagged full declaration follows the subprogram's (completing a private or incomplete view))
                    auto const* fullDecl = full->decl != nullptr ? full->decl->decl : nullptr;
                    completed = completed || ((partial->kind == type_kind::Private || partial->incomplete) && !partial->tagged) ||
                        (fullDecl != nullptr && fullDecl->file == d->decl->file && fullDecl->source.data() > d->decl->source.data());
                };
                for (auto const& p : d->sub->parameters)
                    consider(p.type);
                consider(d->sub->result);
                if (tagged.size() > 1u && completed)
                    recover([&]() { error(d->decl, "a subprogram cannot be a primitive operation of two tagged types (3.9.2(12))"); });
            }
        // (deferred constants: completed by full constant declarations in the private part (7.4(4/3)), or imported)
        if (visibleDecls != nullptr)
            for (auto* d : visibleDecls->children)
                if (d->is(construct::Object) && d->has_flag("constant") && d->ent != nullptr && d->ent->kind == entity_kind::Constant &&
                    d->ent->initial == nullptr && !d->ent->completed && !d->ent->imported)
                    recover([&]() { error(d, "deferred constant \"" + d->ent->name + "\" is not completed in the private part"); });
                // (private types and private extensions: completed in the private part (7.3(4)))
                else if ((d->is(construct::TypePrivate) || d->is(construct::TypePrivateExtension) || (d->is(construct::Type) &&
                    d->first(construct::TypeDerived) != nullptr && d->first(construct::TypeDerived)->first(construct::Extension) != nullptr &&
                    d->first(construct::TypeDerived)->first(construct::Extension)->has_flag("private"))) && d->ent != nullptr &&
                    d->ent->decl == d->first(construct::Defining) && d->ent->type != nullptr && d->ent->type->full == nullptr &&
                    iInstanceBodies == 0 && !iInstancePackages.contains(aDecl) && !full_declaration_given(d, { priv }))
                    recover([&]() { error(d, "the private type \"" + d->ent->name + "\" is not completed in the private part"); });
        if (e->library)
            iPackageElaborations.emplace_back(aDecl, r);
    }

    void program::package_body(node* aDecl, region* aRegion)
    {
        auto* defining = aDecl->first(construct::Defining);
        auto const name = name_text(defining->source);
        auto const simple = name.substr(name.rfind('.') == std::string::npos ? 0u : name.rfind('.') + 1u);
        entity* e = nullptr;
        if (iDeclaringUnit != nullptr)
        {
            e = library_unit(iDeclaringUnit->name);
            if (e == nullptr)
            {
                e = declare(entity_kind::Package, defining, aRegion);
                new_region(e, aRegion)->is_package = true;
            }
            iDeclaringUnit->ent = e;
            iDeclaringUnit = nullptr;
        }
        else
        {
            // (the package's declaration: in the region, or in the specification of the package whose body the region is)
            for (auto* r = aRegion; r != nullptr && e == nullptr; r = r->owner != nullptr && r->parent != nullptr && r->parent->owner == r->owner ? r->parent : nullptr)
            {
                auto range = r->names.equal_range(simple);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->is_package())
                        e = i->second;
            }
            if (e == nullptr)
                error(aDecl, "package body \"" + simple + "\" has no declaration");
        }
        // (an instance: its body is the generic's (12.3(12)); not a package declaration that a body can complete (7.2(4)))
        if (e->generic != nullptr && iInstanceBodies == 0 && iInstantiationSites.empty())
            error(aDecl, "package body \"" + simple + "\" has no package declaration (\"" + simple + "\" is an instance)");
        defining->ent = e;
        aDecl->ent = e;
        // (a library package (or generic package) declaration that does not require a body: none allowed (7.2(4)))
        if (e->library && iInstanceBodies == 0 && iInstantiationSites.empty())
        {
            node* spec = e->decl;
            if (spec != nullptr && spec->is(construct::Generic))
                spec = spec->first(construct::Package);
            while (spec != nullptr && !spec->is(construct::Package))
                spec = spec->parent;
            if (spec != nullptr && !package_requires_body(spec))
                recover([&]() { error(aDecl, "package body \"" + simple + "\" is not allowed: its library package declaration does not require a body (7.2(4))"); });
        }
        if (e->kind == entity_kind::GenericPackage)
        {
            // (a generic package's body: analysed in its instances; the completions of its specification's declarations: here)
            node* spec = e->decl;
            if (spec != nullptr && spec->is(construct::Generic))
                spec = spec->first(construct::Package);
            while (spec != nullptr && !spec->is(construct::Package))
                spec = spec->parent;
            if (iInstanceBodies == 0 && iInstantiationSites.empty() && spec != nullptr)
                if (auto* body = aDecl->first(construct::Declarations))
                    for (auto* part : { spec->first(construct::Declarations), spec->first(construct::Private) })
                        recover([&]() { completion_check(part, body); });
            return;
        }
        auto* r = e->inner;
        // (a library package body compiled again (a later compilation of it replaces the earlier, 10.1.4(5)): its specification's
        // subprograms not yet completed)
        if (e->library && iLibraryBodies.contains(name) && iInstanceBodies == 0)
            for (auto* d : r->declarations)
                if (d->sub != nullptr && d->sub->body != nullptr && d->sub->body->parent != nullptr && d->sub->body->parent->parent != nullptr &&
                    d->sub->body->parent->parent->is(construct::PackageBody))
                    d->sub->body = nullptr;
        bool const saved = r->private_visible;
        r->private_visible = true;
        // (the body's declarations are in a region of their own whose parent is the package's (its declarations are visible))
        auto* bodyRegion = new_region(nullptr, r);
        bodyRegion->package_body = true;
        bodyRegion->is_package = true;
        bodyRegion->owner = e;
        if (!e->library && aRegion != r->parent)
            bodyRegion->enclosing = aRegion;
        e->inner = r;
        iBodyRegions[aDecl] = bodyRegion;
        iLibraryBodies[name] = bodyRegion;
        declarations(aDecl->first(construct::Declarations), bodyRegion);
        // (the private part's incomplete types: completed there or in the body (3.10.1(3)))
        incomplete_check(r->private_part, "in the private part or the package body",
            aDecl->first(construct::End) != nullptr ? aDecl->first(construct::End) : aDecl);
        if (auto* handled = aDecl->first(construct::Handled))
            statements(handled, bodyRegion);
        r->private_visible = saved;
        if (e->library)
            iPackageElaborations.emplace_back(aDecl, bodyRegion);
    }

    // --- subprograms

    void program::subprogram_specification(node* aSpec, entity* aEntity, region* aParameters)
    {
        auto& sub = *aEntity->sub;
        sub.spec = aSpec;
        sub.parameters.clear();
        // (incomplete views: in a declaration's profile; a body's parameters, if tagged (3.10.1(8.1/3), (8.3/2)))
        bool const body = aSpec->parent != nullptr && (aSpec->parent->is(construct::SubprogramBody) || aSpec->parent->is(construct::EntryBody));
        std::int32_t index = 0;
        for (auto* p : aSpec->children)
        {
            if (!p->is(construct::Parameter))
                continue;
            auto mode = parameter_mode::In;
            // (an access parameter: no mode (6.1(15/3)))
            if (p->first(construct::AccessDefinition) != nullptr)
            {
                if ((p->has_flag("in") || p->has_flag("out")) && iInstanceBodies == 0)
                    recover([&]() { error(p, "an access parameter cannot have a mode"); });
            }
            else if (p->has_flag("out"))
                mode = p->has_flag("in") ? parameter_mode::InOut : parameter_mode::Out;
            ada_type* type = nullptr;
            node* defaultExpression = nullptr;
            bool seenType = false;
            // (the parameters being declared: hidden from all visibility within their declaration, hiding their homographs (8.3(16)))
            std::vector<std::pair<std::string, entity*>> hidden;
            if (iInstanceBodies == 0)
                for (auto* d : p->all(construct::Defining))
                {
                    auto* h = new_entity(entity_kind::Parameter, name_text(d->source), d, aParameters);
                    h->declaring = true;
                    aParameters->names.emplace(h->name, h);
                    hidden.emplace_back(h->name, h);
                }
            struct unhide
            {
                region* r;
                std::vector<std::pair<std::string, entity*>>& hidden;
                void release()
                {
                    for (auto const& [name, h] : hidden)
                        for (auto [i, end] = r->names.equal_range(name); i != end; ++i)
                            if (i->second == h)
                            {
                                r->names.erase(i);
                                break;
                            }
                    hidden.clear();
                }
                ~unhide() { release(); }
            } unhiding{ aParameters, hidden };
            for (auto* c : p->children)
            {
                if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects))
                    continue;
                if (!seenType)
                {
                    if (c->is(construct::AccessDefinition))
                    {
                        type = access_definition(c, aParameters);
                        mode = parameter_mode::Access;
                        if (!c->has_flag("not null") && false)
                            ;
                    }
                    else
                    {
                        incomplete_use use{ *this, body ? incomplete_uses::TaggedOnly : incomplete_uses::Allowed };
                        // (a subtype mark: no constraint (6.1(15/3)))
                        if (c->is(construct::Apply) && iInstanceBodies == 0)
                            recover([&]() { error(c, "a parameter's subtype must be given by a subtype mark (without a constraint)"); });
                        // (an error in it: reported, the parameter of Integer (so the rest of the profile and the body are checked))
                        recover([&]() { type = subtype_mark(c->is(construct::Apply) && !c->children.empty() ? c->child(0) : c, aParameters); });
                        if (type == nullptr)
                        {
                            // (a name not to be used here (a private with's, 10.1.2(12/3)): its type, for the rest)
                            bool const saved = iPrivateWithVisible;
                            iPrivateWithVisible = true;
                            try
                            {
                                speculation guard{ *this };
                                type = subtype_mark(c->is(construct::Apply) && !c->children.empty() ? c->child(0) : c, aParameters);
                            }
                            catch (compile_error const&)
                            {
                            }
                            iPrivateWithVisible = saved;
                        }
                        if (type == nullptr)
                            type = iInteger;
                    }
                    seenType = true;
                }
                else
                    defaultExpression = c;
            }
            // (a null exclusion: a null-excluding subtype)
            if (type != nullptr && p->has_flag("not null") && p->first(construct::AccessDefinition) == nullptr)
                null_exclusion_check(type, p);
            if (type != nullptr && p->has_flag("not null") && type->view()->is_access() && !type->view()->not_null)
            {
                type = new_subtype(type->view());
                type->not_null = true;
            }
            // (the default expression: resolved where it is declared; of an in parameter only (6.1(19)))
            if (defaultExpression != nullptr && (mode == parameter_mode::Out || mode == parameter_mode::InOut) && iInstanceBodies == 0)
                recover([&]() { error(defaultExpression, "only an in parameter can have a default expression"); });
            if (defaultExpression != nullptr && type != nullptr && meaning_of(defaultExpression) == meaning::None)
            {
                recover([&]() { resolve(defaultExpression, type, aParameters); });
                others_check(defaultExpression, type);
                // (not naming a parameter of the same profile (they are not visible in the formal part, 8.3(16)))
                if (iInstanceBodies == 0)
                {
                    std::function<node*(node*)> named = [&](node* n) -> node*
                    {
                        if (n->ent != nullptr && n->ent->kind == entity_kind::Parameter && n->ent->scope == aParameters)
                            return n;
                        for (auto* c : n->children)
                            if (auto* x = named(c))
                                return x;
                        return nullptr;
                    };
                    if (auto* x = named(defaultExpression))
                        recover([&]() { error(x, "a default expression cannot name a parameter of the same formal part"); });
                }
            }
            unhiding.release();
            for (auto* d : p->all(construct::Defining))
            {
                // (a parameter of the same name as an earlier one: reported, then declared apart (so the rest of the profile is checked))
                entity* pe = nullptr;
                recover([&]() { pe = declare(entity_kind::Parameter, d, aParameters); });
                if (pe == nullptr)
                    pe = new_entity(entity_kind::Parameter, name_text(d->source), d, aParameters);
                pe->type = type;
                pe->index = index++;
                pe->aliased = p->has_flag("aliased");
                pe->aliased_declared = pe->aliased;
                parameter_info info;
                info.ent = pe;
                info.mode = mode;
                info.type = type;
                info.default_expression = defaultExpression;
                info.aliased = pe->aliased;
                info.not_null = p->has_flag("not null");
                sub.parameters.push_back(info);
            }
        }
        if (aSpec->is(construct::FunctionSpecification) || aSpec->first(construct::Result) != nullptr)
        {
            if (auto* result = aSpec->first(construct::Result))
            {
                for (auto* c : result->children)
                {
                    if (c->is(construct::Flag))
                        continue;
                    if (c->is(construct::AccessDefinition))
                    {
                        sub.result = access_definition(c, aParameters);
                        sub.result_access = true;
                    }
                    else
                    {
                        // (a subtype mark: no constraint (6.1(13/2)))
                        if (c->is(construct::Apply) && iInstanceBodies == 0)
                            recover([&]() { error(c, "a function's result subtype must be given by a subtype mark (without a constraint)"); });
                        incomplete_use use{ *this, body ? incomplete_uses::None : incomplete_uses::Allowed };
                        sub.result = subtype_mark(c->is(construct::Apply) && !c->children.empty() ? c->child(0) : c, aParameters);
                    }
                }
                sub.result_not_null = result->has_flag("not null");
                if (sub.result_not_null && result->first(construct::AccessDefinition) == nullptr && aSpec->parent != nullptr &&
                    !aSpec->parent->is(construct::SubprogramBody))
                    null_exclusion_check(sub.result, result);
            }
            aEntity->type = sub.result;
        }
        if (!aSpec->children.empty() && aSpec->child(0)->is(construct::DefiningOperator) && iInstanceBodies == 0)
            recover([&]() { operator_profile_check(aEntity, aSpec->child(0)); });
    }

    bool program::type_conformant(subprogram_info const& a, subprogram_info const& b)
    {
        if (a.parameters.size() != b.parameters.size())
            return false;
        if ((a.result == nullptr) != (b.result == nullptr))
            return false;
        auto const same = [&](ada_type* x, ada_type* y)
        {
            if (x == nullptr || y == nullptr)
                return x == y;
            if (x->anonymous_access && y->anonymous_access)
                return x->designated == nullptr || y->designated == nullptr || same_type(x->designated, y->designated);
            return same_type(x, y);
        };
        if (a.result != nullptr && !same(a.result, b.result))
            return false;
        for (std::size_t i = 0u; i < a.parameters.size(); ++i)
            if (!same(a.parameters[i].type, b.parameters[i].type))
                return false;
        return true;
    }

    bool program::formal_distinct(subprogram_info const& a, subprogram_info const& b)
    {
        // (profiles whose parameters' (or results') subtypes are different formal types of an instance: distinct declarations (in
        // the generic their types differ), even if the actuals are the same (12.3(11)))
        auto const view = [](ada_type* t) { return t != nullptr ? t->formal_view : nullptr; };
        auto const differ = [&](ada_type* x, ada_type* y) { return view(x) != nullptr && view(y) != nullptr && view(x) != view(y); };
        if (a.parameters.size() != b.parameters.size())
            return false;
        if (differ(a.result, b.result))
            return true;
        for (std::size_t i = 0u; i < a.parameters.size(); ++i)
            if (differ(a.parameters[i].type, b.parameters[i].type))
                return true;
        return false;
    }

    entity* program::find_completion(entity* aSubprogram, region* aRegion)
    {
        // (an earlier declaration in the region (or, for a body in a package body, in the package's declaration) of the same name
        // and a conforming profile, that has no body yet)
        for (auto* r = aRegion; r != nullptr; r = r->owner != nullptr && r->parent != nullptr && r->parent->owner == r->owner ? r->parent : nullptr)
        {
            auto range = r->names.equal_range(aSubprogram->name);
            for (auto i = range.first; i != range.second; ++i)
            {
                auto* other = i->second;
                if (other == aSubprogram || !other->is_subprogram() && !other->is_generic())
                    continue;
                if (other->sub == nullptr || other->sub->body != nullptr || other->sub->inherited)
                    continue;
                if (other->is_generic() && !aSubprogram->is_generic() && (other->kind == entity_kind::GenericProcedure) == (aSubprogram->kind == entity_kind::Procedure))
                    return other;
                if (type_conformant(*other->sub, *aSubprogram->sub) && !formal_distinct(*other->sub, *aSubprogram->sub))
                    return other;
            }
        }
        return nullptr;
    }

    entity* program::generic_completion(node* aSpec, region* aRegion)
    {
        // (a body (or expression function) that completes a generic subprogram declared earlier in the region)
        auto* defining = aSpec->child(0);
        auto const name = defining->is(construct::DefiningOperator) ? designator_of(defining) : name_text(defining->source);
        if (name.find('.') != std::string::npos)
            return nullptr;
        auto const kind = aSpec->is(construct::FunctionSpecification) ? entity_kind::GenericFunction : entity_kind::GenericProcedure;
        for (auto* r = aRegion; r != nullptr; r = r->owner != nullptr && r->parent != nullptr && r->parent->owner == r->owner ? r->parent : nullptr)
        {
            auto range = r->names.equal_range(name);
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->kind == kind && i->second->sub != nullptr && i->second->sub->body == nullptr)
                    return i->second;
        }
        return nullptr;
    }

    entity* program::subprogram_declaration(node* aDecl, region* aRegion)
    {
        auto* spec = specification_of(aDecl);
        auto* defining = spec->child(0);
        // (a library subprogram's designator: an identifier (a defining program unit name), not an operator symbol (10.1.1(3)))
        if (iDeclaringUnit != nullptr && defining->is(construct::DefiningOperator) && iInstanceBodies == 0)
            recover([&]() { error(defining, "a library subprogram's designator cannot be an operator symbol (10.1.1(3))"); });
        auto const kind = spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure;
        if (aDecl->is(construct::SubprogramExpression) || aDecl->is(construct::SubprogramNull))
            if (auto* g = generic_completion(spec, aRegion))
            {
                // (a generic subprogram's body: analysed in its instances)
                g->sub->body = aDecl;
                aDecl->ent = g;
                defining->ent = g;
                return g;
            }
        auto* e = declare(kind, defining, aRegion);
        aDecl->ent = e;
        register_library_unit(e);
        auto* parameters = new_region(e, aRegion);
        parameters->is_subprogram = true;
        parameters->level = aRegion->level + 1;
        subprogram_specification(spec, e, parameters);
        e->sub->level = parameters->level;
        e->sub->spec = spec;
        // (a homograph of an explicit declaration in the same declarative region (8.3(26/2)); not in an instance, where the actuals
        // can make the generic's declarations homographs (12.3(11)))
        if ((aDecl->is(construct::SubprogramDeclaration) || aDecl->is(construct::SubprogramAbstract)) && iInstanceBodies == 0 && !aRegion->generic_formals &&
            iInstantiationSites.empty())
            homograph_check(e, defining, aRegion);
        bool completion = false;
        if (aDecl->is(construct::SubprogramAbstract))
            e->sub->is_abstract = true;
        else if (aDecl->is(construct::SubprogramNull))
        {
            e->sub->is_null = true;
            e->sub->body = aDecl;
            // (a completion of an earlier declaration)
            if (auto* earlier = find_completion(e, aRegion))
            {
                aRegion->names.erase(std::find_if(aRegion->names.begin(), aRegion->names.end(), [&](auto const& p) { return p.second == e; }));
                aRegion->declarations.pop_back();
                recover([&]() { conformance_check(*earlier->sub, *e->sub, earlier->name, defining); });
                earlier->sub->is_null = true;
                earlier->sub->body = aDecl;
                earlier->sub->separate_body = true;
                earlier->inner = e->inner;
                for (std::size_t i = 0u; i < earlier->sub->parameters.size() && i < e->sub->parameters.size(); ++i)
                    e->sub->parameters[i].ent->renamed = earlier->sub->parameters[i].ent;
                earlier->sub->spec = spec;
                if (earlier->sub->spec_parameters.empty())
                    for (auto const& p : earlier->sub->parameters)
                        earlier->sub->spec_parameters.push_back(p.ent);
                earlier->sub->parameters = e->sub->parameters;
                aDecl->ent = earlier;
                e = earlier;
                completion = true;
            }
            iSubprogramsToGenerate.push_back(e);
        }
        else if (aDecl->is(construct::SubprogramExpression))
        {
            for (auto* c : aDecl->children)
                if (!c->is(construct::Flag) && !is_specification(c) && !c->is(construct::Aspects))
                    e->sub->expression = c;
            e->sub->body = aDecl;
            // (a completion of an earlier declaration)
            if (auto* earlier = find_completion(e, aRegion))
            {
                aRegion->names.erase(std::find_if(aRegion->names.begin(), aRegion->names.end(), [&](auto const& p) { return p.second == e; }));
                aRegion->declarations.pop_back();
                recover([&]() { conformance_check(*earlier->sub, *e->sub, earlier->name, defining); });
                earlier->sub->body = aDecl;
                earlier->sub->expression = e->sub->expression;
                earlier->sub->separate_body = true;     // (calls before the completion is elaborated: Program_Error)
                earlier->inner = e->inner;
                // (the parameters of the completion are the earlier declaration's)
                for (std::size_t i = 0u; i < earlier->sub->parameters.size() && i < e->sub->parameters.size(); ++i)
                    e->sub->parameters[i].ent->renamed = earlier->sub->parameters[i].ent;
                earlier->sub->spec = spec;
                if (earlier->sub->spec_parameters.empty())
                    for (auto const& p : earlier->sub->parameters)
                        earlier->sub->spec_parameters.push_back(p.ent);
                earlier->sub->parameters = e->sub->parameters;
                aDecl->ent = earlier;
                e = earlier;
                completion = true;
            }
            iSubprogramsToGenerate.push_back(e);
            resolve(e->sub->expression, e->sub->result, e->inner);
            limited_value_check(e->sub->expression, e->sub->result, e->inner);
            if (iSpeculation == 0)
                others_check(e->sub->expression, e->sub->result);   // (an unconstrained array result: no applicable index constraint)
        }
        if (completion)
            completion_aspects_check(aDecl);
        aspects(aDecl, e, aRegion);
        // (in a package's private part: an abstract primitive subprogram of an abstract type, or a function with a controlling
        // result of a tagged type, declared in the visible part only if overriding (3.9.3(10)))
        bool lateAbstract = false;
        if (iInstanceBodies == 0 && e->is_private && aRegion->is_package && !aRegion->package_body &&
            (e->sub->is_abstract || e->kind == entity_kind::Function))
        {
            auto const visibleType = [&](ada_type* aType) -> ada_type*
            {
                if (aType == nullptr)
                    return nullptr;
                if (aType->anonymous_access && aType->designated != nullptr)
                    aType = aType->designated;
                auto* b = aType->base_type();
                if (b->kind == type_kind::ClassWide || b->declared_in != aRegion || b->decl == nullptr || b->decl->is_private)
                    return nullptr;
                return b;
            };
            auto const overriding = [&](ada_type* aType)
            {
                for (auto* list : { &aType->primitives, aType->full != nullptr ? &aType->full->base_type()->primitives : nullptr })
                    if (list != nullptr)
                        for (auto* op : *list)
                            if (op != e && op->name == e->name && op->sub != nullptr && op->sub->inherited && type_conformant(*op->sub, *e->sub))
                                return true;
                // (an operation of a progenitor (interface), its controlling type being the type)
                std::function<bool(ada_type*)> progenitor = [&](ada_type* aInterface) -> bool
                {
                    auto* p = aInterface->view()->base_type()->view();
                    auto const same = [&](ada_type* x, ada_type* y)
                    {
                        if (x == nullptr || y == nullptr)
                            return x == y;
                        if (x->anonymous_access && y->anonymous_access && x->designated != nullptr && y->designated != nullptr)
                            x = x->designated, y = y->designated;
                        return same_type(x, y) || (x->base_type() == p && y->base_type() == aType);
                    };
                    for (auto* op : p->primitives)
                        if (op->name == e->name && op->sub != nullptr && op->kind == e->kind && op->sub->parameters.size() == e->sub->parameters.size() &&
                            same(op->sub->result, e->sub->result))
                        {
                            bool match = true;
                            for (std::size_t i = 0u; i < e->sub->parameters.size() && match; ++i)
                                match = same(op->sub->parameters[i].type, e->sub->parameters[i].type);
                            if (match)
                                return true;
                        }
                    for (auto* i : p->interfaces)
                        if (progenitor(i))
                            return true;
                    return false;
                };
                for (auto* i : aType->interfaces)
                    if (progenitor(i))
                        return true;
                return false;
            };
            if (auto* r = visibleType(e->sub->result); r != nullptr && r->tagged && !r->untagged_partial && e->kind == entity_kind::Function && !overriding(r))
                lateAbstract = true;
            if (e->sub->is_abstract)
                for (auto const& p : e->sub->parameters)
                    if (auto* pt = visibleType(p.type); pt != nullptr && abstract_type(pt) && !overriding(pt))
                        lateAbstract = true;
            if (auto* r = visibleType(e->sub->result); e->sub->is_abstract && r != nullptr && abstract_type(r) && !overriding(r))
                lateAbstract = true;
        }
        bool const overridesImplicit = implicit_homograph(e, aRegion);
        add_primitive_if(e, aRegion);
        implicit_inequality(e, aRegion);
        if (iInstanceBodies == 0)
            recover([&]() { overriding_indicator_check(e, aDecl, overridesImplicit, aRegion); });
        if (lateAbstract)
            recover([&]() { error(aDecl, "a primitive subprogram that requires overriding cannot be declared in the private part (unless overriding)"); });
        if (iInstanceBodies == 0)
            recover([&]() { abstract_subprogram_check(e, aDecl, aRegion); });
        // (a primitive subprogram of a tagged task or protected type: not type conformant with the prefixed view profile of one of its
        // operations (9.4(11.7/3), 9.1(9.7/3)))
        if (iInstanceBodies == 0 && iInstantiationSites.empty() && e->sub != nullptr && !e->sub->parameters.empty())
            if (auto* ft = e->sub->parameters[0].type; ft != nullptr)
            {
                if (ft->anonymous_access && ft->designated != nullptr)
                    ft = ft->designated;
                auto* u = ft->view()->base_type()->view();
                if ((u->kind == type_kind::Task || u->kind == type_kind::Protected) && !u->interfaces.empty() && u->unit != nullptr &&
                    u->unit->inner != nullptr && u->declared_in == aRegion)
                    for (auto* op : u->unit->inner->declarations)
                    {
                        if (op->sub == nullptr || op->name != e->name || op->sub->parameters.size() + 1u != e->sub->parameters.size())
                            continue;
                        bool same = (op->sub->result == nullptr) == (e->sub->result == nullptr) &&
                            (op->sub->result == nullptr || same_type(op->sub->result, e->sub->result));
                        for (std::size_t k = 0u; same && k < op->sub->parameters.size(); ++k)
                            same = op->sub->parameters[k].type != nullptr && e->sub->parameters[k + 1u].type != nullptr &&
                                same_type(op->sub->parameters[k].type, e->sub->parameters[k + 1u].type);
                        if (same)
                        {
                            recover([&]() { error(aDecl, "a primitive subprogram of a tagged task or protected type cannot be type conformant with the prefixed view of one of its operations (9.4(11.7/3))"); });
                            break;
                        }
                    }
            }
        // (a primitive subprogram of a tagged type declared in the region: before the type is frozen (13.14(16), 3.9.2(13)))
        if (iInstanceBodies == 0 && iInstantiationSites.empty() && e->sub != nullptr && aRegion->is_package)
        {
            std::vector<ada_type*> types;
            for (auto const& p : e->sub->parameters)
                types.push_back(p.type);
            types.push_back(e->sub->result);
            for (auto* pt : types)
            {
                if (pt == nullptr)
                    continue;
                if (pt->anonymous_access && pt->designated != nullptr)
                    pt = pt->designated;
                auto* b = pt->base_type();
                if (b->kind == type_kind::ClassWide)
                    continue;
                if (b->partial != nullptr)
                    b = b->partial->base_type();
                if (b->declared_in != aRegion)
                    continue;
                auto* full = b->full != nullptr ? b->full->base_type() : b;
                if (!full->tagged && !b->tagged)
                    continue;
                if (frozen_before(b, aDecl))
                {
                    recover([&]() { error(aDecl, "a primitive subprogram of a tagged type cannot be declared after the type is frozen (13.14(16), 3.9.2(13))"); });
                    break;
                }
            }
        }
        return e;
    }

    std::pair<bool, bool> program::atomic_view(node* aName)
    {
        // (whether a name denotes an atomic (volatile) object: an object specified so or of a type that is, or a component of an array
        // object (type) with atomic (volatile) components (C.6(8/3)))
        auto* n = aName;
        while (n != nullptr && n->is(construct::Parenthesized))
            n = n->child(0);
        if (n == nullptr)
            return { false, false };
        bool atomic = false;
        bool isVolatile = false;
        if (n->type != nullptr)
        {
            atomic = type_aspect(n->type, "atomic") != nullptr;
            isVolatile = atomic || type_aspect(n->type, "volatile") != nullptr;
        }
        if ((n->is(construct::Identifier) || n->is(construct::Selected)) && n->ent != nullptr && n->ent->is_object())
        {
            atomic = atomic || n->ent->atomic_object;
            isVolatile = isVolatile || n->ent->volatile_object || atomic;
        }
        // (a subcomponent of a volatile object: volatile)
        if ((n->is(construct::Selected) && meaning_of(n) == meaning::Component) || (n->is(construct::Apply) && meaning_of(n) == meaning::Index))
            if (!n->children.empty() && atomic_view(n->child(0)).second)
                isVolatile = true;
        if (n->is(construct::Apply) && meaning_of(n) == meaning::Index && !n->children.empty())
        {
            auto* array = n->child(0);
            if (array->ent != nullptr && array->ent->is_object())
            {
                atomic = atomic || array->ent->atomic_components;
                isVolatile = isVolatile || array->ent->volatile_components || atomic;
            }
            if (array->type != nullptr)
            {
                atomic = atomic || type_aspect(array->type, "atomic_components") != nullptr;
                isVolatile = isVolatile || atomic || type_aspect(array->type, "volatile_components") != nullptr;
            }
        }
        return { atomic, isVolatile };
    }

    bool program::subtype_attribute(node const* aNode)
    {
        // (T'Class, T'Base: a subtype (not a value); T'Access ... of a current instance: not static)
        if (!aNode->is(construct::Attribute) || aNode->size() < 2u)
            return false;
        auto const a = name_text(aNode->child(1)->source);
        return a == "class" || a == "base" || a == "access" || a == "unchecked_access" || a == "unrestricted_access" || a == "address";
    }

    bool program::frozen_before(ada_type* aType, node const* aDecl)
    {
        // (a type declared earlier in the declarative part (or package specification) of a declaration frozen before it (13.14):
        // by a body (3/5), an object declaration (not a deferred constant's) of a type containing it (6, 15), a record extension of a
        // parent containing it (7/2))
        auto* t = aType->base_type();
        auto* typeEntity = t->decl != nullptr ? t->decl : (t->full != nullptr ? t->full->decl : nullptr);
        node const* typeDecl = typeEntity != nullptr && typeEntity->decl != nullptr ? typeEntity->decl->parent : nullptr;
        if (typeDecl == nullptr || aDecl == nullptr || typeDecl->parent == nullptr || aDecl->parent == nullptr)
            return false;
        // (the declarations between them: the same list, or the visible part's then the private part's)
        std::vector<node const*> between;
        auto const* listT = typeDecl->parent;
        auto const* listD = aDecl->parent;
        auto const collect = [&](node const* aList, node const* aFrom, node const* aTo)
        {
            bool on = aFrom == nullptr;
            for (auto const* c : aList->children)
            {
                if (c == aTo)
                    break;
                if (on)
                    between.push_back(c);
                if (c == aFrom)
                    on = true;
            }
        };
        if (listT == listD)
            collect(listT, typeDecl, aDecl);
        else if (listT->parent != nullptr && listT->parent == listD->parent && listT->is(construct::Declarations) && listD->is(construct::Private))
        {
            collect(listT, typeDecl, nullptr);
            collect(listD, nullptr, aDecl);
        }
        else
            return false;
        std::function<bool(ada_type*, int)> contains = [&](ada_type* x, int depth) -> bool
        {
            if (x == nullptr || depth > 16)
                return false;
            auto* v = x->view();
            if (v->kind == type_kind::ClassWide && v->specific != nullptr)
                v = v->specific->view();
            auto* xb = v->base_type();
            if (xb == t || xb == t->full || (t->full != nullptr && xb == t->full->base_type()) || xb->partial == t)
                return true;
            // (freezing a type freezes its parent subtype (13.14(15)))
            if (xb->parent != nullptr && contains(xb->parent, depth + 1))
                return true;
            // (an array type: its component and index subtypes (13.14(15)))
            if (xb->view()->kind == type_kind::Array)
            {
                for (auto* index : xb->view()->indexes)
                    if (contains(index, depth + 1))
                        return true;
                return contains(xb->view()->component, depth + 1);
            }
            if (xb->view()->kind == type_kind::Record)
                for (auto const& c : xb->view()->components)
                    if (c.type != nullptr && contains(c.type, depth + 1))
                        return true;
            return false;
        };
        // (an object's initial value: its nonstatic expressions' types (8), an allocator's designated subtype (13.14(4/1)))
        std::function<bool(node const*, int)> freezes = [&](node const* n, int depth) -> bool
        {
            if (n == nullptr || depth > 64)
                return false;
            if (n->is(construct::Allocator) && n->type != nullptr && n->type->view()->kind == type_kind::Access && contains(designated_of(n->type), 0))
                return true;
            if (n->type != nullptr && !n->type->is_access() && contains(n->type, 0))
                return true;
            // (a name of a type or subtype (an attribute's prefix, a qualified expression's or a conversion's subtype mark) (13.14(11)))
            if (n->ent != nullptr && n->ent->is_type() && n->ent->type != nullptr && contains(n->ent->type, 0))
                return true;
            for (auto const* c : n->children)
                if (freezes(c, depth + 1))
                    return true;
            return false;
        };
        // (a nested package specification's declarations too)
        for (std::size_t k = 0u; k < between.size(); ++k)
            if (between[k]->is(construct::Package))
                for (auto const* part : { between[k]->first(construct::Declarations), between[k]->first(construct::Private) })
                    if (part != nullptr)
                        for (auto const* c : part->children)
                            between.push_back(c);
        // (static expressions (13.14(8/3)): an enumeration literal of the type, a type's name as an attribute's prefix or a qualified
        // expression's subtype mark (other than in aspect specifications and enumeration representation clauses))
        std::function<bool(node const*, int)> statics = [&](node const* n, int depth) -> bool
        {
            if (n == nullptr || depth > 64 || n->is(construct::Aspects) || n->is(construct::RepresentationEnumeration) ||
                n->is(construct::RepresentationAttribute) || n->is(construct::RepresentationRecord) || n->is(construct::RepresentationAt))
                return false;
            if (n->ent != nullptr && n->ent->kind == entity_kind::EnumerationLiteral && n->ent->type != nullptr && contains(n->ent->type, 0))
                return true;
            if ((n->is(construct::Attribute) || n->is(construct::Qualified)) && !n->children.empty() && n->child(0)->ent != nullptr &&
                n->child(0)->ent->is_type() && n->child(0)->ent->type != nullptr && !subtype_attribute(n) && contains(n->child(0)->ent->type, 0))
                return true;
            for (auto const* c : n->children)
                if (statics(c, depth + 1))
                    return true;
            return false;
        };
        for (auto const* d : between)
        {
            if (d->is(construct::SubprogramBody) || d->is(construct::PackageBody) || d->is(construct::TaskBody) || d->is(construct::ProtectedBody) ||
                d->is(construct::Stub))
                return true;
            // (not the type's own (full) declaration)
            bool const own = d->ent != nullptr && d->ent->kind == entity_kind::Type && d->ent->type != nullptr && (d->ent->type->base_type() == t ||
                (t->full != nullptr && d->ent->type->base_type() == t->full->base_type()) || d->ent->type->base_type()->partial == t);
            if (!d->is(construct::Package) && !own && statics(d, 0))
                return true;
            // (an instantiation: its actuals (13.14(5/3)): types, objects' subtypes, expressions)
            if (d->is(construct::Instantiation))
            {
                std::function<bool(node const*, int)> actuals = [&](node const* n, int depth) -> bool
                {
                    if (n == nullptr || depth > 32)
                        return false;
                    if (n->ent != nullptr && (n->ent->is_type() || n->ent->is_object()) && n->ent->type != nullptr && contains(n->ent->type, 0))
                        return true;
                    // (a subprogram actual: its profile (13.14(10.2/4)... 14))
                    if (n->ent != nullptr && n->ent->sub != nullptr && n->ent->is_subprogram())
                    {
                        for (auto const& p : n->ent->sub->parameters)
                            if (p.type != nullptr && contains(p.type->anonymous_access && p.type->designated != nullptr ? p.type->designated : p.type, 0))
                                return true;
                        if (auto* r = n->ent->sub->result; r != nullptr && contains(r->anonymous_access && r->designated != nullptr ? r->designated : r, 0))
                            return true;
                    }
                    if (n->type != nullptr && !n->type->is_access() && contains(n->type, 0))
                        return true;
                    for (auto const* c : n->children)
                        if (actuals(c, depth + 1))
                            return true;
                    return false;
                };
                for (auto const* c : d->children)
                    if (!c->is(construct::Defining) && actuals(c, 0))
                        return true;
            }
            if (d->is(construct::Object))
            {
                node const* init = nullptr;
                std::size_t parts = 0u;
                for (auto const* c : d->children)
                    if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects) && ++parts == 2u)
                        init = c;
                if (init != nullptr && freezes(init, 0))
                    return true;
            }
            if (d->is(construct::Object) && d->ent != nullptr && d->ent->type != nullptr)
            {
                std::size_t parts = 0u;
                for (auto const* c : d->children)
                    if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                        ++parts;
                bool const deferred = d->has_flag("constant") && parts < 2u;    // (a subtype and no initial value)
                if (!deferred && contains(d->ent->type, 0))
                    return true;
            }
            // (a record extension freezes its parent (13.14(7/2)); not a private extension, nor another derived type)
            if (d->is(construct::Type) && d->ent != nullptr && d->ent->type != nullptr && d->first(construct::TypeDerived) != nullptr &&
                d->first(construct::TypeDerived)->first(construct::Extension) != nullptr &&
                !d->first(construct::TypeDerived)->first(construct::Extension)->has_flag("private"))
                if (auto* parent = d->ent->type->base_type()->parent; parent != nullptr && contains(parent, 0))
                    return true;
        }
        return false;
    }

    bool program::implicit_homograph(entity* aSubprogram, region* aRegion)
    {
        // (an implicitly declared homograph (inherited or predefined) in the same declarative region: overridden (8.3(9/1)))
        for (auto* r = aRegion; r != nullptr; r = r->package_body ? r->parent : nullptr)
        {
            auto range = r->names.equal_range(aSubprogram->name);
            for (auto i = range.first; i != range.second; ++i)
                if (auto* x = i->second; x != aSubprogram && x->sub != nullptr && (x->sub->inherited || x->sub->op != intrinsic::None) &&
                    x->kind == aSubprogram->kind && type_conformant(*x->sub, *aSubprogram->sub))
                    return true;
            if (!r->package_body)
                break;
        }
        // (a predefined operator of a type of its profile, declared with the type in the region)
        std::vector<ada_type*> types;
        for (auto const& p : aSubprogram->sub->parameters)
            types.push_back(p.type);
        types.push_back(aSubprogram->sub->result);
        for (auto* t : types)
            if (t != nullptr && !t->anonymous_access && t->base_type()->declared_in != nullptr &&
                (t->base_type()->declared_in == aRegion || (aRegion->package_body && t->base_type()->declared_in == aRegion->parent)))
                for (auto* op : operators_of(t))
                    if (op != aSubprogram && op->name == aSubprogram->name && op->sub != nullptr && op->sub->op != intrinsic::None &&
                        type_conformant(*op->sub, *aSubprogram->sub) &&
                        // (no predefined equality for a limited type (as seen here) (4.5.2(9/3)))
                        !((op->name == "=" || op->name == "/=") && limited_type(t, aRegion)))
                        return true;
        return false;
    }

    bool program::predicate_static(node* aExpression, entity* aCurrentInstance)
    {
        // (predicate-static (3.2.4(15/5)-(22/5)): a static expression; a membership test of the current instance with static
        // choices; a case expression of it with predicate-static dependent expressions; a comparison of it with a static
        // expression; a predefined logical operation or short circuit of predicate-static operands; parenthesized)
        if (aExpression == nullptr)
            return false;
        auto const current = [&](node* n) { return n != nullptr && n->ent != nullptr && n->ent == aCurrentInstance; };
        if (aExpression->is(construct::Parenthesized))
            return predicate_static(aExpression->child(0), aCurrentInstance);
        if (aExpression->is(construct::In) && current(aExpression->child(0)))
        {
            for (std::size_t i = 1u; i < aExpression->size(); ++i)
                if (auto* c = aExpression->child(i); !c->is(construct::Flag))
                {
                    if (c->is(construct::Range))
                    {
                        if (!static_expression(c->child(0)) || !static_expression(c->child(1)))
                            return false;
                    }
                    else if (c->is(construct::SubtypeIndication) || meaning_of(c) == meaning::Type)
                    {
                        if (c->type == nullptr || !static_subtype(c->type))
                            return false;
                    }
                    else if (!static_expression(c))
                        return false;
                }
            return true;
        }
        if (aExpression->is(construct::CaseExpression) && current(aExpression->child(0)))
        {
            for (std::size_t i = 1u; i < aExpression->size(); ++i)
                if (!predicate_static(aExpression->child(i)->children.back(), aCurrentInstance))
                    return false;
            return true;
        }
        if ((aExpression->is(construct::Eq) || aExpression->is(construct::Ne) || aExpression->is(construct::Lt) || aExpression->is(construct::Le) ||
            aExpression->is(construct::Gt) || aExpression->is(construct::Ge)) && aExpression->size() == 2u &&
            (current(aExpression->child(0)) || current(aExpression->child(1))))
            return static_expression(current(aExpression->child(0)) ? aExpression->child(1) : aExpression->child(0));
        if (aExpression->is(construct::And) || aExpression->is(construct::AndThen) || aExpression->is(construct::Or) ||
            aExpression->is(construct::OrElse) || aExpression->is(construct::Xor))
            return aExpression->size() == 2u && predicate_static(aExpression->child(0), aCurrentInstance) &&
                predicate_static(aExpression->child(1), aCurrentInstance);
        if (aExpression->is(construct::Not))
            return predicate_static(aExpression->child(0), aCurrentInstance);
        return static_expression(aExpression);
    }

    void program::completion_aspects_check(node* aDecl)
    {
        // (a completion (a body, stub, or completing null procedure or expression function): no Pre or Post aspects (they are
        // the declaration's) (13.1.1(32/5)))
        if (iInstanceBodies != 0)
            return;
        if (auto* aspects = aDecl->first(construct::Aspects))
            for (auto* aspect : aspects->children)
                if (auto const name = name_text(aspect->child(0)->source); name == "pre" || name == "post")
                    recover([&]() { error(aspect, "aspect " + name + " cannot be specified for a completion"); });
    }

    void program::others_check(node* aExpression, ada_type* aSubtype)
    {
        // (an array aggregate with others: only where an applicable index constraint exists (4.3.3(10/5)-(15.1/3)); not for an
        // object, default or formal object of an unconstrained array subtype)
        while (aExpression != nullptr && aExpression->is(construct::Parenthesized))
            aExpression = aExpression->child(0);
        if (aExpression == nullptr || aSubtype == nullptr || iInstanceBodies != 0 || !(aExpression->is(construct::Aggregate) ||
            aExpression->is(construct::IfExpression) || aExpression->is(construct::CaseExpression)))
            return;
        auto* v = aSubtype->view();
        if (v->kind != type_kind::Array || v->constrained || v->is_formal)
            return;
        // (its subaggregates' too (of the further dimensions))
        std::vector<node*> others;
        others_choices(aExpression, v->indexes.size(), others);
        for (auto* c : others)
            recover([&]() { error(c, "others is allowed only where the aggregate has an applicable index constraint"); });
    }

    void program::homograph_check(entity* aSubprogram, node* aDefining, region* aRegion)
    {
        // (a homograph of an explicit declaration in the same declarative region (8.3(26/2)))
        auto* e = aSubprogram;
        for (auto* r = aRegion; r != nullptr; r = r->package_body ? r->parent : nullptr)
        {
            auto range = r->names.equal_range(e->name);
            for (auto i = range.first; i != range.second; ++i)
                if (auto* other = i->second; other != e && (other->kind == e->kind || (other->kind == entity_kind::Entry &&
                    e->kind == entity_kind::Procedure && (other->sub == nullptr || other->sub->family_type == nullptr))) && other->sub != nullptr &&
                    !other->sub->inherited && other->sub->op == intrinsic::None && other->decl != nullptr && type_conformant(*other->sub, *e->sub))
                    recover([&]() { error(aDefining, "\"" + e->name + "\" is already declared in this declarative region (with the same profile)"); });
                // (an entry family: not overloadable)
                else if (other != e && other->kind == entity_kind::Entry && other->sub != nullptr && other->sub->family_type != nullptr)
                    recover([&]() { error(aDefining, "\"" + e->name + "\" is already declared in this declarative region (an entry family)"); });
                // (an enumeration literal: a parameterless function of its type)
                else if (other->kind == entity_kind::EnumerationLiteral && other->decl != nullptr && e->kind == entity_kind::Function &&
                    e->sub->parameters.empty() && e->sub->result != nullptr && other->type != nullptr && same_type(other->type, e->sub->result))
                    recover([&]() { error(aDefining, "\"" + e->name + "\" is already declared in this declarative region (with the same profile)"); });
            if (!r->package_body)
                break;
        }
    }

    void program::operator_symbol_check(node* aDefining)
    {
        // (an operator symbol: one of the operators, without blanks (6.1(10/3)))
        static std::set<std::string> const operators{ "and", "or", "xor", "=", "/=", "<", "<=", ">", ">=", "+", "-", "&", "*", "/", "mod", "rem",
            "**", "abs", "not" };
        std::string_view const raw{ aDefining->source };
        // (its text (not normalized), in lower case)
        std::string name{ raw.size() >= 2u && raw.front() == '"' ? raw.substr(1u, raw.size() - 2u) : raw };
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!operators.contains(name) || raw.find_first_of(" \t") != std::string_view::npos)
            error(aDefining, "\"" + std::string{ raw.size() >= 2u ? raw.substr(1u, raw.size() - 2u) : raw } + "\" is not an operator symbol");
    }

    void program::operator_profile_check(entity* aOperator, node* aDefining)
    {
        // (an operator symbol: one of the operators, with as many parameters as the operator has operands, without defaults (6.6(2-4)))
        static std::set<std::string> const unary{ "not", "abs" };
        static std::set<std::string> const binary{ "and", "or", "xor", "=", "/=", "<", "<=", ">", ">=", "&", "*", "/", "mod", "rem", "**" };
        auto const& name = aOperator->name;
        if (aOperator->sub == nullptr)
            return;
        if (!unary.contains(name) && !binary.contains(name) && name != "+" && name != "-")
            return;
        // (in an instance (not the instance's own name): its result's type (an actual Boolean, 12.3(11)))
        bool instanceName = false;
        if (!iInstantiationSites.empty())
            for (node const* n = aDefining; n != nullptr && !instanceName; n = n->parent)
                instanceName = n == iInstantiationSites.back();
        if (!iInstantiationSites.empty() && !instanceName)
        {
            if (name == "/=" && aOperator->sub->result != nullptr && boolean() != nullptr &&
                aOperator->sub->result->view()->base_type()->view() == boolean()->view()->base_type()->view())
                error(aDefining, "an explicit \"/=\" cannot have a result of the predefined type Boolean (in the instance, 6.6(5))");
            return;
        }
        auto const n = aOperator->sub->parameters.size();
        if ((unary.contains(name) && n != 1u) || (binary.contains(name) && n != 2u) || ((name == "+" || name == "-") && n != 1u && n != 2u))
            error(aDefining, "the operator \"" + name + "\" has the wrong number of parameters");
        // (its parameters: of mode in (6.6(3/3)))
        for (auto const& p : aOperator->sub->parameters)
            if (p.mode == parameter_mode::Out || p.mode == parameter_mode::InOut)
            {
                error(aDefining, "the parameters of the operator \"" + name + "\" must be of mode in (6.6(3/3))");
                break;
            }
        if (aOperator->kind != entity_kind::Function)
            error(aDefining, "an operator must be a function");
        for (auto const& p : aOperator->sub->parameters)
            if (p.default_expression != nullptr)
                error(p.default_expression, "the parameters of an operator cannot have default expressions");
        // (an explicit "/=": not with a result of the predefined type Boolean (6.6(5)))
        if (name == "/=" && aOperator->sub->result != nullptr && boolean() != nullptr &&
            aOperator->sub->result->view()->base_type()->view() == boolean()->view()->base_type()->view())
            error(aDefining, "an explicit \"/=\" cannot have a result of the predefined type Boolean");
    }

    void program::synchronized_operation_check(entity* aSubprogram, node* aDecl, entity* aUnit)
    {
        // (a protected or task unit's operation: overriding implements an operation of a progenitor (9.4(11.4/3), 9.1(9.6/3)); one
        // that implements one: of a first parameter mode that the kind of operation allows (9.4(11.2/3, 11.3/3), 9.1(9.3/3)))
        bool const overriding = aDecl->has_flag("overriding");
        bool const notOverriding = aDecl->has_flag("not overriding");
        if (overriding && (aUnit->type == nullptr || aUnit->type->interfaces.empty()))
            error(aDecl, "\"" + aSubprogram->name + "\" does not implement an operation of a progenitor (the unit has none)");
        if (aUnit->type == nullptr || aUnit->type->view()->interfaces.empty() || aSubprogram->sub == nullptr || iInstanceBodies != 0)
            return;
        // (implementing a progenitor's primitive subprogram: of its name, its first parameter controlling, the others' (and the result)
        // type conformant (9.4(11.1/3), 9.1(9.2/3)))
        std::vector<ada_type*> interfaces;
        std::function<void(ada_type*)> collect = [&](ada_type* i)
        {
            if (i == nullptr || interfaces.size() > 64u)
                return;
            auto* b = i->view()->base_type()->view();
            if (std::find(interfaces.begin(), interfaces.end(), b) != interfaces.end())
                return;
            interfaces.push_back(b);
            for (auto* j : b->interfaces)
                collect(j);
        };
        for (auto* i : aUnit->type->view()->interfaces)
            collect(i);
        auto const& own = *aSubprogram->sub;
        bool const function = aSubprogram->kind == entity_kind::Function;
        entity* implemented = nullptr;
        for (auto* i : interfaces)
            for (auto* op : i->primitives)
            {
                if (implemented != nullptr || op->sub == nullptr || op->name != aSubprogram->name || op->sub->parameters.size() != own.parameters.size() + 1u)
                    continue;
                if ((op->kind == entity_kind::Function) != function)
                    continue;
                auto* first = op->sub->parameters[0].type;
                if (first != nullptr && first->anonymous_access && first->designated != nullptr)
                    first = first->designated;
                if (first == nullptr || first->view()->base_type()->view() != i)
                    continue;
                bool same = (op->sub->result == nullptr) == (own.result == nullptr) &&
                    (own.result == nullptr || same_type(op->sub->result, own.result));
                for (std::size_t k = 0u; same && k < own.parameters.size(); ++k)
                    same = op->sub->parameters[k + 1u].type != nullptr && own.parameters[k].type != nullptr &&
                        same_type(op->sub->parameters[k + 1u].type, own.parameters[k].type);
                if (same)
                    implemented = op;
            }
        // (implementing it: subtype conformant with its prefixed view profile (9.4(11.4/3)... 6.3.1(17/3)))
        if (implemented != nullptr)
        {
            bool conformant = implemented->sub->result == nullptr || own.result == nullptr || statically_matching(implemented->sub->result, own.result);
            for (std::size_t k = 0u; conformant && k < own.parameters.size(); ++k)
            {
                auto const& a = implemented->sub->parameters[k + 1u];
                auto const& b = own.parameters[k];
                conformant = a.mode == b.mode && (a.type->anonymous_access || b.type->anonymous_access || statically_matching(a.type, b.type));
            }
            if (!conformant)
                error(aDecl, "\"" + aSubprogram->name + "\" implements a progenitor's subprogram, so it must be subtype conformant with its prefixed view (9.4(11.4/3))");
        }
        if (overriding && implemented == nullptr)
            error(aDecl, "\"" + aSubprogram->name + "\" does not implement an operation of a progenitor (9.4(11.4/3))");
        if (notOverriding && implemented != nullptr)
            error(aDecl, "\"" + aSubprogram->name + "\" implements an operation of a progenitor: \"not overriding\" is illegal (9.4(11.5/3))");
        if (implemented != nullptr)
        {
            auto const& first = implemented->sub->parameters[0];
            bool const access = first.type != nullptr && first.type->anonymous_access;
            bool const accessConstant = access && first.type->view()->access_constant;
            // (a procedure or entry: its first parameter out or in out, or access-to-variable; a function: in, or access-to-constant)
            if (!function && ((!access && first.mode == parameter_mode::In) || accessConstant))
                error(aDecl, "\"" + aSubprogram->name + "\" implements a subprogram whose first parameter is of mode in (or access-to-constant): a protected procedure or entry cannot (9.4(11.2/3))");
            if (function && ((!access && first.mode != parameter_mode::In) || (access && !accessConstant)))
                error(aDecl, "\"" + aSubprogram->name + "\" implements a function whose first parameter is not of mode in (or is access-to-variable): a protected function cannot (9.4(11.3/3))");
        }
    }

    void program::overriding_indicator_check(entity* aSubprogram, node* aDecl, bool aOverridesImplicit, region* aRegion)
    {
        // (an overriding indicator (8.3.1(5/2)-(7/2)): overriding: it overrides; not overriding: it does not; either: a primitive
        // operation)
        bool const overriding = aDecl->has_flag("overriding");
        bool const notOverriding = aDecl->has_flag("not overriding");
        // (a protected or task unit's operation)
        if (auto* owner = aRegion != nullptr ? aRegion->owner : nullptr; owner != nullptr &&
            (owner->kind == entity_kind::Protected || owner->kind == entity_kind::Task))
        {
            synchronized_operation_check(aSubprogram, aDecl, owner);
            return;
        }
        if (!overriding && !notOverriding)
            return;
        auto& sub = *aSubprogram->sub;
        bool const overrides = aOverridesImplicit || (sub.parent_operation != nullptr && sub.parent_operation != aSubprogram);
        bool primitive = false;
        auto const consider = [&](ada_type* aType)
        {
            if (aType == nullptr)
                return;
            if (aType->anonymous_access && aType->designated != nullptr)
                aType = aType->designated;
            auto* b = aType->base_type();
            // (of a type declared in a package specification (3.2.3(6/5)))
            for (auto* t : { b, b->partial != nullptr ? b->partial->base_type() : nullptr, b->full != nullptr ? b->full->base_type() : nullptr })
                if (t != nullptr && std::find(t->primitives.begin(), t->primitives.end(), aSubprogram) != t->primitives.end() &&
                    t->declared_in != nullptr && t->declared_in->is_package && !t->declared_in->package_body &&
                    (aSubprogram->scope == nullptr || aSubprogram->scope == t->declared_in))
                    primitive = true;
        };
        for (auto const& p : sub.parameters)
            consider(p.type);
        consider(sub.result);
        // (reported at the specification: the indicator can be on a line of its own)
        node* at = specification_of(aDecl) != nullptr ? specification_of(aDecl) : aDecl;
        if (!primitive && !overrides)
            error(at, "a subprogram with an overriding indicator must be a primitive operation");
        if (overriding && !overrides)
            error(at, "\"" + aSubprogram->name + "\" does not override an inherited subprogram");
        if (notOverriding && overrides)
            error(at, "\"" + aSubprogram->name + "\" overrides an inherited subprogram");
    }

    void program::implicit_inequality(entity* aEquality, region* aRegion)
    {
        // (an explicit "=" whose result is Boolean: a "/=" implicitly declared with it, its complement (6.6))
        if (aEquality != nullptr && aEquality->name == "/=" && aEquality->sub != nullptr && aEquality->sub->op == intrinsic::None && aEquality->sub->parameters.size() == 2u)
        {
            // (an explicit "/=": the implicit one it overrides removed)
            for (auto n = aRegion->names.begin(); n != aRegion->names.end();)
                if (n->second != aEquality && n->second->name == "/=" && n->second->sub != nullptr && n->second->sub->op == intrinsic::NotEqual &&
                    type_conformant(*n->second->sub, *aEquality->sub))
                {
                    auto* implicit = n->second;
                    n = aRegion->names.erase(n);
                    for (auto* t : { aEquality->sub->parameters[0].type })
                        if (t != nullptr)
                            std::erase(t->base_type()->primitives, implicit);
                }
                else
                    ++n;
            return;
        }
        // (a renaming-as-declaration of an "=" too, predefined or not (6.6(6)))
        if (aEquality == nullptr || aEquality->name != "=" || aEquality->sub == nullptr || (aEquality->sub->op != intrinsic::None && aEquality->sub->renames == nullptr) ||
            aEquality->sub->parameters.size() != 2u || aEquality->sub->result == nullptr || !same_type(aEquality->sub->result, iBoolean))
            return;
        if (!iImplicitInequalities.insert(aEquality).second)
            return;
        if (std::getenv("NEOS_ADA_DEBUG"))
            std::fprintf(stderr, "implicit /= of %s\n", aEquality->name.c_str());
        auto* ne = new_entity(aEquality->kind, "/=", aEquality->decl, aRegion);
        ne->sub->parameters = aEquality->sub->parameters;
        ne->sub->result = aEquality->sub->result;
        ne->type = ne->sub->result;
        ne->sub->op = intrinsic::NotEqual;
        ne->sub->complement_of = aEquality;
        ne->sub->level = aEquality->sub->level;
        ne->sub->defined = true;
        aRegion->add(ne);
        add_primitive_if(ne, aRegion);
    }

    std::string program::conformance_text(node const* n)
    {
        // (the lexical elements; a numeric literal by its value (6.3.1(19)))
        std::string result;
        auto const source = n->source;
        for (std::size_t i = 0u; i < source.size();)
        {
            auto const c = source[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) && (result.empty() || !(std::isalnum(static_cast<unsigned char>(result.back())) || result.back() == '_')))
            {
                std::size_t j = i;
                while (j < source.size() && (std::isalnum(static_cast<unsigned char>(source[j])) || source[j] == '_' || source[j] == '.' ||
                    source[j] == '#' || source[j] == ':' || ((source[j] == '+' || source[j] == '-') && j > i && (source[j - 1] == 'e' || source[j - 1] == 'E'))))
                    ++j;
                auto const literal = source.substr(i, j - i);
                if (auto value = rational::parse_literal(literal))
                    result += "#" + value->to_string() + "#";
                else
                    result += std::string{ literal };
                i = j;
                continue;
            }
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ++i;
        }
        // (an expanded name and a direct name of the same declaration conform (6.3.1(21)): the prefixes left out)
        std::string direct;
        for (std::size_t i = 0u; i < result.size(); ++i)
        {
            std::size_t j = i;
            while (j < result.size() && (std::isalnum(static_cast<unsigned char>(result[j])) || result[j] == '_'))
                ++j;
            if (j > i && (i == 0u || !(std::isalnum(static_cast<unsigned char>(result[i - 1u])) || result[i - 1u] == '_')) && j + 1u < result.size() &&
                result[j] == '.' && (std::isalpha(static_cast<unsigned char>(result[j + 1u])) || result[j + 1u] == '"' || result[j + 1u] == '\''))
            {
                i = j;  // (the prefix and its dot)
                continue;
            }
            direct += result[i];
        }
        return direct;
    }

    bool program::fully_conformant(node* a, node* b)
    {
        // (fully conformant expressions (6.3.1(18-21)): the same lexical elements (numeric literals by value, expanded names' prefixes
        // aside), each name (and operator) denoting the same declaration)
        if (a == nullptr || b == nullptr)
            return a == b;
        if (conformance_text(a) != conformance_text(b))
        {
            // (an operator's call in infix and functional notation: of the same operator, with conformant operands)
            auto const operands = [&](node* n, entity*& op) -> std::vector<node*>
            {
                auto const* call = call_of(n);
                op = call != nullptr ? call->callee : nullptr;
                if (op == nullptr || op->name.empty() || std::isalpha(static_cast<unsigned char>(op->name[0])))
                    return {};
                if (n->is(construct::Apply))
                {
                    std::vector<node*> result;
                    for (std::size_t i = 1u; i < n->size(); ++i)
                    {
                        auto* x = n->child(i);
                        if (x->is(construct::Arguments))
                            for (auto* y : x->children)
                                result.push_back(y);
                        else
                            result.push_back(x);
                    }
                    return result;
                }
                return n->children;
            };
            entity* opA = nullptr;
            entity* opB = nullptr;
            auto const xs = operands(a, opA);
            auto const ys = operands(b, opB);
            if (opA == nullptr || opA != opB || xs.size() != ys.size() || xs.empty() || a->is(construct::Apply) == b->is(construct::Apply))
                return false;
            for (std::size_t i = 0u; i < xs.size(); ++i)
                if (!fully_conformant(xs[i], ys[i]))
                    return false;
            return true;
        }
        std::function<void(node*, std::vector<entity*>&)> denotations = [&](node* n, std::vector<entity*>& out)
        {
            if (auto const* call = call_of(n); call != nullptr && call->callee != nullptr)
                out.push_back(call->callee);
            if (n->is(construct::Identifier) || n->is(construct::Selected) || n->is(construct::Character) || (n->is(construct::Operator) && n->ent != nullptr))
            {
                auto* e = n->ent;
                while (e != nullptr && e->renamed != nullptr && e->renamed != e && e->is_package())
                    e = e->renamed;
                out.push_back(e);
                return;
            }
            for (auto* c : n->children)
                denotations(c, out);
        };
        std::vector<entity*> x, y;
        denotations(a, x);
        denotations(b, y);
        return x == y;
    }

    void program::conformance_check(subprogram_info const& aDeclared, subprogram_info const& aBody, std::string const& aName, node* aWhere)
    {
        // (fully conformant with the declaration (6.3.1(17/3, 18/3, 19)): the same parameter names, modes, subtypes and default
        // expressions, and result subtype)
        auto const& a = aDeclared.parameters;
        auto const& b = aBody.parameters;
        auto const subtypes = [&](ada_type* x, ada_type* y)
        {
            if (x == nullptr || y == nullptr)
                return true;
            if (x->anonymous_access && y->anonymous_access)
                return x->designated == nullptr || y->designated == nullptr || same_type(x->designated, y->designated);
            return statically_matching(x, y);
        };
        bool conformant = a.size() == b.size() && (aDeclared.result == nullptr) == (aBody.result == nullptr) &&
            subtypes(aDeclared.result, aBody.result);
        for (std::size_t i = 0u; conformant && i < a.size(); ++i)
            conformant = a[i].ent != nullptr && b[i].ent != nullptr && a[i].ent->name == b[i].ent->name && a[i].mode == b[i].mode &&
                (a[i].not_null == b[i].not_null || (a[i].type != nullptr && a[i].type->anonymous_access)) && subtypes(a[i].type, b[i].type) &&
                (a[i].default_expression == nullptr) == (b[i].default_expression == nullptr) &&
                (a[i].default_expression == nullptr || conformance_text(a[i].default_expression) == conformance_text(b[i].default_expression));
        if (!conformant)
            error(aWhere, "the body of \"" + aName + "\" does not conform to its declaration");
    }

    void program::subprogram_body(node* aDecl, region* aRegion)
    {
        auto* spec = specification_of(aDecl);
        auto* defining = spec->child(0);
        auto const kind = spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure;
        entity* e = nullptr;
        // (a library subprogram body: its declaration, if it has one)
        bool library = iDeclaringUnit != nullptr;
        // (a library subprogram's designator: an identifier (a defining program unit name), not an operator symbol (10.1.1(3)))
        if (library && defining->is(construct::DefiningOperator) && iInstanceBodies == 0)
            recover([&]() { error(defining, "a library subprogram's designator cannot be an operator symbol (10.1.1(3))"); });
        entity* declared = nullptr;
        if (library)
            declared = library_unit(iDeclaringUnit->name);
        // (a library unit of the same name that is not a subprogram declaration: not completed by a subprogram body (10.1.4(4)))
        if (library && declared != nullptr && declared->sub == nullptr)
            error(defining, "\"" + declared->name + "\" is a library unit that is not a subprogram");
        if (library && declared != nullptr && declared->is_generic() && declared->sub != nullptr)
        {
            // (a library generic subprogram's body: analysed in its instances; compiled again (a later compilation replaces the
            // earlier, 10.1.4(5)): the earlier checked now (legality checking))
            if (declared->sub->body != nullptr && declared->sub->body != aDecl && std::getenv("NEOS_ADA_CHECK") != nullptr && iInstanceBodies == 0 &&
                declared->decl != nullptr && declared->sub->body->file == aDecl->file)
            {
                auto* const savedUnit = iDeclaringUnit;
                iDeclaringUnit = nullptr;
                recover([&]() { check_generic(declared); });
                iDeclaringUnit = savedUnit;
            }
            declared->sub->body = aDecl;
            aDecl->ent = declared;
            defining->ent = declared;
            if (iDeclaringUnit != nullptr)
            {
                iDeclaringUnit->ent = declared;
                iDeclaringUnit = nullptr;
            }
            return;
        }
        if (!library)
            if (auto* g = generic_completion(spec, aRegion))
            {
                // (a generic subprogram's body: analysed in its instances)
                g->sub->body = aDecl;
                aDecl->ent = g;
                defining->ent = g;
                return;
            }
        // (the body's own entity (its parameters are its own), then it completes the declaration, if any)
        auto* own = new_entity(kind, defining->is(construct::DefiningOperator) ? designator_of(defining) : name_text(defining->source), defining, aRegion);
        {
            auto const dot = own->name.rfind('.');
            if (dot != std::string::npos)
                own->name = own->name.substr(dot + 1u);
        }
        auto* parameters = new_region(own, aRegion);
        parameters->is_subprogram = true;
        parameters->subprogram_body = !library;     // (a library subprogram body's specification: its declaration (10.1.2(12/3)))
        parameters->level = aRegion->level + 1;
        subprogram_specification(spec, own, parameters);
        parameters->subprogram_body = true;
        own->sub->level = parameters->level;
        if (declared == nullptr && !library)
            declared = find_completion(own, aRegion);
        // (a body without a declaration: its result's null exclusion (checked at the declaration, if any))
        if (declared == nullptr && iInstanceBodies == 0 && iInstantiationSites.empty() && own->sub != nullptr && own->sub->result_not_null && own->sub->result != nullptr)
            if (auto* result = spec->first(construct::Result); result != nullptr && result->first(construct::AccessDefinition) == nullptr)
                null_exclusion_check(own->sub->result, result);
        // (a body without a declaration: its homographs (8.3(26/2)); an operator's designator and profile (6.6))
        if (declared == nullptr && !library && iInstanceBodies == 0 && iInstantiationSites.empty())
        {
            // (not against a generic unit of the name: the body may be meant as its completion (an error there, not a cascade here))
            bool generic = false;
            for (auto [i, end] = aRegion->names.equal_range(own->name); i != end; ++i)
                generic = generic || i->second->is_generic();
            if (!generic)
                recover([&]() { redeclaration_check(own->name, kind, defining, aRegion); });
            homograph_check(own, defining, aRegion);
            if (defining->is(construct::DefiningOperator))
                recover([&]() { operator_symbol_check(defining); });
        }
        if (declared != nullptr && declared->is_generic())
        {
            // (a generic subprogram's body: analysed in its instances)
            declared->sub->body = aDecl;
            aDecl->ent = declared;
            defining->ent = declared;
            if (library && iDeclaringUnit != nullptr)
            {
                iDeclaringUnit->ent = declared;
                iDeclaringUnit = nullptr;
            }
            return;
        }
        if (declared != nullptr && declared->sub != nullptr)
        {
            recover([&]() { conformance_check(*declared->sub, *own->sub, declared->name, defining); });
            completion_aspects_check(aDecl);
            // (the body's overriding indicator: as of the body's place (8.3.1(5/2)-(7/2)))
            if (iInstanceBodies == 0 && (aDecl->has_flag("overriding") || aDecl->has_flag("not overriding")))
                recover([&]() { overriding_indicator_check(declared, aDecl, implicit_homograph(declared, aRegion), aRegion); });
            // (an abstract subprogram: no body (3.9.3(3/2)))
            if (declared->sub->is_abstract)
                recover([&]() { error(defining, "an abstract subprogram cannot have a body"); });
            e = declared;
            // (the body's parameters stand for the declaration's)
            for (std::size_t i = 0u; i < e->sub->parameters.size() && i < own->sub->parameters.size(); ++i)
            {
                own->sub->parameters[i].ent->renamed = nullptr;
                e->sub->parameters[i].default_expression = e->sub->parameters[i].default_expression != nullptr ? e->sub->parameters[i].default_expression : own->sub->parameters[i].default_expression;
            }
            // (the declaration's parameter entities are replaced by the body's (the body refers to them))
            auto defaults = e->sub->parameters;
            if (e->sub->spec_parameters.empty())
                for (auto const& p : defaults)
                    e->sub->spec_parameters.push_back(p.ent);
            e->sub->parameters = own->sub->parameters;
            // (the defaults: the declaration's (resolved; the body's conform to them))
            for (std::size_t i = 0u; i < e->sub->parameters.size() && i < defaults.size(); ++i)
            {
                if (defaults[i].default_expression != nullptr)
                    e->sub->parameters[i].default_expression = defaults[i].default_expression;
                // (a controlling access parameter's null exclusion (see add_primitive))
                if (defaults[i].not_null && e->sub->parameters[i].type != nullptr && e->sub->parameters[i].type->anonymous_access)
                {
                    e->sub->parameters[i].not_null = true;
                    e->sub->parameters[i].type->not_null = true;
                }
            }
            e->inner = parameters;
            parameters->owner = e;
            e->sub->spec = spec;
            e->sub->separate_body = !library;
            if (library && iDeclaringUnit != nullptr)
            {
                iDeclaringUnit->ent = e;
                iDeclaringUnit = nullptr;
            }
        }
        else
        {
            e = own;
            aRegion->add(e);
            register_library_unit(e);
            // (a body in a package body overriding an inherited primitive subprogram of a tagged type declared in the package's
            // specification: a primitive declared after the type is frozen (3.9.2(13)))
            if (aRegion->package_body && iInstanceBodies == 0)
            {
                std::vector<ada_type*> types;
                for (auto const& p : e->sub->parameters)
                    types.push_back(p.type);
                types.push_back(e->sub->result);
                for (auto* pt : types)
                {
                    if (pt == nullptr)
                        continue;
                    if (pt->anonymous_access && pt->designated != nullptr)
                        pt = pt->designated;
                    auto* b = pt->base_type();
                    if (b->partial != nullptr)
                        b = b->partial->base_type();
                    if (b->declared_in != aRegion->parent || b->kind == type_kind::ClassWide)
                        continue;
                    auto* full = b->full != nullptr ? b->full->base_type() : b;
                    if (!full->tagged)
                        continue;
                    bool overriding = false;
                    for (auto* list : { &b->primitives, &full->primitives })
                        for (auto* op : *list)
                            if (op->name == e->name && op->sub != nullptr && op->sub->inherited && type_conformant(*op->sub, *e->sub))
                                overriding = true;
                    if (overriding)
                    {
                        recover([&]() { error(spec, "\"" + e->name + "\" overrides a primitive subprogram of a tagged type after the type is frozen"); });
                        break;
                    }
                }
            }
            bool const overridesImplicit = implicit_homograph(e, aRegion);
            add_primitive_if(e, aRegion);
            implicit_inequality(e, aRegion);
            if (iInstanceBodies == 0)
            {
                recover([&]() { overriding_indicator_check(e, aDecl, overridesImplicit, aRegion); });
                recover([&]() { abstract_subprogram_check(e, spec, aRegion); });
            }
        }
        defining->ent = e;
        aDecl->ent = e;
        e->sub->body = aDecl;
        e->sub->level = parameters->level;
        aspects(aDecl, e, aRegion);
        auto* saved = iCurrentSubprogram;
        iCurrentSubprogram = e;
        declarations(aDecl->first(construct::Declarations), parameters);
        if (auto* handled = aDecl->first(construct::Handled))
        {
            statements(handled, parameters);
            // (a function body: at least one return statement (6.5(5/2)), not in a nested body (or code statements))
            if (kind == entity_kind::Function && iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                std::function<bool(node*)> returns = [&](node* aNode)
                {
                    for (auto* c : aNode->children)
                    {
                        if (c->is(construct::Return) || c->is(construct::ReturnExtended) || c->is(construct::Code))
                            return true;
                        if (c->is(construct::Declarations) || c->is(construct::Private))
                            continue;
                        if (returns(c))
                            return true;
                    }
                    return false;
                };
                if (!returns(handled))
                    recover([&]() { error(end_position(aDecl), "a function body must contain a return statement"); });
            }
        }
        iCurrentSubprogram = saved;
        iSubprogramsToGenerate.push_back(e);
    }

    // --- objects

    bool program::abstract_type(ada_type* aType, region* aRegion) const
    {
        // (an abstract type (3.9.3(1/2)): declared abstract, or an interface; not a class-wide type; a partial view's full view where
        // it is visible)
        if (aType == nullptr || aType->kind == type_kind::ClassWide || aType->base_type()->kind == type_kind::ClassWide)
            return false;
        auto* b = aType->base_type();
        if (aRegion != nullptr && b->kind == type_kind::Private && b->full != nullptr)
            if (auto* package = b->declared_in != nullptr ? b->declared_in->owner : nullptr; package != nullptr && private_part_visible(package, aRegion))
                b = b->full->base_type();
        return b->abstract || b->kind == type_kind::Interface;
    }

    void program::abstract_subprogram_check(entity* aSubprogram, node* aDecl, region* aRegion)
    {
        // (a function whose result type is abstract (or an access result designating one): abstract (3.9.3(8/3)))
        auto& sub = *aSubprogram->sub;
        if (aSubprogram->kind == entity_kind::Function && !sub.is_abstract && sub.result != nullptr)
        {
            auto* result = sub.result;
            if (result->anonymous_access && result->designated != nullptr)
                result = result->designated;
            if (abstract_type(result, aRegion))
                error(aDecl, "a function whose result type is abstract must be abstract");
        }
        // (a dispatching operation of one tagged type only (3.9.2(12)))
        {
            std::vector<ada_type*> tagged;
            auto const consider = [&](ada_type* aType)
            {
                if (aType == nullptr)
                    return;
                if (aType->anonymous_access && aType->designated != nullptr)
                    aType = aType->designated;
                auto* b = aType->base_type();
                if (b->kind == type_kind::ClassWide)
                    return;
                auto* partial = b->partial != nullptr ? b->partial->base_type() : b;
                auto* full = partial->full != nullptr ? partial->full->base_type() : b;
                if (!(partial->tagged || full->tagged) || (partial->untagged_partial && partial != b) ||
                    std::find(tagged.begin(), tagged.end(), partial) != tagged.end())
                    return;
                for (auto* list : { &partial->primitives, &full->primitives })
                    if (std::find(list->begin(), list->end(), aSubprogram) != list->end())
                    {
                        tagged.push_back(partial);
                        return;
                    }
            };
            for (auto const& p : sub.parameters)
                consider(p.type);
            consider(sub.result);
            if (tagged.size() > 1u)
            {
                // (not inherited by their descendants: no further errors)
                for (std::size_t i = 0u; i < tagged.size(); ++i)
                    for (auto* t : { tagged[i], tagged[i]->full != nullptr ? tagged[i]->full->base_type() : nullptr })
                        if (t != nullptr)
                            std::erase(t->primitives, aSubprogram);
                error(aDecl, "a subprogram cannot be a dispatching operation of more than one tagged type");
            }
        }
        // (a dispatching operation's subtypes of its type: statically matching its first subtype (3.9.2(10/2)); overriding: subtype
        // conformant with the operation it overrides (3.9.2(10/2)))
        if (sub.controlling != nullptr && sub.op == intrinsic::None)
        {
            auto* controlling = sub.controlling->base_type();
            auto* first = controlling->decl != nullptr && controlling->decl->type != nullptr ? controlling->decl->type : controlling;
            auto const ofType = [&](ada_type* aType)
            {
                return aType != nullptr && !aType->anonymous_access && aType->view()->kind != type_kind::ClassWide && same_type(aType, controlling);
            };
            bool bad = false;
            for (auto const& p : sub.parameters)
                if (ofType(p.type) && !statically_matching(p.type, first))
                    bad = true;
            if (ofType(sub.result) && !statically_matching(sub.result, first))
                bad = true;
            if (bad)
                error(aDecl, "the subtypes of a dispatching operation's type in its profile must statically match its first subtype");
            if (auto* overridden = sub.parent_operation; overridden != nullptr && overridden != aSubprogram && overridden->sub != nullptr &&
                overridden->sub->parameters.size() == sub.parameters.size() && overridden->sub->controlling != nullptr)
            {
                auto* parentType = overridden->sub->controlling->base_type();
                auto const conformant = [&](ada_type* aInherited, ada_type* aOwn)
                {
                    if (aInherited == nullptr || aOwn == nullptr)
                        return aInherited == aOwn;
                    if (aInherited->anonymous_access || aOwn->anonymous_access)
                        return true;    // (designated subtypes: checked by type conformance)
                    if (aInherited->view()->kind != type_kind::ClassWide && same_type(aInherited, parentType))
                        return true;    // (a controlling subtype: the first subtype's (checked above))
                    return statically_matching(aInherited, aOwn);
                };
                bool conforms = conformant(overridden->sub->result, sub.result);
                for (std::size_t i = 0u; i < sub.parameters.size() && conforms; ++i)
                    conforms = sub.parameters[i].mode == overridden->sub->parameters[i].mode &&
                        conformant(overridden->sub->parameters[i].type, sub.parameters[i].type);
                if (!conforms)
                    error(aDecl, "an overriding dispatching operation must be subtype conformant with the operation it overrides");
            }
        }
        // (a controlling formal parameter's default: tag indeterminate (3.9.2(11/2)))
        if (sub.controlling != nullptr)
        {
            // (a controlling type: the type, or an access parameter or result designating it)
            auto const controllingType = [&](ada_type* aType, ada_type* aControlling)
            {
                if (aType == nullptr)
                    return false;
                if (aType->anonymous_access && aType->designated != nullptr)
                    aType = aType->designated;
                return aType->view()->kind != type_kind::ClassWide && same_type(aType, aControlling);
            };
            std::function<bool(node*)> indeterminate = [&](node* aExpression) -> bool
            {
                while (aExpression->is(construct::Parenthesized) && !aExpression->children.empty())
                    aExpression = aExpression->child(0);
                if (aExpression->is(construct::Qualified) && aExpression->size() >= 2u)
                    return indeterminate(aExpression->children.back());
                auto const* call = meaning_of(aExpression) == meaning::Call ? call_of(aExpression) : nullptr;
                if (call == nullptr || call->callee == nullptr || call->callee->sub == nullptr || call->callee->sub->controlling == nullptr ||
                    !controllingType(call->callee->sub->result, call->callee->sub->controlling))
                    return false;
                auto const& formals = call->callee->sub->parameters;
                for (std::size_t i = 0u; i < call->actuals.size() && i < formals.size(); ++i)
                    if (call->actuals[i] != nullptr && controllingType(formals[i].type, call->callee->sub->controlling) && !indeterminate(call->actuals[i]))
                        return false;
                return true;
            };
            for (auto const& p : sub.parameters)
                if (p.default_expression != nullptr && controllingType(p.type, sub.controlling) && !indeterminate(p.default_expression))
                    recover([&]() { error(p.default_expression, "the default expression of a controlling formal parameter must be tag indeterminate"); });
        }
        // (a primitive subprogram of an interface type: abstract or a null procedure (3.9.4(5/2)))
        if (sub.controlling != nullptr && sub.controlling->base_type()->kind == type_kind::Interface && !sub.is_abstract && !sub.is_null &&
            sub.op == intrinsic::None)
            error(aDecl, "a primitive subprogram of an interface type must be abstract or a null procedure");
        // (an abstract primitive subprogram of a tagged type: the type abstract (3.9.3(3/2)))
        if (sub.is_abstract && sub.controlling != nullptr && !abstract_type(sub.controlling))
            error(aDecl, "an abstract primitive subprogram of a tagged type requires the type to be abstract");
    }

    bool program::indefinite_subtype(ada_type* aType, node* aIndication, region* aRegion) const
    {
        // (an indefinite subtype (3.3(23/3)): class-wide, unknown discriminants, an unconstrained array subtype, or a subtype with
        // discriminants without defaults (unconstrained))
        if (aType == nullptr || aIndication == nullptr || aIndication->is(construct::AccessDefinition))
            return false;
        auto* v = aType->view();
        // (the view at the declaration: a partial view's (unknown discriminants) unless the full view is visible)
        bool fullVisible = v == aType || aType->incomplete || aType->base_type()->incomplete;
        if (!fullVisible)
            if (auto* package = aType->base_type()->declared_in != nullptr ? aType->base_type()->declared_in->owner : nullptr; package != nullptr)
                fullVisible = private_part_visible(package, aRegion);
        if (v->kind == type_kind::ClassWide || (!fullVisible && aType->unknown_discriminants) || v->unknown_discriminants ||
            v->base_type()->inherited_unknown_discriminants ||
            (v->kind == type_kind::Array && !v->constrained && !aType->constrained))
            return true;
        if ((v->kind == type_kind::Record || v->kind == type_kind::Private) && !v->constrained && !aType->constrained &&
            aType->discriminant_values.empty() && !(aIndication->is(construct::SubtypeIndication) && aIndication->size() > 1u) &&
            !aIndication->is(construct::Apply))
            for (auto const& c : v->base_type()->view()->components)
                if (c.discriminant && !c.stored && c.default_expression == nullptr)
                    return true;
        return false;
    }

    void program::limited_value_check(node* aExpression, ada_type* aType, region* aRegion)
    {
        // (an expression of a limited type where a new object is initialized (an object's or component's initial value, a component 
        // association's, an initialized allocator's, a return expression ...): each operative constituent newly constructed (an
        // aggregate, a function call ...) (7.5(2.1/5)))
        if (aExpression == nullptr || iInstanceBodies != 0 || iSpeculation != 0 || aType == nullptr || aType->view()->is_formal || !limited_type(aType, aRegion))
            return;
        std::function<node*(node*)> built = [&](node* n) -> node*
        {
            if (n->is(construct::Parenthesized) || (n->is(construct::Qualified) && !n->children.empty()))
                return built(n->children.back());
            if (n->is(construct::Aggregate) || n->is(construct::AggregateExtension) || n->is(construct::AggregateNullRecord) ||
                n->is(construct::AggregateDelta) || n->is(construct::AggregateBracket) || n->is(construct::Allocator))
                return nullptr;
            if (n->is(construct::IfExpression) || n->is(construct::CaseExpression))
            {
                for (std::size_t i = 1u; i < n->size(); ++i)
                {
                    auto* c = n->child(i);
                    auto* dependent = c->is(construct::Elsif) ? c->child(1) : c->is(construct::Else) || c->is(construct::Alternative) ? c->children.back() : c;
                    if (auto* x = built(dependent))
                        return x;
                }
                return nullptr;
            }
            if (meaning_of(n) == meaning::Call || n->is(construct::Operator) || n->is(construct::DeclareExpression) ||
                (n->ent != nullptr && (n->ent->kind == entity_kind::Function || n->ent->kind == entity_kind::EnumerationLiteral)))
                return nullptr;
            // (an operator call, an attribute function's call)
            if (n->is(construct::Attribute))
                return nullptr;
            return n->is(construct::Identifier) || n->is(construct::Selected) || n->is(construct::All) || n->is(construct::Apply) ? n : nullptr;
        };
        if (auto* x = built(aExpression))
            recover([&]() { error(x, "an expression of a limited type here must be newly constructed (an aggregate or a function call) (7.5(2.1/5))"); });
    }

    void program::object_declaration(node* aDecl, region* aRegion)
    {
        bool const isConstant = aDecl->has_flag("constant");
        bool const isAliased = aDecl->has_flag("aliased");
        node* typeNode = nullptr;
        node* init = nullptr;
        for (auto* c : aDecl->children)
        {
            if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects))
                continue;
            if (typeNode == nullptr)
                typeNode = c;
            else
                init = c;
        }
        // (the objects: declared first (their scope begins with the declaration), hidden from all visibility until its end (8.3(16)))
        declaring hiding;
        std::vector<entity*> declared;
        std::vector<entity*> deferreds;
        for (auto* d : aDecl->all(construct::Defining))
        {
            auto const name = name_text(d->source);
            entity* deferred = nullptr;
            if (isConstant)
            {
                auto range = aRegion->names.equal_range(name);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->kind == entity_kind::Constant && i->second->initial == nullptr && !i->second->completed && !i->second->imported)
                        deferred = i->second;
            }
            auto* e = declare(isConstant ? entity_kind::Constant : entity_kind::Variable, d, aRegion);
            hiding.add(e);
            declared.push_back(e);
            deferreds.push_back(deferred);
        }
        ada_type* type = nullptr;
        if (typeNode->is(construct::TypeArray) || typeNode->is(construct::TypeArrayUnconstrained))
            type = type_definition(typeNode, {}, nullptr, aRegion, nullptr);
        else if (typeNode->is(construct::AccessDefinition))
            type = access_definition(typeNode, aRegion);
        else
            type = subtype_indication(typeNode, aRegion);
        for (std::size_t k = 0u; k < declared.size(); ++k)
        {
            auto* e = declared[k];
            auto* deferred = deferreds[k];
            e->type = type;
            if (k > 0u && !is_subtype_name(typeNode, aRegion) && !typeNode->is(construct::AccessDefinition) &&
                !(typeNode->is(construct::SubtypeIndication) && typeNode->size() == 1u && !typeNode->child(0)->is(construct::Apply)))
            {
                // (a multiple object declaration is a series of single ones (3.3.1(7)): each object's subtype indication elaborated 
                // for it)
                auto* own = clone(typeNode);
                if (typeNode->is(construct::TypeArray) || typeNode->is(construct::TypeArrayUnconstrained))
                    e->type = type_definition(own, {}, nullptr, aRegion, nullptr);
                else
                    e->type = subtype_indication(own, aRegion);
                iObjectSubtypes[e] = own;
            }
            e->aliased = isAliased;
            e->aliased_declared = isAliased;
            e->initial = init;
            if (deferred != nullptr && init != nullptr)
            {
                deferred->completed = true;
                deferred->completion = e;
                deferred->renamed = e;
                // (the full declaration: of the deferred constant's type; statically matching a constrained subtype or an access
                // definition; excluding null if it does; aliased if it is (7.4(9/2)))
                // (in the private part (7.4(4)))
                if (iInstanceBodies == 0 && aDecl->parent != nullptr && aDecl->parent->is(construct::Declarations) && aDecl->parent->parent != nullptr &&
                    aDecl->parent->parent->is(construct::Package))
                    recover([&]() { error(aDecl, "the full declaration of deferred constant \"" + e->name + "\" must be in the private part (7.4(4))"); });
                if (iInstanceBodies == 0 && deferred->type != nullptr && e->type != nullptr)
                {
                    auto* dt = deferred->type;
                    auto* ft = e->type;
                    auto* dv = dt->view();
                    auto* fv = ft->view();
                    std::string problem;
                    if (dt->anonymous_access || dv->anonymous_access)
                    {
                        if (!(ft->anonymous_access || fv->anonymous_access) || dv->kind != fv->kind)
                            problem = "its access definition must statically match the deferred constant's";
                        else if (dv->kind == type_kind::Access && (dv->access_constant != fv->access_constant ||
                            (dv->designated != nullptr && fv->designated != nullptr && !statically_matching(dv->designated, fv->designated))))
                            problem = "its access definition must statically match the deferred constant's";
                        else if ((dt->not_null || dv->not_null) != (ft->not_null || fv->not_null))
                            problem = "its access definition must statically match the deferred constant's (null exclusion)";
                    }
                    else if (!same_type(dt, ft))
                        problem = "it must be of the deferred constant's type";
                    else if ((dt->not_null || dv->not_null) && !(ft->not_null || fv->not_null))
                        problem = "its subtype must exclude null as the deferred constant's does";
                    else if ((dv->kind == type_kind::Array || dv->kind == type_kind::Record) && dv->constrained &&
                        (dv->kind == type_kind::Array || !dv->discriminant_constraint.empty()) && !statically_matching(dt, ft))
                        problem = "its subtype must statically match the deferred constant's constrained subtype";
                    if (problem.empty() && deferred->aliased_declared && !e->aliased_declared)
                        problem = "it must be aliased as the deferred constant is";
                    if (!problem.empty())
                        recover([&]() { error(aDecl, "the full declaration of deferred constant \"" + e->name + "\": " + problem + " (7.4(9/2))"); });
                }
            }
        }
        aDecl->ent = declared.empty() ? nullptr : declared.front();
        // (an object of an abstract type (3.9.3(8/3)))
        if (abstract_type(type, aRegion) && iInstanceBodies == 0)
            recover([&]() { error(typeNode, "an object of an abstract type cannot be declared"); });
        // (an object (not a deferred constant) freezes its type: completely defined (13.14(17)))
        if (!(isConstant && init == nullptr) && iInstanceBodies == 0 && not_completely_defined(type, aRegion))
            recover([&]() { error(typeNode, "the type of an object must be completely defined (its full declaration has not occurred yet)"); });
        if (init == nullptr && type != nullptr && !declared.empty())
        {
            // (imported by a later pragma Import, or the aspect: no initialization)
            bool imported = false;
            if (auto* aspects = aDecl->first(construct::Aspects))
                for (auto* aspect : aspects->children)
                    imported = imported || name_text(aspect->child(0)->source) == "import";
            if (aDecl->parent != nullptr)
            {
                auto const& siblings = aDecl->parent->children;
                for (auto i = std::find(siblings.begin(), siblings.end(), aDecl); i != siblings.end(); ++i)
                    if ((*i)->is(construct::Pragma) && name_text((*i)->child(0)->source) == "import")
                        imported = true;
            }
            // (a constant without an initial value: a deferred constant, in the visible part of a package specification (7.4(3)))
            if (isConstant && !imported && !declared.front()->completed &&
                !(aDecl->parent != nullptr && aDecl->parent->is(construct::Declarations) && aDecl->parent->parent != nullptr &&
                  aDecl->parent->parent->is(construct::Package)))
                error(aDecl, "a constant declaration without an initial value must be a deferred constant (in a package's visible part)");
            // (an object of an indefinite subtype: initialized (3.3.1(5/2)))
            if (!imported && !isConstant && indefinite_subtype(type, typeNode, aRegion))
                error(typeNode, "an object of an indefinite subtype must be initialized");
        }
        if (init != nullptr)
        {
            auto* t = resolve(init, type, aRegion);
            others_check(init, type);
            // (a limited type's initial value: an aggregate, a function call, or a conditional expression of them (7.5(2.1/5)))
            limited_value_check(init, type, aRegion);
            // (a static initial value: in its type's base range (4.9(34/3)))
            if (iInstanceBodies == 0 && type != nullptr)
                static_range_check(init, init, type, true, aRegion);
            // (a deferred constant: not used before its full declaration (7.4(9/5), 13.14(17)))
            if (iInstanceBodies == 0)
            {
                std::function<node*(node*)> deferred = [&](node* n) -> node*
                {
                    if (n->ent != nullptr && n->ent->kind == entity_kind::Constant && n->ent->initial == nullptr && !n->ent->completed &&
                        n->ent->completion == nullptr && !n->ent->imported && n->ent->decl != nullptr && n->ent->renamed == nullptr && !(n->ent->decl != nullptr && n->ent->decl->parent != nullptr &&
                        n->ent->decl->parent->is(construct::FormalObject)) &&
                        n->ent->renamed_name == nullptr && n->is(construct::Identifier))
                        return n;
                    for (auto* c : n->children)
                        if (auto* x = deferred(c))
                            return x;
                    return nullptr;
                };
                if (auto* x = deferred(init))
                    recover([&]() { error(x, "the deferred constant \"" + x->ent->name + "\" is used before its full declaration"); });
            }
            // (an unconstrained array object: its subtype is its initial value's)
            for (auto* e : declared)
            {
                // (a static string constant (4.9(24)): its value)
                if (isConstant && type->view()->kind == type_kind::Array && type->view()->indexes.size() == 1u && type->view()->component != nullptr &&
                    type->view()->component->view()->base_type()->view()->character_type)
                    if (auto v = evaluate(init, aRegion); v.is_string())
                        e->value = v;
                if (isConstant && type->view()->is_scalar())
                {
                    auto v = evaluate(init, aRegion);
                    // (of an ordinary fixed point type: rounded to an integral multiple of its small (4.9(38/2), as Machine_Rounds))
                    if (auto* fv = type->view()->base_type()->view(); v.known() && fv->kind == type_kind::Fixed && fv->small.sign() != 0 &&
                        (v.is_real() || v.is_integer()))
                    {
                        rational const r = v.is_real() ? v.real() : rational{ v.integer() };
                        v = static_value{ rational{ (r / fv->small).round() } * fv->small };
                    }
                    // (a value failing its subtype's static predicate: not static (AI12-0393-1))
                    if (v.known() && v.is_integer() && type->predicate != nullptr && type->view()->is_discrete())
                    {
                        bool allStatic = true;
                        for (auto* p = type->predicate; p != nullptr && allStatic; p = p->parent)
                            allStatic = p->is_static;
                        if (allStatic)
                            if (auto const values = static_values(type, aRegion))
                            {
                                bool in = false;
                                for (auto const& [low, high] : *values)
                                    in = in || (v.integer() >= low && v.integer() <= high);
                                if (!in)
                                    v = static_value{};
                            }
                    }
                    if (v.known())
                        e->value = v;
                }
                if (type->kind == type_kind::Array && !type->constrained && t != nullptr)
                    e->type = type;
            }
        }
        aspects(aDecl, aDecl->ent, aRegion);
        for (auto* e : declared)
            if (e->scope->owner != nullptr && e->scope->level == 0)
                e->library = true;
    }

    void program::number_declaration(node* aDecl, region* aRegion)
    {
        node* init = nullptr;
        for (auto* c : aDecl->children)
            if (!c->is(construct::Defining))
                init = c;
        // (the numbers: declared first, hidden from all visibility until the end of the declaration (8.3(16)))
        declaring hiding;
        std::vector<entity*> numbers;
        for (auto* d : aDecl->all(construct::Defining))
        {
            numbers.push_back(declare(entity_kind::NamedNumber, d, aRegion));
            numbers.back()->type = iUniversalInteger;
            hiding.add(numbers.back());
        }
        auto* t = resolve(init, nullptr, aRegion);
        // (a static expression of a numeric type (3.3.2(3)))
        if (t != nullptr && !t->view()->is_integer() && !t->view()->is_real())
            error(init, "named number's value is not numeric");
        auto value = evaluate(init, aRegion);
        if (!value.known() || !static_expression(init))
            error(init, "named number's value is not static");
        for (auto* e : numbers)
        {
            e->value = value;
            e->type = value.is_integer() ? iUniversalInteger : iUniversalReal;
            (void)t;
        }
    }

    void program::exception_declaration(node* aDecl, region* aRegion)
    {
        for (auto* d : aDecl->all(construct::Defining))
        {
            auto* e = declare(entity_kind::Exception, d, aRegion);
            e->exception_id = iNextExceptionId++;
            iExceptions.push_back(e);
            if (d == aDecl->first(construct::Defining))
                aspects(aDecl, e, aRegion);
        }
    }

    // --- renamings

    void program::renaming_declaration(node* aDecl, region* aRegion)
    {
        switch (aDecl->kind)
        {
        case construct::RenamingObject:
            {
                auto* defining = aDecl->first(construct::Defining);
                node* typeNode = nullptr;
                node* renamed = nullptr;
                std::vector<node*> parts;
                for (auto* c : aDecl->children)
                    if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                        parts.push_back(c);
                if (parts.size() >= 2u)
                {
                    typeNode = parts[0];
                    renamed = parts[1];
                }
                else if (!parts.empty())
                    renamed = parts[0];
                ada_type* type = nullptr;
                // (a subtype mark, without a constraint (8.5.1(2/3)))
                if (typeNode != nullptr && (typeNode->is(construct::Apply) || (typeNode->is(construct::SubtypeIndication) && typeNode->size() >= 2u)) &&
                    iInstanceBodies == 0)
                {
                    recover([&]() { error(typeNode, "the subtype of an object renaming must be given by a subtype mark (without a constraint)"); });
                    typeNode = typeNode->child(0);
                }
                // (the renaming's name: hidden from all visibility within its declaration, hiding its homographs (8.3(16)))
                entity* hiddenName = nullptr;
                if (defining != nullptr && iInstanceBodies == 0)
                {
                    hiddenName = new_entity(entity_kind::Variable, name_text(defining->source), defining, aRegion);
                    hiddenName->declaring = true;
                    aRegion->names.emplace(hiddenName->name, hiddenName);
                }
                struct unhide_name
                {
                    region* r;
                    entity*& e;
                    void release()
                    {
                        if (e != nullptr)
                            for (auto [i, end] = r->names.equal_range(e->name); i != end; ++i)
                                if (i->second == e)
                                {
                                    r->names.erase(i);
                                    break;
                                }
                        e = nullptr;
                    }
                    ~unhide_name() { release(); }
                } unhidingName{ aRegion, hiddenName };
                if (typeNode != nullptr)
                    type = typeNode->is(construct::AccessDefinition) ? access_definition(typeNode, aRegion) : subtype_mark(typeNode, aRegion);
                if (type != nullptr && typeNode != nullptr && !typeNode->is(construct::AccessDefinition) && aDecl->has_flag("not null"))
                    null_exclusion_check(type, typeNode);
                variable_name(renamed);
                auto* actual = resolve(renamed, type, aRegion);
                auto* target = renamed->ent;
                // (a dereference freezes its designated subtype (13.14(11.1/1)): completely defined (7.3(5)))
                if (renamed->is(construct::All) && iSpeculation == 0 && iInstanceBodies == 0 && not_completely_defined(actual, aRegion))
                    recover([&]() { error(renamed, "the dereferenced object's type must be completely defined (its full declaration has not occurred yet)"); });
                // (the name of an object (8.5.1(2/5)): not a literal or other value, nor a type, exception, package, unit or entry)
                if (iInstanceBodies == 0)
                {
                    bool object = !(renamed->is(construct::Operator) || renamed->is(construct::Number) || renamed->is(construct::Null) ||
                        renamed->is(construct::Aggregate) || renamed->is(construct::Character) || renamed->is(construct::Add) ||
                        renamed->is(construct::Subtract) || renamed->is(construct::Multiply) || renamed->is(construct::Divide) ||
                        renamed->is(construct::Concat));
                    if ((renamed->is(construct::Identifier) || renamed->is(construct::Selected)) && target != nullptr && !target->is_object() &&
                        target->kind != entity_kind::Function && target->kind != entity_kind::EnumerationLiteral)
                        object = false;
                    if (renamed->is(construct::Attribute) && renamed->size() == 2u)
                        if (auto const a = name_text(renamed->child(1)->source); a == "pred" || a == "succ" || a == "image" || a == "value" || a == "pos" ||
                            a == "val" || a == "min" || a == "max" || (is_subtype_name(renamed->child(0), aRegion) && a != "class" && a != "base"))
                            object = false;
                    if (!object)
                        recover([&]() { error(renamed, "the renamed entity must be an object"); });
                    else if (discriminant_dependent(renamed))
                        recover([&]() { error(renamed, "a subcomponent that depends on discriminants of an unconstrained variable cannot be renamed"); });
                    // (an access definition: the renamed object of an anonymous access type, both access-to-constant or neither, their
                    // designated subtypes statically matching (8.5.1(3/2), (4.2/2)))
                    if (typeNode != nullptr && typeNode->is(construct::AccessDefinition) && type != nullptr && target != nullptr && target->is_object() &&
                        target->type != nullptr && (renamed->is(construct::Identifier) || renamed->is(construct::Selected)) && type->view()->kind == type_kind::Access)
                    {
                        auto* ot = target->type->view();
                        if (ot->kind != type_kind::Access || !target->type->anonymous_access)
                            recover([&]() { error(renamed, "the renamed object must be of an anonymous access type (8.5.1(3/2))"); });
                        else if (ot->access_constant != type->view()->access_constant)
                            recover([&]() { error(renamed, "the renamed object's type must be access-to-constant if and only if the access definition is"); });
                        else if (ot->designated != nullptr && type->view()->designated != nullptr && !statically_matching(ot->designated, type->view()->designated))
                            recover([&]() { error(renamed, "the designated subtypes of the renamed object's type and the access definition must statically match (8.5.1(4.2/2))"); });
                    }
                    if (typeNode != nullptr && typeNode->is(construct::AccessDefinition) && type != nullptr && target != nullptr && target->is_object() &&
                        target->type != nullptr && (renamed->is(construct::Identifier) || renamed->is(construct::Selected)) &&
                        type->view()->kind == type_kind::AccessSubprogram)
                    {
                        auto* ot = target->type->view();
                        if (ot->kind != type_kind::AccessSubprogram || !target->type->anonymous_access)
                            recover([&]() { error(renamed, "the renamed object must be of an anonymous access type (8.5.1(3/2))"); });
                        else if (ot->profile != nullptr && type->view()->profile != nullptr && (ot->protected_access != type->view()->protected_access ||
                            !subtype_conformant(*ot->profile, *type->view()->profile)))
                            recover([&]() { error(renamed, "the designated profiles of the renamed object's type and the access definition must be subtype conformant (8.5.1(4.3/2))"); });
                    }
                }
                // (a null exclusion: the renamed object's nominal subtype excludes null (8.5.1(4.4/3)); a generic formal in out object's
                // explicitly in the generic's body (8.5.1(4.5/3)); in an instance, its actual's too (also in its private part))
                if (iInstanceBodies == 0 || iInstanceBodies == iInstancePrivateParts)
                {
                    bool const notNull = aDecl->has_flag("not null") || (typeNode != nullptr && typeNode->is(construct::AccessDefinition) &&
                        typeNode->has_flag("not null"));
                    if (notNull && target != nullptr && target->is_object() && renamed->is(construct::Identifier))
                    {
                        auto const nominally = [&](entity* aObject)
                        {
                            bool excludes = aObject->type != nullptr && (aObject->type->not_null || aObject->type->view()->not_null);
                            if (aObject->kind == entity_kind::Parameter && aObject->scope != nullptr && aObject->scope->owner != nullptr &&
                                aObject->scope->owner->sub != nullptr)
                                for (auto const& p : aObject->scope->owner->sub->parameters)
                                    if (p.ent == aObject)
                                        excludes = excludes || p.not_null || (aObject->scope->owner->sub->controlling != nullptr && p.type != nullptr &&
                                            p.type->anonymous_access && p.type->designated != nullptr &&
                                            same_type(p.type->designated, aObject->scope->owner->sub->controlling));
                            return excludes;
                        };
                        bool const formal = target->decl != nullptr && target->decl->parent != nullptr && target->decl->parent->is(construct::FormalObject);
                        bool excludes = nominally(target);
                        if (formal && target->renamed == nullptr && target->decl->parent->has_flag("out") && [&]()
                            {
                                for (auto* r = aRegion; r != nullptr && r != target->scope; r = r->parent)
                                    if (r->package_body || r->subprogram_body)
                                        return true;
                                return false;
                            }())
                            excludes = target->decl->parent->has_flag("not null") || (target->type != nullptr && target->type->anonymous_access &&
                                target->type->view()->not_null);
                        for (auto* a = target->renamed; excludes && a != nullptr && a->is_object(); a = a->renamed)
                            excludes = nominally(a);
                        if (!excludes)
                            recover([&]() { error_in_private_part_too(renamed, "the renamed object's subtype must exclude null"); });
                    }
                }
                unhidingName.release();
                auto* e = declare(entity_kind::Variable, defining, aRegion);
                e->type = actual != nullptr ? actual : type;
                e->renamed_name = renamed;
                e->where = storage::Renaming;
                if (target != nullptr && target->is_object() && (renamed->is(construct::Identifier) || renamed->is(construct::Selected)))
                {
                    e->renamed = target;
                    if (target->kind == entity_kind::Constant || target->kind == entity_kind::LoopParameter || (target->kind == entity_kind::Parameter && target->sub == nullptr))
                        e->kind = target->kind == entity_kind::Parameter ? entity_kind::Variable : entity_kind::Constant;
                    e->value = target->value;
                }
                aDecl->ent = e;
                break;
            }
        case construct::RenamingException:
            {
                auto* defining = aDecl->first(construct::Defining);
                auto* target = resolve_entity(aDecl->child(1), aRegion);
                // (an exception (8.5.2(2)))
                if (target != nullptr && target->kind != entity_kind::Exception && iInstanceBodies == 0)
                    recover([&]() { error(aDecl->child(1), "the renamed entity must be an exception"); });
                auto* e = declare(entity_kind::Exception, defining, aRegion);
                e->renamed = target;
                e->exception_id = target != nullptr ? target->exception_id : 0u;
                aDecl->ent = e;
                break;
            }
        case construct::RenamingPackage:
            {
                auto* defining = aDecl->first(construct::Defining);
                node* target = nullptr;
                for (auto* c : aDecl->children)
                    if (!c->is(construct::Defining) && !c->is(construct::Aspects))
                        target = c;
                auto* renamed = resolve_entity(target, aRegion);
                if (renamed == nullptr || !renamed->is_package())
                    error(target, "package name expected");
                // (a library unit renaming: of a library unit (10.1.1(14)))
                if (iDeclaringUnit != nullptr && iInstanceBodies == 0 && !renamed->library)
                    recover([&]() { error(target, "a library unit renaming declaration must rename a library unit (10.1.1(14))"); });
                auto* e = declare(renamed->kind, defining, aRegion);
                e->renamed = renamed;
                e->inner = renamed->inner;
                e->generic = renamed->generic;
                aDecl->ent = e;
                register_library_unit(e);
                break;
            }
        case construct::RenamingGeneric:
            {
                auto* defining = aDecl->first(construct::Defining);
                node* target = nullptr;
                for (auto* c : aDecl->children)
                    if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects))
                        target = c;
                auto* renamed = resolve_entity(target, aRegion);
                if (renamed == nullptr || !renamed->is_generic())
                    error(target, "generic unit name expected");
                // (a library unit renaming: of a library unit (10.1.1(14)))
                if (iDeclaringUnit != nullptr && iInstanceBodies == 0 && !renamed->library)
                    recover([&]() { error(target, "a library unit renaming declaration must rename a library unit (10.1.1(14))"); });
                // (of a generic unit of the kind (8.5.5(2)))
                if (iInstanceBodies == 0)
                {
                    auto const kind = aDecl->has_flag("package") ? entity_kind::GenericPackage : aDecl->has_flag("procedure") ? entity_kind::GenericProcedure :
                        aDecl->has_flag("function") ? entity_kind::GenericFunction : renamed->kind;
                    if (kind != renamed->kind)
                        recover([&]() { error(target, std::string{ "the renamed unit must be a generic " } + (kind == entity_kind::GenericPackage ? "package" :
                            kind == entity_kind::GenericProcedure ? "procedure" : "function") + " (8.5.5(2))"); });
                }
                // (a generic child of a generic package P: renamed only within P's declarative region (its body, its descendants and
                // subunits) (10.1.1(19/2), 8.5.5(3/2)))
                if (iInstanceBodies == 0 && renamed->scope != nullptr && renamed->scope->owner != nullptr &&
                    renamed->scope->owner->kind == entity_kind::GenericPackage)
                {
                    auto* p = renamed->scope->owner;
                    bool within = false;
                    for (region* jump = nullptr, *r = aRegion; r != nullptr && !within; r = outward(r, jump))
                        within = r->owner != nullptr && (r->owner == p || r->owner->generic == p ||
                            (iTemplateInstanceOf.contains(p) && iTemplateInstanceOf[p] == r->owner));
                    if (!within)
                        recover([&]() { error(target, "a generic child of a generic package can be renamed only within the declarative region of that package (8.5.5(3/2))"); });
                }
                auto* e = declare(renamed->kind, defining, aRegion);
                e->renamed = renamed;
                e->inner = renamed->inner;
                e->decl = renamed->decl;
                aDecl->ent = e;
                register_library_unit(e);
                break;
            }
        case construct::RenamingSubprogram:
            {
                auto* spec = specification_of(aDecl);
                auto* defining = spec->child(0);
                node* target = nullptr;
                for (auto* c : aDecl->children)
                    if (!c->is(construct::Flag) && !is_specification(c) && !c->is(construct::Aspects))
                        target = c;
                auto const kind = spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure;
                if (defining->is(construct::DefiningOperator) && iInstanceBodies == 0 && iInstantiationSites.empty())
                    recover([&]() { operator_symbol_check(defining); });
                auto* e = new_entity(kind, defining->is(construct::DefiningOperator) ? designator_of(defining) : name_text(defining->source), defining, aRegion);
                {
                    auto const dot = e->name.rfind('.');
                    if (dot != std::string::npos)
                        e->name = e->name.substr(dot + 1u);
                }
                defining->ent = e;
                auto* parameters = new_region(e, aRegion);
                parameters->is_subprogram = true;
                parameters->level = aRegion->level + 1;
                subprogram_specification(spec, e, parameters);
                e->sub->level = parameters->level;
                // (a dereference of an access-to-subprogram value: the value, evaluated now (into a hidden constant); calls of the renaming
                // call the subprogram it designates (8.5.4(2/3, 13)))
                if (target->is(construct::All) && target->size() >= 1u)
                {
                    auto* prefix = target->child(0);
                    ada_type* accessType = nullptr;
                    for (auto const& o : interpretations(prefix, aRegion))
                        if (o.type != nullptr && o.type->view()->kind == type_kind::AccessSubprogram)
                            accessType = o.type;
                    if (accessType != nullptr)
                    {
                        resolve(prefix, accessType, aRegion);
                        auto* value = new_entity(entity_kind::Constant, "%renamed", target, aRegion);
                        value->type = accessType;
                        value->initial = prefix;
                        value->captured = true;
                        aRegion->declarations.push_back(value);
                        iDynamicConstraints.push_back({ prefix, value });   // (evaluated where the renaming is elaborated)
                        auto* earlier = iDeclaringUnit == nullptr ? find_completion(e, aRegion) : nullptr;
                        auto* subprogram = earlier != nullptr ? earlier : e;
                        subprogram->sub->renamed_access = value;
                        if (earlier != nullptr)
                            earlier->sub->separate_body = true;
                        subprogram->sub->renames = nullptr;
                        subprogram->sub->renamed_name = target;
                        subprogram->sub->body = aDecl;
                        iSubprogramsToGenerate.push_back(subprogram);
                        aDecl->ent = subprogram;
                        if (earlier == nullptr)
                        {
                            e->is_private = aRegion->private_start != static_cast<std::size_t>(-1) && aRegion->declarations.size() >= aRegion->private_start;
                            aRegion->add(e);
                        }
                        break;
                    }
                }
                auto* renamed = resolve_subprogram_name(target, e, aRegion);
                // (a library unit renaming: of a library unit (10.1.1(14)))
                if (iDeclaringUnit != nullptr && iInstanceBodies == 0 && renamed != nullptr && !renamed->library)
                    recover([&]() { error(target, "a library unit renaming declaration must rename a library unit (10.1.1(14))"); });
                // (the predefined "=" of a tagged type that a user-defined "=" (e.g. in its private part) overrides: the operation, whose
                // body is the overriding one (3.9.2(20.3/3), 8.5.4(7.1/1)))
                if (renamed != nullptr && renamed->sub != nullptr && renamed->sub->op == intrinsic::Eq && renamed->sub->operator_type != nullptr)
                    if (auto* tt = renamed->sub->operator_type->view()->base_type()->view(); tt->tagged)
                        for (auto* list : { &tt->primitives, tt->partial != nullptr ? &tt->partial->primitives : nullptr })
                            if (list != nullptr)
                                for (auto* op : *list)
                                    if (op->name == "=" && op->sub != nullptr && op->sub->op == intrinsic::None && op->sub->parameters.size() == 2u &&
                                        same_type(op->sub->parameters[0].type, tt) && same_type(op->sub->parameters[1].type, tt))
                                        renamed = op;
                e->sub->renames = renamed;
                e->sub->renamed_name = target;
                // (a prefixed view: its prefix renameable as an object (not a discriminant-dependent subcomponent, 8.5.4(5.2/4)))
                if (auto const* call = call_of(target); call != nullptr && call->prefixed && call->object != nullptr && iInstanceBodies == 0 &&
                    discriminant_dependent(call->object))
                    recover([&]() { error(target, "the prefix of a renamed prefixed view must be renameable as an object (it depends on "
                        "discriminants of an unconstrained variable, 8.5.4(5.2/4))"); });
                // (mode conformant with the renamed subprogram (8.5.4(4/3)))
                if (auto const* call = call_of(target); renamed != nullptr && renamed->sub != nullptr && (iInstanceBodies == 0 ||
                    iInstanceBodies == iInstancePrivateParts) &&
                    renamed->sub->parameters.size() == e->sub->parameters.size() + (call != nullptr && call->prefixed ? 1u : 0u))
                {
                    std::size_t const skip = call != nullptr && call->prefixed ? 1u : 0u;
                    for (std::size_t i = 0u; i + skip < renamed->sub->parameters.size() && i < e->sub->parameters.size(); ++i)
                        if (renamed->sub->parameters[i + skip].mode != e->sub->parameters[i].mode)
                        {
                            recover([&]() { error(target, "the renaming must be mode conformant with the renamed subprogram"); });
                            break;
                        }
                    // (a null exclusion: only if the renamed subprogram's parameter (or result) excludes null (8.5.4(4.1/2-4.3/2)))
                    auto const excludes = [&](ada_type* aType, bool aNotNull, bool aControlling)
                    {
                        return aNotNull || aControlling || (aType != nullptr && (aType->not_null || aType->view()->not_null));
                    };
                    auto const controlling_access = [&](parameter_info const& p)
                    {
                        auto* c = renamed->sub->controlling;
                        return c != nullptr && p.type != nullptr && p.type->anonymous_access && p.type->view()->designated != nullptr &&
                            p.type->view()->designated->view()->base_type()->view() == c->view()->base_type()->view();
                    };
                    bool bad = false;
                    // (through renamings (an instance's formal subprogram: its actual))
                    auto* ultimate = renamed;
                    for (int depth = 0; depth < 8 && ultimate->sub != nullptr && ultimate->sub->renames != nullptr && ultimate->sub->renames != ultimate &&
                        ultimate->sub->renames->sub != nullptr && ultimate->sub->renames->sub->parameters.size() == renamed->sub->parameters.size(); ++depth)
                        ultimate = ultimate->sub->renames;
                    // (a generic formal subprogram renamed within the generic's body: its null exclusions explicit (8.5.4(4.3/2)))
                    bool const formalInBody = renamed->scope != nullptr && renamed->scope->generic_formals && renamed->sub->renames == nullptr && [&]()
                    {
                        // (within the generic's body: a body region between here and the generic's formal part)
                        for (auto* r = aRegion; r != nullptr && r != renamed->scope; r = r->parent)
                            if (r->package_body || r->subprogram_body)
                                return true;
                        return false;
                    }();
                    auto const explicitly = [&](ada_type* aType, bool aNotNull, bool aControlling)
                    {
                        return aNotNull || aControlling || (aType != nullptr && aType->anonymous_access && aType->view()->not_null);
                    };
                    for (std::size_t i = 0u; i + skip < renamed->sub->parameters.size() && i < e->sub->parameters.size(); ++i)
                    {
                        auto const& mine = e->sub->parameters[i];
                        auto const& declared = renamed->sub->parameters[i + skip];
                        if (!excludes(mine.type, mine.not_null, false))
                            continue;
                        if (formalInBody)
                            bad = bad || !explicitly(declared.type, declared.not_null, controlling_access(declared));
                        else
                        {
                            auto const& theirs = ultimate->sub->parameters[i + skip];
                            bad = bad || !excludes(declared.type, declared.not_null, controlling_access(declared)) ||
                                !excludes(theirs.type, theirs.not_null, controlling_access(theirs));
                        }
                    }
                    if (e->sub->result != nullptr && renamed->sub->result != nullptr && excludes(e->sub->result, e->sub->result_not_null, false))
                    {
                        if (formalInBody)
                            bad = bad || !explicitly(renamed->sub->result, renamed->sub->result_not_null, false);
                        else
                            bad = bad || !excludes(renamed->sub->result, renamed->sub->result_not_null, false) || (ultimate->sub->result != nullptr &&
                                !excludes(ultimate->sub->result, ultimate->sub->result_not_null, false));
                    }
                    if (bad)
                        recover([&]() { error_in_private_part_too(target, "the renamed subprogram's parameter or result must exclude null where the "
                            "renaming's does (8.5.4(4.1/2))"); });
                }
                // (not a subprogram that requires overriding (8.5.4(5.1/2)))
                if (renamed != nullptr && renamed->sub != nullptr && renamed->sub->inherited && renamed->sub->controlling != nullptr &&
                    iInstanceBodies == 0 && requires_overriding(renamed, renamed->sub->controlling))
                {
                    // (not if it has been overridden already)
                    bool overridden = false;
                    auto* ct = renamed->sub->controlling->base_type();
                    for (auto* list : { &ct->primitives, ct->partial != nullptr ? &ct->partial->base_type()->primitives : nullptr,
                        ct->full != nullptr ? &ct->full->base_type()->primitives : nullptr })
                        if (list != nullptr)
                            for (auto* op : *list)
                                if (op != renamed && op->name == renamed->name && op->sub != nullptr && !op->sub->inherited && type_conformant(*op->sub, *renamed->sub))
                                    overridden = true;
                    if (!overridden)
                        recover([&]() { error(target, "a subprogram that requires overriding cannot be renamed"); });
                }
                if (auto* call = call_of(target); call != nullptr && call->prefixed && call->object != nullptr && target->is(construct::Selected) && renamed != nullptr &&
                    renamed->kind != entity_kind::Entry && iDeclaringUnit == nullptr)
                {
                    // (a prefixed view: a subprogram (its body calls the operation with the object and the parameters))
                    e->sub->renames = nullptr;
                    e->sub->body = aDecl;
                    prefixed_view_object(target, aRegion);
                    for (auto* root = call->object; root != nullptr; root = root->children.empty() ? nullptr : root->child(0))
                        if (root->is(construct::Identifier))
                        {
                            if (root->ent != nullptr && root->ent->is_object())
                                note_reference(root->ent, parameters);
                            break;
                        }
                    iSubprogramsToGenerate.push_back(e);
                }
                // (a default expression: its applicable index constraint the renamed subprogram's parameter subtype's (8.5.4(7)); others
                // in an array aggregate requires one (4.3.3(10/5)-(15.1/3)))
                if (renamed != nullptr && renamed->sub != nullptr && e->sub != nullptr && iInstanceBodies == 0 &&
                    renamed->sub->parameters.size() == e->sub->parameters.size())
                    for (std::size_t i = 0u; i < e->sub->parameters.size(); ++i)
                        if (auto* d = e->sub->parameters[i].default_expression; d != nullptr && d->is(construct::Aggregate))
                            if (auto* rt = renamed->sub->parameters[i].type; rt != nullptr && rt->view()->kind == type_kind::Array && !rt->view()->constrained)
                                for (auto* a : d->children)
                                    if (a->is(construct::Association) && a->first(construct::Others) == nullptr &&
                                        a->first(construct::Choices) != nullptr && a->first(construct::Choices)->first(construct::Others) != nullptr)
                                        recover([&]() { error(a, "others is not allowed here (no applicable index constraint: the renamed subprogram's parameter subtype is unconstrained)"); });
                                    else if (a->is(construct::Association) && a->first(construct::Others) != nullptr)
                                        recover([&]() { error(a, "others is not allowed here (no applicable index constraint: the renamed subprogram's parameter subtype is unconstrained)"); });
                if (renamed != nullptr && renamed->sub != nullptr)
                {
                    e->sub->op = renamed->sub->op;
                    e->sub->operator_type = renamed->sub->operator_type;
                    e->sub->import_name = renamed->sub->import_name;
                    if (renamed->kind == entity_kind::EnumerationLiteral)
                        ;
                }
                // (a renaming-as-body completes an earlier declaration)
                if (auto* earlier = find_completion(e, aRegion); earlier != nullptr && iDeclaringUnit == nullptr)
                {
                    // (fully conformant with the declaration; the renamed subprogram subtype conformant with it if it is frozen (here: declared
                    // in another declarative part, e.g. a package specification); not intrinsic, nor abstract (8.5.4(5/3)))
                    if (iInstanceBodies == 0 && earlier->sub != nullptr && e->sub != nullptr)
                    {
                        recover([&]() { conformance_check(*earlier->sub, *e->sub, e->name, aDecl); });
                        // (not renaming the subprogram itself (directly or through renamings): 8.5.4(8.a/1, TC1 8652/0027))
                        std::set<entity*> chain;
                        bool circular = false;
                        for (auto [x, depth] = std::pair{ renamed, 0 }; x != nullptr && x->sub != nullptr && depth < 32; x = x->sub->renames, ++depth)
                        {
                            chain.insert(x);
                            if (x == earlier)
                            {
                                circular = true;
                                break;
                            }
                        }
                        // (unless the subprogram is frozen already: called (through the renamings) before here)
                        if (circular)
                            for (auto const& [callNode, call] : iCalls)
                                if (call.callee != nullptr && chain.contains(call.callee) && callNode->file == aDecl->file &&
                                    callNode->source.data() < aDecl->source.data())
                                {
                                    circular = false;
                                    break;
                                }
                        if (circular)
                            recover([&]() { error(target, "a renaming-as-body cannot rename the subprogram that it completes"); });
                        if (renamed != nullptr && renamed->sub != nullptr)
                        {
                            if (renamed->sub->is_abstract)
                                recover([&]() { error(target, "a renaming-as-body cannot rename an abstract subprogram (8.5.4(5.1/2))"); });
                            else if (earlier->scope != aRegion && ((renamed->sub->op != intrinsic::None && renamed->sub->op != intrinsic::Import) ||
                                renamed->kind == entity_kind::Attribute || renamed->convention == "intrinsic" ||
                                (renamed->scope != nullptr && renamed->scope->generic_formals && renamed->sub->renames == nullptr)))
                                recover([&]() { error(target, "a renaming-as-body cannot rename a subprogram of convention Intrinsic (8.5.4(5/3))"); });
                            else if (earlier->scope != aRegion && !subtype_conformant(*earlier->sub, *renamed->sub))
                                recover([&]() { error(target, "the renamed subprogram must be subtype conformant with the declaration the renaming-as-body "
                                    "completes (8.5.4(5/3))"); });
                        }
                    }
                    earlier->sub->renames = renamed;
                    earlier->sub->renamed_name = target;
                    earlier->sub->body = aDecl;
                    earlier->sub->separate_body = true;     // (calls before the renaming-as-body is elaborated: Program_Error)
                    if (renamed != nullptr && renamed->sub != nullptr)
                    {
                        earlier->sub->op = renamed->sub->op;
                        earlier->sub->operator_type = renamed->sub->operator_type;
                        earlier->sub->import_name = renamed->sub->import_name;
                    }
                    iSubprogramsToGenerate.push_back(earlier);
                    aDecl->ent = earlier;
                    break;
                }
                e->is_private = aRegion->private_start != static_cast<std::size_t>(-1) && aRegion->declarations.size() >= aRegion->private_start;
                aRegion->add(e);
                register_library_unit(e);
                // (a renaming-as-declaration can be a primitive operation (overriding an inherited one))
                bool const overridesImplicit = implicit_homograph(e, aRegion);
                add_primitive_if(e, aRegion);
                implicit_inequality(e, aRegion);
                if (iInstanceBodies == 0)
                    recover([&]() { overriding_indicator_check(e, aDecl, overridesImplicit, aRegion); });
                aDecl->ent = e;
                break;
            }
        default:
            break;
        }
    }

    // --- use clauses and pragmas

    void program::use_clause(node* aClause, region* aRegion)
    {
        if (aClause->is(construct::Use))
        {
            // (the clause's names: resolved before it takes effect (8.4(5/2): its scope starts after it))
            std::vector<entity*> packages;
            for (auto* name : aClause->children)
            {
                auto* e = resolve_entity(name, aRegion);
                if (e == nullptr || !e->is_package())
                    error(name, "package name expected in use clause");
                // (not a generic package (8.4(4))
                if (e->is_generic() && iInstanceBodies == 0)
                    recover([&]() { error(name, "a use clause cannot name a generic package (8.4(4))"); });
                while (e->renamed != nullptr && e->renamed->is_package())
                    e = e->renamed;
                packages.push_back(e);
            }
            for (auto* e : packages)
                aRegion->used.push_back(e);
        }
        else
        {
            bool const all = aClause->has_flag("all");
            for (auto* name : aClause->children)
            {
                if (name->is(construct::Flag))
                    continue;
                auto* t = subtype_mark(name, aRegion);
                aRegion->used_types.push_back(t->base_type());
                if (all)
                    aRegion->used_all_types.push_back(t->base_type());
            }
        }
    }

    void program::prefixed_view_object(node* aView, region* aRegion)
    {
        // (a prefixed view renamed (or a generic actual): its object, if an access value's dereference (e.g. a function's result),
        // the value, evaluated now (8.5.4(13/2), 12.4(10/2)) into a hidden constant)
        auto const found = iCalls.find(aView);
        if (found == iCalls.end() || found->second.object == nullptr)
            return;
        auto& call = found->second;
        auto* object = call.object;
        if (object->type == nullptr || object->type->view()->kind != type_kind::Access ||
            ((object->is(construct::Identifier) || object->is(construct::Selected)) && meaning_of(object) == meaning::Object))
            return;
        auto* value = new_entity(entity_kind::Constant, "%renamed", object, aRegion);
        value->type = object->type;
        value->initial = object;
        value->captured = true;
        aRegion->declarations.push_back(value);
        iDynamicConstraints.push_back({ object, value });   // (evaluated where the renaming (instantiation) is elaborated)
        auto& reference = iClones.emplace_back();
        reference.kind = construct::Identifier;
        reference.source = object->source;
        reference.file = object->file;
        reference.parent = object->parent;
        reference.ent = value;
        reference.type = value->type;
        set_meaning(&reference, meaning::Object);
        for (auto& actual : call.actuals)
            if (actual == object)
                actual = &reference;
        call.object = &reference;
    }

    void program::pragma(node* aPragma, region* aRegion)
    {
        auto const name = name_text(aPragma->child(0)->source);
        std::vector<node*> arguments;
        for (auto* a : aPragma->children)
            if (a->is(construct::PragmaArgument))
                arguments.push_back(a);
        // (the value of an argument (its last child))
        auto const value = [&](std::size_t aIndex) -> node*
        {
            if (aIndex >= arguments.size())
                return nullptr;
            return arguments[aIndex]->children.back();
        };
        // (an argument's value by its name (a named association), else by its position (a positional one))
        auto const argument = [&](std::size_t aIndex, std::string_view aName) -> node*
        {
            for (auto* a : arguments)
                if (a->size() >= 2u && name_text(a->child(0)->source) == aName)
                    return a->children.back();
            if (aIndex < arguments.size() && arguments[aIndex]->size() < 2u)
                return arguments[aIndex]->children.back();
            return nullptr;
        };
        // (a configuration pragma: before the first compilation unit of a compilation (10.1.5(8)))
        static std::set<std::string> const configuration{ "suppress", "unsuppress", "restrictions", "normalize_scalars", "locking_policy",
            "queuing_policy", "task_dispatching_policy", "priority_specific_dispatching", "detect_blocking", "partition_elaboration_policy",
            "profile", "reviewable", "assertion_policy", "default_storage_pool", "suppress_all" };
        if (configuration.contains(name) && aPragma->parent != nullptr && aPragma->parent->is(construct::Context) && iInstanceBodies == 0)
        {
            bool late = false;
            for (auto* c : aPragma->parent->children)
            {
                if (c == aPragma)
                    break;
                late = late || !c->is(construct::Pragma);
            }
            if (auto* unit = aPragma->parent->parent; unit != nullptr && unit->parent != nullptr && !unit->parent->children.empty() &&
                unit->parent->children.front() != unit)
                late = true;
            if (late)
                recover([&]() { error(aPragma, "a configuration pragma must appear before the first compilation unit of a compilation"); });
        }
        if ((name == "priority" || name == "interrupt_priority") && aRegion->owner != nullptr &&
            (aRegion->owner->kind == entity_kind::Task || aRegion->owner->kind == entity_kind::Protected || aRegion->owner->sub != nullptr))
        {
            // (a task's or protected unit's priority (D.1, D.3), or the main subprogram's (the environment task's): evaluated where
            // a task or protected object is created (its discriminants are the object's))
            auto* owner = aRegion->owner;
            owner->interrupt_priority = name == "interrupt_priority";
            owner->priority = value(0);
            if (owner->priority != nullptr)
                resolve(owner->priority, integer(), aRegion);
            return;
        }
        if ((name == "volatile" || name == "atomic" || name == "volatile_components" || name == "atomic_components") && value(0) != nullptr)
        {
            // (the local name (C.6(2-7)): of Atomic and Volatile, an object, a non-inherited component or a full type; of
            // Atomic_Components and Volatile_Components, an array type or an object of an anonymous array type)
            if (iInstanceBodies == 0)
            {
                auto range = aRegion->names.equal_range(name_text(value(0)->source));
                entity* denoted = range.first != range.second ? range.first->second : nullptr;
                bool const components = name == "volatile_components" || name == "atomic_components";
                std::string problem;
                if (denoted == nullptr || (denoted->kind == entity_kind::Component && aRegion->owner != nullptr && denoted->owner != nullptr &&
                    denoted->owner != aRegion->owner))
                    problem = "must name a declaration of this declarative region (not an inherited component)";
                else if (!components && !denoted->is_object() && !denoted->is_type())
                    problem = "must name an object, a component or a full type";
                else if (!components && denoted->is_type() && (denoted->kind != entity_kind::Type || (denoted->type != nullptr &&
                    denoted->type->kind == type_kind::Private && denoted->type->full == nullptr)))
                    problem = "must name a full type (not a subtype or a partial view)";
                else if (components && denoted->is_type() && (denoted->type == nullptr || denoted->type->view()->kind != type_kind::Array ||
                    denoted->kind != entity_kind::Type))
                    problem = "must name an array type";
                else if (components && denoted->is_object() && (denoted->type == nullptr || denoted->type->view()->kind != type_kind::Array ||
                    !denoted->type->name.empty()))
                    problem = "must name an object of an anonymous array type";
                else if (components && !denoted->is_object() && !denoted->is_type())
                    problem = "must name an array type or an object of an anonymous array type";
                // (Atomic, Atomic_Components: of objects (components) whose reads and updates can be indivisible (C.6(10/4)): at most
                // 64 bits)
                if (problem.empty() && denoted != nullptr && (name == "atomic" || name == "atomic_components"))
                {
                    std::function<bool(ada_type*)> indivisible = [&](ada_type* aType) -> bool
                    {
                        if (aType == nullptr)
                            return true;
                        auto* v = aType->view();
                        if (v->is_formal || v->kind == type_kind::Private)
                            return true;
                        if (v->is_scalar() || v->is_access())
                            return object_size(v) <= 8u;
                        if (v->kind == type_kind::Array)
                        {
                            if (!v->constrained || v->indexes.empty())
                                return false;
                            bigint length{ 1 };
                            for (auto* index : v->indexes)
                            {
                                auto* iv = index->view();
                                if (!iv->low.is_integer() || !iv->high.is_integer())
                                    return false;
                                length = length * (iv->high.integer() - iv->low.integer() + bigint{ 1 });
                            }
                            auto const component = object_size(v->component);
                            return component != 0u && length * bigint{ static_cast<std::int64_t>(component) } <= bigint{ 8 };
                        }
                        if (v->kind == type_kind::Record)
                        {
                            auto const size = object_size(v);
                            return size != 0u && size <= 8u;
                        }
                        return false;
                    };
                    auto* t = denoted->type;
                    if (name == "atomic_components")
                        t = t != nullptr && t->view()->kind == type_kind::Array ? t->view()->component : nullptr;
                    if (t != nullptr && !indivisible(t))
                        problem = "names an entity whose (components') reads and updates cannot be indivisible";
                }
                // (a stand-alone constant: imported too (C.6(13.2/3)))
                if (problem.empty() && denoted != nullptr && denoted->kind == entity_kind::Constant && denoted->decl != nullptr &&
                    denoted->decl->parent != nullptr && denoted->decl->parent->is(construct::Object) && !denoted->imported)
                {
                    bool imported = false;
                    if (aPragma->parent != nullptr)
                        for (auto* sibling : aPragma->parent->children)
                            if (sibling->is(construct::Pragma) && !sibling->children.empty() && name_text(sibling->child(0)->source) == "import")
                                for (auto* a : sibling->all(construct::PragmaArgument))
                                    if (!a->children.empty() && name_text(a->children.back()->source) == denoted->name)
                                        imported = true;
                    if (auto* aspects = denoted->decl->parent->first(construct::Aspects))
                        for (auto* aspect : aspects->children)
                            if (!aspect->children.empty() && name_text(aspect->child(0)->source) == "import")
                                imported = true;
                    if (!imported)
                        recover([&]() { error(denoted->decl->parent, "a stand-alone constant specified " + name + " must be imported (C.6(13.2/3))"); });
                }
                if (!problem.empty())
                    recover([&]() { error(value(0), "the name in pragma " + name + " " + problem + " (C.6)"); });
            }
            // (an object's: see volatile_actual (C.6(19/3)); a type's makes it a by-reference type (C.6(18/3)))
            auto range = aRegion->names.equal_range(name_text(value(0)->source));
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->is_object())
                {
                    (name == "volatile" || name == "atomic" ? i->second->volatile_object : i->second->volatile_components) = true;
                    if (name == "atomic")
                        i->second->atomic_object = true;
                    if (name == "atomic_components")
                        i->second->atomic_components = true;
                }
                else if (i->second->is_type() && i->second->type != nullptr)
                    iTypeAspects[i->second->type->base_type()][name] = aPragma;
        }
        if ((name == "interrupt_handler" || name == "attach_handler") && aRegion->owner != nullptr && aRegion->owner->kind == entity_kind::Protected)
        {
            // (a protected object with an interrupt handler: its default ceiling is Interrupt_Priority'Last (D.3(10)))
            if (aRegion->owner->priority == nullptr)
                aRegion->owner->interrupt_priority = true;
            // (the procedure: an interrupt handler; attached to the interrupt (its expression evaluated for each object) (C.3.1))
            if (auto* procedureName = value(0))
            {
                auto range = aRegion->names.equal_range(name_text(procedureName->source));
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->kind == entity_kind::Procedure && i->second->sub != nullptr && i->second->sub->parameters.empty())
                    {
                        iInterruptHandlers.push_back(i->second);
                        if (name == "attach_handler")
                            if (auto* interrupt = value(1))
                            {
                                resolve(interrupt, nullptr, aRegion);   // (of Ada.Interrupts.Interrupt_Id)
                                iAttachHandlers[aRegion->owner].emplace_back(i->second, interrupt);
                            }
                        break;
                    }
            }
        }
        if (name == "import" || name == "interface")
        {
            // pragma Import (Convention, Entity [, External_Name [, Link_Name]])
            auto* conventionNode = value(0);
            auto* entityNode = value(1);
            if (conventionNode == nullptr || entityNode == nullptr)
                return;
            auto const convention = name_text(conventionNode->source);
            auto const designator = name_text(entityNode->source);
            std::string external;
            if (auto* x = argument(2, "external_name"))
                external = external_name(x, aRegion);
            else if (auto* l = argument(3, "link_name"))
                external = external_name(l, aRegion);
            auto designatorName = designator;
            if (designatorName.size() >= 2u && designatorName.front() == '\"')
                designatorName = designatorName.substr(1u, designatorName.size() - 2u);
            auto range = aRegion->names.equal_range(designatorName);
            for (auto i = range.first; i != range.second; ++i)
            {
                auto* e = i->second;
                e->imported = true;
                e->convention = convention;
                if (e->is_object())
                    e->link_name = external.empty() ? e->name : external;
                if (e->sub != nullptr)
                {
                    if (convention == "neos")
                    {
                        e->sub->op = intrinsic::Import;
                        e->sub->import_name = external;
                    }
                    else if (convention == "intrinsic")
                        e->sub->op = intrinsic_operation(e);
                    e->sub->external_name = external;
                    e->sub->body = aPragma;
                    // (a foreign subprogram (no neos program provides it): its body raises Program_Error when it is called)
                    if (convention != "neos" && convention != "intrinsic")
                        iSubprogramsToGenerate.push_back(e);
                }
            }
        }
        else if (name == "export")
        {
            // pragma Export (Convention, Entity [, External_Name [, Link_Name]]) (B.1): its link name, the external name (else the 
            // name, in lower case)
            auto* conventionNode = value(0);
            auto* entityNode = value(1);
            if (conventionNode == nullptr || entityNode == nullptr)
                return;
            std::string external;
            if (auto* x = argument(2, "external_name"))
                external = external_name(x, aRegion);
            else if (auto* l = argument(3, "link_name"))
                external = external_name(l, aRegion);
            auto designatorName = name_text(entityNode->source);
            if (designatorName.size() >= 2u && designatorName.front() == '\"')
                designatorName = designatorName.substr(1u, designatorName.size() - 2u);
            auto range = aRegion->names.equal_range(designatorName);
            for (auto i = range.first; i != range.second; ++i)
            {
                auto* e = i->second;
                if (!e->exported)
                    iExports.push_back(e);
                e->exported = true;
                e->convention = name_text(conventionNode->source);
                e->link_name = external.empty() ? e->name : external;
                if (e->sub != nullptr)
                    e->sub->external_name = e->link_name;
                if (e->is_object())
                    e->aliased = true;  // (in memory: its address is linked)
            }
        }
        else if (name == "convention")
        {
            // (an enumeration type of convention C: its objects are C's int (B.3(65.1/4)))
            if (auto* conventionNode = value(0); conventionNode != nullptr && name_text(conventionNode->source) == "c")
                if (auto* entityNode = value(1))
                {
                    auto range = aRegion->names.equal_range(name_text(entityNode->source));
                    for (auto i = range.first; i != range.second; ++i)
                        if (i->second->is_type() && i->second->type != nullptr)
                            c_convention(i->second->type);
                }
            // (an array type of convention Fortran: column major)
            if (auto* conventionNode = value(0); conventionNode != nullptr && name_text(conventionNode->source) == "fortran")
                if (auto* entityNode = value(1))
                {
                    auto range = aRegion->names.equal_range(name_text(entityNode->source));
                    for (auto i = range.first; i != range.second; ++i)
                        if (i->second->is_type() && i->second->type != nullptr)
                            i->second->type->view()->base_type()->view()->fortran = true;
                }
        }
        else if (name == "no_return")
        {
            // (the procedures (and generic procedures) named: nonreturning (6.5.1))
            for (auto* a : arguments)
            {
                auto range = aRegion->names.equal_range(name_text(a->children.back()->source));
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->sub != nullptr)
                        i->second->sub->no_return = true;
            }
        }
        else if (name == "dispatching_tag")
        {
            // (implementation-defined: the Tag object named (see function_generator::pragma_statement))
            if (auto* v = value(0))
                v->ent = resolve_entity(v, aRegion);
        }
        else if (name == "pack")
        {
            if (auto* t = value(0))
                if (auto* type = subtype_mark(t, aRegion))
                    type->packed = type->view()->base_type()->view()->packed = true;
        }
        else if (name == "unchecked_union")
        {
            if (auto* t = value(0))
                if (auto* type = subtype_mark(t, aRegion))
                    type->view()->base_type()->view()->unchecked_union = true;
        }
        else if (name == "preelaborable_initialization")
        {
            // (the type (its partial view, or a formal type) has preelaborable initialization (10.2.1(11.6/2)))
            if (auto* t = value(0))
                if (auto* type = subtype_mark(t, aRegion))
                    type->preelaborable_initialization = type->base_type()->preelaborable_initialization = true;
        }
        else if (name == "elaborate_body" || name == "pure" || name == "preelaborate" || name == "elaborate" || name == "elaborate_all" ||
            name == "inline" || name == "optimize" || name == "suppress" || name == "unsuppress" || name == "list" || name == "page" ||
            name == "shared_passive" || name == "remote_types" || name == "remote_call_interface" || name == "all_calls_remote")
        {
            if (name == "elaborate_body" || name == "pure" || name == "preelaborate" || name == "inline" || name == "shared_passive" ||
                name == "remote_types" || name == "remote_call_interface" || name == "all_calls_remote")
                recover([&]() { program_unit_pragma_check(aPragma, name, arguments, aRegion); });
        }
        else
            pragma_extra(aPragma, name, arguments, aRegion);
    }

    void program::program_unit_pragma_check(node* aPragma, std::string const& aName, std::vector<node*> const& aArguments, region* aRegion)
    {
        // (a program unit pragma's place (10.1.5(3-7/3)): not in a generic formal part; immediately within a visible part, before any
        // declaration, applying to the enclosing unit; at the place of a declaration, naming subprograms of the same declarative part;
        // after a library subprogram declaration or instantiation, naming it. A library unit pragma: applying to a library unit)
        if (iInstanceBodies != 0 || !iInstantiationSites.empty() || aPragma->parent == nullptr)
            return;
        bool const library = aName != "inline";
        auto* part = aPragma->parent;
        if (part->is(construct::GenericFormals))
            error(aPragma, "pragma " + aName + " is not allowed in a generic formal part");
        if (!library)
            return;
        auto const preceded = [&]()
        {
            for (auto* c : part->children)
            {
                if (c == aPragma)
                    return false;
                if (!c->is(construct::Pragma) && !c->is(construct::Flag))
                    return true;
            }
            return false;
        };
        // (the name of a program unit (its defining name's text))
        auto const unit_name = [&](node* aItem) -> std::string
        {
            for (auto* n = aItem; n != nullptr; )
            {
                if (n->is(construct::Defining) || n->is(construct::DefiningOperator))
                    return name_text(n->source);
                node* next = nullptr;
                for (auto* c : n->children)
                    if (!c->is(construct::Flag) && !c->is(construct::GenericFormals))
                    {
                        next = c;
                        break;
                    }
                n = next;
            }
            return {};
        };
        // (an argument naming a unit: its name, or an expanded name ending with it)
        auto const names = [&](node* aArgument, std::string const& aUnit)
        {
            auto const a = name_text(aArgument->children.back()->source);
            return a == aUnit || (aUnit.size() > a.size() && aUnit.ends_with(a) && aUnit[aUnit.size() - a.size() - 1u] == '.');
        };
        if (part->is(construct::Context))
        {
            // (at the place of a compilation unit: after a library subprogram declaration, generic subprogram declaration or
            // instantiation, naming it)
            node* previous = nullptr;
            if (auto* unit = part->parent; unit != nullptr && unit->parent != nullptr)
                for (auto* u : unit->parent->children)
                {
                    if (u == unit)
                        break;
                    previous = u;
                }
            node* item = previous != nullptr && !previous->children.empty() ? previous->children.back() : nullptr;
            bool const subprogram = item != nullptr && (item->is(construct::SubprogramDeclaration) || item->is(construct::Instantiation) ||
                (item->is(construct::Generic) && item->first(construct::Package) == nullptr));
            if (!subprogram || preceded() || aArguments.empty() || !names(aArguments.front(), unit_name(item)))
                error(aPragma, "pragma " + aName + " must name the library subprogram declaration or instantiation that it follows");
            return;
        }
        auto* unit = part->parent;
        if (part->is(construct::Declarations) && unit != nullptr && unit->is(construct::Package) && !preceded())
        {
            // (immediately within a visible part, first: the enclosing unit, which must be a library unit)
            // (a library unit: a library item; a generic's copy (being checked): its declaration's)
            auto* enclosing = unit->parent != nullptr && unit->parent->is(construct::Generic) ? unit->parent : unit;
            bool const libraryUnit = (aRegion->owner != nullptr && aRegion->owner->library) || enclosing->parent == nullptr ||
                enclosing->parent->is(construct::Unit);
            if (!aArguments.empty() && !names(aArguments.front(), unit_name(unit)))
                error(aPragma, "pragma " + aName + " must name the enclosing library unit");
            else if (!libraryUnit)
                error(aPragma, "pragma " + aName + " applies only to a library unit");
            return;
        }
        // (first in the declarative part of a library subprogram body (a library unit): applying to it)
        if (part->is(construct::Declarations) && unit != nullptr && unit->is(construct::SubprogramBody) && unit->parent != nullptr &&
            unit->parent->is(construct::Unit) && !preceded())
        {
            if (!aArguments.empty())
                if (auto* spec = unit->first(construct::ProcedureSpecification) != nullptr ? unit->first(construct::ProcedureSpecification) :
                    unit->first(construct::FunctionSpecification); spec != nullptr && !spec->children.empty() && !names(aArguments.front(), name_text(spec->child(0)->source)))
                    error(aPragma, "pragma " + aName + " must name the enclosing library unit");
            return;
        }
        error(aPragma, "pragma " + aName + " is not allowed here (it applies only to a library unit, and must be first in its visible part)");
    }

    void program::representation_clause(node* aClause, region* aRegion)
    {
        // (the local name: a direct name (not an expanded name), or one with an attribute (13.1(3)))
        if (iInstanceBodies == 0 && !aClause->children.empty())
        {
            auto* local = aClause->child(0);
            if (aClause->is(construct::RepresentationAttribute) && local->is(construct::Attribute) && !local->children.empty())
                local = local->child(0);
            if (local->is(construct::Selected))
                recover([&]() { error(local, "the local name of a representation item must be a direct name"); });
        }
        // (an enumeration or record representation clause: of a type not frozen yet (13.14(19)))
        if ((aClause->is(construct::RepresentationEnumeration) || aClause->is(construct::RepresentationRecord)) && iInstanceBodies == 0 &&
            iInstantiationSites.empty() && !aClause->children.empty())
            if (auto* e = resolve_entity(aClause->child(0), aRegion); e != nullptr && e->is_type() && e->type != nullptr && frozen_before(e->type, aClause))
                recover([&]() { error(aClause, "a representation item for a type must occur before the type is frozen (13.14(19))"); });
        if (aClause->is(construct::RepresentationAt))
        {
            // (for X use at Address; (J.7): an address clause)
            auto* e = aClause->size() >= 2u ? resolve_entity(aClause->child(0), aRegion) : nullptr;
            // (of an entry: a single entry (J.7.1(5)), in the task's specification (13.1(5/1)))
            if (e != nullptr && e->kind == entity_kind::Entry && iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                resolve(aClause->children.back(), system_type("address"), aRegion);
                if (e->sub != nullptr && e->sub->family_type != nullptr)
                    recover([&]() { error(aClause->child(0), "an address clause cannot be given for an entry family (J.7.1(5))"); });
                else if (e->scope != nullptr && e->scope != aRegion)
                    recover([&]() { error(aClause, "a representation item must be in the declarative region of the entity's declaration"); });
                return;
            }
            if (e != nullptr && e->is_object())
            {
                resolve(aClause->children.back(), system_type("address"), aRegion);
                e->address_clause = aClause->children.back();
            }
            return;
        }
        if (aClause->is(construct::RepresentationAttribute))
        {
            auto* target = aClause->child(0);
            auto* expression = aClause->child(1);
            if (target == nullptr || !target->is(construct::Attribute))
                return;
            auto const attribute = name_text(target->child(1)->source);
            auto* prefix = target->child(0);
            // (T'Class'Read ...: of T's class-wide type)
            auto* e = prefix->is(construct::Attribute) && prefix->size() >= 2u && name_text(prefix->child(1)->source) == "class" ?
                resolve_entity(prefix->child(0), aRegion) : resolve_entity(prefix, aRegion);
            if (e == nullptr)
                return;
            // (the local name: a first subtype, or an object for an object's attributes (13.1(8.1/3)-(10/3), 13.3))
            if (iInstanceBodies == 0)
            {
                // (a specifiable attribute (13.3(5/1)))
                static std::set<std::string> const specifiable{ "address", "alignment", "bit_order", "component_size", "external_tag", "input",
                    "output", "read", "write", "machine_radix", "object_size", "put_image", "size", "small", "storage_pool", "storage_size",
                    "stream_size", "scalar_storage_order", "value_size" };
                if (!specifiable.contains(attribute))
                {
                    recover([&]() { error(target, "the attribute " + attribute + " cannot be specified (13.3(5/1))"); });
                    return;
                }
                if (e->is_object() && attribute != "address" && attribute != "size" && attribute != "alignment" && attribute != "object_size")
                    recover([&]() { error(prefix, "a '" + attribute + " clause cannot be given for an object"); });
                if (e->kind == entity_kind::Subtype)
                    recover([&]() { error(prefix, "a representation clause must name a first subtype"); });
                // (of a type not frozen yet (13.14(19)), operational stream attributes aside)
                if (e->is_type() && e->type != nullptr && iInstantiationSites.empty() && attribute != "put_image" && attribute != "external_tag" &&
                    frozen_before(e->type, aClause))
                    recover([&]() { error(aClause, "a representation item for a type must occur before the type is frozen (13.14(19))"); });
                // (its expression: not freezing the type itself (a static expression with the type's name, 13.14(8/3), 13.1(9.2/5)))
                else if (e->is_type() && e->type != nullptr && iInstantiationSites.empty() && expression != nullptr && attribute != "read" &&
                    attribute != "write" && attribute != "input" && attribute != "output" && attribute != "put_image" && attribute != "external_tag")
                {
                    std::function<bool(node const*)> names = [&](node const* n)
                    {
                        if ((n->is(construct::Attribute) || n->is(construct::Qualified)) && !n->children.empty() && !subtype_attribute(n))
                            if (auto* x = resolve_entity(n->child(0), aRegion); x != nullptr && x->is_type() && x->type != nullptr &&
                                x->type->base_type() == e->type->base_type())
                                return true;
                        for (auto const* c : n->children)
                            if (c != nullptr && names(c))
                                return true;
                        return false;
                    };
                    if (names(expression))
                        recover([&]() { error(aClause, "the representation item's expression freezes the type (13.14(8/3))"); });
                }
                // (a representation item: not of a partial or incomplete view (13.1(9/3)); once for an entity (13.1(10/3)))
                bool const representation = attribute == "size" || attribute == "small" || attribute == "alignment" || attribute == "component_size" ||
                    attribute == "storage_size" || attribute == "bit_order" || attribute == "object_size" || attribute == "machine_radix" ||
                    attribute == "storage_pool";
                if (representation && e->is_type() && e->type != nullptr && e->type->kind == type_kind::Private && !e->type->is_formal &&
                    e->type->full == nullptr)
                    recover([&]() { error(prefix, "a representation item cannot be given for a partial or incomplete view"); });
                bool const classWide = prefix->is(construct::Attribute) && prefix->size() >= 2u && name_text(prefix->child(1)->source) == "class";
                if (!iRepresentationItems.insert({ e, attribute == "storage_pool" ? std::string{ "storage_size" } : classWide ? attribute + "'class" : attribute }).second)
                    recover([&]() { error(aClause, "the '" + attribute + " of \"" + e->name + "\" is already specified"); });
                // (in the declarative region of the entity's declaration (13.1(5/1)))
                if (e->scope != nullptr && e->scope != aRegion)
                    recover([&]() { error(aClause, "a representation item must be in the declarative region of the entity's declaration"); });
                // (Size, Alignment, Component_Size: static integer expressions (13.3(48), 13.3(25/2), 13.3(71)))
                if (attribute == "size" || attribute == "alignment" || attribute == "component_size" || attribute == "object_size")
                    recover([&]()
                    {
                        resolve_in_class(expression, aRegion, [](ada_type const* t) { return t->is_integer(); });
                        bool integer = false;
                        for (auto const& o : interpretations(expression, aRegion))
                            integer = integer || (o.type != nullptr && o.type->view()->is_integer());
                        if (!integer || !static_expression(expression))
                            error(expression, "the expression of a '" + attribute + " clause must be a static integer expression");
                    });
                // (Storage_Size: not of a derived access type (13.11(15)))
                if ((attribute == "storage_size" || attribute == "storage_pool") && e->is_type() && e->type != nullptr &&
                    e->type->view()->kind == type_kind::Access && e->type->base_type()->parent != nullptr)
                    recover([&]() { error(prefix, "a Storage_" + std::string{ attribute == "storage_size" ? "Size" : "Pool" } + " clause cannot be given for a derived access type (13.11(15))"); });
            }
            if (e->is_type())
            {
                auto* t = e->type;
                if (attribute == "size")
                {
                    resolve(expression, nullptr, aRegion);
                    auto v = evaluate(expression, aRegion);
                    // (not too small for the type's values (13.3(55)))
                    if (v.is_integer() && iInstanceBodies == 0)
                    {
                        auto* tv = t->view();
                        auto const bits_for = [](bigint aValue)
                        {
                            std::uint64_t bits = 0u;
                            while (aValue > bigint{ 0 })
                            {
                                aValue = aValue / bigint{ 2 };
                                ++bits;
                            }
                            return bits;
                        };
                        std::uint64_t minimum = 0u;
                        if (tv->is_discrete() && tv->low.is_integer() && tv->high.is_integer() && !(tv->high.integer() < tv->low.integer()))
                        {
                            // (an enumeration type with a representation: its codes)
                            bigint low = tv->low.integer();
                            bigint high = tv->high.integer();
                            if (auto const& codes = tv->base_type()->view()->representation; !codes.empty())
                            {
                                low = *std::min_element(codes.begin(), codes.end());
                                high = *std::max_element(codes.begin(), codes.end());
                            }
                            if (!low.negative())
                                minimum = bits_for(high);
                            else
                                minimum = std::max(bits_for(high), bits_for(bigint{ 0 } - low - bigint{ 1 })) + 1u;
                        }
                        else if (tv->kind == type_kind::Float)
                            minimum = tv->base_type()->size.has_value() ? *tv->base_type()->size * 8u : 32u;
                        else if ((tv->kind == type_kind::Fixed || tv->kind == type_kind::Decimal) && tv->low.known() && tv->high.known() &&
                            tv->base_type()->view()->small.sign() > 0)
                        {
                            // (the bits for the largest multiple of small (and a sign bit if the range has negative values))
                            auto const small = tv->base_type()->view()->small;
                            bigint const l = bigint{ 0 } - (-(tv->low.real() / small)).floor();   // (ceiling)
                            bigint const h = (tv->high.real() / small).floor();
                            minimum = !l.negative() ? bits_for(h) : std::max(bits_for(h), bits_for(bigint{ 0 } - l - bigint{ 1 })) + 1u;
                        }
                        else if (tv->kind == type_kind::Access || tv->kind == type_kind::AccessSubprogram)
                            minimum = 64u;
                        else if ((tv->kind == type_kind::Array || tv->kind == type_kind::Record) && !tv->is_formal)
                        {
                            // (packing can make it smaller: an array's components at least the bits of a discrete component's values; an
                            // untagged record's components outside its variants at least their values' bits)
                            minimum = 0u;
                            if (tv->kind == type_kind::Record && !tv->tagged)
                                for (auto const& c : tv->components)
                                    if (c.variant < 0 && !c.stored && c.type != nullptr)
                                        if (auto* cv = c.type->view(); cv->is_discrete() && cv->low.is_integer() && cv->high.is_integer() &&
                                            !(cv->high.integer() < cv->low.integer()) && cv->base_type()->view()->representation.empty())
                                        {
                                            auto const low = cv->low.integer();
                                            auto const high = cv->high.integer();
                                            minimum += !low.negative() ? bits_for(high) : std::max(bits_for(high), bits_for(bigint{ 0 } - low - bigint{ 1 })) + 1u;
                                        }
                            if (tv->kind == type_kind::Array && tv->constrained && tv->component != nullptr)
                                if (auto* cv = tv->component->view(); cv->is_discrete() && cv->low.is_integer() && cv->high.is_integer() &&
                                    !(cv->high.integer() < cv->low.integer()) && cv->base_type()->view()->representation.empty())
                                {
                                    bigint count{ 1 };
                                    bool known = true;
                                    for (auto* index : tv->indexes)
                                    {
                                        auto* iv = index->view();
                                        if (!iv->low.is_integer() || !iv->high.is_integer())
                                            known = false;
                                        else if (iv->high.integer() < iv->low.integer())
                                            count = bigint{ 0 };
                                        else
                                            count = count * (iv->high.integer() - iv->low.integer() + bigint{ 1 });
                                    }
                                    if (known && count < bigint{ 1000000 })
                                    {
                                        std::uint64_t const bits = !cv->low.integer().negative() ? bits_for(cv->high.integer()) :
                                            std::max(bits_for(cv->high.integer()), bits_for(bigint{ 0 } - cv->low.integer() - bigint{ 1 })) + 1u;
                                        minimum = static_cast<std::uint64_t>(count.to_int64()) * std::max<std::uint64_t>(bits, 1u);
                                    }
                                }
                        }
                        if (minimum != 0u && v.integer() < bigint{ static_cast<std::int64_t>(minimum) })
                            recover([&]() { error(expression, "the size is too small for the values of the type"); });
                    }
                    if (v.is_integer())
                    {
                        // (both views' (a private type's in its private part))
                        t->size_clause = v.integer().to_uint64();
                        if (t->view() != t)
                            t->view()->size_clause = t->size_clause;
                    }
                }
                else if (attribute == "component_size")
                {
                    // (whole storage units, at least the components' size: their storage; smaller (packing): not supported (the
                    // components' size))
                    resolve(expression, nullptr, aRegion);
                    if (auto v = evaluate(expression, aRegion); v.is_integer() && t->view()->base_type()->view()->kind == type_kind::Array)
                    {
                        auto* base = t->view()->base_type()->view();
                        auto const bits = v.integer().to_uint64();
                        if (bits % 8u == 0u && bits / 8u > component_size(base) && bits <= 64u * 8u)
                            base->component_size_clause = bits;
                        else if ((bits == 1u || bits == 2u || bits == 4u) && base->component != nullptr && base->component->view()->is_discrete())
                            base->component_size_clause = bits;     // (bit-packed: see packed_bits)
                    }
                }
                else if (attribute == "alignment")
                {
                    resolve(expression, nullptr, aRegion);
                    if (auto v = evaluate(expression, aRegion); v.is_integer())
                        for (auto* view : { t, t->view() })
                            view->alignment_clause = v.integer().to_uint64();
                }
                else if (attribute == "machine_radix")
                {
                    resolve(expression, nullptr, aRegion);
                    if (auto v = evaluate(expression, aRegion); v.is_integer())
                        t->view()->base_type()->view()->machine_radix = static_cast<std::uint32_t>(v.integer().to_int64());
                }
                else if (attribute == "small")
                {
                    resolve(expression, nullptr, aRegion);
                    // (static (3.5.10(2/1)))
                    if (!static_expression(expression))
                        recover([&]() { error(expression, "the expression of a Small clause must be static"); });
                    auto v = evaluate(expression, aRegion);
                    if (v.known())
                    {
                        t->base_type()->small = v.real();
                        t->small = v.real();
                        fixed_base_range(t);
                    }
                }
                else if (attribute == "storage_size" || attribute == "storage_pool" || attribute == "alignment" || attribute == "component_size" ||
                    attribute == "read" || attribute == "write" || attribute == "input" || attribute == "output" || attribute == "external_tag" ||
                    attribute == "bit_order" || attribute == "object_size" || attribute == "put_image" || attribute == "stream_size")
                    stream_or_other_attribute_clause(aClause, t, attribute, expression, aRegion);
            }
            else if (attribute == "address" && e != nullptr && e->is_object())
            {
                resolve(expression, system_type("address"), aRegion);
                e->address_clause = expression;
            }
            else if (attribute == "size" && e->is_object())
            {
                resolve(expression, nullptr, aRegion);
                if (auto v = evaluate(expression, aRegion); v.is_integer())
                    e->size_clause = v.integer().to_uint64();
            }
            else if (attribute == "alignment" && e->is_object())
            {
                resolve(expression, nullptr, aRegion);
                if (auto v = evaluate(expression, aRegion); v.is_integer())
                    e->alignment_clause = v.integer().to_uint64();
            }
            else if (attribute == "address" || attribute == "size" || attribute == "alignment")
                resolve(expression, nullptr, aRegion);
        }
        else if (aClause->is(construct::RepresentationEnumeration))
        {
            auto* e = resolve_entity(aClause->child(0), aRegion);
            // (of a first subtype (13.4(5), 13.5.1(8)); once (13.1(10/3)); in the region of its declaration)
            if (e != nullptr && iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                if (!e->is_type() || e->kind == entity_kind::Subtype)
                    recover([&]() { error(aClause->child(0), "an enumeration representation clause must name a first subtype"); });
                else if (!iRepresentationItems.insert({ e, std::string{ "enumeration" } }).second)
                    recover([&]() { error(aClause, "the representation of \"" + e->name + "\" is already specified"); });
                else if (e->scope != nullptr && e->scope != aRegion)
                    recover([&]() { error(aClause, "a representation item must be in the declarative region of the entity's declaration"); });
            }
            if (e == nullptr || !e->is_type())
                return;
            auto* t = e->type->view()->base_type()->view();
            auto* aggregate = aClause->child(1);
            // (of an enumeration type (13.4(5)): not of a generic formal discrete type, nor of a type derived from one)
            if (iInstanceBodies == 0 && iInstantiationSites.empty())
                for (auto* x = t; x != nullptr; x = x->parent != nullptr && x->parent->view()->base_type()->view() != x ? x->parent->view()->base_type()->view() : nullptr)
                    if (x->is_formal || x->kind != type_kind::Enumeration)
                    {
                        recover([&]() { error(aClause->child(0), "an enumeration representation clause must name an enumeration type (13.4(5))"); });
                        break;
                    }
            resolve(aggregate, t->kind == type_kind::Enumeration ? enumeration_rep_array(t) : nullptr, aRegion);
            std::vector<bigint> codes;
            for (auto* c : aggregate->children)
            {
                auto* v = c->is(construct::Association) ? c->children.back() : c;
                // (the codes: static (13.4(6)))
                if (!static_expression(v))
                    recover([&]() { error(v, "the codes of an enumeration representation clause must be static"); });
                auto value = evaluate(v, aRegion);
                auto const code = value.is_integer() ? value.integer() : bigint{ static_cast<std::int64_t>(codes.size()) };
                // (named: by its literals' positions)
                bool named = false;
                std::vector<node*> choices;
                if (c->is(construct::Association))
                    for (auto* choice : c->children)
                        if (choice != v)
                        {
                            if (choice->is(construct::Choices))
                                choices.insert(choices.end(), choice->children.begin(), choice->children.end());
                            else
                                choices.push_back(choice);
                        }
                for (auto* choice : choices)
                    if (auto position = evaluate(choice, aRegion); position.is_integer() && !position.integer().negative())
                    {
                        auto const i = static_cast<std::size_t>(position.integer().to_int64());
                        if (codes.size() <= i)
                            codes.resize(i + 1u);
                        codes[i] = code;
                        named = true;
                    }
                if (!named)
                    codes.push_back(code);
            }
            // (increasing with the literals' positions (13.4(10)))
            if (iInstanceBodies == 0 && iInstantiationSites.empty())
                for (std::size_t i = 1u; i < codes.size(); ++i)
                    if (!(codes[i - 1u] < codes[i]))
                    {
                        recover([&]() { error(aggregate, "the codes of an enumeration representation clause must increase with the literals' positions (13.4(10))"); });
                        break;
                    }
            t->representation = codes;
        }
        else if (aClause->is(construct::RepresentationRecord))
        {
            auto* e = resolve_entity(aClause->child(0), aRegion);
            // (of a first subtype (13.4(5), 13.5.1(8)); once (13.1(10/3)); in the region of its declaration)
            if (e != nullptr && iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                if (!e->is_type() || e->kind == entity_kind::Subtype)
                    recover([&]() { error(aClause->child(0), "a record representation clause must name a first subtype"); });
                else if (!iRepresentationItems.insert({ e, std::string{ "record" } }).second)
                    recover([&]() { error(aClause, "the representation of \"" + e->name + "\" is already specified"); });
                else if (e->scope != nullptr && e->scope != aRegion)
                    recover([&]() { error(aClause, "a representation item must be in the declarative region of the entity's declaration"); });
            }
            if (e == nullptr || !e->is_type())
                return;
            auto* t = e->type->view()->base_type()->view();
            bool const check = iInstanceBodies == 0 && iInstantiationSites.empty();
            // (a record type (13.5.1(8)))
            if (check && t->kind != type_kind::Record)
                recover([&]() { error(aClause->child(0), "a record representation clause must name a record type (13.5.1(8))"); });
            // (static integer expressions: the mod clause's, the positions and bits (13.5.1(7), J.8(2)))
            auto const staticInteger = [&](node* aExpression)
            {
                resolve_in_class(aExpression, aRegion, [](ada_type const* x) { return x->is_integer(); });
                if (!check)
                    return;
                bool integer = false;
                for (auto const& o : interpretations(aExpression, aRegion))
                    integer = integer || (o.type != nullptr && o.type->view()->is_integer());
                if (!integer || !static_expression(aExpression))
                    recover([&]() { error(aExpression, "the expression must be a static integer expression (13.5.1(7))"); });
            };
            std::set<std::string> clauses;
            struct place { int variant; std::uint64_t first; std::uint64_t last; };
            std::vector<place> places;
            // (variants: a and b distinct variants of the same variant part (their places can overlap, 13.5.1(11)))
            auto const ancestorOrSelf = [&](int aAncestor, int aVariant)
            {
                for (int v = aVariant; v >= 0; v = t->variants[static_cast<std::size_t>(v)].parent)
                    if (v == aAncestor)
                        return true;
                return aAncestor < 0;
            };
            for (auto* c : aClause->children)
            {
                if (c->is(construct::RepresentationMod) && !c->children.empty())
                {
                    staticInteger(c->child(0));
                    continue;
                }
                if (!c->is(construct::RepresentationComponent))
                    continue;
                auto const name = name_text(c->child(0)->source);
                auto* r = c->child(2);
                staticInteger(c->child(1));
                staticInteger(r->child(0));
                staticInteger(r->child(1));
                auto position = evaluate(c->child(1), aRegion);
                auto first = evaluate(r->child(0), aRegion);
                auto last = evaluate(r->child(1), aRegion);
                component_info* found = nullptr;
                for (auto& component : t->components)
                    if (component.ent != nullptr && component.ent->name == name && !component.stored)
                        found = &component;
                if (check)
                {
                    // (of a component of the type (13.5.1(9)), at most once (13.5.1(10)))
                    if (found == nullptr)
                        recover([&]() { error(c->child(0), "\"" + name + "\" is not a component of the type (13.5.1(9))"); });
                    else if (!clauses.insert(name).second)
                        recover([&]() { error(c->child(0), "the component \"" + name + "\" already has a component clause (13.5.1(10))"); });
                    // (nonnegative position and first bit, last bit at least first bit - 1 (13.5.1(10)))
                    if (position.is_integer() && position.integer().negative())
                        recover([&]() { error(c->child(1), "the position must be nonnegative (13.5.1(10))"); });
                    if (first.is_integer() && first.integer().negative())
                        recover([&]() { error(r->child(0), "the first bit must be nonnegative (13.5.1(10))"); });
                    if (first.is_integer() && last.is_integer() && last.integer() < first.integer() - bigint{ 1 })
                        recover([&]() { error(r->child(1), "the last bit must be at least the first bit - 1 (13.5.1(10))"); });
                }
                if (found != nullptr && position.is_integer() && first.is_integer() && last.is_integer() && !position.integer().negative() &&
                    !first.integer().negative() && !(last.integer() < first.integer() - bigint{ 1 }))
                {
                    auto const bit = position.integer().to_uint64() * 8u + first.integer().to_uint64();
                    auto const size = (last.integer() - first.integer() + bigint{ 1 }).to_uint64();
                    // (the places of components not in distinct variants: not overlapping (13.5.1(11)))
                    if (check && size != 0u && found->variant < static_cast<int>(t->variants.size()))
                        for (auto const& p : places)
                            if ((ancestorOrSelf(p.variant, found->variant) || ancestorOrSelf(found->variant, p.variant)) &&
                                bit <= p.last && p.first <= bit + size - 1u)
                            {
                                recover([&]() { error(c, "the storage places of the components overlap (13.5.1(11))"); });
                                break;
                            }
                    if (size != 0u)
                        places.push_back({ found->variant, bit, bit + size - 1u });
                    found->bit_position = bit;
                    found->bit_size = size;
                }
            }
        }
    }
}
