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
#include <neolib/file/parser.hpp>
#include <neos/language/schema.hpp>

namespace neos::language::schema_parser
{
    enum class symbol
    {
        Grammar,
        Rule,
        RuleName,
        RuleExpression,
        Argument,
        Identifier,
        SemanticConcept,
        Concatenation,
        Alternation,
        Range,
        RangeNot,
        RangeSubtract,
        Terminal,
        Optional,
        Repetition,
        RepetitionAtLeastOne,
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
declare_symbol(neos::language::schema_parser::symbol, Argument)
declare_symbol(neos::language::schema_parser::symbol, Identifier)
declare_symbol(neos::language::schema_parser::symbol, SemanticConcept)
declare_symbol(neos::language::schema_parser::symbol, Concatenation)
declare_symbol(neos::language::schema_parser::symbol, Alternation)
declare_symbol(neos::language::schema_parser::symbol, Range)
declare_symbol(neos::language::schema_parser::symbol, RangeNot)
declare_symbol(neos::language::schema_parser::symbol, RangeSubtract)
declare_symbol(neos::language::schema_parser::symbol, Terminal)
declare_symbol(neos::language::schema_parser::symbol, Optional)
declare_symbol(neos::language::schema_parser::symbol, Repetition)
declare_symbol(neos::language::schema_parser::symbol, RepetitionAtLeastOne)
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
        
    neolib::parser_rule<symbol> parserRules[] =
    {
        ( symbol::Grammar >> +repeat(symbol::Rule), discard(symbol::Eof) ),
        ( symbol::Rule >> symbol::RuleName, "::=" , symbol::RuleExpression, optional(("::="_ , symbol::RuleExpression )) , ";" ),
        ( symbol::RuleName >> symbol::Identifier , repeat((" "_ , symbol::Identifier)) ),
        ( symbol::Identifier >> (symbol::Alpha , repeat(symbol::AlphaNumeric)) ),
        ( symbol::SemanticConcept >> (symbol::Alpha , repeat(symbol::AlphaNumeric | ("."_ , symbol::AlphaNumeric))) ),
        ( symbol::RuleExpression >> (symbol::Concatenation | symbol::Alternation | symbol::Grouping | 
            symbol::Repetition | symbol::Optional | symbol::Argument ) ),
        ( symbol::Grouping >> "(" , symbol::RuleExpression , ")" ),
        ( symbol::Repetition >> "{" , symbol::RuleExpression , "}" , optional(symbol::RepetitionAtLeastOne) ),
        ( symbol::RepetitionAtLeastOne >> "+"_ ) ,
        ( symbol::Optional >> "[" , symbol::RuleExpression , "]" ),
        ( symbol::Concatenation >> (symbol::Argument , +repeat((","_ , symbol::Argument))) ),
        ( symbol::Alternation >> (symbol::Argument , +repeat(("|"_ , symbol::Argument))) ),
        ( symbol::RangeSubtract >> (symbol::Range , "-"_ , symbol::Argument) ),
        ( symbol::RangeNot >> ("!"_ , symbol::Range) ),
        ( symbol::Range >> (symbol::CharacterLiteral , ".."_ , symbol::CharacterLiteral) ),
        ( symbol::Argument >> (symbol::RangeSubtract | symbol::RangeNot | symbol::Range | symbol::Terminal | symbol::SpecialSequence | symbol::RuleExpression ) ),
        ( symbol::Terminal >> (symbol::RuleName | symbol::Identifier | symbol::CharacterLiteral | symbol::StringLiteral ) ),
        ( symbol::SpecialSequence >> ("?"_ , repeat(symbol::Terminal) , "?"_) ),

        ( symbol::CharacterLiteral >> ("'"_ , (symbol::Character | symbol::EscapedCharacter) , "'"_) ),
        ( symbol::StringLiteral >> ("\""_ , repeat(symbol::Character | symbol::EscapedCharacter) , "\""_) ),
        ( symbol::Alpha >> (range('A', 'Z') | range('a', 'z')) ),
        ( symbol::AlphaNumeric >> (range('A', 'Z') | range('a', 'z') | range('0', '9' )) ),
        ( symbol::HexDigit >> (range('A', 'F') | range('a', 'f' ) | range('0', '9')) ),
        ( symbol::Character >> (
            range('\x20', '\x21') | range('\x23', '\x26') | range('\x28', '\x5B') | range('\x5D', '\xFF')) ),
        ( symbol::EscapedCharacter >> (
            "\\\\"_ | "\\\""_ | "\\'"_ | "\\n"_ | "\\r"_ | "\\t"_ |
            sequence("\\x"_ , symbol::HexDigit , symbol::HexDigit)) ),

        ( symbol::Eof >> discard(optional(symbol::Whitespace)) , "" ),
        ( symbol::Whitespace >> +repeat(' '_ | '\r' | '\n' | '\t' | symbol::Comment ) ),
        ( symbol::Comment >> sequence("(*"_ , repeat(range('\0', '\xFF')) , "*)"_) ),

        ( symbol::Rule >> discard(optional(symbol::Whitespace)), symbol::Rule, discard(optional(symbol::Whitespace)) ),
        ( symbol::RuleName >> discard(optional(symbol::Whitespace)), symbol::RuleName, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::RuleExpression >> discard(optional(symbol::Whitespace)), symbol::RuleExpression, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Grouping >> discard(optional(symbol::Whitespace)), symbol::Grouping, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Repetition >> discard(optional(symbol::Whitespace)), symbol::Repetition, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::RepetitionAtLeastOne >> discard(optional(symbol::Whitespace)), symbol::RepetitionAtLeastOne, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Optional >> discard(optional(symbol::Whitespace)), symbol::Optional, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Concatenation >> discard(optional(symbol::Whitespace)), symbol::Concatenation, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Alternation >> discard(optional(symbol::Whitespace)), symbol::Alternation, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Range >> discard(optional(symbol::Whitespace)), symbol::Range, discard(optional(symbol::Whitespace))  , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::RangeNot >> discard(optional(symbol::Whitespace)), symbol::RangeNot, discard(optional(symbol::Whitespace))  , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::RangeSubtract >> discard(optional(symbol::Whitespace)), symbol::RangeSubtract, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Argument >> discard(optional(symbol::Whitespace)), symbol::Argument, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::Terminal >> discard(optional(symbol::Whitespace)), symbol::Terminal, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::SpecialSequence >> discard(optional(symbol::Whitespace)), symbol::SpecialSequence, discard(optional(symbol::Whitespace)) , optional(("$"_ , symbol::SemanticConcept)) ),
        ( symbol::CharacterLiteral >> discard(optional(symbol::Whitespace)), symbol::CharacterLiteral, discard(optional(symbol::Whitespace)) ),
        ( symbol::StringLiteral >> discard(optional(symbol::Whitespace)), symbol::StringLiteral, discard(optional(symbol::Whitespace)) ),
        ( symbol::SemanticConcept >> discard(optional(symbol::Whitespace)), symbol::SemanticConcept, discard(optional(symbol::Whitespace)) )
    };
}

namespace neos::language
{
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
            iPipeline.push_back(schema_stage{ stageName, stages.at(stageName)});
        }

        std::shared_ptr<neolib::parser<schema_parser::symbol>::ast_node const> previousAst;

        for (auto const& stage : iPipeline)
        {
            neolib::parser<schema_parser::symbol> parser{ schema_parser::parserRules };
            parser.set_debug_output(std::cerr);
            parser.set_debug_scan(false);
            parser.parse(schema_parser::symbol::Grammar, stage.grammar);
            //parser.create_ast();
            //previousAst = parser.ast().shared_from_this();
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