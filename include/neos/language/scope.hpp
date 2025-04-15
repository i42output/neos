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

namespace neos
{
    namespace language
    {
        using scope_name = neolib::string;

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
            virtual neolib::abstract_t<scope_name> const& name() const = 0;
            virtual neolib::abstract_t<scope_name> const& qualified_name() const = 0;
        public:
            virtual i_scope& create_child(neolib::abstract_t<scope_name> const& aName) = 0;
        };

        class scope : public neolib::reference_counted<i_scope>
        {
        public:
            using child_list = neolib::vector<neolib::ref_ptr<i_scope>>;
        private:
            using index = std::unordered_map<std::string_view, child_list::iterator>;
        public:
            scope() :
                iParent{ nullptr },
                iName{}
            {}
            scope(neolib::abstract_t<scope_name> const& aName) :
                iParent{ nullptr },
                iName{ aName }
            {}
            scope(i_scope& aParent, neolib::abstract_t<scope_name> const& aName) :
                iParent{ &aParent },
                iName{ aName }
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
                throw std::logic_error("neos::language::scope: no parent!");
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
        public:
            i_scope& create_child(neolib::abstract_t<scope_name> const& aName) final
            {
                auto const indexed = iChildIndex.find(aName.to_std_string_view());
                if (indexed != iChildIndex.end())
                    return **indexed->second;
                auto const newChild = children().insert(children().end(), neolib::make_ref<scope>(*this, aName));
                iChildIndex[aName.to_std_string_view()] = newChild;
                return **newChild;
            }
        private:
            i_scope* iParent;
            neolib::string iName;
            mutable std::optional<neolib::string> iQualifiedName;
            child_list iChildren;
            index iChildIndex;
        };
    }
}