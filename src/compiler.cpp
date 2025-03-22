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
#include <boost/lexical_cast.hpp>

#include <neolib/core/scoped.hpp>
#include <neolib/core/recursion.hpp>
#include <neolib/core/string_utf.hpp>
#include <neos/bytecode/opcodes.hpp>
#include <neos/bytecode/text.hpp>
#include <neos/i_context.hpp>
#include <neos/language/compiler.hpp>

namespace neos::language
{
    compiler::compiler(i_context& aContext) :
        iContext{ aContext }, iTrace { 0u }, iStartTime{ std::chrono::steady_clock::now() }, iEndTime{ std::chrono::steady_clock::now() }
    {
    }

    std::uint32_t compiler::trace() const
    {
        return iTrace;
    }

    const std::optional<std::string>& compiler::trace_filter() const
    {
        return iTraceFilter;
    }

    void compiler::set_trace(std::uint32_t aTrace, const std::optional<std::string>& aFilter)
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

    namespace
    {
        void walk_ast(i_context& context, ast& ast, fold_stack& foldStack, language::parser::ast_node const& parserAstNode, ast::node& astNode)
        {
            for (auto const& childParserNode : parserAstNode.children)
            {
                astNode.children().push_back(std::make_unique<ast::node>(std::monostate{}, astNode));
                auto& childNode = *astNode.children().back();
                walk_ast(context, ast, foldStack, *childParserNode, childNode);
            }

            neolib::string_view const conceptName{ parserAstNode.c.value() };
            neolib::string_view const conceptValue{ parserAstNode.value };
            auto c = context.find_concept(conceptName.to_std_string_view());
            if (c)
            {
                auto const semanticConcept = c->instantiate(context, conceptValue.begin(), conceptValue.end());
                astNode = semanticConcept;
                foldStack.push_back(semanticConcept);
            }
            else
                throw concept_not_found(parserAstNode.c.value());
        };
    }

    void compiler::compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment)
    {
        if (aFragment.status() != compilation_status::Pending)
            return;

        iCompilationStateStack.push_back(std::make_unique<compilation_state>(&aProgram, &aUnit));

        aFragment.set_status(compilation_status::Compiling);

        bool ok = false;

        for (auto const& stage : aUnit.schema->pipeline())
        {
            bool const last = (stage == aUnit.schema->pipeline().back());
            auto& parser = *stage->parser;
            for (auto const& discard : *stage->discard)
                parser.ignore(stage->symbolMap->at(discard));
            parser.set_debug_output(std::cerr, false, last);
            if (stage->root)
                ok = parser.parse(stage->symbolMap->at(stage->root.value()), aFragment.source().to_std_string_view());
            else
                ok = parser.parse(aFragment.source().to_std_string_view());
            if (!ok)
                break;
            if (last)
            {
                parser.create_ast();
                walk_ast(iContext, aUnit.ast, fold_stack(), parser.ast(), aUnit.ast.root());
            }
        }
            
        if (ok)
        {
            if (!fold())
                throw std::runtime_error("Failed to fold semantic concepts");
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
            auto& single = **isingle;
            if (single.can_fold())
            {
                std::string traceBefore = single.trace();
                if (trace() >= 1)
                    traceOutput << "folding: " << traceBefore << " <- " << traceBefore << std::endl;
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str();
                traceOutput.str({});
                auto result = single.fold(iContext);
                if (result != nullptr)
                {
                    if (trace() >= 1)
                        traceOutput << "folded: " << traceBefore << " <- " << traceBefore << " = " << result->trace() << std::endl;
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
            auto& rhs = **irhs;
            auto& lhs = **ilhs;
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

    fold_stack& compiler::fold_stack()
    {
        return state().iFoldStack;
    }

    std::string compiler::location(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath)
    {
        std::uint32_t line = 1;
        std::uint32_t col = 1;
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
