/*
  math.cpp

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

#include <neos/i_context.hpp>
#include <neos/language/semantic_concept.hpp>
#include <neos/language/type.hpp>
#include <neos/language/operator.hpp>
#include "math.hpp"

namespace neos::concepts::core
{   
    class math_expression : public semantic_concept<math_expression>
    {
        // concept
    public:
        static constexpr auto Name = "math.expression";
        // data
    public:
        // construction
    public:
        math_expression() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
    };

    class math_expression_operand : public semantic_concept<math_expression_operand>
    {
        // concept
    public:
        static constexpr auto Name = "math.expression.operand";
        // data
    public:
        // construction
    public:
        math_expression_operand() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            if (aRhs.is("language.identifier"_sv))
                return true;
            return false;
        }
        void do_fold(i_context & aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>&aResult) override
        {
            if (aRhs.is("language.identifier"_sv))
                aContext.compiler().push_operand(language::symbol_reference{ aContext.compiler().current_scope(), aRhs.source() });
        }
    };

    template <typename Lhs, typename Rhs>
    struct add
    {
        using result_type = decltype(Lhs{} + Rhs{});
        result_type operator()(Lhs const& lhs, Rhs const& rhs) const
        {
            return static_cast<result_type>(lhs) + static_cast<result_type>(rhs);
        }
    };

    template <typename Lhs, typename Rhs>
    struct subtract
    {
        using result_type = decltype(Lhs{} + Rhs{});
        result_type operator()(Lhs const& lhs, Rhs const& rhs) const
        {
            return static_cast<result_type>(lhs) - static_cast<result_type>(rhs);
        }
    };

    template <typename Lhs, typename Rhs>
    struct multiply
    {
        using result_type = decltype(Lhs{} * Rhs{});
        result_type operator()(Lhs const& lhs, Rhs const& rhs) const
        {
            return static_cast<result_type>(lhs) * static_cast<result_type>(rhs);
        }
    };

    template <typename Lhs, typename Rhs>
    struct divide
    {
        using result_type = decltype(Lhs{} * Rhs{});
        result_type operator()(Lhs const& lhs, Rhs const& rhs) const
        {
            return static_cast<result_type>(lhs) / static_cast<result_type>(rhs);
        }
    };

    namespace
    {
        bool exact_immediates(language::i_data_type const& aLhs, language::i_data_type const& aRhs)
        {
            // (arithmetic on floating point immediates is folded by the compiler in the precision of their type)
            auto const real = [](language::i_data_type const& aValue)
            {
                auto const t = static_cast<language::type>(aValue.index());
                return t == language::type::Fbig || t == language::type::F32 || t == language::type::F64;
            };
            return is_scalar_immediate(aLhs) && is_scalar_immediate(aRhs) && !real(aLhs) && !real(aRhs);
        }
    }

    template <template <typename, typename> typename Operator>
    struct math_binary_operator : neolib::reference_counted<language::i_binary_operator>
    {
        using abstract_type = i_binary_operator;

        neolib::ref_ptr<language::i_semantic_concept> semanticConcept;

        math_binary_operator(neolib::ref_ptr<language::i_semantic_concept> aSemanticConcept) :
            semanticConcept{ aSemanticConcept } {}

        using i_binary_operator::operator();
        void operator()(i_context& context, const language::i_data_type& lhs, const language::i_data_type& rhs, language::i_data_type& result) const final
        {
            if (language::is_scalar_immediate(lhs) && language::is_scalar_immediate(rhs))
            {
                neolib::visit([&](auto const& lhs)
                    {
                        neolib::visit([&](auto const& rhs)
                            {
                                using Lhs = typename std::decay_t<decltype(lhs)>::type;
                                using Rhs = typename std::decay_t<decltype(rhs)>::type;
                                if constexpr (language::is_scalar_convertable_v<Lhs, Rhs>)
                                {
                                    language::data const result0 = Operator<Lhs, Rhs>{}(lhs.value().value(), rhs.value().value());
                                    result = result0;
                                }
                            }, rhs);
                    }, lhs);
            }
            else
                semanticConcept->fold(context);
        }
    };

    class math_operator_add : public semantic_concept<math_operator_add>
    {
        // concept
    public:
        static constexpr auto Name = "math.operator.add";
        // construction
    public:
        math_operator_add() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            auto& lhsOperand = aContext.compiler().lhs_operand();
            auto& rhsOperand = aContext.compiler().rhs_operand();
            if (lhsOperand.holds_alternative<language::i_data_type>() &&
                rhsOperand.holds_alternative<language::i_data_type>())
            {
                if (exact_immediates(lhsOperand.get<language::i_data_type>(), rhsOperand.get<language::i_data_type>()))
                {
                    auto rhs = aContext.compiler().pop_operand();
                    auto lhs = aContext.compiler().pop_operand();
                    aContext.compiler().push_operand(math_binary_operator<add>{instance()}(aContext, lhs, rhs));
                }
                else
                    aContext.compiler().emit_operation(language::operation::Add);
            }
            else
                aContext.compiler().emit_operation(language::operation::Add);
        }
    };

    class math_operator_subtract : public semantic_concept<math_operator_subtract>
    {
        // concept
    public:
        static constexpr auto Name = "math.operator.subtract";
        // construction
    public:
        math_operator_subtract() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            auto& lhsOperand = aContext.compiler().lhs_operand();
            auto& rhsOperand = aContext.compiler().rhs_operand();
            if (lhsOperand.holds_alternative<language::i_data_type>() &&
                rhsOperand.holds_alternative<language::i_data_type>())
            {
                if (exact_immediates(lhsOperand.get<language::i_data_type>(), rhsOperand.get<language::i_data_type>()))
                {
                    auto rhs = aContext.compiler().pop_operand();
                    auto lhs = aContext.compiler().pop_operand();
                    aContext.compiler().push_operand(math_binary_operator<subtract>{instance()}(aContext, lhs, rhs));
                }
                else
                    aContext.compiler().emit_operation(language::operation::Subtract);
            }
            else
                aContext.compiler().emit_operation(language::operation::Subtract);
        }
    };

    class math_operator_multiply : public semantic_concept<math_operator_multiply>
    {
        // concept
    public:
        static constexpr auto Name = "math.operator.multiply";
        // construction
    public:
        math_operator_multiply() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            auto& lhsOperand = aContext.compiler().lhs_operand();
            auto& rhsOperand = aContext.compiler().rhs_operand();
            if (lhsOperand.holds_alternative<language::i_data_type>() &&
                rhsOperand.holds_alternative<language::i_data_type>())
            {
                if (exact_immediates(lhsOperand.get<language::i_data_type>(), rhsOperand.get<language::i_data_type>()))
                {
                    auto rhs = aContext.compiler().pop_operand();
                    auto lhs = aContext.compiler().pop_operand();
                    aContext.compiler().push_operand(math_binary_operator<multiply>{instance()}(aContext, lhs, rhs));
                }
                else
                    aContext.compiler().emit_operation(language::operation::Multiply);
            }
            else
                aContext.compiler().emit_operation(language::operation::Multiply);
        }
    };

    class math_operator_divide : public semantic_concept<math_operator_divide>
    {
        // concept
    public:
        static constexpr auto Name = "math.operator.divide";
        // construction
    public:
        math_operator_divide() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            auto& lhsOperand = aContext.compiler().lhs_operand();
            auto& rhsOperand = aContext.compiler().rhs_operand();
            if (lhsOperand.holds_alternative<language::i_data_type>() &&
                rhsOperand.holds_alternative<language::i_data_type>())
            {
                if (exact_immediates(lhsOperand.get<language::i_data_type>(), rhsOperand.get<language::i_data_type>()))
                {
                    auto rhs = aContext.compiler().pop_operand();
                    auto lhs = aContext.compiler().pop_operand();
                    aContext.compiler().push_operand(math_binary_operator<divide>{instance()}(aContext, lhs, rhs));
                }
                else
                    aContext.compiler().emit_operation(language::operation::Divide);
            }
            else
                aContext.compiler().emit_operation(language::operation::Divide);
        }
    };

    class math_operator_negate : public semantic_concept<math_operator_negate>
    {
        // concept
    public:
        static constexpr auto Name = "math.operator.negate";
        // construction
    public:
        math_operator_negate() :
            semantic_concept{ neos::language::emit_type::Infix }
        {
        }
        // emit
    protected:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aContext.compiler().emit_operation(language::operation::Negate);
        }
    };

    math::math(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core mathematical concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "math.expression" }] = neolib::make_ref<math_expression>();
        concepts()[neolib::string{ "math.expression.operand" }] = neolib::make_ref<math_expression_operand>();
        concepts()[neolib::string{ "math.operator" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator");
        concepts()[neolib::string{ "math.operator.identity" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator.decay");
        concepts()[neolib::string{ "math.operator.negate" }] = neolib::make_ref<math_operator_negate>();
        concepts()[neolib::string{ "math.operator.addition" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator.addition");
        concepts()[neolib::string{ "math.operator.add" }] = neolib::make_ref<math_operator_add>();
        concepts()[neolib::string{ "math.operator.subtract" }] = neolib::make_ref<math_operator_subtract>();
        concepts()[neolib::string{ "math.operator.multiplication" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator.multiplication");
        concepts()[neolib::string{ "math.operator.multiply" }] = neolib::make_ref<math_operator_multiply>();
        concepts()[neolib::string{ "math.operator.multiply.matrix" }] = neolib::make_ref<language::unimplemented_semantic_concept>( "math.operator.multiply.matrix");
        concepts()[neolib::string{ "math.operator.divide" }] = neolib::make_ref<math_operator_divide>();
        concepts()[neolib::string{ "math.operator.divide.floor" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator.divide.floor");
        concepts()[neolib::string{ "math.operator.divide.remainder" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator.divide.remainder");
        concepts()[neolib::string{ "math.operator.power" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator.power");
    }

    const std::string& math::library_name()
    {
        static const std::string sName = "neos.math";
        return sName;
    }

    const neolib::uuid& math::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("9AECA57F-0F8E-450A-BB5C-8AC12BFE0E47");
        return sId;
    }
}

