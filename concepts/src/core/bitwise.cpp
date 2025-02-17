/*
  bitwise.cpp

  Copyright (c) 2021 Leigh Johnston.  All Rights Reserved.

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
#include "bitwise.hpp"

namespace neos::concepts::core
{   
    bitwise::bitwise(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core bitwise concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2021 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        neolib::ref_ptr<i_semantic_concept> parent;
        aParent.find_concept(neolib::string{ "language.expression" }, parent);
        concepts()[neolib::string{ "bitwise.expression" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*parent, "bitwise.expression");
        aParent.find_concept(neolib::string{ "language.expression.operand" }, parent);
        concepts()[neolib::string{ "bitwise.expression.operand" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*parent, "bitwise.expression.operand");
        concepts()[neolib::string{ "bitwise.operator" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator");
        concepts()[neolib::string{ "bitwise.operator.not" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.not");
        concepts()[neolib::string{ "bitwise.operator.and" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.and");
        concepts()[neolib::string{ "bitwise.operator.or" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.or");
        concepts()[neolib::string{ "bitwise.operator.xor" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.xor");
        concepts()[neolib::string{ "bitwise.operator.shift.left" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.shift.left");
        concepts()[neolib::string{ "bitwise.operator.shift.right" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.shift.right");
        concepts()[neolib::string{ "bitwise.operator.rotate.left" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.rotate.left");
        concepts()[neolib::string{ "bitwise.operator.rotate.right" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "bitwise.operator" }], "bitwise.operator.rotate.right");
    }

    const std::string& bitwise::library_name()
    {
        static const std::string sName = "neos.bitwise";
        return sName;
    }

    const neolib::uuid& bitwise::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("FC9438D9-D731-4B4F-81F9-BA339886FCC8");
        return sId;
    }
}

