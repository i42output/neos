/*
  concept_library.hpp

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
#include <neolib/core/recursion.hpp>
#include <neolib/core/reference_counted.hpp>
#include <neolib/core/map.hpp>
#include <neolib/core/string.hpp>
#include <neolib/app/version.hpp>
#include <neolib/app/i_application.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/semantic_concept.hpp>

using namespace neolib::string_literals;

namespace neos::language
{
    class concept_library : public neolib::reference_counted<i_concept_library>
    {
    public:
        typedef neolib::map<neolib::string, neolib::ref_ptr<i_concept_library>> sublibraries_t;
        typedef neolib::map<neolib::string, neolib::ref_ptr<i_semantic_concept>> concepts_t;
    public:
        static constexpr std::size_t RecursionLimit = 16u;
    public:
        concept_library(
            const neolib::uuid& aId,
            const std::string& aUri,
            const std::string& aName,
            const std::string& aDescription = {},
            const neolib::version aVersion = {},
            const std::string& aCopyright = {}) :
            iParent{ nullptr },
            iId{ aId },
            iUri{ aUri },
            iName{ aName },
            iDescription{ aDescription },
            iVersion{ aVersion },
            iCopyright{ aCopyright }
        {
        }
        concept_library(
            i_concept_library& aParent,
            const neolib::uuid& aId,
            const std::string& aUri,
            const std::string& aName,
            const std::string& aDescription = {},
            const neolib::version aVersion = {},
            const std::string& aCopyright = {}) :
            iParent{ &aParent },
            iId{ aId },
            iUri{ aUri },
            iName{ aName },
            iDescription{ aDescription },
            iVersion{ aVersion },
            iCopyright{ aCopyright }
        {
        }
        ~concept_library()
        {
        }
    public:
        bool discover(neolib::uuid const& aId, void*& aObject) override
        {
            return false;
        }
    public:
        bool has_parent() const override
        {
            return iParent != nullptr;
        }
        const i_concept_library& parent() const override
        {
            if (has_parent())
                return *iParent;
            throw no_parent();
        }
        i_concept_library& parent() override
        {
            if (has_parent())
                return *iParent;
            throw no_parent();
        }
        const sublibraries_t& sublibraries() const override
        {
            return iSublibraries;
        }
        sublibraries_t& sublibraries() override
        {
            return iSublibraries;
        }
        const concepts_t& concepts() const override
        {
            return iConcepts;
        }
        concepts_t& concepts() override
        {
            return iConcepts;
        }
        using i_concept_library::find_concept;
        bool find_concept(neolib::i_string_view const& aSymbol, neolib::i_ref_ptr<i_semantic_concept>& aConcept) const override
        {
            _limit_recursion_(concept_library);
            auto concept_ = concepts().find(neolib::string{ aSymbol });
            if (concept_ != concepts().end())
            {
                aConcept = concept_->second();
                return true;
            }
            for (auto const& cl : sublibraries())
                if (cl.second()->find_concept(aSymbol, aConcept))
                    return true;
            return false;
        }
    public:
        const neolib::uuid& id() const override
        {
            return iId;
        }
        const neolib::i_string& uri() const override
        {
            return iUri;
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
        i_concept_library* iParent;
        sublibraries_t iSublibraries;
        concepts_t iConcepts;
        neolib::uuid iId;
        neolib::string iUri;
        neolib::string iName;
        neolib::string iDescription;
        neolib::version iVersion;
        neolib::string iCopyright;
    };
}
