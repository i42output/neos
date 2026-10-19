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
#include <neos/language/ecmascript.hpp>
#include <neos/language/javascript.hpp>
#include "javascript.hpp"

// JavaScript (ECMAScript) specific concepts: values are numbers (IEEE 754 doubles), strings and booleans that are
// converted as JavaScript converts them; let/const/var declarations; top-level code interleaved with (hoisted)
// function declarations; functions whose parameter and return types are inferred

namespace neos::concepts::javascript
{
    using language::javascript::construct;

    namespace
    {
        template <construct Construct> struct javascript_construct_name;
        template <> struct javascript_construct_name<construct::Module> { static constexpr auto Name = "javascript.module"; };
        template <> struct javascript_construct_name<construct::Function> { static constexpr auto Name = "javascript.function"; };
        template <> struct javascript_construct_name<construct::Let> { static constexpr auto Name = "javascript.declaration.let"; };
        template <> struct javascript_construct_name<construct::Const> { static constexpr auto Name = "javascript.declaration.const"; };
        template <> struct javascript_construct_name<construct::Var> { static constexpr auto Name = "javascript.declaration.var"; };
        template <> struct javascript_construct_name<construct::GlobalLet> { static constexpr auto Name = "javascript.declaration.global.let"; };
        template <> struct javascript_construct_name<construct::GlobalConst> { static constexpr auto Name = "javascript.declaration.global.const"; };
        template <> struct javascript_construct_name<construct::GlobalVar> { static constexpr auto Name = "javascript.declaration.global.var"; };
        template <> struct javascript_construct_name<construct::Declarator> { static constexpr auto Name = "javascript.declarator"; };
        template <> struct javascript_construct_name<construct::Statement> { static constexpr auto Name = "javascript.statement"; };
        template <> struct javascript_construct_name<construct::EmptyStatement> { static constexpr auto Name = "javascript.statement.empty"; };
        template <> struct javascript_construct_name<construct::GlobalStatement> { static constexpr auto Name = "javascript.statement.global"; };
        template <> struct javascript_construct_name<construct::If> { static constexpr auto Name = "javascript.if"; };
        template <> struct javascript_construct_name<construct::While> { static constexpr auto Name = "javascript.while"; };
        template <> struct javascript_construct_name<construct::DoWhile> { static constexpr auto Name = "javascript.do"; };
        template <> struct javascript_construct_name<construct::For> { static constexpr auto Name = "javascript.for"; };
        template <> struct javascript_construct_name<construct::ForTest> { static constexpr auto Name = "javascript.for.test"; };
        template <> struct javascript_construct_name<construct::Return> { static constexpr auto Name = "javascript.return"; };
        template <> struct javascript_construct_name<construct::Break> { static constexpr auto Name = "javascript.break"; };
        template <> struct javascript_construct_name<construct::Continue> { static constexpr auto Name = "javascript.continue"; };
        template <> struct javascript_construct_name<construct::Assignment> { static constexpr auto Name = "javascript.assignment"; };
        template <> struct javascript_construct_name<construct::PrefixIncrement> { static constexpr auto Name = "javascript.update.increment.prefix"; };
        template <> struct javascript_construct_name<construct::PrefixDecrement> { static constexpr auto Name = "javascript.update.decrement.prefix"; };
        template <> struct javascript_construct_name<construct::PostfixIncrement> { static constexpr auto Name = "javascript.update.increment.postfix"; };
        template <> struct javascript_construct_name<construct::PostfixDecrement> { static constexpr auto Name = "javascript.update.decrement.postfix"; };
        template <> struct javascript_construct_name<construct::Conditional> { static constexpr auto Name = "javascript.conditional"; };
        template <> struct javascript_construct_name<construct::Call> { static constexpr auto Name = "javascript.call"; };
        template <> struct javascript_construct_name<construct::Member> { static constexpr auto Name = "javascript.member"; };
        template <> struct javascript_construct_name<construct::Index> { static constexpr auto Name = "javascript.index"; };
        template <> struct javascript_construct_name<construct::Variable> { static constexpr auto Name = "javascript.variable"; };
        template <> struct javascript_construct_name<construct::TemplateBegin> { static constexpr auto Name = "javascript.template.begin"; };
        template <> struct javascript_construct_name<construct::TemplateSubstitution> { static constexpr auto Name = "javascript.template.substitution"; };
        template <> struct javascript_construct_name<construct::Switch> { static constexpr auto Name = "javascript.switch"; };
        template <> struct javascript_construct_name<construct::Case> { static constexpr auto Name = "javascript.case"; };
        template <> struct javascript_construct_name<construct::DefaultCase> { static constexpr auto Name = "javascript.case.default"; };
    }

