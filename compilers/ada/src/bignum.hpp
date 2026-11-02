/*
  bignum.hpp

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

// Ada: exact arithmetic for static expressions (universal_integer and universal_real values: integers of any size and
// rationals)

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neos::compilers::ada
{
    class bigint
    {
    public:
        bigint() = default;
        bigint(std::int64_t aValue);
        static bigint from_unsigned(std::uint64_t aValue);
        static std::optional<bigint> parse(std::string_view aDigits, unsigned aBase);  ///< (digits of the base (no '_'))
    public:
        bool is_zero() const { return iMagnitude.empty(); }
        bool negative() const { return iNegative; }
        int sign() const { return is_zero() ? 0 : iNegative ? -1 : 1; }
        bool fits_int64() const;
        bool fits_uint64() const;
        std::int64_t to_int64() const;              ///< (wraps if it does not fit)
        std::uint64_t to_uint64() const;            ///< (modulo 2**64)
        double to_double() const;
        std::string to_string(unsigned aBase = 10u) const;
        std::size_t bits() const;                   ///< (of the magnitude)
    public:
        friend bigint operator+(bigint const& a, bigint const& b);
        friend bigint operator-(bigint const& a, bigint const& b);
        friend bigint operator*(bigint const& a, bigint const& b);
        friend bigint operator-(bigint const& a);
        static void divmod(bigint const& a, bigint const& b, bigint& q, bigint& r);   ///< (truncating: Ada's / and rem)
        friend bigint operator/(bigint const& a, bigint const& b) { bigint q, r; divmod(a, b, q, r); return q; }
        friend bigint operator%(bigint const& a, bigint const& b) { bigint q, r; divmod(a, b, q, r); return r; }   ///< (rem)
        static bigint mod(bigint const& a, bigint const& b);    ///< (Ada's mod: the sign of b)
        static bigint pow(bigint const& a, std::uint64_t aExponent);
        static bigint gcd(bigint a, bigint b);
        static int compare(bigint const& a, bigint const& b);
        friend bool operator==(bigint const& a, bigint const& b) { return compare(a, b) == 0; }
        friend bool operator!=(bigint const& a, bigint const& b) { return compare(a, b) != 0; }
        friend bool operator<(bigint const& a, bigint const& b) { return compare(a, b) < 0; }
        friend bool operator<=(bigint const& a, bigint const& b) { return compare(a, b) <= 0; }
        friend bool operator>(bigint const& a, bigint const& b) { return compare(a, b) > 0; }
        friend bool operator>=(bigint const& a, bigint const& b) { return compare(a, b) >= 0; }
        bigint abs() const { bigint r = *this; r.iNegative = false; return r; }
    private:
        static int compare_magnitude(std::vector<std::uint32_t> const& a, std::vector<std::uint32_t> const& b);
        static std::vector<std::uint32_t> add_magnitude(std::vector<std::uint32_t> const& a, std::vector<std::uint32_t> const& b);
        static std::vector<std::uint32_t> sub_magnitude(std::vector<std::uint32_t> const& a, std::vector<std::uint32_t> const& b);
        void trim();
    private:
        bool iNegative = false;
        std::vector<std::uint32_t> iMagnitude;      ///< (little endian base 2**32 digits; empty: zero)
    };

    // a rational (normalized: the denominator is positive and the fraction is in lowest terms)
    class rational
    {
    public:
        rational() : iDenominator{ 1 } {}
        rational(bigint aValue) : iNumerator{ std::move(aValue) }, iDenominator{ 1 } {}
        rational(bigint aNumerator, bigint aDenominator);
        static rational from_double(double aValue);
        static std::optional<rational> parse_literal(std::string_view aLiteral);   ///< (an Ada numeric literal: decimal or based, with an exponent)
    public:
        bigint const& numerator() const { return iNumerator; }
        bigint const& denominator() const { return iDenominator; }
        bool is_integer() const { return iDenominator == bigint{ 1 }; }
        int sign() const { return iNumerator.sign(); }
        double to_double() const;
        bigint truncate() const;                    ///< (towards zero)
        bigint floor() const;
        bigint round() const;                       ///< (to nearest, half away from zero (Ada's rounding of real to integer))
        std::string to_string() const;
    public:
        friend rational operator+(rational const& a, rational const& b);
        friend rational operator-(rational const& a, rational const& b);
        friend rational operator*(rational const& a, rational const& b);
        friend rational operator/(rational const& a, rational const& b);
        friend rational operator-(rational const& a);
        static rational pow(rational const& a, std::int64_t aExponent);
        static int compare(rational const& a, rational const& b);
        friend bool operator==(rational const& a, rational const& b) { return compare(a, b) == 0; }
        friend bool operator!=(rational const& a, rational const& b) { return compare(a, b) != 0; }
        friend bool operator<(rational const& a, rational const& b) { return compare(a, b) < 0; }
        friend bool operator<=(rational const& a, rational const& b) { return compare(a, b) <= 0; }
        friend bool operator>(rational const& a, rational const& b) { return compare(a, b) > 0; }
        friend bool operator>=(rational const& a, rational const& b) { return compare(a, b) >= 0; }
        rational abs() const { return rational{ iNumerator.abs(), iDenominator }; }
    private:
        void normalize();
    private:
        bigint iNumerator;
        bigint iDenominator;
    };
}
