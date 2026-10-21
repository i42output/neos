/*
  neoscript.cpp

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

#include <neolib/neolib.hpp>
#include <neos/i_context.hpp>
#include <neos/language/semantic_concept.hpp>
#include <neos/language/type.hpp>
#include <neos/language/neoscript.hpp>
#include "neoscript.hpp"

// neoscript concepts: each construct concept is a construct of the neoscript compiler (see neos/language/neoscript.hpp), which 
// a concept folds by emitting with its source; the type concept is a type (language.type) that a program defines (an object 
// reference) which the compiler's concepts (e.g. a function's parameters) take as any other type

namespace neos::concepts::neoscript
{
    using language::neoscript::construct;

    namespace
    {
        class neoscript_construct : public semantic_concept<neoscript_construct>
        {
            // concept
        public:
            static constexpr auto Name = "neoscript.construct";
            // construction
        public:
            neoscript_construct(language::neoscript::construct_info const& aInfo) :
                semantic_concept<neoscript_construct>{ neolib::string_view{ aInfo.conceptName }, neos::language::emit_type::Infix },
                iConstruct{ aInfo.id }
            {
            }
            neoscript_construct(neoscript_construct const& aOther) :
                semantic_concept<neoscript_construct>{ aOther },
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
                aContext.compiler().emit_language_construct(neolib::string_view{ language::neoscript::Language }, static_cast<std::uint32_t>(iConstruct), this->source());
            }
        private:
            construct iConstruct;
        };

        // (a name (a qualified name) or type within one of the compiler's constructs (which reads it from its source): its 
        // identifiers or type, then nothing)
        class neoscript_name : public semantic_concept<neoscript_name>
        {
            // concept
        public:
            static constexpr auto Name = "neoscript.name";
            // data
        public:
            using data_type = neolib::string;
            // construction
        public:
            neoscript_name() :
                semantic_concept<neoscript_name>{ neos::language::emit_type::Infix }
            {
            }
            // emit
        public:
            bool can_fold() const override
            {
                return this->holds_data();
            }
            bool can_fold(i_semantic_concept const& aRhs) const override
            {
                return !this->holds_data() && (aRhs.is("language.identifier"_sv) || aRhs.is("language.namespace.name"_sv) || 
                    aRhs.is("language.type"_sv) || aRhs.is("language.function.parameter.in"_sv) || 
                    aRhs.is("language.function.parameter.out"_sv) || aRhs.is("language.function.parameter.inout"_sv));
            }
            void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
            {
            }
            void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
            {
                // (an identifier or type ends it)
                if (aRhs.is("language.identifier"_sv) || aRhs.is("language.type"_sv))
                    this->template data<neolib::i_string>() = this->source();
                aResult = this->instance();
            }
        };

        // (the family of types: the compiler's concepts take a "language.type")
        struct language_type_family
        {
            static constexpr auto Name = "language.type";
        };

        // (a type that a program defines: its name (identifiers))
        class neoscript_type : public semantic_concept<neoscript_type, language_type_family>
        {
            // concept
        public:
            static constexpr auto Name = language::neoscript::TypeConcept;
            // data
        public:
            using data_type = neos::language::type;
            // construction
        public:
            neoscript_type() :
                semantic_concept<neoscript_type, language_type_family>{ neos::language::emit_type::Infix }
            {
            }
            // emit
        public:
            bool can_fold(i_semantic_concept const& aRhs) const override
            {
                return !this->holds_data() && (aRhs.is("language.identifier"_sv) || aRhs.is("language.namespace.name"_sv));
            }
            void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
            {
                // (an object (of a struct, class or interface) is a reference to it; the neoscript compiler knows its type by its name)
                if (aRhs.is("language.identifier"_sv))
                    this->template data<neos::language::type>() = neos::language::type::Pointer;
                aResult = this->instance();
            }
        };
    }

    neoscript::neoscript(const std::string& aLibraryUri) :
        neos::language::concept_library
        {
            library_id(),
            aLibraryUri,
            library_name(),
            "neoscript language concepts",
            neolib::version{ 1, 0, 0 },
            "Copyright (c) 2026 Leigh Johnston.  All Rights Reserved."
        }
    {
        for (auto const& c : language::neoscript::Constructs)
            concepts()[neolib::string{ c.conceptName }] = neolib::make_ref<neoscript_construct>(c);
        concepts()[neolib::string{ neoscript_name::Name }] = neolib::make_ref<neoscript_name>();
        concepts()[neolib::string{ neoscript_type::Name }] = neolib::make_ref<neoscript_type>();
    }

    const std::string& neoscript::library_name()
    {
        static const std::string sName = "neos.neoscript";
        return sName;
    }

    const neolib::uuid& neoscript::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("09D89D9E-8AAF-4038-AFFD-71EA2E1C09EC");
        return sId;
    }
}
