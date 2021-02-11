/*
  atom.hpp

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
#include <neolib/core/reference_counted.hpp>
#include <neos/language/i_concept_atom.hpp>

namespace neos
{
    namespace language
    {
        template <typename Interface>
        class atom : public neolib::reference_counted<Interface>
        {
        public:
            using i_atom::no_parent;
        public:
            atom() :
                iParent{ nullptr }
            {
            }
            atom(i_atom& aParent) :
                iParent{ &aParent }
            {
            }
        public:
            bool has_parent() const override
            {
                return iParent != nullptr;
            }
            const i_atom& parent() const override
            {
                if (has_parent())
                    return *iParent;
                throw no_parent();
            }
            i_atom& parent() override
            {
                if (has_parent())
                    return *iParent;
                throw no_parent();
            }
        public:
            using i_atom::is_conceptually_the_same;
            bool is_conceptually_the_same(const i_atom& rhs) const override
            {
                if (*this == rhs)
                    return true;
                else if (rhs.is_concept_atom() && is_conceptually_the_same(rhs.as_concept_atom().get_concept()))
                    return true;
                else if (rhs.is_schema_atom() && rhs.as_schema_atom().is_schema_node_atom())
                    for (auto const& c : rhs.as_schema_atom().as_schema_node_atom().is())
                        if (is_conceptually_the_same(*c))
                            return true;
                return false;
            }
            using i_atom::is_conceptually_related_to;
            bool is_conceptually_related_to(const i_atom& rhs) const override
            { 
                if (*this == rhs)
                    return true;
                else if (rhs.is_concept_atom() && is_conceptually_related_to(rhs.as_concept_atom().get_concept()))
                    return true;
                else if (rhs.is_schema_atom() && rhs.as_schema_atom().is_schema_node_atom())
                    for (auto const& c : rhs.as_schema_atom().as_schema_node_atom().is())
                        if (is_conceptually_related_to(*c))
                            return true;
                return false;
            }
        public:
            bool operator==(const i_atom& rhs) const override
            {
                return this == &rhs;
            }
        private:
            i_atom* iParent;
        };
    }
}