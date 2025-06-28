/*
  operator.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <neos/language/type.hpp>

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
            neolib::i_ref_ptr<i_binary_operator>>;

        using operator_type = neolib::variant<
            neolib::ref_ptr<i_unary_operator>,
            neolib::ref_ptr<i_binary_operator>>;

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