/*
  bigint.cpp

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

// BigInts: arbitrary-precision integers (a sign and a magnitude of 32-bit limbs, little endian) and the BigInt built-ins.
//
// The arithmetic works on magnitudes (vectors of limbs) and makes a heap cell only for a result; values of one limb take
// fast paths. Division is Knuth's algorithm D (The Art of Computer Programming, vol. 2, 4.3.1); bitwise operations use the
// two's complement representation (of a width that holds both operands) of negative values.

#include <algorithm>
#include <charconv>
#include <neos/language/ecmascript.hpp>
#include "js.hpp"
#include "intl.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace es = neos::ecmascript;

    namespace
    {
        using limb = std::uint32_t;
        using limbs = std::vector<limb>;

        constexpr std::uint64_t MaxBits = 1ull << 30;   ///< (the size of the largest BigInt (bits): a RangeError beyond)

        [[noreturn]] void too_big()
        {
            throw_range_error("Maximum BigInt size exceeded");
        }

        void check_bits(std::uint64_t aBits)
        {
            if (aBits > MaxBits)
                too_big();
        }

        bigint const& big(value aValue)
        {
            return cell_of<bigint>(aValue);
        }

        void normalize(limbs& aMagnitude)
        {
            while (!aMagnitude.empty() && aMagnitude.back() == 0u)
                aMagnitude.pop_back();
        }

        value make(bool aNegative, limbs aMagnitude)
        {
            normalize(aMagnitude);
            auto& result = the_heap().make<bigint>();
            result.negative = aNegative && !aMagnitude.empty();
            result.magnitude = std::move(aMagnitude);
            return value_of(result);
        }

        value make_u64(bool aNegative, std::uint64_t aMagnitude)
        {
            auto& result = the_heap().make<bigint>();
            if (aMagnitude != 0u)
            {
                result.negative = aNegative;
                result.magnitude.push_back(static_cast<limb>(aMagnitude));
                if ((aMagnitude >> 32) != 0u)
                    result.magnitude.push_back(static_cast<limb>(aMagnitude >> 32));
            }
            return value_of(result);
        }

        value make_i64(std::int64_t aValue)
        {
            return aValue < 0 ? make_u64(true, 0u - static_cast<std::uint64_t>(aValue)) : make_u64(false, static_cast<std::uint64_t>(aValue));
        }

        // (a value of at most one limb: its value)
        bool small(bigint const& aValue, std::int64_t& aResult)
        {
            if (aValue.magnitude.size() > 1u)
                return false;
            std::int64_t const m = aValue.magnitude.empty() ? 0 : aValue.magnitude[0];
            aResult = aValue.negative ? -m : m;
            return true;
        }

        std::uint64_t low64(limbs const& aMagnitude)
        {
            std::uint64_t result = aMagnitude.empty() ? 0u : aMagnitude[0];
            if (aMagnitude.size() > 1u)
                result |= static_cast<std::uint64_t>(aMagnitude[1]) << 32;
            return result;
        }

        std::uint64_t bit_length(limbs const& aMagnitude)
        {
            if (aMagnitude.empty())
                return 0u;
            return (aMagnitude.size() - 1u) * 32u + (32u - static_cast<std::uint64_t>(std::countl_zero(aMagnitude.back())));
        }

        // --- magnitudes

        int compare(limbs const& aLhs, limbs const& aRhs)
        {
            if (aLhs.size() != aRhs.size())
                return aLhs.size() < aRhs.size() ? -1 : 1;
            for (auto i = aLhs.size(); i-- > 0u;)
                if (aLhs[i] != aRhs[i])
                    return aLhs[i] < aRhs[i] ? -1 : 1;
            return 0;
        }

        limbs add(limbs const& aLhs, limbs const& aRhs)
        {
            auto const& x = aLhs.size() >= aRhs.size() ? aLhs : aRhs;
            auto const& y = aLhs.size() >= aRhs.size() ? aRhs : aLhs;
            limbs result(x.size() + 1u);
            std::uint64_t carry = 0u;
            for (std::size_t i = 0u; i < x.size(); ++i)
            {
                carry += static_cast<std::uint64_t>(x[i]) + (i < y.size() ? y[i] : 0u);
                result[i] = static_cast<limb>(carry);
                carry >>= 32;
            }
            result[x.size()] = static_cast<limb>(carry);
            normalize(result);
            return result;
        }

        // (aLhs >= aRhs)
        limbs subtract(limbs const& aLhs, limbs const& aRhs)
        {
            limbs result(aLhs.size());
            std::uint64_t borrow = 0u;
            for (std::size_t i = 0u; i < aLhs.size(); ++i)
            {
                std::uint64_t const subtrahend = (i < aRhs.size() ? aRhs[i] : 0u) + borrow;
                std::uint64_t const minuend = aLhs[i];
                result[i] = static_cast<limb>(minuend - subtrahend);
                borrow = minuend < subtrahend ? 1u : 0u;
            }
            normalize(result);
            return result;
        }

        void increment(limbs& aMagnitude)
        {
            for (auto& l : aMagnitude)
                if (++l != 0u)
                    return;
            aMagnitude.push_back(1u);
        }

        limbs multiply(limbs const& aLhs, limbs const& aRhs)
        {
            if (aLhs.empty() || aRhs.empty())
                return {};
            check_bits(bit_length(aLhs) + bit_length(aRhs) - 1u);
            limbs result(aLhs.size() + aRhs.size());
            for (std::size_t i = 0u; i < aLhs.size(); ++i)
            {
                std::uint64_t const a = aLhs[i];
                if (a == 0u)
                    continue;
                std::uint64_t carry = 0u;
                for (std::size_t j = 0u; j < aRhs.size(); ++j)
                {
                    carry += a * aRhs[j] + result[i + j];
                    result[i + j] = static_cast<limb>(carry);
                    carry >>= 32;
                }
                result[i + aRhs.size()] = static_cast<limb>(carry);
            }
            normalize(result);
            return result;
        }

        // (aMagnitude = aMagnitude * aFactor + aAddend)
        void multiply_add(limbs& aMagnitude, limb aFactor, limb aAddend)
        {
            std::uint64_t carry = aAddend;
            for (auto& l : aMagnitude)
            {
                carry += static_cast<std::uint64_t>(l) * aFactor;
                l = static_cast<limb>(carry);
                carry >>= 32;
            }
            if (carry != 0u)
                aMagnitude.push_back(static_cast<limb>(carry));
        }

        // (aMagnitude = aMagnitude / aDivisor: the remainder)
        limb divide_small(limbs& aMagnitude, limb aDivisor)
        {
            std::uint64_t remainder = 0u;
            for (auto i = aMagnitude.size(); i-- > 0u;)
            {
                std::uint64_t const current = (remainder << 32) | aMagnitude[i];
                aMagnitude[i] = static_cast<limb>(current / aDivisor);
                remainder = current % aDivisor;
            }
            normalize(aMagnitude);
            return static_cast<limb>(remainder);
        }

        limbs shift_left(limbs const& aMagnitude, std::uint64_t aBits)
        {
            if (aMagnitude.empty())
                return {};
            auto const whole = static_cast<std::size_t>(aBits / 32u);
            auto const part = static_cast<unsigned>(aBits % 32u);
            limbs result(whole + aMagnitude.size() + 1u);
            for (std::size_t i = 0u; i < aMagnitude.size(); ++i)
            {
                auto const shifted = static_cast<std::uint64_t>(aMagnitude[i]) << part;
                result[whole + i] |= static_cast<limb>(shifted);
                result[whole + i + 1u] = static_cast<limb>(shifted >> 32);
            }
            normalize(result);
            return result;
        }

        // (aLost: bits that were shifted out were not all zero)
        limbs shift_right(limbs const& aMagnitude, std::uint64_t aBits, bool& aLost)
        {
            auto const whole = aBits / 32u;
            auto const part = static_cast<unsigned>(aBits % 32u);
            aLost = false;
            if (whole >= aMagnitude.size())
            {
                aLost = !aMagnitude.empty();
                return {};
            }
            for (std::size_t i = 0u; i < whole; ++i)
                aLost = aLost || aMagnitude[i] != 0u;
            if (part != 0u && (aMagnitude[whole] & ((limb{ 1u } << part) - 1u)) != 0u)
                aLost = true;
            limbs result(aMagnitude.size() - whole);
            for (std::size_t i = 0u; i < result.size(); ++i)
            {
                std::uint64_t pair = aMagnitude[whole + i];
                if (whole + i + 1u < aMagnitude.size())
                    pair |= static_cast<std::uint64_t>(aMagnitude[whole + i + 1u]) << 32;
                result[i] = static_cast<limb>(pair >> part);
            }
            normalize(result);
            return result;
        }

        // (aDivisor non-zero; Knuth's algorithm D for divisors of more than one limb)
        void divide(limbs const& aDividend, limbs const& aDivisor, limbs* aQuotient, limbs* aRemainder)
        {
            if (compare(aDividend, aDivisor) < 0)
            {
                if (aQuotient)
                    aQuotient->clear();
                if (aRemainder)
                    *aRemainder = aDividend;
                return;
            }
            if (aDivisor.size() == 1u)
            {
                limbs q = aDividend;
                auto const r = divide_small(q, aDivisor[0]);
                if (aQuotient)
                    *aQuotient = std::move(q);
                if (aRemainder)
                {
                    aRemainder->clear();
                    if (r != 0u)
                        aRemainder->push_back(r);
                }
                return;
            }
            std::size_t const n = aDivisor.size();
            std::size_t const m = aDividend.size() - n;
            // D1: normalize (the divisor's most significant limb gets its top bit set)
            auto const s = static_cast<unsigned>(std::countl_zero(aDivisor.back()));
            thread_local limbs tV, tU;  // (scratch kept for reuse (a division is made per BigInt / and %))
            tV.assign(n, 0u);
            tU.assign(aDividend.size() + 1u, 0u);
            for (std::size_t i = n; i-- > 0u;)
                tV[i] = static_cast<limb>((static_cast<std::uint64_t>(aDivisor[i]) << s) | (s != 0u && i > 0u ? aDivisor[i - 1u] >> (32u - s) : 0u));
            tU[aDividend.size()] = s != 0u ? aDividend.back() >> (32u - s) : 0u;
            for (std::size_t i = aDividend.size(); i-- > 0u;)
                tU[i] = static_cast<limb>((static_cast<std::uint64_t>(aDividend[i]) << s) | (s != 0u && i > 0u ? aDividend[i - 1u] >> (32u - s) : 0u));
            limbs q(m + 1u);
            std::uint64_t const vTop = tV[n - 1u];
            std::uint64_t const vNext = tV[n - 2u];
            for (std::size_t j = m + 1u; j-- > 0u;)
            {
                // D3: estimate the quotient digit
                std::uint64_t const numerator = (static_cast<std::uint64_t>(tU[j + n]) << 32) | tU[j + n - 1u];
                std::uint64_t qhat = numerator / vTop;
                std::uint64_t rhat = numerator % vTop;
                while (qhat > 0xFFFFFFFFull || qhat * vNext > ((rhat << 32) | tU[j + n - 2u]))
                {
                    --qhat;
                    rhat += vTop;
                    if (rhat > 0xFFFFFFFFull)
                        break;
                }
                // D4: multiply and subtract
                std::uint64_t carry = 0u;
                std::uint64_t borrow = 0u;
                for (std::size_t i = 0u; i < n; ++i)
                {
                    std::uint64_t const product = qhat * tV[i] + carry;
                    carry = product >> 32;
                    std::uint64_t const subtrahend = (product & 0xFFFFFFFFull) + borrow;
                    std::uint64_t const minuend = tU[i + j];
                    tU[i + j] = static_cast<limb>(minuend - subtrahend);
                    borrow = minuend < subtrahend ? 1u : 0u;
                }
                std::uint64_t const subtrahend = carry + borrow;
                std::uint64_t const minuend = tU[j + n];
                tU[j + n] = static_cast<limb>(minuend - subtrahend);
                // D6: add back (the estimate was one too large)
                if (minuend < subtrahend)
                {
                    --qhat;
                    std::uint64_t sum = 0u;
                    for (std::size_t i = 0u; i < n; ++i)
                    {
                        sum += static_cast<std::uint64_t>(tU[i + j]) + tV[i];
                        tU[i + j] = static_cast<limb>(sum);
                        sum >>= 32;
                    }
                    tU[j + n] = static_cast<limb>(tU[j + n] + sum);
                }
                q[j] = static_cast<limb>(qhat);
            }
            if (aQuotient)
            {
                normalize(q);
                *aQuotient = std::move(q);
            }
            if (aRemainder)
            {
                // D8: unnormalize
                limbs r(n);
                for (std::size_t i = 0u; i < n; ++i)
                    r[i] = static_cast<limb>((tU[i] >> s) | (s != 0u ? static_cast<std::uint64_t>(tU[i + 1u]) << (32u - s) : 0u));
                normalize(r);
                *aRemainder = std::move(r);
            }
        }

        // (a non-negative integral double's magnitude)
        limbs from_double(double aValue)
        {
            if (aValue < 18446744073709551616.0)
            {
                limbs result;
                auto const n = static_cast<std::uint64_t>(aValue);
                if (n != 0u)
                    result.push_back(static_cast<limb>(n));
                if ((n >> 32) != 0u)
                    result.push_back(static_cast<limb>(n >> 32));
                return result;
            }
            int exponent = 0;
            auto const fraction = std::frexp(aValue, &exponent);
            auto const mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
            return shift_left({ static_cast<limb>(mantissa), static_cast<limb>(mantissa >> 32) }, static_cast<std::uint64_t>(exponent - 53));
        }

        // (aValue's two's complement in aCount limbs)
        limbs twos_complement(bigint const& aValue, std::size_t aCount)
        {
            limbs result(aCount, 0u);
            std::copy_n(aValue.magnitude.begin(), std::min(aCount, aValue.magnitude.size()), result.begin());
            if (aValue.negative)
            {
                for (auto& l : result)
                    l = ~l;
                for (auto& l : result)
                    if (++l != 0u)
                        break;
            }
            return result;
        }

        // (a two's complement representation's value)
        value from_twos_complement(limbs aBits)
        {
            if (aBits.empty() || (aBits.back() & 0x80000000u) == 0u)
                return make(false, std::move(aBits));
            for (auto& l : aBits)
                l = ~l;
            increment(aBits);
            return make(true, std::move(aBits));
        }

        // (aBits limbs: the value's limbs truncated to its low aBits bits)
        void truncate(limbs& aMagnitude, std::uint64_t aBits)
        {
            auto const count = static_cast<std::size_t>((aBits + 31u) / 32u);
            if (aMagnitude.size() > count)
                aMagnitude.resize(count);
            if (aMagnitude.size() == count && aBits % 32u != 0u)
                aMagnitude.back() &= (limb{ 1u } << (aBits % 32u)) - 1u;
            normalize(aMagnitude);
        }

        // --- digits

        // (the number of digits of a radix in a chunk (of at most 32 bits) and the chunk's radix)
        std::pair<int, limb> chunk(int aRadix)
        {
            int digits = 0;
            std::uint64_t power = 1u;
            while (power * static_cast<std::uint64_t>(aRadix) <= 0xFFFFFFFFull)
            {
                power *= static_cast<std::uint64_t>(aRadix);
                ++digits;
            }
            return { digits, static_cast<limb>(power) };
        }

        template <typename Char>
        std::optional<limbs> parse_digits(std::basic_string_view<Char> aDigits, int aRadix)
        {
            if (aDigits.empty())
                return {};
            auto const [chunkDigits, chunkRadix] = chunk(aRadix);
            limbs result;
            std::size_t i = 0u;
            while (i < aDigits.size())
            {
                // (the first chunk is partial so that the rest are whole)
                auto const count = i == 0u ? (aDigits.size() - 1u) % static_cast<std::size_t>(chunkDigits) + 1u : static_cast<std::size_t>(chunkDigits);
                limb part = 0u;
                limb factor = 1u;
                for (std::size_t k = 0u; k < count; ++k, ++i)
                {
                    auto const c = aDigits[i];
                    int const digit = static_cast<std::uint32_t>(c) < 0x80u ? es::detail::digit_value(static_cast<char16_t>(c)) : 99;
                    if (digit >= aRadix)
                        return {};
                    part = part * static_cast<limb>(aRadix) + static_cast<limb>(digit);
                    factor *= static_cast<limb>(aRadix);
                }
                multiply_add(result, count == static_cast<std::size_t>(chunkDigits) ? chunkRadix : factor, part);
            }
            normalize(result);
            return result;
        }

        int prefix_radix(char16_t aCharacter)
        {
            switch (aCharacter)
            {
            case u'x': case u'X': return 16;
            case u'o': case u'O': return 8;
            case u'b': case u'B': return 2;
            default: return 0;
            }
        }

        // --- the BigInt built-ins

        value this_bigint(value aThis)
        {
            if (is_bigint(aThis))
                return aThis;
            if (is_object(aThis) && obj(aThis).cls == object_class::BigInt)
                return obj(aThis).internal;
            throw_type_error("BigInt.prototype method called on incompatible receiver " + describe(aThis));
        }

        value bigint_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget != Undefined)
                throw_type_error("BigInt is not a constructor");
            auto const prim = to_primitive(argument(aArguments, 0u), hint::Number);
            if (is_number(prim))
                return number_to_bigint(as_number(prim));
            return to_bigint(prim);
        }

        value bigint_as_int_n_(value, arguments aArguments, value, function_object&)
        {
            auto const bits = to_index(argument(aArguments, 0u));
            auto const n = to_bigint(argument(aArguments, 1u));
            return bigint_as_int_n(static_cast<std::uint64_t>(bits), n);
        }

        value bigint_as_uint_n_(value, arguments aArguments, value, function_object&)
        {
            auto const bits = to_index(argument(aArguments, 0u));
            auto const n = to_bigint(argument(aArguments, 1u));
            return bigint_as_uint_n(static_cast<std::uint64_t>(bits), n);
        }

        value bigint_prototype_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const x = this_bigint(aThis);
            auto const radixArgument = argument(aArguments, 0u);
            int radix = 10;
            if (radixArgument != Undefined)
            {
                auto const r = to_integer_or_infinity(radixArgument);
                if (r < 2.0 || r > 36.0)
                    throw_range_error("toString() radix must be between 2 and 36");
                radix = static_cast<int>(r);
            }
            return make_string(bigint_to_string(x, radix));
        }

        value bigint_prototype_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const x = this_bigint(aThis);
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
                return intl::number_to_locale_string(x, argument(aArguments, 0u), argument(aArguments, 1u));
#endif
            (void)aArguments;
            return make_string(bigint_to_string(x));
        }

        value bigint_prototype_value_of(value aThis, arguments, value, function_object&)
        {
            return this_bigint(aThis);
        }
    }

    // --- making BigInts

    value make_bigint(std::int64_t aValue)
    {
        return make_i64(aValue);
    }

    value make_bigint_u64(std::uint64_t aValue)
    {
        return make_u64(false, aValue);
    }

    value number_to_bigint(double aValue)
    {
        if (!std::isfinite(aValue) || std::trunc(aValue) != aValue)
            throw_range_error("The number " + utf8(number_to_string(aValue)) + " cannot be converted to a BigInt because it is not an integer");
        if (std::fabs(aValue) < 9223372036854775808.0)
            return make_i64(static_cast<std::int64_t>(aValue));
        return make(aValue < 0.0, from_double(std::fabs(aValue)));
    }

    std::optional<value> string_to_bigint(std::u16string_view aText)
    {
        auto text = es::trim(aText);
        if (text.empty())
            return make_u64(false, 0u);
        int radix = 10;
        bool negative = false;
        if (text.size() > 2u && text[0] == u'0' && prefix_radix(text[1]) != 0)
        {
            radix = prefix_radix(text[1]);
            text.remove_prefix(2u);
        }
        else if (text[0] == u'+' || text[0] == u'-')
        {
            negative = text[0] == u'-';
            text.remove_prefix(1u);
        }
        auto magnitude = parse_digits(text, radix);
        if (!magnitude)
            return {};
        return make(negative, std::move(*magnitude));
    }

    value parse_bigint_literal(std::string_view aText)
    {
        std::string digits;
        digits.reserve(aText.size());
        for (auto c : aText)
            if (c != '_')
                digits.push_back(c);
        std::string_view text = digits;
        int radix = 10;
        if (text.size() > 2u && text[0] == '0' && prefix_radix(static_cast<char16_t>(text[1])) != 0)
        {
            radix = prefix_radix(static_cast<char16_t>(text[1]));
            text.remove_prefix(2u);
        }
        auto magnitude = parse_digits(text, radix);
        if (!magnitude)
            throw_syntax_error("Invalid BigInt literal " + std::string{ aText } + "n");
        return make(false, std::move(*magnitude));
    }

    // --- conversions

    std::u16string bigint_to_string(value aBigInt, int aRadix)
    {
        auto const& b = big(aBigInt);
        if (b.magnitude.size() <= 2u)
        {
            char buffer[72];
            char* first = buffer;
            if (b.negative)
                *first++ = '-';
            auto const end = std::to_chars(first, buffer + sizeof(buffer), low64(b.magnitude), aRadix).ptr;
            return std::u16string(buffer, end);
        }
        auto const [chunkDigits, chunkRadix] = chunk(aRadix);
        thread_local limbs tM;
        tM.assign(b.magnitude.begin(), b.magnitude.end());
        std::u16string reversed;
        reversed.reserve(static_cast<std::size_t>(bit_length(tM) / std::log2(aRadix)) + 2u);
        while (!tM.empty())
        {
            auto part = divide_small(tM, chunkRadix);
            for (int k = 0; k < chunkDigits && (part != 0u || !tM.empty()); ++k)
            {
                reversed.push_back(static_cast<char16_t>("0123456789abcdefghijklmnopqrstuvwxyz"[part % static_cast<limb>(aRadix)]));
                part /= static_cast<limb>(aRadix);
            }
        }
        if (b.negative)
            reversed.push_back(u'-');
        std::reverse(reversed.begin(), reversed.end());
        return reversed;
    }

    double bigint_to_number(value aBigInt)
    {
        auto const& b = big(aBigInt);
        auto const& m = b.magnitude;
        double result;
        if (m.size() <= 2u)
            result = static_cast<double>(low64(m));
        else
        {
            auto const length = bit_length(m);
            if (length > 1100u)
                result = std::numeric_limits<double>::infinity();
            else
            {
                // (the top 64 bits, with a sticky bit for the rest: the conversion to double rounds them correctly)
                auto const shift = length - 64u;
                auto const whole = static_cast<std::size_t>(shift / 32u);
                auto const part = static_cast<unsigned>(shift % 32u);
                // (the 96 bits from limb whole (lo: its two lowest limbs, hi: the third), shifted right by part (< 32): their low 64 bits)
                auto const limb_at = [&](std::size_t aIndex) -> std::uint64_t { return aIndex < m.size() ? m[aIndex] : 0u; };
                std::uint64_t const lo = limb_at(whole) | (limb_at(whole + 1u) << 32u);
                std::uint64_t const hi = limb_at(whole + 2u);
                auto top = part == 0u ? lo : (lo >> part) | (hi << (64u - part));
                bool sticky = part != 0u && (m[whole] & ((limb{ 1u } << part) - 1u)) != 0u;
                for (std::size_t i = 0u; !sticky && i < whole; ++i)
                    sticky = m[i] != 0u;
                if (sticky)
                    top |= 1u;
                result = std::ldexp(static_cast<double>(top), static_cast<int>(shift));
            }
        }
        return b.negative ? -result : result;
    }

    std::int64_t bigint_to_int64(value aBigInt)
    {
        return static_cast<std::int64_t>(bigint_to_uint64(aBigInt));
    }

    std::uint64_t bigint_to_uint64(value aBigInt)
    {
        auto const& b = big(aBigInt);
        auto const m = low64(b.magnitude);
        return b.negative ? 0u - m : m;
    }

    value to_bigint(value aValue)
    {
        auto const prim = to_primitive(aValue, hint::Number);
        if (is_bigint(prim))
            return prim;
        if (is_number(prim))
            throw_type_error("Cannot convert " + describe(prim) + " to a BigInt");
        switch (prim)
        {
        case True:
            return make_u64(false, 1u);
        case False:
            return make_u64(false, 0u);
        case Undefined:
        case Null:
            throw_type_error("Cannot convert " + describe(prim) + " to a BigInt");
        default:
            break;
        }
        if (is_string(prim))
        {
            auto const result = string_to_bigint(str(prim));
            if (!result)
                throw_syntax_error("Cannot convert " + describe(prim) + " to a BigInt");
            return *result;
        }
        throw_type_error("Cannot convert " + describe(prim) + " to a BigInt");
    }

    // --- comparison

    int bigint_compare(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        if (x.negative != y.negative)
            return x.negative ? -1 : 1;
        auto const c = compare(x.magnitude, y.magnitude);
        return x.negative ? -c : c;
    }

    int bigint_compare_number(value aLhs, double aRhs)
    {
        if (std::isnan(aRhs))
            return 2;
        if (std::isinf(aRhs))
            return aRhs > 0.0 ? -1 : 1;
        auto const& x = big(aLhs);
        std::int64_t s;
        if (small(x, s))
        {
            // (exact as a double)
            auto const d = static_cast<double>(s);
            return d < aRhs ? -1 : d > aRhs ? 1 : 0;
        }
        bool const negative = aRhs < 0.0;
        if (x.negative != negative)
            return x.negative ? -1 : 1;
        auto const magnitude = std::fabs(aRhs);
        auto const integral = std::floor(magnitude);
        auto c = compare(x.magnitude, from_double(integral));
        if (c == 0 && magnitude > integral)
            c = -1;
        return x.negative ? -c : c;
    }

    bool bigint_equal(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        return x.negative == y.negative && x.magnitude == y.magnitude;
    }

    bool bigint_is_zero(value aBigInt)
    {
        return big(aBigInt).magnitude.empty();
    }

    // --- arithmetic

    value bigint_add(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        std::int64_t a, b;
        if (small(x, a) && small(y, b))
            return make_i64(a + b);
        if (x.negative == y.negative)
            return make(x.negative, add(x.magnitude, y.magnitude));
        auto const c = compare(x.magnitude, y.magnitude);
        if (c >= 0)
            return make(x.negative, subtract(x.magnitude, y.magnitude));
        return make(y.negative, subtract(y.magnitude, x.magnitude));
    }

    value bigint_subtract(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        std::int64_t a, b;
        if (small(x, a) && small(y, b))
            return make_i64(a - b);
        if (x.negative != y.negative)
            return make(x.negative, add(x.magnitude, y.magnitude));
        auto const c = compare(x.magnitude, y.magnitude);
        if (c >= 0)
            return make(x.negative, subtract(x.magnitude, y.magnitude));
        return make(!x.negative, subtract(y.magnitude, x.magnitude));
    }

    value bigint_multiply(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        bool const negative = x.negative != y.negative;
        if (x.magnitude.size() <= 1u && y.magnitude.size() <= 1u)
            return make_u64(negative, low64(x.magnitude) * low64(y.magnitude));
        return make(negative, multiply(x.magnitude, y.magnitude));
    }

    value bigint_divide(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        if (y.magnitude.empty())
            throw_range_error("Division by zero");
        bool const negative = x.negative != y.negative;
        if (x.magnitude.size() <= 2u && y.magnitude.size() <= 2u)
            return make_u64(negative, low64(x.magnitude) / low64(y.magnitude));
        limbs quotient;
        divide(x.magnitude, y.magnitude, &quotient, nullptr);
        return make(negative, std::move(quotient));
    }

    value bigint_remainder(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        if (y.magnitude.empty())
            throw_range_error("Division by zero");
        if (x.magnitude.size() <= 2u && y.magnitude.size() <= 2u)
            return make_u64(x.negative, low64(x.magnitude) % low64(y.magnitude));
        limbs remainder;
        divide(x.magnitude, y.magnitude, nullptr, &remainder);
        return make(x.negative, std::move(remainder));
    }

    value bigint_power(value aLhs, value aRhs)
    {
        auto const& base = big(aLhs);
        auto const& exponent = big(aRhs);
        if (exponent.negative)
            throw_range_error("Exponent must be non-negative");
        if (exponent.magnitude.empty())
            return make_u64(false, 1u);
        if (base.magnitude.empty())
            return aLhs;
        bool const negative = base.negative && (exponent.magnitude[0] & 1u) != 0u;
        if (base.magnitude.size() == 1u && base.magnitude[0] == 1u)
            return make_u64(negative, 1u);
        if (exponent.magnitude.size() > 1u)
            too_big();
        std::uint64_t n = exponent.magnitude[0];
        auto const length = bit_length(base.magnitude);
        check_bits((length - 1u) * n);
        // (a power of two: a shift)
        if (base.magnitude.size() == 1u && std::has_single_bit(base.magnitude[0]))
            return make(negative, shift_left({ 1u }, (length - 1u) * n));
        if (length * n <= 64u)
        {
            std::uint64_t result = 1u;
            for (std::uint64_t const m = low64(base.magnitude); n != 0u; --n)
                result *= m;
            return make_u64(negative, result);
        }
        limbs result{ 1u };
        limbs square = base.magnitude;
        for (;;)
        {
            if ((n & 1u) != 0u)
                result = multiply(result, square);
            n >>= 1;
            if (n == 0u)
                break;
            square = multiply(square, square);
        }
        return make(negative, std::move(result));
    }

    value bigint_negate(value aBigInt)
    {
        auto const& x = big(aBigInt);
        if (x.magnitude.empty())
            return aBigInt;
        return make(!x.negative, x.magnitude);
    }

    value bigint_bitwise(int aOperation, value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        std::int64_t a, b;
        if (small(x, a) && small(y, b))
        {
            switch (aOperation)
            {
            case 0: return make_i64(a & b);
            case 1: return make_i64(a | b);
            default: return make_i64(a ^ b);
            }
        }
        auto const count = std::max(x.magnitude.size(), y.magnitude.size()) + 1u;
        auto result = twos_complement(x, count);
        auto const other = twos_complement(y, count);
        for (std::size_t i = 0u; i < count; ++i)
        {
            switch (aOperation)
            {
            case 0: result[i] &= other[i]; break;
            case 1: result[i] |= other[i]; break;
            default: result[i] ^= other[i]; break;
            }
        }
        return from_twos_complement(std::move(result));
    }

    value bigint_not(value aBigInt)
    {
        // (~x is -x - 1)
        auto const& x = big(aBigInt);
        std::int64_t a;
        if (small(x, a))
            return make_i64(-a - 1);
        if (x.negative)
            return make(false, subtract(x.magnitude, { 1u }));
        auto magnitude = x.magnitude;
        increment(magnitude);
        return make(true, std::move(magnitude));
    }

    value bigint_shift_left(value aLhs, value aRhs)
    {
        auto const& x = big(aLhs);
        auto const& y = big(aRhs);
        if (x.magnitude.empty() || y.magnitude.empty())
            return aLhs;
        bool const right = y.negative;
        auto const length = bit_length(x.magnitude);
        if (y.magnitude.size() > 2u)
        {
            if (!right)
                too_big();
            return make_u64(x.negative, x.negative ? 1u : 0u);
        }
        auto const n = low64(y.magnitude);
        if (!right)
        {
            if (n >= MaxBits)
                too_big();
            check_bits(length + n);
            if (length + n <= 63u)
                return make_u64(x.negative, low64(x.magnitude) << n);
            return make(x.negative, shift_left(x.magnitude, n));
        }
        // (a right shift rounds towards -infinity)
        if (n >= length)
            return make_u64(x.negative, x.negative ? 1u : 0u);
        std::int64_t a;
        if (small(x, a))
            return make_i64(a >> n);
        bool lost = false;
        auto magnitude = shift_right(x.magnitude, n, lost);
        if (x.negative && lost)
            increment(magnitude);
        return make(x.negative, std::move(magnitude));
    }

    value bigint_as_int_n(std::uint64_t aBits, value aBigInt)
    {
        auto const& x = big(aBigInt);
        if (aBits == 0u)
            return make_u64(false, 0u);
        // (-2^(bits-1) <= x < 2^(bits-1): itself)
        if (aBits > bit_length(x.magnitude))
            return aBigInt;
        auto const count = static_cast<std::size_t>((aBits + 31u) / 32u);
        auto bits = twos_complement(x, count);
        truncate(bits, aBits);
        auto const top = aBits - 1u;
        if (bits.size() <= top / 32u || (bits[top / 32u] & (limb{ 1u } << (top % 32u))) == 0u)
            return make(false, std::move(bits));
        // (bits - 2^bits: the negation of its two's complement in aBits bits)
        bits.resize(count, 0u);
        for (auto& l : bits)
            l = ~l;
        increment(bits);
        truncate(bits, aBits);
        return make(true, std::move(bits));
    }

    value bigint_as_uint_n(std::uint64_t aBits, value aBigInt)
    {
        auto const& x = big(aBigInt);
        if (aBits == 0u)
            return make_u64(false, 0u);
        if (!x.negative)
        {
            if (aBits >= bit_length(x.magnitude))
                return aBigInt;
            auto magnitude = x.magnitude;
            truncate(magnitude, aBits);
            return make(false, std::move(magnitude));
        }
        check_bits(aBits);
        auto bits = twos_complement(x, static_cast<std::size_t>((aBits + 31u) / 32u));
        truncate(bits, aBits);
        return make(false, std::move(bits));
    }

    // --- the BigInt built-ins

    void init_bigint(realm& aRealm)
    {
        auto const c = make_native("BigInt", 1u, bigint_constructor, true);
        aRealm.bigint = c;
        aRealm.bigintPrototype = make_object();
        auto const p = aRealm.bigintPrototype;
        obj(c).put(the_names().prototype, p, 0u);
        obj(p).put(the_names().constructor, c);
        method(c, "asIntN", 2u, bigint_as_int_n_);
        method(c, "asUintN", 2u, bigint_as_uint_n_);
        method(p, "toLocaleString", 0u, bigint_prototype_to_locale_string);
        method(p, "toString", 0u, bigint_prototype_to_string);
        method(p, "valueOf", 0u, bigint_prototype_value_of);
        obj(p).put(aRealm.symToStringTag, ascii("BigInt"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("BigInt"), c);
    }
}
