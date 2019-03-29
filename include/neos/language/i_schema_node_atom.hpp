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
#include <neolib/i_list.hpp>
#include <neolib/i_map.hpp>
#include <neolib/i_reference_counted.hpp>
#include <neolib/i_string.hpp>
#include <neos/language/i_concept.hpp>
#include <neos/language/i_schema_atom.hpp>

namespace neos
{
    namespace language
    {
        class i_schema_node_atom : public i_schema_atom
        {
        public:
            typedef i_atom::symbol_t symbol_t;
            typedef neolib::i_list<neolib::i_ref_ptr<i_concept>> concept_list_t;
            typedef concept_list_t is_a_t;
            typedef neolib::i_list<neolib::i_ref_ptr<i_atom>> atom_list_t;
            typedef atom_list_t expects_t;
            typedef neolib::i_pair<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>> atom_map_list_entry_t;
            typedef neolib::i_list<atom_map_list_entry_t> atom_map_list_t;
            typedef atom_map_list_t tokens_t;
            typedef neolib::i_map<neolib::i_ref_ptr<i_atom>, neolib::i_ref_ptr<i_atom>> atom_map_t;
            typedef atom_map_t token_map_t;
            typedef atom_map_t children_t;
        public:
            bool is_schema_node_atom() const override { return true; }
            const i_schema_node_atom& as_schema_node_atom() const override { return *this; }
            i_schema_node_atom& as_schema_node_atom() override { return *this; }
            bool is_schema_terminal_atom() const override { return false; }
            const i_schema_terminal_atom& as_schema_terminal_atom() const override { throw wrong_type(); }
            i_schema_terminal_atom& as_schema_terminal_atom() override { throw wrong_type(); }
        public:
            virtual const is_a_t& is_a() const = 0;
            virtual is_a_t& is_a() = 0;
            virtual const expects_t& expects() const = 0;
            virtual expects_t& expects() = 0;
            virtual const tokens_t& tokens() const = 0;
            virtual tokens_t& tokens() = 0;
            virtual const children_t& children() const = 0;
            virtual children_t& children() = 0;
        };
    }
}