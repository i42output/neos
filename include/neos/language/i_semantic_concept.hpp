/*
  i_semantic_concept.hpp

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
        struct invalid_fold : std::logic_error { invalid_fold() : std::logic_error("neos::language::i_semantic_concept::invalid_fold") {} };
    public:
        using data_type = void;
    public:
        typedef i_semantic_concept abstract_type;
        typedef neolib::i_string_view::const_iterator source_iterator;
    public:
        virtual void clone(i_semantic_concept& aInstance, neolib::i_ref_ptr<i_semantic_concept>& aCopy) const = 0;
    public:
        virtual neolib::i_string_view const& name() const = 0;
        virtual bool is(neolib::i_string_view const& aName) const = 0;
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
            return is(child.name());
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
