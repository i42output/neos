/*
  bignum.cpp

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

#include <algorithm>
#include <cmath>
#include <cstring>
#include "bignum.hpp"

namespace neos::compilers::ada
{
    // --- bigint

    bigint::bigint(std::int64_t aValue)
    {
        iNegative = aValue < 0;
        std::uint64_t m = iNegative ? std::uint64_t{ 0u } - static_cast<std::uint64_t>(aValue) : static_cast<std::uint64_t>(aValue);
        while (m != 0u)
        {
            iMagnitude.push_back(static_cast<std::uint32_t>(m));
            m >>= 32u;
        }
    }

    bigint bigint::from_unsigned(std::uint64_t aValue)
    {
        bigint result;
        while (aValue != 0u)
        {
            result.iMagnitude.push_back(static_cast<std::uint32_t>(aValue));
            aValue >>= 32u;
        }
        return result;
    }

    std::optional<bigint> bigint::parse(std::string_view aDigits, unsigned aBase)
    {
        bigint result;
        if (aDigits.empty())
            return {};
        for (auto c : aDigits)
        {
            unsigned d;
            if (c >= '0' && c <= '9')
                d = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'z')
                d = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'Z')
                d = static_cast<unsigned>(c - 'A' + 10);
            else
                return {};
            if (d >= aBase)
                return {};
            // (result = result * base + d, in place)
            std::uint64_t carry = d;
            for (auto& digit : result.iMagnitude)
            {
                std::uint64_t const v = static_cast<std::uint64_t>(digit) * aBase + carry;
                digit = static_cast<std::uint32_t>(v);
                carry = v >> 32u;
            }
            if (carry != 0u)
                result.iMagnitude.push_back(static_cast<std::uint32_t>(carry));
        }
        result.trim();
        return result;
    }

    void bigint::trim()
    {
        while (!iMagnitude.empty() && iMagnitude.back() == 0u)
            iMagnitude.pop_back();
        if (iMagnitude.empty())
            iNegative = false;
    }

    namespace
    {
        std::uint64_t low64(std::vector<std::uint32_t> const& aMagnitude)
        {
            std::uint64_t result = 0u;
            if (aMagnitude.size() > 0u)
                result = aMagnitude[0];
            if (aMagnitude.size() > 1u)
                result |= static_cast<std::uint64_t>(aMagnitude[1]) << 32u;
            return result;
        }
    }

    bool bigint::fits_int64() const
    {
        if (iMagnitude.size() > 2u)
            return false;
        auto const magnitude = low64(iMagnitude);
        return iNegative ? magnitude <= (std::uint64_t{ 1u } << 63u) : magnitude < (std::uint64_t{ 1u } << 63u);
    }

    bool bigint::fits_uint64() const
    {
        return !iNegative && iMagnitude.size() <= 2u;
    }

    std::int64_t bigint::to_int64() const
    {
        auto const magnitude = low64(iMagnitude);
        return static_cast<std::int64_t>(iNegative ? std::uint64_t{ 0u } - magnitude : magnitude);
    }

    std::uint64_t bigint::to_uint64() const
    {
        auto const magnitude = low64(iMagnitude);
        return iNegative ? std::uint64_t{ 0u } - magnitude : magnitude;
    }

    double bigint::to_double() const
    {
        double result = 0.0;
        for (std::size_t i = iMagnitude.size(); i-- > 0u;)
            result = result * 4294967296.0 + iMagnitude[i];
        return iNegative ? -result : result;
    }

    std::size_t bigint::bits() const
    {
        if (iMagnitude.empty())
            return 0u;
        std::size_t result = (iMagnitude.size() - 1u) * 32u;
        for (auto top = iMagnitude.back(); top != 0u; top >>= 1u)
            ++result;
        return result;
    }

    std::string bigint::to_string(unsigned aBase) const
    {
        if (iMagnitude.empty())
            return "0";
        std::string result;
        auto magnitude = iMagnitude;
        while (!magnitude.empty())
        {
            std::uint64_t remainder = 0u;
            for (std::size_t i = magnitude.size(); i-- > 0u;)
            {
                std::uint64_t const v = (remainder << 32u) | magnitude[i];
                magnitude[i] = static_cast<std::uint32_t>(v / aBase);
                remainder = v % aBase;
            }
            result += "0123456789ABCDEF"[remainder];
            while (!magnitude.empty() && magnitude.back() == 0u)
                magnitude.pop_back();
        }
        if (iNegative)
            result += '-';
        std::reverse(result.begin(), result.end());
        return result;
    }

    int bigint::compare_magnitude(std::vector<std::uint32_t> const& a, std::vector<std::uint32_t> const& b)
    {
        if (a.size() != b.size())
            return a.size() < b.size() ? -1 : 1;
        for (std::size_t i = a.size(); i-- > 0u;)
            if (a[i] != b[i])
                return a[i] < b[i] ? -1 : 1;
        return 0;
    }

    std::vector<std::uint32_t> bigint::add_magnitude(std::vector<std::uint32_t> const& a, std::vector<std::uint32_t> const& b)
    {
        std::vector<std::uint32_t> result;
        result.resize(std::max(a.size(), b.size()) + 1u);
        std::uint64_t carry = 0u;
        for (std::size_t i = 0u; i < result.size(); ++i)
        {
            std::uint64_t const v = (i < a.size() ? a[i] : 0u) + static_cast<std::uint64_t>(i < b.size() ? b[i] : 0u) + carry;
            result[i] = static_cast<std::uint32_t>(v);
            carry = v >> 32u;
        }
        return result;
    }

    std::vector<std::uint32_t> bigint::sub_magnitude(std::vector<std::uint32_t> const& a, std::vector<std::uint32_t> const& b)
    {
        // (|a| >= |b|)
        std::vector<std::uint32_t> result(a.size());
        std::int64_t borrow = 0;
        for (std::size_t i = 0u; i < a.size(); ++i)
        {
            std::int64_t v = static_cast<std::int64_t>(a[i]) - (i < b.size() ? static_cast<std::int64_t>(b[i]) : 0) - borrow;
            borrow = v < 0 ? 1 : 0;
            if (v < 0)
                v += std::int64_t{ 1 } << 32;
            result[i] = static_cast<std::uint32_t>(v);
        }
        return result;
    }

    bigint operator+(bigint const& a, bigint const& b)
    {
        bigint result;
        if (a.iNegative == b.iNegative)
        {
            result.iMagnitude = bigint::add_magnitude(a.iMagnitude, b.iMagnitude);
            result.iNegative = a.iNegative;
        }
        else if (bigint::compare_magnitude(a.iMagnitude, b.iMagnitude) >= 0)
        {
            result.iMagnitude = bigint::sub_magnitude(a.iMagnitude, b.iMagnitude);
            result.iNegative = a.iNegative;
        }
        else
        {
            result.iMagnitude = bigint::sub_magnitude(b.iMagnitude, a.iMagnitude);
            result.iNegative = b.iNegative;
        }
        result.trim();
        return result;
    }

    bigint operator-(bigint const& a)
    {
        bigint result = a;
        if (!result.is_zero())
            result.iNegative = !result.iNegative;
        return result;
    }

    bigint operator-(bigint const& a, bigint const& b)
    {
        return a + (-b);
    }

    bigint operator*(bigint const& a, bigint const& b)
    {
        bigint result;
        if (a.is_zero() || b.is_zero())
            return result;
        result.iMagnitude.assign(a.iMagnitude.size() + b.iMagnitude.size(), 0u);
        for (std::size_t i = 0u; i < a.iMagnitude.size(); ++i)
        {
            std::uint64_t carry = 0u;
            for (std::size_t j = 0u; j < b.iMagnitude.size(); ++j)
            {
                std::uint64_t const v = static_cast<std::uint64_t>(a.iMagnitude[i]) * b.iMagnitude[j] + result.iMagnitude[i + j] + carry;
                result.iMagnitude[i + j] = static_cast<std::uint32_t>(v);
                carry = v >> 32u;
            }
            std::size_t k = i + b.iMagnitude.size();
            while (carry != 0u)
            {
                std::uint64_t const v = static_cast<std::uint64_t>(result.iMagnitude[k]) + carry;
                result.iMagnitude[k] = static_cast<std::uint32_t>(v);
                carry = v >> 32u;
                ++k;
            }
        }
        result.iNegative = a.iNegative != b.iNegative;
        result.trim();
        return result;
    }

    void bigint::divmod(bigint const& a, bigint const& b, bigint& q, bigint& r)
    {
        q = bigint{};
        r = bigint{};
        if (b.is_zero())
            return;
        if (compare_magnitude(a.iMagnitude, b.iMagnitude) < 0)
        {
            r = a;
            return;
        }
        if (b.iMagnitude.size() == 1u)
        {
            std::uint64_t const d = b.iMagnitude[0];
            std::uint64_t remainder = 0u;
            q.iMagnitude.resize(a.iMagnitude.size());
            for (std::size_t i = a.iMagnitude.size(); i-- > 0u;)
            {
                std::uint64_t const v = (remainder << 32u) | a.iMagnitude[i];
                q.iMagnitude[i] = static_cast<std::uint32_t>(v / d);
                remainder = v % d;
            }
            q.iNegative = a.iNegative != b.iNegative;
            q.trim();
            r = bigint::from_unsigned(remainder);
            if (a.iNegative && !r.is_zero())
                r.iNegative = true;
            return;
        }
        // (shift and subtract: one bit at a time)
        bigint remainder;
        bigint const divisor = b.abs();
        q.iMagnitude.assign(a.iMagnitude.size(), 0u);
        for (std::size_t bit = a.bits(); bit-- > 0u;)
        {
            // remainder = remainder * 2 + bit
            std::uint32_t carry = (a.iMagnitude[bit / 32u] >> (bit % 32u)) & 1u;
            for (auto& digit : remainder.iMagnitude)
            {
                std::uint32_t const next = digit >> 31u;
                digit = (digit << 1u) | carry;
                carry = next;
            }
            if (carry != 0u)
                remainder.iMagnitude.push_back(carry);
            if (compare_magnitude(remainder.iMagnitude, divisor.iMagnitude) >= 0)
            {
                remainder.iMagnitude = sub_magnitude(remainder.iMagnitude, divisor.iMagnitude);
                remainder.trim();
                q.iMagnitude[bit / 32u] |= 1u << (bit % 32u);
            }
        }
        q.iNegative = a.iNegative != b.iNegative;
        q.trim();
        r = remainder;
        if (a.iNegative && !r.is_zero())
            r.iNegative = true;
    }

    bigint bigint::mod(bigint const& a, bigint const& b)
    {
        auto r = a % b;
        if (!r.is_zero() && r.negative() != b.negative())
            r = r + b;
        return r;
    }

    bigint bigint::pow(bigint const& a, std::uint64_t aExponent)
    {
        bigint result{ 1 };
        bigint base = a;
        while (aExponent != 0u)
        {
            if (aExponent & 1u)
                result = result * base;
            aExponent >>= 1u;
            if (aExponent != 0u)
                base = base * base;
        }
        return result;
    }

    bigint bigint::gcd(bigint a, bigint b)
    {
        a = a.abs();
        b = b.abs();
        while (!b.is_zero())
        {
            auto r = a % b;
            a = std::move(b);
            b = std::move(r);
        }
        return a;
    }

    int bigint::compare(bigint const& a, bigint const& b)
    {
        if (a.iNegative != b.iNegative)
            return a.iNegative ? -1 : 1;
        auto const c = compare_magnitude(a.iMagnitude, b.iMagnitude);
        return a.iNegative ? -c : c;
    }

    // --- rational

    rational::rational(bigint aNumerator, bigint aDenominator) :
        iNumerator{ std::move(aNumerator) }, iDenominator{ std::move(aDenominator) }
    {
        normalize();
    }

    void rational::normalize()
    {
        if (iDenominator.is_zero())
        {
            iDenominator = bigint{ 1 };
            return;
        }
        if (iDenominator.negative())
        {
            iNumerator = -iNumerator;
            iDenominator = -iDenominator;
        }
        auto const g = bigint::gcd(iNumerator, iDenominator);
        if (!g.is_zero() && g != bigint{ 1 })
        {
            iNumerator = iNumerator / g;
            iDenominator = iDenominator / g;
        }
    }

    rational rational::from_double(double aValue)
    {
        if (aValue == 0.0 || !std::isfinite(aValue))
            return rational{};
        int exponent = 0;
        double mantissa = std::frexp(aValue, &exponent);
        // (53 bits of mantissa as an integer)
        auto const m = static_cast<std::int64_t>(std::ldexp(mantissa, 53));
        exponent -= 53;
        if (exponent >= 0)
            return rational{ bigint{ m } * bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(exponent)) };
        return rational{ bigint{ m }, bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(-exponent)) };
    }

    std::optional<rational> rational::parse_literal(std::string_view aLiteral)
    {
        std::string text;
        for (auto c : aLiteral)
            if (c != '_')
                text += c;
        unsigned base = 10u;
        std::string digits = text;
        std::string exponentText;
        auto sharp = text.find_first_of("#:");
        if (sharp != std::string::npos)
        {
            auto const b = bigint::parse(text.substr(0u, sharp), 10u);
            if (!b || !b->fits_int64() || b->to_int64() < 2 || b->to_int64() > 16)
                return {};
            base = static_cast<unsigned>(b->to_int64());
            auto const close = text.find(text[sharp], sharp + 1u);
            if (close == std::string::npos)
                return {};
            digits = text.substr(sharp + 1u, close - sharp - 1u);
            if (close + 1u < text.size())
            {
                if (text[close + 1u] != 'e' && text[close + 1u] != 'E')
                    return {};
                exponentText = text.substr(close + 2u);
            }
        }
        else
        {
            auto const e = text.find_first_of("eE");
            if (e != std::string::npos)
            {
                digits = text.substr(0u, e);
                exponentText = text.substr(e + 1u);
            }
        }
        std::int64_t exponent = 0;
        if (!exponentText.empty())
        {
            bool negative = false;
            std::size_t start = 0u;
            if (exponentText[0] == '+' || exponentText[0] == '-')
            {
                negative = exponentText[0] == '-';
                start = 1u;
            }
            auto const e = bigint::parse(exponentText.substr(start), 10u);
            if (!e || !e->fits_int64())
                return {};
            exponent = negative ? -e->to_int64() : e->to_int64();
        }
        std::string whole = digits;
        std::string fraction;
        auto const point = digits.find('.');
        if (point != std::string::npos)
        {
            whole = digits.substr(0u, point);
            fraction = digits.substr(point + 1u);
        }
        auto const mantissa = bigint::parse(whole + fraction, base);
        if (!mantissa)
            return {};
        rational result{ *mantissa, bigint::pow(bigint{ static_cast<std::int64_t>(base) }, fraction.size()) };
        if (exponent != 0)
            result = result * pow(rational{ bigint{ static_cast<std::int64_t>(base) } }, exponent);
        return result;
    }

    double rational::to_double() const
    {
        // (correctly rounded enough: scale so that the quotient has 64 significant bits)
        if (iNumerator.is_zero())
            return 0.0;
        auto const nb = static_cast<long>(iNumerator.bits());
        auto const db = static_cast<long>(iDenominator.bits());
        long shift = 64 - (nb - db);
        bigint n = iNumerator.abs();
        bigint d = iDenominator;
        if (shift > 0)
            n = n * bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(shift));
        else if (shift < 0)
            d = d * bigint::pow(bigint{ 2 }, static_cast<std::uint64_t>(-shift));
        bigint q, r;
        bigint::divmod(n, d, q, r);
        // (sticky bit)
        double result = q.to_double();
        if (!r.is_zero() && q.bits() <= 64u)
            result = std::nextafter(result, result * 2.0) == result ? result : result;
        result = std::ldexp(result, static_cast<int>(-shift));
        return iNumerator.negative() ? -result : result;
    }

    bigint rational::truncate() const
    {
        return iNumerator / iDenominator;
    }

    bigint rational::floor() const
    {
        auto q = iNumerator / iDenominator;
        if (iNumerator.negative() && !(iNumerator % iDenominator).is_zero())
            q = q - bigint{ 1 };
        return q;
    }

    bigint rational::round() const
    {
        // (half away from zero)
        auto const twice = rational{ iNumerator.abs() * bigint{ 2 } + iDenominator, iDenominator * bigint{ 2 } };
        auto const magnitude = twice.floor();
        return iNumerator.negative() ? -magnitude : magnitude;
    }

    std::string rational::to_string() const
    {
        if (is_integer())
            return iNumerator.to_string();
        return iNumerator.to_string() + "/" + iDenominator.to_string();
    }

    rational operator+(rational const& a, rational const& b)
    {
        return rational{ a.iNumerator * b.iDenominator + b.iNumerator * a.iDenominator, a.iDenominator * b.iDenominator };
    }

    rational operator-(rational const& a, rational const& b)
    {
        return rational{ a.iNumerator * b.iDenominator - b.iNumerator * a.iDenominator, a.iDenominator * b.iDenominator };
    }

    rational operator*(rational const& a, rational const& b)
    {
        return rational{ a.iNumerator * b.iNumerator, a.iDenominator * b.iDenominator };
    }

    rational operator/(rational const& a, rational const& b)
    {
        return rational{ a.iNumerator * b.iDenominator, a.iDenominator * b.iNumerator };
    }

    rational operator-(rational const& a)
    {
        return rational{ -a.iNumerator, a.iDenominator };
    }

    rational rational::pow(rational const& a, std::int64_t aExponent)
    {
        if (aExponent >= 0)
            return rational{ bigint::pow(a.iNumerator, static_cast<std::uint64_t>(aExponent)), bigint::pow(a.iDenominator, static_cast<std::uint64_t>(aExponent)) };
        return rational{ bigint::pow(a.iDenominator, static_cast<std::uint64_t>(-aExponent)), bigint::pow(a.iNumerator, static_cast<std::uint64_t>(-aExponent)) };
    }

    int rational::compare(rational const& a, rational const& b)
    {
        return bigint::compare(a.iNumerator * b.iDenominator, b.iNumerator * a.iDenominator);
    }
}
