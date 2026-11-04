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

// C: declarations (6.7): declaration specifiers, declarators, scopes (6.2.1), linkage (6.2.2), structures, unions and enumerations, 
// typedef names, initializers (6.7.11) and function definitions (6.9.2)

#include <algorithm>
#include <iostream>
#include "program.hpp"

namespace neos::compilers::c
{
    namespace
    {
        bool is_specifier(construct aKind)
        {
            switch (aKind)
            {
            case construct::Storage:
            case construct::Type:
            case construct::BitInt:
            case construct::Struct:
            case construct::Enum:
            case construct::Atomic:
            case construct::Typeof:
            case construct::TypedefName:
            case construct::Qualifier:
            case construct::FunctionSpecifier:
            case construct::Alignas:
                return true;
            default:
                return false;
            }
        }

        std::string keyword_of(node const* aNode)
        {
            // (the keyword that a construct's source begins with)
            auto const s = aNode->source;
            std::size_t i = 0u;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
                ++i;
            return std::string{ s.substr(0u, i) };
        }

        std::uint8_t qualifier_of(node const* aQualifier)
        {
            auto const k = keyword_of(aQualifier);
            if (k == "const") return QConst;
            if (k == "volatile") return QVolatile;
            if (k == "restrict") return QRestrict;
            if (k == "_Atomic") return QAtomic;
            return 0u;
        }

        std::string attribute_name(node const* aAttribute, bool& aGnu)
        {
            // (an attribute's name: without its prefix (gnu:: (aGnu) or __gnu__::, or neos:: (the preprocessor's); another vendor's: 
            // none) and its __ __)
            auto const trim = [](std::string_view aText)
            {
                while (!aText.empty() && aText.front() == ' ')
                    aText.remove_prefix(1u);
                while (!aText.empty() && aText.back() == ' ')
                    aText.remove_suffix(1u);
                return aText;
            };
            auto text = aAttribute->source;
            if (auto const paren = text.find('('); paren != std::string_view::npos)
                text = text.substr(0u, paren);
            text = trim(text);
            aGnu = false;
            if (auto const scope = text.find("::"); scope != std::string_view::npos)
            {
                auto const prefix = trim(text.substr(0u, scope));
                aGnu = prefix == "gnu" || prefix == "__gnu__" || prefix == "neos";
                if (!aGnu)
                    return {};
                text = trim(text.substr(scope + 2u));
            }
            if (text.size() > 4u && text.starts_with("__") && text.ends_with("__"))
                text = text.substr(2u, text.size() - 4u);
            return std::string{ text };
        }
    }

    // --- attributes

    void attribute_info::merge(attribute_info const& aOther)
    {
        noreturn = noreturn || aOther.noreturn;
        packed = packed || aOther.packed;
        aligned = std::max(aligned, aOther.aligned);
        if (aOther.pack != 0u)
            pack = aOther.pack;
        if (aOther.constructor)
            constructor = aOther.constructor;
        if (aOther.destructor)
            destructor = aOther.destructor;
        if (aOther.cleanup != nullptr)
            cleanup = aOther.cleanup;
    }

    void program::read_attributes(node* aAttributes, attribute_info& aResult)
    {
        // (an attribute specifier's attributes that the compiler implements; it ignores the others)
        for (auto* a : aAttributes->children)
        {
            if (!a->is(construct::Attribute))
                continue;
            bool gnu = false;
            auto const name = attribute_name(a, gnu);
            auto const argument = [&](char const* aWhat) -> std::int64_t
            {
                if (a->size() != 1u)
                    error(a, "attribute '" + name + "': its argument is not an integer constant");
                expression(a->child(0));
                return integer_constant(a->child(0), aWhat);
            };
            if (name == "noreturn" || name == "_Noreturn")
                aResult.noreturn = true;
            else if (!gnu)
                continue;
            else if (name == "packed")
                aResult.packed = true;
            else if (name == "aligned")
            {
                // (no argument: the largest alignment of a type (as GCC's on x86-64))
                std::int64_t alignment = 16;
                if (a->size() != 0u)
                    alignment = argument("alignment");
                if (alignment <= 0 || (alignment & (alignment - 1)) != 0)
                    error(a, "requested alignment is not a positive power of 2");
                aResult.aligned = std::max(aResult.aligned, static_cast<std::uint64_t>(alignment));
            }
            else if (name == "cleanup")
            {
                if (a->size() != 1u || !a->child(0)->is(construct::Identifier))
                    error(a, "attribute 'cleanup': its argument is not a function's name");
                aResult.cleanup = a->child(0);
            }
            else if (name == "pack")
                aResult.pack = static_cast<std::uint64_t>(argument("pack"));
            else if (name == "constructor" || name == "destructor")
            {
                std::int64_t priority = 65535;
                if (a->size() != 0u)
                    priority = argument("priority");
                (name == "constructor" ? aResult.constructor : aResult.destructor) = priority;
            }
        }
    }

    attribute_info program::declarator_attributes(node* aDeclarator)
    {
        // (the attributes of a declarator's declared name, of its suffixes (not its parameters') and of its pointers (GNU's position 
        // after a '*'), its parenthesized declarator's too)
        attribute_info result;
        if (aDeclarator == nullptr)
            return result;
        for (auto* c : aDeclarator->children)
        {
            if (c->is(construct::Attributes))
                read_attributes(c, result);
            else if (c->is(construct::Function) || c->is(construct::Array) || c->is(construct::Pointer))
            {
                for (auto* x : c->children)
                    if (x->is(construct::Attributes))
                        read_attributes(x, result);
            }
            else if (c->is(construct::Declarator))
                result.merge(declarator_attributes(c));
        }
        return result;
    }

    void program::cleanup_function(entity* aObject, node* aFunction, node* aWhere)
    {
        // (gnu::cleanup(f): f(&object) called when the object's scope is left (by its end, a jump out of it or a return); not by longjmp)
        if (aObject->kind != entity_kind::Object || aObject->where == object_storage::Static || aObject->linkage != linkage::None)
        {
            warning(aWhere, "attribute 'cleanup' ignored (not an automatic object)");
            return;
        }
        expression(aFunction);
        auto* f = aFunction->ent != nullptr ? aFunction->ent->self() : nullptr;
        if (f == nullptr || f->kind != entity_kind::Function)
            error(aFunction, "attribute 'cleanup': '" + std::string{ aFunction->source } + "' is not a function");
        auto const* ft = f->type.type;
        if (ft->parameters.size() != 1u || ft->variadic || !ft->parameters[0].type->is_pointer() || returns_record(ft) ||
            !compatible(ft->parameters[0].type->base.unqualified(), aObject->type.unqualified()) && !ft->parameters[0].type->base->is_void())
            error(aFunction, "attribute 'cleanup': '" + f->name + "' must take one argument, a pointer to '" + type_name(aObject->type) + "'");
        aObject->cleanup = f;
        aObject->addressTaken = true;
        f->used = true;
        if (iFunction != nullptr)
            iFunction->function->hasCleanups = true;
    }

    void program::startup_function(entity* aFunction, attribute_info const& aAttributes, node* aWhere)
    {
        // (gnu::constructor: called before main; gnu::destructor: after main returns or exit is called (registered as atexit's are,
        // before main))
        if (!aAttributes.constructor && !aAttributes.destructor)
            return;
        auto* f = aFunction->self();
        if (!f->type->parameters.empty() && f->type->prototype)
            warning(aWhere, "a constructor or destructor function '" + f->name + "' with parameters is called without arguments");
        auto const add = [&](std::vector<std::pair<std::int64_t, entity*>>& aList, std::int64_t aPriority)
        {
            for (auto& existing : aList)
                if (existing.second == f)
                {
                    existing.first = aPriority;
                    return;
                }
            aList.emplace_back(aPriority, f);
        };
        if (aAttributes.constructor)
            add(iConstructors, *aAttributes.constructor);
        if (aAttributes.destructor)
            add(iDestructors, *aAttributes.destructor);
    }

    // --- scopes

    void program::push_scope()
    {
        iScopes.emplace_back();
    }

    void program::pop_scope()
    {
        iScopes.pop_back();
    }

