/*
  i_concept.hpp

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
#include <neolib/map.hpp>
#include <neos/language/i_library.hpp>
#include <neos/language/concept.hpp>

namespace neos
{
    namespace language
    {
        class library : public i_library
        {
        public:
            typedef neolib::map<neolib::i_string, i_concept, neolib::string, _concept> concept_list_t;
        public:
            virtual const neolib::i_string& name() const = 0;
            virtual const concept_list_t& concepts() const = 0;
        public:
            static const neolib::uuid& id() { static neolib::uuid sId = neolib::make_uuid("FCE746F0-D650-44E7-99F6-2F442B694F06"); return sId; }
        };
    }
}