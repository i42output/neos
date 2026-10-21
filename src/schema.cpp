/*
  schema.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

#include <functional>
#include <neos/neos.hpp>
#include <filesystem>
#include <neolib/core/scoped.hpp>
#include <neolib/core/recursion.hpp>
#include <neolib/core/string_utils.hpp>
#include <neolib/file/file.hpp>
#include <neos/language/schema.hpp>

namespace neos::language::schema_parser
{
    enum class symbol
    {
        Grammar,
        Rule,
        RuleName,
        RuleExpression,
        RuleExpression2,
        Argument,
        Argument2,
        Identifier,
        SemanticConceptTag,
        SemanticConceptTagRef,
        SemanticConceptName,
        SemanticConcept,
        SemanticConceptRef,
        Concatenation,
        Alternation,
        Range,
        RangeNot,
        RangeSubtract,
        Exception,
        Terminal,
        Subtract,
        Optional,
        Repetition,
        AtLeastOne,
        Not,
        Grouping,
        Alpha,
        AlphaNumeric,
        HexDigit,
        Character,
        EscapedCharacter,
        CharacterLiteral,
        StringLiteral,
        SpecialSequence,
        Eof,
        Comment,
        Whitespace,
    };
}

declare_symbols(neos::language::schema_parser::symbol)
declare_symbol(neos::language::schema_parser::symbol, Grammar)
declare_symbol(neos::language::schema_parser::symbol, Rule)
declare_symbol(neos::language::schema_parser::symbol, RuleName)
declare_symbol(neos::language::schema_parser::symbol, RuleExpression)
declare_symbol(neos::language::schema_parser::symbol, RuleExpression2)
declare_symbol(neos::language::schema_parser::symbol, Argument)
declare_symbol(neos::language::schema_parser::symbol, Argument2)
declare_symbol(neos::language::schema_parser::symbol, Identifier)
declare_symbol(neos::language::schema_parser::symbol, SemanticConceptTag)
declare_symbol(neos::language::schema_parser::symbol, SemanticConceptTagRef)
declare_symbol(neos::language::schema_parser::symbol, SemanticConceptName)
declare_symbol(neos::language::schema_parser::symbol, SemanticConcept)
declare_symbol(neos::language::schema_parser::symbol, SemanticConceptRef)
declare_symbol(neos::language::schema_parser::symbol, Optional)
declare_symbol(neos::language::schema_parser::symbol, Concatenation)
declare_symbol(neos::language::schema_parser::symbol, Alternation)
declare_symbol(neos::language::schema_parser::symbol, Range)
declare_symbol(neos::language::schema_parser::symbol, RangeNot)
declare_symbol(neos::language::schema_parser::symbol, RangeSubtract)
declare_symbol(neos::language::schema_parser::symbol, Exception)
declare_symbol(neos::language::schema_parser::symbol, Terminal)
declare_symbol(neos::language::schema_parser::symbol, Subtract)
declare_symbol(neos::language::schema_parser::symbol, Repetition)
declare_symbol(neos::language::schema_parser::symbol, AtLeastOne)
declare_symbol(neos::language::schema_parser::symbol, Not)
declare_symbol(neos::language::schema_parser::symbol, Grouping)
declare_symbol(neos::language::schema_parser::symbol, Alpha)
declare_symbol(neos::language::schema_parser::symbol, AlphaNumeric)
declare_symbol(neos::language::schema_parser::symbol, HexDigit)
declare_symbol(neos::language::schema_parser::symbol, Character)
declare_symbol(neos::language::schema_parser::symbol, EscapedCharacter)
declare_symbol(neos::language::schema_parser::symbol, CharacterLiteral)
declare_symbol(neos::language::schema_parser::symbol, StringLiteral)
declare_symbol(neos::language::schema_parser::symbol, SpecialSequence)
declare_symbol(neos::language::schema_parser::symbol, Eof)
declare_symbol(neos::language::schema_parser::symbol, Comment)
declare_symbol(neos::language::schema_parser::symbol, Whitespace)
end_declare_symbols(neos::language::schema_parser::symbol);

namespace neos::language::schema_parser
{
    enable_neolib_parser(symbol)

    auto const SC_Rule = repeat(symbol::SemanticConcept);
    auto const SC = repeat((symbol::SemanticConcept | symbol::SemanticConceptRef));
    auto const WS = symbol::Whitespace;
        
    neolib::parser_rule<symbol> parserRules[] =
    {
        ( symbol::Grammar >> +repeat((WS , symbol::Rule <=> "rule"_concept , WS)) ),
        ( symbol::Rule >> WS , symbol::RuleName , SC_Rule , WS , "::=" , WS ,
            symbol::RuleExpression <=> "rule_expression"_concept , SC , WS , optional(("::="_ , WS , symbol::RuleExpression <=> "rule_constraint"_concept , SC)) , WS , ";" ),
        ( symbol::RuleName >> ((symbol::Identifier , repeat((" "_ , symbol::Identifier))) <=> "rule_name"_concept) ),
        ( symbol::Identifier >> (symbol::Alpha , repeat(symbol::AlphaNumeric)) ),
        ( symbol::SemanticConcept >> (WS , symbol::SemanticConceptTag , WS , symbol::SemanticConceptName <=> "semantic_concept"_concept , WS) ),
        ( symbol::SemanticConceptRef >> (WS , symbol::SemanticConceptTagRef , WS , symbol::SemanticConceptName <=> "semantic_concept_ref"_concept , WS)),
        ( symbol::RuleExpression >> (symbol::Grouping | symbol::Repetition | symbol::Optional |
            symbol::Concatenation | symbol::Alternation | symbol::Argument) , SC ),
        ( symbol::RuleExpression2 >> (symbol::Grouping | symbol::Repetition | symbol::Optional | 
            symbol::Concatenation | symbol::Alternation) , SC ),
        ( symbol::Grouping >> "(" , WS , symbol::Argument , SC , WS , ")" ),
        ( symbol::Repetition >> "{" , WS , symbol::Argument <=> "repetition"_concept , SC , WS , "}" , optional((WS , symbol::AtLeastOne <=> "at_least_one"_concept)) ),
        ( symbol::Optional >> "[" , WS , symbol::Argument <=> "optional"_concept , SC , WS , "]" ),
        ( symbol::Concatenation >> ((symbol::Argument2 , SC , +repeat((WS , ","_ , WS , symbol::Argument2 , SC)) ) <=> "concatenation"_concept) ),
        ( symbol::Alternation >> ((symbol::Argument2 , SC , +repeat((WS , "|"_ , WS , symbol::Argument2 , SC)) ) <=> "alternation"_concept) ),
        ( symbol::Alternation >> ((symbol::Argument2 , SC , +repeat((WS , "/"_ , WS , symbol::Argument2 , SC)) ) <=> "alternation_match_first"_concept) ),
        ( symbol::RangeSubtract >> (symbol::Range , WS , (symbol::Subtract <=> "subtract"_infix_concept) , WS , symbol::Argument) ),
        ( symbol::RangeNot >> ((symbol::Not , WS , symbol::Range) <=> "not"_concept) ),
        ( symbol::Range >> ((symbol::CharacterLiteral , WS , ".."_ , WS , symbol::CharacterLiteral) <=> "range"_concept) ),
        // (an exception (ISO 14977's "factor - exception"): a match of the factor (a rule or a group) that is not one of the exception's strings)
        ( symbol::Exception >> ((symbol::Grouping | symbol::RuleName) , WS , "-"_ , WS ,
            ((symbol::Grouping | symbol::StringLiteral | symbol::CharacterLiteral) <=> "exception"_concept)) ),
        ( symbol::Argument >> (symbol::Terminal | symbol::RuleExpression) ),
        ( symbol::Argument2 >> (symbol::Terminal | symbol::RuleExpression2) ),
        ( symbol::Terminal >> (symbol::RangeSubtract | symbol::RangeNot | symbol::Range | symbol::Exception | symbol::SpecialSequence |
            symbol::RuleName | symbol::Identifier | symbol::CharacterLiteral | symbol::StringLiteral) ),
        ( symbol::Subtract >> "-"_ ),
        ( symbol::AtLeastOne >> "+"_ ) ,
        ( symbol::Not >> "!"_ ) ,

        ( symbol::SemanticConceptTag >> "$"_ ),
        ( symbol::SemanticConceptTagRef >> "$:"_),
        ( symbol::SemanticConceptName >> (symbol::Alpha , repeat(symbol::AlphaNumeric | ("."_ , symbol::AlphaNumeric))) ),

        ( symbol::SpecialSequence >> ("?"_ , WS , repeat(symbol::Terminal) <=> "special"_concept , WS , "?"_) ),

        ( symbol::CharacterLiteral >> ("'"_ , (symbol::Character | symbol::EscapedCharacter) <=> "character"_concept , "'"_)),
        ( symbol::CharacterLiteral >> ("U+"_ , +repeat(symbol::HexDigit) <=> "codepoint"_concept) ),
        ( symbol::StringLiteral >> ("\""_ , repeat(symbol::Character | symbol::EscapedCharacter) <=> "string"_concept , "\""_) ),
        ( symbol::Alpha >> (range('A', 'Z') | range('a', 'z')) ),
        ( symbol::AlphaNumeric >> (range('A', 'Z') | range('a', 'z') | range('0', '9' )) ),
        ( symbol::HexDigit >> (range('A', 'F') | range('a', 'f' ) | range('0', '9')) ),
        ( symbol::Character >> (
            range('\x20', '\x21') | range('\x23', '\x26') | range('\x28', '\x5B') | range('\x5D', '\xFF')) ),
        ( symbol::EscapedCharacter >> (
            "\\\\"_ | "\\\""_ | "\\'"_ | "\\n"_ | "\\r"_ | "\\t"_ |
            sequence("\\x"_ , symbol::HexDigit , symbol::HexDigit)) ),

        ( symbol::Whitespace >> repeat(' '_ | '\r' | '\n' | '\t' | symbol::Comment ) ),
        ( symbol::Comment >> sequence("(*"_ , repeat(
            range('\x00', '\x29') | range('\x2B', '\xFF') | ("*"_ , (range('\x00', '\x28') | range('\x2A', '\xFF')))) , "*)"_) ),
    };
}

namespace neos::language
{
    code_parser::symbol lookup_symbol(schema_stage& aStage, std::string_view const& aSymbolLexeme)
    {
        auto existing = aStage.symbolMap->find(aSymbolLexeme);
        if (existing == aStage.symbolMap->end())
            existing = aStage.symbolMap->emplace(aSymbolLexeme, static_cast<code_parser::symbol>(aStage.symbolMap->size())).first;
        return existing->second;
    }

    using atom = parser::atom;
    using primitive = parser::primitive_atom;

    bool is_parent(neolib::parser<schema_parser::symbol>::ast_node const& aNode, std::string_view const& aConcept)
    {
        return aNode.parent && aNode.parent->c == aConcept;
    }

    void walk_ast(neolib::parser<schema_parser::symbol> const& aSchemaParser, neolib::parser<schema_parser::symbol>::ast_node const& aNode, schema_stage& aStage, primitive* aParentAtom = nullptr)
    {
        auto& codeParser = *aStage.parser;

        primitive* currentParent = aParentAtom;
        primitive* newParent = currentParent;

        std::optional<primitive> value;

        thread_local std::int32_t depth = 0;
        std::optional<neolib::scoped_counter<std::int32_t>> scopedDepth;

        if (aNode.c == "exception")
        {
            // (factor - exception: a match of the factor (the primitive before it) is not a match if the exception matches all of it)
            primitive* factor = nullptr;
            if (!codeParser.rules().empty() && !codeParser.rules().back().lhs.empty() && !codeParser.rules().back().rhs.empty() &&
                currentParent == &codeParser.rules().back().lhs.back())
                factor = &codeParser.rules().back().rhs.back();
            else if (currentParent)
                std::visit([&](auto& aParentPrimitive)
                {
                    using type = std::decay_t<decltype(aParentPrimitive)>;
                    if constexpr (
                        std::is_same_v<type, parser::concatenation> ||
                        std::is_same_v<type, parser::alternation> ||
                        std::is_same_v<type, parser::repetition> ||
                        std::is_same_v<type, parser::optional>)
                    {
                        if (!aParentPrimitive.value.empty())
                            factor = &aParentPrimitive.value.back();
                    }
                }, *currentParent);
            if (factor == nullptr)
                throw std::runtime_error("schema: an exception ('-') without a factor");
            // (an exception has no rules (ISO 14977): its terminals (strings, characters, ranges ...))
            std::function<void(neolib::parser<schema_parser::symbol>::ast_node const&)> check = [&](auto const& aChild)
            {
                if (aChild.c == "rule_name")
                    throw std::runtime_error("schema: an exception ('-') cannot refer to a rule (" + std::string{ aChild.value } + ")");
                for (auto const& c : aChild.children)
                    check(*c);
            };
            check(aNode);
            primitive exception = parser::alternation{};
            for (auto const& child : aNode.children)
                walk_ast(aSchemaParser, *child, aStage, &exception);
            auto& alternatives = std::get<parser::alternation>(exception).value;
            factor->exception = std::make_shared<primitive const>(alternatives.size() == 1u ? alternatives[0] : exception);
            return;
        }
        if (aNode.c == "rule_name")
            value.emplace(lookup_symbol(aStage, aNode.value));
        else if (aNode.c == "rule_constraint")
            codeParser.rules().back().rhs.back().constraint.emplace(aNode.children.front()->value);
        else if (aNode.c == "concatenation")
            value.emplace(parser::concatenation{});
        else if (aNode.c == "alternation")
            value.emplace(parser::alternation{});
        else if (aNode.c == "alternation_match_first")
            std::get<parser::alternation>(value.emplace(parser::alternation{})).matchFirst = true;
        else if (aNode.c == "repetition")
            value.emplace(parser::repetition{});
        else if (aNode.c == "optional")
            value.emplace(parser::optional{});
        else if (aNode.c == "range")
            value.emplace(parser::range{});
        else if (aNode.c == "string")
            value.emplace(parser::terminal{ neolib::unescape(aNode.value) });
        else if (aNode.c == "character")
            value.emplace(parser::terminal{ neolib::unescape(aNode.value) });
        else if (aNode.c == "codepoint")
            value.emplace(parser::terminal{ neolib::utf32_to_utf8(std::u32string{ static_cast<char32_t>(std::stoul(aNode.value.data(), nullptr, 16))}) });

        if (value.has_value())
        {
            if (is_parent(aNode, "rule"))
            {
                codeParser.rules().emplace_back(value.value());
                newParent = &codeParser.rules().back().lhs.back();
            }
            else if (is_parent(aNode, "rule_expression") && is_parent(*aNode.parent, "rule"))
            {
                codeParser.rules().back().rhs.push_back(value.value());
                newParent = &codeParser.rules().back().rhs.back();
            }
            else if (currentParent)
            {
                std::visit([&](auto& aParentPrimitive)
                {
                    using type = std::decay_t<decltype(aParentPrimitive)>;
                    if constexpr (
                        std::is_same_v<type, parser::concatenation> ||
                        std::is_same_v<type, parser::alternation> ||
                        std::is_same_v<type, parser::repetition> ||
                        std::is_same_v<type, parser::optional> ||
                        std::is_same_v<type, parser::range>)
                    {
                        aParentPrimitive.value.push_back(value.value());
                        newParent = &aParentPrimitive.value.back();
                    }
                }, *currentParent);
            }
        }
        else if (is_parent(aNode, "rule") && aNode.c == "rule_expression")
            newParent = &codeParser.rules().back().lhs.back();

        if (newParent != currentParent)
            scopedDepth.emplace(depth);

        for (auto const& child : aNode.children)
            walk_ast(aSchemaParser, *child, aStage, newParent);

        primitive* previous = nullptr;
        if (!codeParser.rules().empty() &&
            !codeParser.rules().back().lhs.empty() &&
            !codeParser.rules().back().rhs.empty() &&
            currentParent == &codeParser.rules().back().lhs.back())
        {
            previous = &codeParser.rules().back().rhs.back();
        }
        else if (currentParent)
        {
            std::visit([&](auto& aParentPrimitive)
                {
                    using type = std::decay_t<decltype(aParentPrimitive)>;
                    if constexpr (
                        std::is_same_v<type, parser::concatenation> ||
                        std::is_same_v<type, parser::alternation> ||
                        std::is_same_v<type, parser::repetition> ||
                        std::is_same_v<type, parser::optional>)
                    {
                        if (!aParentPrimitive.value.empty())
                            previous = &aParentPrimitive.value.back();
                    }
                }, *currentParent);
        }

        if (currentParent)
        {
            if (aNode.c == "semantic_concept")
            {
                previous->c.emplace(aNode.value);
                thread_local std::string infixCheck;
                infixCheck = aNode.value;
                if (aStage.infix->find(infixCheck) != aStage.infix->end())
                    previous->c.value().association = neolib::concept_association::Infix;
            }
            else if (aNode.c == "rule_constraint")
                previous->constraint.emplace(aNode.value);
            else if (aNode.c == "at_least_one")
                std::get<parser::repetition>(*previous).atLeastOne = true;
            else if (aNode.c == "not")
                std::get<parser::range>(*previous).negate = true;
            else if (aNode.c == "subtract")
            {
                std::visit([&](auto& aParentPrimitive)
                    {
                        using type = std::decay_t<decltype(aParentPrimitive)>;
                        if constexpr (
                            std::is_same_v<type, parser::concatenation> ||
                            std::is_same_v<type, parser::alternation> ||
                            std::is_same_v<type, parser::repetition> ||
                            std::is_same_v<type, parser::optional>)
                        {
                            auto& ran = std::get<parser::range>(aParentPrimitive.value[0]);
                            std::visit([&](auto& aRhs)
                                {
                                    using type = std::decay_t<decltype(aRhs)>;
                                    if constexpr (
                                        std::is_same_v<type, parser::concatenation> ||
                                        std::is_same_v<type, parser::alternation> ||
                                        std::is_same_v<type, parser::repetition> ||
                                        std::is_same_v<type, parser::optional>)
                                    {
                                        for (auto const& e : aRhs.value)
                                        {
                                            auto const& t = std::get<parser::terminal>(e);
                                            ran.exclusions.insert(static_cast<unsigned char>(t[0]));
                                        }
                                    }
                                    else if constexpr (std::is_same_v<type, parser::range>)
                                    {
                                        // todo
                                    }
                                }, aParentPrimitive.value[1]);
                            aParentPrimitive.value.erase(std::next(aParentPrimitive.value.begin()));
                        }
                    }, *currentParent);
            }
        }
        else
        {
            if (aNode.c == "semantic_concept")
                codeParser.rules().back().lhs.back().c.emplace(aNode.value);
        }
    }

    namespace
    {
        std::vector<std::filesystem::path> schema_folders()
        {
            // (relative to where neos is installed, e.g. /usr/local/bin/neos -> /usr/local/share/neos/languages, else side-by-side with the neos binary)
            std::filesystem::path const programDirectory{ neolib::program_directory() };
            return { programDirectory.parent_path() / "share" / "neos" / "languages", programDirectory };
        }
    }

    std::optional<std::string> schema::find(std::string const& aSourceFileExtension)
    {
        for (auto const& folder : schema_folders())
        {
            std::error_code ec;
            if (!std::filesystem::is_directory(folder, ec))
                continue;
            for (auto const& entry : std::filesystem::directory_iterator{ folder, ec })
            {
                if (entry.path().extension() != ".neos")
                    continue;
                try
                {
                    // (only the schema's meta is read: its grammars are not parsed)
                    std::ifstream sourceFile{ entry.path() };
                    std::stringstream sourceBuffer;
                    sourceBuffer << sourceFile.rdbuf();
                    std::string const source = sourceBuffer.str();
                    auto const metaStart = source.find("%{");
                    auto const metaEnd = source.find("}%");
                    if (metaStart == std::string::npos || metaEnd == std::string::npos || metaStart >= metaEnd)
                        continue;
                    std::istringstream metaSource{ source.substr(metaStart + 1, metaEnd - metaStart) };
                    neolib::rjson metaContents{ metaSource };
                    auto const& root = metaContents.root().as<neolib::rjson_object>();
                    if (!root.has("meta"))
                        continue;
                    for (auto const& meta : root.at("meta"))
                        if (meta.name() == "source.file.extension")
                        {
                            if (meta.type() == neolib::json_type::String)
                            {
                                if (std::string{ meta.as<neolib::rjson_string>() } == aSourceFileExtension)
                                    return entry.path().string();
                            }
                            else
                                for (auto const& ext : meta)
                                    if (std::string{ ext.as<neolib::rjson_string>() } == aSourceFileExtension)
                                        return entry.path().string();
                        }
                }
                catch (...)
                {
                    // (a schema whose meta cannot be read: skip it)
                }
            }
        }
        return {};
    }

    schema::schema(std::string const& aPath, const concept_libraries_t& aConceptLibraries, bool aShowParseTimes, std::optional<std::string> const& aSource) :
        iPath{ aPath },
        iConceptLibraries{ aConceptLibraries }
    {
        std::filesystem::path const specifiedPath{ aPath };
        if (aSource)
            iSource = *aSource;
        else if (!specifiedPath.has_parent_path() && !specifiedPath.has_extension())
        {
            // (a schema name without path or extension: search where neos is installed)
            for (auto const& folder : schema_folders())
                if (std::filesystem::exists(folder / (aPath + ".neos")))
                {
                    iPath = (folder / (aPath + ".neos")).string();
                    break;
                }
        }
        if (!aSource)
        {
            if (!std::filesystem::exists(iPath) && std::filesystem::exists(iPath + ".neos"))
                iPath += ".neos";
            std::ifstream sourceFile(iPath);
            std::stringstream sourceBuffer;
            sourceBuffer << sourceFile.rdbuf();
            iSource = sourceBuffer.str();
        }
        auto part = [&](std::string_view const& keyStart, std::string_view const& keyEnd)
        {
            auto range = keyStart == "%{" ? 
                std::make_pair(iSource.find(keyStart), iSource.find(keyEnd)) : 
                std::make_pair(iSource.rfind(keyStart), iSource.rfind(keyEnd));
            if (range.first == range.second || range.first == std::string::npos || range.second == std::string::npos ||
                range.first >= range.second)
                throw std::runtime_error("Error reading schema");
            range.first += (keyStart == "%{" ? 1 : keyStart.size());
            range.second += (keyEnd == "}%" ? 1 : 0);
            return std::string_view{ std::next(iSource.begin(), range.first), std::next(iSource.begin(), range.second) };
        };

        std::istringstream metaSource{ std::string{ part("%{", "}%") } };
        neolib::rjson metaContents{ metaSource };
        auto const& meta = metaContents.root().as<neolib::rjson_object>().at("meta");
        parse_meta(meta);
        // (the concept libraries the schema's language uses: each must be loaded)
        if (metaContents.root().as<neolib::rjson_object>().has("libraries"))
            parse_meta(metaContents.root().as<neolib::rjson_object>().at("libraries"));

        std::map<std::string, std::pair<std::string_view, std::optional<std::string>>> stages;

        for (auto const& stage : metaContents.root().as<neolib::rjson_object>().at("stages").as<neolib::rjson_object>().contents())
        {
            auto const& partParams = stage.as<neolib::rjson_object>();
            auto const& text = partParams.at("text").as<neolib::rjson_array>();
            std::optional<std::string> root;
            if (partParams.has("root"))
                root = partParams.at("root").as<neolib::rjson_keyword>().text;
            stages[stage.name()] = std::make_pair(part(text[0].as<neolib::rjson_string>(), text[1].as<neolib::rjson_string>()), root);
        }

        auto infix = std::make_shared<std::unordered_set<std::string>>();
        if (metaContents.root().as<neolib::rjson_object>().has("infix"))
        {
            for (auto const& i : metaContents.root().as<neolib::rjson_object>().at("infix").as<neolib::rjson_array>())
                infix->insert(i->text());
        }

        auto discard = std::make_shared<std::unordered_set<std::string>>();
        if (metaContents.root().as<neolib::rjson_object>().has("discard"))
        {
            for (auto const& d : metaContents.root().as<neolib::rjson_object>().at("discard").as<neolib::rjson_array>())
                discard->insert(d->text());
        }

        for (auto const& stage : metaContents.root().as<neolib::rjson_object>().at("pipeline").as<neolib::rjson_array>())
        {
            auto const& stageName = stage->text();
            std::string const stageKey{ stageName.to_std_string_view() };
            auto symbolMap = iPipeline.empty() ? std::make_shared<std::unordered_map<std::string_view, code_parser::symbol>>() : iPipeline.back()->symbolMap;
            if (iPipeline.empty())
                iPipeline.push_back(std::make_unique<schema_stage>(stageName, stages.at(stageKey).first, stages.at(stageKey).second, infix, discard, symbolMap, std::make_shared<parser>()));
            else
                iPipeline.push_back(std::make_unique<schema_stage>(stageName, stages.at(stageKey).first, stages.at(stageKey).second, infix, discard, symbolMap, std::make_shared<parser>(iPipeline.back()->parser)));
        }

        for (auto const& stagePtr : iPipeline)
        {
            auto& stage = *stagePtr;
            neolib::parser<schema_parser::symbol> parser{ schema_parser::parserRules };
            parser.ignore(schema_parser::symbol::Whitespace);
            parser.set_debug_output(std::cerr, false, false);
            parser.set_debug_timing(aShowParseTimes);
            parser.parse(schema_parser::symbol::Grammar, stage.grammar);
            iParseTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(parser.parse_time());
            parser.create_ast();
            walk_ast(parser, parser.ast(), stage);
        }
    }

    std::string const& schema::path() const
    {
        return iPath;
    }

    std::chrono::steady_clock::duration schema::parse_time() const
    {
        return iParseTime;
    }

    language::meta const& schema::meta() const
    {
        return iMeta;
    }

    language::pipeline const& schema::pipeline() const
    {
        return iPipeline;
    }

    void schema::parse_meta(neolib::rjson_value const& aNode)
    {
        if (aNode.name() == "meta")
        {
            for (auto const& meta : aNode)
            {
                if (meta.name() == "language")
                    iMeta.language = meta.as<neolib::rjson_string>();
                else if (meta.name() == "description")
                    iMeta.description = meta.as<neolib::rjson_string>();
                else if (meta.name() == "copyright")
                    iMeta.copyright = meta.as<neolib::rjson_string>();
                else if (meta.name() == "version")
                    iMeta.version = meta.as<neolib::rjson_string>();
                else if (meta.name() == "bigint.promotion")
                {
                    std::string const value{ meta.type() == neolib::json_type::Bool ? (meta.as<neolib::rjson_bool>() ? "true" : "false") : 
                        meta.type() == neolib::json_type::String ? std::string{ meta.as<neolib::rjson_string>() } : std::string{ meta.as<neolib::rjson_keyword>().text } };
                    if (value != "true" && value != "false")
                        throw std::runtime_error("bigint.promotion: expected true or false");
                    iMeta.bigintPromotion = (value == "true");
                }
                else if (meta.name() == "prelude")
                {
                    for (auto const& package : meta)
                        iMeta.prelude.push_back(package.type() == neolib::json_type::String ? 
                            std::string{ package.as<neolib::rjson_string>() } : std::string{ package.as<neolib::rjson_keyword>().text });
                }
                else if (meta.name() == "source.file.extension")
                {
                    if (meta.type() == neolib::json_type::String)
                        iMeta.sourcecodeFileExtension.push_back(meta.as<neolib::rjson_string>());
                    else
                        for (auto const& ext : meta)
                            iMeta.sourcecodeFileExtension.push_back(ext.as<neolib::rjson_string>());
                }
                else if (meta.name() == "source.source.package.specification.file.extension")
                {
                    if (meta.type() == neolib::json_type::String)
                        iMeta.sourcecodeModulePackageSpecificationFileExtension.push_back(meta.as<neolib::rjson_string>());
                    else
                        for (auto const& ext : meta)
                            iMeta.sourcecodeModulePackageSpecificationFileExtension.push_back(ext.as<neolib::rjson_string>());
                }
                else if (meta.name() == "source.source.package.implementation.file.extension")
                {
                    if (meta.type() == neolib::json_type::String)
                        iMeta.sourcecodeModulePackageImplementationFileExtension.push_back(meta.as<neolib::rjson_string>());
                    else
                        for (auto const& ext : meta)
                            iMeta.sourcecodeModulePackageImplementationFileExtension.push_back(ext.as<neolib::rjson_string>());
                }
            }
        }
        else if (aNode.name() == "libraries")
        {
            for (auto const& library : aNode)
                if (iConceptLibraries.find(neolib::string{ library.as<neolib::rjson_keyword>().text }) == iConceptLibraries.end())
                    throw_error(aNode, "concept library '" + library.as<neolib::rjson_keyword>().text + "' not found");
        }
    }

    void schema::throw_error(neolib::rjson_value const& aNode, const std::string aErrorText) const
    {
        if (aNode.has_name())
            throw std::runtime_error(iMetaSource.to_error_text(aNode, aErrorText));
        throw std::runtime_error(aErrorText);
    }
}