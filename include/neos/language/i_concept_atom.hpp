/*
  i_concept_atom.hpp

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
#include <neolib/core/i_reference_counted.hpp>
#include <neolib/core/i_string.hpp>
#include <neos/language/i_concept.hpp>
#include <neos/language/i_atom.hpp>

namespace neos
{
    namespace language
    {
        class i_concept_atom : public i_atom
        {
        public:
            typedef i_atom::symbol_t symbol_t;
        public:
            bool is_schema_atom() const override { return false; }
            const i_schema_atom& as_schema_atom() const override { throw wrong_type(); }
            i_schema_atom& as_schema_atom() override { throw wrong_type(); }
            bool is_concept_atom() const override { return true; }
            const i_concept_atom& as_concept_atom() const override { return *this; }
            i_concept_atom& as_concept_atom() override { return *this; }
        public:
            virtual const i_concept& get_concept() const = 0;
            virtual i_concept& get_concept() = 0;
        };
    }
}