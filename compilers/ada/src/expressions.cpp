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
    // (an array aggregate's others choices: its own, and its subaggregates' (of the further dimensions))
    void others_choices(node* aAggregate, std::size_t aDimensions, std::vector<node*>& aResult)
    {
        while (aAggregate != nullptr && aAggregate->is(construct::Parenthesized))
            aAggregate = aAggregate->child(0);
        // (a conditional expression: its dependent expressions' (they have the same applicable index constraint, 4.3.3(15.1/3)))
        if (aAggregate != nullptr && (aAggregate->is(construct::IfExpression) || aAggregate->is(construct::CaseExpression)))
        {
            for (std::size_t i = 1u; i < aAggregate->size(); ++i)
            {
                auto* c = aAggregate->child(i);
                auto* dependent = c->is(construct::Elsif) ? c->child(1) : c->is(construct::Else) || c->is(construct::Alternative) ? c->children.back() : c;
                others_choices(dependent, aDimensions, aResult);
            }
            return;
        }
        if (aAggregate == nullptr || !aAggregate->is(construct::Aggregate) || aDimensions == 0u)
            return;
        for (auto* a : aAggregate->children)
        {
            if (a->is(construct::Association) && !a->children.empty())
            {
                if (auto* o = a->child(0)->first(construct::Others))
                    aResult.push_back(o);
                if (aDimensions > 1u)
                    others_choices(a->children.back(), aDimensions - 1u, aResult);
            }
            else if (aDimensions > 1u)
                others_choices(a, aDimensions - 1u, aResult);
        }
    }

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
            return { interpretation{ is_real_literal(aExpression->source) ? iRealLiteral : iIntegerLiteral, nullptr } };
        case construct::Null:
            return { interpretation{ iUniversalAccess, nullptr } };
        case construct::Identifier:
        case construct::Selected:
        case construct::Operator:
        case construct::Character:
            {
                // (an expanded name of a character literal (P.'A'): the literal's)
                if (aExpression->is(construct::Selected) && aExpression->child(1)->is(construct::Character) && [&]()
                    {
                        auto const prefixes = prefix_entities(aExpression->child(0), aRegion);
                        return prefixes.size() == 1u && prefixes.front()->is_package() &&
                            selected(prefixes.front(), name_text(aExpression->child(1)->source), aRegion).empty();
                    }())
                    return interpretations(aExpression->child(1), aRegion);
                std::vector<interpretation> result;
                // (a string literal that is not an operator symbol: only a string)
                bool const operatorSymbol = !aExpression->is(construct::Operator) || is_operator_symbol(aExpression);
                if (operatorSymbol)
                    try
                    {
                        speculation guard{ *this };
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
                    speculation guard{ *this };
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
                    speculation guard{ *this };
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
                    if (l.type != nullptr && l.type->view()->base_type()->boolean && !partial_view_here(l.type, aRegion))
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
                    speculation guard{ *this };
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
                    speculation guard{ *this };
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
            if (o.type != nullptr && o.type->view()->boolean && !partial_view_here(o.type, aRegion))
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

    void program::parenthesis_check(node* aExpression)
    {
        // (a conditional, quantified or declare expression: parenthesized, unless it is the sole argument of a call (or attribute)
        // (4.5.7(7/3), 4.5.8, 4.5.9))
        auto* list = aExpression->parent;
        if (list == nullptr || iSpeculation != 0 || iInstanceBodies != 0 || list->source.empty())
            return;
        // (an instance's actuals: checked at the instantiation (see program::instantiation))
        if (!iInstantiationSites.empty() && (list->is(construct::Apply) || list->is(construct::Association)) &&
            list->parent != nullptr && (list->parent->is(construct::Instantiation) || (list->parent->parent != nullptr && list->parent->parent->is(construct::Instantiation))))
            return;
        // (an argument (named, or one of several); a conditional expression's dependent expression)
        if (list->is(construct::Apply) || list->is(construct::Arguments))
        {
            std::size_t const arguments = list->size() - (list->is(construct::Apply) ? 1u : 0u);
            if (arguments <= 1u)
                return;
        }
        else if (list->is(construct::PragmaArgument))
        {
            // (a pragma's: its only argument (not named))
            if (list->size() == 1u && list->parent != nullptr && list->parent->all(construct::PragmaArgument).size() == 1u)
                return;
        }
        else if (!list->is(construct::Association) && !list->is(construct::Aggregate) && !list->is(construct::IfExpression) && !list->is(construct::Elsif) && !list->is(construct::Else))
            return;
        // (its own parentheses: '(' before it and ')' after it (in the list's source))
        char const* const first = list->source.data();
        char const* const last = first + list->source.size();
        char const* before = aExpression->source.data();
        char const* after = before + aExpression->source.size();
        if (before < first || after > last)
            return;
        while (before > first && std::isspace(static_cast<unsigned char>(before[-1])))
            --before;
        while (after < last && std::isspace(static_cast<unsigned char>(*after)))
            ++after;
        if (before > first && before[-1] == '(' && after < last && *after == ')')
            return;
        recover([&]() { error(aExpression, "a conditional, quantified or declare expression must be parenthesized here"); });
    }

    ada_type* program::resolve_in_class(node* aExpression, region* aRegion, bool (*aInClass)(ada_type const*))
    {
        // (an expression expected to be of any type of a class (e.g. any integer type, 3.5.4(5)): of its interpretation in the 
        // class (a specific one rather than a universal one), else as it is)
        ada_type* t = nullptr;
        bool typed = false;
        auto const options = interpretations(aExpression, aRegion);
        for (auto const& o : options)
        {
            if (o.type != nullptr && aInClass(o.type->view()->base_type()->view()) && (t == nullptr || t->is_universal()))
                t = o.type;
            typed = typed || (o.type != nullptr && !o.type->view()->is_formal && !o.type->view()->base_type()->view()->is_formal &&
                o.type->kind != type_kind::Void);
        }
        // (none in the class: an error (e.g. an integer literal where a real one is expected, 3.5.9(6)))
        if (t == nullptr && typed && iSpeculation == 0 && iInstanceBodies == 0)
            error(aExpression, "the expression is not of the expected class of types");
        return resolve(aExpression, t != nullptr ? t->base_type() : nullptr, aRegion);
    }

    ada_type* program::resolve(node* aExpression, ada_type* aExpected, region* aRegion)
    {
        auto* result = resolve_expression(aExpression, aExpected, aRegion);
        // (a specific tagged expected type: the expression not dynamically tagged, unless it is a controlling operand (3.9.2(9/1)))
        // (an implicit conversion of an access parameter to a named access type: illegal (8.6))
        if (aExpression != nullptr && aExpected != nullptr && result != nullptr && iSpeculation == 0 && iInstanceBodies == 0 &&
            aExpected->view()->kind == type_kind::Access && !aExpected->view()->anonymous_access && result->anonymous_access &&
            result->view()->kind == type_kind::Access)
            if (auto* a = aExpression->is(construct::Parenthesized) && !aExpression->children.empty() ? aExpression->child(0) : aExpression;
                a->is(construct::Identifier) && a->ent != nullptr && a->ent->kind == entity_kind::Parameter && a->ent->type != nullptr && a->ent->type->anonymous_access)
                recover([&]() { error(aExpression, "an access parameter cannot be implicitly converted to a named access type (8.6)"); });
        // (an implicit conversion to an anonymous access-to-variable type: not from an access-to-constant type (4.6(24.10/2), 8.6(27.1/4)))
        if (aExpression != nullptr && aExpected != nullptr && result != nullptr && iSpeculation == 0 && iInstanceBodies == 0 &&
            aExpected->view()->kind == type_kind::Access && aExpected->view()->anonymous_access && !aExpected->view()->access_constant &&
            result->view()->kind == type_kind::Access && result->view()->access_constant)
            recover([&]() { error(aExpression, "an access-to-constant value cannot be converted to an access-to-variable type (4.6(24.10/2))"); });
        // (checked where the expected type is the target's: an assignment's value, an object's initialization, a return expression, an
        // operand of a qualified or parenthesized expression)
        if (aExpression != nullptr && aExpected != nullptr && result != nullptr && iSpeculation == 0 && iInstanceBodies == 0 &&
            aExpression->parent != nullptr && aExpression->parent->children.back() == aExpression &&
            (aExpression->parent->is(construct::Assignment) || aExpression->parent->is(construct::Object) || aExpression->parent->is(construct::Return) ||
             aExpression->parent->is(construct::Qualified) || aExpression->parent->is(construct::Parenthesized)) &&
            aExpected->view()->kind != type_kind::ClassWide && aExpected->view()->base_type()->view()->tagged && dynamically_tagged(aExpression) &&
            [&]()
            {
                // (the controlling operand, or the operand of a qualified or parenthesized expression that is one)
                for (node const* n = aExpression; n != nullptr; n = n->parent)
                {
                    if (n == iControllingActual)
                        return false;
                    if (n->parent == nullptr || !(n->parent->is(construct::Parenthesized) || (n->parent->is(construct::Qualified) && n->parent->children.back() == n)))
                        break;
                }
                return true;
            }())
            recover([&]() { error(aExpression, "a dynamically tagged expression cannot have a specific tagged expected type unless it is a controlling operand (3.9.2(9/1))"); });
        // (a static string expression (a string literal, or a catenation) not within a catenation (nor an assignment's value, which slides
        // to its target's bounds): its bounds in the index subtype)
        if (aExpression != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && (aExpression->is(construct::Concat) ||
            (aExpression->is(construct::Operator) && meaning_of(aExpression) == meaning::StringLiteral)) &&
            !(aExpression->parent != nullptr && aExpression->parent->is(construct::Concat)) && result != nullptr &&
            !(aExpression->parent != nullptr && aExpression->parent->is(construct::Assignment) && aExpression->is(construct::Operator) &&
              name_text(aExpression->source).size() <= 2u))
            static_string_check(aExpression, result, aRegion);
        // (a static division by zero (4.5.5(22)): illegal (4.9(33/3)))
        if (aExpression != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && (aExpression->is(construct::Divide) || aExpression->is(construct::Mod) ||
            aExpression->is(construct::Rem)) && aExpression->size() == 2u && static_expression(aExpression->child(1)) && static_expression(aExpression->child(0)) &&
            [&]()
            {
                // (of a predefined operator (a static function), not a user-defined one)
                auto const* call = call_of(aExpression);
                return call == nullptr || call->callee == nullptr || call->callee->sub == nullptr ||
                    (call->callee->sub->op != intrinsic::None && call->callee->sub->op != intrinsic::Import);
            }())
            if (auto const v = evaluate(aExpression->child(1), aRegion); ((v.is_integer() && v.integer() == bigint{ 0 }) ||
                (v.is_real() && v.real() == rational{ 0 })) && !statically_unevaluated(aExpression, aRegion))
                recover([&]() { error(aExpression, "the static expression divides by zero (it is illegal, 4.9(33/3))"); });
        // (a qualified expression or conversion of a static value: in the subtype's range (4.9(33/3), 4.6(28), 4.7(4)))
        if (aExpression != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && result != nullptr && result->view()->is_discrete() &&
            (aExpression->is(construct::Qualified) || (aExpression->is(construct::Apply) && meaning_of(aExpression) == meaning::Conversion &&
            aExpression->size() == 2u)))
            static_range_check(aExpression, aExpression->children.back(), result, false, aRegion);
        // (S'Succ, S'Pred, S'Val of static values: in S's base range (3.5(23, 26), 3.5.5(7)))
        if (aExpression != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && result != nullptr && result->view()->is_discrete())
        {
            node* attribute = aExpression->is(construct::Attribute) ? aExpression : aExpression->is(construct::Apply) && !aExpression->children.empty() &&
                aExpression->child(0)->is(construct::Attribute) ? aExpression->child(0) : nullptr;
            if (attribute != nullptr && attribute->size() >= 2u)
                if (auto const n = name_text(attribute->child(1)->source); n == "succ" || n == "pred" || n == "val")
                {
                    // (the argument: the attribute's own, or the call's)
                    node* argument = nullptr;
                    if (auto* arguments = attribute->first(construct::Arguments); arguments != nullptr && arguments->size() == 1u)
                        argument = arguments->child(0);
                    else if (aExpression != attribute && aExpression->size() == 2u)
                        argument = aExpression->child(1);
                    auto* b = result->view()->base_type();
                    auto const low = low_bound(b);
                    auto const high = high_bound(b);
                    if (argument != nullptr && low && high && !b->view()->is_formal && static_expression(argument) &&
                        !(b->view()->kind == type_kind::Modular && n != "val"))
                        if (auto const v = evaluate(argument, aRegion); v.is_integer())
                        {
                            auto const x = n == "succ" ? v.integer() + bigint{ 1 } : n == "pred" ? v.integer() - bigint{ 1 } : v.integer();
                            if ((x < *low || x > *high) && !statically_unevaluated(aExpression, aRegion))
                                recover([&]() { error(aExpression, "the static expression fails a range check (it is illegal, 4.9(33/3))"); });
                        }
                }
        }
        // (a qualified expression or conversion of a static value to a subtype with a static predicate: the value satisfies it (4.9(34/3)),
        // whatever the assertion policy)
        if (aExpression != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && result != nullptr && result->predicate != nullptr &&
            result->view()->is_discrete() && (aExpression->is(construct::Qualified) || (aExpression->is(construct::Apply) &&
            meaning_of(aExpression) == meaning::Conversion && aExpression->size() == 2u)))
        {
            bool allStatic = true;
            for (auto* p = result->predicate; p != nullptr && allStatic; p = p->parent)
                allStatic = p->is_static;
            if (allStatic)
                if (auto const v = evaluate(aExpression->children.back(), aRegion); v.is_integer())
                    if (auto const values = static_values(result, aRegion))
                    {
                        bool in = false;
                        for (auto const& [low, high] : *values)
                            in = in || (v.integer() >= low && v.integer() <= high);
                        if (!in)
                            recover([&]() { error(aExpression, "the static value does not satisfy the subtype's static predicate (4.9(34/3), 3.2.4)"); });
                    }
        }
        return result;
    }

    bool program::statically_unevaluated(node const* aExpression, region* aRegion)
    {
        auto const boolean = [&](node* n) -> std::optional<bool>
        {
            if (n == nullptr || !static_expression(n))
                return std::nullopt;
            auto const v = evaluate(n, aRegion);
            if (!v.is_integer())
                return std::nullopt;
            return v.integer() != bigint{ 0 };
        };
        for (node const* n = aExpression, *p = aExpression->parent; p != nullptr; n = p, p = p->parent)
        {
            if ((p->is(construct::AndThen) || p->is(construct::OrElse)) && p->size() >= 2u && p->child(1) == n)
            {
                if (auto const left = boolean(p->child(0)); left && *left == p->is(construct::OrElse))
                    return true;
            }
            else if (p->is(construct::IfExpression))
            {
                // (the dependent expressions: then (child 1), elsif parts, else part)
                bool earlierTrue = false;
                if (auto const c = boolean(p->child(0)); c && *c)
                    earlierTrue = true;
                if (n == p->child(1))
                {
                    if (auto const c = boolean(p->child(0)); c && !*c)
                        return true;
                }
                else
                    for (std::size_t i = 2u; i < p->size(); ++i)
                    {
                        auto* c = p->child(i);
                        if (c->is(construct::Elsif))
                        {
                            if (c == n || (c->size() >= 2u && c->child(1) == n))
                            {
                                if (c->child(0) == n)
                                    break;
                                auto const own = boolean(c->child(0));
                                if (earlierTrue || (own && !*own))
                                    return true;
                                break;
                            }
                            if (auto const own = boolean(c->child(0)); own && *own)
                                earlierTrue = true;
                        }
                        else if (c->is(construct::Else) && (c == n || c->children.back() == n))
                        {
                            if (earlierTrue)
                                return true;
                            break;
                        }
                    }
            }
            else if (p->is(construct::In) && n != p->child(0))
            {
                // (a membership's choices: evaluated until one is satisfied (4.5.2(27/3)))
                if (!static_expression(p->child(0)))
                    continue;
                auto const tested = evaluate(p->child(0), aRegion);
                for (std::size_t i = 1u; i < p->size() && p->child(i) != n; ++i)
                    if (auto* c = p->child(i); !c->is(construct::Flag) && !c->is(construct::Range) && static_expression(c))
                        if (auto const v = evaluate(c, aRegion); v.known() && tested.known() &&
                            ((v.is_integer() && tested.is_integer() && v.integer() == tested.integer()) ||
                             (v.is_real() && tested.is_real() && v.real() == tested.real())))
                            return true;
            }
            else if (p->is(construct::CaseExpression) && n->is(construct::Alternative) && n->size() >= 2u)
            {
                // (an alternative whose choices do not cover the static selector (others: unless an earlier alternative does))
                if (!static_expression(p->child(0)))
                    continue;
                auto const selector = evaluate(p->child(0), aRegion);
                if (!selector.is_integer())
                    continue;
                // (whether an alternative's choices cover it: nullopt if not known)
                auto const covers_selector = [&](node* aAlternative) -> std::optional<bool>
                {
                    bool covered = false;
                    for (auto* choice : aAlternative->child(0)->children)
                    {
                        if (choice->is(construct::Others))
                            return std::nullopt;
                        std::optional<bigint> low, high;
                        if (choice->is(construct::Range) && choice->size() >= 2u)
                        {
                            auto const l = evaluate(choice->child(0), aRegion);
                            auto const h = evaluate(choice->child(1), aRegion);
                            if (l.is_integer() && h.is_integer())
                            {
                                low = l.integer();
                                high = h.integer();
                            }
                        }
                        else if (is_subtype_name(choice, aRegion))
                        {
                            ada_type* st = nullptr;
                            {
                                speculation guard{ *this };
                                try { st = subtype_mark(choice, aRegion); } catch (compile_error const&) {}
                            }
                            if (st != nullptr)
                            {
                                low = low_bound(st);
                                high = high_bound(st);
                            }
                        }
                        else if (auto const v = evaluate(choice, aRegion); v.is_integer())
                            low = high = v.integer();
                        if (!low || !high)
                            return std::nullopt;
                        covered = covered || (selector.integer() >= *low && selector.integer() <= *high);
                    }
                    return covered;
                };
                bool othersAlternative = false;
                for (auto* choice : n->child(0)->children)
                    othersAlternative = othersAlternative || choice->is(construct::Others);
                if (othersAlternative)
                {
                    for (std::size_t i = 1u; i < p->size() && p->child(i) != n; ++i)
                        if (auto const c = covers_selector(p->child(i)); c && *c)
                            return true;
                }
                else if (auto const c = covers_selector(const_cast<node*>(n)); c && !*c)
                    return true;
            }
        }
        return false;
    }

    void program::static_range_check(node* aExpression, node* aValue, ada_type* aType, bool aBase, region* aRegion)
    {
        // (a static expression's evaluation failing a range check: illegal (4.9(33/3)); one not part of a larger static expression: in
        // its expected type's base range (4.9(34/3)))
        if (aType == nullptr || aValue == nullptr || aType->is_universal() || aType->view()->is_formal || !static_expression(aValue))
            return;
        // (a floating point type: of its base range (4.9(34/3), 3.5.7(8)); a fixed point type's base range is not known here)
        if (aType->view()->is_real() && (!aBase || aType->view()->base_type()->view()->kind != type_kind::Float))
            return;
        if (aType->view()->is_real() && aBase)
        {
            auto* b = aType->view()->base_type()->view();
            auto const v = evaluate(aValue, aRegion);
            if (!v.known() || !(v.is_real() || v.is_integer()) || !b->low.known() || !b->high.known() || !(b->low.is_real() || b->low.is_integer()) ||
                !(b->high.is_real() || b->high.is_integer()))
                return;
            rational const x = v.is_real() ? v.real() : rational{ v.integer() };
            rational const lo = b->low.is_real() ? b->low.real() : rational{ b->low.integer() };
            rational const hi = b->high.is_real() ? b->high.real() : rational{ b->high.integer() };
            if ((x < lo || x > hi) && !statically_unevaluated(aExpression, aRegion))
                recover([&]() { error(aExpression, "the static value is outside the base range of its type (4.9(34/3))"); });
            return;
        }
        if (!aType->view()->is_discrete())
            return;
        auto* t = aBase ? aType->view()->base_type() : aType;
        auto const low = low_bound(t);
        auto const high = high_bound(t);
        if (!low || !high)
            return;
        auto const v = evaluate(aValue, aRegion);
        if (!v.is_integer())
            return;
        if ((v.integer() < *low || v.integer() > *high) && !statically_unevaluated(aExpression, aRegion))
            recover([&]() { error(aExpression, aBase ? "the static value is outside the base range of its type (4.9(34/3))" :
                "the static expression fails a range check (it is illegal, 4.9(33/3))"); });
    }

    void program::static_string_check(node* aExpression, ada_type* aType, region* aRegion)
    {
        // (of a one-dimensional array type whose index subtype is static: a static value's upper bound (the lower bound's, of the index
        // subtype, plus its length less one) in the index subtype; a null value's lower bound not the base range's first)
        auto* t = aType->view();
        if (t->kind != type_kind::Array || t->indexes.size() != 1u || t->is_formal)
            return;
        auto* index = t->base_type()->view()->indexes.empty() ? nullptr : t->base_type()->view()->indexes[0];
        if (index == nullptr)
            return;
        auto const first = low_bound(index);
        auto const last = high_bound(index);
        auto const baseFirst = low_bound(index->view()->base_type());
        if (!first || !last)
            return;
        auto const value = evaluate(aExpression, aRegion);
        if (!value.is_string())
            return;
        bigint const length{ static_cast<std::int64_t>(value.string().size()) };
        // (a constrained array subtype: its own bounds apply (a length check, not this))
        if (length == bigint{ 0 })
        {
            if (baseFirst && *first == *baseFirst)
                recover([&]() { error(aExpression, "a null string value's upper bound would be outside its index type's base range (4.2(11))"); });
        }
        else if (*first + length - bigint{ 1 } > *last)
            recover([&]() { error(aExpression, "the static string value is too long for its index subtype (4.9(34/3), 4.5.3(7))"); });
    }

    ada_type* program::resolve_expression(node* aExpression, ada_type* aExpected, region* aRegion)
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
                // (a based literal's digits: less than its base, a base from 2 to 16 (2.4.2(3, 6)))
                if (iSpeculation == 0 && !rational::parse_literal(aExpression->source))
                    error(aExpression, "the numeric literal is not valid (its base must be from 2 to 16, and its digits less than the base)");
                auto* universal = is_real_literal(aExpression->source) ? iUniversalReal : iUniversalInteger;
                // (an integer literal's exponent: no minus sign (2.4.1(5)))
                if (iSpeculation == 0 && universal == iUniversalInteger)
                {
                    std::string_view const text = aExpression->source;
                    auto const hash = text.rfind('#');
                    auto const exponent = hash != std::string_view::npos ? text.find_first_of("eE", hash) : text.find_first_of("eE");
                    if (exponent != std::string_view::npos && text.find('-', exponent) != std::string_view::npos)
                        error(aExpression, "an integer literal cannot have a negative exponent (2.4.1(5))");
                }
                if (aExpected != nullptr && !covers(aExpected, universal == iUniversalReal ? iRealLiteral : iIntegerLiteral))
                    error(aExpression, "numeric literal is not of the expected type");
                // (a literal of a type with the Integer_Literal or Real_Literal aspect: a call of its function with the literal's text
                // (4.2.1(6/5)))
                if (aExpected != nullptr && !aExpected->view()->is_numeric() && !aExpected->is_universal())
                    if (auto* aspect = type_aspect(aExpected, universal == iUniversalReal ? "real_literal" : "integer_literal"))
                    {
                        auto* declaredIn = aExpected->base_type()->declared_in != nullptr ? aExpected->base_type()->declared_in : aRegion;
                        entity* function = nullptr;
                        {
                            speculation guard{ *this };
                            try
                            {
                                for (auto* f : prefix_entities(aspect, declaredIn))
                                    if (f->kind == entity_kind::Function && f->sub != nullptr && f->sub->parameters.size() == 1u && f->sub->result != nullptr &&
                                        covers(f->sub->result, aExpected))
                                        function = f;
                            }
                            catch (compile_error const&)
                            {
                            }
                        }
                        if (function == nullptr)
                            error(aExpression, "the literal function of the type is not found");
                        auto* text = &iClones.emplace_back();
                        text->kind = construct::Operator;
                        text->source = iSyntheticSources.emplace_back("\"" + std::string{ aExpression->file != nullptr ? aExpression->file->original_of(aExpression->source) :
                            aExpression->source } + "\"");
                        text->parent = aExpression;
                        resolve(text, function->sub->parameters[0].type, aRegion);
                        auto* callee = &iClones.emplace_back();
                        callee->kind = construct::Identifier;
                        callee->source = aExpression->source;
                        callee->file = aExpression->file;
                        callee->parent = aExpression;
                        callee->ent = function;
                        set_meaning(callee, meaning::Subprogram);
                        aExpression->kind = construct::Apply;
                        aExpression->children = { callee, text };
                        auto& call = iCalls[aExpression];
                        call.callee = function;
                        call.actuals = { text };
                        set_meaning(aExpression, meaning::Call);
                        result = function->sub->result;
                        break;
                    }
                result = aExpected != nullptr && !aExpected->is_universal() ? aExpected : universal;
                set_meaning(aExpression, meaning::Value);
                break;
            }
        case construct::Null:
            if (aExpected != nullptr && (!aExpected->view()->is_access() || (iSpeculation == 0 && iInstanceBodies == 0 &&
                !aExpected->view()->is_formal && partial_view_here(aExpected, aRegion))))
                error(aExpression, "null is not of the expected type");
            result = aExpected != nullptr ? aExpected : iUniversalAccess;
            set_meaning(aExpression, meaning::Value);
            break;
        case construct::Identifier:
        case construct::Selected:
        case construct::Operator:
        case construct::Character:
            result = resolve_name(aExpression, aExpected, aRegion);
            // (a string literal: of a string type, its component type a character type (not a partial view here, 4.2(4), 7.3(4)))
            if (aExpression->is(construct::Operator) && !is_operator_symbol(aExpression) && iSpeculation == 0 && iInstanceBodies == 0 &&
                aExpected != nullptr && aExpected->view()->kind == type_kind::Array && aExpected->view()->component != nullptr &&
                partial_component_here(aExpected->view(), aRegion))
                error(aExpression, "a string literal requires a string type (its component type is not a character type here)");
            break;
        case construct::Apply:
            result = resolve_apply(aExpression, aExpected, aRegion);
            // (an indexed component: of the expected type (a scalar one: else not covered))
            if (meaning_of(aExpression) == meaning::Index && aExpected != nullptr && result != nullptr && iSpeculation == 0 && iInstanceBodies == 0 &&
                aExpected->view()->is_scalar() && result->view()->is_scalar() && !aExpected->is_universal() && !result->is_universal() &&
                !aExpected->view()->is_formal && !result->view()->is_formal && !covers(aExpected, result))
                error(aExpression, "the component is not of the expected type");
            break;
        case construct::Attribute:
            result = resolve_attribute(aExpression, aExpected, aRegion);
            // ('First, 'Last of an array: of its index type)
            if (aExpected != nullptr && result != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && aExpression->size() >= 2u &&
                (name_text(aExpression->child(1)->source) == "first" || name_text(aExpression->child(1)->source) == "last") &&
                aExpected->view()->is_scalar() && result->view()->is_scalar() && !aExpected->is_universal() && !result->is_universal() &&
                !aExpected->view()->is_formal && !result->view()->is_formal && !covers(aExpected, result))
                error(aExpression, "the attribute is not of the expected type");
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
                // (others in an aggregate: only with an applicable index constraint (4.3.3(13)): a constrained subtype)
                if (iSpeculation == 0)
                    others_check(aExpression->child(1), t);
                // (of the expected type)
                if (aExpected != nullptr && aExpected != iVoid && !aExpected->is_universal() && t != nullptr && iSpeculation == 0 &&
                    !covers(aExpected, t))
                    error(aExpression, "the qualified expression is not of the expected type");
                break;
            }
        case construct::All:
            {
                auto const options = interpretations(aExpression->child(0), aRegion);
                ada_type* accessType = nullptr;
                for (auto const& o : options)
                    if (o.type != nullptr && (o.type->view()->kind == type_kind::Access || o.type->view()->kind == type_kind::AccessSubprogram) &&
                        !(iSpeculation == 0 && iInstanceBodies == 0 && !o.type->view()->is_formal && partial_view_here(o.type, aRegion)))
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
                // (a boolean type (not a partial view of one))
                ada_type* t = aExpected != nullptr && aExpected->view()->base_type()->boolean && !partial_view_here(aExpected, aRegion) ? aExpected : nullptr;
                if (t == nullptr)
                {
                    for (auto const& l : interpretations(aExpression, aRegion))
                        if (l.type != nullptr && l.type->view()->base_type()->boolean && !partial_view_here(l.type, aRegion))
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
                    {
                        choices.push_back(aExpression->child(i));
                        // (a membership choice: an expression, a range or a subtype mark (no constraint) (4.5.2(3.1/4)))
                        if (auto* c = aExpression->child(i); c->is(construct::SubtypeIndication) && iSpeculation == 0)
                            for (auto* x : c->children)
                                if (x->is(construct::ConstraintRange) || x->is(construct::ConstraintIndex) || x->is(construct::ConstraintDigits) ||
                                    x->is(construct::ConstraintDelta))
                                    recover([&]() { error(x, "a membership choice cannot have a constraint (a subtype mark or a range)"); });
                        // (an index constraint: parsed as a name)
                        if (auto* c = aExpression->child(i); c->is(construct::Apply) && iSpeculation == 0 && !c->children.empty() &&
                            is_subtype_name(c->child(0), aRegion) && [&]()
                            {
                                // (ranges, or (for a composite type: an index or discriminant constraint, not a conversion) scalar values)
                                ada_type* ct = nullptr;
                                {
                                    speculation guard{ *this };
                                    try { ct = subtype_mark(c->child(0), aRegion); } catch (compile_error const&) {}
                                }
                                bool const composite = ct != nullptr && (ct->view()->kind == type_kind::Array || ct->view()->kind == type_kind::Record);
                                bool constraint = true;
                                for (auto a = c->children.begin() + 1; a != c->children.end() && constraint; ++a)
                                {
                                    if ((*a)->is(construct::Range) || ((*a)->is(construct::Attribute) && (*a)->size() >= 2u && name_text((*a)->child(1)->source) == "range"))
                                        continue;
                                    if (!composite || (*a)->is(construct::Association))
                                    {
                                        constraint = composite;
                                        continue;
                                    }
                                    for (auto const& o : interpretations(*a, aRegion))
                                        if (o.type != nullptr && !o.type->view()->is_scalar() && !o.type->is_universal())
                                            constraint = false;
                                }
                                return constraint;
                            }())
                            recover([&]() { error(c, "a membership choice cannot have a constraint (a subtype mark or a range)"); });
                    }
                ada_type* t = nullptr;
                auto const options = interpretations(tested, aRegion);
                // (a choice that can be of the type)
                auto const choice_fits = [&](ada_type* aType, node* aChoice)
                {
                    if (aChoice->is(construct::SubtypeIndication) || is_subtype_name(aChoice, aRegion))
                    {
                        // (an untagged subtype's type: the tested type (4.5.2(3/3)))
                        ada_type* ct = nullptr;
                        {
                            speculation guard{ *this };
                            try { ct = subtype_mark(aChoice->is(construct::SubtypeIndication) ? aChoice->child(0) : aChoice, aRegion); } catch (compile_error const&) {}
                        }
                        if (ct == nullptr || ct->view()->kind == type_kind::ClassWide || ct->view()->base_type()->view()->tagged || aType->is_universal() ||
                            aType->view()->kind == type_kind::ClassWide || aType->view()->base_type()->view()->tagged || ct->view()->anonymous_access ||
                            aType->view()->anonymous_access)
                            return true;
                        return covers(ct, aType);
                    }
                    if (aChoice->is(construct::Attribute))
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
                // (the tested expression: no applicable index constraint (an array aggregate there has no others, 4.3.3(15)))
                if (iSpeculation == 0 && iInstanceBodies == 0 && t != nullptr && t->view()->kind == type_kind::Array)
                {
                    std::vector<node*> others;
                    others_choices(tested, t->view()->indexes.size(), others);
                    for (auto* o : others)
                        recover([&]() { error(o, "others is not allowed in the tested expression of a membership test (no applicable index constraint)"); });
                }
                if (t == nullptr)
                    t = tested->type;
                for (auto* c : choices)
                {
                    if (c->is(construct::Range))
                        resolve_range_bounds(c, t != nullptr ? t->base_type() : nullptr, aRegion);
                    else if (c->is(construct::SubtypeIndication) || is_subtype_name(c, aRegion) ||
                        (c->is(construct::Attribute) && name_text(c->child(1)->source) == "base" && is_subtype_name(c->child(0), aRegion)))
                    {
                        auto* ct = discrete_range(c, t, aRegion);
                        // (an untagged tested type: the tested expression's expected type (4.5.2(3/3)))
                        if (ct != nullptr && t != nullptr && iSpeculation == 0 && !t->is_universal() && !ct->view()->base_type()->view()->tagged &&
                            ct->view()->kind != type_kind::ClassWide && t->view()->kind != type_kind::ClassWide && !t->view()->base_type()->view()->tagged &&
                            !ct->view()->anonymous_access && !t->view()->anonymous_access && !covers(ct, t))
                            recover([&]() { error(c, "the membership's subtype must be of the type of the tested expression"); });
                    }
                    else if (c->is(construct::Attribute) && (name_text(c->child(1)->source) == "range"))
                        c->type = range_of(c, aRegion);
                    else if (c->is(construct::Attribute) && name_text(c->child(1)->source) == "class" && is_subtype_name(c->child(0), aRegion))
                    {
                        // (T'Class: a subtype mark (4.5.2(3/4)))
                        resolve(c, nullptr, aRegion);
                        set_meaning(c, meaning::Type);
                    }
                    else
                    {
                        resolve(c, t != nullptr && t->view()->kind != type_kind::ClassWide ? t->base_type() : t, aRegion);
                        // (an expression choice of a limited tested type: its primitive equality (visible here) is used (4.5.2(28.1/4),
                        // 4.5.2(4.1/4)))
                        if (t != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && limited_type(t, aRegion))
                        {
                            bool equality = false;
                            auto* b = t->view()->kind == type_kind::ClassWide && t->view()->specific != nullptr ? t->view()->specific->base_type() : t->base_type();
                            for (auto* list : { &b->primitives, b->partial != nullptr ? &b->partial->primitives : nullptr,
                                b->full != nullptr ? &b->full->base_type()->primitives : nullptr })
                                if (list != nullptr)
                                    for (auto* op : *list)
                                        if (op != nullptr && op->name == "=" && op->sub != nullptr && op->sub->op == intrinsic::None && !op->sub->is_abstract &&
                                            (!op->is_private || op->scope == nullptr || op->scope->owner == nullptr ||
                                                private_part_visible(op->scope->owner, aRegion)))
                                            equality = true;
                            if (!equality)
                                recover([&]() { error(c, "a membership choice of a limited type needs a visible primitive equality operator"); });
                        }
                    }
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
            // (a null record aggregate: of a record type without components (4.3.1(15/3)))
            if (aExpression->is(construct::AggregateNullRecord) && result != nullptr && iSpeculation == 0 && iInstanceBodies == 0)
                if (auto* v = result->view()->base_type()->view(); v->kind == type_kind::Record && !v->is_formal &&
                    std::any_of(v->components.begin(), v->components.end(), [](component_info const& c) { return c.ent != nullptr && !c.stored; }))
                    error(aExpression, "a null record aggregate is only for a record type without components");
            break;
        case construct::Allocator:
            {
                if (aExpected == nullptr || aExpected->view()->kind != type_kind::Access || (iSpeculation == 0 && iInstanceBodies == 0 &&
                    !aExpected->view()->is_formal && partial_view_here(aExpected, aRegion)))
                    error(aExpression, "allocator needs an access type context");
                // (of an access type declared where the default pool is null, without a pool of its own (13.11.3))
                if (aExpected != nullptr && iSpeculation == 0 && iInstanceBodies == 0)
                    if (auto* b = aExpected->view()->base_type()->view(); (iNullPoolTypes.contains(b) || iNullPoolTypes.contains(aExpected->view())) &&
                        type_aspect(b, "storage_pool") == nullptr && type_aspect(b, "storage_size") == nullptr)
                        recover([&]() { error(aExpression, "an allocator cannot be of an access type whose default storage pool is null (13.11.3)"); });
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
                    if (aExpected != nullptr && aExpected->view()->kind == type_kind::Access)
                        aliased_actuals_check(qualified, aExpected, aRegion);
                    if (qualified->type != nullptr)
                        limited_value_check(qualified, qualified->type, aRegion);
                    // (a class-wide designated type: the qualified expression's type not statically deeper than the allocator's type
                    // (4.8(5.1/3)))
                    if (qualified->type != nullptr && aExpected != nullptr && aExpected->view()->kind == type_kind::Access && iSpeculation == 0 &&
                        (iInstanceBodies == 0 || iInstanceBodies == iInstancePrivateParts) && !within_generic_body(aRegion) && !aExpected->view()->anonymous_access)
                        if (auto* d = designated_of(aExpected); d != nullptr && d->view()->kind == type_kind::ClassWide)
                        {
                            auto* qt = qualified->type->view()->kind == type_kind::ClassWide && qualified->type->view()->specific != nullptr ?
                                qualified->type->view()->specific : qualified->type;
                            if (!qt->view()->is_formal && type_level(qt) > type_level(aExpected))
                                error_in_private_part_too(qualified, "the type of an allocator's qualified expression is statically deeper than the allocator's access type (4.8(5.1/3))");
                        }
                }
                else
                {
                    // (an uninitialized allocator's subtype indication: no null exclusion (4.8(5.3/2)))
                    if (iSpeculation == 0 && iInstanceBodies == 0 && (indication->has_flag("not null") || aExpression->has_flag("not null")))
                        recover([&]() { error(indication, "the subtype indication of an uninitialized allocator cannot have a null exclusion (4.8(5.3/2))"); });
                    auto* t = subtype_indication(indication, aRegion);
                    // (an object of an abstract type (3.9.3(8/3)))
                    if (abstract_type(t, aRegion))
                        error(indication, "an allocator cannot create an object of an abstract type");
                    // (an uninitialized allocator of an access type: no constraint (4.8(4)))
                    if (iSpeculation == 0 && iInstanceBodies == 0 && t != nullptr && t->view()->is_access() &&
                        ((indication->is(construct::SubtypeIndication) && (indication->first(construct::Apply) != nullptr ||
                          std::count_if(indication->children.begin(), indication->children.end(), [](node* c) { return !c->is(construct::Flag); }) > 1)) ||
                         indication->is(construct::Apply)))
                        recover([&]() { error(indication, "the subtype indication of an uninitialized allocator of an access type cannot have a constraint (4.8(4))"); });
                    // (an uninitialized allocator's subtype: definite (4.8(4)))
                    if (indefinite_subtype(t, indication, aRegion))
                        error(indication, "an uninitialized allocator's subtype must be definite");
                    // (an allocator freezes its subtype (13.14(4/1)): completely defined (7.3(5)))
                    if (iSpeculation == 0 && iInstanceBodies == 0 && not_completely_defined(t, aRegion))
                        recover([&]() { error(indication, "an allocator's subtype must be completely defined (its full declaration has not occurred yet)"); });
                    // (an access discriminant's X'Access: X not statically deeper than the allocator's type (3.10.2(12.1/2), 28/3))
                    if (t != nullptr && aExpected != nullptr && aExpected->view()->kind == type_kind::Access && iSpeculation == 0 &&
                        iInstanceBodies == 0 && !within_generic_body(aRegion))
                        for (auto* value : t->view()->discriminant_constraint)
                        {
                            while (value != nullptr && value->is(construct::Parenthesized))
                                value = value->child(0);
                            if (value == nullptr || !value->is(construct::Attribute) || value->size() < 2u || name_text(value->child(1)->source) != "access" ||
                                value->type == nullptr || !value->type->view()->anonymous_access)
                                continue;
                            if (auto const level = static_level(value->child(0), aRegion); level && *level > type_level(aExpected))
                                error(value->child(0), "the prefix of 'Access is statically deeper than the allocator's access type (3.10.2(12.1/2))");
                        }
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
                parenthesis_check(aExpression);
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
                // (without an else part: of a boolean type (4.5.7(9/3)))
                if (aExpression->first(construct::Else) == nullptr && iSpeculation == 0 && !result->view()->base_type()->view()->boolean)
                    recover([&]() { error(aExpression, "an if expression without an else part must be of a boolean type"); });
                break;
            }
        case construct::CaseExpression:
            {
                parenthesis_check(aExpression);
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
                std::size_t discreteTypes = 0u;
                for (auto const& o : interpretations(selector, aRegion))
                    if (o.type != nullptr && o.type->view()->is_discrete() && !o.type->is_universal())
                    {
                        if (discrete == nullptr || !same_type(discrete, o.type))
                            ++discreteTypes;
                        discrete = o.type;
                    }
                // (resolved without the choices: of a single discrete type (4.5.7(16/3), 5.4(4)))
                bool const ambiguous = (discreteTypes > 1u || selector->is(construct::Character)) && iSpeculation == 0 && iInstanceBodies == 0;
                if (ambiguous)
                    recover([&]() { error(selector, "the selector of a case expression is ambiguous"); });
                auto* selectorType = resolve(selector, discrete, aRegion);
                // (its choices: as a case statement's (4.5.7(16/3)); not against a guessed type)
                if (iSpeculation == 0 && !ambiguous)
                    case_choices_check(aExpression, selector, selectorType, std::vector<node*>(aExpression->children.begin() + 1, aExpression->children.end()), aRegion);
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                {
                    auto* alternative = aExpression->child(i);
                    if (iSpeculation != 0)
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
                parenthesis_check(aExpression);
                auto* r = new_region(nullptr, aRegion);
                iterator_specification(aExpression->child(1), r);
                condition(aExpression->children.back(), r);
                set_meaning(aExpression, meaning::Value);
                result = iBoolean;
                break;
            }
        case construct::DeclareExpression:
            {
                parenthesis_check(aExpression);
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
        {
            if (auto* d = implicit_dereference(result); d != nullptr && d != result && covers(aExpected, d))
            {
                iImplicitDereferences[aExpression] = result;
                result = d;
            }
            // (an indexed component or a slice: of the expected type (8.6(22)); here, one of another class of type)
            else if (aExpression->is(construct::Apply) && (meaning_of(aExpression) == meaning::Index || meaning_of(aExpression) == meaning::Slice) &&
                iSpeculation == 0 && iInstanceBodies == 0 && [&]()
                {
                    auto* r = result->view()->base_type()->view();
                    auto* e = aExpected->view()->base_type()->view();
                    if (r->is_formal || e->is_formal || r->kind == type_kind::Private || e->kind == type_kind::Private || r->is_universal() || e->is_universal() ||
                        r->kind == type_kind::ClassWide || e->kind == type_kind::ClassWide)
                        return false;
                    bool const rNumeric = r->is_numeric();
                    bool const eNumeric = e->is_numeric();
                    return r->kind != e->kind && !(rNumeric && eNumeric);
                }())
                error(aExpression, "the indexed component or slice is not of the expected type");
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
            speculation guard{ *this };
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

    bool program::partial_component_here(ada_type* aArray, region* aRegion)
    {
        // (an array type's component of a private type: its full view's characteristics (for the array's operators) only within the
        // array type's immediate scope (7.3.1(5/1)))
        auto* component = aArray->component;
        if (partial_view_here(component, aRegion))
            return true;
        auto* partial = component->kind == type_kind::Private ? component : component->base != nullptr && component->base->kind == type_kind::Private ? component->base : nullptr;
        auto* base = aArray->view()->base_type();
        auto* where = base->declared_in;
        if (partial == nullptr || partial->is_formal || where == nullptr || iInstanceBodies != 0 || !iInstantiationRegions.empty() ||
            within_scope(where, aRegion) || partial->declared_in == nullptr || partial->declared_in->owner == nullptr)
            return false;
        // (the component's full view not visible where the array type is declared: in the package's visible part (not its private part
        // or body))
        auto* package = partial->declared_in;
        node const* declaration = base->decl != nullptr ? base->decl->decl : nullptr;
        if (declaration == nullptr || package->private_part == nullptr)
            return false;
        bool visiblePart = false;
        for (node const* n = declaration; n != nullptr; n = n->parent)
        {
            if (n == package->private_part || n->is(construct::PackageBody))
                return false;
            if (package->owner != nullptr && n == package->owner->decl)
                visiblePart = true;
            if (package->owner != nullptr && package->owner->decl != nullptr && n == package->owner->decl->parent)
                visiblePart = true;
        }
        return visiblePart;
    }

    bool program::operators_visible(ada_type* aType, region* aRegion)
    {
        // (a type's operators (predefined, or declared with it): directly visible within the region of its declaration, use-visible by
        // a use clause of its package or a use type clause (8.3, 8.4); within an instance: as in its generic)
        if (aType == nullptr || aRegion == nullptr || iInstanceBodies != 0 || !iInstantiationSites.empty())
            return true;
        auto* b = aType->view()->base_type()->view();
        if (b->kind == type_kind::ClassWide && b->specific != nullptr)
            b = b->specific->view()->base_type()->view();
        // (an anonymous access type's equality: Standard's universal_access operators (4.5.2(7.1/2)))
        if (b->is_universal() || b->is_formal || b->anonymous_access || aType->anonymous_access)
            return true;
        std::vector<region*> places;
        for (auto* x : { b, b->partial, aType->base_type(), aType->base_type()->partial })
            if (x != nullptr && x->declared_in != nullptr)
                places.push_back(x->declared_in);
        if (places.empty())
            return true;
        for (auto* where : places)
            if (where == iStandard || where->generic_formals || (where->owner != nullptr && (where->owner->is_generic() || iTemplateInstances.contains(where->owner))))
                return true;
        auto const uses = [&](region* r)
        {
            for (auto* used : r->used)
                for (auto* p = used; p != nullptr; p = p->renamed != nullptr && p->renamed != p ? p->renamed : nullptr)
                {
                    if (std::find(places.begin(), places.end(), p->inner) != places.end())
                        return true;
                    if (p->renamed == nullptr || p->renamed == p)
                        break;
                }
            for (auto* t : r->used_types)
                if (t != nullptr && (t->view()->base_type()->view() == b || t->base_type() == aType->base_type()))
                    return true;
            return false;
        };
        for (region* jump = nullptr, *r = aRegion; r != nullptr; r = outward(r, jump))
            if (std::find(places.begin(), places.end(), r) != places.end() || uses(r))
                return true;
        // (the context clause's use clauses)
        if (auto* context = current_context(); context != nullptr && uses(context))
            return true;
        return false;
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
        // (a partial view whose full view is not visible here: the partial view (its operators are only "=" and "/=", if it is not
        // limited, 7.3(4)))
        auto const seen = [&](ada_type* aType) -> ada_type*
        {
            auto* partial = aType->kind == type_kind::Private ? aType : aType->base != nullptr && aType->base->kind == type_kind::Private ? aType->base : nullptr;
            // (a formal type's actual within the instance: the formal's operators (12.3(15)))
            if (partial != nullptr && partial->full != nullptr && !partial->is_formal && formal_actual_instance(partial, aRegion) == nullptr &&
                formal_actual_instance(aType->view()->base_type()->view(), aRegion) == nullptr)
                if (auto* package = partial->declared_in != nullptr ? partial->declared_in->owner : nullptr;
                    package != nullptr && package->is_package() && !private_part_visible(package, aRegion))
                    return partial;
            return aType->view()->base_type()->view();
        };
        for (auto* operand : aOperands)
            for (auto const& i : interpretations(operand, aRegion))
                if (i.type != nullptr)
                    for (auto* it : { i.type, implicit_dereference(i.type), i.type->view()->kind == type_kind::ClassWide ? i.type->view()->specific : nullptr })
                        if (it != nullptr && std::find(types.begin(), types.end(), seen(it)) == types.end())
                            types.push_back(seen(it));
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
            // (a private type outside the scope of its full view: only equality (if not limited) is predefined for it (7.3(4)))
            bool partialView = false;
            bool partialLimited = false;
            if (t->kind == type_kind::Private && t->full != nullptr)
            {
                auto* package = t->base_type()->declared_in != nullptr ? t->base_type()->declared_in->owner : nullptr;
                partialView = package != nullptr && package->is_package() && !private_part_visible(package, aRegion);
                partialLimited = t->limited || t->base_type()->limited;
            }
            // (a type derived from a partial view: its characteristics, where the full view is not visible)
            else if (t->derived_partial != nullptr && iInstanceBodies == 0 && partial_view_here(t, aRegion))
            {
                partialView = true;
                partialLimited = t->derived_partial->limited;
            }
            // (a limited type: no predefined equality (4.5.2(9/3), 7.5))
            bool const limited = (aDesignator == "=" || aDesignator == "/=") && formal_actual_instance(t, aRegion) == nullptr && limited_type(t, aRegion);
            // (an array type whose component type is a partial view here: no ordering or logical operators (7.3.1(5/1)))
            bool const partialComponent = t->kind == type_kind::Array && t->component != nullptr && partial_component_here(t, aRegion);
            // (... of a limited private type: no concatenation or equality either)
            bool const limitedComponent = partialComponent && t->component != nullptr && (t->component->limited ||
                (t->component->base != nullptr && t->component->base->limited) || t->component->base_type()->limited);
            if (!operators_visible(t, aRegion))
                continue;
            for (auto* op : operators_of(t))
                if (op->name == aDesignator && op->sub->parameters.size() == aOperands.size() &&
                    (!partialView || (!partialLimited && (aDesignator == "=" || aDesignator == "/="))) &&
                    !(partialComponent && aDesignator != "=" && aDesignator != "/=" && aDesignator != "&") &&
                    !(limitedComponent && (aDesignator == "=" || aDesignator == "/=" || aDesignator == "&")) &&
                    !(limited && (op->sub->op == intrinsic::Eq || op->sub->op == intrinsic::Ne || op->sub->op == intrinsic::NotEqual)))
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
        // (the expected type's operators, for operands that are literals or aggregates (visible here, operators_visible))
        if (aExpected != nullptr && !aExpected->is_universal() && operators_visible(aExpected, aRegion))
        {
            // (a partial view outside the scope of its full view: only its equality (7.3(4)), as for the operands' types)
            bool const partialView = partial_view_here(aExpected, aRegion);
            bool const partialComponent = aExpected->view()->kind == type_kind::Array && aExpected->view()->component != nullptr &&
                partial_component_here(aExpected->view(), aRegion);
            auto* ec = partialComponent ? aExpected->view()->component : nullptr;
            bool const limitedComponent = ec != nullptr && (ec->limited || (ec->base != nullptr && ec->base->limited) || ec->base_type()->limited);
            for (auto* op : operators_of(aExpected))
                if (op->name == designator && op->sub->parameters.size() == operands.size() && std::find(candidates.begin(), candidates.end(), op) == candidates.end() &&
                    !(partialView && designator != "=" && designator != "/=") && !(partialComponent && designator != "=" && designator != "/=" && designator != "&") &&
                    !(limitedComponent && (designator == "=" || designator == "/=" || designator == "&")))
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
                    if (i.type != nullptr && i.type->view()->is_access() && !(iInstanceBodies == 0 && (partial_view_here(i.type, aRegion) ||
                        i.type->view()->base_type()->view()->parent != nullptr) && limited_type(i.type, aRegion)))
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
            // (an operand of a predefined operator: no applicable index constraint, so an array aggregate there has no others
            // (4.3.3(10/5)-(15.1/3)))
            if (op->sub->op != intrinsic::None && op->sub->op != intrinsic::NotEqual && expected != nullptr && iSpeculation == 0 &&
                iInstanceBodies == 0 && expected->view()->kind == type_kind::Array)
            {
                std::vector<node*> others;
                others_choices(operands[i], expected->view()->indexes.size(), others);
                for (auto* c : others)
                    recover([&]() { error(c, "others is not allowed in an operand of a predefined operator (no applicable index constraint)"); });
            }
        }
        aOperator->ent = op;
        set_meaning(aOperator, meaning::Call);
        class_wide_contract_call_check(op, aOperator, operands);
        // (a fixed-fixed multiplying operator: not an operand of another (its expected type would be universal_fixed, 4.5.5(19.1/2)))
        if ((op->sub->op == intrinsic::UniversalFixedMultiply || op->sub->op == intrinsic::UniversalFixedDivide) && iSpeculation == 0 && iInstanceBodies == 0)
            for (auto* operand : operands)
            {
                auto* o = operand;
                while (o != nullptr && o->is(construct::Parenthesized))
                    o = o->child(0);
                if (o != nullptr && o->ent != nullptr && o->ent->sub != nullptr && meaning_of(o) == meaning::Call &&
                    (o->ent->sub->op == intrinsic::UniversalFixedMultiply || o->ent->sub->op == intrinsic::UniversalFixedDivide))
                    recover([&]() { error(o, "the result of a fixed point multiplication or division cannot be of universal_fixed here (its type must be identified, 4.5.5(19.1/2))"); });
            }
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
        // (not an Access attribute: its expected type must be a single access type (3.10.2(2/2)))
        if (iSpeculation == 0)
        {
            auto* o = aOperand;
            while (o != nullptr && o->is(construct::Parenthesized))
                o = o->child(0);
            if (o != nullptr && o->is(construct::Attribute) && o->size() >= 2u && o->first(construct::Arguments) == nullptr &&
                (name_text(o->child(1)->source) == "access" || name_text(o->child(1)->source) == "unchecked_access"))
                recover([&]() { error(o, "the operand of a type conversion cannot be an Access attribute (its expected type must be a single access type, 3.10.2(2/2))"); });
            // (nor an allocator, an aggregate, a string or character literal, or null (their types are determined by their context,
            // 4.3(3/5), 4.8(3/3), 4.2(4)))
            if (o != nullptr && (o->is(construct::Allocator) || o->is(construct::Aggregate) || o->is(construct::Null) || o->is(construct::Character) ||
                o->is(construct::Operator)))
                recover([&]() { error(o, "the operand of a type conversion cannot be an allocator, an aggregate, a literal string or character, or null"); });
        }
        auto const options = interpretations(aOperand, aRegion);
        auto* target = aTarget->view();
        ada_type* chosen = nullptr;
        // (a tagged target type: the operand's type a descendant of it, or class-wide covering it (a conversion toward the root, or a
        // checked one away from it); an interface either way (4.6(21.3/2-23/2)))
        auto const taggedConvertible = [&](ada_type* aTo, ada_type* aFrom)
        {
            auto* to = aTo->view();
            auto* from = aFrom->view();
            bool const classWide = from->kind == type_kind::ClassWide;
            auto* ts = to->kind == type_kind::ClassWide && to->specific != nullptr ? to->specific : aTo;
            auto* fs = classWide && from->specific != nullptr ? from->specific : aFrom;
            auto const interfaceType = [](ada_type* x) { return x->view()->base_type()->view()->kind == type_kind::Interface; };
            if (interfaceType(ts) || interfaceType(fs) || ts->view()->is_formal || fs->view()->is_formal || ts->view()->base_type()->view()->is_formal ||
                fs->view()->base_type()->view()->is_formal || iInstanceBodies != 0)
                return true;
            return same_type(ts, fs) || is_descendant(fs, ts) || (classWide && is_descendant(ts, fs));
        };
        for (auto const& o : options)
        {
            if (o.type == nullptr)
                continue;
            auto* t = o.type->view();
            bool convertible = false;
            // (a partial view here (its full view not visible): its own characteristics (not numeric, not an array); related only by
            // derivation from it)
            bool const partial = iSpeculation == 0 && iInstanceBodies == 0 && (partial_view_here(aTarget, aRegion) || partial_view_here(o.type, aRegion));
            if (partial)
            {
                // (the types it is derived from (through partial views whose full views are not visible here): related by
                // derivation when they have a common ancestor)
                auto const ancestors = [&](ada_type* x)
                {
                    std::set<ada_type*> result;
                    for (int depth = 0; x != nullptr && depth < 64; ++depth)
                    {
                        if (x->kind == type_kind::Private && partial_view_here(x, aRegion))
                        {
                            // (a subtype of the partial view: its type's)
                            result.insert(x);
                            if (x->base != nullptr && x->base != x)
                                result.insert(x = x->base);
                            x = x->parent;
                            continue;
                        }
                        x = x->base != nullptr && x->kind != type_kind::Private ? x->base : x;
                        result.insert(x);
                        auto* v = x->view();
                        result.insert(v);
                        x = v->derived_partial != nullptr ? v->derived_partial : v->parent;
                    }
                    return result;
                };
                auto const at = ancestors(aTarget);
                auto const ao = ancestors(o.type);
                convertible = same_type(aTarget, o.type) || covers(aTarget, o.type) || covers(o.type, aTarget) ||
                    std::any_of(at.begin(), at.end(), [&](ada_type* x) { return ao.contains(x); }) ||
                    ((target->tagged || target->kind == type_kind::ClassWide) && (t->tagged || t->kind == type_kind::ClassWide));
                // (a universal (numeric) operand: not to a partial view here (not a numeric type here))
                if (o.type->is_universal() && partial_view_here(aTarget, aRegion))
                    convertible = false;
            }
            else if (target->is_numeric() && t->is_numeric())
                convertible = true;
            else if (same_type(target, t) || covers(target, t) || covers(t, target))
                convertible = true;
            else if (target->kind == type_kind::Array && t->kind == type_kind::Array)
            {
                // (array types of the same dimensionality, with convertible index types and the same component type (4.6(24.2-24.7)))
                convertible = target->indexes.size() == t->indexes.size() && target->component != nullptr && t->component != nullptr &&
                    same_type(target->component, t->component);
                // (the component subtypes: statically matching (4.6(24.5/2)))
                if (convertible && iInstanceBodies == 0 && !statically_matching(target->component, t->component))
                    convertible = false;
                for (std::size_t k = 0u; convertible && k < target->indexes.size(); ++k)
                {
                    auto* a = target->indexes[k]->view()->base_type()->view();
                    auto* b = t->indexes[k]->view()->base_type()->view();
                    convertible = (a->is_numeric() && b->is_numeric()) || same_type(a, b) || a->root == b->root;
                }
            }
            else if (target->is_access() && t->is_access())
            {
                // (to a pool-specific access-to-object type: only from a type related by derivation (4.6(24.11/2), Ada 83 4.6(5)))
                auto* tb = target->base_type()->view();
                auto* ob = t->base_type()->view();
                convertible = !(tb->kind == type_kind::Access && ob->kind == type_kind::Access && !tb->access_all && !tb->access_constant &&
                    !tb->anonymous_access && !ob->anonymous_access && !tb->is_formal && !ob->is_formal && iInstanceBodies == 0 &&
                    (tb->root != nullptr ? tb->root : tb)->view() != (ob->root != nullptr ? ob->root : ob)->view());
                // (general access types designating tagged types: the designated types convertible (4.6(24.15/2)))
                if (convertible && tb->kind == type_kind::Access && ob->kind == type_kind::Access && tb->designated != nullptr && ob->designated != nullptr &&
                    (tb->root != nullptr ? tb->root : tb)->view() != (ob->root != nullptr ? ob->root : ob)->view())
                {
                    auto* td = tb->designated->view();
                    auto* od = ob->designated->view();
                    if ((td->base_type()->view()->tagged || td->kind == type_kind::ClassWide) && (od->base_type()->view()->tagged || od->kind == type_kind::ClassWide))
                        convertible = taggedConvertible(tb->designated, ob->designated);
                }
            }
            else if ([&]()
                {
                    // (tagged views: not a type derived from an untagged partial view (whose full view is tagged, 7.3(7.3/2)))
                    auto const tagged = [](ada_type* x) { return x->kind == type_kind::ClassWide || (x->base_type()->view()->tagged &&
                        x->base_type()->derived_partial == nullptr && x->derived_partial == nullptr); };
                    return tagged(target) && tagged(t);
                }())
                convertible = taggedConvertible(aTarget, o.type);
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
            // (an operand of a type that is not convertible to the target (4.6(8-24)); a formal type's: not checked here)
            auto* t = chosen != nullptr ? chosen->view() : nullptr;
            if (t != nullptr && (!t->is_universal() || (iSpeculation == 0 && iInstanceBodies == 0 && partial_view_here(aTarget, aRegion))) &&
                t->kind != type_kind::String && t->kind != type_kind::Aggregate &&
                t->kind != type_kind::CharacterLiteral && t->kind != type_kind::Void && !t->is_formal && !target->is_formal &&
                !t->base_type()->view()->is_formal && !target->base_type()->view()->is_formal && t->kind != type_kind::Private &&
                target->kind != type_kind::Private)
                error(aOperand, "operand of a type conversion not convertible to the target type");
        }
        if (chosen != nullptr && (chosen->kind == type_kind::String || chosen->kind == type_kind::Aggregate))
            chosen = aTarget;
        resolve(aOperand, chosen != nullptr && !chosen->is_universal() ? chosen : nullptr, aRegion);
        if (target->kind == type_kind::Access)
            aliased_actuals_check(aOperand, aTarget, aRegion);
        // (to an access-to-variable type: not from an access-to-constant type (4.6(24.10/2)))
        if (chosen != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && target->kind == type_kind::Access && !target->access_constant &&
            chosen->view()->kind == type_kind::Access && chosen->view()->access_constant)
            recover([&]() { error(aOperand, "an access-to-constant value cannot be converted to an access-to-variable type (4.6(24.10/2))"); });
        // (remote access types: a remote access-to-subprogram value converted only to or from another (subtype conformant) one; a remote
        // access-to-class-wide value converted explicitly only to another (E.2.2(11/2, 15)))
        if (chosen != nullptr && iSpeculation == 0 && iInstanceBodies == 0 && chosen->is_access() && target->is_access())
        {
            auto* o = chosen->view()->base_type()->view();
            bool const remoteTarget = remote_access_type(aTarget);
            bool const remoteOperand = remote_access_type(chosen);
            if (target->kind == type_kind::AccessSubprogram && o->kind == type_kind::AccessSubprogram)
            {
                if (remoteTarget != remoteOperand)
                    error(aOperand, "a remote access-to-subprogram value can be converted only to or from another remote access-to-subprogram type (E.2.2(11/2))");
                if (remoteTarget && target->profile != nullptr && o->profile != nullptr && !subtype_conformant(*target->profile, *o->profile))
                    error(aOperand, "remote access-to-subprogram types must be subtype conformant to be converted (E.2.2(11/2))");
            }
            else if (remoteOperand && !remoteTarget && o->kind == type_kind::Access)
                error(aOperand, "a remote access-to-class-wide value can be converted only to another remote access-to-class-wide type (E.2.2(15))");
        }
        // (an access type conversion: the operand type not statically deeper than the target type (4.6(24.17/3)), also in an instance's
        // private part; an access discriminant of the current instance of a (record) type: assumed deeper)
        if (chosen != nullptr && iSpeculation == 0 && (iInstanceBodies == 0 || iInstanceBodies == iInstancePrivateParts) && chosen->is_access() &&
            target->is_access() && !target->anonymous_access && !within_generic_body(aRegion))
        {
            auto* o = chosen->view()->base_type()->view();
            std::optional<std::int64_t> level;
            if (!o->anonymous_access)
                level = type_level(chosen);
            else if (auto* operand = aOperand; operand->is(construct::Identifier) && operand->ent != nullptr && operand->ent->kind == entity_kind::Discriminant &&
                operand->ent->scope != nullptr && operand->ent->scope->owner != nullptr && operand->ent->scope->owner->kind == entity_kind::Type)
                level = std::numeric_limits<std::int64_t>::max();
            // (an access discriminant of an object (X.D): the object's level (3.10.2))
            else if (operand->is(construct::Selected) && operand->size() == 2u && operand->child(1)->ent != nullptr &&
                operand->child(1)->ent->kind == entity_kind::Discriminant)
                level = static_level(operand->child(0), aRegion);
            if (level && *level > type_level(aTarget))
                error_in_private_part_too(aOperand, "the operand's access type is statically deeper than the target type (4.6(24.17/3))");
        }
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

    bool program::static_subtype(ada_type* aType)
    {
        // (a static subtype (4.9(26-28)): a scalar subtype with static bounds, or a constrained array subtype with static index
        // subtypes (and component subtype); not a generic formal type)
        if (aType == nullptr)
            return false;
        auto* v = aType->view();
        if (v->is_formal || v->base_type()->view()->is_formal)
            return false;
        // (not of a descendant of a formal type)
        for (auto* x = v->base_type()->view(); x->parent != nullptr && x->parent->view()->base_type()->view() != x; )
        {
            x = x->parent->view()->base_type()->view();
            if (x->is_formal)
                return false;
        }
        if (v->is_scalar())
        {
            if (!v->low.known() || !v->high.known() || v->nonstatic || aType->nonstatic)
                return false;
            // (not a subtype with a dynamic predicate (3.2.4(15/5)))
            for (auto* p = aType->predicate != nullptr ? aType->predicate : v->predicate; p != nullptr; p = p->parent)
                if (!p->is_static)
                    return false;
            return true;
        }
        // (a static string subtype: unconstrained, or with a static constraint, of a string type whose index and component subtypes are
        // static)
        if (v->kind == type_kind::Array && v->indexes.size() == 1u && v->component != nullptr && v->component->view()->kind == type_kind::Enumeration)
            return static_subtype(v->indexes[0]) && static_subtype(v->component);
        return false;
    }

    std::optional<std::pair<bigint, bigint>> program::array_range_bounds(node* aAttribute)
    {
        // (A'Range (or A'Range (N)) of a statically constrained array subtype or object: its static bounds)
        if (aAttribute == nullptr || !aAttribute->is(construct::Attribute) || aAttribute->size() < 2u || name_text(aAttribute->child(1)->source) != "range")
            return std::nullopt;
        auto* prefix = aAttribute->child(0);
        ada_type* t = nullptr;
        if (meaning_of(prefix) == meaning::Type && prefix->type != nullptr)
            t = prefix->type;
        else if (prefix->ent != nullptr && (prefix->ent->kind == entity_kind::Constant || prefix->ent->kind == entity_kind::Variable) &&
            prefix->ent->renamed == nullptr && prefix->is(construct::Identifier))
            t = prefix->ent->type;
        if (t == nullptr)
            return std::nullopt;
        auto* v = t->view();
        // (a static string constant of an unconstrained subtype: its bounds from its index subtype's first, by its length)
        if (v->kind == type_kind::Array && !v->constrained && v->indexes.size() == 1u && prefix->ent != nullptr && prefix->ent->kind == entity_kind::Constant &&
            prefix->ent->value.is_string() && v->indexes[0]->view()->low.is_integer() && aAttribute->first(construct::Arguments) == nullptr)
        {
            auto const low = v->indexes[0]->view()->low.integer();
            return std::make_pair(low, low + bigint{ static_cast<std::int64_t>(prefix->ent->value.string().size()) } - bigint{ 1 });
        }
        if (v->kind != type_kind::Array || !v->constrained || v->indexes.empty())
            return std::nullopt;
        std::size_t dimension = 0u;
        if (auto* arguments = aAttribute->first(construct::Arguments); arguments != nullptr && arguments->size() == 1u)
        {
            auto const n = evaluate(arguments->child(0), nullptr);
            if (!n.is_integer() || n.integer() < bigint{ 1 } || n.integer() > bigint{ static_cast<std::int64_t>(v->indexes.size()) })
                return std::nullopt;
            dimension = static_cast<std::size_t>(n.integer().to_uint64()) - 1u;
        }
        auto* i = v->indexes[dimension]->view();
        if (!i->low.is_integer() || !i->high.is_integer())
            return std::nullopt;
        return std::make_pair(i->low.integer(), i->high.integer());
    }

    bool program::static_expression(node* aExpression)
    {
        // (a static expression (4.9(2-13)): its form, its names' and its subtypes' staticness; its value known (static_value))
        if (aExpression == nullptr)
            return false;
        auto const all_static = [&](std::size_t aFrom)
        {
            for (std::size_t i = aFrom; i < aExpression->size(); ++i)
                if (!aExpression->child(i)->is(construct::Flag) && !static_expression(aExpression->child(i)))
                    return false;
            return true;
        };
        switch (aExpression->kind)
        {
        case construct::Number:
        case construct::Character:
            return true;
        case construct::Operator:
            return meaning_of(aExpression) == meaning::StringLiteral || aExpression->ent == nullptr;
        case construct::Identifier:
        case construct::Selected:
            {
                auto* e = aExpression->ent;
                if (e == nullptr)
                    return false;
                if (e->kind == entity_kind::NamedNumber || e->kind == entity_kind::EnumerationLiteral)
                    return true;
                if (e->kind == entity_kind::Function && meaning_of(aExpression) == meaning::Call && e->sub != nullptr && e->sub->renames != nullptr &&
                    e->sub->renames->kind == entity_kind::EnumerationLiteral)
                    return true;
                // (a static constant (4.9(5)): a constant (or a renaming of one) of a static subtype whose value is a static
                // expression; a deferred constant where its full declaration is visible)
                for (int depth = 0; e != nullptr && depth < 16; ++depth)
                {
                    if (e->kind == entity_kind::Constant && e->value.known() && static_subtype(e->type) &&
                        (e->initial == nullptr || static_expression(e->initial)))
                        return true;
                    if (e->kind == entity_kind::Constant && e->completion != nullptr && e->completion != e)
                        e = e->completion;
                    else if ((e->kind == entity_kind::Constant || e->kind == entity_kind::Variable) && e->renamed != nullptr)
                        e = e->renamed;
                    else
                        break;
                }
                return false;
            }
        case construct::Parenthesized:
            return static_expression(aExpression->child(0));
        case construct::Qualified:
            return static_subtype(aExpression->child(0)->type != nullptr ? aExpression->child(0)->type : aExpression->type) &&
                static_expression(aExpression->child(1));
        case construct::Apply:
            if (meaning_of(aExpression) == meaning::Conversion)
                return aExpression->type != nullptr && aExpression->type->view()->is_scalar() && static_subtype(aExpression->type) &&
                    static_expression(aExpression->child(1));
            if (meaning_of(aExpression) == meaning::Call)
            {
                // (a call of a static function (a predefined operator, or a function attribute of a static subtype) with static actuals)
                if (aExpression->child(0)->is(construct::Attribute))
                    return static_expression(aExpression->child(0)) && [&]()
                    {
                        auto const* call = call_of(aExpression);
                        if (call != nullptr)
                            for (auto* a : call->actuals)
                                if (!static_expression(a))
                                    return false;
                        return true;
                    }();
                auto const* call = call_of(aExpression);
                if (call == nullptr || call->callee == nullptr || call->callee->sub == nullptr)
                    return false;
                auto const op = call->callee->sub->op;
                if (op == intrinsic::None || op == intrinsic::Import || op == intrinsic::Attribute)
                    return call->callee->sub->renames != nullptr && call->callee->sub->renames->kind == entity_kind::EnumerationLiteral;
                for (auto* a : call->actuals)
                    if (!static_expression(a))
                        return false;
                return true;
            }
            return false;
        case construct::Attribute:
            {
                // (4.9(7-9): an attribute of a static scalar subtype, or First, Last or Length of a statically constrained array
                // object or subtype; its arguments static)
                auto* prefix = aExpression->child(0);
                auto const name = name_text(aExpression->child(1)->source);
                if (auto* arguments = aExpression->first(construct::Arguments))
                    for (auto* a : arguments->children)
                        if (!static_expression(a))
                            return false;
                if (name == "storage_unit" || name == "word_size" || name == "address_size" || name == "max_integer_size" ||
                    name == "maximum_alignment" || name == "system_allocator_alignment")
                    return true;
                bool const typePrefix = meaning_of(prefix) == meaning::Type ||
                    (prefix->is(construct::Attribute) && name_text(prefix->child(1)->source) == "base");
                auto* t = prefix->type;
                if (t == nullptr)
                    return false;
                if (prefix->is(construct::Attribute) && name_text(prefix->child(1)->source) == "base")
                    t = t->base_type();
                auto* v = t->view();
                if (v->kind == type_kind::Array)
                {
                    if (name != "first" && name != "last" && name != "length")
                        return false;
                    // (a statically constrained array subtype: constrained, its index subtypes static)
                    auto const statically_constrained = [&](ada_type* aArray)
                    {
                        auto* a = aArray->view();
                        if (!a->constrained)
                            return false;
                        for (auto* i : a->indexes)
                            if (!static_subtype(i))
                                return false;
                        return true;
                    };
                    if (typePrefix)
                        return statically_constrained(t);
                    // (a statically constrained object (4.9(32.4/3)): its nominal subtype; a static string constant)
                    return (prefix->ent != nullptr && (prefix->ent->kind == entity_kind::Constant || prefix->ent->kind == entity_kind::Variable) &&
                        prefix->ent->type != nullptr && statically_constrained(prefix->ent->type)) || (prefix->ent != nullptr && static_expression(prefix));
                }
                if (!typePrefix || !v->is_scalar())
                    return false;
                if (!static_subtype(t))
                    return false;
                return evaluate_attribute(aExpression, nullptr).known() || name == "size" || name == "pos" || name == "val" ||
                    name == "succ" || name == "pred" || name == "min" || name == "max";
            }
        case construct::AndThen:
        case construct::OrElse:
            return all_static(0u);
        case construct::In:
            {
                if (!static_expression(aExpression->child(0)))
                    return false;
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                {
                    auto* c = aExpression->child(i);
                    if (c->is(construct::Flag))
                        continue;
                    if (c->is(construct::Range))
                    {
                        if (!static_expression(c->child(0)) || !static_expression(c->child(1)))
                            return false;
                    }
                    else if (c->type != nullptr && (c->is(construct::SubtypeIndication) || meaning_of(c) == meaning::Type))
                    {
                        if (!static_subtype(c->type))
                            return false;
                    }
                    // (A'Range of a statically constrained array subtype or object (4.9(14)))
                    else if (array_range_bounds(c))
                        continue;
                    else if (!static_expression(c))
                        return false;
                }
                return true;
            }
        case construct::CaseExpression:
            {
                if (!static_expression(aExpression->child(0)))
                    return false;
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                    if (auto* alternative = aExpression->child(i); alternative->is(construct::Alternative))
                    {
                        for (auto* choice : alternative->child(0)->children)
                            if (!choice->is(construct::Others) && !(choice->is(construct::Range) ? static_expression(choice->child(0)) &&
                                static_expression(choice->child(1)) : choice->type != nullptr && meaning_of(choice) == meaning::Type ?
                                static_subtype(choice->type) : static_expression(choice)))
                                return false;
                        if (!static_expression(alternative->children.back()))
                            return false;
                    }
                return true;
            }
        case construct::IfExpression:
            {
                if (!static_expression(aExpression->child(0)) || !static_expression(aExpression->child(1)))
                    return false;
                for (std::size_t i = 2u; i < aExpression->size(); ++i)
                {
                    auto* part = aExpression->child(i);
                    for (auto* c : part->children)
                        if (!static_expression(c))
                            return false;
                }
                return true;
            }
        default:
            break;
        }
        if (is_operator(aExpression->kind))
        {
            auto const* call = call_of(aExpression);
            if (call == nullptr || call->callee == nullptr || call->callee->sub == nullptr)
                return false;
            auto const op = call->callee->sub->op;
            if (op == intrinsic::None || op == intrinsic::Import)
                return false;
            return all_static(0u);
        }
        return false;
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
            {
                // (a value that fails the subtype's constraint: Constraint_Error when evaluated, so not static)
                auto v = evaluate(aExpression->child(1), aRegion);
                auto* q = aExpression->child(0)->type != nullptr ? aExpression->child(0)->type->view() : nullptr;
                if (v.known() && q != nullptr)
                {
                    if (q->is_discrete() && v.is_integer() && q->low.is_integer() && q->high.is_integer() &&
                        (v.integer() < q->low.integer() || v.integer() > q->high.integer()))
                        return {};
                    if (q->kind == type_kind::Array && q->constrained && v.is_string() && q->indexes.size() == 1u)
                        if (auto* i = q->indexes[0]->view(); i->low.is_integer() && i->high.is_integer())
                        {
                            auto const length = i->high.integer() - i->low.integer() + bigint{ 1 };
                            if (bigint{ static_cast<std::int64_t>(v.string().size()) } != (length.negative() ? bigint{ 0 } : length))
                                return {};
                        }
                }
                return v;
            }
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
                // (the right operand statically unevaluated (4.9(32.2/3)))
                if (a.is_integer() && (aExpression->is(construct::AndThen) ? a.integer().is_zero() : !a.integer().is_zero()))
                    return a;
                auto b = evaluate(aExpression->child(1), aRegion);
                return fold(aExpression->is(construct::AndThen) ? intrinsic::AndThen : intrinsic::OrElse, iBoolean, { a, b });
            }
        case construct::In:
            {
                auto v = evaluate(aExpression->child(0), aRegion);
                if (!v.known())
                    return {};
                bool result = false;
                for (std::size_t i = 1u; i < aExpression->size() && !result; ++i)
                {
                    // (the choices after one whose test yields True: statically unevaluated (4.9(32.6/3)))
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
                    else if (c->type != nullptr && (c->is(construct::SubtypeIndication) || meaning_of(c) == meaning::Type) && v.is_string() &&
                        c->type->view()->kind == type_kind::Array)
                    {
                        // (a string in a constrained string subtype: its length (bounds from 1) the subtype's)
                        auto* t = c->type->view();
                        if (!t->constrained || t->indexes.size() != 1u)
                            return {};
                        auto* i = t->indexes[0]->view();
                        if (!i->low.is_integer() || !i->high.is_integer())
                            return {};
                        auto const length = i->high.integer() - i->low.integer() + bigint{ 1 };
                        result = result || (i->low.integer() == bigint{ 1 } && bigint{ static_cast<std::int64_t>(v.string().size()) } == length);
                    }
                    else if (auto const bounds = array_range_bounds(c))
                    {
                        if (!v.is_integer())
                            return {};
                        result = result || (v.integer() >= bounds->first && v.integer() <= bounds->second);
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
        case construct::CaseExpression:
            {
                // (the dependent expression of the alternative whose choices cover the selector's value; the others statically
                // unevaluated (4.9(32.4/3)))
                auto v = evaluate(aExpression->child(0), aRegion);
                if (!v.is_integer())
                    return {};
                for (std::size_t i = 1u; i < aExpression->size(); ++i)
                {
                    auto* alternative = aExpression->child(i);
                    if (!alternative->is(construct::Alternative))
                        continue;
                    for (auto* choice : alternative->child(0)->children)
                    {
                        bool covers = choice->is(construct::Others);
                        if (choice->is(construct::Range))
                        {
                            auto l = evaluate(choice->child(0), aRegion);
                            auto h = evaluate(choice->child(1), aRegion);
                            if (!l.is_integer() || !h.is_integer())
                                return {};
                            covers = v.integer() >= l.integer() && v.integer() <= h.integer();
                        }
                        else if (!covers && choice->type != nullptr && (choice->is(construct::SubtypeIndication) || meaning_of(choice) == meaning::Type))
                        {
                            auto* t = choice->type->view();
                            if (!t->low.is_integer() || !t->high.is_integer())
                                return {};
                            covers = v.integer() >= t->low.integer() && v.integer() <= t->high.integer();
                        }
                        else if (!covers)
                        {
                            auto c = evaluate(choice, aRegion);
                            if (!c.is_integer())
                                return {};
                            covers = c.integer() == v.integer();
                        }
                        if (covers)
                            return evaluate(alternative->children.back(), aRegion);
                    }
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