    entity* program::lookup(std::string const& aName) const
    {
        for (auto i = iScopes.rbegin(); i != iScopes.rend(); ++i)
        {
            auto const found = i->names.find(aName);
            if (found != i->names.end())
                return found->second;
        }
        return nullptr;
    }

    c_type const* program::lookup_tag(std::string const& aName, bool aCurrentOnly) const
    {
        for (auto i = iScopes.rbegin(); i != iScopes.rend(); ++i)
        {
            auto const found = i->tags.find(aName);
            if (found != i->tags.end())
                return found->second;
            if (aCurrentOnly)
                break;
        }
        return nullptr;
    }

    // --- translation units

    void program::analyse_unit(translation_unit& aUnit)
    {
        iUnit = &aUnit;
        iScopes.clear();
        push_scope();
        for (auto* d : aUnit.root->children)
            external_declaration(d);
        // (tentative definitions without a definition: definitions, initialized to zero (6.9.3))
        for (auto* o : iObjects)
            if (o->unit == &aUnit && o->tentative && !o->defined)
            {
                if (o->type->is_array() && !o->type->length)
                {
                    warning(o->decl, "array '" + o->name + "' assumed to have one element");
                    o->type = qtype{ array_of(o->type->base, 1u), o->type.qualifiers };
                }
                if (!o->type->is_complete() && !o->type->is_array())
                    error(o->decl, "storage size of '" + o->name + "' isn't known");
                o->defined = true;
            }
        iScopes.clear();
        iUnit = nullptr;
    }

    void program::external_declaration(node* aDeclaration)
    {
        if (aDeclaration->is(construct::FunctionDefinition))
            function_definition(aDeclaration);
        else if (aDeclaration->is(construct::Declaration))
            declaration(aDeclaration, true);
        else if (aDeclaration->is(construct::StaticAssert))
            static_assertion(aDeclaration);
    }

    void program::static_assertion(node* aAssert)
    {
        auto* condition = aAssert->child(0);
        expression(condition);
        auto const value = integer_constant(condition, "static assertion");
        if (value == 0)
        {
            std::string message = "static assertion failed";
            if (auto* text = aAssert->child(1); text != nullptr)
                message += ": " + std::string{ text->source };
            error(aAssert, message);
        }
    }

    // --- declaration specifiers

    declaration_specifiers program::specifiers(std::vector<node*> const& aNodes, node* aWhere, bool aAllowStorage)
    {
        declaration_specifiers result;
        result.where = aWhere;
        int nVoid = 0, nChar = 0, nShort = 0, nInt = 0, nLong = 0, nFloat = 0, nDouble = 0, nSigned = 0, nUnsigned = 0, nBool = 0, nComplex = 0,
            nDecimal = 0;
        std::uint8_t qualifiers = 0u;
        qtype other;
        node* bitInt = nullptr;
        int storageCount = 0;
        for (std::size_t index = 0u; index < aNodes.size(); ++index)
        {
            auto* n = aNodes[index];
            switch (n->kind)
            {
            case construct::Storage:
            {
                auto const k = keyword_of(n);
                if (!aAllowStorage)
                    error(n, "storage class specified for a type name or member");
                if (k == "thread_local" || k == "_Thread_local")
                {
                    result.threadLocal = true;
                    continue;
                }
                if (k == "constexpr")
                {
                    result.isConstexpr = true;
                    continue;
                }
                storage_class sc = storage_class::None;
                if (k == "auto") sc = storage_class::Auto;
                else if (k == "extern") sc = storage_class::Extern;
                else if (k == "register") sc = storage_class::Register;
                else if (k == "static") sc = storage_class::Static;
                else if (k == "typedef") sc = storage_class::Typedef;
                // (C23: auto with another storage class specifier (or with a type) is the type inference's (or nothing))
                if (sc == storage_class::Auto && result.storage != storage_class::None)
                {
                    result.autoType = true;
                    continue;
                }
                if (result.storage == storage_class::Auto && sc != storage_class::Auto)
                {
                    result.autoType = true;
                    result.storage = sc;
                    continue;
                }
                if (result.storage != storage_class::None && result.storage != sc)
                    error(n, "multiple storage classes in declaration specifiers");
                result.storage = sc;
                ++storageCount;
                break;
            }
            case construct::Type:
            {
                auto const k = keyword_of(n);
                if (k == "void") ++nVoid;
                else if (k == "char") ++nChar;
                else if (k == "short") ++nShort;
                else if (k == "int") ++nInt;
                else if (k == "long") ++nLong;
                else if (k == "float") ++nFloat;
                else if (k == "double") ++nDouble;
                else if (k == "signed") ++nSigned;
                else if (k == "unsigned") ++nUnsigned;
                else if (k == "bool" || k == "_Bool") ++nBool;
                else if (k == "_Complex" || k == "_Imaginary") ++nComplex;
                else ++nDecimal;
                result.typeSpecified = true;
                break;
            }
            case construct::BitInt:
                bitInt = n;
                result.typeSpecified = true;
                break;
            case construct::Struct:
            {
                if (other)
                    error(n, "two or more data types in declaration specifiers");
                // (GNU: the attributes right after a structure's or union's definition are its)
                attribute_info attributes;
                if (!n->source.empty() && n->source.back() == '}')
                    while (index + 1u < aNodes.size() && aNodes[index + 1u]->is(construct::Attributes))
                        read_attributes(aNodes[++index], attributes);
                other = qtype{ struct_specifier(n, false, &attributes) };
                result.typeSpecified = true;
                break;
            }
            case construct::Enum:
                if (other)
                    error(n, "two or more data types in declaration specifiers");
                other = qtype{ enum_specifier(n, false) };
                result.typeSpecified = true;
                break;
            case construct::Atomic:
                if (other)
                    error(n, "two or more data types in declaration specifiers");
                other = type_name_type(n->child(0)).with(QAtomic);
                result.typeSpecified = true;
                break;
            case construct::Typeof:
            {
                if (other)
                    error(n, "two or more data types in declaration specifiers");
                auto* operand = n->child(0);
                qtype t;
                if (operand->is(construct::TypeName))
                    t = type_name_type(operand);
                else
                {
                    expression(operand);
                    t = operand->type;
                    // (typeof of a function designator or array: its own type (no conversion))
                }
                if (n->source.starts_with("typeof_unqual"))
                    t = t.unqualified();
                other = t;
                result.typeSpecified = true;
                break;
            }
            case construct::TypedefName:
            {
                if (other)
                    error(n, "two or more data types in declaration specifiers");
                auto* e = lookup(name_of(n));
                if (e == nullptr || e->kind != entity_kind::Typedef)
                    error(n, "unknown type name '" + name_of(n) + "'");
                other = e->type;
                result.typeSpecified = true;
                break;
            }
            case construct::Qualifier:
                qualifiers |= qualifier_of(n);
                break;
            case construct::FunctionSpecifier:
                if (keyword_of(n) == "inline")
                    result.isInline = true;
                else
                    result.noreturn = true;
                break;
            case construct::Alignas:
            {
                auto* operand = n->child(0);
                std::uint64_t alignment = 0u;
                if (operand->is(construct::TypeName))
                {
                    alignment = align_of(type_name_type(operand));
                    // (GNU: an aligned attribute of the type name's)
                    attribute_info attributes;
                    for (auto* c : operand->children)
                        if (c->is(construct::Attributes))
                            read_attributes(c, attributes);
                    alignment = std::max(alignment, attributes.aligned);
                }
                else
                {
                    expression(operand);
                    alignment = static_cast<std::uint64_t>(integer_constant(operand, "alignment"));
                }
                if (alignment != 0u && (alignment & (alignment - 1u)) != 0u)
                    error(n, "requested alignment is not a power of 2");
                result.alignment = std::max(result.alignment, alignment);
                break;
            }
            case construct::Attributes:
                read_attributes(n, result.attributes);
                if (result.attributes.noreturn)
                    result.noreturn = true;
                break;
            default:
                break;
            }
        }
        (void)storageCount;
        auto const basics = nVoid + nChar + nShort + nInt + nLong + nFloat + nDouble + nSigned + nUnsigned + nBool + nComplex + nDecimal + (bitInt != nullptr ? 1 : 0);
        if (other && basics != 0)
            error(aWhere, "two or more data types in declaration specifiers");
        qtype type;
        if (other)
            type = other;
        else if (nComplex != 0)
            error(aWhere, "complex types are not supported (__STDC_NO_COMPLEX__)");
        else if (nDecimal != 0)
            error(aWhere, "decimal floating types are not supported");
        else if (bitInt != nullptr)
        {
            if (nVoid + nChar + nShort + nInt + nLong + nFloat + nDouble + nBool != 0 || nSigned + nUnsigned > 1)
                error(aWhere, "invalid combination of type specifiers");
            expression(bitInt->child(0));
            auto const width = integer_constant(bitInt->child(0), "_BitInt width");
            bool const isSigned = nUnsigned == 0;
            if (width < (isSigned ? 2 : 1) || width > 64)
                error(bitInt, "_BitInt width " + std::to_string(width) + " is not in the range " + (isSigned ? "2" : "1") + " to BITINT_MAXWIDTH (64)");
            type = qtype{ bit_int(static_cast<std::uint32_t>(width), isSigned) };
        }
        else if (basics == 0)
        {
            if (result.storage == storage_class::Auto || result.autoType || result.isConstexpr)
            {
                result.autoType = true;
                if (result.storage == storage_class::Auto)
                    result.storage = storage_class::None;
                type = int_type();
            }
            else
            {
                warning(aWhere, "type defaults to 'int' in declaration");
                type = int_type();
            }
        }
        else
        {
            auto const bad = [&]() { error(aWhere, "invalid combination of type specifiers"); };
            if (nSigned + nUnsigned > 1 || nLong > 2 || nShort > 1 || nInt > 1 || nChar > 1)
                bad();
            if (nVoid != 0)
            {
                if (basics != 1) bad();
                type = qtype{ basic(type_kind::Void) };
            }
            else if (nBool != 0)
            {
                if (basics != 1) bad();
                type = qtype{ basic(type_kind::Bool) };
            }
            else if (nFloat != 0)
            {
                if (basics != 1) bad();
                type = qtype{ basic(type_kind::Float) };
            }
            else if (nDouble != 0)
            {
                if (basics != 1 + nLong || nLong > 1) bad();
                type = qtype{ basic(nLong != 0 ? type_kind::LongDouble : type_kind::Double) };
            }
            else if (nChar != 0)
            {
                if (nShort + nInt + nLong != 0) bad();
                type = qtype{ basic(nSigned != 0 ? type_kind::SChar : nUnsigned != 0 ? type_kind::UChar : type_kind::Char) };
            }
            else if (nShort != 0)
            {
                if (nLong != 0) bad();
                type = qtype{ basic(nUnsigned != 0 ? type_kind::UShort : type_kind::Short) };
            }
            else if (nLong == 2)
                type = qtype{ basic(nUnsigned != 0 ? type_kind::ULongLong : type_kind::LongLong) };
            else if (nLong == 1)
                type = qtype{ basic(nUnsigned != 0 ? type_kind::ULong : type_kind::Long) };
            else
                type = qtype{ basic(nUnsigned != 0 ? type_kind::UInt : type_kind::Int) };
        }
        if (result.isConstexpr)
            qualifiers |= QConst;
        result.type = qualified(type, qualifiers);
        return result;
    }

