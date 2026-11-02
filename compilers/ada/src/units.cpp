/*
  units.cpp

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

// Ada: the program's units (their order, package Standard and context clauses) and the model's helpers

#include <neolib/neolib.hpp>
#include <cstdio>
#include <cstdlib>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <functional>
#include "program.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        // the name of a defining program unit name (or operator symbol) node, e.g. "ada.text_io"
        std::string defining_name(node* aNode)
        {
            if (aNode == nullptr)
                return {};
            return name_text(aNode->source);
        }

        // the name of a name (an identifier or selected components), e.g. "ada.text_io"
        std::string dotted_name(node* aName)
        {
            if (aName == nullptr)
                return {};
            if (aName->is(construct::Identifier))
                return name_text(aName->source);
            if (aName->is(construct::Selected) && aName->size() == 2u)
                return dotted_name(aName->child(0)) + "." + name_text(aName->child(1)->source);
            return name_text(aName->source);
        }

        node* specification_of(node* aItem)
        {
            for (auto* c : aItem->children)
                if (c->is(construct::ProcedureSpecification) || c->is(construct::FunctionSpecification))
                    return c;
            return nullptr;
        }
    }

    program::program(compiler& aCompiler, std::vector<node*> const& aCompilations) :
        iCompiler{ aCompiler }, iCompilations{ aCompilations }
    {
    }

    program::~program() = default;

    void program::error(node const* aNode, std::string const& aMessage)
    {
        throw compile_error{ aNode, aMessage };
    }

    entity* program::new_entity(entity_kind aKind, std::string const& aName, node* aDecl, region* aScope)
    {
        auto& e = iEntities.emplace_back();
        e.sequence = iEntities.size();
        e.kind = aKind;
        e.name = aName;
        e.decl = aDecl;
        e.scope = aScope;
        if (aKind == entity_kind::Procedure || aKind == entity_kind::Function || aKind == entity_kind::Entry || 
            aKind == entity_kind::GenericProcedure || aKind == entity_kind::GenericFunction || aKind == entity_kind::Attribute)
            e.sub = std::make_unique<subprogram_info>();
        return &e;
    }

    ada_type* program::new_type(type_kind aKind, std::string const& aName)
    {
        auto& t = iTypes.emplace_back();
        t.kind = aKind;
        t.name = aName;
        t.base = &t;
        t.root = &t;
        t.id = iNextTypeId++;
        return &t;
    }

    ada_type* program::new_subtype(ada_type* aOf, std::string const& aName)
    {
        auto& t = iTypes.emplace_back(*aOf);
        t.name = aName;
        t.is_subtype = true;
        t.first_subtype = false;
        t.base = aOf->base_type();
        t.decl = nullptr;
        t.id = iNextTypeId++;
        return &t;
    }

    region* program::new_region(entity* aOwner, region* aParent)
    {
        auto& r = iRegions.emplace_back();
        r.owner = aOwner;
        r.parent = aParent;
        r.level = aParent != nullptr ? aParent->level : 0;
        if (aOwner != nullptr)
            aOwner->inner = &r;
        return &r;
    }

    // --- units

    void program::collect_units()
    {
        bool first = true;
        for (std::size_t c = 0u; c < iCompilations.size(); ++c)
        {
            auto* compilation = iCompilations[c];
            // (the main file: the first compilation that is not the prelude (Standard))
            bool mainFile = false;
            for (auto* unit : compilation->children)
            {
                if (!unit->is(construct::Unit))
                    continue;
                unit_info info;
                info.unit = unit;
                for (auto* child : unit->children)
                    if (!child->is(construct::Context) && !child->is(construct::Flag))
                        info.item = child;
                if (info.item == nullptr)
                    continue;
                auto* item = info.item;
                switch (item->kind)
                {
                case construct::Package:
                    info.name = defining_name(item->first(construct::Defining));
                    break;
                case construct::PackageBody:
                    info.name = defining_name(item->first(construct::Defining));
                    info.body = true;
                    break;
                case construct::SubprogramBody:
                    if (auto* spec = specification_of(item))
                        info.name = defining_name(spec->child(0));
                    info.body = true;
                    break;
                case construct::SubprogramDeclaration:
                    if (auto* spec = specification_of(item))
                        info.name = defining_name(spec->child(0));
                    break;
                case construct::Generic:
                    for (auto* g : item->children)
                        if (g->is(construct::Package))
                            info.name = defining_name(g->first(construct::Defining));
                        else if (g->is(construct::ProcedureSpecification) || g->is(construct::FunctionSpecification))
                            info.name = defining_name(g->child(0));
                    break;
                case construct::Instantiation:
                case construct::RenamingPackage:
                case construct::RenamingSubprogram:
                case construct::RenamingGeneric:
                    if (auto* spec = specification_of(item))
                        info.name = defining_name(spec->child(0));
                    else if (auto* d = item->first(construct::Defining))
                        info.name = defining_name(d);
                    else if (auto* o = item->first(construct::DefiningOperator))
                        info.name = defining_name(o);
                    break;
                case construct::Subunit:
                    {
                        info.subunit = true;
                        info.body = true;
                        auto const parent = dotted_name(item->child(0));
                        auto* proper = item->child(1);
                        std::string own;
                        if (proper != nullptr)
                        {
                            if (auto* spec = specification_of(proper))
                                own = defining_name(spec->child(0));
                            else if (auto* d = proper->first(construct::Defining))
                                own = defining_name(d);
                        }
                        info.name = parent + "." + own;
                        info.withs.push_back(parent);
                        break;
                    }
                default:
                    continue;
                }
                if (auto* context = unit->first(construct::Context))
                    for (auto* clause : context->children)
                        if (clause->is(construct::With))
                        {
                            for (auto* name : clause->children)
                                if (!name->is(construct::Flag))
                                    (clause->has_flag("limited") ? info.limited_withs : info.withs).push_back(dotted_name(name));
                        }
                        else if (clause->is(construct::Pragma) && clause->size() > 0u)
                        {
                            auto const pragmaName = name_text(clause->child(0)->source);
                            if (pragmaName == "normalize_scalars" || pragmaName == "initialize_scalars")
                                iNormalizeScalars = true;
                            if (pragmaName == "elaborate" || pragmaName == "elaborate_all")
                                for (auto* a : clause->children)
                                    if (a->is(construct::PragmaArgument) && !a->children.empty())
                                        info.elaborates.push_back(dotted_name(a->children.back()));
                        }
                if (c > 0u && first)
                    mainFile = true;
                info.main_file = mainFile;
                iUnits.push_back(info);
            }
            if (mainFile)
                first = false;
        }
    }

    unit_info* program::find_unit(std::string const& aName, bool aBody)
    {
        if (std::getenv("NEOS_ADA_DEBUG"))
        {
            std::fprintf(stderr, "find_unit %s %d among:", aName.c_str(), aBody);
            for (auto& u : iUnits)
                std::fprintf(stderr, " %s%s", u.name.c_str(), u.body ? "(b)" : "");
            std::fprintf(stderr, "\n");
        }
        // (the last one found: a unit compiled later replaces one compiled earlier (e.g. the main file's units are first))
        unit_info* result = nullptr;
        for (auto& u : iUnits)
            if (u.name == aName && u.body == aBody && (aBody || !u.subunit))
            {
                if (result == nullptr || (u.main_file && !result->main_file))
                    result = &u;
                if (result->main_file)
                    break;
            }
        return result;
    }

    void program::order_units()
    {
        // (each unit follows the units it depends on: the specifications it names, its parent's specification, its own
        // specification (a body) and (where there is no cycle) the bodies of the units it names, so that they are elaborated first)
        std::map<unit_info*, int> state;
        std::function<void(unit_info*)> visit = [&](unit_info* aUnit)
        {
            if (aUnit == nullptr || state[aUnit] != 0)
                return;
            state[aUnit] = 1;
            auto const dependency = [&](std::string const& aName)
            {
                // (a unit and its parents)
                for (auto dot = aName.find('.'); dot != std::string::npos; dot = aName.find('.', dot + 1u))
                    visit(find_unit(aName.substr(0u, dot), false));
                if (auto* spec = find_unit(aName, false))
                    visit(spec);
                else if (auto* body = find_unit(aName, true))
                    visit(body);
            };
            if (aUnit->name != "standard")
                visit(find_unit("standard", false));
            auto const dot = aUnit->name.rfind('.');
            if (dot != std::string::npos && !aUnit->subunit)
                dependency(aUnit->name.substr(0u, dot));
            if (aUnit->body && !aUnit->subunit)
                visit(find_unit(aUnit->name, false));
            for (auto const& w : aUnit->withs)
                dependency(w);
            // (a body: its subunits' withs too (they are elaborated before it, 10.2(9)))
            if (aUnit->body && !aUnit->subunit)
                for (auto& u : iUnits)
                    if (u.subunit && u.name.size() > aUnit->name.size() && u.name.compare(0u, aUnit->name.size() + 1u, aUnit->name + ".") == 0)
                        for (auto const& w : u.withs)
                            if (w != aUnit->name && w.compare(0u, aUnit->name.size(), aUnit->name) != 0)
                                dependency(w);
            iOrder.push_back(aUnit);
            state[aUnit] = 2;
            // (a specification's body follows it, unless it is being visited)
            // (a library subprogram's body: not followed (it has nothing to elaborate that others need, its calls aren't checked); a
            // body it withs (e.g. a child of a unit whose body is being visited) waits until its turn)
            if (!aUnit->body && !aUnit->item->is(construct::SubprogramDeclaration))
                if (auto* body = find_unit(aUnit->name, true); body != nullptr && state[body] == 0)
                {
                    // (not yet if a unit whose body its pragma Elaborate requires to be elaborated first is being visited (the body
                    // then follows, see below))
                    bool deferred = false;
                    for (auto const& e : body->elaborates)
                        if (auto* eb = find_unit(e, true); eb != nullptr && state[eb] == 1)
                            deferred = true;
                    if (!deferred)
                    {
                        // (its withs' bodies first, where possible; those its pragmas Elaborate name)
                        for (auto const& w : body->withs)
                            dependency(w);
                        for (auto const& e : body->elaborates)
                        {
                            dependency(e);
                            visit(find_unit(e, true));
                        }
                        visit(body);
                    }
                }
        };
        visit(find_unit("standard", false));
        for (auto& u : iUnits)
            if (u.main_file)
                visit(&u);
        for (auto& u : iUnits)
            visit(&u);
        // (subunits follow their parents' bodies)
        std::stable_partition(iOrder.begin(), iOrder.end(), [](unit_info* u) { return !u->subunit; });
    }

    void program::setup_standard()
    {
        iLibrary = new_region(nullptr, nullptr);
        iStandardPackage = new_entity(entity_kind::Package, "standard", nullptr, iLibrary);
        iStandard = new_region(iStandardPackage, iLibrary);
        iStandard->is_package = true;
        iLibrary->add(iStandardPackage);
        iLibraryUnits["standard"] = iStandardPackage;
        iVoid = new_type(type_kind::Void, "");
        iUniversalInteger = new_type(type_kind::UniversalInteger, "universal_integer");
        iUniversalReal = new_type(type_kind::UniversalReal, "universal_real");
        iUniversalFixed = new_type(type_kind::UniversalFixed, "universal_fixed");
        iUniversalAccess = new_type(type_kind::UniversalAccess, "universal_access");
        iStringLiteral = new_type(type_kind::String, "string literal");
        iCharacterLiteral = new_type(type_kind::CharacterLiteral, "character literal");
        iAggregateType = new_type(type_kind::Aggregate, "aggregate");
        iBracketAggregateType = new_type(type_kind::Aggregate, "aggregate");
        iRootInteger = new_type(type_kind::Integer, "root_integer");
        iRootInteger->low = static_value{ bigint{ std::numeric_limits<std::int64_t>::min() } };
        iRootInteger->high = static_value{ bigint{ std::numeric_limits<std::int64_t>::max() } };
        iRootInteger->size = 8u;
        iRootReal = new_type(type_kind::Float, "root_real");
        iRootReal->digits = 15;
        iRootReal->size = 8u;
        // (the character types)
        auto const character_type = [&](std::string const& aName, std::int64_t aLast, std::uint64_t aSize)
        {
            auto* t = new_type(type_kind::Enumeration, aName);
            t->character_type = true;
            t->standard_character = true;
            t->first_subtype = true;
            t->low = static_value{ bigint{ 0 } };
            t->high = static_value{ bigint{ aLast } };
            t->size = aSize;
            t->alignment = aSize;
            auto* e = new_entity(entity_kind::Type, aName, nullptr, iStandard);
            e->type = t;
            t->decl = e;
            t->declared_in = iStandard;
            iStandard->add(e);
            return t;
        };
        iCharacter = character_type("character", 255, 1u);
        iWideCharacter = character_type("wide_character", 65535, 2u);
        iWideWideCharacter = character_type("wide_wide_character", 0x7FFFFFFF, 4u);
    }

    entity* program::limited_view(std::string const& aName)
    {
        if (auto v = iLimitedViews.find(aName); v != iLimitedViews.end())
            return v->second;
        auto* u = find_unit(aName, false);
        if (u == nullptr || u->item == nullptr || !u->item->is(construct::Package))
            return nullptr;
        auto* view = new_entity(entity_kind::Package, aName, nullptr, iStandard);
        view->limited_view = true;
        view->inner = new_region(view, nullptr);
        view->inner->is_package = true;
        // (its types: incomplete (tagged ones' too, so that their class-wide types can be named))
        std::function<bool(node*)> tagged = [&](node* aNode) -> bool
        {
            if (aNode->has_flag("tagged") || aNode->is(construct::Extension) || aNode->is(construct::TypePrivateExtension))
                return true;
            for (auto* c : aNode->children)
                if (tagged(c))
                    return true;
            return false;
        };
        if (auto* decls = u->item->first(construct::Declarations))
            for (auto* d : decls->children)
                if (d->is(construct::Type) || d->is(construct::TypePrivate) || d->is(construct::TypePrivateExtension) || d->is(construct::TypeIncomplete))
                    if (auto* defining = d->first(construct::Defining))
                    {
                        auto* t = new_type(type_kind::Private, {});
                        t->incomplete = true;
                        t->tagged = tagged(d);
                        auto* e = new_entity(entity_kind::Type, name_text(defining->source), nullptr, view->inner);
                        e->type = t;
                        t->decl = e;
                        t->declared_in = view->inner;
                        t->first_subtype = true;
                        view->inner->add(e);
                    }
        if (std::getenv("NEOS_ADA_DEBUG"))
        {
            std::fprintf(stderr, "limited view %s:", aName.c_str());
            for (auto* e : view->inner->declarations)
                std::fprintf(stderr, " %s", e->name.c_str());
            std::fprintf(stderr, "\n");
        }
        iLimitedViews[aName] = view;
        return view;
    }

    void program::apply_context(node* aContext, region* aRegion)
    {
        if (aContext == nullptr)
            return;
        for (auto* clause : aContext->children)
        {
            if (clause->is(construct::With))
            {
                for (auto* name : clause->children)
                {
                    if (name->is(construct::Flag))
                        continue;
                    auto const full = dotted_name(name);
                    // (a limited with of a package not analysed yet: its limited view (its types incomplete))
                    if (clause->has_flag("limited") && library_unit(full) == nullptr && full.find('.') == std::string::npos)
                    {
                        if (auto* view = limited_view(full))
                        {
                            aRegion->add(view);
                            continue;
                        }
                    }
                    // (a limited with of a child unit not analysed yet (e.g. of the unit's own child): its limited view, a declaration
                    // of its parent (added to the parent's region when that is created, if it isn't yet))
                    if (clause->has_flag("limited") && library_unit(full) == nullptr && full.find('.') != std::string::npos)
                        if (auto* view = limited_view(full))
                        {
                            auto const parent = full.substr(0u, full.rfind('.'));
                            auto const simple = full.substr(full.rfind('.') + 1u);
                            if (auto* p = library_unit(parent); p != nullptr && p->inner != nullptr)
                                p->inner->names.emplace(simple, view);
                            else
                                iLimitedChildViews[parent].push_back(view);
                            if (auto* rootUnit = library_unit(full.substr(0u, full.find('.'))))
                            {
                                bool present = false;
                                for (auto [i, end] = aRegion->names.equal_range(rootUnit->name); i != end; ++i)
                                    present = present || i->second == rootUnit;
                                if (!present)
                                    aRegion->add(rootUnit);
                            }
                            continue;
                        }
                    // (the root of the name is visible)
                    auto const root = full.substr(0u, full.find('.'));
                    if (auto* unit = library_unit(root))
                    {
                        bool present = false;
                        auto range = aRegion->names.equal_range(root);
                        for (auto i = range.first; i != range.second; ++i)
                            present = present || i->second == unit;
                        if (!present)
                            aRegion->add(unit);
                    }
                    else
                        error(name, "unit \"" + full + "\" not found");
                    if (library_unit(full) == nullptr)
                        error(name, "unit \"" + full + "\" not found");
                }
            }
            else if (clause->is(construct::Use) || clause->is(construct::UseType))
                use_clause(clause, aRegion);
            else if (clause->is(construct::Pragma))
                pragma(clause, aRegion);
        }
    }

    void program::analyse_unit(unit_info& aUnit)
    {
        if (aUnit.analysed)
            return;
        aUnit.analysed = true;
        iCurrentUnit = &aUnit;
        auto* item = aUnit.item;
        // (the region of the unit's declaration: its parent's (a child unit), else Standard's; its context clauses' names are 
        // visible in its own region's context)
        aUnit.context = new_region(nullptr, nullptr);
        apply_context(aUnit.unit->first(construct::Context), aUnit.context);
        // (a body: its specification's context too)
        if (aUnit.body && !aUnit.subunit)
            if (auto* spec = find_unit(aUnit.name, false); spec != nullptr && spec->context != nullptr)
            {
                for (auto* e : spec->context->declarations)
                    aUnit.context->add(e);
                aUnit.context->used.insert(aUnit.context->used.end(), spec->context->used.begin(), spec->context->used.end());
                aUnit.context->used_types.insert(aUnit.context->used_types.end(), spec->context->used_types.begin(), spec->context->used_types.end());
            }
        // (a subunit: its parent body's context (and so its ancestors') is visible too (10.1.3))
        if (aUnit.subunit)
            if (auto const d = aUnit.name.rfind('.'); d != std::string::npos)
                for (bool body : { true, false })
                    if (auto* pu = find_unit(aUnit.name.substr(0u, d), body); pu != nullptr && pu->context != nullptr)
                    {
                        for (auto* e : pu->context->declarations)
                            aUnit.context->add(e);
                        aUnit.context->used.insert(aUnit.context->used.end(), pu->context->used.begin(), pu->context->used.end());
                        aUnit.context->used_types.insert(aUnit.context->used_types.end(), pu->context->used_types.begin(), pu->context->used_types.end());
                        aUnit.context->used_all_types.insert(aUnit.context->used_all_types.end(), pu->context->used_all_types.begin(), pu->context->used_all_types.end());
                    }
        // (a child unit: its parents' contexts are visible too)
        auto parentRegion = iStandard;
        auto const dot = aUnit.name.rfind('.');
        if (dot != std::string::npos && !aUnit.subunit)
        {
            auto* parent = library_unit(aUnit.name.substr(0u, dot));
            if (parent == nullptr || parent->inner == nullptr)
                error(item, "parent unit \"" + aUnit.name.substr(0u, dot) + "\" not found");
            parentRegion = parent->inner;
            for (auto p = aUnit.name.substr(0u, dot);;)
            {
                if (auto* pu = find_unit(p, false); pu != nullptr && pu->context != nullptr)
                {
                    for (auto* e : pu->context->declarations)
                        aUnit.context->add(e);
                    aUnit.context->used.insert(aUnit.context->used.end(), pu->context->used.begin(), pu->context->used.end());
                }
                auto const d = p.rfind('.');
                if (d == std::string::npos)
                    break;
                p = p.substr(0u, d);
            }
        }
        if (aUnit.name == "standard")
        {
            // (package Standard: its declarations (the built in types are already declared in it))
            package_declaration(item, iLibrary, iStandardPackage);
            auto const find_type = [&](std::string const& aName) -> ada_type*
            {
                auto range = iStandard->names.equal_range(aName);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->is_type())
                        return i->second->type;
                return nullptr;
            };
            iBoolean = find_type("boolean");
            iInteger = find_type("integer");
            iNatural = find_type("natural");
            iString = find_type("string");
            iFloat = find_type("float");
            iLongFloat = find_type("long_float");
            iDuration = find_type("duration");
            iLongLongInteger = find_type("long_long_integer");
            if (iBoolean != nullptr)
                iBoolean->boolean = true;
            // (the predefined operators made before Boolean, Integer and Natural were known: made again when next wanted)
            iOperators.clear();
            iCurrentUnit = nullptr;
            return;
        }
        iDeclaringUnit = &aUnit;
        switch (item->kind)
        {
        case construct::Package:
            package_declaration(item, parentRegion);
            break;
        case construct::PackageBody:
            package_body(item, parentRegion);
            break;
        case construct::SubprogramDeclaration:
            subprogram_declaration(item, parentRegion);
            break;
        case construct::SubprogramBody:
            subprogram_body(item, parentRegion);
            break;
        case construct::Generic:
            generic_declaration(item, parentRegion);
            break;
        case construct::Instantiation:
            instantiation(item, parentRegion);
            break;
        case construct::RenamingPackage:
        case construct::RenamingSubprogram:
        case construct::RenamingGeneric:
            renaming_declaration(item, parentRegion);
            break;
        case construct::Subunit:
            iDeclaringUnit = nullptr;   // (a subunit's proper body completes its stub (in the stub's region): not a library unit)
            body_stub(item, parentRegion);
            break;
        default:
            break;
        }
        // (a library unit's aspects resolved at its end)
        resolve_deferred_aspects(parentRegion);
        // (a package named earlier by a limited with clause: its limited view's types completed by its types)
        if (!aUnit.body)
            if (auto v = iLimitedViews.find(aUnit.name); v != iLimitedViews.end() && aUnit.ent != nullptr && aUnit.ent->inner != nullptr)
                for (auto* e : v->second->inner->declarations)
                {
                    auto range = aUnit.ent->inner->names.equal_range(e->name);
                    for (auto i = range.first; i != range.second; ++i)
                        if (i->second->is_type() && i->second->type != nullptr && i->second->type != e->type)
                        {
                            e->type->full = i->second->type;
                            break;
                        }
                }
        // (a child unit named earlier by its parent's limited with clause: its limited view in the parent's region replaced by it)
        if (!aUnit.body && aUnit.name.find('.') != std::string::npos)
            if (auto v = iLimitedViews.find(aUnit.name); v != iLimitedViews.end())
                if (auto* parent = library_unit(aUnit.name.substr(0u, aUnit.name.rfind('.'))); parent != nullptr && parent->inner != nullptr)
                    for (auto i = parent->inner->names.begin(); i != parent->inner->names.end();)
                        i = i->second == v->second ? parent->inner->names.erase(i) : std::next(i);
        iDeclaringUnit = nullptr;
        iCurrentUnit = nullptr;
    }

    void program::generate()
    {
        collect_units();
        order_units();
        setup_standard();
        for (auto* u : iOrder)
            analyse_unit(*u);
        // (the main subprogram: the last library subprogram body of the main file)
        for (auto* u : iOrder)
            if (u->main_file && u->item->is(construct::SubprogramBody) && u->ent != nullptr)
                iMain = u->ent;
        generate_code();
    }
}
