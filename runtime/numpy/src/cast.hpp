/*
  cast.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

// conversion of element values between types (as NumPy's casts: C casts on x86-64)

#pragma once

#include <cmath>
#include <limits>
#include <type_traits>
#include "numpy.hpp"

namespace neos::numpy
{
    template <typename T> struct is_complex_type : std::false_type {};
    template <typename T> struct is_complex_type<std::complex<T>> : std::true_type {};
    template <typename T> constexpr bool is_complex_v = is_complex_type<T>::value;
    template <typename T> constexpr bool is_half_v = std::is_same_v<T, half>;
    template <typename T> constexpr bool is_floating_v = std::is_floating_point_v<T> || is_half_v<T>;
    template <typename T> constexpr bool is_bool_v = std::is_same_v<T, bool>;
    template <typename T> constexpr bool is_integral_v = std::is_integral_v<T> && !is_bool_v<T>;

    // a floating point value converted to an integer type (out of range or NaN: what x86-64's conversions give)
    template <typename To>
    To float_to_integer(double aValue)
    {
        auto const int64_rule = [](double v) -> std::int64_t
        {
            if (!(v >= -9223372036854775808.0 && v < 9223372036854775808.0))
                return std::numeric_limits<std::int64_t>::min();
            return static_cast<std::int64_t>(v);
        };
        if constexpr (std::is_same_v<To, std::int64_t>)
            return int64_rule(aValue);
        else if constexpr (std::is_same_v<To, std::uint64_t>)
        {
            if (aValue >= 9223372036854775808.0 && aValue < 18446744073709551616.0)
                return static_cast<std::uint64_t>(aValue);
            return static_cast<std::uint64_t>(int64_rule(aValue));
        }
        else if constexpr (std::is_same_v<To, std::uint32_t>)
            return static_cast<std::uint32_t>(static_cast<std::uint64_t>(int64_rule(aValue)));
        else
        {
            std::int32_t v32;
            if (!(aValue > -2147483649.0 && aValue < 2147483648.0))
                v32 = std::numeric_limits<std::int32_t>::min();
            else
                v32 = static_cast<std::int32_t>(aValue);
            return static_cast<To>(v32);
        }
    }

    template <typename To, typename From>
    inline To cast(From aValue)
    {
        if constexpr (std::is_same_v<To, From>)
            return aValue;
        else if constexpr (is_complex_v<From>)
        {
            if constexpr (is_complex_v<To>)
                return To{ static_cast<typename To::value_type>(aValue.real()), static_cast<typename To::value_type>(aValue.imag()) };
            else if constexpr (is_bool_v<To>)
                return aValue.real() != 0 || aValue.imag() != 0;
            else
                return cast<To>(aValue.real());
        }
        else if constexpr (is_half_v<From>)
            return cast<To>(static_cast<float>(aValue));
        else if constexpr (is_bool_v<To>)
            return aValue != 0;
        else if constexpr (is_complex_v<To>)
            return To{ cast<typename To::value_type>(aValue), 0 };
        else if constexpr (is_half_v<To>)
        {
            if constexpr (is_bool_v<From>)
                return half{ aValue ? 1.0f : 0.0f };
            else
                return half{ static_cast<double>(aValue) };
        }
        else if constexpr (is_bool_v<From>)
            return static_cast<To>(aValue ? 1 : 0);
        else if constexpr (std::is_floating_point_v<From> && is_integral_v<To>)
            return float_to_integer<To>(static_cast<double>(aValue));
        else
            return static_cast<To>(aValue);
    }

    // a function called with the C++ type of a dtype: f(ctype<D>{}) (a tag)
    template <typename F>
    decltype(auto) dispatch(dtype aType, F&& f)
    {
        switch (aType)
        {
        case dtype::Bool: return f(bool{});
        case dtype::Int8: return f(std::int8_t{});
        case dtype::UInt8: return f(std::uint8_t{});
        case dtype::Int16: return f(std::int16_t{});
        case dtype::UInt16: return f(std::uint16_t{});
        case dtype::Int32: return f(std::int32_t{});
        case dtype::UInt32: return f(std::uint32_t{});
        case dtype::Int64: return f(std::int64_t{});
        case dtype::UInt64: return f(std::uint64_t{});
        case dtype::Float16: return f(half{});
        case dtype::Float32: return f(float{});
        case dtype::Float64: return f(double{});
        case dtype::Complex64: return f(std::complex<float>{});
        default: return f(std::complex<double>{});
        }
    }

    // an element (of type aType) read as T
    template <typename T>
    inline T read_as(unsigned char const* aPointer, dtype aType)
    {
        return dispatch(aType, [&](auto tag) -> T
        {
            using S = decltype(tag);
            S value;
            std::memcpy(&value, aPointer, sizeof(S));
            return cast<T>(value);
        });
    }

    // an element (of type aType) written from T
    template <typename T>
    inline void write_as(unsigned char* aPointer, dtype aType, T aValue)
    {
        dispatch(aType, [&](auto tag)
        {
            using D = decltype(tag);
            D const value = cast<D>(aValue);
            std::memcpy(aPointer, &value, sizeof(D));
        });
    }

    template <typename T> constexpr dtype dtype_of_ctype()
    {
        if constexpr (std::is_same_v<T, bool>) return dtype::Bool;
        else if constexpr (std::is_same_v<T, std::int8_t>) return dtype::Int8;
        else if constexpr (std::is_same_v<T, std::uint8_t>) return dtype::UInt8;
        else if constexpr (std::is_same_v<T, std::int16_t>) return dtype::Int16;
        else if constexpr (std::is_same_v<T, std::uint16_t>) return dtype::UInt16;
        else if constexpr (std::is_same_v<T, std::int32_t>) return dtype::Int32;
        else if constexpr (std::is_same_v<T, std::uint32_t>) return dtype::UInt32;
        else if constexpr (std::is_same_v<T, std::int64_t>) return dtype::Int64;
        else if constexpr (std::is_same_v<T, std::uint64_t>) return dtype::UInt64;
        else if constexpr (std::is_same_v<T, half>) return dtype::Float16;
        else if constexpr (std::is_same_v<T, float>) return dtype::Float32;
        else if constexpr (std::is_same_v<T, double>) return dtype::Float64;
        else if constexpr (std::is_same_v<T, std::complex<float>>) return dtype::Complex64;
        else return dtype::Complex128;
    }
}
