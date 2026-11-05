/*
  categorization.cpp

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

// Ada: the categorization of library units: preelaborated and declared pure units (10.2.1) and the categories of Annex E (E.2):
// their semantic dependences, and the constructs (preelaborable, pure) that their compilation units may contain

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <functional>
#include <set>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        // the name of a name (an identifier or selected components), e.g. "ada.text_io"
        std::string dotted(node* aName)
        {
            if (aName == nullptr)
                return {};
            if (aName->is(construct::Selected) && aName->size() == 2u)
                return dotted(aName->child(0)) + "." + name_text(aName->child(1)->source);
            return name_text(aName->source);
        }

        // a pragma's argument names the unit (its expanded name, or its simple name)
        bool names_unit(node* aArgument, std::string const& aUnit)
        {
            auto const a = dotted(aArgument->children.empty() ? aArgument : aArgument->children.back());
            return a == aUnit || (aUnit.size() > a.size() && aUnit.ends_with(a) && aUnit[aUnit.size() - a.size() - 1u] == '.');
        }

        node* specification_of(node* aItem)
        {
            for (auto* c : aItem->children)
                if (c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification))
                    return c;
            return nullptr;
        }
    }

    categorization const& program::unit_categorization(std::string const& aUnit)
    {
        if (auto c = iCategorizations.find(aUnit); c != iCategorizations.end())
            return c->second;
        categorization result;
        iCategorizations[aUnit] = result;   // (while it is determined (a renaming of itself): normal)
        auto* u = find_unit(aUnit, false);
        if (u == nullptr)
            u = find_unit(aUnit, true);     // (a library subprogram body without a declaration)
        if (u == nullptr || u->item == nullptr || u->subunit)
            return iCategorizations[aUnit];
        auto* item = u->item;
        // (a library unit renaming: the renamed unit's)
        if (item->is(construct::RenamingPackage) || item->is(construct::RenamingSubprogram) || item->is(construct::RenamingGeneric))
        {
            node* renamed = nullptr;
            for (auto* c : item->children)
                if (c->is(construct::Identifier) || c->is(construct::Selected))
                    renamed = c;
            if (renamed != nullptr)
                result = unit_categorization(dotted(renamed));
            return iCategorizations[aUnit] = result;
        }
        auto const apply = [&](std::string const& aName, node* aWhere)
        {
            auto const kind = aName == "pure" ? categorization::Pure : aName == "preelaborate" ? categorization::Preelaborate :
                aName == "shared_passive" ? categorization::SharedPassive : aName == "remote_types" ? categorization::RemoteTypes :
                aName == "remote_call_interface" ? categorization::RemoteCallInterface : aName == "all_calls_remote" ?
                categorization::AllCallsRemote : categorization::Count;
            if (kind != categorization::Count && result.where[kind] == nullptr)
                result.where[kind] = aWhere;
        };
        // (its aspects)
        auto const aspects = [&](node* aNode)
        {
            if (aNode != nullptr)
                if (auto* list = aNode->first(construct::Aspects))
                    for (auto* a : list->children)
                        if (a->is(construct::Aspect) && !a->children.empty())
                        {
                            auto* value = a->size() >= 2u ? a->children.back() : nullptr;
                            if (value == nullptr || name_text(value->source) != "false")
                                apply(name_text(a->child(0)->source), a);
                        }
        };
        // (pragmas: those (first) in a declarative part or context clause, applying to the unit)
        auto const pragmas = [&](node* aPart, bool aNamed)
        {
            if (aPart != nullptr)
                for (auto* p : aPart->children)
                    if (p->is(construct::Pragma) && !p->children.empty())
                    {
                        auto* argument = p->first(construct::PragmaArgument);
                        if (argument != nullptr ? names_unit(argument, aUnit) : !aNamed)
                            apply(name_text(p->child(0)->source), p);
                    }
        };
        aspects(item);
        for (auto* c : item->children)
            if (c->is(construct::Package) || c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification))
                aspects(c);
        node* package = item->is(construct::Package) ? item : item->is(construct::Generic) ? item->first(construct::Package) : nullptr;
        if (package != nullptr)
            pragmas(package->first(construct::Declarations), false);
        else
        {
            // (a library subprogram body: pragmas first in its declarative part)
            if (item->is(construct::SubprogramBody))
                pragmas(item->first(construct::Declarations), false);
            // (a library subprogram (or generic subprogram) declaration or instantiation: pragmas after it (at the place of a
            // compilation unit, the next unit's context) naming it)
            if (auto* compilation = u->unit->parent)
                for (std::size_t i = 0u; i + 1u < compilation->size(); ++i)
                    if (compilation->child(i) == u->unit)
                        pragmas(compilation->child(i + 1u)->first(construct::Context), true);
        }
        return iCategorizations[aUnit] = result;
    }

    bool program::remote_access_type(ada_type* aType)
    {
        for (int depth = 0; aType != nullptr && depth < 32; ++depth)
        {
            auto* b = aType->view()->base_type()->view();
            if (b->kind != type_kind::Access && b->kind != type_kind::AccessSubprogram)
                return false;
            if (b->anonymous_access)
                return false;
            auto* d = b->decl != nullptr ? b->decl : aType->base_type()->decl;
            if (d != nullptr && !d->is_private && d->scope != nullptr && d->scope->owner != nullptr && d->scope->owner->library &&
                d->scope->owner->is_package())
            {
                auto const& c = unit_categorization(full_name(d->scope->owner));
                if (c.is(categorization::RemoteTypes) || c.is(categorization::RemoteCallInterface))
                    return true;
            }
            if (b->parent == nullptr || b->parent->base_type()->view() == b)
                return false;
            aType = b->parent;
        }
        return false;
    }

    bool program::remote_subprogram(entity* aSubprogram)
    {
        if (aSubprogram == nullptr || !aSubprogram->is_subprogram())
            return false;
        if (aSubprogram->library)
            return unit_categorization(full_name(aSubprogram)).is(categorization::RemoteCallInterface);
        auto* owner = aSubprogram->scope != nullptr ? aSubprogram->scope->owner : nullptr;
        return !aSubprogram->is_private && owner != nullptr && owner->library && owner->is_package() &&
            unit_categorization(full_name(owner)).is(categorization::RemoteCallInterface);
    }

    void program::remote_dereference_check(node* aRoot)
    {
        auto const racw = [&](ada_type* aType)
        {
            if (aType == nullptr)
                return false;
            auto* v = aType->view();
            return v->kind == type_kind::Access && v->designated != nullptr && v->designated->view()->kind == type_kind::ClassWide &&
                remote_access_type(aType);
        };
        // (the dereferences (explicit, or implicit conversions to an anonymous access type) that are controlling operands of dispatching calls)
        std::set<node*> allowed;
        std::function<void(node*)> calls = [&](node* aNode)
        {
            if (auto const* call = call_of(aNode); call != nullptr && call->callee != nullptr && call->callee->sub != nullptr)
            {
                auto const& parameters = call->callee->sub->parameters;
                auto* controlling = call->callee->sub->controlling != nullptr ? call->callee->sub->controlling->view()->base_type()->view() : nullptr;
                for (std::size_t i = 0u; i < call->actuals.size() && i < parameters.size(); ++i)
                {
                    auto* a = call->actuals[i];
                    while (a != nullptr && a->is(construct::Parenthesized) && !a->children.empty())
                        a = a->child(0);
                    if (a == nullptr || parameters[i].type == nullptr)
                        continue;
                    auto* formal = parameters[i].type->view();
                    bool const anonymous = formal->anonymous_access;
                    auto* designated = anonymous && formal->designated != nullptr ? formal->designated->view() : formal;
                    bool const controls = call->dispatching && controlling != nullptr && designated->base_type()->view() == controlling;
                    if (meaning_of(a) == meaning::Dereference || racw(a->type))
                    {
                        if (controls)
                            allowed.insert(a);
                        else if (racw(a->type) && anonymous && iSpeculation == 0)
                            recover([&]() { error(a, "a remote access-to-class-wide value can be converted to an anonymous access type only as a "
                                "controlling operand of a dispatching call (E.2.2(16/4))"); });
                    }
                }
            }
            for (auto* c : aNode->children)
                calls(c);
        };
        calls(aRoot);
        std::function<void(node*)> dereferences = [&](node* aNode)
        {
            if (meaning_of(aNode) == meaning::Dereference && !allowed.contains(aNode) && !aNode->children.empty() && racw(aNode->child(0)->type))
                recover([&]() { error(aNode, "a remote access-to-class-wide value can be dereferenced only as a controlling operand of a dispatching "
                    "call (E.2.2(16/4))"); });
            for (auto* c : aNode->children)
                dereferences(c);
        };
        dereferences(aRoot);
    }

    void program::categorization_check(unit_info& aUnit)
    {
        if (aUnit.item == nullptr || aUnit.name == "standard")
            return;
        // (the library unit that it is a compilation unit of (a subunit: its ancestor body's))
        unit_info* library = &aUnit;
        while (library != nullptr && library->subunit)
        {
            auto const dot = library->name.rfind('.');
            library = dot != std::string::npos ? find_unit(library->name.substr(0u, dot), true) : nullptr;
        }
        if (library == nullptr)
            return;
        auto const category = unit_categorization(library->name);
        // (the declaration of the library unit (or a library subprogram body without one))
        bool const declaration = !aUnit.body || (!aUnit.subunit && find_unit(aUnit.name, false) == nullptr);
        auto* item = aUnit.subunit ? aUnit.item->child(1) : aUnit.item;
        if (item == nullptr)
            return;
        bool const pure = category.is(categorization::Pure);
        bool const remote = category.is(categorization::RemoteTypes) || category.is(categorization::RemoteCallInterface);

        // (a shared passive or remote types library unit: a package or generic package (E.2(4/3)); All_Calls_Remote: of a remote call
        // interface (E.2.3(16/3)))
        if (declaration && !aUnit.body && !(item->is(construct::Package) || (item->is(construct::Generic) && item->first(construct::Package) != nullptr)))
            for (auto kind : { categorization::SharedPassive, categorization::RemoteTypes })
                if (category.is(kind))
                    recover([&]() { error(category.where[kind], std::string{ "a " } + (kind == categorization::SharedPassive ? "shared passive" :
                        "remote types") + " library unit must be a package or generic package"); });
        if (declaration && category.is(categorization::AllCallsRemote) && !category.is(categorization::RemoteCallInterface))
            recover([&]() { error(category.where[categorization::AllCallsRemote], "pragma All_Calls_Remote applies only to a remote call interface library unit"); });

        // (a categorization aspect's value: a static Boolean expression, not naming the unit's own declarations (13.1.1(32/4)))
        if (declaration && aUnit.ent != nullptr && aUnit.ent->inner != nullptr)
            for (auto* where : category.where)
                if (where != nullptr && where->is(construct::Aspect) && where->size() >= 2u)
                {
                    auto* value = where->children.back();
                    auto* const savedUnit = iCurrentUnit;
                    struct restore { unit_info*& target; unit_info* value; ~restore() { target = value; } } restoreUnit{ iCurrentUnit, savedUnit };
                    iCurrentUnit = &aUnit;
                    recover([&]()
                    {
                        resolve(value, iBoolean, aUnit.ent->inner);
                        std::function<void(node*)> own = [&](node* aName)
                        {
                            if (aName->ent != nullptr && aName->ent->scope != nullptr)
                                for (auto* r = aName->ent->scope; r != nullptr; r = r->parent)
                                    if (r == aUnit.ent->inner)
                                        error(aName, "the value of a library unit aspect cannot depend on the unit's own declarations (13.1.1(32/4))");
                            for (auto* c : aName->children)
                                own(c);
                        };
                        own(value);
                        if (!static_expression(value))
                            error(value, "the value of a library unit aspect must be a static expression (13.1.1(32/4))");
                    });
                }

        // (its semantic dependences (10.2.1(11/3, 17/3), E.2.1(6/3), E.2.2(6/3), E.2.3(8/3)): the units its with clauses mention (and
        // their parents, named in the prefixes), and (a child unit's declaration) its parent)
        auto const dependence = [&](categorization const& aOn, bool aPrivateWith) -> char const*
        {
            if (pure)
                return aOn.is(categorization::Pure) ? nullptr : "a declared pure library unit can depend semantically only on declared pure library units";
            if (category.is(categorization::SharedPassive))
                return aOn.is(categorization::Pure) || aOn.is(categorization::SharedPassive) ? nullptr :
                    "a shared passive library unit can depend semantically only on declared pure or shared passive library units";
            if (category.preelaborated() && !aOn.preelaborated())
                return "a preelaborated library unit can depend semantically only on declared pure or preelaborated library units";
            if (remote && declaration)
            {
                bool const rci = category.is(categorization::RemoteCallInterface);
                if (!(aOn.is(categorization::Pure) || aOn.is(categorization::SharedPassive) || aOn.is(categorization::RemoteTypes) ||
                    (rci && aOn.is(categorization::RemoteCallInterface)) || (aPrivateWith && aOn.preelaborated() && !aOn.categorized())))
                    return rci ? "the declaration of a remote call interface library unit can depend semantically only on declared pure, shared "
                        "passive, remote types and remote call interface library units (and preelaborated ones named only in private with clauses)" :
                        "the declaration of a remote types library unit can depend semantically only on declared pure, shared passive and remote "
                        "types library units (and preelaborated ones named only in private with clauses)";
            }
            return nullptr;
        };
        if (category.preelaborated() || category.categorized())
        {
            // (where a with clause's dependence is an error: the with clause; a declaration's categorization pragma within it that names
            // the unit (the pragma that makes it categorized))
            // (the Preelaborate or Shared_Passive pragma: there; a Pure, Remote_Types or Remote_Call_Interface unit's: its with clause)
            node* categorizedAt = nullptr;
            if (declaration && !aUnit.subunit)
                for (auto* w : { category.where[categorization::Preelaborate], category.where[categorization::SharedPassive] })
                    if (w != nullptr && w->is(construct::Pragma) && w->first(construct::PragmaArgument) != nullptr)
                        for (auto* p = w->parent; p != nullptr && categorizedAt == nullptr; p = p->parent)
                            if (p == item)
                                categorizedAt = w;
            if (auto* context = aUnit.unit->first(construct::Context))
                for (auto* clause : context->children)
                    if (clause->is(construct::With) && !clause->has_flag("limited"))
                        for (auto* name : clause->children)
                            if (!name->is(construct::Flag))
                                for (auto* n = name; n != nullptr; n = n->is(construct::Selected) && n->size() == 2u ? n->child(0) : nullptr)
                                    if (auto const message = dependence(unit_categorization(dotted(n)), clause->has_flag("private")))
                                    {
                                        recover([&]() { error(categorizedAt != nullptr ? categorizedAt : name, message); });
                                        break;
                                    }
            if (declaration && !aUnit.subunit)
                if (auto const dot = aUnit.name.rfind('.'); dot != std::string::npos)
                    if (auto const message = dependence(unit_categorization(aUnit.name.substr(0u, dot)), false))
                    {
                        auto* spec = item->is(construct::Generic) ? item->first(construct::Package) : item;
                        auto* where = spec != nullptr ? spec->first(construct::Defining) : nullptr;
                        if (where == nullptr && spec != nullptr)
                            if (auto* s = specification_of(spec != nullptr ? spec : item))
                                where = s->child(0);
                        recover([&]() { error(where != nullptr ? where : item, message); });
                    }
        }

        // (a public child of a remote call interface: a remote call interface (E.2.3(15)))
        if (declaration && !aUnit.subunit && !category.is(categorization::RemoteCallInterface) && !aUnit.unit->has_flag("private"))
            if (auto const dot = aUnit.name.rfind('.'); dot != std::string::npos &&
                unit_categorization(aUnit.name.substr(0u, dot)).is(categorization::RemoteCallInterface))
            {
                auto* spec = item->is(construct::Generic) ? item->first(construct::Package) : item;
                auto* where = spec != nullptr ? spec->first(construct::Defining) : nullptr;
                if (where == nullptr)
                    if (auto* s2 = specification_of(spec != nullptr ? spec : item))
                        where = s2->child(0);
                recover([&]() { error(where != nullptr ? where : item, "a public child of a remote call interface library unit must be a remote "
                    "call interface library unit (E.2.3(15))"); });
            }

        // (the visible part of the declaration of a remote types or remote call interface library unit (E.2.2(7-9.3/1), E.2.3(10/1-14/3)))
        if (remote && declaration)
            if (auto* package = item->is(construct::Package) ? item : item->is(construct::Generic) ? item->first(construct::Package) : nullptr)
            {
                bool const rci = category.is(categorization::RemoteCallInterface);
                std::string const kind = rci ? "remote call interface" : "remote types";
                // (a remote access type: a named access type declared in the visible part of a remote types or RCI unit)
                auto const remote_access = [&](ada_type* aType)
                {
                    for (auto* t = aType; t != nullptr; t = t->parent != nullptr && t->parent->base_type() != t->base_type() ? t->parent : nullptr)
                    {
                        auto* d = t->base_type()->decl;
                        if (d == nullptr || d->is_private || d->scope == nullptr || d->scope->owner == nullptr || !d->scope->owner->library)
                            continue;
                        auto const& c = unit_categorization(full_name(d->scope->owner));
                        if (c.is(categorization::RemoteTypes) || c.is(categorization::RemoteCallInterface))
                            return true;
                    }
                    return false;
                };
                // (a type that supports external streaming (13.13.2(52/3)))
                std::function<bool(ada_type*, int)> streaming = [&](ada_type* aType, int aDepth) -> bool
                {
                    if (aType == nullptr || aDepth > 32)
                        return true;
                    if (type_aspect(aType, "read") != nullptr && type_aspect(aType, "write") != nullptr)
                        return true;
                    if (limited_type(aType))
                        return false;   // (no available stream attributes)
                    auto* v = aType->view()->base_type()->view();
                    if (v->kind == type_kind::Access || v->kind == type_kind::AccessSubprogram)
                        return !v->anonymous_access && remote_access(aType->view());
                    if (v->kind == type_kind::Array)
                        return streaming(v->component, aDepth + 1);
                    if (v->kind == type_kind::Record && v->unit == nullptr)
                    {
                        if (v->parent != nullptr && v->parent->base_type()->view() != v && !streaming(v->parent, aDepth + 1))
                            return false;
                        for (auto const& c : v->components)
                            if (!c.stored && !streaming(c.type, aDepth + 1))
                                return false;
                    }
                    return true;
                };
                auto const defining_of = [](node* aDecl)
                {
                    auto* d = aDecl->first(construct::Defining);
                    if (d == nullptr)
                        if (auto* spec = specification_of(aDecl))
                            d = spec->child(0);
                    return d != nullptr ? d : aDecl;
                };
                std::set<std::string> inlined;
                std::function<void(node*)> visible = [&](node* aPart)
                {
                    if (aPart == nullptr)
                        return;
                    for (auto* p : aPart->children)
                        if (p->is(construct::Pragma) && p->size() >= 2u && name_text(p->child(0)->source) == "inline")
                            for (auto* a : p->children)
                                if (a->is(construct::PragmaArgument) && !a->children.empty())
                                    inlined.insert(name_text(a->children.back()->source));
                    for (auto* d : aPart->children)
                        switch (d->kind)
                        {
                        case construct::Object:
                            if (d->has_flag("constant"))
                                break;
                            [[fallthrough]];
                        case construct::TaskSingle:
                        case construct::ProtectedSingle:
                            recover([&]() { error(defining_of(d), "a variable cannot be declared in the visible part of a " + kind + " library unit (" +
                                (rci ? "E.2.3(10/1)" : "E.2.2(7)") + ")"); });
                            break;
                        case construct::TaskType:
                        case construct::ProtectedType:
                            if (rci)
                                recover([&]() { error(defining_of(d), "a limited type cannot be declared in the visible part of a remote call interface "
                                    "library unit (E.2.3(11/1))"); });
                            break;
                        case construct::Type:
                        case construct::TypePrivate:
                        case construct::TypePrivateExtension:
                            {
                                auto* defining = d->first(construct::Defining);
                                auto* t = defining != nullptr && defining->ent != nullptr ? defining->ent->type : nullptr;
                                if (t == nullptr)
                                    break;
                                bool const limited = limited_type(t);
                                if (rci && limited)
                                {
                                    recover([&]() { error(defining, "a limited type cannot be declared in the visible part of a remote call interface "
                                        "library unit (E.2.3(11/1))"); });
                                    break;
                                }
                                // (a remote access type: access-to-subprogram, or general designating a class-wide limited private type, limited
                                // interface or private extension (E.2.2(9/3-9.2/3)))
                                if (auto* access = d->first(construct::TypeAccess))
                                {
                                    auto* v = t->view();
                                    auto* designated = v->designated != nullptr ? v->designated->view() : nullptr;
                                    auto* specific = designated != nullptr && designated->kind == type_kind::ClassWide ? designated->specific : nullptr;
                                    bool const ok = (access->has_flag("all") || access->has_flag("constant")) && specific != nullptr &&
                                        ((specific->kind == type_kind::Private && specific->limited) || (specific->base_type()->kind == type_kind::Private &&
                                        specific->base_type()->limited) || (specific->view()->kind == type_kind::Interface && specific->view()->limited));
                                    if (!ok)
                                        recover([&]() { error(defining, "a remote access type must be an access-to-subprogram type or a general access type "
                                            "that designates a class-wide limited private type, limited interface or private extension (E.2.2(9/3))"); });
                                    // (the designated type's primitive subprograms: access parameters only controlling ones, the others' types
                                    // supporting external streaming (E.2.2(14/3)))
                                    else
                                    {
                                        auto* own = specific->view()->base_type()->view();
                                        auto const controlling = [&](ada_type* aParameter)
                                        {
                                            auto* x = aParameter->view();
                                            if (x->anonymous_access && x->designated != nullptr)
                                                x = x->designated->view();
                                            return x->base_type()->view() == own;
                                        };
                                        bool bad = false;
                                        auto& primitives = own->primitives.empty() && specific->base_type()->partial != nullptr ?
                                            specific->base_type()->partial->primitives : own->primitives;
                                        for (auto* op : primitives)
                                            if (op != nullptr && op->sub != nullptr)
                                            {
                                                for (auto const& prm : op->sub->parameters)
                                                    if (prm.type != nullptr && !controlling(prm.type) && (prm.type->anonymous_access || !streaming(prm.type, 0)))
                                                        bad = true;
                                                if (op->sub->result != nullptr && !controlling(op->sub->result) &&
                                                    (op->sub->result_access || !streaming(op->sub->result, 0)))
                                                    bad = true;
                                            }
                                        if (bad)
                                            recover([&]() { error(defining, "the primitive subprograms of the type designated by a remote access-to-class-wide "
                                                "type can have access parameters only if controlling, and their other parameters' types must support "
                                                "external streaming (E.2.2(14/3))"); });
                                    }
                                    break;
                                }
                                // (a type with available stream attributes: its full view supports external streaming (E.2.2(8/2), E.2.3(14/3)))
                                if ((!limited || (type_aspect(t, "read") != nullptr && type_aspect(t, "write") != nullptr)) && !streaming(t, 0))
                                {
                                    auto* full = t->view();
                                    node* where = defining;
                                    if (full != t && full->decl != nullptr && full->decl->decl != nullptr)
                                        where = defining_of(full->decl->decl);
                                    recover([&]() { error(where, "the full view of a type declared in the visible part of a " + kind + " library unit "
                                        "must support external streaming (E.2.2(8/2))"); });
                                }
                                break;
                            }
                        case construct::Generic:
                            if (rci)
                                recover([&]() { error(d, "a generic declaration cannot be in the visible part of a remote call interface library unit "
                                    "(E.2.3(12/1))"); });
                            break;
                        case construct::SubprogramDeclaration:
                            if (rci)
                            {
                                auto* defining = defining_of(d);
                                auto* sub = defining->ent;
                                bool inlineAspect = false;
                                if (auto* aspects = d->first(construct::Aspects))
                                    for (auto* a : aspects->children)
                                        if (a->size() >= 1u && name_text(a->child(0)->source) == "inline")
                                            inlineAspect = true;
                                if (inlineAspect || inlined.contains(name_text(defining->source)))
                                    recover([&]() { error(defining, "a subprogram declared in the visible part of a remote call interface library unit "
                                        "cannot be inlined (E.2.3(13/3))"); });
                                if (sub != nullptr && sub->sub != nullptr)
                                {
                                    bool ok = sub->sub->result == nullptr || (!sub->sub->result_access && streaming(sub->sub->result, 0));
                                    for (auto const& prm : sub->sub->parameters)
                                        if (prm.type == nullptr || prm.type->anonymous_access || !streaming(prm.type, 0) ||
                                            (limited_type(prm.type) && !(type_aspect(prm.type, "read") != nullptr && type_aspect(prm.type, "write") != nullptr)))
                                            ok = false;
                                    if (!ok)
                                        recover([&]() { error(defining, "the parameters and result of a remote subprogram must be of types that support "
                                            "external streaming (E.2.3(14/3))"); });
                                }
                            }
                            break;
                        case construct::Package:
                            visible(d->first(construct::Declarations));
                            break;
                        default:
                            break;
                        }
                };
                visible(package->first(construct::Declarations));
            }

        // (its constructs: preelaborable (a preelaborated unit; the declaration of a remote types or remote call interface unit) and
        // pure (10.2.1(5-10.4/2, 15.1/5-15.5/5), E.2.2(5), E.2.3(8/3)))
        bool const preelaborable = category.preelaborated() || (remote && declaration);
        if (!preelaborable)
            return;
        // (the place an error is reported: within an instance, its instantiation)
        node* redirect = nullptr;
        auto const at = [&](node* aWhere) { return redirect != nullptr ? redirect : aWhere; };
        // (the packages whose private parts (or bodies) the construct being checked is in: the full views of their private types are
        // visible (and of their ancestors'))
        std::vector<entity*> privateParts;
        region* currentRegion = nullptr;    // (the region of the declaration being checked, if known)
        auto* declarationUnit = find_unit(library->name, false);
        bool const privateChild = declarationUnit != nullptr && declarationUnit->unit != nullptr && declarationUnit->unit->has_flag("private");
        auto const full_view_visible = [&](ada_type* aPartial)
        {
            if (privateChild)
                return true;
            auto* d = aPartial->decl;
            auto* package = d != nullptr && d->scope != nullptr ? d->scope->owner : nullptr;
            if (package == nullptr)
                return false;
            for (auto* p : privateParts)
                for (auto* a = p; a != nullptr; a = a->scope != nullptr ? a->scope->owner : nullptr)
                    if (a == package)
                        return true;
            // (within the body of the package (or of a descendant), by the regions of the declaration being checked)
            for (auto* r = currentRegion; r != nullptr; r = r->parent)
                if (r->package_body && r->owner != nullptr)
                    for (auto* a = r->owner; a != nullptr; a = a->scope != nullptr ? a->scope->owner : nullptr)
                        if (a == package)
                            return true;
            return false;
        };
        // (a static function (4.9(18-22)), or another function whose call is preelaborable (10.2.1(7.1/5-7.4/5)))
        std::function<bool(entity*)> preelaborable_function = [&](entity* aFunction) -> bool
        {
            if (aFunction == nullptr)
                return true;
            if (aFunction->kind == entity_kind::EnumerationLiteral || aFunction->kind == entity_kind::Attribute)
                return true;
            if (aFunction->sub == nullptr)
                return false;
            auto const op = aFunction->sub->op;
            if (op != intrinsic::None && op != intrinsic::Import && op != intrinsic::NotEqual && op != intrinsic::UncheckedDeallocation)
                return true;
            if (aFunction->sub->renames != nullptr && aFunction->sub->renames != aFunction)
                return preelaborable_function(aFunction->sub->renames);
            if (aFunction->sub->parent_operation != nullptr && aFunction->sub->inherited)
                return preelaborable_function(aFunction->sub->parent_operation);
            auto* owner = aFunction->scope != nullptr ? aFunction->scope->owner : nullptr;
            if (owner != nullptr)
            {
                auto const name = full_name(owner);
                if (name == "system.storage_elements")
                    return true;
                if (owner->generic != nullptr && full_name(owner->generic) == "system.address_to_access_conversions" &&
                    (aFunction->name == "to_pointer" || aFunction->name == "to_address"))
                    return true;
            }
            if (aFunction->generic != nullptr && (full_name(aFunction->generic) == "ada.unchecked_conversion" ||
                full_name(aFunction->generic) == "unchecked_conversion"))
                return true;
            return false;
        };
        // (a descendant of Ada.Finalization.Controlled or Limited_Controlled)
        auto const controlled_type = [&](ada_type* aType)
        {
            auto* root = aType->view()->base_type()->view();
            if (!root->tagged)
                return false;
            for (int i = 0; i < 64 && root->parent != nullptr && root->parent->base_type()->view() != root; ++i)
                root = root->parent->base_type()->view();
            auto* decl = root->decl != nullptr ? root->decl : root->partial != nullptr ? root->partial->decl : nullptr;
            if (decl == nullptr)
                return false;
            auto const rootName = full_name(decl);
            return rootName == "ada.finalization.controlled" || rootName == "ada.finalization.limited_controlled";
        };
        // (an expression (or name) evaluated by the elaboration: preelaborable (10.2.1(7/5-9/3)); declared pure, no allocator of an
        // access-to-variable type (10.2.1(15.3/2)); its violations reported (aReport), or the first returned)
        std::function<bool(ada_type*, int)> preelaborable_initialization;
        std::function<bool(node*, bool, bool)> expression = [&](node* aNode, bool aPrimary, bool aReport) -> bool
        {
            if (aNode == nullptr)
                return true;
            auto const violation = [&](node* aWhere, std::string const& aMessage)
            {
                if (aReport)
                    recover([&]() { error(at(aWhere), aMessage); });
                return false;
            };
            auto const children = [&](std::size_t aFrom, bool aChildPrimary)
            {
                bool ok = true;
                for (std::size_t i = aFrom; i < aNode->size(); ++i)
                    if (!aNode->child(i)->is(construct::Flag) && !expression(aNode->child(i), aChildPrimary, aReport))
                    {
                        ok = false;
                        if (!aReport)
                            break;
                    }
                return ok;
            };
            switch (aNode->kind)
            {
            case construct::Flag:
            case construct::Word:
            case construct::Number:
            case construct::Character:
            case construct::Box:
            case construct::Others:
            case construct::Aspects:
                return true;
            default:
                break;
            }
            auto const m = meaning_of(aNode);
            if (m == meaning::Type)
                // (a subtype mark; a subtype indication's constraint is evaluated)
                return aNode->is(construct::Apply) ? children(1u, true) : true;
            if (m == meaning::Package || m == meaning::Exception || m == meaning::Unit || m == meaning::Subprogram || m == meaning::Entry ||
                m == meaning::Literal || m == meaning::StringLiteral)
                return true;
            if (m == meaning::Call)
            {
                auto const* call = call_of(aNode);
                bool ok = true;
                if (call != nullptr && call->callee != nullptr && !preelaborable_function(call->callee))
                    ok = violation(aNode, "a call of a function that is not a static function is not preelaborable (10.2.1(7/5))");
                if (!ok && !aReport)
                    return false;
                if (call != nullptr)
                {
                    for (auto* a : call->actuals)
                        if (a != nullptr && !expression(a, true, aReport))
                        {
                            ok = false;
                            if (!aReport)
                                return false;
                        }
                    return ok;
                }
                return children(0u, true) && ok;
            }
            if (m == meaning::Object || m == meaning::Index || m == meaning::Slice || m == meaning::Component || m == meaning::Dereference)
            {
                if (!aPrimary)
                    return children(0u, false);
                // (a name of an object: a static expression, or a discriminant of an enclosing type)
                if (aNode->ent != nullptr && aNode->ent->kind == entity_kind::Discriminant)
                    return true;
                if (static_expression(aNode))
                    return true;
                return violation(aNode, "a name of an object that is not a static expression is not preelaborable (10.2.1(8))");
            }
            switch (aNode->kind)
            {
            case construct::Attribute:
                {
                    // (its prefix is not a primary (e.g. X'Access, A'First); its arguments are)
                    bool ok = expression(aNode->child(0), false, aReport);
                    if (auto* arguments = aNode->first(construct::Arguments))
                        for (auto* a : arguments->children)
                            ok = expression(a, true, aReport) && ok;
                    return ok;
                }
            case construct::Allocator:
                {
                    bool ok = true;
                    auto* type = aNode->type != nullptr ? aNode->type->view() : nullptr;
                    if (pure && type != nullptr && type->kind == type_kind::Access && !type->access_constant)
                        ok = violation(aNode, "an allocator of an access-to-variable type is not allowed in a declared pure library unit (10.2.1(15.3/2))");
                    // (an uninitialized allocator: the object's default initialization)
                    auto* designated = aNode->child(0);
                    bool const initialized = designated != nullptr && (designated->is(construct::Qualified) ||
                        (designated->is(construct::SubtypeIndication) && designated->first(construct::Qualified) != nullptr));
                    if (!initialized && type != nullptr && type->designated != nullptr && !preelaborable_initialization(type->designated, 0))
                        ok = violation(aNode, "an allocator that creates an object initialized by default, of a type that does not have preelaborable "
                            "initialization, is not preelaborable (10.2.1(9/3))") && ok;
                    if (!ok && !aReport)
                        return false;
                    return children(0u, true) && ok;
                }
            case construct::AggregateExtension:
                {
                    auto* ancestor = aNode->child(0);
                    if (ancestor != nullptr && meaning_of(ancestor) == meaning::Type && ancestor->type != nullptr &&
                        !preelaborable_initialization(ancestor->type, 0))
                    {
                        if (!violation(aNode, "an extension aggregate whose ancestor is a subtype of a type that does not have preelaborable "
                            "initialization is not preelaborable (10.2.1(9/3))") && !aReport)
                            return false;
                    }
                    // (a controlled ancestor: its Initialize is called (7.6(12)), a call of a procedure (10.2.1(7/5)))
                    else if (ancestor != nullptr && meaning_of(ancestor) == meaning::Type && ancestor->type != nullptr && controlled_type(ancestor->type))
                        if (!violation(aNode, "an extension aggregate whose ancestor is a subtype of a controlled type (its Initialize is called) is "
                            "not preelaborable (10.2.1(7/5), 7.6(12))") && !aReport)
                            return false;
                    return children(0u, true);
                }
            case construct::Qualified:
                return children(1u, true);
            case construct::Selected:
                return true;
            case construct::Apply:
                if (m == meaning::Conversion)
                    return children(1u, true);
                return children(0u, true);
            default:
                return children(0u, true);
            }
        };
        // (a task or protected type (or single object's type): its unit has entries)
        auto const has_entries = [&](ada_type* aType)
        {
            auto* unit = aType->unit;
            if (unit == nullptr || unit->kind == entity_kind::Task)
                return true;
            if (unit->inner != nullptr)
                for (auto* d : unit->inner->declarations)
                    if (d->kind == entity_kind::Entry)
                        return true;
            return false;
        };
        // (a type that has preelaborable initialization (10.2.1(11.1/2-11.5/2)): its view where the construct is)
        preelaborable_initialization = [&](ada_type* aType, int aDepth) -> bool
        {
            if (aType == nullptr || aDepth > 32)
                return true;
            if (aType->preelaborable_initialization || aType->base_type()->preelaborable_initialization ||
                (aType->partial != nullptr && aType->partial->preelaborable_initialization) ||
                (aType->base_type()->partial != nullptr && aType->base_type()->partial->preelaborable_initialization))
                return true;
            auto* t = aType;
            // (a partial view (or a type derived from one) where the full view is not visible: only by the pragma)
            if (t->derived_partial != nullptr && !full_view_visible(t->derived_partial))
                return preelaborable_initialization(t->derived_partial, aDepth + 1);
            if (t->kind == type_kind::Private || (t->base_type()->kind == type_kind::Private))
            {
                auto* base = t->base_type();
                if (base->is_formal || t->is_formal)
                    return false;
                if (base->full == nullptr || !full_view_visible(base))
                    return false;
                return preelaborable_initialization(base->full, aDepth + 1);
            }
            auto* v = t->view();
            auto* b = v->base_type()->view();
            // (a formal private or derived type (presumed, 10.2.1(10.1/3)))
            if ((v->is_formal || b->is_formal) && (b->tagged || b->kind == type_kind::Record || b->kind == type_kind::Private))
                return false;
            switch (b->kind)
            {
            case type_kind::Array:
                return preelaborable_initialization(b->component != nullptr ? b->component : v->component, aDepth + 1);
            case type_kind::Task:
                return false;
            case type_kind::Protected:
                // (a protected type without entries (by 11.2/3, only by the pragma; as in Ada 95 (and the tests), its components'))
                return !has_entries(b);
            case type_kind::Record:
                {
                    if (b->unit != nullptr)
                        return b->unit->kind == entity_kind::Protected && !has_entries(b);    // (a single task or protected object's type)
                    if (b->parent != nullptr && b->parent->base_type() != b && !preelaborable_initialization(b->parent, aDepth + 1))
                        return false;
                    // (a controlled type whose Initialize is not a null procedure (11.4/3))
                    if (controlled_type(b))
                        for (auto* p : b->primitives)
                            if (p->name == "initialize" && p->sub != nullptr && !p->sub->is_null && !p->sub->inherited &&
                                p->sub->parameters.size() == 1u && !p->sub->is_abstract)
                                return false;
                    for (auto const& c : b->components)
                    {
                        if (c.stored)
                            continue;
                        if (c.default_expression != nullptr)
                        {
                            if (!expression(c.default_expression, true, false))
                                return false;
                        }
                        else if (!preelaborable_initialization(c.type, aDepth + 1))
                            return false;
                    }
                    return true;
                }
            default:
                return true;
            }
        };
        // (a type with a part of a task type or a protected type with entries)
        std::function<bool(ada_type*, int)> task_or_entries_part = [&](ada_type* aType, int aDepth) -> bool
        {
            if (aType == nullptr || aDepth > 32)
                return false;
            auto* b = aType->view()->base_type()->view();
            switch (b->kind)
            {
            case type_kind::Task:
                return true;
            case type_kind::Protected:
                return has_entries(b);
            case type_kind::Array:
                return task_or_entries_part(b->component, aDepth + 1);
            case type_kind::Record:
                if (b->unit != nullptr)
                    return b->unit->kind == entity_kind::Task || has_entries(b);
                if (b->parent != nullptr && b->parent->base_type()->view() != b && task_or_entries_part(b->parent, aDepth + 1))
                    return true;
                for (auto const& c : b->components)
                    if (!c.stored && task_or_entries_part(c.type, aDepth + 1))
                        return true;
                return false;
            default:
                return false;
            }
        };
        // (a subtype indication's (or a type definition's) constraints: evaluated by the elaboration)
        std::function<void(node*)> constraints = [&](node* aNode)
        {
            if (aNode == nullptr)
                return;
            switch (aNode->kind)
            {
            case construct::SubtypeIndication:
                for (std::size_t i = 0u; i < aNode->size(); ++i)
                    if (i > 0u || !(aNode->child(i)->is(construct::Identifier) || aNode->child(i)->is(construct::Selected)))
                        expression(aNode->child(i), true, true);
                return;
            case construct::Range:
            case construct::Attribute:
                expression(aNode, true, true);
                return;
            case construct::Discriminants:
            case construct::Aspects:
            case construct::TypeAccessSubprogram:
            case construct::Component:
                if (aNode->is(construct::Component))
                    if (auto* d = aNode->first(construct::ComponentDefinition))
                        constraints(d);
                return;
            default:
                for (auto* c : aNode->children)
                    constraints(c);
            }
        };
        // (a Storage_Size specified for an access type (aspect, or attribute definition clause in the declarative part): static zero)
        auto const storage_size = [&](node* aType, std::string const& aName) -> int
        {
            // (-1: none; 0: static zero; 1: another)
            node* value = nullptr;
            if (auto* list = aType->first(construct::Aspects))
                for (auto* a : list->children)
                    if (a->size() >= 2u && name_text(a->child(0)->source) == "storage_size")
                        value = a->children.back();
            if (value == nullptr && aType->parent != nullptr)
                for (auto* c : aType->parent->children)
                    if (c->is(construct::RepresentationAttribute) && c->size() >= 2u && c->child(0)->is(construct::Attribute) &&
                        c->child(0)->size() >= 2u && name_text(c->child(0)->child(1)->source) == "storage_size" &&
                        dotted(c->child(0)->child(0)) == aName)
                        value = c->child(1);
            if (value == nullptr)
                return -1;
            auto const v = evaluate(value, nullptr);
            return v.is_integer() && v.integer() == bigint{ 0 } ? 0 : 1;
        };
        // (the declarative items elaborated (not those of subprogram, task, protected and entry bodies, nor generic declarations))
        std::function<void(node*)> declarations;
        std::function<void(node*)> item_check = [&](node* aItem)
        {
            switch (aItem->kind)
            {
            case construct::Object:
                {
                    bool const constant = aItem->has_flag("constant");
                    auto* defining = aItem->first(construct::Defining);
                    auto* object = defining != nullptr && defining->ent != nullptr ? defining->ent : aItem->ent;
                    currentRegion = object != nullptr ? object->scope : nullptr;
                    if (pure && !constant)
                        recover([&]() { error(at(defining != nullptr ? defining : aItem), "a variable cannot be declared in a declared pure library unit "
                            "(10.2.1(15.2/2))"); });
                    node* type = nullptr;
                    node* initial = nullptr;
                    for (auto* c : aItem->children)
                    {
                        if (c->is(construct::Defining) || c->is(construct::Flag) || c->is(construct::Aspects))
                            continue;
                        if (type == nullptr)
                            type = c;
                        else if (initial == nullptr)
                            initial = c;
                    }
                    constraints(type);
                    if (initial != nullptr)
                        expression(initial, true, true);
                    else if (!constant && object != nullptr && !object->imported && object->address_clause == nullptr &&
                        !preelaborable_initialization(object->type, 0))
                        recover([&]() { error(at(defining != nullptr ? defining : aItem), "the default initialization of an object of a type that does "
                            "not have preelaborable initialization is not preelaborable (10.2.1(9/3))"); });
                    break;
                }
            case construct::TaskSingle:
            case construct::ProtectedSingle:
                {
                    auto* defining = aItem->first(construct::Defining);
                    if (pure)
                        recover([&]() { error(at(defining != nullptr ? defining : aItem), "a variable cannot be declared in a declared pure library unit "
                            "(10.2.1(15.2/2))"); });
                    auto* object = defining != nullptr && defining->ent != nullptr ? defining->ent : aItem->ent;
                    if (aItem->is(construct::TaskSingle) || object == nullptr || !preelaborable_initialization(object->type, 0))
                        recover([&]() { error(at(defining != nullptr ? defining : aItem), std::string{ "the creation of a " } + (aItem->is(construct::TaskSingle) ?
                            "task object" : "protected object with entries") + " is not preelaborable (10.2.1(9/3))"); });
                    break;
                }
            case construct::Type:
                {
                    for (auto* c : aItem->children)
                        if (!c->is(construct::Defining))
                            constraints(c);
                    // (shared passive: no library level declaration of an access type designating a class-wide type, of a type with a part of
                    // a task type or a protected type with entries (E.2.1(7.1/5-7.3/5)))
                    if (category.is(categorization::SharedPassive))
                        if (auto* defining = aItem->first(construct::Defining); defining != nullptr && defining->ent != nullptr && defining->ent->type != nullptr)
                        {
                            auto* t = defining->ent->type->view();
                            auto* designated = t->kind == type_kind::Access && t->designated != nullptr ? t->designated->view() : nullptr;
                            if (designated != nullptr && designated->kind == type_kind::ClassWide)
                                recover([&]() { error(at(defining), "a shared passive library unit cannot declare an access type that designates a class-wide "
                                    "type (E.2.1(7.1/5))"); });
                            else if (designated != nullptr ? task_or_entries_part(designated, 0) : task_or_entries_part(t, 0))
                                recover([&]() { error(at(defining), std::string{ "a shared passive library unit cannot declare " } + (designated != nullptr ?
                                    "an access type that designates a type" : "a type") + " with a part of a task type or a protected type with entries "
                                    "(E.2.1(7.2/5, 7.3/5))"); });
                        }
                    // (declared pure: a named access-to-variable type whose Storage_Size is not static zero, an access-to-constant one whose
                    // Storage_Size is specified otherwise (10.2.1(15.4/3, 15.5/5)))
                    if (pure)
                        if (auto* access = aItem->first(construct::TypeAccess))
                        {
                            auto* defining = aItem->first(construct::Defining);
                            auto const size = storage_size(aItem, defining != nullptr ? name_text(defining->source) : std::string{});
                            if (!access->has_flag("constant") ? size != 0 : size == 1)
                                recover([&]() { error(at(defining != nullptr ? defining : aItem), std::string{ "a named access-to-" } +
                                    (access->has_flag("constant") ? "constant" : "variable") + " type is not allowed in a declared pure library unit "
                                    "unless its Storage_Size is zero (10.2.1(15.4/3, 15.5/5))"); });
                        }
                    break;
                }
            case construct::Subtype:
                for (auto* c : aItem->children)
                    if (!c->is(construct::Defining))
                        constraints(c);
                break;
            case construct::RenamingObject:
                for (auto* c : aItem->children)
                    if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::SubtypeIndication) &&
                        !c->is(construct::AccessDefinition) && !c->is(construct::Aspects))
                        expression(c, false, true);
                break;
            case construct::Instantiation:
                {
                    // (an instance: its declaration and body elaborated (10.2.1(9.1/5)); errors reported at the instantiation)
                    auto c = iInstanceCopies.find(aItem);
                    if (c == iInstanceCopies.end())
                        break;
                    auto* const saved = redirect;
                    if (redirect == nullptr)
                        redirect = aItem;
                    if (auto* spec = c->second.first)
                        for (auto* d : spec->children)
                            if (d->is(construct::Package))
                                item_check(d);
                    if (auto* body = c->second.second; body != nullptr && body->is(construct::PackageBody))
                        item_check(body);
                    redirect = saved;
                    break;
                }
            case construct::Package:
                {
                    declarations(aItem->first(construct::Declarations));
                    if (auto* p = aItem->first(construct::Private))
                    {
                        auto* defining = aItem->first(construct::Defining);
                        if (defining != nullptr && defining->ent != nullptr)
                            privateParts.push_back(defining->ent);
                        declarations(p);
                        if (defining != nullptr && defining->ent != nullptr)
                            privateParts.pop_back();
                    }
                    break;
                }
            case construct::PackageBody:
                {
                    // (a generic's body: its copy analysed by check_generic)
                    if (auto t = iTemplateBodies.find(aItem); t != iTemplateBodies.end() && t->second != nullptr)
                        aItem = t->second;
                    auto* defining = aItem->first(construct::Defining);
                    entity* package = defining != nullptr ? defining->ent : nullptr;
                    if (package != nullptr)
                        privateParts.push_back(package);
                    declarations(aItem->first(construct::Declarations));
                    // (its statements (10.2.1(6)))
                    if (auto* handled = aItem->first(construct::Handled))
                        if (auto* statements = handled->first(construct::Statements))
                            for (auto* s : statements->children)
                                if (!s->is(construct::NullStatement) && !s->is(construct::Pragma) && !s->is(construct::Label))
                                    recover([&]() { error(at(s), "a statement other than a null statement is not preelaborable (10.2.1(6))"); });
                    if (package != nullptr)
                        privateParts.pop_back();
                    break;
                }
            default:
                break;
            }
        };
        declarations = [&](node* aPart)
        {
            if (aPart != nullptr)
                for (auto* d : aPart->children)
                    item_check(d);
        };
        // (the unit's own declarative items)
        if (aUnit.body && !declaration)
        {
            // (a body (or subunit) of a package: within it, its (and its ancestors') private parts are visible)
            for (auto p = library->name;;)
            {
                if (auto* u = find_unit(p, false); u != nullptr && u->ent != nullptr)
                    privateParts.push_back(u->ent);
                auto const dot = p.rfind('.');
                if (dot == std::string::npos)
                    break;
                p = p.substr(0u, dot);
            }
        }
        if (item->is(construct::Package))
        {
            declarations(item->first(construct::Declarations));
            if (auto* p = item->first(construct::Private))
            {
                // (the private part: its own and its ancestors' private parts are visible)
                auto const count = privateParts.size();
                for (auto p2 = aUnit.name;;)
                {
                    if (auto* u = find_unit(p2, false); u != nullptr && u->ent != nullptr)
                        privateParts.push_back(u->ent);
                    auto const dot = p2.rfind('.');
                    if (dot == std::string::npos)
                        break;
                    p2 = p2.substr(0u, dot);
                }
                declarations(p);
                privateParts.resize(count);
            }
        }
        else if (item->is(construct::PackageBody) || item->is(construct::Instantiation))
            item_check(item);
    }
}
