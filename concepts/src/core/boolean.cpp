/*
  boolean.cpp

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
#include "boolean.hpp"

namespace neos::concepts::core
{   
    boolean::boolean(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core boolean concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        neolib::ref_ptr<i_semantic_concept> parent;
        aParent.find_concept(neolib::string{ "language.expression" }, parent);
        concepts()[neolib::string{ "boolean.expression" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*parent, "boolean.expression");
        aParent.find_concept(neolib::string{ "language.expression.operand" }, parent);
        concepts()[neolib::string{ "boolean.expression.operand" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*parent, "boolean.expression.operand");
        concepts()[neolib::string{ "boolean.operator" }] = neolib::make_ref<language::unimplemented_semantic_concept>("boolean.operator");
        concepts()[neolib::string{ "boolean.operator.logical" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator" }], "boolean.operator.logical");
        concepts()[neolib::string{ "boolean.operator.logical.not" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.logical" }], "boolean.operator.logical.not");
        concepts()[neolib::string{ "boolean.operator.logical.and" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.logical" }], "boolean.operator.logical.and");
        concepts()[neolib::string{ "boolean.operator.logical.or" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.logical" }], "boolean.operator.logical.or");
        concepts()[neolib::string{ "boolean.operator.logical.xor" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.logical" }], "boolean.operator.logical.xor");
        concepts()[neolib::string{ "boolean.operator.relational" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator" }], "boolean.operator.relational");
        concepts()[neolib::string{ "boolean.operator.relational.equal" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.relational" }], "boolean.operator.relational.equal");
        concepts()[neolib::string{ "boolean.operator.relational.notequal" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.relational" }], "boolean.operator.relational.notequal");
        concepts()[neolib::string{ "boolean.operator.relational.lessthan" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.relational" }], "boolean.operator.relational.lessthan");
        concepts()[neolib::string{ "boolean.operator.relational.greaterthan" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.relational" }], "boolean.operator.relational.greaterthan");
        concepts()[neolib::string{ "boolean.operator.relational.lessthanorequal" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.relational" }], "boolean.operator.relational.lessthanorequal");
        concepts()[neolib::string{ "boolean.operator.relational.greaterthanorequal" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "boolean.operator.relational" }], "boolean.operator.relational.greaterthanorequal");
    }

    const std::string& boolean::library_name()
    {
        static const std::string sName = "neos.boolean";
        return sName;
    }

    const neolib::uuid& boolean::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("65FCB09A-186D-4BE9-ACDB-E25FCCD43FF8");
        return sId;
    }
}

