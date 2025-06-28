/*
  i_concept_library.hpp

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