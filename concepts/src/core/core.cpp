/*
  core.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

#include "core.hpp"
#include "module.hpp"
#include "language.hpp"
#include "boolean.hpp"
#include "logic.hpp"
#include "bitwise.hpp"
#include "math.hpp"
#include "string.hpp"
#include "object.hpp"

namespace neos
{
    namespace concepts
    {
        namespace core
        {   
            core::core(const std::string& aLibraryUri) :
                neos::language::concept_library
                { 
                    library_id(), 
                    aLibraryUri, 
                    library_name(), 
                    "neos core language concepts", 
                    neolib::version{ 1, 0, 0 }, 
                    "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
                }
            {
                sublibraries()[neolib::string{ "neos.module" }] = neolib::make_ref<module>(*this);
                sublibraries()[neolib::string{ "neos.language" }] = neolib::make_ref<language>(*this);
                sublibraries()[neolib::string{ "neos.boolean" }] = neolib::make_ref<boolean>(*this);
                sublibraries()[neolib::string{ "neos.logic" }] = neolib::make_ref<logic>(*this);
                sublibraries()[neolib::string{ "neos.bitwise" }] = neolib::make_ref<bitwise>(*this);
                sublibraries()[neolib::string{ "neos.math" }] = neolib::make_ref<math>(*this);
                sublibraries()[neolib::string{ "neos.string" }] = neolib::make_ref<string>(*this);
                sublibraries()[neolib::string{ "neos.object" }] = neolib::make_ref<object>(*this);
            }

            const std::string& core::library_name()
            {
                static const std::string sName = "neos.core";
                return sName;
            }

            const neolib::uuid& core::library_id()
            {
                static const neolib::uuid sId = neolib::make_uuid("C5A28968-B129-4199-8AD6-3E499D9A8AE7");
                return sId;
            }
        }
    }
}

