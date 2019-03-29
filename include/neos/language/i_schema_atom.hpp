/*
  i_schema_atom.hpp

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
#include <neos/language/i_atom.hpp>

namespace neos
{
    namespace language
    {
        class i_schema_node_atom;
        class i_schema_terminal_atom;

        class i_schema_atom : public i_atom
        {
        public:
            bool is_schema_atom() const override { return true; }
            const i_schema_atom& as_schema_atom() const override { return *this; }
            i_schema_atom& as_schema_atom() override { return *this; }
            bool is_concept_atom() const override { return false; }
            const i_concept_atom& as_concept_atom() const override { throw wrong_type(); }
            i_concept_atom& as_concept_atom() override { throw wrong_type(); }
        public:
            virtual bool is_schema_node_atom() const = 0;
            virtual const i_schema_node_atom& as_schema_node_atom() const = 0;
            virtual i_schema_node_atom& as_schema_node_atom() = 0;
            virtual bool is_schema_terminal_atom() const = 0;
            virtual const i_schema_terminal_atom& as_schema_terminal_atom() const = 0;
            virtual i_schema_terminal_atom& as_schema_terminal_atom() = 0;
        };
    }
}