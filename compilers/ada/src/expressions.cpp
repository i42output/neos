/*
  expressions.cpp

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

// Ada: the resolution of expressions (operators, literals, aggregates, attributes, conditional expressions ...) and the
// evaluation of static expressions

#include <neolib/neolib.hpp>
#include <cstdio>
#include <set>
#include <cstdlib>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cmath>
#include "program.hpp"

namespace neos::compilers::ada
{
    bool is_operator_symbol(node const* aNode)
    {
        // (a string literal that is an operator symbol: "and", "+" ... (case insensitive: the source is folded))
        static std::set<std::string> const sOperators = { "and", "or", "xor", "=", "/=", "<", "<=", ">", ">=", "+", "-", "&", "*", "/",
            "mod", "rem", "**", "abs", "not" };
        auto text = std::string{ aNode->source };
        if (text.size() < 2u)
            return false;
        text = text.substr(1u, text.size() - 2u);
        for (auto& c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return sOperators.find(text) != sOperators.end();
    }

    namespace
    {
        std::string operator_designator(construct aKind)
        {
            switch (aKind)
            {
            case construct::And: return "and";
            case construct::Or: return "or";
            case construct::Xor: return "xor";
            case construct::Eq: return "=";
            case construct::Ne: return "/=";
            case construct::Lt: return "<";
            case construct::Le: return "<=";
            case construct::Gt: return ">";
            case construct::Ge: return ">=";
            case construct::Add: return "+";
            case construct::Subtract: return "-";
            case construct::Concat: return "&";
            case construct::Multiply: return "*";
            case construct::Divide: return "/";
            case construct::Mod: return "mod";
            case construct::Rem: return "rem";
            case construct::Power: return "**";
            case construct::Plus: return "+";
            case construct::Negate: return "-";
            case construct::Abs: return "abs";
            case construct::Not: return "not";
            default: return {};
            }
        }

        bool is_operator(construct aKind)
        {
            return !operator_designator(aKind).empty();
        }

        bool is_real_literal(std::string_view aText)
        {
            auto const sharp = aText.find_first_of("#:");
            if (sharp != std::string_view::npos)
            {
                auto const close = aText.find(aText[sharp], sharp + 1u);
                return aText.substr(sharp, close - sharp).find('.') != std::string_view::npos;
            }
            return aText.find('.') != std::string_view::npos;
        }

        // (a string literal's characters (as written: decoded from UTF-8, its doubled quotes single))
        std::u32string string_value(std::string_view aLiteral)
        {
            std::u32string result;
            if (aLiteral.size() < 2u)
                return result;
            char const quote = aLiteral.front();
            auto const body = aLiteral.substr(1u, aLiteral.size() - 2u);
            for (std::size_t i = 0u; i < body.size(); )
            {
                unsigned char c = static_cast<unsigned char>(body[i]);
                if (c == static_cast<unsigned char>(quote) && i + 1u < body.size() && body[i + 1u] == quote)
                {
                    result += static_cast<char32_t>(c);
                    i += 2u;
                    continue;
                }
                char32_t cp = c;
                std::size_t n = 1u;
                if (c >= 0xF0u && i + 3u < body.size() + 1u) { cp = c & 0x07u; n = 4u; }
                else if (c >= 0xE0u) { cp = c & 0x0Fu; n = 3u; }
                else if (c >= 0xC0u) { cp = c & 0x1Fu; n = 2u; }
                for (std::size_t k = 1u; k < n && i + k < body.size(); ++k)
                    cp = (cp << 6u) | (static_cast<unsigned char>(body[i + k]) & 0x3Fu);
                result += cp;
                i += n;
            }
            return result;
        }

        char32_t character_value(std::string_view aLiteral)
        {
            auto const s = string_value(aLiteral);
            return s.empty() ? U'\0' : s.front();
        }
    }

    std::u32string literal_string(node const* aNode)
    {
        return string_value(aNode->file != nullptr ? aNode->file->original_of(aNode->source) : aNode->source);
    }

    char32_t literal_character(node const* aNode)
    {
        return character_value(aNode->file != nullptr ? aNode->file->original_of(aNode->source) : aNode->source);
    }

    // --- interpretations (bottom up: the types an expression could have)

    namespace
    {
        struct resolving_region
        {
            region*& where;
            region* saved;
            resolving_region(region*& aWhere, region* aRegion) : where{ aWhere }, saved{ aWhere } { where = aRegion; }
            ~resolving_region() { where = saved; }
        };
    }

    std::vector<interpretation> program::interpretations(node* aExpression, region* aRegion)
    {
        // (cached: an expression's operands' interpretations are needed by each of its candidates' (else exponential))
        if (aExpression == nullptr)
            return {};
        resolving_region const resolving{ iResolving, aRegion };
        if (auto existing = iInterpretations.find(aExpression); existing != iInterpretations.end() && existing->second.where == aRegion &&
            existing->second.generation == analysis_generation())
            return existing->second.result;
        auto const generation = analysis_generation();
        auto result = compute_interpretations(aExpression, aRegion);
        // (a reference type's value: its designated type's too (implicitly dereferenced))
        for (std::size_t i = 0u, n = result.size(); i < n; ++i)
            if (result[i].type != nullptr && (result[i].type->view()->kind == type_kind::Record || result[i].type->view()->kind == type_kind::Private))
                if (auto* d = implicit_dereference(result[i].type); d != nullptr && d != result[i].type)
                    result.push_back(interpretation{ d, result[i].ent });
        if (generation == analysis_generation())
            iInterpretations[aExpression] = cached_interpretations{ aRegion, generation, result };
        return result;
    }

    std::vector<interpretation> program::compute_interpretations(node* aExpression, region* aRegion)
    {
        if (aExpression == nullptr)
            return {};
        switch (aExpression->kind)
        {
        case construct::Number:
            return { interpretation{ is_real_literal(aExpression->source) ? iUniversalReal : iUniversalInteger, nullptr } };
        case construct::Null:
            return { interpretation{ iUniversalAccess, nullptr } };
        case construct::Identifier:
        case construct::Selected:
        case construct::Operator:
        case construct::Character:
            {
                std::vector<interpretation> result;
                // (a string literal that is not an operator symbol: only a string)
                bool const operatorSymbol = !aExpression->is(construct::Operator) || is_operator_symbol(aExpression);
                if (operatorSymbol)
                    try
                    {
                        result = name_interpretations(aExpression, aRegion);
                    }
                    catch (compile_error const&)
                    {
                    }
                if (aExpression->is(construct::Operator))
                    result.push_back(interpretation{ iStringLiteral, nullptr });
                if (aExpression->is(construct::Character))
                {
                    bool userDefined = false;
                    for (auto const& r : result)
                        userDefined = userDefined || r.ent != nullptr;
                    result.push_back(interpretation{ iCharacterLiteral, nullptr });
                    (void)userDefined;
                }
                return result;
            }
        case construct::Parenthesized:
            return interpretations(aExpression->child(0), aRegion);
        case construct::Qualified:
            {
                try
                {
                    auto* t = subtype_mark(aExpression->child(0), aRegion);
                    return { interpretation{ t, nullptr } };
                }
                catch (compile_error const&)
                {
                    return {};
                }
            }
        case construct::Aggregate:
        case construct::AggregateBracket:
        case construct::AggregateExtension:
        case construct::AggregateDelta:
        case construct::AggregateNullRecord:
            if (aExpression->is(construct::AggregateDelta) || aExpression->is(construct::AggregateExtension))
            {
                auto const base = interpretations(aExpression->child(0), aRegion);
                if (aExpression->is(construct::AggregateDelta))
                    return base;
            }
            return { interpretation{ aExpression->is(construct::AggregateBracket) ? iBracketAggregateType : iAggregateType, nullptr } };
        case construct::Allocator:
            {
                auto* t = new_type(type_kind::Access, {});
                t->anonymous_access = true;
                t->size = 8u;
                try
                {
                    auto* indication = aExpression->children.back();
                    if (indication->is(construct::SubtypeIndication) && indication->size() == 1u && indication->child(0)->is(construct::Qualified))
                        t->designated = subtype_mark(indication->child(0)->child(0), aRegion);
                    else if (indication->is(construct::Qualified))
                        t->designated = subtype_mark(indication->child(0), aRegion);
                    else
                        t->designated = subtype_indication(indication, aRegion);
                }
                catch (compile_error const&)
                {
                    t->designated = nullptr;
                }
                return { interpretation{ t, nullptr } };
            }
        case construct::AndThen:
        case construct::OrElse:
            {
                // (the boolean types that both operands can have)
                auto const left = interpretations(aExpression->child(0), aRegion);
                auto const right = interpretations(aExpression->child(1), aRegion);
                std::vector<interpretation> result;
                for (auto const& l : left)
                    if (l.type != nullptr && l.type->view()->base_type()->boolean)
                    {
                        bool both = false;
                        for (auto const& r : right)
                            both = both || r.type == nullptr || same_type(r.type, l.type);
                        bool seen = false;
                        for (auto const& x : result)
                            seen = seen || same_type(x.type, l.type);
                        if (both && !seen)
                            result.push_back(interpretation{ l.type->base_type(), nullptr });
                    }
                if (result.empty())
                    result.push_back(interpretation{ iBoolean, nullptr });
                return result;
            }
        case construct::In:
        case construct::Quantified:
            return { interpretation{ iBoolean, nullptr } };
        case construct::IfExpression:
        case construct::CaseExpression:
            {
                // (the types of its dependent expressions: those the first can have (that are not universal) that all the others can
                // have too (else those the first can have), or those of the others)
                std::vector<node*> dependents;
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                {
                    auto* c = aExpression->child(i);
                    if (aExpression->is(construct::IfExpression))
                        dependents.push_back(c->is(construct::Elsif) ? c->child(1) : c->is(construct::Else) ? c->child(0) : c);
                    else
                        dependents.push_back(c->children.back());
                }
                std::vector<interpretation> result;
                for (std::size_t i = 0u; i < dependents.size() && result.empty(); ++i)
                    for (auto const& r : interpretations(dependents[i], aRegion))
                        if (r.type == nullptr || !r.type->is_universal())
                            result.push_back(r);
                if (result.empty())
                    return dependents.empty() ? result : interpretations(dependents.front(), aRegion);
                std::vector<interpretation> common;
                for (auto const& r : result)
                {
                    bool all = r.type != nullptr;
                    for (auto* d : dependents)
                    {
                        bool any = false;
                        for (auto const& o : interpretations(d, aRegion))
                            any = any || o.type == nullptr || covers(r.type, o.type);
                        all = all && any;
                    }
                    if (all)
                        common.push_back(r);
                }
                return common.empty() ? result : common;
            }
        case construct::DeclareExpression:
            return interpretations(aExpression->children.back(), aRegion);
        case construct::RaiseExpression:
            return { interpretation{ nullptr, nullptr } };
        case construct::All:
            {
                std::vector<interpretation> result;
                for (auto const& p : interpretations(aExpression->child(0), aRegion))
                {
                    if (p.type != nullptr && p.type->view()->kind == type_kind::Access)
                        if (auto* d = designated_of(p.type))
                            result.push_back(interpretation{ d, nullptr });
                    // (an access-to-subprogram value's subprogram: called (by an enclosing Apply), or a parameterless function's result)
                    if (p.type != nullptr && p.type->view()->kind == type_kind::AccessSubprogram && p.type->view()->profile != nullptr)
                    {
                        auto* profile = p.type->view()->profile;
                        if (aExpression->parent != nullptr && aExpression->parent->is(construct::Apply) && aExpression->parent->child(0) == aExpression)
                            result.push_back(interpretation{ p.type, nullptr });
                        else if (profile->result != nullptr && profile->parameters.empty())
                            result.push_back(interpretation{ profile->result, nullptr });
                    }
                }
                return result;
            }
        case construct::Target:
            return { interpretation{ iTargetType, nullptr } };
        case construct::Attribute:
            {
                if (indexed_attribute(aExpression))
                    return interpretations(aExpression, aRegion);
                try
                {
                    if (auto* t = attribute_type(aExpression, aRegion))
                        return { interpretation{ t, nullptr } };
                }
                catch (compile_error const&)
                {
                }
                return {};
            }
        case construct::Apply:
            {
                std::vector<interpretation> result;
                auto* prefix = aExpression->child(0);
                std::vector<node*> arguments(aExpression->children.begin() + 1, aExpression->children.end());
                std::vector<entity*> entities;
                try
                {
                    if (prefix->is(construct::Identifier) || prefix->is(construct::Selected) || prefix->is(construct::Operator))
                        entities = prefix_entities(prefix, aRegion);
                }
                catch (compile_error const&)
                {
                }
                node* operatorSymbol = prefix->is(construct::Operator) ? prefix :
                    prefix->is(construct::Selected) && prefix->child(1)->is(construct::Operator) ? prefix->child(1) : nullptr;
                if (operatorSymbol != nullptr && is_operator_symbol(operatorSymbol))
                {
                    // ("op" (L, R): the operators of the operands' types, as for an operator (see resolve_apply))
                    std::vector<node*> operands;
                    for (auto* a : arguments)
                        operands.push_back(a->is(construct::Association) ? a->children.back() : a);
                    auto designator = name_text(operatorSymbol->source);
                    if (designator.size() >= 2u && (designator.front() == '\"' || designator.front() == '%'))
                        designator = designator.substr(1u, designator.size() - 2u);
                    // (P."op": only the operators declared in P (4.1.3(10)))
                    auto* declaredIn = selected_operator_scope(prefix, aRegion);
                    for (auto* op : operator_candidates(designator, operands, aRegion))
                        if ((declaredIn == nullptr || op->scope == declaredIn) && std::find(entities.begin(), entities.end(), op) == entities.end())
                            entities.push_back(op);
                    // (operands that are string literals: String's "&" (e.g. "&" ("ab", "cd") (3)))
                    bool literal = false;
                    for (auto* operand : operands)
                        for (auto const& i : interpretations(operand, aRegion))
                            literal = literal || (i.type != nullptr && (i.type->kind == type_kind::String || i.type->kind == type_kind::CharacterLiteral));
                    if (literal)
                        for (auto* op : operators_of(iString))
                            if (op->name == designator && op->sub->parameters.size() == operands.size() &&
                                std::find(entities.begin(), entities.end(), op) == entities.end())
                                entities.push_back(op);
                }
                if (entities.size() == 1u && !entities.front()->overloadable() && !entities.front()->is_type())
                    entities.clear();
                if (!entities.empty())
                {
                    for (auto* e : entities)
                    {
                        if (e->is_type())
                            return { interpretation{ e->type, nullptr } };
                        if (e->kind == entity_kind::EnumerationLiteral)
                            continue;
                        if (e->sub != nullptr && match_call(e, arguments, nullptr, aRegion, nullptr, true))
                            result.push_back(interpretation{ e->sub->result != nullptr ? e->sub->result : iVoid, e });
                        else if (e->kind == entity_kind::Function && e->sub->result != nullptr)
                        {
                            // (a parameterless function's result indexed)
                            bool callable = true;
                            for (auto const& p : e->sub->parameters)
                                callable = callable && p.default_expression != nullptr;
                            auto* rt = e->sub->result->view();
                            if (callable && rt->kind == type_kind::Array && indexed_result_fits(e->sub->result, arguments, nullptr, aRegion))
                                result.push_back(interpretation{ arguments.size() == 1u && is_discrete_range(arguments[0], aRegion) ? e->sub->result : rt->component, nullptr });
                            else if (callable && rt->kind == type_kind::AccessSubprogram && rt->profile != nullptr)
                                result.push_back(interpretation{ rt->profile->result != nullptr ? rt->profile->result : iVoid, nullptr });
                        }
                    }
                    if (!result.empty())
                        return result;
                }
                // (a prefixed view call)
                if (prefix->is(construct::Selected) && entities.empty())
                {
                    auto const name = name_text(prefix->child(1)->source);
                    for (auto const& p : interpretations(prefix->child(0), aRegion))
                    {
                        if (p.type == nullptr)
                            continue;
                        auto* t = p.type->view();
                        if (t->kind == type_kind::Access && designated_of(t) != nullptr)
                            t = designated_of(t)->view();
                        if ((t->kind == type_kind::Task || t->kind == type_kind::Protected) && t->unit != nullptr && t->unit->inner != nullptr)
                        {
                            auto range = t->unit->inner->names.equal_range(name);
                            for (auto i = range.first; i != range.second; ++i)
                                if (auto* op = i->second; op->sub != nullptr)
                                {
                                    bool parameterless = op->kind == entity_kind::Function && op->sub->result != nullptr;
                                    for (auto const& f : op->sub->parameters)
                                        parameterless = parameterless && f.default_expression != nullptr;
                                    if (parameterless && op->sub->result->view()->kind == type_kind::Array && !match_call(op, arguments, nullptr, aRegion, nullptr, true))
                                    {
                                        // (a parameterless protected function's result indexed or sliced)
                                        auto* rt = op->sub->result->view();
                                        result.push_back(interpretation{ arguments.size() == 1u && is_discrete_range(arguments[0], aRegion) ? op->sub->result : rt->component, nullptr });
                                    }
                                    else
                                        result.push_back(interpretation{ op->sub->result != nullptr ? op->sub->result : iVoid, op });
                                }
                        }
                        for (auto* op : prefixed_candidates(t, name))
                            if (match_call(op, arguments, prefix->child(0), aRegion, nullptr, true))
                                result.push_back(interpretation{ op->sub->result != nullptr ? op->sub->result : iVoid, op });
                    }
                    if (!result.empty())
                        return result;
                }
                // (indexing, slicing, a call through an access value)
                for (auto const& p : interpretations(prefix, aRegion))
                {
                    if (p.type == nullptr)
                        continue;
                    auto* t = p.type->view();
                    if (t->kind == type_kind::Access && designated_of(t) != nullptr)
                        t = designated_of(t)->view();
                    if (t->kind == type_kind::Array)
                    {
                        if (arguments.size() == 1u && t->indexes.size() == 1u && is_discrete_range(arguments[0], aRegion))
                            result.push_back(interpretation{ t, nullptr });
                        else
                            result.push_back(interpretation{ t->component, nullptr });
                    }
                    else if (t->kind == type_kind::AccessSubprogram && t->profile != nullptr)
                        result.push_back(interpretation{ t->profile->result != nullptr ? t->profile->result : iVoid, nullptr });
                    else
                    {
                        // (a generalized indexing: its Constant_Indexing or Variable_Indexing functions' results (references))
                        for (auto const* aspect : { "variable_indexing", "constant_indexing" })
                            if (auto* function = aspect_subprogram(p.type, aspect); function != nullptr && function->scope != nullptr)
                                for (auto* c : indexing_candidates(p.type, function))
                                    if (c->sub->result != nullptr && match_call(c, arguments, prefix, aRegion, nullptr, true))
                                        result.push_back(interpretation{ c->sub->result, c });
                    }
                }
                return result;
            }
        default:
            break;
        }
        if (is_operator(aExpression->kind))
        {
            std::vector<node*> operands(aExpression->children.begin(), aExpression->children.end());
            std::vector<interpretation> result;
            // (a root numeric operator: only for universal operands (as when resolved))
            auto const universal_operands = [&](entity* aOperator)
            {
                for (std::size_t k = 0u; k < operands.size() && k < aOperator->sub->parameters.size(); ++k)
                {
                    auto* formal = aOperator->sub->parameters[k].type;
                    if (formal == nullptr || !formal->is_universal())
                        continue;
                    bool universal = false;
                    for (auto const& i : interpretations(operands[k], aRegion))
                        universal = universal || i.type == nullptr || i.type->is_universal();
                    if (!universal)
                        return false;
                }
                return true;
            };
            for (auto* op : operator_candidates(operator_designator(aExpression->kind), operands, aRegion))
            {
                if (op->sub->op != intrinsic::None && op->sub->operator_type != nullptr &&
                    (op->sub->operator_type->kind == type_kind::UniversalInteger || op->sub->operator_type->kind == type_kind::UniversalReal) &&
                    !universal_operands(op))
                    continue;
                if (match_call(op, operands, nullptr, aRegion, nullptr, true))
                {
                    bool seen = false;
                    for (auto const& r : result)
                        seen = seen || r.type == op->sub->result;
                    if (!seen)
                        result.push_back(interpretation{ op->sub->result, op });
                }
            }
            // (an operand that is a string literal, a character literal or an aggregate: the operators of the context's types (e.g. 
            // the predefined "&" of the expected string type) are candidates too (added by resolve_operator): any type)
            // (with no other candidate: when all the operands are such (e.g. "ab" & "cd"))
            bool allLiterals = !operands.empty();
            for (auto* operand : operands)
            {
                bool literal = false;
                for (auto const& i : interpretations(operand, aRegion))
                    literal = literal || (i.type != nullptr && (i.type->kind == type_kind::String || i.type->kind == type_kind::Aggregate || i.type->kind == type_kind::CharacterLiteral));
                allLiterals = allLiterals && literal;
            }
            // ("&" of string and character literals: of any string type (as a string literal is))
            if (result.empty() && allLiterals && aExpression->is(construct::Concat))
                for (auto* operand : operands)
                    for (auto const& i : interpretations(operand, aRegion))
                        if (i.type != nullptr && i.type->kind == type_kind::String)
                            return { interpretation{ i.type, nullptr } };
            // (only when all the operands are such: an operand of a type decides the operator's (its type's operators are candidates))
            if (allLiterals)
                for (auto* operand : operands)
                    for (auto const& i : interpretations(operand, aRegion))
                        if (i.type != nullptr && (i.type->kind == type_kind::String || i.type->kind == type_kind::Aggregate || i.type->kind == type_kind::CharacterLiteral))
                        {
                            result.push_back(interpretation{ nullptr, nullptr });
                            return result;
                        }
            return result;
        }
        return {};
    }

    // --- resolution (top down: the expected type)

    ada_type* program::condition(node* aCondition, region* aRegion)
    {
        // (a condition is of any boolean type (RM 4.5.7(4)): the boolean type of its interpretations if unique)
        ada_type* t = nullptr;
        for (auto const& o : interpretations(aCondition, aRegion))
            if (o.type != nullptr && o.type->view()->boolean)
            {
                if (t != nullptr && t->base_type() != o.type->base_type())
                {
                    t = nullptr;
                    break;
                }
                t = o.type;
            }
        return resolve(aCondition, t != nullptr ? t->base_type() : iBoolean, aRegion);
    }

    ada_type* program::resolve_in_class(node* aExpression, region* aRegion, bool (*aInClass)(ada_type const*))
    {
        // (an expression expected to be of any type of a class (e.g. any integer type, 3.5.4(5)): of its interpretation in the 
        // class (a specific one rather than a universal one), else as it is)
        ada_type* t = nullptr;
        for (auto const& o : interpretations(aExpression, aRegion))
            if (o.type != nullptr && aInClass(o.type->view()->base_type()->view()) && (t == nullptr || t->is_universal()))
                t = o.type;
        return resolve(aExpression, t != nullptr ? t->base_type() : nullptr, aRegion);
    }

    ada_type* program::resolve(node* aExpression, ada_type* aExpected, region* aRegion)
    {
        if (aExpression == nullptr)
            return nullptr;
        resolving_region const resolving{ iResolving, aRegion };
        ++analysis_generation();
        ada_type* result = nullptr;
        switch (aExpression->kind)
        {
        case construct::Number:
            {
                auto* universal = is_real_literal(aExpression->source) ? iUniversalReal : iUniversalInteger;
                if (aExpected != nullptr && !covers(aExpected, universal))
                    error(aExpression, "numeric literal is not of the expected type");
                result = aExpected != nullptr && !aExpected->is_universal() ? aExpected : universal;
                set_meaning(aExpression, meaning::Value);
                break;
            }
        case construct::Null:
            if (aExpected != nullptr && !aExpected->view()->is_access())
                error(aExpression, "null is not of the expected type");
            result = aExpected != nullptr ? aExpected : iUniversalAccess;
            set_meaning(aExpression, meaning::Value);
            break;
        case construct::Identifier:
        case construct::Selected:
        case construct::Operator:
        case construct::Character:
            result = resolve_name(aExpression, aExpected, aRegion);
            break;
        case construct::Apply:
            result = resolve_apply(aExpression, aExpected, aRegion);
            break;
        case construct::Attribute:
            result = resolve_attribute(aExpression, aExpected, aRegion);
            break;
        case construct::Parenthesized:
            result = resolve(aExpression->child(0), aExpected, aRegion);
            set_meaning(aExpression, meaning::Value);
            break;
        case construct::Qualified:
            {
                auto* t = subtype_mark(aExpression->child(0), aRegion);
                resolve(aExpression->child(1), t, aRegion);
                set_meaning(aExpression, meaning::Value);
                result = t;
                break;
            }
        case construct::All:
            {
                auto const options = interpretations(aExpression->child(0), aRegion);
                ada_type* accessType = nullptr;
                for (auto const& o : options)
                    if (o.type != nullptr && (o.type->view()->kind == type_kind::Access || o.type->view()->kind == type_kind::AccessSubprogram))
                    {
                        auto* d = o.type->view()->kind == type_kind::Access ? designated_of(o.type) : nullptr;
                        // (an access-to-subprogram value's subprogram: called (by an enclosing Apply), or a parameterless function's result
                        // (of the expected type))
                        auto* profile = d == nullptr ? o.type->view()->profile : nullptr;
                        bool const called = aExpression->parent != nullptr && aExpression->parent->is(construct::Apply) &&
                            aExpression->parent->child(0) == aExpression;
                        if (aExpected == nullptr || (d != nullptr && covers(aExpected, d)) || (d == nullptr && (called || profile == nullptr ||
                            (profile->result == nullptr && aExpected->view()->kind == type_kind::Void) ||
                            (profile->result != nullptr && profile->parameters.empty() && covers(aExpected, profile->result)))))
                        {
                            accessType = o.type;
                            break;
                        }
                    }
                if (accessType == nullptr)
                    error(aExpression, "prefix of .all is not an access value");
                resolve(aExpression->child(0), accessType, aRegion);
                set_meaning(aExpression, meaning::Dereference);
                if (accessType->view()->kind == type_kind::AccessSubprogram)
                {
                    // (a call through an access-to-subprogram value with no actuals)
                    auto* profile = accessType->view()->profile;
                    auto* holder = new_entity(profile->result != nullptr ? entity_kind::Function : entity_kind::Procedure, "%indirect", aExpression, aRegion);
                    holder->sub->parameters = profile->parameters;
                    holder->sub->result = profile->result;
                    std::vector<node*> none;
                    result = finish_call(aExpression, holder, none, nullptr, aRegion);
                    iCalls[aExpression].object = aExpression->child(0);
                    aExpression->mark |= 0x800u;
                    break;
                }
                result = designated_of(accessType);
                break;
            }
        case construct::AndThen:
        case construct::OrElse:
            {
                ada_type* t = aExpected != nullptr && aExpected->view()->base_type()->boolean ? aExpected : nullptr;
                if (t == nullptr)
                {
                    for (auto const& l : interpretations(aExpression, aRegion))
                        if (l.type != nullptr && l.type->view()->base_type()->boolean)
                        {
                            t = l.type->base_type();
                            break;
                        }
                    if (t == nullptr)
                        t = iBoolean;
                }
                resolve(aExpression->child(0), t, aRegion);
                resolve(aExpression->child(1), t, aRegion);
                set_meaning(aExpression, meaning::Value);
                result = t->base_type();
                break;
            }
        case construct::In:
            {
                // (the tested expression and the choices: of a type that they can all have)
                auto* tested = aExpression->child(0);
                std::vector<node*> choices;
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                    if (!aExpression->child(i)->is(construct::Flag))
                        choices.push_back(aExpression->child(i));
                ada_type* t = nullptr;
                auto const options = interpretations(tested, aRegion);
                // (a choice that can be of the type)
                auto const choice_fits = [&](ada_type* aType, node* aChoice)
                {
                    if (aChoice->is(construct::SubtypeIndication) || aChoice->is(construct::Attribute) || is_subtype_name(aChoice, aRegion))
                        return true;
                    for (auto* value : aChoice->is(construct::Range) ? std::vector<node*>{ aChoice->child(0), aChoice->child(1) } : std::vector<node*>{ aChoice })
                    {
                        bool any = false;
                        for (auto const& o : interpretations(value, aRegion))
                            any = any || (o.type != nullptr && covers(aType, o.type));
                        if (!any)
                            return false;
                    }
                    return true;
                };
                for (auto const& o : options)
                    if (o.type != nullptr && !o.type->is_universal() && o.type->kind != type_kind::Aggregate &&
                        std::all_of(choices.begin(), choices.end(), [&](node* c) { return choice_fits(o.type, c); }))
                    {
                        t = o.type;
                        break;
                    }
                if (t == nullptr)
                    for (auto const& o : options)
                        if (o.type != nullptr && !o.type->is_universal() && o.type->kind != type_kind::Aggregate)
                        {
                            t = o.type;
                            break;
                        }
                if (t == nullptr)
                {
                    // (the tested expression is universal: the choices' type)
                    for (auto* c : choices)
                    {
                        if (is_subtype_name(c, aRegion) || c->is(construct::SubtypeIndication))
                        {
                            t = discrete_range(c, nullptr, aRegion);
                            break;
                        }
                        if (c->is(construct::Attribute) && (name_text(c->child(1)->source) == "range"))
                        {
                            t = range_of(c, aRegion);
                            if (t != nullptr)
                                break;
                            continue;
                        }
                        node* bound = c->is(construct::Range) ? c->child(0) : c;
                        for (auto const& o : interpretations(bound, aRegion))
                            if (o.type != nullptr && !o.type->is_universal() && o.type->kind != type_kind::Aggregate)
                            {
                                t = o.type;
                                break;
                            }
                        if (t == nullptr && c->is(construct::Range))
                            for (auto const& o : interpretations(c->child(1), aRegion))
                                if (o.type != nullptr && !o.type->is_universal() && o.type->kind != type_kind::Aggregate)
                                {
                                    t = o.type;
                                    break;
                                }
                        if (t != nullptr)
                            break;
                    }
                }
                if (t == nullptr && !options.empty())
                    t = options.front().type;
                resolve(tested, t != nullptr ? (t->view()->kind == type_kind::ClassWide ? nullptr : t->base_type()) : nullptr, aRegion);
                if (t == nullptr)
                    t = tested->type;
                for (auto* c : choices)
                {
                    if (c->is(construct::Range))
                        resolve_range_bounds(c, t != nullptr ? t->base_type() : nullptr, aRegion);
                    else if (c->is(construct::SubtypeIndication) || is_subtype_name(c, aRegion) ||
                        (c->is(construct::Attribute) && name_text(c->child(1)->source) == "base" && is_subtype_name(c->child(0), aRegion)))
                        discrete_range(c, t, aRegion);
                    else if (c->is(construct::Attribute) && (name_text(c->child(1)->source) == "range"))
                        c->type = range_of(c, aRegion);
                    else if (c->is(construct::Attribute) && name_text(c->child(1)->source) == "class" && is_subtype_name(c->child(0), aRegion))
                    {
                        // (T'Class: a subtype mark (4.5.2(3/4)))
                        resolve(c, nullptr, aRegion);
                        set_meaning(c, meaning::Type);
                    }
                    else
                        resolve(c, t != nullptr && t->view()->kind != type_kind::ClassWide ? t->base_type() : t, aRegion);
                }
                set_meaning(aExpression, meaning::Value);
                result = iBoolean;
                break;
            }
        case construct::Aggregate:
        case construct::AggregateBracket:
        case construct::AggregateExtension:
        case construct::AggregateDelta:
        case construct::AggregateNullRecord:
            result = resolve_aggregate(aExpression, aExpected, aRegion);
            break;
        case construct::Allocator:
            {
                if (aExpected == nullptr || aExpected->view()->kind != type_kind::Access)
                    error(aExpression, "allocator needs an access type context");
                auto* designated = designated_of(aExpected);
                auto* indication = aExpression->children.back();
                node* qualified = nullptr;
                if (indication->is(construct::Qualified))
                    qualified = indication;
                else if (indication->is(construct::SubtypeIndication) && indication->size() == 1u && indication->child(0)->is(construct::Qualified))
                    qualified = indication->child(0);
                if (qualified != nullptr)
                {
                    resolve(qualified, nullptr, aRegion);
                    aExpression->ent = nullptr;
                    aExpression->mark |= 0x1000u;   // (an initialized allocator)
                }
                else
                {
                    auto* t = subtype_indication(indication, aRegion);
                    (void)t;
                }
                if (auto* subpool = aExpression->first(construct::Subpool))
                    resolve(subpool->child(0), nullptr, aRegion);
                (void)designated;
                set_meaning(aExpression, meaning::Value);
                result = aExpected;
                break;
            }
        case construct::IfExpression:
            {
                ada_type* t = aExpected;
                if (t == nullptr)
                {
                    auto const options = interpretations(aExpression, aRegion);
                    if (!options.empty())
                        t = options.front().type;
                }
                condition(aExpression->child(0), aRegion);
                resolve(aExpression->child(1), t, aRegion);
                for (std::size_t i = 2u; i < aExpression->size(); ++i)
                {
                    auto* c = aExpression->child(i);
                    if (c->is(construct::Elsif))
                    {
                        condition(c->child(0), aRegion);
                        resolve(c->child(1), t, aRegion);
                    }
                    else if (c->is(construct::Else))
                        resolve(c->child(0), t, aRegion);
                }
                set_meaning(aExpression, meaning::Value);
                result = t != nullptr ? t : aExpression->child(1)->type;
                if (result == nullptr)
                    result = iBoolean;
                break;
            }
        case construct::CaseExpression:
            {
                ada_type* t = aExpected;
                if (t == nullptr)
                {
                    auto const options = interpretations(aExpression, aRegion);
                    if (!options.empty())
                        t = options.front().type;
                }
                // (the selector: of a discrete type (5.4(4)))
                auto* selector = aExpression->child(0);
                ada_type* discrete = nullptr;
                for (auto const& o : interpretations(selector, aRegion))
                    if (o.type != nullptr && o.type->view()->is_discrete() && !o.type->is_universal())
                        discrete = o.type;
                auto* selectorType = resolve(selector, discrete, aRegion);
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                {
                    auto* alternative = aExpression->child(i);
                    for (auto* choice : alternative->child(0)->children)
                        if (!choice->is(construct::Others))
                            choice_range(choice, selectorType != nullptr ? selectorType->base_type() : nullptr, aRegion);
                    resolve(alternative->children.back(), t, aRegion);
                }
                set_meaning(aExpression, meaning::Value);
                result = t;
                break;
            }
        case construct::Quantified:
            {
                auto* r = new_region(nullptr, aRegion);
                iterator_specification(aExpression->child(1), r);
                condition(aExpression->children.back(), r);
                set_meaning(aExpression, meaning::Value);
                result = iBoolean;
                break;
            }
        case construct::DeclareExpression:
            {
                auto* r = new_region(nullptr, aRegion);
                for (std::size_t i = 0u; i + 1u < aExpression->size(); ++i)
                    declaration(aExpression->child(i), r);
                aExpression->ent = nullptr;
                iDeclareRegions[aExpression] = r;
                result = resolve(aExpression->children.back(), aExpected, r);
                set_meaning(aExpression, meaning::Value);
                break;
            }
        case construct::RaiseExpression:
            {
                resolve_entity(aExpression->child(0), aRegion);
                if (aExpression->size() > 1u)
                    resolve(aExpression->child(1), iString, aRegion);
                set_meaning(aExpression, meaning::Value);
                result = aExpected;
                break;
            }
        case construct::Target:
            set_meaning(aExpression, meaning::Object);
            result = iTargetType;
            break;
        case construct::Range:
            result = resolve_range_bounds(aExpression, aExpected, aRegion);
            break;
        default:
            if (is_operator(aExpression->kind))
                result = resolve_operator(aExpression, aExpected, aRegion);
            else
                error(aExpression, "unexpected construct in expression" + (std::getenv("NEOS_ADA_DEBUG") ? " (" + std::to_string(static_cast<int>(aExpression->kind)) + ")" : std::string{}));
            break;
        }
        // (a value of a reference type where its designated type is expected: implicitly dereferenced (4.1.5))
        if (result != nullptr && aExpected != nullptr && aExpected != iVoid && implicit_dereference_of(aExpression) == nullptr && !covers(aExpected, result))
            if (auto* d = implicit_dereference(result); d != nullptr && d != result && covers(aExpected, d))
            {
                iImplicitDereferences[aExpression] = result;
                result = d;
            }
        aExpression->type = result;
        return result;
    }

    // --- operators

    region* program::selected_operator_scope(node* aPrefix, region* aRegion)
    {
        // (P."op": the region of the package P, whose operators alone it denotes; otherwise null)
        region* result = nullptr;
        if (!aPrefix->is(construct::Selected))
            return result;
        try
        {
            for (auto* p : prefix_entities(aPrefix->child(0), aRegion))
            {
                while (p->renamed != nullptr)
                    p = p->renamed;
                if (p->kind == entity_kind::Package && p->inner != nullptr)
                    result = p->inner;
            }
        }
        catch (compile_error const&)
        {
        }
        return result;
    }

    std::vector<entity*> program::operator_candidates(std::string const& aDesignator, std::vector<node*> const& aOperands, region* aRegion)
    {
        std::vector<entity*> result;
        // (user-defined operators: visible functions with the designator)
        for (auto* e : visible(aDesignator, aRegion))
            if (e->sub != nullptr && e->sub->parameters.size() == aOperands.size() && (e->kind == entity_kind::Function || e->kind == entity_kind::Attribute))
                result.push_back(e);
        // (operators declared with the operands' types (visible by use type or by selection: permitted here))
        std::vector<ada_type*> types;
        for (auto* operand : aOperands)
            for (auto const& i : interpretations(operand, aRegion))
                if (i.type != nullptr)
                    for (auto* it : { i.type, implicit_dereference(i.type), i.type->view()->kind == type_kind::ClassWide ? i.type->view()->specific : nullptr })
                        if (it != nullptr && std::find(types.begin(), types.end(), it->view()->base_type()->view()) == types.end())
                            types.push_back(it->view()->base_type()->view());
        // (within an instance, an untagged formal type's operators are the predefined ones: not the user-defined ones of its actual
        // declared outside the instance (12.3(15), 12.5(8)))
        for (auto* t : types)
            if (auto* instance = formal_actual_instance(t, aRegion))
                std::erase_if(result, [&](entity* e)
                    {
                        if (e->sub == nullptr || e->sub->op != intrinsic::None)
                            return false;
                        bool ofType = false;
                        for (auto const& p : e->sub->parameters)
                            ofType = ofType || (p.type != nullptr && p.type->view()->base_type()->view() == t);
                        if (!ofType)
                            return false;
                        for (region* jump = nullptr, *q = e->scope; q != nullptr; q = outward(q, jump))
                            if (q == instance)
                                return false;
                        return true;
                    });
        auto const add = [&](entity* e)
        {
            if (std::find(result.begin(), result.end(), e) != result.end())
                return;
            // (a user-defined operator hides the predefined one of the same profile; but a directly visible predefined operator
            // hides a use-visible user-defined homograph (8.4(8)))
            auto const user = [](entity* x) { return x->sub->op == intrinsic::None || x->sub->op == intrinsic::NotEqual; };
            auto const useOnly = [&](entity* x) { return x->scope != nullptr && x->scope->is_package && !within_scope(x->scope, aRegion); };
            auto const direct = [&](entity* x) { return x->scope != nullptr && within_scope(x->scope, aRegion); };
            if (!user(e))
            {
                std::erase_if(result, [&](entity* other) { return user(other) && useOnly(other) && direct(e) && type_conformant(*other->sub, *e->sub); });
                for (auto* other : result)
                    if (user(other) && type_conformant(*other->sub, *e->sub))
                        return;
            }
            else
            {
                for (auto* other : result)
                    if (!user(other) && useOnly(e) && direct(other) && type_conformant(*other->sub, *e->sub))
                        return;
                std::erase_if(result, [&](entity* other) { return !user(other) && type_conformant(*other->sub, *e->sub); });
            }
            result.push_back(e);
        };
        for (auto* t : types)
        {
            if (t->kind == type_kind::String || t->kind == type_kind::Aggregate || t->kind == type_kind::Void || t->kind == type_kind::CharacterLiteral)
                continue;
            if (t->kind == type_kind::UniversalAccess)
                continue;
            for (auto* op : operators_of(t))
                if (op->name == aDesignator && op->sub->parameters.size() == aOperands.size())
                    add(op);
            // (user-defined operators of the type, declared where the type is (visible by selection); not those of a formal type's
            // actual within the instance (12.3(15)))
            if (t->declared_in != nullptr && formal_actual_instance(t, aRegion) == nullptr)
            {
                auto range = t->declared_in->names.equal_range(aDesignator);
                for (auto i = range.first; i != range.second; ++i)
                    if (i->second->sub != nullptr && i->second->sub->parameters.size() == aOperands.size() && i->second->kind == entity_kind::Function &&
                        !instance_hidden(i->second, aRegion))
                        if (std::find(result.begin(), result.end(), i->second) == result.end())
                            result.push_back(i->second);
            }
            // (the operators of the primitive types of class-wide types)
            if (t->kind == type_kind::ClassWide)
                for (auto* op : operators_of(t->specific))
                    if (op->name == aDesignator && op->sub->parameters.size() == aOperands.size())
                        add(op);
        }
        // ("&" of components: the "&" of the one dimensional array types of their type declared in the regions enclosing the
        // expression and in Standard (4.5.3))
        if (aDesignator == "&" && aOperands.size() == 2u)
        {
            std::vector<ada_type*> components;
            for (auto* t : types)
                if (t->kind != type_kind::Array && !t->is_universal() && t->kind != type_kind::String && t->kind != type_kind::Aggregate &&
                    t->kind != type_kind::Void)
                    components.push_back(t);
            auto const consider = [&](region* aDeclarations)
            {
                // (the array types declared there, and the anonymous ones of the objects declared there (their predefined operators are
                // declared with them, 3.2.1(13)))
                for (auto* d : aDeclarations->declarations)
                    if ((d->kind == entity_kind::Type || ((d->kind == entity_kind::Variable || d->kind == entity_kind::Constant) &&
                        d->type != nullptr && d->type->view()->base_type()->view()->decl == nullptr)) && d->type != nullptr)
                    {
                        auto* a = d->type->view()->base_type()->view();
                        if (a->kind != type_kind::Array || a->indexes.size() != 1u || a->component == nullptr || a->limited)
                            continue;
                        auto* c = a->component->view()->base_type()->view();
                        if (std::find(components.begin(), components.end(), c) == components.end())
                            continue;
                        for (auto* op : operators_of(a))
                            if (op->name == "&")
                                add(op);
                    }
            };
            if (!components.empty())
            {
                for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
                    if (r != iStandard)
                        consider(r);
                consider(iStandard);
            }
        }
        // (the primitive operators of the types named by use type clauses in scope (8.4(8/3)))
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            for (auto* t : r->used_types)
                for (auto* where : { t->declared_in, t->partial != nullptr ? t->partial->declared_in : nullptr })
                    if (where != nullptr)
                    {
                        auto range = where->names.equal_range(aDesignator);
                        for (auto i = range.first; i != range.second; ++i)
                            if (i->second->kind == entity_kind::Function && i->second->sub != nullptr && i->second->sub->parameters.size() == aOperands.size() &&
                                std::find(result.begin(), result.end(), i->second) == result.end())
                                result.push_back(i->second);
                    }
        // (equality of access values with null; "&" of string literals; operators of the expected types when all operands are
        // universal or literals: added by resolve_operator)
        // (an abstract nondispatching operator: not a candidate, though it hides the predefined one (3.9.3(4/2), AI95-00310))
        std::erase_if(result, [](entity* e) { return e->sub != nullptr && e->sub->is_abstract && e->sub->controlling == nullptr; });
        return result;
    }

    ada_type* program::resolve_operator(node* aOperator, ada_type* aExpected, region* aRegion)
    {
        auto const designator = operator_designator(aOperator->kind);
        std::vector<node*> operands(aOperator->children.begin(), aOperator->children.end());
        auto candidates = operator_candidates(designator, operands, aRegion);
        // (the expected type's operators, for operands that are literals or aggregates)
        if (aExpected != nullptr && !aExpected->is_universal())
        {
            for (auto* op : operators_of(aExpected))
                if (op->name == designator && op->sub->parameters.size() == operands.size() && std::find(candidates.begin(), candidates.end(), op) == candidates.end())
                    candidates.push_back(op);
            // (user-defined operators whose result is of the expected type, declared where it is (visible by use type or by
            // selection: permitted here, as for the operands' types))
            auto* et = aExpected->view()->base_type()->view();
            for (auto* where : { et->declared_in, et->partial != nullptr ? et->partial->declared_in : nullptr })
                if (where != nullptr && formal_actual_instance(et, aRegion) == nullptr)
                {
                    auto range = where->names.equal_range(designator);
                    for (auto i = range.first; i != range.second; ++i)
                        if (i->second->kind == entity_kind::Function && i->second->sub != nullptr && i->second->sub->parameters.size() == operands.size() &&
                            i->second->sub->result != nullptr && same_type(i->second->sub->result, aExpected) && !instance_hidden(i->second, aRegion) &&
                            std::find(candidates.begin(), candidates.end(), i->second) == candidates.end())
                            candidates.push_back(i->second);
                }
        }
        // (equality and relational operators of the operands' types when one is a literal of the other's type)
        if ((designator == "=" || designator == "/=") && operands.size() == 2u)
        {
            for (auto* operand : operands)
                for (auto const& i : interpretations(operand, aRegion))
                    if (i.type != nullptr && i.type->view()->is_access())
                        for (auto* op : operators_of(i.type))
                            if (op->name == designator && std::find(candidates.begin(), candidates.end(), op) == candidates.end())
                                candidates.push_back(op);
        }
        std::vector<entity*> matching;
        // (a root numeric operator (of universal_integer or universal_real): its operands are universal (a specific type's
        // value isn't implicitly converted to a universal type))
        auto const universal_operands = [&](entity* aOperator)
        {
            for (std::size_t k = 0u; k < operands.size() && k < aOperator->sub->parameters.size(); ++k)
            {
                auto* formal = aOperator->sub->parameters[k].type;
                if (formal == nullptr || !formal->is_universal())
                    continue;   // (e.g. "**"'s exponent)
                bool universal = false;
                for (auto const& i : interpretations(operands[k], aRegion))
                    universal = universal || i.type == nullptr || i.type->is_universal();
                if (!universal)
                    return false;
            }
            return true;
        };
        for (auto* op : candidates)
        {
            if (op->sub->op != intrinsic::None && op->sub->operator_type != nullptr &&
                (op->sub->operator_type->kind == type_kind::UniversalInteger || op->sub->operator_type->kind == type_kind::UniversalReal) &&
                !universal_operands(op))
                continue;
            if (!match_call(op, operands, nullptr, aRegion, nullptr, true))
                continue;
            if (aExpected != nullptr && !covers(aExpected, op->sub->result))
                continue;
            matching.push_back(op);
        }
        if (matching.empty())
        {
            if (std::getenv("NEOS_ADA_DEBUG") != nullptr)
            {
                for (auto* op : candidates)
                {
                    std::fprintf(stderr, "candidate %s(", op->name.c_str());
                    for (auto const& p : op->sub->parameters)
                        std::fprintf(stderr, "%s,", p.type != nullptr ? p.type->name.c_str() : "?");
                    std::fprintf(stderr, ") -> %s match=%d\n", op->sub->result != nullptr ? op->sub->result->name.c_str() : "", match_call(op, operands, nullptr, aRegion, nullptr, true));
                }
                for (auto* operand : operands)
                    for (auto const& i : interpretations(operand, aRegion))
                        std::fprintf(stderr, "operand interpretation %s kind %d covers %d %s\n", i.type != nullptr ? i.type->name.c_str() : "?", i.type != nullptr ? (int)i.type->view()->kind : -1, i.type != nullptr && !candidates.empty() ? (int)covers(candidates[0]->sub->parameters[0].type, i.type) : -1, std::string{ operand->source }.c_str());
            }
            error(aOperator, "no operator \"" + designator + "\" matches the operands");
        }
        if (matching.size() > 1u)
        {
            // (preference: the operator of the expected type; for universal operands, root numeric operators; user-defined over predefined)
            std::vector<entity*> preferred;
            if (aExpected != nullptr && !aExpected->is_universal())
                for (auto* op : matching)
                    if (same_type(op->sub->result, aExpected) || (op->sub->operator_type != nullptr && same_type(op->sub->operator_type, aExpected)))
                        preferred.push_back(op);
            // (universal operands: the root numeric operators (8.6(29)), also among those of the expected result type (a relational
            // operator's Boolean))
            if (preferred.size() != 1u)
            {
                // (universal operands, no specific expected type: the root numeric operators (8.6(29)))
                bool allUniversal = true;
                for (auto* operand : operands)
                {
                    bool universal = false;
                    for (auto const& i : interpretations(operand, aRegion))
                        universal = universal || (i.type != nullptr && i.type->is_universal());
                    allUniversal = allUniversal && universal;
                }
                std::vector<entity*> root;
                if (allUniversal)
                    for (auto* op : preferred.empty() ? matching : preferred)
                        if (op->sub->operator_type != nullptr && op->sub->operator_type->is_universal())
                            root.push_back(op);
                if (!root.empty())
                    preferred = root;
            }
            if (preferred.size() != 1u)
            {
                auto const p = prefer(preferred.empty() ? matching : preferred, operands, aExpected, aRegion);
                if (p.size() >= 1u)
                    preferred = p;
            }
            if (preferred.size() > 1u)
            {
                // (the universal (root) operators are preferred)
                for (auto* op : preferred)
                    if (op->sub->operator_type != nullptr && op->sub->operator_type->is_universal())
                    {
                        preferred = { op };
                        break;
                    }
            }
            if (preferred.size() > 1u)
            {
                // (ambiguous unless the operands' own types decide it: the first whose operands are all of its own type)
                std::vector<entity*> own;
                for (auto* op : preferred)
                {
                    bool ok = true;
                    for (std::size_t i = 0u; i < operands.size(); ++i)
                    {
                        bool found = false;
                        for (auto const& o : interpretations(operands[i], aRegion))
                            if (o.type != nullptr && same_type(o.type, op->sub->parameters[i].type))
                                found = true;
                        ok = ok && found;
                    }
                    if (ok)
                        own.push_back(op);
                }
                if (own.size() == 1u)
                    preferred = own;
            }
            matching = preferred;
        }
        auto* op = matching.front();
        auto& call = iCalls[aOperator];
        call.callee = op;
        call.actuals = operands;
        for (std::size_t i = 0u; i < operands.size(); ++i)
        {
            // (a controlling operand of a tagged type's operator: a class-wide operand (a dispatching call) is resolved as that)
            auto* expected = op->sub->parameters[i].type;
            if (auto* pt = expected != nullptr ? expected->view() : nullptr; pt != nullptr && pt->tagged && pt->kind != type_kind::ClassWide)
                for (auto const& in : interpretations(operands[i], aRegion))
                    if (in.type != nullptr && in.type->view()->kind == type_kind::ClassWide && in.type->view()->specific != nullptr &&
                        same_type(in.type->view()->specific, pt))
                        expected = in.type;
            resolve(operands[i], expected, aRegion);
        }
        aOperator->ent = op;
        set_meaning(aOperator, meaning::Call);
        // (a predefined operator of a universal type whose result is wanted as another type: the expected type)
        auto* result = op->sub->result;
        if (result != nullptr && result->is_universal() && aExpected != nullptr && !aExpected->is_universal() && covers(aExpected, result))
            result = aExpected->base_type();
        return result;
    }

    // --- conversions

    void program::resolve_conversion_operand(node* aOperand, ada_type* aTarget, region* aRegion)
    {
        // (the operand of a type conversion: any type that can be converted to the target (its own type))
        auto const options = interpretations(aOperand, aRegion);
        auto* target = aTarget->view();
        ada_type* chosen = nullptr;
        for (auto const& o : options)
        {
            if (o.type == nullptr)
                continue;
            auto* t = o.type->view();
            bool convertible = false;
            if (target->is_numeric() && t->is_numeric())
                convertible = true;
            else if (same_type(target, t) || covers(target, t) || covers(t, target))
                convertible = true;
            else if (target->kind == type_kind::Array && t->kind == type_kind::Array)
                convertible = true;
            else if (target->is_access() && t->is_access())
                convertible = true;
            else if (target->root == t->root || (target->base_type()->root != nullptr && target->base_type()->root == t->base_type()->root))
                convertible = true;
            else if ((target->tagged || target->kind == type_kind::ClassWide) && (t->tagged || t->kind == type_kind::ClassWide))
                convertible = true;
            if (convertible)
            {
                // (a universal interpretation (a root numeric type's) is preferred (8.6(29/3)))
                if (chosen == nullptr || (o.type->is_universal() && !chosen->is_universal()))
                    chosen = o.type;
                if (chosen->is_universal())
                    break;
            }
        }
        if (chosen == nullptr && !options.empty())
        {
            chosen = options.front().type;
            // (a scalar operand of a record or array target, or a record or array operand of a scalar target: not convertible (4.6))
            auto* t = chosen != nullptr ? chosen->view() : nullptr;
            bool const composite = target->kind == type_kind::Record || target->kind == type_kind::Array;
            if (t != nullptr && !t->is_universal() && ((composite && t->is_scalar()) ||
                (target->is_scalar() && (t->kind == type_kind::Record || t->kind == type_kind::Array))))
                error(aOperand, "operand of a type conversion not convertible to the target type");
        }
        if (chosen != nullptr && (chosen->kind == type_kind::String || chosen->kind == type_kind::Aggregate))
            chosen = aTarget;
        resolve(aOperand, chosen != nullptr && !chosen->is_universal() ? chosen : nullptr, aRegion);
    }

    ada_type* program::string_type_for(ada_type* aCharacter)
    {
        (void)aCharacter;
        return iString;
    }

    ada_type* program::aggregate_component_type(ada_type* aType, std::size_t aDimension)
    {
        (void)aDimension;
        return aType->view()->component;
    }

    // --- static evaluation

    bool program::is_static(node* aExpression)
    {
        return evaluate(aExpression, nullptr).known();
    }

    static_value program::fold(intrinsic aOp, ada_type* aType, std::vector<static_value> const& aOperands)
    {
        auto* t = aType != nullptr ? aType->view()->base_type()->view() : nullptr;
        bool const modular = t != nullptr && t->kind == type_kind::Modular;
        auto const wrap = [&](bigint const& aValue) -> static_value
        {
            if (modular && !t->modulus.is_zero())
                return static_value{ bigint::mod(aValue, t->modulus) };
            return static_value{ aValue };
        };
        auto const boolean = [](bool aValue) { return static_value{ bigint{ aValue ? 1 : 0 } }; };
        if (aOperands.empty())
            return {};
        for (auto const& o : aOperands)
            if (!o.known())
                return {};
        auto const& a = aOperands[0];
        if (aOperands.size() == 1u)
        {
            switch (aOp)
            {
            case intrinsic::Plus:
                return a;
            case intrinsic::Negate:
                if (a.is_integer())
                    return wrap(-a.integer());
                return static_value{ -a.real() };
            case intrinsic::Abs:
                if (a.is_integer())
                    return static_value{ a.integer().abs() };
                return static_value{ a.real().abs() };
            case intrinsic::Not:
                if (t != nullptr && t->boolean)
                    return boolean(a.integer().is_zero());
                if (modular)
                    return wrap(t->modulus - bigint{ 1 } - a.integer());
                return {};
            default:
                return {};
            }
        }
        auto const& b = aOperands[1];
        if (a.is_string() || b.is_string())
        {
            if (aOp == intrinsic::Concat)
            {
                auto const as_string = [](static_value const& v) -> std::u32string
                {
                    if (v.is_string())
                        return v.string();
                    if (v.is_integer())
                        return std::u32string(1u, static_cast<char32_t>(v.integer().to_int64()));
                    return {};
                };
                return static_value{ as_string(a) + as_string(b) };
            }
            if (a.is_string() && b.is_string())
                switch (aOp)
                {
                case intrinsic::Eq: return boolean(a.string() == b.string());
                case intrinsic::Ne: return boolean(a.string() != b.string());
                case intrinsic::Lt: return boolean(a.string() < b.string());
                case intrinsic::Le: return boolean(a.string() <= b.string());
                case intrinsic::Gt: return boolean(a.string() > b.string());
                case intrinsic::Ge: return boolean(a.string() >= b.string());
                default: return {};
                }
            return {};
        }
        bool const integers = a.is_integer() && b.is_integer() && (t == nullptr || !t->is_real());
        switch (aOp)
        {
        case intrinsic::Add:
            return integers ? wrap(a.integer() + b.integer()) : static_value{ a.real() + b.real() };
        case intrinsic::Subtract:
            return integers ? wrap(a.integer() - b.integer()) : static_value{ a.real() - b.real() };
        case intrinsic::Multiply:
        case intrinsic::RootMultiply:
        case intrinsic::UniversalFixedMultiply:
            return integers ? wrap(a.integer() * b.integer()) : static_value{ a.real() * b.real() };
        case intrinsic::Divide:
        case intrinsic::RootDivide:
        case intrinsic::UniversalFixedDivide:
            if (integers)
            {
                if (b.integer().is_zero())
                    return {};
                return wrap(a.integer() / b.integer());
            }
            if (b.real().sign() == 0)
                return {};
            return static_value{ a.real() / b.real() };
        case intrinsic::Mod:
            if (!integers || b.integer().is_zero())
                return {};
            return wrap(bigint::mod(a.integer(), b.integer()));
        case intrinsic::Rem:
            if (!integers || b.integer().is_zero())
                return {};
            return wrap(a.integer() % b.integer());
        case intrinsic::Power:
            {
                if (!b.is_integer())
                    return {};
                auto const e = b.integer();
                if (a.is_integer() && (t == nullptr || !t->is_real()))
                {
                    if (e.negative() || !e.fits_int64() || e.to_int64() > 100000)
                        return {};
                    if (modular)
                    {
                        bigint result{ 1 };
                        for (std::int64_t i = 0; i < e.to_int64(); ++i)
                            result = bigint::mod(result * a.integer(), t->modulus);
                        return static_value{ result };
                    }
                    return static_value{ bigint::pow(a.integer(), static_cast<std::uint64_t>(e.to_int64())) };
                }
                if (!e.fits_int64() || std::abs(e.to_int64()) > 100000)
                    return {};
                if (a.real().sign() == 0 && e.negative())
                    return {};
                return static_value{ rational::pow(a.real(), e.to_int64()) };
            }
        case intrinsic::And:
            if (t != nullptr && t->boolean)
                return boolean(!a.integer().is_zero() && !b.integer().is_zero());
            if (modular && a.is_integer() && b.is_integer() && a.integer().fits_uint64() && b.integer().fits_uint64())
                return static_value{ bigint::from_unsigned(a.integer().to_uint64() & b.integer().to_uint64()) };
            return {};
        case intrinsic::Or:
            if (t != nullptr && t->boolean)
                return boolean(!a.integer().is_zero() || !b.integer().is_zero());
            if (modular && a.integer().fits_uint64() && b.integer().fits_uint64())
                return wrap(bigint::from_unsigned(a.integer().to_uint64() | b.integer().to_uint64()));
            return {};
        case intrinsic::Xor:
            if (t != nullptr && t->boolean)
                return boolean(a.integer().is_zero() != b.integer().is_zero());
            if (modular && a.integer().fits_uint64() && b.integer().fits_uint64())
                return wrap(bigint::from_unsigned(a.integer().to_uint64() ^ b.integer().to_uint64()));
            return {};
        case intrinsic::AndThen:
            return boolean(!a.integer().is_zero() && !b.integer().is_zero());
        case intrinsic::OrElse:
            return boolean(!a.integer().is_zero() || !b.integer().is_zero());
        case intrinsic::Eq:
            return boolean(integers ? a.integer() == b.integer() : a.real() == b.real());
        case intrinsic::Ne:
            return boolean(integers ? a.integer() != b.integer() : a.real() != b.real());
        case intrinsic::Lt:
            return boolean(integers ? a.integer() < b.integer() : a.real() < b.real());
        case intrinsic::Le:
            return boolean(integers ? a.integer() <= b.integer() : a.real() <= b.real());
        case intrinsic::Gt:
            return boolean(integers ? a.integer() > b.integer() : a.real() > b.real());
        case intrinsic::Ge:
            return boolean(integers ? a.integer() >= b.integer() : a.real() >= b.real());
        default:
            return {};
        }
    }

    static_value program::evaluate(node* aExpression, region* aRegion)
    {
        if (aExpression == nullptr)
            return {};
        switch (aExpression->kind)
        {
        case construct::Number:
            {
                auto const value = rational::parse_literal(aExpression->source);
                if (!value)
                    return {};
                if (!is_real_literal(aExpression->source))
                    return static_value{ value->truncate() };
                return static_value{ *value };
            }
        case construct::Character:
            if (aExpression->ent != nullptr && aExpression->ent->kind == entity_kind::EnumerationLiteral)
                return aExpression->ent->value;
            return static_value{ bigint{ static_cast<std::int64_t>(literal_character(aExpression)) } };
        case construct::Operator:
            if (meaning_of(aExpression) == meaning::StringLiteral || aExpression->ent == nullptr)
                return static_value{ literal_string(aExpression) };
            return {};
        case construct::Identifier:
        case construct::Selected:
            {
                auto* e = aExpression->ent;
                if (e == nullptr)
                    return {};
                if (e->kind == entity_kind::NamedNumber || e->kind == entity_kind::EnumerationLiteral)
                    return e->value;
                if (e->kind == entity_kind::Constant)
                {
                    if (e->value.known())
                        return e->value;
                    if (e->renamed != nullptr && e->renamed->value.known())
                        return e->renamed->value;
                    // (a deferred constant: its full declaration's value (static where the full view is visible, 4.9(24)))
                    if (e->completion != nullptr && e->completion->kind == entity_kind::Constant && e->completion->value.known())
                        return e->completion->value;
                }
                if (e->kind == entity_kind::Variable && e->renamed != nullptr && e->renamed->kind == entity_kind::Constant && e->renamed->value.known())
                    return e->renamed->value;
                if (e->kind == entity_kind::Function && meaning_of(aExpression) == meaning::Call && e->sub != nullptr && e->sub->renames != nullptr &&
                    e->sub->renames->kind == entity_kind::EnumerationLiteral)
                    return e->sub->renames->value;
                return {};
            }
        case construct::Parenthesized:
            return evaluate(aExpression->child(0), aRegion);
        case construct::Qualified:
            return evaluate(aExpression->child(1), aRegion);
        case construct::Apply:
            {
                if (meaning_of(aExpression) == meaning::Conversion)
                {
                    auto v = evaluate(aExpression->child(1), aRegion);
                    if (!v.known() || aExpression->type == nullptr)
                        return {};
                    auto* t = aExpression->type->view();
                    if (t->is_integer() || (t->kind == type_kind::Enumeration))
                    {
                        if (v.is_real())
                            return static_value{ v.real().round() };
                        return v;
                    }
                    if (t->is_real())
                        return static_value{ v.real() };
                    return {};
                }
                if (meaning_of(aExpression) == meaning::Call)
                {
                    auto const* call = call_of(aExpression);
                    if (call == nullptr || call->callee == nullptr || call->callee->sub == nullptr)
                        return {};
                    auto op = call->callee->sub->op;
                    if (op == intrinsic::None || op == intrinsic::Import || op == intrinsic::Attribute)
                        return {};
                    std::vector<static_value> values;
                    for (auto* a : call->actuals)
                        values.push_back(evaluate(a, aRegion));
                    return fold(op, call->callee->sub->operator_type, values);
                }
                return {};
            }
        case construct::Attribute:
            return evaluate_attribute(aExpression, aRegion);
        case construct::AndThen:
        case construct::OrElse:
            {
                auto a = evaluate(aExpression->child(0), aRegion);
                auto b = evaluate(aExpression->child(1), aRegion);
                return fold(aExpression->is(construct::AndThen) ? intrinsic::AndThen : intrinsic::OrElse, iBoolean, { a, b });
            }
        case construct::In:
            {
                auto v = evaluate(aExpression->child(0), aRegion);
                if (!v.known())
                    return {};
                bool result = false;
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                {
                    auto* c = aExpression->child(i);
                    if (c->is(construct::Flag))
                        continue;
                    if (c->is(construct::Range))
                    {
                        auto l = evaluate(c->child(0), aRegion);
                        auto h = evaluate(c->child(1), aRegion);
                        if (!l.known() || !h.known())
                            return {};
                        if (v.is_integer() && l.is_integer() && h.is_integer())
                            result = result || (v.integer() >= l.integer() && v.integer() <= h.integer());
                        else
                            result = result || (v.real() >= l.real() && v.real() <= h.real());
                    }
                    else if (c->type != nullptr && (c->is(construct::SubtypeIndication) || meaning_of(c) == meaning::Type || c->is(construct::Attribute)))
                    {
                        auto* t = c->type->view();
                        if (!t->low.known() || !t->high.known())
                            return {};
                        if (v.is_integer() && t->low.is_integer())
                            result = result || (v.integer() >= t->low.integer() && v.integer() <= t->high.integer());
                        else
                            result = result || (v.real() >= t->low.real() && v.real() <= t->high.real());
                    }
                    else
                    {
                        auto c2 = evaluate(c, aRegion);
                        if (!c2.known())
                            return {};
                        result = result || fold(intrinsic::Eq, nullptr, { v, c2 }).integer() == bigint{ 1 };
                    }
                }
                if (aExpression->has_flag("not"))
                    result = !result;
                return static_value{ bigint{ result ? 1 : 0 } };
            }
        case construct::IfExpression:
            {
                auto c = evaluate(aExpression->child(0), aRegion);
                if (!c.is_integer())
                    return {};
                if (!c.integer().is_zero())
                    return evaluate(aExpression->child(1), aRegion);
                for (std::size_t i = 2u; i < aExpression->size(); ++i)
                {
                    auto* part = aExpression->child(i);
                    if (part->is(construct::Elsif))
                    {
                        auto ec = evaluate(part->child(0), aRegion);
                        if (!ec.is_integer())
                            return {};
                        if (!ec.integer().is_zero())
                            return evaluate(part->child(1), aRegion);
                    }
                    else if (part->is(construct::Else))
                        return evaluate(part->child(0), aRegion);
                }
                return {};
            }
        default:
            break;
        }
        if (is_operator(aExpression->kind))
        {
            auto const* call = call_of(aExpression);
            if (call == nullptr || call->callee == nullptr || call->callee->sub == nullptr)
                return {};
            auto const op = call->callee->sub->op;
            if (op == intrinsic::None || op == intrinsic::Import)
                return {};
            std::vector<static_value> values;
            for (auto* operand : aExpression->children)
                values.push_back(evaluate(operand, aRegion));
            auto* t = call->callee->sub->operator_type;
            if (op == intrinsic::Eq || op == intrinsic::Ne || op == intrinsic::Lt || op == intrinsic::Le || op == intrinsic::Gt || op == intrinsic::Ge)
                return fold(op, nullptr, values);
            if (aExpression->type != nullptr && !aExpression->type->is_universal() && (t == nullptr || t->is_universal()))
                t = aExpression->type;
            return fold(op, t, values);
        }
        return {};
    }
}
