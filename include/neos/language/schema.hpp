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
#include <neolib/file/parser.hpp>
#include <neos/language/i_concept_library.hpp>

namespace neos::language
{
    struct meta
    {
        std::string language;
        std::string description;
        std::string copyright;
        std::string version;
        std::vector<std::string> sourcecodeFileExtension;
        std::vector<std::string> sourcecodeModulePackageSpecificationFileExtension;
        std::vector<std::string> sourcecodeModulePackageImplementationFileExtension;
        std::size_t parserRecursionLimit = 256u;
    };

    namespace code_parser
    {
        enum class symbol : std::uint32_t {};
    }
}

declare_symbols(neos::language::code_parser::symbol)
end_declare_symbols(neos::language::code_parser::symbol);

namespace neos::language
{
    namespace code_parser
    {
        enable_neolib_parser(symbol)
    }

    using parser = neolib::parser<code_parser::symbol>;

    struct schema_stage
    {
        std::string name;
        std::string_view grammar;
        std::optional<std::string> root;
        std::shared_ptr<std::unordered_set<std::string>> infix;
        std::shared_ptr<std::unordered_set<std::string>> discard;
        std::shared_ptr<std::unordered_map<std::string_view, code_parser::symbol>> symbolMap = {};
        std::shared_ptr<parser> parser = {};
    };

    using pipeline = std::vector<std::unique_ptr<schema_stage>>;

    class schema
    {
    public:
        static constexpr std::size_t RecursionLimit = 64u;
    public:
        schema(std::string const& aPath, const concept_libraries_t& aConceptLibraries);
    public:
        std::string const& path() const;
        language::meta const& meta() const;
        language::pipeline const& pipeline() const;
    private:
        void parse_meta(neolib::rjson_value const& aNode);
        void throw_error(neolib::rjson_value const& aNode, const std::string aErrorText) const;
    private:
        std::string iPath;
        std::string iSource;
        neolib::rjson iMetaSource;
        language::meta iMeta;
        language::pipeline iPipeline;
        const concept_libraries_t& iConceptLibraries;
    };
}