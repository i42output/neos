/*
  names.cpp

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

// Ada: names (visibility, expanded names, selected components, prefixed views) and the resolution of calls, indexed components,
// slices and type conversions

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        std::string designator_text(node* aNode)
        {
            auto text = name_text(aNode->source);
            if (text.size() >= 2u && (text.front() == '\"' || text.front() == '%'))
                text = text.substr(1u, text.size() - 2u);
            return text;
        }

        bool is_name_node(node const* aNode)
        {
            return aNode->is(construct::Identifier) || aNode->is(construct::Selected) || aNode->is(construct::Operator) || aNode->is(construct::Character);
        }
    }

    std::string program::full_name(entity* aEntity) const
    {
        std::string result = aEntity->name;
        for (auto* r = aEntity->scope; r != nullptr && r->owner != nullptr && r->owner != iStandardPackage; r = r->owner->scope)
        {
            if (r->owner->name.empty() || r->owner->name[0] == '%')
                break;
            result = r->owner->name + "." + result;
            if (r->owner->scope == nullptr)
                break;
        }
        return result;
    }

    bool program::within_scope(region* aScope, region* aFrom) const
    {
        // (a region within another (or it))
        for (region* jump = nullptr, *r = aFrom; r != nullptr; r = outward(r, jump))
            if (r == aScope)
                return true;
        return false;
    }

    bool program::private_part_visible(entity* aPackage, region* aFrom) const
    {
        // (within the package (its private part or body) or a child unit of it)
        for (region* jump = nullptr, *r = aFrom; r != nullptr; r = outward(r, jump))
            if (r->owner == aPackage)
                return true;
        return false;
    }

    bool program::private_part_visible_here(entity* aPackage, region* aFrom) const
    {
        // (strictly: within the package's private part or body, or the private part or body of a (library) child unit of it, or
        // anywhere in a private child)
        bool privateChild = false;
        for (region* jump = nullptr, *r = aFrom; r != nullptr; r = outward(r, jump))
        {
            if (r->owner == aPackage)
                return r->private_visible || r->package_body || privateChild;
            auto const privateUnit = [](entity* aUnit)
            {
                for (node const* n = aUnit->decl; n != nullptr; n = n->parent)
                    if (n->is(construct::Unit))
                        return n->has_flag("private");
                return false;
            };
            // (or of an instance of a library generic (a generic child): as in its generic)
            auto const libraryInstance = [&](entity* aOwner)
            {
                if (aOwner->generic != nullptr)
                    return aOwner->generic->library;
                if (iTemplateInstances.contains(aOwner))
                    for (auto const& [g, i] : iTemplateInstanceOf)
                        if (i == aOwner)
                            return g->library;
                return false;
            };
            if (r->is_package && r->owner != nullptr && (r->owner->library || libraryInstance(r->owner)) &&
                (r->private_visible || r->package_body || privateUnit(r->owner)))
                privateChild = true;
            // (a library child subprogram's body)
            if (r->is_subprogram && r->subprogram_body && r->parent != nullptr && r->parent->level == 0)
                privateChild = true;
        }
        return false;
    }

    bool program::discriminant_dependent(node* aName)
    {
        // (a subcomponent that depends on discriminants of a variable whose nominal subtype is unconstrained (with defaults: mutable),
        // not known to be constrained (8.5.1(5/3), 3.10.2(26/3)))
        if (aName == nullptr || !aName->is(construct::Selected) || aName->size() < 2u)
            return aName != nullptr && aName->is(construct::Apply) && !aName->children.empty() && meaning_of(aName) == meaning::Index &&
                discriminant_dependent(aName->child(0));
        auto* prefix = aName->child(0);
        auto* component = aName->child(1)->ent != nullptr ? aName->child(1)->ent : aName->ent;
        auto* t = prefix->type != nullptr ? prefix->type->view() : nullptr;
        // (a qualified expression of an object: the object's nominal subtype (4.7(4/5)))
        for (auto* q = prefix; q->is(construct::Qualified) && q->size() >= 2u; )
        {
            q = q->child(1);
            while (q->is(construct::Parenthesized) && !q->children.empty())
                q = q->child(0);
            if (!q->is(construct::Qualified) && q->ent != nullptr && q->ent->is_object() && q->ent->type != nullptr)
                t = q->ent->type->view();
        }
        // (a generic formal in out object: its actual may be unconstrained (whatever the formal's subtype, 12.4(9/5)))
        {
            auto* q = prefix;
            while (q->is(construct::Qualified) && q->size() >= 2u)
            {
                q = q->child(1);
                while (q->is(construct::Parenthesized) && !q->children.empty())
                    q = q->child(0);
            }
            if (q->is(construct::Identifier) && q->ent != nullptr && q->ent->decl != nullptr && q->ent->decl->parent != nullptr &&
                q->ent->decl->parent->is(construct::FormalObject) && q->ent->decl->parent->has_flag("out") && t != nullptr &&
                t->kind == type_kind::Record)
                t = t->base_type()->view();
        }
        // (an implicit dereference: the designated object)
        ada_type* through = nullptr;
        if (t != nullptr && t->kind == type_kind::Access && t->designated != nullptr)
        {
            through = t;
            t = t->designated->view();
        }
        // (within a generic body (the prefix's object declared there): formal types assumed the worst)
        bool inGenericBody = false;
        auto const inner = [](node* n) -> node*
        {
            // (the next name inward: a prefix, a qualified expression's or parenthesized expression's operand)
            if (n->is(construct::Qualified))
                return n->size() >= 2u ? n->child(1) : nullptr;
            if (n->is(construct::Selected) || n->is(construct::Apply) || n->is(construct::All) || n->is(construct::Parenthesized))
                return n->children.empty() ? nullptr : n->child(0);
            return nullptr;
        };
        for (auto* n = prefix; n != nullptr && !inGenericBody; n = inner(n))
            if (n->ent != nullptr && n->ent->is_object() && n->ent->scope != nullptr)
            {
                for (auto* r = n->ent->scope; r != nullptr; r = r->parent)
                {
                    if (r->generic_formals)
                        break;
                    if (r->package_body || r->subprogram_body)
                    {
                        for (auto* g = r; g != nullptr; g = g->parent)
                            if (g->generic_formals)
                                inGenericBody = true;
                        break;
                    }
                }
                break;
            }
        if (component != nullptr && component->kind == entity_kind::Component && t != nullptr && t->kind == type_kind::Record &&
            !t->constrained && t->discriminant_constraint.empty())
        {
            bool defaults = (t->is_formal || t->base_type()->view()->is_formal) && inGenericBody;
            component_info const* info = nullptr;
            for (auto const& c : t->base_type()->view()->components)
            {
                if (c.discriminant && c.default_expression != nullptr)
                    defaults = true;
                if (c.ent == component)
                    info = &c;
            }
            // (an array whose bounds are not static: its index constraint names a discriminant)
            auto const dynamic_array = [](ada_type* aType)
            {
                auto* v = aType != nullptr ? aType->view() : nullptr;
                if (v == nullptr || v->kind != type_kind::Array || !v->constrained)
                    return false;
                for (auto* i : v->indexes)
                    if (i != nullptr && (!i->view()->low.known() || !i->view()->high.known()))
                        return true;
                return false;
            };
            bool const dependent = info != nullptr && (info->variant >= 0 || info->depends || dynamic_array(info->type) || (info->type != nullptr &&
                !info->type->view()->discriminant_constraint.empty() && [&]()
                {
                    auto const& values = info->type->view()->discriminant_values;
                    if (values.size() < info->type->view()->discriminant_constraint.size())
                        return true;
                    for (auto const& v : values)
                        if (!v.known())
                            return true;
                    return false;
                }()));
            // (the object: a variable (not a constant), not of a limited type, not a dereference (heap objects are constrained); a
            // qualified expression of a variable is a view of it (4.7(4/5)))
            auto* object = prefix;
            while (object->is(construct::Qualified) && object->size() >= 2u)
            {
                object = object->child(1);
                while (object->is(construct::Parenthesized) && !object->children.empty())
                    object = object->child(0);
            }
            // (a dereference: of a general access type (its designated object may be a declared, unconstrained one, 3.3(23.8/5)))
            if (object->is(construct::All) && !object->children.empty() && object->child(0)->type != nullptr)
                through = object->child(0)->type->view();
            if (through != nullptr)
            {
                // (or a pool-specific one designating a type with a constrained partial view: its allocated objects are unconstrained
                // (3.3(23.10/3)))
                auto* partial = t->base_type()->partial != nullptr ? t->base_type()->partial : t->partial;
                bool const constrainedPartial = partial != nullptr && partial->kind == type_kind::Private && !partial->known_discriminants &&
                    !partial->unknown_discriminants;
                // (a formal access type in a generic body: assumed general (its actual may be))
                bool const general = through->kind == type_kind::Access && (through->access_all || through->access_constant || through->anonymous_access ||
                    constrainedPartial || ((through->is_formal || through->base_type()->view()->is_formal || t->is_formal ||
                    t->base_type()->view()->is_formal) && inGenericBody));
                if (dependent && defaults && general && !t->limited)
                    return true;
                return object->is(construct::All) ? false : discriminant_dependent(prefix);
            }
            bool const variable = (object->is(construct::Identifier) || object->is(construct::Selected) ||
                (object->is(construct::Apply) && meaning_of(object) == meaning::Index)) && (object->ent != nullptr || object->is(construct::Apply)) &&
                is_variable(object) && !t->limited &&
                (object->is(construct::Identifier) ? object->ent->kind != entity_kind::Component : true);
            if (dependent && defaults && variable)
                return true;
        }
        return discriminant_dependent(prefix);
    }

    bool program::partial_view_here(ada_type* aType, region* aRegion)
    {
        // (a partial view whose full view is not visible here (not a formal type's actual within its instance): its
        // characteristics are its own (7.3(4)))
        // (a type derived from one: its parent's full view's characteristics only within its own declarative region, where that full view
        // is visible (7.3.1(5.1/1)))
        if (aType != nullptr && aType->kind != type_kind::Private && aType->view()->derived_partial != nullptr)
            if (auto* p = aType->view()->derived_partial; p->declared_in != nullptr && p->declared_in->owner != nullptr &&
                p->declared_in->owner->is_package() && (!private_part_visible(p->declared_in->owner, aRegion) ||
                    (aType->view()->base_type()->declared_in != nullptr && aRegion != nullptr && iInstanceBodies == 0 &&
                     !within_scope(aType->view()->base_type()->declared_in, aRegion))))
                return true;
        auto* partial = aType == nullptr ? nullptr : aType->kind == type_kind::Private ? aType :
            aType->base != nullptr && aType->base->kind == type_kind::Private ? aType->base : nullptr;
        if (partial == nullptr || partial->full == nullptr || partial->incomplete || partial->is_formal || formal_actual_instance(partial, aRegion) != nullptr ||
            formal_actual_instance(aType->view()->base_type()->view(), aRegion) != nullptr)
            return false;
        auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
        return package != nullptr && package->is_package() && !private_part_visible(package, aRegion);
    }

    bool program::is_variable(node* aName, int aDepth)
    {
        // (a view of a variable (3.3): a variable (not a constant, an in parameter, a loop parameter or a discriminant), an out or in out
        // parameter, a component, slice or view conversion of a variable, a dereference of an access-to-variable value; a renaming of
        // one; when in doubt, true)
        if (aName == nullptr || aDepth > 32)
            return true;
        // (a generalized reference (implicitly dereferenced): a variable view unless its access discriminant is access-to-constant (4.1.5(3/3)))
        if (auto* reference = implicit_dereference_of(aName); reference != nullptr)
            if (auto* aspect = type_aspect(reference, "implicit_dereference"); aspect != nullptr)
                for (auto const& c : reference->view()->components)
                    if (c.discriminant && c.ent != nullptr && c.ent->name == name_text(aspect->source) && c.type != nullptr && c.type->view()->kind == type_kind::Access)
                        return !c.type->view()->access_constant;
        auto const object = [&](entity* e) -> bool
        {
            switch (e->kind)
            {
            case entity_kind::Constant:
            case entity_kind::LoopParameter:
            case entity_kind::Discriminant:
            case entity_kind::NamedNumber:
                return false;
            case entity_kind::Parameter:
                {
                    auto* owner = e->scope != nullptr ? e->scope->owner : nullptr;
                    // (an accept statement's parameter: its entry's)
                    if (owner == nullptr && e->scope != nullptr)
                        if (auto a = iAcceptEntries.find(e->scope); a != iAcceptEntries.end() && a->second->sub != nullptr && e->index >= 0 &&
                            static_cast<std::size_t>(e->index) < a->second->sub->parameters.size())
                        {
                            auto const mode = a->second->sub->parameters[static_cast<std::size_t>(e->index)].mode;
                            return mode == parameter_mode::Out || mode == parameter_mode::InOut;
                        }
                    if (owner != nullptr && owner->sub != nullptr)
                        for (auto const& p : owner->sub->parameters)
                            if (p.ent == e)
                                return p.mode == parameter_mode::Out || p.mode == parameter_mode::InOut;
                    return true;
                }
            case entity_kind::Variable:
                if (e->where == storage::Renaming && e->renamed_name != nullptr)
                    return is_variable(e->renamed_name, aDepth + 1);
                return true;
            default:
                return true;
            }
        };
        auto const dereference = [&](ada_type* aType)
        {
            auto* v = aType != nullptr ? aType->view() : nullptr;
            return !(v != nullptr && v->kind == type_kind::Access && v->access_constant);
        };
        // (a value: an enumeration literal, a named number, a function call (not of a reference type, 4.1.5))
        auto const value = [&](entity* e)
        {
            if (e == nullptr)
                return false;
            if (e->kind == entity_kind::EnumerationLiteral || e->kind == entity_kind::NamedNumber)
                return true;
            if (e->kind == entity_kind::Function && e->sub != nullptr && e->sub->result != nullptr)
            {
                auto* reference = type_aspect(e->sub->result, "implicit_dereference");
                if (reference == nullptr)
                    return true;
                // (a generalized reference whose access discriminant is access-to-constant: a constant view (4.1.5(3/3)))
                for (auto const& c : e->sub->result->view()->components)
                    if (c.discriminant && c.ent != nullptr && c.ent->name == name_text(reference->source) && c.type != nullptr &&
                        c.type->view()->kind == type_kind::Access && c.type->view()->access_constant)
                        return true;
                return false;
            }
            return false;
        };
        switch (aName->kind)
        {
        case construct::Identifier:
            if (value(aName->ent))
                return false;
            // (a component of the current instance of a protected unit within a protected function, or the current instance: a constant
            // (9.5.1(2)))
            if (auto* e = aName->ent; e != nullptr && iCurrentSubprogram != nullptr && iCurrentSubprogram->kind == entity_kind::Function &&
                iCurrentSubprogram->scope != nullptr && iCurrentSubprogram->scope->owner != nullptr &&
                iCurrentSubprogram->scope->owner->kind == entity_kind::Protected)
            {
                auto* unit = iCurrentSubprogram->scope->owner;
                auto const same = [&](entity* x) { return x != nullptr && (x == unit || (x->type != nullptr && x->type == unit->type)); };
                if (e->kind == entity_kind::Component && e->scope != nullptr && same(e->scope->owner))
                    return false;
                if ((e->kind == entity_kind::Protected || e->is_type()) && same(e))
                    return false;
            }
            return aName->ent == nullptr || !aName->ent->is_object() || object(aName->ent);
        case construct::Selected:
            {
                auto* prefix = aName->child(0);
                if (aName->ent != nullptr && aName->ent->kind == entity_kind::Discriminant)
                    return false;
                if (value(aName->ent))
                    return false;
                if (aName->ent != nullptr && aName->ent->is_object() && aName->ent->kind != entity_kind::Component)
                    return object(aName->ent);
                if (prefix->type != nullptr && prefix->type->view()->kind == type_kind::Access)
                    return dereference(prefix->type);
                if (prefix->ent != nullptr && (prefix->ent->is_package() || prefix->ent->is_generic()))
                    return true;
                return is_variable(prefix, aDepth + 1);
            }
        case construct::All:
            return dereference(aName->child(0)->type);
        case construct::Apply:
            {
                auto const m = meaning_of(aName);
                if (m == meaning::Conversion)
                    return is_variable(aName->children.back(), aDepth + 1);
                if (m == meaning::Call)
                {
                    // (a function's result: a constant, unless of a reference type (generalized reference))
                    if (auto const* call = call_of(aName); call != nullptr && call->callee != nullptr && value(call->callee))
                        return false;
                    return true;
                }
                auto* prefix = aName->child(0);
                if (prefix->type != nullptr && prefix->type->view()->kind == type_kind::Access)
                    return dereference(prefix->type);
                return is_variable(prefix, aDepth + 1);
            }
        case construct::Parenthesized:
        case construct::Qualified:
        case construct::Null:
        case construct::Aggregate:
        case construct::Attribute:
        case construct::Number:
        case construct::Character:
        case construct::Operator:
            return false;
        case construct::Allocator:
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::Divide:
        case construct::Concat:
            return false;
        default:
            return true;
        }
    }

    // --- visibility

    bool program::withed_here(std::string const& aUnit)
    {
        // (a library unit that a with clause whose scope includes the current unit names (10.1.2(5)): the unit's own, its declaration's
        // (a body's), its parent bodies' and their declarations' (a subunit's); a with clause naming a child names its ancestors too)
        auto const names = [&](unit_info const* aInfo)
        {
            if (aInfo != nullptr)
                for (auto const& w : aInfo->withs)
                    if (w == aUnit || w.starts_with(aUnit + "."))
                        return true;
            return false;
        };
        for (unit_info* u = iCurrentUnit; u != nullptr;)
        {
            if (names(u))
                return true;
            if (u->body)
                if (auto* spec = find_unit(u->name, false); spec != nullptr && spec != u && names(spec))
                    return true;
            if (!u->subunit)
                break;
            auto const dot = u->name.rfind('.');
            auto* parent = dot == std::string::npos ? nullptr : find_unit(u->name.substr(0u, dot), true);
            u = parent != u ? parent : nullptr;
        }
        return false;
    }

    bool program::private_with_hidden(entity* aUnit, region* aRegion)
    {
        // (a name denoting a library unit visible only by private with clauses (of the current unit's declaration or its ancestors'):
        // only in a private part, a body, a private descendant (10.1.2(12/3-16/2)))
        if (aUnit == nullptr || !(aUnit->library || (aUnit->limited_view && aUnit->is_package())) || iCurrentUnit == nullptr || iCurrentUnit->subunit || iPrivateWithVisible ||
            (iCurrentUnit->body && !(iCurrentUnit->item != nullptr && iCurrentUnit->item->is(construct::SubprogramBody) &&
            find_unit(iCurrentUnit->name, false) == nullptr)) ||
            iCurrentUnit->unit == nullptr || iCurrentUnit->unit->has_flag("private") || iInstanceBodies > 0 ||
            (iContextOverride != nullptr && iTemplateUnit != iCurrentUnit))
            return false;
        auto const name = full_name(aUnit);
        if (iCurrentUnit->name == name || iCurrentUnit->name.starts_with(name + "."))
            return false;
        bool privately = false;
        for (auto unit = iCurrentUnit->name;;)
        {
            auto* u = find_unit(unit, false);
            if (u == nullptr && unit == iCurrentUnit->name)
                u = iCurrentUnit;   // (a library subprogram body without a declaration)
            if (u != nullptr && u->unit != nullptr)
                if (auto* context = u->unit->first(construct::Context))
                    for (auto* clause : context->children)
                        if (clause->is(construct::With))
                            for (auto* n : clause->children)
                                if (!n->is(construct::Flag))
                                {
                                    std::string mentioned;
                                    for (auto* p = n; p != nullptr; p = p->is(construct::Selected) && p->size() == 2u ? p->child(0) : nullptr)
                                        mentioned = mentioned.empty() ? name_text(p->source) : mentioned;
                                    std::function<std::string(node*)> dotted = [&](node* x) -> std::string
                                    {
                                        return x->is(construct::Selected) && x->size() == 2u ? dotted(x->child(0)) + "." + name_text(x->child(1)->source) :
                                            name_text(x->source);
                                    };
                                    auto const d = dotted(n);
                                    if (d == name || d.starts_with(name + "."))
                                    {
                                        if (!clause->has_flag("private"))
                                            return false;
                                        privately = true;
                                    }
                                }
            if (u != nullptr && u->unit != nullptr && u->unit->has_flag("private"))
                return false;   // (a private descendant)
            auto const dot = unit.rfind('.');
            if (dot == std::string::npos)
                break;
            unit = unit.substr(0u, dot);
        }
        if (!privately)
            return false;
        // (here: a context clause of the unit, or its visible part (not a private part or a body))
        if (aRegion == iCurrentUnit->context)
            return true;
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
        {
            if (r->package_body || r->subprogram_body || (r->is_package && r->private_visible))
                return false;
            if (r->owner != nullptr && ((iCurrentUnit->ent != nullptr && r->owner == iCurrentUnit->ent) ||
                ((r->owner->library || r->parent == iStandard || (r->parent != nullptr && r->parent->owner != nullptr && r->parent->owner->library)) &&
                full_name(r->owner) == iCurrentUnit->name)))
                return true;
        }
        return false;
    }

    void program::incomplete_prefix_check(node* aPrefix, ada_type* aType, bool aDereference)
    {
        // (a dereference (implicit, or explicit) of an access-to-incomplete type (before its completion): not a prefix (3.10.1(10/3)))
        if (iSpeculation > 0 || iInstanceBodies > 0 || aPrefix == nullptr)
            return;
        ada_type* access = aDereference ? aType : aPrefix->is(construct::All) && !aPrefix->children.empty() ? aPrefix->child(0)->type : nullptr;
        ada_type* d = nullptr;
        if (access != nullptr && access->view()->kind == type_kind::Access)
            d = access->view()->designated;
        // (a name of an object of an incomplete view (e.g. a parameter of a tagged incomplete type))
        else if (!aDereference && aPrefix->ent != nullptr && aPrefix->ent->is_object() && (aPrefix->is(construct::Identifier) ||
            aPrefix->is(construct::Selected)))
            d = aPrefix->ent->type;
        if (d == nullptr)
            return;
        if (d->kind == type_kind::ClassWide && d->specific != nullptr)
            d = d->specific;
        for (auto* t : { d, d->base_type() })
        {
            // (a limited view's type: incomplete unless the package is named by a (nonlimited) with clause here)
            auto* view = t->decl != nullptr && t->decl->scope != nullptr && t->decl->scope->owner != nullptr && t->decl->scope->owner->limited_view ?
                t->decl->scope->owner : nullptr;
            if (t->kind == type_kind::Private && t->incomplete && (t->full == nullptr || (view != nullptr && !withed_here(view->name) &&
                !(iCurrentUnit != nullptr && (iCurrentUnit->name == view->name || iCurrentUnit->name.starts_with(view->name + "."))))))
                error(aPrefix, "a dereference of an access-to-incomplete type cannot be used as a prefix (3.10.1(10/3))");
        }
    }

    std::vector<entity*> program::visible(std::string const& aName, region* aRegion)
    {
        std::vector<entity*> result;
        auto const homograph = [&](entity* aEntity)
        {
            if (!aEntity->overloadable())
                return false;
            for (auto* e : result)
                if (e->overloadable() && e->name == aEntity->name && e->sub != nullptr && aEntity->sub != nullptr && type_conformant(*e->sub, *aEntity->sub) &&
                    !formal_distinct(*e->sub, *aEntity->sub))
                    return true;
                else if (e->kind == entity_kind::EnumerationLiteral && aEntity->kind == entity_kind::EnumerationLiteral && same_type(e->type, aEntity->type))
                    return true;
            return false;
        };
        bool stop = false;
        region* last = nullptr;
        // (an instance's formal region's parent: only the declarations before the generic's (or its body))
        auto const before = [](entity* aEntity, node* aCutoff)
        {
            if (aCutoff == nullptr || aCutoff->parent == nullptr)
                return true;
            auto* d = aEntity->decl;
            while (d != nullptr && d->parent != aCutoff->parent)
                d = d->parent;
            if (d == nullptr)
                return true;
            auto const& siblings = aCutoff->parent->children;
            return std::find(siblings.begin(), siblings.end(), d) < std::find(siblings.begin(), siblings.end(), aCutoff);
        };
        std::vector<entity const*> specOwners;  // (the owners of the (non-formal) regions passed: a generic's spec within its check instance)
        for (region* jump = nullptr, *r = aRegion; r != nullptr && !stop; r = outward(r, jump))
        {
            // (a generic unit's name in its formal part: hidden from all visibility, hiding outer declarations of the name (8.3(16, 19)))
            if (r->generic_formals && r->owner != nullptr && r->owner->name == aName && iTemplateInstances.contains(r->owner) &&
                std::find(specOwners.begin(), specOwners.end(), r->owner) == specOwners.end())
                return {};
            if (!r->generic_formals && r->owner != nullptr)
                specOwners.push_back(r->owner);
            node* const cutoff = last != nullptr && last->generic_formals && last->parent == r ? last->visibility_cutoff :
                r->subunit_view ? r->visibility_cutoff : nullptr;
            bool const stubCutoff = cutoff != nullptr && r->subunit_view && cutoff == r->visibility_cutoff;
            last = r;
            auto range = r->names.equal_range(aName);
            std::vector<entity*> here;
            for (auto i = range.first; i != range.second; ++i)
                here.push_back(i->second);
            // (in declaration order)
            std::sort(here.begin(), here.end(), [](entity* a, entity* b) { return a->sequence < b->sequence; });
            for (auto* e : here)
            {
                // (a declaration within itself: hidden from all visibility, hiding its homographs (8.3(16)); a task or protected unit's
                // name within it: its current instance (8.6(17/3)))
                if (e->declaring && !e->overloadable())
                {
                    if (e->kind == entity_kind::Task || e->kind == entity_kind::Protected || (e->is_type() && e->type != nullptr &&
                        (e->type->kind == type_kind::Task || e->type->kind == type_kind::Protected)))
                        return { e };
                    return {};
                }
                // (a subunit's view of its stub's region: its stub (its own declaration) and what precedes it)
                if (!e->visible_in_body || instance_hidden(e, aRegion) || (!before(e, cutoff) && !(stubCutoff && e->decl != nullptr &&
                    [&]() { for (node const* n = e->decl; n != nullptr; n = n->parent) if (n == cutoff) return true; return false; }())))
                    continue;
                // (a library unit (declared in Standard): visible only where a with clause names it (its context's region, found
                // before Standard), and in itself and its descendants (10.1.6(2), 8.3))
                if ((e->library || e->limited_view) && private_with_hidden(e, aRegion))
                    continue;
                if (r == iStandard && e->library && iInstanceBodies == 0 && iContextOverride == nullptr && iCurrentUnit != nullptr &&
                    !iCurrentUnit->name.empty() && iCurrentUnit->name != "standard" && iCurrentUnit->name != e->name &&
                    !iCurrentUnit->name.starts_with(e->name + ".") && !withed_here(e->name) && std::getenv("NEOS_ADA_LIBRARY_VISIBLE") == nullptr)
                    continue;
                // (a declaration of a package's private part: directly visible in its private part and body, and in the private parts
                // and bodies of its children (a private child's visible part too), not in a public child's visible part (8.2(5-8)))
                if (e->is_private && r->is_package && r->owner != nullptr && r->owner->is_package() && r != aRegion && iInstanceBodies == 0 &&
                    [&]()
                    {
                        // (from a child unit (the lookup passed through another library unit's region))
                        for (region* j = nullptr, *q = aRegion; q != nullptr && q != r; q = outward(q, j))
                            if (q->owner != nullptr && q->owner->library && q->owner != r->owner)
                                return true;
                        return false;
                    }() && !private_part_visible_here(r->owner, aRegion))
                    continue;
                if (e->sub != nullptr && e->sub->hidden_by != nullptr && std::find(here.begin(), here.end(), e->sub->hidden_by) != here.end())
                    continue;   // (an inherited operation hidden by an overriding one in the private part)
                // (an enumeration literal hidden by a parameterless function of its type in the same region (explicit, or inherited
                // from one that overrode the parent's literal), e.g. a derived type's inherited literal (8.3(12/3)))
                if (e->kind == entity_kind::EnumerationLiteral &&
                    std::any_of(here.begin(), here.end(), [&](entity* f) { return f != e && f->sub != nullptr &&
                        f->sub->parameters.empty() && f->sub->result != nullptr && f->kind == entity_kind::Function && same_type(f->sub->result, e->type) &&
                        f->visible_in_body; }))
                    continue;
                if (e->overloadable())
                {
                    // (an explicit declaration overrides an implicitly declared homograph (a predefined operator) of the same 
                    // declarative region (8.3(10/1, 12/3)))
                    auto const implicit = [](entity* x) { return x->sub != nullptr && x->sub->op != intrinsic::None && x->sub->op != intrinsic::NotEqual &&
                        x->sub->op != intrinsic::Import && x->sub->renames == nullptr; };
                    if (!implicit(e) && e->sub != nullptr)
                        std::erase_if(result, [&](entity* x) { return x->scope == e->scope && implicit(x) && x->sub != nullptr && x->name == e->name &&
                            type_conformant(*x->sub, *e->sub); });
                    if (!homograph(e))
                        result.push_back(e);
                }
                else
                {
                    if (result.empty())
                        return { e };
                    // (a generic subprogram within its instance: its name denotes the instance (found already, in the instance's
                    // formal region), not hiding the outer declarations that are not homographs of it (8.3(18/2)))
                    if (e->is_generic() && std::any_of(result.begin(), result.end(), [](entity* x) { return x->scope != nullptr && x->scope->generic_formals; }))
                        continue;
                    stop = true;
                }
            }
        }
        (void)last;
        // (within a library package (its body, or a child's): its child units that are named by with clauses, by their simple
        // names (8.1(4), 10.1.6(3)))
        if (result.empty() && !stop)
            for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            {
                if (r->owner != nullptr && r->owner->is_package() && r->owner->library)
                    if (auto* child = library_unit(full_name(r->owner) + "." + aName); child != nullptr && !private_with_hidden(child, aRegion))
                        return { child };
                // (within an instance of a generic library package: the generic's generic child units (10.1.1(19)))
                if (r->owner != nullptr && r->owner->is_package() && r->owner->generic != nullptr && r->owner->generic->library)
                    if (auto* child = library_unit(full_name(r->owner->generic) + "." + aName); child != nullptr && child->is_generic())
                        return { child };
            }
        // (the units that the unit's context clauses name)
        if (!stop)
            if (auto* context = current_context())
            {
                auto range = context->names.equal_range(aName);
                for (auto i = range.first; i != range.second; ++i)
                {
                    if ((i->second->library || i->second->limited_view) && private_with_hidden(i->second, aRegion))
                        continue;
                    if (i->second->overloadable())
                    {
                        if (!homograph(i->second))
                            result.push_back(i->second);
                    }
                    else if (result.empty())
                        return { i->second };
                }
            }
        // (use-visible declarations: overloadable ones that are not hidden (by a directly visible homograph: homographs that are
        // both use-visible are both visible, 8.4(11)); others only if nothing else is visible)
        auto const used = use_visible(aName, aRegion);
        std::vector<entity*> nonOverloadable;
        auto const direct = result.size();
        for (auto* e : used)
        {
            if (e->overloadable())
            {
                bool hidden = false;
                for (std::size_t i = 0u; i < direct && !hidden; ++i)
                    hidden = result[i]->overloadable() && result[i]->sub != nullptr && e->sub != nullptr && type_conformant(*result[i]->sub, *e->sub) &&
                        !formal_distinct(*result[i]->sub, *e->sub);
                if (!stop && !hidden && std::find(result.begin(), result.end(), e) == result.end())
                    result.push_back(e);
            }
            // (a partial view's (or deferred constant's) completion: the same declaration's view)
            else if (std::find(nonOverloadable.begin(), nonOverloadable.end(), e) == nonOverloadable.end() &&
                std::none_of(nonOverloadable.begin(), nonOverloadable.end(), [&](entity* f) { return f->completion == e || e->completion == f ||
                    (f->scope == e->scope && f->is_type() && e->is_type() && f->type != nullptr && e->type != nullptr &&
                        (f->type->full == e->type || e->type->full == f->type || f->type->base_type() == e->type->base_type())); }))
                nonOverloadable.push_back(e);
        }
        // (potentially use-visible declarations of the identifier: not use-visible unless each is overloadable (8.4(11)))
        if (result.empty() && nonOverloadable.size() == 1u)
            return { nonOverloadable.front() };
        // (a generic subprogram: within its body, its name denotes its current instance (a subprogram))
        if (std::any_of(nonOverloadable.begin(), nonOverloadable.end(), [](entity* x) { return x->kind != entity_kind::GenericProcedure &&
            x->kind != entity_kind::GenericFunction; }) && result.size() > direct)
            result.resize(direct);
        return result;
    }

    std::vector<entity*> program::use_visible(std::string const& aName, region* aRegion)
    {
        std::vector<entity*> result;
        std::vector<entity*> packages;
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            packages.insert(packages.end(), r->used.begin(), r->used.end());
        if (auto* context = current_context())
            packages.insert(packages.end(), context->used.begin(), context->used.end());
        std::vector<entity*> seen;
        for (auto* p : packages)
        {
            while (p->renamed != nullptr && p->renamed->is_package())
                p = p->renamed;
            if (std::find(seen.begin(), seen.end(), p) != seen.end() || p->inner == nullptr)
                continue;
            seen.push_back(p);
            bool const privateVisible = private_part_visible(p, aRegion);
            auto range = p->inner->names.equal_range(aName);
            bool found = false;
            for (auto i = range.first; i != range.second; ++i)
                if ((!i->second->is_private || privateVisible) && i->second->visible_in_body &&
                    !(privateVisible && i->second->sub != nullptr && i->second->sub->hidden_by != nullptr))
                {
                    result.push_back(i->second);
                    found = true;
                }
            // (an instance's actual parameters (its formals' names): visible as a formal package's (12.7(10)), unless its actual part is
            // explicit (12.7(10/2)))
            if (!found && p->inner->parent != nullptr && p->inner->parent->generic_formals && p->inner->parent->owner == p && !iFormalPackagesWithActuals.contains(p))
            {
                auto formals = p->inner->parent->names.equal_range(aName);
                for (auto i = formals.first; i != formals.second; ++i)
                    if (i->second->kind != entity_kind::Package || i->second->renamed != p)
                        result.push_back(i->second);
            }
            // (child units of the package that are withed)
            if (auto* child = library_unit(full_name(p) + "." + aName); child != nullptr)
                if (current_context() != nullptr && !private_with_hidden(child, aRegion))
                    result.push_back(child);
        }
        // (use type clauses: the types' primitive operators; use all type: all their primitive operations (and literals) (8.4(8/3)))
        std::vector<std::pair<ada_type*, bool>> types;
        auto const addTypes = [&](region* aUses)
        {
            for (auto* t : aUses->used_types)
                types.emplace_back(t, std::find(aUses->used_all_types.begin(), aUses->used_all_types.end(), t) != aUses->used_all_types.end());
        };
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            addTypes(r);
        if (auto* context = current_context())
            addTypes(context);
        bool const isOperator = !aName.empty() && !std::isalpha(static_cast<unsigned char>(aName[0])) && aName[0] != '\'';
        for (auto const& [type, all] : types)
        {
            if (!all && !isOperator)
                continue;
            auto const consider = [&](entity* e)
            {
                if (e != nullptr && e->name == aName && std::find(result.begin(), result.end(), e) == result.end())
                    result.push_back(e);
            };
            for (auto* view : { type, type->full, type->partial })
            {
                if (view == nullptr)
                    continue;
                auto* v = view->view();
                if (all && v->kind == type_kind::Enumeration)
                    for (auto* literal : v->literals)
                        consider(literal);
                for (auto* op : view->primitives)
                    if (op->sub == nullptr || !op->sub->undeclared)
                        consider(op);
            }
            // (use all type: also the subprograms declared where an ancestor of the type (or the type) is that operate on a
            // class-wide type that covers it (8.4(8.1/3)))
            if (all && type->view()->base_type()->view()->tagged)
            {
                auto* specific = type->view()->base_type()->view();
                for (auto* a = specific; a != nullptr;)
                {
                    for (auto* where : { a->declared_in, a->partial != nullptr ? a->partial->declared_in : nullptr })
                        if (where != nullptr)
                        {
                            auto range = where->names.equal_range(aName);
                            for (auto i = range.first; i != range.second; ++i)
                                if (auto* e = i->second; e->sub != nullptr)
                                {
                                    bool classWide = false;
                                    for (auto const& p : e->sub->parameters)
                                        classWide = classWide || (p.type != nullptr && p.type->view()->kind == type_kind::ClassWide && is_descendant(specific, p.type));
                                    if (e->sub->result != nullptr && e->sub->result->view()->kind == type_kind::ClassWide && is_descendant(specific, e->sub->result))
                                        classWide = true;
                                    if (classWide)
                                        consider(e);
                                }
                        }
                    auto* p = a->parent != nullptr ? a->parent->view()->base_type()->view() : nullptr;
                    a = p != a ? p : nullptr;
                }
            }
        }
        return result;
    }

    std::vector<entity*> program::selected(entity* aPrefix, std::string const& aName, region* aRegion)
    {
        // (a declaration within itself: hidden from all visibility (8.3(16)))
        auto result = selected_declared(aPrefix, aName, aRegion);
        std::erase_if(result, [](entity* e) { return e->declaring; });
        return result;
    }

    std::vector<entity*> program::selected_declared(entity* aPrefix, std::string const& aName, region* aRegion)
    {
        std::vector<entity*> result;
        auto* prefix = aPrefix;
        while (prefix->renamed != nullptr && (prefix->renamed->is_package() || prefix->renamed->is_generic()))
            prefix = prefix->renamed;
        if (prefix->inner == nullptr && prefix->sub != nullptr && prefix->sub->renames != nullptr && prefix->scope != nullptr && prefix->scope->generic_formals &&
            prefix->sub->renames->inner != nullptr)
        {
            // (within a subprogram instance the generic's name denotes the instance: its formals, parameters and declarations by
            // selection (4.1.3(13)))
            for (auto* r : { prefix->sub->renames->inner, prefix->scope })
            {
                auto range = r->names.equal_range(aName);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second != prefix)
                        result.push_back(i->second);
            }
            return result;
        }
        if (prefix->inner == nullptr && (prefix->is_object() || prefix->is_type()) && prefix->type != nullptr &&
            (prefix->type->view()->kind == type_kind::Task || prefix->type->view()->kind == type_kind::Protected))
        {
            // (within a task or protected unit's body (a single one's object, or its type): an expanded name of its declarations
            // (4.1.3(13)))
            auto const unit_of = [](region* r) -> entity*
            {
                if (r->owner == nullptr)
                    return nullptr;
                return r->owner->kind == entity_kind::Procedure && r->owner->owner != nullptr ? r->owner->owner : r->owner;
            };
            // (the body's declarations, and those (e.g. block names) of the regions within it that enclose the name)
            std::vector<region*> passed;
            for (region* jump = nullptr, *r = aRegion; r != nullptr && result.empty(); r = outward(r, jump))
            {
                passed.push_back(r);
                if (auto* unit = unit_of(r); unit != nullptr && unit != prefix && unit->name == prefix->name &&
                    (unit->kind == entity_kind::Task || unit->kind == entity_kind::Protected))
                    for (auto it = passed.rbegin(); it != passed.rend() && result.empty(); ++it)
                    {
                        auto range = (*it)->names.equal_range(aName);
                        for (auto i = range.first; i != range.second; ++i)
                            result.push_back(i->second);
                    }
            }
            return result;
        }
        if (prefix->inner == nullptr)
            return result;
        bool within = false;
        for (region* jump = nullptr, *r = aRegion; r != nullptr && !within; r = outward(r, jump))
            within = r == prefix->inner || r->owner == prefix;
        // (a subprogram's declarations (and parameters): expanded names only within it (4.1.3(13)))
        if ((prefix->is_subprogram() || prefix->kind == entity_kind::GenericProcedure || prefix->kind == entity_kind::GenericFunction) && !within)
            return result;
        bool const privateVisible = !prefix->is_package() || private_part_visible(prefix, aRegion);
        for (auto* r = prefix->inner; r != nullptr; r = nullptr)
        {
            auto range = r->names.equal_range(aName);
            for (auto i = range.first; i != range.second; ++i)
                if ((!i->second->is_private || privateVisible) && i->second->visible_in_body &&
                    !(privateVisible && i->second->sub != nullptr && i->second->sub->hidden_by != nullptr))
                    result.push_back(i->second);
        }
        // (an instance of a generic package: the generic's generic child units (10.1.1(19)))
        if (result.empty() && prefix->is_package() && prefix->generic != nullptr)
            if (auto* child = library_unit(full_name(prefix->generic) + "." + aName); child != nullptr && child->is_generic())
                result.push_back(child);
        // (an instance's actual parameters (its formals' names): visible as a formal package's (12.7(10)); within the generic unit 
        // (or instance) its formals are visible by selection too (overloading its declarations))
        if ((result.empty() || within) && prefix->inner->parent != nullptr && prefix->inner->parent->generic_formals && prefix->inner->parent->owner == prefix &&
            (within || !iFormalPackagesWithActuals.contains(prefix)))
        {
            auto formals = prefix->inner->parent->names.equal_range(aName);
            for (auto i = formals.first; i != formals.second; ++i)
                if (i->second->kind != entity_kind::Package || i->second->renamed != prefix)
                    result.push_back(i->second);
        }
        // (an expanded name of a declaration in the body of the package or subprogram that the name is within)
        if (result.empty())
            for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
                if (r->owner == prefix && r != prefix->inner)
                {
                    auto range = r->names.equal_range(aName);
                    for (auto i = range.first; i != range.second; ++i)
                        result.push_back(i->second);
                }
        // (an operator: the predefined operators of the types declared in the package (P."=", Standard."+"))
        static std::set<std::string> const sOperators = { "and", "or", "xor", "=", "/=", "<", "<=", ">", ">=", "+", "-", "&", "*", "/",
            "mod", "rem", "**", "abs", "not" };
        if (prefix->is_package() && sOperators.contains(aName))
            for (auto* d : prefix->inner->declarations)
            {
                if (!d->is_type() || d->kind != entity_kind::Type || d->type == nullptr || (d->is_private && !privateVisible))
                    continue;
                // (a limited type (as seen here): no predefined equality or concatenation (4.5.2(9/3), 4.5.3(2)))
                if ((aName == "=" || aName == "/=" || aName == "&") && iInstanceBodies == 0 && limited_type(d->type, aRegion))
                    continue;
                for (auto* op : operators_of(d->type))
                    if (op->name == aName && std::find(result.begin(), result.end(), op) == result.end() &&
                        // (the universal fixed and root operators: Standard's (4.5.5(18), (19)))
                        (prefix->inner == iStandard || (op->sub->op != intrinsic::UniversalFixedMultiply && op->sub->op != intrinsic::UniversalFixedDivide &&
                            op->sub->op != intrinsic::RootMultiply && op->sub->op != intrinsic::RootDivide)))
                    {
                        // (unless a user-defined operator of the same profile (explicitly declared there) overrides it)
                        bool overridden = false;
                        for (auto* r : result)
                            if (r->sub != nullptr && r->sub->op == intrinsic::None && type_conformant(*r->sub, *op->sub))
                                overridden = true;
                        if (!overridden)
                            result.push_back(op);
                    }
            }
        // (a child unit)
        if (result.empty() && prefix->is_package())
            if (auto* child = library_unit(full_name(prefix) + "." + aName))
                result.push_back(child);
        std::erase_if(result, [&](entity* e) { return (e->library || e->limited_view) && private_with_hidden(e, aRegion); });
        return result;
    }

    std::vector<entity*> program::prefix_entities(node* aPrefix, region* aRegion)
    {
        if (aPrefix->is(construct::Identifier))
            return visible(name_text(aPrefix->source), aRegion);
        if (aPrefix->is(construct::Operator))
        {
            // (an operator symbol called as a function: the operators visible, and the root numeric types' (as for the operator,
            // 4.5(9), 8.6(29/3)))
            auto const designator = designator_text(aPrefix);
            auto result = visible(designator, aRegion);
            for (auto* root : { iUniversalInteger, iUniversalReal })
                for (auto* op : operators_of(root))
                    if (op->name == designator && std::find(result.begin(), result.end(), op) == result.end())
                        result.push_back(op);
            return result;
        }
        if (aPrefix->is(construct::Character))
            return visible(std::string{ aPrefix->source }, aRegion);
        if (aPrefix->is(construct::Selected) && aPrefix->size() == 2u)
        {
            // (an inner prefix (of a further selection): its entries are expanded names' prefixes (e.g. an accept's E.Parameter))
            static thread_local int sInnerPrefix = 0;
            ++sInnerPrefix;
            auto const inner = prefix_entities(aPrefix->child(0), aRegion);
            --sInnerPrefix;
            // (a task or protected unit's object (a single one) or type, named within its body)
            auto const within_body = [&](entity* aUnit)
            {
                if (!(aUnit->is_object() || aUnit->is_type()) || aUnit->type == nullptr ||
                    (aUnit->type->view()->kind != type_kind::Task && aUnit->type->view()->kind != type_kind::Protected))
                    return false;
                for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
                    if (r->owner != nullptr)
                    {
                        auto* unit = r->owner->kind == entity_kind::Procedure && r->owner->owner != nullptr ? r->owner->owner : r->owner;
                        if ((unit->kind == entity_kind::Task || unit->kind == entity_kind::Protected) && unit->name == aUnit->name)
                            return true;
                    }
                return false;
            };
            if (inner.size() == 1u)
            {
                auto* e = inner.front();
                if (e->kind == entity_kind::Entry)
                {
                    // (within an accept statement of the entry: its parameters (4.1.3(13)))
                    auto const name = name_text(aPrefix->child(1)->source);
                    for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
                        for (auto const& [statement, sr] : iStatementRegions)
                            if (sr == r && statement->is(construct::Accept) && statement->ent == e)
                            {
                                std::vector<entity*> result;
                                auto range = r->names.equal_range(name);
                                for (auto i = range.first; i != range.second; ++i)
                                    result.push_back(i->second);
                                if (!result.empty())
                                {
                                    aPrefix->child(0)->ent = e;
                                    set_meaning(aPrefix->child(0), meaning::Unit);
                                    return result;
                                }
                            }
                }
                if (within_body(e) && e->inner == nullptr)
                {
                    auto const name = name_text(aPrefix->child(1)->source);
                    auto result = selected(e, name, aRegion);
                    // (its entries and protected operations: called on the object (as a selected component), unless a further
                    // selection's prefix)
                    if (sInnerPrefix == 0)
                        std::erase_if(result, [](entity* m) { return m->kind == entity_kind::Entry || m->is_subprogram(); });
                    if (!result.empty())
                    {
                        aPrefix->child(0)->ent = e;
                        set_meaning(aPrefix->child(0), meaning::Unit);
                        return result;
                    }
                }
                if (e->is_package() || e->kind == entity_kind::Procedure || e->kind == entity_kind::Function || e->kind == entity_kind::Block ||
                    e->kind == entity_kind::Loop || ((e->kind == entity_kind::Task || e->kind == entity_kind::Protected) && within(e, aRegion)) ||
                    (e->is_type() && (e->type->kind == type_kind::Task || e->type->kind == type_kind::Protected) && within(e, aRegion)) || e->is_generic())
                {
                    auto* selector = aPrefix->child(1);
                    std::string name = selector->is(construct::Operator) ? designator_text(selector) :
                        selector->is(construct::Character) ? std::string{ selector->source } : name_text(selector->source);
                    aPrefix->child(0)->ent = e;
                    set_meaning(aPrefix->child(0), e->is_package() ? meaning::Package : meaning::Unit);
                    return selected(e, name, aRegion);
                }
            }
            else if (!inner.empty() && std::all_of(inner.begin(), inner.end(), [](entity* e) { return e->kind == entity_kind::Function || e->kind == entity_kind::Procedure; }))
            {
                // (an expanded name whose prefix is an overloaded subprogram name: the one that encloses the name)
                for (auto* e : inner)
                    if (within(e, aRegion))
                    {
                        aPrefix->child(0)->ent = e;
                        set_meaning(aPrefix->child(0), meaning::Unit);
                        return selected(e, name_text(aPrefix->child(1)->source), aRegion);
                    }
            }
        }
        return {};
    }

    bool program::within(entity* aUnit, region* aRegion) const
    {
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            if (r->owner == aUnit || (aUnit->inner != nullptr && r == aUnit->inner))
                return true;
        return false;
    }

    entity* program::resolve_entity(node* aName, region* aRegion)
    {
        if (aName == nullptr)
            return nullptr;
        if (aName->is(construct::Parenthesized))
            return resolve_entity(aName->child(0), aRegion);
        auto const candidates = prefix_entities(aName, aRegion);
        if (candidates.empty())
        {
            if (aName->is(construct::Identifier))
                error(aName, "\"" + name_text(aName->source) + "\" is not declared");
            if (aName->is(construct::Selected))
            {
                // (an object's component, e.g. a renamed component)
                resolve(aName, nullptr, aRegion);
                return aName->ent;
            }
            resolve(aName, nullptr, aRegion);
            return aName->ent;
        }
        entity* result = nullptr;
        for (auto* e : candidates)
            if (!e->overloadable())
            {
                result = e;
                break;
            }
        if (result == nullptr)
            result = candidates.front();
        aName->ent = result;
        if (result->is_package())
            set_meaning(aName, meaning::Package);
        else if (result->is_type())
        {
            set_meaning(aName, meaning::Type);
            aName->type = result->type;
        }
        else if (result->kind == entity_kind::Exception)
            set_meaning(aName, meaning::Exception);
        else if (result->is_object())
        {
            set_meaning(aName, meaning::Object);
            aName->type = result->type;
        }
        return result;
    }

    entity* program::resolve_subprogram_name(node* aName, entity* aProfile, region* aRegion)
    {
        // (the subprogram (or enumeration literal, or entry, or attribute) that a renaming or generic actual names: the one whose
        // profile is type conformant)
        node* name = aName;
        if (name->is(construct::Attribute))
        {
            // (an attribute as a subprogram, e.g. Integer'Image)
            resolve_attribute(name, nullptr, aRegion);
            auto* e = new_entity(aProfile->kind == entity_kind::Procedure ? entity_kind::Procedure : entity_kind::Function, name_text(name->child(1)->source), name, aRegion);
            e->sub->parameters = aProfile->sub->parameters;
            e->sub->result = aProfile->sub->result;
            e->type = aProfile->type;
            e->sub->op = intrinsic::Attribute;
            e->convention = "intrinsic";    // (an attribute that is a subprogram: of convention Intrinsic (6.3.1(4)))
            e->decl = name;
            return e;
        }
        std::vector<entity*> candidates;
        if (name->is(construct::Selected) && prefix_entities(name, aRegion).empty())
        {
            // (a prefixed view: a primitive or class-wide operation of the object's type whose profile after its first parameter is
            // type conformant (4.1.3(9.2/2), 8.5.4))
            auto* object = name->child(0);
            auto const selector = name_text(name->child(1)->source);
            for (auto const& i : interpretations(object, aRegion))
            {
                if (i.type == nullptr)
                    continue;
                auto* t = i.type->view()->kind == type_kind::Access && designated_of(i.type) != nullptr ? designated_of(i.type) : i.type;
                if (t->view()->kind == type_kind::Task || t->view()->kind == type_kind::Protected)
                {
                    // (an entry or protected subprogram of the object: the one whose profile is type conformant)
                    auto* tv = t->view();
                    if (tv->unit == nullptr || tv->unit->inner == nullptr)
                        continue;
                    auto range = tv->unit->inner->names.equal_range(selector);
                    for (auto it = range.first; it != range.second; ++it)
                    {
                        auto* op = it->second;
                        if (op->sub == nullptr || op->sub->family_type != nullptr || !type_conformant(*op->sub, *aProfile->sub))
                            continue;
                        resolve(object, i.type, aRegion);
                        name->ent = op;
                        set_meaning(name, meaning::Call);
                        auto& call = iCalls[name];
                        call.callee = op;
                        call.object = object;
                        call.actuals.assign(op->sub->parameters.size(), nullptr);
                        if (i.type->view()->kind == type_kind::Access)
                            name->mark |= MarkDeref;
                        name->type = op->sub->result != nullptr ? op->sub->result : iVoid;
                        return op;
                    }
                    continue;
                }
                for (auto* op : prefixed_candidates(t, selector))
                {
                    auto const& formals = op->sub->parameters;
                    auto const& profile = aProfile->sub->parameters;
                    if (formals.size() != profile.size() + 1u || (op->sub->result == nullptr) != (aProfile->sub->result == nullptr))
                        continue;
                    bool conformant = op->sub->result == nullptr || same_type(op->sub->result, aProfile->sub->result);
                    for (std::size_t k = 0u; k < profile.size() && conformant; ++k)
                        conformant = same_type(formals[k + 1u].type, profile[k].type);
                    if (!conformant)
                        continue;
                    resolve(object, i.type, aRegion);
                    auto& call = iCalls[name];
                    call.callee = op;
                    call.object = object;
                    call.prefixed = true;
                    call.actuals.assign(formals.size(), nullptr);
                    call.actuals[0] = object;
                    name->ent = op;
                    name->type = op->sub->result != nullptr ? op->sub->result : iVoid;
                    set_meaning(name, meaning::Call);
                    return op;
                }
            }
            // (an entry of a task or protected object)
            resolve(name, nullptr, aRegion);
            return name->ent;
        }
        candidates = prefix_entities(name, aRegion);
        for (auto* e : candidates)
        {
            if (e->kind == entity_kind::EnumerationLiteral && aProfile->sub->parameters.empty())
                if (aProfile->sub->result == nullptr || same_type(aProfile->sub->result, e->type))
                {
                    name->ent = e;
                    return e;
                }
            if (e->sub != nullptr && type_conformant(*e->sub, *aProfile->sub))
            {
                // (another type conformant homograph that the name could denote (not a view of the same declaration): ambiguous (8.6(31)))
                if (iSpeculation == 0 && iInstanceBodies == 0)
                    for (auto* other : candidates)
                        if (other != e && other->sub != nullptr && other->kind != entity_kind::EnumerationLiteral && type_conformant(*other->sub, *aProfile->sub) &&
                            !(e->decl != nullptr && other->decl == e->decl) &&
                            !(e->sub->op != intrinsic::None && e->sub->op == other->sub->op))
                        {
                            recover([&]() { error(aName, "the subprogram \"" + name_text(aName->source) + "\" is ambiguous (more than one matches the profile) (8.6(31))"); });
                            break;
                        }
                name->ent = e;
                set_meaning(name, meaning::Subprogram);
                return e;
            }
        }
        // (an entry family member: Object.Family (Index), or Family (Index))
        if (name->is(construct::Apply) && candidates.empty())
        {
            resolve(name, nullptr, aRegion);
            if (name->ent != nullptr && name->ent->kind == entity_kind::Entry)
                return name->ent;
        }
        // (a character literal of a predefined character type (renamed as a function, or a generic actual))
        if (name->is(construct::Character) && aProfile->sub->parameters.empty() && aProfile->sub->result != nullptr &&
            aProfile->sub->result->view()->base_type()->view()->standard_character)
        {
            auto* e = new_entity(entity_kind::EnumerationLiteral, std::string{ name->source }, name, aRegion);
            e->type = aProfile->sub->result->view()->base_type();
            e->value = static_value{ bigint{ static_cast<std::int64_t>(literal_character(name)) } };
            name->ent = e;
            set_meaning(name, meaning::Literal);
            name->type = e->type;
            return e;
        }
        // (a predefined operator, e.g. "+" for a formal type)
        if (name->is(construct::Operator) || (name->is(construct::Selected) && name->child(1)->is(construct::Operator)))
        {
            auto const designator = designator_text(name->is(construct::Selected) ? name->child(1) : name);
            std::vector<ada_type*> types;
            for (auto const& p : aProfile->sub->parameters)
                types.push_back(p.type);
            if (aProfile->sub->result != nullptr)
                types.push_back(aProfile->sub->result);
            for (auto* t : types)
                if (t != nullptr)
                    for (auto* op : operators_of(t))
                        if (op->name == designator && type_conformant(*op->sub, *aProfile->sub))
                        {
                            name->ent = op;
                            return op;
                        }
            for (auto* op : operators_of(iBoolean))
                if (op->name == designator && type_conformant(*op->sub, *aProfile->sub))
                    return op;
        }
        // (a single subprogram whose profile has the same form (when type conformance cannot be decided here))
        if ((iSpeculation != 0 || iInstanceBodies != 0) && candidates.size() == 1u && candidates.front()->sub != nullptr &&
            candidates.front()->sub->parameters.size() == aProfile->sub->parameters.size() &&
            (candidates.front()->sub->result == nullptr) == (aProfile->sub->result == nullptr))
        {
            name->ent = candidates.front();
            return candidates.front();
        }
        error(aName, "no subprogram \"" + name_text(aName->source) + "\" matches the profile");
    }

    // --- interpretations of names

    std::vector<interpretation> program::name_interpretations(node* aName, region* aRegion)
    {
        std::vector<interpretation> result;
        auto const entities = prefix_entities(aName, aRegion);
        if (!entities.empty())
        {
            for (auto* e : entities)
            {
                switch (e->kind)
                {
                case entity_kind::Variable:
                case entity_kind::Constant:
                case entity_kind::Parameter:
                case entity_kind::LoopParameter:
                case entity_kind::Component:
                case entity_kind::Discriminant:
                case entity_kind::EnumerationLiteral:
                    result.push_back(interpretation{ e->type, e });
                    break;
                case entity_kind::NamedNumber:
                    result.push_back(interpretation{ e->value.is_integer() ? iUniversalInteger : iUniversalReal, e });
                    break;
                case entity_kind::Function:
                case entity_kind::Attribute:
                    {
                        // (a call with no actuals: every parameter has a default)
                        bool callable = true;
                        for (auto const& p : e->sub->parameters)
                            callable = callable && p.default_expression != nullptr;
                        if (callable)
                            result.push_back(interpretation{ e->sub->result, e });
                        break;
                    }
                case entity_kind::Procedure:
                case entity_kind::Entry:
                    result.push_back(interpretation{ iVoid, e });
                    break;
                case entity_kind::Type:
                case entity_kind::Subtype:
                    result.push_back(interpretation{ e->type, e });
                    break;
                default:
                    break;
                }
            }
            return result;
        }
        if (aName->is(construct::Selected))
        {
            // (a component of a record object (or its dereference), or a prefixed view)
            auto const prefixes = interpretations(aName->child(0), aRegion);
            auto const selector = name_text(aName->child(1)->source);
            for (auto const& p : prefixes)
            {
                if (p.type == nullptr)
                    continue;
                auto* t = p.type->view();
                if (t->is_access())
                    t = designated_of(t);
                if (t == nullptr)
                    continue;
                t = t->view();
                if (t->kind == type_kind::ClassWide)
                    t = t->specific->view();
                bool component = false;
                for (auto const& c : t->base_type()->view()->components)
                    if (c.ent != nullptr && !c.stored && c.ent->name == selector)
                    {
                        result.push_back(interpretation{ c.type, c.ent });
                        component = true;
                    }
                // (a visible component of the name: the selected component denotes it, not a prefixed view (4.1.3(9.2/3)))
                if (component && iInstanceBodies == 0)
                {
                    auto* seen = p.type->view()->is_access() ? designated_of(p.type->view()) : p.type;
                    if (seen != nullptr && !partial_view_here(seen, aRegion))
                        continue;
                }
                if (t->kind == type_kind::Task || t->kind == type_kind::Protected)
                    if (t->unit != nullptr && t->unit->inner != nullptr)
                    {
                        auto range = t->unit->inner->names.equal_range(selector);
                        for (auto i = range.first; i != range.second; ++i)
                            result.push_back(interpretation{ i->second->type != nullptr ? i->second->type : iVoid, i->second });
                    }
                // (prefixed views: primitive operations of the type (and its ancestors) whose first parameter is of the type)
                for (auto* op : prefixed_candidates(t, selector))
                {
                    bool callable = true;
                    for (std::size_t i = 1u; i < op->sub->parameters.size(); ++i)
                        callable = callable && op->sub->parameters[i].default_expression != nullptr;
                    result.push_back(interpretation{ callable ? (op->sub->result != nullptr ? op->sub->result : iVoid) : nullptr, op });
                }
            }
        }
        return result;
    }

    std::vector<entity*> program::prefixed_candidates(ada_type* aType, std::string const& aName)
    {
        std::vector<entity*> result;
        if (aType == nullptr)
            return result;
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::ClassWide)
            t = t->specific->view()->base_type()->view();
        if (!t->tagged && !(t->kind == type_kind::Task || t->kind == type_kind::Protected))
            return result;
        auto const consider = [&](ada_type* aOwner)
        {
            for (auto* op : aOwner->primitives)
                if (op->name == aName && op->sub != nullptr && !op->sub->undeclared && !op->sub->parameters.empty() &&
                    std::find(result.begin(), result.end(), op) == result.end())
                    result.push_back(op);
            // (class-wide operations declared with the type)
            if (auto* r = aOwner->declared_in)
            {
                auto range = r->names.equal_range(aName);
                for (auto i = range.first; i != range.second; ++i)
                {
                    auto* op = i->second;
                    if (op->sub == nullptr || op->sub->parameters.empty() || std::find(result.begin(), result.end(), op) != result.end())
                        continue;
                    auto* first = op->sub->parameters[0].type;
                    if (first != nullptr && first->anonymous_access && first->designated != nullptr)
                        first = first->designated;
                    if (first != nullptr && first->view()->kind == type_kind::ClassWide && covers(first, aOwner))
                        result.push_back(op);
                }
            }
        };
        for (auto* a = t; a != nullptr; a = a->parent != nullptr ? a->parent->base_type()->view() : nullptr)
        {
            consider(a);
            for (auto* i : a->interfaces)
                consider(i->view());
            if (a->parent == nullptr)
                break;
        }
        return result;
    }

    ada_type* program::designated_of(ada_type* aType)
    {
        auto* t = aType->view();
        if (t->kind != type_kind::Access)
            t = t->base_type()->view();
        if (t->kind != type_kind::Access || t->designated == nullptr)
            return nullptr;
        return t->designated->view() != t->designated && t->designated->kind == type_kind::Private ? t->designated->view() : t->designated;
    }

    bool program::is_discrete_range(node* aNode, region* aRegion)
    {
        if (aNode->is(construct::Range) || aNode->is(construct::SubtypeIndication))
            return true;
        if (aNode->is(construct::Attribute))
        {
            auto const attribute = name_text(aNode->child(1)->source);
            return attribute == "range" || attribute == "reverse_range";
        }
        return is_subtype_name(aNode, aRegion);
    }

    // --- resolution of names

    ada_type* program::resolve_name(node* aName, ada_type* aExpected, region* aRegion)
    {
        bool const stringLiteral = aName->is(construct::Operator) && (!is_operator_symbol(aName) ||
            (aExpected != nullptr && aExpected->view()->kind == type_kind::Array && covers(aExpected, iStringLiteral)));
        auto entities = stringLiteral ? std::vector<entity*>{} : prefix_entities(aName, aRegion);
        // (a character literal of the expected character type (it need not be visible: 4.2(3)))
        auto const characterLiteral = [&]() -> entity*
        {
            if (!aName->is(construct::Character) || aExpected == nullptr || aExpected->view()->base_type()->view()->kind != type_kind::Enumeration)
                return nullptr;
            for (auto* literal : aExpected->view()->base_type()->view()->literals)
                if (literal->name == aName->source)
                    return literal;
            return nullptr;
        };
        if (entities.empty() && aExpected != nullptr && !aExpected->view()->base_type()->standard_character)
            if (auto* literal = characterLiteral())
                entities.push_back(literal);
        if (entities.empty())
        {
            if (aName->is(construct::Selected))
                return resolve_selected(aName, aExpected, aRegion);
            if (aName->is(construct::Operator))
            {
                // (a string literal)
                set_meaning(aName, meaning::StringLiteral);
                ada_type* t = aExpected;
                if (t != nullptr && !covers(t, iStringLiteral))
                    error(aName, "string literal is not of the expected type");
                aName->type = t != nullptr ? t : iString;
                return aName->type;
            }
            if (aName->is(construct::Character))
            {
                // (a character literal of a standard character type)
                ada_type* t = aExpected != nullptr ? aExpected : iCharacter;
                if (!t->view()->base_type()->standard_character)
                {
                    if (t->view()->base_type()->character_type)
                        error(aName, "character literal " + std::string{ aName->source } + " is not of the type");
                    t = iCharacter;
                }
                aName->type = t;
                set_meaning(aName, meaning::Literal);
                return t;
            }
            error(aName, "\"" + name_text(aName->source) + "\" is not declared");
        }
        // (a label, or a loop's or block's name: not a value (it hides the homographs of its region))
        if (entities.size() == 1u && (entities.front()->kind == entity_kind::Label || entities.front()->kind == entity_kind::Loop ||
            entities.front()->kind == entity_kind::Block) && !(aName->parent != nullptr && aName->parent->is(construct::Attribute) && aName->parent->child(0) == aName))
            error(aName, "\"" + entities.front()->name + "\" is a " + (entities.front()->kind == entity_kind::Label ? "label" :
                entities.front()->kind == entity_kind::Loop ? "loop's name" : "block's name") + ", not a value");
        // (the entities whose types are the expected type)
        std::vector<entity*> matching;
        for (auto* e : entities)
        {
            ada_type* t = nullptr;
            switch (e->kind)
            {
            case entity_kind::Function:
            case entity_kind::Attribute:
                {
                    bool callable = true;
                    for (auto const& p : e->sub->parameters)
                        callable = callable && p.default_expression != nullptr;
                    if (!callable)
                        continue;
                    t = e->sub->result;
                    break;
                }
            case entity_kind::Procedure:
            case entity_kind::Entry:
                t = iVoid;
                break;
            case entity_kind::NamedNumber:
                t = e->value.is_integer() ? iUniversalInteger : iUniversalReal;
                break;
            default:
                t = e->type;
                break;
            }
            if (aExpected == nullptr || t == nullptr || covers_dereferenced(aExpected, t) || (aExpected == iVoid && (e->kind == entity_kind::Procedure || e->kind == entity_kind::Entry)))
                matching.push_back(e);
        }
        if (matching.empty() && aExpected != nullptr && !aExpected->view()->base_type()->standard_character)
            if (auto* literal = characterLiteral())
                matching.push_back(literal);
        // (a character literal of a standard character type, when no user-defined one matches)
        if (matching.empty() && aName->is(construct::Character))
        {
            set_meaning(aName, meaning::Literal);
            aName->type = aExpected != nullptr && aExpected->view()->base_type()->standard_character ? aExpected : iCharacter;
            return aName->type;
        }
        if (matching.empty())
        {
            if (entities.size() == 1u && !entities.front()->overloadable() && aExpected == nullptr)
                matching = entities;
            else if (entities.size() == 1u && !entities.front()->overloadable())
                error(aName, "\"" + name_text(aName->source) + "\" is not of the expected type");
            else
                error(aName, "no interpretation of \"" + name_text(aName->source) + "\" matches the context");
        }
        if (matching.size() > 1u)
        {
            // (preferences: an object over others; a non-predefined operation; the innermost (first found))
            std::stable_partition(matching.begin(), matching.end(), [](entity* e) { return !e->overloadable(); });
            if (aExpected == nullptr && matching.size() > 1u && matching[0]->overloadable() && matching[1]->overloadable() &&
                !(aName->is(construct::Character)))
            {
                // (ambiguous unless the context resolves it later: the first)
            }
            // (a procedure call statement without actuals: one callable procedure or entry (8.6(31)))
            if (aExpected == iVoid && iSpeculation == 0 && iInstanceBodies == 0 && iInstantiationSites.empty())
            {
                std::vector<entity*> callable;
                for (auto* m : matching)
                    if ((m->kind == entity_kind::Procedure || m->kind == entity_kind::Entry) && m->sub != nullptr &&
                        std::all_of(m->sub->parameters.begin(), m->sub->parameters.end(), [](parameter_info const& p) { return p.default_expression != nullptr; }) &&
                        std::none_of(callable.begin(), callable.end(), [&](entity* c) { return c == m || (m->decl != nullptr && c->decl == m->decl); }))
                        callable.push_back(m);
                if (callable.size() > 1u)
                    recover([&]() { error(aName, "the call of \"" + name_text(aName->source) + "\" is ambiguous (" + std::to_string(callable.size()) +
                        " subprograms match it) (8.6(31))"); });
            }
        }
        auto* e = matching.front();
        aName->ent = e;
        switch (e->kind)
        {
        case entity_kind::Function:
        case entity_kind::Attribute:
            set_meaning(aName, meaning::Call);
            {
                auto& call = iCalls[aName];
                call.callee = e;
                call.actuals.assign(e->sub->parameters.size(), nullptr);
            }
            aName->type = e->sub->result;
            incomplete_result_check(e, aName);
            break;
        case entity_kind::Procedure:
        case entity_kind::Entry:
            set_meaning(aName, meaning::Call);
            {
                auto& call = iCalls[aName];
                call.callee = e;
                call.actuals.assign(e->sub->parameters.size(), nullptr);
            }
            aName->type = iVoid;
            break;
        case entity_kind::EnumerationLiteral:
            set_meaning(aName, meaning::Literal);
            aName->type = e->type;
            break;
        case entity_kind::NamedNumber:
            set_meaning(aName, meaning::Value);
            aName->type = aExpected != nullptr && !aExpected->is_universal() ? aExpected : (e->value.is_integer() ? iUniversalInteger : iUniversalReal);
            break;
        case entity_kind::Type:
        case entity_kind::Subtype:
            set_meaning(aName, meaning::Type);
            aName->type = e->type;
            break;
        case entity_kind::Package:
        case entity_kind::GenericPackage:
            set_meaning(aName, meaning::Package);
            break;
        case entity_kind::Exception:
            set_meaning(aName, meaning::Exception);
            // (not a value (11.1))
            if (aExpected != nullptr && aExpected != iVoid && iSpeculation == 0 && iInstanceBodies == 0)
                error(aName, "the name of an exception is not an expression");
            break;
        default:
            set_meaning(aName, meaning::Object);
            aName->type = e->type;
            if (e->kind == entity_kind::Variable || e->kind == entity_kind::Constant || e->kind == entity_kind::Parameter ||
                e->kind == entity_kind::LoopParameter)
                note_reference(e, aRegion);
            break;
        }
        return aName->type;
    }

    void program::note_reference(entity* aObject, region* aRegion)
    {
        // (an object of a subprogram referred to by a subprogram nested in it: it is in its subprogram's frame)
        auto const level = aRegion->level;
        if (aObject->scope != nullptr && aObject->scope->level > 0 && aObject->scope->level < level)
            aObject->captured = true;
        if (aObject->renamed != nullptr && aObject->renamed != aObject)
            if (aObject->renamed->is_object())
                note_reference(aObject->renamed, aRegion);
    }

    ada_type* program::resolve_selected(node* aSelected, ada_type* aExpected, region* aRegion)
    {
        auto* prefix = aSelected->child(0);
        auto* selector = aSelected->child(1);
        // (an expanded name of a character literal of a predefined character type (Standard.'A', 4.1.3(4)): the literal (those of
        // enumeration types declared in the package are its declarations))
        if (selector->is(construct::Character) && prefix_entities(prefix, aRegion).size() == 1u && prefix_entities(prefix, aRegion).front()->is_package() &&
            selected(prefix_entities(prefix, aRegion).front(), name_text(selector->source), aRegion).empty())
        {
            auto* t = resolve(selector, aExpected, aRegion);
            aSelected->ent = selector->ent;
            set_meaning(aSelected, meaning_of(selector));
            aSelected->type = t;
            return t;
        }
        auto const name = selector->is(construct::Operator) ? designator_text(selector) : name_text(selector->source);
        // (the current instance of a task or protected unit (its name within its body): its member, as a direct name's)
        if (prefix->is(construct::Identifier) || prefix->is(construct::Selected))
        {
            entity* unit = nullptr;
            for (auto* e : prefix_entities(prefix, aRegion))
                if (e->is_type() && e->type != nullptr && (e->type->view()->kind == type_kind::Task || e->type->view()->kind == type_kind::Protected))
                    unit = e->type->view()->unit;
                else if (e->kind == entity_kind::Task || e->kind == entity_kind::Protected)
                    unit = e;
            bool within = false;
            for (region* jump = nullptr, *r = aRegion; unit != nullptr && r != nullptr && !within; r = outward(r, jump))
                within = r->owner == unit || (r->owner != nullptr && r->owner->owner == unit && r->owner->kind == entity_kind::Procedure);
            if (within && unit->inner != nullptr && unit->inner->names.count(name) != 0u)
            {
                prefix->ent = unit;
                set_meaning(prefix, meaning::Unit);
                auto* member = unit->inner->names.find(name)->second;
                if (member->is_object())
                {
                    aSelected->ent = member;
                    selector->ent = member;
                    set_meaning(aSelected, meaning::Object);
                    aSelected->type = member->type;
                    return member->type;
                }
            }
        }
        // (the prefix's type: a record (or its access), a task or protected object, or a tagged object (a prefixed view))
        auto const prefixes = interpretations(prefix, aRegion);
        // (several prefixes (e.g. overloaded function calls) with the component: one whose component is of the expected type)
        auto const componentOf = [&](interpretation const& aPrefix) -> ada_type*
        {
            if (aPrefix.type == nullptr)
                return nullptr;
            auto* t = aPrefix.type->view();
            if (t->kind == type_kind::Access)
                t = designated_of(t);
            if (t == nullptr)
                return nullptr;
            t = t->view();
            if (t->kind == type_kind::ClassWide)
                t = t->specific->view();
            for (auto const& c : t->base_type()->view()->components)
                if (c.ent != nullptr && !c.stored && c.ent->name == name)
                    return c.type;
            return nullptr;
        };
        bool const filtered = aExpected != nullptr && prefixes.size() > 1u && std::any_of(prefixes.begin(), prefixes.end(), [&](interpretation const& p)
            {
                auto* ct = componentOf(p);
                return ct != nullptr && covers(aExpected, ct);
            });
        for (auto const& p : prefixes)
        {
            if (p.type == nullptr)
                continue;
            if (filtered)
                if (auto* ct = componentOf(p); ct != nullptr && !covers(aExpected, ct))
                    continue;
            auto* t = p.type->view();
            bool deref = false;
            if (t->is_access() && t->kind == type_kind::Access)
            {
                t = designated_of(t);
                deref = true;
            }
            if (t == nullptr)
                continue;
            t = t->view();
            auto* recordType = t->kind == type_kind::ClassWide ? t->specific->view() : t;
            // (a private type's components: only where its full view is visible (but its discriminants, and a private extension's
            // parent's visible components))
            std::function<bool(ada_type*)> visibleComponent = [&](ada_type* aType) -> bool
            {
                auto* declared = aType->base_type();
                if (declared->kind != type_kind::Private && declared->partial != nullptr)
                    declared = declared->partial;
                if (!(declared->kind == type_kind::Private && !declared->incomplete && declared->full != nullptr && declared->declared_in != nullptr &&
                    declared->declared_in->owner != nullptr && declared->declared_in->owner->is_package()) ||
                    private_part_visible_here(declared->declared_in->owner, aRegion))
                    return true;
                if (declared->parent != nullptr && declared->parent != declared)
                    for (auto const& pc : declared->parent->view()->base_type()->view()->components)
                        if (pc.ent != nullptr && pc.ent->name == name)
                            return visibleComponent(declared->parent);
                return false;
            };
            // (a discriminant of a formal type with known discriminants (within an instance): the actual's at its position)
            if (auto const& names = recordType->view()->formal_discriminants; !names.empty() && within_scope(recordType->view()->formal_scope, aRegion))
                if (auto found = std::find(names.begin(), names.end(), name); found != names.end())
                {
                    std::size_t k = static_cast<std::size_t>(found - names.begin());
                    for (auto const& c : recordType->base_type()->view()->components)
                        if (c.discriminant && c.ent != nullptr && k-- == 0u)
                        {
                            resolve(prefix, p.type, aRegion);
                            aSelected->ent = c.ent;
                            selector->ent = c.ent;
                            set_meaning(aSelected, meaning::Component);
                            if (deref)
                                aSelected->mark |= MarkDeref;
                            incomplete_prefix_check(prefix, p.type, deref);
                            aSelected->type = c.type;
                            return c.type;
                        }
                }
            // (two of the name: the extension's (the ancestor's was not visible where the extension was declared, 7.3.1(4/3)))
            // (a discriminant: where the full view is visible, or the partial view has known discriminants (7.3(4)))
            auto const visibleDiscriminant = [&](ada_type* aType) -> bool
            {
                if (visibleComponent(aType))
                    return true;
                auto* declared = aType->base_type();
                if (declared->kind != type_kind::Private && declared->partial != nullptr)
                    declared = declared->partial;
                return declared->kind != type_kind::Private || iInstanceBodies != 0 || (!declared->constrained && !declared->unknown_discriminants);
            };
            component_info const* named = nullptr;
            for (auto const& c : recordType->base_type()->view()->components)
                if (c.ent != nullptr && !c.stored && c.ent->name == name && ((c.discriminant && visibleDiscriminant(recordType)) || visibleComponent(recordType)))
                    named = &c;
            if (named != nullptr)
                if (auto const& c = *named; true)
                {
                    resolve(prefix, p.type, aRegion);
                    aSelected->ent = c.ent;
                    selector->ent = c.ent;
                    set_meaning(aSelected, meaning::Component);
                    if (deref)
                        aSelected->mark |= MarkDeref;
                    incomplete_prefix_check(prefix, p.type, deref);
                    // (of the expected type: the selected component denotes the component, not a prefixed view (4.1.3(9.2/3)))
                    if (aExpected != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && c.type != nullptr &&
                        (aExpected == iVoid || (!aExpected->view()->is_formal && !c.type->view()->is_formal && !covers(aExpected, c.type) &&
                        !covers_dereferenced(aExpected, c.type))))
                        error(aSelected, aExpected == iVoid ? "a component is not a procedure call" : "the component is not of the expected type");
                    // (a component whose subtype depends on discriminants: of the object's constraint)
                    aSelected->type = c.type;
                    return c.type;
                }
            // (a discriminant of the parent type that a new discriminant of the derived type constrains: that one (a view of the type
            // as its ancestor's, e.g. a formal derived type's (12.5.1(21))))
            if (auto* base = recordType->base_type()->view(); base->parent != nullptr && base->parent->view()->base_type()->view() != base)
            {
                auto* parentView = base->parent->view();
                std::size_t d = 0u;
                for (auto const& pc : parentView->base_type()->view()->components)
                {
                    if (!pc.discriminant)
                        continue;
                    auto* value = d < parentView->discriminant_constraint.size() ? parentView->discriminant_constraint[d] : nullptr;
                    ++d;
                    if (value != nullptr && value->is(construct::Association))
                        value = value->children.back();
                    if (pc.ent == nullptr || pc.ent->name != name || value == nullptr || !value->is(construct::Identifier))
                        continue;
                    for (auto const& c : base->components)
                        if (c.discriminant && c.ent != nullptr && c.ent->name == name_text(value->source))
                        {
                            resolve(prefix, p.type, aRegion);
                            aSelected->ent = c.ent;
                            selector->ent = c.ent;
                            set_meaning(aSelected, meaning::Component);
                            if (deref)
                                aSelected->mark |= MarkDeref;
                            incomplete_prefix_check(prefix, p.type, deref);
                            aSelected->type = c.type;
                            return c.type;
                        }
                }
            }
            // (a protected object's private components (within the protected body))
            if ((t->kind == type_kind::Protected || t->kind == type_kind::Task) && t->unit != nullptr && t->unit->inner != nullptr)
            {
                auto range = t->unit->inner->names.equal_range(name);
                std::vector<entity*> ops;
                for (auto i = range.first; i != range.second; ++i)
                    ops.push_back(i->second);
                if (!ops.empty())
                {
                    resolve(prefix, p.type, aRegion);
                    auto* e = ops.front();
                    if (e->kind == entity_kind::Component)
                    {
                        aSelected->ent = e;
                        set_meaning(aSelected, meaning::Component);
                        aSelected->type = e->type;
                        return e->type;
                    }
                    // (an entry or protected subprogram named (not called): a call with no actuals)
                    for (auto* op : ops)
                    {
                        bool callable = true;
                        for (auto const& fp : op->sub->parameters)
                            callable = callable && fp.default_expression != nullptr;
                        auto* rt = op->sub != nullptr && op->sub->result != nullptr ? op->sub->result : iVoid;
                        if (callable && (aExpected == nullptr || covers(aExpected, rt)))
                        {
                            aSelected->ent = op;
                            set_meaning(aSelected, meaning::Call);
                            auto& call = iCalls[aSelected];
                            call.callee = op;
                            call.object = prefix;
                            call.actuals.assign(op->sub->parameters.size(), nullptr);
                            synchronized_target_check(op, prefix, aSelected);
                            if (deref)
                                aSelected->mark |= MarkDeref;
                            aSelected->type = rt;
                            return rt;
                        }
                    }
                    aSelected->ent = ops.front();
                    set_meaning(aSelected, meaning::Entry);
                    aSelected->type = iVoid;
                    return iVoid;
                }
            }
            // (a prefixed view called with no further actuals)
            for (auto* op : prefixed_candidates(t, name))
            {
                bool callable = true;
                for (std::size_t i = 1u; i < op->sub->parameters.size(); ++i)
                    callable = callable && op->sub->parameters[i].default_expression != nullptr;
                auto* rt = op->sub->result != nullptr ? op->sub->result : iVoid;
                if (callable && (aExpected == nullptr || covers_dereferenced(aExpected, rt)))
                {
                    std::vector<node*> none;
                    return finish_call(aSelected, op, none, prefix, aRegion);
                }
            }
        }
        error(aSelected, "\"" + name + "\" is not a component or operation of the prefix");
    }

    // --- calls

    bool program::class_wide_access_actual(ada_type* aFormal, ada_type* aActual)
    {
        // (an access parameter designating a specific tagged type, and an access value designating a class-wide type covered by it)
        if (aFormal == nullptr || aActual == nullptr)
            return false;
        auto* f = aFormal->view();
        auto* a = aActual->view();
        if (f->kind != type_kind::Access || !f->anonymous_access || f->designated == nullptr || a->kind != type_kind::Access)
            return false;
        auto* fd = f->designated->view();
        auto* ad = designated_of(aActual);
        return fd->kind != type_kind::ClassWide && fd->base_type()->view()->tagged && ad != nullptr && ad->view()->kind == type_kind::ClassWide &&
            is_descendant(ad, f->designated);
    }

    bool program::dynamically_tagged(node* aExpression)
    {
        // (a dynamically tagged expression: of a class-wide type, or a dispatching call of a function with a controlling result (3.9.2(5/2, 6/2)))
        auto* a = aExpression;
        while (a != nullptr && (a->is(construct::Parenthesized) || a->is(construct::Qualified)) && !a->children.empty())
            a = a->children.back();
        if (a == nullptr || a->type == nullptr)
            return false;
        if (a->type->view()->kind == type_kind::ClassWide)
            return true;
        if (auto const* c = meaning_of(a) == meaning::Call ? call_of(a) : nullptr; c != nullptr && c->dispatching && c->callee != nullptr &&
            c->callee->sub != nullptr && c->callee->sub->result != nullptr && c->callee->sub->controlling != nullptr &&
            same_type(c->callee->sub->result, c->callee->sub->controlling))
            return true;
        return false;
    }

    bool program::class_wide_actual(ada_type* aActual, ada_type* aFormal)
    {
        // (T'Class for a controlling formal of type T (8.6(23)); for one of an interface type, the class-wide type of a type that
        // has the interface as a progenitor: the operation that type inherits from the progenitor (3.9.4(4/2)) is the progenitor's,
        // dispatching through it (progenitors' operations are not declared again for their descendants))
        auto* specific = aActual->view()->specific;
        if (specific == nullptr)
            return false;
        if (same_type(specific, aFormal))
            return true;
        return aFormal->view()->base_type()->view()->kind == type_kind::Interface && is_descendant(aActual, aFormal);
    }

    bool program::match_call(entity* aCallee, std::vector<node*> const& aArguments, node* aObject, region* aRegion, std::vector<node*>* aActuals, bool aExact)
    {
        if (aCallee->sub == nullptr)
            return aArguments.empty() && aObject == nullptr;
        auto const& formals = aCallee->sub->parameters;
        std::vector<node*> actuals(formals.size(), nullptr);
        std::size_t next = 0u;
        if (aObject != nullptr)
        {
            if (formals.empty())
                return false;
            actuals[0] = aObject;
            next = 1u;
        }
        bool named = false;
        for (auto* a : aArguments)
        {
            if (a->is(construct::Association))
            {
                named = true;
                auto* choices = a->child(0);
                if (choices == nullptr || choices->size() != 1u)
                    return false;
                auto const name = name_text(choices->child(0)->source);
                bool found = false;
                for (std::size_t i = 0u; i < formals.size(); ++i)
                    if (formals[i].ent != nullptr && formals[i].ent->name == name)
                    {
                        if (actuals[i] != nullptr)
                            return false;
                        actuals[i] = a->children.back();
                        found = true;
                    }
                if (!found)
                    return false;
            }
            else
            {
                if (named || next >= formals.size())
                    return false;
                actuals[next++] = a;
            }
        }
        for (std::size_t i = 0u; i < formals.size(); ++i)
            if (actuals[i] == nullptr && formals[i].default_expression == nullptr)
                return false;
        if (aExact)
        {
            for (std::size_t i = 0u; i < formals.size(); ++i)
            {
                if (actuals[i] == nullptr)
                    continue;
                auto* formalType = formals[i].type;
                if (formalType == nullptr)
                    continue;
                // (a prefixed view's object: of the type, or an access to it, or the type of an access parameter)
                auto const options = interpretations(actuals[i], aRegion);
                bool ok = options.empty();
                for (auto const& o : options)
                {
                    if (o.type == nullptr || covers(formalType, o.type))
                        ok = true;
                    else if (o.type->view()->kind == type_kind::ClassWide && formalType->view()->base_type()->view()->tagged &&
                        class_wide_actual(o.type, formalType))
                        ok = true;     // (T'Class for a formal of type T (8.6(23)): a dispatching call)
                    else if (class_wide_access_actual(formalType, o.type))
                        ok = true;     // (an access to a class-wide object for a controlling access parameter: a dispatching call)
                    else if (i == 0u && aObject != nullptr)
                    {
                        // (implicit dereference or 'Access of the object for an access parameter)
                        auto* ft = formalType->view();
                        if (ft->kind == type_kind::Access && ft->designated != nullptr && covers(ft->designated, o.type))
                            ok = true;
                        else if (ft->kind == type_kind::Access && ft->designated != nullptr && o.type->view()->kind == type_kind::ClassWide &&
                            ft->designated->view()->base_type()->view()->tagged && is_descendant(o.type, ft->designated))
                            ok = true;     // (a class-wide object's 'Access: a dispatching call)
                        else if (o.type->view()->kind == type_kind::Access && designated_of(o.type) != nullptr && covers(formalType, designated_of(o.type)))
                            ok = true;
                        else if (o.type->view()->kind == type_kind::Access && designated_of(o.type) != nullptr &&
                            designated_of(o.type)->view()->kind == type_kind::ClassWide && formalType->view()->base_type()->view()->tagged &&
                            is_descendant(designated_of(o.type), formalType))
                            ok = true;     // (an access to a class-wide object: a dispatching call)
                    }
                    if (ok)
                        break;
                }
                if (!ok)
                    return false;
            }
        }
        if (aActuals != nullptr)
            *aActuals = actuals;
        return true;
    }

    bool program::indexed_result_fits(ada_type* aResult, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion)
    {
        // (an array result indexed (or sliced) by the arguments: their number, their types, the component type expected)
        auto* rt = aResult->view();
        if (rt->kind != type_kind::Array)
            return true;
        bool const slice = aArguments.size() == 1u && is_discrete_range(aArguments[0], aRegion);
        if (!slice && aArguments.size() != rt->indexes.size())
            return false;
        if (slice)
            return rt->indexes.size() == 1u;
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
        {
            if (aArguments[i]->is(construct::Association))
                return false;
            bool any = false;
            for (auto const& o : interpretations(aArguments[i], aRegion))
                any = any || (o.type != nullptr && covers(rt->indexes[i], o.type));
            if (!any)
                return false;
        }
        return aExpected == nullptr || covers(aExpected, rt->component);
    }

    void program::prefixed_view_check(node* aObject, entity* aOperation)
    {
        // (a prefixed view: its prefix an aliased view for an access parameter, a variable for an in out or out parameter or an
        // access-to-variable parameter (4.1.3(13.1/2)))
        if (aObject == nullptr || aOperation == nullptr || aOperation->sub == nullptr || iSpeculation > 0 || iInstanceBodies > 0)
            return;
        auto const& formals = aOperation->sub->parameters;
        if (formals.empty() || formals[0].type == nullptr || aObject->type == nullptr || aOperation->kind == entity_kind::Entry)
            return;
        auto* ot = aObject->type->view();
        auto* ft = formals[0].type->view();
        bool const dereferenced = ot->kind == type_kind::Access && !(ft->kind == type_kind::Access && same_type(formals[0].type, aObject->type));
        if (!dereferenced && formals[0].type->anonymous_access && ft->kind == type_kind::Access && !aliased_view(aObject))
            error(aObject, "the prefix of a prefixed view whose first parameter is an access parameter must be an aliased view (4.1.3(13.1/2))");
        bool const needsVariable = formals[0].mode == parameter_mode::Out || formals[0].mode == parameter_mode::InOut ||
            (formals[0].type->anonymous_access && ft->kind == type_kind::Access && !ft->access_constant);
        if (needsVariable && (dereferenced ? ot->access_constant : !is_variable(aObject)))
            error(aObject, "the prefix of a prefixed view whose first parameter is of mode in out or out, or of an access-to-variable type, "
                "must denote a variable (4.1.3(13.1/2))");
    }

    void program::incomplete_result_check(entity* aCallee, node* aCall)
    {
        // (a function call's result: not of an incomplete view (3.10.1(10/2)))
        if (aCallee == nullptr || aCallee->sub == nullptr || aCallee->sub->result == nullptr || aCallee->sub->result->anonymous_access ||
            iSpeculation != 0 || iInstanceBodies != 0)
            return;
        auto* r = aCallee->sub->result;
        if (r->kind == type_kind::ClassWide && r->specific != nullptr)
            r = r->specific;
        if ((r->incomplete && r->full == nullptr) || (r->view()->incomplete && r->view()->full == nullptr))
            recover([&]() { error(aCall, "the result of a function call cannot be of an incomplete view (3.10.1(10/2))"); });
    }

    void program::synchronized_target_check(entity* aOperation, node* aObject, node* aCall)
    {
        // (a protected procedure's or entry's target object: a variable (9.5(7.1/3)))
        if (aObject == nullptr || aOperation == nullptr || iSpeculation != 0 || iInstanceBodies != 0 ||
            (aOperation->kind != entity_kind::Procedure && aOperation->kind != entity_kind::Entry) || aOperation->scope == nullptr ||
            aOperation->scope->owner == nullptr || aOperation->scope->owner->kind != entity_kind::Protected)
            return;
        bool variable = true;
        if (auto* t = aObject->type != nullptr ? aObject->type->view() : nullptr; t != nullptr && t->kind == type_kind::Access)
            variable = !t->access_constant;
        else
            variable = is_variable(aObject);
        if (!variable)
            recover([&]() { error(aCall, "the target object of a call of a protected procedure or entry must be a variable (9.5(7.1/3))"); });
    }

    ada_type* program::finish_call(node* aCall, entity* aCallee, std::vector<node*> const& aArguments, node* aObject, region* aRegion)
    {
        std::vector<node*> actuals;
        if (!match_call(aCallee, aArguments, aObject, aRegion, &actuals, false))
            error(aCall, "wrong number or names of actuals in call to \"" + aCallee->name + "\"");
        auto& call = iCalls[aCall];
        call.callee = aCallee;
        call.actuals = actuals;
        call.object = aObject;
        call.prefixed = aObject != nullptr;
        auto const& formals = aCallee->sub != nullptr ? aCallee->sub->parameters : std::vector<parameter_info>{};
        if (aObject != nullptr)
            synchronized_target_check(aCallee, aObject, aCall);
        incomplete_result_check(aCallee, aCall);
        // (an actual array aggregate with others: of a constrained formal subtype (its applicable index constraint, 4.3.3(11/5)))
        if (iSpeculation == 0 && iInstanceBodies == 0 && aCallee->sub != nullptr && aCallee->sub->op == intrinsic::None && aCallee->kind != entity_kind::Attribute)
            for (std::size_t i = 0u; i < actuals.size() && i < formals.size(); ++i)
                if (auto* ft = formals[i].type != nullptr ? formals[i].type->view() : nullptr; actuals[i] != nullptr && ft != nullptr &&
                    ft->kind == type_kind::Array && !ft->constrained && !ft->is_formal)
                {
                    std::vector<node*> others;
                    others_choices(actuals[i], ft->indexes.size(), others);
                    for (auto* o : others)
                        recover([&]() { error(o, "others is not allowed in an actual parameter of an unconstrained array formal (no applicable index constraint)"); });
                }
        // (an instance of Unchecked_Deallocation: not for an access type whose Storage_Size is statically zero (13.11.2(3.1/3)))
        if (aCallee->sub != nullptr && aCallee->sub->op == intrinsic::UncheckedDeallocation && !formals.empty() && formals[0].type != nullptr &&
            iSpeculation == 0 && iInstanceBodies == 0)
            for (auto* t = formals[0].type->view()->base_type()->view(); t != nullptr; )
            {
                if (auto* size = type_aspect(t, "storage_size"); size != nullptr && static_expression(size))
                {
                    if (auto v = evaluate(size, aRegion); v.is_integer() && v.integer() == bigint{ 0 })
                        recover([&]() { error(aCall, "Unchecked_Deallocation cannot be called for an access type whose Storage_Size is zero (13.11.2(3.1/3))"); });
                    break;
                }
                t = t->parent != nullptr && t->parent->view()->base_type()->view() != t ? t->parent->view()->base_type()->view() : nullptr;
            }
        // (controlling operands: their types determine the controlling tag (class-wide: a dispatching call))
        bool classWide = false;
        // (in a class-wide condition: a controlling formal of its subprogram (or its X'Old) as a controlling operand (the operation
        // of the type of the subprogram that the condition is checked for is called, 6.1.1(7/5)))
        auto const contract_formal = [&](node* aActual, ada_type* aFormalType)
        {
            if (iClassWideContract == nullptr || iClassWideContract->sub == nullptr || iClassWideContract->sub->controlling == nullptr ||
                aCallee->sub == nullptr || aCallee->sub->controlling == nullptr || aFormalType == nullptr ||
                aFormalType->view()->kind == type_kind::ClassWide || !same_type(aFormalType, aCallee->sub->controlling))
                return false;
            auto* a = aActual;
            while ((a->is(construct::Parenthesized) && !a->children.empty()) ||
                (a->is(construct::Attribute) && a->size() >= 2u && name_text(a->child(1)->source) == "old"))
                a = a->child(0);
            if (!a->is(construct::Identifier) || a->ent == nullptr || a->ent->kind != entity_kind::Parameter)
                return false;
            for (auto const& p : iClassWideContract->sub->parameters)
                if (p.ent == a->ent && p.type != nullptr && same_type(p.type, iClassWideContract->sub->controlling))
                    return true;
            return false;
        };
        for (std::size_t i = 0u; i < actuals.size(); ++i)
        {
            if (actuals[i] == nullptr)
                continue;
            auto* formalType = formals[i].type;
            ada_type* expected = formalType;
            if (formals[i].mode == parameter_mode::Out || formals[i].mode == parameter_mode::InOut)
                variable_name(actuals[i]);
            if (i == 0u && aObject != nullptr)
            {
                // (a prefixed view's object: implicitly dereferenced, or its access taken, as the formal requires)
                auto const options = interpretations(aObject, aRegion);
                ada_type* objectType = nullptr;
                for (auto const& o : options)
                    if (o.type != nullptr)
                    {
                        objectType = o.type;
                        break;
                    }
                resolve(aObject, objectType, aRegion);
                if (objectType != nullptr && formalType != nullptr)
                {
                    auto* ft = formalType->view();
                    if (ft->kind == type_kind::Access && !(objectType->view()->kind == type_kind::Access))
                        aCall->mark |= 0x200u;      // (the object's 'Access is the actual)
                    else if (objectType->view()->kind == type_kind::Access && ft->kind != type_kind::Access)
                        aCall->mark |= 0x400u;      // (the object is dereferenced)
                    auto* actualType = (aCall->mark & 0x400u) ? designated_of(objectType) : objectType;
                    if (actualType != nullptr && actualType->view()->kind == type_kind::ClassWide && aCallee->sub->controlling != nullptr)
                        classWide = true;
                    else if (!(aCall->mark & 0x600u) && contract_formal(aObject, formalType))
                    {
                        classWide = true;
                        call.contract_dispatching = true;
                    }
                }
                continue;
            }
            if (formalType != nullptr && formalType->view()->kind != type_kind::ClassWide && formalType->view()->base_type()->view()->tagged)
                for (auto const& o : interpretations(actuals[i], aRegion))
                    if (o.type != nullptr && o.type->view()->kind == type_kind::ClassWide && class_wide_actual(o.type, formalType))
                    {
                        expected = o.type;     // (a class-wide actual of a controlling formal)
                        break;
                    }
            if (formalType != nullptr)
                for (auto const& o : interpretations(actuals[i], aRegion))
                    if (class_wide_access_actual(formalType, o.type))
                    {
                        expected = o.type;     // (an access to a class-wide object for a controlling access parameter)
                        break;
                    }
            // (the actual of a controlling formal: a controlling operand (it may be dynamically tagged, 3.9.2(9/1)))
            bool const controllingFormal = aCallee->sub != nullptr && aCallee->sub->controlling != nullptr && formalType != nullptr &&
                (same_type(formalType, aCallee->sub->controlling) || (formalType->view()->kind == type_kind::Access && formalType->view()->anonymous_access &&
                 formalType->view()->designated != nullptr && same_type(formalType->view()->designated, aCallee->sub->controlling)));
            auto* const savedControlling = iControllingActual;
            iControllingActual = controllingFormal || aCallee->sub == nullptr || aCallee->sub->op != intrinsic::None ? actuals[i] : nullptr;
            ada_type* t = nullptr;
            {
                struct restore { node const*& target; node const* value; ~restore() { target = value; } } restoring{ iControllingActual, savedControlling };
                t = resolve(actuals[i], expected, aRegion);
            }
            // (a dynamically tagged actual of a specific tagged formal that is not controlling: illegal (3.9.2(9/1)))
            if (t != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && formalType != nullptr &&
                !controllingFormal && aCallee->sub != nullptr && aCallee->sub->op == intrinsic::None && formalType->view()->kind != type_kind::ClassWide &&
                formalType->view()->base_type()->view()->tagged && dynamically_tagged(actuals[i]))
                recover([&]() { error(actuals[i], "a dynamically tagged actual of a specific tagged formal must be a controlling operand of a dispatching call (3.9.2(9/1))"); });
            // (a dynamically tagged actual (an access to a class-wide object) of a specific tagged formal: only a controlling operand of a
            // dispatching operation (3.9.2(9/1)))
            if (t != nullptr && formalType != nullptr && aCallee->sub != nullptr && aCallee->sub->controlling == nullptr && iSpeculation == 0 &&
                iInstanceBodies == 0 && aCallee->sub->op == intrinsic::None && formalType->view()->kind == type_kind::Access &&
                formalType->view()->anonymous_access && formalType->view()->designated != nullptr &&
                formalType->view()->designated->view()->kind != type_kind::ClassWide && formalType->view()->designated->view()->tagged &&
                t->view()->kind == type_kind::Access && designated_of(t) != nullptr && designated_of(t)->view()->kind == type_kind::ClassWide)
                recover([&]() { error(actuals[i], "a dynamically tagged actual of a specific tagged formal must be a controlling operand of a dispatching call (3.9.2(9/1))"); });
            // (the actual of an out or in out parameter: a variable (6.4.1(5)))
            if ((formals[i].mode == parameter_mode::Out || formals[i].mode == parameter_mode::InOut) && !is_variable(actuals[i]))
                error(actuals[i], "the actual for an out or in out parameter must be a variable");
            // (a view conversion of an array type: both or neither with aliased components, and not of a tagged or private component type
            // (4.6(24.8/2, 24.9/5)))
            if ((formals[i].mode == parameter_mode::Out || formals[i].mode == parameter_mode::InOut) && iSpeculation == 0 && iInstanceBodies == 0 &&
                actuals[i]->is(construct::Apply) && actuals[i]->size() == 2u && meaning_of(actuals[i]) == meaning::Conversion &&
                actuals[i]->type != nullptr && actuals[i]->child(1)->type != nullptr)
            {
                auto* target = actuals[i]->type->view()->base_type()->view();
                auto* operand = actuals[i]->child(1)->type->view()->base_type()->view();
                if (target->kind == type_kind::Array && operand->kind == type_kind::Array && target != operand && !target->is_formal && !operand->is_formal)
                {
                    if (target->aliased_components != operand->aliased_components)
                        recover([&]() { error(actuals[i], "in a view conversion of array types, both or neither must have aliased components (4.6(24.8/2))"); });
                    else if (auto* c = target->component; c != nullptr && (c->view()->base_type()->view()->tagged || c->kind == type_kind::Private ||
                        (c->base_type()->kind == type_kind::Private) || c->view()->kind == type_kind::ClassWide))
                        recover([&]() { error(actuals[i], "a view conversion of array types cannot have a tagged or private component type (4.6(24.9/5))"); });
                }
            }
            if (t != nullptr && aCallee->sub != nullptr && aCallee->sub->controlling != nullptr && formalType != nullptr)
            {
                auto* ft = formalType->view();
                if (ft->kind == type_kind::Access && ft->anonymous_access)
                {
                    if (t->view()->kind == type_kind::Access && designated_of(t) != nullptr && designated_of(t)->view()->kind == type_kind::ClassWide &&
                        ft->designated != nullptr && ft->designated->view()->kind != type_kind::ClassWide)
                        classWide = true;
                }
                else if (t->view()->kind == type_kind::ClassWide && ft->kind != type_kind::ClassWide)
                    classWide = true;
                else if (contract_formal(actuals[i], formalType))
                {
                    classWide = true;
                    call.contract_dispatching = true;
                }
                else if (ft->kind != type_kind::ClassWide && same_type(formalType, aCallee->sub->controlling))
                {
                    // (a dispatching call of a function with a controlling result: dynamically tagged (3.9.2(6/2)))
                    auto* a = actuals[i];
                    while (a->is(construct::Parenthesized) && !a->children.empty())
                        a = a->child(0);
                    if (auto const* c = meaning_of(a) == meaning::Call ? call_of(a) : nullptr; c != nullptr && c->dispatching && c->callee != nullptr &&
                        c->callee->sub != nullptr && c->callee->sub->result != nullptr && c->callee->sub->controlling != nullptr &&
                        same_type(c->callee->sub->result, c->callee->sub->controlling))
                        classWide = true;
                }
            }
        }
        // (a dispatching operation's controlling operands: not both statically and dynamically tagged (3.9.2(8)))
        if (aCallee->sub != nullptr && aCallee->sub->controlling != nullptr && aCallee->sub->op == intrinsic::None && iSpeculation == 0 && iInstanceBodies == 0)
        {
            auto* controlling = aCallee->sub->controlling;
            std::function<bool(node*)> indeterminate = [&](node* aActual) -> bool
            {
                auto* a = aActual;
                while (a != nullptr && (a->is(construct::Parenthesized) || a->is(construct::Qualified)) && !a->children.empty())
                    a = a->children.back();
                auto const* c = a != nullptr && meaning_of(a) == meaning::Call ? call_of(a) : nullptr;
                if (c == nullptr || c->dispatching || c->callee == nullptr || c->callee->sub == nullptr || c->callee->sub->controlling == nullptr ||
                    c->callee->sub->result == nullptr || !same_type(c->callee->sub->result, c->callee->sub->controlling))
                    return false;
                auto const& fs = c->callee->sub->parameters;
                for (std::size_t k = 0u; k < c->actuals.size() && k < fs.size(); ++k)
                    if (c->actuals[k] != nullptr && fs[k].type != nullptr && same_type(fs[k].type, c->callee->sub->controlling) && !indeterminate(c->actuals[k]))
                        return false;
                return true;
            };
            bool anyStatic = false;
            bool anyDynamic = false;
            for (std::size_t i = (aObject != nullptr ? 1u : 0u); i < actuals.size() && i < formals.size(); ++i)
                if (actuals[i] != nullptr && formals[i].type != nullptr && formals[i].type->view()->kind != type_kind::ClassWide &&
                    formals[i].type->view()->kind != type_kind::Access && same_type(formals[i].type, controlling) && actuals[i]->type != nullptr)
                {
                    if (dynamically_tagged(actuals[i]))
                        anyDynamic = true;
                    else if (!indeterminate(actuals[i]))
                        anyStatic = true;
                }
            if (anyStatic && anyDynamic)
                recover([&]() { error(aCall, "a call cannot have both statically tagged and dynamically tagged controlling operands (3.9.2(8))"); });
        }
        call.dispatching = classWide;
        // (a dispatching call: its controlling type not an incomplete view here (3.10.1(10.1/5)))
        if (classWide && aCallee->sub != nullptr && aCallee->sub->controlling != nullptr && iSpeculation == 0 && iInstanceBodies == 0)
            if (auto* c = aCallee->sub->controlling; c->incomplete || (c->base_type()->partial != nullptr && c->base_type()->partial->incomplete &&
                c->base_type()->decl != nullptr && c->base_type()->decl->decl != nullptr && c->base_type()->decl->decl->file == aCall->file &&
                c->base_type()->decl->decl->source.data() > aCall->source.data()))
                recover([&]() { error(aCall, "a dispatching call's controlling type cannot be an incomplete view here (3.10.1(10.1/5))"); });
        // (a call on an abstract subprogram: dispatching (3.9.3(7)); a renaming of one is abstract too (8.5.4(5.1/2)); a call with a
        // tag-indeterminate controlling operand (or none) can dispatch on its context)
        if (!classWide && aCallee->sub != nullptr && iSpeculation == 0 && iInstanceBodies == 0)
        {
            bool abstractCallee = aCallee->sub->is_abstract;
            bool primitive = aCallee->sub->controlling != nullptr;
            if (!abstractCallee && aCallee->sub->renames != nullptr && aCallee->sub->renames->sub != nullptr && aCallee->sub->renames->sub->is_abstract &&
                !aCallee->sub->inherited && aCallee->sub->renames->kind != entity_kind::Entry)
            {
                abstractCallee = true;
                primitive = false;
            }
            bool staticallyTagged = false;
            if (abstractCallee && primitive)
                for (std::size_t i = 0u; i < actuals.size(); ++i)
                    if (actuals[i] != nullptr && formals[i].type != nullptr && !formals[i].type->anonymous_access &&
                        same_type(formals[i].type, aCallee->sub->controlling) && actuals[i]->type != nullptr &&
                        actuals[i]->type->view()->kind != type_kind::ClassWide && meaning_of(actuals[i]) != meaning::Call)
                        staticallyTagged = true;
            // (all its controlling operands tag-indeterminate and its result not of the controlling type (no context to dispatch on): statically
            // tagged (3.9.2(19/2)))
            if (abstractCallee && primitive && !staticallyTagged && aCallee->sub->controlling != nullptr &&
                !(aCallee->sub->result != nullptr && same_type(aCallee->sub->result, aCallee->sub->controlling)))
            {
                bool any = false;
                bool indeterminate = true;
                for (std::size_t i = 0u; i < actuals.size(); ++i)
                    if (actuals[i] != nullptr && formals[i].type != nullptr && !formals[i].type->anonymous_access &&
                        same_type(formals[i].type, aCallee->sub->controlling))
                    {
                        any = true;
                        auto* a = actuals[i];
                        while (a->is(construct::Parenthesized) && !a->children.empty())
                            a = a->child(0);
                        indeterminate = indeterminate && meaning_of(a) == meaning::Call && a->type != nullptr && a->type->view()->kind != type_kind::ClassWide;
                    }
                staticallyTagged = any && indeterminate;
            }
            if (abstractCallee && (!primitive || staticallyTagged))
                error(aCall, "a call on an abstract subprogram must be a dispatching call");
            // (a statically tagged call: its tag-indeterminate controlling operands statically tagged too, not calls on abstract subprograms)
            if (!abstractCallee && aCallee->sub->controlling != nullptr)
                for (std::size_t i = 0u; i < actuals.size(); ++i)
                    if (actuals[i] != nullptr && formals[i].type != nullptr && !formals[i].type->anonymous_access &&
                        same_type(formals[i].type, aCallee->sub->controlling))
                    {
                        auto* a = actuals[i];
                        while (a->is(construct::Parenthesized) && !a->children.empty())
                            a = a->child(0);
                        // (a chain of tag-indeterminate calls (F (G (...)))
                        while (a != nullptr && meaning_of(a) == meaning::Call)
                        {
                            auto const* c = call_of(a);
                            if (c == nullptr || c->callee == nullptr || c->callee->sub == nullptr || c->dispatching)
                                break;
                            if (c->callee->sub->is_abstract && c->callee->sub->result != nullptr && c->callee->sub->controlling != nullptr &&
                                same_type(c->callee->sub->result, c->callee->sub->controlling))
                            {
                                recover([&]() { error(a, "a call on an abstract subprogram must be a dispatching call (here statically tagged, 3.9.3(7))"); });
                                break;
                            }
                            // (its controlling actual)
                            node* next = nullptr;
                            for (std::size_t k = 0u; k < c->actuals.size() && k < c->callee->sub->parameters.size(); ++k)
                                if (c->actuals[k] != nullptr && c->callee->sub->parameters[k].type != nullptr &&
                                    same_type(c->callee->sub->parameters[k].type, c->callee->sub->controlling))
                                    next = c->actuals[k];
                            a = next;
                            while (a != nullptr && a->is(construct::Parenthesized) && !a->children.empty())
                                a = a->child(0);
                        }
                    }
        }
        if (aObject != nullptr && aCallee->sub != nullptr)
            prefixed_view_check(aObject, aCallee);
        // (an atomic (volatile) actual: of a formal whose type is atomic (volatile) or passed by copy (not a by-reference type) (C.6(12/3)))
        if (iSpeculation == 0 && iInstanceBodies == 0 && aCallee->sub != nullptr)
        {
            std::function<bool(ada_type*, int)> byReference = [&](ada_type* aType, int aDepth) -> bool
            {
                if (aType == nullptr || aDepth > 16 || aType->anonymous_access)
                    return false;
                auto* v = aType->view();
                if (v->is_formal)
                    return false;
                auto* b = v->base_type()->view();
                if (b->tagged || b->kind == type_kind::ClassWide || b->kind == type_kind::Task || b->kind == type_kind::Protected ||
                    (b->kind == type_kind::Record && b->limited) || type_aspect(b, "volatile") != nullptr || type_aspect(b, "atomic") != nullptr)
                    return true;
                if (b->kind == type_kind::Array)
                    return byReference(b->component, aDepth + 1);
                if (b->kind == type_kind::Record)
                    for (auto const& c : b->components)
                        if (c.type != nullptr && byReference(c.type, aDepth + 1))
                            return true;
                return false;
            };
            auto const& parameters = aCallee->sub->parameters;
            for (std::size_t i = 0u; i < actuals.size() && i < parameters.size(); ++i)
            {
                if (actuals[i] == nullptr || parameters[i].type == nullptr || parameters[i].type->anonymous_access)
                    continue;
                auto const [atomic, isVolatile] = atomic_view(actuals[i]);
                if (!atomic && !isVolatile)
                    continue;
                auto* ft = parameters[i].type;
                bool const formalAtomic = type_aspect(ft, "atomic") != nullptr;
                bool const formalVolatile = formalAtomic || type_aspect(ft, "volatile") != nullptr;
                if ((atomic && !formalAtomic && byReference(ft, 0)) || (isVolatile && !formalVolatile && byReference(ft, 0)))
                    recover([&]() { error(actuals[i], std::string{ atomic ? "an atomic" : "a volatile" } + " actual cannot be passed to a formal of a by-reference type that is not " +
                        (atomic ? "atomic" : "volatile") + " (C.6(12/3))"); });
            }
        }
        // (not an actual of an untagged incomplete view, nor a result of an incomplete view, here (3.10.1(10/3)))
        if (iSpeculation == 0 && iInstanceBodies == 0 && aCallee->sub != nullptr)
        {
            auto const incomplete_here = [&](ada_type* aType)
            {
                if (aType == nullptr || aType->anonymous_access)
                    return false;
                auto* t = aType->kind == type_kind::ClassWide && aType->specific != nullptr ? aType->specific : aType;
                if (t->kind != type_kind::Private || !t->incomplete)
                    return false;
                auto* view = t->decl != nullptr && t->decl->scope != nullptr && t->decl->scope->owner != nullptr && t->decl->scope->owner->limited_view ?
                    t->decl->scope->owner : nullptr;
                return (t->full == nullptr && !t->completing) || (view != nullptr && !withed_here(view->name) && iCurrentUnit != nullptr &&
                    iCurrentUnit->name != view->name && !iCurrentUnit->name.starts_with(view->name + "."));
            };
            for (std::size_t i = 0u; i < actuals.size() && i < formals.size(); ++i)
                if (actuals[i] != nullptr && incomplete_here(formals[i].type) && !formals[i].type->tagged)
                    error(actuals[i], "an actual parameter cannot be of an untagged incomplete view (3.10.1(10/3))");
            if (aCallee->sub->result != nullptr && incomplete_here(aCallee->sub->result))
                error(aCall, "the result of a function call cannot be of an incomplete view (3.10.1(10/3))");
        }
        aCall->ent = aCallee;
        set_meaning(aCall, meaning::Call);
        aCall->type = aCallee->sub != nullptr && aCallee->sub->result != nullptr ? aCallee->sub->result : iVoid;
        if (aCallee->kind == entity_kind::EnumerationLiteral)
        {
            set_meaning(aCall, meaning::Literal);
            aCall->type = aCallee->type;
        }
        return aCall->type;
    }

    ada_type* program::resolve_apply(node* aApply, ada_type* aExpected, region* aRegion)
    {
        auto* prefix = aApply->child(0);
        std::vector<node*> arguments(aApply->children.begin() + 1, aApply->children.end());
        // (a conversion to T'Base or T'Class)
        if (prefix->is(construct::Attribute) && arguments.size() == 1u && !arguments[0]->is(construct::Association))
            if (auto const a = name_text(prefix->child(1)->source); (a == "base" || a == "class") && is_subtype_name(prefix->child(0), aRegion))
            {
                auto* t = subtype_mark(prefix, aRegion);
                set_meaning(prefix, meaning::Type);
                prefix->type = t;
                resolve_conversion_operand(arguments[0], t, aRegion);
                set_meaning(aApply, meaning::Conversion);
                aApply->type = t;
                return t;
            }
        // (an entry family member called: Family (Index) (actuals))
        if (prefix->is(construct::Apply) && prefix->size() == 2u)
        {
            auto* familyName = prefix->child(0);
            bool family = false;
            std::vector<entity*> named;
            if (is_name_node(familyName))
                try { speculation guard{ *this }; named = prefix_entities(familyName, aRegion); } catch (compile_error const&) {}
            for (auto* e : named)
                family = family || (e->kind == entity_kind::Entry && e->sub->family_type != nullptr);
            if (!family && familyName->is(construct::Selected))
            {
                // (Object.Family)
                for (auto const& o : interpretations(familyName->child(0), aRegion))
                    if (o.type != nullptr)
                    {
                        auto* t = o.type->view();
                        if (t->is_access() && t->kind == type_kind::Access)
                            t = designated_of(t)->view();
                        if ((t->kind == type_kind::Task || t->kind == type_kind::Protected) && t->unit != nullptr && t->unit->inner != nullptr)
                        {
                            auto range = t->unit->inner->names.equal_range(name_text(familyName->child(1)->source));
                            for (auto i = range.first; i != range.second; ++i)
                                family = family || (i->second->kind == entity_kind::Entry && i->second->sub->family_type != nullptr);
                        }
                    }
            }
            if (family)
            {
                resolve_apply(prefix, nullptr, aRegion);
                auto const member = iCalls[prefix];
                if (member.callee != nullptr && member.callee->kind == entity_kind::Entry)
                {
                    auto* r = finish_call(aApply, member.callee, arguments, nullptr, aRegion);
                    auto& call = iCalls[aApply];
                    call.object = member.object;
                    call.family_index = member.family_index;
                    return r;
                }
            }
        }
        // (the prefix denotes subprograms, entries or a type)
        std::vector<entity*> entities;
        if (is_name_node(prefix))
            entities = prefix_entities(prefix, aRegion);
        node* operatorSymbol = prefix->is(construct::Operator) ? prefix :
            prefix->is(construct::Selected) && prefix->child(1)->is(construct::Operator) ? prefix->child(1) : nullptr;
        if (operatorSymbol != nullptr && is_operator_symbol(operatorSymbol))
        {
            // ("op" (L, R): the operators of the operands' types (and the expected type's), as for an operator)
            std::vector<node*> operands;
            for (auto* a : arguments)
                operands.push_back(a->is(construct::Association) ? a->children.back() : a);
            // (P."op": only the operators declared in P (4.1.3(10)))
            auto* declaredIn = selected_operator_scope(prefix, aRegion);
            // (the universal fixed and root numeric operators: Standard's (4.5.5(18), (19)))
            auto const standards = [](entity* op) { return op->sub != nullptr && (op->sub->op == intrinsic::UniversalFixedMultiply ||
                op->sub->op == intrinsic::UniversalFixedDivide || op->sub->op == intrinsic::RootMultiply || op->sub->op == intrinsic::RootDivide); };
            auto const declared = [&](entity* op) { return declaredIn == nullptr || (standards(op) ? declaredIn == iStandard : op->scope == declaredIn); };
            for (auto* op : operator_candidates(designator_text(operatorSymbol), operands, aRegion))
                if (declared(op) && std::find(entities.begin(), entities.end(), op) == entities.end())
                    entities.push_back(op);
            if (aExpected != nullptr && !aExpected->is_universal())
                for (auto* op : operators_of(aExpected))
                    if (op->name == designator_text(operatorSymbol) && op->sub->parameters.size() == operands.size() && declared(op) &&
                        std::find(entities.begin(), entities.end(), op) == entities.end())
                        entities.push_back(op);
            // (a predefined operator hidden by a directly visible user-defined homograph (8.3(12/3)), whose parameters' names are its own)
            auto const user = [](entity* x) { return x->sub != nullptr && (x->sub->op == intrinsic::None || x->sub->op == intrinsic::NotEqual || 
                x->sub->renames != nullptr); };
            // (P."op": one declared explicitly in P overrides the predefined one of its profile (8.3(10/1)))
            std::erase_if(entities, [&](entity* e)
                {
                    return e->sub != nullptr && !user(e) && std::any_of(entities.begin(), entities.end(), [&](entity* other)
                        { return other != e && user(other) && other->scope != nullptr && (within_scope(other->scope, aRegion) ||
                            (declaredIn != nullptr && other->scope == declaredIn)) && type_conformant(*other->sub, *e->sub); });
                });
            // (the root numeric types' operators (universal operands): not for an operand of another type (it is not implicitly 
            // converted), nor where another type is expected (their result is of the root type: not implicitly converted (4.6(48/3)))
            if ((aExpected != nullptr && !aExpected->is_universal() && aExpected->view()->is_numeric()) || std::any_of(operands.begin(), operands.end(), [&](node* o)
                {
                    auto const is = interpretations(o, aRegion);
                    return !is.empty() && std::all_of(is.begin(), is.end(), [](interpretation const& i) { return i.type != nullptr && !i.type->is_universal(); });
                }))
                std::erase_if(entities, [&](entity* e) { return e->sub != nullptr && !user(e) && e->sub->operator_type != nullptr && e->sub->operator_type->is_universal(); });
        }
        // (an object (or a package...): indexed, sliced or called through)
        if (entities.size() == 1u && !entities.front()->overloadable() && !entities.front()->is_type())
            entities.clear();
        if (!entities.empty())
        {
            // (a type conversion)
            for (auto* e : entities)
                if (e->is_type())
                {
                    if (arguments.size() != 1u || arguments[0]->is(construct::Association))
                    {
                        // (a subtype with a constraint, e.g. in a slice: handled by the caller)
                        error(aApply, "type conversion needs one operand");
                    }
                    prefix->ent = e;
                    set_meaning(prefix, meaning::Type);
                    prefix->type = e->type;
                    resolve_conversion_operand(arguments[0], e->type, aRegion);
                    aApply->ent = e;
                    set_meaning(aApply, meaning::Conversion);
                    aApply->type = e->type;
                    return e->type;
                }
            // (calls: the subprograms whose profiles match the actuals and whose results are of the expected type)
            std::vector<entity*> candidates;
            for (auto* e : entities)
                if ((e->sub != nullptr || e->kind == entity_kind::EnumerationLiteral) && match_call(e, arguments, nullptr, aRegion, nullptr, true))
                {
                    auto* rt = e->kind == entity_kind::EnumerationLiteral ? e->type : (e->sub->result != nullptr ? e->sub->result : iVoid);
                    if (aExpected == nullptr || covers_dereferenced(aExpected, rt) || (aExpected == iVoid && rt == iVoid))
                        candidates.push_back(e);
                }
            // (a parameterless function's result indexed, or an entry family's member: of the functions whose results are arrays, one
            // whose index types and component type fit (8.6(28)))
            if (candidates.empty())
            {
                std::vector<entity*> ordered;
                for (auto* e : entities)
                    if (e->kind == entity_kind::Function && e->sub != nullptr && e->sub->result != nullptr &&
                        indexed_result_fits(e->sub->result, arguments, aExpected, aRegion))
                        ordered.push_back(e);
                for (auto* e : entities)
                    if (std::find(ordered.begin(), ordered.end(), e) == ordered.end())
                        ordered.push_back(e);
                for (auto* e : ordered)
                {
                    if (e->kind == entity_kind::Entry && e->sub->family_type != nullptr && arguments.size() == 1u)
                    {
                        // (an entry family member: Family (Index); not a slice of the family (9.5.3(3)))
                        if (arguments[0]->is(construct::Range) || arguments[0]->is(construct::SubtypeIndication))
                            error(arguments[0], "a call of an entry family must give one entry index (an expression)");
                        resolve(arguments[0], e->sub->family_type, aRegion);
                        prefix->ent = e;
                        aApply->ent = e;
                        set_meaning(aApply, meaning::Entry);
                        auto& call = iCalls[aApply];
                        call.callee = e;
                        call.family_index = arguments[0];
                        aApply->type = iVoid;
                        return iVoid;
                    }
                    if (e->kind == entity_kind::Function)
                    {
                        bool callable = true;
                        for (auto const& p : e->sub->parameters)
                            callable = callable && p.default_expression != nullptr;
                        auto* rt = e->sub->result != nullptr ? e->sub->result->view() : nullptr;
                        if (callable && rt != nullptr && (rt->kind == type_kind::Array || rt->kind == type_kind::Access || rt->kind == type_kind::AccessSubprogram))
                        {
                            // (this function called (not another homograph that the name could denote))
                            if (prefix->is(construct::Identifier) || prefix->is(construct::Selected))
                                finish_call(prefix, e, {}, nullptr, aRegion);
                            else
                                resolve_name(prefix, rt, aRegion);
                            return resolve_indexing(aApply, prefix, arguments, aExpected, aRegion);
                        }
                    }
                }
                std::string message = "no subprogram \"" + name_text(prefix->source) + "\" matches this call";
                error(aApply, message);
            }
            if (candidates.size() > 1u)
                candidates = prefer(candidates, arguments, aExpected, aRegion);
            // (views of one declaration (e.g. an entity of it made again): one interpretation)
            for (std::size_t i = 1u; i < candidates.size(); )
                if (std::any_of(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(i),
                    [&](entity* c) { return c == candidates[i] || (candidates[i]->decl != nullptr && c->decl == candidates[i]->decl) ||
                        // (a predefined operator and its inherited copy: one operation)
                        (c->sub != nullptr && candidates[i]->sub != nullptr && c->sub->op != intrinsic::None && c->sub->op == candidates[i]->sub->op &&
                         c->sub->op != intrinsic::NotEqual && type_conformant(*c->sub, *candidates[i]->sub)); }))
                    candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(i));
                else
                    ++i;
            // (more than one interpretation: ambiguous (8.6(31)))
            if (candidates.size() > 1u && iSpeculation == 0 && iInstanceBodies == 0 && iInstantiationSites.empty())
                recover([&]() { error(aApply, "the call of \"" + name_text(prefix->source) + "\" is ambiguous (" + std::to_string(candidates.size()) +
                    " subprograms match it) (8.6(31))"); });
            prefix->ent = candidates.front();
            set_meaning(prefix, meaning::Subprogram);
            return finish_call(aApply, candidates.front(), arguments, nullptr, aRegion);
        }
        // (a prefixed view call: Object.Operation (actuals))
        if (prefix->is(construct::Selected))
        {
            auto* objectNode = prefix->child(0);
            auto const name = name_text(prefix->child(1)->source);
            auto const prefixes = interpretations(objectNode, aRegion);
            bool component = false;
            for (auto const& p : prefixes)
            {
                if (p.type == nullptr)
                    continue;
                auto* t = p.type->view();
                if (t->kind == type_kind::Access)
                    t = designated_of(t) != nullptr ? designated_of(t)->view() : t;
                auto* rt = t->kind == type_kind::ClassWide ? t->specific->view() : t;
                for (auto const& c : rt->base_type()->view()->components)
                    if (c.ent != nullptr && !c.stored && c.ent->name == name)
                        component = true;
                if (component)
                    break;
                // (task entries and protected operations)
                if ((t->kind == type_kind::Task || t->kind == type_kind::Protected) && t->unit != nullptr && t->unit->inner != nullptr)
                {
                    auto range = t->unit->inner->names.equal_range(name);
                    std::vector<entity*> ops;
                    for (auto i = range.first; i != range.second; ++i)
                        if (i->second->is_subprogram())
                            ops.push_back(i->second);
                    for (auto* op : ops)
                    {
                        // (an entry family member: Object.Family (Index) (actuals))
                        if (op->kind == entity_kind::Entry && op->sub->family_type != nullptr && arguments.size() == 1u)
                        {
                            resolve(objectNode, p.type, aRegion);
                            if (arguments[0]->is(construct::Range) || arguments[0]->is(construct::SubtypeIndication))
                                error(arguments[0], "a call of an entry family must give one entry index (an expression)");
                            resolve(arguments[0], op->sub->family_type, aRegion);
                            aApply->ent = op;
                            set_meaning(aApply, meaning::Entry);
                            auto& call = iCalls[aApply];
                            call.callee = op;
                            call.object = objectNode;
                            call.family_index = arguments[0];
                            synchronized_target_check(op, objectNode, aApply);
                            aApply->type = iVoid;
                            return iVoid;
                        }
                        if (match_call(op, arguments, nullptr, aRegion, nullptr, true))
                        {
                            auto* opResult = op->sub->result != nullptr ? op->sub->result : iVoid;
                            if (aExpected != nullptr && !covers_dereferenced(aExpected, opResult) && !(aExpected == iVoid && opResult == iVoid))
                                continue;
                            resolve(objectNode, p.type, aRegion);
                            if (p.type->view()->kind == type_kind::Access)
                                aApply->mark |= MarkDeref;
                            auto* r = finish_call(aApply, op, arguments, nullptr, aRegion);
                            iCalls[aApply].object = objectNode;
                            synchronized_target_check(op, objectNode, aApply);
                            return r;
                        }
                    }
                }
                std::vector<entity*> candidates;
                for (auto* op : prefixed_candidates(t, name))
                    if (match_call(op, arguments, objectNode, aRegion, nullptr, true))
                    {
                        auto* opResult = op->sub->result != nullptr ? op->sub->result : iVoid;
                        if (aExpected == nullptr || covers_dereferenced(aExpected, opResult) || (aExpected == iVoid && opResult == iVoid))
                            candidates.push_back(op);
                    }
                if (!candidates.empty())
                {
                    if (candidates.size() > 1u)
                        candidates = prefer(candidates, arguments, aExpected, aRegion);
                    return finish_call(aApply, candidates.front(), arguments, objectNode, aRegion);
                }
            }
        }
        // (the prefix is a value: an array (indexed component or slice), an access to an array or subprogram, or a function's result)
        return resolve_indexing(aApply, prefix, arguments, aExpected, aRegion);
    }

    ada_type* program::resolve_indexing(node* aApply, node* aPrefix, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion)
    {
        // (the prefix's interpretations (its own, if it is resolved already); those whose arrays the arguments fit first)
        auto options = meaning_of(aPrefix) == meaning::Call && aPrefix->type != nullptr ? std::vector<interpretation>{ interpretation{ aPrefix->type, aPrefix->ent } } :
            interpretations(aPrefix, aRegion);
        std::stable_partition(options.begin(), options.end(), [&](interpretation const& o)
            {
                return o.type != nullptr && o.type->view()->kind == type_kind::Array && indexed_result_fits(o.type, aArguments, aExpected, aRegion);
            });
        for (auto const& o : options)
        {
            if (o.type == nullptr)
                continue;
            auto* t = o.type->view();
            bool deref = false;
            if (t->kind == type_kind::Access)
            {
                t = designated_of(t) != nullptr ? designated_of(t)->view() : t;
                deref = true;
            }
            if (t->kind == type_kind::AccessSubprogram || (t->kind == type_kind::Access && false))
            {
                // (a call through an access-to-subprogram value (P.all (...): P's))
                auto* object = aPrefix->is(construct::All) ? aPrefix->child(0) : aPrefix;
                if (meaning_of(object) == meaning::None)
                    resolve(object, o.type, aRegion);
                auto* profile = t->profile;
                auto* holder = new_entity(profile->result != nullptr ? entity_kind::Function : entity_kind::Procedure, "%indirect", aApply, aRegion);
                holder->sub->parameters = profile->parameters;
                holder->sub->result = profile->result;
                holder->type = profile->result;
                auto* r = finish_call(aApply, holder, aArguments, nullptr, aRegion);
                iCalls[aApply].object = object;
                aApply->mark |= 0x800u;     // (an indirect call)
                return r;
            }
            if (t->kind != type_kind::Array)
                continue;
            // (a partial view (its full view not visible here): not an array (7.3(4)))
            if (iSpeculation == 0 && iInstanceBodies == 0 && partial_view_here(deref ? o.type->view()->designated : o.type, aRegion))
                continue;
            if (meaning_of(aPrefix) == meaning::None)
                resolve(aPrefix, o.type, aRegion);
            if (deref)
                aApply->mark |= MarkDeref;
            // (a slice: one discrete range)
            if (aArguments.size() == 1u && t->indexes.size() == 1u && is_discrete_range(aArguments[0], aRegion))
            {
                auto* indexType = t->indexes[0];
                auto* range = discrete_range(aArguments[0], indexType, aRegion);
                predicate_check(range, aArguments[0]);
                set_meaning(aApply, meaning::Slice);
                // (the slice's subtype: the array type constrained by the range)
                auto* sliceType = new_subtype(t->base_type());
                sliceType->constrained = true;
                sliceType->indexes = { aArguments[0]->type != nullptr ? aArguments[0]->type : indexType };
                aApply->type = sliceType;
                return sliceType;
            }
            if (aArguments.size() != t->indexes.size())
                error(aApply, "wrong number of indexes");
            // (indexes: expressions (a slice: of a one-dimensional array, 4.1.2(2)))
            for (auto* a : aArguments)
                if (a->is(construct::Range) || (a->is(construct::Attribute) && a->size() >= 2u && name_text(a->child(1)->source) == "range") ||
                    a->is(construct::SubtypeIndication))
                    error(a, "a slice must be of a one-dimensional array (4.1.2(2))");
            for (std::size_t i = 0u; i < aArguments.size(); ++i)
                resolve(aArguments[i], t->indexes[i]->base_type(), aRegion);
            set_meaning(aApply, meaning::Index);
            aApply->type = t->component;
            return t->component;
        }
        // (a generalized indexing (Constant_Indexing / Variable_Indexing aspects))
        if (auto* r = resolve_generalized_indexing(aApply, aPrefix, aArguments, aExpected, aRegion))
            return r;
        error(aApply, "\"" + name_text(aPrefix->source) + "\" cannot be indexed or called");
    }

    std::vector<entity*> program::prefer(std::vector<entity*> const& aCandidates, std::vector<node*> const& aArguments, ada_type* aExpected, region* aRegion)
    {
        // (an actual P'Access of an access-to-subprogram formal: a subprogram P of the formal's profile (3.10.2(2.2/2)))
        if (aCandidates.size() > 1u)
        {
            std::vector<entity*> fitting;
            for (auto* c : aCandidates)
            {
                std::vector<node*> actuals;
                if (c->sub == nullptr || !match_call(c, aArguments, nullptr, aRegion, &actuals, true))
                {
                    fitting.push_back(c);
                    continue;
                }
                bool fits = true;
                for (std::size_t i = 0u; fits && i < actuals.size() && i < c->sub->parameters.size(); ++i)
                {
                    auto* a = actuals[i];
                    while (a != nullptr && a->is(construct::Parenthesized))
                        a = a->child(0);
                    auto* ft = c->sub->parameters[i].type != nullptr ? c->sub->parameters[i].type->view() : nullptr;
                    if (a == nullptr || ft == nullptr || ft->kind != type_kind::AccessSubprogram || ft->profile == nullptr || !a->is(construct::Attribute) ||
                        a->size() < 2u || (name_text(a->child(1)->source) != "access" && name_text(a->child(1)->source) != "unchecked_access") ||
                        !(a->child(0)->is(construct::Identifier) || a->child(0)->is(construct::Selected) || a->child(0)->is(construct::Operator)))
                        continue;
                    bool any = false;
                    bool subprograms = false;
                    for (auto* e : prefix_entities(a->child(0), aRegion))
                        if (e->sub != nullptr && e->is_subprogram())
                        {
                            subprograms = true;
                            any = any || type_conformant(*e->sub, *ft->profile);
                        }
                    fits = !subprograms || any;
                }
                if (fits)
                    fitting.push_back(c);
            }
            if (!fitting.empty() && fitting.size() < aCandidates.size())
                return fitting.size() == 1u ? fitting : prefer(fitting, aArguments, aExpected, aRegion);
        }
        // (among calls that match: in a universal context, a root numeric type's operator whose operands are universal (8.6(29/3));
        // one whose actuals' types are exactly its formals' types (not universal), then one that is not predefined; the innermost)
        if (aExpected == nullptr || aExpected->is_universal())
        {
            std::vector<entity*> root;
            for (auto* c : aCandidates)
            {
                if (c->sub == nullptr || c->sub->op == intrinsic::None || c->sub->operator_type == nullptr || !c->sub->operator_type->is_universal())
                    continue;
                std::vector<node*> actuals;
                if (!match_call(c, aArguments, nullptr, aRegion, &actuals, true))
                    continue;
                bool universal = true;
                for (auto* a : actuals)
                    if (a != nullptr)
                    {
                        bool u = false;
                        for (auto const& o : interpretations(a, aRegion))
                            u = u || (o.type != nullptr && o.type->is_universal());
                        universal = universal && u;
                    }
                if (universal)
                    root.push_back(c);
            }
            if (root.size() == 1u)
                return root;
        }
        std::vector<entity*> exact;
        for (auto* c : aCandidates)
        {
            if (c->sub == nullptr)
                continue;
            std::vector<node*> actuals;
            if (!match_call(c, aArguments, nullptr, aRegion, &actuals, true))
                continue;
            bool allExact = true;
            for (std::size_t i = 0u; i < actuals.size(); ++i)
            {
                if (actuals[i] == nullptr)
                    continue;
                auto const options = interpretations(actuals[i], aRegion);
                bool found = false;
                for (auto const& o : options)
                    if (o.type != nullptr && !o.type->is_universal() && same_type(o.type, c->sub->parameters[i].type))
                        found = true;
                allExact = allExact && found;
            }
            if (allExact)
                exact.push_back(c);
        }
        if (exact.size() == 1u)
            return exact;
        if (exact.size() > 1u)
        {
            // (in an instance: the one whose formals' subtypes are the actuals' formal types (as resolved in the generic, 12.3(11)))
            std::vector<entity*> formal;
            for (auto* c : exact)
            {
                std::vector<node*> actuals;
                if (!match_call(c, aArguments, nullptr, aRegion, &actuals, true))
                    continue;
                bool all = true;
                for (std::size_t i = 0u; i < actuals.size() && all; ++i)
                {
                    if (actuals[i] == nullptr)
                        continue;
                    // (not one whose formal's subtype is another formal type than the actual's)
                    bool found = false;
                    auto* fv = c->sub->parameters[i].type->formal_view;
                    for (auto const& o : interpretations(actuals[i], aRegion))
                        found = found || (o.type != nullptr && same_type(o.type, c->sub->parameters[i].type) &&
                            (fv == nullptr || o.type->formal_view == nullptr || o.type->formal_view == fv));
                    all = found;
                }
                if (all)
                    formal.push_back(c);
            }
            if (formal.size() == 1u)
                return formal;
        }
        std::vector<entity*> user;
        for (auto* c : (exact.empty() ? aCandidates : exact))
            if (c->sub == nullptr || c->sub->op == intrinsic::None || c->sub->op == intrinsic::Import || c->sub->op == intrinsic::NotEqual)
                user.push_back(c);
        if (!user.empty())
            return user;
        (void)aExpected;
        return aCandidates;
    }
}
