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
        // (the generic concept (neos.core's) that a construct's concept derives from, if any: e.g. for a syntax highlighter (see 
        // neos::context::highlight))
        neolib::string_view generic_concept(construct aConstruct)
        {
            switch (aConstruct)
            {
            case construct::Parameters:
                return neolib::string_view{ "language.function.parameters" };
            case construct::Identifier:
            case construct::Binding:
                return neolib::string_view{ "language.identifier" };
            case construct::Initializer:
                return neolib::string_view{ "language.initialize" };
            // (a declaration's names, and a for's: its local variables)
            case construct::Declarator:
            case construct::ForVar:
            case construct::ForLet:
            case construct::ForConst:
                return neolib::string_view{ "language.function.local" };
            // (the generic concepts of a script's structure: e.g. for its outline (see neos::context::outline))
            case construct::Function:
            case construct::AsyncFunction:
            case construct::FunctionExpression:
            case construct::AsyncFunctionExpression:
            case construct::Arrow:
            case construct::AsyncArrow:
            case construct::Method:
            case construct::Getter:
            case construct::Setter:
            case construct::GeneratorMethod:
            case construct::AsyncMethod:
            case construct::AsyncGeneratorMethod:
                return neolib::string_view{ "language.function" };
            case construct::Body:
                return neolib::string_view{ "language.function.body" };
            case construct::Block:
            case construct::StaticBlock:
                return neolib::string_view{ "language.scope" };
            case construct::If:
                return neolib::string_view{ "language.statement.if" };
            case construct::Do:
            case construct::While:
            case construct::For:
            case construct::ForIn:
            case construct::ForOf:
            case construct::ForAwaitOf:
                return neolib::string_view{ "language.statement.loop" };
            case construct::Return:
                return neolib::string_view{ "language.function.return" };
            case construct::Break:
                return neolib::string_view{ "language.statement.break" };
            case construct::Continue:
                return neolib::string_view{ "language.statement.continue" };
            case construct::Throw:
                return neolib::string_view{ "language.statement.throw" };
            case construct::Try:
                return neolib::string_view{ "language.statement.try" };
            case construct::Catch:
                return neolib::string_view{ "language.statement.catch" };
            case construct::Finally:
                return neolib::string_view{ "language.statement.finally" };
            case construct::Switch:
                return neolib::string_view{ "language.statement.switch" };
            case construct::Case:
            case construct::Default:
                return neolib::string_view{ "language.statement.case" };
            case construct::With:
                return neolib::string_view{ "language.statement.with" };
            case construct::Class:
            case construct::ClassExpression:
                return neolib::string_view{ "language.class" };
            case construct::ExpressionStatement:
            case construct::Empty:
            case construct::Debugger:
            case construct::Var:
            case construct::Let:
            case construct::Const:
            case construct::Using:
            case construct::AwaitUsing:
            case construct::Labelled:
                return neolib::string_view{ "language.statement" };
            default:
                return {};
            }
        }

        class javascript_construct : public semantic_concept<javascript_construct>
        {
            // concept
        public:
            static constexpr auto Name = "javascript.construct";
            // construction
        public:
            javascript_construct(language::javascript::construct_info const& aInfo) :
                semantic_concept<javascript_construct>{ neolib::string_view{ aInfo.conceptName }, neos::language::emit_type::Infix, generic_concept(aInfo.id) },
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
