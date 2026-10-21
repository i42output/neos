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

#include <neolib/neolib.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <neos/bytecode/bytecode.hpp>
#include <neos/bytecode/opcodes.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"

namespace neos::compilers::neoscript
{
    using namespace neos::language::emitter;
    using bytecode::opcode;

    // --- neoscript as it is written (the constructs' sources)

    namespace
    {
        bool is_word_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
        bool is_word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        struct token
        {
            std::string_view text;
            bool is_word() const { return !text.empty() && is_word_start(text[0]); }
            bool is(std::string_view aText) const { return text == aText; }
        };

        std::vector<token> tokenize(std::string_view aText)
        {
            std::vector<token> result;
            std::size_t i = 0u;
            auto const n = aText.size();
            while (i < n)
            {
                char const c = aText[i];
                if (std::isspace(static_cast<unsigned char>(c)))
                {
                    ++i;
                    continue;
                }
                if (c == '/' && i + 1u < n && aText[i + 1u] == '/')
                {
                    while (i < n && aText[i] != '\n')
                        ++i;
                    continue;
                }
                std::size_t j = i + 1u;
                if (is_word_start(c))
                {
                    while (j < n && is_word_char(aText[j]))
                        ++j;
                }
                else if (std::isdigit(static_cast<unsigned char>(c)))
                {
                    while (j < n && (std::isalnum(static_cast<unsigned char>(aText[j])) || aText[j] == '.' ||
                        ((aText[j] == '+' || aText[j] == '-') && (aText[j - 1u] == 'e' || aText[j - 1u] == 'E'))))
                        ++j;
                }
                else if (c == '"' || c == '\'')
                {
                    while (j < n && aText[j] != c)
                        j += (aText[j] == '\\' && j + 1u < n) ? 2u : 1u;
                    if (j < n)
                        ++j;
                }
                else if ((c == ':' && i + 1u < n && aText[i + 1u] == ':') || (c == '-' && i + 1u < n && aText[i + 1u] == '>'))
                    j = i + 2u;
                result.push_back(token{ aText.substr(i, j - i) });
                i = j;
            }
            return result;
        }

        // (a type's name: [::]name{::name})
        std::string qualified(std::vector<token> const& aTokens, std::size_t& aIndex)
        {
            std::string result;
            if (aIndex < aTokens.size() && aTokens[aIndex].is("::"))
            {
                result = "::";
                ++aIndex;
            }
            while (aIndex < aTokens.size() && aTokens[aIndex].is_word())
            {
                result += aTokens[aIndex++].text;
                if (aIndex + 1u < aTokens.size() && aTokens[aIndex].is("::") && aTokens[aIndex + 1u].is_word())
                {
                    result += "::";
                    ++aIndex;
                }
                else
                    break;
            }
            return result;
        }

        std::string trimmed(std::string_view aText)
        {
            while (!aText.empty() && std::isspace(static_cast<unsigned char>(aText.front())))
                aText.remove_prefix(1u);
            while (!aText.empty() && (std::isspace(static_cast<unsigned char>(aText.back())) || aText.back() == ';'))
                aText.remove_suffix(1u);
            return std::string{ aText };
        }

        std::string strip_scope(std::string aName)
        {
            while (aName.starts_with("::"))
                aName.erase(0u, 2u);
            return aName;
        }

        struct parsed_signature
        {
            std::string name;
            std::vector<parameter_info> parameters;
            std::optional<std::string> resultText;
            bool isMutable = false;
            bool isFinal = false;
            bool isOverride = false;
            std::size_t end = 0u;       ///< (the token after its parameters)
        };

        // (parameters: blocks separated by ';' of names separated by ',', then ':', a direction and a type)
        void parse_parameters(std::vector<token> const& aTokens, std::size_t& aIndex, std::vector<parameter_info>& aParameters, char aEnd)
        {
            std::vector<std::string> names;
            while (aIndex < aTokens.size() && !(aTokens[aIndex].text.size() == 1u && aTokens[aIndex].text[0] == aEnd))
            {
                auto const& t = aTokens[aIndex];
                if (t.is(":"))
                {
                    ++aIndex;
                    parameter_direction direction = parameter_direction::In;
                    if (aIndex < aTokens.size() && (aTokens[aIndex].is("in") || aTokens[aIndex].is("out") || aTokens[aIndex].is("inout")))
                    {
                        direction = aTokens[aIndex].is("out") ? parameter_direction::Out : aTokens[aIndex].is("inout") ?
                            parameter_direction::InOut : parameter_direction::In;
                        ++aIndex;
                    }
                    auto const typeText = qualified(aTokens, aIndex);
                    for (auto const& name : names)
                        aParameters.push_back(parameter_info{ name, typeText, direction });
                    names.clear();
                }
                else
                {
                    if (t.is_word())
                        names.push_back(std::string{ t.text });
                    ++aIndex;
                }
            }
        }

        parsed_signature parse_signature(std::string_view aText)
        {
            parsed_signature result;
            auto const tokens = tokenize(aText);
            std::size_t i = 0u;
            if (i < tokens.size() && tokens[i].is_word())
                result.name = std::string{ tokens[i++].text };
            if (i < tokens.size() && tokens[i].is("("))
            {
                ++i;
                parse_parameters(tokens, i, result.parameters, ')');
                if (i < tokens.size())
                    ++i;
            }
            for (; i < tokens.size(); ++i)
            {
                if (tokens[i].is("mutable"))
                    result.isMutable = true;
                else if (tokens[i].is("final"))
                    result.isFinal = true;
                else if (tokens[i].is("override"))
                    result.isOverride = true;
                else if (tokens[i].is("->"))
                {
                    ++i;
                    result.resultText = qualified(tokens, i);
                    --i;
                }
                else
                    break;
            }
            result.end = i;
            return result;
        }

        std::optional<type> builtin_type(std::string_view aName)
        {
            static std::map<std::string_view, type> const sTypes =
            {
                { "bool", type::Boolean }, { "i8", type::I8 }, { "u8", type::U8 }, { "i16", type::I16 }, { "u16", type::U16 },
                { "i32", type::I32 }, { "u32", type::U32 }, { "i64", type::I64 }, { "u64", type::U64 },
                { "isize", type::I32 }, { "usize", type::U32 }, { "float", type::F32 }, { "f32", type::F32 },
                { "double", type::F64 }, { "f64", type::F64 }, { "char", type::U8 }, { "string", type::String },
                { "object", type::Object }
            };
            auto const existing = sTypes.find(aName);
            if (existing != sTypes.end())
                return existing->second;
            return {};
        }

        char const* access_name(access_kind aAccess)
        {
            return aAccess == access_kind::Private ? "private" : aAccess == access_kind::Protected ? "protected" : "public";
        }

        struct memory_access
        {
            opcode load;
            opcode store;
            std::uint32_t align;
        };

        std::optional<memory_access> memory_access_of(type aType)
        {
            switch (aType)
            {
            case type::Boolean:
            case type::U8: return memory_access{ opcode::I32LoadMem8U, opcode::I32StoreMem8, 0u };
            case type::I8: return memory_access{ opcode::I32LoadMem8S, opcode::I32StoreMem8, 0u };
            case type::I16: return memory_access{ opcode::I32LoadMem16S, opcode::I32StoreMem16, 1u };
            case type::U16: return memory_access{ opcode::I32LoadMem16U, opcode::I32StoreMem16, 1u };
            case type::I32:
            case type::U32:
            case type::String:
            case type::Pointer: return memory_access{ opcode::I32LoadMem, opcode::I32StoreMem, 2u };
            case type::I64:
            case type::U64: return memory_access{ opcode::I64LoadMem, opcode::I64StoreMem, 3u };
            case type::F32: return memory_access{ opcode::F32LoadMem, opcode::F32StoreMem, 2u };
            case type::F64: return memory_access{ opcode::F64LoadMem, opcode::F64StoreMem, 3u };
            default: return {};
            }
        }

        void put_local_get(fragment& aCode, std::uint32_t aIndex)
        {
            put_op(aCode.fragmentCode.as_std_vector(), opcode::LocalGet);
            put_u32(aCode.fragmentCode.as_std_vector(), aIndex);
        }

        void put_local_set(fragment& aCode, std::uint32_t aIndex)
        {
            put_op(aCode.fragmentCode.as_std_vector(), opcode::LocalSet);
            put_u32(aCode.fragmentCode.as_std_vector(), aIndex);
        }

        void put_i32(fragment& aCode, std::int32_t aValue)
        {
            put_op(aCode.fragmentCode.as_std_vector(), opcode::I32Const);
            put_s64(aCode.fragmentCode.as_std_vector(), aValue);
        }

