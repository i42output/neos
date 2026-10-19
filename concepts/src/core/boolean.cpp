/*
  boolean.cpp

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
#include "boolean.hpp"

namespace neos::concepts::core
{   
    class boolean_expression : public semantic_concept<boolean_expression>
    {
        // concept
    public:
        static constexpr auto Name = "boolean.expression";
        // construction
    public:
        boolean_expression() :
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
        }
    };

    namespace
    {
        template <language::operation Operation> struct boolean_operator_name;
        template <> struct boolean_operator_name<language::operation::LogicalNot> { static constexpr auto Name = "boolean.operator.logical.not"; };
        template <> struct boolean_operator_name<language::operation::LogicalAnd> { static constexpr auto Name = "boolean.operator.logical.and"; };
        template <> struct boolean_operator_name<language::operation::LogicalOr> { static constexpr auto Name = "boolean.operator.logical.or"; };
        template <> struct boolean_operator_name<language::operation::Equal> { static constexpr auto Name = "boolean.operator.relational.equal"; };
        template <> struct boolean_operator_name<language::operation::NotEqual> { static constexpr auto Name = "boolean.operator.relational.notequal"; };
        template <> struct boolean_operator_name<language::operation::LessThan> { static constexpr auto Name = "boolean.operator.relational.lessthan"; };
        template <> struct boolean_operator_name<language::operation::GreaterThan> { static constexpr auto Name = "boolean.operator.relational.greaterthan"; };
        template <> struct boolean_operator_name<language::operation::LessThanOrEqual> { static constexpr auto Name = "boolean.operator.relational.lessthanorequal"; };
        template <> struct boolean_operator_name<language::operation::GreaterThanOrEqual> { static constexpr auto Name = "boolean.operator.relational.greaterthanorequal"; };
    }

    template <language::operation Operation>
    class boolean_operator : public semantic_concept<boolean_operator<Operation>>
    {
        // concept
    public:
        static constexpr auto Name = boolean_operator_name<Operation>::Name;
        // construction
    public:
        boolean_operator() :
            semantic_concept<boolean_operator<Operation>>{ neos::language::emit_type::Infix }
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
            aContext.compiler().emit_operation(Operation);
        }
    };

    boolean::boolean(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core boolean concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ "boolean.expression" }] = neolib::make_ref<boolean_expression>();
        concepts()[neolib::string{ "boolean.expression.operand" }] = neolib::make_ref<language::unimplemented_semantic_concept>("boolean.expression.operand");
        concepts()[neolib::string{ "boolean.operator" }] = neolib::make_ref<language::unimplemented_semantic_concept>("boolean.operator");
        concepts()[neolib::string{ "boolean.operator.logical" }] = neolib::make_ref<language::unimplemented_semantic_concept>("boolean.operator.logical");
        concepts()[neolib::string{ "boolean.operator.logical.not" }] = neolib::make_ref<boolean_operator<language::operation::LogicalNot>>();
        concepts()[neolib::string{ "boolean.operator.logical.and" }] = neolib::make_ref<boolean_operator<language::operation::LogicalAnd>>();
        concepts()[neolib::string{ "boolean.operator.logical.or" }] = neolib::make_ref<boolean_operator<language::operation::LogicalOr>>();
        concepts()[neolib::string{ "boolean.operator.logical.xor" }] = neolib::make_ref<language::unimplemented_semantic_concept>("boolean.operator.logical.xor");
        concepts()[neolib::string{ "boolean.operator.relational" }] = neolib::make_ref<language::unimplemented_semantic_concept>("boolean.operator.relational");
        concepts()[neolib::string{ "boolean.operator.relational.equal" }] = neolib::make_ref<boolean_operator<language::operation::Equal>>();
        concepts()[neolib::string{ "boolean.operator.relational.notequal" }] = neolib::make_ref<boolean_operator<language::operation::NotEqual>>();
        concepts()[neolib::string{ "boolean.operator.relational.lessthan" }] = neolib::make_ref<boolean_operator<language::operation::LessThan>>();
        concepts()[neolib::string{ "boolean.operator.relational.greaterthan" }] = neolib::make_ref<boolean_operator<language::operation::GreaterThan>>();
        concepts()[neolib::string{ "boolean.operator.relational.lessthanorequal" }] = neolib::make_ref<boolean_operator<language::operation::LessThanOrEqual>>();
        concepts()[neolib::string{ "boolean.operator.relational.greaterthanorequal" }] = neolib::make_ref<boolean_operator<language::operation::GreaterThanOrEqual>>();
    }

    const std::string& boolean::library_name()
    {
        static const std::string sName = "neos.boolean";
        return sName;
    }

    const neolib::uuid& boolean::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("65FCB09A-186D-4BE9-ACDB-E25FCCD43FF8");
        return sId;
    }
}

