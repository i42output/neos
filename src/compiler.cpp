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
#include <neolib/raii.hpp>
#include <neolib/recursion.hpp>
#include <neolib/string_utf.hpp>
#include <neos/bytecode/opcodes.hpp>
#include <neos/bytecode/text.hpp>
#include <neos/language/compiler.hpp>

namespace neos::language
{
    compiler::emitter::emitter(compiler& aCompiler, compiler_pass aPass) :
        emitter{ aCompiler, aPass, aCompiler.emit_stack() }
    {
    }

    compiler::emitter::emitter(compiler& aCompiler, compiler_pass aPass, emit_stack_t& aEmitStack) :
        iCompiler{ aCompiler }, iPass{ aPass }, iEmitStack{ aEmitStack }, iEmitFrom{ aEmitStack.size() }
    {
    }

    compiler::emitter::~emitter()
    {
        emit();
    }

    void compiler::emitter::emit()
    {
        if (iPass == compiler_pass::Emit)
        {
            for (emit_stack_t::size_type stackIndex = iEmitFrom; stackIndex < emit_stack().size(); ++stackIndex)
                emit(emit_stack()[stackIndex]);
            emit_stack().erase(emit_stack().begin() + iEmitFrom, emit_stack().end());
        }
    }

    void compiler::emitter::move_to(emit_stack_t& aOtherStack)
    {
        aOtherStack.insert(aOtherStack.end(), emit_stack().begin() + iEmitFrom, emit_stack().end());
        emit_stack().erase(emit_stack().begin() + iEmitFrom, emit_stack().end());
    }

    compiler::emit_stack_t& compiler::emitter::emit_stack()
    {
        return iEmitStack;
    }

    void compiler::emitter::emit(const compiler::emit& aEmit)
    {
        if (aEmit.concept != nullptr)
        {
            if (iCompiler.trace_emits())
                std::cout << "emit: " << "<" << aEmit.level << "> " << aEmit.concept->name() << " (" << std::string(aEmit.sourceStart, aEmit.sourceEnd) << ")" << std::endl;
        }
    }

    compiler::compiler() :
        iTrace{ false }, iTraceEmits{ false }, iLevel{ 0u }, iStartTime{ std::chrono::steady_clock::now() }, iEndTime{ std::chrono::steady_clock::now() }
    {
    }

    bool compiler::trace() const
    {
        return iTrace;
    }

    void compiler::set_trace(bool aTrace)
    {
        iTrace = aTrace;
    }

    bool compiler::trace_emits() const
    {
        return iTraceEmits;
    }

    void compiler::set_trace_emits(bool aTraceEmits)
    {
        iTraceEmits = aTraceEmits;
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
        iStartTime = std::chrono::steady_clock::now();

        try
        {
            for (auto const& unit : aProgram.translationUnits)
            {
                auto source = unit.source.begin();
                while (source != unit.source.end())
                {
                    iDeepestProbe = std::nullopt;
                    auto result = parse(compiler_pass::Emit, aProgram, unit, unit.schema->root(), source);
                    if (result.action == parse_result::NoMatch)
                        throw_error(unit, iDeepestProbe ? *iDeepestProbe : source, "syntax error");
                    source = result.sourceParsed;
                }
            }
        }
        catch(...)
        {
            iEndTime = std::chrono::steady_clock::now();
            throw;
        }

        iEndTime = std::chrono::steady_clock::now();
    }