    // --- structures, unions and enumerations

    c_type const* program::struct_specifier(node* aSpecifier, bool aDeclarationOnly, attribute_info const* aAttributes)
    {
        bool const isUnion = keyword_of(aSpecifier) == "union";
        bool const body = !aSpecifier->source.empty() && aSpecifier->source.back() == '}';
        node* tagNode = aSpecifier->first(construct::Tag);
        std::string const tag = tagNode != nullptr ? tagNode->text() : std::string{};
        c_type const* type = nullptr;
        if (!tag.empty())
        {
            // (a definition, or a declaration "struct S;", declares the tag in the current scope; any other use refers to a visible 
            // declaration, or else declares it)
            type = lookup_tag(tag, body || aDeclarationOnly);
            if (type != nullptr && (!type->is_record() || type->record->isUnion != isUnion))
            {
                if (body || aDeclarationOnly)
                    error(tagNode, "'" + tag + "' defined as the wrong kind of tag");
                error(tagNode, "'" + tag + "' is not a " + (isUnion ? "union" : "struct"));
            }
            if (type != nullptr && body && type->record->complete)
            {
                // (C23: a redefinition with the same content in the same scope is allowed (checked loosely))
                if (type->record->decl == aSpecifier)
                    return type;
                auto& record = iRecords.emplace_back();
                record.tag = tag;
                record.isUnion = isUnion;
                record.decl = aSpecifier;
                type = record_type(&record);
                iScopes.back().tags[tag] = type;
            }
        }
        if (type == nullptr)
        {
            auto& record = iRecords.emplace_back();
            record.tag = tag;
            record.isUnion = isUnion;
            record.decl = aSpecifier;
            type = record_type(&record);
            if (!tag.empty())
                iScopes.back().tags[tag] = type;
        }
        if (!body)
            return type;
        auto* record = type->record;
        record->decl = aSpecifier;
        record->members.clear();
        {
            attribute_info attributes;
            if (aAttributes != nullptr)
                attributes = *aAttributes;
            for (auto* c : aSpecifier->children)
                if (c->is(construct::Attributes))
                    read_attributes(c, attributes);
            record->packed = attributes.packed;
            record->aligned = attributes.aligned;
            record->pack = attributes.pack;
        }
        for (auto* m : aSpecifier->children)
        {
            if (m->is(construct::StaticAssert))
            {
                static_assertion(m);
                continue;
            }
            if (!m->is(construct::MemberDeclaration))
                continue;
            if (m->child(0) != nullptr && m->child(0)->is(construct::StaticAssert))
            {
                static_assertion(m->child(0));
                continue;
            }
            std::vector<node*> specs;
            std::vector<node*> declarators;
            for (auto* c : m->children)
            {
                if (is_specifier(c->kind) || c->is(construct::Attributes))
                    specs.push_back(c);
                else if (c->is(construct::MemberDeclarator))
                    declarators.push_back(c);
            }
            auto const spec = specifiers(specs, m, false);
            if (declarators.empty())
            {
                // (an anonymous structure or union member)
                if (spec.type->is_record() && spec.type->record != nullptr && spec.type->record->tag.empty())
                {
                    member_info info;
                    info.type = spec.type;
                    info.decl = m;
                    info.alignment = std::max(spec.alignment, spec.attributes.aligned);
                    info.packed = spec.attributes.packed;
                    record->members.push_back(info);
                }
                else
                    warning(m, "declaration does not declare anything");
                continue;
            }
            for (auto* d : declarators)
            {
                member_info info;
                info.decl = d;
                node* width = nullptr;
                auto attributes = spec.attributes;
                if (d->child(0) != nullptr && d->child(0)->is(construct::Declarator))
                {
                    attributes.merge(declarator_attributes(d->child(0)));
                    auto const r = declarator(d->child(0), spec.type);
                    info.name = r.name != nullptr ? name_of(r.name) : std::string{};
                    info.type = r.type;
                    width = d->child(1);
                }
                else
                {
                    info.type = spec.type;
                    width = d->child(0);
                }
                if (width != nullptr)
                {
                    expression(width);
                    auto const w = integer_constant(width, "bit-field width");
                    if (!info.type->is_integer())
                        error(d, "bit-field '" + info.name + "' has invalid type");
                    if (w < 0 || static_cast<std::uint64_t>(w) > width_of(info.type.type))
                        error(d, "width of bit-field '" + info.name + "' exceeds its type");
                    if (w == 0 && !info.name.empty())
                        error(d, "zero width for bit-field '" + info.name + "'");
                    info.bitWidth = static_cast<std::uint32_t>(w);
                }
                if (info.type->is_function())
                    error(d, "field '" + info.name + "' declared as a function");
                if (!info.type->is_complete() && !(info.type->is_array() && !info.type->length))
                    error(d, "field '" + info.name + "' has incomplete type");
                if (info.type->is_variably_modified())
                    error(d, "a member cannot have a variably modified type");
                for (auto const& existing : record->members)
                    if (!info.name.empty() && existing.name == info.name)
                        error(d, "duplicate member '" + info.name + "'");
                info.alignment = std::max(spec.alignment, attributes.aligned);
                info.packed = attributes.packed;
                record->members.push_back(info);
            }
        }
        // (a flexible array member: the last member of a structure with another member)
        for (std::size_t i = 0u; i < record->members.size(); ++i)
        {
            auto const& m = record->members[i];
            if (m.type->is_array() && !m.type->length)
            {
                if (i + 1u != record->members.size() || isUnion)
                    error(m.decl, "flexible array member not at end of struct");
                record->flexible = true;
            }
        }
        layout(*record, aSpecifier);
        return type;
    }

