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
    public:
        struct no_parent : std::logic_error { no_parent() : std::logic_error("neos::language::i_semantic_concept::no_parent") {} };
        struct invalid_fold : std::logic_error { invalid_fold() : std::logic_error("neos::language::i_semantic_concept::invalid_fold") {} };
    public:
        using data_type = void;
    public:
        typedef i_semantic_concept abstract_type;
        typedef neolib::i_string_view::const_iterator source_iterator;
    public:
        virtual bool has_parent() const = 0;
        virtual const i_semantic_concept& parent() const = 0;
        virtual const neolib::i_string& name() const = 0;
        // parse
    public:
        virtual void update_source(source_iterator aBegin, source_iterator aEnd) = 0;
        virtual bool holds_data() const = 0;
        virtual void const* data() const = 0;
        virtual void* data() = 0;
        // emit
    public:
        virtual emit_type emit_as() const = 0;
        virtual bool can_fold() const = 0;
        virtual bool can_fold(const i_semantic_concept& aRhs) const = 0;
        // helpers
    public:
        std::string trace() const
        {
            return name().to_std_string();
        }
        // family
    public:
        bool is_same(const i_semantic_concept& other) const
        {
            return &other == this;
        }
        bool is_ancestor_of(const i_semantic_concept& child) const
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
        bool is_related_to(const i_semantic_concept& other) const
        {
            return is_same(other) || is_ancestor_of(other);
        }
        // parse
    public:
        neolib::ref_ptr<i_semantic_concept> instantiate(i_context& aContext, source_iterator aBegin, source_iterator aEnd) const
        {
            neolib::ref_ptr<i_semantic_concept> result;
            do_instantiate(aContext, aBegin, aEnd, result);
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
        neolib::ref_ptr<i_semantic_concept> fold(i_context& aContext, const i_semantic_concept& aRhs) 
        {
            neolib::ref_ptr<i_semantic_concept> result;
            if (can_fold(aRhs))
                do_fold(aContext, aRhs, result);
            return result;
        }
        // parse
    protected:
        virtual void do_instantiate(i_context& aContext, source_iterator aBegin, source_iterator aEnd, neolib::i_ref_ptr<i_semantic_concept>& aResult) const = 0;
        // emit
    protected:
        virtual void do_fold(i_context& aContext, neolib::i_ref_ptr<i_semantic_concept>& aResult) = 0;
        virtual void do_fold(i_context& aContext, const i_semantic_concept& aRhs, neolib::i_ref_ptr<i_semantic_concept>& aResult) = 0;
    };
}

namespace neos::concepts
{
    using neos::language::i_semantic_concept;
}
