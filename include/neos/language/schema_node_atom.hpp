/*
  schema_node_atom.hpp

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
#include <neos/language/i_schema_node_atom.hpp>

namespace neos
{
    namespace language
    {
        class schema_node_atom : public neolib::reference_counted<i_schema_node_atom>
        {
        public:
            typedef neolib::string symbol_t;
            typedef neolib::list<neolib::i_ref_ptr<i_concept>, neolib::ref_ptr<i_concept>> concept_list_t;
            typedef concept_list_t is_a_t;
            typedef neolib::list<neolib::i_ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_list_t;
            typedef atom_list_t expects_t;
            typedef neolib::pair<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>, neolib::ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_map_list_entry_t;
            typedef neolib::list<i_schema_node_atom::atom_map_list_entry_t, atom_map_list_entry_t> atom_map_list_t;
            typedef i_schema_node_atom::tokens_t abstract_tokens_t;
            typedef atom_map_list_t tokens_t;
            typedef neolib::map<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>, neolib::ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_map_t;
            typedef atom_map_t token_map_t;
            typedef atom_map_t children_t;
        public:
            schema_node_atom(i_schema_atom& aParent, const std::string& aSymbol) :
                iParent{ &aParent }, iSymbol { aSymbol }
            {
            }
            schema_node_atom() :
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
            bool is_concept(const i_concept& aConcept) const override 
            { 
                for (auto const& concept : is_a())
                    if (&*concept == &aConcept || aConcept.is_ancestor_of(*concept))
                        return true;
                return false;
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
            const abstract_tokens_t& default_tokens() const override
            {
                if (!iDefaultTokens.empty() || !has_parent())
                    return iDefaultTokens;
                return parent().as_schema_node_atom().default_tokens();
            }
            abstract_tokens_t& default_tokens() override
            {
                return const_cast<abstract_tokens_t&>(const_cast<const schema_node_atom*>(this)->default_tokens());
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
            i_schema_atom* iParent;
            symbol_t iSymbol;
            is_a_t iIsConcepts;
            expects_t iExpects;
            tokens_t iTokens;
            tokens_t iDefaultTokens;
            children_t iChildren;
        };
    }
}