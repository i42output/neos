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
            auto cursor = unit.source.begin();
            while (cursor != unit.source.end())
            {
                if (parse(compiler_pass::Probe, aProgram, unit, unit.schema->root(), cursor) == cursor)
                    throw_error(unit, cursor, "failed to parse");
                cursor = parse(compiler_pass::Emit, aProgram, unit, unit.schema->root(), cursor);
            }
        }
    }

    compiler::source_iterator compiler::parse(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse(" << aAtom.symbol() << ")" << std::endl;
        if (!aAtom.expects().empty())
        {
            for (auto const& expect : aAtom.expects())
            {
                auto nextSource = parse_expect(aPass, aProgram, aUnit, *expect, aSource);
                if (nextSource != aSource)
                    return nextSource;
            }
            throw_error(aUnit, aSource, "unexpected token");
        }
        return parse_tokens(aPass, aProgram, aUnit, aAtom, aSource);
        //auto loop = emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::u64{ 10 });
        //emit(aProgram.text, bytecode::opcode::ADD, bytecode::registers::R1, bytecode::i8{ -1 });
        //emit(aProgram.text, bytecode::opcode::B, loop);
    }

    compiler::source_iterator compiler::parse_expect(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aAtom, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_expect(" << aAtom.symbol() << ")" << std::endl;
        auto nextSource = aSource;
        if (aAtom.is_schema_atom() && aAtom.as_schema_atom().is_schema_node_atom())
            nextSource = parse_tokens(aPass, aProgram, aUnit, aAtom.as_schema_atom().as_schema_node_atom(), aSource);
        if (nextSource != aSource)
            return nextSource;
        return aSource;
    }

    compiler::source_iterator compiler::parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_tokens(" << aAtom.symbol() << ")" << std::endl;
        for (auto const& token : aAtom.tokens())
        {
            auto nextSource = parse_token(aPass, aProgram, aUnit, *token.first(), aSource);
            if (nextSource != aSource)
                return parse_tokens(aPass, aProgram, aUnit, aAtom, *token.second(), nextSource);
        }
        return aSource;
    }

    compiler::source_iterator compiler::parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aMatchedTokenValue, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_tokens(" << aAtom.symbol() << ":" << aMatchedTokenValue.symbol() << ")" << std::endl;
        for (auto const& token : aAtom.tokens())
        {
            if (*token.first() == aMatchedTokenValue)
            {
                auto const& newTokenValue = *token.second();
                if (&newTokenValue.parent() != &newTokenValue)
                    return parse_tokens(aPass, aProgram, aUnit, aAtom, newTokenValue, aSource);
                else if (newTokenValue.is_schema_atom() && newTokenValue.as_schema_atom().is_schema_node_atom())
                    return parse_tokens(aPass, aProgram, aUnit, newTokenValue.as_schema_atom().as_schema_node_atom(), aSource);
            }
        }
        return aSource;
    }

    compiler::source_iterator compiler::parse_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aToken, source_iterator aSource)
    {
        _limit_recursion_to_(compiler, aUnit.schema->meta().parserRecursionLimit);
        if (trace())
            std::cout << std::string(_compiler_recursion_limiter_.depth(), ' ') << "parse_token(" << aToken.symbol() << ")" << std::endl;
        if (aToken.is_schema_atom())
        {
            if (aToken.as_schema_atom().is_schema_node_atom())
                return parse(aPass, aProgram, aUnit, aToken.as_schema_atom().as_schema_node_atom(), aSource);
            else if (aToken.as_schema_atom().is_schema_terminal_atom())
            {
                auto const& terminal = aToken.as_schema_atom().as_schema_terminal_atom();
                switch (terminal.type())
                {
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
                        return aSource + terminal.symbol().size();
                    }
                    return aSource;
                default:
                    // do nothing
                    break;
                }
            }
        }
        else if (aToken.is_concept_atom())
            return aToken.as_concept_atom().concept().consume_token(aPass, aSource, aUnit.source.end());
        return aSource;
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
