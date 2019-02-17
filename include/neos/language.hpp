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
    struct language_meta
    {
        std::string name;
        std::string description;
        std::string copyright;
        std::string version;
        std::vector<std::string> sourcecodeFileExtension;
        std::vector<std::string> sourcecodeModulePackageSpecificationFileExtension;
        std::vector<std::string> sourcecodeModulePackageImplementationFileExtension;
    };

    class language
    {
    public:
        language(const neolib::rjson& aSchema);
    public:
        const language_meta& meta() const;
    private:
        const neolib::rjson& iSchema;
        language_meta iMeta;
    };
}