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

    entity* program::declare(entity_kind aKind, node* aDefining, region* aRegion)
    {
        ++analysis_generation();
        std::string name;
        if (aDefining->is(construct::DefiningOperator))
            name = designator_of(aDefining);
        else if (aDefining->is(construct::DefiningCharacter))
            name = std::string{ aDefining->source };
        else
        {
            name = name_text(aDefining->source);
            auto const dot = name.rfind('.');
            if (dot != std::string::npos)
                name = name.substr(dot + 1u);
        }
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
        for (auto* d : aDeclarations->children)
            declaration(d, aRegion);
        resolve_deferred_aspects(aRegion);
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

    void program::resolve_deferred_aspects(region* aRegion)
    {
        // (aspects whose expressions name later declarations: resolved at the end of the declarative part (their freezing point))
        auto deferred = std::move(iDeferredAspects[aRegion]);
        iDeferredAspects.erase(aRegion);
        for (auto const& [type, name, value, ent, typeEntity] : deferred)
        {
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
                continue;
            }
            if (typeEntity != nullptr && (name == "static_predicate" || name == "dynamic_predicate" || name == "predicate"))
            {
                // (a predicate's function: its parameter is the current instance (named as the subtype is))
                auto* predicate = type->predicate;
                while (predicate != nullptr && predicate->expression != value)
                    predicate = predicate->parent;
                if (predicate == nullptr)
                    continue;
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
                iSubprogramsToGenerate.push_back(f);
                continue;
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
                continue;
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
                continue;
            }
            if (ent != nullptr)
            {
                // (a subprogram's precondition or postcondition (the latter can name its result))
                auto* saved = iCurrentSubprogram;
                auto* savedClassWide = iClassWideContract;
                if (name == "post" || name == "post'class")
                    iCurrentSubprogram = ent;
                // (a class-wide one: calls with its controlling formals as controlling operands dispatch (6.1.1(7/4)))
                iClassWideContract = name == "pre'class" || name == "post'class" ? ent : nullptr;
                condition(value, ent->inner != nullptr ? ent->inner : aRegion);
                iCurrentSubprogram = saved;
                iClassWideContract = savedClassWide;
                continue;
            }
            resolve(value, nullptr, aRegion);
            auto v = evaluate(value, aRegion);
            if (name == "small" && v.known())
            {
                type->base_type()->small = v.real();
                type->small = v.real();
                fixed_base_range(type);
            }
            else if (name == "size" && v.is_integer())
                type->size_clause = v.integer().to_uint64();
        }
    }

    void program::declaration(node* aDecl, region* aRegion)
    {
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
        auto* r = e->inner != nullptr ? e->inner : new_region(e, aRegion);
        r->is_package = true;
        // (limited views of its child units (its own limited with clauses))
        if (auto views = iLimitedChildViews.find(full_name(e)); views != iLimitedChildViews.end())
            for (auto* view : views->second)
                r->names.emplace(view->name.substr(view->name.rfind('.') + 1u), view);
        auto* visibleDecls = aDecl->first(construct::Declarations);
        declarations(visibleDecls, r);
        if (auto* priv = aDecl->first(construct::Private))
        {
            r->private_start = r->declarations.size();
            bool const saved = r->private_visible;
            r->private_visible = true;
            for (auto* d : priv->children)
                declaration(d, r);
            resolve_deferred_aspects(r);
            r->private_visible = saved;
        }
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
        defining->ent = e;
        aDecl->ent = e;
        if (e->kind == entity_kind::GenericPackage)
            return;     // (a generic package's body: analysed in its instances)
        auto* r = e->inner;
        bool const saved = r->private_visible;
        r->private_visible = true;
        // (the body's declarations are in a region of their own whose parent is the package's (its declarations are visible))
        auto* bodyRegion = new_region(nullptr, r);
        bodyRegion->is_package = true;
        bodyRegion->owner = e;
        if (!e->library && aRegion != r->parent)
            bodyRegion->enclosing = aRegion;
        e->inner = r;
        iBodyRegions[aDecl] = bodyRegion;
        iLibraryBodies[name] = bodyRegion;
        declarations(aDecl->first(construct::Declarations), bodyRegion);
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
        std::int32_t index = 0;
        for (auto* p : aSpec->children)
        {
            if (!p->is(construct::Parameter))
                continue;
            auto mode = parameter_mode::In;
            if (p->has_flag("out"))
                mode = p->has_flag("in") ? parameter_mode::InOut : parameter_mode::Out;
            ada_type* type = nullptr;
            node* defaultExpression = nullptr;
            bool seenType = false;
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
                        type = subtype_mark(c, aParameters);
                    seenType = true;
                }
                else
                    defaultExpression = c;
            }
            // (a null exclusion: a null-excluding subtype)
            if (type != nullptr && p->has_flag("not null") && type->view()->is_access() && !type->view()->not_null)
            {
                type = new_subtype(type->view());
                type->not_null = true;
            }
            // (the default expression: resolved where it is declared)
            if (defaultExpression != nullptr && type != nullptr && meaning_of(defaultExpression) == meaning::None)
                resolve(defaultExpression, type, aParameters);
            for (auto* d : p->all(construct::Defining))
            {
                auto* pe = declare(entity_kind::Parameter, d, aParameters);
                pe->type = type;
                pe->index = index++;
                pe->aliased = p->has_flag("aliased");
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
                        sub.result = subtype_mark(c, aParameters);
                }
                sub.result_not_null = result->has_flag("not null");
            }
            aEntity->type = sub.result;
        }
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
        auto const kind = spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure;
        if (aDecl->is(construct::SubprogramExpression))
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
            }
            iSubprogramsToGenerate.push_back(e);
            resolve(e->sub->expression, e->sub->result, e->inner);
        }
        aspects(aDecl, e, aRegion);
        add_primitive_if(e, aRegion);
        implicit_inequality(e, aRegion);
        return e;
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
        if (aEquality == nullptr || aEquality->name != "=" || aEquality->sub == nullptr || aEquality->sub->op != intrinsic::None ||
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

    void program::subprogram_body(node* aDecl, region* aRegion)
    {
        auto* spec = specification_of(aDecl);
        auto* defining = spec->child(0);
        auto const kind = spec->is(construct::FunctionSpecification) ? entity_kind::Function : entity_kind::Procedure;
        entity* e = nullptr;
        // (a library subprogram body: its declaration, if it has one)
        bool library = iDeclaringUnit != nullptr;
        entity* declared = nullptr;
        if (library)
            declared = library_unit(iDeclaringUnit->name);
        if (library && declared != nullptr && declared->is_generic() && declared->sub != nullptr)
        {
            // (a library generic subprogram's body: analysed in its instances)
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
        parameters->level = aRegion->level + 1;
        subprogram_specification(spec, own, parameters);
        own->sub->level = parameters->level;
        if (declared == nullptr && !library)
            declared = find_completion(own, aRegion);
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
            add_primitive_if(e, aRegion);
            implicit_inequality(e, aRegion);
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
            statements(handled, parameters);
        iCurrentSubprogram = saved;
        iSubprogramsToGenerate.push_back(e);
    }

    // --- objects

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
        ada_type* type = nullptr;
        if (typeNode->is(construct::TypeArray) || typeNode->is(construct::TypeArrayUnconstrained))
            type = type_definition(typeNode, {}, nullptr, aRegion, nullptr);
        else if (typeNode->is(construct::AccessDefinition))
            type = access_definition(typeNode, aRegion);
        else
            type = subtype_indication(typeNode, aRegion);
        // (a deferred constant's completion)
        std::vector<entity*> declared;
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
            e->type = type;
            if (!declared.empty() && !is_subtype_name(typeNode, aRegion) && !typeNode->is(construct::AccessDefinition) &&
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
            e->initial = init;
            if (deferred != nullptr && init != nullptr)
            {
                deferred->completed = true;
                deferred->completion = e;
                deferred->renamed = e;
            }
            declared.push_back(e);
        }
        aDecl->ent = declared.empty() ? nullptr : declared.front();
        if (init != nullptr)
        {
            auto* t = resolve(init, type, aRegion);
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
        auto* t = resolve(init, nullptr, aRegion);
        auto value = evaluate(init, aRegion);
        if (!value.known())
            error(init, "named number's value is not static");
        for (auto* d : aDecl->all(construct::Defining))
        {
            auto* e = declare(entity_kind::NamedNumber, d, aRegion);
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
                if (typeNode != nullptr)
                    type = typeNode->is(construct::AccessDefinition) ? access_definition(typeNode, aRegion) : subtype_mark(typeNode, aRegion);
                variable_name(renamed);
                auto* actual = resolve(renamed, type, aRegion);
                auto* target = renamed->ent;
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
                add_primitive_if(e, aRegion);
                implicit_inequality(e, aRegion);
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
            for (auto* name : aClause->children)
            {
                auto* e = resolve_entity(name, aRegion);
                if (e == nullptr || !e->is_package())
                    error(name, "package name expected in use clause");
                while (e->renamed != nullptr && e->renamed->is_package())
                    e = e->renamed;
                aRegion->used.push_back(e);
            }
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
            // (an object's: see volatile_actual (C.6(19/3)); a type's makes it a by-reference type (C.6(18/3)))
            auto range = aRegion->names.equal_range(name_text(value(0)->source));
            for (auto i = range.first; i != range.second; ++i)
                if (i->second->is_object())
                    (name == "volatile" || name == "atomic" ? i->second->volatile_object : i->second->volatile_components) = true;
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
            if (auto* x = value(2))
            {
                auto text = std::string{ x->file != nullptr ? x->file->original_of(x->source) : x->source };
                if (text.size() >= 2u && text.front() == '\"')
                    external = text.substr(1u, text.size() - 2u);
            }
            auto designatorName = designator;
            if (designatorName.size() >= 2u && designatorName.front() == '\"')
                designatorName = designatorName.substr(1u, designatorName.size() - 2u);
            auto range = aRegion->names.equal_range(designatorName);
            for (auto i = range.first; i != range.second; ++i)
            {
                auto* e = i->second;
                e->imported = true;
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
        else if (name == "convention")
        {
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
        else if (name == "elaborate_body" || name == "pure" || name == "preelaborate" || name == "elaborate" || name == "elaborate_all" ||
            name == "inline" || name == "optimize" || name == "suppress" || name == "unsuppress" || name == "list" || name == "page")
            ;
        else
            pragma_extra(aPragma, name, arguments, aRegion);
    }

    void program::representation_clause(node* aClause, region* aRegion)
    {
        if (aClause->is(construct::RepresentationAt))
        {
            // (for X use at Address; (J.7): an address clause)
            auto* e = aClause->size() >= 2u ? resolve_entity(aClause->child(0), aRegion) : nullptr;
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
            auto* e = resolve_entity(prefix, aRegion);
            if (e == nullptr)
                return;
            if (e->is_type())
            {
                auto* t = e->type;
                if (attribute == "size")
                {
                    resolve(expression, nullptr, aRegion);
                    auto v = evaluate(expression, aRegion);
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
            if (e == nullptr || !e->is_type())
                return;
            auto* t = e->type->view()->base_type()->view();
            auto* aggregate = aClause->child(1);
            resolve(aggregate, t->kind == type_kind::Enumeration ? enumeration_rep_array(t) : nullptr, aRegion);
            std::vector<bigint> codes;
            for (auto* c : aggregate->children)
            {
                auto* v = c->is(construct::Association) ? c->children.back() : c;
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
            t->representation = codes;
        }
        else if (aClause->is(construct::RepresentationRecord))
        {
            auto* e = resolve_entity(aClause->child(0), aRegion);
            if (e == nullptr || !e->is_type())
                return;
            auto* t = e->type->view()->base_type()->view();
            for (auto* c : aClause->children)
            {
                if (!c->is(construct::RepresentationComponent))
                    continue;
                auto const name = name_text(c->child(0)->source);
                resolve(c->child(1), nullptr, aRegion);
                auto* r = c->child(2);
                resolve(r->child(0), nullptr, aRegion);
                resolve(r->child(1), nullptr, aRegion);
                auto position = evaluate(c->child(1), aRegion);
                auto first = evaluate(r->child(0), aRegion);
                auto last = evaluate(r->child(1), aRegion);
                for (auto& component : t->components)
                    if (component.ent->name == name && position.is_integer() && first.is_integer() && last.is_integer())
                    {
                        component.bit_position = position.integer().to_uint64() * 8u + first.integer().to_uint64();
                        component.bit_size = (last.integer() - first.integer() + bigint{ 1 }).to_uint64();
                    }
            }
        }
    }
}
