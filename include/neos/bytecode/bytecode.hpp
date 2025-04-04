/*
  bytecode.hpp

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
#include <variant>

namespace neos
{
    namespace bytecode
    {
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
        using reference = void*;

        static_assert(sizeof(f32) == 4);
        static_assert(sizeof(f64) == 8);

        using data_type = std::variant<std::monostate, boolean, u8, u16, u32, u64, i8, i16, i32, i64, f32, f64, reference>;
   }
}