    // a construct whose code the compiler generates once its parts (operands, statements) have been compiled
    template <construct Construct>
    class javascript_construct : public semantic_concept<javascript_construct<Construct>>
    {
        // concept
    public:
        static constexpr auto Name = javascript_construct_name<Construct>::Name;
        // construction
    public:
        javascript_construct() :
            semantic_concept<javascript_construct<Construct>>{ neos::language::emit_type::Infix }
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
            aContext.compiler().emit_language_construct(neolib::string_view{ language::javascript::Language }, static_cast<std::uint32_t>(Construct), this->source());
        }
    };

    class javascript_number : public semantic_concept<javascript_number>
    {
        // concept
    public:
        static constexpr auto Name = "javascript.number";
        // construction
    public:
        javascript_number() :
            semantic_concept{ neos::language::emit_type::Infix }
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
            // numbers are IEEE 754 doubles (numeric separators are ignored)
            std::string literal;
            for (auto c : source().to_std_string_view())
                if (c != '_')
                    literal += c;
            aContext.compiler().push_operand(language::data<language::f64>{ ecmascript::string_to_number(literal) });
        }
    };

    class javascript_string : public semantic_concept<javascript_string>
    {
        // concept
    public:
        static constexpr auto Name = "javascript.string";
        // construction
    public:
        javascript_string() :
            semantic_concept{ neos::language::emit_type::Infix }
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
            auto const literal = source().to_std_string_view();
            aContext.compiler().push_operand(language::data<language::string>{
                neolib::string{ ecmascript::unescape(literal.substr(1u, literal.size() - 2u)) } });
        }
    };

    class javascript_template_text : public semantic_concept<javascript_template_text>
    {
        // concept
    public:
        static constexpr auto Name = "javascript.template.text";
        // construction
    public:
        javascript_template_text() :
            semantic_concept{ neos::language::emit_type::Infix }
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
            // appended to the template's string so far
            aContext.compiler().push_operand(language::data<language::string>{
                neolib::string{ ecmascript::unescape(source().to_std_string_view()) } });
            aContext.compiler().emit_language_construct(neolib::string_view{ language::javascript::Language }, static_cast<std::uint32_t>(construct::Add), source());
        }
    };

    namespace
    {
        template <> struct javascript_construct_name<construct::Add> { static constexpr auto Name = "javascript.operator.add"; };
        template <> struct javascript_construct_name<construct::Subtract> { static constexpr auto Name = "javascript.operator.subtract"; };
        template <> struct javascript_construct_name<construct::Multiply> { static constexpr auto Name = "javascript.operator.multiply"; };
        template <> struct javascript_construct_name<construct::Divide> { static constexpr auto Name = "javascript.operator.divide"; };
        template <> struct javascript_construct_name<construct::Remainder> { static constexpr auto Name = "javascript.operator.remainder"; };
        template <> struct javascript_construct_name<construct::Power> { static constexpr auto Name = "javascript.operator.power"; };
        template <> struct javascript_construct_name<construct::Equal> { static constexpr auto Name = "javascript.operator.equal"; };
        template <> struct javascript_construct_name<construct::NotEqual> { static constexpr auto Name = "javascript.operator.notequal"; };
        template <> struct javascript_construct_name<construct::StrictEqual> { static constexpr auto Name = "javascript.operator.equal.strict"; };
        template <> struct javascript_construct_name<construct::StrictNotEqual> { static constexpr auto Name = "javascript.operator.notequal.strict"; };
        template <> struct javascript_construct_name<construct::LessThan> { static constexpr auto Name = "javascript.operator.lessthan"; };
        template <> struct javascript_construct_name<construct::GreaterThan> { static constexpr auto Name = "javascript.operator.greaterthan"; };
        template <> struct javascript_construct_name<construct::LessThanOrEqual> { static constexpr auto Name = "javascript.operator.lessthanorequal"; };
        template <> struct javascript_construct_name<construct::GreaterThanOrEqual> { static constexpr auto Name = "javascript.operator.greaterthanorequal"; };
        template <> struct javascript_construct_name<construct::LogicalAnd> { static constexpr auto Name = "javascript.operator.logical.and"; };
        template <> struct javascript_construct_name<construct::LogicalOr> { static constexpr auto Name = "javascript.operator.logical.or"; };
        template <> struct javascript_construct_name<construct::LogicalNot> { static constexpr auto Name = "javascript.operator.logical.not"; };
        template <> struct javascript_construct_name<construct::BitwiseAnd> { static constexpr auto Name = "javascript.operator.bitwise.and"; };
        template <> struct javascript_construct_name<construct::BitwiseOr> { static constexpr auto Name = "javascript.operator.bitwise.or"; };
        template <> struct javascript_construct_name<construct::BitwiseXor> { static constexpr auto Name = "javascript.operator.bitwise.xor"; };
        template <> struct javascript_construct_name<construct::BitwiseNot> { static constexpr auto Name = "javascript.operator.bitwise.not"; };
        template <> struct javascript_construct_name<construct::ShiftLeft> { static constexpr auto Name = "javascript.operator.shift.left"; };
        template <> struct javascript_construct_name<construct::ShiftRight> { static constexpr auto Name = "javascript.operator.shift.right"; };
        template <> struct javascript_construct_name<construct::ShiftRightUnsigned> { static constexpr auto Name = "javascript.operator.shift.right.unsigned"; };
        template <> struct javascript_construct_name<construct::Negate> { static constexpr auto Name = "javascript.operator.negate"; };
        template <> struct javascript_construct_name<construct::Plus> { static constexpr auto Name = "javascript.operator.plus"; };
        template <> struct javascript_construct_name<construct::TypeOf> { static constexpr auto Name = "javascript.operator.typeof"; };
    }

    namespace
    {
        template <typename Concept>
        void add_concept(language::concept_library::concepts_t& aConcepts)
        {
            aConcepts[neolib::string{ Concept::Name }] = neolib::make_ref<Concept>();
        }

        template <construct... Constructs>
        void add_constructs(language::concept_library::concepts_t& aConcepts)
        {
            (add_concept<javascript_construct<Constructs>>(aConcepts), ...);
        }
    }

    javascript::javascript(const std::string& aLibraryUri) :
        neos::language::concept_library
        {
            library_id(),
            aLibraryUri,
            library_name(),
            "JavaScript language concepts",
            neolib::version{ 1, 0, 0 },
            "Copyright (c) 2026 Leigh Johnston.  All Rights Reserved."
        }
    {
        add_constructs<
            construct::Module, construct::Function,
            construct::Let, construct::Const, construct::Var,
            construct::GlobalLet, construct::GlobalConst, construct::GlobalVar,
            construct::Declarator, construct::Statement, construct::EmptyStatement, construct::GlobalStatement,
            construct::If, construct::While, construct::DoWhile, construct::For, construct::ForTest,
            construct::Return, construct::Break, construct::Continue,
            construct::Assignment, construct::PrefixIncrement, construct::PrefixDecrement,
            construct::PostfixIncrement, construct::PostfixDecrement,
            construct::Conditional, construct::Call, construct::Member, construct::Index, construct::Variable,
            construct::TemplateBegin, construct::TemplateSubstitution, 
            construct::Switch, construct::Case, construct::DefaultCase>(concepts());
        add_constructs<
            construct::Add, construct::Subtract, construct::Multiply, construct::Divide, construct::Remainder, construct::Power,
            construct::Equal, construct::NotEqual, construct::StrictEqual, construct::StrictNotEqual,
            construct::LessThan, construct::GreaterThan, construct::LessThanOrEqual, construct::GreaterThanOrEqual,
            construct::LogicalAnd, construct::LogicalOr, construct::LogicalNot,
            construct::BitwiseAnd, construct::BitwiseOr, construct::BitwiseXor, construct::BitwiseNot,
            construct::ShiftLeft, construct::ShiftRight, construct::ShiftRightUnsigned,
            construct::Negate, construct::Plus, construct::TypeOf>(concepts());
        add_concept<javascript_number>(concepts());
        add_concept<javascript_string>(concepts());
        add_concept<javascript_template_text>(concepts());
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
