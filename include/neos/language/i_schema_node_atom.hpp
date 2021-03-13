/*
  i_schema_node_atom.hpp

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
#include <neolib/core/i_list.hpp>
#include <neolib/core/i_map.hpp>
#include <neolib/core/i_reference_counted.hpp>
#include <neolib/core/i_string.hpp>
#include <neos/language/i_concept.hpp>
#include <neos/language/i_schema_atom.hpp>

namespace neos::language
{
    class i_schema_node_atom : public i_schema_atom
    {
    public:
        typedef i_atom::symbol_t symbol_t;
        typedef neolib::i_list<neolib::i_ref_ptr<i_concept>> concept_list_t;
        typedef concept_list_t is_t;
        typedef neolib::i_ref_ptr<i_concept> as_t;
        typedef neolib::i_list<neolib::i_ref_ptr<i_atom>> atom_list_t;
        typedef atom_list_t expects_t;
        typedef neolib::i_pair<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>> atom_map_list_entry_t;
        typedef neolib::i_list<atom_map_list_entry_t> atom_map_list_t;
        typedef atom_map_list_t tokens_t;
        typedef neolib::i_map<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>> atom_map_t;
        typedef atom_map_t token_map_t;
        typedef atom_map_t children_t;
	public:
		struct not_a_token_node : std::logic_error { not_a_token_node() : std::logic_error("neos::language::i_schema_node_atom::not_a_token_node") {} };
        struct no_context : std::logic_error { no_context() : std::logic_error("neos::language::i_schema_node_atom::no_context") {} };
    public:
        bool is_schema_node_atom() const override { return true; }
        const i_schema_node_atom& as_schema_node_atom() const override { return *this; }
        i_schema_node_atom& as_schema_node_atom() override { return *this; }
        bool is_schema_terminal_atom() const override { return false; }
        const i_schema_terminal_atom& as_schema_terminal_atom() const override { throw wrong_type(); }
        i_schema_terminal_atom& as_schema_terminal_atom() override { throw wrong_type(); }
    public:
		virtual bool is_token_node() const = 0;
		virtual const i_atom& token() const = 0; 
		virtual neolib::i_ref_ptr<i_atom>& token_ref_ptr() = 0;
    public:
        virtual const is_t& is() const = 0;
        virtual is_t& is() = 0;
        virtual const as_t& as() const = 0;
        virtual as_t& as() = 0;
        virtual const expects_t& expects() const = 0;
        virtual expects_t& expects() = 0;
        virtual bool expect_none() const = 0;
        virtual void set_expect_none() = 0;
        virtual const tokens_t& tokens() const = 0;
        virtual tokens_t& tokens() = 0;
        virtual const children_t& children() const = 0;
        virtual children_t& children() = 0;
    public:
        virtual const i_atom& context() const = 0;
        virtual i_atom& context() = 0;
        virtual const i_atom* find_token(const i_atom& aToken) const = 0;
        virtual uint32_t recursive_token(const i_atom& aToken) const = 0;
    };
}