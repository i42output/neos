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
        void walk_ast(i_context& context, ast& ast, fold_stack& foldStack, parser::ast_node const& parserAstNode, i_ast_node& astNode)
        {
            for (auto const& childParserNode : parserAstNode.children)
            {
                astNode.children().push_back(neolib::make_ref<ast_node>(std::monostate{}, astNode));
                auto& childNode = *astNode.children().back();
                walk_ast(context, ast, foldStack, *childParserNode, childNode);
            }

            neolib::string_view const conceptName{ parserAstNode.c.value() };
            neolib::string_view const conceptValue{ parserAstNode.value };

            auto c = context.find_concept(conceptName.to_std_string_view());
            if (c)
            {
                auto const semanticConcept = c->instantiate(context, conceptValue);
                astNode.value() = semanticConcept;
                if (conceptName != "language.keyword")
                {
                    foldStack.push_back(neolib::ref_ptr<i_ast_node>{ astNode });
                    if (context.compiler().trace() >= 2)
                        context.cout() << "Fold stack add: " << conceptName << " [" <<
                        neolib::to_escaped_string(conceptValue.to_std_string_view(), 32u, true) << "]" << std::endl;
                }
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
            parser.set_debug_output(std::cerr, false, false);
            if (stage->root)
                ok = parser.parse(stage->symbolMap->at(stage->root.value()), aFragment.source().to_std_string_view());
            else
                ok = parser.parse(aFragment.source().to_std_string_view());
            if (!ok)
                break;
            if (last)
            {
                parser.create_ast();
                walk_ast(iContext, aUnit.ast, fold_stack(), parser.ast(), *aUnit.ast.root());
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

    i_source_fragment const& compiler::current_fragment() const
    {
        return *state().fragment;
    }

    i_scope const& compiler::current_scope() const
    {
        if (!state().scopeStack.empty())
            return *state().scopeStack.back();
        return state().program->scope;
    }

    i_scope& compiler::enter_scope(scope_type aScopeType, neolib::i_string const& aScopeName)
    {
        if (state().scopeStack.empty())
            state().scopeStack.push_back(state().program->scope.create_child(aScopeName, aScopeType));
        else
            state().scopeStack.push_back(state().scopeStack.back()->create_child(aScopeName, aScopeType));
        return *state().scopeStack.back();
    }

    void compiler::leave_scope(scope_type aScopeType)
    {
        switch (aScopeType)
        {
        case scope_type::Namespace:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Namespace)
                state().scopeStack.pop_back();
            else
                throw std::runtime_error("Unmatched namespace scopes");
            break;
        case scope_type::Class:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Class)
                state().scopeStack.pop_back();
            else
                throw std::runtime_error("Unmatched class scopes");
            break;
        case scope_type::Function:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Function)
                state().scopeStack.pop_back();
            else
                throw std::runtime_error("Unmatched function scopes");
            break;
        case scope_type::Block:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Block)
                state().scopeStack.pop_back();
            else
                throw std::runtime_error("Unmatched block scopes");
            break;
        }
    }

    i_operand_type const& compiler::lhs_operand() const
    {
        if (state().operandStack.size() == 1)
            return *(state().operandStack.rbegin());
        else if (state().operandStack.size() >= 2)
            return *std::next(state().operandStack.rbegin());
        throw std::logic_error("neos::language::compiler::lhs_operand");
    }

    i_operand_type const& compiler::rhs_operand() const
    {
        if (state().operandStack.size() >= 2)
            return *(state().operandStack.rbegin());
        throw std::logic_error("neos::language::compiler::rhs_operand");
    }

    void compiler::push_operand(i_operand_type const& aOperand)
    {
        state().operandStack.push_back(aOperand);
        if (trace() >= 2)
            iContext.cout() << "Pushed operand: " << aOperand << std::endl;
    }

    void compiler::pop_operand(i_data_type& aOperand)
    {
        if (!state().operandStack.empty())
        {
            aOperand = state().operandStack.back().get<i_data_type>();
            state().operandStack.pop_back();
            if (trace() >= 2)
                iContext.cout() << "Popped operand: " << aOperand << std::endl;
        }
        else
            throw std::runtime_error("No operand");
    }

    void compiler::push_operator(i_operator_type const& aOperator)
    {
        state().operatorStack.push_back(aOperator);
        if (trace() >= 2)
            iContext.cout() << "Pushed operator: " << aOperator << std::endl;
    }

    void compiler::pop_operator(i_operator_type& aOperator)
    {
        if (!state().operatorStack.empty())
        {
            aOperator = state().operatorStack.back();
            state().operatorStack.pop_back();
            if (trace() >= 2)
                iContext.cout() << "Popped operator: " << aOperator << std::endl;
        }
        else
            throw std::runtime_error("No operator");
    }

    void compiler::find_identifier(neolib::i_string_view const& aIdentifier, neolib::i_optional<i_data_type>& aResult) const
    {
        // todo
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

        auto trace_out = [&](std::string const& op, fold_stack::iterator const& lhs, std::optional<fold_stack::iterator> const& rhs = {}, const std::optional<neolib::ref_ptr<i_ast_node>> result = {})
            {
                std::ostringstream traceOutput;
                if (trace() >= 1)
                {
                    if (rhs)
                        traceOutput << op << ": " << 
                            (**lhs).trace() << " <- " << (***rhs).trace() <<
                            (result ? " = " + (**result).trace() : "") <<
                            std::endl;
                    else
                        traceOutput << op << ": " << 
                            (**lhs).trace() << " <-|" <<
                            (result ? " = " + (**result).trace() : "") <<
                            std::endl;
                }
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str() << std::flush;
            };

        bool didSome = false;
        try
        {
            for (auto ilhs = fold_stack().begin(); !didSome && ilhs != fold_stack().end();)
            {
                auto& lhs = **ilhs;
                if (lhs.can_fold())
                {
                    trace_out("Folding", ilhs);
                    auto result = lhs.fold(iContext);
                    trace_out("Folded", ilhs, {}, result);
                    ilhs = fold_stack().erase(ilhs);
                    if (!result->is_empty())
                        ilhs = fold_stack().insert(ilhs, result);
                    didSome = true;
                }
                else if (std::next(ilhs) != fold_stack().end())
                {
                    auto irhs = std::next(ilhs);
                    auto& rhs = **irhs;
                    bool const unrelated = !lhs.is_sibling(rhs) && !lhs.is_child(rhs) && !rhs.is_child(lhs);
                    if (rhs.can_fold())
                    {
                        trace_out("Folding", irhs);
                        auto result = rhs.fold(iContext);
                        trace_out("Folded", irhs, {}, result);
                        irhs = fold_stack().erase(irhs);
                        if (!result->is_empty())
                            irhs = fold_stack().insert(irhs, result);
                        didSome = true;
                    }
                    else if (unrelated && !(lhs.unstructured() && rhs.unstructured()))
                    {
                        if (trace() >= 3)
                            trace_out("Skipping", ilhs, irhs);
                        ++ilhs;
                    }
                    else if (lhs.name() == rhs.name() && lhs.has_ghosts())
                    {
                        if (lhs.holds_data() && !rhs.holds_data())
                        {
                            if (lhs.is_child(rhs))
                                fold_stack().erase(irhs);
                            else
                            {
                                rhs = lhs;
                                ilhs = fold_stack().erase(ilhs);
                            }
                            didSome = true;
                        }
                        else if (!lhs.holds_data() && rhs.holds_data())
                        {
                            if (lhs.is_child(rhs))
                            {
                                lhs = rhs;
                                fold_stack().erase(irhs);
                            }
                            else
                                fold_stack().erase(ilhs);
                            didSome = true;
                        }
                    }
                    else if (lhs.can_fold(rhs))
                    {
                        trace_out(!unrelated ? "Folding" : "Folding (unstructured)", ilhs, irhs);
                        auto result = lhs.fold(iContext, rhs);
                        trace_out(!unrelated ? "Folded" : "Folded (unstructured)", ilhs, irhs, result);
                        ilhs = fold_stack().erase(ilhs);
                        ilhs = fold_stack().erase(ilhs);
                        if (!result->is_empty())
                            ilhs = fold_stack().insert(ilhs, result);
                        didSome = true;
                    }
                    else if (rhs.can_fold(lhs))
                    {
                        trace_out(!unrelated ? "Folding" : "Folding (unstructured)", irhs, ilhs);
                        auto result = rhs.fold(iContext, lhs);
                        trace_out(!unrelated ? "Folded" : "Folded (unstructured)", irhs, ilhs, result);
                        ilhs = fold_stack().erase(ilhs);
                        ilhs = fold_stack().erase(ilhs);
                        if (!result->is_empty())
                            ilhs = fold_stack().insert(ilhs, result);
                        didSome = true;
                    }
                    else
                        break;
                }
                else
                    break;
            }
        }
        catch (compiler_error const&)
        {
            throw;
        }
        catch (std::exception const& ex)
        {
            throw_error(fold_stack().front()->source().begin(),
                "failed to fold semantic concept: "_s + ex.what());
        }
        if (!didSome)
        {
            if (trace() >= 3)
            {
                for (auto const& e : fold_stack())
                    iContext.cout() << "Fold stack: " << e->name() << " [" <<
                    neolib::to_escaped_string(e->source().to_std_string_view(), 32u, true) << "]" << std::endl;
            }
            if (fold_stack().size() == 1)
                throw_error(fold_stack().front()->source().begin(),
                    "failed to fold semantic concept: "_s +
                    fold_stack().front()->trace());
            else
                throw_error(fold_stack().front()->source().begin(),
                    "failed to fold semantic concepts: "_s +
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
        std::vector<std::string> lines;
        std::istringstream iss{ aFragment.source().to_std_string() };
        std::string nextLine;
        while (std::getline(iss, nextLine))
            lines.push_back(nextLine);
        std::size_t numberWidth = std::to_string(lines.size()).size();
        std::uint32_t lineNumber = 1u;
        std::ostringstream oss;
        for (auto const& outputLine : lines)
        {
            if (std::abs(static_cast<int>(line - lineNumber)) <= 5)
                oss << std::setw(numberWidth) << lineNumber << (lineNumber == line ? ">" : "|") << outputLine << std::endl;
            ++lineNumber;
        }
        oss << std::string(col + numberWidth, '-') << "^" << std::endl;
        std::string info = (aShowFragmentFilePath && aFragment.source_file_path() != std::nullopt ? aFragment.source_file_path()->to_std_string() : "") + "(" + boost::lexical_cast<std::string>(line) + "," + boost::lexical_cast<std::string>(col) + ")";
        oss << info;
        return oss.str();
    }

    void compiler::throw_error(source_iterator aSourcePos, neolib::i_string const& aError, neolib::i_string const& aErrorType)
    {
        throw compiler_error((location(*state().unit, *state().fragment, aSourcePos) + ": " + aErrorType + ": " + aError).to_std_string());
    }
}
