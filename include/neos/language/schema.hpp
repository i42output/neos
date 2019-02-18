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
#include <neolib/json.hpp>
#include <neos/language/i_library.hpp>

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

        class ast
        {
        public:
            class node : std::variant<i_concept const*>
            {
            public:
                typedef std::vector<std::unique_ptr<node>> children_t;
            public:
                node() : iParent{ nullptr }
                {
                }
                node(node& aParent) : iParent{ &aParent }
                {
                }
            public:
                const children_t& children() const
                {
                    return iChildren;
                }
                children_t& children()
                {
                    return iChildren;
                }
            private:
                node* iParent;
                children_t iChildren;
            };
        };

        class schema
        {
        public:
            schema(const neolib::rjson& aSchema);
        public:
            const language::meta& meta() const;
        private:
            language::meta iMeta;
        };
    }
}