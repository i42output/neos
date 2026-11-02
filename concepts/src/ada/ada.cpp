/*
  ada.cpp

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
#include <neos/language/ada.hpp>
#include "ada.hpp"

// Ada concepts: each is a construct of the Ada compiler (see neos/language/ada.hpp), which builds its syntax tree from them; a
// concept folds by emitting its construct with its source

namespace neos::concepts::ada
{
    using language::ada::construct;

    namespace
    {
        // (the generic concept (neos.core's) that a construct's concept derives from, if any: e.g. for a syntax highlighter (see 
        // neos::context::highlight) and a script's outline (see neos::context::outline))
        neolib::string_view generic_concept(construct aConstruct)
        {
            switch (aConstruct)
            {
            case construct::Identifier:
            case construct::Defining:
                return neolib::string_view{ "language.identifier" };
            case construct::SubprogramBody:
            case construct::SubprogramDeclaration:
            case construct::SubprogramExpression:
            case construct::SubprogramNull:
            case construct::SubprogramAbstract:
                return neolib::string_view{ "language.function" };
            case construct::ProcedureSpecification:
            case construct::FunctionSpecification:
                return neolib::string_view{ "language.function.name" };
            case construct::Parameter:
                return neolib::string_view{ "language.function.parameters" };
            case construct::Object:
                return neolib::string_view{ "language.function.local" };
            case construct::Package:
            case construct::PackageBody:
            case construct::Block:
                return neolib::string_view{ "language.scope" };
            case construct::If:
            case construct::Case:
                return neolib::string_view{ "language.statement.if" };
            case construct::Loop:
                return neolib::string_view{ "language.statement.loop" };
            case construct::Return:
            case construct::ReturnExtended:
                return neolib::string_view{ "language.function.return" };
            case construct::Exit:
                return neolib::string_view{ "language.statement.break" };
            case construct::NullStatement:
            case construct::Assignment:
            case construct::Call:
            case construct::Raise:
            case construct::Goto:
            case construct::Delay:
                return neolib::string_view{ "language.statement" };
            default:
                return {};
            }
        }

        class ada_construct : public semantic_concept<ada_construct>
        {
            // concept
        public:
            static constexpr auto Name = "ada.construct";
            // construction
        public:
            ada_construct(language::ada::construct_info const& aInfo) :
                semantic_concept<ada_construct>{ neolib::string_view{ aInfo.conceptName }, neos::language::emit_type::Infix, generic_concept(aInfo.id) },
                iConstruct{ aInfo.id }
            {
            }
            ada_construct(ada_construct const& aOther) :
                semantic_concept<ada_construct>{ aOther },
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
                aContext.compiler().emit_language_construct(neolib::string_view{ language::ada::Language }, static_cast<std::uint32_t>(iConstruct), this->source());
            }
        private:
            construct iConstruct;
        };
    }

    ada::ada(const std::string& aLibraryUri) :
        neos::language::concept_library
        {
            library_id(),
            aLibraryUri,
            library_name(),
            "Ada language concepts",
            neolib::version{ 1, 0, 0 },
            "Copyright (c) 2026 Leigh Johnston.  All Rights Reserved."
        }
    {
        for (auto const& c : language::ada::Constructs)
            concepts()[neolib::string{ c.conceptName }] = neolib::make_ref<ada_construct>(c);
    }

    const std::string& ada::library_name()
    {
        static const std::string sName = "neos.ada";
        return sName;
    }

    const neolib::uuid& ada::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("3F9A6C12-7B48-4E2D-A1C5-9D0E8B3F2A67");
        return sId;
    }
}
