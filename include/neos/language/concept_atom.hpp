/*
  schema_concept.hpp

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
#include <neolib/list.hpp>
#include <neolib/pair.hpp>
#include <neolib/map.hpp>
#include <neolib/reference_counted.hpp>
#include <neos/language/i_concept_atom.hpp>

namespace neos
{
    namespace language
    {
        class concept_atom : public neolib::reference_counted<i_concept_atom>
        {
        public:
            typedef neolib::string symbol_t;
        public:
            concept_atom(const neolib::i_ref_ptr<i_concept>& aConcept) : 
                iParent{ nullptr }, iConcept { aConcept }
            {
            }
            concept_atom(i_concept_atom& aParent, const neolib::i_ref_ptr<i_concept>& aConcept) :
                iParent{ &aParent }, iConcept{ aConcept }
            {
            }
        public:
            bool has_parent() const override
            {
                return iParent != nullptr;
            }
            const i_concept_atom& parent() const override
            {
                if (has_parent())
                    return *iParent;
                throw no_parent();
            }
            i_concept_atom& parent() override
            {
                if (has_parent())
                    return *iParent;
                throw no_parent();
            }
            const symbol_t& symbol() const override
            {
                if (iSymbol == std::nullopt)
                    iSymbol = iConcept->name();
                return *iSymbol;
            }
        public:
            bool is_schema_atom() const override 
            { 
                return false; 
            }
            const i_schema_atom& as_schema_atom() const override 
            { 
                throw wrong_type(); 
            }
            i_schema_atom& as_schema_atom() override 
            { 
                throw wrong_type(); 
            }
            bool is_concept_atom() const override 
            { 
                return true; 
            }
            const i_concept_atom& as_concept_atom() const override 
            { 
                return *this; 
            }
            i_concept_atom& as_concept_atom() override 
            { 
                return *this; 
            }
            bool is_concept(const i_concept& aConcept) const override 
            { 
                return aConcept.is_related_to(concept()); 
            }
            bool is_related_to(const i_concept& aConcept) const override 
            { 
                return concept().is_related_to(aConcept); 
            }
        public:
            bool operator==(const i_atom& rhs) const override
            {
                return this == &rhs;
            }
        public:
            const i_concept& concept() const override
            {
                return *iConcept;
            }
            i_concept& concept() override
            {
                return *iConcept;
            }
        private:
            i_concept_atom* iParent;
            neolib::ref_ptr<i_concept> iConcept;
            mutable std::optional<symbol_t> iSymbol;
        };
    }
}