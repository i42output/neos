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
    compiler::semantic_concept_stack_entry::semantic_concept_stack_entry(
        translation_unit* unit,
        i_source_fragment const* fragment,
        std::uint32_t level,
        const i_semantic_concept* semanticConcept,
        source_iterator sourceStart,
        source_iterator sourceEnd
    ) :
        unit{ unit },
        fragment{ fragment },
        level{ level },
        semanticConcept{ semanticConcept },
        sourceStart{ sourceStart },
        sourceEnd{ sourceEnd }
    {
    }

    bool compiler::semantic_concept_stack_entry::can_fold() const
    {
        return foldedConcept ? foldedConcept->can_fold() : semanticConcept->can_fold();
    }

    bool compiler::semantic_concept_stack_entry::can_fold(const semantic_concept_stack_entry& rhs) const
    {
        return foldedConcept ?
            foldedConcept->can_fold(rhs.foldedConcept ? *rhs.foldedConcept : *rhs.semanticConcept) :
            semanticConcept->can_fold(rhs.foldedConcept ? *rhs.foldedConcept : *rhs.semanticConcept);
    }

    void compiler::semantic_concept_stack_entry::instantiate_if_needed(i_context& aContext)
    {
        if (!foldedConcept)
            foldedConcept = semanticConcept->instantiate(aContext, sourceStart, sourceEnd);
    }

    void compiler::semantic_concept_stack_entry::fold(i_context& aContext)
    {
        instantiate_if_needed(aContext);
        foldedConcept = foldedConcept->fold(aContext);
    }

    void compiler::semantic_concept_stack_entry::fold(i_context& aContext, semantic_concept_stack_entry& rhs)
    {
        if (rhs.can_fold())
            rhs.fold(aContext);
        instantiate_if_needed(aContext);
        if (rhs.foldedConcept)
            foldedConcept = foldedConcept->fold(aContext, *rhs.foldedConcept);
        else
            foldedConcept = foldedConcept->fold(aContext, *rhs.semanticConcept);
        if (fragment == rhs.fragment)
        {
            sourceStart = std::min(sourceStart, rhs.sourceStart);
            sourceEnd = std::max(sourceEnd, rhs.sourceEnd);
            foldedConcept->update_source(sourceStart, sourceEnd);
        }
    }

    std::string compiler::semantic_concept_stack_entry::trace() const
    {
        return foldedConcept ? foldedConcept->trace() : semanticConcept->trace();
    }

    compiler::scoped_semantic_concept_folder::scoped_semantic_concept_folder(compiler& aCompiler) :
        scoped_semantic_concept_folder{ aCompiler, aCompiler.parse_stack() }
    {
    }

    compiler::scoped_semantic_concept_folder::scoped_semantic_concept_folder(compiler& aCompiler, semantic_concept_stack_t& aStack) :
        iCompiler{ aCompiler }, iStack{ aStack }, iScopeStart{ aStack.size() }
    {
    }

    compiler::scoped_semantic_concept_folder::~scoped_semantic_concept_folder()
    {
        if (std::uncaught_exceptions() == 0)
            fold();
    }

    void compiler::scoped_semantic_concept_folder::fold()
    {
        std::for_each(stack().begin() + iScopeStart, stack().end(), 
            [this](const semantic_concept_stack_entry& aEntry)
            {
                if (aEntry.semanticConcept != nullptr)
                {
                    iCompiler.fold_stack().push_back(aEntry);
                    if (iCompiler.trace() >= 2)
                        iCompiler.iContext.cout() << "prefold: " << "<" << aEntry.level << ": " << location(*aEntry.unit, *aEntry.fragment, aEntry.sourceStart, false) << "> "
                            << aEntry.semanticConcept->name() << " (" << std::string(aEntry.sourceStart, aEntry.sourceEnd) << ")" << std::endl;
                }
            });
        iCompiler.fold();
        stack().erase(stack().begin() + iScopeStart, stack().end());
    }

    void compiler::scoped_semantic_concept_folder::move_to(semantic_concept_stack_t& aOtherStack)
    {
        aOtherStack.insert(aOtherStack.end(), stack().begin() + iScopeStart, stack().end());
        stack().erase(stack().begin() + iScopeStart, stack().end());
    }

    compiler::semantic_concept_stack_t& compiler::scoped_semantic_concept_folder::stack()
    {
        return iStack;
    }

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

    void compiler::compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment)
    {
        if (aFragment.status() != compilation_status::Pending)
            return;

        aFragment.set_status(compilation_status::Compiling);

        // todo
            
        aFragment.set_status(compilation_status::Compiled);
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

    compiler::semantic_concept_stack_t& compiler::parse_stack()
    {
        return state().iParseStack;
    }

    compiler::semantic_concept_stack_t& compiler::postfix_operation_stack()
    {
        return state().iPostfixOperationStack;
    }

    compiler::semantic_concept_stack_t& compiler::fold_stack()
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
