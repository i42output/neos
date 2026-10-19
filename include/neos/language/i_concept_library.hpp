/*
  i_concept_library.hpp

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
#include <neolib/core/i_discoverable.hpp>
#include <neolib/core/i_map.hpp>
#include <neolib/core/string_view.hpp>
#include <neolib/app/i_version.hpp>
#include <neos/language/i_semantic_concept.hpp>

namespace neos
{
    namespace language
    {
        class i_concept_library;
        typedef neolib::i_map<neolib::i_string, neolib::i_ref_ptr<i_concept_library>> concept_libraries_t;

        class i_concept_library : public neolib::i_discoverable
        {
            // exceptions
        public:
            struct no_parent : std::logic_error { no_parent() : std::logic_error("neos::language::i_concept_library::no_parent") {} };
            // types
        public:
            typedef i_concept_library abstract_type;
            typedef concept_libraries_t sublibraries_t;
            typedef neolib::i_map<neolib::i_string, neolib::i_ref_ptr<i_semantic_concept>> concepts_t;
            // sublibrary
        public:
            virtual bool has_parent() const = 0;
            virtual const i_concept_library& parent() const = 0;
            virtual i_concept_library& parent() = 0;
            virtual const sublibraries_t& sublibraries() const = 0;
            virtual sublibraries_t& sublibraries() = 0;
            // concepts
        public:
            virtual const concepts_t& concepts() const = 0;
            virtual concepts_t& concepts() = 0;
            virtual bool find_concept(neolib::i_string_view const& aSymbol, neolib::i_ref_ptr<i_semantic_concept>& aConcept) const = 0;
            // meta
        public:
            virtual const neolib::uuid& id() const = 0;
            virtual const neolib::i_string& uri() const = 0;
            virtual const neolib::i_string& name() const = 0;
            virtual const neolib::i_string& description() const = 0;
            virtual const neolib::i_version& version() const = 0;
            virtual const neolib::i_string& copyright() const = 0;
            // helpers
        public:
            std::uint32_t depth() const
            {
                std::uint32_t result = 0;
                i_concept_library const* l = this;
                while (l->has_parent())
                {
                    ++result;
                    l = &l->parent();
                }
                return result;
            }
            bool find_concept(neolib::string const& aSymbol, neolib::i_ref_ptr<i_semantic_concept>& aConcept) const
            {
                return find_concept(neolib::string_view{ aSymbol }, aConcept);
            }
            neolib::ref_ptr<i_semantic_concept> find_concept(neolib::string const& aSymbol) const
            {
                neolib::ref_ptr<i_semantic_concept> result;
                find_concept(neolib::string_view{ aSymbol }, result);
                return result;
            }
            // interface
        public:
            static const neolib::uuid& iid() { static neolib::uuid sId = neolib::make_uuid("DF0D818D-7C3F-4958-8FCF-9FE6A9B2A501"); return sId; }
        };
    }
}