    c_type const* program::enum_specifier(node* aSpecifier, bool aDeclarationOnly)
    {
        bool const body = !aSpecifier->source.empty() && aSpecifier->source.back() == '}';
        node* tagNode = aSpecifier->first(construct::Tag);
        std::string const tag = tagNode != nullptr ? tagNode->text() : std::string{};
        node* fixedNode = aSpecifier->first(construct::EnumType);
        c_type const* type = nullptr;
        if (!tag.empty())
        {
            type = lookup_tag(tag, body || aDeclarationOnly || fixedNode != nullptr);
            if (type != nullptr && type->kind != type_kind::Enum)
                error(tagNode, "'" + tag + "' defined as the wrong kind of tag");
            if (type != nullptr && body && type->enumeration->complete)
                error(tagNode, "redeclaration of 'enum " + tag + "'");
        }
        if (type == nullptr)
        {
            auto& info = iEnums.emplace_back();
            info.tag = tag;
            info.decl = aSpecifier;
            info.underlying = int_type();
            type = enum_type(&info);
            if (!tag.empty())
                iScopes.back().tags[tag] = type;
        }
        auto* info = type->enumeration;
        if (fixedNode != nullptr)
        {
            std::vector<node*> specs{ fixedNode->children.begin(), fixedNode->children.end() };
            auto const t = specifiers(specs, fixedNode, false);
            if (!t.type->is_integer() || t.type->kind == type_kind::Enum || t.type->kind == type_kind::Bool)
                error(fixedNode, "invalid underlying type for enumeration");
            info->fixed = true;
            info->underlying = t.type.unqualified();
        }
        if (!body)
            return type;
        std::int64_t next = 0;
        bool first = true;
        std::int64_t low = 0, high = 0;
        bool anyUnsignedLarge = false;
        std::vector<entity*> constants;
        for (auto* e : aSpecifier->all(construct::Enumerator))
        {
            auto* nameNode = e->child(0);
            node* valueNode = nullptr;
            for (std::size_t i = 1u; i < e->size(); ++i)
                if (!e->child(i)->is(construct::Attributes))
                    valueNode = e->child(i);
            std::int64_t value = next;
            if (valueNode != nullptr)
            {
                expression(valueNode);
                value = integer_constant(valueNode, "enumerator value");
                if (valueNode->type->is_unsigned() && size_of(valueNode->type) == 8u && value < 0)
                    anyUnsignedLarge = true;
            }
            else if (!first && next == INT64_MIN)
                error(e, "overflow in enumeration values");
            if (info->fixed)
            {
                // (a value that the fixed underlying type cannot represent)
                auto const w = width_of(info->underlying.type);
                bool const s = info->underlying->is_signed();
                bool fits = true;
                if (w < 64u)
                    fits = s ? (value >= -(std::int64_t{ 1 } << (w - 1u)) && value < (std::int64_t{ 1 } << (w - 1u))) :
                        (value >= 0 && static_cast<std::uint64_t>(value) < (std::uint64_t{ 1 } << w));
                if (!fits)
                    error(e, "enumerator value " + std::to_string(value) + " outside the range of the underlying type");
            }
            if (first)
                low = high = value;
            low = std::min(low, value);
            high = std::max(high, value);
            first = false;
            next = value + 1;
            auto& ent = iEntities.emplace_back();
            ent.kind = entity_kind::EnumConstant;
            ent.name = name_of(nameNode);
            ent.value = value;
            ent.decl = e;
            ent.unit = iUnit;
            // (during the enumeration's definition a constant is of type int if it fits, else of its value's type)
            if (info->fixed)
                ent.type = info->underlying;
            else if (value >= INT32_MIN && value <= INT32_MAX && !anyUnsignedLarge)
                ent.type = int_type();
            else
                ent.type = qtype{ basic(anyUnsignedLarge || value > INT64_MAX ? type_kind::ULong : type_kind::Long) };
            if (auto const existing = iScopes.back().names.find(ent.name); existing != iScopes.back().names.end())
                error(nameNode, "redeclaration of '" + ent.name + "'");
            iScopes.back().names[ent.name] = &ent;
            constants.push_back(&ent);
        }
        if (!info->fixed)
        {
            // (the underlying type (as GCC's): unsigned int if there are no negative values and all fit, else int if all fit (else long
            // or unsigned long))
            if (low >= 0 && static_cast<std::uint64_t>(high) <= UINT32_MAX && !anyUnsignedLarge)
                info->underlying = qtype{ basic(type_kind::UInt) };
            else if (low >= INT32_MIN && high <= INT32_MAX && !anyUnsignedLarge)
                info->underlying = int_type();
            else if (low < 0 && !anyUnsignedLarge)
                info->underlying = qtype{ basic(type_kind::Long) };
            else
                info->underlying = qtype{ basic(type_kind::ULong) };
            // (after its definition, each constant has the enumerated type's ... (C23: int if all fit, else the enumerated type))
            if (!(low >= INT32_MIN && high <= INT32_MAX && !anyUnsignedLarge))
                for (auto* c : constants)
                    c->type = qtype{ type };
        }
        info->complete = true;
        return type;
    }

    // --- declarators

    declarator_result program::declarator(node* aDeclarator, qtype aType, bool aParameter)
    {
        // (a declarator (or an abstract declarator): its pointers, then its identifier or parenthesized declarator, then its suffixes,
        // as its children in order; the type is derived from the specifiers' outwards: the pointers, then the suffixes from the last,
        // then the parenthesized declarator)
        declarator_result result;
        if (aDeclarator == nullptr)
        {
            result.type = aType;
            return result;
        }
        qtype type = aType;
        std::vector<node*> suffixes;
        node* inner = nullptr;
        for (auto* c : aDeclarator->children)
        {
            switch (c->kind)
            {
            case construct::Pointer:
            {
                std::uint8_t q = 0u;
                for (auto* qn : c->all(construct::Qualifier))
                    q |= qualifier_of(qn);
                type = qtype{ pointer_to(type), q };
                break;
            }
            case construct::DeclName:
                result.name = c;
                break;
            case construct::Declarator:
            case construct::Abstract:
                inner = c;
                break;
            case construct::Array:
            case construct::Function:
                suffixes.push_back(c);
                break;
            default:
                break;
            }
        }
        node* outermostFunction = nullptr;
        for (auto i = suffixes.rbegin(); i != suffixes.rend(); ++i)
        {
            auto* s = *i;
            if (s->is(construct::Array))
            {
                if (type->is_function())
                    error(s, "declaration of an array of functions");
                if (type->is_void())
                    error(s, "declaration of an array of voids");
                if (!type->is_complete() && !(type->is_array() && type->vla))
                {
                    if (type->is_array())
                        error(s, "array type has incomplete element type");
                    if (!type->is_record() || !aParameter)
                        error(s, "array type has incomplete element type '" + type_name(type) + "'");
                }
                std::uint8_t q = 0u;
                node* size = nullptr;
                bool star = false;
                for (auto* c : s->children)
                {
                    if (c->is(construct::Qualifier))
                        q |= qualifier_of(c);
                    else if (c->is(construct::ArrayStar))
                        star = true;
                    else if (!c->is(construct::ArrayStatic) && !c->is(construct::Attributes))
                        size = c;
                }
                (void)q;
                if (size != nullptr)
                {
                    expression(size);
                    if (!value_type(size)->is_integer())
                        error(size, "size of array has non-integer type");
                    auto const v = evaluate(size, true);
                    if (v && v->is_integer())
                    {
                        if (size->type->is_signed() && v->s() < 0)
                            error(size, "size of array is negative");
                        if (v->integer == 0u)
                            ;   // (a zero-length array: an extension, as GCC's)
                        type = qtype{ array_of(type, v->integer) };
                    }
                    else
                        type = qtype{ array_of(type, std::nullopt, true, size) };
                }
                else
                    type = qtype{ array_of(type, std::nullopt, star) };
                if (q != 0u && aParameter)
                {
                    // (qualifiers in a parameter's array declarator qualify the pointer it is adjusted to)
                    auto& t = const_cast<c_type&>(*type.type);
                    (void)t;
                }
            }
            else
            {
                if (type->is_function())
                    error(s, "function returning a function");
                if (type->is_array())
                    error(s, "function returning an array");
                bool variadic = false;
                bool prototype = true;
                auto parameters = parameters_of(s, variadic, prototype);
                type = qtype{ function_of(type.unqualified(), parameters, variadic, prototype) };
                outermostFunction = s;
            }
        }
        if (inner != nullptr)
        {
            auto const r = declarator(inner, type, aParameter);
            result.name = r.name;
            result.type = r.type;
            // (the function suffix of the declarator that makes the identifier's type a function: the innermost's if it made one)
            result.function = r.function != nullptr ? r.function : (r.type->is_function() && inner->children.empty() ? outermostFunction : nullptr);
            if (result.function == nullptr && r.type->is_function() && type->is_function())
            {
                // ("int (f)(void)": the parenthesized declarator is the name)
                bool onlyName = true;
                for (auto* c : inner->children)
                    if (!c->is(construct::DeclName) && !c->is(construct::Attributes))
                        onlyName = false;
                if (onlyName)
                    result.function = outermostFunction;
            }
            return result;
        }
        result.type = type;
        result.function = type->is_function() ? outermostFunction : nullptr;
        return result;
    }

