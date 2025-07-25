/*
  concept.hpp

  Copyright (c) 2024 Leigh Johnston.  All Rights Reserved.

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
        bool has_parent() const final
        {
            return iConcept->has_parent();
        }
        i_semantic_concept const& parent() const final
        {
            return iConcept->parent();
        }
        const neolib::i_string& name() const final
        {
            return iConcept->name();
        }
        bool is(neolib::i_string const& aName) const final
        {
            return iConcept->is(aName);
        }
        bool similar(i_semantic_concept const& aRhs) const final
        {
            return iConcept->similar(aRhs);
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

    template <typename Concept>
    class semantic_concept : public neolib::reference_counted<i_semantic_concept>
    {
        // types
    public:
        using base_type = neolib::reference_counted<i_semantic_concept>;
        using concept_type = Concept;
        // folding
    public:
        static constexpr bool HasGhosts = false;
        static constexpr bool Unstructured = false;
        // construction
    public:
        semantic_concept(std::string const& aName, emit_type aEmitAs = emit_type::Postfix) :
            iParent{ nullptr }, iName { aName}, iEmitAs{ aEmitAs }
        {
        }
        semantic_concept(i_semantic_concept& aParent, std::string const& aName, emit_type aEmitAs = emit_type::Postfix) :
            iParent{ &aParent }, iName{ aName }, iEmitAs{ aEmitAs }
        {
        }
        semantic_concept(semantic_concept const& aOther) :
            iParent{ aOther.iParent }, iName{ aOther.iName }, iEmitAs{ aOther.iEmitAs }
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
        bool has_parent() const final
        {
            return iParent != nullptr;
        }
        i_semantic_concept const& parent() const final
        {
            if (has_parent())
                return *iParent;
            throw no_parent();
        }
        const neolib::i_string& name() const final
        {
            return iName;
        }
        virtual bool is(neolib::i_string const& aName) const final
        {
            if (name() == aName)
                return true;
            if (has_parent())
                return parent().is(aName);
            return false;
        }
        bool similar(i_semantic_concept const& aRhs) const final
        {
            if (name() == aRhs.name())
                return true;
            if (aRhs.has_parent())
                return similar(aRhs.parent());
            if (has_parent())
                return parent().similar(aRhs);
            return false;
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
            return Concept::HasGhosts;
        }
        bool unstructured() const final
        {
            return Concept::Unstructured;
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
            aResult = neolib::make_ref<semanteme<Concept>>(*this, aContext, aSource);
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
        i_semantic_concept const* iParent;
        neolib::string iName;
        emit_type iEmitAs;
        neolib::ref_ptr<i_semantic_concept> iInstance;
    };

    class unimplemented_semantic_concept : public semantic_concept<unimplemented_semantic_concept>
    {
    public:
        using semantic_concept::semantic_concept;
    };
}

namespace neos::concepts
{
    using neos::language::semantic_concept;
}
