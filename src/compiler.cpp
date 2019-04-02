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
#include <neolib/recursion.hpp>
#include <neolib/string_utf.hpp>
#include <neos/bytecode/opcodes.hpp>
#include <neos/bytecode/text.hpp>
#include <neos/language/compiler.hpp>

namespace neos::language
{
    compiler::compiler() :
        iTrace{ false }
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

    void compiler::compile(program& aProgram)
    {
        for (auto const& unit : aProgram.translationUnits)
        {
            auto source = unit.source.begin();
            while (source != unit.source.end())
            {
                iDeepestProbe = std::nullopt;
                auto probeResult = parse(compiler_pass::Probe, aProgram, unit, unit.schema->root(), source);
                if (!probeResult.sourceParsed || probeResult.sourceParsed == source)
                    throw_error(unit, iDeepestProbe ? *iDeepestProbe : source, "syntax error");
                source = *parse(compiler_pass::Emit, aProgram, unit, unit.schema->root(), source).sourceParsed;
            }
        }
    }

    compiler::parse_result compiler::parse(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse(" << aAtom.symbol() << ")" << std::endl;
        bool const expectingToken = !aAtom.expects().empty();
        if (aSource != aUnit.source.end())
        {
            if (expectingToken)
            {
                for (auto const& expect : aAtom.expects())
                {
                    auto result = parse_expect(aPass, aProgram, aUnit, aAtom, *expect, aSource);
                    if (result.sourceParsed && result.sourceParsed != aSource)
                        return result;
                }
                return parse_result{ optional_source_iterator{} };
            }
            return parse_tokens(aPass, aProgram, aUnit, aAtom, aSource);
        }
        else
            return expectingToken ? parse_result{ optional_source_iterator{} } : parse_result{ aSource };
        //auto loop = emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::u64{ 10 });
        //emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::i8{ -1 });
        //emit(aProgram.text, bytecode::opcode::B, loop);
    }

    compiler::parse_result compiler::parse_expect(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aExpectedToken, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_expect(" << aAtom.symbol() << ")" << std::endl;
        if (aExpectedToken.is_schema_atom() && aExpectedToken.as_schema_atom().is_schema_node_atom())
        {
            auto result = parse(aPass, aProgram, aUnit, aExpectedToken.as_schema_atom().as_schema_node_atom(), aSource);
            if (result.sourceParsed && result.sourceParsed != aSource)
                return parse_tokens(aPass, aProgram, aUnit, aAtom, aExpectedToken, *result.sourceParsed, false);
        }
        else if (aExpectedToken.is_schema_atom() && aExpectedToken.as_schema_atom().is_schema_terminal_atom())
            return parse_token(aPass, aProgram, aUnit, aAtom, aExpectedToken, aSource);
        return parse_result{ aSource };
    }

    compiler::parse_result compiler::parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_tokens(" << aAtom.symbol() << ")" << std::endl;
        auto currentSource = aSource;
        for (uint32_t pass = 1; pass <= 2; ++pass)
        {
            auto const& tokens = (pass == 1 ? aAtom.tokens() : aAtom.default_tokens());
            for (auto iterToken = tokens.begin(); currentSource != aUnit.source.end() && iterToken != tokens.end();)
            {
                auto const& token = *iterToken;
                auto result = parse_token(aPass, aProgram, aUnit, aAtom, *token.first(), currentSource);
                bool const ateSome = (result.sourceParsed && result.sourceParsed != currentSource);
                if (ateSome)
                {
                    auto const& matchedTokenValue = *token.second();
                    if (!matchedTokenValue.is_schema_atom() ||
                        !matchedTokenValue.as_schema_atom().is_schema_terminal_atom() ||
                        (matchedTokenValue.as_schema_atom().as_schema_terminal_atom().type() != schema_terminal::Done &&
                            matchedTokenValue.as_schema_atom().as_schema_terminal_atom().type() != schema_terminal::Next)) // todo: handle Next properly
                    {
                        auto trySource = result.sourceParsed;
                        if (aAtom.is_parent_of(matchedTokenValue))
                            result = parse_token(aPass, aProgram, aUnit, aAtom, matchedTokenValue, *trySource);
                        else
                            result = parse_tokens(aPass, aProgram, aUnit, aAtom, matchedTokenValue, *trySource);
                        bool const ateSome = (result.sourceParsed && result.sourceParsed != trySource);
                        if (ateSome || (result.sourceParsed && !aAtom.is_parent_of(matchedTokenValue)))
                        {
                            currentSource = *result.sourceParsed;
                            pass = 0;
                            break;
                        }
                        else
                            ++iterToken;
                    }
                    else
                        return result;
                }
                else
                    ++iterToken;
            }
        }
        return parse_result{ currentSource };
    }

    compiler::parse_result compiler::parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aMatchedTokenValue, source_iterator aSource, bool aSelf)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_tokens(" << aAtom.symbol() << ":" << aMatchedTokenValue.symbol() << ")" << std::endl;
        auto iterToken = std::find_if(aAtom.tokens().begin(), aAtom.tokens().end(), [&](auto&& aTokenMapEntry) 
        {
            auto const& token = *aTokenMapEntry.first();
            auto const& newTokenValue = *aTokenMapEntry.second();
            return (token == aMatchedTokenValue ||
                (token.is_concept_atom() && aMatchedTokenValue.is_concept_atom() &&
                    token.as_concept_atom().concept().is_ancestor_of(aMatchedTokenValue.as_concept_atom().concept())));
        });
        if (iterToken != aAtom.tokens().end())
        {
            auto const& newTokenValue = *iterToken->second();
            auto result = parse_token(aPass, aProgram, aUnit, aAtom, newTokenValue, aSource);
            if (result.sourceParsed)
                return parse_tokens(aPass, aProgram, aUnit, aAtom, newTokenValue, *result.sourceParsed);
            return result;
        }
        return parse_result{ aSource };
    }

    compiler::parse_result compiler::parse_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aToken, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_token(" << aAtom.symbol() << ":" << aToken.symbol() << ")" << std::endl;
        if (iDeepestProbe == std::nullopt || *iDeepestProbe < aSource)
            iDeepestProbe = aSource;
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
                case schema_terminal::Next:
                    // todo
                    // fall through...
                case schema_terminal::Done:
                    // todo: emit
                    break;
                case schema_terminal::String:
                    if (std::distance(aSource, aUnit.source.end()) >= terminal.symbol().size() &&
                        std::equal(terminal.symbol().begin(), terminal.symbol().end(), aSource))
                    {
                        return parse_result{ aSource + terminal.symbol().size() };
                    }
                    return parse_result{ aSource };
                default:
                    // do nothing
                    break;
                }
            }
        }
        else if (aToken.is_concept_atom())
            return parse_result{ aToken.as_concept_atom().concept().consume_token(aPass, aSource, aUnit.source.end()) };
        return parse_result{ aSource };
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
