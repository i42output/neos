/*
  logic.cpp

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
#include "logic.hpp"

namespace neos::concepts::core
{   
    template <typename Derived = void>
    class logic_operator : public semantic_concept_t<Derived, logic_operator<Derived>>
    {
        // concept
    public:
        static constexpr auto Name = "logic.operator";
        // construction
    public:
        logic_operator() :
            semantic_concept_t<Derived, logic_operator<Derived>>{ neos::language::emit_type::Infix }
        {
        }
    };

    class logic_operator_if : public logic_operator<logic_operator_if>
    {
        // concept
    public:
        static constexpr auto Name = "logic.operator.if";
        // emit
    public:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aContext.compiler().emit_if(source());
        }
    };

    class logic_operator_elseif : public logic_operator<logic_operator_elseif>
    {
        // concept
    public:
        static constexpr auto Name = "logic.operator.elseif";
        // emit
    public:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aContext.compiler().emit_else_if(source());
        }
    };

    class logic_operator_else : public logic_operator<logic_operator_else>
    {
        // concept
    public:
        static constexpr auto Name = "logic.operator.else";
        // emit
    public:
        bool can_fold() const override
        {
            return true;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            aContext.compiler().emit_else(source());
        }
    };

    logic::logic(language::i_concept_library& aParent) :
        neos::language::concept_library
        { 
            aParent,
            library_id(), 
            aParent.uri().to_std_string(),
            library_name(), 
            "Core logic concepts", 
            neolib::version{ 1, 0, 0 }, 
            "Copyright (c) 2025 Leigh Johnston.  All Rights Reserved."
        }
    {
        /* todo */
        concepts()[neolib::string{ logic_operator<>::Name }] = neolib::make_ref<logic_operator<>>();
        concepts()[neolib::string{ logic_operator_if::Name }] = neolib::make_ref<logic_operator_if>();
        concepts()[neolib::string{ logic_operator_else::Name }] = neolib::make_ref<logic_operator_else>();
        concepts()[neolib::string{ logic_operator_elseif::Name }] = neolib::make_ref<logic_operator_elseif>();
    }

    const std::string& logic::library_name()
    {
        static const std::string sName = "neos.logic";
        return sName;
    }

    const neolib::uuid& logic::library_id()
    {
        static const neolib::uuid sId = neolib::make_uuid("D00976B0-0AE3-4C21-B41D-95EB07253D4E");
        return sId;
    }
}

