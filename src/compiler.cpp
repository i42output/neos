/*
  compiler.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

    bool compiler::compile(program& aProgram)
    {
        for (auto& unit : aProgram.translationUnits)
            for (auto fragment = unit.fragments.begin(); fragment != unit.fragments.end();)
                if (fragment->imported())
                    fragment = unit.fragments.erase(fragment);
                else
                    (fragment++)->set_status(compilation_status::Pending);

        iStartTime = std::chrono::steady_clock::now();

        bool ok = true;

        try
        {
            for (auto& unit : aProgram.translationUnits)
                if (ok)
                    ok = compile(aProgram, unit);
        }
        catch(...)
        {
            iEndTime = std::chrono::steady_clock::now();
            throw;
        }

        iEndTime = std::chrono::steady_clock::now();

        return ok;
    }

    bool compiler::compile(program& aProgram, translation_unit& aUnit)
    {
        bool ok = true;

        for (auto& fragment : aUnit.fragments)
            if (ok)
                ok = compile(aProgram, aUnit, fragment);

        return ok;
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
                auto const semanticConcept = c->instantiate(context, conceptValue);
                astNode = semanticConcept;
                if (conceptName != "language.keyword")
                    foldStack.push_back(semanticConcept);
                if (context.compiler().trace() >= 2)
                    context.cout() << "Fold stack add: " << conceptName << " [" <<
                    neolib::to_escaped_string(conceptValue.to_std_string_view(), 32u, true) << "]" << std::endl;
            }
            else
                throw concept_not_found(parserAstNode.c.value());
        };
    }

    bool compiler::compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment)
    {
        if (aFragment.status() != compilation_status::Pending)
            return false;

        iCompilationStateStack.push_back(std::make_unique<compilation_state>(&aProgram, &aUnit, &aFragment));

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
            {
                aFragment.set_status(compilation_status::Error);

                iCompilationStateStack.pop_back();

                throw std::runtime_error("Failed to fold semantic concepts");
            }
        }

        aFragment.set_status(compilation_status::Compiled);

        iCompilationStateStack.pop_back();

        return ok;
    }

    bool compiler::compile(const i_source_fragment& aFragment)
    {
        auto& program = *state().program;
        auto& unit = *state().unit;
        auto& fragment = *unit.fragments.emplace(unit.fragments.end(), aFragment);
        return compile(program, unit, fragment);
    }

    void compiler::enter_namespace(neolib::i_string const& aNamespace)
    {
        if (state().namespaceScope.empty())
            state().namespaceScope.push_back(state().program->scope.create_child(aNamespace));
        else
            state().namespaceScope.push_back(state().namespaceScope.back()->create_child(aNamespace));
    }

    void compiler::leave_namespace()
    {
        if (!state().namespaceScope.empty())
            state().namespaceScope.pop_back();
        else
            throw std::runtime_error("Unmatched namespace scopes");
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
        while (fold2())
            ;
        return fold_stack().empty();
    }

    bool compiler::fold2()
    {
        if (fold_stack().empty())
            return false;

        auto trace_out = [&](std::string const& op, fold_stack::iterator const& lhs, std::optional<fold_stack::iterator> const& rhs = {}, const std::optional<neolib::ref_ptr<i_semantic_concept>> result = {})
            {
                std::ostringstream traceOutput;
                if (trace() >= 1)
                {
                    if (rhs)
                        traceOutput << op << ": " << (**lhs).trace() << " <- " << (***rhs).trace() <<
                            (result ? (*result) ? " = " + (**result).trace() : " = ()" : "") <<
                            std::endl;
                    else
                        traceOutput << op << ": " << (**lhs).trace() << " <-|" <<
                        (result ? (*result) ? " = " + (**result).trace() : " = ()" : "") <<
                        std::endl;
                }
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str() << std::flush;
            };

        bool didSome = false;
        for (auto ilhs = fold_stack().begin(); ilhs != fold_stack().end();)
        {
            auto& lhs = **ilhs;
            if (lhs.can_fold())
            {
                trace_out("Folding", ilhs);
                auto result = lhs.fold(iContext);
                trace_out("Folded", ilhs, {}, result);
                ilhs = fold_stack().erase(ilhs);
                didSome = true;
            }
            else if (std::next(ilhs) != fold_stack().end())
            {
                auto irhs = std::next(ilhs);
                auto& rhs = **irhs;
                if (rhs.can_fold(lhs))
                {
                    trace_out("Folding", irhs, ilhs);
                    auto result = rhs.fold(iContext, lhs);
                    trace_out("Folded", irhs, ilhs, result);
                    ilhs = fold_stack().erase(ilhs);
                    if (!result)
                        ilhs = fold_stack().erase(ilhs);
                    didSome = true;
                }
                else if (lhs.can_fold(rhs))
                {
                    trace_out("Folding", ilhs, irhs);
                    auto result = lhs.fold(iContext, rhs);
                    trace_out("Folded", ilhs, irhs, result);
                    fold_stack().erase(irhs);
                    if (!result)
                        ilhs = fold_stack().erase(ilhs);
                    didSome = true;
                }
                else
                    ++ilhs;
            }
            else
                ++ilhs;
        }
        if (!didSome)
        {
            if (fold_stack().size() == 1)
                throw_error(*state().unit, *state().fragment, fold_stack().front()->source().begin(),
                    "Failed to fold semantic concept: "s +
                    fold_stack().front()->trace());
            else
                throw_error(*state().unit, *state().fragment, fold_stack().front()->source().begin(),
                    "Failed to fold semantic concepts: "s +
                    (**fold_stack().begin()).trace() + " <- " + (**std::next(fold_stack().begin())).trace());
        }
        return didSome;
    }

    fold_stack& compiler::fold_stack()
    {
        return state().foldStack;
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