    void program::old_style_parameters(declarator_result& aResult, std::vector<node*> const& aDeclarations, std::vector<entity*>& aParameters)
    {
        // (an old-style definition (before C23): its parameters (an identifier list) of the types that the declarations before its body
        // give (int if none); its type: a prototype of their promoted types (the arguments of a call without a prototype), or a visible
        // prototype's (its arguments converted to the parameters' types on entry))
        auto const names = aResult.function->all(construct::OldParameter);
        std::map<std::string, std::pair<qtype, node*>> declared;
        for (auto* declaration : aDeclarations)
        {
            std::vector<node*> specs;
            std::vector<node*> declarators;
            for (auto* c : declaration->children)
                if (is_specifier(c->kind) || c->is(construct::Attributes))
                    specs.push_back(c);
                else if (c->is(construct::InitDeclarator))
                    declarators.push_back(c);
            auto const spec = specifiers(specs, declaration);
            if (spec.storage != storage_class::None && spec.storage != storage_class::Register)
                error(declaration, "storage class specified for parameter");
            for (auto* id : declarators)
            {
                if (id->child(1) != nullptr)
                    error(id, "parameter is initialized");
                auto const r = declarator(id->child(0), spec.type, true);
                if (r.name == nullptr)
                    error(id, "a parameter's declaration without a name");
                auto type = r.type;
                if (type->is_array())
                    type = qtype{ pointer_to(type->base), type.qualifiers };
                else if (type->is_function())
                    type = qtype{ pointer_to(type) };
                auto const name = name_of(r.name);
                if (std::none_of(names.begin(), names.end(), [&](node* n) { return n->text() == name; }))
                    error(r.name, "declaration for parameter '" + name + "' but no such parameter");
                if (declared.contains(name))
                    error(r.name, "redefinition of parameter '" + name + "'");
                declared[name] = { type, r.name };
            }
        }
        std::vector<parameter_type> types;
        for (auto* n : names)
        {
            auto const name = n->text();
            qtype type = int_type();
            node* where = n;
            if (auto const found = declared.find(name); found != declared.end())
            {
                type = found->second.first;
                where = found->second.second;
            }
            else
                warning(n, "type of '" + name + "' defaults to 'int'");
            parameter_type pt;
            pt.type = type->kind == type_kind::Float ? qtype{ basic(type_kind::Double) } : promote(type);
            pt.name = name;
            types.push_back(pt);
            auto& e = iEntities.emplace_back();
            e.kind = entity_kind::Object;
            e.name = name;
            e.type = type;
            e.parameter = true;
            e.decl = where;
            e.unit = iUnit;
            aParameters.push_back(&e);
        }
        if (auto* prior = lookup(name_of(aResult.name)); prior != nullptr && prior->kind == entity_kind::Function && prior->type->is_function() &&
            prior->type->prototype)
        {
            if (prior->type->parameters.size() != types.size() || prior->type->variadic)
                error(aResult.name, "the number of parameters of '" + name_of(aResult.name) + "' does not match its prototype's");
            for (std::size_t i = 0u; i < types.size(); ++i)
                types[i].type = prior->type->parameters[i].type;
        }
        aResult.type = qtype{ function_of(aResult.type->base, types, false, true), aResult.type.qualifiers };
    }

    std::vector<parameter_type> program::parameters_of(node* aFunctionSuffix, bool& aVariadic, bool& aPrototype, std::vector<entity*>* aEntities)
    {
        std::vector<parameter_type> result;
        aVariadic = false;
        aPrototype = true;
        auto const parameters = aFunctionSuffix->all(construct::Parameter);
        aVariadic = aFunctionSuffix->first(construct::Ellipsis) != nullptr;
        // (an empty list: C23's (void) in a definition; in a declaration, a function without a prototype (as before C23: calls of it 
        // are not checked, their arguments promoted), which a later declaration or definition with a prototype completes)
        if (parameters.empty() && !aVariadic)
            aPrototype = false;
        push_scope();
        for (auto* p : parameters)
        {
            std::vector<node*> specs;
            node* d = nullptr;
            for (auto* c : p->children)
            {
                if (is_specifier(c->kind) || c->is(construct::Attributes))
                    specs.push_back(c);
                else if (c->is(construct::Declarator) || c->is(construct::Abstract))
                    d = c;
            }
            auto const spec = specifiers(specs, p);
            if (spec.storage != storage_class::None && spec.storage != storage_class::Register)
                error(p, "storage class specified for parameter");
            auto const r = declarator(d, spec.type, true);
            qtype type = r.type;
            // (void alone: no parameters)
            if (type->is_void() && parameters.size() == 1u && r.name == nullptr && !aVariadic && type.qualifiers == 0u)
                break;
            if (type->is_void())
                error(p, "parameter has void type");
            // (adjusted: an array to a pointer to its element, a function to a pointer to it)
            if (type->is_array())
                type = qtype{ pointer_to(type->base), type.qualifiers };
            else if (type->is_function())
                type = qtype{ pointer_to(type) };
            parameter_type pt;
            pt.type = type;
            pt.name = r.name != nullptr ? name_of(r.name) : std::string{};
            result.push_back(pt);
            if (aEntities != nullptr)
            {
                auto& e = iEntities.emplace_back();
                e.kind = entity_kind::Object;
                e.name = pt.name;
                e.type = type;
                e.parameter = true;
                e.decl = r.name != nullptr ? r.name : p;
                e.unit = iUnit;
                e.storage = spec.storage;
                aEntities->push_back(&e);
            }
            else if (r.name != nullptr)
                iScopes.back().names[pt.name] = nullptr;
        }
        pop_scope();
        return result;
    }

    qtype program::type_name_type(node* aTypeName)
    {
        std::vector<node*> specs;
        node* d = nullptr;
        for (auto* c : aTypeName->children)
        {
            if (is_specifier(c->kind) || c->is(construct::Attributes))
                specs.push_back(c);
            else if (c->is(construct::Abstract))
                d = c;
        }
        auto const spec = specifiers(specs, aTypeName, false);
        return declarator(d, spec.type).type;
    }

    // --- declarations

