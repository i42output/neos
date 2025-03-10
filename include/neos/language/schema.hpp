/*
  schema.hpp

  Copyright (c) 2024 Leigh Johnston.  All Rights Reserved.

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

#pragma once

#include <neos/neos.hpp>
#include <boost/functional/hash.hpp>
#include <neolib/core/i_map.hpp>
#include <neolib/core/i_string.hpp>
#include <neolib/file/json.hpp>
#include <neos/language/i_concept_library.hpp>

namespace neos::language
{
    struct meta
    {
        std::string name;
        std::string description;
        std::string copyright;
        std::string version;
        std::vector<std::string> sourcecodeFileExtension;
        std::vector<std::string> sourcecodeModulePackageSpecificationFileExtension;
        std::vector<std::string> sourcecodeModulePackageImplementationFileExtension;
        std::size_t parserRecursionLimit = 256u;
    };

    struct schema_stage
    {
        std::string name;
        std::string_view grammar;
    };

    using pipeline = std::vector<schema_stage>;

    class schema
    {
    public:
        static constexpr std::size_t RecursionLimit = 64u;
    public:
        schema(std::string const& aPath, const concept_libraries_t& aConceptLibraries);
    public:
        std::string const& path() const;
        language::meta const& meta() const;
        neolib::ref_ptr<i_semantic_concept> find_concept(const std::string& aSymbol) const;
    private:
        void parse_meta(neolib::rjson_value const& aNode);
        void throw_error(neolib::rjson_value const& aNode, const std::string aErrorText) const;
    private:
        std::string iPath;
        std::string iSource;
        neolib::rjson iMetaSource;
        language::meta iMeta;
        pipeline iPipeline;
        const concept_libraries_t& iConceptLibraries;
    };
}