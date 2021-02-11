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
#include <neolib/core/reference_counted.hpp>
#include <neos/language/i_schema_terminal_atom.hpp>
#include <neos/language/atom.hpp>

namespace neos
{
    namespace language
    {
        class schema_terminal_atom : public atom<i_schema_terminal_atom>
        {
        public:
            typedef neolib::string symbol_t;
        public:
            schema_terminal_atom(i_schema_atom& aParent, schema_terminal aType, const std::string& aSymbol = std::string{}) :
                atom<i_schema_terminal_atom>{ aParent }, iType{ aType }, iSymbol{ aSymbol }
            {
            }
            schema_terminal_atom() :
                atom<i_schema_terminal_atom>{}
            {
            }
        public:
            const i_schema_atom& parent() const override
            {
                return static_cast<const i_schema_atom&>(atom<i_schema_terminal_atom>::parent());
            }
            i_schema_atom& parent() override
            {
                return static_cast<i_schema_atom&>(atom<i_schema_terminal_atom>::parent());
            }
            const symbol_t& symbol() const override
            {
                return iSymbol;
            }
        public:
            bool is_conceptually_the_same(const i_concept&) const override
            {
                return false;
            }
            bool is_conceptually_related_to(const i_concept&) const override
            {
                return false;
            }
        public:
            bool operator==(const i_atom& rhs) const override
            {
                return this == &rhs ||
                    (type() == schema_terminal::String &&
                        rhs.is_schema_atom() && rhs.as_schema_atom().is_schema_terminal_atom() && rhs.as_schema_atom().as_schema_terminal_atom().type() == schema_terminal::String &&
                        symbol() == rhs.symbol());
            }
        public:
            schema_terminal type() const override
            {
                return iType;
            }
        private:
            schema_terminal iType;
            symbol_t iSymbol;
        };
    }
}