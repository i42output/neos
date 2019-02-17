/*
  language.cpp

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

#include <neos/neos.hpp>
#include <neos/language.hpp>

namespace neos
{
    language::language(const neolib::rjson& aSchema) :
        iSchema{ aSchema },
        iName{ aSchema.root().as<neolib::rjson_object>().at("meta").as<neolib::rjson_object>().at("language").as<neolib::rjson_string>() }
    {
        for (auto const& e : aSchema.root())
        {
            if (e.name() == "meta")
            {
                for (auto const& meta : e)
                {
                    if (meta.name() == "description")
                        iDescription = meta.as<neolib::rjson_string>();
                    else if (meta.name() == "copyright")
                        iCopyright = meta.as<neolib::rjson_string>();
                    else if (meta.name() == "version")
                        iVersion = meta.as<neolib::rjson_string>();
                }
            }
        }
    }

    const std::string& language::name() const
    {
        return iName;
    }

    const std::string& language::description() const
    {
        return iDescription;
    }

    const std::string& language::copyright() const
    {
        return iCopyright;
    }

    const std::string& language::version() const
    {
        return iVersion;
    }
}