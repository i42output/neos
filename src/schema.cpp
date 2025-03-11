/*
  schema.cpp

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

#include <neos/neos.hpp>
#include <filesystem>
#include <neolib/core/scoped.hpp>
#include <neolib/core/recursion.hpp>
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
        SemanticConceptName,
        SemanticConcept,
        Concatenation,
        Alternation,
        Range,
        RangeNot,
        RangeSubtract,
        Terminal,
        Optional,
        Repetition,
        AtLeastOne,
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
declare_symbol(neos::language::schema_parser::symbol, SemanticConceptName)
declare_symbol(neos::language::schema_parser::symbol, SemanticConcept)
declare_symbol(neos::language::schema_parser::symbol, Concatenation)
declare_symbol(neos::language::schema_parser::symbol, Alternation)
declare_symbol(neos::language::schema_parser::symbol, Range)
declare_symbol(neos::language::schema_parser::symbol, RangeNot)
declare_symbol(neos::language::schema_parser::symbol, RangeSubtract)
declare_symbol(neos::language::schema_parser::symbol, Terminal)
declare_symbol(neos::language::schema_parser::symbol, Optional)
declare_symbol(neos::language::schema_parser::symbol, Repetition)
declare_symbol(neos::language::schema_parser::symbol, AtLeastOne)
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

    auto const SC = repeat(symbol::SemanticConcept);
    auto const WS = symbol::Whitespace;
        
    neolib::parser_rule<symbol> parserRules[] =
    {
        ( symbol::Grammar >> +repeat((WS , symbol::Rule <=> "rule"_concept , WS)) ),
        ( symbol::Rule >> WS , symbol::RuleName , SC , WS , "::=" , WS , 
            symbol::RuleExpression <=> "rule_expression"_concept , SC , WS , optional(("::="_ , WS , symbol::RuleExpression <=> "rule_constraint"_concept , SC)) , WS , ";" ),
        ( symbol::RuleName >> ((symbol::Identifier , repeat((" "_ , symbol::Identifier))) <=> "rule_name"_concept) ),
        ( symbol::Identifier >> (symbol::Alpha , repeat(symbol::AlphaNumeric)) ),
        ( symbol::SemanticConcept >> (WS , symbol::SemanticConceptTag , WS , symbol::SemanticConceptName <=> "semantic_concept"_concept , WS) ),
        ( symbol::RuleExpression >> (symbol::Grouping | symbol::Repetition | symbol::Optional | 
            symbol::Concatenation | symbol::Alternation | symbol::Argument) , SC ),
        ( symbol::RuleExpression2 >> (symbol::Grouping | symbol::Repetition | symbol::Optional | 
            symbol::Concatenation | symbol::Alternation) , SC ),
        ( symbol::Grouping >> "(" , WS , symbol::Argument <=> "grouping"_concept , SC , WS , ")" ),
        ( symbol::Repetition >> "{" , WS , symbol::Argument <=> "repetition"_concept , SC , WS , "}" , optional((WS , symbol::AtLeastOne <=> "at_least_one"_concept)) ),
        ( symbol::AtLeastOne >> "+"_ ) ,
        ( symbol::Optional >> "[" , WS , symbol::Argument <=> "optional"_concept , SC , WS , "]" ),
        ( symbol::Concatenation >> ((symbol::Argument2 , SC , +repeat((WS , ","_ , WS , symbol::Argument2 , SC)) ) <=> "concatenation"_concept) ),
        ( symbol::Alternation >> ((symbol::Argument2 , SC , +repeat((WS , "|"_ , WS , symbol::Argument2 , SC)) ) <=> "alternation"_concept) ),
        ( symbol::RangeSubtract >> ((symbol::Range , WS , "-"_ , WS , symbol::Argument) <=> "subtract"_concept) ),
        ( symbol::RangeNot >> (("!"_ , WS , symbol::Range) <=> "not"_concept) ),
        ( symbol::Range >> ((symbol::CharacterLiteral , WS , ".."_ , WS , symbol::CharacterLiteral) <=> "range"_concept) ),
        ( symbol::Argument >> (symbol::Terminal | symbol::RuleExpression) ),
        ( symbol::Argument2 >> (symbol::Terminal | symbol::RuleExpression2) ),
        ( symbol::Terminal >> (symbol::RangeSubtract | symbol::RangeNot | symbol::Range | symbol::SpecialSequence |
            symbol::RuleName | symbol::Identifier | symbol::CharacterLiteral | symbol::StringLiteral) ),
        
        ( symbol::SemanticConceptTag >> "$"_ ),
        ( symbol::SemanticConceptName >> (symbol::Alpha , repeat(symbol::AlphaNumeric | ("."_ , symbol::AlphaNumeric))) ),

        ( symbol::SpecialSequence >> ("?"_ , WS , repeat(symbol::Terminal) <=> "special"_concept , WS , "?"_) ),

        ( symbol::CharacterLiteral >> ("'"_ , (symbol::Character | symbol::EscapedCharacter) <=> "character"_concept , "'"_)),
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
    symbol lookup_symbol(schema_stage& aStage, std::string_view const& aSymbolLexeme)
    {
        auto existing = aStage.symbolMap.find(aSymbolLexeme);
        if (existing == aStage.symbolMap.end())
            existing = aStage.symbolMap.emplace(aSymbolLexeme, static_cast<symbol>(aStage.symbolMap.size())).first;
        return existing->second;
    }

    using atom = neolib::parser<symbol>::atom;
    using primitive = neolib::parser<symbol>::primitive_atom;

    bool is_parent(neolib::parser<schema_parser::symbol>::ast_node const& aNode, std::string_view const& aConcept)
    {
        return aNode.parent && aNode.parent->c == aConcept;
    }

    void walk_ast(neolib::parser<schema_parser::symbol> const& aParser, neolib::parser<schema_parser::symbol>::ast_node const& aNode, schema_stage& aStage, atom* aParentAtom = nullptr)
    {
        std::optional<atom> atom;

        if (aNode.c == "rule_name")
            atom.emplace(primitive{ lookup_symbol(aStage, aNode.value) });
        
        if (atom.has_value())
        {
            if (is_parent(aNode, "rule"))
                aStage.parser.rules().emplace_back(atom.value());
            else if (is_parent(aNode, "rule_expression") && is_parent(*aNode.parent, "rule"))
                aStage.parser.rules().back().rhs = atom.value();
        }

        for (auto const& child : aNode.children)
            walk_ast(aParser, *child, aStage);
    }

    schema::schema(std::string const& aPath, const concept_libraries_t& aConceptLibraries) :
        iPath{ aPath },
        iConceptLibraries{ aConceptLibraries }
    {
        if (!std::filesystem::exists(iPath) && std::filesystem::exists(iPath + ".neos"))
            iPath += ".neos";

        std::ifstream sourceFile(iPath);
        std::stringstream sourceBuffer;
        sourceBuffer << sourceFile.rdbuf();
        iSource = sourceBuffer.str();
        auto part = [&](std::string_view const& keyStart, std::string_view const& keyEnd)
        {
            auto range = keyStart == "%{" ? 
                std::make_pair(iSource.find(keyStart), iSource.find(keyEnd)) : 
                std::make_pair(iSource.rfind(keyStart), iSource.rfind(keyEnd));
            if (range.first == range.second || range.first == std::string::npos || range.second == std::string::npos)
                throw std::runtime_error("Error reading schema");
            range.first += (keyStart == "%{" ? 1 : keyStart.size());
            range.second += (keyEnd == "}%" ? 1 : 0);
            return std::string_view{ std::next(iSource.begin(), range.first), std::next(iSource.begin(), range.second) };
        };

        std::istringstream metaSource{ std::string{ part("%{", "}%") } };
        neolib::rjson metaContents{ metaSource };
        auto const& meta = metaContents.root().as<neolib::rjson_object>().at("meta");
        parse_meta(meta);

        std::map<std::string, std::string_view> stages;

        for (auto const& stage : metaContents.root().as<neolib::rjson_object>().at("stages").as<neolib::rjson_object>().contents())
        {
            auto const& partParams = stage.as<neolib::rjson_array>();
            stages[stage.name()] = part(partParams[0].as<neolib::rjson_string>(), partParams[1].as<neolib::rjson_string>());
        }

        for (auto const& stage : metaContents.root().as<neolib::rjson_object>().at("pipeline").as<neolib::rjson_array>())
        {
            auto const& stageName = stage->text();
            iPipeline.push_back(std::make_unique<schema_stage>(stageName, stages.at(stageName)));
        }

        for (auto const& stagePtr : iPipeline)
        {
            auto& stage = *stagePtr;
            neolib::parser<schema_parser::symbol> parser{ schema_parser::parserRules };
            parser.ignore(schema_parser::symbol::Whitespace);
            parser.set_debug_output(std::cerr, false, true);
            parser.set_debug_scan(false);
            parser.parse(schema_parser::symbol::Grammar, stage.grammar);
            parser.create_ast();
            walk_ast(parser, parser.ast(), stage);
        }
    }

    std::string const& schema::path() const
    {
        return iPath;
    }

    language::meta const& schema::meta() const
    {
        return iMeta;
    }

    void schema::parse_meta(neolib::rjson_value const& aNode)
    {
        if (aNode.name() == "meta")
        {
            for (auto const& meta : aNode)
            {
                if (meta.name() == "description")
                    iMeta.description = meta.as<neolib::rjson_string>();
                else if (meta.name() == "copyright")
                    iMeta.copyright = meta.as<neolib::rjson_string>();
                else if (meta.name() == "version")
                    iMeta.version = meta.as<neolib::rjson_string>();
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

    neolib::ref_ptr<i_semantic_concept> schema::find_concept(const std::string& aSymbol) const
    {
        neolib::ref_ptr<i_semantic_concept> semanticConcept;
        for (auto const& cl : iConceptLibraries)
        {
            if (cl.second()->find_concept(neolib::string{ aSymbol }, semanticConcept))
                return semanticConcept;
        }
        return nullptr;
    }

    void schema::throw_error(neolib::rjson_value const& aNode, const std::string aErrorText) const
    {
        if (aNode.has_name())
            throw std::runtime_error(iMetaSource.to_error_text(aNode, aErrorText));
        throw std::runtime_error(aErrorText);
    }
}