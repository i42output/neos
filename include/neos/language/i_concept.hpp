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
#include <neolib/i_reference_counted.hpp>
#include <neolib/i_string.hpp>

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

    class i_concept : public neolib::i_reference_counted
    {
    public:
        struct no_parent : std::logic_error { no_parent() : std::logic_error("neos::language::i_concept::no_parent") {} };
        struct invalid_fold : std::logic_error { invalid_fold() : std::logic_error("neos::language::i_concept::invalid_fold") {} };
        struct cannot_instantiate : std::logic_error { cannot_instantiate() : std::logic_error("neos::language::i_concept::cannot_instantiate") {} };
        struct already_instantiated : std::logic_error { already_instantiated() : std::logic_error("neos::language::i_concept::already_instantiated") {} };
        struct not_an_instance : std::logic_error { not_an_instance() : std::logic_error("neos::language::i_concept::not_an_instance") {} };
    public:
        typedef const char* source_iterator;
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
        // emit
    public:
        virtual bool can_fold(const i_concept& aRhs) const = 0;
    protected:
        virtual bool can_instantiate() const = 0;
        virtual i_concept* instantiate() const = 0;
        virtual bool is_instance() const = 0;
        virtual const i_concept* instance() const = 0;
        virtual i_concept* instance() = 0;
        virtual const void* representation() const = 0;
        virtual void* representation() = 0;
        virtual bool is_folded() const = 0;
        virtual i_concept* do_fold(const i_concept& aRhs) = 0;
        // helpers
    public:
        template<typename SourceIterator>
        struct consume_result
        {
            SourceIterator sourceParsed;
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
        template <typename SourceIterator>
        consume_result<SourceIterator> consume_token(compiler_pass aPass, SourceIterator aSource, SourceIterator aSourceEnd) const
        {
            if (aSource != aSourceEnd)
            {
                bool consumed;
                auto source = &*aSource;
                auto result = consume_token(aPass, source, std::next(source, std::distance(aSource, aSourceEnd)), consumed);
                return consume_result<SourceIterator>{ std::next(aSource, std::distance(source, result)), consumed };
            }
            else
            {
                bool consumed;
                auto result = consume_token(aPass, nullptr, nullptr, consumed);
                return consume_result<SourceIterator>{ aSource, consumed };
            }
        }
        template <typename SourceIterator>
        consume_result<SourceIterator> consume_atom(compiler_pass aPass, const i_atom& aAtom, SourceIterator aSource, SourceIterator aSourceEnd) const
        {
            if (aSource != aSourceEnd)
            {
                bool consumed;
                auto source = &*aSource;
                auto result = consume_atom(aPass, aAtom, source, std::next(source, std::distance(aSource, aSourceEnd)), consumed);
                return consume_result<SourceIterator>{ std::next(aSource, std::distance(source, result)), consumed };
            }
            else
            {
                bool consumed;
                auto result = consume_atom(aPass, aAtom, nullptr, nullptr, consumed);
                return consume_result<SourceIterator>{ aSource, consumed };
            }
        }
        // emit
    public:
        neolib::ref_ptr<i_concept> fold(const i_concept& aRhs)
        {
            if (can_fold(aRhs))
            {
                auto lhs = (is_instance() ? this : can_instantiate() ? instantiate() : this);
                return lhs->do_fold(aRhs);
            }
            return nullptr;
        }
        template <typename Data>
        const Data& data() const
        {
            if (is_instance())
            {
                return *static_cast<const Data*>(representation());
            }
            throw not_an_instance();
        }
        template <typename Data>
        Data& data()
        {
            return const_cast<Data&>(const_cast<const i_concept*>(this)->data<Data>());
        }
    };
}

namespace neos::concept
{
    using neos::language::i_concept;
}
