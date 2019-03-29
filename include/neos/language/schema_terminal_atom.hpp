/*
  schema_terminal_atom.hpp

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
#include <neolib/reference_counted.hpp>
#include <neos/language/i_schema_terminal_atom.hpp>

namespace neos
{
    namespace language
    {
        class schema_terminal_atom : public neolib::reference_counted<i_schema_terminal_atom>
        {
        public:
            schema_terminal_atom(i_schema_atom& aParent, schema_terminal aType) :
                iParent{ &aParent }, iType{ aType }
            {
            }
            schema_terminal_atom() :
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
                throw wrong_type();
            }
        public:
            schema_terminal type() const override
            {
                return iType;
            }
        private:
            i_schema_atom* iParent;
            schema_terminal iType;
        };
    }
}