        void put_call(fragment& aCode, std::uint32_t aFunction)
        {
            auto& code = aCode.fragmentCode.as_std_vector();
            put_op(code, opcode::CallFunction);
            aCode.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), aFunction });
            code.resize(code.size() + 5u);
        }
    }

    // --- the compiler

    compiler::compiler() :
        language_compiler{ language::neoscript::Language }
    {
    }

    void compiler::begin_compilation()
    {
        iPass = 0u;
        iResolved = false;
        iTypes.clear();
        iTypeNames.clear();
        iAliases.clear();
        iFunctionRecords.clear();
    }

    void compiler::begin_pass()
    {
        ++iPass;
        iTypeOrdinal = 0u;
        iTypeStack.clear();
        iContexts.clear();
        iDefinitions.clear();
        iFrameNames.clear();
        iVariables.clear();
        iAnnotations.clear();
        iCopyFunctions.clear();
        iDispatchFunctions.clear();
        for (auto& t : iTypes)
        {
            t.methodOrdinal = 0u;
            t.fieldOrdinal = 0u;
        }
        // (the types that the declaration pass found)
        if (iPass >= 2u && !iResolved)
        {
            iResolved = true;
            resolve_types();
        }
    }

    void compiler::begin_fragment(i_source_fragment& aFragment)
    {
        iFragments.push_back(&aFragment);
    }

    void compiler::end_fragment()
    {
        if (!iFragments.empty())
            iFragments.pop_back();
    }

    void compiler::emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource)
    {
        auto const source = aSource.to_std_string_view();
        switch (static_cast<construct>(aConstruct))
        {
        case construct::Class: type_definition(source, false); break;
        case construct::Interface: type_definition(source, true); break;
        case construct::ClassEnd: type_definition_end(source); break;
        case construct::Field: field(source); break;
        case construct::AbstractMethod: abstract_method(source); break;
        case construct::Alias: alias(source); break;
        case construct::ObjectInitialize: object_initialize(source); break;
        case construct::Declaration: declaration(source); break;
        case construct::AutoInitialize: auto_initialize(source); break;
        case construct::Assignment: assignment(source); break;
        case construct::Call: call(source); break;
        case construct::MethodCall: method_call(source); break;
        case construct::Member: member(source); break;
        case construct::Postfix: break; // (its members have been compiled: it is the value they produced)
        default: throw std::logic_error("neoscript: unknown construct");
        }
    }

    // --- type definitions

    void compiler::type_definition(std::string_view aSource, bool aInterface)
    {
        auto const tokens = tokenize(aSource);
        std::size_t i = 0u;
        type_kind kind = aInterface ? type_kind::Interface : tokens.at(i).is("struct") ? type_kind::Struct : type_kind::Class;
        ++i;
        std::string const name{ tokens.at(i++).text };
        if (declaration_pass())
        {
            auto const scope = scope_name();
            auto const qualifiedName = scope.empty() ? name : scope + "::" + name;
            if (iTypeNames.contains(qualifiedName) || iAliases.contains(qualifiedName))
                error(aSource, "redefinition of '" + qualifiedName + "'");
            auto& t = iTypes.emplace_back();
            t.name = name;
            t.qualifiedName = qualifiedName;
            t.scope = scope;
            t.kind = kind;
            for (; i < tokens.size(); ++i)
            {
                if (tokens[i].is("final"))
                    t.isFinal = true;
                else if (tokens[i].is("extends") || tokens[i].is("implements"))
                {
                    bool const extends = tokens[i].is("extends");
                    ++i;
                    for (;;)
                    {
                        auto const text = qualified(tokens, i);
                        if (extends && kind != type_kind::Interface)
                        {
                            if (t.baseText)
                                error(aSource, "'" + qualifiedName + "' can extend only one class");
                            t.baseText = text;
                        }
                        else
                            t.interfaceTexts.push_back(text);
                        if (i < tokens.size() && tokens[i].is(","))
                            ++i;
                        else
                            break;
                    }
                    --i;
                }
            }
            if (kind == type_kind::Struct && t.baseText)
                error(aSource, "struct '" + qualifiedName + "' cannot extend a class (a struct has no base)");
            iTypeNames[qualifiedName] = &t;
        }
        if (iTypeOrdinal >= iTypes.size())
            throw std::logic_error("neoscript: type definitions changed");
        auto& t = iTypes[iTypeOrdinal++];
        if (t.error)
            error(aSource, *t.error);
        generator().enter_scope(scope_type::Class, neolib::string{ t.name });
        iTypeStack.push_back(&t);
    }

    void compiler::type_definition_end(std::string_view aSource)
    {
        if (iTypeStack.empty())
            error(aSource, "unmatched '}'");
        generator().leave_scope(scope_type::Class);
        iTypeStack.pop_back();
    }

    void compiler::field(std::string_view aSource)
    {
        if (iTypeStack.empty())
            error(aSource, "a field outside a type");
        auto& owner = *iTypeStack.back();
        // (its initial value: a literal)
        std::optional<operand_type> initializer;
        if (aSource.find('=') != std::string_view::npos)
            initializer = pop_operand_value();
        if (!declaration_pass())
        {
            if (owner.fieldOrdinal < owner.fields.size())
                owner.fields[owner.fieldOrdinal].initializer = initializer;
            ++owner.fieldOrdinal;
            return;
        }
        if (owner.kind == type_kind::Interface)
            error(aSource, "interface '" + owner.qualifiedName + "' cannot have fields");
        auto const tokens = tokenize(aSource);
        field_info f;
        f.owner = &owner;
        f.scope = owner.qualifiedName;
        std::size_t i = 0u;
        for (; i < tokens.size(); ++i)
        {
            if (tokens[i].is("public"))
                f.access = access_kind::Public;
            else if (tokens[i].is("protected"))
                f.access = access_kind::Protected;
            else if (tokens[i].is("private"))
                f.access = access_kind::Private;
            else if (tokens[i].is("mutable"))
                f.isMutable = true;
            else
                break;
        }
        f.typeText = qualified(tokens, i);
        if (i >= tokens.size() || !tokens[i].is_word())
            error(aSource, "field name expected");
        f.name = std::string{ tokens[i].text };
        f.initializer = initializer;
        for (auto const& existing : owner.fields)
            if (existing.name == f.name)
                error(aSource, "redefinition of '" + owner.qualifiedName + "::" + f.name + "'");
        owner.fields.push_back(f);
    }

    void compiler::abstract_method(std::string_view aSource)
    {
        if (iTypeStack.empty())
            error(aSource, "a method declaration outside an interface");
        auto& owner = *iTypeStack.back();
        if (!declaration_pass())
            return;
        auto tokens = tokenize(aSource);
        std::size_t i = 0u;
        method_info m;
        m.owner = &owner;
        m.isAbstract = true;
        for (; i < tokens.size(); ++i)
        {
            if (tokens[i].is("public"))
                m.access = access_kind::Public;
            else if (tokens[i].is("protected") || tokens[i].is("private"))
                error(aSource, "an interface's methods are public");
            else if (tokens[i].is("fn"))
                m.procedure = false;
            else if (tokens[i].is("proc"))
                m.procedure = true;
            else
                break;
        }
        auto const signature = parse_signature(aSource.substr(static_cast<std::size_t>(tokens.at(i).text.data() - aSource.data())));
        m.name = signature.name;
        m.function = owner.qualifiedName + "::" + m.name;
        m.parameters = signature.parameters;
        m.isMutable = signature.isMutable;
        if (signature.resultText)
            m.resultText = *signature.resultText;
        else if (!m.procedure)
            error(aSource, "function '" + m.name + "' must have a result (-> type)");
        if (m.procedure && signature.resultText)
            error(aSource, "procedure '" + m.name + "' cannot have a result");
        owner.methods.push_back(m);
    }

    void compiler::alias(std::string_view aSource)
    {
        if (!declaration_pass())
            return;
        auto const tokens = tokenize(aSource);
        std::size_t i = 0u;
        std::string const name{ tokens.at(i++).text };
        ++i; // (is)
        auto const target = qualified(tokens, i);
        auto const scope = scope_name();
        auto const qualifiedName = scope.empty() ? name : scope + "::" + name;
        if (iTypeNames.contains(qualifiedName) || iAliases.contains(qualifiedName))
            error(aSource, "redefinition of '" + qualifiedName + "'");
        iAliases[qualifiedName] = alias_info{ name, target, scope };
    }

    // --- the types that the declaration pass found

    void compiler::resolve_types()
    {
        std::uint32_t id = 0u;
        for (auto& t : iTypes)
            t.id = ++id;
        for (auto& a : iAliases)
            resolve_alias(a.second);
        for (auto& t : iTypes)
            resolve_type(t);
        for (auto& t : iTypes)
            if (!t.error)
                lay_out(t);
        for (auto& t : iTypes)
            if (!t.error)
                check_methods(t);
        distinguish_overloads();
    }

    void compiler::distinguish_overloads()
    {
        // (overloads whose parameters are the same but for the types of the objects they take are the same function to the 
        // compiler: their functions are named apart)
        auto same = [](std::vector<parameter_info> const& aLhs, std::vector<parameter_info> const& aRhs)
        {
            if (aLhs.size() != aRhs.size())
                return false;
            for (std::size_t i = 0u; i < aLhs.size(); ++i)
                if (aLhs[i].valueType.builtin != aRhs[i].valueType.builtin)
                    return false;
            return true;
        };
        for (auto& t : iTypes)
            for (std::size_t i = 0u; i < t.methods.size(); ++i)
            {
                auto& m = t.methods[i];
                if (m.isAbstract)
                    continue;
                std::size_t index = 0u;
                bool collides = false;
                for (std::size_t j = 0u; j < t.methods.size(); ++j)
                {
                    auto const& other = t.methods[j];
                    if (j == i || other.isAbstract || other.name != m.name)
                        continue;
                    if (j < i)
                        ++index;
                    collides = collides || same(other.parameters, m.parameters);
                }
                if (collides)
                    m.function = t.qualifiedName + "::" + m.name + "$" + std::to_string(index);
            }
        for (auto& records : iFunctionRecords)
            for (std::size_t i = 0u; i < records.second.size(); ++i)
            {
                auto& r = records.second[i];
                if (r.object != nullptr)
                    continue;
                bool objects = false;
                for (auto& p : r.parameters)
                {
                    p.valueType = find_type(p.typeText, r.scope);
                    objects = objects || p.valueType.is_object();
                }
                if (!objects)
                    continue; // (the compiler calls it)
                bool collides = false;
                for (std::size_t j = 0u; j < records.second.size(); ++j)
                {
                    auto& other = records.second[j];
                    if (j == i || other.object != nullptr)
                        continue;
                    for (auto& p : other.parameters)
                        p.valueType = find_type(p.typeText, other.scope);
                    collides = collides || same(other.parameters, r.parameters);
                }
                if (collides)
                    r.function = r.name + "$" + std::to_string(i);
            }
    }

    bool compiler::frame_name(std::uint32_t aFunction, neolib::i_string& aName)
    {
        auto const existing = iFrameNames.find(aFunction);
        if (existing == iFrameNames.end())
            return false;
        aName = neolib::string{ existing->second };
        return true;
    }

    type_info* compiler::resolve_alias(alias_info& aAlias) const
    {
        if (aAlias.target != nullptr || aAlias.resolving)
            return aAlias.target;
        aAlias.resolving = true;
        aAlias.target = find_object_type(aAlias.targetText, aAlias.scope);
        aAlias.resolving = false;
        return aAlias.target;
    }

    void compiler::resolve_type(type_info& aType)
    {
        auto fail = [&](std::string const& aError)
        {
            if (!aType.error)
                aType.error = aError;
        };
        if (aType.baseText)
        {
            aType.base = find_object_type(*aType.baseText, aType.scope);
            if (aType.base == nullptr)
                fail("base class '" + *aType.baseText + "' of '" + aType.qualifiedName + "' not found");
            else if (aType.base->kind != type_kind::Class)
                fail("'" + aType.qualifiedName + "' cannot extend '" + aType.base->qualifiedName + "': it is not a class");
            else if (aType.base->isFinal)
                fail("'" + aType.qualifiedName + "' cannot extend '" + aType.base->qualifiedName + "': it is final");
        }
        for (auto const& text : aType.interfaceTexts)
        {
            auto* const i = find_object_type(text, aType.scope);
            if (i == nullptr)
                fail("interface '" + text + "' of '" + aType.qualifiedName + "' not found");
            else if (i->kind != type_kind::Interface)
                fail("'" + aType.qualifiedName + "' cannot " + (aType.kind == type_kind::Interface ? "extend" : "implement") + " '" +
                    i->qualifiedName + "': it is not an interface");
            else
                aType.interfaces.push_back(i);
        }
        // (no type is its own base)
        std::set<type_info const*> seen{ &aType };
        for (auto* b = aType.base; b != nullptr; b = b->base)
            if (!seen.insert(b).second)
            {
                fail("'" + aType.qualifiedName + "' is its own base class");
                aType.base = nullptr;
                break;
            }
        std::function<bool(type_info const&)> cyclic = [&](type_info const& aInterface)
        {
            for (auto* i : aInterface.interfaces)
                if (i == &aType || (seen.insert(i).second && cyclic(*i)))
                    return true;
            return false;
        };
        seen = { &aType };
        if (aType.kind == type_kind::Interface && cyclic(aType))
        {
            fail("interface '" + aType.qualifiedName + "' extends itself");
            aType.interfaces.clear();
        }
        // (members' types)
        for (auto& f : aType.fields)
        {
            f.valueType = find_type(f.typeText, f.scope);
            if (f.valueType.builtin == type::UNKNOWN)
                fail("type '" + f.typeText + "' of field '" + aType.qualifiedName + "::" + f.name + "' not found");
            else if (!memory_access_of(f.valueType.builtin))
                fail("field '" + aType.qualifiedName + "::" + f.name + "' cannot be of type '" + f.typeText + "'");
        }
        for (auto& m : aType.methods)
        {
            for (auto& p : m.parameters)
            {
                p.valueType = find_type(p.typeText, aType.qualifiedName);
                if (p.valueType.builtin == type::UNKNOWN)
                    fail("type '" + p.typeText + "' of parameter '" + p.name + "' of '" + m.function + "' not found");
            }
            if (!m.resultText.empty())
            {
                m.result = find_type(m.resultText, aType.qualifiedName);
                if (m.result.builtin == type::UNKNOWN)
                    fail("result type '" + m.resultText + "' of '" + m.function + "' not found");
            }
        }
    }

    void compiler::lay_out(type_info& aType)
    {
        if (aType.laidOut)
            return;
        if (aType.layingOut)
        {
            if (!aType.error)
                aType.error = "struct '" + aType.qualifiedName + "' contains itself";
            return;
        }
        aType.layingOut = true;
        std::uint32_t offset = 4u; // (its type's id)
        if (aType.base != nullptr)
        {
            lay_out(*aType.base);
            aType.layout = aType.base->layout;
            offset = aType.base->size;
        }
        for (auto& f : aType.fields)
        {
            // (a struct that is a field is the field's own object: a struct cannot contain itself)
            if (f.valueType.is_object() && f.valueType.object->kind == type_kind::Struct)
            {
                lay_out(*f.valueType.object);
                if (f.valueType.object->error && !aType.error)
                    aType.error = *f.valueType.object->error;
            }
            auto const size = size_of(f.valueType.builtin);
            offset = (offset + size - 1u) / size * size;
            f.offset = offset;
            offset += size;
            aType.layout.push_back(&f);
        }
        aType.size = (offset + 7u) / 8u * 8u;
        aType.layingOut = false;
        aType.laidOut = true;
    }

    void compiler::check_methods(type_info& aType)
    {
        auto fail = [&](std::string const& aError)
        {
            if (!aType.error)
                aType.error = aError;
        };
        auto same_parameters = [](method_info const& aLhs, method_info const& aRhs)
        {
            if (aLhs.parameters.size() != aRhs.parameters.size())
                return false;
            for (std::size_t i = 0u; i < aLhs.parameters.size(); ++i)
                if (aLhs.parameters[i].valueType != aRhs.parameters[i].valueType || aLhs.parameters[i].direction != aRhs.parameters[i].direction)
                    return false;
            return true;
        };
        for (auto& m : aType.methods)
        {
            for (auto const& other : aType.methods)
                if (&other != &m && other.name == m.name && same_parameters(other, m))
                {
                    fail("redefinition of '" + m.function + "'");
                    break;
                }
            for (auto const& f : aType.layout)
                if (f->name == m.name)
                    fail("'" + aType.qualifiedName + "' has a field and a method named '" + m.name + "'");
            if (m.isConstructor)
            {
                if (aType.kind == type_kind::Interface)
                    fail("interface '" + aType.qualifiedName + "' cannot have a constructor");
                if (m.isFinal || m.isOverride || m.isMutable)
                    fail("constructor of '" + aType.qualifiedName + "' cannot be final, override or mutable");
                continue;
            }
            // (the method that it overrides (of a base class) and those that it implements (of interfaces))
            method_info const* overridden = nullptr;
            for (auto* b = aType.base; b != nullptr && overridden == nullptr; b = b->base)
                for (auto const& bm : b->methods)
                    if (!bm.isConstructor && bm.name == m.name && same_parameters(bm, m))
                    {
                        overridden = &bm;
                        break;
                    }
            bool implemented = false;
            std::function<void(type_info const&)> interfaces = [&](type_info const& aInterface)
            {
                for (auto const& im : aInterface.methods)
                    if (im.name == m.name && same_parameters(im, m))
                    {
                        implemented = true;
                        if (im.result != m.result)
                            fail("'" + m.function + "' implements '" + im.function + "' but its result is not the same type");
                        if (im.procedure != m.procedure)
                            fail("'" + m.function + "' implements '" + im.function + "' but is not a" + (im.procedure ? " procedure" : " function"));
                        if (!im.isMutable && m.isMutable)
                            fail("'" + m.function + "' implements '" + im.function + "' which is not mutable");
                    }
                for (auto* i : aInterface.interfaces)
                    interfaces(*i);
            };
            if (aType.kind != type_kind::Interface)
                for (auto* x = &aType; x != nullptr; x = x->base)
                    for (auto* i : x->interfaces)
                        interfaces(*i);
            if (overridden != nullptr)
            {
                if (overridden->isFinal)
                    fail("'" + m.function + "' cannot override '" + overridden->function + "': it is final");
                if (overridden->result != m.result)
                    fail("'" + m.function + "' overrides '" + overridden->function + "' but its result is not the same type");
                if (overridden->procedure != m.procedure)
                    fail("'" + m.function + "' overrides '" + overridden->function + "' but is not a" + (overridden->procedure ? " procedure" : " function"));
                if (overridden->isMutable != m.isMutable)
                    fail("'" + m.function + "' overrides '" + overridden->function + "' but " + (m.isMutable ? "is" : "is not") + " mutable");
            }
            if (m.isOverride && overridden == nullptr && !implemented)
                fail("'" + m.function + "' is declared override but overrides no method");
            if ((m.isFinal || m.isOverride) && aType.kind == type_kind::Struct && overridden == nullptr && !implemented)
                fail("'" + m.function + "' of struct '" + aType.qualifiedName + "' cannot be final or override");
        }
        // (a struct or class implements its interfaces' methods)
        if (aType.kind != type_kind::Interface)
        {
            std::function<void(type_info const&)> required = [&](type_info const& aInterface)
            {
                for (auto const& im : aInterface.methods)
                    if (implementation(aType, im) == nullptr)
                        fail("'" + aType.qualifiedName + "' does not implement '" + im.function + "'");
                for (auto* i : aInterface.interfaces)
                    required(*i);
            };
            for (auto* x = &aType; x != nullptr; x = x->base)
                for (auto* i : x->interfaces)
                    required(*i);
        }
    }

    value_type compiler::find_type(std::string const& aText, std::string const& aScope, std::string_view aSource) const
    {
        if (auto const b = builtin_type(aText))
            return value_type{ *b };
        if (auto* const t = find_object_type(aText, aScope))
        {
            if (t->error && !aSource.empty())
                error(aSource, *t->error);
            return value_type{ type::Pointer, t };
        }
        return value_type{};
    }

    type_info* compiler::find_object_type(std::string const& aText, std::string const& aScope) const
    {
        auto lookup = [&](std::string const& aName) -> type_info*
        {
            if (auto const t = iTypeNames.find(aName); t != iTypeNames.end())
                return t->second;
            if (auto const a = iAliases.find(aName); a != iAliases.end())
                return resolve_alias(const_cast<alias_info&>(a->second));
            return nullptr;
        };
        if (aText.starts_with("::"))
            return lookup(aText.substr(2u));
        std::string scope = aScope;
        for (;;)
        {
            if (auto* const t = lookup(scope.empty() ? aText : scope + "::" + aText))
                return t;
            if (scope.empty())
                return nullptr;
            auto const separator = scope.rfind("::");
            scope = separator == std::string::npos ? std::string{} : scope.substr(0u, separator);
        }
    }

    std::string compiler::scope_name() const
    {
        return strip_scope(generator().current_scope().qualified_name().to_std_string());
    }

    bool compiler::derives(type_info const& aType, type_info const& aBase) const
    {
        for (auto const* t = &aType; t != nullptr; t = t->base)
            if (t == &aBase)
                return true;
        return false;
    }

    bool compiler::implements(type_info const& aType, type_info const& aInterface) const
    {
        std::function<bool(type_info const&)> extends = [&](type_info const& aExtending)
        {
            if (&aExtending == &aInterface)
                return true;
            for (auto* i : aExtending.interfaces)
                if (extends(*i))
                    return true;
            return false;
        };
        if (aType.kind == type_kind::Interface)
            return extends(aType);
        for (auto const* t = &aType; t != nullptr; t = t->base)
            for (auto* i : t->interfaces)
                if (extends(*i))
                    return true;
        return false;
    }

    bool compiler::assignable(value_type const& aFrom, value_type const& aTo) const
    {
        if (!aTo.is_object())
            return !aFrom.is_object();
        if (!aFrom.is_object() || aFrom.object == nullptr || aTo.object == nullptr)
            return false;
        if (aFrom.object == aTo.object)
            return true;
        if (aTo.object->kind == type_kind::Class)
            return aFrom.object->kind == type_kind::Class && derives(*aFrom.object, *aTo.object);
        if (aTo.object->kind == type_kind::Interface)
            return implements(*aFrom.object, *aTo.object);
        return false;
    }

    std::vector<type_info*> compiler::concrete_types(type_info& aType) const
    {
        std::vector<type_info*> result;
        if (aType.kind == type_kind::Struct)
        {
            result.push_back(&aType);
            return result;
        }
        for (auto& t : iTypes)
            if (t.kind != type_kind::Interface && !t.error &&
                (aType.kind == type_kind::Interface ? implements(t, aType) : derives(t, aType)))
                result.push_back(const_cast<type_info*>(&t));
        return result;
    }

    field_info* compiler::find_field(type_info& aType, std::string const& aName) const
    {
        for (auto* t = &aType; t != nullptr; t = t->base)
            for (auto& f : t->fields)
                if (f.name == aName)
                    return &f;
        return nullptr;
    }

    std::vector<method_info*> compiler::find_methods(type_info& aType, std::string const& aName) const
    {
        std::vector<method_info*> result;
        auto add = [&](method_info& aMethod)
        {
            for (auto const* existing : result)
            {
                bool same = existing->parameters.size() == aMethod.parameters.size();
                for (std::size_t i = 0u; same && i < aMethod.parameters.size(); ++i)
                    same = existing->parameters[i].valueType == aMethod.parameters[i].valueType;
                if (same)
                    return; // (hidden by a method of a derived type)
            }
            result.push_back(&aMethod);
        };
        if (aType.kind == type_kind::Interface)
        {
            std::set<type_info*> seen;
            std::function<void(type_info&)> visit = [&](type_info& aInterface)
            {
                if (!seen.insert(&aInterface).second)
                    return;
                for (auto& m : aInterface.methods)
                    if (m.name == aName)
                        add(m);
                for (auto* i : aInterface.interfaces)
                    visit(*i);
            };
            visit(aType);
            return result;
        }
        for (auto* t = &aType; t != nullptr; t = t->base)
            for (auto& m : t->methods)
                if (m.name == aName && !m.isConstructor)
                    add(m);
        return result;
    }

    method_info* compiler::implementation(type_info& aType, method_info const& aMethod) const
    {
        for (auto* t = &aType; t != nullptr; t = t->base)
            for (auto& m : t->methods)
            {
                if (m.isConstructor || m.isAbstract || m.name != aMethod.name || m.parameters.size() != aMethod.parameters.size())
                    continue;
                bool same = true;
                for (std::size_t i = 0u; same && i < m.parameters.size(); ++i)
                    same = m.parameters[i].valueType == aMethod.parameters[i].valueType && m.parameters[i].direction == aMethod.parameters[i].direction;
                if (same)
                    return &m;
            }
        return nullptr;
    }

    type compiler::declared_result(method_info const& aMethod) const
    {
        // (as the declaration pass knows it)
        if (aMethod.resultText.empty())
            return type::Void;
        return builtin_type(aMethod.resultText).value_or(type::Pointer);
    }

    std::uint32_t compiler::size_of(type aType) const
    {
        switch (aType)
        {
        case type::Boolean:
        case type::I8:
        case type::U8: return 1u;
        case type::I16:
        case type::U16: return 2u;
        case type::I64:
        case type::U64:
        case type::F64: return 8u;
        default: return 4u;
        }
    }

    // --- functions and methods

    bool compiler::complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource)
    {
        auto const source = aSource.to_std_string_view();
        auto const signature = parse_signature(source);
        // (the keywords that precede it: fn or proc and its access)
        bool procedure = false;
        access_kind access = access_kind::Public;
        if (!iFragments.empty())
        {
            auto const text = iFragments.back()->source().to_std_string_view();
            if (source.data() >= text.data() && source.data() <= text.data() + text.size())
            {
                auto p = static_cast<std::size_t>(source.data() - text.data());
                for (;;)
                {
                    while (p > 0u && std::isspace(static_cast<unsigned char>(text[p - 1u])))
                        --p;
                    auto const end = p;
                    while (p > 0u && is_word_char(text[p - 1u]))
                        --p;
                    auto const word = text.substr(p, end - p);
                    if (word == "proc")
                        procedure = true;
                    else if (word == "private")
                        access = access_kind::Private;
                    else if (word == "protected")
                        access = access_kind::Protected;
                    else if (word != "fn" && word != "def" && word != "public")
                        break;
                    if (word.empty())
                        break;
                }
            }
        }
        // (the locals of a procedure: those that follow its signature)
        std::vector<parameter_info> locals;
        if (procedure && !iFragments.empty())
        {
            auto const text = iFragments.back()->source().to_std_string_view();
            auto const start = static_cast<std::size_t>(source.data() + source.size() - text.data());
            if (start <= text.size())
            {
                auto const end = text.find('{', start);
                auto const localsText = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
                auto const tokens = tokenize(localsText);
                std::size_t i = 0u;
                parse_parameters(tokens, i, locals, '{');
            }
        }
        auto const scope = scope_name();
        auto const functionName = (scope.empty() ? std::string{} : scope + "::") + aSignature.name().to_std_string();
        type_info* object = nullptr;
        method_info* method = nullptr;
        if (!iTypeStack.empty() && generator().current_scope().type() == scope_type::Class)
        {
            object = iTypeStack.back();
            if (declaration_pass())
            {
                method_info m;
                m.name = signature.name;
                m.function = functionName;
                m.parameters = signature.parameters;
                m.resultText = signature.resultText.value_or(std::string{});
                m.procedure = procedure;
                m.isMutable = signature.isMutable;
                m.isFinal = signature.isFinal;
                m.isOverride = signature.isOverride;
                m.isConstructor = (m.name == "this");
                if (m.isConstructor && (procedure || signature.resultText))
                    error(source, "a constructor (this) is a function without a result");
                if (!m.isConstructor && !procedure && !signature.resultText)
                    error(source, "function '" + m.name + "' must have a result (-> type) (a constructor is named this)");
                m.access = access;
                m.owner = object;
                if (object->kind == type_kind::Interface)
                    error(source, "interface '" + object->qualifiedName + "' cannot define methods");
                object->methods.push_back(m);
            }
            if (object->methodOrdinal >= object->methods.size())
                throw std::logic_error("neoscript: method definitions changed");
            method = &object->methods[object->methodOrdinal++];
            // (a method's first parameter is its object)
            function_parameters parameters;
            function_parameter self;
            self.set_parameter_name(neolib::string_view{ "this" });
            self.set_parameter_type(type::Pointer);
            parameters.push_back(self);
            if (aSignature.parameters().has_value())
                for (auto const& p : aSignature.parameters().value())
                    parameters.push_back(function_parameter{ p });
            aSignature.set_parameters(parameters);
        }
        else if (signature.isMutable || signature.isFinal || signature.isOverride)
            error(source, "'" + functionName + "' is not a method: it cannot be mutable, final or override");
        if (declaration_pass())
        {
            function_record record{ functionName, functionName, signature.parameters, signature.resultText.value_or(std::string{}), scope, object };
            if (object != nullptr)
                record.parameters.insert(record.parameters.begin(), parameter_info{ "this", object->qualifiedName });
            iFunctionRecords[functionName].push_back(record);
        }
        else
        {
            // (its function's name: distinguished from those of its overloads that the compiler would not)
            auto const definition = iDefinitions[functionName]++;
            std::string distinguished = functionName;
            if (method != nullptr)
                distinguished = method->function;
            else if (auto const records = iFunctionRecords.find(functionName); records != iFunctionRecords.end() && definition < records->second.size())
                distinguished = records->second[definition].function;
            if (distinguished != functionName)
            {
                aSignature.set_name(neolib::string{ distinguished.substr(scope.empty() ? 0u : scope.size() + 2u) });
                iFrameNames[static_cast<std::uint32_t>(generator().function_count())] = functionName;
            }
        }
        // (the function's context: its object and the types of its parameters and locals)
        function_context context{ object, method };
        if (object != nullptr)
            context.variables["this"] = value_type{ type::Pointer, object };
        if (!declaration_pass())
        {
            auto resolve = [&](parameter_info const& aParameter)
            {
                auto const t = find_type(aParameter.typeText, scope, source);
                if (t.builtin == type::UNKNOWN)
                    error(source, "type '" + aParameter.typeText + "' not found");
                context.variables[aParameter.name] = t;
            };
            for (auto const& p : signature.parameters)
                resolve(p);
            for (auto const& l : locals)
            {
                resolve(l);
                // (a struct is a value: one that is a local of a procedure is made in its body (where its constructor can be called))
                auto const& t = context.variables[l.name];
                if (t.is_object() && t.object->kind == type_kind::Struct)
                    error(source, "local '" + l.name + "' is a struct: declare it in the body of '" + functionName + "' (" + l.typeText + " " + 
                        l.name + ";)");
            }
            if (signature.resultText && find_type(*signature.resultText, scope, source).builtin == type::UNKNOWN)
                error(source, "type '" + *signature.resultText + "' not found");
        }
        iContexts[static_cast<std::uint32_t>(generator().function_count())] = std::move(context);
        return false;
    }

    compiler::function_context* compiler::current_context()
    {
        if (!in_function())
            return nullptr;
        auto const existing = iContexts.find(current_function());
        return existing != iContexts.end() ? &existing->second : nullptr;
    }

    std::uint32_t compiler::context_function() const
    {
        return in_function() ? current_function() : 0xFFFFFFFFu;
    }

    // --- values

    void compiler::annotate(fragment const& aValue, value_type const& aType, bool aFresh, bool aViaThis)
    {
        annotation a;
        a.function = context_function();
        a.code.assign(aValue.code().begin(), aValue.code().end());
        for (auto const& r : aValue.relocations())
            a.relocations.emplace_back(r.first(), r.second());
        a.valueType = aType;
        a.fresh = aFresh;
        a.viaThis = aViaThis;
        iAnnotations.push_back(std::move(a));
    }

    compiler::annotation const* compiler::annotation_of(i_fragment const& aValue) const
    {
        auto const function = context_function();
        for (auto a = iAnnotations.rbegin(); a != iAnnotations.rend(); ++a)
        {
            if (a->function != function || a->code.size() != aValue.code().size() || a->relocations.size() != aValue.relocations().size())
                continue;
            if (!std::equal(a->code.begin(), a->code.end(), aValue.code().begin()))
                continue;
            bool same = true;
            for (std::size_t i = 0u; same && i < a->relocations.size(); ++i)
                same = a->relocations[i].first == aValue.relocations()[i].first() && a->relocations[i].second == aValue.relocations()[i].second();
            if (same)
                return &*a;
        }
        return nullptr;
    }

    value_type compiler::variable_type(i_symbol_table_entry const& aSymbol, std::string const& aName)
    {
        if (aSymbol.type() != type::Pointer)
            return value_type{ aSymbol.type() };
        if (auto const v = iVariables.find(&aSymbol); v != iVariables.end())
            return v->second;
        if (auto* const context = current_context())
        {
            if (!aName.empty())
            {
                if (auto const v = context->variables.find(aName); v != context->variables.end())
                    return v->second;
            }
            else
                for (auto const& v : context->variables)
                    if (find_symbol(current_scope(), neolib::string_view{ v.first }) == &aSymbol)
                        return v.second;
        }
        return value_type{ type::Pointer };
    }

    value_type compiler::type_of(i_operand_type const& aOperand, bool* aFresh, bool* aViaThis)
    {
        if (aFresh != nullptr)
            *aFresh = false;
        if (aViaThis != nullptr)
            *aViaThis = false;
        if (aOperand.holds_alternative<i_symbol_reference>())
        {
            auto const& reference = aOperand.get<i_symbol_reference>();
            auto const name = reference.name().to_std_string();
            auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ name });
            if (symbol == nullptr)
                return value_type{};
            if (aViaThis != nullptr)
                *aViaThis = (name == "this");
            return variable_type(*symbol, name);
        }
        if (aOperand.holds_alternative<i_fragment>())
        {
            auto const& value = aOperand.get<i_fragment>();
            if (value.value_type() != type::Pointer)
                return value_type{ value.value_type() };
            if (value.lvalue() != nullptr)
            {
                if (aViaThis != nullptr)
                    *aViaThis = (find_symbol(current_scope(), neolib::string_view{ "this" }) == value.lvalue());
                return variable_type(*value.lvalue(), {});
            }
            if (auto const* a = annotation_of(value))
            {
                if (aFresh != nullptr)
                    *aFresh = a->fresh;
                if (aViaThis != nullptr)
                    *aViaThis = a->viaThis;
                return a->valueType;
            }
            return value_type{ type::Pointer };
        }
        return value_type{ static_cast<type>(aOperand.get<i_data_type>().index()) };
    }

    std::string type_text(value_type const& aType)
    {
        if (aType.is_object())
            return aType.object != nullptr ? "'" + aType.object->qualifiedName + "'" : "an object";
        return type_name(aType.builtin);
    }

    void compiler::define_variable(std::string const& aName, value_type const& aType, std::string_view aSource)
    {
        generator().define_symbol(neolib::string{ aName }, aType.builtin, neolib::string_view{ aSource });
        if (auto const* symbol = find_symbol(current_scope(), neolib::string_view{ aName }, true))
            iVariables[symbol] = aType;
    }

    fragment compiler::this_value(std::string_view aSource)
    {
        auto* const context = current_context();
        auto const* self = find_symbol(current_scope(), neolib::string_view{ "this" });
        if (context == nullptr || context->object == nullptr || self == nullptr)
            error(aSource, "'this' is not available outside a method");
        fragment result;
        result.valueType = type::Pointer;
        put_local_get(result, self->index());
        annotate(result, value_type{ type::Pointer, context->object }, false, true);
        return result;
    }

    fragment compiler::dummy(type aType, fragment_kind aKind, std::string_view aSource)
    {
        auto result = make_fragment(aKind, aType, neolib::string_view{ aSource });
        return result;
    }

    // (a value as it is stored as a value of a type: converted (and a struct copied, unless it is new))
    fragment compiler::stored(fragment aValue, value_type const& aValueType, bool aFresh, value_type const& aTarget, std::string_view aSource)
    {
        if (declaration_pass())
        {
            aValue.valueType = aTarget.builtin;
            return aValue;
        }
        if (!aTarget.is_object())
        {
            if (aValueType.is_object())
                error(aSource, "cannot convert " + type_text(aValueType) + " to " + type_text(aTarget));
            coerce(aValue, aTarget.builtin);
            return aValue;
        }
        if (!aValueType.is_object())
            error(aSource, "cannot convert " + type_text(aValueType) + " to " + type_text(aTarget));
        if (aValueType.object == nullptr)
            error(aSource, "the type of the object is not known");
        if (!assignable(aValueType, aTarget))
            error(aSource, "cannot convert " + type_text(aValueType) + " to " + type_text(aTarget));
        if (aValueType.object->kind == type_kind::Struct && !aFresh)
            put_call(aValue, copy_function(*aValueType.object));
        aValue.valueType = type::Pointer;
        return aValue;
    }

    fragment compiler::zero(value_type const& aType)
    {
        fragment result;
        result.valueType = aType.builtin;
        auto& code = result.fragmentCode.as_std_vector();
        switch (aType.builtin)
        {
        case type::I64:
        case type::U64:
            put_op(code, opcode::I64Const);
            put_s64(code, 0);
            break;
        case type::F32:
            put_op(code, opcode::F32Const);
            put_raw(code, 0.0f);
            break;
        case type::F64:
            put_op(code, opcode::F64Const);
            put_raw(code, 0.0);
            break;
        case type::String:
            put_i32(result, static_cast<std::int32_t>(string_literal("")));
            break;
        default:
            put_i32(result, 0);
            break;
        }
        return result;
    }

    // (a field's initial value: a literal)
    fragment compiler::constant(field_info const& aField, std::string_view aSource)
    {
        auto const t = aField.valueType.builtin;
        auto const literal = type_of(*aField.initializer).builtin;
        bool const valid = !aField.valueType.is_object() && t != type::Object && 
            ((t == type::String) == (literal == type::String)) && ((t == type::Boolean) == (literal == type::Boolean)) &&
            !(literal == type::Fbig && !is_float(t));
        if (!valid)
            error(aSource, "the initial value of field '" + aField.owner->qualifiedName + "::" + aField.name + "' is not a literal of its type (" + 
                aField.typeText + ")");
        return materialize(*aField.initializer, t);
    }

    void compiler::load(fragment& aCode, field_info const& aField)
    {
        auto const access = *memory_access_of(aField.valueType.builtin);
        auto& code = aCode.fragmentCode.as_std_vector();
        put_op(code, access.load);
        put_u32(code, access.align);
        put_u32(code, aField.offset);
    }

    void compiler::store(fragment& aCode, field_info const& aField)
    {
        auto const access = *memory_access_of(aField.valueType.builtin);
        auto& code = aCode.fragmentCode.as_std_vector();
        put_op(code, access.store);
        put_u32(code, access.align);
        put_u32(code, aField.offset);
    }

    std::uint32_t compiler::allocate_function()
    {
        // (the VM's: memory for an object (zeroed))
        return runtime_function("neos::allocate", { type::I32 }, type::I32);
    }

    std::vector<type> compiler::parameter_types(method_info const& aMethod) const
    {
        std::vector<type> result{ type::Pointer };
        for (auto const& p : aMethod.parameters)
            result.push_back(p.valueType.builtin);
        return result;
    }

    // (a struct's copy: a new object with its fields' values (a struct that is a field copied too))
    std::uint32_t compiler::copy_function(type_info& aType)
    {
        if (auto const existing = iCopyFunctions.find(&aType); existing != iCopyFunctions.end())
            return existing->second;
        function_signature signature;
        signature.set_name(neolib::string{ aType.qualifiedName + "::$copy" });
        function_parameters parameters;
        function_parameter source;
        source.set_parameter_name(neolib::string_view{ "source" });
        source.set_parameter_type(type::Pointer);
        parameters.push_back(source);
        signature.set_parameters(parameters);
        signature.set_return_type(type::Pointer);
        neolib::vector<type> locals;
        locals.push_back(type::Pointer);
        auto const id = generator().define_function(signature.name(), signature, locals, neolib::string{}, 0u);
        iCopyFunctions[&aType] = id;
        fragment body;
        put_i32(body, static_cast<std::int32_t>(aType.size));
        put_call(body, allocate_function());
        put_local_set(body, 1u);
        put_local_get(body, 1u);
        put_local_get(body, 0u);
        put_op(body.fragmentCode.as_std_vector(), opcode::I32LoadMem);
        put_u32(body.fragmentCode.as_std_vector(), 2u);
        put_u32(body.fragmentCode.as_std_vector(), 0u);
        put_op(body.fragmentCode.as_std_vector(), opcode::I32StoreMem);
        put_u32(body.fragmentCode.as_std_vector(), 2u);
        put_u32(body.fragmentCode.as_std_vector(), 0u);
        for (auto const* f : aType.layout)
        {
            put_local_get(body, 1u);
            put_local_get(body, 0u);
            load(body, *f);
            if (f->valueType.is_object() && f->valueType.object->kind == type_kind::Struct)
                put_call(body, copy_function(*f->valueType.object));
            store(body, *f);
        }
        put_local_get(body, 1u);
        put_op(body.fragmentCode.as_std_vector(), opcode::Return);
        generator().set_function_body(id, body);
        return id;
    }

    // (a call of a virtual method: the method of the object's type)
    std::uint32_t compiler::dispatch_function(type_info& aType, method_info& aMethod)
    {
        auto const key = std::make_pair(&aType, &aMethod);
        if (auto const existing = iDispatchFunctions.find(key); existing != iDispatchFunctions.end())
            return existing->second;
        function_signature signature;
        signature.set_name(neolib::string{ aMethod.function + "$dispatch$" + aType.qualifiedName });
        function_parameters parameters;
        function_parameter self;
        self.set_parameter_name(neolib::string_view{ "this" });
        self.set_parameter_type(type::Pointer);
        parameters.push_back(self);
        std::uint32_t inputs = 1u;
        for (auto const& p : aMethod.parameters)
        {
            function_parameter parameter;
            parameter.set_parameter_name(neolib::string_view{ p.name });
            parameter.set_parameter_type(p.valueType.builtin);
            parameter.set_direction(p.direction);
            parameters.push_back(parameter);
            if (p.direction != parameter_direction::Out)
                ++inputs;
        }
        signature.set_parameters(parameters);
        signature.set_return_type(aMethod.result.builtin);
        neolib::vector<type> locals;
        locals.push_back(type::I32);
        auto const id = generator().define_function(signature.name(), signature, locals, neolib::string{}, 0u);
        iDispatchFunctions[key] = id;
        // (the types whose objects it can be called with, by the method that they call)
        std::vector<std::pair<method_info*, std::vector<std::uint32_t>>> targets;
        for (auto* t : concrete_types(aType))
            if (auto* const m = implementation(*t, aMethod))
            {
                auto existing = std::find_if(targets.begin(), targets.end(), [&](auto const& e) { return e.first == m; });
                if (existing == targets.end())
                    existing = targets.insert(targets.end(), std::make_pair(m, std::vector<std::uint32_t>{}));
                existing->second.push_back(t->id);
            }
        fragment body;
        auto& code = body.fragmentCode.as_std_vector();
        put_local_get(body, 0u);
        put_op(code, opcode::I32LoadMem);
        put_u32(code, 2u);
        put_u32(code, 0u);
        put_local_set(body, inputs);
        auto const types = parameter_types(aMethod);
        for (auto const& target : targets)
        {
            for (std::size_t i = 0u; i < target.second.size(); ++i)
            {
                put_local_get(body, inputs);
                put_i32(body, static_cast<std::int32_t>(target.second[i]));
                put_op(code, opcode::I32Eq);
                if (i > 0u)
                    put_op(code, opcode::I32Ior);
            }
            put_op(code, opcode::If);
            code.push_back(0x40u);
            for (std::uint32_t i = 0u; i < inputs; ++i)
                put_local_get(body, i);
            put_call(body, generator().deferred_call(neolib::string{ target.first->function }, neolib::vector<type>{ types.begin(), types.end() }));
            put_op(code, opcode::Return);
            put_op(code, opcode::End);
        }
        put_i32(body, static_cast<std::int32_t>(string_literal("'" + aMethod.function + "' called with a null reference")));
        put_call(body, runtime_function("neos::raise", { type::String }, type::Void));
        put_op(code, opcode::Unreachable);
        generator().set_function_body(id, body);
        return id;
    }

    void compiler::check_access(access_kind aAccess, type_info& aOwner, std::string const& aName, std::string_view aSource)
    {
        if (aAccess == access_kind::Public)
            return;
        auto* const context = current_context();
        auto* const from = context != nullptr ? context->object : nullptr;
        bool const allowed = from != nullptr && (from == &aOwner || (aAccess == access_kind::Protected && derives(*from, aOwner)));
        if (!allowed)
            error(aSource, "'" + aOwner.qualifiedName + "::" + aName + "' is " + access_name(aAccess));
    }

    void compiler::check_change(bool aViaThis, std::string const& aWhat, std::string_view aSource)
    {
        if (!aViaThis)
            return;
        auto* const context = current_context();
        if (context == nullptr || context->method == nullptr)
            return;
        auto const& m = *context->method;
        if (!m.procedure && !m.isMutable && !m.isConstructor)
            error(aSource, "cannot " + aWhat + ": '" + m.function + "' is a function that is not mutable");
    }

    std::int32_t compiler::score(method_info const& aMethod, std::vector<value_type> const& aArguments) const
    {
        return score(aMethod.parameters, aArguments);
    }

    std::int32_t compiler::score(std::vector<parameter_info> const& aParameters, std::vector<value_type> const& aArguments) const
    {
        if (aParameters.size() != aArguments.size())
            return -1;
        std::int32_t result = 0;
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
        {
            auto const& p = aParameters[i].valueType;
            auto const& a = aArguments[i];
            if (p.is_object() || a.is_object())
            {
                if (!p.is_object() || !a.is_object())
                    return -1;
                if (p.object == a.object)
                    result += 3;
                else if (assignable(a, p))
                    result += 2;
                else
                    return -1;
            }
            else if (p.builtin == a.builtin)
                result += 3;
            else if (p.builtin == type::Object || a.builtin == type::Object)
                ;
            else if (p.builtin == type::String || a.builtin == type::String)
                return -1;
            else if (is_numeric(p.builtin) && is_numeric(a.builtin))
                result += (is_float(p.builtin) == is_float(a.builtin) && (p.builtin == type::Boolean) == (a.builtin == type::Boolean)) ? 2 : 1;
            else
                return -1;
        }
        return result;
    }

    method_info* compiler::choose(std::vector<method_info*> const& aCandidates, std::vector<value_type> const& aArguments) const
    {
        method_info* best = nullptr;
        std::int32_t bestScore = -1;
        for (auto* m : aCandidates)
        {
            auto const s = score(*m, aArguments);
            if (s > bestScore)
            {
                best = m;
                bestScore = s;
            }
        }
        return best;
    }

    void compiler::initialize_object(fragment& aCode, type_info& aType, std::uint32_t aObject, std::string_view aSource)
    {
        put_i32(aCode, static_cast<std::int32_t>(aType.size));
        put_call(aCode, allocate_function());
        put_local_set(aCode, aObject);
        put_local_get(aCode, aObject);
        put_i32(aCode, static_cast<std::int32_t>(aType.id));
        auto& code = aCode.fragmentCode.as_std_vector();
        put_op(code, opcode::I32StoreMem);
        put_u32(code, 2u);
        put_u32(code, 0u);
        for (auto const* f : aType.layout)
        {
            if (f->valueType.is_object() && f->valueType.object->kind == type_kind::Struct)
            {
                put_local_get(aCode, aObject);
                append(aCode, construct_object(*f->valueType.object, {}, {}, aSource));
                store(aCode, *f);
            }
            else if (f->initializer)
            {
                put_local_get(aCode, aObject);
                append(aCode, constant(*f, aSource));
                store(aCode, *f);
            }
            else if (f->valueType.builtin == type::String)
            {
                put_local_get(aCode, aObject);
                append(aCode, zero(f->valueType));
                store(aCode, *f);
            }
        }
    }

    fragment compiler::construct_object(type_info& aType, std::vector<fragment> const& aArguments, std::vector<value_type> const& aArgumentTypes, std::string_view aSource)
    {
        if (aType.error)
            error(aSource, *aType.error);
        if (aType.kind == type_kind::Interface)
            error(aSource, "cannot make an object of interface '" + aType.qualifiedName + "'");
        if (declaration_pass())
            return dummy(type::Pointer);
        std::vector<method_info*> constructors;
        for (auto& m : aType.methods)
            if (m.isConstructor)
                constructors.push_back(&m);
        method_info* constructor = nullptr;
        if (!constructors.empty())
        {
            constructor = choose(constructors, aArgumentTypes);
            if (constructor == nullptr)
                error(aSource, "no constructor of '" + aType.qualifiedName + "' takes " + (aArguments.empty() ? std::string{ "no arguments" } :
                    "these arguments (" + std::to_string(aArguments.size()) + ")"));
            check_access(constructor->access, aType, "this", aSource);
        }
        else if (!aArguments.empty())
            error(aSource, "'" + aType.qualifiedName + "' has no constructor that takes arguments");
        auto const object = temporary(type::Pointer);
        fragment result;
        result.valueType = type::Pointer;
        initialize_object(result, aType, object, aSource);
        // (its base classes' default constructors (its outermost first), then its constructor)
        std::vector<type_info*> bases;
        for (auto* b = aType.base; b != nullptr; b = b->base)
            bases.insert(bases.begin(), b);
        for (auto* b : bases)
            for (auto& m : b->methods)
                if (m.isConstructor && m.parameters.empty())
                {
                    put_local_get(result, object);
                    put_call(result, generator().deferred_call(neolib::string{ m.function }, neolib::vector<type>{ type::Pointer }));
                }
        if (constructor != nullptr)
        {
            put_local_get(result, object);
            for (std::size_t i = 0u; i < aArguments.size(); ++i)
            {
                auto const& p = constructor->parameters[i];
                if (p.direction != parameter_direction::In)
                    error(aSource, "a constructor's parameters are in parameters");
                append(result, stored(aArguments[i], aArgumentTypes[i], false, p.valueType, aSource));
            }
            auto const types = parameter_types(*constructor);
            put_call(result, generator().deferred_call(neolib::string{ constructor->function }, neolib::vector<type>{ types.begin(), types.end() }));
        }
        put_local_get(result, object);
        annotate(result, value_type{ type::Pointer, &aType }, true);
        return result;
    }

    fragment compiler::call_method(fragment const& aObject, type_info& aType, bool aViaThis, std::string const& aName, std::vector<fragment> aArguments, std::string_view aSource)
    {
        if (aType.error)
            error(aSource, *aType.error);
        std::vector<value_type> argumentTypes;
        std::vector<bool> fresh;
        for (auto const& a : aArguments)
        {
            bool f = false;
            argumentTypes.push_back(type_of(operand_type{ a }, &f));
            fresh.push_back(f);
        }
        auto const candidates = find_methods(aType, aName);
        if (candidates.empty())
        {
            if (find_field(aType, aName) != nullptr)
                error(aSource, "'" + aType.qualifiedName + "::" + aName + "' is a field, not a method");
            error(aSource, "'" + aType.qualifiedName + "' has no method '" + aName + "'");
        }
        auto* const m = choose(candidates, argumentTypes);
        if (m == nullptr)
            error(aSource, "no method '" + aType.qualifiedName + "::" + aName + "' takes these arguments (" + std::to_string(aArguments.size()) + ")");
        check_access(m->access, *m->owner, aName, aSource);
        if (m->isMutable || m->procedure)
            check_change(aViaThis, std::string{ "call " } + (m->procedure ? "procedure '" : "mutable function '") + aName + "'", aSource);
        auto const result = m->result.builtin;
        auto call = make_fragment(result == type::Void ? fragment_kind::Statement : fragment_kind::Value, result,
            result == type::Void ? neolib::string_view{ aSource } : neolib::string_view{});
        if (declaration_pass())
            return call;
        append(call, aObject);
        std::vector<std::pair<i_symbol_table_entry const*, type>> outputs;
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
        {
            auto const& p = m->parameters[i];
            if (p.direction != parameter_direction::In)
            {
                if (aArguments[i].lvalue() == nullptr)
                    error(aSource, "argument " + std::to_string(i + 1u) + " of '" + aName + "' must be a variable");
                if (argumentTypes[i] != p.valueType)
                    error(aSource, "argument " + std::to_string(i + 1u) + " of '" + aName + "' must be a variable of type " + type_text(p.valueType));
                outputs.emplace_back(aArguments[i].lvalue(), p.valueType.builtin);
            }
            if (p.direction != parameter_direction::Out)
                append(call, stored(aArguments[i], argumentTypes[i], fresh[i], p.valueType, aSource));
        }
        // (a virtual method: the method of the object's type)
        std::set<method_info*> implementations;
        for (auto* t : concrete_types(aType))
            if (auto* const im = implementation(*t, *m))
                implementations.insert(im);
        auto const types = parameter_types(*m);
        if (aType.kind != type_kind::Interface && implementations.size() <= 1u)
        {
            auto* const target = implementations.empty() ? m : *implementations.begin();
            put_call(call, generator().deferred_call(neolib::string{ target->function }, neolib::vector<type>{ types.begin(), types.end() }));
        }
        else
            put_call(call, dispatch_function(aType, *m));
        auto& code = call.fragmentCode.as_std_vector();
        for (auto o = outputs.rbegin(); o != outputs.rend(); ++o)
        {
            put_op(code, opcode::LocalSet);
            put_u32(code, o->first->index());
        }
        if (m->result.is_object())
            annotate(call, m->result);
        return call;
    }

    // --- constructs (code)

    i_fragment const* compiler::literal(i_operand_type const& aOperand)
    {
        // (a name within a method that is not a variable but a field of its object: the field)
        if (!aOperand.holds_alternative<i_symbol_reference>())
            return nullptr;
        auto* const context = current_context();
        if (context == nullptr || context->object == nullptr)
            return nullptr;
        auto const& reference = aOperand.get<i_symbol_reference>();
        auto const name = reference.name().to_std_string();
        if (find_symbol(reference.scope(), neolib::string_view{ name }) != nullptr)
            return nullptr;
        auto* const f = find_field(*context->object, name);
        if (f == nullptr)
            return nullptr;
        if (declaration_pass())
        {
            iResult = dummy(f->valueType.builtin);
            return &iResult;
        }
        check_access(f->access, *f->owner, name, {});
        iResult = this_value({});
        load(iResult, *f);
        iResult.valueType = f->valueType.builtin;
        if (f->valueType.is_object())
            annotate(iResult, f->valueType, false, f->valueType.object->kind == type_kind::Struct);
        return &iResult;
    }

    void compiler::member(std::string_view aSource)
    {
        auto const tokens = tokenize(aSource);
        std::string const name{ tokens.at(1).text };
        auto const operand = pop_operand_value();
        bool viaThis = false;
        auto const objectType = type_of(operand, nullptr, &viaThis);
        if (declaration_pass())
        {
            if (objectType.is_object() && objectType.object != nullptr)
                if (auto* const f = find_field(*objectType.object, name))
                {
                    push_operand(operand_type{ dummy(f->valueType.builtin) });
                    return;
                }
            push_operand(operand_type{ dummy(type::I32) });
            return;
        }
        if (!objectType.is_object())
            error(aSource, "'." + name + "': " + type_text(objectType) + " is not an object");
        if (objectType.object == nullptr)
            error(aSource, "'." + name + "': the type of the object is not known");
        auto* const f = find_field(*objectType.object, name);
        if (f == nullptr)
        {
            if (!find_methods(*objectType.object, name).empty())
                error(aSource, "method '" + objectType.object->qualifiedName + "::" + name + "' must be called");
            error(aSource, "'" + objectType.object->qualifiedName + "' has no member '" + name + "'");
        }
        check_access(f->access, *f->owner, name, aSource);
        auto value = materialize(operand);
        value.fragmentLvalue = nullptr;
        value.fragmentKind = fragment_kind::Value;
        load(value, *f);
        value.valueType = f->valueType.builtin;
        if (f->valueType.is_object())
            annotate(value, f->valueType, false, viaThis && f->valueType.object->kind == type_kind::Struct);
        push_operand(operand_type{ value });
    }

    void compiler::method_call(std::string_view aSource)
    {
        auto const tokens = tokenize(aSource);
        std::string const name{ tokens.at(1).text };
        auto arguments = pop_fragments(neolib::string_view{ aSource }, fragment_kind::Argument);
        auto const operand = pop_operand_value();
        bool viaThis = false;
        auto const objectType = type_of(operand, nullptr, &viaThis);
        if (declaration_pass())
        {
            type result = type::I32;
            if (objectType.is_object() && objectType.object != nullptr)
            {
                auto const candidates = find_methods(*objectType.object, name);
                if (!candidates.empty())
                    result = declared_result(*candidates.front());
            }
            push_operand(operand_type{ dummy(result, result == type::Void ? fragment_kind::Statement : fragment_kind::Value,
                result == type::Void ? aSource : std::string_view{}) });
            return;
        }
        if (!objectType.is_object())
            error(aSource, "'." + name + "()': " + type_text(objectType) + " is not an object");
        if (objectType.object == nullptr)
            error(aSource, "'." + name + "()': the type of the object is not known");
        push_operand(operand_type{ call_method(materialize(operand), *objectType.object, viaThis, name, arguments, aSource) });
    }

    // (the function of the program that a call of a name with arguments calls: that of the innermost scope that has one that takes them)
    function_record const* compiler::choose_function(std::string const& aName, std::vector<value_type> const& aArguments) const
    {
        std::vector<std::string> names;
        if (aName.starts_with("::"))
            names.push_back(aName.substr(2u));
        else
            for (std::string scope = scope_name();;)
            {
                names.push_back(scope.empty() ? aName : scope + "::" + aName);
                if (scope.empty())
                    break;
                auto const separator = scope.rfind("::");
                scope = separator == std::string::npos ? std::string{} : scope.substr(0u, separator);
            }
        for (auto const& name : names)
        {
            auto const records = iFunctionRecords.find(name);
            if (records == iFunctionRecords.end())
                continue;
            function_record const* best = nullptr;
            std::int32_t bestScore = -1;
            for (auto const& record : records->second)
            {
                if (record.object != nullptr)
                    continue;
                std::vector<parameter_info> parameters = record.parameters;
                for (auto& p : parameters)
                    p.valueType = find_type(p.typeText, record.scope);
                auto const s = score(parameters, aArguments);
                if (s > bestScore)
                {
                    best = &record;
                    bestScore = s;
                }
            }
            if (best != nullptr)
                return best;
        }
        return nullptr;
    }

    // (a call of a function of the program that takes or returns objects)
    fragment compiler::call_function(function_record const& aFunction, std::vector<fragment> aArguments, std::string_view aSource)
    {
        auto const result = aFunction.resultText.empty() ? value_type{ type::Void } : find_type(aFunction.resultText, aFunction.scope);
        auto call = make_fragment(result.builtin == type::Void ? fragment_kind::Statement : fragment_kind::Value, result.builtin, 
            result.builtin == type::Void ? neolib::string_view{ aSource } : neolib::string_view{});
        std::vector<type> types;
        std::vector<std::pair<i_symbol_table_entry const*, type>> outputs;
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
        {
            auto const& p = aFunction.parameters[i];
            auto const t = find_type(p.typeText, aFunction.scope);
            types.push_back(t.builtin);
            bool fresh = false;
            auto const argumentType = type_of(operand_type{ aArguments[i] }, &fresh);
            if (p.direction != parameter_direction::In)
            {
                if (aArguments[i].lvalue() == nullptr)
                    error(aSource, "argument " + std::to_string(i + 1u) + " of '" + aFunction.name + "' must be a variable");
                if (argumentType != t)
                    error(aSource, "argument " + std::to_string(i + 1u) + " of '" + aFunction.name + "' must be a variable of type " + type_text(t));
                outputs.emplace_back(aArguments[i].lvalue(), t.builtin);
            }
            if (p.direction != parameter_direction::Out)
                append(call, stored(aArguments[i], argumentType, fresh, t, aSource));
        }
        put_call(call, generator().deferred_call(neolib::string{ aFunction.function }, neolib::vector<type>{ types.begin(), types.end() }));
        auto& code = call.fragmentCode.as_std_vector();
        for (auto o = outputs.rbegin(); o != outputs.rend(); ++o)
        {
            put_op(code, opcode::LocalSet);
            put_u32(code, o->first->index());
        }
        if (result.is_object())
            annotate(call, result);
        return call;
    }

    void compiler::call(std::string_view aSource)
    {
        auto const open = aSource.find('(');
        std::string name = trimmed(aSource.substr(0u, open));
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), name.end());
        auto* const context = current_context();
        bool const qualifiedName = name.find("::") != std::string::npos;
        // a method of the object of the method being compiled
        if (!qualifiedName && context != nullptr && context->object != nullptr && !find_methods(*context->object, name).empty())
        {
            auto arguments = pop_fragments(neolib::string_view{ aSource }, fragment_kind::Argument);
            if (declaration_pass())
            {
                auto const result = declared_result(*find_methods(*context->object, name).front());
                push_operand(operand_type{ dummy(result, result == type::Void ? fragment_kind::Statement : fragment_kind::Value,
                    result == type::Void ? aSource : std::string_view{}) });
                return;
            }
            push_operand(operand_type{ call_method(this_value(aSource), *context->object, true, name, arguments, aSource) });
            return;
        }
        // a new object
        if (auto* const t = find_object_type(name, scope_name()))
        {
            auto arguments = pop_fragments(neolib::string_view{ aSource }, fragment_kind::Argument);
            std::vector<value_type> argumentTypes;
            for (auto const& a : arguments)
                argumentTypes.push_back(type_of(operand_type{ a }));
            push_operand(operand_type{ construct_object(*t, arguments, argumentTypes, aSource) });
            return;
        }
        // a function of the program that takes or returns objects: its overload by their types
        std::vector<value_type> argumentTypes;
        if (!declaration_pass())
        {
            auto stack = operands();
            std::size_t first = stack.size();
            while (first > operand_base() && stack[first - 1u].holds_alternative<i_fragment>() && 
                stack[first - 1u].get<i_fragment>().kind() == fragment_kind::Argument &&
                within(stack[first - 1u].get<i_fragment>().source(), neolib::string_view{ aSource }))
                --first;
            for (auto i = first; i < stack.size(); ++i)
                argumentTypes.push_back(type_of(stack[i]));
            if (auto const* function = choose_function(name, argumentTypes))
            {
                bool objects = !function->resultText.empty() && find_type(function->resultText, function->scope).is_object();
                for (auto const& p : function->parameters)
                    objects = objects || find_type(p.typeText, function->scope).is_object();
                for (auto const& a : argumentTypes)
                    objects = objects || a.is_object();
                if (objects)
                {
                    auto arguments = pop_fragments(neolib::string_view{ aSource }, fragment_kind::Argument);
                    push_operand(operand_type{ call_function(*function, arguments, aSource) });
                    return;
                }
            }
            // (a function (the compiler's, e.g. an import): its arguments that are structs are copies)
            for (auto i = first; i < stack.size(); ++i)
            {
                bool fresh = false;
                auto const t = type_of(stack[i], &fresh);
                if (t.is_object() && t.object != nullptr && t.object->kind == type_kind::Struct && !fresh)
                {
                    fragment copy{ stack[i].get<i_fragment>() };
                    put_call(copy, copy_function(*t.object));
                    copy.fragmentLvalue = nullptr;
                    generator().set_operand(i, operand_type{ copy });
                }
            }
        }
        generator().emit_call(neolib::string_view{ name }, neolib::string_view{ aSource });
    }

    void compiler::assignment(std::string_view aSource)
    {
        auto const tokens = tokenize(aSource);
        std::vector<std::string> path;
        for (std::size_t i = 0u; i < tokens.size() && tokens[i].is_word(); i += 2u)
        {
            path.emplace_back(tokens[i].text);
            if (i + 1u >= tokens.size() || !tokens[i + 1u].is("."))
                break;
        }
        auto* const context = current_context();
        auto* const variable = find_symbol(current_scope(), neolib::string_view{ path[0] });
        // a variable
        if (path.size() == 1u && path[0] == "this" && variable != nullptr)
            error(aSource, "cannot assign to 'this'");
        if (path.size() == 1u && variable != nullptr)
        {
            if (!declaration_pass() && variable->type() == type::Pointer)
            {
                auto stack = operands();
                if (stack.size() <= operand_base())
                    error(aSource, "expression expected");
                bool fresh = false;
                auto const valueType = type_of(stack.back(), &fresh);
                auto value = materialize(operand_type{ stack.back() });
                value = stored(value, valueType, fresh, variable_type(*variable, path[0]), aSource);
                generator().set_operand(stack.size() - 1u, operand_type{ value });
            }
            generator().emit_assignment(neolib::string_view{ path[0] }, neolib::string_view{ aSource });
            return;
        }
        // a field: of an object a variable refers to, or of the object of the method being compiled
        auto operand = pop_operand_value();
        if (declaration_pass())
        {
            push_operand(operand_type{ dummy(type::Void, fragment_kind::Statement, aSource) });
            return;
        }
        fragment target;
        value_type targetType;
        bool viaThis = false;
        std::size_t index = 0u;
        if (variable != nullptr)
        {
            target = materialize(operand_type{ symbol_reference{ current_scope(), neolib::string{ path[0] } } });
            targetType = variable_type(*variable, path[0]);
            viaThis = (path[0] == "this");
            index = 1u;
        }
        else if (context != nullptr && context->object != nullptr && find_field(*context->object, path[0]) != nullptr)
        {
            target = this_value(aSource);
            targetType = value_type{ type::Pointer, context->object };
            viaThis = true;
        }
        else
            error(aSource, "undefined identifier '" + path[0] + "'");
        target.fragmentLvalue = nullptr;
        target.fragmentKind = fragment_kind::Value;
        for (; index < path.size(); ++index)
        {
            if (!targetType.is_object())
                error(aSource, "'" + path[index - 1u] + "' is not an object");
            if (targetType.object == nullptr)
                error(aSource, "the type of '" + path[index - 1u] + "' is not known");
            auto* const f = find_field(*targetType.object, path[index]);
            if (f == nullptr)
                error(aSource, "'" + targetType.object->qualifiedName + "' has no field '" + path[index] + "'");
            check_access(f->access, *f->owner, path[index], aSource);
            if (index + 1u == path.size())
            {
                if (!f->isMutable)
                    check_change(viaThis, "change '" + path[index] + "'", aSource);
                bool fresh = false;
                auto const valueType = type_of(operand, &fresh);
                auto value = !f->valueType.is_object() && !valueType.is_object() ? materialize(operand, f->valueType.builtin) : materialize(operand);
                auto statement = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{ aSource });
                append(statement, target);
                append(statement, stored(value, valueType, fresh, f->valueType, aSource));
                store(statement, *f);
                push_operand(operand_type{ statement });
                return;
            }
            load(target, *f);
            target.valueType = f->valueType.builtin;
            // (a struct within the object is part of it)
            viaThis = viaThis && f->valueType.is_object() && f->valueType.object->kind == type_kind::Struct;
            targetType = f->valueType;
        }
        error(aSource, "cannot assign to '" + path.back() + "'");
    }

    void compiler::object_initialize(std::string_view aSource)
    {
        auto const tokens = tokenize(aSource);
        std::size_t i = 0u;
        auto const typeText = qualified(tokens, i);
        std::string const name{ tokens.at(i).text };
        auto const t = find_type(typeText, scope_name(), aSource);
        if (declaration_pass())
        {
            define_variable(name, value_type{ type::Pointer, t.object }, aSource);
            generator().emit_assignment(neolib::string_view{ name }, neolib::string_view{ aSource });
            return;
        }
        if (!t.is_object())
            error(aSource, "type '" + typeText + "' not found");
        auto stack = operands();
        if (stack.size() <= operand_base())
            error(aSource, "expression expected");
        bool fresh = false;
        auto const valueType = type_of(stack.back(), &fresh);
        auto value = stored(materialize(operand_type{ stack.back() }), valueType, fresh, t, aSource);
        generator().set_operand(stack.size() - 1u, operand_type{ value });
        define_variable(name, t, aSource);
        generator().emit_assignment(neolib::string_view{ name }, neolib::string_view{ aSource });
    }

    void compiler::declaration(std::string_view aSource)
    {
        auto const tokens = tokenize(aSource);
        std::size_t i = 0u;
        auto const typeText = qualified(tokens, i);
        std::string const name{ tokens.at(i).text };
        auto const t = find_type(typeText, scope_name(), aSource);
        if (declaration_pass())
        {
            auto const declared = t.builtin != type::UNKNOWN ? t : value_type{ type::Pointer };
            define_variable(name, declared, aSource);
            push_operand(operand_type{ dummy(declared.builtin) });
            generator().emit_assignment(neolib::string_view{ name }, neolib::string_view{ aSource });
            return;
        }
        if (t.builtin == type::UNKNOWN)
            error(aSource, "type '" + typeText + "' not found");
        fragment value;
        if (!t.is_object())
            value = zero(t);
        else if (t.object->kind == type_kind::Interface)
        {
            // (a reference to no object)
            value = zero(t);
            value.valueType = type::Pointer;
        }
        else
            value = construct_object(*t.object, {}, {}, aSource);
        define_variable(name, t, aSource);
        push_operand(operand_type{ value });
        generator().emit_assignment(neolib::string_view{ name }, neolib::string_view{ aSource });
    }

    void compiler::auto_initialize(std::string_view aSource)
    {
        auto const tokens = tokenize(aSource);
        std::string const name{ tokens.at(1).text };
        auto stack = operands();
        if (stack.size() <= operand_base())
            error(aSource, "expression expected");
        if (declaration_pass())
        {
            define_variable(name, value_type{ type::I32 }, aSource);
            generator().emit_assignment(neolib::string_view{ name }, neolib::string_view{ aSource });
            return;
        }
        bool fresh = false;
        auto t = type_of(stack.back(), &fresh);
        auto value = materialize(operand_type{ stack.back() });
        if (!t.is_object())
            t = value_type{ value.valueType };
        if (t.builtin == type::Void || t.builtin == type::UNKNOWN)
            error(aSource, "'" + name + "': its initial value has no type");
        if (t.is_object() && t.object == nullptr)
            error(aSource, "'" + name + "': the type of its initial value is not known");
        if (t.is_object())
            value = stored(value, t, fresh, t, aSource);
        generator().set_operand(stack.size() - 1u, operand_type{ value });
        define_variable(name, t, aSource);
        generator().emit_assignment(neolib::string_view{ name }, neolib::string_view{ aSource });
    }

    void compiler::error(std::string_view aSource, std::string const& aMessage) const
    {
        auto const& fragment = generator().current_fragment();
        auto const text = fragment.source().to_std_string_view();
        if (!aSource.empty() && aSource.data() >= text.data() && aSource.data() <= text.data() + text.size())
            generator().throw_error(fragment.source().begin() + static_cast<std::ptrdiff_t>(aSource.data() - text.data()),
                neolib::string{ aMessage }, neolib::string{ "error" });
        throw std::runtime_error(aMessage);
    }
}
