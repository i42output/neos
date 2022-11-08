/*
  compiler.cpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <neolib/neolib.hpp>
#include <iostream>
#include <neolib/core/scoped.hpp>
#include <neolib/core/recursion.hpp>
#include <neolib/core/string_utf.hpp>
#include <neos/bytecode/opcodes.hpp>
#include <neos/bytecode/text.hpp>
#include <neos/i_context.hpp>
#include <neos/language/compiler.hpp>

namespace neos::language
{
    compiler::scoped_concept_folder::scoped_concept_folder(compiler& aCompiler, compiler_pass aPass) :
        scoped_concept_folder{ aCompiler, aPass, aCompiler.parse_stack() }
    {
    }

    compiler::scoped_concept_folder::scoped_concept_folder(compiler& aCompiler, compiler_pass aPass, concept_stack_t& aStack) :
        iCompiler{ aCompiler }, iPass{ aPass }, iStack{ aStack }, iScopeStart{ aStack.size() }
    {
    }

    compiler::scoped_concept_folder::~scoped_concept_folder()
    {
        if (std::uncaught_exceptions() == 0)
            fold();
    }

    void compiler::scoped_concept_folder::fold()
    {
        if (iPass == compiler_pass::Emit)
        {
            std::for_each(stack().begin() + iScopeStart, stack().end(), 
                [this](const concept_stack_entry& aEntry)
                {
                    if (aEntry.concept_ != nullptr)
                    {
                        iCompiler.fold_stack().push_back(aEntry);
                        if (iCompiler.trace() >= 2)
                            iCompiler.iContext.cout() << "prefold: " << "<" << aEntry.level << ": " << location(*aEntry.unit, *aEntry.fragment, aEntry.sourceStart, false) << "> "
                                << aEntry.concept_->name() << " (" << std::string(aEntry.sourceStart, aEntry.sourceEnd) << ")" << std::endl;
                    }
                });
            iCompiler.fold();
        }
        stack().erase(stack().begin() + iScopeStart, stack().end());
    }

    void compiler::scoped_concept_folder::move_to(concept_stack_t& aOtherStack)
    {
        if (iPass == compiler_pass::Emit)
            aOtherStack.insert(aOtherStack.end(), stack().begin() + iScopeStart, stack().end());
        stack().erase(stack().begin() + iScopeStart, stack().end());
    }

    compiler::concept_stack_t& compiler::scoped_concept_folder::stack()
    {
        return iStack;
    }

    compiler::compiler(i_context& aContext) :
        iContext{ aContext }, iTrace { 0u }, iStartTime{ std::chrono::steady_clock::now() }, iEndTime{ std::chrono::steady_clock::now() }
    {
    }

    uint32_t compiler::trace() const
    {
        return iTrace;
    }

    const std::optional<std::string>& compiler::trace_filter() const
    {
        return iTraceFilter;
    }

    void compiler::set_trace(uint32_t aTrace, const std::optional<std::string>& aFilter)
    {
        iTrace = aTrace;
        iTraceFilter = aFilter;
    }

    const std::chrono::steady_clock::time_point& compiler::start_time() const
    {
        return iStartTime;
    }

    const std::chrono::steady_clock::time_point& compiler::end_time() const
    {
        return iEndTime;
    }

    void compiler::compile(program& aProgram)
    {
        for (auto& unit : aProgram.translationUnits)
            for (auto fragment = unit.fragments.begin(); fragment != unit.fragments.end();)
                if (fragment->imported())
                    fragment = unit.fragments.erase(fragment);
                else
                    (fragment++)->set_status(compilation_status::Pending);

        iStartTime = std::chrono::steady_clock::now();

        try
        {
            for (auto& unit : aProgram.translationUnits)
                compile(aProgram, unit);
        }
        catch(...)
        {
            iEndTime = std::chrono::steady_clock::now();
            throw;
        }

        iEndTime = std::chrono::steady_clock::now();
    }

    void compiler::compile(program& aProgram, translation_unit& aUnit)
    {
        for (auto& fragment : aUnit.fragments)
            compile(aProgram, aUnit, fragment);
    }

    void compiler::compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment)
    {
        if (aFragment.status() != compilation_status::Pending)
            return;

        aFragment.set_status(compilation_status::Compiling);
            
        iCompilationStateStack.push_back(std::unique_ptr<compilation_state>(new compilation_state{ &aProgram, &aUnit }));

        auto source = aFragment.cbegin();
        while (source != aFragment.cend())
        {
            state().iDeepestProbe = std::nullopt;
            auto result = parse(compiler_pass::Emit, aProgram, aUnit, aFragment, aUnit.schema->root(), source, source);
            if (result.action == parse_result::NoMatch)
            {
                if (trace() >= 3 && state().iDeepestProbe)
                    display_probe_trace(aUnit, aFragment);
                throw_error(aUnit, aFragment, state().iDeepestProbe ? state().iDeepestProbe->source : source, "syntax error");
            }
            if (source == result.sourceParsed)
                throw_error(aUnit, aFragment, state().iDeepestProbe ? state().iDeepestProbe->source : source, "parse halted, bad schema?");
            source = result.sourceParsed;
        }

        aFragment.set_status(compilation_status::Compiled);

        iCompilationStateStack.pop_back();
    }

    void compiler::compile(const i_source_fragment& aFragment)
    {
        auto& program = *state().program;
        auto& unit = *state().unit;
        auto& fragment = *unit.fragments.emplace(unit.fragments.end(), aFragment);
        compile(program, unit, fragment);
    }

    const compiler::compilation_state& compiler::state() const
    {
        return *iCompilationStateStack.back();
    }

    compiler::compilation_state& compiler::state()
    {
        return *iCompilationStateStack.back();
    }

    compiler::parse_result compiler::parse(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, source_iterator aSourceStart, source_iterator aSourceParsed)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        neolib::scoped_counter sc{ state().iLevel };
        scoped_stack_trace sst{ *this, aAtom, aSourceParsed, "parse" };
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse(compiler_pass::Probe, aProgram, aUnit, aFragment, aAtom, aSourceStart, aSourceParsed);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace() >= 4)
            iContext.cout() << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse(" << aAtom.symbol() << ")" << std::endl;
        if (aSourceParsed == aFragment.cend())
            return parse_result{ aSourceStart, aSourceParsed, parse_result::NoMatch, aUnit.schema->eof() };
        scoped_concept_folder scs{ *this, aPass };
        bool const expectingToken = !aAtom.expects().empty();
        if (aSourceParsed != aFragment.cend())
        {
            if (expectingToken)
            {
                for (auto const& expect : aAtom.expects())
                {
                    auto recursiveLevel = aAtom.recursive_token(*expect);
                    if (aAtom.expects().size() == 1 && recursiveLevel != 0u)
                    {
                        if (aPass == compiler_pass::Emit && postfix_operation_stack().size() >= 2)
                        {
                            auto const& current = *std::prev(postfix_operation_stack().end());
                            auto& previous = *std::prev(std::prev(postfix_operation_stack().end()));
                            if (current.level == previous.level + recursiveLevel && previous.concept_ != nullptr &&
                                aAtom.token().is_conceptually_related_to(*current.concept_) &&
                                aAtom.token().is_conceptually_related_to(*previous.concept_))
                            {
                                parse_stack().push_back(previous);
                                scs.fold();
                                previous.concept_ = nullptr;
                            }
                        }
                    }
                    auto result = parse_tokens(aPass, aProgram, aUnit, aFragment, aAtom, expected{ &*expect, &aAtom }, aSourceStart, aSourceParsed);
                    if (is_finished(result) || result.action == parse_result::Consumed)
                        return result;
                    if (state().iDeepestProbe == std::nullopt || state().iDeepestProbe->source < result.sourceParsed)
                        state().iDeepestProbe = deepest_probe{ result.sourceParsed, { state().iStackTrace } };
                    else if (state().iDeepestProbe->source == result.sourceParsed)
                        state().iDeepestProbe->stacks.push_back(state().iStackTrace);
                }
                return parse_result{ aSourceStart, aSourceParsed, parse_result::NoMatch };
            }
            else
                return parse_tokens(aPass, aProgram, aUnit, aFragment, aAtom, expected{}, aSourceStart, aSourceParsed);
        }
        else
            return expectingToken ? parse_result{ aSourceStart, aSourceParsed, parse_result::NoMatch } : parse_result{ aSourceStart, aSourceParsed };
        //auto loop = emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::u64{ 10 });
        //emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::i8{ -1 });
        //emit(aProgram.text, bytecode::opcode::B, loop);
    }

    compiler::parse_result compiler::parse_tokens(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, const expected& aExpected, source_iterator aSourceStart, source_iterator aSourceParsed)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse_tokens(compiler_pass::Probe, aProgram, aUnit, aFragment, aAtom, aExpected, aSourceStart, aSourceParsed);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace() >= 4)
            iContext.cout() << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_tokens(" << aAtom.symbol() << ")" << std::endl;
        scoped_concept_folder scs{ *this, aPass };
        auto sourceStart = aSourceStart;
        auto currentSource = aSourceParsed;
        bool defaultOk = aAtom.expect_none();
        auto expectedAtom = aExpected.what;
        auto expectedConsumed = aExpected.consumed;
        bool const consumeSelf = (expectedAtom == nullptr || *aExpected.context == aAtom);
        bool handledExpect = false;
        if (expectedAtom && expectedAtom->is_schema_atom() && expectedAtom->as_schema_atom().is_schema_node_atom() &&
            expectedAtom->as_schema_atom().as_schema_node_atom().as())
        {
            auto result = parse(aPass, aProgram, aUnit, aFragment, expectedAtom->as_schema_atom().as_schema_node_atom(), sourceStart, currentSource);
            if (is_finished(result))
            {
                currentSource = result.sourceParsed;
                expectedAtom = result.atom;
                expectedConsumed = true;
            }
        }
        auto iterToken = expectedAtom ?
            std::find_if(aAtom.tokens().begin(), aAtom.tokens().end(), [expectedAtom](auto&& tokensEntry)
            { 
                auto const& token = *tokensEntry.first();
                return *expectedAtom == token;
            }) : 
            aAtom.tokens().begin();
        if (expectedAtom && iterToken == aAtom.tokens().end())
        {
            iterToken = expectedAtom ?
                std::find_if(aAtom.tokens().begin(), aAtom.tokens().end(), [expectedAtom](auto&& tokensEntry)
                { 
                    auto const& token = *tokensEntry.first();
                    return expectedAtom->is_conceptually_related_to(token);
                }) : 
                aAtom.tokens().begin();
        }
        if (expectedAtom && iterToken == aAtom.tokens().end())
        {
            if (aAtom.has_parent())
                return parse_tokens(aPass, aProgram, aUnit, aFragment, aAtom.parent().as_schema_atom().as_schema_node_atom(), aExpected, aSourceStart, aSourceParsed);
            return parse_result{ aSourceStart, aSourceParsed, parse_result::NoMatch };
        }
        bool skipConsume = (expectedAtom && expectedConsumed);
        for (; currentSource != aFragment.cend() && iterToken != aAtom.tokens().end();)
        {
            auto const& token = *iterToken->first();
            auto const& tokenValue = *iterToken->second();
            if (aAtom.is_token_node() && aAtom.token() == token)
                defaultOk = true;
            parse_result result{ sourceStart, currentSource };
            if (!skipConsume)
                result = parse_token(aPass, aProgram, aUnit, aFragment, aAtom, token, result);
            skipConsume = false;
            if (finished(result))
            {
                if (aAtom.as() && aAtom.is_conceptually_related_to(tokenValue))
                    result.atom = &token;
                if (token.is_schema_atom() && token.as_schema_atom().is_schema_node_atom() && token.as_schema_atom().as_schema_node_atom().is_token_node())
                    return consume_token(aPass, aProgram, aUnit, aFragment, aAtom, result);
            }
            bool const ateSome = (result.action == parse_result::Consumed);
            if (ateSome)
            {
                defaultOk = true;
                auto const& matchedTokenValue = tokenValue;
                if (!matchedTokenValue.is_schema_atom() || matchedTokenValue.as_schema_atom().is_schema_node_atom())
                {
                    if (aAtom.is_parent_of(matchedTokenValue) || aAtom.is_sibling_of(matchedTokenValue))
                        result = parse_token(aPass, aProgram, aUnit, aFragment, aAtom, matchedTokenValue, result);
                    if (aAtom.as() && aAtom.is_conceptually_related_to(matchedTokenValue))
                        result.atom = &matchedTokenValue;
                    if (is_finished(result))
                    {
                        if (consumeSelf)
                            result = consume_token(aPass, aProgram, aUnit, aFragment, aAtom, result);
                        if (aAtom.is_parent_of(matchedTokenValue))
                            return result;
                        if (consumeSelf && result.action == parse_result::Done)
                            finished(result);
                    }
                    if (result.action == parse_result::Consumed)
                    {
                        if (matchedTokenValue.is_concept_atom())
                        {
                            result = parse_token_match(aPass, aProgram, aUnit, aFragment, aAtom, matchedTokenValue, result);
                            if (is_finished(result))
                                return consumeSelf ? consume_token(aPass, aProgram, aUnit, aFragment, aAtom, result) : result;
                        }
                        else
                        {
                            result = parse_token_match(aPass, aProgram, aUnit, aFragment, matchedTokenValue.as_schema_atom().as_schema_node_atom(), token, result, false);
                            if (is_finished(result))
                            {
                                result = consume_token(aPass, aProgram, aUnit, aFragment, matchedTokenValue, result);
                                return consumeSelf ? consume_token(aPass, aProgram, aUnit, aFragment, aAtom, result) : result;
                            }
                            if (result.action == parse_result::Consumed)
                            {
                                result = parse_token_match(aPass, aProgram, aUnit, aFragment, aAtom, matchedTokenValue, result, aAtom.is_parent_of(matchedTokenValue));
                                if (is_finished(result))
                                    return consumeSelf ? consume_token(aPass, aProgram, aUnit, aFragment, aAtom, result) : result;
                            }
                        }
                        if (result.action == parse_result::Consumed)
                        {
                            currentSource = result.sourceParsed;
                            iterToken = aAtom.tokens().begin();
                        }
                        else if (is_finished(result))
                            return result;
                        else
                            ++iterToken;
                    }
                    else if (result.action == parse_result::Consumed)
                        ++iterToken;
                    else
                        return result;
                }
                else if (matchedTokenValue.is_schema_atom() && matchedTokenValue.as_schema_atom().is_schema_terminal_atom())
                {
                    result = parse_token(aPass, aProgram, aUnit, aFragment, aAtom, matchedTokenValue, result);
                    switch(result.action)
                    {
                    case parse_result::Drain:
                    case parse_result::Done:
                    case parse_result::ForNext:
                        return result;
                    case parse_result::Ignored:
                    case parse_result::Continue:
                        currentSource = result.sourceParsed;
                        sourceStart = currentSource;
                        iterToken = aAtom.tokens().begin();
                        break;
                    case parse_result::Recurse:
                        if (aAtom.has_parent())
                            return parse_tokens(aPass, aProgram, aUnit, aFragment, aAtom.parent().as_schema_atom().as_schema_node_atom(), aExpected, aSourceStart, aSourceParsed);
                        return parse_result{ aSourceStart, aSourceParsed, parse_result::NoMatch };
                    case parse_result::NoMatch:
                        if (token.is_schema_atom() && token.as_schema_atom().is_schema_terminal_atom() && token.as_schema_atom().as_schema_terminal_atom().type() == schema_terminal::String)
                            result.sourceParsed = std::prev(result.sourceParsed, token.symbol().size());
                        return result;
                    case parse_result::Error:
                        return parse_result{ aSourceStart, result.sourceParsed, parse_result::NoMatch };
                    default:
                        /* do nothing */
                        break;
                    }
                }
                else
                    return result;
            }
            else
            {
                if (!handledExpect && aExpected.what)
                    return parse_result{ aSourceStart, aSourceParsed, parse_result::NoMatch };
                ++iterToken;
            }
            handledExpect = true;
            if (iterToken == aAtom.tokens().end())
            {
                if (token.is_schema_atom() && token.as_schema_atom().is_schema_terminal_atom() &&
                    token.as_schema_atom().as_schema_terminal_atom().type() == schema_terminal::Default)
                {
                    auto result = parse_token(aPass, aProgram, aUnit, aFragment, aAtom, tokenValue, parse_result{ sourceStart, currentSource });
                    switch (result.action)
                    {
                    case parse_result::Drain:
                    case parse_result::Done:
                    case parse_result::ForNext:
                        return defaultOk ? result : result.with(parse_result::NoMatch);
                    case parse_result::Consumed:
                        currentSource = result.sourceParsed;
                        break;
                    case parse_result::Ignored:
                        if (!ateSome && currentSource == result.sourceParsed)
                            ++currentSource;
                        iterToken = aAtom.tokens().begin();
                        break;
                    case parse_result::NoMatch:
                        return result;
                    case parse_result::Error:
                        return result.with(parse_result::NoMatch);
                    }
                }
            }
        }
        return parse_result{ aSourceStart, currentSource, currentSource == aFragment.cend() || aAtom.tokens().empty() ? parse_result::Consumed : parse_result::NoMatch };
    }

    compiler::parse_result compiler::parse_token_match(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, const i_atom& aMatchResult, const parse_result& aResult, bool aConsumeMatchResult, bool aSelf)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse_token_match(compiler_pass::Probe, aProgram, aUnit, aFragment, aAtom, aMatchResult, aResult, aConsumeMatchResult, aSelf);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace() >= 4)
            iContext.cout() << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_token_match(" << aAtom.symbol() << ":" << aMatchResult.symbol() << ")" << std::endl;
        scoped_concept_folder poe{ *this, aPass, postfix_operation_stack() };
        std::optional<scoped_concept_folder> scs;
        if (!aSelf)
            scs.emplace(*this, aPass);
        parse_result result{ !aAtom.as() || !aAtom.is_conceptually_related_to(aMatchResult) ? aResult : aResult.with_if(aMatchResult)};
        if (aConsumeMatchResult)
        {
            if (aMatchResult.is_concept_atom())
            {
                if (aMatchResult.as_concept_atom().get_concept().emit_as() == emit_type::Infix)
                    result = consume_concept_atom(aPass, aProgram, aUnit, aFragment, aMatchResult, aMatchResult.as_concept_atom().get_concept(), result);
                else if (aMatchResult.as_concept_atom().get_concept().emit_as() == emit_type::Postfix)
                    result = consume_concept_atom(aPass, aProgram, aUnit, aFragment, aMatchResult, aMatchResult.as_concept_atom().get_concept(), result, postfix_operation_stack());
            }
            else
                result = consume_token(aPass, aProgram, aUnit, aFragment, aMatchResult, result);
            if (scs)
                scs->fold();
        }
        if (result.action == parse_result::Consumed)
        {
            auto nextMatch = aAtom.find_token(aMatchResult);
            if (nextMatch != nullptr)
            {
                if (*nextMatch != aAtom)
                {
                    if (!nextMatch->is_concept_atom())
                        result = parse_token(aPass, aProgram, aUnit, aFragment, aAtom, *nextMatch, result);
                    if (result.action == parse_result::Consumed)
                        result = parse_token_match(aPass, aProgram, aUnit, aFragment, aAtom, *nextMatch, result, true, true);
                }
            }
        }
        poe.move_to(parse_stack());
        return result;
    }

    compiler::parse_result compiler::parse_token(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, const i_atom& aToken, const parse_result& aResult)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse_token(compiler_pass::Probe, aProgram, aUnit, aFragment, aAtom, aToken, aResult);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace() >= 4)
            iContext.cout() << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_token(" << aAtom.symbol() << ":" << aToken.symbol() << ")" << std::endl;
        scoped_concept_folder scs{ *this, aPass };
        if (aToken.is_schema_atom())
        {
            if (aToken.as_schema_atom().is_schema_node_atom())
                return parse(aPass, aProgram, aUnit, aFragment, aToken.as_schema_atom().as_schema_node_atom(), 
                    std::next(aResult.sourceStart, aAtom.is_string_token_node() ? aAtom.token().symbol().size() : 0), 
                    aResult.sourceParsed).with_if(aResult.atom);
            else if (aToken.as_schema_atom().is_schema_terminal_atom())
            {
                auto const& terminal = aToken.as_schema_atom().as_schema_terminal_atom();
                switch (terminal.type())
                {
                case schema_terminal::Default:
                    break;
                case schema_terminal::Error:
                    return aResult.with(parse_result::Error);
                case schema_terminal::Ignore:
                    return aResult.with(parse_result::Ignored);
                case schema_terminal::Continue:
                    {
                        auto result = consume_token(aPass, aProgram, aUnit, aFragment, aAtom, aResult);
                        if (result.action == parse_result::Consumed)
                            result.action = parse_result::Continue;
                        return result;
                    }
                case schema_terminal::Recurse:
                    {
                        auto result = consume_token(aPass, aProgram, aUnit, aFragment, aAtom, aResult);
                        if (result.action == parse_result::Consumed)
                            result.action = parse_result::Recurse;
                        return result;
                    }
                case schema_terminal::Done:
                    {
                        auto result = consume_token(aPass, aProgram, aUnit, aFragment, aAtom, aResult);
                        if (result.action == parse_result::Consumed)
                            result.action = parse_result::Done;
                        return result;
                    }
                case schema_terminal::Drain:
                    return aResult.with(parse_result::Drain);
                case schema_terminal::Next:
                    return aResult.with(parse_result::ForNext);
                case schema_terminal::String:
                    {
                        auto const terminalSymbol = terminal.symbol().to_std_string_view();
                        if (static_cast<std::size_t>(std::distance(aResult.sourceParsed, aFragment.cend())) >= terminalSymbol.size() && std::equal(terminalSymbol.begin(), terminalSymbol.end(), aResult.sourceParsed))
                            return aResult.with(aResult.sourceParsed + terminalSymbol.size());
                    }
                default:
                    // do nothing
                    break;
                }
            }
        }
        else if (aToken.is_concept_atom())
            return consume_token(aPass, aProgram, aUnit, aFragment, aToken, aResult.with(parse_result::Consumed));
        return aResult.with(parse_result::NoMatch);
    }

    compiler::parse_result compiler::consume_token(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_atom& aToken, const parse_result& aResult)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        parse_result result = aResult;
        if (aToken.is_concept_atom())
            result = consume_concept_token(aPass, aProgram, aUnit, aFragment, aToken.as_concept_atom().get_concept(), result);
        else if (aToken.is_schema_atom() && aToken.as_schema_atom().is_schema_node_atom())
            for (auto& concept_ : aToken.as_schema_atom().as_schema_node_atom().is())
                if (result.action != parse_result::NoMatch && result.action != parse_result::Ignored && result.action != parse_result::Drain)
                    result = consume_concept_atom(aPass, aProgram, aUnit, aFragment, aToken, *concept_, result);
        return result;
    }

    compiler::parse_result compiler::consume_concept_token(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_concept& aConcept, const parse_result& aResult)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        auto consumeResult = aConcept.consume_token(aPass, aResult.sourceParsed, aFragment.cend());
        if (consumeResult.consumed && aPass == compiler_pass::Emit)
        {
            if (trace() >= 5)
                iContext.cout() << std::string(_compiler_recursion_limiter_.depth(), ' ') << "push(token): " << aConcept.name().to_std_string() << " (" << std::string(aResult.sourceParsed, consumeResult.sourceParsed) << ")" << std::endl;
            parse_stack().push_back(concept_stack_entry{ &aUnit, &aFragment, state().iLevel, &aConcept, aResult.sourceParsed, consumeResult.sourceParsed });
        }
        return aResult.with(consumeResult.sourceParsed, consumeResult.consumed ? aResult.action : parse_result::NoMatch);
    }

    compiler::parse_result compiler::consume_concept_atom(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult)
    {
        return consume_concept_atom(aPass, aProgram, aUnit, aFragment, aAtom, aConcept, aResult, parse_stack());
    }
        
    compiler::parse_result compiler::consume_concept_atom(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult, concept_stack_t& aConceptStack)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        auto consumeResult = aConcept.consume_atom(aPass, aAtom, aResult.sourceParsed, aFragment.cend());
        if (consumeResult.consumed && aPass == compiler_pass::Emit)
        {
            if (trace() >= 5)
                iContext.cout() << std::string(_compiler_recursion_limiter_.depth(), ' ') << "push(atom): " << aConcept.name().to_std_string() << " (" << std::string(aResult.sourceParsed, consumeResult.sourceParsed) << ")" << std::endl;
            aConceptStack.push_back(concept_stack_entry{ &aUnit, &aFragment, state().iLevel, &aConcept, aResult.sourceStart, consumeResult.sourceParsed });
        }
        return aResult.with(consumeResult.sourceParsed, consumeResult.consumed ? aResult.action : parse_result::NoMatch);
    }

    bool compiler::fold()
    {
        bool didSome = false;
        bool finished = false;
        while (!finished)
        {
            finished = true;
            while (fold2())
            {
                finished = false;
                didSome = true;
            }
            while (fold1())
            {
                finished = false;
                didSome = true;
            }
        }
        return didSome;
    }

    bool compiler::fold1()
    {
        if (fold_stack().size() < 1)
            return false;
        bool didSome = false;
        std::ostringstream traceOutput;
        for (auto isingle = fold_stack().begin(); isingle != fold_stack().end();)
        {
            auto& single = *isingle;
            if (single.can_fold())
            {
                std::string traceBefore = single.trace();
                if (trace() >= 1)
                    traceOutput << "folding: " << traceBefore << " <- " << traceBefore << std::endl;
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str();
                traceOutput.str({});
                single.fold(iContext);
                if (single.foldedConcept != nullptr)
                {
                    if (trace() >= 1)
                        traceOutput << "folded: " << traceBefore << " <- " << traceBefore << " = " << single.trace() << std::endl;
                }
                else
                {
                    if (trace() >= 1)
                        traceOutput << "folded: " << traceBefore << " <- " << traceBefore << " = ()" << std::endl;
                    isingle = fold_stack().erase(isingle);
                }
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str();
                traceOutput.str({});
                didSome = true;
            }
            else
                ++isingle;
        }
        return didSome;
    }

    bool compiler::fold2()
    {
        if (fold_stack().size() < 2)
            return false;
        bool didSome = false;
        std::ostringstream traceOutput;
        for (auto irhs = fold_stack().begin(), ilhs = std::next(irhs); ilhs != fold_stack().end();)
        {
            auto& rhs = *irhs;
            auto& lhs = *ilhs;
            if (lhs.can_fold(rhs))
            {
                std::string lhsTraceBefore = lhs.trace();
                std::string rhsTraceBefore = rhs.trace();
                if (trace() >= 1)
                    traceOutput << "folding: " << lhsTraceBefore << " <- " << rhsTraceBefore << std::endl;
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str();
                traceOutput.str({});
                lhs.fold(iContext, rhs);
                if (trace() >= 1)
                    traceOutput << "folded: " << lhsTraceBefore << " <- " << rhsTraceBefore << " = " << lhs.trace() << std::endl;
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str();
                traceOutput.str({});
                irhs = fold_stack().erase(irhs);
                if (irhs != fold_stack().begin())
                    --irhs;
                ilhs = std::next(irhs);
                didSome = true;
            }
            else
                ++irhs, ++ilhs;
        }
        return didSome;
    }

    compiler::concept_stack_t& compiler::parse_stack()
    {
        return state().iParseStack;
    }

    compiler::concept_stack_t& compiler::postfix_operation_stack()
    {
        return state().iPostfixOperationStack;
    }

    compiler::concept_stack_t& compiler::fold_stack()
    {
        return state().iFoldStack;
    }

    void compiler::display_probe_trace(translation_unit& aUnit, const i_source_fragment& aFragment)
    {
        iContext.cout() << "Probe trace:-" << std::endl;
        iContext.cout() << "================" << std::endl;
        while (std::adjacent_find(
            state().iDeepestProbe->stacks.begin(),
            state().iDeepestProbe->stacks.end(),
            [](auto&& lhs, auto&& rhs) 
            { 
                bool match = (!lhs.empty() && !rhs.empty() && lhs.front().atom == rhs.front().atom); 
                return !match;
            }) == state().iDeepestProbe->stacks.end())
        {
            bool first = true;
            for (auto& s : state().iDeepestProbe->stacks)
            {
                if (first)
                {
                    s.front().trace(aUnit, false, trace_filter(), iContext.cout());
                    first = false;
                }
                s.pop_front();
            }
        }
        for (auto i = state().iDeepestProbe->stacks.begin(); i != state().iDeepestProbe->stacks.end();)
            if (i->empty())
                i = state().iDeepestProbe->stacks.erase(i);
            else
                ++i;
        iContext.cout() << "================" << std::endl;
        for (auto s : state().iDeepestProbe->stacks)
        {
            for (auto const& e : s)
            {
                e.trace(aUnit, &e == &s.back(), trace_filter(), iContext.cout());
            }
            iContext.cout() << "----------------" << std::endl;
        }
    }

    bool compiler::is_finished(const parse_result& aResult)
    {
        switch (aResult.action)
        {
        case parse_result::ForNext:
            return true;
        case parse_result::Done:
            return true;
        case parse_result::Drain:
            return true;
        default:
            return false;
        }
    }

    bool compiler::finished(parse_result& aResult, bool aConsumeErrors)
    {
        switch (aResult.action)
        {
        case parse_result::ForNext:
            aResult.action = parse_result::Consumed;
            return true;
        case parse_result::Done:
            aResult.action = parse_result::Consumed;
            return true;
        case parse_result::Drain:
            aResult.action = parse_result::Consumed;
            return true;
        case parse_result::NoMatch:
            if (aConsumeErrors)
            {
                aResult.action = parse_result::Consumed;
                return true;
            }
            return false;
        case parse_result::Error:
            if (aConsumeErrors)
            {
                aResult.action = parse_result::NoMatch;
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    std::string compiler::location(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath)
    {
        uint32_t line = 1;
        uint32_t col = 1;
        for (auto pos = aFragment.begin(); pos != aSourcePos; ++pos)
        {
            if (*pos == '\n')
            {
                ++line;
                col = 1;
            }
            else
                ++col;
        }
        return (aShowFragmentFilePath && aFragment.source_file_path() != std::nullopt ? "file '" + *aFragment.source_file_path() + "', " : "") + "line " + boost::lexical_cast<std::string>(line) + ", col " + boost::lexical_cast<std::string>(col);
    }

    void compiler::throw_error(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, const std::string& aError)
    {
        throw std::runtime_error("(" + aError + ") " + location(aUnit, aFragment, aSourcePos));
    }
}
