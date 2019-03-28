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
#include <neos/language/i_schema_atom.hpp>

namespace neos
{
    namespace language
    {
        class schema_atom : public neolib::reference_counted<i_schema_atom>
        {
        public:
            typedef neolib::string symbol_t;
            typedef neolib::list<neolib::i_ref_ptr<i_concept>, neolib::ref_ptr<i_concept>> concept_list_t;
            typedef concept_list_t is_a_t;
            typedef neolib::list<neolib::i_ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_list_t;
            typedef atom_list_t expects_t;
            typedef neolib::pair<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>, neolib::ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_map_list_entry_t;
            typedef neolib::list<i_schema_atom::atom_map_list_entry_t, atom_map_list_entry_t> atom_map_list_t;
            typedef atom_map_list_t tokens_t;
            typedef neolib::map<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>, neolib::ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_map_t;
            typedef atom_map_t token_map_t;
            typedef atom_map_t children_t;
        public:
            schema_atom(i_schema_atom& aParent, const std::string& aSymbol) :
                iParent{ &aParent }, iSymbol { aSymbol }
            {
            }
            schema_atom() :
                iParent{ nullptr }
            {
            }
        public:
            bool has_parent() const override
            {
                return iParent != nullptr;
            }
            const i_schema_atom& parent() const override
            {
                if (has_parent())
                    return *iParent;
                throw no_parent();
            }
            i_schema_atom& parent() override
            {
                if (has_parent())
                    return *iParent;
                throw no_parent();
            }
            const symbol_t& symbol() const override
            {
                return iSymbol;
            }
        public:
            const is_a_t& is_a() const override
            {
                return iIsConcepts;
            }
            is_a_t& is_a() override
            {
                return iIsConcepts;
            }
            const expects_t& expects() const override
            {
                return iExpects;
            }
            expects_t& expects() override
            {
                return iExpects;
            }
            const tokens_t& tokens() const override
            {
                return iTokens;
            }
            tokens_t& tokens() override
            {
                return iTokens;
            }
            const children_t& children() const override
            {
                return iChildren;
            }
            children_t& children() override
            {
                return iChildren;
            }
        private:
            symbol_t iSymbol;
            i_schema_atom* iParent;
            is_a_t iIsConcepts;
            expects_t iExpects;
            tokens_t iTokens;
            children_t iChildren;
        };
    }
}