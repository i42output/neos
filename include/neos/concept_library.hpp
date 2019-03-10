/*
  concept_library.hpp

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
#include <neolib/string.hpp>
#include <neolib/version.hpp>
#include <neolib/reference_counted.hpp>
#include <neolib/i_application.hpp>
#include <neos/i_concept_library.hpp>

namespace neos
{
    class concept_library : public neolib::reference_counted<i_concept_library>
    {
    public:
        concept_library(
            const neolib::uuid& aId, 
            const std::string& aName, 
            const std::string& aDescription = {}, 
            const neolib::version aVersion = {}, 
            const std::string& aCopyright = {}) : 
            iId{ aId },
            iName{ aName },
            iDescription{ aDescription },
            iVersion{ aVersion },
            iCopyright{ aCopyright },
            iLoaded{ false }
        {
        }
    public:
        bool discover(const neolib::uuid& aId, void*& aObject) override
        {
            if (aId == i_concept_library::iid())
            {
                aObject = this;
                return true;
            }
            return false;
        }
    public:
        const neolib::uuid& id() const override
        {
            return iId;
        }
        const neolib::i_string& name() const override
        {
            return iName;
        }
        const neolib::i_string& description() const override
        {
            return iDescription;
        }
        const neolib::i_version& version() const override
        {
            return iVersion;
        }
        const neolib::i_string& copyright() const override
        {
            return iCopyright;
        }
        bool load() override
        {
            iLoaded = true;
            return true;
        }
        bool unload() override
        {
            iLoaded = false;
            return true;
        }
        bool loaded() const override
        {
            return iLoaded;
        }
        bool open_uri(const neolib::i_string& aUri) override
        {
            return false;
        }
    private:
        neolib::uuid iId;
        neolib::string iName;
        neolib::string iDescription;
        neolib::version iVersion;
        neolib::string iCopyright;
        bool iLoaded;
    };
}