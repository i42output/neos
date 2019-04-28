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

namespace neos::language
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
    private:
        typedef std::unordered_map<const i_atom*, const i_atom*> token_cache_t; // for packrat memoization.
    public:
        schema_node_atom(i_schema_atom& aParent, const std::string& aSymbol) :
			iParent{ &aParent }, iSymbol{ aSymbol }, iExpectNone{ false }
        {
        }
		schema_node_atom() :
            iParent{ nullptr }, iExpectNone{ false }
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
		bool is_token_node() const override
		{
			return iToken != nullptr;
		}
		const i_atom& token() const override
		{
			if (is_token_node())
				return *iToken;
			throw not_a_token_node();
		}
		neolib::i_ref_ptr<i_atom>& token_ref_ptr() override
		{
			return iToken;
		}
        bool is_concept(const i_concept& aConcept) const override 
        { 
            for (auto const& concept : is_a())
                if (aConcept.is_related_to(*concept))
                    return true;
            return false;
        }
        bool is_related_to(const i_concept& aConcept) const override
        {
            for (auto const& concept : is_a())
                if (concept->is_related_to(aConcept))
                    return true;
            return false;
        }
    public:
        bool operator==(const i_atom& rhs) const override
        {
            return this == &rhs;
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
        bool expect_none() const override
        {
            return iExpectNone;
        }
        void set_expect_none() override
        {
            iExpectNone = true;
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
        const i_atom* find_token(const i_atom& aToken) const override
        {
            auto iterCacheToken = iTokenCache.find(&aToken);
            if (iterCacheToken == iTokenCache.end())
            {
                auto iterToken = std::find_if(tokens().begin(), tokens().end(), [&](auto&& aTokenMapEntry)
                {
                    auto const& token = *aTokenMapEntry.first();
                    auto const& newTokenValue = *aTokenMapEntry.second();
                    return (token == aToken ||
                        (token.is_concept_atom() && aToken.is_concept_atom() &&
                            token.as_concept_atom().concept().is_ancestor_of(aToken.as_concept_atom().concept())));
                });
                iterCacheToken = iTokenCache.emplace(&aToken, iterToken != tokens().end() ? &*iterToken->second() : nullptr).first;
            }
            return iterCacheToken->second;
        }
        uint32_t recursive_token(const i_atom& aToken) const override
        {
            if (is_token_node() && token() == aToken)
                return 1u;
            else if (has_parent() && parent().as_schema_node_atom().is_token_node())
                return 1u + parent().as_schema_node_atom().recursive_token(aToken);
            else
                return 0u;
        }
    private:
        i_schema_atom* iParent;
        symbol_t iSymbol;
		neolib::ref_ptr<i_atom> iToken;
        is_a_t iIsConcepts;
        expects_t iExpects;
        bool iExpectNone;
        tokens_t iTokens;
        children_t iChildren;
        mutable token_cache_t iTokenCache;
    };
}