    entity* program::declare(node* aName, qtype aType, declaration_specifiers const& aSpecifiers, bool aFileScope, bool aHasInitializer, node* aDecl)
    {
        auto const name = name_of(aName);
        auto& current = iScopes.back();
        auto const existingHere = current.names.find(name);
        entity* prior = existingHere != current.names.end() ? existingHere->second : nullptr;
        // typedef names
        if (aSpecifiers.storage == storage_class::Typedef)
        {
            if (prior != nullptr)
            {
                if (prior->kind != entity_kind::Typedef || !compatible(prior->type, aType))
                    error(aName, "conflicting types for '" + name + "'");
                return prior;
            }
            auto& e = iEntities.emplace_back();
            e.kind = entity_kind::Typedef;
            e.name = name;
            e.type = aType;
            e.decl = aName;
            e.unit = iUnit;
            current.names[name] = &e;
            return &e;
        }
        bool const isFunction = aType->is_function();
        // linkage
        enum linkage link = linkage::None;
        bool staticStorage = aFileScope;
        if (isFunction)
        {
            if (aSpecifiers.storage == storage_class::Static)
            {
                if (!aFileScope)
                    error(aName, "invalid storage class for function '" + name + "'");
                link = linkage::Internal;
            }
            else if (aSpecifiers.storage != storage_class::None && aSpecifiers.storage != storage_class::Extern)
                error(aName, "invalid storage class for function '" + name + "'");
            else
            {
                auto* visible = lookup(name);
                link = visible != nullptr && visible->linkage != linkage::None ? visible->linkage : linkage::External;
            }
        }
        else if (aFileScope)
        {
            if (aSpecifiers.storage == storage_class::Auto || aSpecifiers.storage == storage_class::Register)
                error(aName, "file-scope declaration of '" + name + "' specifies '" + (aSpecifiers.storage == storage_class::Auto ? "auto" : "register") + "'");
            if (aSpecifiers.storage == storage_class::Static || aSpecifiers.isConstexpr)
                link = linkage::Internal;
            else if (aSpecifiers.storage == storage_class::Extern)
            {
                auto* visible = lookup(name);
                link = visible != nullptr && visible->linkage != linkage::None ? visible->linkage : linkage::External;
            }
            else
                link = linkage::External;
        }
        else
        {
            if (aSpecifiers.storage == storage_class::Extern)
            {
                if (aHasInitializer)
                    error(aName, "'" + name + "' has both 'extern' and initializer");
                auto* visible = lookup(name);
                link = visible != nullptr && visible->linkage != linkage::None ? visible->linkage : linkage::External;
                staticStorage = true;
            }
            else
                staticStorage = aSpecifiers.storage == storage_class::Static || aSpecifiers.threadLocal;
        }
        // (an identifier with linkage: the same entity as its other declarations in the translation unit (internal) or the program
        // (external))
        entity* canonical = nullptr;
        if (link == linkage::External)
        {
            auto const found = iExternals.find(name);
            if (found != iExternals.end())
                canonical = found->second;
            // (an internal declaration of the name in this unit takes precedence (6.2.2(4)))
            auto const internal = iInternals.find(std::make_pair(iUnit, name));
            if (internal != iInternals.end())
                canonical = internal->second, link = linkage::Internal;
        }
        else if (link == linkage::Internal)
        {
            auto const found = iInternals.find(std::make_pair(iUnit, name));
            if (found != iInternals.end())
                canonical = found->second;
            else if (auto const external = iExternals.find(name); external != iExternals.end() && external->second->unit == iUnit &&
                aSpecifiers.storage == storage_class::Static)
                error(aName, "static declaration of '" + name + "' follows non-static declaration");
        }
        if (prior != nullptr && prior != canonical)
        {
            if (prior->kind == entity_kind::Typedef || prior->kind == entity_kind::EnumConstant || link == linkage::None || prior->linkage == linkage::None)
                error(aName, prior->kind == entity_kind::Object && prior->kind == entity_kind::Object && link == linkage::None && prior->linkage == linkage::None ?
                    "redefinition of '" + name + "'" : "'" + name + "' redeclared as different kind of symbol");
        }
        if (link == linkage::External)
        {
            // (a declaration of this unit's own that conflicts with another unit's (see below))
            auto const own = iUnitDeclarations.find(std::make_pair(iUnit, name));
            if (own != iUnitDeclarations.end())
                canonical = own->second;
        }
        if (canonical != nullptr && isFunction && canonical->kind == entity_kind::Function && canonical->unit != iUnit &&
            !compatible(canonical->type.unqualified(), aType.unqualified()) && iUnitDeclarations.find(std::make_pair(iUnit, name)) == iUnitDeclarations.end())
        {
            // (a function declared by another translation unit (e.g. the C library's, by its prelude) with a type that is not compatible: 
            // this unit's declarations are of its own entity (its calls are adapted to the function's type: see program::function_index))
            warning(aName, "conflicting types for '" + name + "'; have '" + type_name(aType) + "', '" + type_name(canonical->type) + "' in another translation unit");
            auto& e = iEntities.emplace_back();
            e.kind = entity_kind::Function;
            e.name = name;
            e.type = aType;
            e.storage = aSpecifiers.storage;
            e.linkage = linkage::External;
            e.decl = aName;
            e.unit = iUnit;
            auto& f = iFunctionInfos.emplace_back();
            f.ent = &e;
            e.function = &f;
            iFunctions.push_back(&e);
            iUnitDeclarations[std::make_pair(iUnit, name)] = &e;
            current.names[name] = &e;
            return &e;
        }
        if (canonical != nullptr)
        {
            if ((canonical->kind == entity_kind::Function) != isFunction)
                error(aName, "'" + name + "' redeclared as different kind of symbol");
            if (!compatible(canonical->type.unqualified(), aType.unqualified()) && !(canonical->function != nullptr && canonical->function->runtime))
                error(aName, "conflicting types for '" + name + "'; have '" + type_name(aType) + "', previous '" + type_name(canonical->type) + "'");
            canonical->type = composite(canonical->type, aType);
            if (aSpecifiers.noreturn)
                canonical->noreturn = true;
            if (aSpecifiers.isInline)
                canonical->isInline = true;
            if (aSpecifiers.threadLocal && !isFunction)
                canonical->threadLocal = true;
            current.names[name] = canonical;
            return canonical;
        }
        auto& e = iEntities.emplace_back();
        e.kind = isFunction ? entity_kind::Function : entity_kind::Object;
        e.name = name;
        e.type = aType;
        e.storage = aSpecifiers.storage;
        e.linkage = link;
        e.decl = aName;
        e.unit = iUnit;
        e.isInline = aSpecifiers.isInline;
        e.noreturn = aSpecifiers.noreturn;
        e.isConstexpr = aSpecifiers.isConstexpr;
        e.threadLocal = aSpecifiers.threadLocal && !isFunction;
        if (isFunction)
        {
            auto& f = iFunctionInfos.emplace_back();
            f.ent = &e;
            e.function = &f;
            iFunctions.push_back(&e);
        }
        else if (staticStorage)
        {
            e.where = object_storage::Static;
            iObjects.push_back(&e);
        }
        if (link == linkage::External)
            iExternals[name] = &e;
        else if (link == linkage::Internal)
            iInternals[std::make_pair(iUnit, name)] = &e;
        if (aDecl != nullptr)
            (void)aDecl;
        current.names[name] = &e;
        return &e;
    }

