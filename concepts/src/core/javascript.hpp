/*
  javascript.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

#include <neos/language/concept_library.hpp>

namespace neos
{
    namespace concepts
    {
        namespace core
        {
            class javascript : public language::concept_library
            {
            public:
                javascript(language::i_concept_library& aParent);
            public:
                static const std::string& library_name();
                static const neolib::uuid& library_id();
            };
        }
    }
}
