/*
  math.cpp

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

#include <neos/i_context.hpp>
#include <neos/language/semantic_concept.hpp>
#include <neos/language/type.hpp>
#include "math.hpp"

namespace neos::concepts::core
{   
    class math_expression : public semantic_concept<math_expression>
    {
        // data
    public:
        // construction
    public:
        math_expression(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "math.expression", neos::language::emit_type::Infix }
        {
        }
        math_expression(i_context& aContext) :
            semantic_concept{ *aContext.find_concept("language.expression"), "math.expression", neos::language::emit_type::Infix}
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

    class math_operator_add : public semantic_concept<math_operator_add>
    {
        // construction
    public:
        math_operator_add(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "math.operator.add", neos::language::emit_type::Infix }
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
            auto rhs = aContext.compiler().pop_operand();
            auto lhs = aContext.compiler().pop_operand();
            if (lhs.holds_alternative<language::data<language::ibig>>() &&
                lhs.holds_alternative<language::data<language::ibig>>())
            {
                language::data<language::ibig> result;
                result.v = lhs.get<language::data<language::ibig>>().v.value() +
                    rhs.get<language::data<language::ibig>>().v.value();
                aContext.compiler().push_operand(result);
            }
        }
    };

    class math_operator_subtract : public semantic_concept<math_operator_subtract>
    {
        // construction
    public:
        math_operator_subtract(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "math.operator.subtract", neos::language::emit_type::Infix }
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
            auto rhs = aContext.compiler().pop_operand();
            auto lhs = aContext.compiler().pop_operand();
            if (lhs.holds_alternative<language::data<language::ibig>>() &&
                lhs.holds_alternative<language::data<language::ibig>>())
            {
                language::data<language::ibig> result;
                result.v = lhs.get<language::data<language::ibig>>().v.value() -
                    rhs.get<language::data<language::ibig>>().v.value();
                aContext.compiler().push_operand(result);
            }
        }
    };

    class math_operator_multiply : public semantic_concept<math_operator_multiply>
    {
        // construction
    public:
        math_operator_multiply(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "math.operator.multiply", neos::language::emit_type::Infix }
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
            auto rhs = aContext.compiler().pop_operand();
            auto lhs = aContext.compiler().pop_operand();
            if (lhs.holds_alternative<language::data<language::ibig>>() &&
                lhs.holds_alternative<language::data<language::ibig>>())
            {
                language::data<language::ibig> result;
                result.v = lhs.get<language::data<language::ibig>>().v.value() *
                    rhs.get<language::data<language::ibig>>().v.value();
                aContext.compiler().push_operand(result);
            }
        }
    };

    class math_operator_divide : public semantic_concept<math_operator_divide>
    {
        // construction
    public:
        math_operator_divide(i_semantic_concept& aParent) :
            semantic_concept{ aParent, "math.operator.divide", neos::language::emit_type::Infix }
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
            auto rhs = aContext.compiler().pop_operand();
            auto lhs = aContext.compiler().pop_operand();
            if (lhs.holds_alternative<language::data<language::ibig>>() &&
                lhs.holds_alternative<language::data<language::ibig>>())
            {
                language::data<language::ibig> result;
                result.v = lhs.get<language::data<language::ibig>>().v.value() /
                    rhs.get<language::data<language::ibig>>().v.value();
                aContext.compiler().push_operand(result);
            }
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
        neolib::ref_ptr<i_semantic_concept> parent;
        aParent.find_concept(neolib::string{ "language.expression" }, parent);
        concepts()[neolib::string{ "math.expression" }] = neolib::make_ref<math_expression>(*parent);
        aParent.find_concept(neolib::string{ "language.expression.operand" }, parent);
        concepts()[neolib::string{ "math.expression.operand" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*parent, "math.expression.operand");
        concepts()[neolib::string{ "math.operator" }] = neolib::make_ref<language::unimplemented_semantic_concept>("math.operator");
        concepts()[neolib::string{ "math.operator.identity" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator" }], "math.operator.decay");
        concepts()[neolib::string{ "math.operator.negate" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator" }], "math.operator.negate");
        concepts()[neolib::string{ "math.operator.addition" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator" }], "math.operator.addition");
        concepts()[neolib::string{ "math.operator.add" }] = neolib::make_ref<math_operator_add>(*concepts()[neolib::string{ "math.operator.addition" }]);
        concepts()[neolib::string{ "math.operator.subtract" }] = neolib::make_ref<math_operator_subtract>(*concepts()[neolib::string{ "math.operator.addition" }]);
        concepts()[neolib::string{ "math.operator.multiplication" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator" }], "math.operator.multiplication");
        concepts()[neolib::string{ "math.operator.multiply" }] = neolib::make_ref<math_operator_multiply>(*concepts()[neolib::string{ "math.operator.multiplication" }]);
        concepts()[neolib::string{ "math.operator.multiply.matrix" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator.multiplication" }], "math.operator.multiply.matrix");
        concepts()[neolib::string{ "math.operator.divide" }] = neolib::make_ref<math_operator_divide>(*concepts()[neolib::string{ "math.operator.multiplication" }]);
        concepts()[neolib::string{ "math.operator.divide.floor" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator.multiplication" }], "math.operator.divide.floor");
        concepts()[neolib::string{ "math.operator.divide.remainder" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator.multiplication" }], "math.operator.divide.remainder");
        concepts()[neolib::string{ "math.operator.power" }] = neolib::make_ref<language::unimplemented_semantic_concept>(*concepts()[neolib::string{ "math.operator" }], "math.operator.power");
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

