/*
  concept_library.hpp

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
