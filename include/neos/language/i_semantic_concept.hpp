/*
  i_semantic_concept.hpp

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
#include <neolib/core/i_reference_counted.hpp>
#include <neolib/core/i_string.hpp>
#include <neolib/core/i_string_view.hpp>
#include <neos/fwd.hpp>

namespace neos::language
{
    enum class emit_type : std::uint32_t
    {
        Prefix,
        Infix,
        Postfix
    };

    class i_semantic_concept : public neolib::i_reference_counted
    {
        template <typename Concept>
        friend class semanteme;
    public:
        struct no_parent : std::logic_error { no_parent() : std::logic_error("neos::language::i_semantic_concept::no_parent") {} };
        struct invalid_fold : std::logic_error { invalid_fold() : std::logic_error("neos::language::i_semantic_concept::invalid_fold") {} };
    public:
        using data_type = void;
    public:
        typedef i_semantic_concept abstract_type;
        typedef neolib::i_string_view::const_iterator source_iterator;
    public:
        virtual void clone(i_semantic_concept& aInstance, neolib::i_ref_ptr<i_semantic_concept>& aCopy) const = 0;
    public:
        virtual bool has_parent() const = 0;
        virtual i_semantic_concept const& parent() const = 0;
        virtual neolib::i_string const& name() const = 0;
        virtual bool is(neolib::i_string const& aName) const = 0;
        virtual bool similar(i_semantic_concept const& aRhs) const = 0;
        // parse
    public:
        virtual neolib::i_string_view const& source() const = 0;
        virtual void update_source(neolib::i_string_view const& aSource) = 0;
        virtual void instance(neolib::i_ref_ptr<i_semantic_concept>& aInstance) const = 0;
        neolib::ref_ptr<i_semantic_concept> instance() const { neolib::ref_ptr<i_semantic_concept> result; instance(result); return result; }
        virtual void set_instance(i_semantic_concept& aInstance) = 0;
        virtual bool holds_data() const = 0;
        virtual void const* data() const = 0;
        virtual void* data() = 0;
        // emit
    public:
        virtual emit_type emit_as() const = 0;
        virtual bool has_ghosts() const = 0;
        virtual bool unstructured() const = 0;
        virtual bool can_fold() const = 0;
        virtual bool can_fold(i_semantic_concept const& aRhs) const = 0;
        // debug
    public:
        virtual void trace(neolib::i_string& aResult) const = 0;
        // helpers
    public:
        neolib::ref_ptr<i_semantic_concept> clone(i_semantic_concept& aInstance) const
        {
            neolib::ref_ptr<i_semantic_concept> copy;
            clone(aInstance, copy);
            return copy;
        }
        std::string trace() const
        {
            neolib::string result;
            trace(result);
            return result.to_std_string();
        }
        // family
    public:
        bool is_same(i_semantic_concept const& other) const
        {
            return &other == this;
        }
        bool is_ancestor_of(i_semantic_concept const& child) const
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
        bool is_related_to(i_semantic_concept const& other) const
        {
            return is_same(other) || is_ancestor_of(other);
        }
        // parse
    public:
        neolib::ref_ptr<i_semantic_concept> instantiate(i_context& aContext, neolib::i_string_view const& aSource) const
        {
            neolib::ref_ptr<i_semantic_concept> result;
            do_instantiate(aContext, aSource, result);
            return result;
        }
        template <typename T>
        T const& data() const
        {
            return *static_cast<T const*>(data());
        }
        template <typename T>
        T& data()
        {
            return *static_cast<T*>(data());
        }
        // emit
    public:
        neolib::ref_ptr<i_semantic_concept> fold(i_context& aContext)
        {
            neolib::ref_ptr<i_semantic_concept> result;
            if (can_fold())
                do_fold(aContext, result);
            return result;
        }
        neolib::ref_ptr<i_semantic_concept> fold(i_context& aContext, i_semantic_concept const& aRhs)
        {
            neolib::ref_ptr<i_semantic_concept> result;
            if (can_fold(aRhs))
                do_fold(aContext, aRhs, result);
            return result;
        }
        // parse
    protected:
        virtual void do_instantiate(i_context& aContext, neolib::i_string_view const& aSource, neolib::i_ref_ptr<i_semantic_concept>& aResult) const = 0;
        // emit
    protected:
        virtual void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) = 0;
        virtual void do_fold(i_context& aContext, i_semantic_concept const& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) = 0;
    };
}

namespace neos::concepts
{
    using neos::language::i_semantic_concept;
}
