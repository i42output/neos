/*
  concept_library_plugin.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <boost/dll.hpp>
#include <neolib/core/string.hpp>
#include <neolib/core/reference_counted.hpp>
#include <neolib/app/version.hpp>
#include <neolib/app/i_application.hpp>
#include <neolib/plugin/plugin.hpp>
#include <neos/language/i_concept_library.hpp>

namespace neos
{
    namespace language
    {
        template <typename ConceptLibrary>
        class concept_library_plugin : public neolib::plugin<>
        {
        public:
            typedef ConceptLibrary concept_library_type;
        public:
            concept_library_plugin(
                const neolib::uuid& aId = concept_library_type::library_id(),
                const std::string& aName = concept_library_type::library_name(),
                const std::string& aDescription = {},
                const neolib::version aVersion = {},
                const std::string& aCopyright = {}) :
                iId{ aId },
                iName{ aName },
                iDescription{ aDescription },
                iVersion{ aVersion },
                iCopyright{ aCopyright }
            {
            }
        public:
            bool discover(const neolib::uuid& aId, void*& aObject) override
            {
                if (aId == i_concept_library::iid())
                {
                    aObject = new concept_library_type{ "file:///" + boost::dll::this_line_location().string() };
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
        private:
            neolib::uuid iId;
            neolib::string iName;
            neolib::string iDescription;
            neolib::version iVersion;
            neolib::string iCopyright;
        };
    }
}