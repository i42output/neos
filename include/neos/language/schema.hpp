/*
  schema.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <neolib/i_map.hpp>
#include <neolib/i_string.hpp>
#include <neolib/json.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/schema_atom.hpp>

namespace neos
{
    namespace language
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
        };

        class schema
        {
        public:
            typedef neolib::ref_ptr<i_schema_atom> atom_ptr;
        public:
            schema(neolib::rjson const& aSchema, const concept_libraries_t& aConceptLibraries);
        public:
            language::meta const& meta() const;
        private:
            void parse(neolib::rjson_value const& aNode, atom_ptr aAtom);
            void parse_meta(neolib::rjson_value const& aNode);
        private:
            language::meta iMeta;
            const concept_libraries_t& iConceptLibraries;
            atom_ptr iRoot;
        };
    }
}