    compiler::parse_result compiler::parse(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        neolib::scoped_counter sc{ iLevel };
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse(compiler_pass::Probe, aProgram, aUnit, aAtom, aSource);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse(" << aAtom.symbol() << ")" << std::endl;
        emitter e{ *this, aPass };
        bool const expectingToken = !aAtom.expects().empty();
        if (aSource != aUnit.source.end())
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
                            if (current.level == previous.level + recursiveLevel && previous.concept != nullptr &&
                                aAtom.token().is_related_to(*current.concept) &&
                                aAtom.token().is_related_to(*previous.concept))
                            {
                                emit_stack().push_back(previous);
                                e.emit();
                                previous.concept = nullptr;
                            }
                        }
                    }
                    auto result = parse_tokens(aPass, aProgram, aUnit, aAtom, expected{ &*expect, &aAtom }, aSource);
                    if (is_finished(result, result.sourceParsed) || result.action == parse_result::Consumed)
                        return result;
                    if (iDeepestProbe == std::nullopt || *iDeepestProbe < result.sourceParsed)
                        iDeepestProbe = result.sourceParsed;
                }
                return parse_result{ aSource, parse_result::NoMatch };
            }
            else
                return parse_tokens(aPass, aProgram, aUnit, aAtom, expected{}, aSource);
        }
        else
            return expectingToken ? parse_result{ aSource, parse_result::NoMatch } : parse_result{ aSource };
        //auto loop = emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::u64{ 10 });
        //emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::i8{ -1 });
        //emit(aProgram.text, bytecode::opcode::B, loop);
    }

    compiler::parse_result compiler::parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const expected& aExpected, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse_tokens(compiler_pass::Probe, aProgram, aUnit, aAtom, aExpected, aSource);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_tokens(" << aAtom.symbol() << ")" << std::endl;
        emitter e{ *this, aPass };
        auto currentSource = aSource;
        bool defaultOk = aAtom.expect_none();
        bool const consumeSelf = (aExpected.what == nullptr || *aExpected.context == aAtom);
        bool handledExpect = false;
        auto iterToken = aExpected.what ? 
            std::find_if(aAtom.tokens().begin(), aAtom.tokens().end(), [aExpected](auto&& token) { return *token.first() == *aExpected.what; }) : 
            aAtom.tokens().begin();
        if (iterToken == aAtom.tokens().end() && aExpected.what)
        {
            if (aAtom.has_parent())
                return parse_tokens(aPass, aProgram, aUnit, aAtom.parent().as_schema_atom().as_schema_node_atom(), aExpected, aSource);
            return parse_result{ aSource, parse_result::NoMatch };
        }
        for (; currentSource != aUnit.source.end() && iterToken != aAtom.tokens().end();)
        {
            auto const& token = *iterToken->first();
            auto const& tokenValue = *iterToken->second();
            if (aAtom.is_token_node() && aAtom.token() == token)
                defaultOk = true;
            auto result = parse_token(aPass, aProgram, aUnit, aAtom, token, currentSource);
            if (finished(result, currentSource))
            {
                if (token.is_schema_atom() && token.as_schema_atom().is_schema_node_atom() && token.as_schema_atom().as_schema_node_atom().is_token_node())
                    return consume_token(aPass, aProgram, aUnit, aAtom, result);
            }
            bool const ateSome = (result.action == parse_result::Consumed);
            if (ateSome)
            {
                defaultOk = true;
                auto const& matchedTokenValue = tokenValue;
                if (!matchedTokenValue.is_schema_atom() || matchedTokenValue.as_schema_atom().is_schema_node_atom())
                {
                    auto trySource = result.sourceParsed;
                    if (aAtom.is_parent_of(matchedTokenValue))
                        result = parse_token(aPass, aProgram, aUnit, aAtom, matchedTokenValue, trySource);
                    if (is_finished(result, trySource))
                        return consumeSelf ? consume_token(aPass, aProgram, aUnit, aAtom, result) : result;
                    bool const ateSome = (result.action == parse_result::Consumed && result.sourceParsed != trySource);
                    if (ateSome || (result.action == parse_result::Consumed && !aAtom.is_parent_of(matchedTokenValue)))
                    {
                        if (matchedTokenValue.is_schema_atom())
                            result = parse_token_match(aPass, aProgram, aUnit, matchedTokenValue.as_schema_atom().as_schema_node_atom(), token, result.sourceParsed, false);
                        if (is_finished(result, result.sourceParsed))
                        {
                            result = consume_token(aPass, aProgram, aUnit, matchedTokenValue, result);
                            return consumeSelf ? consume_token(aPass, aProgram, aUnit, aAtom, result) : result;
                        }
                        if (result.action == parse_result::Consumed)
                        {
                            result = parse_token_match(aPass, aProgram, aUnit, aAtom, matchedTokenValue, result.sourceParsed);
                            if (is_finished(result, result.sourceParsed))
                                return consumeSelf ? consume_token(aPass, aProgram, aUnit, aAtom, result) : result;
                        }
                        if (result.action == parse_result::Consumed)
                        {
                            currentSource = result.sourceParsed;
                            iterToken = aAtom.tokens().begin();
                        }
                        else if (result.action != parse_result::Ignored && result.action != parse_result::Continue)
                            return result;
                    }
                    else if (result.action == parse_result::Consumed)
                        ++iterToken;
                    else
                        return result;
                }
                else if (matchedTokenValue.is_schema_atom() && matchedTokenValue.as_schema_atom().is_schema_terminal_atom())
                {
                    result = parse_token(aPass, aProgram, aUnit, aAtom, matchedTokenValue, result.sourceParsed);
                    switch(result.action)
                    {
                    case parse_result::Drain:
                    case parse_result::Done:
                    case parse_result::ForNext:
                        return result;
                    case parse_result::Ignored:
                    case parse_result::Continue:
                        currentSource = result.sourceParsed;
                        iterToken = aAtom.tokens().begin();
                        break;
                    case parse_result::NoMatch:
                        if (token.is_schema_atom() && token.as_schema_atom().is_schema_terminal_atom() && token.as_schema_atom().as_schema_terminal_atom().type() == schema_terminal::String)
                            result.sourceParsed = std::prev(result.sourceParsed, token.symbol().size());
                        return result;
                    case parse_result::Error:
                        return parse_result{ result.sourceParsed, parse_result::NoMatch };
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
                    return parse_result{ aSource, parse_result::NoMatch };
                ++iterToken;
            }
            handledExpect = true;
            if (iterToken == aAtom.tokens().end())
            {
                if (token.is_schema_atom() && token.as_schema_atom().is_schema_terminal_atom() &&
                    token.as_schema_atom().as_schema_terminal_atom().type() == schema_terminal::Default)
                {
                    auto result = parse_token(aPass, aProgram, aUnit, aAtom, tokenValue, currentSource);
                    switch (result.action)
                    {
                    case parse_result::Drain:
                    case parse_result::Done:
                    case parse_result::ForNext:
                        if (!defaultOk)
                            result.action = parse_result::NoMatch;
                        return result;
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
                        return parse_result{ result.sourceParsed, parse_result::NoMatch };
                    }
                }
            }
        }
        return parse_result{ currentSource, parse_result::NoMatch };
    }

    compiler::parse_result compiler::parse_token_match(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aMatchResult, source_iterator aSource, bool aConsumeMatchResult, bool aSelf)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse_token_match(compiler_pass::Probe, aProgram, aUnit, aAtom, aMatchResult, aSource, aConsumeMatchResult, aSelf);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_token_match(" << aAtom.symbol() << ":" << aMatchResult.symbol() << ")" << std::endl;
        emitter poe{ *this, aPass, postfix_operation_stack() };
        std::optional<emitter> e;
        if (!aSelf)
            e.emplace(*this, aPass);
        parse_result result{ aSource };
        if (aConsumeMatchResult)
        {
            if (aMatchResult.is_concept_atom())
            {
                if (aMatchResult.as_concept_atom().concept().emit_as() == emit_type::Infix)
                    result = consume_concept_atom(aPass, aProgram, aUnit, aMatchResult, aMatchResult.as_concept_atom().concept(), result);
                else if (aMatchResult.as_concept_atom().concept().emit_as() == emit_type::Postfix)
                    result = consume_concept_atom(aPass, aProgram, aUnit, aMatchResult, aMatchResult.as_concept_atom().concept(), result, postfix_operation_stack());
            }
            else
                result = consume_token(aPass, aProgram, aUnit, aMatchResult, result);
            if (e)
                e->emit();
        }
        if (result.action == parse_result::Consumed)
        {
            auto nextMatch = aAtom.find_token(aMatchResult);
            if (nextMatch != nullptr)
            {
                if (*nextMatch != aAtom)
                {
                    if (!nextMatch->is_concept_atom())
                        result = parse_token(aPass, aProgram, aUnit, aAtom, *nextMatch, result.sourceParsed);
                    if (result.action == parse_result::Consumed)
                        result = parse_token_match(aPass, aProgram, aUnit, aAtom, *nextMatch, result.sourceParsed, true, true);
                }
            }
        }
        poe.move_to(emit_stack());
        return result;
    }

    compiler::parse_result compiler::parse_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aToken, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (aPass == compiler_pass::Emit)
        {
            auto probeResult = parse_token(compiler_pass::Probe, aProgram, aUnit, aAtom, aToken, aSource);
            if (probeResult.action == parse_result::NoMatch)
                return probeResult;
        }
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_token(" << aAtom.symbol() << ":" << aToken.symbol() << ")" << std::endl;
        emitter e{ *this, aPass };
        if (aToken.is_schema_atom())
        {
            if (aToken.as_schema_atom().is_schema_node_atom())
                return parse(aPass, aProgram, aUnit, aToken.as_schema_atom().as_schema_node_atom(), aSource);
            else if (aToken.as_schema_atom().is_schema_terminal_atom())
            {
                auto const& terminal = aToken.as_schema_atom().as_schema_terminal_atom();
                switch (terminal.type())
                {
                case schema_terminal::Default:
                    break;
                case schema_terminal::Error:
                    return parse_result{ aSource, parse_result::Error };
                case schema_terminal::Ignore:
                    return parse_result{ aSource, parse_result::Ignored };
                case schema_terminal::Continue:
                    {
                        auto result = consume_token(aPass, aProgram, aUnit, aAtom, parse_result{ aSource });
                        if (result.action == parse_result::Consumed)
                            result.action = parse_result::Continue;
                        return result;
                    }
                case schema_terminal::Done:
                    {
                        auto result = consume_token(aPass, aProgram, aUnit, aAtom, parse_result{ aSource });
                        if (result.action == parse_result::Consumed)
                            result.action = parse_result::Done;
                        return result;
                    }
                case schema_terminal::Drain:
                    return parse_result{ aSource, parse_result::Drain };
                case schema_terminal::Next:
                    return parse_result{ aSource, parse_result::ForNext };
                case schema_terminal::String:
                    {
                        auto const terminalSymbol = terminal.symbol().to_std_string_view();
                        if (static_cast<std::size_t>(std::distance(aSource, aUnit.source.end())) >= terminalSymbol.size() && std::equal(terminalSymbol.begin(), terminalSymbol.end(), aSource))
                            return parse_result{ aSource + terminalSymbol.size() };
                    }
                default:
                    // do nothing
                    break;
                }
            }
        }
        else if (aToken.is_concept_atom())
            return consume_token(aPass, aProgram, aUnit, aToken, parse_result{ aSource });
        return parse_result{ aSource, parse_result::NoMatch };
    }

    compiler::parse_result compiler::consume_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aToken, const parse_result& aResult)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        parse_result result = aResult;
        if (aToken.is_concept_atom())
            result = consume_concept_token(aPass, aProgram, aUnit, aToken.as_concept_atom().concept(), result);
        else if (aToken.is_schema_atom() && aToken.as_schema_atom().is_schema_node_atom())
            for (auto& concept : aToken.as_schema_atom().as_schema_node_atom().is_a())
                if (result.action != parse_result::NoMatch && result.action != parse_result::Ignored)
                    result = consume_concept_atom(aPass, aProgram, aUnit, aToken, *concept, result);
        return result;
    }

    compiler::parse_result compiler::consume_concept_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_concept& aConcept, const parse_result& aResult)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        auto consumeResult = aConcept.consume_token(aPass, aResult.sourceParsed, aUnit.source.end());
        if (consumeResult.consumed && aPass == compiler_pass::Emit)
        {
            if (trace())
                std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "push_emit(token): " << aConcept.name().to_std_string() << " (" << std::string(aResult.sourceParsed, consumeResult.sourceParsed) << ")" << std::endl;
            emit_stack().push_back(emit{ iLevel, &aConcept, aResult.sourceParsed, consumeResult.sourceParsed });
        }
        return parse_result{ consumeResult.sourceParsed, consumeResult.consumed ? aResult.action : parse_result::NoMatch };
    }

    compiler::parse_result compiler::consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult)
    {
        return consume_concept_atom(aPass, aProgram, aUnit, aAtom, aConcept, aResult, emit_stack());
    }
        
    compiler::parse_result compiler::consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult, emit_stack_t& aEmitStack)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        auto consumeResult = aConcept.consume_atom(aPass, aAtom, aResult.sourceParsed, aUnit.source.end());
        if (consumeResult.consumed && aPass == compiler_pass::Emit)
        {
            if (trace())
                std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "push_emit(atom): " << aConcept.name().to_std_string() << " (" << std::string(aResult.sourceParsed, consumeResult.sourceParsed) << ")" << std::endl;
            aEmitStack.push_back(emit{ iLevel, &aConcept, aResult.sourceParsed, consumeResult.sourceParsed });
        }
        return parse_result{ consumeResult.sourceParsed, consumeResult.consumed ? aResult.action : parse_result::NoMatch };
    }

    compiler::emit_stack_t& compiler::emit_stack()
    {
        return iEmitStack;
    }

    compiler::emit_stack_t& compiler::postfix_operation_stack()
    {
        return iPostfixOperationStack;
    }

    bool compiler::is_finished(const parse_result& aResult, source_iterator aSource)
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

    bool compiler::finished(parse_result& aResult, source_iterator aSource, bool aConsumeErrors)
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

    void compiler::throw_error(const translation_unit& aUnit, source_iterator aSourcePos, const std::string& aError)
    {
        uint32_t line = 1;
        uint32_t col = 1;
        for (auto pos = aUnit.source.begin(); pos != aSourcePos; ++pos)
        {
            if (*pos == '\n')
            {
                ++line;
                col = 1;
            }
            else
                ++col;
        }
        throw std::runtime_error("(" + aError + ") line " + boost::lexical_cast<std::string>(line) + ", col " + boost::lexical_cast<std::string>(col));
    }
}
