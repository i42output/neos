/*
  compiler.cpp

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

// Lua: the syntax tree and its analysis (names: local variables, upvalues and globals; labels and gotos); code generation is in
// codegen.cpp
//
// A function's local variables are WebAssembly locals unless an inner function uses them: those are boxes (runtime cells) that
// their locals hold, which the closures of the inner functions refer to (their upvalues). A name that is not a local variable is
// a global (a field of the chunk's environment, _ENV).

#include <neolib/neolib.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"

namespace neos::compilers::lua
{
    bool is_statement(construct aKind)
    {
        switch (aKind)
        {
        case construct::Empty:
        case construct::Label:
        case construct::Break:
        case construct::Goto:
        case construct::Do:
        case construct::While:
        case construct::Repeat:
        case construct::If:
        case construct::NumericFor:
        case construct::GenericFor:
        case construct::FunctionStatement:
        case construct::LocalFunction:
        case construct::Local:
        case construct::Assign:
        case construct::CallStatement:
        case construct::Return:
            return true;
        default:
            return false;
        }
    }

    bool is_expression(construct aKind)
    {
        switch (aKind)
        {
        case construct::Identifier:
        case construct::Nil:
        case construct::True:
        case construct::False:
        case construct::Vararg:
        case construct::Number:
        case construct::String:
        case construct::Or:
        case construct::And:
        case construct::Less:
        case construct::Greater:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
        case construct::BitOr:
        case construct::BitXor:
        case construct::BitAnd:
        case construct::ShiftLeft:
        case construct::ShiftRight:
        case construct::Concat:
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::Divide:
        case construct::FloorDivide:
        case construct::Modulo:
        case construct::Not:
        case construct::Negate:
        case construct::Length:
        case construct::BitNot:
        case construct::Power:
        case construct::Parenthesized:
        case construct::Dot:
        case construct::Index:
        case construct::Method:
        case construct::Call:
        case construct::Function:
        case construct::Table:
            return true;
        default:
            return false;
        }
    }

    bool is_multiple(node const* aNode)
    {
        return aNode->is(construct::Call) || aNode->is(construct::Method) || aNode->is(construct::Vararg);
    }

    // --- the language compiler

    compiler::compiler() :
        language_compiler{ language::lua::Language }
    {
    }

    void compiler::begin_compilation()
    {
        // (a new program: a compilation that failed may not have ended its fragments)
        iDepth = 0;
        iNodes.clear();
        iStack.clear();
    }

    void compiler::set_options(neolib::i_string const& aOptions)
    {
        // (the kind of chunk: "" (the program) or "load" (a chunk that load compiles: its function is the program's result))
        std::string const options = aOptions.to_std_string();
        iLoad = options.starts_with("load");
    }

    bool compiler::compiles_at_run_time() const
    {
        return iCompilesAtRunTime;
    }

    void compiler::begin_fragment(i_source_fragment& aFragment)
    {
        if (iDepth++ == 0)
        {
            iSource = aFragment.source().to_std_string_view();
            iChunkName = "?";
            if (aFragment.source_file_path().has_value())
            {
                // (the chunk's name: its file's name, as Lua's standalone interpreter names it)
                auto const path = aFragment.source_file_path().value().to_std_string();
                auto const slash = path.find_last_of("/\\");
                iChunkName = slash == std::string::npos ? path : path.substr(slash + 1u);
            }
            iNodes.clear();
            iStack.clear();
        }
    }

    void compiler::end_fragment()
    {
        --iDepth;
    }

    bool compiler::frame_name(std::uint32_t aFunction, neolib::i_string& aName)
    {
        auto const existing = iFrameNames.find(aFunction);
        if (existing == iFrameNames.end())
            return false;
        aName = existing->second;
        return true;
    }

    void compiler::error(std::string_view aSource, std::string const& aMessage) const
    {
        throw syntax_error{ aSource, aMessage };
    }

    // --- the syntax tree

    node* compiler::make_node(construct aKind, std::string_view aSource)
    {
        iNodes.push_back(node{ aKind, aSource });
        return &iNodes.back();
    }

    void compiler::emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource)
    {
        build(static_cast<construct>(aConstruct), aSource.to_std_string_view());
    }

    void compiler::build(construct aKind, std::string_view aSource)
    {
        // a construct's children are the nodes on the stack whose source is within its source (in order); a postfix construct's
        // first child is the node before them (its operand), and its source begins with its operand's
        auto const within = [&](node const* aNode)
        {
            return aNode->source.data() >= aSource.data() && aNode->source.data() + aNode->source.size() <= aSource.data() + aSource.size();
        };
        std::size_t first = iStack.size();
        while (first > 0u && within(iStack[first - 1u]))
            --first;
        auto* result = make_node(aKind, aSource);
        if (language::lua::info(aKind).postfix)
        {
            if (first == 0u)
                error(aSource, "unexpected symbol near '" + std::string{ aSource.substr(0u, 16u) } + "'");
            --first;
            auto* operand = iStack[first];
            result->source = std::string_view{ operand->source.data(), static_cast<std::size_t>(aSource.data() + aSource.size() - operand->source.data()) };
        }
        result->children.assign(iStack.begin() + static_cast<std::ptrdiff_t>(first), iStack.end());
        for (auto* child : result->children)
            child->parent = result;
        iStack.resize(first);
        iStack.push_back(result);
        if (aKind == construct::Chunk)
        {
            iStack.clear();
            if (!declaration_pass())
            {
                if (std::getenv("NEOS_LUA_AST") != nullptr)
                    dump(result);
                generate(result);
            }
        }
    }

    void compiler::dump(node* aNode, int aDepth) const
    {
        std::string text{ aNode->source.substr(0u, 40u) };
        for (auto& c : text)
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
        std::cerr << std::string(static_cast<std::size_t>(aDepth) * 2u, ' ') << language::lua::info(aNode->kind).name << " '" << text <<
            (aNode->source.size() > 40u ? "...'" : "'") << std::endl;
        for (auto* child : aNode->children)
            dump(child, aDepth + 1);
    }

    std::size_t compiler::first_statement(node* aNode)
    {
        switch (aNode->kind)
        {
        case construct::Chunk:
        case construct::Do:
        case construct::Repeat:
        case construct::Else:
            return 0u;
        case construct::Function:
        case construct::While:
        case construct::If:
        case construct::ElseIf:
            return 1u;
        case construct::FunctionStatement:
        case construct::LocalFunction:
            return 2u;
        case construct::NumericFor:
        case construct::GenericFor:
            {
                std::size_t i = 0u;
                while (i < aNode->size() && aNode->child(i)->is(construct::Binding))
                    ++i;
                while (i < aNode->size() && is_expression(aNode->child(i)->kind))
                    ++i;
                return i;
            }
        default:
            return aNode->size();
        }
    }

    std::vector<node*> compiler::statements_of(node* aNode, std::size_t aFirst)
    {
        std::vector<node*> result;
        for (auto i = aFirst; i < aNode->size(); ++i)
        {
            auto* c = aNode->child(i);
            if (c->is(construct::ElseIf) || c->is(construct::Else))
                break;
            if (aNode->is(construct::Repeat) && i + 1u == aNode->size())
                break;  // (the condition)
            result.push_back(c);
        }
        return result;
    }

    // --- analysis

    function_info* compiler::new_function(node* aDeclaration, function_info* aParent)
    {
        iFunctions.push_back(function_info{});
        auto& f = iFunctions.back();
        f.declaration = aDeclaration;
        f.parent = aParent;
        f.line = source_line_of(aDeclaration->source);
        aDeclaration->function = &f;
        return &f;
    }

    variable* compiler::new_variable(std::string const& aName, function_info* aFunction, node* aDeclaration, attribute aAttribute)
    {
        iVariables.push_back(variable{});
        auto& v = iVariables.back();
        v.name = aName;
        v.function = aFunction;
        v.declaration = aDeclaration;
        v.attrib = aAttribute;
        if (aDeclaration != nullptr)
            aDeclaration->declared = &v;
        return &v;
    }

    void compiler::declare(variable* aVariable)
    {
        iScopes.back().names.emplace_back(aVariable->name, aVariable);
    }

    variable* compiler::find(std::string_view aName) const
    {
        for (auto s = iScopes.rbegin(); s != iScopes.rend(); ++s)
            for (auto n = s->names.rbegin(); n != s->names.rend(); ++n)
                if (n->first == aName)
                    return n->second;
        return nullptr;
    }

    void compiler::use(variable* aVariable)
    {
        if (aVariable->function != iFunctionStack.back())
        {
            aVariable->captured = true;
            upvalue_index(*iFunctionStack.back(), aVariable);
        }
    }

    std::uint32_t compiler::upvalue_index(function_info& aFunction, variable* aVariable)
    {
        // (a variable of a function that encloses the function: its upvalue (and so an upvalue of each function between them))
        auto const existing = aFunction.upvalueIndex.find(aVariable);
        if (existing != aFunction.upvalueIndex.end())
            return existing->second;
        function_info::upvalue u{ aVariable, aFunction.parent == aVariable->function, 0u };
        if (!u.parentLocal)
            u.parentIndex = upvalue_index(*aFunction.parent, aVariable);
        auto const index = static_cast<std::uint32_t>(aFunction.upvalues.size());
        aFunction.upvalues.push_back(u);
        aFunction.upvalueIndex.emplace(aVariable, index);
        return index;
    }

    void compiler::analyse(node* aChunk)
    {
        iFunctions.clear();
        iVariables.clear();
        iLabelInfos.clear();
        iScopes.clear();
        iFunctionStack.clear();
        iCompilesAtRunTime = false;
        auto* main = new_function(aChunk, nullptr);
        main->chunk = true;
        main->vararg = true;
        main->name = "main chunk";
        main->line = 0u;
        analyse_function(aChunk, main, nullptr, statements_of(aChunk, 0u));
    }

    void compiler::analyse_function(node* aDeclaration, function_info* aFunction, node* aParameters, std::vector<node*> const& aBody)
    {
        iFunctionStack.push_back(aFunction);
        iScopes.push_back(name_scope{});
        iScopes.back().function = aFunction;
        if (aFunction->method)
        {
            auto* self = new_variable("self", aFunction, nullptr);
            self->parameter = true;
            aFunction->parameters.push_back(self);
            declare(self);
        }
        if (aParameters != nullptr)
            for (auto* p : aParameters->children)
            {
                if (p->is(construct::Vararg))
                {
                    aFunction->vararg = true;
                    continue;
                }
                auto* v = new_variable(p->text(), aFunction, p);
                v->parameter = true;
                aFunction->parameters.push_back(v);
                declare(v);
            }
        analyse_block(aDeclaration, aBody);
        iScopes.pop_back();
        iFunctionStack.pop_back();
    }

    void compiler::analyse_block(node* aOwner, std::vector<node*> const& aStatements, std::size_t, std::vector<variable*> const& aDeclare,
        std::function<void()> const& aEpilogue)
    {
        iScopes.push_back(name_scope{});
        iScopes.back().function = iFunctionStack.back();
        for (auto* v : aDeclare)
            declare(v);
        // (the block's labels: visible in all of it (and the blocks within it, but not the functions within it))
        for (std::size_t i = 0u; i < aStatements.size(); ++i)
        {
            auto* s = aStatements[i];
            if (!s->is(construct::Label))
                continue;
            auto const name = s->child(0u)->text();
            for (auto scope = iScopes.rbegin(); scope != iScopes.rend() && scope->function == iFunctionStack.back(); ++scope)
                for (auto* existing : scope->labels)
                    if (existing->name == name)
                        error(s->source, "label '" + name + "' already defined on line " + std::to_string(source_line_of(existing->statement->source)));
            iLabelInfos.push_back(label_info{});
            auto& l = iLabelInfos.back();
            l.name = name;
            l.statement = s;
            l.owner = aOwner;
            l.position = i;
            s->label = &l;
            iScopes.back().labels.push_back(&l);
        }
        for (auto* s : aStatements)
            analyse_statement(s);
        if (aEpilogue)
            aEpilogue();
        resolve_gotos(aOwner, aStatements, 0u);
        iScopes.pop_back();
    }

    void compiler::resolve_gotos(node* aOwner, std::vector<node*> const& aStatements, std::size_t)
    {
        // (the gotos that jump to the block's labels: forward (from before a label) or backward; a forward goto may not jump into
        // the scope of a local variable (unless its label is at the end of the block: only void statements follow it))
        for (auto* l : iScopes.back().labels)
        {
            std::function<void(node*, std::size_t)> visit = [&](node* aNode, std::size_t aPosition)
            {
                if (aNode->is(construct::Goto) && aNode->label == l)
                {
                    if (aPosition < l->position)
                    {
                        if (!l->forward || aPosition < l->open)
                            l->open = aPosition;
                        l->forward = true;
                        bool atEnd = true;
                        for (auto j = l->position + 1u; j < aStatements.size(); ++j)
                            if (!aStatements[j]->is(construct::Label) && !aStatements[j]->is(construct::Empty))
                                atEnd = false;
                        if (!atEnd)
                            for (auto j = aPosition + 1u; j < l->position; ++j)
                                if (aStatements[j]->is(construct::Local) || aStatements[j]->is(construct::LocalFunction))
                                {
                                    auto const name = aStatements[j]->is(construct::Local) ? aStatements[j]->child(0u)->child(0u)->text() : aStatements[j]->child(0u)->text();
                                    error(aNode->source, "<goto " + l->name + "> at line " + std::to_string(source_line_of(aNode->source)) +
                                        " jumps into the scope of local '" + name + "'");
                                }
                    }
                    else
                        l->backward = true;
                }
                if (aNode->is(construct::Function) || aNode->is(construct::FunctionStatement) || aNode->is(construct::LocalFunction))
                    return;
                for (auto* c : aNode->children)
                    visit(c, aPosition);
            };
            for (std::size_t i = 0u; i < aStatements.size(); ++i)
                visit(aStatements[i], i);
        }
        (void)aOwner;
    }

    void compiler::analyse_statement(node* aNode)
    {
        auto const body = [&](node* aOwner, std::vector<variable*> const& aDeclare = {}, std::function<void()> const& aEpilogue = {})
        {
            auto const first = first_statement(aOwner);
            analyse_block(aOwner, statements_of(aOwner, first), first, aDeclare, aEpilogue);
        };
        switch (aNode->kind)
        {
        case construct::Empty:
        case construct::Label:
        case construct::Break:
            break;
        case construct::Goto:
            {
                auto const name = aNode->child(0u)->text();
                for (auto scope = iScopes.rbegin(); scope != iScopes.rend() && scope->function == iFunctionStack.back() && aNode->label == nullptr; ++scope)
                    for (auto* l : scope->labels)
                        if (l->name == name)
                        {
                            aNode->label = l;
                            break;
                        }
                if (aNode->label == nullptr)
                    error(aNode->source, "no visible label '" + name + "' for <goto> at line " + std::to_string(source_line_of(aNode->source)));
            }
            break;
        case construct::Do:
            body(aNode);
            break;
        case construct::While:
            analyse_expression(aNode->child(0u));
            aNode->loopBody = true;
            body(aNode);
            break;
        case construct::Repeat:
            // (the condition is in the scope of the body's locals)
            aNode->loopBody = true;
            body(aNode, {}, [&]() { analyse_expression(aNode->children.back()); });
            break;
        case construct::If:
            analyse_expression(aNode->child(0u));
            body(aNode);
            for (auto* c : aNode->children)
                if (c->is(construct::ElseIf))
                {
                    analyse_expression(c->child(0u));
                    body(c);
                }
                else if (c->is(construct::Else))
                    body(c);
            break;
        case construct::NumericFor:
            {
                auto const first = first_statement(aNode);
                for (std::size_t i = 1u; i < first; ++i)
                    analyse_expression(aNode->child(i));
                auto* v = new_variable(aNode->child(0u)->text(), iFunctionStack.back(), aNode->child(0u));
                aNode->loopBody = true;
                body(aNode, { v });
            }
            break;
        case construct::GenericFor:
            {
                auto const first = first_statement(aNode);
                std::vector<variable*> variables;
                for (std::size_t i = 0u; i < first; ++i)
                {
                    auto* c = aNode->child(i);
                    if (c->is(construct::Binding))
                        variables.push_back(new_variable(c->text(), iFunctionStack.back(), c));
                    else
                        analyse_expression(c);
                }
                // (a closing value (to-be-closed): the explist may have four values)
                std::size_t expressions = 0u;
                for (std::size_t i = 0u; i < first; ++i)
                    if (!aNode->child(i)->is(construct::Binding))
                        ++expressions;
                aNode->closing = expressions >= 4u || (expressions > 0u && is_multiple(aNode->child(first - 1u)));
                if (aNode->closing)
                    iFunctionStack.back()->hasClose = true;
                aNode->loopBody = true;
                body(aNode, variables);
            }
            break;
        case construct::FunctionStatement:
            {
                auto* name = aNode->child(0u);
                analyse_expression(name->child(0u));
                auto* f = new_function(aNode, iFunctionStack.back());
                f->method = name->children.back()->is(construct::MethodName);
                std::string text;
                for (auto* part : name->children)
                {
                    if (!text.empty())
                        text += part->is(construct::MethodName) ? ":" : ".";
                    text += part->text();
                }
                f->name = f->method ? "method '" + name->children.back()->text() + "'" : name->size() > 1u ? "field '" + name->children.back()->text() + "'" :
                    "function '" + text + "'";
                analyse_function(aNode, f, aNode->child(1u), statements_of(aNode, 2u));
            }
            break;
        case construct::LocalFunction:
            {
                // (the function's name is in scope in its body)
                auto* v = new_variable(aNode->child(0u)->text(), iFunctionStack.back(), aNode->child(0u));
                declare(v);
                auto* f = new_function(aNode, iFunctionStack.back());
                f->name = "local '" + v->name + "'";
                analyse_function(aNode, f, aNode->child(1u), statements_of(aNode, 2u));
            }
            break;
        case construct::Local:
            {
                std::vector<variable*> variables;
                for (auto* c : aNode->children)
                {
                    if (c->is(construct::AttributeName))
                    {
                        auto a = attribute::None;
                        if (c->size() > 1u)
                        {
                            auto const name = c->child(1u)->text();
                            if (name == "const")
                                a = attribute::Const;
                            else if (name == "close")
                                a = attribute::Close;
                            else
                                error(c->child(1u)->source, "unknown attribute '" + name + "'");
                        }
                        variables.push_back(new_variable(c->child(0u)->text(), iFunctionStack.back(), c->child(0u), a));
                    }
                    else
                    {
                        // (an anonymous function assigned to a local is named for it)
                        if (c->is(construct::Function) && variables.size() > 0u)
                        {
                            auto const index = static_cast<std::size_t>(std::count_if(aNode->children.begin(), std::find(aNode->children.begin(), aNode->children.end(), c),
                                [](node* n) { return !n->is(construct::AttributeName); }));
                            if (index < variables.size())
                            {
                                analyse_expression(c);
                                c->function->name = "local '" + variables[index]->name + "'";
                                continue;
                            }
                        }
                        analyse_expression(c);
                    }
                }
                std::size_t closes = 0u;
                for (auto* v : variables)
                {
                    if (v->attrib == attribute::Close)
                    {
                        ++closes;
                        iFunctionStack.back()->hasClose = true;
                    }
                    declare(v);
                }
                if (closes > 1u)
                    error(aNode->source, "multiple to-be-closed variables in local list");
            }
            break;
        case construct::Assign:
            for (auto* target : aNode->child(0u)->children)
            {
                if (!target->is(construct::Identifier) && !target->is(construct::Dot) && !target->is(construct::Index))
                    error(target->source, "syntax error near '='");
                analyse_expression(target);
                if (target->is(construct::Identifier) && target->resolved != nullptr && target->resolved->attrib != attribute::None)
                    error(target->source, "attempt to assign to const variable '" + target->resolved->name + "'");
            }
            for (std::size_t i = 1u; i < aNode->size(); ++i)
            {
                analyse_expression(aNode->child(i));
                if (aNode->child(i)->is(construct::Function) && i - 1u < aNode->child(0u)->size())
                {
                    auto* target = aNode->child(0u)->child(i - 1u);
                    if (target->is(construct::Identifier))
                        aNode->child(i)->function->name = (target->resolved != nullptr ? "local '" : "function '") + target->text() + "'";
                    else if (target->is(construct::Dot))
                        aNode->child(i)->function->name = "field '" + target->child(1u)->text() + "'";
                }
            }
            break;
        case construct::CallStatement:
            if (!aNode->child(0u)->is(construct::Call) && !aNode->child(0u)->is(construct::Method))
            {
                // (an expression that is not a call: the token that follows it)
                auto const end = static_cast<std::size_t>(aNode->source.data() + aNode->source.size() - iSource.data());
                auto next = end;
                while (next < iSource.size() && std::isspace(static_cast<unsigned char>(iSource[next])))
                    ++next;
                auto last = next;
                if (last < iSource.size() && (std::isalnum(static_cast<unsigned char>(iSource[last])) || iSource[last] == '_'))
                    while (last < iSource.size() && (std::isalnum(static_cast<unsigned char>(iSource[last])) || iSource[last] == '_'))
                        ++last;
                else if (last < iSource.size())
                    ++last;
                auto const token = next < iSource.size() ? "'" + std::string{ iSource.substr(next, last - next) } + "'" : std::string{ "<eof>" };
                error(next < iSource.size() ? iSource.substr(next, last - next) : aNode->source, "syntax error near " + token);
            }
            analyse_expression(aNode->child(0u));
            break;
        case construct::Return:
            for (auto* c : aNode->children)
                analyse_expression(c);
            break;
        default:
            error(aNode->source, "unexpected construct");
        }
    }

    void compiler::analyse_expression(node* aNode)
    {
        switch (aNode->kind)
        {
        case construct::Identifier:
            {
                auto const name = aNode->text();
                aNode->resolved = find(name);
                if (aNode->resolved != nullptr)
                    use(aNode->resolved);
                else if (auto* environment = find("_ENV"))
                {
                    // (a global in the scope of a local variable named _ENV: a field of it)
                    aNode->environment = environment;
                    use(environment);
                }
                if (aNode->resolved == nullptr && (name == "load" || name == "loadstring" || name == "dofile" || name == "require" || name == "loadfile"))
                    iCompilesAtRunTime = true;
            }
            break;
        case construct::Vararg:
            if (!iFunctionStack.back()->vararg)
                error(aNode->source, "cannot use '...' outside a vararg function near '...'");
            break;
        case construct::Function:
            {
                auto* f = new_function(aNode, iFunctionStack.back());
                f->name = "function <" + iChunkName + ":" + std::to_string(f->line) + ">";
                analyse_function(aNode, f, aNode->child(0u), statements_of(aNode, 1u));
            }
            break;
        case construct::Table:
            for (auto* c : aNode->children)
            {
                if (c->is(construct::FieldNamed))
                {
                    analyse_expression(c->child(1u));
                    if (c->child(1u)->is(construct::Function))
                        c->child(1u)->function->name = "field '" + c->child(0u)->text() + "'";
                }
                else if (c->is(construct::FieldKeyed))
                {
                    analyse_expression(c->child(0u));
                    analyse_expression(c->child(1u));
                }
                else
                    analyse_expression(c);
            }
            break;
        case construct::Dot:
            analyse_expression(aNode->child(0u));
            break;
        case construct::Method:
            analyse_expression(aNode->child(0u));
            for (std::size_t i = 2u; i < aNode->size(); ++i)
                analyse_expression(aNode->child(i));
            break;
        default:
            for (auto* c : aNode->children)
                if (!c->is(construct::Name) && !c->is(construct::Binding))
                    analyse_expression(c);
            break;
        }
    }

    // --- code generation (support)

    std::uint32_t compiler::global_index(std::string const& aName, type aType)
    {
        // (the generator's globals, by name: those added since the index was last brought up to date are indexed first)
        auto& g = generator();
        if (g.global_count() < iGlobalIndexed)
        {
            iGlobalIndex.clear();
            iGlobalIndexed = 0u;
        }
        for (; iGlobalIndexed < g.global_count(); ++iGlobalIndexed)
            iGlobalIndex.emplace(g.global_name(iGlobalIndexed).to_std_string(), static_cast<std::uint32_t>(iGlobalIndexed + 1u));
        if (auto const existing = iGlobalIndex.find(aName); existing != iGlobalIndex.end())
            return existing->second;
        g.add_global(neolib::string{ aName }, aType);
        return static_cast<std::uint32_t>(g.global_count());
    }

    std::uint32_t compiler::literal_string(std::string const& aText)
    {
        auto const existing = iLiterals.find(aText);
        if (existing != iLiterals.end())
            return existing->second;
        auto const g = global_index("%lua" + std::to_string(iLiterals.size()), type::I64);
        iLiterals.emplace(aText, g);
        iLiteralOrder.emplace_back(aText, g);
        return g;
    }

    std::uint32_t compiler::source_line_of(std::string_view aSource) const
    {
        if (aSource.data() < iSource.data() || aSource.data() > iSource.data() + iSource.size())
            return 1u;
        return static_cast<std::uint32_t>(std::count(iSource.data(), aSource.data(), '\n') + 1);
    }
}
