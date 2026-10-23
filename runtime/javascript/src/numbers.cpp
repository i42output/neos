/*
  numbers.cpp

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

// Number formatting: toFixed, toExponential and toPrecision (from the exact decimal expansion of a double, rounded half up
// as ECMAScript requires)

#include <algorithm>
#include <charconv>
#include "js.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // the exact decimal value of a finite non-negative double: its digits (no leading zeros) and the position of the decimal
        // point (the value is 0.digits * 10^point)
        struct decimal
        {
            std::string digits;
            int point = 0;
        };

        decimal exact(double aValue)
        {
            decimal result;
            if (aValue == 0.0)
            {
                result.digits = "0";
                result.point = 1;
                return result;
            }
            // (mantissa * 2^exponent)
            int exponent;
            double const fraction = std::frexp(aValue, &exponent);
            auto mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
            exponent -= 53;
            while ((mantissa & 1u) == 0u && mantissa != 0u)
            {
                mantissa >>= 1;
                ++exponent;
            }
            // (a big decimal number (base 10^9 limbs, little endian))
            thread_local std::vector<std::uint32_t> tLimbs;
            tLimbs.clear();
            for (auto m = mantissa; m != 0u; m /= 1000000000u)
                tLimbs.push_back(static_cast<std::uint32_t>(m % 1000000000u));
            auto const multiply = [&](std::uint32_t aFactor)
            {
                std::uint64_t carry = 0u;
                for (auto& l : tLimbs)
                {
                    auto const v = static_cast<std::uint64_t>(l) * aFactor + carry;
                    l = static_cast<std::uint32_t>(v % 1000000000u);
                    carry = v / 1000000000u;
                }
                while (carry != 0u)
                {
                    tLimbs.push_back(static_cast<std::uint32_t>(carry % 1000000000u));
                    carry /= 1000000000u;
                }
            };
            int decimalExponent = 0;
            if (exponent > 0)
                for (int i = 0; i < exponent; ++i)
                    multiply(2u);
            else
                for (int i = 0; i < -exponent; ++i)
                {
                    // (x / 2 = x * 5 / 10)
                    multiply(5u);
                    --decimalExponent;
                }
            std::string digits;
            for (auto l = tLimbs.rbegin(); l != tLimbs.rend(); ++l)
            {
                char buffer[16];
                if (l == tLimbs.rbegin())
                    digits += std::string{ buffer, static_cast<std::size_t>(std::to_chars(buffer, buffer + sizeof(buffer), *l).ptr - buffer) };
                else
                {
                    std::snprintf(buffer, sizeof(buffer), "%09u", static_cast<unsigned>(*l));
                    digits += buffer;
                }
            }
            // (value = digits * 10^decimalExponent)
            auto const trailing = digits.find_last_not_of('0');
            int const zeros = static_cast<int>(digits.size() - 1u - trailing);
            digits.erase(trailing + 1u);
            decimalExponent += zeros;
            result.point = static_cast<int>(digits.size()) + decimalExponent;
            result.digits = digits;
            return result;
        }

        // (aDigits rounded (half up) to aCount significant digits: the point may move)
        decimal round_significant(decimal aValue, int aCount)
        {
            if (static_cast<int>(aValue.digits.size()) <= aCount)
            {
                aValue.digits.append(static_cast<std::size_t>(aCount) - aValue.digits.size(), '0');
                return aValue;
            }
            bool const up = aValue.digits[static_cast<std::size_t>(aCount)] >= '5';
            aValue.digits.erase(static_cast<std::size_t>(aCount));
            if (up)
            {
                int i = aCount - 1;
                for (; i >= 0; --i)
                {
                    if (aValue.digits[static_cast<std::size_t>(i)] == '9')
                        aValue.digits[static_cast<std::size_t>(i)] = '0';
                    else
                    {
                        ++aValue.digits[static_cast<std::size_t>(i)];
                        break;
                    }
                }
                if (i < 0)
                {
                    aValue.digits.insert(aValue.digits.begin(), '1');
                    aValue.digits.pop_back();
                    ++aValue.point;
                }
            }
            return aValue;
        }
    }

    std::string number_to_fixed(double aValue, int aDigits)
    {
        if (std::fabs(aValue) >= 1e21)
            return utf8(number_to_string(aValue));
        auto const d = exact(std::fabs(aValue));
        auto const digit = [&](int aIndex) { return aIndex >= 0 && aIndex < static_cast<int>(d.digits.size()) ? d.digits[static_cast<std::size_t>(aIndex)] : '0'; };
        // (the integer's digits and aDigits of the fraction, rounded half up by the digit that follows them)
        std::string all;
        int const integerDigits = std::max(d.point, 1);
        for (int i = d.point - integerDigits; i < d.point + aDigits; ++i)
            all.push_back(digit(i));
        if (digit(d.point + aDigits) >= '5')
        {
            int i = static_cast<int>(all.size()) - 1;
            for (; i >= 0; --i)
            {
                if (all[static_cast<std::size_t>(i)] == '9')
                    all[static_cast<std::size_t>(i)] = '0';
                else
                {
                    ++all[static_cast<std::size_t>(i)];
                    break;
                }
            }
            if (i < 0)
                all.insert(all.begin(), '1');
        }
        auto const split = all.size() - static_cast<std::size_t>(aDigits);
        std::string result = all.substr(0u, split);
        if (aDigits > 0)
            result += "." + all.substr(split);
        return (aValue < 0.0 ? "-" : "") + result;
    }

    std::string number_to_exponential(double aValue, std::optional<int> aDigits)
    {
        bool const negative = aValue < 0.0;
        double const x = std::fabs(aValue);
        decimal d;
        if (x == 0.0)
        {
            d.digits = std::string(static_cast<std::size_t>(aDigits.value_or(0) + 1), '0');
            d.point = 1;
        }
        else if (!aDigits)
        {
            // (as few digits as identify the number: those of ToString)
            auto const shortest = utf8(number_to_string(x));
            std::string digits;
            int point = 0;
            auto const e = shortest.find('e');
            auto const mantissa = shortest.substr(0u, e);
            auto const dot = mantissa.find('.');
            digits = mantissa;
            digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
            point = static_cast<int>(dot == std::string::npos ? mantissa.size() : dot);
            if (e != std::string::npos)
                point += std::stoi(shortest.substr(e + 1u));
            auto const first = digits.find_first_not_of('0');
            point -= static_cast<int>(first);
            digits = digits.substr(first);
            auto const last = digits.find_last_not_of('0');
            digits.erase(last + 1u);
            d.digits = digits;
            d.point = point;
        }
        else
            d = round_significant(exact(x), *aDigits + 1);
        int const e = d.point - 1;
        std::string result = d.digits.substr(0u, 1u);
        if (d.digits.size() > 1u)
            result += "." + d.digits.substr(1u);
        result += e >= 0 ? "e+" + std::to_string(e) : "e-" + std::to_string(-e);
        return (negative ? "-" : "") + result;
    }

    std::string number_to_precision(double aValue, int aDigits)
    {
        bool const negative = aValue < 0.0;
        double const x = std::fabs(aValue);
        decimal d;
        int e = 0;
        if (x == 0.0)
        {
            d.digits = std::string(static_cast<std::size_t>(aDigits), '0');
            d.point = 1;
        }
        else
            d = round_significant(exact(x), aDigits);
        e = d.point - 1;
        std::string result;
        if (x != 0.0 && (e < -6 || e >= aDigits))
        {
            result = d.digits.substr(0u, 1u);
            if (aDigits > 1)
                result += "." + d.digits.substr(1u);
            result += e >= 0 ? "e+" + std::to_string(e) : "e-" + std::to_string(-e);
        }
        else if (e == aDigits - 1)
            result = d.digits;
        else if (e >= 0)
            result = d.digits.substr(0u, static_cast<std::size_t>(e + 1)) + "." + d.digits.substr(static_cast<std::size_t>(e + 1));
        else
            result = "0." + std::string(static_cast<std::size_t>(-(e + 1)), '0') + d.digits;
        return (negative ? "-" : "") + result;
    }
}
