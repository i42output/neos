/*
  bytecode.hpp

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
#include <neolib/core/variant.hpp>
#include <neolib/core/vector.hpp>
#include <neolib/core/optional.hpp>
#include <neonumeric/integer.hpp>
#include <neonumeric/real.hpp>
#include <neos/language/symbols.hpp>

namespace neos
{
    namespace bytecode
    {
        using _void = std::monostate;
        using boolean = bool;
        using u8 = std::uint8_t;
        using u16 = std::uint16_t;
        using u32 = std::uint32_t;
        using u64 = std::uint64_t;
        using i8 = std::int8_t;
        using i16 = int16_t;
        using i32 = std::int32_t;
        using i64 = std::int64_t;
        using f32 = float;
        using f64 = double;
        using ibig = neonumeric::integer<0u>;
        using fbig = neonumeric::real<0u>;
        using string = neolib::string;
        using reference = void*;

        static_assert(sizeof(f32) == 4);
        static_assert(sizeof(f64) == 8);

        struct composite_type;

        template<typename T>
        struct i_data
        {
            using type = T;

            virtual neolib::i_optional<type>& value() = 0;
            virtual neolib::i_optional<language::symbol_table::iterator>& symbol() = 0;
        };

        template<typename T>
        struct data : i_data<typename neolib::abstract_t<T>>
        {
            using abstract_type = i_data<typename neolib::abstract_t<T>>;
            using type = T;

            neolib::optional<type> v;
            neolib::optional<language::symbol_table::iterator> s;

            neolib::optional<type>& value() final { return v; }
            neolib::optional<language::symbol_table::iterator>& symbol() final { return s; }
        };

        struct i_composite_type : neolib::i_reference_counted
        {
            using abstract_type = i_composite_type;

            using i_data_type = neolib::i_variant<
                i_data<_void>,
                i_data<boolean>,
                i_data<u8>,
                i_data<u16>,
                i_data<u32>,
                i_data<u64>,
                i_data<i8>,
                i_data<i16>,
                i_data<i32>,
                i_data<i64>,
                i_data<f32>,
                i_data<f64>,
                i_data<neolib::abstract_t<ibig>>,
                i_data<neolib::abstract_t<fbig>>,
                i_data<neolib::abstract_t<string>>,
                i_data<reference>,
                i_data<neolib::i_ref_ptr<i_composite_type>>>;

            virtual neolib::i_vector<i_data_type> const& contents() const = 0;
            virtual neolib::i_vector<i_data_type>& contents() = 0;
        };

        using data_type = neolib::variant<
            data<_void>,
            data<boolean>,
            data<u8>,
            data<u16>,
            data<u32>,
            data<u64>,
            data<i8>,
            data<i16>,
            data<i32>,
            data<i64>,
            data<f32>,
            data<f64>,
            data<ibig>,
            data<fbig>,
            data<string>,
            data<reference>,
            data<neolib::ref_ptr<i_composite_type>>>;

        struct composite_type : neolib::reference_counted<i_composite_type>, neolib::vector<data_type>
        {
            neolib::vector<data_type> const& contents() const final
            {
                return *this;
            }
            neolib::vector<data_type>& contents() final
            {
                return *this;
            }
        };
   }
}