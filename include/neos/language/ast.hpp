/*
  ast.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <neolib/core/reference_counted.hpp>
#include <neos/language/symbols.hpp>
#include <neos/language/i_semantic_concept.hpp>

namespace neos
{
    namespace language
    {
        class ast
        {
        public:
            class node : public std::variant<std::monostate, neolib::ref_ptr<i_semantic_concept>, symbol_table_t::iterator>
            {
            public:
                using value_type = std::variant<std::monostate, neolib::ref_ptr<i_semantic_concept>, symbol_table_t::iterator>;
                using children_t = std::vector<std::unique_ptr<node>>;
            public:
                node()
                {
                }
                node(node const& aOther) :
                    value_type{ aOther },
                    iParent{ aOther.iParent }
                {
                }
                node(node&& aOther) :
                    value_type{ std::move(aOther) },
                    iParent{ aOther.iParent }
                {
                }
                node(value_type const& aValue, node& aParent) :
                    value_type{ aValue },
                    iParent{ &aParent } 
                {
                }
            public:
                using value_type::operator=;
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
                node* iParent = nullptr;
                children_t iChildren;
            };
        public:
            ast(symbol_table_t& aSymbolTable) : iSymbolTable{ aSymbolTable }
            {
            }
            ast(ast const& aOther) : iSymbolTable{ aOther.iSymbolTable }
            {
            }
        public:
            ast& operator=(ast const& aOther)
            {
                if (&aOther == this)
                    return *this;
                this->~ast();
                new (this) ast{ aOther };
                return *this;
            }
        public:
            node const& root() const
            {
                return iRoot;
            }
            node& root()
            {
                return iRoot;
            }
        private:
            symbol_table_t& iSymbolTable;
            node iRoot;
        };
    }
}