    void program::declaration(node* aDeclaration, bool aFileScope)
    {
        if (aDeclaration->child(0) != nullptr && aDeclaration->child(0)->is(construct::StaticAssert))
        {
            static_assertion(aDeclaration->child(0));
            return;
        }
        std::vector<node*> specs;
        std::vector<node*> declarators;
        for (auto* c : aDeclaration->children)
        {
            if (is_specifier(c->kind))
                specs.push_back(c);
            else if (c->is(construct::InitDeclarator))
                declarators.push_back(c);
            else if (c->is(construct::Attributes))
                specs.push_back(c);     // (before the specifiers too: they appertain to the declared entities)
        }
        if (std::none_of(specs.begin(), specs.end(), [](node* s) { return is_specifier(s->kind); }))
            return;     // (an attribute declaration)
        // (a declaration with only a structure, union or enumeration specifier declares its tag (in the current scope))
        if (declarators.empty())
        {
            for (auto* s : specs)
            {
                if (s->is(construct::Struct) && (s->source.back() == '}' || s->first(construct::Tag) != nullptr))
                {
                    if (s->source.back() != '}')
                    {
                        // ("struct S;": a new incomplete type in this scope)
                        struct_specifier(s, true);
                        return;
                    }
                }
                if (s->is(construct::Enum) && s->source.back() != '}' && s->first(construct::EnumType) != nullptr)
                {
                    enum_specifier(s, true);
                    return;
                }
            }
            auto const spec = specifiers(specs, aDeclaration);
            if (!spec.type->is_record() && spec.type->kind != type_kind::Enum)
                warning(aDeclaration, "declaration does not declare anything");
            return;
        }
        auto const spec = specifiers(specs, aDeclaration);
        for (auto* d : declarators)
        {
            auto* dn = d->child(0);
            node* init = d->child(1);
            auto r = declarator(dn, spec.type);
            if (r.name == nullptr)
                error(d, "declaration without a name");
            qtype type = r.type;
            if (spec.alignment != 0u && spec.storage == storage_class::Typedef)
                error(d, "alignment specified for typedef '" + name_of(r.name) + "'");
            // (the declarator's attributes, with the specifiers')
            auto dspec = spec;
            dspec.attributes.merge(declarator_attributes(dn));
            if (dspec.attributes.noreturn)
                dspec.noreturn = true;
            auto const alignment = std::max(dspec.alignment, dspec.attributes.aligned);
            if (dspec.attributes.aligned != 0u && spec.storage == storage_class::Typedef)
                warning(d, "attribute 'aligned' ignored on typedef '" + name_of(r.name) + "' (not supported)");
            if (spec.autoType)
            {
                // (C23: auto: the type of the initializer (after lvalue, array and function conversions))
                if (init == nullptr || init->is(construct::Braced))
                {
                    if (init != nullptr && init->size() == 1u && !init->child(0)->is(construct::Braced) && !init->child(0)->is(construct::Designated))
                        init = init->child(0);
                    else
                        error(d, "'auto' requires a plain identifier with an initializer");
                }
                expression(init);
                type = qualified(value_type(init), spec.type.qualifiers);
            }
            if (type->is_function() && init != nullptr)
                error(d, "function '" + name_of(r.name) + "' is initialized like a variable");
            if (spec.storage == storage_class::Typedef && init != nullptr)
                error(d, "typedef '" + name_of(r.name) + "' is initialized");
            if (init != nullptr && type->is_array() && !type->length && !type->vla && aFileScope)
            {
                // (an array of unknown size declared before with its size: of that size (the composite type))
                if (auto* prior = lookup(name_of(r.name)); prior != nullptr && prior->kind == entity_kind::Object && prior->type->is_array() &&
                    prior->type->length && compatible(prior->type.unqualified(), type.unqualified()))
                    type = prior->type;
            }
            if (init != nullptr && !type->is_function())
                type = initializer_type(type, init);
            if (type->is_variably_modified() && aFileScope && spec.storage != storage_class::Typedef)
                error(d, "variably modified '" + name_of(r.name) + "' at file scope");
            auto* e = declare(r.name, type, dspec, aFileScope, init != nullptr, d);
            r.name->ent = e;
            d->ent = e;
            if (e->kind == entity_kind::Function)
            {
                startup_function(e, dspec.attributes, d);
                if (r.function != nullptr && e->function != nullptr && e->function->parameters.empty() && false)
                    ;
                continue;
            }
            if (e->kind != entity_kind::Object)
                continue;
            if (e->where == object_storage::Static)
            {
                if (init != nullptr)
                {
                    if (e->defined && (aFileScope || e->linkage == linkage::None))
                        error(d, "redefinition of '" + e->name + "'");
                    e->defined = true;
                    e->initializer = init;
                    e->definition = d;
                    check_initializer(e->type, init, true);
                    if (spec.isConstexpr)
                    {
                        if (!init->is(construct::Braced) && e->type->is_scalar())
                            e->constantValue = evaluate(init, false);
                        if (!init->is(construct::Braced) && e->type->is_scalar() && !e->constantValue)
                            error(init, "constexpr initializer is not a constant expression");
                    }
                }
                else if (spec.storage != storage_class::Extern)
                {
                    if (aFileScope && !spec.threadLocal)
                        e->tentative = true;
                    else
                        e->defined = true;
                }
                if (alignment != 0u)
                    e->value = static_cast<std::int64_t>(alignment);
            }
            else
            {
                // (an object with automatic storage duration: one of the function's locals)
                if (!type->is_complete() && !(type->is_array() && type->vla))
                    error(d, "storage size of '" + e->name + "' isn't known");
                if (type->is_variably_modified() && iFunction != nullptr)
                    iFunction->function->hasVla = true;
                e->defined = true;
                e->storage = spec.storage;
                if (alignment != 0u)
                    e->value = static_cast<std::int64_t>(alignment);
                if (dspec.attributes.cleanup != nullptr)
                    cleanup_function(e, dspec.attributes.cleanup, d);
                if (iFunction != nullptr)
                    iFunction->function->locals.push_back(e);
                if (init != nullptr)
                {
                    e->initializer = init;
                    check_initializer(e->type, init, false);
                    if (spec.isConstexpr && !init->is(construct::Braced) && e->type->is_scalar())
                    {
                        e->constantValue = evaluate(init, false);
                        if (!e->constantValue)
                            error(init, "constexpr initializer is not a constant expression");
                    }
                }
                if (type->is_variably_modified())
                    for (auto const* t = type.type; t != nullptr && t->is_array(); t = t->base.type)
                        if (t->vlaSize != nullptr)
                            ;   // (its size expressions are evaluated where its declaration is reached)
            }
            // (static block scope objects: also kept with the function's (their initializers may name its other static objects))
            if (e->where == object_storage::Static && !aFileScope && iFunction != nullptr && e->linkage == linkage::None)
                iFunction->function->locals.push_back(e);
        }
    }

    void program::function_definition(node* aDefinition)
    {
        std::vector<node*> specs;
        node* d = nullptr;
        node* body = nullptr;
        std::vector<node*> oldDeclarations;     ///< (an old-style definition's parameters' declarations)
        for (auto* c : aDefinition->children)
        {
            if (is_specifier(c->kind) || (c->is(construct::Attributes) && d == nullptr))
                specs.push_back(c);
            else if (c->is(construct::Declarator))
                d = c;
            else if (c->is(construct::Declaration))
                oldDeclarations.push_back(c);
            else if (c->is(construct::Compound))
                body = c;
        }
        auto spec = specifiers(specs, aDefinition);
        spec.attributes.merge(declarator_attributes(d));
        if (spec.attributes.noreturn)
            spec.noreturn = true;
        auto r = declarator(d, spec.type);
        if (r.name == nullptr || !r.type->is_function() || r.function == nullptr)
            error(d, "a function definition's declarator must declare a function");
        std::vector<entity*> oldParameters;
        bool const oldStyle = r.function->first(construct::OldParameter) != nullptr;
        if (oldStyle)
            old_style_parameters(r, oldDeclarations, oldParameters);
        else if (!oldDeclarations.empty())
            error(oldDeclarations[0], "declarations before a function's body without an identifier list");
        if (!r.type->prototype)
            r.type = qtype{ function_of(r.type->base, {}, false, true), r.type.qualifiers };
        if (spec.storage == storage_class::Typedef)
            error(d, "function definition declared 'typedef'");
        auto* e = declare(r.name, r.type, spec, true, false, d);
        r.name->ent = e;
        if (e->defined)
            error(r.name, "redefinition of '" + e->name + "'");
        e->defined = true;
        e->definition = aDefinition;
        if (e->function == nullptr)
            error(r.name, "'" + e->name + "' is not a function");
        startup_function(e, spec.attributes, d);
        auto* f = e->function;
        f->body = body;
        f->runtime = false;
        // (its parameters: in the scope of its body)
        bool variadic = false;
        bool prototype = true;
        std::vector<entity*> parameters;
        auto const types = parameters_of(r.function, variadic, prototype, &parameters);
        (void)types;
        if (oldStyle)
            parameters = oldParameters;
        auto* const savedFunction = iFunction;
        iFunction = e;
        f->parameters = parameters;
        push_scope();
        for (auto* p : parameters)
        {
            if (!p->type->is_complete() && !p->type->is_array())
                error(p->decl, "parameter '" + p->name + "' has incomplete type");
            if (!p->name.empty())
            {
                if (iScopes.back().names.contains(p->name))
                    error(p->decl, "redefinition of parameter '" + p->name + "'");
                iScopes.back().names[p->name] = p;
            }
            p->defined = true;
        }
        // (__func__: a static array of char holding the function's name)
        {
            auto& fn = iEntities.emplace_back();
            fn.kind = entity_kind::Object;
            fn.name = "__func__";
            fn.type = qtype{ array_of(qtype{ basic(type_kind::Char), QConst }, e->name.size() + 1u) };
            fn.where = object_storage::Static;
            fn.defined = true;
            fn.decl = r.name;
            fn.unit = iUnit;
            fn.value = 0;
            fn.constantValue = constant{};
            fn.constantValue->k = constant::kind::None;
            fn.initializer = nullptr;
            fn.storage = storage_class::Static;
            iObjects.push_back(&fn);
            iScopes.back().names["__func__"] = &fn;
            fn.definition = nullptr;
            fn.address = 0u;
            fn.tentative = false;
            fn.local = 0xFFFFFFFFu;     // (marks __func__: its initializer is the function's name)
            fn.canonical = nullptr;
            fn.function = nullptr;
            fn.isConstexpr = false;
            fn.used = false;
            fn.addressTaken = false;
            fn.parameter = false;
            fn.initializer = nullptr;
            fn.storage = storage_class::Static;
            f->locals.push_back(&fn);
            iSyntheticNodes.emplace_back();
            auto& literalNode = iSyntheticNodes.back();
            literalNode.kind = construct::Strings;
            literalNode.unit = iUnit;
            literalNode.source = std::string_view{ e->name };
            fn.definition = &literalNode;
        }
        iLabels.clear();
        iLabelAddresses.clear();
        iGotos.clear();
        iBreakables.clear();
        iSwitches.clear();
        compound(body, false);
        // (gotos' labels)
        for (auto* g : iGotos)
        {
            auto const label = name_of(g->child(0));
            auto const found = iLabels.find(label);
            if (found == iLabels.end())
                error(g, "label '" + label + "' used but not defined");
            g->target = found->second;
        }
        // (labels' addresses: the labels' numbers (from 1) in the function's list of them, null-based addresses)
        for (auto* a : iLabelAddresses)
        {
            auto const label = name_of(a->child(0));
            auto const found = iLabels.find(label);
            if (found == iLabels.end())
                error(a, "label '" + label + "' used but not defined");
            a->target = found->second;
            auto& values = e->function->labelValues;
            auto position = std::find(values.begin(), values.end(), found->second);
            if (position == values.end())
                position = values.insert(values.end(), found->second);
            constant v;
            v.k = constant::kind::Address;
            v.integer = static_cast<std::uint64_t>(position - values.begin()) + 1u;
            a->value = v;
        }
        iLabelAddresses.clear();
        pop_scope();
        iFunction = savedFunction;
    }

