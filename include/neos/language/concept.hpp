/*
  concept.hpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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
#include <neolib/reference_counted.hpp>
#include <neolib/string.hpp>
#include <neos/language/i_concept.hpp>

namespace neos::language
{
    template <typename Concept>
    class concept_instance;

    template<typename InstanceType = void>
    class neos_concept : public neolib::reference_counted<i_concept>
    {
        // types
    public:
        typedef InstanceType instance_type;
        // construction
    public:
        neos_concept(const std::string& aName, emit_type aEmitAs = emit_type::Postfix) :
            iParent{ nullptr }, iName { aName}, iEmitAs{ aEmitAs }
        {
        }
        neos_concept(i_concept& aParent, const std::string& aName, emit_type aEmitAs = emit_type::Postfix) :
            iParent{ &aParent }, iName{ aName }, iEmitAs{ aEmitAs }
        {
        }
        template <typename Concept>
        neos_concept(const std::string& aName, emit_type aEmitAs = emit_type::Postfix) :
            iParent{ nullptr }, iName{ aName }, iEmitAs{ aEmitAs }
        {
        }
        template <typename Concept>
        neos_concept(i_concept& aParent, const std::string& aName, Concept*, emit_type aEmitAs = emit_type::Postfix) :
            iParent{ &aParent }, iName{ aName }, iEmitAs{ aEmitAs }
        {
        }
        // family
    public:
        bool has_parent() const override
        {
            return iParent != nullptr;
        }
        const i_concept& parent() const override
        {
            if (has_parent())
                return *iParent;
            throw no_parent();
        }
        i_concept& parent() override
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
        emit_type emit_as() const override
        {
            return iEmitAs;
        }
        source_iterator consume_token(compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = false;
            return aSource;
        }
        source_iterator consume_atom(compiler_pass aPass, const i_atom& aAtom, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            aConsumed = true;
            return aSource;
        }
        // emit
    public:
        bool can_fold(const i_concept& aRhs) const override
        {
            return false;
        }
    protected:
        bool can_instantiate() const override
        {
            if constexpr (!std::is_same_v<instance_type, void>)
                return true;
            else
                return false;
        }
        i_concept* instantiate() const override
        {
            if constexpr (!std::is_same_v<instance_type, void>)
                return new concept_instance<instance_type>{ static_cast<const InstanceType&>(*this) };
            else
                throw cannot_instantiate();
        }
        bool is_instance() const override
        {
            return false;
        }
        const i_concept* instance() const override
        {
            if (can_instantiate())
            {
                return instantiate();
            }
            throw not_an_instance();
        }
        i_concept* instance() override
        {
            return const_cast<i_concept*>(const_cast<const neos_concept*>(this)->instance());
        }
        const void* representation() const override
        {
            throw not_an_instance();
        }
        void* representation() override
        {
            throw not_an_instance();
        }
        bool is_folded() const override
        {
            return false;
        }
        i_concept* do_fold(const i_concept& aRhs) override
        {
            return nullptr;
        }
        // attributes
    private:
        i_concept* iParent;
        neolib::string iName;
        emit_type iEmitAs;
    };

    class unimplemented_concept : public neos_concept<>
    {
    public:
        using neos_concept::neos_concept;
    };

    template <typename Concept>
    class concept_instance : public Concept
    {
        // exceptions
    public:
        using i_concept::already_instantiated;
        // types
    public:
        typedef Concept concept_type;
        typedef typename concept_type::representation_type representation_type;
        // construction
    public:
        concept_instance(const concept_type& aConcept) :
            concept_type{ aConcept }, iRepresentation{}
        {
        }
        concept_instance() = delete;
        // emit
    private:
        bool can_instantiate() const override
        {
            return false;
        }
        i_concept* instantiate() const override
        {
            throw already_instantiated();
        }
        bool is_instance() const override
        {
            return true;
        }
        const concept_type* instance() const override
        {
            return this;
        }
        concept_type* instance() override
        {
            return this;
        }
        const void* representation() const override
        {
            return &iRepresentation;
        }
        void* representation() override
        {
            return &iRepresentation;
        }
    private:
        representation_type iRepresentation;
    };
}

namespace neos::concept
{
    using neos::language::neos_concept;
}
