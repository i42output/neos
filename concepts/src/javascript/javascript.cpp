/*
  javascript.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

#include <neos/i_context.hpp>
#include <neos/language/semantic_concept.hpp>
#include <neos/language/javascript.hpp>
#include "javascript.hpp"

// JavaScript (ECMAScript) concepts: each is a construct of the JavaScript compiler (see neos/language/javascript.hpp), which 
// builds its syntax tree from them; a concept folds by emitting its construct with its source

namespace neos::concepts::javascript
{
    using language::javascript::construct;

    namespace
    {
        class javascript_construct : public semantic_concept<javascript_construct>
        {
            // concept
        public:
            static constexpr auto Name = "javascript.construct";
            // construction
        public:
            javascript_construct(language::javascript::construct_info const& aInfo) :
                semantic_concept<javascript_construct>{ neolib::string_view{ aInfo.conceptName }, neos::language::emit_type::Infix },
                iConstruct{ aInfo.id }
            {
            }
            javascript_construct(javascript_construct const& aOther) :
                semantic_concept<javascript_construct>{ aOther },
                iConstruct{ aOther.iConstruct }
            {
            }
            // emit
        public:
            bool can_fold() const override
            {
                return true;
            }
            void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
            {
                aContext.compiler().emit_language_construct(neolib::string_view{ language::javascript::Language }, static_cast<std::uint32_t>(iConstruct), this->source());
            }
        private:
            construct iConstruct;
        };
    }

    javascript::javascript(const std::string& aLibraryUri) :
        neos::language::concept_library
        {
            library_id(),
            aLibraryUri,
            library_name(),
            "JavaScript language concepts",
            neolib::version{ 2, 0, 0 },
            "Copyright (c) 2026 Leigh Johnston.  All Rights Reserved."
        }
    {
        for (auto const& c : language::javascript::Constructs)
            concepts()[neolib::string{ c.conceptName }] = neolib::make_ref<javascript_construct>(c);
    }

    const std::string& javascript::library_name()
    {
        static const std::string sName = "neos.javascript";
        return sName;
    }

    const neolib::uuid& javascript::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("5B0C9D2E-7A41-4E36-9F1D-3C8B2A6E4D17");
        return sId;
    }
}
