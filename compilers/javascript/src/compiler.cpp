// JavaScript (ECMAScript): the syntax tree and its analysis (scopes, bindings and closures); code generation is in
// codegen.cpp
//
// Each function's bindings are WebAssembly locals unless an inner function uses them (or they can be found by name):
// those are slots of the environment (a runtime object) of their scope, which is created when the scope is entered and is
// the closure of the functions created in it. A script's top-level var and function declarations are properties of the
// global object and its lexical declarations bindings of the global lexical environment (found by name at run time),
// except for those that no function uses (they are locals of the script's function when the program cannot find bindings
// by name, e.g. with eval).

#include <neolib/neolib.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"

namespace neos::compilers::javascript
{
    // --- the language compiler

    compiler::compiler() :
        language_compiler{ language::javascript::Language }
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
        // (the kind of code: "" (a script), "eval:<flags>" (a direct eval's code; flags (decimal): EvalStrict etc.), "eval-indirect",
        // "script-eval" (a script whose completion value is the result, e.g. $262.evalScript's) or "function:<parameters end>:<body begin>"
        // (the Function constructor's source))
        std::string const options = aOptions.to_std_string();
        iKind = code_kind::Script;
        iEvalFlags = 0u;
        iModuleMain = false;
        if (options.starts_with("eval:"))
        {
            iKind = code_kind::DirectEval;
            iEvalFlags = static_cast<std::uint32_t>(std::stoul(options.substr(5u)));
        }
        else if (options == "eval-indirect")
            iKind = code_kind::IndirectEval;
        else if (options == "script-eval")
            iKind = code_kind::EvaluateScript;
        else if (options == "module")
            iKind = code_kind::Module;
        else if (options.starts_with("function:"))
        {
            iKind = code_kind::Function;
            auto const colon = options.find(':', 9u);
            iFunctionParametersEnd = std::stoul(options.substr(9u, colon - 9u));
            iFunctionBodyBegin = std::stoul(options.substr(colon + 1u));
        }
    }

    bool compiler::sloppy_eval_var_scope(scope const* aScope) const
    {
        return aScope->kind == scope_kind::Function && aScope->function != nullptr && aScope->function->directEval && !aScope->function->strict &&
            aScope == aScope->function->top;
    }

    void compiler::begin_pass()
    {
    }

    void compiler::end_pass()
    {
    }

    void compiler::begin_fragment(i_source_fragment& aFragment)
    {
        if (iDepth++ == 0)
        {
            // (a program whose source is a .mjs file: a module)
            if (iKind == code_kind::Script && aFragment.source_file_path().has_value() && aFragment.source_file_path().value().to_std_string().ends_with(".mjs"))
            {
                iKind = code_kind::Module;
                iModuleMain = true;
            }
            iSource = aFragment.source().to_std_string_view();
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
        if (language::javascript::info(aKind).postfix)
        {
            if (first == 0u)
                error(aSource, "unexpected '" + std::string{ aSource.substr(0u, 16u) } + "'");
            --first;
            auto* operand = iStack[first];
            result->source = std::string_view{ operand->source.data(), static_cast<std::size_t>(aSource.data() + aSource.size() - operand->source.data()) };
        }
        result->children.assign(iStack.begin() + static_cast<std::ptrdiff_t>(first), iStack.end());
        for (auto* child : result->children)
            child->parent = result;
        iStack.resize(first);
        iStack.push_back(result);
        if (aKind == construct::Script && !declaration_pass())
        {
            if (std::getenv("NEOS_JS_AST") != nullptr)
                dump(result);
            iStack.clear();
            generate(result);
        }
        else if (aKind == construct::Script)
            iStack.clear();
    }

    void compiler::dump(node* aNode, int aDepth) const
    {
        std::string text{ aNode->source.substr(0u, 40u) };
        for (auto& c : text)
            if (c == '\n' || c == '\r')
                c = ' ';
        std::cerr << std::string(static_cast<std::size_t>(aDepth) * 2u, ' ') << language::javascript::info(aNode->kind).name << " [" << text <<
            (aNode->source.size() > 40u ? "..." : "") << "]" << std::endl;
        for (auto* child : aNode->children)
            dump(child, aDepth + 1);
    }

    // --- analysis

    namespace
    {
        bool is_function_node(node const* aNode)
        {
            switch (aNode->kind)
            {
            case construct::Function:
            case construct::AsyncFunction:
            case construct::FunctionExpression:
            case construct::AsyncFunctionExpression:
            case construct::Arrow:
            case construct::AsyncArrow:
            case construct::Method:
            case construct::Getter:
            case construct::Setter:
            case construct::GeneratorMethod:
            case construct::AsyncMethod:
            case construct::AsyncGeneratorMethod:
                return true;
            default:
                return false;
            }
        }

        bool is_class_node(node const* aNode)
        {
            return aNode->is(construct::Class) || aNode->is(construct::ClassExpression);
        }

        // (the text of a function's header (before its parameters): is it a generator?)
        bool generator_header(node const* aNode)
        {
            if (aNode->is(construct::GeneratorMethod) || aNode->is(construct::AsyncGeneratorMethod))
                return true;
            if (!aNode->is(construct::Function) && !aNode->is(construct::AsyncFunction) && !aNode->is(construct::FunctionExpression) &&
                !aNode->is(construct::AsyncFunctionExpression))
                return false;
            node const* parameters = nullptr;
            for (auto* c : aNode->children)
                if (c->is(construct::Parameters))
                {
                    parameters = c;
                    break;
                }
            if (parameters == nullptr)
                return false;
            auto const header = aNode->source.substr(0u, static_cast<std::size_t>(parameters->source.data() - aNode->source.data()));
            auto const keyword = header.find("function");
            return keyword != std::string_view::npos && header.find('*', keyword + 8u) != std::string_view::npos;
        }

        std::string identifier_text(node const* aNode)
        {
            // (an identifier: its unicode escapes are its characters)
            std::string const text{ aNode->source };
            if (text.find('\\') == std::string::npos)
                return text;
            std::u32string result;
            for (std::size_t i = 0u; i < text.size();)
            {
                if (text[i] == '\\' && i + 1u < text.size() && text[i + 1u] == 'u')
                {
                    std::size_t j = i + 2u;
                    char32_t cp = 0u;
                    if (j < text.size() && text[j] == '{')
                    {
                        ++j;
                        while (j < text.size() && text[j] != '}')
                            cp = cp * 16u + static_cast<char32_t>(std::isdigit(static_cast<unsigned char>(text[j])) ? text[j] - '0' : (std::tolower(text[j]) - 'a' + 10)), ++j;
                        ++j;
                    }
                    else
                        for (std::size_t k = 0u; k < 4u && j < text.size(); ++k, ++j)
                            cp = cp * 16u + static_cast<char32_t>(std::isdigit(static_cast<unsigned char>(text[j])) ? text[j] - '0' : (std::tolower(text[j]) - 'a' + 10));
                    result.push_back(cp);
                    i = j;
                }
                else
                {
                    // (UTF-8)
                    auto const c = static_cast<unsigned char>(text[i]);
                    std::size_t n = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                    char32_t cp = n == 1u ? c : n == 2u ? (c & 0x1Fu) : n == 3u ? (c & 0x0Fu) : (c & 0x07u);
                    for (std::size_t k = 1u; k < n && i + k < text.size(); ++k)
                        cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
                    result.push_back(cp);
                    i += n;
                }
            }
            std::string utf8;
            for (auto cp : result)
            {
                if (cp < 0x80u)
                    utf8.push_back(static_cast<char>(cp));
                else if (cp < 0x800u)
                {
                    utf8.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                    utf8.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
                else if (cp < 0x10000u)
                {
                    utf8.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                    utf8.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    utf8.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
                else
                {
                    utf8.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                    utf8.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                    utf8.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    utf8.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
            }
            return utf8;
        }
    }

    std::string name_of(node const* aNode)
    {
        return identifier_text(aNode);
    }

    function_info* compiler::new_function(node* aDeclaration, function_info* aParent)
    {
        iFunctions.emplace_back();
        auto* f = &iFunctions.back();
        f->declaration = aDeclaration;
        f->parent = aParent;
        if (aParent != nullptr)
            aParent->functions.push_back(f);
        if (aDeclaration != nullptr)
            aDeclaration->function = f;
        return f;
    }

    scope* compiler::new_scope(scope_kind aKind, scope* aParent, node* aOwner, function_info* aFunction)
    {
        iScopes.emplace_back();
        auto* s = &iScopes.back();
        s->kind = aKind;
        s->parent = aParent;
        s->owner = aOwner;
        s->function = aFunction;
        if (aOwner != nullptr && aOwner->ownScope == nullptr)
            aOwner->ownScope = s;
        return s;
    }

    binding* compiler::declare(scope* aScope, std::string const& aName, binding_kind aKind, node* aDeclaration)
    {
        if (auto* existing = aScope->find(aName))
        {
            // (a module's top-level function declarations and imports are lexical declarations)
            bool const moduleTop = iModule && aScope->kind == scope_kind::Script;
            bool const existingLexical = existing->lexical() ||
                (moduleTop && (existing->kind == binding_kind::Function || existing->kind == binding_kind::Import));
            bool const newLexical = (aKind == binding_kind::Let || aKind == binding_kind::Const || aKind == binding_kind::Class) ||
                (moduleTop && (aKind == binding_kind::Function || aKind == binding_kind::Import));
            if ((existingLexical || newLexical) && !(moduleTop && existing->declaration == aDeclaration && aDeclaration != nullptr))
            {
                // (a function declaration in a block can be redeclared by another in sloppy mode (Annex B))
                if (!(aScope->kind != scope_kind::Function && !aScope->function->strict && existing->kind == binding_kind::Function && aKind == binding_kind::Function))
                    error(aDeclaration != nullptr ? aDeclaration->source : iSource, "Identifier '" + aName + "' has already been declared");
            }
            if (aKind == binding_kind::Function && existing->kind != binding_kind::Parameter)
                existing->kind = binding_kind::Function;
            if (aKind == binding_kind::Function)
                existing->declaration = aDeclaration;
            return existing;
        }
        aScope->bindings.push_back(std::make_unique<binding>());
        auto* b = aScope->bindings.back().get();
        b->name = aName;
        b->kind = aKind;
        b->owner = aScope;
        b->declaration = aDeclaration;
        b->tdz = (aKind == binding_kind::Let || aKind == binding_kind::Const || aKind == binding_kind::Class);
        aScope->names.emplace(aName, b);
        return b;
    }

    void compiler::declare_pattern(scope* aScope, node* aTarget, binding_kind aKind)
    {
        switch (aTarget->kind)
        {
        case construct::Binding:
            {
                auto const name = name_of(aTarget);
                if (aScope->function->strict && (name == "eval" || name == "arguments"))
                    error(aTarget->source, "Unexpected eval or arguments in strict mode");
                if ((aKind == binding_kind::Let || aKind == binding_kind::Const || aKind == binding_kind::Class) && name == "let")
                    error(aTarget->source, "let is disallowed as a lexically bound name");
                declare(aScope, name, aKind, aTarget);
            }
            break;
        case construct::Initializer:
            declare_pattern(aScope, aTarget->child(0u), aKind);
            break;
        case construct::Rest:
            declare_pattern(aScope, aTarget->child(0u), aKind);
            break;
        case construct::ObjectPattern:
        case construct::ArrayPattern:
            for (auto* c : aTarget->children)
                declare_pattern(aScope, c, aKind);
            break;
        case construct::PatternProperty:
            declare_pattern(aScope, aTarget->child(1u), aKind);
            break;
        case construct::Hole:
            break;
        default:
            break;
        }
    }

    void compiler::diagnose(i_source_fragment const&, std::uint32_t&, std::uint32_t&, std::uint32_t&, std::uint32_t&, neolib::i_string& aType,
        neolib::i_string& aMessage, neolib::i_string&)
    {
        // (a source that does not parse: ECMAScript's SyntaxError)
        std::string const type = aType.to_std_string();
        if (type == "syntax error" || type == "error")
            aType = neolib::string{ "SyntaxError" };
        std::string message = aMessage.to_std_string();
        for (auto const prefix : { "failed to fold semantic concepts: ", "failed to fold semantic concept: " })
            if (message.starts_with(prefix))
                aMessage = neolib::string{ message.substr(std::char_traits<char>::length(prefix)) };
    }

    bool compiler::directive_strict(node* aStatements) const
    {
        // (a directive prologue: the string literal expression statements that begin the code)
        for (auto* s : aStatements->children)
        {
            if (!s->is(construct::ExpressionStatement) || s->size() != 1u || !s->child(0u)->is(construct::String))
                return false;
            auto const text = s->child(0u)->source;
            if (text == "\"use strict\"" || text == "'use strict'")
                return true;
        }
        return false;
    }

    void compiler::analyse(node* aScript)
    {
        iAnnexBFunctions.clear();
        if (iKind == code_kind::Function)
        {
            // (the Function constructor's source ("(function anonymous(<parameters>\n) {\n<body>\n})"): its parameters and body must be
            // what they are (not, e.g., a body that ends the function early))
            auto const invalid = [&]() { error(aScript->source, "Arguments of the Function constructor are not a valid function"); };
            if (aScript->size() != 1u || !aScript->child(0u)->is(construct::ExpressionStatement))
                invalid();
            auto* e = aScript->child(0u)->child(0u);
            while (e != nullptr && e->is(construct::Parenthesized) && e->size() == 1u)
                e = e->child(0u);
            if (e == nullptr || !(e->is(construct::FunctionExpression) || e->is(construct::AsyncFunctionExpression)))
                invalid();
            auto const offset = [&](std::string_view aText) { return static_cast<std::size_t>(aText.data() - iSource.data()); };
            if (offset(e->source) != 1u)
                invalid();
            {
                // (what follows it: the closing parenthesis (and the white space, if any, that the source was given with))
                auto rest = iSource.substr(offset(e->source) + e->source.size());
                while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r' || rest.back() == ' ' || rest.back() == '\0'))
                    rest.remove_suffix(1u);
                if (rest != ")")
                    invalid();
            }
            for (auto* c : e->children)
            {
                if (c->is(construct::Parameters) && offset(c->source) + c->source.size() > iFunctionParametersEnd)
                    invalid();
                if (c->is(construct::Body) && offset(c->source) != iFunctionBodyBegin)
                    invalid();
            }
        }
        // (await operators that parse as calls of a function named await (e.g. await (x), await `x`))
        await_operators(aScript, iKind == code_kind::Module);
        check_early_errors(aScript);
        yield_identifiers(aScript, false);
        array_commas(aScript);
        iFunctions.clear();
        iScopes.clear();
        auto* script = new_function(nullptr, nullptr);
        script->script = true;
        script->name = "<script>";
        iModule = iKind == code_kind::Module;
        script->strict = directive_strict(aScript) || (iKind == code_kind::DirectEval && (iEvalFlags & EvalStrict) != 0u) || iModule;
        script->top = new_scope(scope_kind::Script, nullptr, aScript, script);
        script->body = script->top;
        if (iModule)
        {
            // (a module: its bindings are those of its environment (found by name: its exports and imports refer to them); its imports)
            script->top->dynamic = true;
            auto const import_binding = [&](node* aBinding)
            {
                auto const name = name_of(aBinding);
                if (name == "eval" || name == "arguments")
                    error(aBinding->source, "Unexpected eval or arguments in strict mode");
                declare(script->top, name, binding_kind::Import, aBinding);
            };
            for (auto* item : aScript->children)
                if (item->is(construct::Import))
                    for (auto* c : item->children)
                    {
                        if (c->is(construct::ImportDefault) || c->is(construct::ImportNamespace))
                            import_binding(c->child(0u));
                        else if (c->is(construct::ImportNamed))
                            for (auto* specifier : c->children)
                                import_binding(specifier->children.back());
                    }
                else if (item->is(construct::Export) && item->size() > 0u && (item->child(0u)->is(construct::ExportDefault) ||
                    item->child(0u)->is(construct::ClassExpression) || item->child(0u)->is(construct::FunctionExpression) ||
                    item->child(0u)->is(construct::AsyncFunctionExpression)))
                    declare(script->top, "*default*", binding_kind::Let, item->child(0u));
        }
        // (declarations, functions and scopes)
        for (auto* statement : aScript->children)
            hoist_var(statement, script->top, script);
        if (iKind == code_kind::DirectEval && (iEvalFlags & EvalArgumentsObject) != 0u && !script->strict)
            if (auto* b = script->top->find("arguments"); b != nullptr && (b->kind == binding_kind::Var || b->kind == binding_kind::Function))
                error(b->declaration != nullptr ? b->declaration->source : aScript->source,
                    "A direct eval in a function's parameters cannot declare a var named arguments");
        for (auto* statement : aScript->children)
            collect(statement, script->top, script);
        // (Annex B.3.3: block-level functions' var bindings, unless a var binding would be an error (a lexical declaration of the name
        // between the block and the function's top level) or the name is a parameter's)
        for (auto const& [declaration, blockScope, function] : iAnnexBFunctions)
        {
            if (function->strict)
                continue;
            auto const name = name_of(declaration->child(0u));
            if (name == "arguments" && !function->script)
                continue;
            bool conflict = false;
            for (auto* s = blockScope->parent; s != nullptr; s = s->parent)
            {
                if (auto* b = s->find(name); b != nullptr && (b->lexical() || b->kind == binding_kind::Parameter ||
                    (b->kind == binding_kind::Catch && false)))
                    conflict = true;
                if (s == function->top)
                    break;
            }
            if (auto* b = function->top->find(name); b != nullptr && (b->lexical() || b->kind == binding_kind::Parameter))
                conflict = true;
            if (conflict)
                continue;
            if (function->top->find(name) == nullptr)
                declare(function->top, name, binding_kind::Var, nullptr);
            declaration->annexB = true;
        }
        iAnnexBFunctions.clear();
        // (eval code: new.target, super.x and super() only where the code that calls eval may use them)
        if (eval_code())
            for (auto* statement : aScript->children)
                eval_context_errors(statement);
        // (references: captured bindings)
        for (auto* statement : aScript->children)
            resolve_uses(statement, script->top, script);
        analyse_function(*script);
    }

    void compiler::eval_context_errors(node* aNode)
    {
        if (aNode->function != nullptr && !aNode->function->arrow)
            return;     // (a function of its own (an arrow function's are its enclosing code's))
        bool const direct = iKind == code_kind::DirectEval;
        switch (aNode->kind)
        {
        case construct::NewTarget:
            if (!direct || (iEvalFlags & EvalFunction) == 0u)
                error(aNode->source, "new.target expression is not allowed here");
            break;
        case construct::SuperDot:
        case construct::SuperIndex:
            if (!direct || (iEvalFlags & EvalMethod) == 0u)
                error(aNode->source, "'super' keyword unexpected here");
            break;
        case construct::SuperCall:
            if (!direct || (iEvalFlags & EvalDerivedConstructor) == 0u)
                error(aNode->source, "'super' keyword unexpected here");
            break;
        default:
            break;
        }
        for (auto* c : aNode->children)
            eval_context_errors(c);
    }

    void compiler::yield_identifiers(node* aNode, bool aGenerator)
    {
        // (outside generators (sloppy code), yield is an identifier: "yield" and "yield * x" parse as yield expressions)
        if (aNode->function != nullptr || aNode->is(construct::Function) || aNode->is(construct::FunctionExpression) ||
            aNode->is(construct::AsyncFunction) || aNode->is(construct::AsyncFunctionExpression) || aNode->is(construct::Method) ||
            aNode->is(construct::GeneratorMethod) || aNode->is(construct::AsyncMethod) || aNode->is(construct::AsyncGeneratorMethod) ||
            aNode->is(construct::Getter) || aNode->is(construct::Setter))
            aGenerator = generator_header(aNode);
        for (std::size_t i = 0u; i < aNode->size(); ++i)
        {
            auto* c = aNode->child(i);
            if (!aGenerator && c->is(construct::Yield))
            {
                auto const keyword = c->source.substr(0u, 5u);
                auto* identifier = make_node(construct::Identifier, keyword);
                if (c->size() == 0u)
                {
                    identifier->parent = aNode;
                    aNode->children[i] = identifier;
                    continue;
                }
                auto rest = c->source.substr(5u);
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == '\n' || rest.front() == '\r'))
                    rest.remove_prefix(1u);
                if (!rest.starts_with("*"))
                    continue;
                // (yield * x ...: the multiplication's right operand is the leftmost operand of the expression that follows)
                auto const binary = [](node const* n)
                {
                    switch (n->kind)
                    {
                    case construct::Multiply: case construct::Divide: case construct::Remainder: case construct::Add: case construct::Subtract:
                    case construct::ShiftLeft: case construct::ShiftRight: case construct::ShiftRightUnsigned: case construct::LessThan:
                    case construct::GreaterThan: case construct::LessEqual: case construct::GreaterEqual: case construct::InstanceOf:
                    case construct::In: case construct::StrictEqual: case construct::StrictNotEqual: case construct::Equal:
                    case construct::NotEqual: case construct::BitAnd: case construct::BitXor: case construct::BitOr: case construct::And:
                    case construct::Or: case construct::Coalesce: case construct::Conditional:
                        return true;
                    default:
                        return false;
                    }
                };
                node* root = c->child(0u);
                node* holder = nullptr;
                node* leftmost = root;
                while (binary(leftmost))
                {
                    holder = leftmost;
                    leftmost = leftmost->child(0u);
                }
                auto* multiply = make_node(construct::Multiply, std::string_view{ c->source.data(),
                    static_cast<std::size_t>(leftmost->source.data() + leftmost->source.size() - c->source.data()) });
                multiply->children = { identifier, leftmost };
                identifier->parent = multiply;
                leftmost->parent = multiply;
                if (holder != nullptr)
                {
                    holder->children[0] = multiply;
                    multiply->parent = holder;
                }
                else
                    root = multiply;
                root->parent = aNode;
                aNode->children[i] = root;
                c = root;
            }
            yield_identifiers(c, aGenerator);
        }
    }

    void compiler::array_commas(node* aNode)
    {
        // (an array literal's (or pattern's) elements are separated by commas: the grammar does not require them)
        if (aNode->is(construct::Array) || aNode->is(construct::ArrayPattern))
            for (std::size_t i = 0u; i + 1u < aNode->size(); ++i)
            {
                auto* a = aNode->child(i);
                auto* b = aNode->child(i + 1u);
                if (a->is(construct::Hole) || b->is(construct::Hole))
                    continue;
                auto const end = a->source.data() + a->source.size();
                if (b->source.data() <= end)
                    continue;
                bool comma = false;
                for (auto p = end; p < b->source.data() && !comma; ++p)
                    comma = *p == ',';
                if (!comma)
                    error(b->source, "Unexpected token: missing ',' in array literal");
            }
        for (auto* c : aNode->children)
            array_commas(c);
    }

    void compiler::await_operators(node* aNode, bool aAwait)
    {
        // (where await is an operator (an async function's body and parameters, a module's top level), "await (x)", "await `x`" and
        // "await [x]" are await expressions, not a call, a tagged template or an index of an identifier named await; the operand is the
        // whole chain (e.g. "await (f)(x).y" awaits f(x).y))
        switch (aNode->kind)
        {
        case construct::AsyncFunction:
        case construct::AsyncFunctionExpression:
        case construct::AsyncArrow:
        case construct::AsyncMethod:
        case construct::AsyncGeneratorMethod:
            aAwait = true;
            break;
        case construct::Function:
        case construct::FunctionExpression:
        case construct::Arrow:
        case construct::Method:
        case construct::GeneratorMethod:
        case construct::Getter:
        case construct::Setter:
        case construct::Field:
        case construct::StaticBlock:
            aAwait = false;
            break;
        default:
            break;
        }
        auto const link = [](node* n)
        {
            return n->is(construct::Call) || n->is(construct::Tagged) || n->is(construct::Index) || n->is(construct::Dot) ||
                n->is(construct::DotPrivate) || n->is(construct::PostIncrement) || n->is(construct::PostDecrement);
        };
        for (std::size_t i = 0u; i < aNode->size(); ++i)
        {
            auto* top = aNode->child(i);
            if (aAwait && link(top))
            {
                // (the chain's base: the first link's callee/tag/object)
                node* first = top;
                while (link(first->child(0u)))
                    first = first->child(0u);
                auto* base = first->child(0u);
                if (base != nullptr && base->is(construct::Identifier) && base->source == "await" &&
                    (first->is(construct::Call) || first->is(construct::Tagged) || first->is(construct::Index)))
                {
                    node* operand = nullptr;
                    if (first->is(construct::Call))
                    {
                        if (first->size() < 2u)
                            error(first->source, "'await' expression expected an operand");
                        for (std::size_t a = 1u; a < first->size(); ++a)
                        {
                            auto* argument = first->child(a);
                            if (argument->is(construct::Spread))
                                error(argument->source, "'...' was unexpected here");
                            if (operand == nullptr)
                                operand = argument;
                            else
                            {
                                auto* comma = make_node(construct::Comma, std::string_view{ operand->source.data(),
                                    static_cast<std::size_t>(argument->source.data() + argument->source.size() - operand->source.data()) });
                                comma->children = { operand, argument };
                                operand->parent = comma;
                                argument->parent = comma;
                                operand = comma;
                            }
                        }
                        auto const text = std::string_view{ base->source.data() + base->source.size(),
                            static_cast<std::size_t>(first->source.data() + first->source.size() - (base->source.data() + base->source.size())) };
                        auto* parenthesized = make_node(construct::Parenthesized, text);
                        parenthesized->children = { operand };
                        operand->parent = parenthesized;
                        operand = parenthesized;
                    }
                    else if (first->is(construct::Tagged))
                        operand = first->child(1u);
                    else
                    {
                        auto const text = std::string_view{ base->source.data() + base->source.size(),
                            static_cast<std::size_t>(first->source.data() + first->source.size() - (base->source.data() + base->source.size())) };
                        operand = make_node(construct::Array, text);
                        operand->children = { first->child(1u) };
                        first->child(1u)->parent = operand;
                    }
                    // (the chain with the operand as its base, awaited)
                    node* chain = operand;
                    if (first != top)
                    {
                        first->parent->children[0] = operand;
                        operand->parent = first->parent;
                        chain = top;
                    }
                    auto* await = make_node(construct::Await, top->source);
                    await->children = { chain };
                    chain->parent = await;
                    await->parent = aNode;
                    aNode->children[i] = await;
                    top = await;
                }
            }
            await_operators(top, aAwait);
        }
    }

    void compiler::analyse_function(function_info& aFunction)
    {
        // (the slots of the environments of its scopes; generators' and async functions' bindings are all captured (their bodies
        // run as functions of their own))
        for (auto& s : iScopes)
        {
            if (s.function != &aFunction)
                continue;
            if ((aFunction.generator || aFunction.async) && !aFunction.script)
                for (auto& b : s.bindings)
                    b->captured = true;
            if (s.dynamic)
                for (auto& b : s.bindings)
                    b->captured = true;
            // (a sloppy function with simple parameters that uses its arguments object: its parameters are the object's mapped elements)
            if (&s == aFunction.top && aFunction.argumentsBinding != nullptr && !aFunction.strict && aFunction.simpleParameters)
                for (auto& b : s.bindings)
                    if (b->kind == binding_kind::Parameter)
                        b->captured = true;
            std::uint32_t slot = 0u;
            for (auto& b : s.bindings)
                if (b->captured)
                    b->slot = slot++;
            s.environmentSize = slot;
            s.hasEnvironment = slot != 0u || s.kind == scope_kind::With || sloppy_eval_var_scope(&s);
        }
        for (auto* f : aFunction.functions)
            analyse_function(*f);
    }

    void compiler::hoist_var(node* aNode, scope* aVarScope, function_info* aFunction)
    {
        // (var declarations within aNode (not within functions) are declared in the function's var scope)
        if (aNode == nullptr || is_function_node(aNode) || is_class_node(aNode))
            return;
        switch (aNode->kind)
        {
        case construct::Var:
        case construct::ForVar:
            for (auto* d : aNode->children)
            {
                if (d->is(construct::Declarator))
                    declare_pattern(aVarScope, d->child(0u), binding_kind::Var);
                else
                    declare_pattern(aVarScope, d, binding_kind::Var);
            }
            return;
        default:
            break;
        }
        for (auto* c : aNode->children)
            hoist_var(c, aVarScope, aFunction);
    }

    void compiler::collect(node* aNode, scope* aScope, function_info* aFunction)
    {
        // declarations: the function's var declarations (hoisted), lexical declarations, functions and classes
        if (aNode == nullptr)
            return;
        auto const function_scope = [&](node* aFunctionNode, function_info* aInner)
        {
            // (a function: its parameters and body)
            node* parameters = nullptr;
            node* body = nullptr;
            node* name = nullptr;
            for (auto* c : aFunctionNode->children)
            {
                if (c->is(construct::Parameters))
                    parameters = c;
                else if (c->is(construct::Body))
                    body = c;
                else if (c->is(construct::Binding) && parameters == nullptr && name == nullptr)
                    name = c;
            }
            if (body == nullptr && aFunctionNode->size() >= 2u && (aFunctionNode->is(construct::Arrow) || aFunctionNode->is(construct::AsyncArrow)))
                body = aFunctionNode->children.back();   // (a concise body: an expression)
            aInner->generator = generator_header(aFunctionNode);
            aInner->async = aFunctionNode->is(construct::AsyncFunction) || aFunctionNode->is(construct::AsyncFunctionExpression) ||
                aFunctionNode->is(construct::AsyncArrow) || aFunctionNode->is(construct::AsyncMethod) || aFunctionNode->is(construct::AsyncGeneratorMethod);
            aInner->arrow = aFunctionNode->is(construct::Arrow) || aFunctionNode->is(construct::AsyncArrow);
            aInner->strict = aFunction->strict || (body != nullptr && body->is(construct::Body) && directive_strict(body));
            aInner->expression = aFunctionNode->is(construct::FunctionExpression) || aFunctionNode->is(construct::AsyncFunctionExpression);
            if (name != nullptr)
                aInner->name = name_of(name);
            scope* outer = aScope;
            if (aInner->expression && name != nullptr)
            {
                // (a named function expression's name is a binding of a scope of its own)
                outer = new_scope(scope_kind::Block, aScope, nullptr, aInner);
                declare(outer, aInner->name, binding_kind::Callee, name);
                aInner->calleeBinding = outer->find(aInner->name);
            }
            aInner->top = new_scope(scope_kind::Function, outer, aFunctionNode, aInner);
            aInner->body = aInner->top;
            if (parameters != nullptr)
            {
                std::uint32_t length = 0u;
                bool counting = true;
                for (auto* p : parameters->children)
                {
                    if (!p->is(construct::Binding))
                        aInner->simpleParameters = false;
                    if (p->is(construct::Initializer) || p->is(construct::Rest))
                        counting = false;
                    if (counting)
                        ++length;
                    declare_pattern(aInner->top, p, binding_kind::Parameter);
                }
                aInner->length = length;
                if (!aInner->simpleParameters && aInner->strict && body != nullptr && body->is(construct::Body) && directive_strict(body))
                    error(aFunctionNode->source, "Illegal 'use strict' directive in function with non-simple parameter list");
            }
            if (body != nullptr && body->is(construct::Body))
            {
                if (!aInner->simpleParameters)
                    aInner->body = new_scope(scope_kind::Block, aInner->top, body, aInner);
                hoist_var(body, aInner->top, aInner);
                for (auto* s : body->children)
                    collect(s, aInner->body, aInner);
            }
            else if (body != nullptr)
                collect(body, aInner->body, aInner);
            if (parameters != nullptr)
                for (auto* p : parameters->children)
                    collect(p, aInner->top, aInner);
        };
        switch (aNode->kind)
        {
        case construct::Function:
        case construct::AsyncFunction:
            {
                // (a function declaration: a binding of its scope (var-like at the function's top level, lexical in a block))
                auto* inner = new_function(aNode, aFunction);
                auto* name = aNode->child(0u);
                inner->name = name_of(name);
                bool const topLevel = (aScope == aFunction->body || aScope == aFunction->top) && aScope->kind != scope_kind::Block;
                bool const topBody = aScope == aFunction->body;
                auto* b = declare(aScope, inner->name, (topLevel || topBody) ? binding_kind::Function : binding_kind::Function, name);
                if (!topLevel && !topBody)
                {
                    b->tdz = false;
                    // (Annex B.3.3: a sloppy block's plain function declaration may also be a var binding of its function)
                    if (!aFunction->strict && aNode->is(construct::Function) && !generator_header(aNode))
                        iAnnexBFunctions.push_back({ aNode, aScope, aFunction });
                }
                function_scope(aNode, inner);
            }
            return;
        case construct::FunctionExpression:
        case construct::AsyncFunctionExpression:
        case construct::Arrow:
        case construct::AsyncArrow:
            function_scope(aNode, new_function(aNode, aFunction));
            return;
        case construct::Method:
        case construct::Getter:
        case construct::Setter:
        case construct::GeneratorMethod:
        case construct::AsyncMethod:
        case construct::AsyncGeneratorMethod:
            {
                // (a computed key is evaluated in the enclosing scope)
                collect(aNode->child(0u), aScope, aFunction);
                auto* inner = new_function(aNode, aFunction);
                inner->method = true;
                inner->getter = aNode->is(construct::Getter);
                inner->setter = aNode->is(construct::Setter);
                function_scope(aNode, inner);
                inner->strict = inner->strict || aScope->kind == scope_kind::Class;
            }
            return;
        case construct::Class:
        case construct::ClassExpression:
            {
                node* name = (aNode->size() > 0u && aNode->child(0u)->is(construct::Binding)) ? aNode->child(0u) : nullptr;
                if (aNode->is(construct::Class))
                    declare(aScope, name_of(name), binding_kind::Class, name);
                auto* classScope = new_scope(scope_kind::Class, aScope, aNode, aFunction);
                bool has_heritage = false;
                for (auto* c : aNode->children)
                    if (c->is(construct::Extends))
                        has_heritage = true;
                if (name != nullptr)
                    declare(classScope, name_of(name), binding_kind::Const, name)->tdz = true;
                // (its private names: bindings of its scope)
                for (auto* c : aNode->children)
                {
                    auto* e = c->is(construct::Static) ? c->child(0u) : c;
                    if (e != nullptr && e->size() > 0u && !e->is(construct::StaticBlock) && !e->is(construct::Extends) && e != name &&
                        e->child(0u)->is(construct::Private))
                    {
                        auto const privateName = name_of(e->child(0u));
                        if (classScope->find(privateName) == nullptr)
                        {
                            auto* b = declare(classScope, privateName, binding_kind::Const, e->child(0u));
                            b->tdz = false;
                            b->captured = true;
                        }
                    }
                }
                // (class code is strict)
                bool const wasStrict = aFunction->strict;
                aFunction->strict = true;
                for (auto* c : aNode->children)
                {
                    if (c == name)
                        continue;
                    if (c->is(construct::Extends))
                        collect(c, classScope, aFunction);
                    else if (c->is(construct::Field) || (c->is(construct::Static) && c->child(0u)->is(construct::Field)))
                    {
                        // (a field's initializer is a method of its own (its key is evaluated in the class's scope))
                        auto* field = c->is(construct::Static) ? c->child(0u) : c;
                        collect(field->child(0u), classScope, aFunction);
                        if (field->size() > 1u)
                        {
                            auto* inner = new_function(field, aFunction);
                            inner->method = true;
                            inner->fieldInitializer = true;
                            inner->strict = true;
                            inner->arrow = false;
                            inner->top = new_scope(scope_kind::Function, classScope, field, inner);
                            inner->body = inner->top;
                            collect(field->child(1u), inner->top, inner);
                        }
                    }
                    else if (c->is(construct::StaticBlock) || (c->is(construct::Static) && c->child(0u)->is(construct::StaticBlock)))
                    {
                        auto* block = c->is(construct::Static) ? c->child(0u) : c;
                        auto* inner = new_function(block, aFunction);
                        inner->method = true;
                        inner->staticBlock = true;
                        inner->strict = true;
                        inner->top = new_scope(scope_kind::Function, classScope, block, inner);
                        inner->body = inner->top;
                        hoist_var(block, inner->top, inner);
                        for (auto* s : block->children)
                            collect(s, inner->top, inner);
                    }
                    else
                    {
                        collect(c, classScope, aFunction);
                        if (is_class_constructor_method(c) && c->function != nullptr)
                        {
                            c->function->classConstructor = true;
                            c->function->derived = has_heritage;
                        }
                    }
                }
                aFunction->strict = wasStrict;
            }
            return;
        case construct::Let:
        case construct::Const:
        case construct::Using:
        case construct::AwaitUsing:
            for (auto* d : aNode->children)
            {
                declare_pattern(aScope, d->child(0u), aNode->is(construct::Let) ? binding_kind::Let : binding_kind::Const);
                for (auto* c : d->children)
                    collect(c, aScope, aFunction);
            }
            return;
        case construct::Block:
            {
                auto* s = new_scope(scope_kind::Block, aScope, aNode, aFunction);
                for (auto* c : aNode->children)
                    collect(c, s, aFunction);
            }
            return;
        case construct::Switch:
            {
                collect(aNode->child(0u), aScope, aFunction);
                auto* s = new_scope(scope_kind::Block, aScope, aNode, aFunction);
                for (std::size_t i = 1u; i < aNode->size(); ++i)
                    for (auto* c : aNode->child(i)->children)
                        collect(c, s, aFunction);
            }
            return;
        case construct::For:
        case construct::ForIn:
        case construct::ForOf:
        case construct::ForAwaitOf:
            {
                auto* s = new_scope(scope_kind::Block, aScope, aNode, aFunction);
                for (auto* c : aNode->children)
                {
                    if (c->is(construct::ForInit) && c->size() == 1u && (c->child(0u)->is(construct::Let) || c->child(0u)->is(construct::Const)))
                        collect(c->child(0u), s, aFunction);
                    else if (c->is(construct::ForLet) || c->is(construct::ForConst) || c->is(construct::ForUsing) || c->is(construct::ForAwaitUsing))
                    {
                        declare_pattern(s, c->child(0u), c->is(construct::ForLet) ? binding_kind::Let : binding_kind::Const);
                        collect(c->child(0u), s, aFunction);
                    }
                    else
                        collect(c, s, aFunction);
                }
            }
            return;
        case construct::Catch:
            {
                auto* s = new_scope(scope_kind::Catch, aScope, aNode, aFunction);
                if (aNode->size() == 2u)
                {
                    declare_pattern(s, aNode->child(0u), binding_kind::Catch);
                    collect(aNode->child(0u), s, aFunction);
                }
                // (the catch block's own declarations: its block's scope)
                collect(aNode->children.back(), s, aFunction);
            }
            return;
        case construct::With:
            {
                collect(aNode->child(0u), aScope, aFunction);
                auto* s = new_scope(scope_kind::With, aScope, aNode, aFunction);
                // (the bindings of the scopes that enclose it can be found by name (a name in it may be its object's property))
                for (auto* t = s; t != nullptr; t = t->parent)
                    t->dynamic = true;
                collect(aNode->child(1u), s, aFunction);
            }
            return;
        default:
            break;
        }
        for (auto* c : aNode->children)
            collect(c, aScope, aFunction);
    }

    void compiler::use(std::string const& aName, scope* aScope, function_info* aFunction, node* aSite)
    {
        // (a reference to a binding: bindings of functions other than the one that uses them are captured)
        (void)aSite;
        for (auto* s = aScope; s != nullptr; s = s->parent)
        {
            if (auto* b = s->find(aName); b != nullptr && !(eval_code() && s->kind == scope_kind::Script && !s->function->strict && (b->kind == binding_kind::Var || b->kind == binding_kind::Function)))
            {
                if (s->function != aFunction)
                {
                    b->captured = true;
                    for (auto* f = aFunction; f != nullptr && f != s->function; f = f->parent)
                        f->needsOuterEnvironment = true;
                }
                return;
            }
            // (a name that may be a with statement's object's property, a sloppy direct eval's var binding or (in eval code) a binding of the
            // code that called eval: found by name at run time, through the environments)
            if (s->kind == scope_kind::With || sloppy_eval_var_scope(s) || (s->kind == scope_kind::Script && eval_code()))
                for (auto* f = aFunction; f != nullptr && f != s->function; f = f->parent)
                    f->needsOuterEnvironment = true;
        }
    }

    void compiler::resolve_uses(node* aNode, scope* aScope, function_info* aFunction)
    {
        if (aNode == nullptr)
            return;
        if (aNode->ownScope != nullptr && aNode->ownScope->function == aFunction && aNode->ownScope != aScope && !is_function_node(aNode))
        {
            // (a scope of the function: its children are in it (a with statement's object and a switch's discriminant are not; a
            // class's heritage is))
            auto* s = aNode->ownScope;
            if (aNode->is(construct::With) || aNode->is(construct::Switch))
            {
                resolve_uses(aNode->child(0u), aScope, aFunction);
                for (std::size_t i = 1u; i < aNode->size(); ++i)
                    resolve_uses(aNode->child(i), s, aFunction);
                return;
            }
            for (auto* c : aNode->children)
                resolve_uses(c, s, aFunction);
            return;
        }
        if (aNode->function != nullptr && aNode->function != aFunction)
        {
            auto* inner = aNode->function;
            // (a method's computed key belongs to the enclosing code)
            if (inner->method && !inner->fieldInitializer && !inner->staticBlock && aNode->size() > 0u)
                resolve_uses(aNode->child(0u), aScope, aFunction);
            if (inner->fieldInitializer)
            {
                resolve_uses(aNode->child(1u), inner->top, inner);
                return;
            }
            for (std::size_t i = (inner->method && !inner->staticBlock) ? 1u : 0u; i < aNode->size(); ++i)
            {
                auto* c = aNode->child(i);
                if (c->is(construct::Binding) && !inner->arrow)
                    continue;
                resolve_uses(c, c->is(construct::Body) && inner->body != inner->top ? inner->body : inner->top, inner);
            }
            if (inner->arrow)
            {
                // (an arrow function's this, arguments, new.target and super are those of the function that encloses it)
                auto* f = inner;
                while (f->arrow && f->parent != nullptr)
                    f = f->parent;
                if (inner->usesThis || inner->usesArguments || inner->usesNewTarget || inner->usesSuper || inner->usesSuperCall)
                {
                    if (inner->usesThis)
                        f->usesThis = true;
                    if (inner->usesArguments)
                        f->usesArguments = true;
                    if (inner->usesNewTarget)
                        f->usesNewTarget = true;
                    if (inner->usesSuper)
                        f->usesSuper = true;
                    if (inner->usesSuperCall)
                        f->usesSuperCall = true;
                    for (auto* g = inner; g != f; g = g->parent)
                        g->needsOuterEnvironment = true;
                    auto const internal = [&](binding*& aBinding, char const* aName)
                    {
                        if (aBinding == nullptr)
                            aBinding = declare(f->top, aName, binding_kind::Internal, nullptr);
                        aBinding->captured = true;
                    };
                    if (inner->usesThis && (!f->script || eval_code()))
                        internal(f->thisBinding, "%this");
                    if (inner->usesNewTarget && !f->script)
                        internal(f->newTargetBinding, "%newtarget");
                    if ((inner->usesSuper || inner->usesSuperCall) && !f->script)
                        internal(f->homeBinding, "%home");
                    if ((inner->usesSuperCall || inner->usesArguments) && !f->script)
                        internal(f->calleeBinding == nullptr || f->calleeBinding->kind != binding_kind::Internal ? f->calleeBinding : f->calleeBinding, "%callee");
                    if (inner->usesArguments && !f->script && f->argumentsBinding != nullptr)
                        f->argumentsBinding->captured = true;
                    if (inner->usesSuperCall && !f->script && f->thisBinding == nullptr)
                        internal(f->thisBinding, "%this");
                }
            }
            return;
        }
        switch (aNode->kind)
        {
        case construct::Identifier:
            {
                auto const name = name_of(aNode);
                if (name == "arguments" && !aFunction->script)
                {
                    // (arguments: the arguments object of the function (or of the function that encloses an arrow function))
                    bool shadowed = false;
                    for (auto* s = aScope; s != nullptr; s = s->parent)
                    {
                        if (s->find(name) != nullptr && !(s->find(name)->kind == binding_kind::Internal))
                        {
                            shadowed = true;
                            break;
                        }
                        if (s->kind == scope_kind::Function && !s->function->arrow)
                            break;
                    }
                    if (!shadowed)
                    {
                        aFunction->usesArguments = true;
                        auto* f = aFunction;
                        while (f->arrow && f->parent != nullptr && !f->parent->script)
                            f = f->parent;
                        if (!f->script && !f->arrow && f->argumentsBinding == nullptr && !f->fieldInitializer && !f->staticBlock)
                            f->argumentsBinding = declare(f->top, "arguments", binding_kind::Var, nullptr);
                        if (f != aFunction && f->argumentsBinding != nullptr)
                        {
                            f->argumentsBinding->captured = true;
                            for (auto* g = aFunction; g != f; g = g->parent)
                                g->needsOuterEnvironment = true;
                        }
                        return;
                    }
                }
                if (name == "eval" && aNode->parent != nullptr && aNode->parent->is(construct::Call) && aNode->parent->child(0u) == aNode)
                {
                    // (a direct eval: every binding of the scopes that enclose it can be found by name)
                    aFunction->directEval = true;
                    for (auto* s = aScope; s != nullptr; s = s->parent)
                    {
                        s->dynamic = true;
                        for (auto* f = s->function; f != nullptr; f = f->parent)
                            f->needsOuterEnvironment = f->needsOuterEnvironment || f != s->function;
                    }
                    for (auto* f = aFunction; f != nullptr; f = f->parent)
                    {
                        f->usesThis = true;
                        f->usesNewTarget = true;
                    }
                    {
                        // (in a derived class's constructor the eval code may call super() and use this before (and after) it: the
                        // constructor's this and callee are bindings it finds by name)
                        auto* f = aFunction;
                        while (f->arrow && f->parent != nullptr)
                            f = f->parent;
                        if (!f->script && f->classConstructor && f->derived)
                        {
                            if (f->thisBinding == nullptr)
                                f->thisBinding = declare(f->top, "%this", binding_kind::Internal, nullptr);
                            f->thisBinding->captured = true;
                            if (f->calleeBinding == nullptr)
                                f->calleeBinding = declare(f->top, "%callee", binding_kind::Internal, nullptr);
                            f->calleeBinding->captured = true;
                            f->usesSuperCall = true;
                        }
                    }
                    // (the eval code may use arguments: the function's arguments object)
                    {
                        auto* f = aFunction;
                        while (f->arrow && f->parent != nullptr && !f->parent->script)
                            f = f->parent;
                        if (!f->script && !f->arrow && !f->fieldInitializer && !f->staticBlock && f->argumentsBinding == nullptr && f->top->find("arguments") == nullptr)
                            f->argumentsBinding = declare(f->top, "arguments", binding_kind::Var, nullptr);
                        if (f->argumentsBinding != nullptr)
                            f->argumentsBinding->captured = true;
                    }
                }
                use(name, aScope, aFunction, aNode);
            }
            return;
        case construct::Private:
            {
                // (a private name: a binding of the class that declares it)
                auto const name = name_of(aNode);
                bool declared = false;
                for (auto* s = aScope; s != nullptr && !declared; s = s->parent)
                    declared = s->find(name) != nullptr;
                if (!declared)
                    error(aNode->source, "Private field '" + name + "' must be declared in an enclosing class");
                use(name, aScope, aFunction, aNode);
            }
            return;
        case construct::This:
            aFunction->usesThis = true;
            return;
        case construct::NewTarget:
            aFunction->usesNewTarget = true;
            return;
        case construct::SuperDot:
        case construct::SuperIndex:
            aFunction->usesSuper = true;
            aFunction->usesThis = true;
            break;
        case construct::SuperCall:
            aFunction->usesSuperCall = true;
            aFunction->usesThis = true;
            aFunction->usesNewTarget = true;
            break;
        default:
            break;
        }
        for (auto* c : aNode->children)
            resolve_uses(c, aScope, aFunction);
    }

    // --- code generation

    std::uint32_t compiler::global_index(std::string const& aName, type aType)
    {
        for (std::size_t g = 0u; g < generator().global_count(); ++g)
            if (generator().global_name(g).to_std_string_view() == aName)
                return static_cast<std::uint32_t>(g + 1u);
        generator().add_global(neolib::string{ aName }, aType);
        return static_cast<std::uint32_t>(generator().global_count());
    }

    std::uint32_t compiler::literal_string(std::string const& aText)
    {
        auto const existing = iLiterals.find(aText);
        if (existing != iLiterals.end())
            return existing->second;
        auto const g = global_index("%js" + std::to_string(iLiterals.size()), type::I64);
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
