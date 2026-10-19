/*
  bitwise.cpp

  Copyright (c) 2021 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
        concepts()[neolib::string{ "bitwise.expression" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.expression");
        concepts()[neolib::string{ "bitwise.expression.operand" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.expression.operand");
        concepts()[neolib::string{ "bitwise.operator" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator");
        concepts()[neolib::string{ "bitwise.operator.not" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.not");
        concepts()[neolib::string{ "bitwise.operator.and" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.and");
        concepts()[neolib::string{ "bitwise.operator.or" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.or");
        concepts()[neolib::string{ "bitwise.operator.xor" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.xor");
        concepts()[neolib::string{ "bitwise.operator.shift.left" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.shift.left");
        concepts()[neolib::string{ "bitwise.operator.shift.right" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.shift.right");
        concepts()[neolib::string{ "bitwise.operator.rotate.left" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.rotate.left");
        concepts()[neolib::string{ "bitwise.operator.rotate.right" }] = neolib::make_ref<language::unimplemented_semantic_concept>("bitwise.operator.rotate.right");
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

