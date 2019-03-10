/*
  i_concept_library.hpp

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
#include <neolib/i_plugin.hpp>

namespace neos
{
    class i_concept_library : public neolib::i_plugin
    {
    public:
        static const neolib::uuid& iid() { static neolib::uuid sId = neolib::make_uuid("DF0D818D-7C3F-4958-8FCF-9FE6A9B2A501"); return sId; }
    };
}