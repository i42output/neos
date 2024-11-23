/*
  core.cpp

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

#include "core.hpp"
#include "math.universal.hpp"

namespace neos::concepts::core
{   
    class math_universal_number_digit : public semantic_concept<math_universal_number_digit>
    {
    public:
        using semantic_concept::semantic_concept;
    };

    class math_universal_number_hexdigit : public semantic_concept<math_universal_number_hexdigit>
    {
    public:
        using semantic_concept::semantic_concept;
    };

    math_universal::math_universal(const std::string& aLibraryUri) :
        neos::language::concept_library
        { 
            library_id(), 
            aLibraryUri, 
            library_name(), 
            "neos math universal number language concept", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2021 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "math.universal.number" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.digit" }] = neolib::make_ref<math_universal_number_digit>("math.universal.number.digit", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.point" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.point", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.base" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.base", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.hexdigit" }] = neolib::make_ref<math_universal_number_hexdigit>("math.universal.number.hexdigit", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent.positive" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent.positive", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent.negative" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent.negative", language::emit_type::Infix);
        concepts()[neolib::string{ "math.universal.number.exponent.digit" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.universal.number.exponent.digit", language::emit_type::Infix);
    }

    const std::string& math_universal::library_name()
    {
        static const std::string sName = "neos.math.universal";
        return sName;
    }

    const neolib::uuid& math_universal::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("6EF80912-B773-4F79-BC11-7FEBF375B224");
        return sId;
    }
}

