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

        enum class symbol_type : uint32_t
        {
            Function,
            Data
        };
        typedef std::string symbol_name_t;
        typedef std::pair<symbol_type, symbol_name_t> symbol_key_t;
        typedef void const* symbol_reference_t;
        typedef std::map<symbol_key_t, symbol_reference_t> symbol_table_t;

        class ast
        {
        public:
            class node : public std::variant<i_concept const*, symbol_table_t::iterator>
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
                children_t const& children() const
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
            schema(neolib::rjson const& aSchema);
        public:
            language::meta const& meta() const;
        private:
            language::meta iMeta;
        };
    }
}