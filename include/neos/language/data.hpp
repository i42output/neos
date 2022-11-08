/*
  data.hpp

  Copyright (c) 2022 Leigh Johnston.  All Rights Reserved.

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

namespace neos
{
    namespace language
    {
        enum class data_type : std::uint32_t
        {
            _void,
            _bool,
            u8,
            u16,
            u32,
            u64,
            i8,
            i16,
            i32,
            i64,
            f32,
            f64,
            string
        };

        using data = std::variant<
            std::monostate,
            bool,
            std::uint8_t,
            std::uint16_t,
            std::uint32_t,
            std::uint64_t,
            std::int8_t,
            std::int16_t,
            std::int32_t,
            std::int64_t,
            float,
            double,
            std::string>;
    }
}