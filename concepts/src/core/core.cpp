/*
  core.cpp

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

