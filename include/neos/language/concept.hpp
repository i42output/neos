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
#include <neolib/core/reference_counted.hpp>
#include <neolib/core/string.hpp>
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
        typedef neolib::reference_counted<i_concept> base_type;
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
        source_iterator source() const override
        {
            throw not_an_instance();
        }
        source_iterator source_end() const override
        {
            throw not_an_instance();
        }
        const neolib::i_string& trace() const override
        {
            thread_local neolib::string traceResult = name() + "()";
            return traceResult;
        }
        // emit
    public:
        bool has_constant_data() const override
        {
            return false;
        }
        bool can_fold() const override
        {
            return false;
        }
        bool can_fold(const i_concept& aRhs) const override
        {
            return false;
        }
        bool can_instantiate() const override
        {
            if constexpr (!std::is_same_v<instance_type, void>)
                return true;
            else
                return false;
        }
        bool is_instance() const override
        {
            return false;
        }
        const i_concept& as_instance() const override
        {
            throw not_an_instance();
        }
        i_concept& as_instance() override
        {
            return const_cast<i_concept&>(const_cast<const neos_concept*>(this)->as_instance());
        }
    protected:
        i_concept* do_instantiate(source_iterator aSource, source_iterator aSourceEnd) const override
        {
            if constexpr (!std::is_same_v<instance_type, void>)
                return new concept_instance<instance_type>{ static_cast<const InstanceType&>(*this), aSource, aSourceEnd };
            else
                throw cannot_instantiate(name());
        }
        const void* representation(representation_kind) const override
        {
            throw not_an_instance();
        }
        void* representation(representation_kind) override
        {
            throw not_an_instance();
        }
        i_concept* do_fold(i_context& aContext) override
        {
            return nullptr;
        }
        i_concept* do_fold(i_context& aContext, const i_concept& aRhs) override
        {
            if constexpr (!std::is_same_v<instance_type, void>)
            {
                typedef typename instance_type::representation_type representation_type;
                if (can_fold(aRhs) && as_instance().can_fold(aRhs))
                {
                    if (aRhs.is_instance() || aRhs.has_constant_data())
                        as_instance().data<neolib::abstract_interface_t<representation_type>>() = aRhs.data<neolib::abstract_interface_t<representation_type>>();
                    else
                        throw base_type::invalid_fold();
                    return &as_instance();
                }
                throw invalid_fold();
            }
            else
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
        typedef typename concept_type::source_iterator source_iterator;
        typedef typename concept_type::representation_type representation_type;
        // construction
    public:
        concept_instance(const concept_type& aConcept, source_iterator aSource, source_iterator aSourceEnd) :
            concept_type{ aConcept }, iSource{ aSource }, iSourceEnd{ aSourceEnd }, iRepresentation{}
        {
        }
        concept_instance() = delete;
        // parse
    public:
        source_iterator source() const override
        {
            return iSource;
        }
        source_iterator source_end() const override
        {
            return iSourceEnd;
        }
        const neolib::i_string& trace() const override
        {
            thread_local neolib::string traceResult;
            std::ostringstream oss;
            oss << concept_type::name() << "(" << *static_cast<const representation_type*>(representation(representation_kind_v<representation_type>)) << ")";
            traceResult = oss.str();
            return traceResult;
        }
        // emit
    public:
        bool can_instantiate() const override
        {
            return false;
        }
        bool is_instance() const override
        {
            return true;
        }
        const concept_type& as_instance() const override
        {
            return *this;
        }
        concept_type& as_instance() override
        {
            return *this;
        }
    protected:
        i_concept* do_instantiate(source_iterator, source_iterator) const override
        {
            throw already_instantiated();
        }
        const void* representation(representation_kind) const override
        {
            return &iRepresentation;
        }
        void* representation(representation_kind) override
        {
            return &iRepresentation;
        }
    private:
        source_iterator iSource;
        source_iterator iSourceEnd;
        representation_type iRepresentation;
    };

    class concept_instance_proxy : public neolib::reference_counted<i_concept>
    {
        // types
    public:
        typedef neolib::string representation_type;
        // construction
    public:
        concept_instance_proxy(const i_concept & aSubject, source_iterator aSource, source_iterator aSourceEnd) :
            iSubject{ aSubject }, iSource{ aSource }, iSourceEnd{ aSourceEnd }, iRepresentation{ aSource, aSourceEnd }
        {
        }
        // family
    public:
        bool has_parent() const override
        {
            return iSubject.has_parent();
        }
        const i_concept& parent() const override
        {
            return iSubject.parent();
        }
        i_concept& parent() override
        {
            return const_cast<i_concept&>(const_cast<const concept_instance_proxy*>(this)->parent());
        }
        const neolib::i_string& name() const override
        {
            return iSubject.name();
        }
        // parse
    public:
        emit_type emit_as() const override
        {
            return iSubject.emit_as();
        }
        source_iterator consume_token(compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            return iSubject.consume_token(aPass, aSource, aSourceEnd, aConsumed);
        }
        source_iterator consume_atom(compiler_pass aPass, const i_atom & aAtom, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const override
        {
            return iSubject.consume_atom(aPass, aAtom, aSource, aSourceEnd, aConsumed);
        }
        source_iterator source() const override
        {
            return iSource;
        }
        source_iterator source_end() const override
        {
            return iSourceEnd;
        }
        const neolib::i_string& trace() const override
        {
            return iSubject.trace();
        }
        // emit
    public:
        bool has_constant_data() const override
        {
            return iSubject.has_constant_data();
        }
        bool can_fold() const override
        {
            return iSubject.can_fold();
        }
        bool can_fold(const i_concept & aRhs) const override
        {
            return iSubject.can_fold(aRhs);
        }
        bool can_instantiate() const override
        {
            return false;
        }
        bool is_instance() const override
        {
            return true;
        }
        const i_concept& as_instance() const override
        {
            return *this;
        }
        i_concept& as_instance() override
        {
            return *this;
        }
    protected:
        i_concept* do_instantiate(source_iterator, source_iterator) const override
        {
            throw already_instantiated();
        }
        const void* representation(representation_kind aKind) const override
        {
            switch(aKind)
            {
            case representation_kind::Character:
                return &iRepresentation[0];
            case representation_kind::String:
            case representation_kind::Object:
            default:
                return static_cast<const neolib::i_string*>(&iRepresentation);
            }
        }
        void* representation(representation_kind aKind) override
        {
            return const_cast<void*>(const_cast<const concept_instance_proxy*>(this)->representation(aKind));
        }
        i_concept* do_fold(i_context&) override
        {
            return this;
        }
        i_concept* do_fold(i_context&, const i_concept&) override
        {
            return this;
        }
        // attributes
    private:
        const i_concept& iSubject;
        source_iterator iSource;
        source_iterator iSourceEnd;
        representation_type iRepresentation;
    };
}

namespace neos::concepts
{
    using neos::language::neos_concept;
}
