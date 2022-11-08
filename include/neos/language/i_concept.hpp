/*
  i_concept.hpp

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
#include <neolib/core/i_reference_counted.hpp>
#include <neolib/core/i_string.hpp>
#include <neos/fwd.hpp>

namespace neos::language
{
    enum class compiler_pass
    {
        Probe,
        Emit
    };

    class i_atom;

    enum class emit_type
    {
        Postfix,
        Infix
    };

    enum class representation_kind
    {
        Character,
        String,
        Object
    };

    template <typename T>
    struct representation_class_cracker { static constexpr representation_kind kind = representation_kind::Object; };
    template <>
    struct representation_class_cracker<neolib::i_string> { static constexpr representation_kind kind = representation_kind::String; };
    template <>
    struct representation_class_cracker<char> { static constexpr representation_kind kind = representation_kind::Character; };
    template <typename T>
    inline constexpr representation_kind representation_kind_v = representation_class_cracker<neolib::abstract_interface_t<T>>::kind;

    class concept_instance_proxy;

    class i_concept : public neolib::i_reference_counted
    {
        friend concept_instance_proxy;
    public:
        struct no_parent : std::logic_error { no_parent() : std::logic_error("neos::language::i_concept::no_parent") {} };
        struct invalid_fold : std::logic_error { invalid_fold() : std::logic_error("neos::language::i_concept::invalid_fold") {} };
        struct cannot_instantiate : std::logic_error { cannot_instantiate(std::string const& aName) : std::logic_error("neos::language::i_concept::cannot_instantiate: " + aName) {} };
        struct already_instantiated : std::logic_error { already_instantiated() : std::logic_error("neos::language::i_concept::already_instantiated") {} };
        struct not_an_instance : std::logic_error { not_an_instance() : std::logic_error("neos::language::i_concept::not_an_instance") {} };
        struct data_is_constant : std::logic_error { data_is_constant() : std::logic_error("neos::language::i_concept::data_is_constant") {} };
    public:
        typedef i_concept abstract_type;
        typedef neolib::i_string::const_iterator source_iterator;
    public:
        virtual bool has_parent() const = 0;
        virtual const i_concept& parent() const = 0;
        virtual i_concept& parent() = 0;
        virtual const neolib::i_string& name() const = 0;
        // parse
    public:
        virtual emit_type emit_as() const = 0;
        virtual source_iterator consume_token(compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const = 0;
        virtual source_iterator consume_atom(compiler_pass aPass, const i_atom& aAtom, source_iterator aSource, source_iterator aSourceEnd, bool& aConsumed) const = 0;
        virtual source_iterator source() const = 0;
        virtual source_iterator source_end() const = 0;
        virtual void update_source(source_iterator aSource, source_iterator aSourceEnd) = 0;
        virtual const neolib::i_string& trace() const = 0;
        // emit
    public:
        virtual bool has_constant_data() const = 0;
        virtual bool can_fold() const = 0;
        virtual bool can_fold(const i_concept& aRhs) const = 0;
        virtual bool can_instantiate() const = 0;
        virtual bool is_instance() const = 0;
        virtual const i_concept& as_instance() const = 0;
        virtual i_concept& as_instance() = 0;
    protected:
        virtual i_concept* do_instantiate(source_iterator aSource, source_iterator aSourceEnd) const = 0;
        virtual const void* representation(representation_kind aKind) const = 0;
        virtual void* representation(representation_kind aKind) = 0;
        virtual i_concept* do_fold(i_context& aContext) = 0;
        virtual i_concept* do_fold(i_context& aContext, const i_concept& aRhs) = 0;
        // helpers
    public:
        struct consume_result
        {
            source_iterator sourceParsed;
            bool consumed;
        };
        // family
    public:
        bool is_same(const i_concept& other) const
        {
            return &other == this;
        }
        bool is_ancestor_of(const i_concept& child) const
        {
            auto a = &child;
            while (a->has_parent())
            {
                a = &a->parent();
                if (a == this)
                    return true;
            }
            return false;
        }
        bool is_related_to(const i_concept& other) const
        {
            return is_same(other) || is_ancestor_of(other);
        }
        // parse
    public:
        consume_result consume_token(compiler_pass aPass, source_iterator aSource, source_iterator aSourceEnd) const
        {
            if (aSource != aSourceEnd)
            {
                bool consumed;
                auto result = consume_token(aPass, aSource, aSourceEnd, consumed);
                return consume_result{ result, consumed };
            }
            else
            {
                bool consumed;
                auto result = consume_token(aPass, nullptr, nullptr, consumed);
                return consume_result{ aSource, consumed };
            }
        }
        consume_result consume_atom(compiler_pass aPass, const i_atom& aAtom, source_iterator aSource, source_iterator aSourceEnd) const
        {
            if (aSource != aSourceEnd)
            {
                bool consumed;
                auto result = consume_atom(aPass, aAtom, aSource, aSourceEnd, consumed);
                return consume_result{ result, consumed };
            }
            else
            {
                bool consumed;
                auto result = consume_atom(aPass, aAtom, nullptr, nullptr, consumed);
                return consume_result{ aSource, consumed };
            }
        }
        // emit
    public:
        i_concept* instantiate(source_iterator aSource, source_iterator aSourceEnd) const
        {
            return do_instantiate(aSource, aSourceEnd);
        }
        neolib::ref_ptr<i_concept> fold(i_context& aContext)
        {
            if (can_fold())
                return as_instance().do_fold(aContext);
            return nullptr;
        }
        neolib::ref_ptr<i_concept> fold(i_context& aContext, const i_concept& aRhs)
        {
            if (can_fold(aRhs))
                return as_instance().do_fold(aContext, aRhs);
            return nullptr;
        }
        template <typename source_iterator>
        neolib::ref_ptr<i_concept> fold(i_context& aContext, const i_concept& aRhs)
        {
            if (can_fold(aRhs))
                return as_instance().do_fold(aContext, aRhs);
            return nullptr;
        }
        template <typename Data>
        const neolib::abstract_interface_t<Data>& data() const
        {
            if (is_instance() || has_constant_data())
                return *static_cast<const neolib::abstract_interface_t<Data>*>(representation(representation_kind_v<neolib::abstract_interface_t<Data>>));
            throw not_an_instance();
        }
        template <typename Data>
        neolib::abstract_interface_t<Data>& data()
        {
            if (!has_constant_data())
                return const_cast<neolib::abstract_interface_t<Data>&>(const_cast<const i_concept*>(this)->data<Data>());
            throw data_is_constant();
        }
    };
}

namespace neos::concepts
{
    using neos::language::i_concept;
}
