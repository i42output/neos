/*
  language.hpp

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
#include <neolib/json.hpp>

namespace neos
{
    class language
    {
    public:
        language(const neolib::rjson& aSchema);
    public:
        const std::string& name() const;
        const std::string& description() const;
        const std::string& copyright() const;
        const std::string& version() const;
    private:
        const neolib::rjson& iSchema;
        std::string iName;
        std::string iDescription;
        std::string iCopyright;
        std::string iVersion;
    };
}