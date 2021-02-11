/*
  concept_atom.hpp

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
#include <neolib/core/list.hpp>
#include <neolib/core/pair.hpp>
#include <neolib/core/map.hpp>
#include <neolib/core/reference_counted.hpp>
#include <neos/language/i_concept_atom.hpp>
#include <neos/language/atom.hpp>

namespace neos
{
    namespace language
    {
        class concept_atom : public atom<i_concept_atom>
        {
        public:
            typedef neolib::string symbol_t;
        public:
            concept_atom(const neolib::i_ref_ptr<i_concept>& aConcept) : 
                atom<i_concept_atom>{}, iConcept { aConcept }
            {
            }
            concept_atom(i_concept_atom& aParent, const neolib::i_ref_ptr<i_concept>& aConcept) :
                atom<i_concept_atom>{ aParent }, iConcept{ aConcept }
            {
            }
        public:
            const i_concept_atom& parent() const override
            {
                return static_cast<const i_concept_atom&>(atom<i_concept_atom>::parent());
            }
            i_concept_atom& parent() override
            {
                return static_cast<i_concept_atom&>(atom<i_concept_atom>::parent());
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
            bool is_conceptually_the_same(const i_concept& aConcept) const override
            {
                return get_concept().is_same(aConcept);
            }
            bool is_conceptually_related_to(const i_concept& aConcept) const override
            { 
                return get_concept().is_related_to(aConcept);
            }
        public:
            const i_concept& get_concept() const override
            {
                return *iConcept;
            }
            i_concept& get_concept() override
            {
                return *iConcept;
            }
        private:
            neolib::ref_ptr<i_concept> iConcept;
            mutable std::optional<symbol_t> iSymbol;
        };
    }
}