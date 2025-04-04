/*
  logic.cpp

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
#include "logic.hpp"

namespace neos::concepts::core
{   
    logic::logic(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core logic concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "logic.operator" }] = neolib::make_ref<language::unimplemented_semantic_concept>("logic.operator", language::emit_type::Infix);
        concepts()[neolib::string{ "logic.operator.if" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "logic.operator" }], "logic.operator.if", language::emit_type::Infix);
        concepts()[neolib::string{ "logic.operator.else" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "logic.operator" }], "logic.operator.else", language::emit_type::Infix);
        concepts()[neolib::string{ "logic.operator.elseif" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "logic.operator" }], "logic.operator.elseif", language::emit_type::Infix);
    }

    const std::string& logic::library_name()
    {
        static const std::string sName = "neos.logic";
        return sName;
    }

    const neolib::uuid& logic::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("D00976B0-0AE3-4C21-B41D-95EB07253D4E");
        return sId;
    }
}

