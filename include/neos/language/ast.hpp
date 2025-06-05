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
#include <neos/language/i_semantic_concept.hpp>
#include <neos/language/symbols.hpp>

namespace neos
{
    namespace language
    {
        class ast
        {
        public:
            class node : 
                public std::variant<std::monostate, neolib::ref_ptr<i_semantic_concept>, symbol_table::iterator>, 
                public std::enable_shared_from_this<node>
            {
            public:
                using value_type = std::variant<std::monostate, neolib::ref_ptr<i_semantic_concept>, symbol_table::iterator>;
                using children_t = std::vector<std::shared_ptr<node>>;
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
                bool is_empty() const
                {
                    return std::holds_alternative<std::monostate>(*this) ||
                        (std::holds_alternative<neolib::ref_ptr<i_semantic_concept>>(*this) &&
                            !std::get<neolib::ref_ptr<i_semantic_concept>>(*this).valid());
                }
            public:
                bool is_sibling(node const& aSibling) const
                {
                    return iParent == aSibling.iParent;
                }
                bool is_child(node const& aChild) const
                {
                    for (auto const& child : children())
                        if (&*child == &aChild)
                            return true;
                    return false;
                }
                bool has_parent() const
                {
                    return iParent != nullptr;
                }
                node const& parent() const
                {
                    if (iParent != nullptr)
                        return *iParent;
                    throw std::logic_error("neos::language::ast::node::parent");
                }
                node& parent()
                {
                    if (iParent != nullptr)
                        return *iParent;
                    throw std::logic_error("neos::language::ast::node::parent");
                }
                children_t const& children() const
                {
                    return iChildren;
                }
                children_t& children()
                {
                    return iChildren;
                }
            public:
                neolib::i_string_view const& source() const
                {
                    neolib::i_string_view const* result = nullptr;
                    std::visit([&](auto const& lhs)
                        {
                            if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                                neolib::ref_ptr<i_semantic_concept>>)
                            {
                                result = &lhs->source();
                            }
                        }, *this);
                    if (result != nullptr)
                        return *result;
                    throw std::logic_error("neos::language::ast::node::source()");
                }
            public:
                bool can_fold() const
                {
                    bool canFold = false;
                    std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            canFold = lhs->can_fold();
                        }
                    }, *this);
                    return canFold;
                }
                bool can_fold(node const& aRhs) const
                {
                    bool canFold = false;
                    std::visit([&](auto const& lhs)
                        {
                            if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                                neolib::ref_ptr<i_semantic_concept>>)
                            {
                                std::visit([&](auto const& rhs)
                                {
                                    if constexpr (std::is_same_v<std::decay_t<decltype(rhs)>,
                                        neolib::ref_ptr<i_semantic_concept>>)
                                    {
                                        canFold = lhs->can_fold(*rhs);
                                    }
                                }, aRhs);
                            }
                        }, *this);
                    return canFold;
                }
                std::shared_ptr<node> fold(i_context& aContext)
                {
                    std::visit([&](auto const& lhs)
                        {
                            if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                                neolib::ref_ptr<i_semantic_concept>>)
                            {
                                auto result = lhs->fold(aContext);
                                *this = result;
                            }
                        }, *this);
                    return shared_from_this();
                }
                std::shared_ptr<node> fold(i_context& aContext, node const& aRhs)
                {
                    std::visit([&](auto const& lhs)
                        {
                            if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                                neolib::ref_ptr<i_semantic_concept>>)
                            {
                                std::visit([&](auto const& rhs)
                                    {
                                        if constexpr (std::is_same_v<std::decay_t<decltype(rhs)>,
                                            neolib::ref_ptr<i_semantic_concept>>)
                                        {
                                            auto result = lhs->fold(aContext, *rhs);
                                            *this = result;
                                        }
                                    }, aRhs);
                            }
                        }, *this);
                    return shared_from_this();
                }
            public:
                std::string trace() const
                {
                    std::string result;
                    std::visit([&](auto const& lhs)
                        {
                            if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                                neolib::ref_ptr<i_semantic_concept>>)
                            {
                                if (lhs)
                                    result = lhs->trace();
                                else
                                    result = "()";
                            }
                        }, *this);
                    return result;
                }
            private:
                node* iParent = nullptr;
                children_t iChildren;
            };
        public:
            ast(symbol_table& aSymbolTable) : iSymbolTable{ aSymbolTable }
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
            std::shared_ptr<node> root() const
            {
                return iRoot;
            }
        private:
            symbol_table& iSymbolTable;
            std::shared_ptr<node> iRoot = std::make_shared<node>();
        };
    }
}