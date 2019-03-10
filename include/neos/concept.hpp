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
#include <neos/i_concept.hpp>

namespace neos
{
    class neos_concept : public neolib::reference_counted<i_concept>
    {
    public:
        neos_concept(const std::string& aName) :
            iName{ aName }
        {
        }
    public:
        const neolib::i_string& name() const override
        {
            return iName;
        }
    private:
        neolib::string iName;
    };
}