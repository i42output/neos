/*
  scope.hpp

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
#include <unordered_map>
#include <neolib/core/reference_counted.hpp>
#include <neolib/core/vector.hpp>
#include <neolib/core/string.hpp>
#include <neos/language/function.hpp>

namespace neos
{
    namespace language
    {
        using i_scope_name = neolib::i_string;
        using scope_name = neolib::string;

        enum class scope_type : std::uint32_t
        {
            Program,
            Namespace,
            Class,
            Function,
            Block
        };

        class i_scope : public neolib::i_reference_counted
        {
        public:
            using abstract_type = i_scope;
            using i_child_list = neolib::i_vector<neolib::i_ref_ptr<i_scope>>;
        public:
            virtual ~i_scope() = default;
        public:
            virtual bool has_parent() const = 0;
            virtual i_scope const& parent() const = 0;
            virtual i_child_list const& children() const = 0;
            virtual i_child_list& children() = 0;
            virtual i_scope_name const& name() const = 0;
            virtual i_scope_name const& qualified_name() const = 0;
            virtual scope_type type() const = 0;
        public:
            virtual i_scope& create_child(i_scope_name const& aName, scope_type aType) = 0;
        };

        class i_function_scope : public i_scope
        {
        public:
            using abstract_type = i_function_scope;
        public:
            virtual i_function_signature const& function_signature() const = 0;
            virtual void set_function_signature(i_function_signature const& aSignature) = 0;
        };

        template <typename Base = i_scope>
        class scope : public neolib::reference_counted<Base>
        {
        public:
            using child_list = neolib::vector<neolib::ref_ptr<i_scope>>;
        private:
            using index = std::unordered_map<std::string_view, child_list::iterator>;
        public:
            scope() :
                iParent{ nullptr },
                iName{},
                iType{ scope_type::Program }
            {}
            scope(i_scope_name const& aName, scope_type aType) :
                iParent{ nullptr },
                iName{ aName },
                iType{ aType }
            {}
            scope(i_scope& aParent, i_scope_name const& aName, scope_type aType) :
                iParent{ &aParent },
                iName{ aName },
                iType{ aType }
            {}
        public:
            bool has_parent() const final
            {
                return iParent != nullptr;
            }
            i_scope const& parent() const final
            {
                if (iParent)
                    return *iParent;
                throw std::logic_error("neos::scope: no parent!");
            }
            child_list const& children() const final
            {
                return iChildren;
            }
            child_list& children() final
            {
                return iChildren;
            }
            scope_name const& name() const final
            {
                return iName;
            }
            scope_name const& qualified_name() const final
            {
                if (iQualifiedName.has_value())
                    return iQualifiedName.value();
                if (has_parent())
                    iQualifiedName = parent().qualified_name() + "::" + name();
                else
                    iQualifiedName = name();
                return iQualifiedName.value();
            }
            scope_type type() const final
            {
                return iType;
            }
        public:
            i_scope& create_child(i_scope_name const& aName, scope_type aType) final;
        private:
            i_scope* iParent;
            neolib::string iName;
            mutable std::optional<neolib::string> iQualifiedName;
            scope_type iType;
            child_list iChildren;
            index iChildIndex;
        };

        class function_scope : public scope<i_function_scope>
        {
        public:
            using scope::scope;
        public:
            i_function_signature const& function_signature() const final
            {
                return iFunctionSignature;
            }
            void set_function_signature(i_function_signature const& aSignature) final
            {
                iFunctionSignature = aSignature;
            }
        private:
            language::function_signature iFunctionSignature;
        };

        template <typename Base>
        inline i_scope& scope<Base>::create_child(i_scope_name const& aName, scope_type aType)
        {
            auto const indexed = iChildIndex.find(aName.to_std_string_view());
            if (indexed != iChildIndex.end())
                return **indexed->second;
            switch (aType)
            {
            case scope_type::Namespace:
                iChildIndex[aName.to_std_string_view()] = children().insert(children().end(), child_list::value_type{ neolib::make_ref<scope<>>(*this, aName, aType) });
                break;
            case scope_type::Class:
                iChildIndex[aName.to_std_string_view()] = children().insert(children().end(), child_list::value_type{ neolib::make_ref<scope<>>(*this, aName, aType) });
                break;
            case scope_type::Function:
                iChildIndex[aName.to_std_string_view()] = children().insert(children().end(), child_list::value_type{ neolib::make_ref<function_scope>(*this, aName, aType) });
                break;
            case scope_type::Block:
                iChildIndex[aName.to_std_string_view()] = children().insert(children().end(), child_list::value_type{ neolib::make_ref<scope<>>(*this, aName, aType) });
                break;
            }
            return **iChildIndex[aName.to_std_string_view()];
        }

    }
}