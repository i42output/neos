/*
  concept.hpp

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
#include <neolib/string.hpp>
#include <neos/language/i_concept.hpp>

namespace neos::language
{
    class neos_concept : public neolib::reference_counted<i_concept>
    {
    public:
        neos_concept(const std::string& aName) :
            iParent{ nullptr }, iName { aName}
        {
        }
        neos_concept(i_concept& aParent, const std::string& aName) :
            iParent{ &aParent }, iName{ aName }
        {
        }
    public:
        bool has_parent() const override
        {
            return iParent != nullptr;
        }
        const i_concept& parent() const override
        {
            if (has_parent())
                return *iParent;
            throw no_parent();
        }
        i_concept& parent() override
        {
            if (has_parent())
                return *iParent;
            throw no_parent();
        }
        const neolib::i_string& name() const override
        {
            return iName;
        }
    public:
        source_iterator consume_token(compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
        source_iterator consume_atom(compiler_pass aPass, const i_atom& aAtom, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = true;
            return aSource;
        }
    private:
        i_concept* iParent;
        neolib::string iName;
    };

    class unimplemented_concept : public neos_concept
    {
    public:
        using neos_concept::neos_concept;
    };
}
