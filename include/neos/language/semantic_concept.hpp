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
        semanteme(i_semantic_concept const& aConcept, i_context& aContext, source_iterator aBegin, source_iterator aEnd) :
            iConcept{ aConcept.clone(*this) }, iBegin{aBegin}, iEnd{aEnd}
        {
        }
        bool has_parent() const override
        {
            return iConcept->has_parent();
        }
        const i_semantic_concept& parent() const override
        {
            return iConcept->parent();
        }
        const neolib::i_string& name() const override
        {
            return iConcept->name();
        }
        // folding support
    public:
        void clone(i_semantic_concept& aDataStore, neolib::i_ref_ptr<i_semantic_concept>& aCopy) const final
        {
            throw std::logic_error("neos::language::semanteme: not clonable");
        }
        // parse
    public:
        void update_source(source_iterator aBegin, source_iterator aEnd) override
        {
            iBegin = aBegin;
            iEnd = aEnd;
        }
        bool holds_data() const override
        {
            return iData.has_value();
        }
        using i_semantic_concept::data;
        void const* data() const override
        {
            if constexpr (!std::is_same_v<typename Concept::data_type, void>)
            {
                if (!iData.has_value())
                {
                    if constexpr (std::is_same_v<typename Concept::data_type, neolib::string>)
                        iData = typename Concept::data_type{ neolib::string_view{ iBegin, iEnd } };
                    else
                        iData = typename Concept::data_type{};
                }
                return &std::any_cast<typename Concept::data_type&>(iData);
            }
            else
                return nullptr;
        }
        void* data() override
        {
            return const_cast<void*>(const_cast<semanteme const&>(*this).data());
        }
        // emit
    public:
        emit_type emit_as() const override
        {
            return iConcept->emit_as();
        }
        bool can_fold() const override
        {
            return iConcept->can_fold();
        }
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            return iConcept->can_fold(aRhs);
        }
        // parse
    protected:
        void do_instantiate(i_context& aContext, source_iterator aBegin, source_iterator aEnd, neolib::i_ref_ptr<i_semantic_concept>& aResult) const override
        {
            throw std::logic_error("neos::language::semanteme: already instantiated!");
        }
        // emit
    protected:
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            iConcept->do_fold(aContext, aResult);
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
            iConcept->do_fold(aContext, aRhs, aResult);
        }
    private:
        neolib::ref_ptr<i_semantic_concept> iConcept;
        source_iterator iBegin;
        source_iterator iEnd;
        mutable std::any iData;
    };

    template <typename Concept>
    class semantic_concept : public neolib::reference_counted<i_semantic_concept>
    {
        // types
    public:
        using base_type = neolib::reference_counted<i_semantic_concept>;
        using concept_type = Concept;
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
        void clone(i_semantic_concept& aDataStore, neolib::i_ref_ptr<i_semantic_concept>& aCopy) const final
        {
            auto copy = neolib::make_ref<concept_type>(static_cast<concept_type const&>(*this));
            copy->set_data_store(aDataStore);
            aCopy = copy;
        }
        // family
    public:
        bool has_parent() const override
        {
            return iParent != nullptr;
        }
        const i_semantic_concept& parent() const override
        {
            if (has_parent())
                return *iParent;
            throw no_parent();
        }
        const neolib::i_string& name() const override
        {
            return iName;
        }
        // parse
    public:
        void update_source(source_iterator aBegin, source_iterator aEnd) override
        {
            throw std::logic_error("neos::language::semantic_concept: definitions have no source!");
        }
        bool holds_data() const override
        {
            return iDataStore && iDataStore->holds_data();
        }
        using i_semantic_concept::data;
        void const* data() const override
        {
            return iDataStore ? iDataStore->data() : nullptr;
        }
        void* data() override
        {
            return iDataStore ? iDataStore->data() : nullptr;
        }
        // emit
    public:
        emit_type emit_as() const override
        {
            return iEmitAs;
        }
        bool can_fold() const override
        {
            return false;
        }
        bool can_fold(const i_semantic_concept& aRhs) const override
        {
            return false;
        }
        // parse
    protected:
        void do_instantiate(i_context& aContext, source_iterator aBegin, source_iterator aEnd, neolib::i_ref_ptr<i_semantic_concept>& aResult) const override
        {
            aResult = neolib::make_ref<semanteme<Concept>>(*this, aContext, aBegin, aEnd);
        }
        // emit
    protected:
        void set_data_store(i_semantic_concept& aDataStore)
        {
            iDataStore = aDataStore;
        }
        void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
        void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) override
        {
        }
        // attributes
    private:
        i_semantic_concept const* iParent;
        neolib::string iName;
        emit_type iEmitAs;
        neolib::ref_ptr<i_semantic_concept> iDataStore;
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
