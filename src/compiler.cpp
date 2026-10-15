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
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <limits>
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

        iModule = {};
        aProgram.text.clear();
        aProgram.symbolTable.clear();
        aProgram.scope.clear();

        try
        {
            for (auto& unit : aProgram.translationUnits)
                if (ok)
                    ok = compile(aProgram, unit);
            if (ok)
                emit_module(aProgram);
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
            if (ok && fragment.status() == compilation_status::Pending)
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
                throw concept_not_found(conceptName.to_std_string_view(), conceptValue.begin());
        };

        template <std::size_t Index = 1u>
        data_type make_symbol_data(i_symbol_table_entry& aSymbol)
        {
            if constexpr (Index < std::variant_size_v<data_type::std_type>)
            {
                using alternative = std::variant_alternative_t<Index, data_type::std_type>;
                if (static_cast<std::size_t>(aSymbol.type()) == Index)
                {
                    if constexpr (std::is_default_constructible_v<alternative>)
                    {
                        alternative symbolData;
                        symbolData.symbol() = &aSymbol;
                        return symbolData;
                    }
                }
                else
                    return make_symbol_data<Index + 1u>(aSymbol);
            }
            throw std::runtime_error("unsupported symbol type: " + type_name(aSymbol.type()));
        }
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
                auto& unitRoot = *aUnit.ast.root();
                unitRoot.children().push_back(neolib::make_ref<ast_node>(std::monostate{}, unitRoot)); // each fragment gets its own root
                walk_ast(iContext, aUnit.ast, fold_stack(), parser.ast(), *unitRoot.children().back());
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

            aFragment.set_status(compilation_status::Compiled);
        }
        else
            aFragment.set_status(compilation_status::Error);


        iCompilationStateStack.pop_back();

        return ok;
    }

    bool compiler::compile(const i_source_fragment& aFragment)
    {
        auto& program = *state().program;
        auto& unit = *state().unit;
        if (aFragment.source_file_path().has_value())
            for (auto const& existing : unit.fragments)
                if (existing.source_file_path() == aFragment.source_file_path())
                    return true; // already compiled (package imported more than once)
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
        if (aScopeType == scope_type::Function)
        {
            if (!state().pendingFunction)
                throw std::logic_error("neos::language::compiler::enter_scope: no function defined");
            auto& function = iModule.functions[*state().pendingFunction];
            function.scope = static_cast<i_function_scope const*>(&*state().scopeStack.back());
            function.operandBase = state().operandStack.size();
            function.fromImportedFragment = state().fragment->imported();
            function.entry = state().pendingEntry;
            state().pendingEntry = false;
            state().function = state().pendingFunction;
            state().pendingFunction = std::nullopt;
        }
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
            {
                finish_function();
                state().scopeStack.pop_back();
            }
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
            auto const& operand = state().operandStack.back();
            if (operand.holds_alternative<i_symbol_reference>())
            {
                auto const& reference = operand.get<i_symbol_reference>();
                auto* const symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
                if (symbol == nullptr)
                    throw std::runtime_error("undefined identifier '" + reference.name().to_std_string() + "'");
                aOperand = make_symbol_data(*symbol);
            }
            else if (operand.holds_alternative<i_fragment>())
                throw std::logic_error("neos::language::compiler::pop_operand: operand is not data");
            else
                aOperand = operand.get<i_data_type>();
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

    void compiler::define_symbol(i_symbol_name const& aName, symbol_type aType)
    {
        auto& symbols = state().program->symbolTable[&current_scope()];
        auto const existing = symbols.find(aName);
        if (existing != symbols.end() && aType != symbol_type::Function && existing->second().type() == aType && state().function)
        {
            // no block scopes yet: a local redeclared with the same type (e.g. a second loop variable) reuses the existing local
            auto const& signature = current_function().scope->function_signature();
            bool parameter = false;
            if (signature.parameters().has_value())
                for (auto const& p : signature.parameters().value())
                    if (p.parameter_name() == aName)
                        parameter = true;
            if (!parameter)
                return;
        }
        if (existing != symbols.end() && (aType != symbol_type::Function || existing->second().type() != symbol_type::Function))
            throw std::runtime_error("redefinition of '" + aName.to_std_string() + "'");
        std::uint32_t index = 0u;
        if (aType == symbol_type::Function)
        {
            std::string qualifiedName = current_scope().qualified_name().to_std_string() + "::" + aName.to_std_string();
            while (qualifiedName.starts_with("::"))
                qualifiedName.erase(0, 2);
            index = add_function(qualifiedName, false);
            state().pendingFunction = index;
        }
        else if (state().function)
        {
            auto& function = current_function();
            auto const& signature = function.scope->function_signature();
            std::optional<std::uint32_t> parameterIndex;
            std::uint32_t inParameters = 0u;
            if (signature.parameters().has_value())
                for (auto const& parameter : signature.parameters().value())
                {
                    if (parameter.direction() == parameter_direction::Out)
                        continue;
                    if (parameter.parameter_name() == aName)
                        parameterIndex = inParameters;
                    ++inParameters;
                }
            if (parameterIndex)
                index = *parameterIndex;
            else
            {
                index = inParameters + static_cast<std::uint32_t>(function.locals.size());
                function.locals.push_back(aType);
            }
        }
        else
            throw std::runtime_error("global variables are not supported: '" + aName.to_std_string() + "'");
        symbols.insert(aName, symbol_table_entry{ aType, index });
        if (trace() >= 2)
            iContext.cout() << "Defined symbol: " << current_scope().qualified_name() << "::" << aName << " : " << type_name(aType) << std::endl;
    }

    void compiler::find_identifier(neolib::i_string_view const& aIdentifier, neolib::i_optional<i_data_type>& aResult) const
    {
        auto* const symbol = find_symbol(current_scope(), aIdentifier);
        if (symbol != nullptr)
            aResult = make_symbol_data(*symbol);
        else
            aResult = std::nullopt;
    }

    i_symbol_table_entry* compiler::find_symbol(i_scope const& aScope, neolib::i_string_view const& aName) const
    {
        neolib::string const name{ aName };
        for (i_scope const* scope = &aScope;; scope = &scope->parent())
        {
            auto const symbols = state().program->symbolTable.find(scope);
            if (symbols != state().program->symbolTable.end())
            {
                auto const symbol = symbols->second().find(name);
                if (symbol != symbols->second().end())
                    return &symbol->second();
            }
            if (!scope->has_parent())
                return nullptr;
        }
    }

    // --- WASM code generation

    namespace
    {
        using bytes = std::vector<std::uint8_t>;

        constexpr std::uint32_t DataBase = 16u;

        void put_op(bytes& aCode, bytecode::opcode aOpcode)
        {
            text t;
            t << aOpcode;
            for (auto b : t)
                aCode.push_back(static_cast<std::uint8_t>(b));
        }

        void put_u32(bytes& aCode, std::uint64_t aValue)
        {
            do
            {
                std::uint8_t b = aValue & 0x7Fu;
                aValue >>= 7u;
                if (aValue != 0u)
                    b |= 0x80u;
                aCode.push_back(b);
            } while (aValue != 0u);
        }

        void put_s64(bytes& aCode, std::int64_t aValue)
        {
            bool more = true;
            while (more)
            {
                std::uint8_t b = aValue & 0x7F;
                aValue >>= 7;
                if ((aValue == 0 && (b & 0x40u) == 0u) || (aValue == -1 && (b & 0x40u) != 0u))
                    more = false;
                else
                    b |= 0x80u;
                aCode.push_back(b);
            }
        }

        void put_u32_padded(bytes& aCode, std::size_t aOffset, std::uint32_t aValue)
        {
            for (std::size_t i = 0u; i < 5u; ++i)
            {
                std::uint8_t b = aValue & 0x7Fu;
                aValue >>= 7u;
                if (i < 4u)
                    b |= 0x80u;
                aCode[aOffset + i] = b;
            }
        }

        void put_name(bytes& aCode, std::string const& aName)
        {
            put_u32(aCode, aName.size());
            aCode.insert(aCode.end(), aName.begin(), aName.end());
        }

        template <typename T>
        void put_raw(bytes& aCode, T aValue)
        {
            auto const* p = reinterpret_cast<std::uint8_t const*>(&aValue);
            aCode.insert(aCode.end(), p, p + sizeof(T));
        }

        void put_section(bytes& aModule, std::uint8_t aId, bytes const& aContents)
        {
            aModule.push_back(aId);
            put_u32(aModule, aContents.size());
            aModule.insert(aModule.end(), aContents.begin(), aContents.end());
        }

        bool is_float(type aType)
        {
            return aType == type::F32 || aType == type::F64 || aType == type::Fbig;
        }

        bool is_64(type aType)
        {
            return aType == type::I64 || aType == type::U64 || aType == type::Ibig;
        }

        bool is_unsigned(type aType)
        {
            return aType == type::U8 || aType == type::U16 || aType == type::U32 || aType == type::U64 || aType == type::Boolean;
        }

        bool is_numeric(type aType)
        {
            return (aType >= type::Boolean && aType <= type::Fbig);
        }

        bool is_flexible(type aType)
        {
            return aType == type::Ibig || aType == type::Fbig;
        }

        std::optional<std::uint8_t> wasm_type(type aType)
        {
            switch (aType)
            {
            case type::Void:
                return std::nullopt;
            case type::I64:
            case type::U64:
            case type::Ibig:
                return 0x7Eu;
            case type::F32:
                return 0x7Du;
            case type::F64:
            case type::Fbig:
                return 0x7Cu;
            case type::UNKNOWN:
                throw std::runtime_error("type unknown (type inference not supported)");
            default:
                return 0x7Fu;
            }
        }

        std::uint32_t rank(type aType)
        {
            switch (aType)
            {
            case type::Boolean: return 0u;
            case type::I8: case type::U8: return 1u;
            case type::I16: case type::U16: return 2u;
            case type::I32: case type::U32: return 3u;
            case type::I64: case type::U64: return 4u;
            case type::F32: return 5u;
            case type::F64: return 6u;
            default: return 3u;
            }
        }

        void convert(bytes& aCode, type aFrom, type aTo)
        {
            using bytecode::opcode;
            if (aFrom == aTo)
                return;
            if (aFrom == type::String || aTo == type::String)
                throw std::runtime_error("cannot convert " + type_name(aFrom) + " to " + type_name(aTo));
            if (!is_numeric(aFrom) || !is_numeric(aTo))
                throw std::runtime_error("cannot convert " + type_name(aFrom) + " to " + type_name(aTo));
            if (aTo == type::Boolean)
            {
                // compare against zero in the source type
                if (aFrom == type::F32)
                {
                    put_op(aCode, opcode::F32Const);
                    put_raw(aCode, 0.0f);
                    put_op(aCode, opcode::F32Ne);
                }
                else if (is_float(aFrom))
                {
                    put_op(aCode, opcode::F64Const);
                    put_raw(aCode, 0.0);
                    put_op(aCode, opcode::F64Ne);
                }
                else if (is_64(aFrom))
                {
                    put_op(aCode, opcode::I64Const);
                    put_s64(aCode, 0);
                    put_op(aCode, opcode::I64Ne);
                }
                else
                {
                    put_op(aCode, opcode::I32Const);
                    put_s64(aCode, 0);
                    put_op(aCode, opcode::I32Ne);
                }
                return;
            }
            bool const fromFloat = is_float(aFrom);
            bool const toFloat = is_float(aTo);
            bool const fromUnsigned = is_unsigned(aFrom);
            bool const toUnsigned = is_unsigned(aTo);
            if (!fromFloat && !toFloat)
            {
                if (is_64(aFrom) && !is_64(aTo))
                    put_op(aCode, opcode::I32ConvertI64);
                else if (!is_64(aFrom) && is_64(aTo))
                    put_op(aCode, fromUnsigned ? opcode::I64UConvertI32 : opcode::I64SConvertI32);
            }
            else if (!fromFloat && toFloat)
            {
                if (aTo == type::F32)
                    put_op(aCode, is_64(aFrom) ? (fromUnsigned ? opcode::F32UConvertI64 : opcode::F32SConvertI64) : (fromUnsigned ? opcode::F32UConvertI32 : opcode::F32SConvertI32));
                else
                    put_op(aCode, is_64(aFrom) ? (fromUnsigned ? opcode::F64UConvertI64 : opcode::F64SConvertI64) : (fromUnsigned ? opcode::F64UConvertI32 : opcode::F64SConvertI32));
                return;
            }
            else if (fromFloat && toFloat)
            {
                if (aFrom == type::F32 && aTo != type::F32)
                    put_op(aCode, opcode::F64ConvertF32);
                else if (aFrom != type::F32 && aTo == type::F32)
                    put_op(aCode, opcode::F32ConvertF64);
                return;
            }
            else
            {
                if (aFrom == type::F32)
                    put_op(aCode, is_64(aTo) ? (toUnsigned ? opcode::I64UConvertF32 : opcode::I64SConvertF32) : (toUnsigned ? opcode::I32UConvertF32 : opcode::I32SConvertF32));
                else
                    put_op(aCode, is_64(aTo) ? (toUnsigned ? opcode::I64UConvertF64 : opcode::I64SConvertF64) : (toUnsigned ? opcode::I32UConvertF64 : opcode::I32SConvertF64));
            }
            switch (aTo)
            {
            case type::I8:
                put_op(aCode, opcode::I32SExtendI8);
                break;
            case type::I16:
                put_op(aCode, opcode::I32SExtendI16);
                break;
            case type::U8:
                put_op(aCode, opcode::I32Const);
                put_s64(aCode, 0xFF);
                put_op(aCode, opcode::I32And);
                break;
            case type::U16:
                put_op(aCode, opcode::I32Const);
                put_s64(aCode, 0xFFFF);
                put_op(aCode, opcode::I32And);
                break;
            default:
                break;
            }
        }

        void append(fragment& aTarget, i_fragment const& aSource)
        {
            auto& code = aTarget.fragmentCode.as_std_vector();
            auto const offset = static_cast<std::uint32_t>(code.size());
            for (auto const& r : aSource.relocations())
                aTarget.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first() + offset, r.second() });
            for (auto b : aSource.code())
                code.push_back(b);
        }

        bool within(neolib::i_string_view const& aInner, neolib::i_string_view const& aOuter)
        {
            if (aInner.empty())
                return false;
            auto const inner = aInner.to_std_string_view();
            auto const outer = aOuter.to_std_string_view();
            return inner.data() >= outer.data() && inner.data() + inner.size() <= outer.data() + outer.size();
        }

        fragment make_fragment(fragment_kind aKind, type aType, neolib::i_string_view const& aSource)
        {
            fragment result;
            result.fragmentKind = aKind;
            result.valueType = aType;
            result.fragmentSource = aSource;
            return result;
        }

        bool is_immediate(operand_type const& aOperand)
        {
            if (!aOperand.holds_alternative<i_data_type>())
                return false;
            bool result = false;
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (!std::is_same_v<vt, std::monostate>)
                        result = !d.symbol().has_value() && d.value().has_value();
                }, std::get<data_type>(aOperand));
            return result;
        }

        // immediate value as a scalar: integer (as int64), real (as double), boolean or string
        struct immediate
        {
            type t = type::UNKNOWN;
            std::int64_t i = 0;
            double f = 0.0;
            std::string s;
        };

        immediate get_immediate(operand_type const& aOperand)
        {
            immediate result;
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (!std::is_same_v<vt, std::monostate>)
                    {
                        using value_type = typename vt::type;
                        if constexpr (std::is_same_v<value_type, ibig>)
                        {
                            result.t = type::Ibig;
                            try
                            {
                                result.i = std::stoll(d.value().value().to_string());
                            }
                            catch (std::out_of_range const&)
                            {
                                throw std::runtime_error("integer literal out of range: " + d.value().value().to_string());
                            }
                        }
                        else if constexpr (std::is_same_v<value_type, fbig>)
                        {
                            result.t = type::Fbig;
                            result.f = std::stod(d.value().value().to_string()); // (neonumeric's operator double is not correctly rounded)
                        }
                        else if constexpr (std::is_same_v<value_type, string>)
                        {
                            result.t = type::String;
                            result.s = d.value().value().to_std_string();
                        }
                        else if constexpr (std::is_same_v<value_type, boolean>)
                        {
                            result.t = type::Boolean;
                            result.i = d.value().value() ? 1 : 0;
                        }
                        else if constexpr (std::is_floating_point_v<value_type>)
                        {
                            result.t = type_to_enum_v<value_type>;
                            result.f = static_cast<double>(d.value().value());
                        }
                        else if constexpr (std::is_integral_v<value_type>)
                        {
                            result.t = type_to_enum_v<value_type>;
                            result.i = static_cast<std::int64_t>(d.value().value());
                        }
                        else
                            throw std::runtime_error("unsupported immediate value");
                    }
                }, std::get<data_type>(aOperand));
            return result;
        }

        data_type make_immediate(immediate const& aValue)
        {
            switch (aValue.t)
            {
            case type::Boolean:
                return data<boolean>{ aValue.i != 0 };
            case type::String:
                return data<string>{ string{ aValue.s } };
            case type::F32:
            case type::F64:
            case type::Fbig:
                {
                    std::ostringstream oss;
                    oss << std::setprecision(17) << aValue.f;
                    return data<fbig>{ fbig{ oss.str() } };
                }
            default:
                return data<ibig>{ ibig{ std::to_string(aValue.i) } };
            }
        }
    }

    compiler::wasm_function& compiler::current_function()
    {
        if (!state().function)
            throw std::runtime_error("not in a function");
        return iModule.functions[*state().function];
    }

    std::uint32_t compiler::add_function(std::string const& aName, bool aImported)
    {
        iModule.functions.emplace_back();
        iModule.functions.back().name = aName;
        iModule.functions.back().imported = aImported;
        return static_cast<std::uint32_t>(iModule.functions.size() - 1u);
    }

    std::uint32_t compiler::runtime_function(std::string const& aName, std::vector<type> const& aParameters, type aReturnType)
    {
        for (std::uint32_t id = 0u; id < iModule.functions.size(); ++id)
            if (iModule.functions[id].imported && iModule.functions[id].name == aName)
                return id;
        auto const id = add_function(aName, true);
        auto& function = iModule.functions[id];
        function.signature.set_name(neolib::string{ aName });
        function_parameters parameters;
        for (auto const& p : aParameters)
        {
            function_parameter parameter;
            parameter.set_parameter_type(p);
            parameters.push_back(parameter);
        }
        function.signature.set_parameters(parameters);
        function.signature.set_return_type(aReturnType);
        return id;
    }

    std::uint32_t compiler::string_literal(std::string const& aValue)
    {
        auto existing = iModule.strings.find(aValue);
        if (existing != iModule.strings.end())
            return existing->second;
        while (iModule.data.size() % 4u != 0u)
            iModule.data.push_back(0u);
        auto const address = DataBase + static_cast<std::uint32_t>(iModule.data.size());
        put_raw(iModule.data, static_cast<std::uint32_t>(aValue.size()));
        iModule.data.insert(iModule.data.end(), aValue.begin(), aValue.end());
        iModule.strings[aValue] = address;
        return address;
    }

    fragment compiler::materialize(operand_type const& aOperand, std::optional<type> aTargetType)
    {
        using bytecode::opcode;
        if (aOperand.holds_alternative<i_fragment>())
        {
            fragment result{ aOperand.get<i_fragment>() };
            if (result.kind() != fragment_kind::Value && result.kind() != fragment_kind::Argument)
                throw std::runtime_error("expression expected");
            if (aTargetType && result.valueType != *aTargetType)
            {
                convert(result.fragmentCode.as_std_vector(), result.valueType, *aTargetType);
                result.valueType = *aTargetType;
            }
            return result;
        }
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        i_symbol_table_entry* symbol = nullptr;
        if (aOperand.holds_alternative<i_symbol_reference>())
        {
            auto const& reference = aOperand.get<i_symbol_reference>();
            symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
            if (symbol == nullptr)
                throw std::runtime_error("undefined identifier '" + reference.name().to_std_string() + "'");
        }
        else
        {
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (!std::is_same_v<vt, std::monostate>)
                        if (d.symbol().has_value())
                            symbol = d.symbol().value();
                }, std::get<data_type>(aOperand));
        }
        if (symbol != nullptr)
        {
            if (symbol->type() == type::Function)
                throw std::runtime_error("function used as a value");
            put_op(code, opcode::LocalGet);
            put_u32(code, symbol->index());
            result.valueType = symbol->type();
            result.fragmentLvalue = symbol;
        }
        else
        {
            auto const value = get_immediate(aOperand);
            type const target = aTargetType.value_or(
                value.t == type::Ibig ? (value.i >= std::numeric_limits<std::int32_t>::min() && value.i <= std::numeric_limits<std::int32_t>::max() ? type::I32 : type::I64) :
                value.t == type::Fbig ? type::F64 : value.t);
            if (value.t == type::String)
            {
                if (target != type::String)
                    throw std::runtime_error("cannot convert string to " + type_name(target));
                put_op(code, opcode::I32Const);
                put_s64(code, static_cast<std::int32_t>(string_literal(value.s)));
            }
            else if (target == type::String)
                throw std::runtime_error("cannot convert " + type_name(value.t) + " to string");
            else if (target == type::F32)
            {
                put_op(code, opcode::F32Const);
                put_raw(code, static_cast<float>(is_float(value.t) ? value.f : static_cast<double>(value.i)));
            }
            else if (target == type::F64 || target == type::Fbig)
            {
                put_op(code, opcode::F64Const);
                put_raw(code, is_float(value.t) ? value.f : static_cast<double>(value.i));
            }
            else
            {
                std::int64_t const i = is_float(value.t) ? static_cast<std::int64_t>(value.f) : value.i;
                if (is_64(target))
                {
                    put_op(code, opcode::I64Const);
                    put_s64(code, i);
                }
                else
                {
                    put_op(code, opcode::I32Const);
                    put_s64(code, target == type::Boolean ? ((is_float(value.t) ? value.f != 0.0 : value.i != 0) ? 1 : 0) : static_cast<std::int32_t>(i));
                }
            }
            result.valueType = target;
            return result;
        }
        if (aTargetType && result.valueType != *aTargetType)
        {
            convert(code, result.valueType, *aTargetType);
            result.valueType = *aTargetType;
        }
        return result;
    }

    fragment compiler::pop_value()
    {
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const operand = state().operandStack.back();
        if (operand.holds_alternative<i_fragment>() && operand.get<i_fragment>().kind() != fragment_kind::Value)
            throw std::runtime_error("expression expected");
        state().operandStack.pop_back();
        return materialize(operand);
    }

    std::vector<fragment> compiler::pop_fragments(neolib::i_string_view const& aSource, fragment_kind aKind)
    {
        std::vector<fragment> result;
        auto& stack = state().operandStack;
        while (stack.size() > (state().function ? current_function().operandBase : 0u) &&
            stack.back().holds_alternative<i_fragment>() &&
            stack.back().get<i_fragment>().kind() == aKind &&
            within(stack.back().get<i_fragment>().source(), aSource))
        {
            result.insert(result.begin(), fragment{ stack.back().get<i_fragment>() });
            stack.pop_back();
        }
        return result;
    }

    void compiler::declare_import(i_function_signature const& aSignature)
    {
        define_symbol(aSignature.name(), type::Function);
        auto& function = iModule.functions[*state().pendingFunction];
        function.imported = true;
        function.signature = aSignature;
        function.fromImportedFragment = state().fragment->imported();
        state().pendingFunction = std::nullopt;
    }

    void compiler::set_entry_point()
    {
        if (state().function)
            current_function().entry = true;
        else
            state().pendingEntry = true;
    }

    void compiler::emit_operation(operation aOperation)
    {
        using bytecode::opcode;
        bool const unary = (aOperation == operation::Negate || aOperation == operation::LogicalNot);
        if (state().operandStack.size() < (unary ? 1u : 2u))
            throw std::runtime_error("operand expected");
        auto const rhs = state().operandStack.back();
        state().operandStack.pop_back();
        std::optional<operand_type> lhs;
        if (!unary)
        {
            lhs = state().operandStack.back();
            state().operandStack.pop_back();
        }
        auto operand_type_of = [&](operand_type const& aOperand) -> type
        {
            if (aOperand.holds_alternative<i_fragment>())
                return aOperand.get<i_fragment>().value_type();
            if (aOperand.holds_alternative<i_symbol_reference>())
            {
                auto const& reference = aOperand.get<i_symbol_reference>();
                auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
                if (symbol == nullptr)
                    throw std::runtime_error("undefined identifier '" + reference.name().to_std_string() + "'");
                return symbol->type();
            }
            if (is_immediate(aOperand))
                return get_immediate(aOperand).t;
            return static_cast<type>(aOperand.get<i_data_type>().index());
        };
        bool const relational = (aOperation >= operation::Equal && aOperation <= operation::GreaterThanOrEqual);
        bool const logical = (aOperation >= operation::LogicalAnd && aOperation <= operation::LogicalNot);
        // constant folding
        if (is_immediate(rhs) && (unary || is_immediate(*lhs)))
        {
            auto const r = get_immediate(rhs);
            immediate result;
            if (unary)
            {
                result = r;
                if (aOperation == operation::LogicalNot)
                {
                    result.t = type::Boolean;
                    result.i = (r.t == type::Fbig ? r.f == 0.0 : r.i == 0) ? 1 : 0;
                }
                else if (r.t == type::String || r.t == type::Boolean)
                    throw std::runtime_error("invalid operand for negation");
                else
                {
                    result.i = -r.i;
                    result.f = -r.f;
                }
            }
            else
            {
                auto const l = get_immediate(*lhs);
                if (l.t == type::String || r.t == type::String)
                {
                    if (l.t != r.t)
                        throw std::runtime_error("invalid string operation");
                    int const c = l.s.compare(r.s);
                    result.t = relational ? type::Boolean : type::String;
                    switch (aOperation)
                    {
                    case operation::Add: result.s = l.s + r.s; break;
                    case operation::Equal: result.i = c == 0; break;
                    case operation::NotEqual: result.i = c != 0; break;
                    case operation::LessThan: result.i = c < 0; break;
                    case operation::GreaterThan: result.i = c > 0; break;
                    case operation::LessThanOrEqual: result.i = c <= 0; break;
                    case operation::GreaterThanOrEqual: result.i = c >= 0; break;
                    default: throw std::runtime_error("invalid string operation");
                    }
                }
                else if (logical)
                {
                    result.t = type::Boolean;
                    bool const lb = is_float(l.t) ? l.f != 0.0 : l.i != 0;
                    bool const rb = is_float(r.t) ? r.f != 0.0 : r.i != 0;
                    result.i = (aOperation == operation::LogicalAnd ? (lb && rb) : (lb || rb)) ? 1 : 0;
                }
                else if (is_float(l.t) || is_float(r.t))
                {
                    double const lf = is_float(l.t) ? l.f : static_cast<double>(l.i);
                    double const rf = is_float(r.t) ? r.f : static_cast<double>(r.i);
                    result.t = relational ? type::Boolean : type::Fbig;
                    switch (aOperation)
                    {
                    case operation::Add: result.f = lf + rf; break;
                    case operation::Subtract: result.f = lf - rf; break;
                    case operation::Multiply: result.f = lf * rf; break;
                    case operation::Divide: result.f = lf / rf; break;
                    case operation::Equal: result.i = lf == rf; break;
                    case operation::NotEqual: result.i = lf != rf; break;
                    case operation::LessThan: result.i = lf < rf; break;
                    case operation::GreaterThan: result.i = lf > rf; break;
                    case operation::LessThanOrEqual: result.i = lf <= rf; break;
                    case operation::GreaterThanOrEqual: result.i = lf >= rf; break;
                    default: break;
                    }
                }
                else
                {
                    result.t = relational ? type::Boolean : type::Ibig;
                    switch (aOperation)
                    {
                    case operation::Add: result.i = l.i + r.i; break;
                    case operation::Subtract: result.i = l.i - r.i; break;
                    case operation::Multiply: result.i = l.i * r.i; break;
                    case operation::Divide:
                        if (r.i == 0)
                            throw std::runtime_error("division by zero");
                        result.i = l.i / r.i; 
                        break;
                    case operation::Equal: result.i = l.i == r.i; break;
                    case operation::NotEqual: result.i = l.i != r.i; break;
                    case operation::LessThan: result.i = l.i < r.i; break;
                    case operation::GreaterThan: result.i = l.i > r.i; break;
                    case operation::LessThanOrEqual: result.i = l.i <= r.i; break;
                    case operation::GreaterThanOrEqual: result.i = l.i >= r.i; break;
                    default: break;
                    }
                }
            }
            push_operand(operand_type{ make_immediate(result) });
            return;
        }
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        if (unary)
        {
            type const t = operand_type_of(rhs);
            if (aOperation == operation::LogicalNot)
            {
                append(result, materialize(rhs, type::Boolean));
                put_op(code, opcode::I32Eqz);
                result.valueType = type::Boolean;
            }
            else if (is_float(t))
            {
                append(result, materialize(rhs, t));
                put_op(code, t == type::F32 ? opcode::F32Neg : opcode::F64Neg);
                result.valueType = t;
            }
            else if (t == type::String || t == type::Boolean)
                throw std::runtime_error("invalid operand for negation");
            else
            {
                put_op(code, is_64(t) ? opcode::I64Const : opcode::I32Const);
                put_s64(code, 0);
                append(result, materialize(rhs, t));
                put_op(code, is_64(t) ? opcode::I64Sub : opcode::I32Sub);
                if (!is_64(t))
                    convert(code, type::I32, t); // re-narrow (i8/u8/i16/u16)
                result.valueType = t;
            }
            push_operand(operand_type{ result });
            return;
        }
        type const lt = operand_type_of(*lhs);
        type const rt = operand_type_of(rhs);
        if (logical)
        {
            append(result, materialize(*lhs, type::Boolean));
            put_op(code, opcode::If);
            code.push_back(0x7Fu);
            if (aOperation == operation::LogicalAnd)
            {
                append(result, materialize(rhs, type::Boolean));
                put_op(code, opcode::Else);
                put_op(code, opcode::I32Const);
                put_s64(code, 0);
            }
            else
            {
                put_op(code, opcode::I32Const);
                put_s64(code, 1);
                put_op(code, opcode::Else);
                append(result, materialize(rhs, type::Boolean));
            }
            put_op(code, opcode::End);
            result.valueType = type::Boolean;
            push_operand(operand_type{ result });
            return;
        }
        if (lt == type::String || rt == type::String)
        {
            if (lt != rt)
                throw std::runtime_error("cannot combine " + type_name(lt) + " and " + type_name(rt));
            append(result, materialize(*lhs, type::String));
            append(result, materialize(rhs, type::String));
            if (aOperation == operation::Add)
            {
                auto const id = runtime_function("neos::string_concat", { type::String, type::String }, type::String);
                put_op(code, opcode::CallFunction);
                result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), id });
                code.resize(code.size() + 5u);
                result.valueType = type::String;
            }
            else if (relational)
            {
                auto const id = runtime_function("neos::string_compare", { type::String, type::String }, type::I32);
                put_op(code, opcode::CallFunction);
                result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), id });
                code.resize(code.size() + 5u);
                put_op(code, opcode::I32Const);
                put_s64(code, 0);
                static std::map<operation, opcode> const sCompare = {
                    { operation::Equal, opcode::I32Eq }, { operation::NotEqual, opcode::I32Ne },
                    { operation::LessThan, opcode::I32LtS }, { operation::GreaterThan, opcode::I32GtS },
                    { operation::LessThanOrEqual, opcode::I32LeS }, { operation::GreaterThanOrEqual, opcode::I32GeS } };
                put_op(code, sCompare.at(aOperation));
                result.valueType = type::Boolean;
            }
            else
                throw std::runtime_error("invalid string operation");
            push_operand(operand_type{ result });
            return;
        }
        if (!is_numeric(lt) || !is_numeric(rt))
            throw std::runtime_error("invalid operands: " + type_name(lt) + " and " + type_name(rt));
        type common;
        if (is_flexible(lt) && !is_flexible(rt))
            common = (lt == type::Fbig && !is_float(rt)) ? type::F64 : rt;
        else if (!is_flexible(lt) && is_flexible(rt))
            common = (rt == type::Fbig && !is_float(lt)) ? type::F64 : lt;
        else if (is_flexible(lt) && is_flexible(rt))
            common = (lt == type::Fbig || rt == type::Fbig) ? type::F64 : type::I32;
        else
            common = rank(rt) > rank(lt) ? rt : lt;
        if (common == type::Boolean && !relational)
            common = type::I32;
        // widen if an integer literal does not fit the other operand's type
        for (operand_type const* operand : { static_cast<operand_type const*>(&*lhs), &rhs })
            if (is_immediate(*operand) && get_immediate(*operand).t == type::Ibig && !is_float(common) && !is_64(common))
            {
                auto const i = get_immediate(*operand).i;
                bool const fits = is_unsigned(common) ? 
                    (i >= 0 && i <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) :
                    (i >= std::numeric_limits<std::int32_t>::min() && i <= std::numeric_limits<std::int32_t>::max());
                if (!fits)
                    common = type::I64;
            }
        if (rank(common) < 3u)
            common = is_unsigned(common) ? type::U32 : type::I32;
        append(result, materialize(*lhs, common));
        append(result, materialize(rhs, common));
        bool const f = is_float(common);
        bool const w = is_64(common);
        bool const u = is_unsigned(common);
        opcode op = opcode::Nop;
        switch (aOperation)
        {
        case operation::Add: op = f ? (common == type::F32 ? opcode::F32Add : opcode::F64Add) : (w ? opcode::I64Add : opcode::I32Add); break;
        case operation::Subtract: op = f ? (common == type::F32 ? opcode::F32Sub : opcode::F64Sub) : (w ? opcode::I64Sub : opcode::I32Sub); break;
        case operation::Multiply: op = f ? (common == type::F32 ? opcode::F32Mul : opcode::F64Mul) : (w ? opcode::I64Mul : opcode::I32Mul); break;
        case operation::Divide: op = f ? (common == type::F32 ? opcode::F32Div : opcode::F64Div) : (w ? (u ? opcode::I64DivU : opcode::I64DivS) : (u ? opcode::I32DivU : opcode::I32DivS)); break;
        case operation::Equal: op = f ? (common == type::F32 ? opcode::F32Eq : opcode::F64Eq) : (w ? opcode::I64Eq : opcode::I32Eq); break;
        case operation::NotEqual: op = f ? (common == type::F32 ? opcode::F32Ne : opcode::F64Ne) : (w ? opcode::I64Ne : opcode::I32Ne); break;
        case operation::LessThan: op = f ? (common == type::F32 ? opcode::F32Lt : opcode::F64Lt) : (w ? (u ? opcode::I64LtU : opcode::I64LtS) : (u ? opcode::I32LtU : opcode::I32LtS)); break;
        case operation::GreaterThan: op = f ? (common == type::F32 ? opcode::F32Gt : opcode::F64Gt) : (w ? (u ? opcode::I64GtU : opcode::I64GtS) : (u ? opcode::I32GtU : opcode::I32GtS)); break;
        case operation::LessThanOrEqual: op = f ? (common == type::F32 ? opcode::F32Le : opcode::F64Le) : (w ? (u ? opcode::I64LeU : opcode::I64LeS) : (u ? opcode::I32LeU : opcode::I32LeS)); break;
        case operation::GreaterThanOrEqual: op = f ? (common == type::F32 ? opcode::F32Ge : opcode::F64Ge) : (w ? (u ? opcode::I64GeU : opcode::I64GeS) : (u ? opcode::I32GeU : opcode::I32GeS)); break;
        default: break;
        }
        put_op(code, op);
        result.valueType = relational ? type::Boolean : common;
        push_operand(operand_type{ result });
    }

    void compiler::emit_statement(neolib::i_string_view const& aSource)
    {
        auto& stack = state().operandStack;
        if (stack.size() <= (state().function ? current_function().operandBase : 0u))
            return;
        auto const& top = stack.back();
        if (top.holds_alternative<i_fragment>() && top.get<i_fragment>().kind() != fragment_kind::Value)
        {
            if (top.get<i_fragment>().kind() == fragment_kind::Statement && top.get<i_fragment>().source().empty())
            {
                fragment statement{ top.get<i_fragment>() };
                statement.fragmentSource = aSource;
                stack.back() = operand_type{ statement };
            }
            return;
        }
        auto value = pop_value();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        append(statement, value);
        if (value.valueType != type::Void)
            put_op(statement.fragmentCode.as_std_vector(), bytecode::opcode::Drop);
        push_operand(operand_type{ statement });
    }

    void compiler::emit_block(neolib::i_string_view const& aSource)
    {
        auto block = make_fragment(fragment_kind::Statement, type::Void, aSource);
        for (auto const& statement : pop_fragments(aSource, fragment_kind::Statement))
            append(block, statement);
        push_operand(operand_type{ block });
    }

    void compiler::emit_return(neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto& function = current_function();
        auto const& signature = function.scope->function_signature();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (signature.return_type() == type::Void)
            throw std::runtime_error("procedure cannot return a value");
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const operand = state().operandStack.back();
        state().operandStack.pop_back();
        append(statement, materialize(operand, signature.return_type()));
        auto& code = statement.fragmentCode.as_std_vector();
        if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.direction() != parameter_direction::In)
                {
                    put_op(code, opcode::LocalGet);
                    put_u32(code, find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() })->index());
                }
        put_op(code, opcode::Return);
        push_operand(operand_type{ statement });
    }

    void compiler::emit_assignment(neolib::i_string_view const& aTarget, neolib::i_string_view const& aSource)
    {
        auto* const symbol = find_symbol(current_scope(), aTarget);
        if (symbol == nullptr)
            throw std::runtime_error("undefined identifier '" + aTarget.to_std_string() + "'");
        if (symbol->type() == type::Function)
            throw std::runtime_error("cannot assign to function '" + aTarget.to_std_string() + "'");
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const operand = state().operandStack.back();
        state().operandStack.pop_back();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        append(statement, materialize(operand, symbol->type()));
        put_op(statement.fragmentCode.as_std_vector(), bytecode::opcode::LocalSet);
        put_u32(statement.fragmentCode.as_std_vector(), symbol->index());
        push_operand(operand_type{ statement });
    }

    void compiler::emit_argument(neolib::i_string_view const& aSource)
    {
        if (state().operandStack.empty())
            throw std::runtime_error("argument expected");
        auto const operand = state().operandStack.back();
        if (operand.holds_alternative<i_fragment>() && operand.get<i_fragment>().kind() != fragment_kind::Value)
            throw std::runtime_error("argument expected");
        state().operandStack.pop_back();
        auto argument = materialize(operand);
        argument.fragmentKind = fragment_kind::Argument;
        argument.fragmentSource = aSource;
        push_operand(operand_type{ argument });
    }

    void compiler::emit_call(neolib::i_string_view const& aFunctionName, neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto arguments = pop_fragments(aSource, fragment_kind::Argument);
        std::vector<std::string> path;
        {
            std::string name = aFunctionName.to_std_string();
            name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), name.end());
            std::size_t pos = 0u;
            for (auto next = name.find("::"); next != std::string::npos; next = name.find("::", pos))
            {
                path.push_back(name.substr(pos, next - pos));
                pos = next + 2u;
            }
            path.push_back(name.substr(pos));
        }
        auto signature_of = [&](wasm_function const& aFunction) -> i_function_signature const&
        {
            return aFunction.scope != nullptr ? aFunction.scope->function_signature() : static_cast<i_function_signature const&>(aFunction.signature);
        };
        std::optional<std::uint32_t> chosen;
        for (i_scope const* scope = &current_scope(); scope != nullptr && !chosen; scope = scope->has_parent() ? &scope->parent() : nullptr)
        {
            i_scope const* target = scope;
            for (std::size_t i = 0u; target != nullptr && i + 1u < path.size(); ++i)
            {
                i_scope const* next = nullptr;
                for (auto const& child : target->children())
                    if (child->type() == scope_type::Namespace && child->name().to_std_string() == path[i])
                        next = &*child;
                target = next;
            }
            if (target == nullptr)
                continue;
            auto const symbols = state().program->symbolTable.find(target);
            if (symbols == state().program->symbolTable.end())
                continue;
            std::optional<std::uint32_t> countMatch;
            for (auto const& symbol : symbols->second())
            {
                if (symbol.first().to_std_string() != path.back() || symbol.second().type() != type::Function)
                    continue;
                auto const& signature = signature_of(iModule.functions[symbol.second().index()]);
                std::size_t const parameterCount = signature.parameters().has_value() ? signature.parameters().value().size() : 0u;
                if (parameterCount != arguments.size())
                    continue;
                bool exact = true;
                for (std::size_t i = 0u; i < parameterCount; ++i)
                    if (signature.parameters().value()[i].parameter_type() != arguments[i].valueType)
                        exact = false;
                if (exact)
                {
                    chosen = symbol.second().index();
                    break;
                }
                if (!countMatch)
                    countMatch = symbol.second().index();
            }
            if (!chosen)
                chosen = countMatch;
        }
        if (!chosen)
            throw std::runtime_error("no matching function for call to '" + aFunctionName.to_std_string() + "' with " + std::to_string(arguments.size()) + " argument(s)");
        auto const& signature = signature_of(iModule.functions[*chosen]);
        type const returnType = signature.return_type();
        auto call = make_fragment(returnType == type::Void ? fragment_kind::Statement : fragment_kind::Value, returnType, returnType == type::Void ? neolib::string_view{ aSource } : neolib::string_view{});
        std::vector<std::pair<i_symbol_table_entry*, type>> outputs;
        for (std::size_t i = 0u; i < arguments.size(); ++i)
        {
            auto const& parameter = signature.parameters().value()[i];
            auto& argument = arguments[i];
            if (parameter.direction() != parameter_direction::In)
            {
                if (argument.fragmentLvalue == nullptr)
                    throw std::runtime_error("argument " + std::to_string(i + 1u) + " of '" + aFunctionName.to_std_string() + "' must be a variable");
                outputs.emplace_back(argument.fragmentLvalue, parameter.parameter_type());
            }
            if (parameter.direction() != parameter_direction::Out)
            {
                if (argument.valueType != parameter.parameter_type())
                    convert(argument.fragmentCode.as_std_vector(), argument.valueType, parameter.parameter_type());
                append(call, argument);
            }
        }
        auto& code = call.fragmentCode.as_std_vector();
        put_op(code, opcode::CallFunction);
        call.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), *chosen });
        code.resize(code.size() + 5u);
        for (auto o = outputs.rbegin(); o != outputs.rend(); ++o)
        {
            convert(code, o->second, o->first->type());
            put_op(code, opcode::LocalSet);
            put_u32(code, o->first->index());
        }
        push_operand(operand_type{ call });
    }

    void compiler::emit_if(neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto elseArm = pop_fragments(aSource, fragment_kind::Else);
        auto elseIfArms = pop_fragments(aSource, fragment_kind::ElseIf);
        auto body = pop_fragments(aSource, fragment_kind::Statement);
        if (body.size() != 1u)
            throw std::runtime_error("if statement body expected");
        if (state().operandStack.empty())
            throw std::runtime_error("if condition expected");
        auto const condition = state().operandStack.back();
        state().operandStack.pop_back();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        append(statement, materialize(condition, type::Boolean));
        put_op(statement.fragmentCode.as_std_vector(), opcode::If);
        statement.fragmentCode.push_back(0x40u);
        append(statement, body[0]);
        for (auto const& arm : elseIfArms)
        {
            put_op(statement.fragmentCode.as_std_vector(), opcode::Else);
            append(statement, arm);
        }
        if (!elseArm.empty())
        {
            put_op(statement.fragmentCode.as_std_vector(), opcode::Else);
            append(statement, elseArm[0]);
        }
        for (std::size_t i = 0u; i <= elseIfArms.size(); ++i)
            put_op(statement.fragmentCode.as_std_vector(), opcode::End);
        push_operand(operand_type{ statement });
    }

    void compiler::emit_else_if(neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto body = pop_fragments(aSource, fragment_kind::Statement);
        if (body.size() != 1u)
            throw std::runtime_error("else if statement body expected");
        if (state().operandStack.empty())
            throw std::runtime_error("else if condition expected");
        auto const condition = state().operandStack.back();
        state().operandStack.pop_back();
        auto arm = make_fragment(fragment_kind::ElseIf, type::Void, aSource);
        append(arm, materialize(condition, type::Boolean));
        put_op(arm.fragmentCode.as_std_vector(), opcode::If);
        arm.fragmentCode.push_back(0x40u);
        append(arm, body[0]);
        push_operand(operand_type{ arm });
    }

    void compiler::emit_else(neolib::i_string_view const& aSource)
    {
        auto arm = make_fragment(fragment_kind::Else, type::Void, aSource);
        for (auto const& statement : pop_fragments(aSource, fragment_kind::Statement))
            append(arm, statement);
        push_operand(operand_type{ arm });
    }

    void compiler::emit_loop(loop_type aLoopType, neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto pop_statement = [&]()
        {
            auto statements = pop_fragments(aSource, fragment_kind::Statement);
            if (statements.empty())
                throw std::runtime_error("loop statement expected");
            for (std::size_t i = 0u; i + 1u < statements.size(); ++i)
                push_operand(operand_type{ statements[i] });
            return statements.back();
        };
        auto pop_condition = [&]()
        {
            if (state().operandStack.empty())
                throw std::runtime_error("loop condition expected");
            auto const condition = state().operandStack.back();
            state().operandStack.pop_back();
            return materialize(condition, type::Boolean);
        };
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        auto& code = statement.fragmentCode.as_std_vector();
        auto begin_loop = [&]()
        {
            put_op(code, opcode::Block);
            code.push_back(0x40u);
            put_op(code, opcode::Loop);
            code.push_back(0x40u);
        };
        auto end_loop = [&]()
        {
            put_op(code, opcode::End);
            put_op(code, opcode::End);
        };
        auto exit_unless = [&](fragment const& aCondition)
        {
            append(statement, aCondition);
            put_op(code, opcode::I32Eqz);
            put_op(code, opcode::BrIf);
            put_u32(code, 1u);
        };
        switch (aLoopType)
        {
        case loop_type::While:
            {
                auto body = pop_statement();
                auto condition = pop_condition();
                begin_loop();
                exit_unless(condition);
                append(statement, body);
                put_op(code, opcode::Br);
                put_u32(code, 0u);
                end_loop();
            }
            break;
        case loop_type::DoWhile:
            {
                auto condition = pop_condition();
                auto body = pop_statement();
                begin_loop();
                append(statement, body);
                append(statement, condition);
                put_op(code, opcode::BrIf);
                put_u32(code, 0u);
                end_loop();
            }
            break;
        case loop_type::For:
            {
                auto body = pop_statement();
                auto step = pop_statement();
                auto condition = pop_condition();
                auto init = pop_fragments(aSource, fragment_kind::Statement);
                for (auto const& i : init)
                    append(statement, i);
                begin_loop();
                exit_unless(condition);
                append(statement, body);
                append(statement, step);
                put_op(code, opcode::Br);
                put_u32(code, 0u);
                end_loop();
            }
            break;
        }
        push_operand(operand_type{ statement });
    }

    void compiler::finish_function()
    {
        using bytecode::opcode;
        auto& function = current_function();
        auto const& signature = function.scope->function_signature();
        auto& stack = state().operandStack;
        fragment body;
        for (auto i = function.operandBase; i < stack.size(); ++i)
        {
            if (!stack[i].holds_alternative<i_fragment>() || stack[i].get<i_fragment>().kind() != fragment_kind::Statement)
                throw std::runtime_error("unexpected expression in body of '" + function.name + "'");
            append(body, stack[i].get<i_fragment>());
        }
        stack.resize(function.operandBase);
        auto& code = body.fragmentCode.as_std_vector();
        if (signature.return_type() != type::Void)
            put_op(code, opcode::Unreachable);
        else if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.direction() != parameter_direction::In)
                {
                    put_op(code, opcode::LocalGet);
                    put_u32(code, find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() })->index());
                }
        function.body = code;
        function.relocations.clear();
        for (auto const& r : body.relocations())
            function.relocations.emplace_back(r.first(), r.second());
        function.signature = signature;
        function.compiled = true;
        state().function = std::nullopt;
    }

    void compiler::emit_module(program& aProgram)
    {
        using bytecode::opcode;
        // function index space: imports first
        std::vector<std::uint32_t> order;
        for (std::uint32_t id = 0u; id < iModule.functions.size(); ++id)
            if (iModule.functions[id].imported)
                order.push_back(id);
        std::uint32_t const importCount = static_cast<std::uint32_t>(order.size());
        for (std::uint32_t id = 0u; id < iModule.functions.size(); ++id)
            if (!iModule.functions[id].imported)
            {
                if (!iModule.functions[id].compiled)
                    throw std::runtime_error("function '" + iModule.functions[id].name + "' has no body");
                order.push_back(id);
            }
        std::vector<std::uint32_t> finalIndex(iModule.functions.size());
        for (std::uint32_t i = 0u; i < order.size(); ++i)
            finalIndex[order[i]] = i;
        // types
        std::vector<bytes> types;
        std::vector<std::uint32_t> typeOf(iModule.functions.size());
        for (auto id : order)
        {
            auto const& signature = iModule.functions[id].signature;
            bytes functionType;
            functionType.push_back(0x60u);
            bytes parameters, results;
            std::uint32_t parameterCount = 0u, resultCount = 0u;
            if (auto const r = wasm_type(signature.return_type()))
            {
                results.push_back(*r);
                ++resultCount;
            }
            if (signature.parameters().has_value())
                for (auto const& parameter : signature.parameters().value())
                {
                    if (parameter.direction() != parameter_direction::Out)
                    {
                        parameters.push_back(*wasm_type(parameter.parameter_type()));
                        ++parameterCount;
                    }
                    if (parameter.direction() != parameter_direction::In)
                    {
                        results.push_back(*wasm_type(parameter.parameter_type()));
                        ++resultCount;
                    }
                }
            put_u32(functionType, parameterCount);
            functionType.insert(functionType.end(), parameters.begin(), parameters.end());
            put_u32(functionType, resultCount);
            functionType.insert(functionType.end(), results.begin(), results.end());
            auto existing = std::find(types.begin(), types.end(), functionType);
            typeOf[id] = static_cast<std::uint32_t>(existing - types.begin());
            if (existing == types.end())
                types.push_back(functionType);
        }
        // entry point
        std::optional<std::uint32_t> entry;
        for (auto id : order)
            if (iModule.functions[id].entry && !iModule.functions[id].fromImportedFragment)
                entry = id;
        if (!entry)
            for (auto id : order)
                if (!iModule.functions[id].imported && iModule.functions[id].name == "main" && !iModule.functions[id].fromImportedFragment)
                    entry = id;
        if (!entry)
            for (auto id : order)
                if (iModule.functions[id].entry)
                    entry = id;
        std::uint32_t const heapBase = (DataBase + static_cast<std::uint32_t>(iModule.data.size()) + 7u) & ~7u;
        bytes module = { 0x00u, 0x61u, 0x73u, 0x6Du, 0x01u, 0x00u, 0x00u, 0x00u };
        {
            bytes section;
            put_u32(section, types.size());
            for (auto const& t : types)
                section.insert(section.end(), t.begin(), t.end());
            put_section(module, 1u, section);
        }
        if (importCount != 0u)
        {
            bytes section;
            put_u32(section, importCount);
            for (std::uint32_t i = 0u; i < importCount; ++i)
            {
                put_name(section, "env");
                put_name(section, iModule.functions[order[i]].name);
                section.push_back(0x00u);
                put_u32(section, typeOf[order[i]]);
            }
            put_section(module, 2u, section);
        }
        {
            bytes section;
            put_u32(section, order.size() - importCount);
            for (std::uint32_t i = importCount; i < order.size(); ++i)
                put_u32(section, typeOf[order[i]]);
            put_section(module, 3u, section);
        }
        {
            bytes section;
            put_u32(section, 1u);
            section.push_back(0x00u);
            put_u32(section, heapBase / 65536u + 1u);
            put_section(module, 5u, section);
        }
        {
            bytes section;
            put_u32(section, 1u);
            section.push_back(0x7Fu);
            section.push_back(0x00u);
            put_op(section, opcode::I32Const);
            put_s64(section, static_cast<std::int32_t>(heapBase));
            put_op(section, opcode::End);
            put_section(module, 6u, section);
        }
        {
            bytes section;
            put_u32(section, entry ? 3u : 2u);
            put_name(section, "memory");
            section.push_back(0x02u);
            put_u32(section, 0u);
            put_name(section, "__heap_base");
            section.push_back(0x03u);
            put_u32(section, 0u);
            if (entry)
            {
                put_name(section, "main");
                section.push_back(0x00u);
                put_u32(section, finalIndex[*entry]);
            }
            put_section(module, 7u, section);
        }
        {
            bytes section;
            put_u32(section, order.size() - importCount);
            for (std::uint32_t i = importCount; i < order.size(); ++i)
            {
                auto const& function = iModule.functions[order[i]];
                bytes body;
                std::vector<std::pair<std::uint32_t, std::uint8_t>> runs;
                for (auto const& local : function.locals)
                {
                    auto const t = *wasm_type(local);
                    if (!runs.empty() && runs.back().second == t)
                        ++runs.back().first;
                    else
                        runs.emplace_back(1u, t);
                }
                put_u32(body, runs.size());
                for (auto const& r : runs)
                {
                    put_u32(body, r.first);
                    body.push_back(r.second);
                }
                auto code = function.body;
                for (auto const& r : function.relocations)
                    put_u32_padded(code, r.first, finalIndex[r.second]);
                body.insert(body.end(), code.begin(), code.end());
                put_op(body, opcode::End);
                put_u32(section, body.size());
                section.insert(section.end(), body.begin(), body.end());
            }
            put_section(module, 10u, section);
        }
        if (!iModule.data.empty())
        {
            bytes section;
            put_u32(section, 1u);
            section.push_back(0x00u);
            put_op(section, opcode::I32Const);
            put_s64(section, static_cast<std::int32_t>(DataBase));
            put_op(section, opcode::End);
            put_u32(section, iModule.data.size());
            section.insert(section.end(), iModule.data.begin(), iModule.data.end());
            put_section(module, 11u, section);
        }
        aProgram.text.clear();
        for (auto b : module)
            aProgram.text.push_back(std::byte{ b });
        if (trace() >= 1)
            iContext.cout() << "Emitted WASM module: " << aProgram.text.size() << " bytes, " << order.size() << " function(s)" << std::endl;
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

        auto trace_out = [&](std::string const& op, i_ast_node const& lhs, std::optional<neolib::ref_ptr<i_ast_node>> const& rhs = {}, const std::optional<neolib::ref_ptr<i_ast_node>> result = {})
            {
                std::ostringstream traceOutput;
                if (trace() >= 1)
                {
                    if (rhs)
                        traceOutput << op << ": " << 
                            lhs.trace() << " <- " << (**rhs).trace() <<
                            (result ? " = " + (**result).trace() : "") <<
                            std::endl;
                    else
                        traceOutput << op << ": " << 
                            lhs.trace() << " <-|" <<
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
                    trace_out("Folding", lhs);
                    auto const result = lhs.fold(iContext);
                    trace_out("Folded", lhs, {}, result);
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
                        trace_out("Folding", rhs);
                        auto const result = rhs.fold(iContext);
                        trace_out("Folded", rhs, {}, result);
                        irhs = fold_stack().erase(irhs);
                        if (!result->is_empty())
                            irhs = fold_stack().insert(irhs, result);
                        didSome = true;
                    }
                    else if (unrelated && !(lhs.unstructured() && rhs.unstructured()))
                    {
                        if (trace() >= 3)
                            trace_out("Skipping", lhs, rhs);
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
                                ilhs = fold_stack().erase(ilhs);
                            didSome = true;
                        }
                    }
                    else if (lhs.can_fold(rhs))
                    {
                        trace_out(!unrelated ? "Folding" : "Folding (unstructured)", lhs, rhs);
                        auto const result = lhs.fold(iContext, rhs);
                        trace_out(!unrelated ? "Folded" : "Folded (unstructured)", lhs, rhs, result);
                        ilhs = fold_stack().erase(ilhs);
                        ilhs = fold_stack().erase(ilhs);
                        if (!result->is_empty())
                            ilhs = fold_stack().insert(ilhs, result);
                        didSome = true;
                    }
                    else if (rhs.can_fold(lhs))
                    {
                        trace_out(!unrelated ? "Folding" : "Folding (unstructured)", rhs, lhs);
                        auto const result = rhs.fold(iContext, lhs);
                        trace_out(!unrelated ? "Folded" : "Folded (unstructured)", rhs, lhs, result);
                        ilhs = fold_stack().erase(ilhs);
                        ilhs = fold_stack().erase(ilhs);
                        if (!result->is_empty())
                            ilhs = fold_stack().insert(ilhs, result);
                        didSome = true;
                    }
                    else if (lhs.has_parent())
                    {
                        if (lhs.can_fold(lhs.parent()))
                        {
                            trace_out("Folding", lhs, lhs.parent());
                            auto const result = lhs.fold(iContext, lhs.parent());
                            trace_out("Folded", lhs, lhs.parent(), result);
                            bool const insert = (!result->is_empty() && result != &lhs.parent());
                            ilhs = fold_stack().erase(ilhs);
                            if (insert)
                                ilhs = fold_stack().insert(ilhs, result);
                            didSome = true;
                        }
                        else if (lhs.parent().can_fold(lhs))
                        {
                            trace_out("Folding", lhs.parent(), lhs);
                            auto const result = lhs.parent().fold(iContext, lhs);
                            trace_out("Folded", lhs.parent(), lhs, result);
                            bool const insert = (!result->is_empty() && result != &lhs.parent());
                            ilhs = fold_stack().erase(ilhs);
                            if (insert)
                                ilhs = fold_stack().insert(ilhs, result);
                            didSome = true;
                        }
                        else
                            break;
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
        catch (concept_not_found const& ex)
        {
            throw_error(ex.sourcePos.value(), neolib::string{ ex.what() });
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
        std::string info = (aShowFragmentFilePath && aFragment.source_file_path() != std::nullopt ? aFragment.source_file_path()->to_std_string() : "") + 
            "(" + boost::lexical_cast<std::string>(line) + "," + boost::lexical_cast<std::string>(col) + ")";
        oss << info;
        return oss.str();
    }

    void compiler::throw_error(source_iterator aSourcePos, neolib::i_string const& aError, neolib::i_string const& aErrorType)
    {
        throw compiler_error((location(*state().unit, *state().fragment, aSourcePos) + ": " + aErrorType + ": " + aError).to_std_string());
    }
}
