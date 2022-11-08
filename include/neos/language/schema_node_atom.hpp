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
#include <neolib/core/list.hpp>
#include <neolib/core/pair.hpp>
#include <neolib/core/map.hpp>
#include <neolib/core/reference_counted.hpp>
#include <neos/language/i_concept_atom.hpp>
#include <neos/language/i_schema_node_atom.hpp>
#include <neos/language/i_schema_terminal_atom.hpp>
#include <neos/language/atom.hpp>
#include <neos/language/concept.hpp>

namespace neos::language
{
    class schema_node_atom : public atom<i_schema_node_atom>
    {
    public:
        typedef neolib::string symbol_t;
        typedef neolib::list<neolib::ref_ptr<i_concept>> concept_list_t;
        typedef concept_list_t is_t;
        typedef neolib::list<neolib::ref_ptr<i_atom>> atom_list_t;
        typedef atom_list_t expects_t;
        typedef neolib::pair<neolib::ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_map_list_entry_t;
        typedef neolib::list<atom_map_list_entry_t> atom_map_list_t;
        typedef i_schema_node_atom::tokens_t abstract_tokens_t;
        typedef atom_map_list_t tokens_t;
        typedef neolib::map<neolib::ref_ptr<i_atom>, neolib::ref_ptr<i_atom>> atom_map_t;
        typedef atom_map_t children_t;
    private:
        typedef std::unordered_map<const i_atom*, const i_atom*> token_cache_t; // for packrat memoization.
    public:
        schema_node_atom(i_schema_atom& aParent, const std::string& aSymbol) :
            atom<i_schema_node_atom>{ aParent }, iSymbol{ aSymbol }, iExpectNone{ false }
        {
        }
		schema_node_atom() :
            atom<i_schema_node_atom>{}, iExpectNone{ false }
        {
        }
    public:
        const i_schema_atom& parent() const override
        {
            return static_cast<const i_schema_atom&>(atom<i_schema_node_atom>::parent());
        }
        i_schema_atom& parent() override
        {
            return static_cast<i_schema_atom&>(atom<i_schema_node_atom>::parent());
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
        bool is_string_token_node() const override
        {
            return is_token_node() && token().is_schema_atom() &&
                token().as_schema_atom().is_schema_terminal_atom() &&
                token().as_schema_atom().as_schema_terminal_atom().type() == schema_terminal::String;
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
        bool is_conceptually_the_same(const i_concept& aConcept) const override
        {
            for (auto const& concept_ : is())
                if (concept_->is_same(aConcept))
                    return true;
            if (as() && as()->is_same(aConcept))
                return true;
            return false;
        }
        bool is_conceptually_related_to(const i_concept& aConcept) const override
        {
            for (auto const& concept_ : is())
                if (concept_->is_related_to(aConcept))
                    return true;
            if (as() && as()->is_related_to(aConcept))
                return true;
            return false;
        }
    public:
        const is_t& is() const override
        {
            return iIs;
        }
        is_t& is() override
        {
            return iIs;
        }
        const as_t& as() const override
        {
            return iAs;
        }
        as_t& as() override
        {
            return iAs;
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
        const i_atom& context() const override
        {
            if (!is_token_node())
                return *this;
            else if (has_parent())
                return parent().as_schema_node_atom().context();
            else
                throw no_context();
        }
        i_atom& context() override
        {
            return const_cast<i_atom&>(const_cast<schema_node_atom const&>(*this).context());
        }
        const i_atom* find_token(const i_atom& aToken) const override
        {
            auto iterCacheToken = iTokenCache.find(&aToken);
            if (iterCacheToken == iTokenCache.end())
            {
                auto iterToken = std::find_if(tokens().begin(), tokens().end(), [&](auto&& aTokenMapEntry)
                {
                    auto const& token = *aTokenMapEntry.first();
                    return token.is_conceptually_related_to(aToken);
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
        symbol_t iSymbol;
		neolib::ref_ptr<i_atom> iToken;
        is_t iIs;
        neolib::ref_ptr<i_concept> iAs;
        expects_t iExpects;
        bool iExpectNone;
        tokens_t iTokens;
        children_t iChildren;
        mutable token_cache_t iTokenCache;
    };
}
