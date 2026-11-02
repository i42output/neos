/*
  int128.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

// 128-bit integers (the runtime's exact intermediate results) and overflow checked arithmetic: the compiler's own where it has them
// (__int128, __builtin_*_overflow), else portable (e.g. MSVC)

#pragma once

#include <cstdint>
#include <concepts>
#include <limits>
#include <type_traits>
#if !defined(__SIZEOF_INT128__) && defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace neos::ada_runtime
{
#if defined(__SIZEOF_INT128__)
    using int128 = __int128;
    using uint128 = unsigned __int128;
#else
    // (two's complement, modulo 2^128)
    class uint128
    {
    public:
        constexpr uint128() = default;
        template <std::integral T>
        constexpr uint128(T aValue) : iLow{ static_cast<std::uint64_t>(aValue) }
        {
            if constexpr (std::is_signed_v<T>)
                iHigh = aValue < 0 ? ~std::uint64_t{} : 0u;
        }
        constexpr uint128(std::uint64_t aHigh, std::uint64_t aLow) : iLow{ aLow }, iHigh{ aHigh } {}
    public:
        template <std::integral T>
        constexpr explicit operator T() const { return static_cast<T>(iLow); }
        constexpr std::uint64_t low() const { return iLow; }
        constexpr std::uint64_t high() const { return iHigh; }
    public:
        friend constexpr bool operator==(uint128 const& aLeft, uint128 const& aRight) { return aLeft.iLow == aRight.iLow && aLeft.iHigh == aRight.iHigh; }
        friend constexpr bool operator<(uint128 const& aLeft, uint128 const& aRight) { return aLeft.iHigh != aRight.iHigh ? aLeft.iHigh < aRight.iHigh : aLeft.iLow < aRight.iLow; }
        friend constexpr bool operator>(uint128 const& aLeft, uint128 const& aRight) { return aRight < aLeft; }
        friend constexpr bool operator<=(uint128 const& aLeft, uint128 const& aRight) { return !(aRight < aLeft); }
        friend constexpr bool operator>=(uint128 const& aLeft, uint128 const& aRight) { return !(aLeft < aRight); }
        friend constexpr uint128 operator~(uint128 const& aValue) { return uint128{ ~aValue.iHigh, ~aValue.iLow }; }
        friend constexpr uint128 operator-(uint128 const& aValue) { return ~aValue + 1u; }
        friend constexpr uint128 operator+(uint128 const& aLeft, uint128 const& aRight)
        {
            auto const low = aLeft.iLow + aRight.iLow;
            return uint128{ aLeft.iHigh + aRight.iHigh + (low < aLeft.iLow ? 1u : 0u), low };
        }
        friend constexpr uint128 operator-(uint128 const& aLeft, uint128 const& aRight)
        {
            return uint128{ aLeft.iHigh - aRight.iHigh - (aLeft.iLow < aRight.iLow ? 1u : 0u), aLeft.iLow - aRight.iLow };
        }
        friend constexpr uint128 operator*(uint128 const& aLeft, uint128 const& aRight)
        {
            auto const product = multiply(aLeft.iLow, aRight.iLow);
            return uint128{ product.iHigh + aLeft.iLow * aRight.iHigh + aLeft.iHigh * aRight.iLow, product.iLow };
        }
        friend constexpr uint128 operator/(uint128 const& aLeft, uint128 const& aRight) { uint128 remainder; return divide(aLeft, aRight, remainder); }
        friend constexpr uint128 operator%(uint128 const& aLeft, uint128 const& aRight) { uint128 remainder; divide(aLeft, aRight, remainder); return remainder; }
        friend constexpr uint128 operator<<(uint128 const& aValue, int aShift)
        {
            if (aShift == 0)
                return aValue;
            if (aShift >= 64)
                return uint128{ aValue.iLow << (aShift - 64), 0u };
            return uint128{ (aValue.iHigh << aShift) | (aValue.iLow >> (64 - aShift)), aValue.iLow << aShift };
        }
        friend constexpr uint128 operator>>(uint128 const& aValue, int aShift)
        {
            if (aShift == 0)
                return aValue;
            if (aShift >= 64)
                return uint128{ 0u, aValue.iHigh >> (aShift - 64) };
            return uint128{ aValue.iHigh >> aShift, (aValue.iLow >> aShift) | (aValue.iHigh << (64 - aShift)) };
        }
        uint128& operator+=(uint128 const& aRight) { return *this = *this + aRight; }
        uint128& operator-=(uint128 const& aRight) { return *this = *this - aRight; }
        uint128& operator*=(uint128 const& aRight) { return *this = *this * aRight; }
        uint128& operator/=(uint128 const& aRight) { return *this = *this / aRight; }
        uint128& operator%=(uint128 const& aRight) { return *this = *this % aRight; }
        uint128& operator++() { return *this = *this + 1u; }
    public:
        // (the full product of two 64-bit values)
        static constexpr uint128 multiply(std::uint64_t aLeft, std::uint64_t aRight)
        {
            std::uint64_t const l0 = aLeft & 0xFFFFFFFFu, l1 = aLeft >> 32u, r0 = aRight & 0xFFFFFFFFu, r1 = aRight >> 32u;
            std::uint64_t const p00 = l0 * r0, p01 = l0 * r1, p10 = l1 * r0, p11 = l1 * r1;
            std::uint64_t const middle = (p00 >> 32u) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
            return uint128{ p11 + (p01 >> 32u) + (p10 >> 32u) + (middle >> 32u), (p00 & 0xFFFFFFFFu) | (middle << 32u) };
        }
        // (the quotient and remainder (a divisor of zero: as the native division's, undefined))
        static constexpr uint128 divide(uint128 const& aDividend, uint128 const& aDivisor, uint128& aRemainder)
        {
            if (aDividend.iHigh == 0u && aDivisor.iHigh == 0u)
            {
                aRemainder = uint128{ 0u, aDividend.iLow % aDivisor.iLow };
                return uint128{ 0u, aDividend.iLow / aDivisor.iLow };
            }
            if (aDivisor > aDividend)
            {
                aRemainder = aDividend;
                return uint128{};
            }
            // (a bit at a time, from the dividend's highest)
            uint128 quotient;
            uint128 remainder;
            for (int bit = 127; bit >= 0; --bit)
            {
                remainder = remainder << 1;
                if ((bit >= 64 ? aDividend.iHigh >> (bit - 64) : aDividend.iLow >> bit) & 1u)
                    remainder.iLow |= 1u;
                if (remainder >= aDivisor)
                {
                    remainder = remainder - aDivisor;
                    if (bit >= 64)
                        quotient.iHigh |= std::uint64_t{ 1u } << (bit - 64);
                    else
                        quotient.iLow |= std::uint64_t{ 1u } << bit;
                }
            }
            aRemainder = remainder;
            return quotient;
        }
    private:
        std::uint64_t iLow = 0u;
        std::uint64_t iHigh = 0u;
    };

    // (two's complement: division truncates (the remainder has the dividend's sign), as the native division)
    class int128
    {
    public:
        constexpr int128() = default;
        template <std::integral T>
        constexpr int128(T aValue) : iBits{ aValue } {}
        constexpr explicit int128(uint128 const& aBits) : iBits{ aBits } {}
    public:
        template <std::integral T>
        constexpr explicit operator T() const { return static_cast<T>(iBits); }
        constexpr explicit operator uint128() const { return iBits; }
        constexpr bool negative() const { return (iBits.high() >> 63u) != 0u; }
        constexpr uint128 magnitude() const { return negative() ? -iBits : iBits; }
    public:
        friend constexpr bool operator==(int128 const& aLeft, int128 const& aRight) { return aLeft.iBits == aRight.iBits; }
        friend constexpr bool operator<(int128 const& aLeft, int128 const& aRight)
        {
            return aLeft.negative() != aRight.negative() ? aLeft.negative() : aLeft.iBits < aRight.iBits;
        }
        friend constexpr bool operator>(int128 const& aLeft, int128 const& aRight) { return aRight < aLeft; }
        friend constexpr bool operator<=(int128 const& aLeft, int128 const& aRight) { return !(aRight < aLeft); }
        friend constexpr bool operator>=(int128 const& aLeft, int128 const& aRight) { return !(aLeft < aRight); }
        friend constexpr int128 operator-(int128 const& aValue) { return int128{ -aValue.iBits }; }
        friend constexpr int128 operator+(int128 const& aLeft, int128 const& aRight) { return int128{ aLeft.iBits + aRight.iBits }; }
        friend constexpr int128 operator-(int128 const& aLeft, int128 const& aRight) { return int128{ aLeft.iBits - aRight.iBits }; }
        friend constexpr int128 operator*(int128 const& aLeft, int128 const& aRight) { return int128{ aLeft.iBits * aRight.iBits }; }
        friend constexpr int128 operator/(int128 const& aLeft, int128 const& aRight)
        {
            auto const quotient = int128{ aLeft.magnitude() / aRight.magnitude() };
            return aLeft.negative() != aRight.negative() ? -quotient : quotient;
        }
        friend constexpr int128 operator%(int128 const& aLeft, int128 const& aRight)
        {
            auto const remainder = int128{ aLeft.magnitude() % aRight.magnitude() };
            return aLeft.negative() ? -remainder : remainder;
        }
        int128& operator+=(int128 const& aRight) { return *this = *this + aRight; }
        int128& operator-=(int128 const& aRight) { return *this = *this - aRight; }
        int128& operator*=(int128 const& aRight) { return *this = *this * aRight; }
    private:
        uint128 iBits;
    };
#endif

    // (aLeft op aRight in *aResult (modulo, if it overflows); true if it overflows)
    template <typename T>
    inline bool add_overflow(T aLeft, T aRight, T* aResult)
    {
#if (defined(__GNUC__) || defined(__clang__)) && defined(__SIZEOF_INT128__)
        return __builtin_add_overflow(aLeft, aRight, aResult);
#else
        static_assert(std::is_same_v<T, std::int64_t>);
        auto const sum = static_cast<std::int64_t>(static_cast<std::uint64_t>(aLeft) + static_cast<std::uint64_t>(aRight));
        *aResult = sum;
        return ((aLeft ^ sum) & (aRight ^ sum)) < 0;
#endif
    }

    template <typename T>
    inline bool mul_overflow(T aLeft, T aRight, T* aResult)
    {
#if (defined(__GNUC__) || defined(__clang__)) && defined(__SIZEOF_INT128__)
        return __builtin_mul_overflow(aLeft, aRight, aResult);
#else
        if constexpr (std::is_same_v<T, std::int64_t>)
        {
            auto const product = int128{ aLeft } * int128{ aRight };
            *aResult = static_cast<std::int64_t>(product);
            return product < int128{ std::numeric_limits<std::int64_t>::min() } || product > int128{ std::numeric_limits<std::int64_t>::max() };
        }
        else
        {
            static_assert(std::is_same_v<T, int128>);
            *aResult = aLeft * aRight;
            // (the magnitudes' product within 2^127 - 1 (2^127 if it is negative))
            auto const left = aLeft.magnitude();
            auto const right = aRight.magnitude();
            if (left == 0u || right == 0u)
                return false;
            auto const limit = (uint128{ 1u } << 127) - (aLeft.negative() != aRight.negative() ? 0u : 1u);
            return right > limit / left;
        }
#endif
    }
}
