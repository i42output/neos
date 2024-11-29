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
        std::string source = sourceBuffer.str();
        auto part = [&source](std::string_view const& keyStart, std::string_view const& keyEnd)
        {
            auto range = keyStart == "%{" ? 
                std::make_pair(source.find(keyStart), source.find(keyEnd)) : 
                std::make_pair(source.rfind(keyStart), source.rfind(keyEnd));
            if (range.first == range.second || range.first == std::string::npos || range.second == std::string::npos)
                throw std::runtime_error("Error reading schema");
            range.first += (keyStart == "%{" ? 1 : keyStart.size());
            range.second += (keyEnd == "}%" ? 1 : 0);
            return std::string{ std::next(source.begin(), range.first), std::next(source.begin(), range.second) };
        };

        std::istringstream metaSource{ part("%{", "}%") };
        neolib::rjson metaContents{ metaSource };
        auto const& meta = metaContents.root().as<neolib::rjson_object>().at("meta");
        parse_meta(meta);

        auto const& tokenizer = metaContents.root().as<neolib::rjson_object>().at("stages").as<neolib::rjson_object>().at("tokenizer").as<neolib::rjson_array>();
        auto const tokenizerGrammar = part(tokenizer[0].as<neolib::rjson_string>(), tokenizer[1].as<neolib::rjson_string>());

        auto const& parser = metaContents.root().as<neolib::rjson_object>().at("stages").as<neolib::rjson_object>().at("parser").as<neolib::rjson_array>();
        auto const parserGrammar = part(parser[0].as<neolib::rjson_string>(), parser[1].as<neolib::rjson_string>());
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