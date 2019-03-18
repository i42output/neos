/*
  i_atom.hpp

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
#include <neolib/i_reference_counted.hpp>
#include <neolib/i_string.hpp>

namespace neos
{
    namespace language
    {
        class i_schema_atom;
        class i_concept_atom;

        class i_atom : public neolib::i_reference_counted
        {
        public:
            struct no_parent : std::logic_error { no_parent() : std::logic_error("neos::language::i_atom::no_parent") {} };
            struct wrong_type : std::logic_error { wrong_type() : std::logic_error("neos::language::i_atom::wrong_type") {} };
        public:
            typedef neolib::i_string symbol_t;
        public:
            virtual bool has_parent() const = 0;
            virtual const i_atom& parent() const = 0;
            virtual i_atom& parent() = 0;
            virtual const symbol_t& symbol() const = 0;
        public:
            virtual bool is_schema_atom() const = 0;
            virtual const i_schema_atom& as_schema_atom() const = 0;
            virtual i_schema_atom& as_schema_atom() = 0;
            virtual bool is_concept_atom() const = 0;
            virtual const i_concept_atom& as_concept_atom() const = 0;
            virtual i_concept_atom& as_concept_atom() = 0;
        public:
            bool operator<(const i_atom& rhs) const
            {
                return symbol() < rhs.symbol();
            }
        };
    }
}