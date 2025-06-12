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
#include <neolib/core/vector.hpp>
#include <neos/language/i_semantic_concept.hpp>
#include <neos/language/symbols.hpp>

namespace neos
{
    namespace language
    {
        using ast_node_value = neolib::variant<neolib::ref_ptr<i_semantic_concept>, symbol_table_pointer>;
        using i_ast_node_value = neolib::abstract_t<ast_node_value>;

        class i_ast_node : public neolib::i_reference_counted
        {
        public:
            using abstract_type = i_ast_node;
        public:
            using children_t = neolib::i_vector<neolib::i_ref_ptr<i_ast_node>>;
        public:
            virtual ~i_ast_node() = default;
        public:
            virtual void name(neolib::i_string& aResult) const = 0;
            virtual bool is_empty() const = 0;
            virtual bool holds_data() const = 0;
            virtual i_ast_node_value const& value() const = 0;
            virtual i_ast_node_value& value() = 0;
        public:
            virtual bool is_sibling(i_ast_node const& aSibling) const = 0;
            virtual bool is_child(i_ast_node const& aChild) const = 0;
            virtual bool is_descendent(i_ast_node const& aChild) const = 0;
            virtual bool has_parent() const = 0;
            virtual i_ast_node& parent() const = 0;
            virtual children_t& children() const = 0;
        public:
            virtual neolib::i_string_view const& source() const = 0;
        public:
            virtual bool has_ghosts() const = 0;
            virtual bool unstructured() const = 0;
            virtual bool can_fold() const = 0;
            virtual bool can_fold(i_ast_node const& aRhs) const = 0;
            virtual void fold(i_context& aContext, neolib::i_ref_ptr<i_ast_node>& aResult) = 0;
            virtual void fold(i_context& aContext, i_ast_node const& aRhs, neolib::i_ref_ptr<i_ast_node>& aResult) = 0;
        public:
            virtual void trace(neolib::i_string& aResult) const = 0;
            // helpers
        public:
            std::string name() const
            {
                neolib::string result;
                name(result);
                return result.to_std_string();
            }
        public:
            neolib::ref_ptr<i_ast_node> fold(i_context& aContext)
            {
                neolib::ref_ptr<i_ast_node> result;
                fold(aContext, result);
                return result;
            }
            neolib::ref_ptr<i_ast_node> fold(i_context& aContext, i_ast_node const& aRhs)
            {
                neolib::ref_ptr<i_ast_node> result;
                fold(aContext, aRhs, result);
                return result;
            }
        public:
            std::string trace() const
            {
                neolib::string result;
                trace(result);
                return result.to_std_string();
            }
        };

        class ast_node : public neolib::reference_counted<i_ast_node>
        {
        public:
            using abstract_type = i_ast_node;
        public:
            using value_type = ast_node_value;
            using children_t = neolib::vector<neolib::ref_ptr<i_ast_node>>;
        public:
            ast_node()
            {
            }
            ast_node(value_type const& aValue, i_ast_node& aParent) :
                iParent{ &aParent },
                iValue{ aValue }
            {
            }
        public:
            using i_ast_node::name;
            void name(neolib::i_string& aResult) const final
            {
                std::string result;
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            result = lhs->name();
                        }
                    }, value());
                if (result.empty())
                    throw std::logic_error("neos::language::ast::ast_node::name");
                aResult = result;
            }
            bool is_empty() const final
            {
                return std::holds_alternative<std::monostate>(value()) ||
                    (std::holds_alternative<neolib::ref_ptr<i_semantic_concept>>(value()) &&
                        !std::get<neolib::ref_ptr<i_semantic_concept>>(value()).valid());
            }
            bool holds_data() const final
            {
                return !is_empty() &&
                    (std::holds_alternative<neolib::ref_ptr<i_semantic_concept>>(value()) &&
                        std::get<neolib::ref_ptr<i_semantic_concept>>(value())->holds_data());
            }
            ast_node_value const& value() const final
            {
                return iValue;
            }
            ast_node_value& value() final
            {
                return iValue;
            }
        public:
            bool is_sibling(i_ast_node const& aSibling) const final
            {
                return iParent == &aSibling.parent();
            }
            bool is_child(i_ast_node const& aChild) const final
            {
                for (auto const& child : children())
                    if (&*child == &aChild)
                        return true;
                return false;
            }
            bool is_descendent(i_ast_node const& aChild) const final
            {
                for (auto const& child : children())
                    if (&*child == &aChild)
                        return true;
                    else if (child->is_descendent(aChild))
                        return true;
                return false;
            }
            bool has_parent() const final
            {
                return iParent != nullptr;
            }
            i_ast_node& parent() const final
            {
                if (iParent != nullptr)
                    return *iParent;
                throw std::logic_error("neos::language::ast::ast_node::parent");
            }
            children_t& children() const final
            {
                return *iChildren;
            }
        public:
            neolib::i_string_view const& source() const final
            {
                neolib::i_string_view const* result = nullptr;
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            result = &lhs->source();
                        }
                    }, value());
                if (result != nullptr)
                    return *result;
                throw std::logic_error("neos::language::ast::ast_node::source");
            }
        public:
            bool has_ghosts() const final
            {
                bool result = false;
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            result = lhs->has_ghosts();
                        }
                    }, value());
                return result;
            }
            bool unstructured() const final
            {
                bool result = false;
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            result = lhs->unstructured();
                        }
                    }, value());
                return result;
            }
            bool can_fold() const final
            {
                bool canFold = false;
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            canFold = lhs->can_fold();
                        }
                    }, value());
                return canFold;
            }
            bool can_fold(i_ast_node const& aRhs) const final
            {
                bool canFold = false;
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            if (aRhs.value().holds_alternative<neolib::ref_ptr<i_semantic_concept>>())
                                canFold = lhs->can_fold(*aRhs.value().get<neolib::ref_ptr<i_semantic_concept>>());
                        }
                    }, value());
                return canFold;
            }
            using i_ast_node::fold;
            void fold(i_context& aContext, neolib::i_ref_ptr<i_ast_node>& aResult) final
            {
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            value() = lhs->fold(aContext);
                        }
                    }, value());
                aResult.reset(this);
            }
            void fold(i_context& aContext, i_ast_node const& aRhs, neolib::i_ref_ptr<i_ast_node>& aResult) final
            {
                std::visit([&](auto const& lhs)
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(lhs)>,
                            neolib::ref_ptr<i_semantic_concept>>)
                        {
                            if (aRhs.value().holds_alternative<neolib::ref_ptr<i_semantic_concept>>())
                                value() = lhs->fold(aContext, *aRhs.value().get<neolib::ref_ptr<i_semantic_concept>>());
                        }
                    }, value());
                aResult.reset(this);
            }
        public:
            using i_ast_node::trace;
            void trace(neolib::i_string& aResult) const final
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
                    }, value());
                aResult = result;
            }
        private:
            i_ast_node* iParent = nullptr;
            std::unique_ptr<children_t> iChildren = std::make_unique<children_t>();
            ast_node_value iValue;
        };

        namespace
        {
            ast_node n;
        }

        class ast
        {
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
            neolib::ref_ptr<ast_node> root() const
            {
                return iRoot;
            }
        private:
            symbol_table& iSymbolTable;
            neolib::ref_ptr<ast_node> iRoot = neolib::make_ref<ast_node>();
        };
    }
}