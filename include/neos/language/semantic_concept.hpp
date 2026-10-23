/*
  concept.hpp

  Copyright (c) 2024 Leigh Johnston.  All Rights Reserved.

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
#include <any>
#include <neolib/core/reference_counted.hpp>
#include <neolib/core/string.hpp>
#include <neolib/core/string_view.hpp>
#include <neolib/core/string_utils.hpp>
#include <neos/language/i_semantic_concept.hpp>

namespace neos::language
{
    template <typename Concept>
    class semanteme : public neolib::reference_counted<i_semantic_concept>
    {
    public:
        using concept_type = Concept;
        using data_type = typename concept_type::data_type;
    public:
        semanteme(i_semantic_concept const& aConcept, i_context& aContext, neolib::i_string_view const& aSource) :
            iConcept{ aConcept.clone(*this) }, iSource{ aSource }
        {
        }
        ~semanteme()
        {
        }
        const neolib::i_string_view& name() const final
        {
            return iConcept->name();
        }
        bool is(neolib::i_string_view const& aName) const final
        {
            return iConcept->is(aName);
        }
        // folding support
    public:
        void clone(i_semantic_concept& aInstance, neolib::i_ref_ptr<i_semantic_concept>& aCopy) const final
        {
            throw std::logic_error("neos::language::semanteme: not clonable");
        }
        // parse
    public:
        neolib::i_string_view const& source() const final
        {
            return iSource;
        }
        void update_source(neolib::i_string_view const& aSource) final
        {
            iSource = aSource;
        }
        void instance(neolib::i_ref_ptr<i_semantic_concept>& aInstance) const final
        {
            throw std::logic_error("neos::language::semanteme: is instance!");
        }
        void set_instance(i_semantic_concept& aInstance) final
        {
            throw std::logic_error("neos::language::semanteme: already instantiated!");
        }
        bool holds_data() const final
        {
            return iData.has_value();
        }
        using i_semantic_concept::data;
        void const* data() const final
        {
            if constexpr (!std::is_same_v<typename Concept::data_type, void>)
            {
                if (!iData.has_value())
                {
                    if constexpr (std::is_same_v<typename Concept::data_type, neolib::string>)
                        iData = typename Concept::data_type{ iSource };
                    else if constexpr (std::is_constructible_v<typename Concept::data_type, std::string_view>)
                        iData = typename Concept::data_type{ iSource.to_std_string_view() };
                    else
                        iData = typename Concept::data_type{};
                }
                return &std::any_cast<typename Concept::data_type&>(iData);
            }
            else
                return nullptr;
        }
        void* data() final
        {
            return const_cast<void*>(const_cast<semanteme const&>(*this).data());
        }
        // emit
    public:
        emit_type emit_as() const final
        {
            return iConcept->emit_as();
        }
        bool has_ghosts() const final
        {
            return iConcept->has_ghosts();
        }
        bool unstructured() const final
        {
            return iConcept->unstructured();
        }
        bool can_fold() const final
        {
            return iConcept->can_fold();
        }
        bool can_fold(i_semantic_concept const& aRhs) const final
        {
            return iConcept->can_fold(aRhs);
        }
        // debug
    public:
        void trace(neolib::i_string& aResult) const final
        {
            std::ostringstream oss;
            oss << name();
            if (iData.has_value())
            {
                oss << "[";
                if constexpr (std::is_same_v<typename Concept::data_type, neolib::string>)
                    oss << neolib::to_escaped_string(data<neolib::string>().to_std_string_view(), 32u, true);
                else
                    oss << "*";
                oss << "]";
            }
            else
                oss << "[" << neolib::to_escaped_string(source().to_std_string_view(), 32u, true) << "]";
            aResult = neolib::string{ oss.str()};
        }
        // parse
    protected:
        void do_instantiate(i_context& aContext, neolib::i_string_view const& aSource, neolib::i_ref_ptr<i_semantic_concept>& aResult) const final
        {
            throw std::logic_error("neos::language::semanteme: already instantiated!");
        }
        // emit
    protected:
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) final
        {
            iConcept->do_fold(aContext, aResult);
        }
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) final
        {
            iConcept->do_fold(aContext, aRhs, aResult);
        }
    private:
        neolib::ref_ptr<i_semantic_concept> iConcept;
        neolib::string_view iSource;
        mutable std::any iData;
    };

    namespace
    {
        template <typename First, typename... Rest>
        struct primary_concept { using type = First; };
    }

    template <typename... ConceptHierarchy>
    class semantic_concept : public neolib::reference_counted<i_semantic_concept>
    {
        // types
    public:
        using base_type = neolib::reference_counted<i_semantic_concept>;
        using concept_type = typename primary_concept<ConceptHierarchy...>::type;
        // folding
    public:
        static constexpr auto Name = "";
        static constexpr bool HasGhosts = false;
        static constexpr bool Unstructured = false;
        // construction
    public:
        semantic_concept(emit_type aEmitAs = emit_type::Postfix) :
            iName{ concept_type::Name }, iEmitAs { aEmitAs }
        {
        }
        // (aBase: a concept that a concept named at run time (aName) derives from (see is), e.g. a generic one (neos.core's))
        semantic_concept(neolib::string_view const& aName, emit_type aEmitAs = emit_type::Postfix, neolib::string_view const& aBase = {}) :
            iName{ aName }, iBase{ aBase }, iEmitAs{ aEmitAs }
        {
        }
        semantic_concept(semantic_concept const& aOther) :
            iName{ aOther.iName }, iBase{ aOther.iBase }, iEmitAs{ aOther.iEmitAs }
        {
        }
        // folding support
    public:
        void clone(i_semantic_concept& aInstance, neolib::i_ref_ptr<i_semantic_concept>& aCopy) const final
        {
            auto copy = neolib::make_ref<concept_type>(static_cast<concept_type const&>(*this));
            copy->set_instance(aInstance);
            aCopy = copy;
        }
        // family
    public:
        const neolib::i_string_view& name() const final
        {
            return iName;
        }
        virtual bool is(neolib::i_string_view const& aName) const final
        {
            bool found = (aName == name()) || (!iBase.empty() && aName == iBase);
            ((found = found || ConceptHierarchy::Name == aName), ...);
            return found;
        }
        // parse
    public:
        neolib::i_string_view const& source() const final
        {
            if (iInstance)
                return iInstance->source();
            throw std::logic_error("neos::language::semantic_concept: definitions have no source!");
        }
        void update_source(neolib::i_string_view const& aSource) final
        {
            if (iInstance)
                return iInstance->update_source(aSource);
            throw std::logic_error("neos::language::semantic_concept: definitions have no source!");
        }
        bool holds_data() const final
        {
            return iInstance && iInstance->holds_data();
        }
        using i_semantic_concept::data;
        void const* data() const final
        {
            return iInstance ? iInstance->data() : nullptr;
        }
        void* data() final
        {
            return iInstance ? iInstance->data() : nullptr;
        }
        // emit
    public:
        emit_type emit_as() const final
        {
            return iEmitAs;
        }
        bool has_ghosts() const final
        {
            return concept_type::HasGhosts;
        }
        bool unstructured() const final
        {
            return concept_type::Unstructured;
        }
        bool can_fold() const override
        {
            return false;
        }
        bool can_fold(i_semantic_concept const& aRhs) const override
        {
            return false;
        }
        // debug
    public:
        void trace(neolib::i_string& aResult) const override
        {
            if (iInstance)
                iInstance->trace(aResult);
            else
                aResult = name();
        }
        // parse
    protected:
        void do_instantiate(i_context& aContext, neolib::i_string_view const& aSource, neolib::i_ref_ptr<i_semantic_concept>& aResult) const final
        {
            aResult = neolib::make_ref<semanteme<concept_type>>(*this, aContext, aSource);
        }
        // emit
    protected:
        using i_semantic_concept::instance;
        void instance(neolib::i_ref_ptr<i_semantic_concept>& aInstance) const final
        {
            aInstance = iInstance;
        }
        void set_instance(i_semantic_concept& aInstance) final
        {
            iInstance.reset(&aInstance);
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override {}
        void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override {}
        // attributes
    private:
        neolib::string_view iName;
        neolib::string_view iBase;
        emit_type iEmitAs;
        neolib::ref_ptr<i_semantic_concept> iInstance;
    };

    class unimplemented_semantic_concept : public semantic_concept<unimplemented_semantic_concept>
    {
    public:
        using semantic_concept::semantic_concept;
    };

    namespace
    {
        template <typename... ConceptHierarchy>
        struct semantic_concept_maker
        {
            using type = semantic_concept<ConceptHierarchy...>;
        };
        template <typename... ConceptHierarchy>
        struct semantic_concept_maker<void, ConceptHierarchy...>
        {
            using type = semantic_concept<ConceptHierarchy...>;
        };
    }

    template <typename... ConceptHierarchy>
    using semantic_concept_t = typename semantic_concept_maker<ConceptHierarchy...>::type;
}

namespace neos::concepts
{
    using neos::language::semantic_concept;
    using neos::language::semantic_concept_t;
}