    // --- initializers

    qtype program::initializer_type(qtype aType, node* aInitializer)
    {
        // (an array of unknown size: its size from the initializer (6.7.11(22)))
        if (!aType->is_array() || aType->length || aType->vla)
            return aType;
        auto const* element = aType->base.type;
        if (!aInitializer->is(construct::Braced))
        {
            if (aInitializer->is(construct::Strings) || (aInitializer->is(construct::Paren) && aInitializer->child(0)->is(construct::Strings)))
            {
                auto* s = aInitializer->is(construct::Paren) ? aInitializer->child(0) : aInitializer;
                expression(s);
                return qtype{ array_of(aType->base, s->type->length), aType.qualifiers };
            }
            return aType;
        }
        // (a braced list: its highest index (designators included) plus one; a string literal as the only item initializes a char array)
        if (aInitializer->size() == 1u && element->is_integer() && size_of(qtype{ element }) <= 4u)
        {
            auto* only = aInitializer->child(0);
            if (only->is(construct::Strings))
            {
                expression(only);
                return qtype{ array_of(aType->base, only->type->length), aType.qualifiers };
            }
        }
        std::uint64_t index = 0u;
        std::uint64_t count = 0u;
        auto const elementType = aType->base;
        // (the number of initializers an element takes from the list when it is not braced: its scalars' count (brace elision))
        std::function<std::uint64_t(qtype)> scalars = [&](qtype aT) -> std::uint64_t
        {
            if (aT->is_array())
                return (aT->length ? *aT->length : 0u) * scalars(aT->base);
            if (aT->is_record() && aT->record != nullptr)
            {
                if (aT->record->isUnion)
                    return aT->record->members.empty() ? 1u : scalars(aT->record->members[0].type);
                std::uint64_t n = 0u;
                for (auto const& m : aT->record->members)
                    if (!(m.bitWidth && m.name.empty()))
                        n += scalars(m.type);
                return n;
            }
            return 1u;
        };
        std::uint64_t const per = std::max<std::uint64_t>(1u, scalars(elementType));
        std::uint64_t pending = 0u;     // (scalars taken by the current element so far (brace elision))
        for (auto* item : aInitializer->children)
        {
            if (item->is(construct::Designated))
            {
                auto* designator = item->child(0);
                if (!designator->is(construct::DesignatorIndex))
                    error(designator, "member designator in an array initializer");
                // (a range (GNU): its last)
                auto* last = designator->child(designator->size() - 1u);
                expression(designator->child(0));
                expression(last);
                index = static_cast<std::uint64_t>(integer_constant(last, "array index in initializer"));
                pending = 0u;
                ++index;
                count = std::max(count, index);
                continue;
            }
            // (an expression item initializes an element (or, with brace elision, part of one); a string literal for an array of char, 
            // a braced item, or an expression of the element's (record) type is a whole element)
            bool whole = item->is(construct::Braced) || per == 1u;
            if (!whole && (elementType->is_record() || elementType->is_array()))
            {
                if (item->is(construct::Strings) && elementType->is_array())
                    whole = true;
                else if (!item->is(construct::Strings))
                {
                    expression(item);
                    if (compatible(value_type(item).unqualified(), elementType.unqualified(), true))
                        whole = true;
                }
            }
            if (whole)
            {
                if (pending != 0u)
                {
                    ++index;
                    pending = 0u;
                }
                ++index;
            }
            else
            {
                if (++pending == per)
                {
                    pending = 0u;
                    ++index;
                }
            }
            count = std::max(count, index + (pending != 0u ? 1u : 0u));
        }
        return qtype{ array_of(aType->base, count), aType.qualifiers };
    }

    void program::check_initializer(qtype aType, node* aInitializer, bool aStatic)
    {
        // (the initializer's expressions analysed; a static object's must be constant expressions (checked when the data is made);
        // a scalar's (or a record's from an expression of its type) assignment rules)
        if (aType->is_variably_modified() && !(aInitializer->is(construct::Braced) && aInitializer->children.empty()))
            error(aInitializer, "variable-sized object may not be initialized except with an empty initializer");
        if (aInitializer->is(construct::Braced))
        {
            std::function<void(node*)> analyse = [&](node* aList)
            {
                for (auto* item : aList->children)
                {
                    if (item->is(construct::Designated))
                    {
                        for (auto* c : item->children)
                        {
                            if (c->is(construct::DesignatorIndex))
                                for (auto* index : c->children)
                                    expression(index);
                            else if (c->is(construct::Braced))
                                analyse(c);
                            else if (!c->is(construct::DesignatorMember))
                                expression(c);
                        }
                    }
                    else if (item->is(construct::Braced))
                        analyse(item);
                    else
                        expression(item);
                }
            };
            analyse(aInitializer);
            if (aType->is_scalar() && aInitializer->size() == 1u && !aInitializer->child(0)->is(construct::Braced) && !aInitializer->child(0)->is(construct::Designated))
                check_assignable(aType, aInitializer->child(0), aInitializer, "initialization");
            (void)aStatic;
            return;
        }
        expression(aInitializer);
        if (aType->is_array())
        {
            if (!aInitializer->is(construct::Strings) && !(aInitializer->is(construct::Paren) && aInitializer->child(0)->is(construct::Strings)))
                error(aInitializer, "array initializer must be an initializer list or a string literal");
            return;
        }
        if (aType->is_record())
        {
            if (!compatible(value_type(aInitializer).unqualified(), aType.unqualified(), true))
                error(aInitializer, "invalid initializer for '" + type_name(aType) + "'");
            return;
        }
        check_assignable(aType, aInitializer, aInitializer, "initialization");
    }
}
