/*
  operator.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <neos/language/type.hpp>
#include <neos/language/i_semantic_concept.hpp>

namespace neos
{
    class i_context;

    namespace language
    {
        struct i_unary_operator : neolib::i_reference_counted
        {
            using abstract_type = i_unary_operator;

            virtual void operator()(i_context& context, i_data_type const& lhs, i_data_type& result) const = 0;

            data_type operator()(i_context& context, i_data_type const& lhs) const
            {
                data_type result;
                (*this)(context, lhs, result);
                return result;
            }
        };

        struct i_binary_operator : neolib::i_reference_counted
        {
            using abstract_type = i_binary_operator;

            virtual void operator()(i_context& context, i_data_type const& lhs, i_data_type const& rhs, i_data_type& result) const = 0;

            data_type operator()(i_context& context, i_data_type const& lhs, i_data_type const& rhs) const
            {
                data_type result;
                (*this)(context, lhs, rhs, result);
                return result;
            }
        };

        using i_operator_type = neolib::i_variant<
            neolib::i_ref_ptr<i_unary_operator>,
            neolib::i_ref_ptr<i_binary_operator>,
            neolib::i_ref_ptr<i_semantic_concept>>;

        using operator_type = neolib::variant<
            neolib::ref_ptr<i_unary_operator>,
            neolib::ref_ptr<i_binary_operator>,
            neolib::ref_ptr<i_semantic_concept>>;

        inline std::ostream& operator<<(std::ostream& aStream, i_operator_type const& aOperator)
        {
            std::ostringstream oss;
            neolib::visit([&](auto const& o)
                {
                    using vt = std::decay_t<decltype(o)>;
                    oss << "?op?"; ///< @todo
                }, aOperator);
            return aStream;
        }
    }
}