/*
  concept_library_plugin.hpp

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