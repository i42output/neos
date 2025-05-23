/*
  object.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

#include <neos/language/semantic_concept.hpp>
#include "object.hpp"

namespace neos::concepts::core
{   
    object::object(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core object concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "object.assign" }] = neolib::make_ref<language::unimplemented_semantic_concept>("object.assign");
    }

    const std::string& object::library_name()
    {
        static const std::string sName = "neos.object";
        return sName;
    }

    const neolib::uuid& object::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("9CB7CA6F-6559-43CE-ADEE-CC8A9ADC08B6");
        return sId;
    }
}

